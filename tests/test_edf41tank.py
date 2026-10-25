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
import cas_pose
import edf41calls
import edf41_objects
import edf41tank
import hk2014
import legacy_assets
import legacy_sgo
import mdb
import make_edf41_objects
import sgo


def _classic(v):
    """A DSGO's values as the classic SGO the module built had them: DSGO numbers are doubles, the module's indices
    and counts were ints (as_dsgo turned them into doubles; whole numbers back to int here)."""
    if isinstance(v, list):
        return [_classic(x) for x in v]
    if isinstance(v, dict):
        return {k: _classic(x) for k, x in v.items()}
    if isinstance(v, float) and v.is_integer():
        return int(v)
    return v


class BundledV301(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = {}
        for root in edf41tank.ROOTS:
            data = edf41_objects.read('OBJECT/' + root)
            # Vehicle601_Tank ships only as DSGO in EDF6 (make_edf41_objects check_class_format): so does V301
            assert data[:4] == b'DSGO', root
            members = _classic(edf41tank._template(data)[1])
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
                self.assertEqual(len(md.bones), 42)
                self.assertEqual(len(members['car_base_wheel']), 14)
                self.assertEqual(members['xgs_scene_object_class'], 'Vehicle601_Tank')
                self.assertEqual(members['vehicle_riding_position'][0][3], '505_TANK_DRIVER')
                self.assertEqual(sum(o.type.name == 'hkpRigidBody' for o in hk2014.read(physics).objects), 18)
                self.assertNotIn('v505', members['animation_model'][0][0].lower())

    def test_requests_keep_their_own_models(self):
        for root in edf41tank.ROOTS:
            if root.endswith('_AI.SGO'):
                continue
            self.assertEqual(edf41calls.VEHICLES[root.lower()], root.lower())

    def test_every_physical_body_has_a_reverse_initialization_binding(self):
        for root, (members, _md, physics, _cas) in self.samples.items():
            with self.subTest(root=root):
                bodies = {o['name'] for o in hk2014.read(physics).objects if o.type.name == 'hkpRigidBody'}
                reverse = sgo.read(members['ragdoll'][1])[1]['animation_from_ragdoll']
                self.assertFalse(bodies - {row[0][0] for row in reverse},
                                 'native initializes body+0x60 from this stage before any animation update')

    def test_505_subclass_cannot_be_used_without_its_model_capabilities(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        broken['xgs_scene_object_class'] = 'Vehicle505_Tank'
        with self.assertRaisesRegex(ValueError, 'Vehicle601_Tank class contract'):
            edf41tank.check(broken, *rest)

    def test_missing_existing_body_binding_is_refused(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        version, binding = sgo.read(broken['ragdoll'][1])
        binding['animation_from_ragdoll'] = [row for row in binding['animation_from_ragdoll']
                                              if row[0][0] != 'RagDollProxys.cannon_slide']
        broken['ragdoll'][1] = sgo.write_depth_first(version, binding)
        with self.assertRaisesRegex(ValueError, 'unbound animation_from_ragdoll.*cannon_slide'):
            edf41tank.check(broken, *rest)

    def test_reverse_binding_cannot_take_animation_ownership_back(self):
        members, *rest = self.samples['VEHICLE301_TANK.SGO']
        broken = copy.deepcopy(members)
        version, binding = sgo.read(broken['ragdoll'][1])
        for row in binding['animation_from_ragdoll']:
            if row[0][0] == 'RagDollProxys.cannon_slide':
                row[0][1] = 'cannon_slide'
        broken['ragdoll'][1] = sgo.write_depth_first(version, binding)
        with self.assertRaisesRegex(ValueError, 'recoil readback'):
            edf41tank.check(broken, *rest)

    def test_class_posed_bones_keep_a_static_rest_track(self):
        # Vehicle601_Tank's pose turns these bones' locals by the aim's pitch in place every frame (0x661810 reads the
        # record's +0x70 and writes it back turned): the CAS must rewrite them each frame, or the turn accumulates (the
        # spinning barrel whose matrix reaches 1e20 and greys out the map, docs/feedback-2026-10-25-greymap.md).
        self.assertEqual(edf41tank.CLASS_POSED, {'cannon'})
        for root, (members, md, physics, cas) in self.samples.items():
            with self.subTest(root=root):
                pose = cas_pose.CasPose(cas)
                for clip in pose.clips:
                    tracks = {t.name: t for t in clip.tracks}
                    self.assertLessEqual(edf41tank.CLASS_POSED, set(tracks))
                    for name in edf41tank.CLASS_POSED:
                        local = md.bones[mdb.bone_named(md, name)].local
                        for frame in range(cas_pose.clip_info(pose, clip)[2]):
                            at = pose.translation(tracks[name].translation, frame)
                            self.assertLess(max(abs(a - b) for a, b in zip(at, local[12:15])), 1e-4)

    def test_cas_without_the_cannon_rest_track_is_refused(self):
        members, md, physics, cas = self.samples['VEHICLE301_TANK.SGO']
        stripped = cas_pose.retarget(cas, md, md, set(), {'cannon'})   # the 2026-10-19 CAS: the recoil track alone
        self.assertEqual({t.name for c in cas_pose.CasPose(stripped).clips for t in c.tracks}, {'cannon_slide'})
        with self.assertRaisesRegex(ValueError, 'rest pose of'):
            edf41tank.check(members, md, physics, stripped)

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
                self.assertEqual(edf41_objects.read(archive), edf41tank.recoil_model_archive(
                    legacy_assets.convert(archive, self.games.read4(archive)), entry[1]))
                physics = edf41tank.rel(source['ragdoll'][0])
                self.assertEqual(edf41_objects.read(physics), self.games.read4(physics))

    def test_all_seven_real_sources_keep_offsets_and_complete_native_initialization(self):
        for root in edf41tank.ROOTS:
            with self.subTest(root=root):
                output = edf41tank.build(self.games, root)
                members = sgo.read(output['OBJECT/' + root])[1]
                self.assertEqual(members['xgs_scene_object_class'], 'Vehicle601_Tank')
                source = sgo.read(legacy_sgo.convert(self.games.read4('OBJECT/' + root)))[1]
                original = sgo.read(source['ragdoll'][1])[1]
                for field in ('vehicle_setup', 'mission_setup'):
                    if field not in source:
                        continue
                    expected_setup = copy.deepcopy(source[field])
                    for row in expected_setup[2]:
                        row[0] = 'app:/weapon/' + edf41calls.WEAPONS[edf41calls.leaf(row[0])][0] + '.sgo'
                    self.assertEqual(members[field], expected_setup)
                if root.endswith('_AI.SGO'):
                    self.assertEqual(members['mission_setup'][2][0][0], 'app:/weapon/v_505_tank_ai_cannon01.sgo')
                binding = sgo.read(members['ragdoll'][1])[1]
                expected = copy.deepcopy(original['animation_from_ragdoll'])
                for row in expected:
                    if row[0][1] == 'cannon_slide':
                        row[0][1] = edf41tank.RECOIL_BIND_BONE
                self.assertEqual(binding['animation_from_ragdoll'], expected)
                self.assertEqual(binding['ragdoll_from_animation'], original['ragdoll_from_animation'])
                entry = members['animation_model'][0]
                model_bytes = output[edf41tank.rel(entry[0])]
                md = edf41tank.model(model_bytes, entry[1])
                self.assertEqual(len(md.bones), 42)
                self.assertEqual(mdb.mdb_read(mdb.mdb_write(md)), md)
                self.assertEqual(mdb.rab_write(mdb.rab_read(model_bytes)), model_bytes)
                edf41tank.check(members, md, output[edf41tank.rel(members['ragdoll'][0])],
                               output[edf41tank.rel(members['animation_model'][1])])


if __name__ == '__main__':
    unittest.main()
