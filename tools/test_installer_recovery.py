"""Installer review regressions; all files and mutations stay in temporary fake games."""
from __future__ import annotations

from contextlib import ExitStack, redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / p) for p in ('tools', 'pylib', 'testrange', 'autoturret/tools')]
import build as at_build
import call_weapons
import gen
import installer
import ledger
import modfiles
import rootcpk


class RecoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory(prefix='edf6-installer-recovery-')
        self.addCleanup(temp.cleanup)
        self.game = Path(temp.name)
        self.mods = str(self.game / 'Mods')
        self.rel = 'WEAPON/TEST.SGO'
        self.path = self.game / 'Mods' / self.rel
        self.files = {self.rel: b'pack data'}
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(redirect_stdout(io.StringIO()))
        self.stack.enter_context(patch.object(at_build, 'build_files', lambda legacy=False: self.files))
        self.stack.enter_context(patch.object(at_build, '_refuse_while_running', lambda _: None))
        self.stack.enter_context(patch.object(rootcpk, 'default', side_effect=AssertionError('real game read')))

    def install(self, force: bool = False) -> None:
        at_build.install(self.mods, text=False, force=force, files=self.files)

    def test_foreign_edit_after_owned_install_is_restored(self) -> None:
        self.install()
        self.path.write_bytes(b'later foreign edit')
        self.install(force=True)
        self.assertEqual(self.path.read_bytes(), b'pack data')
        at_build.uninstall(self.mods, force=False)
        self.assertEqual(self.path.read_bytes(), b'later foreign edit')

    def test_latest_foreign_edit_survives_reinstalls_and_old_backup(self) -> None:
        modfiles.atomic_write(str(self.path), b'first foreign file')
        self.install(force=True)
        for data in (b'foreign edit two', b'foreign edit three'):
            self.path.write_bytes(data)
            self.install(force=True)
            self.install()  # a normal update must retain the chosen recovery point
        at_build.uninstall(self.mods, force=False)
        self.assertEqual(self.path.read_bytes(), b'foreign edit three')

    def test_refused_foreign_edit_is_untouched(self) -> None:
        self.install()
        self.path.write_bytes(b'foreign')
        with self.assertRaises(SystemExit):
            self.install()
        self.assertEqual(self.path.read_bytes(), b'foreign')

    def test_interrupted_forced_upgrade_restores_latest_foreign_edit(self) -> None:
        self.install()
        self.path.write_bytes(b'foreign')
        write = modfiles.atomic_write

        def interrupted(path: str, data: bytes) -> None:
            write(path, data)
            if Path(path) == self.path:
                raise OSError('interrupted after target replacement')

        with patch.object(modfiles, 'atomic_write', interrupted):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                self.install(force=True)
        at_build.uninstall(self.mods, force=False)
        self.assertEqual(self.path.read_bytes(), b'foreign')

    def test_backup_failure_changes_neither_target_nor_manifest(self) -> None:
        self.install()
        self.path.write_bytes(b'foreign')
        manifest = self.game / 'Mods' / at_build.MANIFEST
        before = manifest.read_bytes()
        write = modfiles.atomic_write

        def failed(path: str, data: bytes) -> None:
            if 'overrides' in Path(path).parts:
                raise OSError('backup failed')
            write(path, data)

        with patch.object(modfiles, 'atomic_write', failed):
            with self.assertRaisesRegex(OSError, 'backup failed'):
                self.install(force=True)
        self.assertEqual(self.path.read_bytes(), b'foreign')
        self.assertEqual(manifest.read_bytes(), before)

    def test_pack_check_requires_every_nonempty_mission_file(self) -> None:
        # Everything previously checked is complete. Only mission files vary.
        shipped = {name: (b'dll', f'[{section}]\nKey=1\n'.encode()) for name, section in installer.PLUGINS}
        for name, blobs in shipped.items():
            for ext, data in zip(installer.PLUGIN_FILES, blobs):
                modfiles.atomic_write(str(self.game / 'Mods/Plugins' / (name + ext)), data)
        ledger.Ledger(str(self.game)).put('jets', 'OBJECT/TEST.SGO', b'asset')
        self.stack.enter_context(patch.object(installer, 'plugin_files', return_value=shipped))
        self.stack.enter_context(patch.object(at_build, 'check', return_value=True))
        self.stack.enter_context(patch.object(call_weapons, 'check', return_value=True))
        import make_edf5_campaign
        self.stack.enter_context(patch.object(make_edf5_campaign, 'check', return_value=True))
        self.stack.enter_context(patch.object(make_edf5_campaign, 'range_installed', return_value=True))
        self.stack.enter_context(patch.object(rootcpk, 'use', lambda _: None))
        self.assertFalse(installer.check(str(self.game)))  # interrupted before gen.install
        out = Path(gen.mission_dir(str(self.game), gen.RANGE_MISSION))
        names = ('MISSION.AC', 'MISSION.RMPA', 'MISSION.JSON', gen.MARKER)
        for name in names:
            modfiles.atomic_write(str(out / name), b'complete')
        self.assertTrue(installer.check(str(self.game)))
        for name in names:
            with self.subTest(name=name):
                path = out / name
                path.unlink()
                self.assertFalse(installer.check(str(self.game)))
                path.write_bytes(b'')
                self.assertFalse(installer.check(str(self.game)))
                path.write_bytes(b'complete')
        before = {p: p.read_bytes() for p in self.game.rglob('*') if p.is_file()}
        self.assertTrue(installer.check(str(self.game)))
        self.assertEqual(before, {p: p.read_bytes() for p in self.game.rglob('*') if p.is_file()})


def run_checks() -> None:
    result = unittest.TextTestRunner(stream=io.StringIO()).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(RecoveryTests))
    if not result.wasSuccessful():
        raise AssertionError('\n'.join(text for _, text in result.errors + result.failures))


if __name__ == '__main__':
    unittest.main()
