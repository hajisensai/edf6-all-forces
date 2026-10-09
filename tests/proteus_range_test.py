"""Real Root.cpk Proteus assets through the actual range/installer lifecycle.

Only temporary Mods directories are written; the installed game is read-only.
"""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'testrange')]
import gen
import ledger
import make_proteus as mp
import rootcpk


class RangeIdentity(unittest.TestCase):
    def test_saved_plan_ids_are_stable_but_spawned_files_are_private(self):
        plan = gen.Plan(); plan.vehicles = {name: 1 for name in gen.PROTEUS_MISSION}
        plan.friends = {}; plan.air.enabled = False; plan.scenario = ''
        self.assertEqual({name for name, _ in gen.placements(plan)}, set(gen.PROTEUS_MISSION))
        self.assertEqual(gen.spawned(plan), set(gen.PROTEUS_MISSION.values()))
        self.assertTrue(all(name in gen.DERIVED for name in gen.spawned(plan)))
        self.assertIn('proteus', ledger.OWNERS)  # no patched owner list in this integration test


@unittest.skipUnless(Path(rootcpk.DEFAULT_GAME, 'Root.cpk').is_file(), 'read-only Root.cpk unavailable')
class RealRange(unittest.TestCase):
    """The range's Proteus SGOs are the stock ones (no model redirect); the range holds the shield SGO the plugin raises."""
    @classmethod
    def setUpClass(cls):
        cls.game = rootcpk.Game(rootcpk.DEFAULT_GAME)
        cls.files = mp.build(rootcpk.DEFAULT_GAME)

    def test_full_install_range_reinstall_remove_order_keeps_the_shield(self):
        with tempfile.TemporaryDirectory() as root:
            source = 'V614_PROTEUS_MK2_MISSION.SGO'
            original = self.game.read('OBJECT', source)
            loose = Path(root, 'Mods', 'OBJECT', source)
            loose.parent.mkdir(parents=True); loose.write_bytes(original)
            mp.install(root, self.files)
            self.assertEqual(loose.read_bytes(), original, 'the install never touches a vehicle SGO any more')
            wanted = set(gen.PROTEUS_MISSION.values())
            for _ in range(2):
                gen._write_derived(root, self.game, wanted)
            for name in wanted:
                data = Path(root, 'Mods', 'OBJECT', name.upper()+'.SGO').read_bytes()
                self.assertEqual(data, self.game.read('OBJECT', gen.DERIVED[name] + '.SGO'), 'the stock vehicle, unredirected')
            shield = Path(root, 'Mods', *mp.SHIELD_FILE.split('/'))
            self.assertIn('testrange', ledger.Ledger(root).owners(mp.SHIELD_FILE))
            self.assertEqual(shield.read_bytes(), self.files[mp.SHIELD_FILE])
            mp.remove(root)
            self.assertTrue(shield.exists(), 'the range still holds it')
            gen._write_derived(root, self.game, set())
            mp.remove(root)
            self.assertFalse(shield.exists())
            self.assertEqual(loose.read_bytes(), original)

    def test_standalone_real_range_script_preloads_and_spawns_private_sgo(self):
        with tempfile.TemporaryDirectory() as root:
            plan = gen.Plan()
            stock_name = 'v614_proteus_mk2_mission'
            private = gen.mission_vehicle(stock_name)
            plan.vehicles = {stock_name: 1}; plan.friends = {}; plan.scenario = ''
            plan.waves.enabled = False; plan.air.enabled = False
            # Resolve the temporary game's resource reads to the genuine read-only
            # archive; all production install/ledger writes still target `root`.
            # The pack registration reads the whole game (tools/make_edf5_campaign.py, its own tests): not this one's.
            with patch.object(gen, 'Game', return_value=self.game), patch.object(gen, 'register_pack', return_value=[]):
                gen.install(root, plan)
            text = Path(gen.mission_dir(root, gen.RANGE_MISSION), 'MISSION.AC').read_text(encoding='utf-8-sig')
            self.assertIn(f'Preload("app:/object/{private}.sgo"', text)
            self.assertIn(f'"app:/object/{private}.sgo", {plan.vehicle_level:.2f}', text)
            self.assertNotIn(f'"app:/object/{stock_name}.sgo"', text)
            self.assertFalse(Path(root, 'Mods', 'OBJECT', stock_name.upper()+'.SGO').exists())
            shield = Path(root, 'Mods', *mp.SHIELD_FILE.split('/'))
            self.assertEqual(shield.read_bytes(), self.files[mp.SHIELD_FILE], 'a range alone builds the same shield SGO')
            self.assertIn('testrange', ledger.Ledger(root).owners(mp.SHIELD_FILE))
            self.assertTrue(gen.uninstall(root))
            self.assertFalse(shield.exists())


if __name__ == '__main__': unittest.main()
