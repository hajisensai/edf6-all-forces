"""pylib/model_pieces.py on synthetic meshes (no game files)."""
from __future__ import annotations

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import hkshape as hs  # noqa: E402
import model_pieces as mp  # noqa: E402


def box_mesh(lo, hi):  # noqa: ANN001, ANN201 - 12 triangles of an axis-aligned box (separate faces' own vertices)
    c = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return [(c[a], c[b], c[d]) for a, b, d, _ in faces] + [(c[a], c[d], c[e]) for a, _, d, e in faces]


def ring(r_in: float, r_out: float, h: float, n: int = 24):  # noqa: ANN201 - a closed annulus (a turret ring)
    tris = []
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        P = lambda r, a, y: (r * math.cos(a), y, r * math.sin(a))      # noqa: E731
        for r in (r_in, r_out):
            tris += [(P(r, a0, 0), P(r, a1, 0), P(r, a1, h)), (P(r, a0, 0), P(r, a1, h), P(r, a0, h))]
        for y in (0, h):
            tris += [(P(r_in, a0, y), P(r_out, a0, y), P(r_out, a1, y)), (P(r_in, a0, y), P(r_out, a1, y), P(r_in, a1, y))]
    return tris


class Pieces(unittest.TestCase):
    def test_connected_and_welded(self) -> None:
        tris = box_mesh((0, 0, 0), (1, 1, 1)) + box_mesh((3, 0, 0), (4, 1, 1))
        pts, idx = [], []
        for t in tris:
            idx.append(tuple(range(len(pts), len(pts) + 3)))
            pts += list(t)
        self.assertEqual(sorted(len(g) for g in mp.connected(pts, idx)), [12, 12])
        sets = mp.fit(tris)
        self.assertEqual(len(sets), 2)
        self.assertEqual(sorted(len(s) for s in sets), [8, 8])

    def test_a_ring_is_cut_and_its_hole_stays_open(self) -> None:
        sets = mp.fit(ring(0.6, 0.9, 0.2), tolerance=0.02)
        self.assertGreater(len(sets), 4)
        hulls = [hs.build_hull(s) for s in sets]
        inside = lambda p: any(max(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] for pl in h.planes) <= 0  # noqa
                               for h in hulls)
        self.assertFalse(inside((0.0, 0.1, 0.0)))          # the well
        self.assertTrue(inside((0.75, 0.1, 0.0)))          # the wall
        self.assertEqual(mp.coverage([p for t in ring(0.6, 0.9, 0.2) for p in t], sets, 1e-6), 1.0)

    def test_cut_to_a_box_keeps_the_solid(self) -> None:
        cube = [(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)]
        inner = mp.intersect_box(cube, ((-0.5, -0.5, -0.5), (0.5, 0.5, 2.0)))
        want = sorted((x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 1.0))
        self.assertEqual(len(inner), 8)
        self.assertTrue(all(max(abs(a - b) for a, b in zip(p, q)) < 1e-5 for p, q in zip(sorted(inner), want)))
        self.assertIsNone(mp.intersect_box(cube, ((2.0, 2.0, 2.0), (3.0, 3.0, 3.0))))
        sets = mp.fit(box_mesh((-1, -1, -1), (1, 1, 1)), box=((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5)))
        self.assertEqual(len(sets), 1)
        self.assertAlmostEqual(hs.from_hull(hs.build_hull(sets[0]), 0).volume, 1.0)

    def test_flat_and_shrunk(self) -> None:
        plate = [((0, 0, 0), (1, 0, 0), (1, 1, 0)), ((0, 0, 0), (1, 1, 0), (0, 1, 0))]
        sets = mp.fit(plate, thickness=0.02)
        self.assertEqual(len(sets), 1)
        self.assertAlmostEqual(hs.build_hull(sets[0]).min_width(), 0.02, places=6)
        pts, r = mp.shrink([(x, y, z) for x in (0.0, 1.0) for y in (0.0, 2.0) for z in (0.0, 0.1)], 0.01)
        self.assertEqual(r, 0.01)
        h = hs.build_hull(pts)
        self.assertAlmostEqual(min(p[2] for p in h.vertices) - r, 0.0)
        self.assertAlmostEqual(max(p[1] for p in h.vertices) + r, 2.0)

    def test_reduce(self) -> None:
        ball = [(math.cos(a) * math.sin(b), math.sin(a) * math.sin(b), math.cos(b))
                for a in [k * 0.3 for k in range(21)] for b in [k * 0.3 + 0.15 for k in range(11)]]
        few = mp.reduce(ball, 32)
        self.assertLessEqual(len(few), 32)
        full = hs.build_hull(ball)
        self.assertTrue(all(max(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] for pl in full.planes) <= 1e-6
                            for p in few))
        self.assertGreater(hs.from_hull(hs.build_hull(few), 0).volume, 0.8 * hs.from_hull(full, 0).volume)


if __name__ == '__main__':
    unittest.main()
