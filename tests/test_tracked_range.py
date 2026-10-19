"""Tracked vehicles in the range: private mission SGOs and complete shared assets.

Lifecycle tests write only temporary Mods trees. Native cases read Root.cpk and
build in memory; the real game directory is never changed or launched.
"""
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'testrange')]
import catalog
import gen
import jet_models
import ledger
import make_bmpt
import make_vulcan
import mdb
import rmpa
import rootcpk
import sgo

BUILDERS = {'edf6tr_vulcan_mission': make_vulcan, 'edf6tr_bmpt_mission': make_bmpt}


def files_for(name):
    """Small opaque builder files: lifecycle checks exercise real ledger writes."""
    vehicle = gen.TRACKED_MISSION[name]
    return {f'OBJECT/{vehicle.sgo}.SGO': b'call-in setup',
            **{rel: rel.encode('ascii') for rel in gen.range_files(name)[1:]}}


class RangeCatalogTests(unittest.TestCase):
    def test_native_call_in_objects_get_private_placeable_mission_twins(self):
        rows = {row.sgo: row for row in catalog.rows()}
        plan = gen.target_range(gen.Plan())
        for name in BUILDERS:
            with self.subTest(name=name):
                vehicle = gen.TRACKED_MISSION[name]
                self.assertEqual(gen.DERIVED[name], vehicle.sgo)
                self.assertEqual(gen.GROUND_MISSION[name], vehicle.tool)
                self.assertIn(name, gen.PLACEABLE_DERIVED)
                self.assertEqual(plan.vehicles[name], 1)
                self.assertEqual(rows[name].source, 'range')
                self.assertEqual(rows[name].category, 'ours')
                self.assertNotIn(vehicle.sgo.lower(), rows)
                self.assertNotIn(f'OBJECT/{vehicle.sgo}.SGO', catalog.files_of(rows[name]))

    def test_ported_aircraft_and_walkers_stay_on_ground_for_boarding(self):
        rows = {row.sgo: row for row in catalog.rows()}
        names = ('vehicle302_heli', 'vehicle406_begaruta', 'vehicle406_begaruta_red',
                 'vehicle406_begaruta_gold', 'vehicle406_begaruta_green')
        for name in names:
            with self.subTest(name=name):
                row = rows[name]
                self.assertEqual(row.category, 'vehicle')
                self.assertEqual(row.source, 'edf41')
                self.assertEqual(row.lift, 0.5)
                self.assertFalse(row.flying)

    def test_old_tank_plans_migrate_without_losing_counts_or_missing_file_checks(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root, 'plan.json')
            path.write_text(json.dumps({'vehicles': {'vehicle301_tank_ai': 2, 'vehicle301_tank': 1},
                                        'friends': {'vehicle301_tank_ai': 1}}), encoding='utf-8')
            plan = gen.load_plan(str(path))
            self.assertEqual(plan.vehicles, {'vehicle301_tank': 3})
            self.assertEqual(plan.friends, {'vehicle301_tank': 1})
            self.assertIn('vehicle301_tank', gen.target_range(gen.Plan()).vehicles)
            direct = gen.Plan(vehicles={'vehicle301_tank_ai': 1})
            kept, skipped = gen.without_missing(root, direct)
            self.assertFalse(kept.vehicles)
            self.assertEqual(len(skipped), 1)
            self.assertIn('OBJECT/VEHICLE301_TANK_AI.SGO', skipped[0])

    def test_m_map_requires_model_and_every_generated_weapon(self):
        rows = {row.sgo: row for row in catalog.rows()}
        with tempfile.TemporaryDirectory() as root:
            for name in BUILDERS:
                row = rows[name]
                dependencies = catalog.files_of(row)
                self.assertEqual(dependencies, list(gen.range_files(name)))
                self.assertEqual(len(dependencies), 6 if 'vulcan' in name else 7)
                for rel in dependencies:
                    path = Path(root, 'Mods', *rel.split('/'))
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'present')
                self.assertTrue(catalog.available(root, row))
                for rel in dependencies:
                    with self.subTest(name=name, missing=rel):
                        path = Path(root, 'Mods', *rel.split('/'))
                        path.unlink()
                        self.assertFalse(catalog.available(root, row))
                        path.write_bytes(b'present')


class RangeLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = self.temp.name
        self.game = SimpleNamespace(root='read-only-source')
        self.payloads = {name: files_for(name) for name in BUILDERS}
        self.builders = {}
        for name, module in BUILDERS.items():
            stub = patch.object(module, 'build', return_value=self.payloads[name])
            self.builders[name] = stub.start()
            self.addCleanup(stub.stop)
        stub = patch.object(gen, 'vehicle_sgo', side_effect=lambda game, name, model: name.encode('ascii'))
        stub.start()
        self.addCleanup(stub.stop)

    def assert_range_files(self, name, expected=True):
        led = ledger.Ledger(self.root)
        for rel in gen.range_files(name):
            with self.subTest(name=name, rel=rel):
                self.assertEqual(Path(led.disk(rel)).is_file(), expected)
                self.assertEqual(gen.OWNER in led.owners(rel), expected)

    def test_standalone_build_claims_complete_assets_without_call_in_sgos(self):
        gen._write_derived(self.root, self.game, set(BUILDERS))
        for name in BUILDERS:
            self.assert_range_files(name)
            self.builders[name].assert_called_once_with(self.game.root)
            call = f'OBJECT/{gen.TRACKED_MISSION[name].sgo}.SGO'
            self.assertFalse(Path(ledger.Ledger(self.root).disk(call)).exists())
        gen._write_derived(self.root, self.game, set())
        for name in BUILDERS:
            self.assert_range_files(name, False)

    def test_installer_removal_preserves_range_models_and_ammunition(self):
        for name, module in BUILDERS.items():
            module.install(self.root, self.payloads[name])
        gen._write_derived(self.root, self.game, set(BUILDERS))
        for name, module in BUILDERS.items():
            module.remove(self.root)
            self.assert_range_files(name)
            for rel in gen.range_files(name)[1:]:
                self.assertEqual(Path(ledger.Ledger(self.root).disk(rel)).read_bytes(), self.payloads[name][rel])
        gen._write_derived(self.root, self.game, set())
        for name in BUILDERS:
            self.assert_range_files(name, False)

    def test_range_removal_preserves_installer_assets(self):
        for name, module in BUILDERS.items():
            module.install(self.root, self.payloads[name])
        gen._write_derived(self.root, self.game, set(BUILDERS))
        gen._write_derived(self.root, self.game, set())
        for name, module in BUILDERS.items():
            for rel in self.payloads[name]:
                led = ledger.Ledger(self.root)
                self.assertTrue(Path(led.disk(rel)).exists())
                self.assertNotIn(gen.OWNER, led.owners(rel))
            module.remove(self.root)
            self.assert_range_files(name, False)

    def test_rebuilding_subset_releases_only_the_vehicle_no_longer_used(self):
        gen._write_derived(self.root, self.game, set(BUILDERS))
        keep = 'edf6tr_vulcan_mission'
        gen._write_derived(self.root, self.game, {keep})
        self.assert_range_files(keep)
        self.assert_range_files('edf6tr_bmpt_mission', False)


@unittest.skipUnless(Path(rootcpk.DEFAULT_GAME, 'Root.cpk').is_file(), 'read-only Root.cpk unavailable')
class NativeRangeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = gen.Game(rootcpk.DEFAULT_GAME)
        cls.files = {name: module.build(cls.game.root) for name, module in BUILDERS.items()}

    def test_private_mission_setup_contains_every_native_weapon(self):
        for name in BUILDERS:
            with self.subTest(name=name):
                data = gen.vehicle_sgo(self.game, name)
                values = sgo.load(data=data)
                self.assertIn('mission_setup', values)
                weapons = tuple(mount[0] for mount in values['mission_setup'][-1])
                self.assertEqual(weapons, gen.TRACKED_MISSION[name].weapons)
                self.assertEqual(len(values['vehicle_weapon_setting']), len(weapons))
                self.assertEqual(values['xgs_scene_object_class'], 'Vehicle603_Flak')

    def test_authored_models_fit_their_placement_footprints(self):
        for name in BUILDERS:
            with self.subTest(name=name):
                vehicle = gen.TRACKED_MISSION[name]
                archive = mdb.rab_read(self.files[name][f'OBJECT/{vehicle.sgo}.MRAB'])
                model = next(f for f in archive.files if f.name.lower() == 'v603_flak.mdb').data
                mesh = mdb.mdb_read(mdb.cmpl_decompress(model))
                reach = max((x*x + z*z)**.5 for x, _y, z in jet_models.bind_positions(mesh))
                self.assertGreaterEqual(gen.footprint(name) + gen.OVERLAP_SLACK, reach)

    def test_default_fleet_fits_real_m045_and_keeps_ground_and_air_targets(self):
        plan = gen.target_range(gen.Plan())
        self.assertEqual(plan.site, 'M045')
        points = rmpa.points(self.game.read('MISSION/EDF6/M045', 'MISSION.RMPA'))
        lay = gen.layout(points, gen.small_count(plan), gen.far_reserved(plan))
        placed = gen.spots_for(plan, lay)
        self.assertEqual(len(placed), len(gen.placements(plan)))
        self.assertEqual(len({p.name for _, _, p in placed}), len(placed))
        taken = [(lay.player, gen.SPOT)]
        for name, _npc, point in placed:
            radius = gen.footprint(name)
            self.assertEqual(gen.overlap(point, radius, taken), 0.0, f'{name} at {point.name}')
            taken.append((point, radius))
        targets = gen.target_spots(lay)
        self.assertGreaterEqual(len(targets), gen.TARGET_SPOTS)
        for point in targets:
            self.assertEqual(gen.overlap(point, gen.TARGET_SPREAD, taken), 0.0)
        self.assertTrue(gen.air_targets(lay))
        self.assertGreater(len(targets), len(gen.air_targets(lay)))
        text = gen.script(plan, gen.layout(points, gen.small_count(plan), gen.far_reserved(plan)))
        for name in BUILDERS:
            self.assertIn(f'"app:/object/{name}.sgo"', text)
        self.assertTrue(gen.target_ranged(plan))
        self.assertFalse(plan.air.enabled)

    def test_real_standalone_install_builds_private_sgos_and_all_resources(self):
        wanted = set(BUILDERS)
        with tempfile.TemporaryDirectory() as root:
            plan = gen.Plan(vehicles={name: 1 for name in wanted})
            plan.waves.enabled = False
            with patch.object(gen, 'Game', return_value=self.game), \
                 patch.object(gen, 'register_pack', return_value=[]), \
                 patch.object(gen, 'placeable_derived', return_value=wanted), \
                 patch.object(make_vulcan, 'build', return_value=self.files['edf6tr_vulcan_mission']), \
                 patch.object(make_bmpt, 'build', return_value=self.files['edf6tr_bmpt_mission']):
                gen.install(root, plan)
            rows = {row.sgo: row for row in catalog.rows()}
            text = Path(gen.mission_dir(root, gen.RANGE_MISSION), 'MISSION.AC').read_text(encoding='utf-8-sig')
            for name in wanted:
                self.assertTrue(catalog.available(root, rows[name]))
                for rel in gen.range_files(name):
                    self.assertIn(gen.OWNER, ledger.Ledger(root).owners(rel))
                values = sgo.load(data=Path(root, 'Mods', *rows[name].rel.split('/')).read_bytes())
                self.assertIn('mission_setup', values)
                self.assertIn(f'Preload("app:/object/{name}.sgo"', text)
                for rel in gen.range_files(name)[1:]:
                    self.assertEqual(Path(root, 'Mods', *rel.split('/')).read_bytes(), self.files[name][rel])
            self.assertTrue(gen.uninstall(root, unregister=False))
            for name in wanted:
                self.assertFalse(catalog.available(root, rows[name]))


if __name__ == '__main__':
    unittest.main()
