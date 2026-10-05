"""EDF6VC_DRILL.MRAB: the drill tank (EDF: Iron Rain's Tank_C) on the Blacker's skeleton, built from the user's OBJ and
the player's own Root.cpk (pure Python: pylib/obj_model.py, graft_pure, mdb; no numpy / PIL).

The model: the stock Blacker archive (OBJECT/V505_TANK.MRAB, v505_tank.mdb, kept as the model file name: the stock
Vehicle505_Tank class and V505_TANK.SGO name it, with its ragdoll shapes, CAS clips and constraints, all by bone name)
with every stock mesh taken out and the OBJ in their place:
  - the OBJ (MODEL_SUBDIR/OBJ_FILE, one object, two materials) turned from its +X-forward axes into the game's +Z
    forward (AXES), scaled by SCALE and moved OFFSET_Z forward: 13.4 m long, 4.8 m wide and 5.7 m tall in Iron Rain,
    its hull (7.5 m long) comes to 6.0 x 3.8 x 4.6 m, over the Blacker's 6.8 x 3.2 m hull and collision shapes (the
    ragdoll's are the physics: a hull much bigger than them would sink into walls it cannot touch), the drill 3.8 m
    long in front of it (docs/drill-re.md §1);
  - the hull rigidly on the Blacker's `body` bone, the drill (every triangle past DRILL_SPLIT_Z) on a new bone
    DRILL_BONE, a child of `body`, its origin on the drill's axis at its base and its axes the model's, so the plugin
    (src/drill.cpp) spins it by turning its local matrix about Z. The bone is inserted at the end of `body`'s subtree:
    the object bones after it move one index on (no stock data refers to bones by index: the SGO, CAS, ragdoll and
    constraints name them);
  - two materials made from the Blacker's hull material (its shader and parameters): MI_Tank_C with the OBJ's
    Tank_C_BC.png, MI_Tank_B_CS (the tracks and running gear, whose texture is not in the OBJ's folder) a plain dark
    steel; their normal maps flat, their roughness / metal / occlusion maps one neutral value.
The track object (Caterpi) is left out; its materials stay (the SGO's tank_caterpillar_animation names them).

The OBJ is the user's (a ripped game asset, never in the repository): model_dir() finds it where obj_model.model_dir
looks ($EDF6VC_MODELS, `models` next to the installer, the developer's folder).
"""
from __future__ import annotations

import math
import os
import struct
from dataclasses import replace

import graft_pure as g
import obj_model as om
import texfile
from mdb import Bone, Mdb, bind_world, cmpl_compress, cmpl_decompress, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import link

HOST_ARC, HOST_MDB = 'V505_TANK.MRAB', 'v505_tank.mdb'
OUT_ARC = 'EDF6VC_DRILL.MRAB'
MODEL_SUBDIR, OBJ_FILE = 'drill_tank', 'drill_tank.obj'
TEXTURE_FILES = {'MI_Tank_C': 'Tank_C_BC.png'}        # the OBJ's materials' textures in its folder (the MTL has no map_Kd)
SOLID = {'MI_Tank_B_CS': (62, 64, 60)}                  # ...and the one without a texture: dark steel
FLAT_NORMAL, NEUTRAL_RMO = (128, 128, 255), (150, 110, 255)   # tangent-space up; roughness 0.6, metal 0.43, no occlusion
TEMPLATE_MATERIAL = 'v505_tank'
TEX_STEM = {'MI_Tank_C': 'edf6vc_drill_c', 'MI_Tank_B_CS': 'edf6vc_drill_b'}
NORMAL_TEX, RMO_TEX = 'edf6vc_drill_n.dds', 'edf6vc_drill_rmo.dds'
# OBJ (+X forward, +Y up) -> game (+Z forward, +Y up, +X left): x -> z, y -> y, z -> -x (a rotation, no mirror)
AXES: tuple[om.Vec3, om.Vec3, om.Vec3] = ((0.0, 0.0, 1.0), (0.0, 1.0, 0.0), (-1.0, 0.0, 0.0))
SCALE = 0.8
OFFSET_Z = 0.25
CONVERSION = om.Conversion(AXES, SCALE, (0.0, 0.0, OFFSET_Z))
DRILL_SPLIT_Z = 4.5 * SCALE + OFFSET_Z      # the OBJ has no geometry between its hull (x <= 3.6) and its drill (x >= 4.7)
DRILL_BONE, DRILL_PARENT = 'edf6vc_drill', 'body'
# The drill's length and base radius as built (src/drill.cpp kDrillLength / kDrillRadius; tools/selftest.py holds them
# equal; check() holds the geometry to them).
DRILL_LENGTH, DRILL_RADIUS = 3.77, 0.97
# Its base in the model (= the vehicle's frame: x, y up, z forward; src/drill.cpp kDrillBaseY / kDrillBaseZ, the
# contact probes' axis) and its rotational repeat: the mesh maps onto itself turned 1/DRILL_FOLDS of a turn (its
# flutes; src/drill.cpp kSpinRepeat caps the drawn turn a frame under it, docs/drill-re.md §4).
DRILL_BASE = (0.0, 3.37, 4.19)
DRILL_FOLDS = 16


class DrillModelError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise DrillModelError(msg)


def model_dir() -> str | None:
    """The folder that holds MODEL_SUBDIR (obj_model.model_dir: $EDF6VC_MODELS, `models` next to the installer, the
    developer's folder), None when no root has it."""
    d = om.model_dir(MODEL_SUBDIR)
    return os.path.dirname(d) if d else None


def obj_path(models: str | None = None) -> str | None:
    """The drill tank's OBJ, or None when it is not there."""
    d = models or model_dir()
    p = os.path.join(d, MODEL_SUBDIR, OBJ_FILE) if d else None
    return p if p and os.path.isfile(p) else None


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hit = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hit) == 1, f'{name}: {len(hit)} archive members')
    return hit[0]


# ------------------------------------------------------------------------------------------ geometry

def split(parts: list[om.Part]) -> tuple[list[om.Part], list[om.Part]]:
    """(hull parts, drill parts): a triangle with every corner past DRILL_SPLIT_Z is the drill's."""
    hull, drill = [], []
    for p in parts:
        cut = om.split_part(p, lambda t: all(v[2] > DRILL_SPLIT_Z for v in t))
        if False in cut:
            hull.append(cut[False])
        if True in cut:
            drill.append(cut[True])
    return hull, drill


def repeat_share(points: list[om.Vec3], base: om.Vec3, folds: int, tol: float = 0.03) -> float:
    """The share of `points` that land on one of them (within `tol` m) turned 1/folds of a turn about the +Z axis
    through `base`: 1.0 for a mesh that repeats every 1/folds turn."""
    cell: dict[tuple[int, int, int], list[om.Vec3]] = {}
    key = lambda p: (round(p[0] / tol), round(p[1] / tol), round(p[2] / tol))  # noqa: E731
    for p in points:
        cell.setdefault(key(p), []).append(p)
    a = 2.0 * math.pi / folds
    c, s = math.cos(a), math.sin(a)
    hits = 0
    for p in points:
        x, y = p[0] - base[0], p[1] - base[1]
        q = (base[0] + c * x - s * y, base[1] + s * x + c * y, p[2])
        k = key(q)
        near = (o for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)
                for o in cell.get((k[0] + dx, k[1] + dy, k[2] + dz), ()))
        hits += any(math.dist(o, q) < tol for o in near)
    return hits / len(points) if points else 0.0


def drill_axis(drill: list[om.Part]) -> tuple[om.Vec3, float, float]:
    """(base: the axis at the drill's back end, length, base radius) of the drill's vertices: the axis along +Z
    through the middle of their x / y extent."""
    P = [v.pos for p in drill for v in p.verts]
    cx = (min(p[0] for p in P) + max(p[0] for p in P)) / 2
    cy = (min(p[1] for p in P) + max(p[1] for p in P)) / 2
    z0, z1 = min(p[2] for p in P), max(p[2] for p in P)
    r = max(math.hypot(p[0] - cx, p[1] - cy) for p in P)
    return (cx, cy, z0), z1 - z0, r


# ------------------------------------------------------------------------------------------ skeleton

def insert_bone(md: Mdb, parent: str, name: str, origin: om.Vec3) -> tuple[Mdb, int]:
    """`md` with a skin bone `name` (kind 3, bounded) under `parent` at model-space `origin` (axes the model's),
    placed at the end of the parent's subtree (preorder kept); every later bone, parent link and object bone shifted.
    Returns (model, its index)."""
    pi = md.bone_index(parent)
    _req(pi >= 0, f'no bone {parent}')
    at = max(g.subtree(md, pi)) + 1
    shift = lambda i: i + 1 if i >= at else i  # noqa: E731
    names = list(md.names) + [name]
    w = bind_world(md)
    world = g.translation(origin)
    bones = []
    for b in md.bones:
        bones.append(replace(b, index=shift(b.index), parent=shift(b.parent) if b.parent >= 0 else -1))
    bones.insert(at, Bone(at, pi, -1, -1, len(names) - 1, 0, 3, 0, 1, 0, 0,
                          mmul(world, inverse_affine(w[pi])), g.translation((-origin[0], -origin[1], -origin[2])),
                          [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    link(bones)
    objects = [replace(o, bone=shift(o.bone)) for o in md.objects]
    return replace(md, names=names, bones=bones, objects=objects), at


# ------------------------------------------------------------------------------------------ build

def textures(obj: om.ObjFile) -> dict[str, bytes]:
    """The archive's new texture files {name: DDS}: each OBJ material's albedo, the flat normal, the neutral RMO."""
    folder = os.path.dirname(obj.path)
    out = {NORMAL_TEX: texfile.solid_dxt1(FLAT_NORMAL), RMO_TEX: texfile.solid_dxt1(NEUTRAL_RMO)}
    for mat, stem in TEX_STEM.items():
        if mat in TEXTURE_FILES:
            out[f'{stem}.dds'] = om.texture_dds(os.path.join(folder, TEXTURE_FILES[mat]))
        else:
            out[f'{stem}.dds'] = texfile.solid_dxt1(SOLID[mat])
    return out


def build_model(game, obj_file: str) -> tuple[Mdb, object, dict[str, bytes], dict]:  # noqa: ANN001 - rootcpk.Game
    """(model, host Rab, new textures, info)."""
    rab = rab_read(game.read('OBJECT', HOST_ARC))
    host = mdb_read(member(rab, HOST_MDB).data)
    obj = om.read_obj(obj_file)
    _req(set(obj.materials) >= set(TEX_STEM), f'{obj_file}: materials {sorted(obj.materials)}, want {sorted(TEX_STEM)}')
    for mat, f in TEXTURE_FILES.items():
        _req(os.path.isfile(os.path.join(os.path.dirname(obj_file), f)), f'{mat}: {f} not next to {obj_file}')
    hull, drill = split(om.obj_parts(obj, CONVERSION))
    _req(bool(hull) and bool(drill), 'the OBJ has no drill past DRILL_SPLIT_Z (or no hull)')
    base, length, radius = drill_axis(drill)
    _req(abs(length - DRILL_LENGTH) < 0.05 and abs(radius - DRILL_RADIUS) < 0.05,
         f'drill {length:.3f} m long, {radius:.3f} m base radius: DRILL_LENGTH / DRILL_RADIUS are {DRILL_LENGTH} / {DRILL_RADIUS}')
    _req(max(abs(a - b) for a, b in zip(base, DRILL_BASE)) < 0.02, f'drill base {base}: DRILL_BASE is {DRILL_BASE}')
    share = repeat_share([v.pos for p in drill for v in p.verts], base, DRILL_FOLDS)
    _req(share > 0.99, f'the drill repeats every 1/{DRILL_FOLDS} turn for {share:.3f} of its vertices: DRILL_FOLDS is off')
    md = replace(host, objects=[o for o in host.objects if host.name_of(o.name) == 'v505_tank'], buffer_order=None)
    _req(len(md.objects) == 1, 'the Blacker model has no v505_tank object')
    md, drill_bone = insert_bone(md, DRILL_PARENT, DRILL_BONE, base)
    body = md.bone_index(DRILL_PARENT)
    template = next(me for o in host.objects for me in o.meshes if host.name_of(host.materials[me.material].name) == TEMPLATE_MATERIAL)
    meshes = []
    for mat, stem in TEX_STEM.items():
        md, mi = om.add_material(md, host, TEMPLATE_MATERIAL, stem,
                                 {'albedo': f'{stem}.dds', 'normal': NORMAL_TEX, 'param_r_m_occ_hr': RMO_TEX})
        pieces = [(p, om.rigid(p, body)) for p in hull if p.material == mat]
        pieces += [(p, om.rigid(p, drill_bone)) for p in drill if p.material == mat]
        meshes += om.build_meshes(template, pieces, mi)
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    md = replace(md, objects=[replace(md.objects[0], meshes=meshes)])
    md = g.recompute_bounds(md)
    info = {'drill base': base, 'drill length': length, 'drill radius': radius, 'drill repeat': share, 'meshes': len(meshes),
            'triangles': sum(len(me.indices) // 6 for me in meshes), 'drill bone': drill_bone}
    return md, rab, textures(obj), info


def build_with_info(game, obj_file: str) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    md, rab, tex, info = build_model(game, obj_file)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(rab, HOST_MDB).stored = stored
    for name, dds in tex.items():
        om.add_texture(rab, name, dds)
    return rab_write(rab), md, info


def build(game, obj_file: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_DRILL.MRAB."""
    return build_with_info(game, obj_file)[0]


# ------------------------------------------------------------------------------------------ check

def check(arc: bytes, host_bones: list[str] | None = None) -> None:
    """Re-read `arc` and raise DrillModelError unless: the archive and model round-trip; one object, on a kind-2 bone,
    every mesh valid (vertex / index buffers, < 65536 vertices, mesh_index a permutation, material and blend indices in
    range, skinned only to skin bones, weights 1); every material texture (HD and .lod) an archive member; every bone's
    bind x inverse bind the identity; the bone names in order are `host_bones` (the stock Blacker's) with DRILL_BONE
    inserted under `body`; the drill bone's axes the model's, its geometry inside the cylinder of DRILL_RADIUS round
    its +Z from its origin to DRILL_LENGTH, every other vertex on `body`, behind the drill (but for the drive shaft
    into the drill's base, inside its radius) and over the ground."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    nb = len(md.bones)
    _req(nb < 256 and len(md.objects) == 1, f'{nb} bones, {len(md.objects)} objects')
    files = {f.name.lower() for f in rab.files}
    o = md.objects[0]
    _req(md.bones[o.bone].kind == 2 and o.meshes, 'the object is not on a kind-2 bone or has no mesh')
    _req(sorted(me.mesh_index for me in o.meshes) == list(range(len(o.meshes))), 'mesh_index not a permutation')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    names = [md.name_of(b.name) for b in md.bones]
    di, body = md.bone_index(DRILL_BONE), md.bone_index(DRILL_PARENT)
    _req(di > body >= 0 and md.bones[di].parent == body and md.bones[di].kind == 3, f'{DRILL_BONE} not a skin bone under body')
    if host_bones is not None:
        _req([n for n in names if n != DRILL_BONE] == host_bones, 'the stock bones changed')
    rot = w[di][:3] + w[di][4:7] + w[di][8:11]
    _req(max(abs(a - b) for a, b in zip(rot, (1, 0, 0, 0, 1, 0, 0, 0, 1))) < 1e-5, f'{DRILL_BONE}: axes not the model\'s')
    origin = w[di][12:15]
    drill_pts, hull_lo = 0, 1e9
    for j, me in enumerate(o.meshes):
        nv = me.nverts
        tag = f'mesh {j}'
        _req(len(me.vdata) == nv * me.vsize and 0 < nv < 0x10000, f'{tag}: {nv} vertices')
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(idx and len(idx) % 3 == 0 and max(idx) < nv, f'{tag}: index buffer')
        _req(0 <= me.material < len(md.materials), f'{tag}: material {me.material}')
        bi, bw = g.skin_columns(me)
        for p, r, wt in zip(g.mesh_positions(me), bi, bw):
            bone = int(r[0])
            _req(bone < nb and md.bones[bone].kind == 3 and abs(sum(wt) - 1.0) < 1e-3, f'{tag}: skin {r} {wt}')
            if bone == di:
                drill_pts += 1
                rr = math.hypot(p[0] - origin[0], p[1] - origin[1])
                _req(rr <= DRILL_RADIUS + 1e-3 and -1e-3 <= p[2] - origin[2] <= DRILL_LENGTH + 0.05,
                     f'{tag}: drill vertex {p} outside its cylinder')
            else:
                # the drive shaft reaches from the hull into the drill's base: it stays on the hull, inside the drill
                shaft = math.hypot(p[0] - origin[0], p[1] - origin[1]) <= DRILL_RADIUS and p[2] <= origin[2] + 0.1
                _req(bone == body and (p[2] < DRILL_SPLIT_Z + 1e-3 or shaft), f'{tag}: vertex {p} on bone {bone}')
                hull_lo = min(hull_lo, p[1])
    _req(drill_pts > 1000 and -0.01 <= hull_lo < 0.05, f'{drill_pts} drill vertices, hull bottom {hull_lo:.3f}')
    for m in md.materials:
        for x in m.textures:
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'material {md.name_of(m.name)}: texture member {want} missing')


def stock_bones(game) -> list[str]:  # noqa: ANN001 - rootcpk.Game
    """The stock Blacker model's bone names, in order (check()'s reference)."""
    md = mdb_read(member(rab_read(game.read('OBJECT', HOST_ARC)), HOST_MDB).data)
    return [md.name_of(b.name) for b in md.bones]
