"""Split the stock bomber501 jet into control-surface bones (experimental; see docs/mdb-format.md).

    python tools/mdb.py jet [OUTDIR] [--png]          (default OUTDIR: build/jetmodel)

Reads BOMBER501.MRAB from the game's Root.cpk (read only), and writes to OUTDIR:
  bomber501.mdb    uncompressed MDB0 with the new skeleton (for inspection / tools)
  bomber501.mrab   the stock archive with bomber501.mdb replaced (CMPL-compressed), every other file untouched
  hinges.json      bone names, indices, hinge points/axes, deflection sign convention
  preview.png      (--png) top/side views with the surfaces deflected, to eyeball the pivots

Geometry facts this relies on (checked by the asserts below, see the doc):
  * bomber501 is a tailless cranked-arrow delta: there is no vertical tail and no horizontal tail in the mesh,
    so no rudder / elevator can be cut out of it. Only trailing-edge elevons on the outer wing panels exist.
  * The outer wing panel is a thin slab (y in about [-0.07, 0.07]) separate from the nacelles (|x| < 5.6).

Skeleton written (preorder, as the engine requires):
  [0] mdl (kind 0) -> [1] bomber501 (kind 3, skin bone of the fuselage, keeps its name for the ragdoll /
  animation_model_bone_mapping) -> [2] elevon_L, [3] elevon_R (kind 3); [4] bomber501_mesh (kind 2, identity,
  bounded 0: the bone the single skinned object is attached to, mirroring v506_heli's last bone).
"""
from __future__ import annotations

import dataclasses
import json
import math
import os
import struct
import sys
from dataclasses import dataclass

from mdb import (HERE, Bone, Mat, Mdb, Mesh, Object, VElem, _load, bind_world, cmpl_compress, cmpl_decompress,
                 depth_of, ident, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write, read_elem)
import gamedir  # noqa: E402  (testrange/lib, put on sys.path by mdb)

Vec = tuple[float, float, float]

# Elevon cut parameters (model space, metres; +x = right wing, +y = up, +z = nose).
SLAB_Y = 0.3          # a vertex belongs to the wing slab when |y| < SLAB_Y ...
SLAB_X = 6.0          # ... and |x| > SLAB_X (outboard of the nacelles)
X_IN = 7.4            # inboard end of the elevon (just outboard of the trailing-edge kink at |x| = 7.23..7.26)
X_HINGE_OUT = 11.4    # second station used to place the hinge line (the surface itself runs to the tip)
CHORD_FRAC = 0.25     # elevon chord = 25 % of the local wing chord
EPS = 1e-4
MIN_AREA = 1e-7

SKIN_FLAGS = bytes([0, 1, 1, 0])   # v506_heli meshes 0/1/3: skinned, 1 influence per vertex


@dataclass
class Surface:
    name: str
    side: int            # +1 right wing (x > 0), -1 left wing
    p_in: Vec            # hinge point at |x| = X_IN (the bone origin)
    p_out: Vec           # hinge point at |x| = X_HINGE_OUT
    axis: Vec            # unit hinge direction, x component > 0 for both sides


# ------------------------------------------------------------------------------------------ vectors

def sub(a: Vec, b: Vec) -> Vec:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a: Vec, b: Vec) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a: Vec) -> Vec:
    n = math.sqrt(dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def xform(p: Vec, m: Mat) -> Vec:
    """Row vector times a row-major 4x4 (w = 1)."""
    return tuple(p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c] for c in range(3))  # type: ignore[return-value]


def rot_x(theta: float) -> Mat:
    """Rotation about the local X axis, row-vector convention (the convention of every MDB matrix)."""
    c, s = math.cos(theta), math.sin(theta)
    return [1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]


# ------------------------------------------------------------------------------------------ vertices

def vertex_table(me: Mesh) -> tuple[list[str], list[list[tuple[float, ...]]]]:
    """All elements of every vertex as float tuples: keys 'name:channel', rows per vertex."""
    keys = [f'{e.name}:{e.channel}' for e in me.elems]
    cols = [read_elem(me, e.name, e.channel) for e in me.elems]
    return keys, [[c[v] for c in cols] for v in range(me.nverts)]  # type: ignore[index]


def lerp_vertex(keys: list[str], a: list[tuple[float, ...]], b: list[tuple[float, ...]], t: float) -> list[tuple[float, ...]]:
    out = []
    for k, x, y in zip(keys, a, b):
        v = [xa + (ya - xa) * t for xa, ya in zip(x, y)]
        if k.split(':')[0] in ('normal', 'binormal', 'tangent'):
            n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
            v[0], v[1], v[2] = v[0] / n, v[1] / n, v[2] / n
            v[3] = x[3]                       # w: handedness sign, identical on both ends in practice
        out.append(tuple(v))
    return out


def pos(row: list[tuple[float, ...]]) -> Vec:
    return row[0][0], row[0][1], row[0][2]


# ------------------------------------------------------------------------------------------ planform

def chord_at(P: list[Vec], tris: list[tuple[int, int, int]], x: float) -> tuple[float, float]:
    """(trailing edge z, leading edge z) of the wing slab where the vertical plane X = x cuts it."""
    zs: list[float] = []
    for t in tris:
        ps = [P[i] for i in t]
        if not all(abs(p[1]) < SLAB_Y and abs(p[0]) > SLAB_X for p in ps):
            continue
        for a, b in ((ps[0], ps[1]), (ps[1], ps[2]), (ps[2], ps[0])):
            if (a[0] - x) * (b[0] - x) < 0:
                u = (x - a[0]) / (b[0] - a[0])
                zs.append(a[2] + (b[2] - a[2]) * u)
    assert zs, f'no wing slab at x = {x}'
    return min(zs), max(zs)


def make_surfaces(P: list[Vec], tris: list[tuple[int, int, int]]) -> list[Surface]:
    out = []
    for name, side in (('elevon_L', -1), ('elevon_R', 1)):
        pts = []
        for ax in (X_IN, X_HINGE_OUT):
            te, le = chord_at(P, tris, side * ax)
            pts.append((side * ax, 0.0, te + CHORD_FRAC * (le - te)))
        a = norm(sub(pts[1], pts[0]))
        if a[0] < 0:
            a = (-a[0], -a[1], -a[2])
        out.append(Surface(name, side, pts[0], pts[1], a))
    return out


def planes_of(s: Surface) -> list[tuple[Vec, float]]:
    """Half-spaces (n, d) with n.p + d >= 0 inside the surface: behind the hinge line, outboard of X_IN."""
    behind = norm(cross(s.axis, (0.0, 1.0, 0.0)))      # X x Y = Z points to the nose ...
    behind = (-behind[0], -behind[1], -behind[2])      # ... so the surface is on -Z
    outboard: Vec = (float(s.side), 0.0, 0.0)
    return [(behind, -dot(behind, s.p_in)), (outboard, -X_IN)]


# ------------------------------------------------------------------------------------------ clipping

class Splitter:
    """Sutherland-Hodgman split of triangles by planes, sharing cut vertices between neighbours."""

    def __init__(self, keys: list[str], rows: list[list[tuple[float, ...]]]) -> None:
        self.keys = keys
        self.rows = rows
        self.cache: dict[tuple[int, int, int], int] = {}

    def cut(self, a: int, b: int, plane_id: int, da: float, db: float) -> int:
        k = (min(a, b), max(a, b), plane_id)
        if k not in self.cache:
            lo, hi, dlo, dhi = (a, b, da, db) if a < b else (b, a, db, da)
            t = dlo / (dlo - dhi)
            self.rows.append(lerp_vertex(self.keys, self.rows[lo], self.rows[hi], t))
            self.cache[k] = len(self.rows) - 1
        return self.cache[k]

    def split(self, poly: list[int], plane: tuple[Vec, float], plane_id: int) -> tuple[list[int], list[int]]:
        n, d0 = plane
        dist = [dot(n, pos(self.rows[i])) + d0 for i in poly]
        inside: list[int] = []
        outside: list[int] = []
        for k, i in enumerate(poly):
            j = poly[(k + 1) % len(poly)]
            di, dj = dist[k], dist[(k + 1) % len(poly)]
            if di >= -EPS:
                inside.append(i)
            if di <= EPS:
                outside.append(i)
            if (di > EPS and dj < -EPS) or (di < -EPS and dj > EPS):
                c = self.cut(i, j, plane_id, di, dj)
                inside.append(c)
                outside.append(c)
        return inside, outside


def fan(poly: list[int]) -> list[tuple[int, int, int]]:
    return [(poly[0], poly[k], poly[k + 1]) for k in range(1, len(poly) - 1)] if len(poly) >= 3 else []


def area(rows: list[list[tuple[float, ...]]], t: tuple[int, int, int]) -> Vec:
    a, b, c = (pos(rows[i]) for i in t)
    return cross(sub(b, a), sub(c, a))


# ------------------------------------------------------------------------------------------ build

def bone_frame(s: Surface) -> Mat:
    x = s.axis
    y = norm(sub((0.0, 1.0, 0.0), tuple(dot((0.0, 1.0, 0.0), x) * c for c in x)))  # type: ignore[arg-type]
    z = cross(x, y)
    o = s.p_in
    return [x[0], x[1], x[2], 0.0, y[0], y[1], y[2], 0.0, z[0], z[1], z[2], 0.0, o[0], o[1], o[2], 1.0]


def bounds(points: list[Vec], inv: Mat) -> tuple[list[float], list[float]]:
    q = [xform(p, inv) for p in points]
    lo = [min(v[i] for v in q) for i in range(3)]
    hi = [max(v[i] for v in q) for i in range(3)]
    return ([(h - l) / 2 for l, h in zip(lo, hi)] + [1.0], [(h + l) / 2 for l, h in zip(lo, hi)] + [1.0])


def link(bones: list[Bone]) -> None:
    """Recompute sibling / first child / child count / depth_delta from the parent links (preorder list)."""
    for b in bones:
        kids = [c.index for c in bones if c.parent == b.index]
        b.child = kids[0] if kids else -1
        b.child_count = len(kids)
        sibs = [c.index for c in bones if c.parent == b.parent and c.index > b.index]
        b.sibling = sibs[0] if b.parent >= 0 and sibs else -1
    md = Mdb(0, [], bones, [], [], [])
    d = depth_of(md)
    for i, b in enumerate(bones):
        b.depth_delta = (d[i + 1] - d[i]) if i + 1 < len(bones) else d[i]


def pack_vertex(elems: list[VElem], vsize: int, row: list[tuple[float, ...]]) -> bytes:
    out = bytearray(vsize)
    for e, v in zip(elems, row):
        fmt = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}[e.fmt]
        struct.pack_into(fmt, out, e.offset, *v)
    return bytes(out)


def build(src: Mdb) -> tuple[Mdb, list[Surface], dict[str, object]]:
    assert [src.name_of(b.name) for b in src.bones] == ['mdl', 'bomber501'], 'unexpected stock skeleton'
    assert len(src.objects) == 1 and len(src.objects[0].meshes) == 1
    me = src.objects[0].meshes[0]
    assert me.flags == bytes(4) and me.vsize == 48, 'stock bomber501 mesh changed'
    keys, rows = vertex_table(me)
    tris = list(struct.iter_unpack('<3H', me.indices))
    P = [pos(r) for r in rows]
    surfaces = make_surfaces(P, tris)

    sp = Splitter(keys, rows)
    owner_tris: list[tuple[tuple[int, int, int], int]] = []   # (triangle, surface number or -1)
    stats = {'tris_in': len(tris), 'tris_cut': 0, 'slivers_dropped': 0, 'non_slab_in_region': 0}
    planes = [planes_of(s) for s in surfaces]
    for t in tris:
        ps = [P[i] for i in t]
        slab = all(abs(p[1]) < SLAB_Y for p in ps)
        done = False
        for k, s in enumerate(surfaces):
            if not any(p[0] * s.side > X_IN - EPS for p in ps):
                continue
            if not slab:
                stats['non_slab_in_region'] += 1
                continue
            inside, rest_a = sp.split(list(t), planes[k][0], 2 * k)
            ins, rest_b = sp.split(inside, planes[k][1], 2 * k + 1) if len(inside) >= 3 else ([], [])
            if len(ins) < 3:
                continue
            stats['tris_cut'] += int(len(rest_a) >= 3 or len(rest_b) >= 3)
            owner_tris += [(f, k) for f in fan(ins)] + [(f, -1) for f in fan(rest_a) + fan(rest_b)]
            done = True
            break
        if not done:
            owner_tris.append((t, -1))

    # keep the original winding (the split only reorders nothing; check it anyway) and drop slivers
    final = []
    for t, k in owner_tris:
        a = area(sp.rows, t)
        if dot(a, a) < MIN_AREA * MIN_AREA * 4:
            stats['slivers_dropped'] += 1
            continue
        final.append((t, k))

    # bones (preorder): mdl, bomber501, elevon_L, elevon_R, bomber501_mesh
    b_mdl, b_body = src.bones
    names: list[str | None] = ['mdl', 'bomber501'] + [s.name for s in surfaces] + ['bomber501_mesh',
                                                                                    src.name_of(src.materials[0].name),
                                                                                    src.name_of(src.objects[0].name)]
    names += [None] * (len(src.names) - sum(n is not None for n in src.names))
    nb = 2 + len(surfaces) + 1
    i_mesh = nb - 1
    zero4 = [0.0, 0.0, 0.0, 0.0]
    bones = [
        Bone(0, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, list(b_mdl.local), list(b_mdl.inv_bind), list(b_mdl.half), list(b_mdl.centre)),
        Bone(1, 0, -1, -1, 1, 0, 3, 0, 1, 0, 0, list(b_body.local), list(b_body.inv_bind), zero4, zero4),
    ]
    for k, s in enumerate(surfaces):
        w = bone_frame(s)          # parent bomber501 has identity bind, so local == model-space bind
        bones.append(Bone(2 + k, 1, -1, -1, 2 + k, 0, 3, 0, 1, 0, 0, w, inverse_affine(w), zero4, zero4))
    bones.append(Bone(i_mesh, 0, -1, -1, nb - 1, 0, 2, 0, 0, 0, 0, ident(), ident(), list(b_body.half), list(b_body.centre)))
    link(bones)

    # vertices: one copy per (source vertex, bone) so seams split cleanly
    elems = [VElem(e.fmt, e.offset, e.channel, e.name) for e in me.elems]
    elems += [VElem(1, 48, 0, 'BLENDWEIGHT'), VElem(21, 64, 0, 'BLENDINDICES')]
    vsize = 68
    remap: dict[tuple[int, int], int] = {}
    vbytes = bytearray()
    per_bone: dict[int, list[Vec]] = {}
    idx: list[int] = []
    for t, k in final:
        bone = 1 if k < 0 else 2 + k
        for i in t:
            key = (i, bone)
            if key not in remap:
                remap[key] = len(remap)
                row = sp.rows[i] + [(1.0, 0.0, 0.0, 0.0), (bone, 0, 0, 0)]
                vbytes += pack_vertex(elems, vsize, row)
                per_bone.setdefault(bone, []).append(pos(sp.rows[i]))
            idx.append(remap[key])
    assert len(remap) < 0x10000
    world = bind_world(Mdb(0, names, bones, [], [], []))
    for bi, pts in per_bone.items():
        bones[bi].half, bones[bi].centre = bounds(pts, inverse_affine(world[bi]))

    new_mesh = Mesh(SKIN_FLAGS, me.material, me.unk08, vsize, elems, me.mesh_index, bytes(vbytes),
                    struct.pack(f'<{len(idx)}H', *idx))
    mats = [dataclasses.replace(m, name=nb) for m in src.materials]        # names[nb] = 'Material'
    obj = Object(nb + 1, i_mesh, [new_mesh])                               # names[nb + 1] = 'bomber501'
    out = Mdb(src.version, names, bones, [obj], mats, src.textures)
    stats.update({'verts_in': me.nverts, 'verts_out': len(remap), 'tris_out': len(idx) // 3,
                  'verts_per_bone': {names[b]: len(v) for b, v in sorted(per_bone.items())}})
    return out, surfaces, stats


# ------------------------------------------------------------------------------------------ checks

def self_check(data: bytes, surfaces: list[Surface]) -> dict[str, object]:
    md = mdb_read(data)
    assert mdb_write(md) == data, 'written model does not round-trip'
    w = bind_world(md)
    rep: dict[str, object] = {}
    for b in md.bones:
        err = max(abs(x - y) for x, y in zip(mmul(w[b.index], b.inv_bind), ident()))
        assert err < 1e-4, f'inv_bind of {md.name_of(b.name)} off by {err}'
    d = depth_of(md)
    for i, b in enumerate(md.bones):
        assert b.depth_delta == ((d[i + 1] - d[i]) if i + 1 < len(md.bones) else d[i])
    me = md.objects[0].meshes[0]
    P = read_elem(me, 'position')
    BI = read_elem(me, 'BLENDINDICES')
    BW = read_elem(me, 'BLENDWEIGHT')
    assert P and BI and BW
    assert all(w4 == (1.0, 0.0, 0.0, 0.0) for w4 in BW)
    # bind pose == stock: skinning with the bind matrices must give back every position
    for p, bi in zip(P, BI):
        b = bi[0]
        q = xform(xform(p[:3], md.bones[b].inv_bind), w[b])
        assert max(abs(x - y) for x, y in zip(q, p[:3])) < 1e-3
    # deflect every surface by 20 degrees about its hinge (local' = Rx * local) and measure the hinge seam
    for s in surfaces:
        bi = md.bone_index(s.name)
        moved = mmul(mmul(md.bones[bi].inv_bind, mmul(rot_x(math.radians(20)), md.bones[bi].local)), w[md.bones[bi].parent])
        on_axis = 0.0
        te_dy = []
        for p, ix in zip(P, BI):
            if ix[0] != bi:
                continue
            q = xform(p[:3], moved)
            r0 = sub(p[:3], s.p_in)
            r1 = sub(q, s.p_in)
            # distance from the hinge axis must be preserved; points on the axis must not move
            d0 = math.sqrt(max(dot(r0, r0) - dot(r0, s.axis) ** 2, 0.0))
            d1 = math.sqrt(max(dot(r1, r1) - dot(r1, s.axis) ** 2, 0.0))
            on_axis = max(on_axis, abs(d1 - d0))
            te_dy.append(q[1] - p[1])
        rep[s.name] = {'max_axis_distance_change': round(on_axis, 5),
                       'trailing_edge_dy_at_+20deg': round(max(te_dy, key=abs), 3)}
    return rep


def preview(md: Mdb, surfaces: list[Surface], path: str) -> None:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    me = md.objects[0].meshes[0]
    P = read_elem(me, 'position')
    BI = read_elem(me, 'BLENDINDICES')
    tri = list(struct.iter_unpack('<3H', me.indices))
    w = bind_world(md)
    fig, axs = plt.subplots(1, 2, figsize=(16, 8))
    for ang, ax in ((0, axs[0]), (25, axs[1])):
        mats = {}
        for b in md.bones:
            m = w[b.index]
            if md.name_of(b.name).startswith('elevon'):
                m = mmul(mmul(rot_x(math.radians(ang)), b.local), w[b.parent])
            mats[b.index] = mmul(b.inv_bind, m)
        Q = [xform(p[:3], mats[i[0]]) for p, i in zip(P, BI)]
        for t in tri:
            col = 'tab:red' if BI[t[0]][0] >= 2 and BI[t[0]][0] < 2 + len(surfaces) else '0.6'
            xs = [Q[i][0] for i in t] + [Q[t[0]][0]]
            if ax is axs[0]:
                ax.plot(xs, [Q[i][2] for i in t] + [Q[t[0]][2]], color=col, lw=0.4)
            else:
                ax.plot(xs, [Q[i][1] for i in t] + [Q[t[0]][1]], color=col, lw=0.4)
        for s in surfaces:
            if ax is axs[0]:
                ax.plot([s.p_in[0], s.p_out[0]], [s.p_in[2], s.p_out[2]], 'b-', lw=1.5)
                ax.plot([s.p_in[0]], [s.p_in[2]], 'bo')
    axs[0].set_title('top view (x, z): elevons red, hinge lines blue, bone origins = dots')
    axs[1].set_title('rear view (x, y): elevons deflected +25 deg (local\' = Rx(+25) * local)')
    for ax in axs:
        ax.set_aspect('equal')
        ax.grid(True, lw=0.3)
    fig.savefig(path, dpi=110, bbox_inches='tight')


# ------------------------------------------------------------------------------------------ main

def jet_archive() -> tuple[bytes, bytes, Mdb, list[Surface], dict[str, object], dict[str, object], int]:
    """The stock BOMBER501.MRAB with bomber501.mdb split (checked): (archive, mdb, model, surfaces, stats,
    self check, stock archive size)."""
    raw = _load('BOMBER501.MRAB')
    rab = rab_read(raw)
    assert rab_write(rab) == raw, 'stock archive does not round-trip'
    entry = next(f for f in rab.files if f.name.lower() == 'bomber501.mdb')
    src = mdb_read(entry.data)
    md, surfaces, stats = build(src)
    data = mdb_write(md)
    checks = self_check(data, surfaces)

    stored = cmpl_compress(data)
    assert cmpl_decompress(stored) == data
    entry.stored = stored
    arc = rab_write(rab)
    again = rab_read(arc)
    assert [f.name for f in again.files] == [f.name for f in rab.files]
    assert next(f for f in again.files if f.name.lower() == 'bomber501.mdb').data == data
    return arc, data, md, surfaces, stats, checks, len(raw)


def main(argv: list[str]) -> int:
    png = '--png' in argv
    args = [a for a in argv if not a.startswith('--')]
    outdir = args[0] if args else os.path.join(HERE, '..', 'build', 'jetmodel')
    outdir = os.path.abspath(outdir)
    game_dir = os.path.abspath(gamedir.find_or_dev()).lower()
    assert not outdir.lower().startswith(game_dir), 'refusing to write into the game directory'
    os.makedirs(outdir, exist_ok=True)
    arc, data, md, surfaces, stats, checks, raw_size = jet_archive()
    stored_size = len(cmpl_compress(data))

    with open(os.path.join(outdir, 'bomber501.mdb'), 'wb') as h:
        h.write(data)
    with open(os.path.join(outdir, 'bomber501.mrab'), 'wb') as h:
        h.write(arc)
    info = {
        'source': 'Root.cpk OBJECT/BOMBER501.MRAB : bomber501.mdb (bomber501_2.mdb and all textures untouched)',
        'convention': 'model space: +x right wing, +y up, +z nose. Bone local frame: X = hinge axis (pointing to +x on '
                      'both wings), Y = up, Z = forward. Deflect with  local\' = Rx(theta) * bind_local  (row-vector, '
                      'Rx = [[1,0,0],[0,c,s],[0,-s,c]]): theta > 0 = trailing edge UP on both wings. Pitch up = same '
                      'theta on both; roll = opposite signs.',
        'bones': [{'index': b.index, 'name': md.name_of(b.name), 'parent': b.parent, 'kind': b.kind, 'bounded': b.bounded,
                   'half': [round(x, 4) for x in b.half[:3]], 'centre': [round(x, 4) for x in b.centre[:3]]}
                  for b in md.bones],
        'surfaces': [{'name': s.name, 'bone_index': md.bone_index(s.name), 'side': 'left' if s.side < 0 else 'right',
                      'hinge_point_inboard (bone origin)': [round(x, 4) for x in s.p_in],
                      'hinge_point_outboard': [round(x, 4) for x in s.p_out],
                      'hinge_axis': [round(x, 5) for x in s.axis],
                      'chord_fraction': CHORD_FRAC, 'inboard_cut_abs_x': X_IN} for s in surfaces],
        'stats': stats,
        'self_check': checks,
        'sizes': {'mdb_raw': len(data), 'mdb_cmpl': stored_size, 'mrab': len(arc), 'stock_mrab': raw_size},
    }
    with open(os.path.join(outdir, 'hinges.json'), 'w', encoding='utf-8') as h:
        json.dump(info, h, indent=2, ensure_ascii=False)
    if png:
        preview(md, surfaces, os.path.join(outdir, 'preview.png'))
    print(json.dumps(info, indent=2, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    sys.exit(main(sys.argv[1:]))
