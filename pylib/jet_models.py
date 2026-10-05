"""Scaled stock aircraft models for the jet variants (experimental; format in docs/mdb-format.md).

    python pylib/jet_models.py [OUTDIR]         (default OUTDIR: build/jetmodels; never the game directory)

Each output archive is a stock OBJECT/*.MRAB from Root.cpk (read only) with exactly one .mdb replaced (same file
name inside the archive, CMPL-compressed like pylib/mdb_jet.py does); every other member keeps its stored bytes.

  EDF6VC_INTERCEPTOR.MRAB  BOMBER501.MRAB         bomber501_2.mdb  elevon split (mdb_jet.build), x 0.65
  EDF6VC_MULTIROLE.MRAB    BOMBER401.MRAB         bomber401.mdb    x 0.5
  EDF6VC_CARRIER.MRAB      V508_TRANSPORT.MRAB    v508_transport.mdb  x 1.6
  EDF6VC_DRONE.MRAB        PD607_DRONE_AIRSTRIKE.MRAB  pd607_Drone_airstrike.mdb  x 3.0, root bone renamed `mdl`, `body` levelled

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


MODELS: dict[str, Recipe] = {
    'EDF6VC_INTERCEPTOR.MRAB': Recipe('BOMBER501.MRAB', 'bomber501_2.mdb', 0.65, split=True, fuselage_x=2.0),
    'EDF6VC_MULTIROLE.MRAB': Recipe('BOMBER401.MRAB', 'bomber401.mdb', 0.5, fuselage_x=2.5),
    'EDF6VC_CARRIER.MRAB': Recipe('V508_TRANSPORT.MRAB', 'v508_transport.mdb', 1.6, fuselage_x=4.5),
    'EDF6VC_DRONE.MRAB': Recipe('PD607_DRONE_AIRSTRIKE.MRAB', 'pd607_Drone_airstrike.mdb', 3.0, root='mdl', level='body'),
    # The Primer swarm (src/jet_swarm.cpp, docs/swarm-plan.md): its cores in the Imperial drone's model (83 m with
    # its cannon arms, x 0.5, and x 1 for the huge one), which already has the root `mdl` and a level `body` bone,
    # so only the scale changes. Its drone is a model of its own (GENERATED).
    'EDF6VC_SWARM_CORE.MRAB': Recipe('E515_IMPERIALUFO.MRAB', 'e515_imperialufo.mdb', 0.5),
    # the huge swarm's core: the Imperial drone at its own size (83 m across)
    'EDF6VC_SWARM_CORE_XL.MRAB': Recipe('E515_IMPERIALUFO.MRAB', 'e515_imperialufo.mdb', 1.0),
}
# The submarine carrier (tools/make_sub.py, docs/subcarrier-re.md): the mission object EV603_MARINE's model,
# at its size in the missions (x 1: 1664 m long, 355 m wide, hull bottom to main deck 360 m). Its `body` is bound turned (x -> y, y -> z, z -> x) like the
# drone's. Kept out of MODELS so tools/make_jets.py does not write it; build(game, SUB_MODELS) does.
SUB_MODELS: dict[str, Recipe] = {
    'EDF6VC_SUB.MRAB': Recipe('EV603_MARINE.MRAB', 'ev603_marine.mdb', 1.0, root='mdl', level='body'),
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


def make_model(src: Mdb, r: Recipe) -> Mdb:
    if not r.split:
        md = scale_mdb(level_bone(src, r.level) if r.level else src, r.scale)
        return rename_root(md, r.root) if r.root else md
    split, surfaces, _stats = mdb_jet.build(collapse_501_2(src))
    mdb_jet.self_check(mdb_write(split), surfaces)        # hinge / bind checks on the unscaled split model
    return scale_mdb(split, r.scale)


def replace_member(raw: bytes, model: str, data: bytes) -> bytes:
    rab = rab_read(raw)
    assert rab_write(rab) == raw, 'stock archive does not round-trip'
    hits = [f for f in rab.files if f.name.lower() == model.lower()]
    assert len(hits) == 1, f'{model}: {len(hits)} members'
    stored = cmpl_compress(data)
    assert cmpl_decompress(stored) == data
    hits[0].stored = stored
    return rab_write(rab)


# Models made from primitives rather than scaled (file -> builder module): the swarm's dragonfly drone
# (pylib/dragonfly_model.py, in the stock gold drone's archive and materials).
GENERATED_FILES = ('EDF6VC_SWARM_UNIT.MRAB',)


def generated(game, name: str) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The archive of GENERATED_FILES entry `name`."""
    import dragonfly_model
    assert name in GENERATED_FILES, name
    return dragonfly_model.build(game)


def build(game, models: dict[str, Recipe] | None = None) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """Output file name -> archive bytes, every one checked (`check`); `models` default MODELS, and then the
    GENERATED_FILES too."""
    out: dict[str, bytes] = {}
    if models is None:
        for name in GENERATED_FILES:
            out[name] = generated(game, name)
    for name, r in (MODELS if models is None else models).items():
        raw = game.read('OBJECT', r.archive)
        src = mdb_read(next(f for f in rab_read(raw).files if f.name.lower() == r.model.lower()).data)
        data = mdb_write(make_model(src, r))
        arc = replace_member(raw, r.model, data)
        check(raw, arc, r)
        out[name] = arc
    return out


# ------------------------------------------------------------------------------------------ checks

def close(a: float, b: float) -> bool:
    return abs(a - b) <= 1e-3 * abs(b) + 2e-3      # half4 positions re-rounded after scaling


def check(raw: bytes, arc: bytes, r: Recipe) -> None:
    """Re-read the written archive: same members, untouched members byte-identical, the new model round-trips,
    its vertex box == source box x scale, bone names as the source (plus the elevon split), and every bone's
    bind x inv_bind and world translation consistent with the source x scale."""
    a, b = rab_read(raw), rab_read(arc)
    assert rab_write(b) == arc
    assert [(f.name, f.folder, f.flag) for f in a.files] == [(f.name, f.folder, f.flag) for f in b.files]
    for fa, fb in zip(a.files, b.files):
        if fa.name.lower() != r.model.lower():
            assert fa.stored == fb.stored, f'{fa.name} changed'
    src = mdb_read(next(f for f in a.files if f.name.lower() == r.model.lower()).data)
    data = next(f for f in b.files if f.name.lower() == r.model.lower()).data
    new = mdb_read(data)
    assert mdb_write(new) == data, 'new model does not round-trip'

    ref = mdb_jet.build(collapse_501_2(src))[0] if r.split else src     # the unscaled model it was made from
    if r.level:
        ref = level_bone(ref, r.level)
    names = [ref.name_of(x.name) for x in ref.bones]
    if r.root:
        names[0] = r.root
    assert [new.name_of(x.name) for x in new.bones] == names
    assert [x.parent for x in new.bones] == [x.parent for x in ref.bones]
    if not r.split:
        assert names[1:] == [src.name_of(x.name) for x in src.bones][1:]
    lo0, hi0 = bbox(bind_positions(src))
    lo1, hi1 = bbox(bind_positions(new))
    for i in range(3):
        assert close(lo1[i], lo0[i] * r.scale) and close(hi1[i], hi0[i] * r.scale), \
            f'{r.model} box axis {i}: {lo1[i]}..{hi1[i]} vs {lo0[i] * r.scale}..{hi0[i] * r.scale}'
    w0, w1 = bind_world(ref), bind_world(new)
    for k, (x0, x1) in enumerate(zip(ref.bones, new.bones)):
        p0, p1 = mmul(w0[k], x0.inv_bind), mmul(w1[k], x1.inv_bind)
        assert max(abs(u - v * (r.scale if i in (12, 13, 14) else 1.0)) for i, (u, v) in enumerate(zip(p1, p0))) < 1e-3
        assert all(abs(w1[k][12 + c] - w0[k][12 + c] * r.scale) < 1e-4 for c in range(3))
        assert all(abs(x1.half[c] - x0.half[c] * r.scale) < 1e-4 for c in range(3))


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
