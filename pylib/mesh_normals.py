"""Vertex normals for a source mesh that has none (tools/prep_wt_tracked.py, tools/prep_glb_model.py): the auto-smooth
Sketchfab gives such an upload, so that the model shades as on its page (docs/model-quality-1110.md 2).

    auto_smooth(P, tv, degrees=AUTO_SMOOTH) -> (normals, normal_of)

Positions are welded (WELD: the War Thunder exports repeat a position for every UV and buffer seam), and two faces that
share a welded edge are smoothed across it when their normals are within `degrees`; each corner's normal is the
area-weighted sum of the faces connected to it that way around its position. Measured against the normals Sketchfab
put into the glTF of the same upload (wt_m163, wt_bmpt: their source OBJ has none): 45 degrees gives them, 99.3 % to
100 % of the corners within 1 degree; the per-position greedy grouping this replaces (50 degrees, by OBJ vertex index,
not along edges) left 11 % (M163) to 15 % (BMPT) of them more than 1 degree off and 5-7 % more than 15.
It needs numpy and scipy (the developer tools only: the installer reads the prepared normals).
"""
from __future__ import annotations

import numpy as np

AUTO_SMOOTH = 45.0      # degrees
WELD = 1e-5             # m: positions this close are one


def auto_smooth(P: np.ndarray, tv: np.ndarray, degrees: float = AUTO_SMOOTH) -> tuple[np.ndarray, np.ndarray]:
    """(normals (k, 3), normal_of (t, 3): each corner's row). A zero-area face smooths with nothing; its corners take
    the faces' normal around them that they share a group with, or +y when they are alone."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    P = np.asarray(P, dtype=np.float64)
    tv = np.asarray(tv, dtype=np.int64)
    t = len(tv)
    _key, weld = np.unique(np.round(P / WELD).astype(np.int64), axis=0, return_inverse=True)
    w = weld.reshape(-1)[tv]                                     # (t, 3) welded positions
    fn = np.cross(P[tv[:, 1]] - P[tv[:, 0]], P[tv[:, 2]] - P[tv[:, 0]])
    area = np.linalg.norm(fn, axis=1)
    unit = fn / np.maximum(area, 1e-20)[:, None]
    # every directed edge of every face: its welded ends (low, high) and the corners at them
    corner = np.arange(3 * t).reshape(t, 3)
    a, b = w[:, [0, 1, 2]].reshape(-1), w[:, [1, 2, 0]].reshape(-1)
    ca, cb = corner[:, [0, 1, 2]].reshape(-1), corner[:, [1, 2, 0]].reshape(-1)
    face = np.repeat(np.arange(t), 3)
    swap = a > b
    lo, hi = np.where(swap, b, a), np.where(swap, a, b)
    c_lo, c_hi = np.where(swap, cb, ca), np.where(swap, ca, cb)
    order = np.lexsort((hi, lo))
    lo, hi, c_lo, c_hi, face = lo[order], hi[order], c_lo[order], c_hi[order], face[order]
    # the faces on one welded edge: each joined to the run's first (a fan of more than two: all through it)
    new = np.r_[True, (lo[1:] != lo[:-1]) | (hi[1:] != hi[:-1])]
    first = np.maximum.accumulate(np.where(new, np.arange(len(lo)), 0))
    pair = ~new & (lo != hi)
    i, j = first[pair], np.flatnonzero(pair)
    smooth = (area[face[i]] > 1e-20) & (area[face[j]] > 1e-20) & \
        ((unit[face[i]] * unit[face[j]]).sum(1) >= np.cos(np.radians(degrees)))
    i, j = i[smooth], j[smooth]
    rows = np.r_[c_lo[i], c_hi[i]]
    cols = np.r_[c_lo[j], c_hi[j]]
    n = 3 * t
    _count, label = connected_components(coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n)), directed=False)
    acc = np.zeros((int(label.max()) + 1, 3))
    np.add.at(acc, label, np.repeat(fn, 3, axis=0))
    size = np.linalg.norm(acc, axis=1)
    normals = np.where(size[:, None] > 1e-20, acc / np.maximum(size, 1e-20)[:, None], (0.0, 1.0, 0.0))
    return normals, label.reshape(t, 3)
