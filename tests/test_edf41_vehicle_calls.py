"""The original vehicle's call setup must survive different EDF6 donor layouts."""
import copy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import dsgo
import edf41calls as ec
import edf5port


def values(value):
    return [values(x) for x in value.items] if isinstance(value, dsgo.Node) else value


def template(settings):
    root = dsgo.Node([], {})
    root.set('Ammo_CustomParameter', edf5port.to_value([0.05, 180, 120, 1,
             ['app:/Object/v508_transport.sgo', 'app:/Object/v509_transportbox.sgo', 'old.sgo', settings], []]))
    return root


class VehicleCalls(unittest.TestCase):
    def test_302_adds_fuel_without_losing_flight_or_muzzle(self):
        source = [[1.3, 1.3], [80, .0003, 15, .003, 1.25, 35, .005],
                  [['app:/weapon/v_302heli_lasercannon01.sgo', [12, 13]],
                   ['app:/weapon/v_302heli_napalm01.sgo', [14, 15]]]]
        donor = [[1, 1], [80, .0003, 15, .003, 2.5, 35, .005], [20000, 1.5],
                 [['stock_l', [0, 0]], ['stock_r', [0, 0]], ['stock_napalm', [0, 0]],
                  ['app:/weapon/v_fuel01.sgo']]]
        members = {'Ammo_CustomParameter': [0, 0, 0, 0, ['a', 'b', 'c', source]]}
        original = copy.deepcopy(donor)
        converted = ec.vehicle_call(members, template(donor), 'vehicle302_heli.sgo').root
        setup = values(converted.get('Ammo_CustomParameter').items[4].items[3])
        self.assertEqual(setup[:2], source[:2])
        self.assertEqual(setup[2], donor[2])
        self.assertEqual(setup[3][-1], donor[3][-1])
        self.assertEqual([r[0] for r in setup[3][:-1]], [
            'app:/weapon/edf41_v_302heli_lasercannon01_l.sgo',
            'app:/weapon/edf41_v_302heli_lasercannon01_r.sgo',
            'app:/weapon/edf41_v_302heli_napalm01.sgo'])
        self.assertEqual([r[1] for r in setup[3][:-1]], [[12, 13], [12, 13], [14, 15]])
        self.assertEqual(donor, original)

    def test_406_six_slots_are_kept_with_all_original_tuning(self):
        weapons = [[f'app:/weapon/v_406begaruta_slot{i}.sgo', [i, i + 1]] for i in range(6)]
        source = [[1, 1], [2, 3, 4, 5], [8] * 6, [7], weapons, [9, 10]]
        donor = [[1, 1], [2, 3, 4, 5], [8] * 6, [7], [['v504', [0, 0]]] * 8, [9, 10]]
        members = {'Ammo_CustomParameter': [0, 0, 0, 0, ['a', 'b', 'c', source]]}
        converted = ec.vehicle_call(members, template(donor), 'vehicle406_begaruta_gold.sgo').root
        setup = values(converted.get('Ammo_CustomParameter').items[4].items[3])
        self.assertEqual(setup, source)
        self.assertEqual(len(setup[4]), 6)

    def test_408_keeps_one_gunner_gun_instead_of_503_two_guns_and_fuel(self):
        source = [[1.7, 1.7], [0.1, 10, 0.02, 1, 0.2, 0.8],
                  [['app:/weapon/v_408motorbike_gun.sgo', ['AimRecoil', [.002618, .0062832]], [90, .015, .1]]]]
        donor = [[1, 1], [0.1] * 6, [4000, 1],
                 [['stock_l', [0, 0]], ['stock_r', [0, 0]], ['app:/weapon/v_fuel01.sgo']]]
        members = {'Ammo_CustomParameter': [0, 0, 0, 0, ['a', 'b', 'c', source]]}
        converted = ec.vehicle_call(members, template(donor), 'vehicle408_motorbike.sgo').root
        setup = values(converted.get('Ammo_CustomParameter').items[4].items[3])
        self.assertEqual(setup[:2], source[:2])
        self.assertEqual(len(setup), 3)
        self.assertEqual(setup[2], [['app:/weapon/edf41_v_408motorbike_gun.sgo', [0.0, 0.0], [90, .015, .1]]])

    def test_bundled_call_snapshot_matches_actual_ported_vehicle(self):
        import json
        import edf41tank
        import edf41heli
        import edf41mech
        import edf41bike
        roots = set(edf41tank.ROOTS + edf41heli.ROOTS + edf41mech.ROOTS + edf41bike.ROOTS)
        rows = json.loads((ROOT / 'edf41port/calls.json').read_text(encoding='utf8'))['weapons']
        checked = 0
        for row in rows:
            call = row.get('call', {})
            if call.get('kind') != 'vehicle' or call['vehicle'].upper() not in roots:
                continue
            with self.subTest(id=row['id']):
                drop = dsgo.load(row['weapon']).get('Ammo_CustomParameter').items[4]
                self.assertEqual(drop.items[2].lower(), 'app:/object/' + call['vehicle'])
                setup = values(drop.items[3])
                if call['vehicle'] == 'vehicle302_heli.sgo':
                    self.assertEqual(len(setup), 4)
                    self.assertTrue(all(gun[0].startswith('app:/weapon/edf41_v_302heli_') for gun in setup[3][:-1]))
                if call['vehicle'] == 'vehicle408_motorbike.sgo':
                    self.assertEqual(len(setup), 3)
                    self.assertEqual(len(setup[2]), 1)
                    self.assertEqual(setup[2][0][0], 'app:/weapon/edf41_v_408motorbike_gun.sgo')
                    self.assertEqual(setup[2][0][1], [0.0, 0.0])
                if call['vehicle'].startswith('vehicle406_begaruta'):
                    self.assertEqual(len(setup[4]), 6)
                    self.assertTrue(all('v_406begaruta_' in gun[0] for gun in setup[4] if isinstance(gun[0], str)))
            checked += 1
        self.assertEqual(checked, 45)

    def test_406_unknown_holder_layout_is_refused(self):
        source = [[1, 1], [], [], [], [], []]
        members = {'Ammo_CustomParameter': [0, 0, 0, 0, ['a', 'b', 'c', source]]}
        with self.assertRaisesRegex(edf5port.Unsupported, 'six-holder'):
            ec.vehicle_call(members, template([]), 'vehicle406_begaruta.sgo')


if __name__ == '__main__':
    unittest.main()
