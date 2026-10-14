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

    def test_each_family_change_invalidates_cached_optics_recipe(self) -> None:
        # Use the actual registry sources with the public recipe scanner. Before
        # the fix __import__ hid these dependencies from both freezing and caching.
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tools').mkdir()
            (root / 'pylib').mkdir()
            for group in buildcache.GROUPS:
                (root / 'tools' / ('make_' + group + '.py')).write_text(
                    'import cockpit_fits\nimport cockpit_kits\n' if group == 'optics' else 'pass\n')
            for name in ('cockpit_fits', 'cockpit_kits'):
                for path in (ROOT / 'pylib').glob(name + '*.py'):
                    (root / 'pylib' / path.name).write_bytes(path.read_bytes())
            before = buildcache.source_recipes(folder)
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
