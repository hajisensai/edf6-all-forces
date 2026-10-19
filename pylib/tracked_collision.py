"""Per-vehicle V603 contact shapes and road-wheel contracts, built read-only.

Preserve the native body/joint/skeleton topology, but fit each collision assembly
in its own proxy frame to the authored model. Flat-convex CMS extern routing is
kept; vertices, planes, hierarchy bounds and mass caches are rebuilt together.
The mass cache uses the source CMS wrapper's conservative solid-AABB model
(1.5 times uniform-box inertia), rather than retaining the donor vehicle inertia.
"""
from __future__ import annotations

import copy
import math
import struct

import numpy as np

import dsgo
import graft_pure as g
import hkcms
import hkcms_build as hb
from hktag import Tag, sections
from mdb import bind_world
import ragdoll_fit as rf
import sgo
import shkt_check as sc

HOST = 'Ragdoll_v603_flak.shkt'
KINDS = ('vulcan', 'bmpt')
IDENTITY_MASS_QUAT = bytes.fromhex('00800080008030f5')


class CollisionError(ValueError):
    pass


def _req(ok, message):
    if not ok:
        raise CollisionError(message)


def _bbox(points):
    p = np.asarray(points, dtype=float)
    _req(p.ndim == 2 and p.shape[1] == 3 and len(p) > 0, 'empty point set')
    return np.asarray((p.min(0), p.max(0)))


def _pack_vec(v):
    """Native hkPackedVector3 bit-exact encoder (EDF.dll 0x1305DE0)."""
    v=np.asarray(v,dtype=np.float32)
    _req(v.shape==(3,) and bool(np.isfinite(v).all()),'finite vec3 required')
    def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
    def from_bits(x):return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]
    guarded=np.asarray([*v,2.**-46],dtype=np.float32)
    guarded*=np.float32(from_bits(0x3f800089))
    exponent=int((guarded.view(np.uint32)&np.uint32(0x7f800000)).max())
    words=[]
    for x in v:
        scaled=from_bits(bits(x)+0x4e800000-exponent)
        n=2147483647 if scaled>=2147483648 else int(scaled)
        words.append(((n+0x8000)>>16)&0xffff)
    words.append(((exponent-0x4e800000+0x3f800000)>>16)&0xffff)
    return struct.pack('<4H',*words)


def _unpack_vec(blob, offset=0):
    x, y, z, exponent = struct.unpack_from('<3hH', blob, offset)
    scale = struct.unpack('<f', struct.pack('<I', exponent << 16))[0] * 65536.
    return np.asarray((x, y, z), dtype=float) * scale


def _mass_cache(box, mass):
    """The same conservative AABB mass model measured on the stock CMS wrappers."""
    size = box[1] - box[0]
    _req(bool((size > 1e-6).all()) and mass > 0, 'degenerate mass box')
    inertia = mass / 8. * np.asarray((size[1]**2 + size[2]**2,
                                     size[0]**2 + size[2]**2,
                                     size[0]**2 + size[1]**2))
    return (_pack_vec(box.mean(0)) + _pack_vec(inertia) + IDENTITY_MASS_QUAT +
            struct.pack('<2f', mass, float(np.prod(size))))


class _Editor:
    """Append same-TYPE items; original indices and original opaque records stay intact."""
    def __init__(self, raw):
        self.tag = Tag(raw)
        self.sec = sections(raw)
        a, b = self.sec['/TAG0/DATA']
        self.data = bytearray(raw[a:b])
        self.items = list(self.tag.items)
        self.raw = raw
        self.reader = sc.Shkt(raw)
        self.generic = next(i for i in range(1, len(self.items))
                            if self.tag.item(i)[0] == 'hknpConvexShape')

    def original(self, item, size=None):
        kind, at, count = self.tag.item(item)
        size = self.tag.size(kind) if size is None else size
        return bytearray(self.raw[at:at+size*count])

    def add(self, template, data, count=1):
        self.data += bytes(-len(self.data) % 16)
        index = len(self.items)
        self.items.append((self.items[template][0], len(self.data), count))
        self.data += data
        return index

    def put(self, absolute, fmt, *values):
        struct.pack_into(fmt, self.data, absolute-self.tag.data0, *values)

    def mass(self, old_shape, box, mass):
        t = self.tag
        _, at, _ = t.item(old_shape)
        kind = t.item(old_shape)[0]
        pi = t.u32(at+t.offset(kind, 'properties'))
        _req(pi != 0, 'source shape has no properties')
        _, pa, pn = t.item(pi)
        props = self.original(pi)
        found = False
        for i in range(pn):
            row = i*t.size('hknpShapeProperties::Property')
            key = struct.unpack_from('<H', props, row+4)[0]
            if key == sc.MASS_PROPERTIES_KEY:
                old = t.u32(pa+row)
                _req(t.item(old)[0] == 'hknpShapeMassProperties', 'unexpected mass-cache type')
                struct.pack_into('<I', props, row, self.add(old, _mass_cache(box, mass)))
                found = True
        _req(found, 'source shape has no 0xF100 mass property')
        return self.add(pi, props, pn)

    def convex(self, shape, matrix, offset):
        """General positive affine map; subtype caches become a generic convex hull."""
        t = self.tag
        kind, at, _ = t.item(shape)
        hull = at+t.offset(kind, 'hull')
        vi, pi = t.u32(hull), t.u32(hull+4)
        _, va, vn = t.item(vi)
        _, pa, pn = t.item(pi)
        _req(np.isfinite(matrix).all() and np.linalg.det(matrix)>1e-12,'invalid or mirrored convex affine map')
        old_v = np.asarray([struct.unpack_from('<3f', self.raw, va+12*i) for i in range(vn)])
        old_p = np.asarray([struct.unpack_from('<4f', self.raw, pa+16*i) for i in range(pn)])
        points = old_v @ matrix.T + offset
        normals = old_p[:,:3] @ np.linalg.inv(matrix)
        distances = old_p[:,3] - normals @ offset
        length = np.linalg.norm(normals, axis=1)
        planes = np.column_stack((normals/length[:,None], distances/length))
        # Dihedral-angle bounds are derived from the transformed face planes.
        faces_i,indices_i=t.u32(hull+8),t.u32(hull+12)
        _,faces_a,faces_n=t.item(faces_i);_,indices_a,indices_n=t.item(indices_i)
        faces=self.original(faces_i)
        polygons=[]
        for i in range(faces_n):
            first,count=struct.unpack_from('<HB',faces,i*4)
            polygons.append(set(self.raw[indices_a+first:indices_a+first+count]))
        for i,vertices in enumerate(polygons):
            adjacent=[j for j,v in enumerate(polygons) if j!=i and len(vertices&v)>=2]
            _req(bool(adjacent),'convex face has no neighbours')
            cosine=max(float(np.dot(planes[i,:3],planes[j,:3])) for j in adjacent)
            faces[i*4+3]=max(0,min(255,int(math.acos(np.clip(cosine,-1.,1.))*255./math.pi)))
        old_radius = struct.unpack_from('<f', self.raw, at+t.offset(kind,'convexRadius'))[0]
        radius = max(.00001, old_radius*float(np.linalg.svd(matrix,compute_uv=False).min()))
        box = _bbox(points) + np.asarray((-np.ones(3)*radius, np.ones(3)*radius))
        out = self.original(self.generic)
        go = t.offset('hknpConvexShape','hull')
        out[go:go+24] = self.raw[hull:hull+24]
        struct.pack_into('<II', out, go, self.add(vi, points.astype('<f4').tobytes(), vn),
                         self.add(pi, planes.astype('<f4').tobytes(), pn))
        struct.pack_into('<I',out,go+8,self.add(faces_i,faces,faces_n))
        struct.pack_into('<f', out, t.offset('hknpConvexShape','convexRadius'), radius)
        # hkHalf16 stores the high word of a float32. A donor hull's 7.8 cm
        # tolerance must not survive on a centimetre-thick optic contact shape.
        old_pen_bits=struct.unpack_from('<H',self.raw,at+t.offset(kind,'maxAllowedPenetration'))[0]
        old_pen=struct.unpack('<f',struct.pack('<I',old_pen_bits<<16))[0]
        penetration=max(1e-6,min(old_pen*float(np.linalg.svd(matrix,compute_uv=False).min()),float(np.min(box[1]-box[0]))*.1))
        pen_bits=struct.unpack('<I',struct.pack('<f',penetration))[0]>>16
        struct.pack_into('<H',out,t.offset('hknpConvexShape','maxAllowedPenetration'),pen_bits)
        # Preserve the source shape's nominal mass, rebuilding its COM and inertia.
        old_mass = _mass_record(self.reader, shape)[1]
        struct.pack_into('<I', out, t.offset('hknpConvexShape','properties'), self.mass(shape,box,old_mass))
        return self.add(self.generic,out), box

    def finish(self):
        def chunk(name, payload, leaf=True):
            return struct.pack('>I', len(payload)+8 | (0x40000000 if leaf else 0)) + name.encode() + payload
        def old(path):
            a,b=self.sec[path]
            return self.raw[a-8:b]
        self.data += bytes(-len(self.data)%16)
        return chunk('TAG0',old('/TAG0/SDKV')+chunk('DATA',self.data)+old('/TAG0/TYPE')+
                     chunk('INDX',chunk('ITEM',b''.join(struct.pack('<III',*it) for it in self.items)),False),False)


def _mass_record(reader, shape):
    t = reader.tag
    kind, at, _ = t.item(shape)
    pi = t.u32(at+t.offset(kind,'properties'))
    _req(pi, 'shape mass properties absent')
    _,pa,pn=t.item(pi)
    for i in range(pn):
        o=pa+i*t.size('hknpShapeProperties::Property')
        if struct.unpack_from('<H',t.b,o+4)[0] == sc.MASS_PROPERTIES_KEY:
            _,at,_=t.item(t.u32(o))
            blob=t.b[at:at+32]
            mass,volume=struct.unpack_from('<2f',blob,24)
            return blob,mass,volume
    raise CollisionError('shape mass property key absent')


def _instances(reader, shape):
    t=reader.tag
    kind,at,_=t.item(shape)
    _req(kind=='hknpCompressedMeshShape','not a flat-convex CMS')
    item=t.u32(at+t.offset(kind,'externShapes'))
    _,a,n=t.item(item)
    rows=[]
    for i in range(n):
        o=a+i*t.size('hknpShapeInstance')
        rows.append((struct.unpack_from('<4f',t.b,o),struct.unpack_from('<3f',t.b,o+16),
                     struct.unpack_from('<3f',t.b,o+32),t.u32(o+t.offset('hknpShapeInstance','shape'))))
    return item,rows


def _shape_box(reader, shape):
    t=reader.tag
    if t.item(shape)[0]=='hknpCompressedMeshShape':
        boxes=[reader.instance_box(x) for x in _instances(reader,shape)[1]]
        _req(boxes and all(b is not None for b in boxes),'nonconvex CMS extern')
        return np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))
    radius,points,_=reader.hull(shape)
    return _bbox(points)+np.asarray((-np.ones(3)*radius,np.ones(3)*radius))


def _routing(reader, shape):
    t=reader.tag
    _,at,_=t.item(shape)
    data=t.u32(at+t.offset('hknpCompressedMeshShape','data'))
    _,da,_=t.item(data)
    T='hknpCompressedMeshShapeTree'
    si=t.u32(da+t.offset(T,'sections'));_,sa,sn=t.item(si)
    _req(sn==1,'only the audited single-section CMS is supported')
    prim=t.u32(da+t.offset(T,'primitives'));_,pa,pn=t.item(prim)
    svi=t.u32(da+t.offset(T,'sharedVerticesIndex'));_,ia,ni=t.item(svi)
    section_first=t.u32(sa+t.offset('hkcdStaticMeshTree::Section','firstSharedVertexIndex'))&0xffffff
    routes=[]
    for i in range(pn):
        a,b,c,d=struct.unpack_from('<4B',t.b,pa+4*i)
        _req(a==b==d and c==4 and section_first+a+3<ni,'unexpected flat-convex primitive route')
        marker,index=struct.unpack_from('<2I',t.b,ia+2*(section_first+a))
        _req(marker==146,'unexpected external-shape marker')
        routes.append(index)
    return data,si,sa,routes


def _fit_shape(edit, reader, shape, target, mass):
    """Fit one complete proxy shape, preserving the source CMS routing and topology."""
    t=reader.tag
    source=_shape_box(reader,shape)
    scale=(target[1]-target[0])/(source[1]-source[0])
    _req(bool((scale>0).all()),'nonpositive shape scale')
    offset=target[0]-source[0]*scale
    if t.item(shape)[0]!='hknpCompressedMeshShape':
        result,box=edit.convex(shape,np.diag(scale),offset)
        return result,box
    inst_item,instances=_instances(reader,shape)
    data_item,sec_item,sec_at,routes=_routing(reader,shape)
    _req(sorted(routes)==list(range(len(instances))),'extern routing is not one-to-one')
    inst=edit.original(inst_item)
    boxes=[]
    for i,(q,tr,scales,child) in enumerate(instances):
        R=np.asarray(rf.axes(q)).T
        M=np.diag(scale) @ R @ np.diag(scales)
        O=np.asarray(tr)*scale+offset
        new,box=edit.convex(child,M,O)
        boxes.append(box)
        o=i*t.size('hknpShapeInstance')
        struct.pack_into('<4f',inst,o,0.,0.,0.,1.)
        struct.pack_into('<3f',inst,o+16,0.,0.,0.)
        struct.pack_into('<3f',inst,o+32,1.,1.,1.)
        struct.pack_into('<I',inst,o+t.offset('hknpShapeInstance','shape'),new)
    box=np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))
    domain=hb._out32(box+np.asarray((-np.ones(3)*.0001,np.ones(3)*.0001)))
    pb=np.asarray([boxes[i] for i in routes])
    data=edit.original(data_item)
    T='hknpCompressedMeshShapeTree';S='hkcdStaticMeshTree::Section'
    section=edit.original(sec_item)
    sec_nodes=t.u32(sec_at+t.offset(S,'nodes'))
    tree=hb._section_tree(pb,domain)
    struct.pack_into('<I',section,t.offset(S,'nodes'),edit.add(sec_nodes,tree,len(tree)//4))
    struct.pack_into('<8f',section,t.offset(S,'domain'),*domain[0],1.,*domain[1],1.)
    struct.pack_into('<I',data,t.offset(T,'sections'),edit.add(sec_item,section))
    struct.pack_into('<8f',data,t.offset(T,'domain'),*domain[0],1.,*domain[1],1.)
    old_nodes=struct.unpack_from('<I',data,t.offset(T,'nodes'))[0]
    struct.pack_into('<I',data,t.offset(T,'nodes'),edit.add(old_nodes,hkcms.encode_box(domain,domain)+b'\0\0'))
    so=t.offset('hknpCompressedMeshShapeData','simdTree')+t.offset('hkcdSimdTree','nodes')
    old_simd=struct.unpack_from('<I',data,so)[0]
    simd=hb.simd_tree(pb,[i<<1 for i in range(len(pb))])
    struct.pack_into('<I',data,so,edit.add(old_simd,simd,len(simd)//128))
    out=edit.original(shape)
    struct.pack_into('<I',out,t.offset('hknpCompressedMeshShape','data'),edit.add(data_item,data))
    struct.pack_into('<I',out,t.offset('hknpCompressedMeshShape','externShapes'),edit.add(inst_item,inst,len(instances)))
    struct.pack_into('<I',out,t.offset('hknpCompressedMeshShape','properties'),edit.mass(shape,box,mass))
    return edit.add(shape,out),box


def _world_target_to_proxy(box, position, orientation):
    R=np.asarray(rf.axes(orientation)).T
    corners=np.asarray([(x,y,z) for x in box[:,0] for y in box[:,1] for z in box[:,2]])
    return _bbox((corners-np.asarray(position)) @ np.linalg.inv(R).T)


def _named_box(parts, names, predicate=None):
    rows=[]
    for name in names:
        _req(name in parts,f'authored part absent: {name}')
        rows.extend(p for p in parts[name].pos if predicate is None or predicate(np.asarray(p)))
    return _bbox(rows)


def _targets(md,parts,kind):
    groups={}
    for name,p in parts.items():
        bone=p._rigid_bone
        ancestry=[]
        while bone>=0:
            ancestry.append(md.name_of(md.bones[bone].name));bone=md.bones[bone].parent
        owner=next((n for n in ancestry if n in ('cannon_l','cannon_r') or n.startswith('tire_move')),None)
        if owner is None and ancestry[0]=='body':owner='body'
        if owner is not None:groups.setdefault(owner,[]).extend(p.pos)
    out={n:_bbox(P) for n,P in groups.items()}
    if kind=='vulcan':
        # A real solid box inscribed in the closed turntable cylinder; the open
        # operator well must stay empty, even though the source turret was solid.
        base=_named_box(parts,['m163_turntable_and_turret_well'])
        out['cannon_main']=np.asarray(((-.60,base[0,1],-.50),(.60,1.80,.70)))
        # Named receiver/cradle geometry remains authoritative if the model
        # assigns its rear assembly to the native right hinge for death ragdoll.
        rear=_named_box(parts,['m61_elevation_cradle','m61_receiver'])
        rear[1,2]=.30
        front=_named_box(parts,['six_m61_barrels','m61_barrel_clamps'])
        front[0,2]=.50
        out['cannon_l'],out['cannon_r']=front,rear
        # The private CAS removes only the old radar tracks. Each real optic
        # keeps its own original radar skin bone and matching proxy in life/death.
        out['doppler_radar']=_named_box(parts,['m163_optical_sight_and_range_radar','optical_glass'],lambda p:p[0]<0)
        out['tracking_radar']=_named_box(parts,['m163_rangefinder'])
        for side in 'lr':
            key=f'tire_moveG_{side}'
            box=_named_box(parts,[f'wheel_G_{side}',f'wheel_G_outer_{side}'])
            inner,outer=box.copy(),box.copy()
            mid=box[:,0].mean();gap=.04
            if side=='l':inner[1,0]=mid-gap/2;outer[0,0]=mid+gap/2
            else:inner[0,0]=mid+gap/2;outer[1,0]=mid-gap/2
            out[key]=inner;out[f'tire_moveH_{side}']=outer
    else:
        out['cannon_main']=_named_box(parts,['bmpt_turntable_and_low_turret'])
        out['doppler_radar']=_named_box(parts,['commander_panorama_and_gunner_sights','panoramic_optics_glass'],lambda p:p[1]>=2.75)
        out['tracking_radar']=_named_box(parts,['bmpt_gunner_sight','gunner_optics_glass'])
    for side in 'lr':
        for letter in ('A','B','C','D','E','F','G','H'):
            key=f'tire_move{letter}_{side}'
            if kind=='vulcan' and letter in 'GH':continue
            out[key]=_named_box(parts,[f'wheel_{letter}_{side}'])
    return out


def _wheel_overrides(stock,parts,kind):
    roads='BCDEF' if kind=='vulcan' else 'BCDEFG'
    active=[f'tire_move{letter}_{side}' for letter in roads for side in 'lr']
    source={row[0]:row for row in stock['car_base_wheel']}
    wheels=[]
    radii={}
    for name in active:
        row=copy.deepcopy(source[name]);letter,side=name[9],name[-1]
        b=_named_box(parts,[f'wheel_{letter}_{side}'])
        radius=float(min(b[1,1]-b[0,1],b[1,2]-b[0,2])/2)
        radii[name]=radius
        # 679B80 reads child8; 679BFA stores wheel+5C. 679E53 computes
        # its rolling reciprocal, 6787BC uses it for the contact point, and
        # 67A45B passes it as the generated wheel shape's radius.
        row[8]=radius
        wheels.append(row)
    draw=[]
    for i,name in enumerate(active):
        row=copy.deepcopy(next(row for row in stock['car_base_draw_wheel_info'] if row[0][1]==name))
        row[0][0]=i
        draw.append(row)
    idlers='AG' if kind=='vulcan' else 'AH'
    for letter in idlers:
        for side in 'lr':
            b=_named_box(parts,[f'wheel_{letter}_{side}'])
            radius=float(min(b[1,1]-b[0,1],b[1,2]-b[0,2])/2)
            draw.append([[-1.,f'tire_move{letter}_{side}'],[radius]])
    # Exclude the unused H station and both donor radar bones. Only true road
    # stations remain simulation nodes; fixed idlers are drawn by rolling speed.
    # CarBase first puts every proxy into mode 4 (6640A9 -> 6E40B0).
    # Explicit simulation value 0 calls 6E94E0 -> 6E96C0, restoring default
    # mode 1 and disabling its parent constraint. Omitting an accessory would
    # leave it dynamic. AFR records below retain each name's lookup owner.
    kinematic=[f'tire_move{letter}_{side}' for letter in 'ABCDEFGH' for side in 'lr']
    kinematic+=['doppler_radar','tracking_radar']
    overrides={'car_base_wheel':wheels,'car_base_draw_wheel_info':draw,
               'car_base_simulation_node':[[name,0.] for name in kinematic]}
    if 'car_base_cast_mode' in stock:overrides['car_base_cast_mode']=[1]*len(active)
    if 'car_base_draw_wheel_limit_info' in stock:
        old=stock['car_base_draw_wheel_limit_info']
        _req(not old,'nonempty draw-wheel-limit table needs an audited remap')
        overrides['car_base_draw_wheel_limit_info']=[]
    if 'car_base_wheel_effect' in stock:
        old=stock['car_base_wheel_effect']
        _req(len(old)==4 and all(isinstance(r,list) and len(r)==1 for r in old),'unexpected wheel-effect schema')
        overrides['car_base_wheel_effect']=[[0],[1],[len(active)-2],[len(active)-1]]
    return overrides,radii


def _reframe(md,raw,blob,targets,kind):
    """Move real accessory proxies to their assemblies, retaining a unique AFR owner."""
    rs=rf.Shkt(raw)
    version,inner=sgo.read(blob)
    world=bind_world(md)
    moved={}
    replacements={n:n for n in ('doppler_radar','tracking_radar')}
    if kind=='vulcan':replacements.update({f'tire_moveH_{s}':f'tire_moveG_{s}' for s in 'lr'})
    for proxy,bone in replacements.items():
        name='ragdoll_'+proxy
        b=rs.bodies[rs.index[name]]
        position=tuple(targets[proxy].mean(0)) if 'radar' in proxy else tuple(world[md.bone_index(bone)][12:15])
        b.frame=rf.Frame(position,b.frame.q)
        rs._put(b.at+rs.tag.offset(sc.B,'position'),position)
        frame=world[md.bone_index(bone)]
        _req(np.max(np.abs(np.asarray(frame[:12])-np.asarray((1,0,0,0,0,1,0,0,0,0,1,0))))<1e-5,
             'accessory parent bind rotation changed')
        local=g.xform(position,md.bones[md.bone_index(bone)].inv_bind)
        for row in inner['ragdoll_from_animation']:
            if row[0][1]==name:
                row[0][0]=bone;row[1]=list(local);row[2]=list(b.frame.q)
        moved[name]={'bone':bone,'position':position}
    # AFR also establishes the native bone-name -> proxy index used by
    # simulation_node (6E7E40..50). Keep each old, invisible bone's own owner;
    # removing it would make the mode override miss its proxy. It never writes
    # cannon_main/G, whose real dynamic owners remain unique.
    for row in inner['animation_from_ragdoll']:
        if row[0][0] in moved:
            P=rs.bodies[rs.index[row[0][0]]].frame
            bone=md.bones[md.bone_index(row[0][1])]
            position=tuple(world[bone.index][12:15])
            row[1]=list(rf.rot(rf.conj(P.q),rf.sub(position,P.t)))
    for joint in rs.joints:
        A,B=rs.bodies[joint.child].frame,rs.bodies[joint.parent].frame
        pivot=rf.rot(rf.conj(B.q),rf.sub(rf.add(A.t,rf.rot(A.q,rs.pivot(joint.a))),B.t))
        rs._put(joint.b+48,pivot)
    for i,parent in enumerate(rs.parents):
        A=rs.bodies[rs.bone_body[i]].frame
        B=rs.bodies[rs.bone_body[parent]].frame if parent>=0 else rf.Frame((0.,0.,0.),(0.,0.,0.,1.))
        rs._put(rs.ref[i],rf.rot(rf.conj(B.q),rf.sub(A.t,B.t)))
    out=rs.bytes(),sgo.write_depth_first(version,inner)
    bad=rf.problems(md,*out)
    _req(not bad,'reframed binding: '+'; '.join(bad))
    writers={}
    for row in inner['animation_from_ragdoll']:
        proxy,bone=row[0]
        _req(bone not in writers or writers[bone]==proxy,f'duplicate physics writer for {bone}')
        writers[bone]=proxy
    _req(writers['cannon_main']=='ragdoll_cannon_main','turret lost its unique physical owner')
    return *out,moved


def build(game,kind,md,parts):
    """Return (SHKT, ragdoll-binding SGO, vehicle SGO overrides, quantitative report)."""
    _req(kind in KINDS,f'unsupported tracked vehicle {kind}')
    raw=game.read('OBJECT',HOST)
    root=dsgo.parse(game.read('OBJECT','V603_FLAK.SGO')).root
    blob=root.get('ragdoll').items[1].data
    targets=_targets(md,parts,kind)
    framed,binding,moved=_reframe(md,raw,blob,targets,kind)
    reader=sc.Shkt(framed)
    edit=_Editor(framed)
    record=[]
    B=sc.B
    scene=edit.tag.root_variant('hknpPhysicsSceneData')
    _,systems,_=edit.tag.item(edit.tag.u32(scene+edit.tag.offset('hknpPhysicsSceneData','systemDatas')))
    _,system,_=edit.tag.item(edit.tag.u32(systems))
    _,bodies,_=edit.tag.item(edit.tag.u32(system+edit.tag.offset('hknpPhysicsSystemData','bodyCinfos')))
    for body in reader.systems[0].bodies:
        name=body.name.removeprefix('ragdoll_')
        target=targets[name]
        local=_world_target_to_proxy(target,body.position,body.orientation)
        shape,box=_fit_shape(edit,reader,body.shape,local,body.mass)
        edit.put(bodies+body.index*edit.tag.size(B)+edit.tag.offset(B,'shape'),'<I',shape)
        record.append({'proxy':body.name,'target_world':target.tolist(),'actual_proxy_box':box.tolist(),
                       'mass':body.mass,'shape':shape})
    out=edit.finish()
    stock=dsgo.to_py(root)
    overrides,radii=_wheel_overrides(stock,parts,kind)
    report={'kind':kind,'proxies':record,'reframed':moved,'road_wheels_per_side':len(radii)//2,
            'wheel_radii':radii,'mass_model':'solid AABB; native CMS inertia factor 1.5; positive rebuilt 0xF100',
            'collision_model':'affine-fitted source convex partitions; conservative contact approximation',
            'open_basket':'solid turntable interior only' if kind=='vulcan' else None,
            'sibling_filter':'same-group siblings do collide (D93250); split gun volumes and split idler axial halves',
            'simulation_contract':'6640A9 resets all to mode4; explicit value0 restores mode1 via 6E94E0/6E96C0; AFR owner retained',
            'gun_split_gap_m':.20 if kind=='vulcan' else None,
            'idler_split_gap_m':.04 if kind=='vulcan' else None,
            'body_count':len(reader.systems[0].bodies),'joint_count':len(reader.systems[0].joints)}
    check(out,binding,md,overrides,report)
    return out,binding,overrides,report


def _convex_check(reader,shape):
    radius,points,planes=reader.hull(shape)
    P=np.asarray(points);N=np.asarray(planes)
    _req(bool(np.isfinite(P).all()) and len(P)>=4,'invalid convex vertices')
    _req(radius>0 and math.isfinite(radius),'invalid convex radius')
    _req(bool(np.isfinite(N).all()) and len(N)>=4,'invalid convex planes')
    _req(np.max(np.abs(np.linalg.norm(N[:,:3],axis=1)-1))<2e-4,'nonunit convex plane')
    outside=float((P@N[:,:3].T+N[:,3]).max())
    _req(outside<max(1e-4,.0001*float(np.ptp(P,axis=0).max())),'convex planes miss vertices')
    box=_shape_box(reader,shape)
    t=reader.tag;kind,at,_=t.item(shape)
    penetration=struct.unpack_from('<H',t.b,at+t.offset(kind,'maxAllowedPenetration'))[0]<<16
    penetration=struct.unpack('<f',struct.pack('<I',penetration))[0]
    _req(0<penetration<=float(np.min(box[1]-box[0]))*.1001,'stale maximum penetration tolerance')
    hull=at+t.offset(kind,'hull');faces_i,indices_i=t.u32(hull+8),t.u32(hull+12)
    _,fa,fn=t.item(faces_i);_,ia,ni=t.item(indices_i)
    polygons=[];angles=[]
    for i in range(fn):
        first,count,angle=struct.unpack_from('<HBB',t.b,fa+i*4)
        _req(first+count<=ni and count>=3,'invalid convex face indices')
        vertices=set(t.b[ia+first:ia+first+count]);_req(max(vertices)<len(P),'convex face index exceeds vertices')
        polygons.append(vertices);angles.append(angle)
    for i,vertices in enumerate(polygons):
        adjacent=[j for j,v in enumerate(polygons) if j!=i and len(vertices&v)>=2]
        _req(bool(adjacent),'convex face missing adjacent face')
        cosine=max(float(np.dot(N[i,:3],N[j,:3])) for j in adjacent)
        expected_angle=max(0,min(255,int(math.acos(np.clip(cosine,-1.,1.))*255./math.pi)))
        _req(abs(angles[i]-expected_angle)<=1,'stale convex dihedral-angle cache')
    cache,mass,volume=_mass_record(reader,shape)
    _req(mass>0 and volume>0 and math.isfinite(mass) and math.isfinite(volume),'nonpositive mass cache')
    _req(cache[16:24]==IDENTITY_MASS_QUAT,'invalid mass principal-axis quaternion')
    expected=_mass_cache(box,mass)
    for offset in (0,8):
        got=_unpack_vec(cache,offset);want=_unpack_vec(expected,offset)
        _req(np.max(np.abs(got-want))<max(1e-5,float(np.max(np.abs(want)))*2e-4),'stale mass COM/inertia')
    _req(abs(volume-float(np.prod(box[1]-box[0])))<max(1e-5,volume*2e-4),'stale mass volume')
    return box


def _cms_check(reader,shape):
    t=reader.tag
    _,rows=_instances(reader,shape)
    boxes=[_convex_check(reader,row[3]) for row in rows]
    # New externs must have identity transforms; every fitted vertex is already
    # in the proxy frame, so stale source instance transforms would double it.
    for q,tr,scale,_ in rows:
        _req(np.max(np.abs(np.asarray(q)-(0,0,0,1)))<1e-6 and
             np.max(np.abs(tr))<1e-6 and np.max(np.abs(np.asarray(scale)-1))<1e-6,
             'stale extern transform')
    data,sec,sa,routes=_routing(reader,shape)
    _req(sorted(routes)==list(range(len(rows))),'duplicate or missing external shape route')
    _,da,_=t.item(data)
    T='hknpCompressedMeshShapeTree';S='hkcdStaticMeshTree::Section'
    def aabb(at):
        f=struct.unpack_from('<8f',t.b,at)
        return np.asarray((f[:3],f[4:7]))
    domain=aabb(da+t.offset(T,'domain'));section=aabb(sa+t.offset(S,'domain'))
    actual=np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))
    for outer in (domain,section):
        _req(bool((outer[0]<=actual[0]+1e-5).all() and (outer[1]>=actual[1]-1e-5).all()),'stale CMS domain')
    nodes=t.u32(sa+t.offset(S,'nodes'));_,na,nn=t.item(nodes)
    leaves=set();visited=set()
    def visit(index,parent):
        _req(0<=index<nn and index not in visited,'CMS section child out of range or cyclic')
        visited.add(index)
        at=na+index*4
        box=hkcms.decode_box(parent,t.b[at:at+3]);code=t.b[at+3]
        if code&1:
            _req(code>=3,'invalid CMS section branch offset')
            visit(index+1,box);visit(index+2*(code>>1),box)
        else:
            p=code>>1
            _req(p<len(routes) and p not in leaves,'CMS section duplicate or invalid primitive')
            leaves.add(p);wanted=boxes[routes[p]]
            _req(bool((box[0]<=wanted[0]+1e-4).all() and (box[1]>=wanted[1]-1e-4).all()),'stale CMS leaf')
    visit(0,section)
    _req(leaves==set(range(len(routes))),'CMS section missing primitive')
    _,shape_at,_=t.item(shape)
    _req(struct.unpack_from('<2i',t.b,shape_at+t.offset('hknpCompressedMeshShape','numTriangles'))==(0,len(rows)),
         'stale CMS convex count')
    key_count,bits,max_key=struct.unpack_from('<iiI',t.b,da+t.offset(T,'numPrimitiveKeys'))
    _req(key_count==len(routes) and max_key==2*(len(routes)-1) and bits==max_key.bit_length(),'stale CMS key metadata')
    top=t.u32(da+t.offset(T,'nodes'));_,ta,tn=t.item(top)
    _req(tn==1,'unexpected CMS top tree')
    top_box=hkcms.decode_box(domain,t.b[ta:ta+3])
    _req(t.b[ta+3:ta+5]==bytes(2) and bool((top_box[0]<=section[0]+1e-4).all() and
         (top_box[1]>=section[1]-1e-4).all()),'stale CMS top tree')
    so=t.offset('hknpCompressedMeshShapeData','simdTree')+t.offset('hkcdSimdTree','nodes')
    simd=t.u32(da+so);seen=set();nodes=reader.simd_lanes(simd);visited=set()
    def simd_visit(index):
        _req(0<index<len(nodes) and index not in visited,'invalid or cyclic SIMD child')
        visited.add(index);leaf,lanes=nodes[index]
        _req(bool(lanes),'empty SIMD node')
        for lo,hi,key in lanes:
            if leaf:
                p=key>>1
                _req(key%2==0 and p<len(routes) and p not in seen,'invalid SIMD primitive key')
                seen.add(p);box=boxes[routes[p]]
            else:box=simd_visit(key)
            _req(bool((np.asarray(lo)<=box[0]+1e-4).all() and (np.asarray(hi)>=box[1]-1e-4).all()),'stale SIMD bounds')
        return np.asarray((np.min([v[0] for v in lanes],0),np.max([v[1] for v in lanes],0)))
    simd_visit(1)
    _req(seen==set(range(len(routes))),'SIMD missing external shape')
    cache,mass,volume=_mass_record(reader,shape);expected=_mass_cache(actual,mass)
    _req(cache[16:24]==IDENTITY_MASS_QUAT,'invalid CMS mass principal-axis quaternion')
    _req(mass>0 and volume>0 and math.isfinite(mass) and math.isfinite(volume),'invalid CMS mass cache')
    for offset in (0,8):
        _req(np.max(np.abs(_unpack_vec(cache,offset)-_unpack_vec(expected,offset)))<max(1e-5,mass*1e-4),
             'stale CMS mass cache')
    _req(abs(volume-float(np.prod(actual[1]-actual[0])))<max(1e-5,volume*2e-4),'stale CMS volume')
    return actual


def _sweep(report):
    """Prove sibling splits stay separated under common traverse/elevation/roll.

    The cannon hinges have the same Y/Z origin and the same X axis; translation
    along X cannot change their pitch axis. A 20 cm axial gap includes an extra
    1 cm contact skin on both proxies and 15 cm of relative recoil allowance.
    Idler halves split along the rotation axis, independent of wheel angle.
    """
    if report['kind']!='vulcan':return
    boxes={row['proxy']:np.asarray(row['target_world']) for row in report['proxies']}
    left,right=boxes['ragdoll_cannon_l'],boxes['ragdoll_cannon_r']
    _req(left[0,2]-right[1,2]>.02+.15,'M61 sibling collision gap is too small')
    for side in 'lr':
        a,b=boxes[f'ragdoll_tire_moveG_{side}'],boxes[f'ragdoll_tire_moveH_{side}']
        gap=max(a[0,0]-b[1,0],b[0,0]-a[1,0])
        _req(gap>=.0399,'idler halves overlap')


def check(shkt,binding,md,overrides,report):
    """Inspect serialized shapes, derived trees/mass caches and unique pose ownership."""
    reader=sc.Shkt(shkt)
    bad=[str(x) for x in sc.problems(shkt) if x.severity=='error']
    _req(not bad,'SHKT validation: '+'; '.join(bad))
    bad=rf.problems(md,shkt,binding)
    _req(not bad,'ragdoll binding: '+'; '.join(bad))
    _req(len(reader.systems)==1 and len(reader.systems[0].bodies)==22 and len(reader.systems[0].joints)==21,
         'native V603 physical topology changed')
    records={r['proxy']:r for r in report['proxies']}
    for body in reader.systems[0].bodies:
        box=_cms_check(reader,body.shape) if reader.tag.item(body.shape)[0]=='hknpCompressedMeshShape' else _convex_check(reader,body.shape)
        target=_world_target_to_proxy(np.asarray(records[body.name]['target_world']),body.position,body.orientation)
        _req(bool((box[0]>=target[0]-2e-4).all() and (box[1]<=target[1]+2e-4).all()),
             f'{body.name}: collision extends outside its authored assembly')
    inner=sgo.read(binding)[1]
    writers={}
    for row in inner['animation_from_ragdoll']:
        proxy,bone=row[0]
        _req(bone not in writers or writers[bone]==proxy,'multiple AFR physics owners')
        writers[bone]=proxy
    _req(writers.get('cannon_main')=='ragdoll_cannon_main','wrong turret physics owner')
    for proxy in report['reframed']:
        name=proxy.removeprefix('ragdoll_')
        owners=[row[0][1] for row in inner['animation_from_ragdoll'] if row[0][0]==proxy]
        _req(owners==[name],'accessory lost its unique old-bone lookup owner')
        _req([name,0.] in overrides['car_base_simulation_node'],'accessory was left dynamic')
    wheels=overrides['car_base_wheel']
    _req(len(wheels)==report['road_wheels_per_side']*2,'road-wheel count differs')
    names=[row[0] for row in wheels]
    _req(len(set(names))==len(names),'duplicate road-wheel station')
    for row in overrides['car_base_draw_wheel_info']:
        index,bone=row[0]
        _req(md.bone_index(bone)>=0,'draw wheel references a missing bone')
        if index>=0:_req(index<len(names) and names[int(index)]==bone,'draw wheel index differs')
    simulation={row[0] for row in overrides['car_base_simulation_node']}
    required={f'tire_move{k}_{side}' for k in 'ABCDEFGH' for side in 'lr'}|{'doppler_radar','tracking_radar'}
    _req(simulation==required,'kinematic proxy stations differ')
    _req(all(row[1]==0 for row in overrides['car_base_simulation_node']),'wrong proxy simulation mode')
    if 'car_base_cast_mode' in overrides:_req(len(overrides['car_base_cast_mode'])==len(names),'cast-mode count differs')
    if 'car_base_wheel_effect' in overrides:
        _req(all(0<=int(row[0])<len(names) for row in overrides['car_base_wheel_effect']),'wheel effect has stale index')
    _sweep(report)
    return {'bodies':22,'joints':21,'shapes':'all rebuilt and checked','unique_AFR':True,
            'road_wheels_per_side':len(names)//2,'sibling_separation':True}
