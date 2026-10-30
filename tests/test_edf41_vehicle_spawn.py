"""Private, fully configured EDF4.1 spawn resources keep call-in prototypes separate."""
from __future__ import annotations

import hashlib
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'testrange'), str(ROOT / 'tools'), str(ROOT / 'pylib')]
import catalog
import edf41_objects as objects
import gen
import sgo


class PrivateSpawnContract(unittest.TestCase):
    def row(self) -> catalog.Row:
        return catalog.Row('vehicle301_tank', '4.1 tank', 'vehicle', 'edf41',
                           spawn_sgo='edf41_range_vehicle301_tank')

    def test_plan_and_log_id_stay_stable_while_path_targets_private_spawn(self) -> None:
        row = self.row()
        self.assertEqual(row.ident, 'VEHICLE301_TANK')
        self.assertEqual(row.rel, 'OBJECT/EDF41_RANGE_VEHICLE301_TANK.SGO')
        self.assertEqual(row.path, 'app:/object/EDF41_RANGE_VEHICLE301_TANK.sgo')
        with patch.dict(gen.OPTIONAL_VEHICLES, {row.sgo: row}, clear=True):
            self.assertEqual(gen.mission_vehicle(row.sgo), row.spawn_sgo)

    def test_range_reads_private_setup_without_modifying_call_in(self) -> None:
        row = self.row()
        setup = [[1.0, 1.0], [0.1, 10.0, 0.025, 2.0, 0.1, 0.8],
                 [['app:/weapon/v_505tank_cannon01.sgo', [0.2, 3.0], [20.0, 0.01, 0.1]]]]
        original = sgo.write(0x102, {'xgs_scene_object_class': 'Vehicle601_Tank', 'vehicle_setup': setup})
        configured = sgo.write(0x102, {'xgs_scene_object_class': 'Vehicle601_Tank',
                                     'vehicle_setup': setup, 'mission_setup': setup})
        with tempfile.TemporaryDirectory() as root, patch.dict(gen.OPTIONAL_VEHICLES, {row.sgo: row}, clear=True):
            base = Path(root, 'Mods', 'OBJECT')
            base.mkdir(parents=True)
            source = base / 'VEHICLE301_TANK.SGO'
            source.write_bytes(original)
            Path(root, 'Mods', *row.rel.split('/')).write_bytes(configured)
            game = SimpleNamespace(root=root)
            self.assertEqual(gen.vehicle_sgo(game, row.sgo), configured)
            self.assertEqual(gen.vehicle_sgo(game, row.spawn_sgo), configured)
            self.assertTrue(gen.has_mission_setup(game, row.spawn_sgo))
            self.assertEqual(source.read_bytes(), original)

    def test_bundled_vehicles_have_separate_fully_configured_spawn_resources(self) -> None:
        reg = objects.registry()
        by_source = {obj['object']: obj for obj in reg['objects']}
        checked = 0
        for row in catalog.edf41_rows():
            if row.category != 'vehicle':
                continue
            with self.subTest(vehicle=row.sgo):
                source_rel = 'OBJECT/' + row.sgo.upper() + '.SGO'
                obj = by_source[source_rel]
                self.assertEqual(obj.get('spawn_object'), row.rel)
                self.assertNotEqual(row.rel, source_rel)
                self.assertIn(row.rel, obj['files'])
                self.assertIn(row.rel, catalog.files_of(row))
                source_raw, spawn_raw = objects.read(source_rel), objects.read(row.rel)
                self.assertEqual(hashlib.sha256(source_raw).hexdigest(), reg['files'][source_rel])
                self.assertEqual(hashlib.sha256(spawn_raw).hexdigest(), reg['files'][row.rel])
                source, spawn = sgo.load(data=source_raw), sgo.load(data=spawn_raw)
                self.assertEqual(spawn['xgs_scene_object_class'], source['xgs_scene_object_class'])
                self.assertEqual(sgo.plain(spawn['mission_setup']), sgo.plain(source['vehicle_setup']))
                self.assertEqual(sgo.plain(spawn['vehicle_setup']), sgo.plain(source['vehicle_setup']))
                self.assertEqual(gen.mission_vehicle(row.sgo), row.spawn_sgo)
                self.assertFalse(row.preload, 'vehicles load when first placed (RequestLoad), not at the range start')
                if 'VEHICLE301' in source_rel:
                    self.assertEqual(len(spawn['mission_setup'][2]), 1)
                elif 'VEHICLE302' in source_rel:
                    self.assertEqual(len(spawn['mission_setup'][3]), 4)
                    self.assertIn('FUEL', str(spawn['mission_setup'][3][-1][0]).upper())
                elif 'VEHICLE406' in source_rel:
                    self.assertEqual(len(spawn['mission_setup'][4]), 6)
                elif 'VEHICLE408' in source_rel:
                    self.assertEqual(len(spawn['car_base_wheel']), 3)
                    self.assertEqual(spawn['vehicle_weapon_setting'], [['gun', 1]])
                    self.assertEqual(len(spawn['mission_setup'][2]), 1)
                elif 'FORTRESSROBO' in source_rel:
                    self.assertEqual(len(spawn['mission_setup'][2]), 2)
                checked += 1
        self.assertGreaterEqual(checked, 13)


if __name__ == '__main__':
    unittest.main()
