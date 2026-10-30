"""Offline coverage for the planner's 4.1 catalog and script lookup."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import make_level_list as levels
import make_mission_digest as digests


class LevelListTest(unittest.TestCase):
    def test_committed_catalog_matches_pack_sources(self):
        catalog = json.loads((ROOT / 'testhub/src/missions.json').read_text(encoding='utf-8'))
        self.assertEqual(levels.edf41_rows(), [m for m in catalog if m.get('pack_n')])
        self.assertEqual(len(levels.edf41_rows()), 138)

    def test_build_preserves_existing_numbers_and_adds_packs(self):
        six = [{'key': 'EDF6/M001', 'n': 1}]
        five = [{'key': 'EDF5/M001', 'n': 2}]
        with patch.object(levels, 'edf6_rows', return_value=six), \
                patch.object(levels, 'edf5_rows', return_value=five) as edf5_rows, \
                patch.object(levels.rootcpk, 'Game'):
            self.assertEqual(levels.build('unused'), levels.edf41_rows() + five + six)
            edf5_rows.assert_called_once_with('unused', 2)

    def test_each_pack_digest_uses_its_own_script(self):
        game = Mock()
        for p41 in levels.edf5.PACKS41:
            m = levels.edf5.edf41_text(p41.data)['missions'][0]
            expected = levels.edf5.edf41_script(m['id'], 'MISSION.BVM', p41.data)
            self.assertEqual(digests.script(game, f'{p41.folder}/{m["id"]}'), expected)
        game.read.assert_not_called()


if __name__ == '__main__':
    unittest.main()
