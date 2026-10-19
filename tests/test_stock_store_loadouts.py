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
                kinds = [kind_of(w) for m in stores.LOADOUTS[stem] for w in m.options()]
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
                load = {kind_of(w): m for m in stores.LOADOUTS[stem] for w in m.options()}
                for k in ('AP', 'HE', 'GLM'):
                    self.assertEqual(load[k].like, 0, 'fired from the main gun (holder 0)')
                self.assertIs(load['AP'], load['HE'], 'AP and HE are one holder: the round the stock gun is not')
                self.assertIsInstance(vc.STORES['AP'].weapon, vc.Shell)
                self.assertEqual(vc.STORES['AP'].weapon.kind, 'ap')
                self.assertEqual(vc.STORES['HE'].weapon.kind, 'he')
                self.assertEqual(vc.STORES['GLM'].role, 'ground')
                self.assertEqual(vc.STORES['GLM'].weapon.muzzle, 'V_505TANK_CANNON01.SGO', 'from the tank gun muzzle')

    def test_a_round_pair_carries_the_kind_the_stock_gun_is_not(self) -> None:
        # The user, 2026-10-10: 「这个he和原本的弹药重复了吧」.
        pair = stores.LOADOUTS['V505_TANK'][0]
        kinds = {'app:/weapon/howitzer.sgo': 'he', 'app:/weapon/smoothbore.sgo': 'ap'}.get
        beside = lambda path: ['app:/weapon/' + path, [0.1, 0.2]]   # noqa: E731
        self.assertEqual(kind_of(stores.pick(pair, beside('howitzer.sgo'), kinds)), 'AP')
        self.assertEqual(kind_of(stores.pick(pair, beside('smoothbore.sgo'), kinds)), 'HE')
        self.assertEqual(kind_of(stores.pick(pair, beside('unknown.sgo'), kinds)), 'AP', 'not known: AP')
        self.assertEqual(kind_of(stores.pick(pair, 0.0, kinds)), 'AP', 'an empty stock holder')
        glm = stores.LOADOUTS['V505_TANK'][1]
        self.assertEqual(stores.pick(glm, beside('smoothbore.sgo'), kinds), glm.weapon, 'a single store: itself')

    def test_rounds_are_catalogued_and_typed(self) -> None:
        inc = (ROOT / 'src' / 'stores.inc').read_text(encoding='utf-8')
        roles = {'air', 'ground', 'bomb', 'rocket', 'gun', 'coax'}
        for weapon in stores.store_files():
            kind = kind_of(weapon)
            store = vc.STORES[kind]
            with self.subTest(weapon=weapon):
                self.assertIn(f'L"EDF6VC_{kind}_"', inc)
                self.assertIn(store.role, roles)
                if isinstance(store.weapon, vc.Shell):
                    self.assertEqual(store.role, 'coax' if store.weapon.kind == 'mg' else 'gun')
                    self.assertIn(store.weapon.kind, ('ap', 'he', 'mg'))
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
                native = 2 if stores.CATEGORIES[stem] == 'flak' else 4
                self.assertLessEqual(len(load) + native, most_payload())

    def test_flak_ap_and_proximity_are_paired_native_barrels(self) -> None:
        load = {kind_of(m.weapon): m for m in stores.LOADOUTS['V603_FLAK']}
        for left, right, ammo in (('FLAK_AP', 'FLAK_AP_R', 'ap'), ('FLAK_HE', 'FLAK_HE_R', 'he')):
            a, b = vc.STORES[left], vc.STORES[right]
            self.assertGreater(a.group, 0)
            self.assertEqual(a.group, b.group, 'one R/LB choice for both barrels')
            self.assertEqual((a.barrel, b.barrel), (0, 1))
            self.assertEqual((load[left].like, load[right].like), (0, 1), 'mounted to the matching native holder')
            self.assertEqual((a.weapon.kind, b.weapon.kind), (ammo, ammo))
            self.assertTrue(a.weapon.template.endswith('_L.SGO') and b.weapon.template.endswith('_R.SGO'))
        self.assertNotEqual(vc.STORES['FLAK_AP'].group, vc.STORES['FLAK_HE'].group)
        for kind in ('VULCAN_HE', 'VULCAN_AP'):
            self.assertTrue(vc.STORES[kind].native_control, 'M61 belts are exclusive native choices')
            self.assertEqual(vc.STORES[kind].group, 0)

    def test_coaxial_guns_are_real_7_62_mm_coaxes(self) -> None:
        # The user, 2026-10-11: 「这个坦克的同轴机枪做的什么玩意，找现实的模型来做」: each tank's coax is the real gun of its
        # class (M240C / Type 74), at its cyclic rate, muzzle velocity and reach, with a belt in the thousands, a tracer
        # every fifth round in the plugin's colours, the muzzle on the flash hider out of a port on the mantlet.
        payload = (ROOT / 'src' / 'payload.cpp').read_text(encoding='utf-8')

        def floats(name: str) -> tuple[float, ...]:
            body = re.search(rf'constexpr float {name}\[4\]=\{{([^}}]*)\}};', payload).group(1)
            return tuple(float(x.strip().rstrip('f')) for x in body.split(','))
        self.assertEqual(floats('kCoaxTracer'), vc.COAX_TRACER)
        self.assertEqual(floats('kCoaxBall'), vc.COAX_BALL)
        self.assertIn(f'constexpr int kCoaxTracerEvery={vc.COAX_TRACER_EVERY};', payload)
        self.assertIn('constexpr float kCoaxNpcM=900.0f;', payload)
        self.assertEqual({m.gun.name for m in vc.COAX_MOUNTS.values()}, {'M240C', 'Type 74'})
        for tank, mount in vc.COAX_MOUNTS.items():
            gun = mount.gun
            store = vc.STORES[f'MG{tank}']
            extra = dict(store.weapon.extra)
            with self.subTest(tank=tank, gun=gun.name):
                self.assertTrue(650.0 <= gun.rpm <= 950.0, 'a 7.62 mm coax cyclic rate')
                self.assertEqual(extra['FireInterval'], gun.interval)
                self.assertTrue(820.0 <= gun.speed <= 870.0)
                self.assertAlmostEqual(extra['AmmoSpeed'] * 60.0, gun.speed, delta=0.5)
                self.assertTrue(900.0 <= extra['AmmoSpeed'] * extra['AmmoAlive'] <= 1150.0, 'its rounds reach ~1100 m')
                self.assertGreaterEqual(mount.belt, 2000, 'a belt in the thousands')
                self.assertEqual(stores.COAX[next(s for s, m in stores.COAX.items()
                                                  if m.weapon.startswith(f'EDF6VC_MG{tank}_'))].weapon,
                                 vc.store_file(f'MG{tank}', mount.belt))
                self.assertEqual(store.weapon.colour, vc.COAX_TRACER, 'the file: every round a tracer (the plugin dims 4 in 5)')
                self.assertIn(gun.name, dict(store.names)['en'])
                # The visible gun: a barrel's end and a flash hider, rifle calibre, a short way out of the port.
                self.assertTrue(0.010 <= gun.barrel_r <= 0.014 and gun.barrel_r < gun.hider_r <= 0.018)
                self.assertTrue(0.15 <= gun.outside <= 0.35, 'only the end of the 0.63 m barrel shows')
                x, y, z = mount.port
                self.assertTrue(-0.30 <= x <= -0.15, 'right of the main gun, on the mantlet')
                self.assertEqual(store.weapon.muzzle[1], mount.muzzle)
                self.assertAlmostEqual(mount.muzzle[2] - z, mount.collar_out + gun.outside, places=4)

    def test_coax_model_is_its_barrel_out_of_a_port(self) -> None:
        # Offline geometry (no game): the parts of each coax lie on the port's axis, the flash hider's face at the
        # muzzle, nothing of it reaching further back than the collar into the mantlet.
        import numpy as np
        for tank, mount in vc.COAX_MOUNTS.items():
            with self.subTest(tank=tank):
                parts = vc.coax_parts(mount)
                self.assertEqual(set(parts), set(vc.COAX_GUN_TINTS))
                pts = np.array([p for part in parts.values() for p in part.pos])
                x, y, z = mount.port
                self.assertAlmostEqual(float(pts[:, 2].max()), mount.muzzle[2], places=5)
                self.assertAlmostEqual(float(pts[:, 2].min()), z - mount.collar_in, places=5)
                radial = np.hypot(pts[:, 0] - x, pts[:, 1] - y)
                self.assertAlmostEqual(float(radial.max()), mount.collar_r, places=5)
                ahead = pts[pts[:, 2] > z + mount.collar_out + 1e-6]
                self.assertLessEqual(float(np.hypot(ahead[:, 0] - x, ahead[:, 1] - y).max()), mount.gun.hider_r + 1e-6,
                                     'past the collar: barrel and flash hider only')
                for part in parts.values():
                    self.assertTrue(all(0 <= i < len(part.pos) for t in part.tris for i in t))

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
                if store.weapon.muzzle is not None:
                    changed.add('animation_model')
                if store.weapon.colour is not None:
                    changed.add('AmmoColor')
                if store.weapon.flash is not None:
                    changed.add('MuzzleFlash_CustomParameter')
                self.assertEqual({k: v for k, v in got.items() if k not in changed},
                                 {k: v for k, v in stock.items() if k not in changed})
                if store.weapon.kind == 'ap':
                    self.assertEqual((got['AmmoExplosion'], got['AmmoIsPenetration']), (0.0, 1.0))
                elif store.weapon.kind == 'mg':
                    self.assertEqual(got['AmmoExplosion'], 0.0)
                else:
                    self.assertGreater(got['AmmoExplosion'], 0.0)

    def test_flak_templates_preserve_native_fuse_and_penetration(self) -> None:
        rows = stores._read_any(self.game.read('OBJECT', 'V603_FLAK.SGO'))['vehicle_weapon_setting']
        for kind in ('FLAK_AP', 'FLAK_AP_R', 'FLAK_HE', 'FLAK_HE_R'):
            spec = vc.STORES[kind]
            generated = dsgo.to_py(dsgo.parse(vc.store_sgo(self.game, vc.store_file(kind, 500))).root)
            self.assertEqual(rows[spec.barrel][0], 'cannon_slide_l' if spec.barrel == 0 else 'cannon_slide_r')
            if spec.weapon.kind == 'ap':
                self.assertEqual((generated['AmmoExplosion'], generated['AmmoIsPenetration']), (0.0, 1.0))
                self.assertEqual(generated['AmmoSpeed'] * 60.0, 960.0)
            else:
                self.assertEqual(generated['AmmoClass'], 'GrenadeBullet01_MapNoDamage')
                self.assertEqual(generated['AmmoExplosion'], 4.0)
                self.assertEqual(generated['AmmoSize'] * generated['AmmoHitSizeAdjust'], 4.0)

    def test_coaxial_gun_lays_with_the_main_gun_from_its_own_muzzle(self) -> None:
        # The user, 2026-10-11: 「应该有同轴机枪的坦克增加同轴机枪」. Its holder hangs on the bone that elevates the main gun
        # (or the gun itself where the model has no recoil bone), its muzzle beside the barrel (right of it, +X is left)
        # and not the main gun's; the model has that bone.
        import struct
        rows = {stem: stores.stock_rows(self.game, stem) for stem in stores.COAX}
        for stem, mount in stores.COAX.items():
            with self.subTest(stem=stem):
                self.assertIn(mount, stores.LOADOUTS[stem])
                stock = stores._read_any(self.game.read('OBJECT', stem + '.SGO'))['vehicle_weapon_setting']
                self.assertEqual(rows[stem], 1)
                bone = mount.bone or stock[0][0]
                bones = stores.model_bones(self.game, stem)
                if bones is not None:
                    self.assertIn(bone, bones)
                got = dsgo.parse(vc.store_sgo(self.game, mount.weapon)).root.get('animation_model').items[2].data
                muzzles = vc.mab_muzzles(got)
                self.assertEqual(len(muzzles), 1)
                x, y, z = struct.unpack_from('<3f', got, muzzles[0][2])
                self.assertLess(x, -0.15, 'right of the main gun')
                self.assertLess(abs(y), 0.2, 'at the gun axis height')
                self.assertGreater(z, 0.3, 'in front of the mantlet')
                self.assertEqual(stores._params(mount, [0.1, 0.4]), stores.NO_RECOIL, 'no hull rock')
                # Its own gun to see (the user: 「同轴机枪的模型也要用」, then 「找现实的模型来做」): the weapon names its
                # model, the model's gun bone is the muzzle's node with no scale of its own (the stock bone's 1.3 put
                # the 2026-10-11 gun 30% out of place), its flash hider's face on the muzzle point, and all of it a
                # short barrel out of a port: nothing a hand-held gun has (stock, grip, magazine) hangs off it.
                import mdb
                import model_view as mv
                import numpy as np
                shell = vc.STORES[kind_of(mount.weapon)].weapon
                d = dsgo.to_py(dsgo.parse(vc.store_sgo(self.game, mount.weapon)).root)
                ref = d['animation_model'][0]
                self.assertEqual(ref[0].lower(), 'app:/weapon/' + shell.model.lower())
                self.assertTrue(np.allclose(d['AmmoColor'], vc.COAX_TRACER, atol=1e-6))
                self.assertTrue(np.allclose(d['MuzzleFlash_CustomParameter'][8], vc.COAX_FLASH, atol=1e-6))
                rab = mdb.rab_read(vc.coax_model(self.game, shell.model))
                member = next(f for f in rab.files if f.name.lower().endswith('.mdb'))
                self.assertEqual(member.name, ref[1])
                md = mdb.mdb_read(member.data)
                names = [md.name_of(b.name) for b in md.bones]
                self.assertIn(vc.COAX_GUN_NODE, names)
                self.assertEqual(muzzles[0][1], vc.COAX_GUN_NODE)
                self.assertEqual(md.bones[names.index(vc.COAX_GUN_NODE)].local, mdb.ident(), 'no scale on the gun bone')
                v, _, _ = mv.geometry(md, {}, [])   # as drawn: through the bones
                self.assertLess(abs(float(v[:, 2].max()) - z), 0.002, 'its flash hider on the muzzle point')
                self.assertLess(float(np.abs(v[:, 0] - x).max()), 0.04, 'nothing off the port axis sideways')
                self.assertLess(float(np.abs(v[:, 1] - y).max()), 0.04, 'nothing hanging below or above it')
                self.assertTrue(0.25 < float(v[:, 2].max() - v[:, 2].min()) < 0.5, 'collar, barrel end and hider only')

    def test_no_request_carries_the_round_its_stock_gun_already_fires(self) -> None:
        # 22 of the tank requests bring a howitzer (an HE gun): they get APFSDS, the smooth-bore ones HE.
        kinds = stores.stock_round_kind(self.game)
        rows = {stem: stores.stock_rows(self.game, stem) for stem in stores.LOADOUTS}
        seen = set()
        for name in stores.requests(self.game):
            data, stem = stores.request_sgo(self.game.read('WEAPON', name), name, rows, kinds)
            entry = next(x for x in dsgo.to_py(dsgo.parse(data).root)['Ammo_CustomParameter']
                         if isinstance(x, list) and len(x) >= 4 and isinstance(x[2], str) and 'edf6vc_' in x[2])
            weapons = next(x for x in entry[3] if stores._is_list(x))
            for mount, w in zip(stores.LOADOUTS[stem], weapons[rows[stem]:]):
                if mount.other is None:
                    continue
                with self.subTest(request=name):
                    stock = kinds(weapons[mount.like][0]) if isinstance(weapons[mount.like], list) else None
                    mine = vc.STORES[kind_of(w[0].split('/')[-1])].weapon.kind
                    self.assertNotEqual(stock, mine, f'{w[0]} beside {weapons[mount.like]}')
                    seen.add(mine)
        self.assertEqual(seen, {'ap', 'he'}, 'both rounds are carried somewhere')

    def test_gun_launched_missile_fires_from_the_tank_gun_muzzle(self) -> None:
        # The user, 2026-10-10: 「这个炮射导弹是从坦克车头两边射出去的」: not the heli pods' four muzzles.
        got = dsgo.parse(vc.store_sgo(self.game, vc.store_file('GLM', 4))).root.get('animation_model').items[2].data
        gun = dsgo.parse(self.game.read('WEAPON', 'V_505TANK_CANNON01.SGO')).root.get('animation_model').items[2].data
        self.assertEqual(got, gun)
        self.assertEqual(len(vc.mab_muzzles(got)), 1)
        for name in ('V601_TANK_CANNON01.SGO', 'V_505TANK_CANNON01S.SGO'):
            other = dsgo.parse(self.game.read('WEAPON', name)).root.get('animation_model').items[2].data
            self.assertEqual(vc.mab_muzzles(other), vc.mab_muzzles(got), f'{name}: the same muzzle')


if __name__ == '__main__':
    unittest.main()
