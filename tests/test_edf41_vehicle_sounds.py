"""A registered vehicle preset must resolve to a bank that the object loads."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import make_edf41_objects as mo
import sgo


class VehicleSounds(unittest.TestCase):
    def check(self, members):
        games = SimpleNamespace(seset={'engine': 'engine_cue'}, banks={'TANK.ACB': {'engine_cue'}})
        mo.check_vehicle_sounds(games, sgo.write_depth_first(0x102, members), 'OBJECT/EXAMPLE.SGO')

    def object(self):
        return {'xgs_scene_object_class': 'Vehicle505_Tank',
                'car_base_se_table': ['engine'], 'game_sound': ['app:/sound/adx/TANK.ACB']}

    def test_native_carbase_table_is_checked(self):
        self.check(self.object())

    def test_unknown_class_preset_is_refused(self):
        for key in ('car_base_se_table', 'heli_se_table', 'begaruta_se_table', 'begaruta_aim_se_table'):
            with self.subTest(key=key):
                members = self.object()
                members[key] = ['not_registered']
                with self.assertRaisesRegex(ValueError, 'not in SEPRESET'):
                    self.check(members)

    def test_registered_preset_without_loaded_bank_is_refused(self):
        members = self.object()
        members['game_sound'] = ['app:/sound/adx/UNRELATED.ACB']
        with self.assertRaisesRegex(ValueError, 'none of'):
            self.check(members)


    def test_mab_event_preset_is_checked(self):
        import edf41_objects
        import edf41mech
        import mab_legacy
        full = sgo.read(edf41_objects.read('OBJECT/VEHICLE406_BEGARUTA.SGO'))[1]
        blob = full['animation_model'][2]
        names = {p['name'] for events in mab_legacy.mab_event_params(blob).values()
                 for kind, _time, p in events if kind == 'se'}
        self.assertTrue(names)
        games = SimpleNamespace(seset={n: 'engine_cue' for n in names}, banks={'TANK.ACB': {'engine_cue'}})
        members = {'xgs_scene_object_class': 'VehicleBegaruta', 'game_sound': ['app:/sound/adx/TANK.ACB'],
                   'animation_model': [0, 0, blob]}
        mo.check_vehicle_sounds(games, sgo.write_depth_first(0x102, members), 'OBJECT/EXAMPLE.SGO')
        members['animation_model'][2] = edf41mech._mab_presets(blob, {sorted(names)[0]: 'bad_mab_preset'})
        with self.assertRaisesRegex(ValueError, 'bad_mab_preset'):
            mo.check_vehicle_sounds(games, sgo.write_depth_first(0x102, members), 'OBJECT/EXAMPLE.SGO')


if __name__ == '__main__':
    unittest.main()
