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
import hkshape
from mdb import bind_world
import ragdoll_fit as rf
import sgo
import shkt_check as sc

HOST = 'Ragdoll_v603_flak.shkt'
KINDS = ('vulcan', 'bmpt')


class CollisionError(ValueError):
    pass


def _req(ok, message):
    if not ok:
        raise CollisionError(message)


def _bbox(points):
    p = np.asarray(points, dtype=float)
    _req(p.ndim == 2 and p.shape[1] == 3 and len(p) > 0, 'empty point set')
    return np.asarray((p.min(0), p.max(0)))


class _Editor:
    """New shapes made beside the donor's in one tagfile graph (pylib/hkfile.py, pylib/hkshape.py): the donor's
    blocks are only read; every fitted shape is a new block, and what nothing points at any more is not written."""
    def __init__(self, tf):
        self.tf = tf

    def convex(self, shape, matrix, offset):
        """`shape` under a positive affine map (hkshape.affine_convex): rebuilt hull, radius x the smallest singular
        value, its own mass properties; with its box (vertices +- radius)."""
        _req(np.isfinite(matrix).all() and np.linalg.det(matrix)>1e-12,'invalid or mirrored convex affine map')
        try:
            new = hkshape.affine_convex(self.tf, shape, np.asarray(matrix).tolist(), np.asarray(offset).tolist())
        except hkshape.HullError as e:
            raise CollisionError(f'fitted convex: {e}') from e
        return new, _shape_box(new)


def _mass_record(shape):
    m = hkshape.mass_properties(shape)
    _req(m is not None, 'shape mass properties absent')
    blob=bytes(m.data[:32])
    mass,volume=struct.unpack_from('<2f',blob,24)
    return blob,mass,volume


def _instances(shape):
    _req(shape.type.full_name=='hknpCompressedMeshShape','not a flat-convex CMS')
    return hkshape.instance_array(shape),hkshape.instances(shape)


def _shape_box(shape):
    if shape.type.full_name=='hknpCompressedMeshShape':
        boxes=[sc.Shkt.instance_box(x) for x in _instances(shape)[1]]
        _req(boxes and all(b is not None for b in boxes),'nonconvex CMS extern')
        return np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))
    radius,points,_=hkshape.convex_hull(shape)
    return _bbox(points)+np.asarray((-np.ones(3)*radius,np.ones(3)*radius))


def _routing(shape):
    data=shape.value['data'].target
    tree=data.value['meshTree']
    sections=tree['sections'].target
    _req(sections is not None and sections.count==1,'only the audited single-section CMS is supported')
    prims=tree['primitives'].target
    svi=tree['sharedVerticesIndex'].target
    section_first=sections[0]['firstSharedVertexIndex'].int()&0xffffff
    routes=[]
    for i in range(prims.count):
        a,b,c,d=struct.unpack_from('<4B',prims.data,4*i)
        _req(a==b==d and c==4 and section_first+a+3<svi.count,'unexpected flat-convex primitive route')
        marker,index=struct.unpack_from('<2I',svi.data,2*(section_first+a))
        _req(marker==146,'unexpected external-shape marker')
        routes.append(index)
    return data,sections,routes


def _fit_shape(edit, shape, target, mass):
    """Fit one complete proxy shape: a convex one mapped onto `target`, a compressed mesh of convex shapes written
    again over its mapped externs (hkshape.write_convex_mesh: canonical routing, both trees, the stock box mass rule
    with `mass`)."""
    source=_shape_box(shape)
    scale=(target[1]-target[0])/(source[1]-source[0])
    _req(bool((scale>0).all()),'nonpositive shape scale')
    offset=target[0]-source[0]*scale
    if shape.type.full_name!='hknpCompressedMeshShape':
        return edit.convex(shape,np.diag(scale),offset)
    _,instances=_instances(shape)
    _,_,routes=_routing(shape)
    _req(sorted(routes)==list(range(len(instances))),'extern routing is not one-to-one')
    children=[]
    boxes=[]
    for q,tr,scales,child in instances:
        R=np.asarray(rf.axes(q)).T
        M=np.diag(scale) @ R @ np.diag(scales)
        O=np.asarray(tr)*scale+offset
        new,box=edit.convex(child,M,O)
        children.append(new)
        boxes.append(box)
    out=hkshape.write_convex_mesh(edit.tf,children,mass,keep=shape)
    return out,np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))


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
        rs.set_position(rs.index[name],position)
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
        pivot=rf.rot(rf.conj(B.q),rf.sub(rf.add(A.t,rf.rot(A.q,rs.pivot(joint,'A'))),B.t))
        rs.set_pivot(joint,'B',pivot)
    for i,parent in enumerate(rs.parents):
        A=rs.bodies[rs.bone_body[i]].frame
        B=rs.bodies[rs.bone_body[parent]].frame if parent>=0 else rf.Frame((0.,0.,0.),(0.,0.,0.,1.))
        rs.set_ref_t(i,rf.rot(rf.conj(B.q),rf.sub(A.t,B.t)))
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
    edit=_Editor(reader.tf)
    record=[]
    for body in reader.systems[0].bodies:
        name=body.name.removeprefix('ragdoll_')
        target=targets[name]
        local=_world_target_to_proxy(target,body.position,body.orientation)
        shape,box=_fit_shape(edit,body.shape,local,body.mass)
        body.body.set_shape(shape)
        record.append({'proxy':body.name,'target_world':target.tolist(),'actual_proxy_box':box.tolist(),
                       'mass':body.mass,'shape':shape.type.full_name})
    out=reader.tf.write()
    stock=dsgo.to_py(root)
    overrides,radii=_wheel_overrides(stock,parts,kind)
    report={'kind':kind,'proxies':record,'reframed':moved,'road_wheels_per_side':len(radii)//2,
            'wheel_radii':radii,'mass_model':'hulls: their own (hkshape.from_hull); meshes: the stock solid-domain box rule (x1.5)',
            'collision_model':'affine-fitted source convex partitions; conservative contact approximation',
            'open_basket':'solid turntable interior only' if kind=='vulcan' else None,
            'sibling_filter':'same-group siblings do collide (D93250); split gun volumes and split idler axial halves',
            'simulation_contract':'6640A9 resets all to mode4; explicit value0 restores mode1 via 6E94E0/6E96C0; AFR owner retained',
            'gun_split_gap_m':.20 if kind=='vulcan' else None,
            'idler_split_gap_m':.04 if kind=='vulcan' else None,
            'body_count':len(reader.systems[0].bodies),'joint_count':len(reader.systems[0].joints)}
    check(out,binding,md,overrides,report)
    return out,binding,overrides,report


def _convex_check(shape):
    radius,points,planes=hkshape.convex_hull(shape)
    P=np.asarray(points);N=np.asarray(planes)
    _req(bool(np.isfinite(P).all()) and len(P)>=4,'invalid convex vertices')
    _req(radius>0 and math.isfinite(radius),'invalid convex radius')
    _req(bool(np.isfinite(N).all()) and len(N)>=4,'invalid convex planes')
    _req(np.max(np.abs(np.linalg.norm(N[:,:3],axis=1)-1))<2e-4,'nonunit convex plane')
    outside=float((P@N[:,:3].T+N[:,3]).max())
    _req(outside<max(1e-4,.0001*float(np.ptp(P,axis=0).max())),'convex planes miss vertices')
    hull=hkshape.read_hull(shape)
    bad=hkshape.hull_problems(hull)
    _req(not bad,'inconsistent convex hull: '+'; '.join(bad[:3]))
    box=_shape_box(shape)
    penetration=shape.value['maxAllowedPenetration'].f32()
    want=hkshape.PENETRATION_FACTOR*(hull.min_width()+2*radius)
    _req(0<penetration<=want and penetration>=want*(1-2**-7),'stale maximum penetration tolerance')
    m=hkshape.mass_properties(shape)
    _req(m is not None,'shape mass properties absent')
    got=hkshape.read_mass(m)
    _req(got.mass>0 and got.volume>0 and math.isfinite(got.mass) and math.isfinite(got.volume),'nonpositive mass cache')
    plain=hkshape.from_hull(hull,radius,1.0)
    _req(abs(got.volume-plain.volume)<=max(1e-6,plain.volume*1e-4),'stale mass volume')
    extent=float(np.ptp(P,axis=0).max())
    packing=max(abs(c) for c in plain.com)*2**-14      # hkPackedVector3: 16-bit mantissas of the largest component
    _req(max(abs(got.com[k]-plain.com[k]) for k in range(3))<=max(1e-5,extent*1e-4,packing),'stale mass COM')
    _req(abs(math.sqrt(sum(c*c for c in got.axes))-1)<3e-4,'invalid mass principal-axis quaternion')
    factor=(sum(got.moments)/got.mass)/(sum(plain.moments)/plain.mass)
    _req(min(abs(factor-1.0),abs(factor-1.5))<2e-3,'stale mass inertia')
    want=np.asarray(plain.tensor())*(1.5 if abs(factor-1.5)<abs(factor-1.0) else 1.0)*got.mass/plain.mass
    _req(float(np.max(np.abs(np.asarray(got.tensor())-want)))<=2e-3*float(np.max(np.abs(want))),'stale mass inertia axes')
    return box


def _cms_check(shape):
    _,rows=_instances(shape)
    boxes=[_convex_check(row[3]) for row in rows]
    # New externs must have identity transforms; every fitted vertex is already
    # in the proxy frame, so stale source instance transforms would double it.
    for q,tr,scale,_ in rows:
        _req(np.max(np.abs(np.asarray(q)-(0,0,0,1)))<1e-6 and
             np.max(np.abs(tr))<1e-6 and np.max(np.abs(np.asarray(scale)-1))<1e-6,
             'stale extern transform')
    data,sections,routes=_routing(shape)
    _req(sorted(routes)==list(range(len(rows))),'duplicate or missing external shape route')
    tree=data.value['meshTree']
    def aabb(v):
        return np.asarray((v['min'].vec(3),v['max'].vec(3)))
    domain=aabb(tree['domain']);section=aabb(sections[0]['domain'])
    actual=np.asarray((np.min([b[0] for b in boxes],0),np.max([b[1] for b in boxes],0)))
    for outer in (domain,section):
        _req(bool((outer[0]<=actual[0]+1e-5).all() and (outer[1]>=actual[1]-1e-5).all()),'stale CMS domain')
    nodes=sections[0]['nodes'].target;nn=nodes.count;code=nodes.data
    leaves=set();visited=set()
    def visit(index,parent):
        _req(0<=index<nn and index not in visited,'CMS section child out of range or cyclic')
        visited.add(index)
        at=index*4
        box=hkcms.decode_box(parent,bytes(code[at:at+3]));c=code[at+3]
        if c&1:
            _req(c>=3,'invalid CMS section branch offset')
            visit(index+1,box);visit(index+2*(c>>1),box)
        else:
            p=c>>1
            _req(p<len(routes) and p not in leaves,'CMS section duplicate or invalid primitive')
            leaves.add(p);wanted=boxes[routes[p]]
            _req(bool((box[0]<=wanted[0]+1e-4).all() and (box[1]>=wanted[1]-1e-4).all()),'stale CMS leaf')
    visit(0,section)
    _req(leaves==set(range(len(routes))),'CMS section missing primitive')
    _req((shape.value['numTriangles'].int(),shape.value['numConvexShapes'].int())==(0,len(rows)),
         'stale CMS convex count')
    key_count,bits,max_key=tree['numPrimitiveKeys'].int(),tree['bitsPerKey'].int(),tree['maxKeyValue'].int()
    _req(key_count==len(routes) and max_key==2*(len(routes)-1) and bits==max_key.bit_length(),'stale CMS key metadata')
    top=tree['nodes'].target
    _req(top.count==1,'unexpected CMS top tree')
    top_box=hkcms.decode_box(domain,bytes(top.data[:3]))
    _req(top.data[3:5]==bytes(2) and bool((top_box[0]<=section[0]+1e-4).all() and
         (top_box[1]>=section[1]-1e-4).all()),'stale CMS top tree')
    simd=data.value['simdTree']['nodes'].target;seen=set();nodes=hkshape.simd_lanes(simd);visited=set()
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
    cache,mass,volume=_mass_record(shape)
    expected=hkshape.mass_bytes(hkshape.box_mass(tuple(domain[0]),tuple(domain[1]),mass))
    _req(cache[16:24]==expected[16:24],'invalid CMS mass principal-axis quaternion')
    _req(mass>0 and volume>0 and math.isfinite(mass) and math.isfinite(volume),'invalid CMS mass cache')
    for offset in (0,8):
        _req(np.max(np.abs(np.asarray(hkshape.unpack_vec3(struct.unpack_from('<4H',cache,offset)))-
                           np.asarray(hkshape.unpack_vec3(struct.unpack_from('<4H',expected,offset)))))<max(1e-5,mass*1e-4),
             'stale CMS mass cache')
    _req(abs(volume-float(np.prod(domain[1]-domain[0])))<max(1e-5,volume*2e-4),'stale CMS volume')
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
    bad=[str(x) for x in sc.problems(shkt) if x.severity in ('error','warning')]
    _req(not bad,'SHKT validation: '+'; '.join(bad))
    # The V603 bodies keep no massDistribution: the game derives each from its fitted shape's mass properties.
    _req(all(b.mass_distribution is None and hkshape.mass_properties(b.shape) is not None
             for b in reader.systems[0].bodies),'a body whose mass does not follow its fitted shape')
    bad=rf.problems(md,shkt,binding)
    _req(not bad,'ragdoll binding: '+'; '.join(bad))
    _req(len(reader.systems)==1 and len(reader.systems[0].bodies)==22 and len(reader.systems[0].joints)==21,
         'native V603 physical topology changed')
    records={r['proxy']:r for r in report['proxies']}
    for body in reader.systems[0].bodies:
        box=_cms_check(body.shape) if body.shape.type.full_name=='hknpCompressedMeshShape' else _convex_check(body.shape)
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
