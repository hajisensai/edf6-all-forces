"""Request generation and upgrade regressions for the live 505 recoil crash."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import dsgo
import ledger
import make_stock_stores as stores
import modfiles


def request(stem: str, count: int) -> bytes:
    weapons = [[f'app:/weapon/stock_{i}.sgo', [0.1, 0.2]] for i in range(count)]
    entry = ['transport', 'box', f'app:/object/{stem.lower()}.sgo', [1.0, weapons]]
    root = dsgo.Node([stores._node([entry]), stores._node([entry[2]])],
                     {0: 'Ammo_CustomParameter', 1: 'resource'})
    return dsgo.write(dsgo.Document(root, []))


class StockStoreRecoilTests(unittest.TestCase):
    def test_all_requests_keep_stock_weapons_without_inventing_machine_guns(self) -> None:
        for stem, mounts in stores.LOADOUTS.items():
            with self.subTest(stem=stem):
                count = max(m.like for m in mounts) + 1
                before = request(stem, count)
                after, returned = stores.request_sgo(before, 'request.sgo', {stem: count})
                self.assertEqual(returned, stem)
                stores.check({'WEAPON/REQUEST.SGO': after}, {stem: count})
                old = dsgo.to_py(dsgo.parse(before).root)['Ammo_CustomParameter'][0][3][1]
                new = dsgo.to_py(dsgo.parse(after).root)['Ammo_CustomParameter'][0][3][1]
                self.assertEqual(new[:count], old)
                self.assertNotIn('coax_mg', str(new).lower())   # the retired 2026-10-07 gun (retired_loadout.h)
                for mount, weapon in zip(mounts, new[count:]):
                    # rockets, missiles and the coaxial machine gun recoil nothing; a gun round as the stock gun beside
                    want = [0.1, 0.2] if stores.is_shell(mount.weapon) and not stores.is_coax(mount.weapon) else [0.0, 0.0]
                    self.assertEqual(weapon[1], want)

    def test_old_aim_recoil_variant_is_rejected(self) -> None:
        data, _ = stores.request_sgo(request('V505_TANK', 1), 'request.sgo', {'V505_TANK': 1})
        doc = dsgo.parse(data)
        entry = doc.root.get('Ammo_CustomParameter').items[0]
        weapon_list = entry.items[3].items[1]
        weapon_list.items[-1].items[1] = stores._node(['AimRecoil', [0.0, 0.0026]])
        with self.assertRaisesRegex(AssertionError, 'BodyRecoil'):
            stores.check({'WEAPON/REQUEST.SGO': dsgo.write(doc)}, {'V505_TANK': 1})
        with self.assertRaises(ValueError):
            stores._params(stores.Mount(stores.COAX_MG, 0))

    def test_slot48_table_covers_every_built_class(self) -> None:
        self.assertEqual(set(stores.SLOT48_RECOIL), set(stores.BUILT_CLASSES))
        self.assertTrue(set(stores.SLOT48_ROUTED) <= {c for c, t in stores.SLOT48_RECOIL.items() if t and None in t})

    def test_403_store_past_the_seats(self) -> None:
        """testhub #12: the 403's AGM-114s are holder 3 of a three-seat 403. Its stock slot 48 takes holder 3 for seat
        3's machine gun (AimRecoil into seat 3): no record is safe there; src/storerecoil.cpp runs the hull push."""
        cls = 'Vehicle403_Tank'
        self.assertEqual(stores.store_recoil(cls, 0, 3), 'BodyRecoil')
        self.assertEqual([stores.store_recoil(cls, i, 3) for i in (1, 2)], ['AimRecoil', 'AimRecoil'])
        self.assertEqual(stores.store_recoil(cls, 3, 3), 'BodyRecoil')
        stores.check_store_recoil('VEHICLE403_TANK', cls, 3, 3)
        # Without the plugin's own code for that holder (before the fix) the 403's store is refused, not shipped.
        with patch.dict(stores.SLOT48_ROUTED, clear=True):
            with self.assertRaisesRegex(ValueError, '不安全'):
                stores.check_store_recoil('VEHICLE403_TANK', cls, 3, 3)
        # A store that would land on a gunner's index wants AimRecoil, which _params never writes.
        with self.assertRaisesRegex(ValueError, 'AimRecoil'):
            stores.check_store_recoil('VEHICLE403_TANK', cls, 1, 3)
        with self.assertRaisesRegex(ValueError, 'SLOT48_RECOIL'):
            stores.store_recoil('Vehicle999_Unknown', 1, 1)

    def test_403_route_is_the_plugins(self) -> None:
        """SLOT48_ROUTED names code that exists: the 403's slot 48 chained, a holder past the seats on 0x5FFEE0."""
        src = (ROOT / 'src' / 'storerecoil.cpp').read_text(encoding='utf-8')
        head = (ROOT / 'src' / 'storerecoil.h').read_text(encoding='utf-8')
        self.assertIn('kVtable403=0x17D8FA0,kFired403=0x5FED60,kHullFired=0x5FFEE0', src)
        self.assertIn('index!=0 && index<holders && index>=seats ? Path::hull : Path::stock', head)
        self.assertIn('InstallStoreRecoil();', (ROOT / 'src' / 'plugin.cpp').read_text(encoding='utf-8'))
        self.assertIn('src/storerecoil.cpp', (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8'))

    def test_upgrade_rewrites_request_and_releases_old_coax_asset(self) -> None:
        with tempfile.TemporaryDirectory(prefix='edf6-stock-recoil-') as game, \
                patch.object(modfiles, 'refuse_while_running', lambda: None):
            led = ledger.Ledger(game)
            for name in stores.store_files():
                led.put('jets', f'WEAPON/{name}', b'jet store fixture')
            obsolete = f'WEAPON/{stores.COAX_MG}'
            led.put(stores.OWNER, obsolete, b'old invented machine gun')
            rel = 'WEAPON/REQUEST.SGO'
            led.put(stores.OWNER, rel, b'old AimRecoil request')
            updated, _ = stores.request_sgo(request('V505_TANK', 1), rel, {'V505_TANK': 1})
            stores.install(game, {rel: updated})
            self.assertEqual(Path(led.disk(rel)).read_bytes(), updated)
            self.assertFalse(Path(led.disk(obsolete)).exists())
            self.assertEqual(ledger.Ledger(game).owners(obsolete), [])
            self.assertTrue(all(Path(led.disk(f'WEAPON/{f}')).exists() for f in stores.store_files()))


if __name__ == '__main__':
    unittest.main()
