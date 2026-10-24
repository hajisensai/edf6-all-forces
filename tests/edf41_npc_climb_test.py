"""EDF4.1 soldiers never enter a climb their CAS cannot play (2026-10-19: 「NPC有的会立正」).

EDF6's climb state (EDF.dll 0x5790A0) plays one of make_edf41_objects.SOLDIER_CLIMB_STATES and leaves only when the main player's
state is done; 4.1's CAS has none of them, so the play is refused, the walk keeps looping and the soldier stays frozen.
tools/make_edf41_objects.py drops human_climb_settings from such a soldier (drop_climb); this holds the shipped files to
that and checks the pieces the build uses.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import dsgo
import edf41_objects as eo
import make_edf41_objects as convert
import sgo
import selftest

TARGET = 'OBJECT/EDF41/AIARMYSOLDIER_SN.SGO'   # the sniper of the report's screenshot


class NpcClimbContract(unittest.TestCase):
    def test_shipped_soldiers_never_climb_without_the_states(self):
        selftest.edf41_friend_climb_shipped()

    def test_climb_key_back_on_a_4_1_soldier_is_rejected(self):
        original_read = eo.read
        doc = dsgo.parse(original_read(TARGET))
        doc.root.set(convert.SOLDIER_CLIMB_KEY, dsgo.Node([1.5], {0: 'step_over_distance'}))
        changed = dsgo.write(doc)
        with mock.patch.object(eo, 'read', side_effect=lambda rel, *a: changed if rel == TARGET else original_read(rel, *a)):
            with self.assertRaises(AssertionError):
                selftest.edf41_friend_climb_shipped()

    def test_without_key_keeps_every_other_member(self):
        data = eo.read(TARGET)
        doc = dsgo.parse(data)
        doc.root.set(convert.SOLDIER_CLIMB_KEY, dsgo.Node([3.0], {0: 'max_step_height'}))
        with_key = dsgo.write(doc)
        before, after = sgo.load(data=with_key), sgo.load(data=convert.without_key(with_key, convert.SOLDIER_CLIMB_KEY))
        self.assertIn(convert.SOLDIER_CLIMB_KEY, before)
        self.assertNotIn(convert.SOLDIER_CLIMB_KEY, after)
        self.assertEqual({k: v for k, v in before.items() if k != convert.SOLDIER_CLIMB_KEY}, after)

    def test_4_1_cas_lacks_every_climb_state(self):
        for cas_rel in convert.FRIEND_CAS:
            self.assertEqual(convert.climb_states_missing(eo.read(cas_rel)), list(convert.SOLDIER_CLIMB_STATES), cas_rel)
        self.assertEqual(convert.climb_states_missing(None), list(convert.SOLDIER_CLIMB_STATES))

    def test_soldier_classes_may_leave_the_climb_out(self):
        for cls in ('AssultSoldier', 'HeavyArmor', 'PaleWing', 'Engineer'):
            self.assertIn(convert.SOLDIER_CLIMB_KEY, convert.NULL_CHECKED_KEYS[cls])
            spec = convert.CLASS_NAMES.get(cls)
            if spec is not None:
                self.assertIn((convert.SOLDIER_CLIMB_KEY, convert.SOLDIER_CLIMB_STATES), spec.when_key)


if __name__ == '__main__':
    unittest.main()
