"""Regression checks for 406's real geometry, six mounts and EDF6 ABI upgrade."""
import copy
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import dsgo
import edf41_objects
import edf41calls
import edf41mech
import legacy_assets
import mab_legacy
import sgo


class VehicleUpgrade(unittest.TestCase):
    def test_upgrade_keeps_geometry_and_six_mounts(self):
        source = {'xgs_scene_object_class': 'VehicleBegaruta',
                  'vehicle_weapon_setting': [[str(i), 0] for i in range(6)],
                  'vehicle_setup': [0, 0, 0, 0, [[0]] * 6, 0],
                  'vehicle_riding_position': [['entry', 'seat', ['sight', 0, 0], '406_DRIVER', 9, 1]],
                  'animation_model': [['original.rab', 'original.mdb'], 'original.cas', b'original MAB']}
        original = copy.deepcopy(source)
        driver = ['other entry', 'other seat', ['other sight', 1, 1], '504_BEGARUTA_DRIVER', 15, 10.0, 2]
        out = edf41mech.vehicle(source, driver)
        self.assertEqual(source, original)
        self.assertEqual(out['animation_model'], source['animation_model'])
        self.assertEqual(out['vehicle_setup'], source['vehicle_setup'])
        self.assertEqual(out['vehicle_riding_position'][0][:3], source['vehicle_riding_position'][0][:3])
        self.assertEqual(out['vehicle_riding_position'][0][3:], driver[3:])
        self.assertEqual(out['begaruta_foot_node'], ['ashi_l', 'ashi_r'])

    def test_eight_mount_remake_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'six weapon mounts'):
            edf41mech.vehicle({'xgs_scene_object_class': 'VehicleBegaruta',
                              'vehicle_weapon_setting': [[0]] * 8}, [0] * 7)


class Bundled406(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = {}
        for root in edf41mech.ROOTS:
            members = sgo.read(edf41_objects.read('OBJECT/' + root))[1]
            am = members['animation_model']
            md = edf41mech.model(edf41_objects.read(edf41mech.rel(am[0][0][0])), am[0][0][1])
            physics = edf41_objects.read(edf41mech.rel(members['ragdoll'][0]))
            cas = edf41_objects.read(edf41mech.rel(am[1]))
            cls.samples[root] = members, md, physics, cas

    def test_all_four_model_contracts(self):
        for root, sample in self.samples.items():
            with self.subTest(root=root):
                edf41mech.check(*sample)
                members, md, _, _ = sample
                self.assertEqual(len(md.bones), 68 if '_GOLD' in root else 67)
                self.assertEqual(len(members['vehicle_weapon_setting']), 6)
                self.assertNotIn('v504', members['animation_model'][0][0][0].lower())
                self.assertEqual(members['vehicle_riding_position'][0][3], '504_BEGARUTA_DRIVER')
                self.assertEqual(edf41calls.VEHICLES[root.lower()], root.lower())

    def test_missing_source_mount_is_refused(self):
        members, *rest = self.samples[edf41mech.ROOTS[0]]
        broken = copy.deepcopy(members)
        broken['vehicle_weapon_setting'][2][0] = 'weapon_lowerArm_r'
        with self.assertRaisesRegex(ValueError, 'runtime bone contract'):
            edf41mech.check(broken, *rest)

    def test_wrong_physics_rotation_is_refused(self):
        members, *rest = self.samples[edf41mech.ROOTS[0]]
        broken = copy.deepcopy(members)
        version, binding = sgo.read(broken['ragdoll'][1])
        binding['ragdoll_from_animation'][0][2] = [0.0, 0.0, 1.0, 0.0]
        broken['ragdoll'][1] = sgo.write_depth_first(version, binding)
        with self.assertRaisesRegex(ValueError, 'physics bind frames disagree'):
            edf41mech.check(broken, *rest)

    def test_original_weapon_models_are_private(self):
        for path in edf41mech.WEAPONS:
            with self.subTest(path=path):
                data = edf41_objects.read(path)
                self.assertEqual(data[:4], b'DSGO')
                weapon = dsgo.parse(data).root
                self.assertTrue(weapon.get('name.en'))
                self.assertTrue(weapon.get('name.sc'))
                am = weapon.get('animation_model')
                model_path = edf41mech.rel(am.items[0].items[0])
                self.assertTrue(model_path.startswith(edf41mech.PRIVATE))
                edf41mech.model(edf41_objects.read(model_path), am.items[0].items[1])
                self.assertEqual(len(am.items[2].data) > 0, True)


class Native406(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        edf41 = Path(os.environ.get('EDF41_GAME_DIR', r'D:\Steam\steamapps\common\Earth Defense Force 4.1'))
        edf6 = Path(os.environ.get('EDF6_GAME_DIR', r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 6'))
        if not (edf41 / 'Root.cpk').is_file() or not (edf6 / 'Root.cpk').is_file():
            raise unittest.SkipTest('source Steam games are not installed')
        import make_edf41_objects
        cls.games = make_edf41_objects.Games(str(edf41), str(edf6))
        cls.edf6 = edf6

    def test_models_cas_and_physics_are_source_406(self):
        for root in edf41mech.ROOTS:
            with self.subTest(root=root):
                members = sgo.read(self.games.read4('OBJECT/' + root))[1]
                archive = edf41mech.rel(members['animation_model'][0][0][0])
                cas = edf41mech.rel(members['animation_model'][1])
                physics = edf41mech.rel(members['ragdoll'][0])
                self.assertEqual(edf41_objects.read(archive), legacy_assets.convert(archive, self.games.read4(archive)))
                self.assertEqual(edf41_objects.read(cas), legacy_assets.convert(cas, self.games.read4(cas)))
                self.assertEqual(edf41_objects.read(physics), self.games.read4(physics))
                bundled = sgo.read(edf41_objects.read('OBJECT/' + root))[1]
                self.assertEqual(bundled['ragdoll'][1], members['ragdoll'][1])
                self.assertEqual(bundled['vehicle_weapon_setting'], members['vehicle_weapon_setting'])
                self.assertEqual(bundled['begaruta_boost'], members['begaruta_boost'])

    def test_sound_names_keep_cues_and_event_timing(self):
        source_presets = sgo.load(data=self.games.read4('SOUND/SEPRESET.SGO'))
        stock_presets = sgo.load(data=self.games.read6('SOUND/SEPRESET.SGO'))
        for root in edf41mech.ROOTS:
            with self.subTest(root=root):
                original = sgo.read(self.games.read4('OBJECT/' + root))[1]
                bundled = sgo.read(edf41_objects.read('OBJECT/' + root))[1]
                before = original['animation_model'][2]
                after = bundled['animation_model'][2]
                self.assertEqual(mab_legacy.mab_locators(before), mab_legacy.mab_locators(after))
                a, b = mab_legacy.mab_event_params(before), mab_legacy.mab_event_params(after)
                self.assertEqual(a.keys(), b.keys())
                for track in a:
                    self.assertEqual(len(a[track]), len(b[track]))
                    for old, new in zip(a[track], b[track]):
                        self.assertEqual(old[:2], new[:2])
                        if old[0] == 'se':
                            old_name, new_name = old[2]['name'], new[2]['name']
                            self.assertIn(new_name, stock_presets)
                            self.assertEqual(source_presets[old_name][0], stock_presets[new_name][0])
                            self.assertEqual({k: v for k, v in old[2].items() if k != 'name'},
                                             {k: v for k, v in new[2].items() if k != 'name'})
                        else:
                            self.assertEqual(old[2], new[2])
                for key in ('begaruta_se_table', 'begaruta_aim_se_table'):
                    for name in bundled[key]:
                        if isinstance(name, str):
                            self.assertIn(name, stock_presets)

    def test_native_retains_old_class_and_boost_parser(self):
        try:
            import pefile
        except ImportError:
            self.skipTest('pefile is not installed')
        pe = pefile.PE(str(self.edf6 / 'EDF.dll'), fast_load=True)
        self.assertEqual(pe.FILE_HEADER.TimeDateStamp, 0x678CCB46)
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.assertEqual(struct.unpack_from('<Q', image, 0x17DDE90)[0] - base, 0x634D60)
        self.assertEqual(struct.unpack_from('<Q', image, 0x17DDE98)[0] - base, 0x634D70)
        for at, text in [(0x634D60, 'VehicleBegaruta'), (0x635F29, 'begaruta_foot_node'),
                         (0x636DCE, 'begaruta_boost'), (0x636E3B, 'vehicle_dead_effect')]:
            self.assertEqual(image[at + 1], 0x8D)
            target = at + 7 + struct.unpack_from('<i', image, at + 3)[0]
            self.assertTrue(image[target:].startswith(text.encode('utf-16le') + b'\0\0'))
        self.assertEqual(image[0x636E07], 0xE8)
        self.assertEqual(0x636E0C + struct.unpack_from('<i', image, 0x636E08)[0], 0x640340)


if __name__ == '__main__':
    unittest.main()
