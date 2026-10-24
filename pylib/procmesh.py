"""Models made from primitives (the Primer dragonfly, pylib/dragonfly_model.py; the centipede, pylib/centipede_model.py),
written as an MDB into a stock archive in place of one of its models, so they use that model's materials and
textures. Nothing outside Root.cpk (read only) is needed.

A model is a Skeleton (bones: name, parent, joint position in model space; all bound level) and Parts (vertices in
model space, each skinned to up to 4 bones, triangles, a material index of the template model). Every mesh is
written in the vertex layout of the template's own (first) mesh of that material (its elements and offsets: the
shader expects them). Its UVs land in one small patch of the template's own: round a vertex of the template's
meshes of that material where the material's albedo is most even (PATCH wide, its brightness at least
MIN_LIGHT; or, for a part with a `tint`, the even patch nearest that colour), every channel at that same
vertex's UV on it, so the surface takes one even colour of the texture
(a box over all the material's UVs mixed every part painted on the atlas: a patchwork) and the occlusion and
light masks match it. Without a readable albedo: the 10-90% box of the material's UVs. The template's object bone becomes the
model's object bone (`object_bone`, a kind-2 bone under the root), skin bones are kind 3 with their bounds over
the vertices they lead. A build is checked: written, read back and written again byte for byte.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field

import numpy as np

from graft_pure import prune_members
from mdb import (Bone, Mdb, Mesh, Object, cmpl_compress, cmpl_decompress, ident, inverse_affine, mdb_read, mdb_write,
                 rab_read, rab_write, read_elem)

Joint = tuple[str, int, tuple[float, float, float]]   # name, parent index (-1: root), position (model space)
Skin = list[tuple[int, float]]


@dataclass
class Part:
    """Vertices (model space), their skin (up to 4 bone, weight pairs), triangles, the template material."""
    material: int
    tint: tuple[int, int, int] | None = None   # the colour of the texture it wants (None: the most even patch)
    pos: list[np.ndarray] = field(default_factory=list)
    skin: list[Skin] = field(default_factory=list)
    uv: list[tuple[float, float]] = field(default_factory=list)
    tris: list[tuple[int, int, int]] = field(default_factory=list)

    def add(self, p, skin: Skin, uv) -> int:
        self.pos.append(np.array(p, dtype=np.float64))
        self.skin.append(skin)
        self.uv.append(uv)
        return len(self.pos) - 1


def grid(part: Part, rows: list[list[int]], close: bool) -> None:
    """Quads between consecutive rows of vertex indices (each row a ring when `close`). A triangle with two corners at
    one point (a row shrunk to a point: an ellipsoid's poles) has no area: it draws nothing and adds nothing to the
    smooth normals (frames), so it is not written."""
    for a, b in zip(rows, rows[1:]):
        n = len(a)
        for i in range(n if close else n - 1):
            j = (i + 1) % n
            for t in ((a[i], b[i], b[j]), (a[i], b[j], a[j])):
                p = [part.pos[k] for k in t]
                if not (np.array_equal(p[0], p[1]) or np.array_equal(p[1], p[2]) or np.array_equal(p[0], p[2])):
                    part.tris.append(t)


def ellipsoid(part: Part, c, r, skin, rings: int = 12, segs: int = 16) -> None:
    """An ellipsoid at `c` with radii `r`, its axis along z; `skin`: a Skin, or a function of the point. Its first and
    last rows are its poles, every vertex of each exactly there (sin(pi) is 1.2e-16, not 0: the far pole's vertices
    were apart by that, and only met in the model's half floats), so grid leaves out their empty triangles."""
    rows = []
    for k in range(rings + 1):
        th = math.pi * k / rings
        sin_th, cos_th = (0.0, 1.0 - 2.0 * (k > 0)) if k in (0, rings) else (math.sin(th), math.cos(th))
        row = []
        for s in range(segs):
            ph = 2 * math.pi * s / segs
            p = (c[0] + r[0] * sin_th * math.cos(ph), c[1] + r[1] * sin_th * math.sin(ph), c[2] + r[2] * cos_th)
            row.append(part.add(p, skin(p) if callable(skin) else skin, (s / segs, k / rings)))
        rows.append(row)
    grid(part, rows, True)


def tube(part: Part, path, radii, skins, segs: int = 12, squash: float = 1.0, cap: bool = True) -> None:
    """A tube through `path` (ring centres; the rings lie across the path's direction) with a radius and a skin per
    ring (`squash` flattens it vertically); capped ends."""
    pts = [np.array(p, dtype=np.float64) for p in path]
    rows = []
    n = len(pts)
    for k, (c, rad, sk) in enumerate(zip(pts, radii, skins)):
        d = pts[min(k + 1, n - 1)] - pts[max(k - 1, 0)]
        d /= np.linalg.norm(d)
        ref = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        a = np.cross(ref, d)
        a /= np.linalg.norm(a)
        b = np.cross(d, a)
        row = []
        for s in range(segs):
            ph = 2 * math.pi * s / segs
            p = c + a * rad * math.cos(ph) + b * rad * squash * math.sin(ph)
            row.append(part.add(p, sk, (s / segs, k / max(1, n - 1))))
        rows.append(row)
    grid(part, rows, True)
    if cap:
        for row, c, sk in ((rows[0], pts[0], skins[0]), (rows[-1], pts[-1], skins[-1])):
            m = part.add(c, sk, (0.5, 0.5))
            for i in range(len(row)):
                part.tris.append((m, row[i], row[(i + 1) % len(row)]))


# ------------------------------------------------------------------------------------------ wound primitives
# Solids wound out of themselves (cross(b - a, c - a) along the face's normal, out of the solid: the stock meshes'
# winding), once the sidecar's and the tracked vehicles' own (pylib/sidecar_model.py, pylib/tracked_combat_model.py),
# moved here as they were (their models are byte for byte the same).

Vec3 = tuple[float, float, float]


def flip_outward(part: Part, start: int, ref, outward: bool = True) -> None:  # noqa: ANN001 - point -> point
    """Turn the triangles added since `start` so that most face away from (outward) or toward `ref(centroid)`: the
    winding the stock meshes use (cross(b - a, c - a) along the face's normal, out of the solid)."""
    P = np.array(part.pos)
    T = np.array(part.tris[start:], dtype=np.int64)
    if not len(T):
        return
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    n = np.cross(b - a, c - a)
    cen = (a + b + c) / 3.0
    away = cen - np.array([ref(x) for x in cen])
    score = float(np.sign(np.einsum('ij,ij->i', n, away)).sum())
    if (score < 0) == outward:
        part.tris[start:] = [(t[0], t[2], t[1]) for t in part.tris[start:]]


def facing(part: Part, tris: list[int], want: np.ndarray) -> None:
    """Turn each of the triangles `tris` (indices into part.tris) so that its normal goes along `want`."""
    for i in tris:
        a, b, c = (np.asarray(part.pos[k]) for k in part.tris[i])
        if float(np.dot(np.cross(b - a, c - a), want)) < 0.0:
            t = part.tris[i]
            part.tris[i] = (t[0], t[2], t[1])


def cap_ends(part: Part, start: int, centres: tuple[int, int], ends: tuple[np.ndarray, np.ndarray]) -> None:
    """The end caps among the triangles since `start` (those fanned round the cap centres `centres`) turned to face
    out of the ends: along `ends` (the way out of the first end, the way out of the last)."""
    for m, want in zip(centres, ends):
        facing(part, [i for i in range(start, len(part.tris)) if m in part.tris[i]], want)


def path_ref(path: list[Vec3]):  # noqa: ANN202
    """The nearest point of the polyline `path`."""
    pts = [np.array(p, dtype=np.float64) for p in path]

    def ref(c: np.ndarray) -> np.ndarray:
        best, bd = pts[0], math.inf
        for a, b in zip(pts, pts[1:]):
            d = b - a
            t = min(1.0, max(0.0, float(np.dot(c - a, d) / max(np.dot(d, d), 1e-12))))
            q = a + d * t
            dd = float(np.dot(c - q, c - q))
            if dd < bd:
                best, bd = q, dd
        return best
    return ref


def flat_hexahedron(part: Part, corners: list[Vec3], skin: Skin) -> None:
    """A six-faced solid of 8 corners (0-3 the bottom ring, 4-7 the top ring over them), each face flat (its own 4
    vertices), wound out of the solid."""
    C = [np.array(p, dtype=np.float64) for p in corners]
    centre = sum(C) / 8.0
    for f in ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        start = len(part.tris)
        q = [part.add(C[i], skin, uv) for i, uv in zip(f, ((0, 0), (1, 0), (1, 1), (0, 1)))]
        part.tris += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
        flip_outward(part, start, lambda _c: centre)


def closed_tube(part: Part, path: list[Vec3], radius: float, skin: Skin, segs: int = 10) -> None:
    """tube (capped) along `path`, wound out of it, its caps facing out of its ends."""
    start = len(part.tris)
    tube(part, path, [radius] * len(path), [skin] * len(path), segs)
    flip_outward(part, start, path_ref(path))
    P = [np.array(p, dtype=np.float64) for p in path]
    cap_ends(part, start, (len(part.pos) - 2, len(part.pos) - 1), (P[0] - P[1], P[-1] - P[-2]))


def hard_polygon(part: Part, points, outward, skin: Skin) -> None:
    """A planar convex polygon, isolated vertices for hard armour edges."""
    start = len(part.pos)
    centre = np.mean(points, axis=0)
    normal = np.cross(np.asarray(points[1]) - points[0], np.asarray(points[2]) - points[0])
    order = list(range(len(points)))
    if np.dot(normal, outward) < 0:
        order.reverse()
    for k in order:
        p = points[k]
        part.add(p, skin, (0.5 + 0.1 * (p[0] - centre[0]), 0.5 + 0.1 * (p[2] - centre[2])))
    part.tris.extend((start, start + i, start + i + 1) for i in range(1, len(points) - 1))


def hard_hexahedron(part: Part, lower, upper, skin: Skin) -> None:
    """Four corners at each height, ordered cyclically in XZ; outward hard faces."""
    points = list(lower) + list(upper)
    centre = np.mean(points, axis=0)
    faces = ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    for face in faces:
        p = [np.asarray(points[k], dtype=float) for k in face]
        hard_polygon(part, p, np.mean(p, axis=0) - centre, skin)


def hard_box(part: Part, centre, half, skin: Skin) -> None:
    x, y, z = centre
    a, b, c = half
    hard_hexahedron(part, [(x-a, y-b, z-c), (x+a, y-b, z-c), (x+a, y-b, z+c), (x-a, y-b, z+c)],
                    [(x-a, y+b, z-c), (x+a, y+b, z-c), (x+a, y+b, z+c), (x-a, y+b, z+c)], skin)


def hard_cylinder(part: Part, start, end, radius: float, skin: Skin, segments: int = 16,
                  end_radius: float | None = None, cap: bool = True) -> None:
    """Tube with correct outward winding and flat caps; arbitrary axis."""
    a, b = np.asarray(start, dtype=float), np.asarray(end, dtype=float)
    axis = b - a
    if not np.linalg.norm(axis) > 1e-6:
        raise ValueError('zero-length cylinder')
    axis /= np.linalg.norm(axis)
    ref = np.array((0., 1., 0.)) if abs(axis[1]) < .9 else np.array((1., 0., 0.))
    u = np.cross(axis, ref)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)
    r1 = radius if end_radius is None else end_radius
    rows = []
    for centre, r, t in ((a, radius, 0.), (b, r1, 1.)):
        rows.append([part.add(centre + r * (u*math.cos(k*math.tau/segments) + v*math.sin(k*math.tau/segments)),
                              skin, (k / segments, t)) for k in range(segments)])
    for k in range(segments):
        j = (k+1) % segments
        part.tris.extend(((rows[0][k], rows[0][j], rows[1][j]), (rows[0][k], rows[1][j], rows[1][k])))
    if cap:
        for centre, row, wanted in ((a, rows[0], -axis), (b, rows[1], axis)):
            hard_polygon(part, [part.pos[i] for i in row], wanted, skin)


PATCH, MIN_LIGHT, CANDIDATES = 0.03, 45.0, 600


def _even_patch(image, uvs: np.ndarray, tint=None) -> int:
    """The index among `uvs` (channel 0) whose PATCH-wide square of `image` is most even (and bright enough), or
    with a `tint`, nearest that colour among the even ones."""
    px = np.asarray(image, dtype=np.float64)
    h, w = px.shape[:2]
    rng = np.random.default_rng(1)
    idx = rng.choice(len(uvs), size=min(CANDIDATES, len(uvs)), replace=False)
    grid8 = np.linspace(-PATCH / 2, PATCH / 2, 8)
    best, score = int(idx[0]), None
    for i in idx:
        u, v = uvs[i]
        us, vs = np.meshgrid(np.mod(u + grid8, 1.0), np.mod(v + grid8, 1.0))
        sample = px[(vs * (h - 1)).astype(int), (us * (w - 1)).astype(int)]
        light = sample.mean()
        if tint is None and light < MIN_LIGHT:
            continue
        sc = sample.std(axis=(0, 1)).sum()
        if tint is not None:
            sc = sc * 0.5 + float(np.abs(sample.mean(axis=(0, 1)) - np.array(tint)).sum())
        if score is None or sc < score:
            best, score = int(i), sc
    return best


def uv_boxes(src: Mdb, albedos: dict | None = None, tint=None) -> dict[tuple[int, int], tuple[float, float, float, float]]:
    """Per (material, UV channel) the box its parts' UVs land in (see the module's head); `albedos`: material ->
    its albedo texture (a PIL image) where one could be read; `tint`: the colour wanted (None: the most even)."""
    out: dict[int, dict[int, list]] = {}
    for ob in src.objects:
        for me in ob.meshes:
            chans = {e.channel: read_elem(me, e.name, e.channel) or [] for e in me.elems if e.name.lower() == 'texcoord'}
            if not chans or 0 not in chans:
                continue
            per = out.setdefault(me.material, {})
            for ch, uv in chans.items():
                per.setdefault(ch, []).extend(uv)
    boxes = {}
    for mat, per in out.items():
        image = (albedos or {}).get(mat)
        if image is not None and all(len(v) == len(per[0]) for v in per.values()):
            i = _even_patch(image, np.array(per[0]), tint)
            for ch, uvs in per.items():
                u, v = uvs[i]
                half = PATCH / 2 if ch == 0 else PATCH / 8
                boxes[(mat, ch)] = (u - half, v - half, u + half, v + half)
            continue
        for ch, uvs in per.items():
            a = np.array(uvs)
            lo, hi = np.percentile(a, 10, axis=0), np.percentile(a, 90, axis=0)
            boxes[(mat, ch)] = (float(lo[0]), float(lo[1]), float(hi[0]), float(hi[1]))
    return boxes


def frames(pos: np.ndarray, tris: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
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
    return n, t, np.cross(n, t)


def pack(part: Part, tmpl: Mesh, boxes, scale: float) -> tuple[bytes, bytes]:
    """The part's vertices in the template mesh's own layout (its elements and their offsets), and its triangles."""
    pos = np.array(part.pos) * scale
    tris = np.array(part.tris, dtype=np.int64)
    n, t, bn = frames(pos, tris)
    vec = {'position': pos, 'normal': n, 'binormal': bn, 'tangent': t}
    out = bytearray(len(pos) * tmpl.vsize)
    for i in range(len(pos)):
        base = i * tmpl.vsize
        u, v = part.uv[i]
        sk = sorted(part.skin[i], key=lambda x: -x[1])[:4]
        for e in tmpl.elems:
            at, name = base + e.offset, e.name.lower()
            if name in vec and e.fmt == 7:
                struct.pack_into('<4e', out, at, *vec[name][i], 1.0)
            elif name == 'texcoord' and e.fmt == 12:
                u0, v0, u1, v1 = boxes[(part.material, e.channel)]
                struct.pack_into('<2f', out, at, u0 + (u1 - u0) * u, v0 + (v1 - v0) * v)
            elif name == 'blendweight' and e.fmt == 1:
                struct.pack_into('<4f', out, at, *([w for _, w in sk] + [0.0] * (4 - len(sk))))
            elif name == 'blendindices' and e.fmt == 21:
                struct.pack_into('<4B', out, at, *([b for b, _ in sk] + [0] * (4 - len(sk))))
            else:
                raise ValueError(f'unexpected vertex element {e.name}{e.channel} fmt {e.fmt}')
    assert len(pos) < 65536
    return bytes(out), tris.astype('<u2').tobytes()


def bones(joints: list[Joint], object_bone: str, meshes_pos: list[tuple[np.ndarray, list]], scale: float) -> list[Bone]:
    """The skeleton (`joints`, scaled), each skin bone's bounds over the vertices it leads."""
    n = len(joints)
    # Depth-first preorder (every stock model's; the engine walks bones by depth_delta): each bone's parent is the
    # bone before it or one of that one's ancestors.
    chain: list[int] = []
    for i, (name, parent, _) in enumerate(joints):
        while chain and chain[-1] != parent:
            chain.pop()
        assert (parent == -1 and not chain and i == 0) or (chain and chain[-1] == parent), f'{name}: bones not in preorder'
        chain.append(i)
    children: dict[int, list[int]] = {i: [] for i in range(n)}
    for i, (_, p, _) in enumerate(joints):
        if p >= 0:
            children[p].append(i)
    depth: list[int] = []
    for _, p, _ in joints:
        depth.append(0 if p < 0 else depth[p] + 1)
    owned: dict[int, list[np.ndarray]] = {}
    every = np.vstack([pos for pos, _ in meshes_pos])
    for pos, skins in meshes_pos:
        for v, sk in zip(pos, skins):
            owned.setdefault(max(sk, key=lambda x: x[1])[0], []).append(v)
    out = []
    for i, (name, parent, at) in enumerate(joints):
        world = np.array(at) * scale
        rel = world - (np.array(joints[parent][2]) * scale if parent >= 0 else 0.0)
        local, bind = ident(), ident()
        local[12:15] = [float(x) for x in rel]
        bind[12:15] = [float(x) for x in world]
        sib = next((c for c in children[parent] if c > i), -1) if parent >= 0 else -1
        kind = 2 if name == object_bone else 0 if not owned.get(i) else 3
        pts = every if kind == 2 else np.array(owned[i]) if kind == 3 else None
        if pts is not None:
            lo, hi = pts.min(axis=0), pts.max(axis=0)
            centre = list((lo + hi) / 2 - (world if kind == 3 else 0.0)) + [1.0]
            half = list((hi - lo) / 2) + [1.0]
        else:
            centre, half = [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]
        dd = depth[i + 1] - depth[i] if i + 1 < n else depth[i]
        out.append(Bone(i, parent, sib, children[i][0] if children[i] else -1, i, len(children[i]), kind, dd,
                        1 if kind == 3 else 0, 0, 0, local, inverse_affine(bind),
                        [float(x) for x in half], [float(x) for x in centre]))
    return out


def build_mdb(src: Mdb, joints: list[Joint], object_bone: str, parts: list[Part], scale: float,
              albedos: dict | None = None) -> Mdb:
    boxes: dict = {}
    meshes, posed = [], []
    for k, p in enumerate(q for q in parts if q.pos):
        tmpl = next(me for ob in src.objects for me in ob.meshes if me.material == p.material)
        if p.tint not in boxes:
            boxes[p.tint] = uv_boxes(src, albedos, p.tint)
        vdata, idx = pack(p, tmpl, boxes[p.tint], scale)
        meshes.append(Mesh(tmpl.flags, p.material, tmpl.unk08, tmpl.vsize, tmpl.elems, k, vdata, idx))
        posed.append((np.array(p.pos) * scale, p.skin))
    names: list[str | None] = [n for n, _, _ in joints]
    mats = []
    for m in src.materials:
        names.append(src.name_of(m.name))
        mats.append(type(m)(**{**m.__dict__, 'name': len(names) - 1}))
    names.append(object_bone)
    obj = Object(len(names) - 1, [n for n, _, _ in joints].index(object_bone), meshes)
    return Mdb(src.version, names, bones(joints, object_bone, posed, scale), [obj], mats, src.textures)


def albedos(rab, src: Mdb) -> dict:  # noqa: ANN001 - mdb.Rab
    """Material -> its albedo texture (PIL, RGB 512 x 512) from the archive, where PIL can read it."""
    import io
    from PIL import Image
    files = {f.name.lower(): f for f in rab.files}
    out = {}
    for m in src.materials:
        tex = next((t for t in m.textures if t.kind.lower() == 'albedo'), None)
        f = files.get(src.textures[tex.texture].filename.lower()) if tex is not None and 0 <= tex.texture < len(src.textures) else None
        if f is None:
            continue
        try:
            im = Image.open(io.BytesIO(f.data))
            im.draft('RGB', (512, 512))
            out[m.index] = im.convert('RGB').resize((512, 512))
        except Exception:   # a format PIL cannot read: that material keeps the plain box
            pass
    return out


def build_archive(game, archive: str, member: str, joints: list[Joint], object_bone: str, parts: list[Part],  # noqa: ANN001
                  scale: float) -> bytes:
    """`archive` (Root.cpk OBJECT, read only) with its `member` model replaced by the one made of `parts`, and only what
    that model uses kept (graft_pure.prune_members: the generated SGO names `member` alone; the stock creature's LODs,
    debris and colour variants and the textures only they use went with it, 6 MB of the dragonfly's and centipede's)."""
    rab = rab_read(game.read('OBJECT', archive))
    hits = [f for f in rab.files if f.name.lower() == member.lower()]
    assert len(hits) == 1, f'{archive}: {member}'
    src = mdb_read(hits[0].data)
    data = mdb_write(build_mdb(src, joints, object_bone, parts, scale, albedos(rab, src)))
    back = mdb_read(data)
    assert mdb_write(back) == data, f'{member}: the model does not round-trip'
    assert [back.name_of(b.name) for b in back.bones] == [n for n, _, _ in joints]
    hits[0].stored = cmpl_compress(data)
    assert cmpl_decompress(hits[0].stored) == data
    prune_members(rab, [member])
    return rab_write(rab)
