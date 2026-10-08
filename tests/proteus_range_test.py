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
import dsgo
import ledger
import make_proteus as mp
import rootcpk
import sgo


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
    @classmethod
    def setUpClass(cls):
        cls.game = rootcpk.Game(rootcpk.DEFAULT_GAME)
        cls.files = mp.build(rootcpk.DEFAULT_GAME)

    def test_full_install_range_reinstall_remove_order_keeps_dependencies(self):
        with tempfile.TemporaryDirectory() as root:
            source = 'V614_PROTEUS_MK2_MISSION.SGO'
            original = self.game.read('OBJECT', source)
            loose = Path(root, 'Mods', 'OBJECT', source)
            loose.parent.mkdir(parents=True); loose.write_bytes(original)
            mp.install(root, self.files)
            before_range = loose.read_bytes()
            wanted = set(gen.PROTEUS_MISSION.values())
            for _ in range(2):
                gen._write_derived(root, self.game, wanted)
            self.assertEqual(loose.read_bytes(), before_range)
            for name in wanted:
                data = Path(root, 'Mods', 'OBJECT', name.upper()+'.SGO').read_bytes()
                _, needs = mp.redirect(data)
                self.assertIn('mission_setup', sgo.load(data=data))
                self.assertEqual(len(needs), 2)
                for rel in needs:
                    self.assertIn('testrange', ledger.Ledger(root).owners(rel))
                    self.assertEqual(Path(root, 'Mods', rel).read_bytes(), self.files[rel])
            mp.remove(root)
            self.assertEqual(loose.read_bytes(), original)
            for rel in self.files:
                if rel.endswith(('.MRAB', '.CAS')):
                    self.assertTrue(Path(root, 'Mods', rel).exists(), rel)
            gen._write_derived(root, self.game, set())
            mp.remove(root)
            self.assertEqual(loose.read_bytes(), original)
            for rel in self.files:
                if rel.endswith(('.MRAB', '.CAS')):
                    self.assertFalse(Path(root, 'Mods', rel).exists(), rel)

    def test_standalone_real_range_script_preloads_and_spawns_private_sgo(self):
        with tempfile.TemporaryDirectory() as root:
            plan = gen.Plan()
            stock_name = 'v614_proteus_mk2_mission'
            private = gen.mission_vehicle(stock_name)
            plan.vehicles = {stock_name: 1}; plan.friends = {}; plan.scenario = ''
            plan.waves.enabled = False; plan.air.enabled = False
            # Resolve the temporary game's resource reads to the genuine read-only
            # archive; all production install/ledger writes still target `root`.
            with patch.object(gen, 'Game', return_value=self.game):
                gen.install(root, plan)
            text = Path(gen.mission_dir(root, plan.slot), 'MISSION.AC').read_text(encoding='utf-8-sig')
            self.assertIn(f'Preload("app:/object/{private}.sgo"', text)
            self.assertIn(f'"app:/object/{private}.sgo", {plan.vehicle_level:.2f}', text)
            self.assertNotIn(f'"app:/object/{stock_name}.sgo"', text)
            self.assertFalse(Path(root, 'Mods', 'OBJECT', stock_name.upper()+'.SGO').exists())
            data = Path(root, 'Mods', 'OBJECT', private.upper()+'.SGO').read_bytes()
            _, needs = mp.redirect(data)
            for rel in needs:
                self.assertEqual(Path(root, 'Mods', rel).read_bytes(), self.files[rel])
                self.assertIn('testrange', ledger.Ledger(root).owners(rel))
            self.assertTrue(gen.uninstall(root))
            for rel in needs:
                self.assertFalse(Path(root, 'Mods', rel).exists())

    def test_edited_private_consumer_survives_reinstall_and_both_uninstall_orders(self):
        with tempfile.TemporaryDirectory() as root:
            mp.install(root, self.files)
            name = gen.mission_vehicle('v614_proteus_mk2_mission')
            gen._write_derived(root, self.game, {name})
            path = Path(root, 'Mods', 'OBJECT', name.upper()+'.SGO')
            document = dsgo.parse(path.read_bytes())
            document.root.set('game_object_durability', 54321.0)
            altered = dsgo.write(document); path.write_bytes(altered)
            gen._write_derived(root, self.game, {name})
            self.assertEqual(path.read_bytes(), altered)
            _, needs = mp.redirect(altered)
            gen.uninstall(root)
            mp.remove(root)
            self.assertEqual(path.read_bytes(), altered)
            for rel in needs:
                self.assertTrue(Path(root, 'Mods', rel).exists())
                self.assertIn('testrange', ledger.Ledger(root).owners(rel))


if __name__ == '__main__': unittest.main()
