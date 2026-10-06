"""Scaled stock aircraft models for the jet variants (experimental; format in docs/mdb-format.md).

    python pylib/jet_models.py [OUTDIR]         (default OUTDIR: build/jetmodels; never the game directory)

Each output archive is a stock OBJECT/*.MRAB from Root.cpk (read only) with exactly one .mdb replaced (same file
name inside the archive, CMPL-compressed like pylib/mdb_jet.py does); every other member keeps its stored bytes.

  EDF6VC_INTERCEPTOR.MRAB  BOMBER501.MRAB         bomber501_2.mdb  elevon split (mdb_jet.build), landing gear, x 0.65
  EDF6VC_MULTIROLE.MRAB    BOMBER401.MRAB         bomber401.mdb    skinned (jet_gear.skin_rigid), landing gear, x 0.5
  EDF6VC_CARRIER.MRAB      V508_TRANSPORT.MRAB    v508_transport.mdb  x 1.6
  EDF6VC_DRONE.MRAB        PD607_DRONE_AIRSTRIKE.MRAB  pd607_Drone_airstrike.mdb  x 3.0, root bone renamed `mdl`, `body` levelled

The fixed-wing models carry retractable landing gear (pylib/jet_gear.py, Recipe.gear): three legs grafted from a stock
helicopter's gear, extended in the bind pose, the wheels the model's lowest points (so the grounded model stands on
them and its box's bottom is their contact); the archive gains the donor's textures. Hover craft (the carrier, the
drone) have none.

Every jet model's root bone is `mdl` (pylib/vcobjects.py JET_MAB_ROOT: the V506 locators hang on that name), so
a model whose root is called otherwise gets it renamed (Recipe.root; only that bone uses the name).
The V506 animation drives the model's `body` bone with the heli body's own pose (level, nose +z), in place of the
bone's bind local: the drone's `body` is bound turned (nose -> -y), so it flew nose down, upright, on 2026-10-03.
Its turn goes into its children instead (Recipe.level, level_bone); the mesh is skinned in model space, untouched.

Facts relied on (checked by the asserts / `check` below):
  * Every bone matrix (local and inverse bind) is affine, row-vector convention, row 3 = translation. A uniform
    scale s of the whole model is the conjugation C(M) = S^-1 M S with S = diag(s, s, s, 1), which for an affine
    M only multiplies its translation row by s (any 3x3 part, rotations or the rare scaled bones, is untouched)
    and is multiplicative: C(A) C(B) = C(AB). So scaling every local and every inv_bind translation keeps
    world = local x parent and bind x inv_bind exactly as before, and both skinned vertices (model space) and
    rigid vertices (bone-local space) come out scaled by s when their positions are scaled by s.
  * Bone bounds (+0xA0 half extents, +0xB0 centre, both in the bone's bind frame, w = 1) scale by s in xyz.
  * Positions are the layout element named 'position' / 'POSITION' (stored half4, float3 or float4; w kept).
    Normals / binormals / tangents / texcoords / blend data are scale free. No material parameter of these four
    models is a length (diffuse, metallic, roughness, specular_*, fall_off_*, change_color*, ...), so they stay.
  * bomber501_2.mdb is bomber501.mdb's mesh (byte-identical vertex and index data) under an extra identity
    node: mdl -> bomber501_2 -> bomber501, with the df2 texture set. mdb_jet.build only takes the two-bone
    stock skeleton, so the interceptor drops the identity `bomber501_2` node (mdl -> bomber501, which is what
    mdb_jet.build expects) and keeps bomber501_2's own mesh, material and texture table.
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, replace

HERE = os.path.dirname(os.path.abspath(__file__))
from mdb import (Bone, Mat, Mdb, Mesh, Object, bind_world, cmpl_compress, cmpl_decompress, ident, inverse_affine, mdb_read,  # noqa: E402
                 mdb_write, mmul, rab_read, rab_write, read_elem, verify)
import gamedir  # noqa: E402
import graft_pure  # noqa: E402
import jet_gear  # noqa: E402
import mdb_jet  # noqa: E402

Box = tuple[list[float], list[float]]          # (min xyz, max xyz)
GAME_DIR = gamedir.find_or_dev()


@dataclass(frozen=True)
class Recipe:
    archive: str            # stock OBJECT/ archive in Root.cpk
    model: str              # the .mdb inside it that gets replaced (name kept)
    scale: float
    split: bool = False     # elevon split (mdb_jet.build) before scaling
    fuselage_x: float | None = None   # rigid box from the vertices with |x| <= this (source metres); None = all
    root: str | None = None   # the root bone's new name (see the docstring); None = kept
    level: str | None = None  # a bone turned level (level_bone): the one the V506 body drives, if its bind is turned
    grounded: bool = True     # lifted so its lowest point is the model origin (grounded): the game puts a vehicle's origin
                              # on the ground; a model under it sinks into the ground, a box under it falls through it
    gear: str | None = None   # its landing gear (jet_gear.SPECS key) before scaling; None: none (hover craft)


MODELS: dict[str, Recipe] = {
    'EDF6VC_INTERCEPTOR.MRAB': Recipe('BOMBER501.MRAB', 'bomber501_2.mdb', 0.65, split=True, fuselage_x=2.0, gear='bomber501'),
    'EDF6VC_MULTIROLE.MRAB': Recipe('BOMBER401.MRAB', 'bomber401.mdb', 0.5, fuselage_x=2.5, gear='bomber401'),
    'EDF6VC_CARRIER.MRAB': Recipe('V508_TRANSPORT.MRAB', 'v508_transport.mdb', 1.6, fuselage_x=4.5),
    'EDF6VC_DRONE.MRAB': Recipe('PD607_DRONE_AIRSTRIKE.MRAB', 'pd607_Drone_airstrike.mdb', 3.0, root='mdl', level='body'),
}
# The submarine carrier (tools/make_sub.py, docs/subcarrier-re.md): the mission object EV603_MARINE's model,
# at its size in the missions (x 1: 1664 m long, 355 m wide, hull bottom to main deck 360 m). Its `body` is bound turned (x -> y, y -> z, z -> x) like the
# drone's. Kept out of MODELS so tools/make_jets.py does not write it; build(game, SUB_MODELS) does.
SUB_MODELS: dict[str, Recipe] = {
    'EDF6VC_SUB.MRAB': Recipe('EV603_MARINE.MRAB', 'ev603_marine.mdb', 1.0, root='mdl', level='body', grounded=False),
}

PACK = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}


# ------------------------------------------------------------------------------------------ scaling

def scale_translation(m: Mat, s: float) -> Mat:
    """C(M) = S^-1 M S for an affine row-vector matrix: the translation row times s."""
    assert abs(m[3]) < 1e-6 and abs(m[7]) < 1e-6 and abs(m[11]) < 1e-6 and abs(m[15] - 1) < 1e-6, 'not affine'
    return m[:12] + [m[12] * s, m[13] * s, m[14] * s, m[15]]


def scale_mesh(me: Mesh, s: float) -> Mesh:
    pos = [e for e in me.elems if e.name.lower() == 'position']
    assert len(pos) == 1, f'expected one position element, got {[e.name for e in me.elems]}'
    e = pos[0]
    assert e.fmt in (1, 4, 7), f'position format {e.fmt}'
    fmt = PACK[e.fmt]
    out = bytearray(me.vdata)
    for v in range(me.nverts):
        at = v * me.vsize + e.offset
        p = list(struct.unpack_from(fmt, out, at))
        p[0], p[1], p[2] = p[0] * s, p[1] * s, p[2] * s
        struct.pack_into(fmt, out, at, *p)
    return replace(me, vdata=bytes(out))


def scale_bone(b: Bone, s: float) -> Bone:
    return replace(b, local=scale_translation(b.local, s), inv_bind=scale_translation(b.inv_bind, s),
                   half=[b.half[0] * s, b.half[1] * s, b.half[2] * s, b.half[3]],
                   centre=[b.centre[0] * s, b.centre[1] * s, b.centre[2] * s, b.centre[3]])


def scale_mdb(md: Mdb, s: float) -> Mdb:
    """Uniform scale about the model origin: vertex positions, bone local / inverse-bind translations, bone
    bounds. Everything else (normals, tangents, uvs, skin weights, materials, names, buffer order) unchanged."""
    return replace(md, bones=[scale_bone(b, s) for b in md.bones],
                   objects=[replace(o, meshes=[scale_mesh(me, s) for me in o.meshes]) for o in md.objects])


# ------------------------------------------------------------------------------------------ lifting
# Where the lift goes (2026-10-05): the game draws a jet's mesh on its mesh bone (the bone the V506 animation drives,
# mesh_bone: the SGO's animation_model_bone_mapping[1]) with that bone's world record at the collision box frame's
# origin (FLAME log: the mesh bone 1.385 m under / 1.69 m behind the player fighter's origin, its box centre
# (0, 1.381, 1.688); the strike jet's (0, 2.12, 2.72), its box (0, 2.123, 2.723)) and skins v x inv_bind x world. The
# root's record is not kept up, so a lift put in the root's local (as it was) never reached the drawn mesh while the
# mesh bone's inverse bind still took it off: the mesh was drawn `d` under what the boxes and flames were measured on
# (the fighter 1.485 m, the strike jet 2.285 m: its box over its top, its wheels in the ground, its flames over its
# exits). So the mesh bone and its ancestors keep their bind (the mesh bone at the model's origin, as drawn) and the
# lift is in what hangs on it: skinned vertices up d; the mesh bone's children's locals moved d (in the model's frame),
# so every bone under it is bound d higher (inverse bind T(-d) x inv).

def lift_mesh(me: Mesh, d: float) -> Mesh:
    """A skinned mesh (stored in model space) raised d; a rigid one (bone-local) is raised with its bone instead."""
    if me.flags[1] == 0:
        return me
    e = next(x for x in me.elems if x.name.lower() == 'position')
    fmt = PACK[e.fmt]
    out = bytearray(me.vdata)
    for v in range(me.nverts):
        at = v * me.vsize + e.offset
        p = list(struct.unpack_from(fmt, out, at))
        p[1] += d
        struct.pack_into(fmt, out, at, *p)
    return replace(me, vdata=bytes(out))


def mesh_bone(md: Mdb) -> int:
    """The mesh bone: the first bone that carries mesh (kind 1 rigid or 3 skinned), the one the V506 animation drives
    (vcobjects.jet_sgo maps it: Jet.body). Every other mesh-carrying bone is under it."""
    return next(b.index for b in md.bones if b.kind in (1, 3))


def _under(md: Mdb, k: int) -> set[int]:
    """The bones under bone k (not k)."""
    out: set[int] = set()
    for b in md.bones:      # parents come first
        if b.parent == k or b.parent in out:
            out.add(b.index)
    return out


def lift_mdb(md: Mdb, d: float) -> Mdb:
    """`md` raised d on its mesh bone (see above): its skinned vertices up d, the mesh bone's children's locals moved d up
    in the model's frame (local x W T(d) W^-1, W the mesh bone's bind world), every bone under it bound d higher (inverse
    bind T(-d) x inv: its translation row less d times its y row). The mesh bone and its ancestors keep their bind; no
    rigid mesh may hang on them (it would stay down)."""
    k = mesh_bone(md)
    under = _under(md, k)
    assert all(o.bone in under for o in md.objects for me in o.meshes if me.flags[1] == 0), 'a rigid mesh on the mesh bone'
    w = bind_world(md)[k]
    up = ident()
    up[13] = d
    move = mmul(mmul(w, up), inverse_affine(w))
    bones = []
    for b in md.bones:
        if b.index in under:
            inv = list(b.inv_bind)
            for c in range(3):
                inv[12 + c] -= d * inv[4 + c]
            local = mmul(b.local, move) if b.parent == k else b.local
            b = replace(b, local=local, inv_bind=inv)
        bones.append(b)
    return replace(md, bones=bones, objects=[replace(o, meshes=[lift_mesh(me, d) for me in o.meshes]) for o in md.objects])


def ground_lift(md: Mdb) -> float:
    """How far `md` must rise for its lowest bind-pose vertex to be the origin (0 when none is under it)."""
    low = min(p[1] for p in bind_positions(md))
    return -low if low < 0.0 else 0.0


def grounded(md: Mdb) -> Mdb:
    """`md` lifted onto its origin (ground_lift), checked: its box rose exactly that, every bone's bind x inverse
    bind is unchanged and its world translation rose that."""
    d = ground_lift(md)
    if d == 0.0:
        return md
    out = lift_mdb(md, d)
    lo0, hi0 = bbox(bind_positions(md))
    lo1, hi1 = bbox(bind_positions(out))
    assert close(lo1[1], 0.0) and close(hi1[1], hi0[1] + d), f'lift box {lo0}..{hi0} -> {lo1}..{hi1}'
    w0, w1 = bind_world(md), bind_world(out)
    under = _under(md, mesh_bone(md))
    for k, (x0, x1) in enumerate(zip(md.bones, out.bones)):
        p0, p1 = mmul(w0[k], x0.inv_bind), mmul(w1[k], x1.inv_bind)
        assert max(abs(u - v) for u, v in zip(p0, p1)) < 1e-4, f'bone {k}: bind x inverse bind moved'
        rise = d if k in under else 0.0   # the mesh bone and its ancestors stay (lift_mdb)
        assert abs(w1[k][13] - w0[k][13] - rise) < 1e-4 and abs(w1[k][12] - w0[k][12]) < 1e-4 and abs(w1[k][14] - w0[k][14]) < 1e-4
    return out


# The stock bomber with elevon bones (mdb_jet.jet_archive: bomber501.mdb split) and landing gear, grounded: the default
# jet model (vcobjects.JET_ELEVON_FILE) and the strike jets' and the player strike jet's.
ELEVON_ARCHIVE, ELEVON_MODEL = 'BOMBER501.MRAB', 'bomber501.mdb'
ELEVON_FUSELAGE_X = 2.0   # m: the default jets' fuselage box half width (formation wings do not catch)
ELEVON_GEAR = 'bomber501'

_DONORS: dict[int, jet_gear.Donors] = {}


def donors(game) -> jet_gear.Donors:  # noqa: ANN001 - rootcpk.Game
    """The landing gear's donor (jet_gear.load_donors), read once per game reader."""
    if id(game) not in _DONORS:
        _DONORS[id(game)] = jet_gear.load_donors(game)
    return _DONORS[id(game)]


def with_gear(md: Mdb, gear: str | None, d: jet_gear.Donors | None) -> tuple[Mdb, list[str]]:
    """`md` with the landing gear `gear` (jet_gear.SPECS; a rigid two-bone model skinned first: jet_gear.skin_rigid) and
    the donor textures it needs; `gear` None: `md` as it is."""
    if gear is None:
        return md, []
    assert d is not None, 'a model with landing gear needs the donor'
    if md.bones[1].kind == 1:
        md = jet_gear.skin_rigid(md)
    return jet_gear.add_gear(md, jet_gear.SPECS[gear], d)


def elevon_model(game) -> Mdb:  # noqa: ANN001 - rootcpk.Game
    md, _tex = with_gear(mdb_jet.jet_archive(game.read('OBJECT', ELEVON_ARCHIVE))[2], ELEVON_GEAR, donors(game))
    return grounded(md)


def elevon_archive(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """EDF6VC_JET.MRAB, its model's gear checked (jet_gear.check_gear)."""
    raw = game.read('OBJECT', ELEVON_ARCHIVE)
    d = donors(game)
    md, tex = with_gear(mdb_jet.jet_archive(raw)[2], ELEVON_GEAR, d)
    md = grounded(md)
    jet_gear.check_gear(md, 1.0)
    return replace_member(raw, ELEVON_MODEL, mdb_write(md), d.rab, tex)


# ------------------------------------------------------------------------------------------ interceptor

def collapse_501_2(src: Mdb) -> Mdb:
    """bomber501_2.mdb as the two-bone stock skeleton mdb_jet.build takes: drop the identity `bomber501_2` node
    between mdl and bomber501 (its own mesh, material and textures kept)."""
    assert [src.name_of(b.name) for b in src.bones] == ['mdl', 'bomber501_2', 'bomber501'], 'unexpected skeleton'
    assert max(abs(x - y) for x, y in zip(src.bones[1].local, ident())) < 1e-6, 'bomber501_2 node is not identity'
    assert len(src.objects) == 1 and src.objects[0].bone == 2
    used = ['mdl', 'bomber501', src.name_of(src.materials[0].name), src.name_of(src.objects[0].name)]
    names: list[str | None] = list(used) + [None] * (len(src.names) - 1 - len(used))
    b0, _node, b2 = src.bones
    bones = [replace(b0, child=1, name=0), replace(b2, index=1, parent=0, name=1, depth_delta=1)]
    mats = [replace(m, name=2) for m in src.materials]
    obj = Object(3, 1, src.objects[0].meshes)
    return Mdb(src.version, names, bones, [obj], mats, src.textures)


# ------------------------------------------------------------------------------------------ geometry

def bind_positions(md: Mdb, bones: set[int] | None = None) -> list[tuple[float, float, float]]:
    """Model-space bind-pose positions of every vertex (rigid meshes: through the object bone's bind matrix;
    skinned meshes are stored in model space). `bones`: only skinned vertices whose first influence is one of
    these (rigid meshes: only if the object bone is one of them)."""
    w = bind_world(md)
    out = []
    for o in md.objects:
        for me in o.meshes:
            e = next(x for x in me.elems if x.name.lower() == 'position')
            P = read_elem(me, e.name, e.channel) or []
            skinned = me.flags[1] != 0
            bi = read_elem(me, next((x.name for x in me.elems if x.name.upper() == 'BLENDINDICES'), ''), 0) if skinned else None
            m = ident() if skinned else w[o.bone]
            for v, p in enumerate(P):
                owner = bi[v][0] if bi else o.bone
                if bones is not None and owner not in bones:
                    continue
                out.append(tuple(p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c] for c in range(3)))  # type: ignore[misc]
    return out  # type: ignore[return-value]


def bbox(points: list[tuple[float, float, float]]) -> Box:
    return [min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)]


def rigid_box(points: list[tuple[float, float, float]], fx: float | None) -> list[list[float]]:
    """[centre, half extents] of the vertices with |x| <= fx (all when None), x centred on 0."""
    sel = [p for p in points if fx is None or abs(p[0]) <= fx + 1e-6]
    lo, hi = bbox(sel)
    hx = max(abs(lo[0]), abs(hi[0]))
    return [[0.0, round((lo[1] + hi[1]) / 2, 3), round((lo[2] + hi[2]) / 2, 3)],
            [round(hx, 3), round((hi[1] - lo[1]) / 2, 3), round((hi[2] - lo[2]) / 2, 3)]]


# ------------------------------------------------------------------------------------------ build

def rename_root(md: Mdb, name: str) -> Mdb:
    """`md` with its root bone called `name`: the root's name entry is rewritten, which no other bone, object
    or material may share."""
    i = md.bones[0].name
    users = [b.index for b in md.bones if b.name == i] + [o.name for o in md.objects if o.name == i] + \
            [m.name for m in md.materials if m.name == i]
    assert md.bones[0].parent == -1 and users == [0], f'root name entry {i} shared: {users}'
    names = list(md.names)
    names[i] = name
    return replace(md, names=names)


def level_bone(md: Mdb, name: str) -> Mdb:
    """`md` with bone `name` turned level (its bind rotation taken out, its translation kept) and the turn put
    into its children's locals instead, so every other bone's bind matrix and every vertex stay where they
    were. Only a signed axis permutation, on a bone that carries no rigid mesh (its bounds are permuted with it)."""
    k = next(b.index for b in md.bones if md.name_of(b.name) == name)
    assert not any(o.bone == k for o in md.objects), f'{name} carries a rigid mesh'
    w = bind_world(md)
    rot = w[k][:12] + [0.0, 0.0, 0.0, 1.0]
    rows = [rot[i * 4:i * 4 + 3] for i in range(3)]
    assert all(sorted(abs(v) for v in row) == [0.0, 0.0, 1.0] for row in rows), f'{name}: not an axis permutation'
    flat = ident()[:12] + w[k][12:16]
    parent = ident() if md.bones[k].parent < 0 else w[md.bones[k].parent]
    bones = list(md.bones)
    bones[k] = replace(bones[k], local=mmul(flat, inverse_affine(parent)), inv_bind=inverse_affine(flat),
                       half=[sum(abs(bones[k].half[i] * rows[i][c]) for i in range(3)) for c in range(3)] + [bones[k].half[3]],
                       centre=[sum(bones[k].centre[i] * rows[i][c] for i in range(3)) for c in range(3)] + [bones[k].centre[3]])
    for b in md.bones:
        if b.parent == k:
            bones[b.index] = replace(b, local=mmul(b.local, rot))
    out = replace(md, bones=bones)
    w1 = bind_world(out)
    assert all(max(abs(x - y) for x, y in zip(w[i], w1[i])) < 1e-5 for i in range(len(w)) if i != k), 'bind moved'
    return out


def unscaled(src: Mdb, r: Recipe, d: jet_gear.Donors | None) -> tuple[Mdb, list[str]]:
    """The model `r` makes of `src` before its scale (levelled, split, with its gear) and the gear's textures."""
    if r.split:
        split, surfaces, _stats = mdb_jet.build(collapse_501_2(src))
        mdb_jet.self_check(mdb_write(split), surfaces)        # hinge / bind checks on the unscaled split model
        return with_gear(split, r.gear, d)
    return with_gear(level_bone(src, r.level) if r.level else src, r.gear, d)


def finish(md: Mdb, r: Recipe) -> Mdb:
    """The unscaled model `md` (unscaled) scaled, its root renamed, grounded as `r` says."""
    md = scale_mdb(md, r.scale)
    md = rename_root(md, r.root) if r.root else md
    return grounded(md) if r.grounded else md


def make_model(src: Mdb, r: Recipe, d: jet_gear.Donors | None = None) -> Mdb:
    """`src` made as `r` says (`d`: the gear's donor, needed when r.gear is set)."""
    return finish(unscaled(src, r, d)[0], r)


def replace_member(raw: bytes, model: str, data: bytes, donor=None, textures: list[str] | None = None) -> bytes:  # noqa: ANN001 - mdb.Rab
    """The archive `raw` with member `model` replaced by `data` (CMPL-compressed) and the texture files `textures`
    (each with its .lod variant) copied in from the archive `donor` (graft_pure.copy_texture_members)."""
    rab = rab_read(raw)
    assert rab_write(rab) == raw, 'stock archive does not round-trip'
    hits = [f for f in rab.files if f.name.lower() == model.lower()]
    assert len(hits) == 1, f'{model}: {len(hits)} members'
    stored = cmpl_compress(data)
    assert cmpl_decompress(stored) == data
    hits[0].stored = stored
    if textures:
        graft_pure.copy_texture_members(rab, donor, textures)
    return rab_write(rab)


def build(game, models: dict[str, Recipe] | None = None) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """Output file name -> archive bytes, every one checked (`check`); `models` default MODELS."""
    out: dict[str, bytes] = {}
    for name, r in (MODELS if models is None else models).items():
        raw = game.read('OBJECT', r.archive)
        src = mdb_read(next(f for f in rab_read(raw).files if f.name.lower() == r.model.lower()).data)
        d = donors(game) if r.gear else None
        md, tex = unscaled(src, r, d)
        data = mdb_write(finish(md, r))
        arc = replace_member(raw, r.model, data, d.rab if d else None, tex)
        check(raw, arc, r, d)
        out[name] = arc
    return out


def model_box(game, file: str | None) -> list[list[float]]:  # noqa: ANN001 - rootcpk.Game
    """[centre, half extents] of the whole of a jet's model as the game draws it (wings, nose and tail): `file` one of
    MODELS, None the bomber with elevon bones (mdb_jet.jet_archive, the stock bomber501.mdb's geometry). A player
    jet's collision box (vcobjects.jet_sgo) is this, so it is the plane the player sees."""
    return rigid_box(bind_positions(_model_of(game, file)), None)


_MODELS_MADE: dict[tuple[int, str | None], Mdb] = {}


def _model_of(game, file: str | None) -> Mdb:  # noqa: ANN001 - rootcpk.Game
    """The model `file` (as model_box) as it is made, made once per game reader (every jet SGO measures its box and
    its door off it: vcobjects.jet_sgo). Callers only read it."""
    key = (id(game), file)
    if key not in _MODELS_MADE:
        _MODELS_MADE[key] = _make_model_of(game, file)
    return _MODELS_MADE[key]


def _make_model_of(game, file: str | None) -> Mdb:  # noqa: ANN001 - rootcpk.Game
    if file is None:
        return elevon_model(game)
    if file in STOCK_BOMBERS:
        arc, mdl = STOCK_BOMBERS[file]
        return mdb_read(next(f for f in rab_read(game.read('OBJECT', arc)).files if f.name.lower() == mdl).data)
    r = MODELS[file]
    raw = game.read('OBJECT', r.archive)
    return make_model(mdb_read(next(f for f in rab_read(raw).files if f.name.lower() == r.model.lower()).data), r,
                      donors(game) if r.gear else None)


# Each jet model's nozzles, in its frame (x right, y up, z forward): src/booster.cpp kJetNozzles (tools/selftest.py
# holds that table to this one). Read off the meshes, each checked against the game where there is a picture:
#  - the interceptor (the player's fighter): two square nozzles either side of the tail, each exit a slanted quad
#    (1.43,0.18,-7.77) (2.22,0.38,-8.23) (3.22,0.84,-7.77) (2.34,1.67,-7.63). The first table put each flame at
#    (1.85,0.85,-7.3): 0.45 m in from the opening's middle and 0.55 m inside it, which from behind and below shows
#    the flame high in the opening (the user's picture, 2026-10-05: 「飞机的尾焰高了一点」).
#  - the strike jet's bomber501: the interceptor's mesh at x 1 (bomber501_2 is the same mesh): the same two square
#    nozzles either side of the tail. The first tables put one flame on the tail cone's end on the centre line (x 0),
#    between them, half their size (2026-10-05).
#  - the multirole: a 0.68 x 0.26 m exhaust box at the fuselage's end (z -0.8), not the flat tail's tip (-1.78).
#  - the drone: a round 0.33 m nozzle at z -1.26 (the old table had it 0.33 m above and 0.24 m behind it).
# Each flame sits on its exit's centre in the exit plane and is as big as its engine (the user, 2026-10-05:
# 「尾焰大小应该根据引擎大小来」): width the exit's diameter (a circle of the exit's area; an exit that is only an edge:
# its length), length FLAME_LENGTH_PER_DIAMETER of that. NOZZLE_EXITS picks each exit's rim vertices (a box in the
# model's frame, the right one of a mirrored pair); measure_nozzles reads them off the model, NOZZLES is what it
# reads (constants: the self-test has no Root.cpk), check_nozzles holds the two together.
# STOCK_BOMBERS: the stock bombers the airstrike's strike jets take over fly their own models as they are (no scale,
# no grounding, no gear), sharing the strike jet's mark: their exits are measured on those (src/booster.cpp
# kBomberNozzles). bomber501_2's x 0.65 + grounding is the interceptor's, bomber401's x 0.5 the multirole's.
STOCK_BOMBERS: dict[str, tuple[str, str]] = {'bomber401': ('BOMBER401.MRAB', 'bomber401.mdb'),
                                             'bomber501_2': ('BOMBER501.MRAB', 'bomber501_2.mdb')}
# Their fuselages' half widths (fuselage_box): the elevon bomber's (same mesh, x 1) and the multirole's (unscaled).
STOCK_FUSELAGE_X: dict[str, float] = {'bomber401': 2.5, 'bomber501_2': ELEVON_FUSELAGE_X}
# The landing gear (Recipe.gear) stands each model up on its wheels, so its grounding lifts it by the gear's height
# more (2026-10-05): the bomber501 1.0 m, the interceptor 0.65 m (both: jet_gear.SPECS drop x scale), the multirole
# 0.4365 m ((1.0 - its stock lowest point 0.127) x 0.5); the drone has no gear. The exit boxes are in the lifted frame.
FLAME_LENGTH_PER_DIAMETER = 5.0
ExitBox = tuple[tuple[float, float], tuple[float, float], tuple[float, float]]   # (x0, x1), (y0, y1), (z0, z1)
NOZZLE_EXITS: dict[str | None, tuple[ExitBox, bool]] = {    # (box, mirrored: a left twin at -x)
    None: (((1.5, 5.6), (0.0, 4.6), (-12.95, -11.69)), True),
    'EDF6VC_INTERCEPTOR.MRAB': (((1.0, 3.6), (0.0, 3.0), (-8.4, -7.6)), True),
    'EDF6VC_MULTIROLE.MRAB': (((-0.5, 0.5), (0.7, 1.5), (-0.85, -0.75)), False),
    'EDF6VC_DRONE.MRAB': (((-0.3, 0.3), (0.7, 1.3), (-1.3, -1.22)), False),
    'bomber401': (((-1.0, 1.0), (0.8, 1.8), (-1.7, -1.5)), False),
    'bomber501_2': (((1.5, 5.6), (-2.3, 2.3), (-12.95, -11.69)), True),
}
Nozzle = tuple[tuple[float, float, float], float]   # (exit centre, diameter)
NOZZLES: dict[str | None, tuple[Nozzle, ...]] = {
    None: (((3.58, 2.325, -12.006), 1.839), ((-3.58, 2.325, -12.006), 1.839)),
    'EDF6VC_INTERCEPTOR.MRAB': (((2.327, 1.511, -7.804), 1.195), ((-2.327, 1.511, -7.804), 1.195)),
    'EDF6VC_MULTIROLE.MRAB': (((0.0, 1.07, -0.799), 0.475),),
    'EDF6VC_DRONE.MRAB': (((0.0, 1.005, -1.261), 0.323),),
    'bomber401': (((0.0, 1.267, -1.597), 0.951),),
    'bomber501_2': (((3.58, 0.039, -12.006), 1.839), ((-3.58, 0.039, -12.006), 1.839)),
}


def _hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """The convex hull of `points`, counter-clockwise (Andrew's monotone chain)."""
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def turn(o: tuple[float, float], a: tuple[float, float], b: tuple[float, float]) -> float:
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower: list[tuple[float, float]] = []
    upper: list[tuple[float, float]] = []
    for p in pts:
        while len(lower) >= 2 and turn(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and turn(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def exit_of(rim: list[tuple[float, float, float]]) -> Nozzle:
    """An exhaust exit from its rim vertices: (centre, diameter). Seen from behind (x, y) the rim's hull is the
    opening: its area centroid is the centre, the circle of its area gives the diameter; z is the rim's mean (the exit
    plane). A rim with no area (an edge, a point) has its box middle as centre and its longest extent as diameter."""
    h = _hull([(p[0], p[1]) for p in rim])
    z = sum(p[2] for p in rim) / len(rim)
    a2 = sum(h[i][0] * h[i - 1][1] - h[i - 1][0] * h[i][1] for i in range(len(h))) if len(h) >= 3 else 0.0
    if abs(a2) < 2e-3:
        lo = [min(p[c] for p in rim) for c in range(2)]
        hi = [max(p[c] for p in rim) for c in range(2)]
        return ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, z), max(hi[0] - lo[0], hi[1] - lo[1])
    cx = sum((h[i - 1][0] + h[i][0]) * (h[i][0] * h[i - 1][1] - h[i - 1][0] * h[i][1]) for i in range(len(h))) / (3 * a2)
    cy = sum((h[i - 1][1] + h[i][1]) * (h[i][0] * h[i - 1][1] - h[i - 1][0] * h[i][1]) for i in range(len(h))) / (3 * a2)
    return (cx, cy, z), 2.0 * (abs(a2) / 2 / 3.141592653589793) ** 0.5


def measure_nozzles(game, file: str | None) -> tuple[Nozzle, ...]:  # noqa: ANN001 - rootcpk.Game
    """`file`'s exits read off its model (NOZZLE_EXITS picks their rim vertices), the right one first."""
    ((x0, x1), (y0, y1), (z0, z1)), mirrored = NOZZLE_EXITS[file]
    rim = [p for p in bind_positions(_model_of(game, file)) if x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and z0 <= p[2] <= z1]
    if len({(round(p[0], 3), round(p[1], 3)) for p in rim}) < 2:
        raise ValueError(f'{file}: {len(rim)} rim vertices in its exit box')
    (c, d) = exit_of(rim)
    if not mirrored:
        return ((c, d),)
    return ((c, d), ((-c[0], c[1], c[2]), d))


def check_nozzles(game) -> None:  # noqa: ANN001 - rootcpk.Game
    """NOZZLES is what measure_nozzles reads off the player's models (to 5 mm)."""
    for file, want in NOZZLES.items():
        got = measure_nozzles(game, file)
        ok = len(got) == len(want) and all(abs(a - b) < 0.005 for (gc, gd), (wc, wd) in zip(got, want)
                                           for a, b in zip(gc + (gd,), wc + (wd,)))
        if not ok:
            raise ValueError(f'{file}: NOZZLES {want}, the model has {got}')


def fuselage_box(game, file: str | None) -> list[list[float]]:  # noqa: ANN001 - rootcpk.Game
    """[centre, half extents] of a jet model's fuselage (the vertices within its Recipe.fuselage_x, all with none; the
    default model's within ELEVON_FUSELAGE_X): an NPC jet's collision box (vcobjects.jet_sgo), measured off the model
    as it is made (grounded), so it never reaches under the origin. Its bottom is the model's lowest point (the origin:
    what it stands on), wherever that is: the carrier's hull is 3.49 m over its landing pods (at |x| 11.5-12.7 m, out of
    its 7.2 m fuselage), and a box of the hull alone stood it on its hull, the pods 3.49 m in the ground (2026-10-05)."""
    if file is None:
        fx = ELEVON_FUSELAGE_X
    elif file in STOCK_BOMBERS:
        fx = STOCK_FUSELAGE_X[file]
    else:
        r = MODELS[file]
        fx = None if r.fuselage_x is None else r.fuselage_x * r.scale
    pts = bind_positions(_model_of(game, file))
    (cx, cy, cz), (hx, hy, hz) = rigid_box(pts, fx)
    low = min(p[1] for p in pts)
    top = cy + hy
    return [[cx, round((low + top) / 2, 3), cz], [hx, round((top - low) / 2, 3), hz]]


def drawn_lift(game, file: str | None) -> float:  # noqa: ANN001 - rootcpk.Game
    """How far the model's mesh bone (mesh_bone: where the game draws the model, at the box frame's origin) is bound
    over the model's origin: 0 on every grounded model (lift_mdb lifts what hangs on it, not it). `file` as model_box."""
    md = _model_of(game, file)
    return round(bind_world(md)[mesh_bone(md)][13], 4)


# ------------------------------------------------------------------------------------------ checks

def close(a: float, b: float) -> bool:
    return abs(a - b) <= 1e-3 * abs(b) + 2e-3      # half4 positions re-rounded after scaling


def check(raw: bytes, arc: bytes, r: Recipe, d: jet_gear.Donors | None = None) -> None:
    """Re-read the written archive: the stock members in their order, untouched ones byte-identical, plus (a model with
    gear) exactly the gear's donor textures, their bytes the donor's; the new model round-trips; bone names as the
    unscaled model it was made from (unscaled: the stock skeleton, the elevon split, the gear), every bone's bind x
    inv_bind and world translation consistent with that x scale; the stock geometry's box == the source box x scale
    and the whole model's == the unscaled model's x scale (both raised by the grounding); the gear's own checks
    (jet_gear.check_gear: one ground plane under every wheel, the legs fold into the body)."""
    a, b = rab_read(raw), rab_read(arc)
    assert rab_write(b) == arc
    src = mdb_read(next(f for f in a.files if f.name.lower() == r.model.lower()).data)
    ref, tex = unscaled(src, r, d)      # the unscaled model it was made from
    added = [f for f in b.files if f.name.lower() not in {x.name.lower() for x in a.files}]
    want = sorted(x.name.lower() for t in tex for x in graft_pure.texture_members(d.rab, t)) if tex else []  # type: ignore[union-attr]
    assert sorted(f.name.lower() for f in added) == want, f'added members {[f.name for f in added]}, want {want}'
    for f in added:
        assert f.stored == next(x for x in d.rab.files if x.name.lower() == f.name.lower()).stored  # type: ignore[union-attr]
    kept = [f for f in b.files if f not in added]
    assert [(f.name, a.folders[f.folder], f.flag) for f in a.files] == [(f.name, b.folders[f.folder], f.flag) for f in kept]
    for fa, fb in zip(a.files, kept):
        if fa.name.lower() != r.model.lower():
            assert fa.stored == fb.stored, f'{fa.name} changed'
    data = next(f for f in b.files if f.name.lower() == r.model.lower()).data
    new = mdb_read(data)
    assert mdb_write(new) == data, 'new model does not round-trip'

    names = [ref.name_of(x.name) for x in ref.bones]
    if r.root:
        names[0] = r.root
    assert [new.name_of(x.name) for x in new.bones] == names
    assert [x.parent for x in new.bones] == [x.parent for x in ref.bones]
    if not r.split:
        assert set([src.name_of(x.name) for x in src.bones][1:]) <= set(names[1:])
    lo0, hi0 = bbox(bind_positions(ref))
    lift = -lo0[1] * r.scale if r.grounded and lo0[1] < 0.0 else 0.0   # grounded: the scaled box raised onto its origin
    shift = (0.0, lift, 0.0)
    gear = {new.bone_index(n) for n in jet_gear.GEAR_BONES} if r.gear else set()
    stock = set(range(len(new.bones))) - gear
    for (l0, h0), pts in (((lo0, hi0), bind_positions(new)), (bbox(bind_positions(src)), bind_positions(new, stock))):
        lo1, hi1 = bbox(pts)
        for i in range(3):
            assert close(lo1[i], l0[i] * r.scale + shift[i]) and close(hi1[i], h0[i] * r.scale + shift[i]), \
                f'{r.model} box axis {i}: {lo1[i]}..{hi1[i]} vs {l0[i] * r.scale + shift[i]}..{h0[i] * r.scale + shift[i]}'
    w0, w1 = bind_world(ref), bind_world(new)
    under = _under(new, mesh_bone(new))   # lifted (lift_mdb); the mesh bone and its ancestors stay where the game draws them
    for k, (x0, x1) in enumerate(zip(ref.bones, new.bones)):
        p0, p1 = mmul(w0[k], x0.inv_bind), mmul(w1[k], x1.inv_bind)
        assert max(abs(u - v * (r.scale if i in (12, 13, 14) else 1.0)) for i, (u, v) in enumerate(zip(p1, p0))) < 1e-3
        rise = shift if k in under else (0.0, 0.0, 0.0)
        assert all(abs(w1[k][12 + c] - (w0[k][12 + c] * r.scale + rise[c])) < 2e-3 for c in range(3))
        assert all(abs(x1.half[c] - x0.half[c] * r.scale) < 1e-4 for c in range(3))
    if r.gear:
        jet_gear.check_gear(new, r.scale)


# ------------------------------------------------------------------------------------------ main

def main(argv: list[str]) -> int:
    outdir = os.path.abspath(argv[0] if argv else os.path.join(HERE, '..', 'build', 'jetmodels'))
    assert not outdir.lower().startswith(os.path.abspath(GAME_DIR).lower()), 'refusing to write into the game directory'
    from rootcpk import DEFAULT_GAME, Game
    game = Game(DEFAULT_GAME)
    os.makedirs(outdir, exist_ok=True)
    arcs = build(game)
    for name, arc in arcs.items():
        r = MODELS[name]
        path = os.path.join(outdir, name)
        with open(path, 'wb') as h:
            h.write(arc)
        md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == r.model.lower()).data)
        pts = bind_positions(md)
        lo, hi = bbox(pts)
        skin = sorted({md.name_of(md.bones[i].name) for i in range(len(md.bones)) if md.bones[i].kind in (1, 3)})
        root = md.name_of(md.bones[0].name)
        body = next(md.name_of(x.name) for x in md.bones if x.kind in (1, 3))
        print(f'== {name}  ({len(arc)} bytes)  {r.archive} : {r.model}  x {r.scale}')
        for x in md.bones:
            par = md.name_of(md.bones[x.parent].name) if x.parent >= 0 else '-'
            print(f'  bone [{x.index}] {md.name_of(x.name):<24} parent {par:<24} kind {x.kind} bounded {x.bounded}')
        print('  box min ' + ' '.join(f'{v:8.3f}' for v in lo) + '   max ' + ' '.join(f'{v:8.3f}' for v in hi)
              + '   size ' + ' '.join(f'{h - l:.2f}' for l, h in zip(lo, hi)))
        print(f'  mesh-carrying bones: {skin}')
        print(f'  animation_model_bone_mapping: {[root, body]}')
        print(f'  rigid box (|x| <= {None if r.fuselage_x is None else round(r.fuselage_x * r.scale, 3)}): '
              f'{rigid_box(pts, None if r.fuselage_x is None else r.fuselage_x * r.scale)}')
        if verify([path], False, True):
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
