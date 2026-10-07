"""Campaign installer lifecycle regressions; all mutations are in temporary fake games."""
from __future__ import annotations

from contextlib import ExitStack, contextmanager, redirect_stdout
import ctypes
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / p) for p in ('tools', 'pylib', 'testrange', 'autoturret/tools')]
import installer
import make_edf5_campaign as campaign
import modfiles


class CampaignLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        # CTest pipes stdout on Windows CI, where Python uses cp1252. Capture the installer's Chinese UI
        # like a user-interface fixture; nested captures below still verify the actual menu text.
        output = redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        temp = tempfile.TemporaryDirectory(prefix='edf6-campaign-lifecycle-')
        self.addCleanup(temp.cleanup)
        self.root = temp.name
        running = patch.object(modfiles, 'refuse_while_running', lambda: None)
        running.start()
        self.addCleanup(running.stop)
        self.ini = Path(self.root, 'Mods', campaign.INI)
        modfiles.atomic_write(str(self.ini), b'[VehicleCrew]\nEDF5CampaignRows=0\n')
        self.files = {rel: b'old ' + rel.encode() for rel in campaign.FILES}
        campaign.install(self.root, (self.files, 147, {}))

    @contextmanager
    def pack(self):
        """Run the production installer; only unrelated expensive generators and game reads are stubbed."""
        import buildcache
        import call_weapons
        import gen
        import make_bigmap
        import make_stock_stores
        import rootcpk
        plugins = {name: (b'new plugin', f'[{section}]\nStockVehicleStores=0\nEDF5CampaignRows=0\n'.encode())
                   for name, section in installer.PLUGINS}
        with ExitStack() as stack:
            for name, value in {'check_loader': None, 'plugin_files': plugins, 'stack_weapons': {},
                                'build_autoturret': ({}, False), 'install_autoturret': None,
                                'build_asset': None}.items():
                stack.enter_context(patch.object(installer, name, return_value=value))
            for module, name, value in ((rootcpk, 'use', None), (call_weapons, 'recover', False),
                                        (call_weapons, 'install', []), (gen, 'install', []),
                                        (gen, 'target_range', None), (make_stock_stores, 'wanted', False),
                                        (make_stock_stores, 'remove', ([], [])), (make_bigmap, 'set_big_world', None)):
                stack.enter_context(patch.object(module, name, return_value=value))
            stack.enter_context(patch.object(buildcache, 'Cache'))
            build = stack.enter_context(patch.object(campaign, 'build', return_value=(
                self.files, 147, {'rows': [object()], 'skipped': []})))
            stack.enter_context(redirect_stdout(io.StringIO()))
            yield build

    def test_default_install_does_not_opt_in(self) -> None:
        campaign.remove(self.root)
        with self.pack() as build:
            installer.install(self.root)
            build.assert_not_called()
        self.assertFalse(campaign.enabled(self.root))
        self.assertFalse(Path(campaign.rel_path(self.root, campaign.LIST)).exists())

    def test_explicit_opt_in_and_normal_update_preserve_enabled_campaign(self) -> None:
        campaign.remove(self.root)
        with self.pack() as build:
            installer.install(self.root, campaign_requested=True)
            self.assertTrue(campaign.enabled(self.root))
            installer.install(self.root)
            self.assertEqual(build.call_count, 2)
        self.assertTrue(campaign.check(self.root))

    def test_menu_six_dispatches_explicit_install_and_displays_limits(self) -> None:
        output = io.StringIO()
        with patch.object(installer, 'ask', side_effect=['6', '1']), \
                patch.object(installer, 'pick_game', return_value=self.root), \
                patch.object(modfiles, 'game_running', return_value=False), \
                patch.object(installer, 'install') as install, redirect_stdout(output):
            self.assertEqual(installer.main([]), 0)
        install.assert_called_once_with(self.root, campaign_requested=True)
        self.assertIn('BVM', output.getvalue())
        self.assertIn('4', output.getvalue())

    def test_cancel_campaign_menu_does_not_install(self) -> None:
        with patch.object(installer, 'ask', return_value=''), patch.object(installer, 'install') as install:
            self.assertEqual(installer.manage_campaign(self.root), 0)
        install.assert_not_called()

    def test_disable_preserves_plugins_and_default_update_does_not_reenable(self) -> None:
        plugin = self.ini.with_suffix('.dll')
        plugin.write_bytes(b'existing plugin')
        with patch.object(installer, 'ask', return_value='2'):
            self.assertEqual(installer.manage_campaign(self.root), 0)
        self.assertEqual(plugin.read_bytes(), b'existing plugin')
        self.assertFalse(campaign.enabled(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_not_called()
        self.assertFalse(campaign.enabled(self.root))

    def test_disabled_foreign_image_residue_is_not_enabled_or_unhealthy(self) -> None:
        image = campaign.rel_path(self.root, campaign.IMAGE)
        modfiles.atomic_write(image, b'foreign image')
        with patch.object(installer, 'ask', return_value='2'):
            self.assertEqual(installer.manage_campaign(self.root), 0)
        self.assertTrue(campaign.installed(self.root))  # recovery record stays for the other tool's file
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.check(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_not_called()
        self.assertEqual(modfiles.read(image), b'foreign image')
        self.assertFalse(campaign.enabled(self.root))

    def test_disable_changed_list_reports_failure_and_preserves_dependencies(self) -> None:
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.LIST), b'foreign appended list')
        with patch.object(installer, 'ask', return_value='2'):
            self.assertEqual(installer.manage_campaign(self.root), 1)
        self.assertTrue(campaign.enabled(self.root))
        self.assertTrue(all(Path(campaign.rel_path(self.root, rel)).exists() for rel in campaign.FILES))

    def test_changed_list_preserves_all_dependencies_and_recovery_records(self) -> None:
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.LIST), b'foreign list retaining appended rows')
        before = {rel: modfiles.read(campaign.rel_path(self.root, rel)) for rel in campaign.FILES}
        manifest = campaign.load_manifest(self.root)
        done, kept = campaign.remove(self.root)
        self.assertEqual(done, [])
        self.assertEqual(len(kept), len(campaign.FILES))
        self.assertEqual(campaign.load_manifest(self.root), manifest)
        for rel, data in before.items():
            self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), data)
        self.assertIn('EDF5CampaignRows=147', self.ini.read_text())

    def test_pack_uninstall_stops_before_any_other_removal(self) -> None:
        import rootcpk
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.LIST), b'foreign appended list')
        for choice in ('1', '2'):
            with self.subTest(choice=choice), patch.object(installer, 'ask', return_value=choice), \
                    patch.object(rootcpk, 'use'), patch.object(installer, 'retire_weapons') as retire, \
                    patch.object(installer, 'uninstall_stock_stores') as stores, \
                    patch.object(installer, 'remove_plugin') as plugin:
                installer.uninstall(self.root)
                retire.assert_not_called()
                stores.assert_not_called()
                plugin.assert_not_called()

    def test_partial_upgrade_is_not_reported_healthy(self) -> None:
        new = {rel: b'new ' + rel.encode() for rel in campaign.FILES}
        actual = modfiles.atomic_write

        def fail_first_text(path: str, data: bytes) -> None:
            if path == campaign.rel_path(self.root, campaign.TXT['CN']):
                raise OSError('interrupted')
            actual(path, data)

        with patch.object(modfiles, 'atomic_write', fail_first_text):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                campaign.install(self.root, (new, 147, {}))
        self.assertEqual(modfiles.read(campaign.rel_path(self.root, campaign.LIST)), new[campaign.LIST])
        self.assertEqual(modfiles.read(campaign.rel_path(self.root, campaign.TXT['CN'])), self.files[campaign.TXT['CN']])
        self.assertFalse(campaign.check(self.root))
        campaign.install(self.root, (new, 147, {}))
        self.assertTrue(campaign.check(self.root))
        self.assertFalse(campaign.removal_blocked(self.root))
        self.assertFalse(campaign.remove(self.root)[1])

    def test_pending_manifest_is_not_healthy_even_with_all_new_files(self) -> None:
        manifest = campaign.load_manifest(self.root)
        manifest['files'][campaign.LIST]['also'] = ['previous-hash']
        modfiles.save_json(os.path.join(self.root, 'Mods', campaign.MANIFEST), manifest)
        self.assertFalse(campaign.check(self.root))

    def assert_actual_rows(self, rows: int) -> None:
        lines, key, _ = campaign.row_setting(self.ini.read_text())
        self.assertIsNotNone(key)
        self.assertEqual(int(lines[key].split('=', 1)[1]), rows)
        if sys.platform == 'win32':
            read = ctypes.windll.kernel32.GetPrivateProfileIntW
            read.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_wchar_p]
            read.restype = ctypes.c_uint
            self.assertEqual(read('VehicleCrew', 'EDF5CampaignRows', 0, str(self.ini)), rows)

    def test_case_whitespace_and_other_section_match_win32(self) -> None:
        self.ini.write_text('[vehiclecrew]\n edf5campaignrows = 0\n[Other]\nEDF5CampaignRows=99\n', encoding='utf-8')
        campaign.set_rows(self.root, 147)
        self.assert_actual_rows(147)
        self.assertIn('[Other]\nEDF5CampaignRows=99', self.ini.read_text())
        self.assertTrue(campaign.check(self.root))

    def test_missing_key_inserted_in_vehiclecrew_section(self) -> None:
        self.ini.write_text('[VehicleCrew]\nEnabled=1\n[Other]\nEnabled=1\n', encoding='utf-8')
        campaign.set_rows(self.root, 147)
        self.assert_actual_rows(147)
        self.assertLess(self.ini.read_text().index('EDF5CampaignRows=147'), self.ini.read_text().index('[Other]'))

    def test_missing_section_added(self) -> None:
        self.ini.write_text('[Other]\nEDF5CampaignRows=99\n', encoding='utf-8')
        self.assertFalse(campaign.check(self.root))
        campaign.set_rows(self.root, 147)
        self.assert_actual_rows(147)
        self.assertTrue(campaign.check(self.root))

    def test_normal_remove_resets_actual_key_and_releases_files(self) -> None:
        self.ini.write_text('[vehiclecrew]\n edf5campaignrows = 147\n[Other]\nEnabled=1\n', encoding='utf-8')
        done, kept = campaign.remove(self.root)
        self.assertEqual(len(done), len(campaign.FILES))
        self.assertEqual(kept, [])
        self.assert_actual_rows(0)
        self.assertFalse(campaign.installed(self.root))


if __name__ == '__main__':
    unittest.main()
