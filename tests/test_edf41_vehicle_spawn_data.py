"""Data generation for M placement must preserve the separate call-in prototype."""
import hashlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import edf41_objects
import make_edf41_objects as mo
import sgo


class PrivateData(unittest.TestCase):
    def generate(self, *, dsgo=False, mission=None, ai=False):
        members = {'xgs_scene_object_class': 'Vehicle505_Tank', 'game_object_durability': 1000.0,
                   'vehicle_setup': [[1.0, 1.0], [0.1, 2.0], [['app:/weapon/test.sgo', [0.2, 3.0]]]],
                   'unrelated': ['original', 13.0]}
        if mission is not None:
            members['mission_setup'] = mission
        raw = sgo.write_depth_first(0x102, members)
        if dsgo:
            raw = mo.as_dsgo(raw)
        source = 'OBJECT/EXAMPLE' + ('_AI' if ai else '') + '.SGO'
        obj = {'object': source, 'class': 'Vehicle505_Tank', 'files': [source]}
        files = {source: raw}
        mo.vehicle_spawn_objects([obj], files)
        self.assertEqual(files[source], raw)
        return obj, files, members

    def test_classic_call_in_stays_unchanged(self):
        obj, files, original = self.generate()
        spawn = sgo.load(data=files[obj['spawn_object']])
        self.assertEqual(spawn['mission_setup'], sgo.plain(original['vehicle_setup']))
        self.assertEqual(spawn['vehicle_setup'], spawn['mission_setup'])
        self.assertEqual(spawn['game_object_durability'], 1000.0)
        self.assertEqual(spawn['unrelated'], ['original', 13.0])
        self.assertIn(obj['spawn_object'], obj['files'])

    def test_dsgo_empty_mission_is_completed_only_in_private_copy(self):
        obj, files, original = self.generate(dsgo=True, mission=[[1.0, 1.0], [], []])
        self.assertEqual(files[obj['spawn_object']][:4], b'DSGO')
        spawn = sgo.load(data=files[obj['spawn_object']])
        source = sgo.load(data=files[obj['object']])
        self.assertEqual(source['mission_setup'], [[1.0, 1.0], [], []])
        self.assertEqual(spawn['mission_setup'], source['vehicle_setup'])

    def test_authored_ai_mission_remains_independent(self):
        obj, files, _ = self.generate(mission=[[7.0, 8.0], [], []], ai=True)
        self.assertNotIn('spawn_object', obj)
        self.assertEqual(len(files), 1)

    def test_missing_default_setup_is_rejected(self):
        source = 'OBJECT/EXAMPLE.SGO'
        files = {source: sgo.write_depth_first(0x102, {'xgs_scene_object_class': 'Vehicle505_Tank'})}
        with self.assertRaisesRegex(ValueError, 'no final vehicle_setup'):
            mo.vehicle_spawn_objects([{'object': source, 'class': 'Vehicle505_Tank', 'files': [source]}], files)


class NativeClosure(unittest.TestCase):
    def test_private_spawn_references_resolve(self):
        edf41 = Path(mo.STEAM) / 'Earth Defense Force 4.1'
        edf6 = Path(mo.STEAM) / 'EARTH DEFENSE FORCE 6'
        if not (edf41 / 'Root.cpk').is_file() or not (edf6 / 'Root.cpk').is_file():
            self.skipTest('source games are not installed')
        games = mo.Games(str(edf41), str(edf6))
        reg = edf41_objects.registry()
        checked = 0
        for obj in reg['objects']:
            private = obj.get('spawn_object')
            if not private:
                continue
            with self.subTest(private=private):
                raw = edf41_objects.read(private)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), reg['files'][private])
                members = sgo.load(data=raw)
                self.assertEqual(members['mission_setup'], members['vehicle_setup'])
                for ref in mo.strings(members):
                    if not mo.is_ref(ref):
                        continue
                    path = mo.ref_rel(ref)
                    if not Path(path).suffix or path in reg['files'] or path in games.have6:
                        continue
                    if path.endswith('.ACB') and games.sound(path):
                        continue
                    if path.endswith(('_LIGHT.RAB', '_LIGHT.MRAB')) and not games.has4(path):
                        continue  # source virtual light models are optional, not imported assets
                    self.fail(f'unresolved private spawn dependency: {path}')
            checked += 1
        self.assertGreaterEqual(checked, 12)


if __name__ == '__main__':
    unittest.main()
