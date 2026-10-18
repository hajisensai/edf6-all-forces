"""Authored M163/BMPT: matching native model / weapon / request contracts.

The native data cases read only Root.cpk. They generate in memory and never write
or start the game. Missing game data skips these cases but keeps table tests.
"""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import call_weapons
import calls
import dsgo
import make_tracked_combat as combat
import rootcpk
import vcobjects as vc


class CatalogContractTests(unittest.TestCase):
    def test_new_requests_append_without_reordering_shipped_ids(self) -> None:
        self.assertEqual(calls.IDS[-2:], ('EDF6VC_CALL_VULCAN', 'EDF6VC_CALL_BMPT'))
        for release in calls.RELEASED.values():
            self.assertEqual(calls.IDS[:len(release)], release)

    def test_native_weapon_choices_use_same_catalog_as_runtime(self) -> None:
        vehicle = vc.GROUND_VEHICLES['vulcan']
        self.assertEqual(len(vehicle.weapons), 2)
        for kind, path in zip(('VULCAN_HE', 'VULCAN_AP'), vehicle.weapons):
            self.assertEqual(vc.store_of(path), (kind, 2000))
            self.assertTrue(vc.STORES[kind].native_control)
        missile = vc.GROUND_VEHICLES['bmpt'].weapons[2]
        self.assertEqual(vc.store_of(missile), ('BMPT_ATGM', 4))
        self.assertEqual(vc.STORES['BMPT_ATGM'].role, 'ground')


@unittest.skipUnless((Path(rootcpk.DEFAULT_GAME) / 'Root.cpk').is_file(), 'EDF6 Root.cpk is unavailable')
class NativeAssetContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.game = rootcpk.default()
        cls.files = {kind: combat.build(cls.game.root, kind) for kind in combat.KINDS}

    def test_authored_geometry_native_skeleton_and_weapon_muzzles(self) -> None:
        for kind, files in self.files.items():
            with self.subTest(kind=kind):
                combat.check(files, kind, self.game)
                model = combat.model_module(kind)
                info = model.check(files[f'OBJECT/{model.OUT_ARC}'])
                self.assertIsNotNone(info)

    def test_request_default_setup_has_every_real_holder(self) -> None:
        for kind in combat.KINDS:
            with self.subTest(kind=kind):
                call = next(c for c in calls.CALLS if c.ground == kind)
                template = self.game.read('WEAPON', call_weapons.template_of(call) + '.SGO')
                request = call_weapons.vehicle_sgo(template, call, (1.0, 1.0))
                root = dsgo.parse(request).root
                setup = dsgo.to_py(root.get('Ammo_CustomParameter'))[4][3]
                weapons = tuple(entry[0] for entry in setup[-1])
                self.assertEqual(weapons, vc.GROUND_VEHICLES[kind].weapons)
                self.assertEqual(root.get('use_underground'), 0.0)
                model = dsgo.parse(self.files[kind][f'OBJECT/{vc.GROUND_VEHICLES[kind].sgo}.SGO']).root
                self.assertEqual(len(setup[-1]), len(model.get('vehicle_weapon_setting').items))
                if kind == 'bmpt':
                    self.assertEqual(setup[-1][2][1], [0.0, 0.0])
                    self.assertEqual(len(setup[-1][2]), 2, 'missile store has no turret-motor parameters')
                    self.assertEqual(len(dsgo.to_py(model.get('vehicle_setup'))[-1][2]), 2)

    def test_m163_alternate_belts_share_the_visible_moving_barrel(self) -> None:
        vehicle = vc.GROUND_VEHICLES['vulcan']
        r = dsgo.parse(self.files['vulcan'][f'OBJECT/{vehicle.sgo}.SGO']).root
        settings = dsgo.to_py(r.get('vehicle_weapon_setting'))
        self.assertEqual(settings[0][0], 'cannon_slide_l')
        self.assertEqual(settings[1][0], settings[0][0])
        for path in vehicle.weapons:
            gun = dsgo.parse(self.files['vulcan'][f'WEAPON/{path.rsplit("/",1)[1].upper()}']).root
            self.assertEqual(gun.get('FireInterval'), 1.0)
            self.assertGreater(gun.get('AmmoSpeed'), 15.0)
        for kind in ('VULCAN_HE', 'VULCAN_AP'):
            weapon = dsgo.parse(self.files['vulcan'][f'WEAPON/{vc.store_file(kind,2000)}']).root
            self.assertEqual(weapon.get('AmmoIsPenetration'), 1.0 if kind.endswith('AP') else 0.0)
            self.assertEqual(weapon.get('AmmoExplosion'), 0.0 if kind.endswith('AP') else 0.6)

    def test_cases_leave_authored_receiver_with_cartridge_scale(self) -> None:
        for kind in combat.KINDS:
            model = combat.model_module(kind)
            expected = model.ejection_local(self.game)
            for side, path in zip(('left', 'right'), vc.GROUND_VEHICLES[kind].weapons[:2]):
                gun = dsgo.parse(self.files[kind][f'WEAPON/{path.rsplit("/",1)[1].upper()}']).root
                self.assertEqual(gun.get('ShellCase_CustomParameter').get('scale'), 0.4 if kind == 'vulcan' else 0.64)
                block = gun.get('animation_model').items[2].data
                heads = struct.unpack_from('<I', block, 0x14)[0]
                header = heads + 16
                record = header + struct.unpack_from('<i', block, header+4)[0]
                offset = record + struct.unpack_from('<i', block, record+0xC)[0]
                for got, want in zip(struct.unpack_from('<3f', block, offset), expected[side]):
                    self.assertAlmostEqual(got, want, places=5)

    def test_muzzle_patch_preserves_native_casing_and_effect_lists(self) -> None:
        native = dsgo.parse(self.game.read('WEAPON', combat.STOCK_GUNS[0])).root.get('animation_model').items[2].data
        point = ('cannon_slide_l', (0.125, 0.25, 1.5))
        changed = combat.set_muzzles(native, [point])
        locator = vc.mab_muzzles(native)[0][2]
        expected = bytearray(native)
        struct.pack_into('<3f', expected, locator, *point[1])
        self.assertEqual(changed, bytes(expected))
        with self.assertRaises(ValueError):
            combat.set_muzzles(native, [point, point])


if __name__ == '__main__':
    unittest.main()
