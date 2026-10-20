"""pylib/vehicle_check.py. The first class needs no game files; the second checks every stock vehicle of Root.cpk and
the DLC archives (skipped without the game): no error, no warning (the calibration of its rules)."""
from __future__ import annotations

import math
import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import gamedir  # noqa: E402
import vehicle_check as vc  # noqa: E402


def wheel(i: int, node: str, spring: float = 3.0) -> vc.Wheel:
    return vc.Wheel(i, node, (0.0, -1.0, 0.0), 0.0, 0.5, 0.1, spring, 0.4, 0.2, None)


class Portable(unittest.TestCase):
    def test_settings(self) -> None:
        doc = {'car_base_all_rigid_body': [1000.0, 0.1, 0.2, [0.0, -0.5, 0.0]],
               'car_base_rigid_body': [['body', 2.0, -1, -1, -1]],
               'car_base_wheel': [['tire_l', [0, -1, 0], 0.0, 0.5, 0.1, 3.0, 0.5, 0.5, 0.4, 0.2]],
               'car_base_cast_mode': [1], 'car_base_simulation_node': [['tire_l', 0.0]]}
        v = vc.settings(doc)
        assert v is not None
        self.assertEqual(v.total_mass, 1000.0)
        self.assertEqual(v.com_offset, (0.0, -0.5, 0.0))
        self.assertEqual(v.factors, [('body', 2.0)])
        self.assertEqual((v.wheels[0].node, v.wheels[0].spring, v.wheels[0].radius, v.wheels[0].cast_mode),
                         ('tire_l', 3.0, 0.4, 1))
        self.assertIn(('car_base_simulation_node[0]', 'tire_l', True), v.names)
        self.assertIsNone(vc.settings({'name': 'not a vehicle'}))

    def test_rest_compression_carries_the_weight(self) -> None:
        """Four equal springs round a centred mass share it; moving the mass forward loads the front, and the
        compressions keep carrying it (sum k c = g, sum k c x = g x0, sum k c z = g z0)."""
        pos = {'fl': (1.0, 0.0, 2.0), 'fr': (-1.0, 0.0, 2.0), 'bl': (1.0, 0.0, -2.0), 'br': (-1.0, 0.0, -2.0)}
        ws = [wheel(i, n) for i, n in enumerate(pos)]
        c = vc.rest_compression(ws, pos.get, (0.0, 0.0, 0.0))
        self.assertTrue(all(abs(x - vc.GRAVITY / 12) < 1e-12 for x in c))
        c = vc.rest_compression(ws, pos.get, (0.0, 0.0, 1.0))
        self.assertGreater(c[0], c[2])
        self.assertAlmostEqual(sum(3.0 * x for x in c), vc.GRAVITY)
        self.assertAlmostEqual(sum(3.0 * x * pos[w.node][2] for w, x in zip(ws, c)), vc.GRAVITY * 1.0)
        c = vc.rest_compression(ws, pos.get, (0.0, 0.0, 5.0))      # far ahead of the front axle: the rear lifts
        self.assertLess(c[2], 0)
        c = vc.rest_compression(ws, lambda n: None, (0.0, 0.0, 0.0))
        self.assertTrue(all(abs(x - vc.GRAVITY / 12) < 1e-12 for x in c))

    def test_names_through_the_ragdoll_binding(self) -> None:
        bs = [vc.Body('ragdoll_cannon_main', (0, 0, 0), (0, 0, 0, 1), 1.0, (0, 0, 0), None),
              vc.Body('RagDollProxys.body', (0, 0, 0), (0, 0, 0, 1), 1.0, (0, 0, 0), None)]
        self.assertIs(vc.find_body(bs, 'body'), bs[1])
        self.assertIsNone(vc.find_body(bs, 'cannon_main'))
        self.assertIs(vc.find_body(bs, 'cannon_main', {'cannon_main': 'ragdoll_cannon_main'}), bs[0])

    def test_mass_balance_and_centre(self) -> None:
        bs = [vc.Body('a', (0.0, 0.0, 0.0), (0, 0, 0, 1), 1.0, (0.0, 1.0, 0.0), None),
              vc.Body('b', (2.0, 0.0, 0.0), (0, 0, 0, 1), 3.0, (0.0, 0.0, 0.0), None)]
        v = vc.Settings(8.0, (0.0, 0.0, 0.0), [('b', 0.5)], [], [], False)
        self.assertEqual([m for _, m in vc.mass_balance(bs, v)], [2.0, 3.0])
        com = vc.centre_of_mass(bs, v, None)
        self.assertTrue(math.isclose(com[0], 6 / 5) and math.isclose(com[1], 2 / 5))


GAME = gamedir.find()


@unittest.skipUnless(GAME, 'needs the game (Root.cpk)')
class Stock(unittest.TestCase):
    def test_every_stock_vehicle(self) -> None:
        import check_shkt
        import shkt_check as sc
        files = check_shkt.Files(GAME, True)
        n = 0
        for ref in files.own(True, '.SGO'):
            data = files.read(ref) or b''
            if data[:4] not in sc.SGO_MAGICS:
                continue
            doc = sc.load_plain(data)
            if not doc or not doc.get('car_base_wheel'):
                continue
            shkt = vc.shkt_of(doc)
            sd = files.read(shkt) if shkt else None
            if sd is None:
                continue
            bones = vc.model_bones(doc, files.read)
            self.assertIsNotNone(bones, ref)
            self.assertEqual([str(p) for p in vc.vehicle_problems(doc, sd, bones, ref)], [])
            n += 1
        self.assertGreaterEqual(n, 48)


if __name__ == '__main__':
    unittest.main()
