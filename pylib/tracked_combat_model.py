"""Original procedural tracked-vehicle geometry on EDF6's unmodified V603 skeleton.

This is newly authored low-poly geometry, not a downloaded model or renamed stock
vehicle. Only the player's own V603 material/texture data is reused. Bone order,
names, hierarchy, flags, local matrices and inverse binds are kept byte-for-byte;
only bounds change to enclose the new geometry. Consequently the stock CAS and
ragdoll remain compatible, but the stock collision hull is an approximation of
these vehicles and is not a validated M113/T-72 collision model.
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import dataclass, field, replace

import numpy as np

import graft_pure as g
import procmesh as pm
from mdb import Mdb, Mesh, Rab, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, rab_read, rab_write

HOST_ARC, HOST_MDB = 'V603_FLAK.MRAB', 'v603_flak.mdb'
HOST_CAS = 'V603_FLAK.CAS'
# Steam EDF6 V603 exact bone names/links/flags and 32 matrix floats per bone.
STOCK_SKELETON_SHA256 = 'aef0e6d74c4ea9e2ae599f48db0fffb6fde647f96a50c761a756efc0081e5ff1'
BODY_MATERIAL = 'v603_body'
BODY_TINT = (77, 88, 54)
DARK_TINT = (30, 33, 29)
METAL_TINT = (104, 104, 91)


class TrackedCombatCheckError(ValueError):
    pass


def require(ok: bool, message: str) -> None:
    if not ok:
        raise TrackedCombatCheckError(message)


def member(rab: Rab, name: str):
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    require(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def stock(game) -> tuple[Mdb, Rab]:
    rab = rab_read(game.read('OBJECT', HOST_ARC))
    md = mdb_read(member(rab, HOST_MDB).data)
    require(len(md.objects) == 1, 'V603 must have one skinned object')
    require(skeleton_fingerprint(md) == STOCK_SKELETON_SHA256, 'unsupported V603 skeleton')
    return md, rab


def skeleton_fingerprint(md: Mdb) -> str:
    """All skeleton data except the explicitly recomputed render bounds."""
    h = hashlib.sha256()
    for b in md.bones:
        h.update(md.name_of(b.name).encode('utf-8') + b'\0')
        h.update(struct.pack('<11i', b.index, b.parent, b.sibling, b.child, b.name, b.child_count,
                             b.kind, b.depth_delta, b.bounded, b.pad, b.unk1c))
        h.update(struct.pack('<32f', *b.local, *b.inv_bind))
    return h.hexdigest()


@dataclass
class Assembly:
    """Named parts and authoritative barrel mouths, all in model bind space."""
    host: Mdb
    parts: dict[str, pm.Part] = field(default_factory=dict)
    muzzles: dict[str, dict] = field(default_factory=dict)
    wheels: dict[str, tuple[float, float, float]] = field(default_factory=dict)

    def bone(self, name: str) -> int:
        result = self.host.bone_index(name)
        require(result >= 0, f'missing host bone {name}')
        return result

    def origin(self, name: str) -> tuple[float, float, float]:
        return tuple(bind_world(self.host)[self.bone(name)][12:15])

    def part(self, name: str, bone: str, tint=BODY_TINT, material: str = BODY_MATERIAL) -> pm.Part:
        require(name not in self.parts, f'duplicate part {name}')
        mat = next((m.index for m in self.host.materials if self.host.name_of(m.name) == material), -1)
        require(mat >= 0, f'missing material {material}')
        p = pm.Part(mat, tint)
        # One bone per part: the packer and check() verify this remains rigid.
        p._rigid_bone = self.bone(bone)
        self.parts[name] = p
        return p

    def muzzle(self, name: str, bone: str, position, bore: float) -> None:
        b = self.host.bones[self.bone(bone)]
        self.muzzles[name] = {'bone': bone, 'model': tuple(float(v) for v in position),
                              'local': g.xform(position, b.inv_bind), 'bore': float(bore)}


def skin(part: pm.Part):
    return [(part._rigid_bone, 1.0)]


def polygon(part: pm.Part, points, outward) -> None:
    """A planar convex polygon, isolated vertices for hard armour edges."""
    start = len(part.pos)
    centre = np.mean(points, axis=0)
    normal = np.cross(np.asarray(points[1]) - points[0], np.asarray(points[2]) - points[0])
    order = list(range(len(points)))
    if np.dot(normal, outward) < 0:
        order.reverse()
    for k in order:
        p = points[k]
        part.add(p, skin(part), (0.5 + 0.1 * (p[0] - centre[0]), 0.5 + 0.1 * (p[2] - centre[2])))
    part.tris.extend((start, start + i, start + i + 1) for i in range(1, len(points) - 1))


def hexahedron(part: pm.Part, lower, upper) -> None:
    """Four corners at each height, ordered cyclically in XZ; outward hard faces."""
    points = list(lower) + list(upper)
    centre = np.mean(points, axis=0)
    faces = ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    for face in faces:
        p = [np.asarray(points[k], dtype=float) for k in face]
        polygon(part, p, np.mean(p, axis=0) - centre)


def box(part: pm.Part, centre, half) -> None:
    x, y, z = centre
    a, b, c = half
    hexahedron(part, [(x-a, y-b, z-c), (x+a, y-b, z-c), (x+a, y-b, z+c), (x-a, y-b, z+c)],
               [(x-a, y+b, z-c), (x+a, y+b, z-c), (x+a, y+b, z+c), (x-a, y+b, z+c)])


def cylinder(part: pm.Part, start, end, radius: float, segments: int = 16,
             end_radius: float | None = None, cap: bool = True) -> None:
    """Tube with correct outward winding and flat caps; arbitrary axis."""
    a, b = np.asarray(start, dtype=float), np.asarray(end, dtype=float)
    axis = b - a
    require(np.linalg.norm(axis) > 1e-6, 'zero-length cylinder')
    axis /= np.linalg.norm(axis)
    ref = np.array((0., 1., 0.)) if abs(axis[1]) < .9 else np.array((1., 0., 0.))
    u = np.cross(axis, ref)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)
    r1 = radius if end_radius is None else end_radius
    rows = []
    for centre, r, t in ((a, radius, 0.), (b, r1, 1.)):
        rows.append([part.add(centre + r * (u*math.cos(k*math.tau/segments) + v*math.sin(k*math.tau/segments)),
                              skin(part), (k / segments, t)) for k in range(segments)])
    for k in range(segments):
        j = (k+1) % segments
        part.tris.extend(((rows[0][k], rows[0][j], rows[1][j]), (rows[0][k], rows[1][j], rows[1][k])))
    if cap:
        for centre, row, wanted in ((a, rows[0], -axis), (b, rows[1], axis)):
            polygon(part, [part.pos[i] for i in row], wanted)


def barrel(part: pm.Part, centre, z0: float, z1: float, radius: float, bore: float) -> None:
    """Hollow front mouth, not a solid rod; bore expressed as diameter."""
    x, y = centre
    cylinder(part, (x, y, z0), (x, y, z1), radius, 16, cap=False)
    inner_start = len(part.tris)
    cylinder(part, (x, y, z1-.12), (x, y, z1), bore / 2, 16, cap=False)
    part.tris[inner_start:] = [(a, c, b) for a, b, c in part.tris[inner_start:]]
    # Outer/front and inner/front vertices share no face with their walls: a crisp muzzle rim.
    ring0, ring1 = [], []
    for r, ring in ((radius, ring0), (bore / 2, ring1)):
        for k in range(16):
            theta = k * math.tau / 16
            ring.append(part.add((x+r*math.cos(theta), y+r*math.sin(theta), z1), skin(part), (k/16, .5)))
    for k in range(16):
        j = (k+1) % 16
        part.tris.extend(((ring0[k], ring0[j], ring1[j]), (ring0[k], ring1[j], ring1[k])))


def rail(part: pm.Part, start, end, radius: float = .018) -> None:
    cylinder(part, start, end, radius, 8)


def chassis(a: Assembly, road_letters: str, length: float, width: float, top: float, m113: bool) -> None:
    """New M113 or T-72 hull, tracks and independently skinned road wheels.

    Every wheel's centre is the original bone pivot. No original mesh triangles
    are copied. The five-wheel M113 uses G as its rear idler (H is unused).
    """
    body = a.part('hull_armour', 'body')
    z0, z1 = (-2.18, 2.68) if m113 else (-length / 2, length / 2)
    lower_y = .40
    hw = width / 2
    if m113:
        hexahedron(body, [(-1.03, lower_y, z0+.20), (1.03, lower_y, z0+.20),
                          (1.03, lower_y, z1-.50), (-1.03, lower_y, z1-.50)],
                   [(-1.17, top, z0), (1.17, top, z0), (1.17, top, z1-.42), (-1.17, top, z1-.42)])
        # Recognisable M113 high, nearly vertical side armour with sloped bow.
        hexahedron(body, [(-1.17, .76, z0), (1.17, .76, z0), (1.17, .76, z1), (-1.17, .76, z1)],
                   [(-1.17, top, z0), (1.17, top, z0), (1.17, top, z1-.42), (-1.17, top, z1-.42)])
        rear = a.part('rear_ramp', 'body', METAL_TINT)
        box(rear, (0, 1.04, z0-.014), (.81, .44, .025))
        for x in (-.86, .86):
            box(rear, (x, 1.05, z0-.045), (.025, .48, .035))
        hatch = a.part('driver_hatch_and_engine_louvres', 'body', DARK_TINT)
        cylinder(hatch, (-.70, top+.015, 1.55), (-.70, top+.06, 1.55), .31, 20)
        for z in np.linspace(.92, 1.90, 9):
            box(hatch, (.49, top+.015, z), (.37, .015, .025))
    else:
        # Low T-72 glacis and engine deck, long overhangs and distinct turret ring.
        hexahedron(body, [(-1.21, lower_y, z0+.28), (1.21, lower_y, z0+.28),
                          (1.21, lower_y, z1-.60), (-1.21, lower_y, z1-.60)],
                   [(-1.47, top, z0), (1.47, top, z0), (1.47, top-.12, z1), (-1.47, top-.12, z1)])
        deck = a.part('engine_deck', 'body', DARK_TINT)
        for z in np.linspace(z0+.30, -.92, 13):
            box(deck, (0, top+.025, z), (.94, .025, .036))
        # Modular ERA on the bow and armoured side skirts identify the T-72 chassis.
        era = a.part('reactive_armour_blocks', 'body', (92, 100, 64))
        for x in np.linspace(-1.15, 1.15, 7):
            for z in (2.0, 2.42, 2.84):
                box(era, (x, top-.005, z), (.145, .095, .18))
        for s in (-1, 1):
            for z in np.linspace(-2.76, 2.62, 9):
                box(era, (s*(hw-.065), 1.04, z), (.065, .34, .28))
    fenders = a.part('fenders_and_stowage', 'body')
    for s in (-1, 1):
        box(fenders, (s*(hw-.24), .91 if m113 else 1.22, (z0+z1)/2), (.24, .055, (z1-z0)/2))
        if not m113:
            for z in (-2.42, -1.46):
                box(fenders, (s*1.50, 1.42, z), (.23, .16, .38))
    # The raised drive wheel is A; rear M113 idler uses G and BMPT uses H.
    for side, sign in (('l', 1), ('r', -1)):
        wheel_letters = 'A'+road_letters+('G' if m113 else 'H')
        for letter in wheel_letters:
            bone = f'tire_move{letter}_{side}'
            x, y, z = a.origin(bone)
            road = letter in road_letters
            r = .355 if road else (.355 if m113 and letter == 'G' else .43)
            p = a.part(f'wheel_{letter}_{side}', bone, DARK_TINT)
            cylinder(p, (x-.19, y, z), (x+.19, y, z), r, 20)
            hub = a.part(f'hub_{letter}_{side}', bone)
            outside = x + sign*.205
            cylinder(hub, (outside-sign*.07, y, z), (outside, y, z), r*.71, 16)
            cylinder(hub, (outside, y, z), (outside+sign*.035, y, z), r*.24, 12)
            if not road:
                teeth = a.part(f'sprocket_{letter}_{side}', bone, METAL_TINT)
                for k in range(16):
                    th = k*math.tau/16
                    box(teeth, (x, y+r*math.cos(th), z+r*math.sin(th)), (.20, .038, .040))
            a.wheels[bone] = (x, y, z)
        # A closed belt of individually authored shoes around the stock pivots.
        # It is skinned to catapi_body; wheel rotation uses the separate wheel bones.
        x = sign*1.226
        rear = a.origin(f'tire_move{wheel_letters[-1]}_{side}')
        front = a.origin(f'tire_moveA_{side}')
        rear_z, front_z = rear[2], front[2]
        rear_y, front_y = rear[1], front[1]
        rear_r, front_r = (.405 if m113 else .48), .48
        contour = []
        # Capsule path runs clockwise in yz, with a sloped top/bottom for the M113 idler.
        for centre_z, centre_y, radius, begin in ((front_z, front_y, front_r, -math.pi/2),
                                                 (rear_z, rear_y, rear_r, math.pi/2)):
            for k in range(15):
                th = begin+k*math.pi/14
                contour.append((centre_y+radius*math.sin(th), centre_z+radius*math.cos(th)))
        path = []
        for i, pa in enumerate(contour):
            pb = contour[(i+1) % len(contour)]
            count = max(1, math.ceil(math.dist(pa, pb)/.15))
            path.extend((pa[0]+(pb[0]-pa[0])*j/count, pa[1]+(pb[1]-pa[1])*j/count) for j in range(count))
        material = f'v603_kyata_{side.upper()}'
        track = a.part(f'track_belt_{side}', 'catapi_body', DARK_TINT, material)
        for i, pa in enumerate(path):
            pb = path[(i+1) % len(path)]
            d = np.asarray((pb[0]-pa[0], pb[1]-pa[1]))
            d /= np.linalg.norm(d)
            n = np.asarray((-d[1], d[0]))*.036
            mid = (np.asarray(pa)+pb)/2
            reach = min(.072, math.dist(pa, pb)*.46)
            q = [mid-d*reach-n, mid+d*reach-n, mid+d*reach+n, mid-d*reach+n]
            hexahedron(track, [(x-.245, p[0], p[1]) for p in q], [(x+.245, p[0], p[1]) for p in q])


def pack_meshes(host: Mdb, rab: Rab, parts: dict[str, pm.Part]) -> list[Mesh]:
    albedos = pm.albedos(rab, host)
    boxes = {}
    out = []
    for name, part in parts.items():
        require(part.pos and part.tris, f'{name}: empty geometry')
        require(len(part.pos) < 65536, f'{name}: vertex count exceeds u16')
        tmpl = next(me for ob in host.objects for me in ob.meshes if me.material == part.material)
        key = part.tint
        if key not in boxes:
            boxes[key] = pm.uv_boxes(host, albedos, key)
        vdata, indices = pm.pack(part, tmpl, boxes[key], 1.)
        flags = bytes((tmpl.flags[0], 1, 1, tmpl.flags[3]))
        out.append(replace(tmpl, flags=flags, mesh_index=len(out), vdata=vdata, indices=indices))
    return out


def build_with_info(game, make_parts, designation: str, dimensions, camera, drive_scale=1.) -> tuple[bytes, Mdb, dict]:
    host, rab = stock(game)
    a = Assembly(host)
    make_parts(a)
    meshes = pack_meshes(host, rab, a.parts)
    md = g.recompute_bounds(replace(host, objects=[replace(host.objects[0], meshes=meshes)], buffer_order=None))
    require(skeleton_fingerprint(md) == skeleton_fingerprint(host), 'host skeleton was changed')
    data = mdb_write(md)
    require(mdb_write(mdb_read(data)) == data, 'MDB write/read/write differs')
    packed = cmpl_compress(data)
    require(cmpl_decompress(packed) == data, 'CMPL compression differs')
    member(rab, HOST_MDB).stored = packed
    g.prune_members(rab, [HOST_MDB])
    arc = rab_write(rab)
    bounds = [np.min(np.vstack([p.pos for p in a.parts.values()]), axis=0).tolist(),
              np.max(np.vstack([p.pos for p in a.parts.values()]), axis=0).tolist()]
    info = {'designation': designation, 'source': 'original procedural geometry; not downloaded or stock vehicle mesh',
            'dimension_target_m': list(dimensions), 'render_bounds': bounds,
            'skeleton_fingerprint': skeleton_fingerprint(host), 'bone_count': len(md.bones),
            'stock_skeleton_unchanged': True, 'stock_meshes_retained': 0,
            'collision_contract': 'Unmodified V603 ragdoll and skeleton; visual hull differs. In-game collision acceptance required.',
            'cas_contract': 'V603_FLAK.CAS copied unchanged; no inserted or moved bones',
            'camera': camera, 'drive_scale': float(drive_scale), 'muzzles': a.muzzles,
            'wheel_centres': a.wheels, 'parts': {}, 'triangles': sum(len(p.tris) for p in a.parts.values())}
    for name, p in a.parts.items():
        info['parts'][name] = {'bone': host.name_of(host.bones[p._rigid_bone].name), 'triangles': len(p.tris),
                               'bounds': [np.min(p.pos, axis=0).tolist(), np.max(p.pos, axis=0).tolist()]}
    check(arc, make_parts, host)
    return arc, md, info


def check(arc: bytes, make_parts, host: Mdb | None = None) -> dict:
    rab = rab_read(arc)
    require(rab_write(rab) == arc, 'RAB write/read/write differs')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    require(mdb_write(md) == data, 'MDB write/read/write differs')
    require(skeleton_fingerprint(md) == STOCK_SKELETON_SHA256, 'stock skeleton name/link/matrix contract differs')
    if host is not None:
        require(skeleton_fingerprint(md) == skeleton_fingerprint(host), 'host bone contract differs')
    a = Assembly(md)
    make_parts(a)
    require(len(md.objects) == 1, 'generated model must have one object')
    meshes = md.objects[0].meshes
    require(len(meshes) == len(a.parts), 'stock/residual/missing meshes present')
    drawn = {}
    for mesh, (name, part) in zip(meshes, a.parts.items()):
        require(mesh.mesh_index == len(drawn), f'{name}: mesh order differs')
        require(mesh.material == part.material, f'{name}: material differs')
        require(mesh.nverts == len(part.pos) and len(mesh.indices) == len(part.tris)*6, f'{name}: geometry size differs')
        stored = np.asarray(g.mesh_positions(mesh))
        expected = np.asarray(part.pos).astype('<f2').astype(float)
        require(np.array_equal(stored, expected), f'{name}: geometry is not the authored part')
        require(mesh.indices == np.asarray(part.tris, dtype='<u2').tobytes(), f'{name}: triangles differ')
        require(np.isfinite(stored).all(), f'{name}: nonfinite vertex')
        require(all(0 <= i < mesh.nverts for t in g.triangles(mesh) for i in t), f'{name}: invalid index')
        bi, bw = g.skin_columns(mesh)
        require(all(int(i[0]) == part._rigid_bone and abs(w[0]-1) < 1e-6 and
                    all(abs(x) < 1e-6 for x in w[1:]) for i, w in zip(bi, bw)), f'{name}: incorrect rigid skin')
        require(0 <= part._rigid_bone < len(md.bones) and md.bones[part._rigid_bone].kind == 3,
                f'{name}: vertices do not use a skin bone')
        T = stored[np.asarray(list(g.triangles(mesh)))]
        areas = np.linalg.norm(np.cross(T[:,1]-T[:,0], T[:,2]-T[:,0]), axis=1)
        require(bool((areas > 1e-9).all()), f'{name}: degenerate stored triangle')
        drawn[name] = {'bone': md.name_of(md.bones[part._rigid_bone].name), 'triangles': len(part.tris)}
    files = {f.name.lower() for f in rab.files}
    for tex in md.textures:
        require(tex.filename.lower() in files, f'missing texture {tex.filename}')
    for b, w in zip(md.bones, bind_world(md)):
        ident = np.asarray(w).reshape(4,4) @ np.asarray(b.inv_bind).reshape(4,4)
        require(np.max(np.abs(ident-np.eye(4))) < 2e-5, f'{md.name_of(b.name)}: inverse bind differs')
    for name, wheel in a.wheels.items():
        p = a.parts[f'wheel_{name[9]}_{name[-1]}']
        mid = (np.min(p.pos,axis=0)+np.max(p.pos,axis=0))/2
        require(max(abs(mid[i]-wheel[i]) for i in range(3)) < 1e-6, f'{name}: wheel not at pivot')
    return {'bones': len(md.bones), 'skeleton_fingerprint': skeleton_fingerprint(md),
            'meshes': len(meshes), 'triangles': sum(x['triangles'] for x in drawn.values()),
            'stock_meshes_retained': 0, 'parts': drawn, 'muzzles': a.muzzles}


def animation(game, _md=None) -> bytes:
    """No retargeting: all original bone matrices are unchanged."""
    return game.read('OBJECT', HOST_CAS)


def dump_obj(md: Mdb, path: str) -> None:
    """Geometry-only bind pose for reviewing the original authored model."""
    base = 1
    with open(path, 'w', encoding='utf-8') as f:
        for k, me in enumerate(md.objects[0].meshes):
            f.write(f'o part_{k}\n')
            for x, y, z in g.mesh_positions(me):
                f.write(f'v {x:.6f} {y:.6f} {z:.6f}\n')
            for a, b, c in g.triangles(me):
                f.write(f'f {a+base} {b+base} {c+base}\n')
            base += me.nverts
