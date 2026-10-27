"""Release regressions: exercise the import chain and require the frozen EXE gate."""
from __future__ import annotations

import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import build_release
import buildcache
import installer


class FrozenCockpitPackageTests(unittest.TestCase):
    def test_package_check_never_selects_game_or_prompts(self) -> None:
        with patch.object(sys, 'argv', ['installer.exe', '--check-package']), \
                patch.object(sys, 'frozen', True, create=True), \
                patch.object(installer, 'main', side_effect=AssertionError('game flow entered')), \
                patch.object(installer, 'ask', side_effect=AssertionError('interactive prompt')), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(installer.run(), 0)
        self.assertIn('EDF6VC_PACKAGE_OK cockpit families=4', out.getvalue())

    def test_frozen_check_failure_aborts_build_from_empty_folder(self) -> None:
        calls = []

        def run(cmd: list[str], **kwargs: object) -> None:
            calls.append(cmd)
            if cmd[-1] == '--check-package':
                self.assertEqual(os.listdir(kwargs['cwd']), [])
                self.assertNotIn('PYTHONPATH', kwargs['env'])
                self.assertTrue(kwargs['check'])
                raise subprocess.CalledProcessError(1, cmd)

        with tempfile.TemporaryDirectory() as folder, \
                patch.object(build_release, 'WORK', folder), \
                patch.object(build_release, 'check_bundled'), \
                patch.object(build_release.subprocess, 'run', side_effect=run), \
                patch.dict(os.environ, {'PYTHONPATH': str(ROOT / 'pylib')}):
            with self.assertRaises(subprocess.CalledProcessError):
                build_release.build_exe('test')
        self.assertEqual(len(calls), 2)

    def test_collision_gate_runs_the_actual_consumer(self) -> None:
        import importlib.util
        import frozen_collision_probe
        import tracked_collision
        spec = importlib.util.spec_from_file_location('shkt_cli_probe', ROOT / 'tools/check_shkt.py')
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        with patch.object(tracked_collision, 'sc', cli):
            with self.assertRaisesRegex(AttributeError, "shkt_cli_probe' has no attribute"):
                frozen_collision_probe.check()
        frozen_collision_probe.check()

    def test_both_freezers_refuse_duplicate_project_modules(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('tools', 'pylib', 'testrange', 'autoturret/tools'):
                (root / name).mkdir(parents=True)
            for name in ('tools/collision_probe.py', 'pylib/collision_probe.py'):
                (root / name).write_text('pass\n', encoding='utf-8')
            with patch.object(build_release, 'ROOT', folder), \
                    patch.object(build_release.subprocess, 'run', side_effect=AssertionError('freezing entered')):
                for freeze in (build_release.generator_cmd, lambda: build_release.build_exe('test')):
                    with self.subTest(freeze=freeze), self.assertRaisesRegex(ValueError, 'collision_probe'):
                        freeze()

    def test_each_family_change_invalidates_cached_optics_recipe(self) -> None:
        # Use the actual registry sources with the public recipe scanner. Before
        # the fix __import__ hid these dependencies from both freezing and caching.
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tools').mkdir()
            (root / 'pylib').mkdir()
            for group in buildcache.GROUPS:   # each group's generator (an X-Morph unit's: make_xmorph)
                (root / 'tools' / (buildcache.generator(group) + '.py')).write_text(
                    'import cockpit_fits\nimport cockpit_kits\n' if group == 'optics' else 'pass\n')
            for name in ('cockpit_fits', 'cockpit_kits'):
                for path in (ROOT / 'pylib').glob(name + '*.py'):
                    (root / 'pylib' / path.name).write_bytes(path.read_bytes())
            for name in ('cockpits', 'cockpit_shell'):
                (root / 'pylib' / (name+'.py')).write_bytes((ROOT/'pylib'/(name+'.py')).read_bytes())
            shell=root/'pylib'/'cockpit_shell.py'
            retrofit=root/'pylib'/'cockpit_retrofit.py'
            retrofit.write_bytes((ROOT/'pylib'/'cockpit_retrofit.py').read_bytes())
            before = buildcache.source_recipes(folder)
            raw_shell=shell.read_bytes();shell.write_bytes(raw_shell+b'\n# changed native shell selection\n')
            self.assertNotEqual(before['optics'],buildcache.source_recipes(folder)['optics'])
            shell.write_bytes(raw_shell)
            raw=retrofit.read_bytes();retrofit.write_bytes(raw+b'\n# changed instrument surfaces\n')
            self.assertNotEqual(before['optics'],buildcache.source_recipes(folder)['optics'])
            retrofit.write_bytes(raw)
            for kind in ('fits', 'kits'):
                for family in ('ground', 'air', 'mech', 'plugin'):
                    path = root / 'pylib' / f'cockpit_{kind}_{family}.py'
                    raw = path.read_bytes()
                    path.write_bytes(raw + b'\n# changed fit or geometry\n')
                    after = buildcache.source_recipes(folder)
                    self.assertNotEqual(before['optics'], after['optics'], path.name)
                    path.write_bytes(raw)


if __name__ == '__main__':
    unittest.main()
