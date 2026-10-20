"""Collision fitted to a model's own mesh: its connected pieces (triangles that share a vertex position), each cut
until it is nearly convex, each wrapped in a convex hull (pylib/hkshape.py), for a compound or a compressed mesh of
convex shapes.

The pieces follow EDF6 SHKT Editor's ModelPieces (vertices at the same position are one, so texture and normal seams
do not split a piece) and its MeshShapes.Reduce (a hull of at most N vertices inside the full one); the cutting is
this module's own, a simple form of what convex decomposition (V-HACD) does: a piece whose hull stands farther than
`tolerance` from its surface (a ring's hole, a track loop's inside, an L's corner) is cut in two across its longest
axis, and again, until each part is within it. EDF6 SHKT Editor: Copyright (c) 2026 Ktaro, MIT License (pylib/LICENSE.edf6-shkt-editor).

    connected(points, tris) -> [[triangle index, ...], ...]
    concavity(polygons, hull) -> m        how far the hull stands from the piece's surface at worst
    reduce(points, n) -> points           at most n of them, their hull inside the full one
    clip(triangles, box) -> [polygon]     the triangles cut to an axis-aligned box
    intersect_box(points, box)            the part of their hull inside a box (a solid's, not its surface's)
    shrink(points, radius)                squeezed so that hull + convex radius keeps their box
    fit(triangles, ...) -> [hull points]  the whole pipeline
"""
from __future__ import annotations

from typing import Sequence

import hkshape

Vec3 = tuple[float, float, float]
Tri = tuple[Vec3, Vec3, Vec3]
WELD = 1e-6                   # m: vertices this close are one (the pieces' connectivity)
SLIVER = 1e-3                 # m: a piece thinner than this is dropped


def connected(points: Sequence[Vec3], tris: Sequence[tuple[int, int, int]]) -> list[list[int]]:
    """The connected pieces: triangles grouped through shared vertex positions (exact float equality), in order of
    their first triangle."""
    node: dict[Vec3, int] = {}
    of = [node.setdefault(tuple(p), len(node)) for p in points]     # type: ignore[arg-type]
    parent = list(range(len(node)))

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in tris:
        r = find(of[a])
        parent[find(of[b])] = r
        parent[find(of[c])] = r
    groups: dict[int, list[int]] = {}
    for t, (a, _, _) in enumerate(tris):
        groups.setdefault(find(of[a]), []).append(t)
    return list(groups.values())


def _point_triangle_distance(P, A, B, C):  # noqa: ANN001, ANN202 - numpy (m, 3) points, (n, 3) corners
    """(m,) the distance of each point to the nearest of the triangles (Ericson's closest point, vectorised)."""
    import numpy as np
    P = P[:, None, :]
    ab, ac, ap = B - A, C - A, P - A
    d1, d2 = (ab * ap).sum(-1), (ac * ap).sum(-1)
    bp = P - B
    d3, d4 = (ab * bp).sum(-1), (ac * bp).sum(-1)
    cp = P - C
    d5, d6 = (ab * cp).sum(-1), (ac * cp).sum(-1)
    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    denom = va + vb + vc
    with np.errstate(divide='ignore', invalid='ignore'):
        v = np.where(denom != 0, vb / denom, 0.0)
        w = np.where(denom != 0, vc / denom, 0.0)
        q = A + ab * v[..., None] + ac * w[..., None]                     # inside the face
        t_ab = np.clip(np.where(d1 - d3 != 0, d1 / (d1 - d3), 0.0), 0, 1)
        t_ac = np.clip(np.where(d2 - d6 != 0, d2 / (d2 - d6), 0.0), 0, 1)
        e = (d4 - d3) + (d5 - d6)
        t_bc = np.clip(np.where(e != 0, (d4 - d3) / e, 0.0), 0, 1)
    q_ab = A + ab * t_ab[..., None]
    q_ac = A + ac * t_ac[..., None]
    q_bc = B + (C - B) * t_bc[..., None]
    region = [(d1 <= 0) & (d2 <= 0), (d3 >= 0) & (d4 <= d3), (d6 >= 0) & (d5 <= d6),
              (vc <= 0) & (d1 >= 0) & (d3 <= 0), (vb <= 0) & (d2 >= 0) & (d6 <= 0),
              (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)]
    choice = [A + 0 * ab, B + 0 * ab, C + 0 * ab, q_ab, q_ac, q_bc]
    for cond, pt in zip(reversed(region), reversed(choice)):
        q = np.where(cond[..., None], pt, q)
    return np.sqrt(((P - q) ** 2).sum(-1)).min(1)


def concavity(polys: Sequence[Sequence[Vec3]], hull: hkshape.ConvexHull) -> float:
    """How far (m) the hull stands from the piece's surface at worst (a simple form of V-HACD's measure): points
    sampled over each hull face, their distance to the nearest of the piece's triangles. 0 for a closed convex
    piece; a ring's hole, an L's inner corner and a track loop's inside show up."""
    import numpy as np
    tris = [(p[0], p[k], p[k + 1]) for p in polys for k in range(1, len(p) - 1)]
    if not tris:
        return 0.0
    T = np.asarray(tris, dtype=np.float64)
    samples = []
    for f in range(len(hull.faces)):
        loop = [hull.vertices[i] for i in hull.face(f)]
        c = tuple(sum(p[k] for p in loop) / len(loop) for k in range(3))
        samples.append(c)
        for i in range(len(loop)):
            a, b = loop[i], loop[(i + 1) % len(loop)]
            for u, v in ((0.5, 0.0), (0.25, 0.25), (0.5, 0.25), (0.25, 0.5)):
                samples.append(tuple(c[k] + u * (a[k] - c[k]) + v * (b[k] - c[k]) for k in range(3)))
    P = np.asarray(samples, dtype=np.float64)
    out = 0.0
    for k in range(0, len(P), 256):
        out = max(out, float(_point_triangle_distance(P[k:k + 256], T[:, 0][None], T[:, 1][None], T[:, 2][None]).max()))
    return out


def reduce(points: Sequence[Vec3], n: int) -> list[Vec3]:
    """At most `n` of the hull's vertices whose hull approximates it (MeshShapes.Reduce): from a tetrahedron of
    extreme points, the vertex farthest outside the current hull is added until there are n. The result lies inside
    the full hull."""
    if n < 4:
        raise ValueError('a hull needs at least 4 vertices')
    cand = hkshape.hull_vertices(points)
    if len(cand) <= n:
        return cand

    def argmax(f) -> int:  # noqa: ANN001
        return max(range(len(cand)), key=lambda i: f(cand[i]))

    def d2(p: Vec3, q: Vec3) -> float:
        return sum((p[k] - q[k]) ** 2 for k in range(3))
    a = argmax(lambda p: -p[0])
    b = argmax(lambda p: d2(p, cand[a]))
    a = argmax(lambda p: d2(p, cand[b]))
    ab = hkshape._unit(hkshape._sub(cand[b], cand[a]))
    c = argmax(lambda p: hkshape._len(hkshape._cross(hkshape._sub(p, cand[a]), ab)))
    nrm = hkshape._unit(hkshape._cross(hkshape._sub(cand[b], cand[a]), hkshape._sub(cand[c], cand[a])))
    d = argmax(lambda p: abs(hkshape._dot(hkshape._sub(p, cand[a]), nrm)))
    chosen = list(dict.fromkeys((a, b, c, d)))
    while len(chosen) < n:
        try:
            cur = hkshape.build_hull([cand[i] for i in chosen])
        except hkshape.HullError:
            break
        best, best_d = -1, 1e-6
        taken = set(chosen)
        for i, p in enumerate(cand):
            if i in taken:
                continue
            out = max(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] for pl in cur.planes)
            if out > best_d:
                best, best_d = i, out
        if best < 0:
            break
        chosen.append(best)
    return [cand[i] for i in chosen]


def _clip_poly(poly: list[Vec3], axis: int, edge: float, sign: float) -> list[Vec3]:
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        da, db = sign * (a[axis] - edge), sign * (b[axis] - edge)
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            out.append(tuple(a[k] + t * (b[k] - a[k]) for k in range(3)))    # type: ignore[arg-type]
    return out


def clip(triangles: Sequence[Tri], box: tuple[Vec3, Vec3]) -> list[list[Vec3]]:
    """Each triangle cut to the box (Sutherland-Hodgman); those wholly outside dropped."""
    out = []
    for tri in triangles:
        poly = list(tri)
        for axis in range(3):
            poly = _clip_poly(poly, axis, box[0][axis], 1.0)
            if poly:
                poly = _clip_poly(poly, axis, box[1][axis], -1.0)
            if not poly:
                break
        if len(poly) >= 3:
            out.append(poly)
    return out


def _split(polys: list[list[Vec3]], tolerance: float, depth: int, out: list[list[Vec3]],
           min_size: float) -> None:
    pts = list({p for poly in polys for p in poly})
    try:
        hull = hkshape.build_hull(pts)
    except hkshape.HullError:
        out.append(pts)            # flat: kept as it is (fit() thickens it)
        return
    ext = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3))
    if depth == 0 or len(polys) < 2 or ext < min_size or concavity(polys, hull) <= tolerance:
        out.append(hull.vertices)
        return
    centres = [tuple(sum(p[k] for p in poly) / len(poly) for k in range(3)) for poly in polys]
    axis = max(range(3), key=lambda k: max(c[k] for c in centres) - min(c[k] for c in centres))
    order = sorted(range(len(polys)), key=lambda i: centres[i][axis])
    cut = centres[order[len(order) // 2]][axis]
    left, right = [], []
    for poly in polys:            # the polygons cut at the plane, so that the halves meet without a gap
        a = _clip_poly(poly, axis, cut, -1.0)
        b = _clip_poly(poly, axis, cut, 1.0)
        if len(a) >= 3:
            left.append(a)
        if len(b) >= 3:
            right.append(b)
    if not left or not right:
        out.append(hull.vertices)
        return
    _split(left, tolerance, depth - 1, out, min_size)
    _split(right, tolerance, depth - 1, out, min_size)


def intersect_box(points: Sequence[Vec3], box: tuple[Vec3, Vec3]) -> list[Vec3] | None:
    """The points whose hull is (the hull of `points`) cut to the box: a convex solid's part inside it, not just its
    surface's: its vertices inside the box, its edges' crossings of the box's faces, the box's corners inside it."""
    try:
        h = hkshape.build_hull(points)
    except hkshape.HullError:
        return None
    lo, hi = box

    def inside_box(p: Vec3, e: float = 1e-9) -> bool:
        return all(lo[k] - e <= p[k] <= hi[k] + e for k in range(3))
    out = [v for v in h.vertices if inside_box(v)]
    edges = set()
    for f in range(len(h.faces)):
        loop = h.face(f)
        for k in range(len(loop)):
            a, b = loop[k], loop[(k + 1) % len(loop)]
            edges.add((min(a, b), max(a, b)))
    for a, b in edges:
        p, q = h.vertices[a], h.vertices[b]
        for k in range(3):
            for edge in (lo[k], hi[k]):
                if (p[k] - edge) * (q[k] - edge) < 0:
                    t = (edge - p[k]) / (q[k] - p[k])
                    x = tuple(p[j] + t * (q[j] - p[j]) for j in range(3))
                    if inside_box(x, 1e-7):
                        out.append(tuple(edge if j == k else x[j] for j in range(3)))      # type: ignore[misc]
    tol = 1e-6 * max(1.0, max(abs(c) for v in h.vertices for c in v))     # the planes are float32
    corners = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    for i, j in ((0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)):
        p, q = corners[i], corners[j]          # each box edge cut to the hull: its part inside, both ends
        t0, t1 = 0.0, 1.0
        for pl in h.planes:
            f0 = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] - tol
            f1 = pl[0] * q[0] + pl[1] * q[1] + pl[2] * q[2] + pl[3] - tol
            if f0 > 0 and f1 > 0:
                t0, t1 = 1.0, 0.0
                break
            if f0 > 0:
                t0 = max(t0, f0 / (f0 - f1))
            elif f1 > 0:
                t1 = min(t1, f0 / (f0 - f1))
        if t0 <= t1:
            out += [tuple(p[k] + t * (q[k] - p[k]) for k in range(3)) for t in (t0, t1)]     # type: ignore[misc]
    out = list(dict.fromkeys(out))
    try:
        return hkshape.build_hull(out).vertices if len(out) >= 4 else None
    except hkshape.HullError:
        return None


def shrink(points: Sequence[Vec3], radius: float) -> tuple[list[Vec3], float]:
    """The points squeezed (per axis, about their box) so that their hull grown by the convex radius keeps their box;
    the radius used (at most a quarter of their thinnest extent)."""
    lo = [min(p[k] for p in points) for k in range(3)]
    hi = [max(p[k] for p in points) for k in range(3)]
    r = min(radius, min(hi[k] - lo[k] for k in range(3)) / 4)
    if r <= 0:
        return list(points), 0.0
    out = [tuple(lo[k] + r + (p[k] - lo[k]) * (hi[k] - lo[k] - 2 * r) / (hi[k] - lo[k]) for k in range(3))
           for p in points]
    return out, r     # type: ignore[return-value]


def fit(triangles: Sequence[Tri], box: tuple[Vec3, Vec3] | None = None, tolerance: float = 0.02,
        max_vertices: int = 64, max_pieces: int = hkshape.MAX_MESH_SHAPES, min_size: float = 0.0,
        thickness: float = 0.01, depth: int = 10) -> list[list[Vec3]]:
    """Point sets whose convex hulls cover `triangles`: one per connected piece, pieces cut until nearly convex
    (`tolerance` m), flat ones thickened to `thickness` across, each solid cut to `box` when given (intersect_box: a
    piece's inside, not only its surface), pieces smaller than `min_size` (m) dropped, at most `max_vertices` per hull
    (reduce); when more than `max_pieces` result, the tolerance doubles until they fit."""
    polys = [list(t) for t in triangles]
    if not polys:
        return []
    index: dict[tuple[int, int, int], int] = {}
    pts_all: list[Vec3] = []
    tris_idx = []
    for poly in polys:       # welded at WELD: a cut edge's two copies meet again (their floats differ in the last bits)
        ids = []
        for p in poly:
            key = (round(p[0] / WELD), round(p[1] / WELD), round(p[2] / WELD))
            if key not in index:
                index[key] = len(pts_all)
                pts_all.append(p)
            ids.append(index[key])
        for k in range(1, len(ids) - 1):
            if len({ids[0], ids[k], ids[k + 1]}) == 3:
                tris_idx.append((ids[0], ids[k], ids[k + 1]))
    groups = connected(pts_all, tris_idx)
    if box is not None:
        groups = [g for g in groups if all(min(pts_all[i][k] for t in g for i in tris_idx[t]) <= box[1][k] and
                                           max(pts_all[i][k] for t in g for i in tris_idx[t]) >= box[0][k]
                                           for k in range(3))]
    while True:
        sets: list[list[Vec3]] = []
        for g in groups:
            ps = [[pts_all[i] for i in tris_idx[t]] for t in g]
            _split(ps, tolerance, depth, sets, 0.0)
        out = []
        for s in sets:
            s = _thicken(s, thickness)
            if box is not None:
                s = intersect_box(s, box)
                if s is None:
                    continue
            ext = [max(p[k] for p in s) - min(p[k] for p in s) for k in range(3)]
            if max(ext) < min_size or min(ext) < SLIVER:
                continue
            try:
                if hkshape.build_hull(s).min_width() < SLIVER:      # a sliver the box cut off a plate
                    continue
            except hkshape.HullError:
                continue
            out.append(reduce(s, max_vertices) if len(s) > max_vertices else s)
        if len(out) <= max_pieces:
            return out
        tolerance *= 2


def _thicken(points: list[Vec3], thickness: float) -> list[Vec3]:
    """A flat point set (a plate, a single polygon) given `thickness` across its thinnest direction, so that it hulls."""
    try:
        h = hkshape.build_hull(points)
        if h.min_width() >= thickness:
            return h.vertices
    except hkshape.HullError:
        pass
    c = tuple(sum(p[k] for p in points) / len(points) for k in range(3))
    cov = [[sum((p[i] - c[i]) * (p[j] - c[j]) for p in points) for j in range(3)] for i in range(3)]
    values, vectors = hkshape.eigen_symmetric(cov)
    k = min(range(3), key=lambda i: values[i])
    n = tuple(vectors[r][k] for r in range(3))
    half = thickness / 2
    return [tuple(p[i] + s * half * n[i] for i in range(3)) for p in points for s in (-1.0, 1.0)]  # type: ignore[misc]


def volume(hulls: list[list[Vec3]]) -> float:
    """The summed volume of the hulls (overlaps counted twice)."""
    total = 0.0
    for s in hulls:
        try:
            total += hkshape.from_hull(hkshape.build_hull(s), 0.0).volume
        except hkshape.HullError:
            pass
    return total


def coverage(points: Sequence[Vec3], hulls: list[list[Vec3]], tolerance: float = 1e-3) -> float:
    """The share of `points` inside at least one of the hulls (within `tolerance`)."""
    built = []
    for s in hulls:
        try:
            built.append(hkshape.build_hull(s))
        except hkshape.HullError:
            pass
    inside = 0
    for p in points:
        if any(max(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] for pl in h.planes) <= tolerance for h in built):
            inside += 1
    return inside / len(points) if points else 1.0
