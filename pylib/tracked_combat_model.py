"""Original procedural tracked-vehicle geometry on EDF6's unmodified V603 skeleton.

This is newly authored low-poly geometry, not a downloaded model or renamed stock
vehicle. Only the player's own V603 material/texture data is reused. Original
physical bone names, relative hierarchy, flags and bind matrices are preserved;
declared visual leaves are inserted in preorder with consistent index remapping.
Bounds enclose the new geometry. Per-vehicle collision is emitted separately by
tracked_collision; private CAS changes only eight radar-reference bytes. These
remain documented contact approximations, not measured M113/T-72 simulations.
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import dataclass, field, replace
import copy
from collections import Counter

import numpy as np

import cockpits
import graft_pure as g
import procmesh as pm
from mdb import Mdb, Mesh, Rab, bind_world, cmpl_compress, cmpl_decompress, inverse_affine, insert_bone, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import link

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


@dataclass(frozen=True)
class VisualBone:
    name: str
    parent: str
    origin: tuple[float, float, float]


def add_visual_bones(host: Mdb, additions: tuple[VisualBone, ...]) -> Mdb:
    """Insert visual-only leaves in native preorder; keep every source world bind.

    Native MDB subtree traversal requires each parent's descendants contiguous.
    insert_bone remaps later links, object roots and mesh blend indices together.
    The original names stay at the same string indices, and physics/CAS bind by
    name. No ragdoll proxy or existing native pose owner is repurposed.
    """
    md = copy.deepcopy(host)
    for extra in additions:
        require(md.bone_index(extra.name) < 0, f'duplicate visual bone {extra.name}')
        index = insert_bone(md, extra.name, extra.parent)
        bone = md.bones[index]
        parent = md.bone_index(extra.parent)
        world = g.translation(extra.origin)
        bone.kind = 3
        bone.bounded = 1
        bone.local = mmul(world, inverse_affine(bind_world(md)[parent]))
        bone.inv_bind = inverse_affine(world)
    return md


def check_skeleton(md: Mdb, additions: tuple[VisualBone, ...] = ()) -> None:
    """The only changes to native skeleton topology are declared visual leaves."""
    names = [md.name_of(b.name) for b in md.bones]
    stack: list[int] = []
    for index,bone in enumerate(md.bones):
        require(bone.index == index, 'bone indices do not match native file order')
        while stack and stack[-1] != bone.parent:
            stack.pop()
        require(bone.parent < 0 and not stack or bool(stack) and stack[-1] == bone.parent,
                'bone subtree is not contiguous native preorder')
        stack.append(index)
    require(len(names) == len(set(names)), 'duplicate skeleton bone names')
    extra_names = {extra.name for extra in additions}
    require(len(extra_names) == len(additions), 'duplicate visual bone declarations')
    world = bind_world(md)
    for extra in additions:
        index = md.bone_index(extra.name)
        require(index >= 0, f'missing visual bone {extra.name}')
        b = md.bones[index]
        require(0 <= b.parent < len(md.bones), f'{extra.name}: invalid visual parent')
        require(md.name_of(md.bones[b.parent].name) == extra.parent and b.kind == 3,
                f'{extra.name}: incorrect visual-bone parent or kind')
        require(not any(other.parent == index for other in md.bones), f'{extra.name}: not a visual leaf')
        require(max(abs(a-b) for a,b in zip(world[index][12:15],extra.origin)) < 1e-5,
                f'{extra.name}: rotor origin moved off its barrel axis')
    # the seats' eye / screen markers a finished model carries (cockpits.graft_files) are not the model's own
    native = [b for b in md.bones if md.name_of(b.name) not in extra_names and not cockpits.grafted_bone(md.name_of(b.name))]
    remap = {b.index: index for index,b in enumerate(native)}
    restored = [replace(b,index=remap[b.index],parent=remap[b.parent] if b.parent>=0 else -1) for b in native]
    link(restored)
    normalized = replace(md,bones=restored)
    require(skeleton_fingerprint(normalized) == STOCK_SKELETON_SHA256,
            'native skeleton names, binds or relative hierarchy changed')


@dataclass
class Assembly:
    """Named parts and authoritative barrel mouths, all in model bind space."""
    host: Mdb
    parts: dict[str, pm.Part] = field(default_factory=dict)
    muzzles: dict[str, dict] = field(default_factory=dict)
    wheels: dict[str, tuple[float, float, float]] = field(default_factory=dict)
    wheel_parts: dict[str, list[str]] = field(default_factory=dict)

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
    """A planar convex polygon, isolated vertices for hard armour edges (pylib/procmesh.py hard_polygon)."""
    pm.hard_polygon(part, points, outward, skin(part))


def hexahedron(part: pm.Part, lower, upper) -> None:
    """Four corners at each height, ordered cyclically in XZ; outward hard faces."""
    pm.hard_hexahedron(part, lower, upper, skin(part))


def box(part: pm.Part, centre, half) -> None:
    pm.hard_box(part, centre, half, skin(part))


def cylinder(part: pm.Part, start, end, radius: float, segments: int = 16,
             end_radius: float | None = None, cap: bool = True) -> None:
    """Tube with correct outward winding and flat caps; arbitrary axis (pylib/procmesh.py hard_cylinder)."""
    require(np.linalg.norm(np.asarray(end, dtype=float) - np.asarray(start, dtype=float)) > 1e-6, 'zero-length cylinder')
    pm.hard_cylinder(part, start, end, radius, skin(part), segments, end_radius, cap)


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
    are copied. The five-wheel M113 uses G as its rear idler; its outer half
    uses H's original bind so each half follows its own physical death body.
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
            split = m113 and letter == 'G'
            p = a.part(f'wheel_{letter}_{side}', bone, DARK_TINT)
            if split:
                outer_bone = f'tire_moveH_{side}'
                outer = a.part(f'wheel_G_outer_{side}', outer_bone, DARK_TINT)
                cylinder(p, (x-sign*.19, y, z), (x, y, z), r, 20)
                cylinder(outer, (x, y, z), (x+sign*.19, y, z), r, 20)
                a.wheel_parts[bone] = [f'wheel_G_{side}', f'wheel_G_outer_{side}']
            else:
                outer_bone = bone
                cylinder(p, (x-.19, y, z), (x+.19, y, z), r, 20)
                a.wheel_parts[bone] = [f'wheel_{letter}_{side}']
            hub = a.part(f'hub_{letter}_{side}', outer_bone)
            outside = x + sign*.205
            cylinder(hub, (outside-sign*.07, y, z), (outside, y, z), r*.71, 16)
            cylinder(hub, (outside, y, z), (outside+sign*.035, y, z), r*.24, 12)
            if not road:
                teeth = a.part(f'sprocket_{letter}_{side}', bone, METAL_TINT)
                outer_teeth = a.part(f'sprocket_G_outer_{side}', outer_bone, METAL_TINT) if split else None
                for k in range(16):
                    th = k*math.tau/16
                    if split:
                        box(teeth, (x-sign*.10, y+r*math.cos(th), z+r*math.sin(th)), (.10, .038, .040))
                        box(outer_teeth, (x+sign*.10, y+r*math.cos(th), z+r*math.sin(th)), (.10, .038, .040))
                    else:
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


def build_with_info(game, make_parts, designation: str, dimensions, camera, drive_scale=1.,
                    visual_bones: tuple[VisualBone, ...] = ()) -> tuple[bytes, Mdb, dict]:
    original, rab = stock(game)
    host = add_visual_bones(original, visual_bones) if visual_bones else original
    a = Assembly(host)
    make_parts(a)
    meshes = pack_meshes(host, rab, a.parts)
    md = g.recompute_bounds(replace(host, objects=[replace(host.objects[0], meshes=meshes)], buffer_order=None))
    check_skeleton(md, visual_bones)
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
            'stock_skeleton_unchanged': not visual_bones, 'native_bone_binds_unchanged': True,
            'visual_bones': [b.name for b in visual_bones], 'stock_meshes_retained': 0,
            'collision_contract': 'make_tracked_combat builds private fitted SHKT/bindings from these actual parts; native topology/controllers retained. In-game contact acceptance required.',
            'cas_contract': 'Private V603 CAS changes only eight radar T/R-reference bytes; all other tracks/channels/Fire/state bytes remain native; visual rotor poses before native all-bone world build',
            'camera': camera, 'drive_scale': float(drive_scale), 'muzzles': a.muzzles,
            'wheel_centres': a.wheels, 'parts': {}, 'triangles': sum(len(p.tris) for p in a.parts.values())}
    for name, p in a.parts.items():
        info['parts'][name] = {'bone': host.name_of(host.bones[p._rigid_bone].name), 'triangles': len(p.tris),
                               'bounds': [np.min(p.pos, axis=0).tolist(), np.max(p.pos, axis=0).tolist()]}
    check(arc, make_parts, original, visual_bones)
    return arc, md, info


def _triangle_keys(T: np.ndarray) -> list[tuple]:
    """Each triangle (n, 3, 3) as a key independent of which corner a mesh starts it on (its winding kept)."""
    keys = []
    for t in T:
        corners = [tuple(float(x) for x in c) for c in t]
        k = min(range(3), key=lambda i: corners[i])
        keys.append(tuple(corners[k:] + corners[:k]))
    return keys


def check(arc: bytes, make_parts, host: Mdb | None = None,
          visual_bones: tuple[VisualBone, ...] = ()) -> dict:
    rab = rab_read(arc)
    require(rab_write(rab) == arc, 'RAB write/read/write differs')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    require(mdb_write(md) == data, 'MDB write/read/write differs')
    check_skeleton(md, visual_bones)
    if host is not None:
        require(skeleton_fingerprint(host) == STOCK_SKELETON_SHA256, 'source native bone contract differs')
    a = Assembly(md)
    make_parts(a)
    require(len(md.objects) == 1, 'generated model must have one object')
    meshes = md.objects[0].meshes
    # What is drawn, by (bone, material), is the authored parts' triangles (winding kept), nothing more: an authored
    # model has a mesh a part, a finished one (model_lod.finish) its meshes merged by material and its seats' cockpits
    # (cockpits.graft_files: their own materials) beside them.
    want: dict[tuple[str, int], Counter] = {}
    drawn = {}
    for name, part in a.parts.items():
        require(0 <= part._rigid_bone < len(md.bones) and md.bones[part._rigid_bone].kind == 3,
                f'{name}: vertices do not use a skin bone')
        P = np.asarray(part.pos).astype('<f2').astype(float)
        require(np.isfinite(P).all(), f'{name}: nonfinite vertex')
        T = P[np.asarray(part.tris, dtype=int).reshape(-1, 3)]
        areas = np.linalg.norm(np.cross(T[:,1]-T[:,0], T[:,2]-T[:,0]), axis=1)
        require(bool((areas > 1e-9).all()), f'{name}: degenerate stored triangle')
        bone = md.name_of(md.bones[part._rigid_bone].name)
        want.setdefault((bone, part.material), Counter()).update(_triangle_keys(T))
        drawn[name] = {'bone': bone, 'triangles': len(part.tris)}
    got: dict[tuple[str, int], Counter] = {}
    for mesh in meshes:
        if cockpits.grafted_material(md.name_of(md.materials[mesh.material].name)):
            continue
        require(all(0 <= i < mesh.nverts for t in g.triangles(mesh) for i in t), f'mesh {mesh.mesh_index}: invalid index')
        stored = np.asarray(g.mesh_positions(mesh))
        require(np.isfinite(stored).all(), f'mesh {mesh.mesh_index}: nonfinite vertex')
        bi, bw = g.skin_columns(mesh)
        require(all(abs(w[0]-1) < 1e-6 and all(abs(x) < 1e-6 for x in w[1:]) for w in bw),
                f'mesh {mesh.mesh_index}: a vertex is not rigid')
        bones = np.asarray([int(i[0]) for i in bi])
        tris = np.asarray(list(g.triangles(mesh)), dtype=int).reshape(-1, 3)
        for b in np.unique(bones[tris[:, 0]]):
            sel = tris[bones[tris[:, 0]] == b]
            require(bool((bones[sel] == b).all()), f'mesh {mesh.mesh_index}: a triangle spans two bones')
            got.setdefault((cockpits.own_bone(md, int(b)), mesh.material), Counter()).update(
                _triangle_keys(stored[sel]))
    for key in sorted(set(want) | set(got)):
        require(want.get(key) == got.get(key),
                f'{key[0]} / {md.name_of(md.materials[key[1]].name)}: drawn triangles are not the authored parts')
    files = {f.name.lower() for f in rab.files}
    for tex in md.textures:
        require(tex.filename.lower() in files, f'missing texture {tex.filename}')
    for b, w in zip(md.bones, bind_world(md)):
        ident = np.asarray(w).reshape(4,4) @ np.asarray(b.inv_bind).reshape(4,4)
        require(np.max(np.abs(ident-np.eye(4))) < 2e-5, f'{md.name_of(b.name)}: inverse bind differs')
    for name, wheel in a.wheels.items():
        names = a.wheel_parts.get(name, [f'wheel_{name[9]}_{name[-1]}'])
        points = np.vstack([a.parts[n].pos for n in names])
        mid = (points.min(axis=0)+points.max(axis=0))/2
        require(max(abs(mid[i]-wheel[i]) for i in range(3)) < 1e-6, f'{name}: complete wheel not at pivot')
        if len(names) == 2:
            require(a.parts[names[0]]._rigid_bone == a.bone(name), f'{name}: inner half uses another bone')
            require(a.parts[names[1]]._rigid_bone == a.bone(f'tire_moveH_{name[-1]}'),
                    f'{name}: outer half lost its death-physics bone')
    return {'bones': len(md.bones), 'skeleton_fingerprint': skeleton_fingerprint(md),
            'meshes': len(meshes), 'triangles': sum(x['triangles'] for x in drawn.values()),
            'stock_meshes_retained': 0, 'parts': drawn, 'muzzles': a.muzzles}


def animation(game, _md=None) -> bytes:
    """Private CAS: fix only the two donor radar T/R references at bind.

    Each original radar bone carries a real optic. Both are children of main,
    so bind locals follow turret pose alive and their own proxies drive death.
    strip_bone changes only four int16 channel references: no shared channel,
    clip duration/state, track list, firing recoil key or other bone is changed.
    """
    import cas_pose
    data = game.read('OBJECT', HOST_CAS)
    for name in ('doppler_radar', 'tracking_radar'):
        data = cas_pose.strip_bone(data, {'doppler_radar_loop'}, name)
    return data


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
