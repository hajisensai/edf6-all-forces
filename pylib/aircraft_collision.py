"""Aircraft contact shapes measured from the actual rendered mesh, including its wings.

The 506 has TWO collision shapes: its flight body (normally a box) and its animation
ragdoll (normally a helicopter). We generate a tagged ragdoll body compound; body506.cpp
also uses that exact compound for the flight body. NPC, parked and requested aircraft
share these files. No game directory is written by this module.

The compound is a height-field of small solid convex cells. Triangles are clipped to
each X/Z cell before measuring its Y range, so empty corners around swept wings stay
empty. Unlike a vertex-only sampler this includes long triangles spanning many cells.
This is a conservative collision approximation, not a triangle-perfect mesh: 24 by 32
cells, at most one cell of silhouette error and a 3 cm skin for zero-thickness surfaces.
Landing gear is included in the grounded bind pose (its movement is cosmetic).
"""
from __future__ import annotations

import math
import struct
from functools import lru_cache

import jet_models
from hktag import Tag, sections
from hkcms import bodies
from mdb import bind_world, ident, read_elem

MAGIC = 0x4544463641495231  # EDF6AIR1: native hknpShape::userData, src/body506.cpp
STOCK = 'RAGDOLL_V506_HELI.SHKT'
NX, NZ = 24, 32
SKIN = 0.03
FILES = {None: 'EDF6VC_AIRFRAME_BOMBER.SHKT',
         **{k: k.replace('.MRAB', '_AIRFRAME.SHKT') for k in jet_models.MODELS},
         **{k: f'EDF6VC_AIRFRAME_{k.upper()}.SHKT' for k in jet_models.STOCK_BOMBERS}}


def model_key(jet, model):
    """Supported rendered model, or False for aircraft built elsewhere (Primer/sub)."""
    if jet.file in jet_models.MODELS:
        return jet.file
    if jet.box_model in jet_models.STOCK_BOMBERS:
        return jet.box_model
    if jet.model is not None:
        return False
    if model == ['app:/object/edf6vc_jet.mrab', 'bomber501.mdb']:
        return None
    if model is None:
        return False  # a caller using the legacy, ungrounded stock bomber
    file = model[1].removesuffix('.mdb')
    return file if file in jet_models.STOCK_BOMBERS else False


def triangles(md):
    """Triangle vertices in the same model-space bind frame as jet_models.model_box."""
    world = bind_world(md)
    for obj in md.objects:
        for mesh in obj.meshes:
            elem = next(e for e in mesh.elems if e.name.lower() == 'position')
            raw = read_elem(mesh, elem.name, elem.channel)
            matrix = ident() if mesh.flags[1] else world[obj.bone]
            points = [tuple(sum(p[k] * matrix[4*k+c] for k in range(3)) + matrix[12+c]
                            for c in range(3)) for p in raw]
            indices = struct.unpack('<' + 'H'*(len(mesh.indices)//2), mesh.indices)
            for i in range(0, len(indices), 3):
                yield tuple(points[k] for k in indices[i:i+3])


def _clip(poly, axis, edge, sign):
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        da, db = sign*(a[axis]-edge), sign*(b[axis]-edge)
        if da >= -1e-9:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da/(da-db)
            out.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(3)))
    return out


def mesh_cells(tris, nx=NX, nz=NZ):
    """Model-space (min,max) boxes of occupied cells; the actual clipped geometry bounds."""
    tris = list(tris)
    lo, hi = jet_models.bbox([p for tri in tris for p in tri])
    dx, dz = (hi[0]-lo[0])/nx, (hi[2]-lo[2])/nz
    if not dx > 0 or not dz > 0:
        raise ValueError('aircraft has no X/Z extent')
    cells = {}
    for tri in tris:
        x0 = max(0, min(nx-1, int((min(p[0] for p in tri)-lo[0])/dx)))
        x1 = max(0, min(nx-1, int((max(p[0] for p in tri)-lo[0])/dx)))
        z0 = max(0, min(nz-1, int((min(p[2] for p in tri)-lo[2])/dz)))
        z1 = max(0, min(nz-1, int((max(p[2] for p in tri)-lo[2])/dz)))
        for x in range(x0, x1+1):
            for z in range(z0, z1+1):
                poly = list(tri)
                for axis, edge, sign in ((0, lo[0]+x*dx, 1), (0, lo[0]+(x+1)*dx, -1),
                                         (2, lo[2]+z*dz, 1), (2, lo[2]+(z+1)*dz, -1)):
                    poly = _clip(poly, axis, edge, sign)
                    if not poly:
                        break
                if not poly:
                    continue
                a, b = jet_models.bbox(poly)
                if (x, z) in cells:
                    olda, oldb = cells[x, z]
                    a, b = [min(a[k], olda[k]) for k in range(3)], [max(b[k], oldb[k]) for k in range(3)]
                cells[x, z] = a, b
    # A wing may be a plane, so give it a small thickness on both sides. Keep the
    # wheel contact (minimum Y) exactly on the model's existing grounded origin.
    return [(tuple(a[k] if b[k]-a[k] >= 2*SKIN else max(lo[k], (a[k]+b[k])/2-SKIN) for k in range(3)),
             tuple(b[k] if b[k]-a[k] >= 2*SKIN else (a[k]+b[k])/2+SKIN for k in range(3)))
            for _, (a, b) in sorted(cells.items())]


class _Edit:
    """Append DATA/ITEM records without changing any original item indices or TYPEs."""
    def __init__(self, blob):
        self.tag = Tag(blob)
        self.sec = sections(blob)
        a, b = self.sec['/TAG0/DATA']
        self.data = bytearray(blob[a:b])
        self.items = list(self.tag.items)

    def add(self, template, data, count=1):
        self.data.extend(b'\0' * (-len(self.data) % 16))
        index = len(self.items)
        self.items.append((self.items[template][0], len(self.data), count))
        self.data.extend(data)
        return index

    def original(self, item):
        _, at, n = self.tag.item(item)
        return bytearray(self.tag.b[at:at + self.tag.size(self.tag.item(item)[0])*n])

    def finish(self):
        def chunk(name, payload, leaf=True):
            return struct.pack('>I', len(payload)+8 | (0x40000000 if leaf else 0)) + name.encode() + payload
        source = self.tag.b
        def old(path):
            a, b = self.sec[path]
            return source[a-8:b]
        self.data.extend(b'\0' * (-len(self.data) % 16))
        items = b''.join(struct.pack('<III', *e) for e in self.items)
        return chunk('TAG0', old('/TAG0/SDKV') + chunk('DATA', self.data) + old('/TAG0/TYPE') +
                     chunk('INDX', chunk('ITEM', items), False), False)


def _tree(boxes):
    nodes = [bytearray(128)]  # Havok sentinel; root is node 1.
    def node(ids):
        index = len(nodes)
        buf = bytearray(128)
        nodes.append(buf)
        leaves = len(ids) <= 4
        if leaves:
            groups = [[i] for i in ids]
        else:
            centres = {i: [(boxes[i][0][k]+boxes[i][1][k])/2 for k in range(3)] for i in ids}
            axis = max(range(3), key=lambda k: max(centres[i][k] for i in ids)-min(centres[i][k] for i in ids))
            ids = sorted(ids, key=lambda i: centres[i][axis])
            width = math.ceil(len(ids)/4)
            groups = [ids[i:i+width] for i in range(0, len(ids), width)]
        f = [3.402823466e38 if k%2 == 0 else -3.402823466e38 for k in range(6) for _ in range(4)]
        data = [0]*4
        for lane, group in enumerate(groups):
            data[lane] = group[0] if leaves else node(group)
            for k in range(3):
                # Outward padding covers float32 rounding of the serialized hull.
                f[8*k+lane] = min(boxes[i][0][k] for i in group)-0.0001
                f[8*k+4+lane] = max(boxes[i][1][k] for i in group)+0.0001
        struct.pack_into('<24f4I?', buf, 0, *f, *data, leaves)
        return index
    node(list(range(len(boxes))))
    return b''.join(nodes), len(nodes)


def _compound(edit, template, boxes, magic=0):
    """New compound of axis-aligned convex hulls, with a rebuilt 4-way BVH."""
    t = edit.tag
    _, cat, _ = t.item(template)
    inst = t.u32(cat+t.offset('hknpCompoundShape', 'instances'))
    _, iat, _ = t.item(inst)
    # V506 body instance 3 is an eight-vertex, six-plane, axis-aligned box.
    original = iat+3*t.size('hknpShapeInstance')
    shape = t.u32(original+t.offset('hknpShapeInstance', 'shape'))
    shape_data = edit.original(shape)
    _, sat, _ = t.item(shape)
    hull = t.offset('hknpConvexShape', 'hull')
    vi, pi = t.u32(sat+hull), t.u32(sat+hull+4)
    _, va, vn = t.item(vi)
    _, pa, pn = t.item(pi)
    if vn != 8 or pn != 6:
        raise ValueError('V506 box hull template changed')
    vertices = [struct.unpack_from('<3f', t.b, va+12*i) for i in range(8)]
    planes = [struct.unpack_from('<4f', t.b, pa+16*i) for i in range(6)]
    mid = [(min(v[k] for v in vertices)+max(v[k] for v in vertices))/2 for k in range(3)]
    if any(sum(abs(n) > 1e-6 for n in p[:3]) != 1 for p in planes):
        raise ValueError('V506 box hull is no longer axis aligned')
    instances = bytearray()
    for lo, hi in boxes:
        sh = bytearray(shape_data)
        struct.pack_into('<f', sh, t.offset('hknpConvexShape', 'convexRadius'), 0.0)
        # Cached mass properties describe the donor's tiny box. Let Havok derive
        # inertia from the new vertices instead of retaining the helicopter's.
        struct.pack_into('<I', sh, t.offset('hknpConvexShape', 'properties'), 0)
        verts = b''.join(struct.pack('<3f', *(hi[k] if p[k] > mid[k] else lo[k] for k in range(3))) for p in vertices)
        normals = []
        for p in planes:
            axis = max(range(3), key=lambda k: abs(p[k]))
            normals.append(struct.pack('<4f', *p[:3], -p[axis]*(hi[axis] if p[axis] > 0 else lo[axis])))
        struct.pack_into('<II', sh, hull, edit.add(vi, verts, 8), edit.add(pi, b''.join(normals), 6))
        shape_id = edit.add(shape, sh)
        instance = bytearray(t.b[original:original+64])
        struct.pack_into('<I', instance, t.offset('hknpShapeInstance', 'shape'), shape_id)
        instances.extend(instance)
    comp = edit.original(template)
    struct.pack_into('<I', comp, t.offset('hknpCompoundShape', 'properties'), 0)
    inst_id = edit.add(inst, instances, len(boxes))
    struct.pack_into('<III', comp, t.offset('hknpCompoundShape', 'instances'), inst_id, 0xFFFFFFFF, len(boxes))
    comp[t.offset('hknpCompoundShape', 'numShapeKeyBits')] = max(1, (len(boxes)-1).bit_length())
    struct.pack_into('<Q', comp, t.offset('hknpCompoundShape', 'userData'), magic)
    lo = [min(b[0][k] for b in boxes)-0.0001 for k in range(3)]
    hi = [max(b[1][k] for b in boxes)+0.0001 for k in range(3)]
    struct.pack_into('<8f', comp, t.offset('hknpCompoundShape', 'aabb'), *lo, 0, *hi, 0)
    struct.pack_into('<f', comp, t.offset('hknpCompoundShape', 'boundingRadius'),
                     math.sqrt(sum(max(abs(lo[k]), abs(hi[k]))**2 for k in range(3))))
    struct.pack_into('<I', comp, t.offset('hknpCompoundShape', 'estimatedNumShapeKeys'), len(boxes))
    bvd = t.offset('hknpCompoundShape', 'boundingVolumeData')
    tree_at = bvd+t.offset('hknpCompoundShapeData', 'simdTree')+t.offset('hkcdSimdTree', 'nodes')
    tree_template = t.u32(cat+tree_at)
    data, count = _tree(boxes)
    struct.pack_into('<I', comp, tree_at, edit.add(tree_template, data, count))
    return edit.add(template, comp)


@lru_cache(maxsize=32)
def build(game, key):
    """Return one complete .SHKT; all points relative to the unchanged full-model centre."""
    md = jet_models._model_of(game, key)
    centre = jet_models.model_box(game, key)[0]
    boxes = [(tuple(lo[k]-centre[k] for k in range(3)), tuple(hi[k]-centre[k] for k in range(3)))
             for lo, hi in mesh_cells(triangles(md))]
    e = _Edit(game.read('OBJECT', STOCK))
    t = e.tag
    template = bodies(t)['RagDollProxys.body']
    body = _compound(e, template, boxes, MAGIC)
    # Retain all stock proxy names and constraints for the 506's animation/death
    # code, but its helicopter blades must no longer collide outside the aircraft.
    lo, hi = max(boxes, key=lambda b: math.prod(b[1][k]-b[0][k] for k in range(3)))
    point = [(lo[k]+hi[k])/2 for k in range(3)]
    tiny = _compound(e, template, [(tuple(p-0.005 for p in point), tuple(p+0.005 for p in point))])
    scene = t.root_variant('hknpPhysicsSceneData')
    _, sat, ns = t.item(t.u32(scene+t.offset('hknpPhysicsSceneData', 'systemDatas')))
    B = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
    for i in range(ns):
        _, system, _ = t.item(t.u32(sat+4*i))
        _, at, n = t.item(t.u32(system+t.offset('hknpPhysicsSystemData', 'bodyCinfos')))
        for j in range(n):
            pos = at+j*t.size(B)
            name = t.cstr(t.u32(pos+t.offset(B, 'name')))
            struct.pack_into('<I', e.data, pos-t.data0+t.offset(B, 'shape'), body if name == 'RagDollProxys.body' else tiny)
    return e.finish()


def assets(game):
    return {name: build(game, key) for key, name in FILES.items()}


# The carrier's nacelles (src/nacelle_reach.h, jet_flight.cpp Thrusters): the collision is the grounded bind pose, its
# bottom the model origin's plane, but the plugin tilts the four nacelles about their X (HingePose: local Y' = co Y + si Z,
# Z' = -si Y + co Z) from 0 to -NACELLE_BACK; the front pair then reaches 5.2 m under that plane. Their lowest model y
# every NACELLE_STEP degrees, so the plugin keeps them over the ground under it.
NACELLE_MODEL = 'EDF6VC_CARRIER.MRAB'
NACELLE_STEP, NACELLE_BACK = 5, 110
NACELLES = {'front': 'boosterF_l', 'back': 'boosterB_l'}


def nacelle_lowest(game, key=NACELLE_MODEL):
    """{'front'|'back': [lowest model y at tilt 0, -5, ... -NACELLE_BACK degrees]} of the nacelle's skinned vertices."""
    md = jet_models._model_of(game, key)
    world = bind_world(md)
    out = {}
    for side, bone in NACELLES.items():
        i = next(k for k, b in enumerate(md.bones) if md.name_of(b.name) == bone)
        piv = world[i][12:15]
        rows = [world[i][4*r:4*r+3] for r in range(3)]
        pts = jet_models.bind_positions(md, {i})
        local = [[sum((p[c]-piv[c])*rows[r][c] for c in range(3)) for r in range(3)] for p in pts]
        lows = []
        for deg in range(0, -NACELLE_BACK-1, -NACELLE_STEP):
            a = math.radians(deg)
            co, si = math.cos(a), math.sin(a)
            y = [co*rows[1][k]+si*rows[2][k] for k in range(3)], [-si*rows[1][k]+co*rows[2][k] for k in range(3)]
            lows.append(min(piv[1]+d[0]*rows[0][1]+d[1]*y[0][1]+d[2]*y[1][1] for d in local))
        out[side] = lows
    return out
