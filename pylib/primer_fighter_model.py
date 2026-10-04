"""EDF6VC_PRIMER_FIGHTER.MRAB: a Primer (alien) flapping-wing fighter, built from the player's own Root.cpk.
Pure Python (pylib: mdb, mdb_jet, graft_pure; no numpy / PIL).

    build(game) -> bytes     the finished archive (game: rootcpk.Game, read only)
    check(arc)  -> None      raises PrimerFighterCheckError on any self-check failure

Source: OBJECT/E611_TIMESHIP.MRAB, e611_timeship.mdb (the Primers' new-type ship; one skinned mesh, one material
`e611_bodymat`, textures carriertop_rgb / _df / _nm). All eight arm end pods arm_A3..arm_H3 are the same geometry
(rotated copies, 1808 triangles each); hatch petal pairs hatch_X0 (outer panel) + hatch_X1 (inner panel) likewise.

  fuselage   arm_H3's pod, turned so the arm's outward axis is the nose (+z), x POD_SCALE (10.85 m long). The arm's
             hanging link under the pod's rear (70 triangles below HOOK_CUT_Y) is dropped.
  left wing  hatch_C0 + hatch_C1 (the petal pair on the ship's -x side), x WING_SCALE, put on the fuselage's +x side
             with the petal's rim (the ship's dome rim, where the stock petal hinges) as the wing root against the
             hull and the petal's point (the dome apex) as the wing tip; turned about the hinge so the root -> tip line
             is level (the dome's curvature stays: the inner panel rises, the outer one droops slightly).
             hatch_C0 (rim panel) -> wing_l, hatch_C1 (apex panel) -> wing_l2, hinged where the stock C1 hinges on C0.
  right wing the exact mirror of the left wing about x = 0 (positions, normals, tangents, binormals x negated; the
             tangent frame is stored explicitly, w = 1 everywhere, so no handedness sign changes; triangle winding
             reversed). Half-float negation is exact, so the stored right wing is the left one mirrored bit for bit.

Skeleton (preorder):  mdl (root, identity) -> body (identity: the plugin drives it with the heli body's pose)
                      -> wing_l -> wing_l2, -> wing_r -> wing_r2;  mdl -> edf6vc_primer_fighter (kind 2, object bone).
Wing bones: rotation rows X = model +z (forward), Y = model +y (up), Z = model -x, translation = hinge point. The
plugin poses a bone as Rx(theta) x bind local (src/jet_flight.cpp PoseSurfaces): theta > 0 lifts the bone's local -z
end. wing_l / wing_l2 (tips at model +x = local -z): theta > 0 raises them. wing_r / wing_r2 (tips at model -x =
local +z): theta < 0 raises them. A symmetric stroke is wing_l = +a, wing_r = -a.
The model is grounded (lowest vertex y = 0 with the wings at rest).
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import replace

import graft_pure as g
from graft_pure import Vec3
from mdb import (Bone, Mat, Mdb, Mesh, Object, Rab, RabFile, bind_world, cmpl_compress, cmpl_decompress, ident,
                 inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write)
from mdb_jet import link, pack_vertex, vertex_table

SRC_ARC, SRC_MDB = 'E611_TIMESHIP.MRAB', 'e611_timeship.mdb'
OUT_ARC, OUT_MDB = 'EDF6VC_PRIMER_FIGHTER.MRAB', 'edf6vc_primer_fighter.mdb'
# sha256 of the decompressed stock e611_timeship.mdb (a different game build stops the build instead of guessing)
SRC_SHA256 = '0c3da04b173b4725d90726366919f621f22e972097111be34c40d06978011b07'

POD_BONE = 'arm_H3'
POD_AZIMUTH = 22.5          # deg: arm H's outward axis, from +z toward +x (arms are 22.5 + k * 45 deg)
POD_SCALE = 0.4
HOOK_CUT_Y = -6.7           # source m: pod triangles with a vertex below this are the arm's hanging link
WING_BONES = ('hatch_C0', 'hatch_C1')   # rim panel, apex panel (the petal pair on the ship's -x side)
WING_SCALE = 0.34
WING_ROOT: Vec3 = (2.3, -1.1, 0.0)      # the left wing's hinge in the pod frame (pod centred on x and z, source y x scale)

BONES = ['mdl', 'body', 'wing_l', 'wing_l2', 'wing_r', 'wing_r2', 'edf6vc_primer_fighter']
PARENTS = [-1, 0, 1, 2, 1, 4, 0]
KINDS = [0, 3, 3, 3, 3, 3, 2]
BOUNDED = [0, 1, 1, 1, 1, 1, 0]
MIRROR = {'wing_l': 'wing_r', 'wing_l2': 'wing_r2'}
OBJ_BONE = 6
MAT_NAME = 7
# wing bone rotation rows: local X = model +z, Y = model +y, Z = model -x
WING_ROT: Mat = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0]
TEXTURE_MEMBERS = 3         # HD textures, each with its .lod variant


class PrimerFighterCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O."""
    if not ok:
        raise PrimerFighterCheckError(msg)


def member(rab: Rab, name: str) -> RabFile:
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


# ------------------------------------------------------------------------------------------ geometry helpers

Rot = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]


def rot_y(deg: float) -> Rot:
    """v' = R v about +y (z toward x for deg > 0)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, 0.0, s), (0.0, 1.0, 0.0), (-s, 0.0, c))


def rot_z(rad: float) -> Rot:
    """v' = R v, x toward y for rad > 0."""
    c, s = math.cos(rad), math.sin(rad)
    return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))


def apply(r: Rot, v: tuple[float, ...]) -> Vec3:
    return (r[0][0] * v[0] + r[0][1] * v[1] + r[0][2] * v[2], r[1][0] * v[0] + r[1][1] * v[1] + r[1][2] * v[2],
            r[2][0] * v[0] + r[2][1] * v[1] + r[2][2] * v[2])


def translation(t: Vec3) -> Mat:
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, t[0], t[1], t[2], 1.0]


class Part:
    """Picked source vertices (float rows) and triangles, re-indexed from 0."""

    def __init__(self, keys: list[str], rows: list[list[tuple[float, ...]]], tris: list[tuple[int, int, int]]) -> None:
        used = sorted({i for t in tris for i in t})
        remap = {v: k for k, v in enumerate(used)}
        self.keys = keys
        self.rows = [list(rows[v]) for v in used]
        self.tris = [(remap[a], remap[b], remap[c]) for a, b, c in tris]

    def key(self, name: str) -> int:
        return next(k for k, s in enumerate(self.keys) if s.split(':')[0].lower() == name.lower())


def pick(md: Mdb, keys: list[str], rows: list[list[tuple[float, ...]]], tris: list[tuple[int, int, int]],
         bones: set[int], cut_y: float | None = None) -> Part:
    bk = next(k for k, s in enumerate(keys) if s.split(':')[0].upper() == 'BLENDINDICES')
    pk = next(k for k, s in enumerate(keys) if s.split(':')[0].lower() == 'position')
    own = [int(r[bk][0]) in bones for r in rows]
    sel = [t for t in tris if all(own[i] for i in t) and (cut_y is None or all(rows[i][pk][1] >= cut_y for i in t))]
    return Part(keys, rows, sel)


def transform(part: Part, r: Rot, scale: float, pre: Vec3, post: Vec3) -> None:
    """In place: position -> R (p - pre) * scale + post; normal / tangent / binormal -> R v."""
    pk = part.key('position')
    vec = [part.key(n) for n in ('normal', 'tangent', 'binormal')]
    for row in part.rows:
        p = row[pk]
        q = apply(r, (p[0] - pre[0], p[1] - pre[1], p[2] - pre[2]))
        row[pk] = (q[0] * scale + post[0], q[1] * scale + post[1], q[2] * scale + post[2], p[3])
        for k in vec:
            v = apply(r, row[k])
            row[k] = (v[0], v[1], v[2], row[k][3])


def set_bones(part: Part, bone_of: dict[int, int]) -> None:
    bk = part.key('BLENDINDICES')
    for row in part.rows:
        i = row[bk]
        row[bk] = (bone_of[int(i[0])], 0, 0, 0)


def mirrored(part: Part, bone_map: dict[int, int]) -> Part:
    """The part mirrored about x = 0 (positions and tangent frame x negated, winding reversed, bones mapped)."""
    out = Part.__new__(Part)
    out.keys = part.keys
    flip = {part.key(n) for n in ('position', 'normal', 'tangent', 'binormal')}
    bk = part.key('BLENDINDICES')
    out.rows = []
    for row in part.rows:
        new = [((-v[0],) + tuple(v[1:])) if k in flip else v for k, v in enumerate(row)]
        new[bk] = (bone_map[int(row[bk][0])], 0, 0, 0)
        out.rows.append(new)
    out.tris = [(a, c, b) for a, b, c in part.tris]
    return out


def shift_y(part: Part, dy: float) -> None:
    pk = part.key('position')
    for row in part.rows:
        p = row[pk]
        row[pk] = (p[0], p[1] + dy, p[2], p[3])


# ------------------------------------------------------------------------------------------ build

def source(game) -> tuple[Rab, Mdb]:  # noqa: ANN001 - rootcpk.Game
    rab = rab_read(game.read('OBJECT', SRC_ARC))
    data = member(rab, SRC_MDB).data
    _req(hashlib.sha256(data).hexdigest() == SRC_SHA256, f'{SRC_MDB}: not the stock model this recipe was made for')
    md = mdb_read(data)
    _req(len(md.objects) == 1 and len(md.materials) == 1 and len(md.textures) == TEXTURE_MEMBERS, 'unexpected source layout')
    return rab, md


def build_model(game) -> tuple[Mdb, Rab, dict]:  # noqa: ANN001 - rootcpk.Game
    rab, src = source(game)
    me = src.objects[0].meshes[0]
    keys, rows = vertex_table(me)
    tris = g.triangles(me)
    w = bind_world(src)
    bi = {n: src.bone_index(n) for n in (POD_BONE,) + WING_BONES}
    _req(all(i >= 0 for i in bi.values()), f'source bones missing: {bi}')
    info: dict = {}

    # fuselage: the arm's outward axis -> +z, scaled, centred on x and z
    pod = pick(src, keys, rows, tris, {bi[POD_BONE]}, HOOK_CUT_Y)
    yaw = rot_y(-POD_AZIMUTH)
    transform(pod, yaw, POD_SCALE, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    pk = pod.key('position')
    xs, zs = [r[pk][0] for r in pod.rows], [r[pk][2] for r in pod.rows]
    centre = ((min(xs) + max(xs)) / 2, 0.0, (min(zs) + max(zs)) / 2)
    transform(pod, ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), 1.0, centre, (0.0, 0.0, 0.0))

    # left wing: petal C translated to +x (rim inboard), levelled about the hinge, scaled
    hinge = tuple(w[bi[WING_BONES[0]]][12:15])
    joint = tuple(w[bi[WING_BONES[1]]][12:15])
    wing = pick(src, keys, rows, tris, {bi[WING_BONES[0]], bi[WING_BONES[1]]})
    wk = wing.key('position')
    tip_x = max(r[wk][0] for r in wing.rows)
    tip_y = [r[wk][1] for r in wing.rows if r[wk][0] >= tip_x - 1e-3]
    tip = (tip_x, (min(tip_y) + max(tip_y)) / 2)
    level = -math.atan2(tip[1] - hinge[1], tip[0] - hinge[0])
    rz = rot_z(level)
    transform(wing, rz, WING_SCALE, hinge, WING_ROOT)  # type: ignore[arg-type]
    j = apply(rz, (joint[0] - hinge[0], joint[1] - hinge[1], joint[2] - hinge[2]))
    pivot_l: Vec3 = WING_ROOT
    pivot_l2: Vec3 = (WING_ROOT[0] + j[0] * WING_SCALE, WING_ROOT[1] + j[1] * WING_SCALE, WING_ROOT[2] + j[2] * WING_SCALE)
    info['level_deg'] = math.degrees(level)

    # bones: body 1, wing_l 2, wing_l2 3, wing_r 4, wing_r2 5
    set_bones(pod, {bi[POD_BONE]: 1})
    set_bones(wing, {bi[WING_BONES[0]]: 2, bi[WING_BONES[1]]: 3})
    right = mirrored(wing, {2: 4, 3: 5})

    # ground: lowest vertex onto y = 0 (a y shift commutes with the x mirror)
    low = min(r[pk][1] for p in (pod, wing, right) for r in p.rows)
    dy = -low
    for p in (pod, wing, right):
        shift_y(p, dy)
    up = lambda q: (q[0], q[1] + dy, q[2])  # noqa: E731
    piv = {2: up(pivot_l), 3: up(pivot_l2), 4: up((-pivot_l[0], pivot_l[1], pivot_l[2])),
           5: up((-pivot_l2[0], pivot_l2[1], pivot_l2[2]))}
    info['ground_lift'] = dy

    # one mesh: pod, left wing, right wing
    vdata = bytearray()
    idx: list[int] = []
    for p in (pod, wing, right):
        base = len(vdata) // me.vsize
        vdata += b''.join(pack_vertex(me.elems, me.vsize, r) for r in p.rows)
        idx += [base + i for t in p.tris for i in t]
    _req(len(vdata) // me.vsize < 0x10000, 'too many vertices')
    mesh = Mesh(bytes(me.flags), 0, me.unk08, me.vsize, list(me.elems), 0, bytes(vdata), struct.pack(f'<{len(idx)}H', *idx))

    # skeleton: world binds, then locals from the parent's
    world: list[Mat] = [ident(), ident()]
    for k in (2, 3, 4, 5):
        world.append(WING_ROT[:12] + [piv[k][0], piv[k][1], piv[k][2], 1.0])
    world.append(ident())
    zero = [0.0, 0.0, 0.0, 1.0]
    bones = []
    for k, name in enumerate(BONES):
        par = PARENTS[k]
        local = world[k] if par < 0 else mmul(world[k], inverse_affine(world[par]))
        bones.append(Bone(k, par, -1, -1, k, 0, KINDS[k], 0, BOUNDED[k], 0, 0, local, inverse_affine(world[k]),
                          list(zero), list(zero)))
    link(bones)
    names: list[str | None] = list(BONES) + [src.name_of(src.materials[0].name)]
    mat = replace(src.materials[0], index=0, name=MAT_NAME)
    md = Mdb(src.version, names, bones, [Object(OBJ_BONE, OBJ_BONE, [mesh])], [mat], list(src.textures))
    md = g.recompute_bounds(md)
    info['pivots'] = {BONES[k]: piv[k] for k in piv}
    info['tris'] = {'pod': len(pod.tris), 'wing': len(wing.tris)}
    return md, rab, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_PRIMER_FIGHTER.MRAB: the new model and every texture it uses (HD + .lod), stock bytes."""
    md, src_rab, _info = build_model(game)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    files = []
    for f in src_rab.files:
        if f.name.lower() == SRC_MDB.lower():
            files.append(RabFile(OUT_MDB, f.folder, f.flag, stored, f.unk))
        else:
            files.append(RabFile(f.name, f.folder, f.flag, f.stored, f.unk))
    arc = rab_write(Rab(src_rab.version, list(src_rab.folders), files))
    check(arc)
    return arc


# ------------------------------------------------------------------------------------------ check

def _positions(me: Mesh) -> list[Vec3]:
    return g.mesh_positions(me)


def check(arc: bytes) -> None:
    """Re-read `arc` and raise PrimerFighterCheckError unless: archive and model round-trip; members are the model
    plus every referenced texture (HD and .lod); the skeleton is BONES / PARENTS / KINDS with consistent links,
    bind x inverse bind = identity, mdl / body identity, wing bones at WING_ROT; the mesh's vertex / index buffers,
    blend indices (skin bones only), weights and material are valid; every skin bone carries geometry; the model is
    grounded; the right wing (vertices, tangent frames, triangles with reversed winding, pivots) mirrors the left
    within 1 mm."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, OUT_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    names = [md.name_of(b.name) for b in md.bones]
    _req(names == BONES, f'bones {names}')
    _req([b.parent for b in md.bones] == PARENTS, 'bone parents')
    _req([b.kind for b in md.bones] == KINDS and [b.bounded for b in md.bones] == BOUNDED, 'bone kinds / bounded')
    again = [replace(b) for b in md.bones]
    link(again)
    _req(all((a.sibling, a.child, a.child_count, a.depth_delta) == (b.sibling, b.child, b.child_count, b.depth_delta)
             for a, b in zip(again, md.bones)), 'bone links inconsistent')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-5, f'bone {names[b.index]}: bind x inverse bind off identity by {err}')
        want = WING_ROT if names[b.index].startswith('wing') else ident()
        _req(max(abs(w[b.index][k] - want[k]) for k in range(12)) < 1e-6, f'bone {names[b.index]}: rotation')
    _req(max(abs(x) for x in w[0][12:15] + w[1][12:15] + w[OBJ_BONE][12:15]) < 1e-6, 'mdl / body / object bone moved')
    nb = len(md.bones)
    files = {f.name.lower() for f in rab.files}
    _req(len(md.objects) == 1 and md.objects[0].bone == OBJ_BONE and len(md.objects[0].meshes) == 1, 'object layout')
    me = md.objects[0].meshes[0]
    nv = me.nverts
    _req(me.vsize > 0 and len(me.vdata) == nv * me.vsize and 0 < nv < 0x10000, 'vertex buffer')
    _req(len(me.indices) % 6 == 0 and me.indices, 'index count')
    idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
    _req(max(idx) < nv, f'index {max(idx)} >= {nv} vertices')
    _req(0 <= me.material < len(md.materials), 'material index')
    bi, bw = g.skin_columns(me)
    owner = []
    for r, wt in zip(bi, bw):
        _req(all(int(i) < nb for i in r) and md.bones[int(r[0])].kind == 3, f'blend index {r}')
        _req(abs(sum(wt) - 1.0) < 1e-3, f'weights sum {sum(wt)}')
        owner.append(int(r[0]))
    _req({k for k in range(nb) if KINDS[k] == 3} == set(owner), 'a skin bone without geometry')
    for m in md.materials:
        for x in m.textures:
            _req(0 <= x.texture < len(md.textures), f'texture slot {x.texture}')
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'texture member {want} missing')
    _req(len(files) == 1 + 2 * len(md.textures), f'unexpected members {sorted(files)}')
    P = _positions(me)
    low = min(p[1] for p in P)
    _req(abs(low) < 2e-3, f'lowest vertex y = {low}')

    # mirror: vertices with uv and tangent frame, triangles (winding reversed), pivots
    from mdb_jet import vertex_table as vt
    keys, rows = vt(me)
    kk = {s.split(':')[0].lower(): k for k, s in enumerate(keys)}
    vecs = [kk['position'], kk['normal'], kk['tangent'], kk['binormal']]

    def sig(row: list[tuple[float, ...]], flip: bool) -> tuple[float, ...]:
        out: list[float] = []
        for k in vecs:
            v = row[k]
            out += [-v[0] if flip else v[0], v[1], v[2]]
        return tuple(out) + tuple(row[kk['texcoord']])

    for l, r in MIRROR.items():
        il, ir = BONES.index(l), BONES.index(r)
        a = sorted(sig(rows[v], True) for v in range(nv) if owner[v] == il)
        b = sorted(sig(rows[v], False) for v in range(nv) if owner[v] == ir)
        _req(len(a) == len(b) and a, f'{l} / {r}: {len(a)} vs {len(b)} vertices')
        _req(all(max(abs(x - y) for x, y in zip(p, q)) < 1e-3 for p, q in zip(a, b)), f'{r} is not the mirror of {l}')
        pl, pr = w[il][12:15], w[ir][12:15]
        _req(abs(pl[0] + pr[0]) < 1e-3 and abs(pl[1] - pr[1]) < 1e-3 and abs(pl[2] - pr[2]) < 1e-3, f'{r} pivot not mirrored')
    side = {BONES.index(k) for k in MIRROR}, {BONES.index(k) for k in MIRROR.values()}

    def canon(t: tuple[Vec3, Vec3, Vec3]) -> tuple:
        k = min(range(3), key=lambda i: t[i])
        return t[k:] + t[:k]

    q = lambda p: tuple(round(c * 1000) for c in p)  # noqa: E731  (1 mm grid; stored values mirror exactly)
    tl, tr = [], []
    for t in zip(idx[0::3], idx[1::3], idx[2::3]):
        o = {owner[i] for i in t}
        if o <= side[0]:
            m = [q((-P[i][0], P[i][1], P[i][2])) for i in t]
            tl.append(canon((m[0], m[2], m[1])))
        elif o <= side[1]:
            tr.append(canon(tuple(q(P[i]) for i in t)))  # type: ignore[arg-type]
    _req(tl and sorted(tl) == sorted(tr), 'right wing triangles are not the mirrored left ones (winding reversed)')


# ------------------------------------------------------------------------------------------ report helpers

def rest_box(md: Mdb) -> tuple[list[float], list[float]]:
    """[centre, half extents] of every vertex at rest."""
    P = _positions(md.objects[0].meshes[0])
    lo = [min(p[c] for p in P) for c in range(3)]
    hi = [max(p[c] for p in P) for c in range(3)]
    return [(l + h) / 2 for l, h in zip(lo, hi)], [(h - l) / 2 for l, h in zip(lo, hi)]
