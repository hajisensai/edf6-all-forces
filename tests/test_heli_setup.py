"""The range's helicopter flight law must come from its playable request, not a template default."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testrange'))
import gen
import dsgo
import sgo
from rootcpk import DEFAULT_GAME, Game


def encode(values, classic):
    if classic:
        return sgo.write(0x102, values)
    def node(v):
        if isinstance(v, dict):
            return dsgo.Node([node(x) for x in v.values()], dict(enumerate(v)))
        return dsgo.Node([node(x) for x in v]) if isinstance(v, list) else v
    return dsgo.write(dsgo.Document(node(values), []))


class HeliSetupTests(unittest.TestCase):
    def test_flight_only_in_both_formats(self):
        name = 'edf6tr_v602_heli_mission'
        motion = [90., .003, 15., .03, 5., 35., .025]
        values = {'mission_setup': [[1., 1.], [80., .0003, 23.5, .0011, 1., 35., .005],
                                    [999900., 1.666], [['app:/weapon/test.sgo', [0., 0.]]]],
                  'heli_movement': [[.99, 70., .95, 1.], [.15, .125, .05]]}
        call = {'Ammo_CustomParameter': [0., 180., 120., 1., ['transport', 'box', 'app:/object/v602_heli.sgo',
                                                          [[5., 5.], motion, [10000., 1.5], []]]]}
        class FakeGame:
            def read(self, folder, file):
                self.last = (folder, file)
                return encode(call, True)
        game = FakeGame()
        for classic in (False, True):
            raw = encode(values, classic)
            before = sgo.load(data=raw)
            got = sgo.load(data=gen._with_player_heli_motion(game, name, raw))
            for a, b in zip(got['mission_setup'][1], motion):
                self.assertAlmostEqual(a, b, places=5)
            got['mission_setup'][1] = before['mission_setup'][1]
            self.assertEqual(got, before)
            self.assertEqual(game.last, ('WEAPON', 'AWEAPON371.SGO'))
            self.assertIs(gen._with_player_heli_motion(game, 'edf6tr_vehicle401_striker_mission', raw), raw)
        call['Ammo_CustomParameter'][4][2] = 'app:/object/another.sgo'
        with self.assertRaisesRegex(ValueError, 'another vehicle'):
            gen._with_player_heli_motion(game, name, raw)

    @unittest.skipUnless((Path(DEFAULT_GAME) / 'Root.cpk').exists(), 'real Root.cpk not available')
    def test_real_requests_through_vehicle_builder(self):
        game = Game(DEFAULT_GAME)
        for name, call_name in gen.PLAYER_CALLS.items():
            if '_heli' not in name:
                continue
            request = sgo.load(data=game.read('WEAPON', call_name + '.SGO'))['Ammo_CustomParameter'][4][3][1]
            vehicle = sgo.load(data=gen.vehicle_sgo(game, name))
            for a, b in zip(vehicle['mission_setup'][1], request):
                self.assertAlmostEqual(a, b, places=5)
            if 'v602' in name:
                k, blend = vehicle['mission_setup'][1][:2]
                damp = vehicle['heli_movement'][0][0]
                top = k * blend / (1. - damp * (1. - blend))
                self.assertAlmostEqual(top * 3.6, 74.94, places=1)
                template = sgo.load(data=game.read('OBJECT', 'V602_HELI.SGO'))
                k0, b0 = template['vehicle_setup'][1][:2]
                self.assertAlmostEqual(k0*b0/(1.-damp*(1.-b0))*3.6, 8.39, places=1)


if __name__ == '__main__':
    unittest.main()
