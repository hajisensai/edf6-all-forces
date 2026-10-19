"""Asset recipes follow unique runtime modules, independent of developer CLI wrappers."""
from __future__ import annotations

import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import buildcache


class ModuleIdentityTests(unittest.TestCase):
    def recipes_after_edit(self, relative: str) -> dict[str, str]:
        selected = ROOT / relative
        self.assertTrue(selected.is_file(), relative)
        original = Path.read_bytes

        def changed(path: Path) -> bytes:
            data = original(path)
            return data + b'\n# in-memory dependency fingerprint probe\n' if path == selected else data

        with patch.object(Path, 'read_bytes', changed):
            return buildcache.source_recipes(str(ROOT))

    def test_real_runtime_library_edit_invalidates_both_tracked_recipes(self) -> None:
        before = buildcache.source_recipes(str(ROOT))
        after = self.recipes_after_edit('pylib/shkt_check.py')
        for kind in ('vulcan', 'bmpt'):
            with self.subTest(kind=kind):
                self.assertNotEqual(before[kind], after[kind])

    def test_unrelated_cli_edit_does_not_invalidate_tracked_recipes(self) -> None:
        before = buildcache.source_recipes(str(ROOT))
        after = self.recipes_after_edit('tools/check_shkt.py')
        for kind in ('vulcan', 'bmpt'):
            with self.subTest(kind=kind):
                self.assertEqual(before[kind], after[kind])

    def assert_duplicate_rejected(self, duplicate: str) -> None:
        with tempfile.TemporaryDirectory(prefix='edf6-module-identity-') as folder:
            root = Path(folder)
            sources = {
                'pylib/shkt_check.py': "B = 'body-schema'\n",
                'tools/make_vulcan.py': 'import shkt_check\n',
                'tools/make_bmpt.py': 'import shkt_check\n',
                duplicate: "CLI = 'same-name wrapper'\n",
            }
            for relative, text in sources.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            with patch.object(buildcache, 'GROUPS', ('vulcan', 'bmpt')):
                with self.assertRaises(ValueError) as failure:
                    buildcache.source_recipes(str(root))
            message = str(failure.exception).replace('\\', '/')
            self.assertIn((root / 'pylib/shkt_check.py').as_posix(), message)
            self.assertIn((root / duplicate).as_posix(), message)

    def test_duplicate_stem_is_rejected_instead_of_overwriting_the_library(self) -> None:
        self.assert_duplicate_rejected('tools/shkt_check.py')

    def test_windows_case_duplicate_in_fourth_runtime_directory_is_rejected(self) -> None:
        self.assert_duplicate_rejected('autoturret/tools/ShKt_ChEcK.py')


def run_checks() -> None:
    """tools/selftest.py entry: preserve diagnostics while avoiding an extra test process."""
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ModuleIdentityTests))
    if not result.wasSuccessful():
        raise AssertionError(output.getvalue())


if __name__ == '__main__':
    unittest.main()
