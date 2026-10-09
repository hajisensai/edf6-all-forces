"""Every stock vehicle carries the stores its kind should (the user, 2026-10-09: 「各种载具要有应有的挂载（符合设定的，
例如坦克应该有ap和he，甚至炮射导弹如应有的话）」; tools/make_stock_stores.py LOADOUTS / CATEGORIES / REQUIRED).

Offline (no game): each vehicle of LOADOUTS has a category, carries its category's kinds, and every store it names is a
catalogued kind (src/stores.inc) whose role fits it (AP rounds penetrate with no blast, HE ones burst, the stock-round
templates are named and of the right class); the switch can hold them (src/payload.h kMostPayload). With the game's
Root.cpk: every vehicle a stock request brings is in LOADOUTS or NOT_LOADED, and every round this tool writes is built
from its template with the template's numbers (only its load and name changed).
"""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import dsgo  # noqa: E402
import make_stock_stores as stores  # noqa: E402
import vcobjects as vc  # noqa: E402


def kind_of(weapon: str) -> str:
    got = vc.store_of('app:/weapon/' + weapon.lower())
    assert got is not None, weapon
    return got[0]


def most_payload() -> int:
    text = (ROOT / 'src' / 'payload.h').read_text(encoding='utf-8')
    return int(re.search(r'constexpr int kMostPayload=(\d+);', text).group(1))


class LoadoutTableTests(unittest.TestCase):
    def test_every_vehicle_carries_its_category(self) -> None:
        self.assertEqual(set(stores.LOADOUTS), set(stores.CATEGORIES))
        for stem, category in stores.CATEGORIES.items():
            with self.subTest(stem=stem, category=category):
                kinds = [kind_of(m.weapon) for m in stores.LOADOUTS[stem]]
                roles = {vc.STORES[k].role for k in kinds}
                self.assertEqual(len(kinds), len(set(kinds)), 'one kind once')
                for need in stores.REQUIRED[category]:
                    if need.startswith('role:'):
                        self.assertIn(need[5:], roles)
                    else:
                        self.assertIn(need, kinds)

    def test_tanks_have_ap_he_and_a_gun_launched_missile_from_the_main_gun(self) -> None:
        for stem, category in stores.CATEGORIES.items():
            if category != 'mbt':
                continue
            with self.subTest(stem=stem):
                load = {kind_of(m.weapon): m for m in stores.LOADOUTS[stem]}
                for k in ('AP', 'HE', 'GLM'):
                    self.assertEqual(load[k].like, 0, 'fired from the main gun (holder 0)')
                self.assertIsInstance(vc.STORES['AP'].weapon, vc.Shell)
                self.assertEqual(vc.STORES['AP'].weapon.kind, 'ap')
                self.assertEqual(vc.STORES['HE'].weapon.kind, 'he')
                self.assertEqual(vc.STORES['GLM'].role, 'ground')

    def test_rounds_are_catalogued_and_typed(self) -> None:
        inc = (ROOT / 'src' / 'stores.inc').read_text(encoding='utf-8')
        roles = {'air', 'ground', 'bomb', 'rocket', 'gun'}
        for weapon in stores.store_files():
            kind = kind_of(weapon)
            store = vc.STORES[kind]
            with self.subTest(weapon=weapon):
                self.assertIn(f'L"EDF6VC_{kind}_"', inc)
                self.assertIn(store.role, roles)
                if isinstance(store.weapon, vc.Shell):
                    self.assertEqual(store.role, 'gun')
                    self.assertIn(store.weapon.kind, ('ap', 'he'))
                    self.assertTrue(store.weapon.template.upper().endswith('.SGO'))
                    if store.weapon.kind == 'ap':
                        self.assertIn('SolidBullet01', store.weapon.ammo_class)
                    self.assertEqual({lang for lang, _ in store.names}, {'en', 'ja', 'cn', 'sc', 'kr'})
                    self.assertIn(weapon, stores.own_store_files())
                else:
                    self.assertNotEqual(store.role, 'gun')

    def test_switch_holds_every_choice(self) -> None:
        # The seat's stock weapons (at most 3 on one seat of these classes, with the fuel tank left out) and its stores.
        for stem, load in stores.LOADOUTS.items():
            with self.subTest(stem=stem):
                self.assertLessEqual(len(load) + 4, most_payload())

    def test_shell_params_follow_the_gun_they_fire_beside(self) -> None:
        mount = stores.Mount(vc.store_file('AP', 20), 0)
        self.assertEqual(stores._params(mount, [0.1, 0.4]), [0.1, 0.4])
        self.assertEqual(stores._params(mount, ['AimRecoil', [0.0, 0.1]]), stores.NO_RECOIL)
        self.assertEqual(stores._params(stores.Mount(vc.store_file('GLM', 4), 0), [0.1, 0.4]), stores.NO_RECOIL)


class WithGameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import rootcpk
        root = Path(rootcpk.DEFAULT_GAME)
        if not (root / 'Root.cpk').is_file():
            raise unittest.SkipTest('no game here')
        cls.game = vc.Game(str(root))

    def test_every_stock_vehicle_is_loaded_or_explained(self) -> None:
        brought = set()
        for name in self.game.names('WEAPON'):
            if not name.upper().endswith('.SGO'):
                continue
            data = self.game.read('WEAPON', name)
            if data[:4] != b'DSGO':
                continue
            try:
                found = stores._brought_any(dsgo.parse(data))
            except (ValueError, KeyError, IndexError):
                continue
            if found:
                brought.add(found.rsplit('.', 1)[0].upper())
        self.assertTrue(brought)
        missing = sorted(brought - set(stores.LOADOUTS) - set(stores.NOT_LOADED))
        self.assertEqual(missing, [], 'a stock vehicle with neither stores nor a reason')
        self.assertFalse(set(stores.LOADOUTS) & set(stores.NOT_LOADED))

    def test_own_rounds_are_their_templates(self) -> None:
        for weapon in stores.own_store_files():
            kind = kind_of(weapon)
            store = vc.STORES[kind]
            with self.subTest(weapon=weapon):
                got = dsgo.to_py(dsgo.parse(vc.store_sgo(self.game, weapon)).root)
                rounds = vc.store_of('app:/weapon/' + weapon.lower())[1]
                self.assertEqual(got['AmmoCount'], float(rounds))
                self.assertEqual(got['name.en'], dict(store.names).get('en', store.name))
                if not isinstance(store.weapon, vc.Shell):
                    continue
                stock = dsgo.to_py(dsgo.parse(self.game.read('WEAPON', store.weapon.template)).root)
                self.assertEqual(got['AmmoClass'], store.weapon.ammo_class)
                changed = {'AmmoCount', *dict(store.weapon.extra)} | {k for k in got if k.startswith('name.')}
                self.assertEqual({k: v for k, v in got.items() if k not in changed},
                                 {k: v for k, v in stock.items() if k not in changed})
                if store.weapon.kind == 'ap':
                    self.assertEqual((got['AmmoExplosion'], got['AmmoIsPenetration']), (0.0, 1.0))
                else:
                    self.assertGreater(got['AmmoExplosion'], 0.0)


if __name__ == '__main__':
    unittest.main()
