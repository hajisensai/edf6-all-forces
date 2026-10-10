"""The Proteus shield's SGO (tools/make_proteus.py) and its contract with src/proteus_shield.inc.

python tools/test_proteus_assets.py         offline fixtures; with the game's Root.cpk also the real build (read only)
"""
from __future__ import annotations

import math
import re
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import make_proteus as mp
import rootcpk
import sgo


def stock_round() -> bytes:
    """A DEMOGUNSHIPFIRESOLID-shaped DemoIndirectFire (19 indirect_fire_param entries)."""
    return sgo.write(0x102, {
        'xgs_scene_object_class': 'DemoIndirectFire', 'indirect_fire_damage': 2000.0,
        'indirect_fire_param': [[1.2, 0.0], [800.0, 0.0], 1, 0, 'SolidBullet01', 20.0, 0.0, 10.0, 2.0, 0.0, 600, 1,
                                [0.4, 0.4, 6.0, 1.0], [], 0, 60, 0, [0, 'shot', 0.5, 1.0, 1.0, 500.0], [0, 'hit', 0.9, 1.0, 3.0, 200.0]]})


DEVICE = [['app:/Weapon/e_support_barrier01.rab', 'e_support_barrier01.mdb'], 'app:/Weapon/e_support_barrier01.cas',
          b'MAB\x00' + bytes(range(28))]


def stock_barrier(model: bool = True) -> bytes:
    """An EWEAPON196-shaped electromagnetic barrier (`model`: with its animation_model, the device)."""
    m = {'AmmoClass': 'BarrierBullet01', 'AmmoColor': [0.1, 0.1, 1.0, 0.25],
         'Ammo_CustomParameter': [2.4, 2.5, 3.0, [1.0, 1.0, 0.5], [0.0, 0.0, 1.0]],
         'FireSe': [0.0, 'weapon_Engineer_EX_spcBarrier01', 1.0, 1.0, 2.0, 30.0]}
    if model:
        m['animation_model'] = DEVICE
    return sgo.write(0x102, m)


class FakeGame:
    def __init__(self, files: dict[tuple[str, str], bytes]) -> None:
        self.files = files

    def read(self, folder: str, name: str) -> bytes:
        return self.files[(folder.upper(), name.upper())]


def plugin_constants() -> dict[str, float]:
    """kBarrierArcDeg / kBarrierRadius / kBarrierHeight as src/proteus_shield.inc states them."""
    text = (ROOT / 'src/proteus_shield.inc').read_text(encoding='utf-8')
    out = {}
    for name in ('kBarrierArcDeg', 'kBarrierRadius', 'kBarrierHeight'):
        m = re.search(name + r'=([0-9.]+)f', text)
        assert m, name
        out[name] = float(m.group(1))
    return out


class ShieldFile(unittest.TestCase):
    def built(self) -> dict:
        game = FakeGame({('OBJECT', mp.SHIELD_STOCK): stock_round(), ('WEAPON', mp.BARRIER_WEAPON): stock_barrier()})
        data = mp.shield(game)
        mp.check_shield(data)
        return sgo.plain(sgo.load(data=data))

    def test_one_still_barrier_round(self) -> None:
        m = self.built()
        p = m['indirect_fire_param']
        self.assertEqual(p[4], 'BarrierBullet01')
        self.assertEqual((p[2], p[3], p[5], p[6], p[9], p[15]), (1, 0, 0.0, 0.0, 0.0, 0), 'one round, still, no gravity / blast / wait')
        self.assertEqual(p[10], mp.SHIELD_LIFE, 'it never runs out: the plugin takes it down')
        self.assertEqual(p[7], 1.0, 'AmmoSize scales the wall\'s render matrix (slot 3): 1')
        self.assertLess(p[8], 0.1, 'its own touch radius is tiny (a touch would anchor it)')
        self.assertEqual(p[12], [0.1, 0.1, 1.0, 0.25], 'the barrier\'s own colour')
        self.assertEqual(p[17][1], 'weapon_Engineer_EX_spcBarrier01', 'the barrier\'s own deploy sound')
        self.assertEqual(m['indirect_fire_damage'], 0.0, 'its HP is the plugin\'s')

    def test_wall_matches_the_plugin(self) -> None:
        p = self.built()['indirect_fire_param']
        k = plugin_constants()
        arc, radius, height, scale, offset = p[13]
        self.assertAlmostEqual(arc, k['kBarrierArcDeg'] * math.pi / 180.0, places=5)
        self.assertEqual((radius, height), (k['kBarrierRadius'], k['kBarrierHeight']))
        self.assertEqual((scale, offset), ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))
        # The plugin claims its round by the ctor's segment count (int(arc / 0.0872), float32): never the tochka's.
        self.assertEqual(mp.segments(arc), 24)
        self.assertEqual(mp.segments(2.4000000953674316), 27, 'EWEAPON196 (the tochka) builds 27 segments')
        self.assertGreater(radius, 9.8, 'the wall stands outside the MK2 hull (9.7 m forward, 8.9 m aside)')
        self.assertGreater(height, 15.9, 'and above it (15.2-15.9 m)')

    def test_carries_the_barrier_device(self) -> None:
        # A BarrierBullet01 builds its device from its InitParam +0x1B0, which only a weapon fills (0x68DAD5): the IFC
        # gets it from this SGO (src/ifc_model.h). Without it the ctor throws and the game dies (report #6).
        game = FakeGame({('OBJECT', mp.SHIELD_STOCK): stock_round(), ('WEAPON', mp.BARRIER_WEAPON): stock_barrier()})
        self.assertEqual(sgo.read(mp.shield(game))[1]['animation_model'], DEVICE)
        bare = FakeGame({('OBJECT', mp.SHIELD_STOCK): stock_round(), ('WEAPON', mp.BARRIER_WEAPON): stock_barrier(False)})
        with self.assertRaises((ValueError, KeyError)):
            mp.shield(bare)
        old = sgo.write(*sgo.read(mp.shield(game))[:1], {k: v for k, v in sgo.read(mp.shield(game))[1].items()
                                                          if k != 'animation_model'})
        with self.assertRaises(AssertionError):
            mp.check_shield(old)

    def test_plugin_reads_the_member_the_sgo_has(self) -> None:
        text = (ROOT / 'src/ifc_model.h').read_text(encoding='utf-8')
        self.assertIn('kModelMember[]=L"animation_model"', text)

    def test_refuses_unexpected_sources(self) -> None:
        bad = sgo.write(0x102, {'AmmoClass': 'SolidBullet01', 'AmmoColor': [1, 1, 1, 1], 'Ammo_CustomParameter': []})
        with self.assertRaises(ValueError):
            mp.shield(FakeGame({('OBJECT', mp.SHIELD_STOCK): stock_round(), ('WEAPON', mp.BARRIER_WEAPON): bad}))


class RealRoot(unittest.TestCase):
    def setUp(self) -> None:
        root = Path(rootcpk.DEFAULT_GAME)
        if not (root / 'Root.cpk').is_file():
            self.skipTest('no Root.cpk')
        self.game = rootcpk.Game(str(root))

    def test_real_build(self) -> None:
        data = mp.shield(self.game)
        mp.check_shield(data)
        tochka = sgo.plain(sgo.load(data=self.game.read('WEAPON', mp.BARRIER_WEAPON)))
        self.assertEqual(tochka['AmmoClass'], 'BarrierBullet01')
        built = sgo.plain(sgo.load(data=data))['indirect_fire_param']
        for a, b in zip(built[12], tochka['AmmoColor'], strict=True):
            self.assertAlmostEqual(a, b, places=6)
        self.assertEqual(mp.segments(tochka['Ammo_CustomParameter'][0]), 27)
        import dsgo
        device = dsgo.parse(self.game.read('WEAPON', mp.BARRIER_WEAPON)).root.get('animation_model')
        model = sgo.read(data)[1]['animation_model']
        self.assertEqual(model[0], device.items[0].items)
        self.assertEqual(model[1], device.items[1])
        self.assertEqual(model[2], device.items[2].data, 'the MAB block byte for byte')
        files = mp.build(str(Path(rootcpk.DEFAULT_GAME)))
        self.assertEqual(set(files), {mp.SHIELD_FILE}, 'no model, animation or vehicle SGO is generated any more')


if __name__ == '__main__':
    unittest.main(verbosity=1)
