"""Tracked combat asset install/cache/remove and release packaging boundaries.

Only temporary stand-in game folders are written. Native model/weapon generation
is covered by test_tracked_combat.py; these cases test the installation lifecycle.
"""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib'), str(ROOT / 'testrange')]
import build_release
import buildcache
import installer
import ledger
import make_bmpt
import make_vulcan

BUILDERS = (make_vulcan, make_bmpt)


def fixture(kind: str) -> dict[str, bytes]:
    import make_tracked_combat
    vehicle = make_tracked_combat.vc.GROUND_VEHICLES[kind]
    model = make_tracked_combat.model_module(kind)
    paths = [f'OBJECT/{vehicle.sgo}.SGO', f'OBJECT/{model.OUT_ARC}',
             *(f'WEAPON/{path.rsplit("/", 1)[1].upper()}' for path in vehicle.weapons)]
    return {rel: f'{kind}:{rel}'.encode() for rel in paths}


class TrackedInstallTests(unittest.TestCase):
    def test_installed_assets_cache_and_missing_weapon_forces_regeneration(self) -> None:
        with tempfile.TemporaryDirectory() as game, contextlib.redirect_stdout(io.StringIO()), \
                patch.object(buildcache, 'recipes', return_value={kind: 'recipe-v1' for kind in ('vulcan', 'bmpt')}):
            Path(game, 'Root.cpk').write_bytes(b'stand-in archive identity')
            for builder in BUILDERS:
                with self.subTest(kind=builder.OWNER), patch.object(builder, 'build', return_value=fixture(builder.OWNER)) as make:
                    cache = buildcache.Cache(game)
                    files = installer.build_asset(cache, builder, builder.OWNER)
                    self.assertEqual(make.call_count, 1)
                    paths = builder.install(game, files)
                    self.assertEqual(len(paths), 4 if builder.OWNER == 'vulcan' else 5)
                    cache.record(builder.OWNER, files)
                    cache.save()
                    current = buildcache.Cache(game)
                    self.assertIsNone(installer.build_asset(current, builder, builder.OWNER))
                    self.assertEqual(make.call_count, 1, 'complete unchanged assets must be reused')
                    weapon = next(rel for rel in files if rel.startswith('WEAPON/'))
                    Path(game, 'Mods', weapon).unlink()
                    missing = buildcache.Cache(game)
                    self.assertEqual(installer.build_asset(missing, builder, builder.OWNER), files)
                    self.assertEqual(make.call_count, 2, 'missing ammo is as incomplete as a missing model')
                    builder.install(game, files)
                    deleted, kept = builder.remove(game)
                    self.assertEqual(len(deleted), len(files))
                    self.assertFalse(kept)
                    self.assertFalse(any(Path(path).exists() for path in paths))

    def test_range_owner_retains_every_model_and_weapon_until_last_release(self) -> None:
        with tempfile.TemporaryDirectory() as game:
            for builder in BUILDERS:
                files = fixture(builder.OWNER)
                builder.install(game, files)
                led = ledger.Ledger(game)
                for rel in files:
                    led.need('testrange', rel)
                self.assertEqual(builder.remove(game), ([], []))
                reread = ledger.Ledger(game)
                for rel, data in files.items():
                    self.assertEqual(reread.owners(rel), ['testrange'])
                    self.assertEqual(Path(reread.disk(rel)).read_bytes(), data)
                deleted, kept = reread.release('testrange', list(files))
                self.assertEqual(len(deleted), len(files))
                self.assertFalse(kept)

    def test_removal_preserves_a_players_modified_model(self) -> None:
        with tempfile.TemporaryDirectory() as game:
            for builder in BUILDERS:
                files = fixture(builder.OWNER)
                builder.install(game, files)
                led = ledger.Ledger(game)
                model = next(rel for rel in files if rel.endswith('.MRAB'))
                Path(led.disk(model)).write_bytes(b'player-modified model')
                deleted, kept = builder.remove(game)
                self.assertIn(led.disk(model), kept)
                self.assertEqual(len(deleted), len(files) - 1)
                self.assertEqual(Path(led.disk(model)).read_bytes(), b'player-modified model')


class InstallMaterialSettingTests(unittest.TestCase):
    def test_default_and_explicit_zero_from_effective_section(self) -> None:
        self.assertTrue(installer.material_compat_wanted(''))
        self.assertTrue(installer.material_compat_wanted('[Other]\nEdf41MaterialCompatibility=0'))
        self.assertFalse(installer.material_compat_wanted('\ufeff[ vehicleCREW ]\n  edf41materialcompatibility = 0 ; restore'))
        self.assertTrue(installer.material_compat_wanted('[VehicleCrew]\nEdf41MaterialCompatibility=1'))
        self.assertTrue(installer.material_compat_wanted('[VehicleCrew]\nEdf41MaterialCompatibility=oops'))
        self.assertFalse(installer.material_compat_wanted('[VehicleCrew]\nEdf41MaterialCompatibility=0\nEdf41MaterialCompatibility=1'))

    def test_upgrade_preserves_explicit_zero_and_adds_new_default(self) -> None:
        shipped = '[VehicleCrew]\nEdf41MaterialCompatibility=1\n'
        off = installer.planned_ini('[VehicleCrew]\nEdf41MaterialCompatibility=0\n', shipped)[0]
        self.assertFalse(installer.material_compat_wanted(off))
        old = installer.planned_ini('[VehicleCrew]\nEnabled=1\n', shipped)[0]
        self.assertTrue(installer.material_compat_wanted(old))
        self.assertIn('Edf41MaterialCompatibility=1', old)


class ReleaseTrackedTests(unittest.TestCase):
    def test_freeze_command_contains_all_lazy_models_materials_and_recipe_groups(self) -> None:
        with tempfile.TemporaryDirectory() as temp, patch.object(build_release, 'WORK', temp), \
                patch.object(build_release, 'check_bundled'), patch.object(build_release, 'cmpl_binary', return_value=[]), \
                patch.object(build_release, 'plugin_data', return_value=[]), \
                patch.object(build_release.subprocess, 'run') as run:
            build_release.build_exe('test-tracked', generator='stand-in-generator.exe')
            freeze = run.call_args_list[0].args[0]
            hidden = {freeze[i + 1] for i, value in enumerate(freeze[:-1]) if value == '--hidden-import'}
            self.assertTrue({'make_vulcan', 'make_bmpt', 'make_tracked_combat', 'tracked_combat_model',
                             'vulcan_model', 'bmpt_model', 'edf41_materials', 'edf41materials'} <= hidden)
            recipes = json.loads(Path(temp, buildcache.RECIPES).read_text())
            self.assertTrue({'vulcan', 'bmpt'} <= recipes.keys())
            self.assertNotEqual(recipes['vulcan'], recipes['bmpt'])
            self.assertIn('--check-package', run.call_args_list[1].args[0])
            self.assertIn('--check-workers', run.call_args_list[2].args[0])


if __name__ == '__main__':
    unittest.main()
