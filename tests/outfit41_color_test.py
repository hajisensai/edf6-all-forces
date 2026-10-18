"""EDF4.1 player outfits keep EDF6's two saved colour slots through battle, HQ and every LOD.

Run without the game: python -B tests/outfit41_color_test.py
The shipped resources are the fixture. With EDF6 installed, its native player SGOs are checked too (read only).
"""
from __future__ import annotations

import copy
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]

import dsgo  # noqa: E402
import edf41_outfits as shipped  # noqa: E402
import gamedir  # noqa: E402
import mdb  # noqa: E402
import outfit41  # noqa: E402
import sgo  # noqa: E402


class OutfitColourTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.outfits = {}
        for row in shipped.registry()['classes']:
            archive = mdb.rab_read(shipped.read(next(r for r in row['files'] if r.endswith('.MRAB'))))
            models = [mdb.mdb_read(f.data) for f in archive.files if f.name.lower().endswith('.mdb')]
            cls.outfits[row['class']] = (shipped.read(row['battle']), shipped.read(row['menu']), models)

    def fixture(self):
        return copy.deepcopy(self.outfits['wingdiver'])

    def test_all_four_outfits_and_lods_receive_both_saved_colours(self) -> None:
        self.assertEqual(set(self.outfits), {'ranger', 'wingdiver', 'airraider', 'fencer'})
        for name, (battle, menu, models) in self.outfits.items():
            with self.subTest(name=name):
                self.assertEqual(len(models), 3, 'body plus both LODs must retain colouring')
                self.assertEqual(outfit41.check_color_bindings(battle, menu, models), [])

    def test_fixed_rgba_does_not_replace_the_players_saved_colour(self) -> None:
        battle, menu, models = self.fixture()
        doc = dsgo.parse(battle)
        doc.root.get('soldier_color').items[0].items[1] = dsgo.Node([0.5, 0.5, 0.5, 1.0])
        self.assertTrue(any('battle' in f for f in outfit41.check_color_bindings(dsgo.compact(doc), menu, models)))

    def test_swapping_saved_slots_is_rejected(self) -> None:
        battle, menu, models = self.fixture()
        doc = dsgo.parse(battle)
        doc.root.get('soldier_color').items.reverse()
        self.assertTrue(any('battle' in f for f in outfit41.check_color_bindings(dsgo.compact(doc), menu, models)))

    def test_original_material_names_cannot_leak_into_a_replacement_body(self) -> None:
        battle, menu, models = self.fixture()
        doc = dsgo.parse(battle)
        doc.root.get('soldier_color').items[0].items[0].items[0].items[0] = 'p506_stock_material'
        self.assertTrue(any('battle' in f for f in outfit41.check_color_bindings(dsgo.compact(doc), menu, models)))
        version, members = sgo.read(menu)
        members['change_color'][0][0] = 'p506_stock_material'
        self.assertTrue(any('menu' in f for f in outfit41.check_color_bindings(battle, sgo.write_depth_first(version, members), models)))

    def test_menu_slots_cannot_diverge_from_battle(self) -> None:
        battle, menu, models = self.fixture()
        version, members = sgo.read(menu)
        members['change_color'].reverse()
        self.assertTrue(any('menu' in f for f in outfit41.check_color_bindings(battle, sgo.write_depth_first(version, members), models)))

    def test_colour_cannot_disappear_when_the_body_switches_to_lod(self) -> None:
        battle, menu, models = self.fixture()
        for material in models[2].materials:
            material.params = [p for p in material.params if p.name != 'change_color1']
        faults = outfit41.check_color_bindings(battle, menu, models)
        self.assertTrue(faults)
        self.assertTrue(all(f.startswith('model 2:') and 'change_color1' in f for f in faults))

    def test_unused_colour_material_does_not_count(self) -> None:
        battle, menu, models = self.fixture()
        model = models[0]
        unused = copy.deepcopy(next(m for m in model.materials if any(p.name == 'change_color0' for p in m.params)))
        for material in model.materials:
            material.params = [p for p in material.params if p.name != 'change_color0']
        model.materials.append(unused)
        faults = outfit41.check_color_bindings(battle, menu, models)
        self.assertTrue(faults)
        self.assertTrue(all(f.startswith('model 0:') and 'change_color0' in f for f in faults))

    def test_invalid_material_values_are_rejected(self) -> None:
        battle, menu, models = self.fixture()
        used = {mesh.material for obj in models[0].objects for mesh in obj.meshes}
        for i in used:
            for param in models[0].materials[i].params:
                if param.name == 'change_color0':
                    param.value[0] = float('nan')
        self.assertTrue(any(f.startswith('model 0:') and 'finite change_color0' in f
                            for f in outfit41.check_color_bindings(battle, menu, models)))

    def test_a_single_armour_material_cannot_lose_its_colour(self) -> None:
        battle, menu, models = self.fixture()
        material = next(m for m in models[0].materials if models[0].name_of(m.name) == 'palewing')
        material.params = [p for p in material.params if p.name != 'change_color1']
        self.assertEqual(outfit41.check_color_bindings(battle, menu, models),
                         ['model 0: material palewing: must expose one finite change_color1 float4'])

    def test_a_missing_colour_mask_is_rejected(self) -> None:
        battle, menu, models = self.fixture()
        material = next(m for m in models[0].materials if models[0].name_of(m.name) == 'palewing')
        material.textures = [t for t in material.textures if t.kind != outfit41.COLOR_MASK]
        self.assertEqual(outfit41.check_color_bindings(battle, menu, models),
                         ['model 0: material palewing: colour mask must reference one texture'])

    def test_native_player_sgos_use_the_same_saved_colour_routes(self) -> None:
        game_dir = gamedir.find()
        if not game_dir or not os.path.isfile(os.path.join(game_dir, 'Root.cpk')):
            self.skipTest('EDF6 is not installed; native colour routes were not read')
        import rootcpk
        game = rootcpk.Game(game_dir)
        for name, spec in outfit41.SPECS.items():
            with self.subTest(name=name):
                battle, menu, models = self.outfits[name]
                stock_battle = game.read(*spec.base_sgo.split('/'))
                stock_menu = game.read(*spec.base_menu.split('/'))
                self.assertEqual(outfit41.check_color_bindings(stock_battle, stock_menu, models), [])
                self.assertEqual(dsgo.to_py(dsgo.parse(battle).root.get('soldier_color')),
                                 dsgo.to_py(dsgo.parse(stock_battle).root.get('soldier_color')))
                self.assertEqual(sgo.plain(sgo.read(menu)[1]['change_color']),
                                 sgo.plain(sgo.read(stock_menu)[1]['change_color']))


if __name__ == '__main__':
    unittest.main()
