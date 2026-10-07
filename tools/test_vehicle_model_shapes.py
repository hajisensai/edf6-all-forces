"""Vehicle shape regressions. No installs or game writes; --game enables read-only Root.cpk checks."""
from __future__ import annotations

import argparse
from collections import Counter
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import graft_pure as g
import katyusha_model as km
import make_katyusha
import make_sidecar
import rootcpk
import sidecar_model as sm


class ShapeTests(unittest.TestCase):
    def test_deck_and_shell_share_the_complete_nose_boundary(self) -> None:
        part = sm.tub_body([(0, 1.0)])
        edges = Counter(tuple(sorted(e)) for a, b, c in part.tris for e in ((a, b), (b, c), (c, a)))
        # The first RING vertices are the shell's actual top edge. Both deck surfaces
        # must attach to each front rim edge; an independently sampled surface has a slit.
        front = [i for i in range(sm.RING // 2 + 1) if part.pos[i][2] >= sm.DECK_Z]
        self.assertGreater(len(front), 20)
        for a, b in zip(front, front[1:]):
            self.assertEqual(edges[(a, b)], 4, f'open deck-to-shell seam at rim edge {a}, {b}')

    def test_all_new_parts_have_real_faces_and_clear_passenger_space(self) -> None:
        parts = sm.sidecar_parts(0)
        for name, part in parts.items():
            with self.subTest(part=name):
                tri = np.array(part.pos)[np.array(part.tris)]
                twice_area = np.linalg.norm(np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]), axis=1)
                self.assertTrue(np.all(twice_area > 1e-10), 'zero-area surface in generated part')
        measured = sm.check_tub(sm.parts_triangles(list(parts.values())), sm.SOLDIER_REACH)
        self.assertGreaterEqual(measured['nearest wall'], sm.SOLDIER_REACH)
        self.assertEqual(sm.GUNNER_POINT, (-1.0, 0.30, 0.35))  # plugin's established attachment contract
        self.assertEqual(sm.FLOOR, ((-1.46, 0.20, -0.60), (-0.54, 0.30, 1.50)))
        frame = np.array(parts['frame'].pos)
        inside = frame[(frame[:, 0] > sm.TUB_OUT + sm.WALL + 0.03) &
                       (frame[:, 0] < sm.TUB_IN - sm.WALL - 0.03) &
                       (frame[:, 2] > -0.3) & (frame[:, 2] < 0.9)]
        self.assertGreater(len(inside), 0)
        self.assertLessEqual(float(inside[:, 1].max()), sm.FLOOR_Y, 'a support arm protrudes into the cockpit')

    def test_sideboard_region_does_not_include_frame_cab_or_tailgate(self) -> None:
        side = [(1.3, 1.8, -2.0), (1.3, 2.1, -2.0), (1.3, 2.1, 0.5)]
        self.assertTrue(km.sideboard_triangle(side))
        self.assertTrue(km.sideboard_triangle([(-x, y, z) for x, y, z in side]))
        for points in (
            [(x, y - 1.0, z) for x, y, z in side],  # wheel/frame below the bed
            [(x, y, z + 4.0) for x, y, z in side],  # cabin
            [(-1.3, 1.8, -3.9), (1.3, 1.8, -3.9), (1.3, 2.1, -3.9)],  # tailgate across the bed
        ):
            self.assertFalse(km.sideboard_triangle(points))

    def test_bed_rails_include_the_tailgate_but_not_floor_bumper_or_cab(self) -> None:
        # Measured V607 tailgate: inner face z -3.89 (y 1.64..2.18), outer skin to -3.99 wrapping down to 1.57.
        for points in (
            [(-1.3, 1.64, -3.89), (1.3, 1.64, -3.89), (1.3, 2.18, -3.89)],   # inner face, standing on the floor
            [(-1.3, 1.57, -3.99), (1.3, 1.57, -3.99), (1.3, 1.74, -3.99)],   # outer lower skin over the floor's edge
            [(1.4, 1.57, -3.94), (1.4, 1.74, -3.92), (1.4, 1.74, -3.94)],    # hinge corner
            [(1.3, 1.8, -2.0), (1.3, 2.1, -2.0), (1.3, 2.1, 0.5)],           # sideboard
        ):
            self.assertTrue(km.bed_rail_triangle(points), points)
        for points in (
            [(0.0, 1.64, -3.89), (1.3, 1.64, -3.89), (1.3, 1.64, -3.59)],    # bed floor up to its rear edge
            [(-1.3, 1.2, -3.99), (1.3, 1.2, -3.99), (1.3, 1.45, -3.99)],     # bumper / lights below the bed
            [(1.4, 2.2, 1.1), (1.4, 3.4, 1.1), (1.4, 3.4, 1.8)],             # cab
        ):
            self.assertFalse(km.bed_rail_triangle(points), points)


class RealModelTests(unittest.TestCase):
    game_path = ''

    @classmethod
    def setUpClass(cls) -> None:
        if not cls.game_path:
            raise unittest.SkipTest('pass --game for read-only model generation checks')
        cls.game = rootcpk.Game(cls.game_path)
        with patch.object(km, 'remove_bed_rails', lambda md, body, offset: (md, 0, 0)):
            cls.before = km.build_model(cls.game)[0]
        cls.after, _, _, _, cls.info = km.build_model(cls.game)

    def test_only_the_identified_body_faces_are_removed(self) -> None:
        body = self.before.bone_index('body')
        offset = self.info['truck_offset']
        expected, actual, removed_sides = Counter(), Counter(), Counter()
        for md, target, strip in ((self.before, expected, True), (self.after, actual, False)):
            for me in md.objects[0].meshes:
                points, (bi, _bw) = g.mesh_positions(me), g.skin_columns(me)
                for tri in g.triangles(me):
                    donor = [tuple((points[v][c] - offset[c]) / km.TRUCK_SCALE for c in range(3)) for v in tri]
                    if strip and all(int(bi[v][0]) == body for v in tri) and km.bed_rail_triangle(donor):
                        removed_sides['tailgate' if km.tailgate_triangle(donor)
                                      else 'left' if donor[0][0] > 0 else 'right'] += 1
                        continue
                    # Byte-level retention of vertex attributes (position, skin, normals, UVs).
                    target[(me.material, tuple(me.vdata[v * me.vsize:(v + 1) * me.vsize] for v in tri))] += 1
        # Everything kept is byte-identical; the only additions are the faces closing the floor's rear edge.
        self.assertEqual(expected - actual, Counter())
        self.assertEqual(sum((actual - expected).values()), self.info['tailgate_cap_faces'])
        self.assertGreater(removed_sides['left'], 100)
        self.assertGreater(removed_sides['right'], 100)
        self.assertGreater(removed_sides['tailgate'], 100)
        self.assertEqual(sum(removed_sides.values()), self.info['bed_rail_faces_removed'])
        # Nothing of the truck's body stands over the bed's edge trim behind the floor: the tailgate is gone whole
        # (it stood to 2.18 m; the floor's side trim tops out at 1.74 m).
        rear_edges = Counter()
        for me in self.after.objects[0].meshes:
            points, (bi, _bw) = g.mesh_positions(me), g.skin_columns(me)
            for tri in g.triangles(me):
                if not all(int(bi[v][0]) == body for v in tri):
                    continue
                p = [tuple(round(points[v][c] - offset[c], 4) for c in range(3)) for v in tri]
                for y in (q[1] for q in p if q[2] < km.TAILGATE_INNER_Z):
                    self.assertLess(y, 1.75)
                for a, b in ((p[0], p[1]), (p[1], p[2]), (p[2], p[0])):
                    rear_edges[tuple(sorted((a, b)))] += 1
        # ...and the floor's rear edge it covered is closed: every edge the tailgate shared with a kept face
        # (floor top, underside, side trim ends) has a face on its other side again.
        seam = self.tailgate_seam()
        self.assertGreater(len(seam), 8)
        for edge in seam:
            self.assertGreaterEqual(rear_edges[edge], 2, f'open edge {edge} where the tailgate was')

        for before, after in zip(self.before.bones, self.after.bones):
            self.assertEqual(before.local, after.local)
            self.assertEqual(before.inv_bind, after.inv_bind)
        self.assertEqual(self.before.objects[1].meshes, self.after.objects[1].meshes)

    def tailgate_seam(self) -> set:
        body = self.before.bone_index('body')
        offset = self.info['truck_offset']
        gate, rest = Counter(), Counter()
        for me in self.before.objects[0].meshes:
            points, (bi, _bw) = g.mesh_positions(me), g.skin_columns(me)
            for tri in g.triangles(me):
                p = [tuple(round(points[v][c] - offset[c], 4) for c in range(3)) for v in tri]
                donor = [tuple(q[c] / km.TRUCK_SCALE for c in range(3)) for q in p]
                body_face = all(int(bi[v][0]) == body for v in tri)
                if body_face and km.bed_rail_triangle(donor):
                    target = gate if km.tailgate_triangle(donor) else None
                else:
                    target = rest
                if target is not None:
                    for a, b in ((p[0], p[1]), (p[1], p[2]), (p[2], p[0])):
                        target[tuple(sorted((a, b)))] += 1
        return set(gate) & set(rest)

    def test_both_actual_built_archives_and_collision_pass_checks(self) -> None:
        make_katyusha.build(self.game_path)  # includes model, vehicle SGO, rocket SGO and muzzle agreement checks
        files = make_sidecar.build(self.game_path)
        report = make_sidecar.check(files, self.game)
        self.assertGreater(report['model']['nearest wall'], report['soldier reach'])
        self.assertAlmostEqual(report['collision']['top y (model)'], sm.FLOOR_Y, places=6)


def run_checks() -> None:
    result = unittest.TextTestRunner(stream=io.StringIO()).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ShapeTests))
    if not result.wasSuccessful():
        raise AssertionError('\n'.join(text for _, text in result.errors + result.failures))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', default='', help='read Root.cpk only; never install or write into this directory')
    args, remaining = ap.parse_known_args()
    RealModelTests.game_path = args.game
    unittest.main(argv=[sys.argv[0], *remaining])
