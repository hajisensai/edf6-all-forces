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
import sgo


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
        modfiles.atomic_write(str(self.ini), b'[VehicleCrew]\nEDF5CampaignContent=0\nTestRangeContent=0\n')
        self.files = {rel: b'old ' + rel.encode() for rel in campaign.FILES}
        campaign.install(self.root, self.built())

    def built(self, with_campaign: bool = True, test_range: bool = True, files: dict[str, bytes] | None = None) -> tuple:
        """make_edf5_campaign.build's result for these packs over stand-in files (no Root.cpk here): the tables, each
        chosen pack's files, the ids the real build gives (EDF5's 3..5, the range's 6 with or without them)."""
        files = self.files if files is None else files
        packs = (*(campaign.PACKS if with_campaign else ()), *((campaign.RANGE,) if test_range else ()))
        rels = {campaign.CONFIG, *campaign.TEXTS.values(), *(f for pack in packs for f in pack.files())}
        ids = campaign.Contents(3 if with_campaign else 0, 6 if test_range else 0)
        # In build()'s order: the mode table first.
        return {rel: files[rel] for rel in campaign.FILES if rel in rels}, ids, {'rows': [], 'skipped': []}

    def stub_build(self):
        """campaign.build answered by built() (its game reads are not this test's)."""
        return patch.object(campaign, 'build', side_effect=lambda root, campaign=True, test_range=True:
                            self.built(campaign, test_range))

    @contextmanager
    def pack(self):
        """Run the production installer; only unrelated expensive generators and game reads are stubbed."""
        import buildcache
        import call_weapons
        import gen
        import make_bigmap
        import make_optics
        import make_stock_stores
        import rootcpk
        plugins = {name: (b'new plugin', f'[{section}]\nStockVehicleStores=0\nEDF5CampaignContent=0\nTestRangeContent=0\n'.encode())
                   for name, section in installer.PLUGINS}
        with ExitStack() as stack:
            for name, value in {'check_loader': None, 'plugin_files': plugins, 'stack_weapons': {},
                                'build_autoturret': ({}, False), 'install_autoturret': None,
                                'build_asset': None}.items():
                stack.enter_context(patch.object(installer, name, return_value=value))
            for module, name, value in ((rootcpk, 'use', None), (call_weapons, 'recover', False),
                                        (call_weapons, 'install', []), (gen, 'install', []),
                                        (gen, 'target_range', None), (make_stock_stores, 'wanted', False),
                                        (make_stock_stores, 'remove', ([], [])), (make_bigmap, 'set_big_world', None),
                                        (make_optics, 'build_stock_redirects', {})):
                stack.enter_context(patch.object(module, name, return_value=value))
            stack.enter_context(patch.object(buildcache, 'Cache'))
            build = stack.enter_context(self.stub_build())
            stack.enter_context(redirect_stdout(io.StringIO()))
            yield build

    def test_default_install_enables_campaign(self) -> None:
        campaign.remove(self.root)
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=True)
        self.assertTrue(campaign.enabled(self.root))
        self.assertTrue(campaign.range_installed(self.root))
        self.assertTrue(campaign.check(self.root))

    def test_explicit_enable_replaces_opt_out(self) -> None:
        campaign.remove(self.root, remember_disabled=True)
        self.assertFalse(campaign.wanted(self.root))
        with self.pack() as build:
            installer.install(self.root, campaign_requested=True)
            self.assertTrue(campaign.wanted(self.root))
            installer.install(self.root)
            self.assertEqual(build.call_count, 2)

    def test_refused_explicit_enable_preserves_opt_out(self) -> None:
        campaign.remove(self.root, remember_disabled=True)
        with self.pack(), patch.object(campaign, 'build', side_effect=campaign.Refused('foreign list')):
            installer.install(self.root, campaign_requested=True)
        self.assertFalse(campaign.wanted(self.root))
        self.assertFalse(campaign.enabled(self.root))

    def test_interrupted_enable_is_repaired_by_normal_update(self) -> None:
        campaign.remove(self.root, remember_disabled=True)
        write = modfiles.atomic_write

        def fail_text(path: str, data: bytes) -> None:
            if path == campaign.rel_path(self.root, campaign.TEXTS['CN']):
                raise OSError('disk full')
            write(path, data)

        with patch.object(modfiles, 'atomic_write', side_effect=fail_text), self.assertRaises(OSError):
            campaign.install(self.root, self.built())
        self.assertTrue(campaign.wanted(self.root))
        self.assertFalse(campaign.check(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=True)
        self.assertTrue(campaign.check(self.root))

    def test_interrupted_disable_finishes_on_normal_update(self) -> None:
        remove = os.remove

        def fail_image(path: str) -> None:
            if path == campaign.rel_path(self.root, campaign.PACKS[0].image):
                raise OSError('disk unavailable')
            remove(path)

        with patch.object(os, 'remove', side_effect=fail_image), self.stub_build(), self.assertRaises(OSError):
            campaign.disable(self.root)
        self.assertFalse(campaign.wanted(self.root))
        self.assertFalse(campaign.check(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=False)
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.range_installed(self.root))
        self.assertTrue(campaign.check(self.root))
        self.assertFalse(Path(campaign.rel_path(self.root, campaign.PACKS[0].image)).exists())

    def test_refused_rebuild_blocks_interrupted_disable_recovery(self) -> None:
        modfiles.atomic_write(os.path.join(self.root, 'Mods', campaign.DISABLED), b'disabled')
        with self.pack(), patch.object(campaign, 'build', side_effect=campaign.Refused('foreign changed list')), \
                patch.object(installer, 'install_plugin') as plugin, self.assertRaises(campaign.Refused):
            installer.install(self.root)
        plugin.assert_not_called()
        self.assertTrue(all(Path(campaign.rel_path(self.root, rel)).exists() for rel in campaign.FILES))

    def test_interrupted_disable_recognizes_already_restored_originals(self) -> None:
        campaign.remove(self.root)
        originals = {rel: b'foreign original ' + rel.encode() for rel in campaign.FILES}
        for rel, data in originals.items():
            modfiles.atomic_write(campaign.rel_path(self.root, rel), data)
        campaign.install(self.root, self.built())
        write = modfiles.atomic_write

        def fail_image(path: str, data: bytes) -> None:
            if path == campaign.rel_path(self.root, campaign.PACKS[0].image):
                raise OSError('disk unavailable')
            write(path, data)

        with patch.object(modfiles, 'atomic_write', side_effect=fail_image), self.assertRaises(OSError):
            campaign.remove(self.root, remember_disabled=True)
        self.assertEqual(modfiles.read(campaign.rel_path(self.root, campaign.CONFIG)), originals[campaign.CONFIG])
        self.assertFalse(campaign.removal_blocked(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=False)
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.range_installed(self.root))
        for rel in (f for pack in campaign.PACKS for f in pack.files()):   # the campaign's: the originals back
            self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), originals[rel])
        self.assertTrue(campaign.check(self.root))
        campaign.remove(self.root)   # and with the range's pack gone too, every original
        for rel, data in originals.items():
            self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), data)

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
        with patch.object(installer, 'ask', return_value='2'), self.stub_build():
            self.assertEqual(installer.manage_campaign(self.root), 0)
        self.assertEqual(plugin.read_bytes(), b'existing plugin')
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.range_installed(self.root))
        self.assertIn('EDF5CampaignContent=0', self.ini.read_text())
        self.assertIn('TestRangeContent=6', self.ini.read_text())   # the range keeps its id without the campaign
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=False)
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.check(self.root))

    def test_disabled_foreign_image_residue_is_not_enabled_or_unhealthy(self) -> None:
        image = campaign.rel_path(self.root, campaign.PACKS[0].image)
        modfiles.atomic_write(image, b'foreign image')
        with patch.object(installer, 'ask', return_value='2'), self.stub_build():
            self.assertEqual(installer.manage_campaign(self.root), 0)
        self.assertIn(campaign.PACKS[0].image, campaign.load_manifest(self.root)['files'])  # its recovery record stays
        self.assertFalse(campaign.enabled(self.root))
        self.assertTrue(campaign.check(self.root))
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=False)
        self.assertEqual(modfiles.read(image), b'foreign image')
        self.assertFalse(campaign.enabled(self.root))

    def test_disable_changed_list_reports_failure_and_preserves_dependencies(self) -> None:
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.CONFIG), b'foreign appended list')
        # The real build reads the changed table as its base and cannot parse it (Refused, nothing written).
        with patch.object(installer, 'ask', return_value='2'), \
                patch.object(campaign, 'build', side_effect=campaign.Refused('Mods/DEFAULTPACKAGE/CONFIG.SGO 读不了')):
            self.assertEqual(installer.manage_campaign(self.root), 1)
        self.assertTrue(campaign.enabled(self.root))
        self.assertTrue(campaign.wanted(self.root))
        self.assertTrue(all(Path(campaign.rel_path(self.root, rel)).exists() for rel in campaign.FILES))

    def test_changed_list_preserves_all_dependencies_and_recovery_records(self) -> None:
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.CONFIG), b'foreign list retaining appended rows')
        before = {rel: modfiles.read(campaign.rel_path(self.root, rel)) for rel in campaign.FILES}
        manifest = campaign.load_manifest(self.root)
        done, kept = campaign.remove(self.root)
        self.assertEqual(done, [])
        self.assertEqual(len(kept), len(campaign.FILES))
        self.assertEqual(campaign.load_manifest(self.root), manifest)
        for rel, data in before.items():
            self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), data)
        self.assertIn('EDF5CampaignContent=3', self.ini.read_text())
        self.assertIn('TestRangeContent=6', self.ini.read_text())

    def test_pack_uninstall_stops_before_any_other_removal(self) -> None:
        import rootcpk
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.CONFIG), b'foreign appended list')
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
            if path == campaign.rel_path(self.root, campaign.TEXTS['CN']):
                raise OSError('interrupted')
            actual(path, data)

        with patch.object(modfiles, 'atomic_write', fail_first_text):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                campaign.install(self.root, self.built(files=new))
        self.assertEqual(modfiles.read(campaign.rel_path(self.root, campaign.CONFIG)), new[campaign.CONFIG])
        self.assertEqual(modfiles.read(campaign.rel_path(self.root, campaign.TEXTS['CN'])), self.files[campaign.TEXTS['CN']])
        self.assertFalse(campaign.check(self.root))
        campaign.install(self.root, self.built(files=new))
        self.assertTrue(campaign.check(self.root))
        self.assertFalse(campaign.removal_blocked(self.root))
        self.assertFalse(campaign.remove(self.root)[1])

    def test_pending_manifest_is_not_healthy_even_with_all_new_files(self) -> None:
        manifest = campaign.load_manifest(self.root)
        manifest['files'][campaign.CONFIG]['also'] = ['previous-hash']
        modfiles.save_json(os.path.join(self.root, 'Mods', campaign.MANIFEST), manifest)
        self.assertFalse(campaign.check(self.root))

    def assert_actual_content(self, value: int, test_range: int | None = None) -> None:
        for name, want in ((campaign.INI_KEY, value), (campaign.RANGE_INI_KEY, test_range)):
            if want is None:
                continue
            lines, key, _ = campaign.key_setting(self.ini.read_text(), name)
            self.assertIsNotNone(key)
            self.assertEqual(int(lines[key].split('=', 1)[1]), want)
            if sys.platform == 'win32':
                read = ctypes.windll.kernel32.GetPrivateProfileIntW
                read.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_wchar_p]
                read.restype = ctypes.c_uint
                self.assertEqual(read('VehicleCrew', name, 0, str(self.ini)), want)

    def test_case_whitespace_and_other_section_match_win32(self) -> None:
        self.ini.write_text('[vehiclecrew]\n edf5campaigncontent = 0\n[Other]\nEDF5CampaignContent=99\n', encoding='utf-8')
        campaign.set_content(self.root, campaign.Contents(3, 6))
        self.assert_actual_content(3, 6)
        self.assertIn('[Other]\nEDF5CampaignContent=99', self.ini.read_text())
        self.assertTrue(campaign.check(self.root))

    def test_missing_key_inserted_in_vehiclecrew_section(self) -> None:
        self.ini.write_text('[VehicleCrew]\nEnabled=1\n[Other]\nEnabled=1\n', encoding='utf-8')
        campaign.set_content(self.root, campaign.Contents(3, 6))
        self.assert_actual_content(3, 6)
        self.assertLess(self.ini.read_text().index('EDF5CampaignContent=3'), self.ini.read_text().index('[Other]'))
        self.assertLess(self.ini.read_text().index('TestRangeContent=6'), self.ini.read_text().index('[Other]'))

    def test_missing_section_added(self) -> None:
        self.ini.write_text('[Other]\nEDF5CampaignContent=99\n', encoding='utf-8')
        self.assertFalse(campaign.check(self.root))
        campaign.set_content(self.root, campaign.Contents(3, 6))
        self.assert_actual_content(3, 6)
        self.assertTrue(campaign.check(self.root))

    def test_normal_remove_resets_actual_key_and_releases_files(self) -> None:
        self.ini.write_text('[vehiclecrew]\n edf5campaigncontent = 3\n testrangecontent = 6\n[Other]\nEnabled=1\n',
                            encoding='utf-8')
        done, kept = campaign.remove(self.root)
        self.assertEqual(len(done), len(campaign.FILES))
        self.assertEqual(kept, [])
        self.assert_actual_content(0, 0)
        self.assertFalse(campaign.installed(self.root))


    def legacy_install(self, foreign: bool) -> dict[str, bytes]:
        """The 2026-10-07 version's install: EDF6's offline list, texts and thumbnails replaced (over another mod's
        files when `foreign`), its manifest (version 1) and row cap."""
        import base64
        campaign.remove(self.root)
        originals = {rel: b'foreign ' + rel.encode() for rel in campaign.LEGACY} if foreign else {}
        files = {}
        for rel in campaign.LEGACY:
            data = b'appended ' + rel.encode()
            modfiles.atomic_write(campaign.rel_path(self.root, rel), data)
            original = originals.get(rel)
            files[rel] = {'sha': modfiles.sha256(data),
                          'original': None if original is None else base64.b64encode(original).decode('ascii')}
        modfiles.save_json(os.path.join(self.root, 'Mods', campaign.MANIFEST), {'version': 1, 'rows': 147, 'files': files})
        self.ini.write_text('[VehicleCrew]\nEDF5CampaignRows=147\n', encoding='utf-8')
        return originals

    def test_upgrade_puts_the_appended_list_back(self) -> None:
        for foreign in (False, True):
            with self.subTest(foreign=foreign):
                originals = self.legacy_install(foreign)
                self.assertTrue(campaign.enabled(self.root))
                self.assertFalse(campaign.check(self.root), 'the old version is not a healthy install')
                campaign.install(self.root, self.built())
                for rel in campaign.LEGACY:
                    self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), originals.get(rel))
                manifest = campaign.load_manifest(self.root)
                self.assertEqual(sorted(manifest['files']), sorted(campaign.FILES))
                self.assertEqual((manifest['version'], manifest['content']), (2, 3))
                text = self.ini.read_text()
                self.assertIn('EDF5CampaignContent=3', text)
                self.assertIn('EDF5CampaignRows=0', text, 'the old row cap left in force')
                self.assertTrue(campaign.check(self.root))

    def test_interrupted_upgrade_still_puts_the_appended_list_back(self) -> None:
        originals = self.legacy_install(True)
        write = modfiles.atomic_write

        def fail_text(path: str, data: bytes) -> None:
            if path == campaign.rel_path(self.root, campaign.TEXTS['CN']):
                raise OSError('disk full')
            write(path, data)

        with patch.object(modfiles, 'atomic_write', side_effect=fail_text), self.assertRaises(OSError):
            campaign.install(self.root, self.built())
        self.assertTrue(set(campaign.LEGACY) <= set(campaign.load_manifest(self.root)['files']), 'what to put back lost')
        campaign.install(self.root, self.built())
        for rel in campaign.LEGACY:
            self.assertEqual(modfiles.read(campaign.rel_path(self.root, rel)), originals[rel])
        self.assertTrue(campaign.check(self.root))

    def test_changed_appended_list_refuses_upgrade_and_removal(self) -> None:
        self.legacy_install(False)
        modfiles.atomic_write(campaign.rel_path(self.root, campaign.LEGACY_LIST), b'someone else appended more')
        with self.assertRaises(campaign.Refused):
            campaign.build(self.root)
        self.assertTrue(campaign.removal_blocked(self.root))
        done, kept = campaign.remove(self.root)
        self.assertEqual(done, [])
        self.assertEqual(len(kept), len(campaign.LEGACY))

    def test_packs_have_their_own_files_and_saves(self) -> None:
        packs = (*campaign.PACKS, campaign.RANGE)
        names = [f for p in packs for f in p.files()]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(len({p.mst.upper() for p in packs}), 4)
        self.assertFalse({p.mst.upper() for p in packs} & {'M00.MST', 'DLC1.MST', 'DLC2.MST'})
        self.assertFalse(set(names) & set(campaign.LEGACY), 'a pack writes EDF6 own offline list')
        self.assertEqual([p.group for p in packs], ['main', 'dlc1', 'dlc2', 'range'])
        self.assertEqual(campaign.RANGE.kinds, (campaign.OFFLINE, campaign.ONLINE))
        for p in packs:
            self.assertEqual(set(p.name), set(campaign.LANGS))
            self.assertEqual(set(p.desc), set(campaign.LANGS))
        for field in ('title', 'brief'):
            self.assertEqual(set(campaign.RANGE_ROW[field]), set(campaign.LANGS))

    def test_content_ids_do_not_depend_on_the_packs_installed(self) -> None:
        """A room's mode is its content id: the range's is the same whether or not the player has the EDF5 campaign
        (a guest who opted out joins the host's range, not the story's M01), and the campaign's stay 3..5."""
        def stock(content: int, online: int) -> list:
            return ['STORY', 'DESC', 0, 0, 0, f'M{content}{online}.MST', ['app:/mission/missionlist.sgo'], 0, online,
                    content, online]
        config = sgo.write_depth_first(0, {'ModeList': [stock(c, o) for c in (0, 1, 2) for o in (0, 1)]})
        _, full = campaign.pack_config(config, (*campaign.PACKS, campaign.RANGE))
        _, alone = campaign.pack_config(config, (campaign.RANGE,))
        _, story = campaign.pack_config(config, campaign.PACKS)
        self.assertEqual([full[p.tag] for p in campaign.PACKS], [3, 4, 5])
        self.assertEqual(alone, {campaign.RANGE.tag: full[campaign.RANGE.tag]})
        self.assertEqual(story, {p.tag: full[p.tag] for p in campaign.PACKS})
        table = campaign.modes(sgo.read(campaign.pack_config(config, (campaign.RANGE,))[0])[1])
        self.assertEqual({int(e[campaign.M_CONTENT]) for e in table[6:]}, {full[campaign.RANGE.tag]})

    def test_range_pack_and_mission_go_together(self) -> None:
        """testrange/gen.py: the pack is registered with the mission and taken out before it (a pack naming a missing
        folder would be a menu entry the game cannot load); the campaign's packs stay as they are."""
        import gen
        with self.stub_build():
            campaign.set_packs(self.root, True, False)
            self.assertFalse(campaign.range_installed(self.root))
            self.assertTrue(campaign.enabled(self.root))
            gen.register_pack(self.root)
            self.assertTrue(campaign.range_installed(self.root))
            self.assertTrue(campaign.enabled(self.root))
            gen.uninstall(self.root)
            self.assertFalse(campaign.range_installed(self.root))
            self.assertTrue(campaign.enabled(self.root))
            self.assertIn('TestRangeContent=0', self.ini.read_text())
            campaign.disable(self.root)   # no pack left: the tables put back as they were
            self.assertFalse(campaign.installed(self.root))
        with self.assertRaises(ValueError):
            campaign.build(self.root, False, False)

    def test_legacy_slot_range_is_taken_out_of_the_story(self) -> None:
        """A range from before 2026-10-09 over RM015 (its script never ends: the story stopped there) goes on install
        and on uninstall; a folder another mod put there stays."""
        import gen
        for mission in gen.LEGACY_SLOTS:
            modfiles.atomic_write(os.path.join(gen.mission_dir(self.root, mission), gen.MARKER), b'range')
        other = os.path.join(gen.mission_dir(self.root, 'RM015_3'), 'MISSION.AC')
        modfiles.atomic_write(other, b'another mod')
        gen.uninstall(self.root, unregister=False)
        self.assertFalse(any(gen.ours(self.root, m) for m in gen.LEGACY_SLOTS))
        self.assertTrue(os.path.isfile(other))


if __name__ == '__main__':
    unittest.main()
