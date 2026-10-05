"""EDF6VC_CHUTE.MRAB / .SGO: the player's parachute canopy (src/playerjet.cpp Chute*), built from the player's own
Root.cpk (pure Python: pylib + graft_pure, no numpy / PIL, so the installer can run it).

    canopy()        -> (vertices, triangles)  the geometry alone, game-free (tools/selftest.py checks it)
    build(game)     -> bytes                  the finished archive (game: rootcpk.Game, read only)
    sgo(game)       -> bytes                  the object that shows it (a stock FarEventObject's SGO, its model ours)
    check(arc)      -> None                   raises ChuteCheckError on any self-check failure

The model, in the object's frame (y up, +z its front; the plugin puts the object CANOPY_UP over the player's feet):
  - a dome canopy: half an ellipsoid RADIUS across the rim and HEIGHT tall, its rim RIM_Y up, open underneath, two
    shells (the outside facing out, the inside facing in, SHELL_GAP apart) so it shows from above and from below
    whatever the face culling;
  - LINES suspension lines from the rim down to the riser point RISER_Y (the jumper's shoulders), each two crossed
    double-sided strips LINE_WIDTH wide.
One mesh, skinned to one bone (bind = identity), with the Grape's seat fabric (OBJECT/VEHICLE401_STRIKER.MRAB, its
v401_interiorSheet_* textures: a small tiling cloth weave) as its material: that material (snd_BRDF_Common_SeparateOcc)
and its mesh layout are copied as they are, the occlusion map's coordinates (texcoord1) pinned to the brightest block of
v401_inner_occ.DDS (the canopy has no baked occlusion of its own). The skeleton: mdl (root) / edf6vc_chute (the object's
bone, kind 2) / canopy (the skin bone, kind 3), the stock Grape's first three bones' shape.

Why a FarEventObject (docs/player-jet-re.md §8): its update (0x5C4FB0) only renders the model at the object's matrix
(+0x60, scaled by its setting.scale) and plays an optional default animation; it builds a physics body only for a
"ragdoll" entry (0x5C4970), which this SGO has none of: nothing to collide with, push or shoot.
"""
from __future__ import annotations

import math
import struct
from dataclasses import replace

import graft_pure as g
from mdb import (Bone, Mdb, Mesh, Object, Rab, RabFile, Texture, cmpl_compress, cmpl_decompress, mdb_read, mdb_write,
                 rab_read, rab_write, read_elem)
from mdb_jet import pack_vertex

OUT_ARC = 'EDF6VC_CHUTE.MRAB'
OUT_MDB = 'edf6vc_chute.mdb'
SGO_FILE = 'EDF6VC_CHUTE.SGO'
DONOR_ARC, DONOR_MDB = 'VEHICLE401_STRIKER.MRAB', 'Vehicle401_STRIKER.mdb'
FABRIC = 'v401_interiorSheet_col.DDS'      # the albedo of the donor material taken
STOCK_SGO = 'EV601_PLANT.SGO'              # a stock FarEventObject (the far-off plant): its SGO, its model ours

# The canopy (m), in the object's frame; the object sits CANOPY_UP over the player's feet (src/playerjet.cpp kChuteUp).
CANOPY_UP = 4.0
RADIUS = 3.75            # ~7.5 m across the rim
HEIGHT = 3.0             # rim to crown
RIM_Y = 2.0              # the rim over the object's origin (6 m over the feet)
RISER_Y = -2.4           # where the lines meet: the jumper's shoulders (1.6 m over the feet)
SEGMENTS = 32            # around
RINGS = 10               # rim to crown
SHELL_GAP = 0.02         # the inside shell under the outside one
LINES = 16
LINE_WIDTH = 0.04
TILE = 1.0               # m of canopy per repeat of the 128 px weave (the seats': 1 uv per m)

Vertex = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float],
               tuple[float, float, float], tuple[float, float]]     # position, normal, tangent (dP/du), binormal (dP/dv), uv
Tri = tuple[int, int, int]


class ChuteCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise ChuteCheckError(msg)


def _norm(v: tuple[float, float, float]) -> tuple[float, float, float]:
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return v[0] / n, v[1] / n, v[2] / n


def _cross(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]


def _sub(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def _neg(v: tuple[float, float, float]) -> tuple[float, float, float]:
    return -v[0], -v[1], -v[2]


def facing(verts: list[Vertex], t: Tri) -> float:
    """cross(b - a, c - a) . normal(a): the stock models' front faces have it > 0 (V401 / V506, every triangle)."""
    a, b, c = (verts[i][0] for i in t)
    n = _cross(_sub(b, a), _sub(c, a))
    return sum(n[k] * verts[t[0]][1][k] for k in range(3))


def _dome(verts: list[Vertex], tris: list[Tri], inside: bool) -> None:
    """One shell: (SEGMENTS + 1) x (RINGS + 1) vertices (the seam column doubled for the texture), quads rim to
    crown, the crown row's quads as single triangles (its vertices meet at the apex)."""
    a, b = RADIUS - (SHELL_GAP if inside else 0.0), HEIGHT - (SHELL_GAP if inside else 0.0)
    tiles_u = max(1, round(2.0 * math.pi * RADIUS / TILE))
    first = len(verts)
    arc = [0.0]   # arc length rim -> crown along a meridian, per ring
    for k in range(1, RINGS + 1):
        t0, t1 = (k - 1) / RINGS * math.pi / 2, k / RINGS * math.pi / 2
        arc.append(arc[-1] + math.hypot(a * (math.cos(t1) - math.cos(t0)), b * (math.sin(t1) - math.sin(t0))))
    for k in range(RINGS + 1):
        th = k / RINGS * math.pi / 2
        for s in range(SEGMENTS + 1):
            ph = s / SEGMENTS * 2.0 * math.pi
            c, sn = math.cos(ph), math.sin(ph)
            p = (a * math.cos(th) * c, RIM_Y + b * math.sin(th), a * math.cos(th) * sn)
            n = _norm((math.cos(th) * c / a, math.sin(th) / b, math.cos(th) * sn / a)) if k < RINGS else (0.0, 1.0, 0.0)
            # u around (phi rising), v up the meridian; tangents as the stock layout has them (dP/du, dP/dv)
            du = _norm((-sn, 0.0, c))
            dv = _norm(_cross(n, du)) if k < RINGS else _norm((-c, 0.0, -sn))
            if inside:
                n = _neg(n)
            verts.append((p, n, du, dv, (s / SEGMENTS * tiles_u, arc[k] / TILE)))
    row = SEGMENTS + 1
    for k in range(RINGS):
        for s in range(SEGMENTS):
            v00, v01 = first + k * row + s, first + k * row + s + 1
            v10, v11 = first + (k + 1) * row + s, first + (k + 1) * row + s + 1
            quad = [(v00, v10, v01), (v01, v10, v11)] if k < RINGS - 1 else [(v00, v10, v01)]
            for t in quad:
                tris.append((t[0], t[2], t[1]) if inside else t)


def _strip(verts: list[Vertex], tris: list[Tri], top: tuple[float, float, float], bottom: tuple[float, float, float],
           side: tuple[float, float, float]) -> None:
    """A double-sided strip from `top` to `bottom`, LINE_WIDTH wide along `side`."""
    h = LINE_WIDTH / 2
    along = _norm(_sub(bottom, top))
    n = _norm(_cross(side, along))
    length = math.dist(top, bottom)
    for face in (n, _neg(n)):
        first = len(verts)
        for p, uv in (((top[0] - side[0] * h, top[1] - side[1] * h, top[2] - side[2] * h), (0.0, 0.0)),
                      ((top[0] + side[0] * h, top[1] + side[1] * h, top[2] + side[2] * h), (0.05, 0.0)),
                      ((bottom[0] - side[0] * h, bottom[1] - side[1] * h, bottom[2] - side[2] * h), (0.0, length / TILE)),
                      ((bottom[0] + side[0] * h, bottom[1] + side[1] * h, bottom[2] + side[2] * h), (0.05, length / TILE))):
            verts.append((p, face, side, along, uv))
        for t in ((first, first + 1, first + 2), (first + 1, first + 3, first + 2)):
            tris.append(t if facing(verts, t) > 0 else (t[0], t[2], t[1]))


def canopy() -> tuple[list[Vertex], list[Tri]]:
    """The canopy and its lines, in the object's frame (see the module doc), front faces as the stock models'."""
    verts: list[Vertex] = []
    tris: list[Tri] = []
    _dome(verts, tris, inside=False)
    _dome(verts, tris, inside=True)
    riser = (0.0, RISER_Y, 0.0)
    for i in range(LINES):
        ph = (i + 0.5) / LINES * 2.0 * math.pi
        rim = (RADIUS * math.cos(ph), RIM_Y, RADIUS * math.sin(ph))
        across = _norm((-math.sin(ph), 0.0, math.cos(ph)))       # along the rim
        out = _norm(_cross(across, _norm(_sub(riser, rim))))     # off the line's plane
        _strip(verts, tris, rim, riser, across)
        _strip(verts, tris, rim, riser, out)
    return verts, tris


# ------------------------------------------------------------------------------------------ the model


def member(rab: Rab, name: str) -> RabFile:
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def brightest_block(dds: bytes) -> tuple[float, float]:
    """The uv centre of the DXT1 block of `dds`'s top mip whose darker end colour is the brightest (an occlusion
    map: the least occluded spot), first one on ties."""
    _req(dds[:4] == b'DDS ' and dds[84:88] == b'DXT1', 'occlusion map is not a DXT1 DDS')
    h, w = struct.unpack_from('<II', dds, 12)
    bw, bh = max(1, w // 4), max(1, h // 4)
    best, at = -1, 0
    for k in range(bw * bh):
        c0, c1 = struct.unpack_from('<HH', dds, 128 + k * 8)
        lum = min(((c >> 11) & 31) * 2 + ((c >> 5) & 63) + (c & 31) * 2 for c in (c0, c1))
        if c0 <= c1 and c0 != c1:
            continue   # 3-colour mode: index 3 is black
        if lum > best:
            best, at = lum, k
    return ((at % bw) * 4 + 2) / w, ((at // bw) * 4 + 2) / h


def _row(elems: list, v: Vertex, occ: tuple[float, float], bone: int) -> list[tuple[float, ...]]:
    p, n, t, b, uv = v
    out = []
    for e in elems:
        k = e.name.lower()
        out.append({'position': (*p, 1.0), 'normal': (*n, 1.0), 'tangent': (*t, 1.0), 'binormal': (*b, 1.0),
                    'texcoord': uv if e.channel == 0 else occ, 'blendweight': (1.0, 0.0, 0.0, 0.0),
                    'blendindices': (bone, 0, 0, 0)}[k])
    return out


def model(donor: Mdb, occ: tuple[float, float]) -> Mdb:
    """The chute's model from the donor's fabric material and its seat mesh's layout (see the module doc)."""
    mats = [m for m in donor.materials if any(donor.textures[x.texture].filename.lower() == FABRIC.lower() and x.kind == 'albedo'
                                              for x in m.textures)]
    _req(len(mats) == 1, f'{DONOR_MDB}: {len(mats)} materials with {FABRIC}')
    mat = mats[0]
    tmpl = next((me for o in donor.objects for me in o.meshes if me.material == mat.index), None)
    _req(tmpl is not None, f'{DONOR_MDB}: no mesh with the fabric material')
    names = {e.name.lower() for e in tmpl.elems}
    _req({'position', 'normal', 'tangent', 'binormal', 'texcoord', 'blendweight', 'blendindices'} <= names,
         f'{DONOR_MDB}: seat layout {sorted(names)}')
    _req(tmpl.flags[1] == 1 and tmpl.flags[2] == 1, f'{DONOR_MDB}: seat mesh not single-bone skinned')
    _req([donor.bones[i].kind for i in range(3)] == [0, 2, 3], f'{DONOR_MDB}: first three bones changed')
    bones = [replace(donor.bones[i], index=i, name=i, parent=-1 if i == 0 else 0, sibling=-1, child=-1, child_count=0)
             for i in range(3)]
    for b in bones:
        _req(all(abs(x - y) < 1e-6 for x, y in zip(b.local, [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0])),
             f'{DONOR_MDB}: bone {b.index} bind is not the identity')
    textures = []
    slots = []
    for x in mat.textures:
        t = donor.textures[x.texture]
        slots.append(replace(x, texture=len(textures)))
        textures.append(Texture(len(textures), t.name, t.filename, t.unk))
    verts, tris = canopy()
    vdata = b''.join(pack_vertex(tmpl.elems, tmpl.vsize, _row(tmpl.elems, v, occ, 2)) for v in verts)
    _req(len(verts) < 0x10000, f'{len(verts)} vertices')
    mesh = Mesh(tmpl.flags, 0, tmpl.unk08, tmpl.vsize, list(tmpl.elems), 0, vdata,
                struct.pack(f'<{3 * len(tris)}H', *(i for t in tris for i in t)))
    md = Mdb(donor.version, ['mdl', 'edf6vc_chute', 'canopy', donor.name_of(mat.name)], bones,
             [Object(1, 1, [mesh])], [replace(mat, index=0, name=3, textures=slots)], textures)
    return g.recompute_bounds(g.relink(md))


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished archive: the model, then the donor's fabric texture members (low and high detail) in the
    donor's folder order (TEXTURE lods, MODEL, HD-TEXTURE)."""
    donor_rab = rab_read(game.read('OBJECT', DONOR_ARC))
    donor = mdb_read(member(donor_rab, DONOR_MDB).data)
    occ_tex = next(donor.textures[x.texture].filename for m in donor.materials for x in m.textures if x.kind == 'param_occ')
    occ = brightest_block(member(donor_rab, occ_tex).data)
    md = model(donor, occ)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    _req('MODEL' in donor_rab.folders, f'{DONOR_ARC}: no MODEL folder')
    tex = [f for t in md.textures for f in g.texture_members(donor_rab, t.filename)]
    seen: set[str] = set()
    tex = [f for f in tex if not (f.name.lower() in seen or seen.add(f.name.lower()))]
    model_file = RabFile(OUT_MDB, donor_rab.folders.index('MODEL'), 0, stored)
    order = {name: k for k, name in enumerate(donor_rab.folders)}
    files = sorted(tex + [model_file], key=lambda f: (order[donor_rab.folders[f.folder]], f is not model_file and f.name.lower()))
    return rab_write(Rab(donor_rab.version, list(donor_rab.folders), files))


def sgo(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The stock FarEventObject's SGO with this model (no cas, no animation data, no damage grid, no ragdoll) and only
    a unit scale in its setting (no default animation)."""
    import dsgo
    doc = dsgo.parse(game.read('OBJECT', STOCK_SGO))
    r = doc.root
    _req(r.get('xgs_scene_object_class') == 'FarEventObject', f'{STOCK_SGO} is no FarEventObject')
    try:
        r.get('ragdoll')
        _req(False, f'{STOCK_SGO} has a ragdoll')
    except KeyError:
        pass
    r.set('animation_model', dsgo.Node([dsgo.Node([f'app:/Object/{OUT_ARC.lower()}', OUT_MDB]), 0.0, 0.0]))
    r.set('setting', dsgo.Node([dsgo.Node([1.0, 1.0, 1.0])], {0: 'scale'}))
    return dsgo.compact(doc)


# ------------------------------------------------------------------------------------------ check


def check(arc: bytes) -> None:
    """Re-read `arc` and raise ChuteCheckError unless: the archive and model round-trip; one object on a kind-2 bone,
    one mesh skinned to the kind-3 bone only, indices in range; every texture (and its .lod) is a member; the canopy
    spans 2 x RADIUS, its crown HEIGHT over the rim, the lines down to RISER_Y; every triangle faces as its vertices'
    normals (the stock winding)."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, OUT_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    _req([b.kind for b in md.bones] == [0, 2, 3], f'bone kinds {[b.kind for b in md.bones]}')
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'object not on the kind-2 bone')
    files = {f.name.lower() for f in rab.files}
    for t in md.textures:
        stem = t.filename.rsplit('.', 1)[0].lower()
        _req(t.filename.lower() in files and f'{stem}.lod.dds' in files, f'texture {t.filename} missing')
    me = md.objects[0].meshes[0]
    nv = me.nverts
    idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
    _req(idx and len(idx) % 3 == 0 and max(idx) < nv, 'index buffer')
    bi = read_elem(me, g.elem_name(me, 'BLENDINDICES') or '') or []
    _req({int(v[0]) for v in bi} == {2}, 'skinned to a bone other than canopy')
    p = g.mesh_positions(me)
    xs = [v[0] for v in p]
    ys = [v[1] for v in p]
    _req(abs(max(xs) - min(xs) - 2 * RADIUS) < 0.05, f'span {max(xs) - min(xs):.2f} m')
    _req(abs(max(ys) - RIM_Y - HEIGHT) < 0.02, f'crown {max(ys):.2f} m')
    _req(abs(min(ys) - RISER_Y) < 0.02, f'riser {min(ys):.2f} m')
    n = read_elem(me, g.elem_name(me, 'normal') or '') or []
    verts = [((v[0], v[1], v[2]), (m[0], m[1], m[2]), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0)) for v, m in zip(p, n)]
    bad = sum(1 for k in range(0, len(idx), 3) if facing(verts, (idx[k], idx[k + 1], idx[k + 2])) <= 0)
    _req(bad == 0, f'{bad} triangles face against their normals')
