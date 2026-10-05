"""Makes the plain's 3500 m block tile without seams (the bigmap, tools/make_bigmap.py).

The block (ig_Heigen601: the 2500 m ground and the 500 m ring round it) was never meant to repeat: its east edge
(x = +1750) and west edge (x = -1750), and its north and south edges, have different heights, so neighbouring
copies meet in steps of up to 33 m (measured on the collision, docs/map-collision.md) that you can see through.

`Field` is a height correction that makes the block periodic. On each edge it moves the height to the mean of
that edge and the opposite one (the corners to the mean of the four corners); inside, the correction fades to zero
over BAND metres from the edge with a smoothstep, so only the ring pieces move, never the middle ground (|x|, |z|
<= 1250, whose seam with the ring stays as shipped):

    delta(x, z) = wx Dx(sx, z) + wz Dz(sz, x) - wx wz Dc(sx, sz)

with sx, sz the sides nearest to the point, w = smoothstep(1 - distance to the edge / BAND), Dx / Dz the edge
corrections along the edge and Dc the corner correction. The x and z edge profiles meeting at a corner are pinned to
one height there (their mean; they may differ by CORNER_TOL / corner_tol), so Dx and Dz agree at the corners and
delta is exactly Dx on an x edge and exactly Dz on a z edge. Each asset (collision, the near terrain, the far terrain) gets a field made
from its own edge profile: its edges end up equal to each other and the asset's relation to the others is kept.
"""
from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    import mdb

HALF = 1750.0      # m: half the block (the ring's outer edge)
INNER = 1250.0     # m: half the middle ground (never moved)
BAND = 450.0       # m: the correction fades over this much from the edge (must stay <= HALF - INNER)
EDGE_EPS = 0.05    # m: a vertex this close to an edge line is on it
CORNER_TOL = 0.05  # m: the x and z edge profiles must agree this well at a corner


def _smooth(u: np.ndarray) -> np.ndarray:
    u = np.clip(u, 0.0, 1.0)
    return u * u * (3.0 - 2.0 * u)


class Profile:
    """Height along one edge, piecewise linear between the asset's own vertices on it."""

    def __init__(self, t: np.ndarray, y: np.ndarray) -> None:
        if len(t) < 2:
            raise ValueError('an edge with fewer than two vertices')
        order = np.argsort(t)
        t, y = t[order], y[order]
        # several vertices at one t (split meshes, duplicated seams): their mean
        ut, inv = np.unique(np.round(t, 3), return_inverse=True)
        uy = np.bincount(inv, weights=y) / np.bincount(inv)
        if ut[0] > -HALF + 15.0 or ut[-1] < HALF - 15.0:
            raise ValueError('edge vertices span only %.1f .. %.1f' % (ut[0], ut[-1]))
        self.t, self.y = ut, uy

    def pin(self, t: float, y: float) -> None:
        """The profile passes through (t, y) (an end: the corner both edges meeting there must agree on)."""
        keep = np.abs(self.t - t) > 1e-3
        self.t, self.y = np.append(self.t[keep], t), np.append(self.y[keep], y)
        order = np.argsort(self.t)
        self.t, self.y = self.t[order], self.y[order]

    def __call__(self, t: np.ndarray | float) -> np.ndarray:
        return np.interp(t, self.t, self.y)


class Field:
    """The correction for one asset, from its vertices (world x, y, z of every vertex on the block's edges is
    enough; any others are ignored)."""

    def __init__(self, pts: np.ndarray, band: float = BAND, corner_tol: float = CORNER_TOL,
                 edge_eps: float = EDGE_EPS) -> None:
        if band > HALF - INNER:
            raise ValueError('the band would reach the middle ground')
        self.band = band
        x, y, z = pts[:, 0], pts[:, 1], pts[:, 2]
        self.px = {s: Profile(z[np.abs(x - s * HALF) < edge_eps], y[np.abs(x - s * HALF) < edge_eps]) for s in (-1, 1)}
        self.pz = {s: Profile(x[np.abs(z - s * HALF) < edge_eps], y[np.abs(z - s * HALF) < edge_eps]) for s in (-1, 1)}
        # a corner is one point of an x edge and of a z edge: both must see the same height there, or delta would
        # not reduce to the edge's own correction on both edges
        corner = []
        for sx in (-1, 1):
            for sz in (-1, 1):
                a, b = float(self.px[sx](sz * HALF)), float(self.pz[sz](sx * HALF))
                if abs(a - b) > corner_tol:
                    raise ValueError('corner (%d, %d): x edge %.3f m, z edge %.3f m' % (sx * HALF, sz * HALF, a, b))
                corner.append(0.5 * (a + b))
                self.px[sx].pin(sz * HALF, 0.5 * (a + b))
                self.pz[sz].pin(sx * HALF, 0.5 * (a + b))
        self.c = float(np.mean(corner))

    def target_x(self, t: np.ndarray) -> np.ndarray:
        """The height both x edges get at z = t."""
        base = 0.5 * (self.px[1](t) + self.px[-1](t))
        lo = self.c - 0.5 * (self.px[1](-HALF) + self.px[-1](-HALF))
        hi = self.c - 0.5 * (self.px[1](HALF) + self.px[-1](HALF))
        return base + lo + (hi - lo) * (t + HALF) / (2 * HALF)

    def target_z(self, t: np.ndarray) -> np.ndarray:
        base = 0.5 * (self.pz[1](t) + self.pz[-1](t))
        lo = self.c - 0.5 * (self.pz[1](-HALF) + self.pz[-1](-HALF))
        hi = self.c - 0.5 * (self.pz[1](HALF) + self.pz[-1](HALF))
        return base + lo + (hi - lo) * (t + HALF) / (2 * HALF)

    def delta(self, x: np.ndarray, z: np.ndarray) -> np.ndarray:
        x, z = np.asarray(x, dtype=np.float64), np.asarray(z, dtype=np.float64)
        sx = np.where(x >= 0, 1, -1)
        sz = np.where(z >= 0, 1, -1)
        wx = _smooth(1.0 - (HALF - np.abs(x)) / self.band)
        wz = _smooth(1.0 - (HALF - np.abs(z)) / self.band)
        zc = np.clip(z, -HALF, HALF)
        xc = np.clip(x, -HALF, HALF)
        dx = self.target_x(zc) - np.where(sx > 0, self.px[1](zc), self.px[-1](zc))
        dz = self.target_z(xc) - np.where(sz > 0, self.pz[1](xc), self.pz[-1](xc))
        corner_own = np.where(sx > 0, np.where(sz > 0, self.px[1](HALF), self.px[1](-HALF)),
                              np.where(sz > 0, self.px[-1](HALF), self.px[-1](-HALF)))
        dc = self.c - corner_own
        return wx * dx + wz * dz - wx * wz * dc

    def heights(self, pts: np.ndarray) -> np.ndarray:
        """New y for points (n, 3)."""
        return pts[:, 1] + self.delta(pts[:, 0], pts[:, 2])

    def gradient(self, x: np.ndarray, z: np.ndarray, h: float = 0.5) -> tuple[np.ndarray, np.ndarray]:
        """(d delta / dx, d delta / dz), central differences."""
        return ((self.delta(x + h, z) - self.delta(x - h, z)) / (2 * h),
                (self.delta(x, z + h) - self.delta(x, z - h)) / (2 * h))

    def normals(self, pts: np.ndarray, n: np.ndarray) -> np.ndarray:
        """Unit normals of the corrected surface at `pts`, from the old ones: a surface y = f(x, z) has the normal
        (-f_x, 1, -f_z) up to length, so adding delta gives (n.x / n.y - delta_x, 1, n.z / n.y - delta_z)."""
        gx, gz = self.gradient(pts[:, 0], pts[:, 2])
        ny = np.where(np.abs(n[:, 1]) < 1e-3, 1e-3, n[:, 1])
        out = np.stack([n[:, 0] / ny - gx, np.ones(len(n)), n[:, 2] / ny - gz], 1)
        out *= np.sign(ny)[:, None]
        return out / np.linalg.norm(out, axis=1, keepdims=True)


def mdb_points(md: 'mdb.Mdb') -> np.ndarray:
    """Every POSITION of a static (one identity bone) MDB, model space == world space for a piece at (0, 0, 0)."""
    import mdb
    return np.concatenate([np.array([v[:3] for v in mdb.read_elem(me, 'POSITION')])
                           for o in md.objects for me in o.meshes])


def apply_mdb(md: 'mdb.Mdb', field: Field) -> int:
    """Moves every vertex of the MDB by `field` (positions and normals; the tangent frame is re-orthogonalised to
    the new normal); returns the number of vertices moved."""
    import struct
    moved = 0
    for o in md.objects:
        for me in o.meshes:
            el = {e.name: e for e in me.elems if e.channel == 0}
            fmt = {1: '<4f', 4: '<3f', 7: '<4e'}
            pos_e = el['POSITION']
            v = bytearray(me.vdata)
            pts = np.array([struct.unpack_from(fmt[pos_e.fmt], v, i * me.vsize + pos_e.offset)
                            for i in range(me.nverts)], dtype=np.float64)
            ny = field.heights(pts[:, :3])
            hit = np.abs(ny - pts[:, 1]) > 1e-6
            moved += int(hit.sum())
            n_e = el.get('NORMAL')
            normals = None
            if n_e is not None:
                nr = np.array([struct.unpack_from(fmt[n_e.fmt], v, i * me.vsize + n_e.offset)
                               for i in range(me.nverts)], dtype=np.float64)
                normals = field.normals(pts[:, :3], nr[:, :3])
            for i in np.nonzero(hit)[0]:
                p = list(pts[i])
                p[1] = ny[i]
                struct.pack_into(fmt[pos_e.fmt], v, i * me.vsize + pos_e.offset, *p)
                if normals is None:
                    continue
                nn = normals[i]
                old = list(struct.unpack_from(fmt[n_e.fmt], v, i * me.vsize + n_e.offset))
                struct.pack_into(fmt[n_e.fmt], v, i * me.vsize + n_e.offset, *nn, *old[3:])
                for name in ('TANGENT', 'BINORMAL'):
                    e = el.get(name)
                    if e is None:
                        continue
                    t = list(struct.unpack_from(fmt[e.fmt], v, i * me.vsize + e.offset))
                    tv = np.array(t[:3]) - np.dot(t[:3], nn) * nn
                    if np.linalg.norm(tv) > 1e-6:
                        tv = tv / np.linalg.norm(tv) * np.linalg.norm(t[:3])
                        struct.pack_into(fmt[e.fmt], v, i * me.vsize + e.offset, *tv, *t[3:])
            me.vdata = bytes(v)
    return moved
