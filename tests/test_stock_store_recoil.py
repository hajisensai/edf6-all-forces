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
                self.assertNotIn('coax_mg', str(new).lower())
                for mount, weapon in zip(mounts, new[count:]):
                    # rockets and missiles recoil nothing; a gun round as the stock gun it fires beside
                    want = [0.1, 0.2] if stores.is_shell(mount.weapon) else [0.0, 0.0]
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
