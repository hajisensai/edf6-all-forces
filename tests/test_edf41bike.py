"""Real V408 data contracts and negative controls; no game is started or written."""
from __future__ import annotations

import copy
import math
import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(__file__))
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import cas_graft  # noqa: E402
import dsgo  # noqa: E402
import edf41bike as bike  # noqa: E402
import hk2014  # noqa: E402
import legacy_sgo  # noqa: E402
import mab_legacy  # noqa: E402
import make_edf41_objects as objects  # noqa: E402
import mdb  # noqa: E402
import outfit41  # noqa: E402
import sgo  # noqa: E402

GAME4 = os.environ.get('EDF41_GAME_DIR', r'D:\Steam\steamapps\common\Earth Defense Force 4.1')
GAME6 = os.environ.get('EDF6_GAME_DIR', r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 6')


def locator_params(block: bytes, name: str) -> dict:
    _layout, records = mab_legacy._records(block)
    for group, at in records:
        n = mab_legacy._text_at(block, at + struct.unpack_from('<i', block, at)[0])
        if group == 0 and n == name:
            param = at + struct.unpack_from('<i', block, at + 0x1C)[0]
            return sgo.read(block[param:])[1]
    raise ValueError(f'no locator {name}')


SHIPPED = os.path.join(ROOT, 'edf41port', 'objects')


class Shipped(unittest.TestCase):
    """What the installer ships (edf41port/objects), checked without any game: the contracts the generator's check()
    holds, on the committed bytes. The 2026-10-21 fixes lived only in the generator; the shipped SGO kept the
    keyframed gun (row 1) that pins the chassis, so W only crept the bike along and round to the left (2026-10-25
    feedback 「按W键不会前进 只是缓缓向左」), plus V512's fall-out seats past the 408's two."""

    @classmethod
    def setUpClass(cls) -> None:
        with open(os.path.join(SHIPPED, 'OBJECT', 'VEHICLE408_MOTORBIKE.SGO'), 'rb') as f:
            cls.members = sgo.read(f.read())[1]
        with open(os.path.join(SHIPPED, 'OBJECT', 'VEHICLE408_MOTORBIKE.SHKT'), 'rb') as f:
            cls.physics = f.read()
        with open(os.path.join(SHIPPED, 'OBJECT', 'EDF41_RANGE_VEHICLE408_MOTORBIKE.SGO'), 'rb') as f:
            cls.spawn = sgo.read(f.read())[1]

    def test_no_keyframed_proxy_pins_the_chassis(self) -> None:
        for members in (self.members, self.spawn):
            rows = sgo.plain(members['car_base_simulation_node'])
            self.assertEqual([r for r in rows if r[0] == 'gun'], [['gun', 0]])
            self.assertEqual(bike.pinned({'car_base_simulation_node': rows}, self.physics), [])
            self.assertNotIn('gun', {r[0] for r in sgo.plain(members['car_base_rigid_body'])})

    def test_fall_out_seats_exist(self) -> None:
        for members in (self.members, self.spawn):
            seats = len(members['vehicle_riding_position'])
            self.assertEqual(sgo.plain(members['car_base_fall_out'][4]), list(range(1, seats)))

    def test_rear_wheel_drive_front_steering_free_sidecar_wheel(self) -> None:
        for members in (self.members, self.spawn):
            rows = sgo.plain(members['car_base_wheel'])
            self.assertEqual({r[0]: r[10:13] for r in rows},
                             {'front_tire': [0, 1, 1], 'rear_tire': [1, -1, 0], 'side_tire': [0, -1, 0]})

    def test_driver_rides_the_bike_pose(self) -> None:
        for members in (self.members, self.spawn):
            block = members['animation_model'][2]
            self.assertEqual([locator_params(block, n)['Animation'] for n in ('座席１', '座席２')],
                             list(bike.SEAT_STATES))
            self.assertEqual([r[4] for r in sgo.plain(members['vehicle_riding_position'])], [9, 15])

    def test_call_drive_block(self) -> None:
        for members in (self.members, self.spawn):
            for key in ('vehicle_setup', 'mission_setup'):
                self.assertEqual([round(float(x), 6) for x in sgo.plain(members[key][1])], bike.CAR_DRIVE)


@unittest.skipUnless(all(os.path.isfile(os.path.join(p, 'Root.cpk')) for p in (GAME4, GAME6)),
                     'requires EDF4.1 and EDF6 native resources')
class Native(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.games = objects.Games(GAME4, GAME6)
        cls.files = bike.build(cls.games, bike.ROOTS[0])
        cls.source = sgo.read(legacy_sgo.convert(cls.games.read4('OBJECT/' + bike.ROOTS[0])))[1]
        cls.members = sgo.read(cls.files['OBJECT/' + bike.ROOTS[0]])[1]
        cls.physics = cls.files['OBJECT/VEHICLE408_MOTORBIKE.SHKT']
        cls.md = bike.model(cls.files['OBJECT/VEHICLE408_MOTORBIKE.RAB'], 'Vehicle408_motorbike.mdb')

    def test_model_and_all_eight_physics_parts_stay_matched(self) -> None:
        self.assertEqual(self.physics, self.games.read4('OBJECT/VEHICLE408_MOTORBIKE.SHKT'))
        self.assertEqual(self.members['ragdoll'], self.source['ragdoll'])
        self.assertEqual(self.md.version, 0x20)
        source = bike.model(self.games.read4('OBJECT/VEHICLE408_MOTORBIKE.RAB'), 'Vehicle408_motorbike.mdb')
        old = {source.name_of(b.name): (b.local, b.inv_bind) for b in source.bones}
        new = {self.md.name_of(b.name): (b.local, b.inv_bind) for b in self.md.bones}
        self.assertEqual({k: new[k] for k in old}, old)
        self.assertEqual(set(new) - set(old), {bike.MARKER})
        bodies = {o['name'] for o in hk2014.read(self.physics).objects if o.type.name == 'hkpRigidBody'}
        self.assertEqual(bodies, {'RagDollProxys.' + n for n in bike.PROXIES})
        bike.check(self.members, self.md, self.physics)
        self.assertEqual(self.members['car_aim_node'], [])
        self.assertNotIn('gun', {r[0] for r in self.members['car_base_rigid_body']})
        self.assertEqual([r for r in self.members['car_base_simulation_node'] if r[0] == 'gun'], [['gun', 0]])

    def test_no_keyframed_proxy_holds_the_chassis(self) -> None:
        # The SHKT's seven constraints, (child, parent): the gun's ragdoll joint is to the body itself.
        self.assertEqual(bike.joints(self.physics), [
            ('side_tire', 'body'), ('rear_tire', 'rear_sus'), ('rear_sus', 'body'), ('front_tire', 'front_sus'),
            ('front_sus', 'handle'), ('handle', 'body'), ('gun', 'body')])
        self.assertEqual(bike.pinned(self.members, self.physics), [])
        # The 2026-10-19 data (gun row 1, mode 2 keyframed) kept that joint between a keyframed gun and the dynamic
        # body: the hover (2026-10-21 run). A wheel made keyframed the same way would hold it too.
        old = copy.deepcopy(self.members)
        old['car_base_simulation_node'] = [[n, 0] for n in bike.WHEELS] + [['gun', 1]]
        self.assertEqual(bike.pinned(old, self.physics), [('gun', 'body')])
        with self.assertRaisesRegex(ValueError, 'gun needs its one animation-driven'):
            bike.check(old, self.md, self.physics)
        sus = copy.deepcopy(self.members)
        sus['car_base_simulation_node'].append(['rear_sus', 1])
        self.assertEqual(bike.pinned(sus, self.physics), [('rear_sus', 'body')])
        with self.assertRaisesRegex(ValueError, 'pin the dynamic chassis'):
            bike.check(sus, self.md, self.physics)
        # A row 0 takes the whole subtree out: front_sus with front_tire, no joint to a dynamic part left behind.
        sub = copy.deepcopy(self.members)
        sub['car_base_simulation_node'].append(['handle', 0])
        self.assertEqual(bike.pinned(sub, self.physics), [])

    def test_fall_out_names_only_the_408s_own_seats(self) -> None:
        self.assertEqual(sgo.plain(self.members['car_base_fall_out'][4]), [1])
        template = sgo.read(self.games.read6(bike.CAR_TEMPLATE))[1]
        self.assertEqual(sgo.plain(template['car_base_fall_out'][4]), [1, 2, 3, 4])   # V512's bed, five seats
        self.assertEqual(sgo.plain(self.members['car_base_fall_out'][:4]), sgo.plain(template['car_base_fall_out'][:4]))
        bad = copy.deepcopy(self.members)
        bad['car_base_fall_out'][4] = [1, 2, 3, 4]
        with self.assertRaisesRegex(ValueError, 'fall_out names seats'):
            bike.check(bad, self.md, self.physics)

    def test_three_physical_wheels_and_original_radii(self) -> None:
        rows = self.members['car_base_wheel']
        self.assertEqual([r[0] for r in rows], list(bike.WHEELS))
        self.assertEqual([r[8] for r in rows], [r[5] for r in self.source['vehicle_tire']])
        # Driven / steered as 4.1's own rows say (bike.wheel_flags): rear-wheel drive, front steering, the sidecar
        # wheel free. The flags were by position before: the sidecar wheel was driven too.
        self.assertEqual([r[10:13] for r in rows], [bike.wheel_flags(t) for t in self.source['vehicle_tire']])
        self.assertEqual([r[10:13] for r in rows], [[0, 1, 1], [1, -1, 0], [0, -1, 0]])
        with self.assertRaisesRegex(ValueError, 'steer / drive'):
            bike.wheel_flags(self.source['vehicle_tire'][0][:9])
        self.assertEqual([r[1][-1] for r in self.members['car_base_draw_wheel_info']], [1, 0, 0])
        self.assertEqual({r[0] for r in self.members['car_base_breaking_parts'][1]}, bike.PROXIES - {'body'})

    def test_two_real_seats_keep_the_original_doors(self) -> None:
        original = {x.name: x for _, x in mab_legacy.mab_locators(self.source['animation_model'][2])}
        actual = {x.name: x for _, x in mab_legacy.mab_locators(self.members['animation_model'][2])}
        for name in ('搭乗口１', '搭乗口２', 'カメラ１', 'カメラ２'):
            self.assertEqual(actual[name], original[name])
        for name, state in zip(('座席１', '座席２'), bike.SEAT_STATES):
            params = locator_params(self.members['animation_model'][2], name)
            self.assertEqual(params, {'Animation': state, 'DispCharacter': 1})
        # The driver rides EDF6's own motorbike pose, the gunner the bench; the masks are 4.1's (driver R + AR).
        self.assertEqual(bike.SEAT_STATES, ('vehicle503_bike_driver', 'vehicle_striker_rear'))
        self.assertEqual([r[4] for r in self.members['vehicle_riding_position']],
                         [r[4] for r in self.source['vehicle_riding_position']])
        self.assertEqual([r[4] for r in self.members['vehicle_riding_position']], [9, 15])
        self.assertEqual(len(self.members['vehicle_riding_position']), 2)
        self.assertEqual(self.members['vehicle_weapon_setting'], [['gun', 1]])

    def test_single_source_machinegun_and_real_muzzle(self) -> None:
        raw = self.games.read4('WEAPON/V_408MOTORBIKE_GUN.SGO')
        self.assertEqual(raw[:4], b'\0OGS')
        source = sgo.read(raw)[1]
        weapon = dsgo.parse(self.files['WEAPON/EDF41_V_408MOTORBIKE_GUN.SGO'])
        self.assertEqual(mab_legacy.mab_locators(weapon.root.get('animation_model').items[2].data),
                         mab_legacy.mab_locators(source['animation_model'][2]))
        self.assertEqual(weapon.root.get('AmmoCount'), source['AmmoCount'])
        self.assertEqual(weapon.root.get('FireInterval'), source['FireInterval'])
        self.assertEqual(len(self.members['vehicle_setup'][2]), 1)
        self.assertEqual(weapon.root.get('FireRecoil'), source['FireRecoil'].value)
        for key in ('vehicle_setup', 'mission_setup'):
            self.assertEqual(sgo.plain(self.members[key][2][0][1]), [0.0, 0.0])

    def test_seat_pelvis_fit_and_posed_mesh_bounds(self) -> None:
        masks = (9, 15)
        offsets, rows = bike.pose_fit(self.games, masks)
        self.assertEqual(len(rows), 4)
        fitted = [[row[0] for row in rows if row[3][seat] is not None] for seat in range(2)]
        # The bike pose for the two classes the driver's seat admits (the stock bikes' pair); the bench for all four.
        self.assertEqual(fitted, [['ARMYSOLDIER', 'ENGINEER'], ['ARMYSOLDIER', 'ENGINEER', 'PALEWING', 'HEAVYARMOR']])
        for (source_class, native_cas, native_model, _bit), row in zip(bike.RIDER_CLASSES, rows):
            self.assertEqual(source_class, row[0])
            cas = self.games.read6(f'OBJECT/{native_cas}.CAS')
            obj = sgo.load(data=self.games.read6(f'OBJECT/{native_model}.SGO'))
            entry = obj['animation_model'][0]
            md = bike.model(self.games.read6(bike.rel(entry[0])), entry[1])
            for seat, state in enumerate(bike.SEAT_STATES):
                if row[3][seat] is None:
                    continue
                clip = cas_graft.state_clips(cas)['main'][state][0]
                positions = outfit41.skinned_positions(md, outfit41.clip_locals(cas, clip, 0, md))
                moved = positions + offsets[seat]
                lo, hi = moved.min(axis=0), moved.max(axis=0)
                self.assertTrue(all(math.isfinite(float(v)) for v in (*lo, *hi)))
                self.assertTrue(all(0.15 < float(v) < 5.0 for v in hi - lo))
                self.assertLess(row[3][seat], 0.08)
                # Open motorcycle seats are above the wheel contact plane;
                # a driver-pose fallback at the unshifted origin fails this.
                self.assertGreater(float(hi[1]), 1.0)
        # A seat that admits a class whose CAS lacks the seat's pose is refused (the driver's opened to Wing Divers).
        with self.assertRaisesRegex(ValueError, 'lacks vehicle503_bike_driver'):
            bike.pose_fit(self.games, (9 | 2, 15))

    def test_shipped_object_is_this_generator_output(self) -> None:
        # The installer ships edf41port/objects, not a fresh build: the 2026-10-21 fixes (gun row 0, fall-out [1])
        # were in the generator but never regenerated into the shipped SGO, which kept the hovering, pinned 408.
        for rel in ('OBJECT/VEHICLE408_MOTORBIKE.SGO', 'OBJECT/VEHICLE408_MOTORBIKE.SHKT'):
            with open(os.path.join(SHIPPED, *rel.split('/')), 'rb') as f:
                self.assertEqual(f.read(), self.files[rel], f'{rel} is stale: run tools/make_edf41_objects.py')
        with open(os.path.join(SHIPPED, 'OBJECT', 'EDF41_RANGE_VEHICLE408_MOTORBIKE.SGO'), 'rb') as f:
            spawn = sgo.read(f.read())[1]
        self.assertEqual({k: v for k, v in spawn.items() if k != 'mission_setup'},
                         {k: v for k, v in self.members.items() if k != 'mission_setup'})

    def test_dynamic_gun_and_missing_body_fail_closed(self) -> None:
        members = copy.deepcopy(self.members)
        members['car_base_rigid_body'].append(['gun', -1.0, -1.0, -1.0, -1.0, [], []])
        with self.assertRaisesRegex(ValueError, 'animation-driven'):
            bike.check(members, self.md, self.physics)
        for rows in ([[n, 0] for n in bike.WHEELS], [[n, 0] for n in bike.WHEELS] + [['gun', 1]],
                     [[n, 0] for n in bike.WHEELS] + [['gun', 0], ['gun', 0]]):
            bad = copy.deepcopy(self.members)
            bad['car_base_simulation_node'] = rows
            with self.assertRaisesRegex(ValueError, 'gun needs its one animation-driven'):
                bike.check(bad, self.md, self.physics)
        bad = copy.deepcopy(self.members)
        bad['mission_setup'][2][0][1] = ['AimRecoil', [0.1, 0.2]]
        with self.assertRaisesRegex(ValueError, 'native BodyRecoil'):
            bike.check(bad, self.md, self.physics)
        md = copy.deepcopy(self.md)
        md.names[md.bones[mdb.bone_named(md, 'side_tire')].name] = 'missing_side'
        with self.assertRaisesRegex(ValueError, 'unresolved bone / body'):
            bike.check(self.members, md, self.physics)

    def test_seat_parameter_injection_rejects_wrong_layout(self) -> None:
        with self.assertRaisesRegex(ValueError, 'first be converted'):
            original = sgo.read(self.games.read4('OBJECT/' + bike.ROOTS[0]))[1]
            bike.seat_mab(original['animation_model'][2], [[0, 0, 0], [0, 0, 0]])
        broken = bytearray(self.members['animation_model'][2])
        struct.pack_into('<I', broken, 0x20, 0x7FFFFFFF)
        with self.assertRaises(ValueError):
            bike.seat_mab(bytes(broken), [[0, 0, 0], [0, 0, 0]])


if __name__ == '__main__':
    unittest.main()
