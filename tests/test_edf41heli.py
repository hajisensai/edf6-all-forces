"""Native-data contracts for the 4.1 Nereid port; skipped without both games.

This checks construction resources and deliberately broken dependencies.  It does
not claim that a game has accepted, flown or destroyed the resulting helicopter.
"""
from __future__ import annotations

import os
import struct
import sys
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(__file__))
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import cas_graft  # noqa: E402
import cas_pose  # noqa: E402
import dsgo  # noqa: E402
import edf41heli as heli  # noqa: E402
import legacy_assets  # noqa: E402
import mab_legacy  # noqa: E402
import make_edf41_objects as objects  # noqa: E402
import mdb  # noqa: E402
import sgo  # noqa: E402

GAME4 = os.environ.get('EDF41_GAME_DIR', r'D:\Steam\steamapps\common\Earth Defense Force 4.1')
GAME6 = os.environ.get('EDF6_GAME_DIR', r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 6')


@unittest.skipUnless(all(os.path.isfile(os.path.join(p, 'Root.cpk')) for p in (GAME4, GAME6)),
                     'requires EDF4.1 and EDF6 native resources')
class Native(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.games = objects.Games(GAME4, GAME6)
        cls.files = heli.build(cls.games, heli.ROOTS[0])
        cls.mine = sgo.read(cls.games.read4('OBJECT/VEHICLE302_HELI.SGO'))[1]
        cls.host = sgo.read(cls.games.read6('OBJECT/V506_HELI.SGO'))[1]
        cls.result = sgo.read(cls.files['OBJECT/VEHICLE302_HELI.SGO'])[1]

    def test_model_cas_ragdoll_stay_in_the_same_rest_pose(self) -> None:
        old_rab = self.games.read4('OBJECT/VEHICLE302_HELI.RAB')
        self.assertEqual(self.files['OBJECT/VEHICLE302_HELI.RAB'],
                         legacy_assets.convert('OBJECT/VEHICLE302_HELI.RAB', old_rab))
        old = mdb.rab_read(old_rab)
        new = mdb.rab_read(self.files['OBJECT/VEHICLE302_HELI.RAB'])
        for a, b in zip(old.files, new.files):
            if a.name.lower().endswith('.dds'):
                self.assertEqual(a.data, b.data)
            if a.name.lower().endswith('.mdb'):
                before, after = mdb.mdb_read(a.data), mdb.mdb_read(b.data)
                self.assertEqual(struct.unpack_from('<I', b.data, 4)[0], 0x20)
                self.assertEqual([m.shader for m in before.materials], [m.shader for m in after.materials])
                self.assertEqual([(before.name_of(x.name), x.local, x.inv_bind) for x in before.bones],
                                 [(after.name_of(x.name), x.local, x.inv_bind) for x in after.bones])
        self.assertEqual(self.files['OBJECT/VEHICLE302_HELI.SHKT'], self.games.read4('OBJECT/VEHICLE302_HELI.SHKT'))
        self.assertEqual(self.result['ragdoll'], self.mine['ragdoll'])
        cas = self.files['OBJECT/VEHICLE302_HELI.CAS']
        self.assertEqual(cas_graft.states(cas), cas_graft.states(self.games.read6('OBJECT/V506_HELI.CAS')))
        pose = cas_pose.CasPose(cas)
        default = next(c for c in pose.clips if c.name == 'default')
        body = next(t for t in default.tracks if t.name == 'body')
        self.assertAlmostEqual(pose.translation(body.translation)[1], 1.484474, places=5)
        self.assertEqual(self.result['xgs_scene_object_class'], 'Vehicle506_Helicopter')
        self.assertEqual(self.result['vehicle_riding_position'][0][3:], self.host['vehicle_riding_position'][0][3:])

    def test_locators_keep_302_frames_and_have_the_506_names(self) -> None:
        source = mab_legacy.mab_locators(self.mine['animation_model'][2])
        actual = mab_legacy.mab_locators(self.result['animation_model'][2])
        expected_names = {loc.name for _group, loc in mab_legacy.mab_locators(self.host['animation_model'][2])}
        self.assertEqual({loc.name for _group, loc in actual}, expected_names)
        self.assertEqual([(g, loc.node, loc.kind, loc.a, loc.b, loc.c) for g, loc in actual],
                         [(g, loc.node, loc.kind, loc.a, loc.b, loc.c) for g, loc in source])
        self.assertEqual(len(self.result['vehicle_weapon_setting']), 4)
        self.assertEqual(len(self.result['vehicle_setup'][3]), 4)

    def test_all_call_variants_have_302_muzzles(self) -> None:
        for source, names in heli.WEAPONS.items():
            old = sgo.read(self.games.read4('WEAPON/' + source.upper()))[1]
            expected = mab_legacy.mab_locators(old['animation_model'][2])
            for name in names:
                weapon = dsgo.parse(self.files['WEAPON/' + name.upper() + '.SGO'])
                self.assertEqual(mab_legacy.mab_locators(weapon.root.get('animation_model').items[2].data), expected)
        guns = self.result['vehicle_weapon_setting'][:2]
        self.assertEqual([row[0] for row in guns], ['gatling_l', 'gatling_r'])
        # In 4.1 the gatling muzzle is zero in the gun bone's frame; retaining
        # 506's body-local +/-2.5 m as well would offset it a second time.
        weapon = dsgo.parse(self.files['WEAPON/EDF41_V_302HELI_GATLING01_L.SGO'])
        muzzle = mab_legacy.mab_locators(weapon.root.get('animation_model').items[2].data)[0][1]
        self.assertEqual(muzzle.a, (0.0, 0.0, 0.0, 1.0))

    def test_missing_ragdoll_bone_is_rejected(self) -> None:
        rab = mdb.rab_read(self.games.read4('OBJECT/VEHICLE302_HELI.RAB'))
        member = next(f for f in rab.files if f.name.lower() == 'vehicle302_heli.mdb')
        md = mdb.mdb_read(member.data)
        md.names[md.bones[mdb.bone_named(md, 'bend_roterB')].name] = 'broken_rotor'
        member.stored = mdb.mdb_write(md)
        bad = mdb.rab_write(rab)
        old_read = self.games.read4
        with mock.patch.object(self.games, 'read4', side_effect=lambda p: bad if p == 'OBJECT/VEHICLE302_HELI.RAB'
                               else old_read(p)):
            with self.assertRaisesRegex(ValueError, 'ragdoll names missing model bones'):
                heli.build(self.games, heli.ROOTS[0])

    def test_missing_helicopter_control_is_rejected(self) -> None:
        cas = self.files['OBJECT/VEHICLE302_HELI.CAS']
        self.assertIn('roter_speed', cas_graft.variable_names(cas))
        bad = cas.replace('roter_speed'.encode('utf-16le'), 'gone__speed'.encode('utf-16le'))
        with mock.patch.object(heli.cas_legacy, 'cas_from_legacy', return_value=bad):
            with self.assertRaisesRegex(ValueError, 'control variable'):
                heli.build(self.games, heli.ROOTS[0])


if __name__ == '__main__':
    unittest.main()
