"""EDF4.1 NPC visual ownership and source-exact weapon frame regressions."""
from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import dsgo
import edf41_objects as eo
import make_edf41_objects as convert
import selftest


class NpcVisualContract(unittest.TestCase):
    def test_shipped_visual_closure(self):
        selftest.edf41_friend_grip_shipped()

    def test_actual_source_frames(self):
        sources = [Path(convert.STEAM) / game / 'Root.cpk' for game in
                   ('Earth Defense Force 4.1', 'EARTH DEFENSE FORCE 6')]
        if not all(path.is_file() for path in sources):
            self.skipTest('actual EDF4.1 source game is unavailable')
        selftest.edf41_friend_grip_matches_weapons()

    def test_manifest_hashes_and_private_identity(self):
        reg = eo.registry()
        private = [r for r in reg['files'] if '/EDF41_NPC/' in r]
        self.assertEqual(len(private), 48)
        self.assertEqual(sum(r.endswith('.SGO') for r in private), 19)
        for rel in private:
            self.assertTrue(rel.startswith('WEAPON/EDF41_NPC/'), rel)
            self.assertEqual(hashlib.sha256(eo.read(rel)).hexdigest(), reg['files'][rel], rel)
        # The same-named original EDF6 soldier, gun and model paths are not
        # exported by this registry, so the original NPCs retain their frames.
        for rel in ('OBJECT/N601_COMMON_RANGER_AF.SGO', 'OBJECT/AIARMYSOLDIER_AF.SGO',
                    'OBJECT/P505_RANGER.SGO', 'OBJECT/EDF6ARMYSOLDIER.CAS',
                    'WEAPON/AISOLDIERRIFLE01.SGO', 'WEAPON/S_ASSAULT_AF14.RAB'):
            self.assertNotIn(rel, reg['files'])

    def test_native_weapon_path_regression_is_rejected(self):
        target = 'OBJECT/EDF41/AIARMYSOLDIER_AF.SGO'
        original_read = eo.read
        doc = dsgo.parse(original_read(target))
        doc.root.get('soldier_load_weapon').items[0] = 'app:/weapon/AiSoldierRifle01.sgo'
        changed = dsgo.write(doc)
        with mock.patch.object(eo, 'read', side_effect=lambda rel, *a: changed if rel == target else original_read(rel, *a)):
            with self.assertRaises(AssertionError):
                selftest.edf41_friend_grip_shipped()

    def test_approximate_bind_rotation_regression_is_rejected(self):
        target = 'OBJECT/EDF41/AIARMYSOLDIER_RL.SGO'
        original_read = eo.read
        doc = dsgo.parse(original_read(target))
        doc.root.get('soldier_weapon_grab_adjust').items[1].items[0] = 84.0
        changed = dsgo.write(doc)
        with mock.patch.object(eo, 'read', side_effect=lambda rel, *a: changed if rel == target else original_read(rel, *a)):
            with self.assertRaises(AssertionError):
                selftest.edf41_friend_grip_shipped()

    def test_invalid_source_names_and_audio_modes_are_refused(self):
        for path in ('OBJECT/P505_RANGER.SGO', 'WEAPON/ICON/AF14.DDS'):
            with self.assertRaises(ValueError):
                convert.friend_weapon_rel(path)

        def weapon(mode, cue):
            root = dsgo.Node([])
            root.set('FireSe', dsgo.Node([mode, cue, 0.8, 1.0, 2.0, 25.0]))
            return dsgo.Document(root, [])

        class Source:
            def __init__(self, mode):
                self.mode = mode

            def read6(self, _path):
                return dsgo.write(weapon(self.mode, 'resident_ai_cue'))

        banks = ({}, {'TIKYUUX_SE.ACB': {'resident_ai_cue'}})
        with mock.patch.object(convert, 'sound_cues', return_value=banks):
            looping = weapon(1.0, 'optional_legacy_cue')
            convert.friend_weapon_cues(Source(1.0), 'WEAPON/AI.SGO', looping)
            self.assertEqual(looping.root.get('FireSe').items,
                             [1.0, 'resident_ai_cue', 0.8, 1.0, 2.0, 25.0])
            with self.assertRaises(ValueError):
                convert.friend_weapon_cues(Source(0.0), 'WEAPON/AI.SGO', weapon(1.0, 'optional_legacy_cue'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
