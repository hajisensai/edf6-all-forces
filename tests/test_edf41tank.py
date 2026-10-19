"""Regression tests for real V301 assets and its EDF6 runtime contracts.

The bundled tests require no game. Native provenance runs additionally when the two
Steam games are installed, without writing either game or starting a process.
"""
import copy
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import edf41calls
import edf41_objects
import edf41tank
import hk2014
import legacy_assets
import make_edf41_objects
import sgo


class BundledV301(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = {}
        for root in edf41tank.ROOTS:
            members = sgo.read(edf41_objects.read('OBJECT/' + root))[1]
            entry = members['animation_model'][0]
            md = edf41tank.model(edf41_objects.read(edf41tank.rel(entry[0])), entry[1])
            physics = edf41_objects.read(edf41tank.rel(members['ragdoll'][0]))
            cas = edf41_objects.read(edf41tank.rel(members['animation_model'][1]))
            cls.samples[root] = members, md, physics, cas

    def test_every_model_and_physics_contract(self):
        for root, sample in self.samples.items():
            with self.subTest(root=root):
                edf41tank.check(*sample)
                members, md, physics, _ = sample
                self.assertEqual(len(md.bones), 41)
                self.assertEqual(len(members['car_base_wheel']), 14)
                self.assertEqual(members['xgs_scene_object_class'], 'Vehicle505_Tank')
                self.assertEqual(members['vehicle_riding_position'][0][3], '505_TANK_DRIVER')
                self.assertEqual(sum(o.type.name == 'hkpRigidBody' for o in hk2014.read(physics).objects), 18)
                self.assertNotIn('v505', members['animation_model'][0][0].lower())

    def test_requests_keep_their_own_models(self):
        for root in edf41tank.ROOTS:
            if root.endswith('_AI.SGO'):
                continue
            self.assertEqual(edf41calls.VEHICLES[root.lower()], root.lower())

    def test_another_skeleton_wheel_is_refused(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        broken['car_base_wheel'][0][0] = 'tire_moveH_l'
        with self.assertRaisesRegex(ValueError, 'bone contract'):
            edf41tank.check(broken, *rest)

    def test_wrong_physics_rotation_is_refused(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        version, binding = sgo.read(broken['ragdoll'][1])
        binding['ragdoll_from_animation'][0][2] = [0.0, 0.0, 1.0, 0.0]
        broken['ragdoll'][1] = sgo.write_depth_first(version, binding)
        with self.assertRaisesRegex(ValueError, 'physics bind frames disagree'):
            edf41tank.check(broken, *rest)

    def test_stale_wheel_index_is_refused(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        broken['car_base_draw_wheel_info'][0][0][0] = 13
        with self.assertRaisesRegex(ValueError, 'another wheel'):
            edf41tank.check(broken, *rest)


class NativeV301(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        edf41 = Path(os.environ.get('EDF41_GAME_DIR', r'D:\Steam\steamapps\common\Earth Defense Force 4.1'))
        edf6 = Path(os.environ.get('EDF6_GAME_DIR', r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 6'))
        if not (edf41 / 'Root.cpk').is_file() or not (edf6 / 'Root.cpk').is_file():
            raise unittest.SkipTest('source Steam games are not installed')
        cls.games = make_edf41_objects.Games(str(edf41), str(edf6))

    def test_bundled_assets_are_the_real_source(self):
        for root in edf41tank.ROOTS:
            with self.subTest(root=root):
                source = sgo.read(self.games.read4('OBJECT/' + root))[1]
                entry = source['animation_model'][0]
                if not isinstance(entry[0], str):
                    entry = entry[0]
                archive = edf41tank.rel(entry[0])
                self.assertEqual(edf41_objects.read(archive), legacy_assets.convert(archive, self.games.read4(archive)))
                physics = edf41tank.rel(source['ragdoll'][0])
                self.assertEqual(edf41_objects.read(physics), self.games.read4(physics))


if __name__ == '__main__':
    unittest.main()
