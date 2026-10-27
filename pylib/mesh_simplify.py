"""Quadric-error edge collapse (numpy only, deterministic) for the generated models' LODs (pylib/model_lod.py).

    simplify(points, tris, group, target, max_error, normals, attrs) -> triangles     the kept ones, in the same rows
    drop_small(points, tris, size) -> triangles                                       without pieces under `size`

The input is a triangle list over attribute rows (a model's packed vertices: position, normal, UV, skin...). Rows at
one position are welded for the topology and the error quadrics (Garland & Heckbert: each triangle's plane, area
weighted, summed on its corners); a collapse u -> v is a half-edge collapse onto an existing position, so the result
uses only rows that were there (nothing is re-encoded, interpolated or re-skinned) and its bounding box can only
shrink. Rows are told apart in classes: two rows of one class (`attrs` and `group` equal) differ only in their normal
(and tangent frame): a crease, which a collapse may cross; rows of different classes on the two sides of a welded
edge make a seam (UV, material, bone), which it may not. What it keeps:

- open borders: a border vertex moves only along its border (onto a border neighbour along a border edge), and the
  border edges carry a plane perpendicular to their face (the border's own quadric), so outlines stay;
- seams: a seam vertex moves only along its seam, each side's class onto the same side's class at the target (both
  sides together: no crack), with the same perpendicular planes; each row then takes the row of its target class at
  the target that is on the same face of the edge, else the one whose normal is nearest its own;
- groups (`group`, the caller's key: bone, material, layout): a row is only ever replaced by a row of the same group,
  so no triangle ever spans two bones that did not already;
- the topology: non-manifold edges, inconsistent windings, seam / border junctions and seam ends are locked; a collapse
  must pass the link condition (no new non-manifold edge) and must not turn any face by more than ~78 degrees.

Collapses are made in rounds: every unlocked vertex takes its cheapest valid collapse, the cheapest part of those is
kept, and of these only the ones that are the cheapest around both their ends (_independent: no two collapses of a
round touch one triangle, share a target or make one new edge) are applied, all at once (_clean then drops what they left
degenerate or doubled). Sorting is stable and every tie is broken by index: the same input gives the same output.
"""
from __future__ import annotations

import numpy as np

FLIP_COS = 0.2          # a collapse may turn a face's normal by at most acos(0.2) ~ 78 degrees
BORDER_WEIGHT = 4.0     # a border / seam edge's perpendicular plane weighs as this many face planes
ROUND_FRACTION = 0.3    # of the valid collapses, at most this cheapest part is tried in one round
MAX_ROUNDS = 400
_INF = np.iinfo(np.int64).max


def _planes(pts: np.ndarray, tp: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    p0, p1, p2 = pts[tp[:, 0]], pts[tp[:, 1]], pts[tp[:, 2]]
    n = np.cross(p1 - p0, p2 - p0)
    l2 = np.linalg.norm(n, axis=1)
    unit = n / np.maximum(l2, 1e-30)[:, None]
    unit[l2 <= 1e-30] = 0.0
    d = -np.einsum('ij,ij->i', unit, p0)
    return np.hstack((unit, d[:, None])), l2 / 2


def _accumulate(P: int, at: np.ndarray, K: np.ndarray) -> np.ndarray:
    """(P, 16): the rows of K (n, 16) summed at `at` (n,)."""
    out = np.empty((P, 16))
    for j in range(16):
        out[:, j] = np.bincount(at, weights=K[:, j], minlength=P)
    return out


def _quadric(plane: np.ndarray, weight: np.ndarray) -> np.ndarray:
    return (weight[:, None, None] * plane[:, :, None] * plane[:, None, :]).reshape(-1, 16)


class _Edges:
    """The welded topology of the current triangles."""

    def __init__(self, tris: np.ndarray, weld: np.ndarray, P: int, cls: np.ndarray) -> None:
        tp = weld[tris]
        NC = int(cls.max()) + 1 if len(cls) else 1
        T = len(tris)
        self.tp = tp
        a, b = tp.ravel(), tp[:, [1, 2, 0]].ravel()
        ra, rb = cls[tris].ravel(), cls[tris][:, [1, 2, 0]].ravel()
        fwd = a < b
        lo, hi = np.where(fwd, a, b), np.where(fwd, b, a)
        rlo, rhi = np.where(fwd, ra, rb), np.where(fwd, rb, ra)
        key = lo.astype(np.int64) * P + hi
        order = np.argsort(key, kind='stable')
        uniq, start, count = np.unique(key[order], return_index=True, return_counts=True)
        first = order[start]
        second = np.where(count >= 2, order[np.minimum(start + 1, len(order) - 1)], -1)
        self.lo, self.hi, self.count = lo[first], hi[first], count
        s2 = np.maximum(second, 0)
        self.t1, self.t2 = first // 3, np.where(count >= 2, second // 3, -1)
        self.rlo1, self.rhi1 = rlo[first], rhi[first]
        self.rlo2, self.rhi2 = np.where(count >= 2, rlo[s2], -1), np.where(count >= 2, rhi[s2], -1)
        nonman = (count > 2) | ((count == 2) & (fwd[first] == fwd[s2]))
        self.border = count == 1
        self.seam = (count == 2) & ~nonman & ((self.rlo1 != self.rlo2) | (self.rhi1 != self.rhi2))
        self.nonman = nonman
        bc = lambda x: np.bincount(x, minlength=P)  # noqa: E731
        self.nb = bc(self.lo[self.border]) + bc(self.hi[self.border])
        self.ns = bc(self.lo[self.seam]) + bc(self.hi[self.seam])
        nm = bc(self.lo[nonman]) + bc(self.hi[nonman])
        deg_e = bc(self.lo) + bc(self.hi)
        self.deg_t = bc(tp.ravel())
        pairs = np.unique(tp.ravel().astype(np.int64) * NC + cls[tris].ravel())
        self.nrows = bc((pairs // NC).astype(np.int64))       # classes at each position
        nb, ns = self.nb, self.ns
        fan_ok = np.where(nb == 2, deg_e == self.deg_t + 1, deg_e == self.deg_t)
        self.locked = ((nm > 0) | ~np.isin(nb, (0, 2)) | ~np.isin(ns, (0, 2)) | ((nb > 0) & (ns > 0))
                       | (self.nrows > 2) | ~fan_ok)
        # vertex -> its triangles (CSR over the corners) and its neighbours (CSR over the edges)
        corners = np.argsort(tp.ravel(), kind='stable')
        self.vt_start = np.concatenate(([0], np.cumsum(self.deg_t)))
        self.vt = corners // 3
        ends = np.concatenate((self.lo, self.hi))
        other = np.concatenate((self.hi, self.lo))
        o = np.lexsort((other, ends))
        self.nb_start = np.concatenate(([0], np.cumsum(bc(ends))))
        self.nbr = other[o]
        self.T = T


def _expand(start: np.ndarray, which: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """For CSR `start` and the items `which`: (owner position in `which`, flat index) of every entry."""
    n = start[which + 1] - start[which]
    owner = np.repeat(np.arange(len(which)), n)
    first = np.repeat(start[which], n)
    within = np.arange(len(owner)) - np.repeat(np.cumsum(n) - n, n)
    return owner, first + within


def _components(tp: np.ndarray, P: int) -> np.ndarray:
    """A label per welded vertex: its connected component's smallest vertex (pointer jumping)."""
    lab = np.arange(P)
    a = np.concatenate((tp[:, 0], tp[:, 1], tp[:, 2]))
    b = np.concatenate((tp[:, 1], tp[:, 2], tp[:, 0]))
    while True:
        m = np.minimum(lab[a], lab[b])
        new = lab.copy()
        np.minimum.at(new, a, m)
        np.minimum.at(new, b, m)
        new = new[new]
        new = new[new]
        if np.array_equal(new, lab):
            return lab
        lab = new


def drop_small(points: np.ndarray, tris: np.ndarray, size: float) -> np.ndarray:
    """`tris` without the connected pieces (welded by position) whose bounding box diagonal is under `size`: what a
    LOD seen from far enough away cannot show (bolts, hinges, lamps)."""
    if size <= 0 or not len(tris):
        return tris
    pts, weld = np.unique(points, axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    tp = weld[tris]
    lab = _components(tp, len(pts))
    used = np.unique(tp.ravel())
    L = lab[used]
    lo = np.full((len(pts), 3), np.inf)
    hi = np.full((len(pts), 3), -np.inf)
    for c in range(3):
        np.minimum.at(lo[:, c], L, pts[used, c])
        np.maximum.at(hi[:, c], L, pts[used, c])
    diag = np.linalg.norm(hi - lo, axis=1)
    return tris[diag[lab[tp[:, 0]]] >= size]


def simplify(points: np.ndarray, tris: np.ndarray, group: np.ndarray, target: int, max_error: float,
             normals: np.ndarray | None = None, attrs: np.ndarray | None = None, stats: dict | None = None
             ) -> np.ndarray:
    """`tris` (T, 3: rows into `points` (R, 3)) collapsed toward `target` triangles, never past `max_error` (metres:
    the root of the summed squared distances of the target from the planes the collapse carries). `group` (R,): the rows' merge key; `attrs` (R,):
    rows of one group with equal attrs differ only in `normals` (R, 3) (None: every row is a class of its own)."""
    tris = np.asarray(tris, dtype=np.int64)
    R = len(points)
    pts, weld = np.unique(np.asarray(points, dtype=np.float64), axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    P = len(pts)
    group = np.asarray(group, dtype=np.int64)
    if attrs is None:
        cls = np.arange(R)
        normals = np.zeros((R, 3))
    else:
        _u, cls = np.unique(np.stack((group, np.asarray(attrs, dtype=np.int64)), axis=1), axis=0, return_inverse=True)
        cls = cls.reshape(-1)
        normals = np.asarray(normals, dtype=np.float64)
    shared = _shared(tris, weld, P)
    tris = _clean(tris, weld, P, shared)
    plane, area = _planes(pts, weld[tris])
    # Every face's plane counts once, whatever its area, and a collapse's error is the root of the summed squared
    # distances of its target from the planes it carries (at least its largest distance from any of them). Weighted by
    # area (Garland & Heckbert's choice) a thin piece's planes weigh nothing and it collapses away whatever its length
    # (a 1.3 m antenna on the stock V608 make_optics copies); averaged over the planes, a wheel's many flat side planes
    # hide its rim being pulled in (the 2S6's road wheels at LOD2 turned into stars, 2026-10-27 renders).
    weight = (area > 0).astype(np.float64)
    Q = _accumulate(P, weld[tris].ravel(), np.repeat(_quadric(plane, weight), 3, axis=0))
    # the border and seam edges' perpendicular planes, on both ends
    e = _Edges(tris, weld, P, cls)
    cons = e.border | e.seam
    if cons.any():
        lo, hi, t1 = e.lo[cons], e.hi[cons], e.t1[cons]
        d = pts[hi] - pts[lo]
        n = np.cross(d, plane[t1, :3])
        ln = np.linalg.norm(n, axis=1)
        ok = ln > 1e-30
        n = n[ok] / ln[ok][:, None]
        pl = np.hstack((n, -np.einsum('ij,ij->i', n, pts[lo[ok]])[:, None]))
        K = _quadric(pl, np.full(int(ok.sum()), BORDER_WEIGHT))
        # (not counted in the mean: they add to a collapse's error, they do not dilute it)
        Q += _accumulate(P, np.concatenate((lo[ok], hi[ok])), np.vstack((K, K)))
    rounds = 0
    while len(tris) > target and rounds < MAX_ROUNDS:
        rounds += 1
        if rounds > 1:
            e = _Edges(tris, weld, P, cls)
        done = _round(e, tris, weld, pts, cls, normals, Q, target, max_error)
        if done is None:
            break
        tris = _clean(done, weld, P, shared)
    if stats is not None:
        stats['rounds'] = rounds
    return tris


def _triples(tp: np.ndarray, P: int) -> np.ndarray:
    srt = np.sort(tp, axis=1).astype(np.int64)
    return (srt[:, 0] * P + srt[:, 1]) * P + srt[:, 2]


def _parity(tp: np.ndarray) -> np.ndarray:
    """The winding: whether the corners after the smallest one ascend."""
    r = np.argmin(tp, axis=1)
    rot = np.take_along_axis(tp, (r[:, None] + np.arange(3)) % 3, axis=1)
    return rot[:, 1] < rot[:, 2]


def _shared(tris: np.ndarray, weld: np.ndarray, P: int) -> np.ndarray:
    """The position triples (_triples) the source has more than one face on: its two-sided sheets and its surfaces
    drawn twice (a decal, another material over the same faces), which _clean leaves as they are."""
    key, cnt = np.unique(_triples(weld[tris], P), return_counts=True)
    return key[cnt > 1]


def _clean(tris: np.ndarray, weld: np.ndarray, P: int, shared: np.ndarray) -> np.ndarray:
    """Without the triangles with two corners at one position, and of the faces collapses left on one position triple
    with one winding (not the source's own, `shared`) the first. Two faces of opposite windings both stay: a closed
    thin piece collapsed flat (an antenna, a barrel) is still a sheet the next collapses can only shorten against its
    end's plane; removing both would drop the piece whatever its length (the V608's 1.3 m antenna)."""
    tp = weld[tris]
    keep = (tp[:, 0] != tp[:, 1]) & (tp[:, 1] != tp[:, 2]) & (tp[:, 0] != tp[:, 2])
    tris, tp = tris[keep], tp[keep]
    if not len(tris):
        return tris
    key = _triples(tp, P) * 2 + _parity(tp)
    _u, first = np.unique(key, return_index=True)
    out = np.zeros(len(tris), dtype=bool)
    out[first] = True
    out |= np.isin(key // 2, shared)
    return tris[out]


def _round(e: _Edges, tris: np.ndarray, weld: np.ndarray, pts: np.ndarray, cls: np.ndarray, normals: np.ndarray,
           Q: np.ndarray, target: int, max_error: float) -> np.ndarray | None:
    E = len(e.lo)
    # directed candidates: lo -> hi and hi -> lo
    u = np.concatenate((e.lo, e.hi))
    v = np.concatenate((e.hi, e.lo))
    eid = np.concatenate((np.arange(E), np.arange(E)))
    # the classes at u and v on the edge's first and second face (-1: a border edge has none)
    ru1 = np.concatenate((e.rlo1, e.rhi1))
    rv1 = np.concatenate((e.rhi1, e.rlo1))
    ru2 = np.concatenate((e.rlo2, e.rhi2))
    rv2 = np.concatenate((e.rhi2, e.rlo2))
    cnt = e.count[eid]
    border, seam = e.border[eid], e.seam[eid]
    ok = ~e.locked[u] & ~e.nonman[eid]
    ok &= np.where(e.nb[u] == 2, border, np.where(e.ns[u] == 2, seam, ~border & ~seam))
    one = cnt == 1
    same = ru1 == ru2
    ok &= np.where(one, e.nrows[u] == 1,
                   np.where(same, (rv1 == rv2) & (e.nrows[u] == 1), e.nrows[u] == 2))
    idx = np.flatnonzero(ok)
    if not len(idx):
        return None
    pv = np.hstack((pts[v[idx]], np.ones((len(idx), 1))))
    Qs = (Q[u[idx]] + Q[v[idx]]).reshape(-1, 4, 4)
    cost = np.einsum('ni,nij,nj->n', pv, Qs, pv)
    err = np.sqrt(np.maximum(cost, 0.0))
    keep = err <= max_error
    idx, err = idx[keep], err[keep]
    if not len(idx):
        return None
    # each source's cheapest valid collapse: its cheapest live one checked, the failed ones dropped, a few times over
    alive = np.ones(len(idx), dtype=bool)
    good: list[np.ndarray] = []
    for _attempt in range(4):
        live = np.flatnonzero(alive)
        if not len(live):
            break
        o = live[np.lexsort((v[idx[live]], err[live], u[idx[live]]))]
        firsts = o[np.concatenate(([True], u[idx[o]][1:] != u[idx[o]][:-1]))]
        bad = _invalid(e, pts, u[idx[firsts]], v[idx[firsts]], cnt[idx[firsts]])
        good.append(firsts[~bad])
        alive[firsts] = False
        alive &= ~np.isin(u[idx], u[idx[firsts[~bad]]])
    take = np.concatenate(good) if good else np.zeros(0, dtype=np.int64)
    if not len(take):
        return None
    need = max(1, (len(tris) - target + 1) // 2)
    take = take[np.lexsort((u[idx[take]], err[take]))]
    take = take[:max(1, min(need, int(np.ceil(ROUND_FRACTION * len(take)))))]
    c = idx[take]
    c = _independent(e, u[c], v[c], err[take], len(Q), c)
    if not len(c):
        return None
    rmap = _row_map(e, tris, weld, cls, normals, u[c], v[c], ru1[c], rv1[c], ru2[c], rv2[c], e.t1[eid[c]], e.t2[eid[c]])
    Q[v[c]] += Q[u[c]]
    return rmap[tris]


def _corner_rows(e: _Edges, tris: np.ndarray, at: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(owner, row) of every distinct row the triangles around the positions `at` use at that position."""
    o, f = _expand(e.vt_start, at)
    t = e.vt[f]
    hit = e.tp[t] == at[o][:, None]
    rows = (tris[t] * hit).sum(axis=1)          # one corner of a (manifold, welded) face is at the position
    pairs = np.unique(np.stack((o, rows), axis=1), axis=0)
    return pairs[:, 0], pairs[:, 1]


def _row_map(e: _Edges, tris: np.ndarray, weld: np.ndarray, cls: np.ndarray, normals: np.ndarray, U: np.ndarray,
             V: np.ndarray, CU1: np.ndarray, CV1: np.ndarray, CU2: np.ndarray, CV2: np.ndarray, T1: np.ndarray,
             T2: np.ndarray) -> np.ndarray:
    """rows -> rows for the collapses U -> V (classes CU1 -> CV1 on the edge's first face T1, CU2 -> CV2 on its second
    T2, -1: none): each row at u takes, among the rows at v of its target class, the one it shares an edge face with,
    else the one whose normal is nearest its own."""
    R = len(cls)
    rmap = np.arange(R)
    ou, ru = _corner_rows(e, tris, U)
    ov, rv = _corner_rows(e, tris, V)
    target = np.where(cls[ru] == CU1[ou], CV1[ou], CV2[ou])
    NC = int(cls.max()) + 1
    kv = ov.astype(np.int64) * NC + cls[rv]
    order = np.argsort(kv, kind='stable')
    kv, rv_s = kv[order], rv[order]
    ku = ou.astype(np.int64) * NC + target
    lo = np.searchsorted(kv, ku, side='left')
    n = np.searchsorted(kv, ku, side='right') - lo
    if (n == 0).any():
        raise AssertionError('a row has no row of its class at the collapse target')
    pair = np.repeat(np.arange(len(ru)), n)
    cand = rv_s[np.repeat(lo, n) + np.arange(int(n.sum())) - np.repeat(np.cumsum(n) - n, n)]
    keys = []
    for T in (T1, T2):
        ok = T >= 0
        rows, pos = tris[T[ok]], weld[tris[T[ok]]]
        at_u = (rows * (pos == U[ok][:, None])).sum(axis=1)
        at_v = (rows * (pos == V[ok][:, None])).sum(axis=1)
        keys.append(at_u.astype(np.int64) * R + at_v)
    face = np.isin(ru[pair].astype(np.int64) * R + cand, np.concatenate(keys))
    score = np.einsum('ij,ij->i', normals[ru[pair]], normals[cand]) + 4.0 * face
    best = np.lexsort((cand, -score, pair))
    first = best[np.concatenate(([True], pair[best][1:] != pair[best][:-1]))]
    rmap[ru[pair[first]]] = cand[first]
    return rmap


def _independent(e: _Edges, U: np.ndarray, V: np.ndarray, err: np.ndarray, P: int, c: np.ndarray) -> np.ndarray:
    """The candidates `c` (sources U, targets V) that can be applied together: no two whose closed neighbourhoods
    N[u] + N[v] hold the other's source or target. So no two collapses of a round share a triangle, two never land on
    one target, and two never make the same new edge (u1 -> v1 next to u2 while u2 -> v2 next to u1): each one's link
    condition and face check, made alone, still hold. Picked like Luby's maximal independent set: in each pass a
    candidate is taken when its rank is the smallest of the live candidates' around it, and the neighbourhoods of
    the taken ones are closed to the rest. The rank: the error in half-octave steps, then a fixed scramble of the
    vertex number (ranked by the error alone, the ranks follow the surface's smooth error field and hardly any
    candidate is a local minimum)."""
    step = np.floor(2 * np.log2(np.maximum(err, 1e-9))).astype(np.int64)
    scramble = (U.astype(np.uint64) * np.uint64(2654435761)) % np.uint64(1 << 32)
    order = np.lexsort((U, scramble, step))
    rk = np.empty(len(c), dtype=np.int64)
    rk[order] = np.arange(len(c))
    blocked = np.zeros(P, dtype=bool)
    live = np.ones(len(c), dtype=bool)
    taken = np.zeros(len(c), dtype=bool)
    for _pass in range(12):
        live &= ~blocked[U] & ~blocked[V]
        if not live.any():
            break
        rank = np.full(P, _INF, dtype=np.int64)
        rank[U[live]] = rk[live]
        near = rank.copy()
        np.minimum.at(near, e.lo, rank[e.hi])
        np.minimum.at(near, e.hi, rank[e.lo])
        now = live & (np.minimum(near[U], near[V]) == rk)
        if not now.any():
            break
        taken |= now
        live &= ~now
        ends = np.concatenate((U[now], V[now]))
        blocked[ends] = True
        _o, f = _expand(e.nb_start, ends)
        blocked[e.nbr[f]] = True
    return c[taken]


def _invalid(e: _Edges, pts: np.ndarray, U: np.ndarray, V: np.ndarray, cnt: np.ndarray) -> np.ndarray:
    """Per candidate u -> v: True when it fails the link condition or turns a face too far."""
    n = len(U)
    bad = np.zeros(n, dtype=bool)
    # link: the common neighbours are exactly the edge's opposite corners (one per face on the edge)
    ou, fu = _expand(e.nb_start, U)
    ov, fv = _expand(e.nb_start, V)
    P = len(e.nb_start)
    ku = ou.astype(np.int64) * P + e.nbr[fu]
    kv = ov.astype(np.int64) * P + e.nbr[fv]
    common = np.intersect1d(ku, kv, assume_unique=True)
    bad |= np.bincount((common // P).astype(np.int64), minlength=n) != cnt
    # faces: every triangle of u's fan not on the edge, with u moved onto v
    ot, ft = _expand(e.vt_start, U)
    tri = e.vt[ft]
    tp = e.tp[tri]
    on_edge = (tp == V[ot][:, None]).any(axis=1)
    ot, tp = ot[~on_edge], tp[~on_edge]
    p = pts[tp]
    n_old = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    moved = tp == U[ot][:, None]
    p = np.where(moved[:, :, None], pts[V[ot]][:, None, :], p)
    n_new = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    lo, ln = np.linalg.norm(n_old, axis=1), np.linalg.norm(n_new, axis=1)
    turn = np.einsum('ij,ij->i', n_old, n_new) < FLIP_COS * lo * ln
    flat = ln <= 1e-12 * np.maximum(lo, 1e-30)
    hit = turn | flat
    bad |= np.bincount(ot[hit], minlength=n) > 0
    return bad
