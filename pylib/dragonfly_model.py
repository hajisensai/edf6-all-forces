"""The Primer swarm drone's own model: a bio-mechanical dragonfly (docs/swarm-plan.md §7), made here from primitives
and written as an MDB into the stock E507_GOLDUFO.MRAB in place of its e507_goldufo.mdb, so it uses that drone's
five materials and textures (gold, copper, metal, matte black, and the blue light-scrolling translucent one for
the wings and eyes). Nothing outside Root.cpk (read only) is needed.

    python pylib/dragonfly_model.py [OUT.MRAB]        build it (default build/EDF6VC_SWARM_UNIT.MRAB) and check it
    python pylib/model_view.py build/EDF6VC_SWARM_UNIT.MRAB e507_goldufo.mdb --color wing,abd,head --out ...

Axes as every model here: +x right, +y up, +z the nose. The skeleton (all bound level, so the plugin's hinges are
plain axes, src/swarm_pose.h): mdl (root, the V506 locators' parent) -> globalSRT -> body (the bone the V506 body
drives) -> head, wing_fl / wing_fr / wing_bl / wing_br (hinged about z at their roots: flap), abd1 -> abd2 -> abd3
-> abd4 (hinged about x: the abdomen curls), and the skinned object's own bone `dragonfly` under mdl, as the
stock drone has it. The vertex layout is the stock drone's (half4 position / normal / binormal / tangent, float2
uv, float4 weights, ubyte4 bone indices: 60 bytes). Each material's UVs land inside the box the stock drone's own
meshes of that material use, so its textures (and the translucent one's light mask) show.
"""
from __future__ import annotations

import math
import os
import struct
import sys
from dataclasses import dataclass, field

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mdb import (Bone, Mdb, Mesh, Object, cmpl_compress, cmpl_decompress, ident, inverse_affine, mdb_read,  # noqa: E402
                 mdb_write, rab_read, rab_write, read_elem)

TEMPLATE = ('E507_GOLDUFO.MRAB', 'e507_goldufo.mdb')
COPPER, METAL, GLOW, BLACK, GOLD = 0, 1, 2, 3, 4   # the template's materials
# Bones: name, parent, joint position (model space, bound level).
BONES: list[tuple[str, int, tuple[float, float, float]]] = [
    ('mdl', -1, (0.0, 0.0, 0.0)),
    ('dragonfly', 0, (0.0, 0.0, 0.0)),     # the skinned object's bone
    ('globalSRT', 0, (0.0, 0.0, 0.0)),
    ('body', 2, (0.0, 0.0, 0.0)),
    ('head', 3, (0.0, 0.2, 3.9)),
    ('wing_fl', 3, (-0.55, 0.85, 2.7)),
    ('wing_fr', 3, (0.55, 0.85, 2.7)),
    ('wing_bl', 3, (-0.55, 0.8, 1.55)),
    ('wing_br', 3, (0.55, 0.8, 1.55)),
    ('abd1', 3, (0.0, 0.1, 0.6)),
    ('abd2', 9, (0.0, 0.1, -1.4)),
    ('abd3', 10, (0.0, 0.1, -3.4)),
    ('abd4', 11, (0.0, 0.1, -5.2)),
]
BONE = {n: i for i, (n, _, _) in enumerate(BONES)}
SIZE = 1.1   # the whole model, times: 14 m nose to tail tip, 13.5 m across the wings


@dataclass
class Part:
    """Vertices (model space), their skin (up to 4 bone, weight pairs), triangles, the material."""
    material: int
    pos: list[np.ndarray] = field(default_factory=list)
    skin: list[list[tuple[int, float]]] = field(default_factory=list)
    uv: list[tuple[float, float]] = field(default_factory=list)
    tris: list[tuple[int, int, int]] = field(default_factory=list)

    def add(self, p, skin, uv) -> int:
        self.pos.append(np.array(p, dtype=np.float64))
        self.skin.append(skin)
        self.uv.append(uv)
        return len(self.pos) - 1


def grid(part: Part, rows: list[list[int]], close: bool) -> None:
    """Quads between consecutive rows of vertex indices (each row a ring when `close`)."""
    for a, b in zip(rows, rows[1:]):
        n = len(a)
        for i in range(n if close else n - 1):
            j = (i + 1) % n
            part.tris.append((a[i], b[i], b[j]))
            part.tris.append((a[i], b[j], a[j]))


def ellipsoid(part: Part, c, r, bone: str, rings: int = 12, segs: int = 16, skin=None) -> None:
    """An ellipsoid at `c` with radii `r`, its axis along z."""
    rows = []
    for k in range(rings + 1):
        th = math.pi * k / rings
        row = []
        for s in range(segs):
            ph = 2 * math.pi * s / segs
            p = (c[0] + r[0] * math.sin(th) * math.cos(ph), c[1] + r[1] * math.sin(th) * math.sin(ph), c[2] + r[2] * math.cos(th))
            row.append(part.add(p, skin(p) if skin else [(BONE[bone], 1.0)], (s / segs, k / rings)))
        rows.append(row)
    grid(part, rows, True)


def tube(part: Part, path: list[tuple[float, float, float]], radii: list[float], skins: list, segs: int = 12,
         squash: float = 1.0, cap: bool = True) -> None:
    """A tube through `path` (ring centres, roughly along z) with a radius and a skin per ring; capped ends."""
    rows = []
    n = len(path)
    for k, (c, rad, sk) in enumerate(zip(path, radii, skins)):
        row = []
        for s in range(segs):
            ph = 2 * math.pi * s / segs
            p = (c[0] + rad * math.cos(ph), c[1] + rad * squash * math.sin(ph), c[2])
            row.append(part.add(p, sk, (s / segs, k / max(1, n - 1))))
        rows.append(row)
    grid(part, rows, True)
    if cap:
        for row, c, sk in ((rows[0], path[0], skins[0]), (rows[-1], path[-1], skins[-1])):
            m = part.add(c, sk, (0.5, 0.5))
            for i in range(len(row)):
                part.tris.append((m, row[i], row[(i + 1) % len(row)]))


def wing(membrane: Part, spar: Part, root, side: float, length: float, chord: float, sweep: float, bone: str) -> None:
    """A wing from `root` out along +x (side +1) or -x (-1): a thin membrane (both faces) round an ellipse-ish
    outline, swept back `sweep` m at the tip, with a spar along its leading edge."""
    steps, half = 14, 0.04
    top, bot = [], []
    for k in range(steps + 1):
        u = k / steps
        x = root[0] + side * length * u
        w = chord * math.sqrt(max(0.0, 1.0 - (2 * u - 1) ** 2 * 0.85)) * (0.55 + 0.45 * (1 - u))
        lead = root[2] - sweep * u * u
        z0, z1 = lead, lead - w
        for face, rows in ((half, top), (-half, bot)):
            row = []
            for j, z in enumerate((z0, (z0 + z1) / 2, z1)):
                row.append(membrane.add((x, root[1] + face, z), [(BONE[bone], 1.0)], (u, j / 2)))
            rows.append(row)
    grid(membrane, top, False)
    grid(membrane, [r[::-1] for r in bot], False)   # the underside faces down
    path = [(root[0] + side * length * u, root[1] + 0.05, root[2] - sweep * u * u + 0.05) for u in np.linspace(0, 0.98, 10)]
    ring = []   # a tube along x: build it along z, then turn each point (x <- z)
    rows = []
    for k, c in enumerate(path):
        rad = 0.12 * (1 - 0.7 * k / (len(path) - 1))
        row = []
        for s in range(6):
            ph = 2 * math.pi * s / 6
            p = (c[0], c[1] + rad * math.sin(ph), c[2] + rad * math.cos(ph))
            row.append(spar.add(p, [(BONE[bone], 1.0)], (s / 6, k / (len(path) - 1))))
        rows.append(row)
    grid(spar, rows, True)
    del ring


def abdomen_skin(z: float) -> list[tuple[int, float]]:
    """A ring at z along the abdomen: blended between the segment bones round each joint (smooth bending)."""
    joints = [(BONE['abd1'], 0.6), (BONE['abd2'], -1.4), (BONE['abd3'], -3.4), (BONE['abd4'], -5.2)]
    blend = 0.45
    for i in range(len(joints) - 1):
        b0, z0 = joints[i]
        b1, z1 = joints[i + 1]
        if z > z1 + blend:
            return [(b0, 1.0)]
        if z > z1 - blend:
            t = (z1 + blend - z) / (2 * blend)
            return [(b0, 1.0 - t), (b1, t)]
    return [(joints[-1][0], 1.0)]


def parts() -> list[Part]:
    gold, copper, glow, black, metal = Part(GOLD), Part(COPPER), Part(GLOW), Part(BLACK), Part(METAL)
    # Thorax: a tall ellipsoid, the wings' and legs' mount.
    ellipsoid(gold, (0.0, 0.2, 2.15), (1.05, 1.0, 1.55), 'body')
    # Head and its two big compound eyes (glowing), a mandible cannon under it.
    ellipsoid(gold, (0.0, 0.15, 4.05), (0.75, 0.7, 0.65), 'head', rings=10, segs=14)
    for side in (-1, 1):
        ellipsoid(glow, (side * 0.62, 0.3, 4.3), (0.62, 0.66, 0.6), 'head', rings=10, segs=14)
    tube(metal, [(0.0, -0.45, 4.2), (0.0, -0.5, 5.2)], [0.22, 0.16], [[(BONE['head'], 1.0)]] * 2, segs=8)
    # Abdomen: ten rings, segmented (a ridge each segment), tapering to a barbed tip.
    zs = list(np.linspace(0.8, -6.9, 22))
    radii = [0.62 * (1 - 0.55 * (k / 21)) * (1.0 if k % 3 else 1.12) for k in range(22)]
    path = [(0.0, 0.1 + 0.02 * k, z) for k, z in enumerate(zs)]
    tube(copper, path, radii, [abdomen_skin(z) for z in zs], segs=12, squash=0.9)
    tube(gold, [(0.0, 0.55, -6.6), (0.0, 0.2, -7.6)], [0.18, 0.02], [abdomen_skin(-6.6)] * 2, segs=6)
    # Wings: the front pair longer, the back pair broader, a glowing membrane on a black spar.
    for side, fb in ((-1, 'wing_fl'), (1, 'wing_fr')):
        wing(glow, black, BONES[BONE[fb]][2], side, 5.6, 1.15, 0.7, fb)
    for side, bb in ((-1, 'wing_bl'), (1, 'wing_br')):
        wing(glow, black, BONES[BONE[bb]][2], side, 5.0, 1.45, 0.4, bb)
    # Legs: three pairs folded under the thorax.
    for side in (-1, 1):
        for z in (2.9, 2.2, 1.5):
            knee = (side * 1.3, -0.6, z + 0.2)
            tube(black, [(side * 0.5, -0.5, z), knee, (side * 1.1, -1.4, z - 0.3)], [0.11, 0.09, 0.05],
                 [[(BONE['body'], 1.0)]] * 3, segs=6)
    return [gold, copper, glow, black, metal]


# ---------------------------------------------------------------------------------------------- writing

def _uv_boxes(src: Mdb) -> dict[tuple[int, int], tuple[float, float, float, float]]:
    """The UV box (u0, v0, u1, v1: the 10-90% range) the template's meshes of each material use, per UV channel."""
    out: dict[tuple[int, int], list] = {}
    for ob in src.objects:
        for me in ob.meshes:
            for e in me.elems:
                if e.name == 'texcoord':
                    out.setdefault((me.material, e.channel), []).extend(read_elem(me, e.name, e.channel) or [])
    boxes = {}
    for key, uvs in out.items():
        a = np.array(uvs)
        lo, hi = np.percentile(a, 10, axis=0), np.percentile(a, 90, axis=0)
        boxes[key] = (float(lo[0]), float(lo[1]), float(hi[0]), float(hi[1]))
    return boxes


def _frames(pos: np.ndarray, tris: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Smooth vertex normals, and a tangent / binormal across each."""
    n = np.zeros_like(pos)
    a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    fn = np.cross(b - a, c - a)
    for k in range(3):
        np.add.at(n, tris[:, k], fn)
    ln = np.linalg.norm(n, axis=1)
    ln[ln == 0] = 1
    n /= ln[:, None]
    ref = np.where(np.abs(n[:, 1:2]) < 0.9, np.array([[0.0, 1.0, 0.0]]), np.array([[1.0, 0.0, 0.0]]))
    t = np.cross(ref, n)
    t /= np.linalg.norm(t, axis=1)[:, None]
    bn = np.cross(n, t)
    return n, t, bn


def _pack(part: Part, tmpl: Mesh, boxes, scale: float) -> tuple[bytes, bytes]:
    """The part's vertices in the template mesh's own layout (its elements and their offsets), and its triangles."""
    pos = np.array(part.pos) * scale
    tris = np.array(part.tris, dtype=np.int64)
    n, t, bn = _frames(pos, tris)
    vec = {'position': pos, 'normal': n, 'binormal': bn, 'tangent': t}
    out = bytearray(len(pos) * tmpl.vsize)
    for i in range(len(pos)):
        base = i * tmpl.vsize
        u, v = part.uv[i]
        sk = sorted(part.skin[i], key=lambda x: -x[1])[:4]
        for e in tmpl.elems:
            at = base + e.offset
            if e.name in vec and e.fmt == 7:
                struct.pack_into('<4e', out, at, *vec[e.name][i], 1.0)
            elif e.name == 'texcoord' and e.fmt == 12:
                u0, v0, u1, v1 = boxes[(part.material, e.channel)]
                struct.pack_into('<2f', out, at, u0 + (u1 - u0) * u, v0 + (v1 - v0) * v)
            elif e.name == 'BLENDWEIGHT' and e.fmt == 1:
                struct.pack_into('<4f', out, at, *([w for _, w in sk] + [0.0] * (4 - len(sk))))
            elif e.name == 'BLENDINDICES' and e.fmt == 21:
                struct.pack_into('<4B', out, at, *([b for b, _ in sk] + [0] * (4 - len(sk))))
            else:
                raise ValueError(f'unexpected vertex element {e.name}{e.channel} fmt {e.fmt}')
    assert len(pos) < 65536
    return bytes(out), tris.astype('<u2').tobytes()


def _bones(meshes_pos: list[tuple[np.ndarray, list]], scale: float) -> list[Bone]:
    """The skeleton (BONES, scaled), with each skin bone's bounds over the vertices it leads."""
    n = len(BONES)
    children: dict[int, list[int]] = {i: [] for i in range(n)}
    for i, (_, p, _) in enumerate(BONES):
        if p >= 0:
            children[p].append(i)
    depth = []
    for _, p, _ in BONES:
        depth.append(0 if p < 0 else depth[p] + 1)
    owned: dict[int, list[np.ndarray]] = {}
    allpos = []
    for pos, skins in meshes_pos:
        allpos.append(pos)
        for v, sk in zip(pos, skins):
            owned.setdefault(max(sk, key=lambda x: x[1])[0], []).append(v)
    every = np.vstack(allpos)
    out = []
    for i, (name, parent, at) in enumerate(BONES):
        world = np.array(at) * scale
        rel = world - (np.array(BONES[parent][2]) * scale if parent >= 0 else 0.0)
        local = ident()
        local[12:15] = [float(x) for x in rel]
        bind = ident()
        bind[12:15] = [float(x) for x in world]
        sib = next((c for c in children[parent] if c > i), -1) if parent >= 0 else -1
        kind = 0 if name in ('mdl', 'globalSRT') else 2 if name == 'dragonfly' else 3
        pts = every if kind == 2 else np.array(owned.get(i, [])) if owned.get(i) else None
        if pts is not None:
            lo, hi = pts.min(axis=0), pts.max(axis=0)
            centre = list((lo + hi) / 2 - (world if kind == 3 else 0.0)) + [1.0]
            half = list((hi - lo) / 2) + [1.0]
        else:
            centre, half = [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]
        dd = depth[i + 1] - depth[i] if i + 1 < n else depth[i]
        out.append(Bone(i, parent, sib, children[i][0] if children[i] else -1, i, len(children[i]), kind, dd,
                        1 if kind == 3 and pts is not None else 0, 0, 0, local, inverse_affine(bind),
                        [float(x) for x in half], [float(x) for x in centre]))
    return out


def build_mdb(src: Mdb, scale: float = SIZE) -> Mdb:
    boxes = _uv_boxes(src)
    ps = [p for p in parts() if p.pos]
    meshes, posed = [], []
    for k, p in enumerate(ps):
        tmpl = next(me for ob in src.objects for me in ob.meshes if me.material == p.material)
        vdata, idx = _pack(p, tmpl, boxes, scale)
        meshes.append(Mesh(tmpl.flags, p.material, tmpl.unk08, tmpl.vsize, tmpl.elems, k, vdata, idx))
        posed.append((np.array(p.pos) * scale, p.skin))
    bones = _bones(posed, scale)
    names: list[str | None] = [n for n, _, _ in BONES]
    mats = []
    for m in src.materials:
        names.append(src.name_of(m.name))
        mats.append(type(m)(**{**m.__dict__, 'name': len(names) - 1}))
    names.append('dragonfly')
    obj = Object(len(names) - 1, BONE['dragonfly'], meshes)
    return Mdb(src.version, names, bones, [obj], mats, src.textures)


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The archive: the stock drone's MRAB with its model replaced by the dragonfly (same member name)."""
    raw = game.read('OBJECT', TEMPLATE[0])
    rab = rab_read(raw)
    hits = [f for f in rab.files if f.name.lower() == TEMPLATE[1]]
    assert len(hits) == 1
    src = mdb_read(hits[0].data)
    data = mdb_write(build_mdb(src))
    back = mdb_read(data)
    assert mdb_write(back) == data, 'the dragonfly does not round-trip'
    assert [back.name_of(b.name) for b in back.bones] == [n for n, _, _ in BONES]
    hits[0].stored = cmpl_compress(data)
    assert cmpl_decompress(hits[0].stored) == data
    return rab_write(rab)


def main(argv: list[str]) -> int:
    import rootcpk
    out = argv[0] if argv else os.path.join(HERE, '..', 'build', 'EDF6VC_SWARM_UNIT.MRAB')
    arc = build(rootcpk.default())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as h:
        h.write(arc)
    md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == TEMPLATE[1]).data)
    nv = sum(me.nverts for o in md.objects for me in o.meshes)
    print(f'wrote {out}: {len(md.bones)} bones, {nv} vertices, {len(arc)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
