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
        # The installer run from source queues for the game (pylib/gamelease.py): this scratch game's queue stays here.
        queue = patch.dict(os.environ, {'EDF6_QUEUE_DIR': os.path.join(self.root, 'queues')})
        queue.start()
        self.addCleanup(queue.stop)
        running = patch.object(modfiles, 'refuse_while_running', lambda: None)
        running.start()
        self.addCleanup(running.stop)
        self.ini = Path(self.root, 'Mods', campaign.INI)
        modfiles.atomic_write(str(self.ini), b'[VehicleCrew]\nEDF5CampaignContent=0\nTestRangeContent=0\n')
        self.files = {rel: b'old ' + rel.encode() for rel in campaign.FILES}
        campaign.install(self.root, self.built())

    def built(self, with_campaign: bool = True, test_range: bool = True, files: dict[str, bytes] | None = None) -> tuple:
        """make_edf5_campaign.build's result for these packs over stand-in files (no Root.cpk here): the tables, each
        chosen pack's files, the ids the real build gives (EDF5's 65..67, the range's 68 with or without them)."""
        files = self.files if files is None else files
        packs = (*(campaign.PACKS if with_campaign else ()), *((campaign.RANGE,) if test_range else ()))
        rels = {campaign.CONFIG, campaign.PACKS_FILE, *campaign.TEXTS.values(), *(f for pack in packs for f in pack.files())}
        ids = campaign.Contents(65 if with_campaign else 0, 68 if test_range else 0)
        # In build()'s order: the mode table first.
        return {rel: files[rel] for rel in campaign.FILES if rel in rels}, ids, {'rows': [], 'skipped': []}

    def stub_build(self):
        """campaign.build answered by built() (its game reads are not this test's)."""
        return patch.object(campaign, 'build', side_effect=lambda root, campaign=True, test_range=True, root41=None, skip41=():
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
                                'build_asset': None, 'find_edf41': None}.items():   # a stand-in game: no EDF4.1
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
            build.assert_called_once_with(self.root, campaign=True, root41=None, skip41={})
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
            build.assert_called_once_with(self.root, campaign=True, root41=None, skip41={})
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
            build.assert_called_once_with(self.root, campaign=False, root41=None, skip41={})
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
            build.assert_called_once_with(self.root, campaign=False, root41=None, skip41={})
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
        self.assertIn('TestRangeContent=68', self.ini.read_text())   # the range keeps its id without the campaign
        with self.pack() as build:
            installer.install(self.root)
            build.assert_called_once_with(self.root, campaign=False, root41=None, skip41={})
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
            build.assert_called_once_with(self.root, campaign=False, root41=None, skip41={})
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
        self.assertIn('EDF5CampaignContent=65', self.ini.read_text())
        self.assertIn('TestRangeContent=68', self.ini.read_text())

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
        campaign.set_content(self.root, campaign.Contents(65, 68))
        self.assert_actual_content(65, 68)
        self.assertIn('[Other]\nEDF5CampaignContent=99', self.ini.read_text())
        self.assertTrue(campaign.check(self.root))

    def test_missing_key_inserted_in_vehiclecrew_section(self) -> None:
        self.ini.write_text('[VehicleCrew]\nEnabled=1\n[Other]\nEnabled=1\n', encoding='utf-8')
        campaign.set_content(self.root, campaign.Contents(65, 68))
        self.assert_actual_content(65, 68)
        self.assertLess(self.ini.read_text().index('EDF5CampaignContent=65'), self.ini.read_text().index('[Other]'))
        self.assertLess(self.ini.read_text().index('TestRangeContent=68'), self.ini.read_text().index('[Other]'))

    def test_missing_section_added(self) -> None:
        self.ini.write_text('[Other]\nEDF5CampaignContent=99\n', encoding='utf-8')
        self.assertFalse(campaign.check(self.root))
        campaign.set_content(self.root, campaign.Contents(65, 68))
        self.assert_actual_content(65, 68)
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
                self.assertEqual((manifest['version'], manifest['content']), (2, 65))
                text = self.ini.read_text()
                self.assertIn('EDF5CampaignContent=65', text)
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
        (a guest who opted out joins the host's range, not the story's M01), and the campaign's stay 65..67."""
        config = sgo.write_depth_first(0, {'ModeList': [stock_mode(c, o) for c in (0, 1, 2) for o in (0, 1)]})
        _, full = campaign.pack_config(config, (*campaign.PACKS, campaign.RANGE))
        _, alone = campaign.pack_config(config, (campaign.RANGE,))
        _, story = campaign.pack_config(config, campaign.PACKS)
        self.assertEqual([full[p.tag] for p in campaign.PACKS], [65, 66, 67])
        self.assertEqual(alone, {campaign.RANGE.tag: full[campaign.RANGE.tag]})
        self.assertEqual(story, {p.tag: full[p.tag] for p in campaign.PACKS})
        table = campaign.modes(sgo.read(campaign.pack_config(config, (campaign.RANGE,))[0])[1])
        self.assertEqual({int(e[campaign.M_CONTENT]) for e in table[6:]}, {full[campaign.RANGE.tag]})

    def test_content_ids_do_not_depend_on_other_mods(self) -> None:
        """2026-10-15 (docs/feedback-2026-10-15-room-join.md): the ids were the table's highest + 1, so a machine with
        another mod's modes (3, 7: DEFP_MOD1.MST) gave every pack other ids than one without, and the guest did not
        own the host's pack (「未持有此内容」). They are fixed now; a mod already on one of them refuses, saying which."""
        plain = sgo.write_depth_first(0, {'ModeList': [stock_mode(c, o) for c in (0, 1, 2) for o in (0, 1)]})
        modded = sgo.write_depth_first(0, {'ModeList': [stock_mode(c, o) for c in (0, 1, 2) for o in (0, 1)]
                                           + [stock_mode(3, 0, 'MOD1.MST'), stock_mode(7, 1, 'MOD3.MST')]})
        every = campaign.SLOTS
        self.assertEqual(campaign.pack_config(plain, every)[1], campaign.pack_config(modded, every)[1])
        self.assertEqual(campaign.pack_config(modded, every)[1], campaign.CONTENT)
        self.assertEqual(sorted(campaign.CONTENT.values()), list(range(65, 72)))
        clash = sgo.write_depth_first(0, {'ModeList': [stock_mode(c, o) for c in (0, 1, 2) for o in (0, 1)]
                                          + [stock_mode(69, 0, 'MOD9.MST')]})
        with self.assertRaises(campaign.Refused) as refused:
            campaign.pack_config(clash, (campaign.RANGE,))   # the whole reserved run, not only the packs installed
        self.assertIn('69（STORY）', str(refused.exception))
        self.assertIn('65..71', str(refused.exception))

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


def stock_mode(content: int, online: int, mst: str = '') -> list:
    """A ModeList entry like the stock ones (members as docs/mission-list-re.md §7 lists them)."""
    return ['STORY', 'DESC', 0, 0, 0, mst or f'M{content}{online}.MST', ['app:/mission/missionlist.sgo'], 0, online,
            content, online]


TANK = 'OBJECT/VEHICLE301_TANK_AI.SGO'   # the object the real game faulted on (M515, M190), 2026-10-13
ANTHILL = 'OBJECT/ANTHILL301.SGO'


def bvm(*refs: str) -> bytes:
    """A mission script stand-in: the app:/ files it names, as the game's scripts hold them (UTF-16 strings)."""
    return b''.join(f'app:/{ref}'.encode('utf-16-le') for ref in refs)


# id -> (MISSION.BVM refs, BEFORE.BVM refs or None for a mission without one, like M515)
MISSIONS41 = {
    'M001': (('Object/Vehicle301_tank_ai.sgo',), None),
    'M002': (('Object/ANTHILL301.SGO',), None),
    'M003': (('Map/NW_KOUSOUBLD01.mac', 'Map/NW_KOUSOUBLD01.mac'), None),
    'M004': (('Map/IG_CAVE01.mac',), ('Object/Vehicle301_tank_ai.sgo',)),   # only BEFORE.BVM names the tank
    'M005': (('Map/IG_CAVE01.mac',), None),
}


class Edf41SkipTests(unittest.TestCase):
    """What the EDF4.1 install leaves out takes the missions using it out of the pack (plan41), whichever script of
    the mission names it; the installer rebuilds the pack when a map's conversion fails."""

    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory(prefix='edf6-edf41-skip-')
        self.addCleanup(temp.cleanup)
        self.root = temp.name   # no archives here: only the 4.1 objects and maps are "installed"
        os.makedirs(os.path.join(self.root, 'Mods'))
        text = {'missions': [{'id': mid, 'files': ['MISSION.BVM'] + (['BEFORE.BVM'] if before else []),
                              'title': {}, 'brief': {}} for mid, (_m, before) in MISSIONS41.items()]}

        def script(mid: str, name: str, data: str = campaign.EDF41) -> bytes:
            mission, before = MISSIONS41[mid]
            return bvm(*(mission if name == 'MISSION.BVM' else before))

        def missions(data: str = campaign.EDF41) -> dict:   # the story's missions; the DLC packs have none here
            return text if data == campaign.EDF41 else {'missions': []}

        for target, value in (('edf41_text', missions), ('edf41_script', script)):
            started = patch.object(campaign, target, value)
            started.start()
            self.addCleanup(started.stop)

    def planned(self, skip41: object = frozenset()) -> tuple[list[str], dict[str, str]]:
        """The missions this machine can play (the rest are placeholder rows), and why each other one is not."""
        rows, skipped = campaign.plan41(self.root, set(), 'edf41', skip41)   # type: ignore[arg-type]
        self.assertEqual([r['path'] for r in rows], list(MISSIONS41), 'a row left out: the rows after it moved')
        self.assertEqual({r['path'] for r in rows if not campaign.available(r)}, {path for _g, path, _w in skipped})
        return [r['path'] for r in rows if campaign.available(r)], {path: why for _g, path, why in skipped}

    def test_everything_installed_offers_every_mission(self) -> None:
        self.assertEqual(self.planned()[0], list(MISSIONS41))

    def test_two_machines_agree_on_every_room_row(self) -> None:
        """A room publishes only (DLC, MISSION_NO): two machines must map each to the same mission. Machine A has
        every 4.1 map and no other mod; machine B has another mod's modes and failed to convert NW_KOUSOUBLD01 (M003).
        Before 2026-10-18 B's ids were others (the table's highest + 1) and its M004 sat at A's M003's row: the guest
        entered another mission than the host (or none). Now both map every (id, row) alike, B's M003 a placeholder
        row; the definition digest is the same, only B's Missing mask differs."""
        def machine(config_modes: list, skip41: object) -> tuple[dict[tuple[int, int], str], list[dict]]:
            config = sgo.write_depth_first(0, {'ModeList': config_modes})
            _, ids = campaign.pack_config(config, campaign.SLOTS)
            rows, _ = campaign.plan41(self.root, set(), 'edf41', skip41)   # type: ignore[arg-type]
            group = {p41.pack.group: p41.pack.tag for p41 in campaign.PACKS41}
            by_pack: dict[str, list[dict]] = {}
            for r in rows:
                by_pack.setdefault(group[r['group']], []).append(r)
            return {(ids[tag], i): r['key'] for tag, pack_rows in by_pack.items()
                    for i, r in enumerate(pack_rows)}, rows
        stock = [stock_mode(c, o) for c in (0, 1, 2) for o in (0, 1)]
        a, rows_a = machine(stock, frozenset())
        b, rows_b = machine(stock + [stock_mode(3, 0, 'MOD1.MST'), stock_mode(4, 1, 'MOD1.MST')],
                            {'NW_KOUSOUBLD01': '转换失败：x'})
        self.assertEqual(a, b)
        self.assertEqual(a[(campaign.CONTENT['EDF41'], 3)], 'EDF41/M004')
        held = [r['path'] for r in rows_b if not campaign.available(r)]
        self.assertEqual(held, ['M003'])
        self.assertEqual(campaign.pack_digest(campaign.PACK41, rows_a), campaign.pack_digest(campaign.PACK41, rows_b))
        self.assertEqual((campaign.missing_mask(rows_a), campaign.missing_mask(rows_b)), ('00', '40'))
        # the list's successors skip the placeholder: clearing M002 opens M004, nothing ever opens M003
        self.assertEqual(campaign.successors(rows_b), [[1], [3], [3], [4], []])

    def test_object_left_out_removes_its_missions_only(self) -> None:
        paths, skipped = self.planned({TANK: "another mod's file in Mods: OBJECT/V505_TANKEDF4.MRAB"})
        self.assertEqual(paths, ['M002', 'M003', 'M005'])
        self.assertIn('V505_TANKEDF4.MRAB', skipped['M001'])   # says which object, and why
        self.assertIn('未安装', skipped['M001'])

    def test_object_made_unavailable_by_a_foreign_file(self) -> None:
        import edf41_objects
        theirs = 'OBJECT/V505_TANKEDF4.MRAB'
        modfiles.atomic_write(os.path.join(self.root, 'Mods', *theirs.split('/')), b'another mod')
        left_out = dict(edf41_objects.unavailable(self.root))
        self.assertIn(TANK, left_out)
        self.assertEqual(left_out.keys(), dict(edf41_objects.build(self.root)[1]).keys())   # build skips the same
        self.assertEqual(self.planned(campaign.unavailable41(self.root, 'edf41'))[0], ['M002', 'M003', 'M005'])

    def test_map_left_out_removes_its_missions_only(self) -> None:
        paths, skipped = self.planned({'NW_KOUSOUBLD01': '转换失败：x'})
        self.assertEqual(paths, ['M001', 'M002', 'M004', 'M005'])
        self.assertIn('NW_KOUSOUBLD01', skipped['M003'])
        self.assertIn('转换失败', skipped['M003'])

    def test_map_made_unavailable_by_a_foreign_file(self) -> None:
        import edf41_maps
        self.assertEqual(edf41_maps.unavailable(self.root, None)[0][0], next(iter(edf41_maps.MAPS)))
        self.assertEqual(edf41_maps.unavailable(self.root, 'edf41'), [])
        theirs = edf41_maps.files_of('NW_KOUSOUBLD01')[0]
        modfiles.atomic_write(os.path.join(self.root, 'Mods', *theirs.split('/')), b'another mod')
        self.assertEqual([name for name, _ in edf41_maps.unavailable(self.root, 'edf41')], ['NW_KOUSOUBLD01'])
        self.assertEqual(self.planned(campaign.unavailable41(self.root, 'edf41'))[0], ['M001', 'M002', 'M004', 'M005'])

    def test_asset_only_before_bvm_names_counts(self) -> None:
        paths, skipped = self.planned({TANK: ''})
        self.assertNotIn('M004', paths)
        self.assertIn('M004', skipped)

    def test_failed_map_conversion_rebuilds_the_pack_without_its_missions(self) -> None:
        """The installer, on a stand-in game: the pack is built before the maps convert; NW_KOUSOUBLD01 fails, and
        the pack that is written has no mission of it."""
        import edf41_maps
        calls: list[object] = []

        def build(root: str, campaign: bool = True, test_range: bool = True, root41: str | None = None,
                  skip41: object = frozenset()) -> tuple:
            calls.append(set(skip41))   # type: ignore[call-overload]
            paths, _ = self.planned(skip41)
            built = list(pack_case.built(campaign, test_range))
            built[2] = {'rows': [{'group': 'edf41', 'path': p} for p in paths], 'skipped': []}
            return tuple(built)

        installed: list[tuple] = []
        pack_case = CampaignLifecycleTests('test_default_install_enables_campaign')
        pack_case.setUp()
        self.addCleanup(pack_case.doCleanups)
        with pack_case.pack(),                 patch.object(installer, 'find_edf41', return_value='edf41'),                 patch.object(edf41_maps, 'install', return_value=([], [('NW_KOUSOUBLD01', '转换失败：x')])),                 patch.object(campaign, 'build', side_effect=build),                 patch.object(campaign, 'install', side_effect=lambda game, built: installed.append(built) or []):
            installer.install(pack_case.root)
        self.assertEqual(len(calls), 2, calls)
        self.assertNotIn('NW_KOUSOUBLD01', calls[0])
        self.assertIn('NW_KOUSOUBLD01', calls[1])
        self.assertEqual(len(installed), 1)
        self.assertEqual([r['path'] for r in installed[0][2]['rows']], ['M001', 'M002', 'M004', 'M005'])


class PacksFileTests(unittest.TestCase):
    def test_plugin_reads_what_the_installer_writes(self) -> None:
        """EDF6VehicleCrew.packs.ini as the plugin reads it (src/plugin.cpp LoadMissionPacks: the Win32 profile API on
        [Pack<id>] Tag, Digest, Rows, Missing): every pack's section, the values as written."""
        rows = [{'key': f'EDF41/M{i:03}'} for i in range(9)]
        rows[2]['missing'] = rows[8]['missing'] = 'x'
        with patch.object(campaign, 'released_layouts', return_value=[]):   # these stand-in rows: no release had them
            data = campaign.packs_ini((campaign.PACK41, campaign.RANGE),
                                      {campaign.PACK41.group: rows, campaign.RANGE.group: [{'key': 'EDF6/R'}]})
        with tempfile.TemporaryDirectory(prefix='edf6-packs-ini-') as d:
            path = os.path.join(d, 'EDF6VehicleCrew.packs.ini')
            with open(path, 'wb') as f:
                f.write(data)
            text = ctypes.windll.kernel32.GetPrivateProfileStringW
            text.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint,
                             ctypes.c_wchar_p]
            number = ctypes.windll.kernel32.GetPrivateProfileIntW
            number.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_wchar_p]
            number.restype = ctypes.c_uint

            def read(section: str, key: str) -> str:
                out = ctypes.create_unicode_buffer(256)
                text(section, key, '', out, 256, path)
                return out.value
            self.assertEqual(read('Pack69', 'Tag'), 'EDF41')
            self.assertEqual(read('Pack69', 'Digest'), campaign.pack_digest(campaign.PACK41, rows))
            self.assertEqual(len(read('Pack69', 'Digest')), 16)
            self.assertEqual(number('Pack69', 'Rows', 0, path), 9)
            self.assertEqual(read('Pack69', 'Missing'), '401')   # rows 2 and 8
            self.assertEqual((read('Pack68', 'Tag'), read('Pack68', 'Missing')), ('EDF6TR', '0'))
            self.assertEqual(read('Pack65', 'Tag'), '', 'a pack not installed has no section')
            self.assertEqual(read('Pack69', 'Legacy'), '')
            self.assertEqual(read('Pack69', 'Layout'), campaign.layout_id('EDF41', [r['key'] for r in rows]))
            self.assertEqual(len(read('Pack69', 'Layout')), 8)
            self.assertEqual(number('Pack69', 'StableRows', 99, path), 0, 'rows not known stable: none')
            self.assertEqual(number('Pack68', 'StableRows', 99, path), 0)
            self.assertEqual(read('Pack69', 'Older'), '', 'no older release layout of these rows')

    def test_layout_is_the_rows_order_only(self) -> None:
        """The save marker (src/mission_packs.h Pack::layout) is the rows' mission keys in order: a change of a shipped
        script changes the room's definition digest but keeps every save's layout; another order or another pack is
        another layout."""
        a = [{'key': f'EDF41/M{i:03}', 'sha': {'MISSION.BVM': '1'}} for i in range(5)]
        b = [{**r, 'sha': {'MISSION.BVM': '2'}} for r in a]
        keys = [r['key'] for r in a]
        self.assertNotEqual(campaign.pack_digest(campaign.PACK41, a), campaign.pack_digest(campaign.PACK41, b))
        self.assertEqual(campaign.layout_id('EDF41', keys), campaign.layout_id('EDF41', [r['key'] for r in b]))
        self.assertNotEqual(campaign.layout_id('EDF41', keys), campaign.layout_id('EDF41', keys[::-1]))
        self.assertNotEqual(campaign.layout_id('EDF41', keys), campaign.layout_id('EDF41MP01', keys))
        self.assertRegex(campaign.layout_id('EDF41', keys), r'^[0-9a-f]{8}$')

    def test_every_definition_is_a_released_layout(self) -> None:
        """edf5campaign/pack_layouts.json has every pack's layout now as its last (a change of a pack's rows appends
        the new layout, so the saves of the one before get a map to it), every layout of a pack once, and the plugin
        takes as many older layouts as the installer writes (kMaxFrom)."""
        import json
        with open(campaign.LAYOUTS, encoding='utf-8') as f:
            registry = json.load(f)
        for pack in campaign.SLOTS:
            layouts = registry.get(pack.tag)
            self.assertTrue(layouts, pack.tag)
            self.assertEqual(layouts[-1], campaign.definition_keys(pack), f'{pack.tag}: its layout now is not the last released')
            ids = [campaign.layout_id(pack.tag, k) for k in layouts]
            self.assertEqual(len(set(ids)), len(ids), f'{pack.tag}: a layout twice')
        header = open(os.path.join(os.path.dirname(campaign.TEXT), '..', 'src', 'mission_packs.h'), encoding='utf-8').read()
        self.assertIn(f'constexpr int kMaxFrom={campaign.MAX_FROM};', header)

    def test_stable_rows(self) -> None:
        """StableRows: the leading rows of the first released layout that every install had (row 'stable'), up to the
        first that is not or that is no longer at its row."""
        rows = [{'key': f'K{i}', 'stable': True} for i in range(6)]
        rows[3]['stable'] = False
        with patch.object(campaign, 'released_layouts', return_value=[[r['key'] for r in rows]]):
            self.assertEqual(campaign.stable_rows(campaign.PACK41, rows), 3)
        with patch.object(campaign, 'released_layouts', return_value=[['K0', 'K2', 'K1', 'K3']]):
            self.assertEqual(campaign.stable_rows(campaign.PACK41, rows), 1, 'the first layout had K2 at row 1')
        with patch.object(campaign, 'released_layouts', return_value=[]):
            self.assertEqual(campaign.stable_rows(campaign.PACK41, rows), 3, 'no release yet: its own rows')

    def test_older_layouts_written_with_their_maps(self) -> None:
        """A save marked by any older release moves straight to the rows now: every released layout but the one now
        gets Older=<marker>,... and From<marker>=<rows>:<map>, the most recent MAX_FROM (one fewer with Legacy), as
        the plugin reads them (src/plugin.cpp LoadMissionPacks)."""
        rows = [{'key': f'EDF41/M{i}'} for i in range(5)]
        now = [r['key'] for r in rows]
        v1 = ['EDF41/M0', 'EDF41/M1', 'EDF41/M3', 'EDF41/M4']            # M2 added in the second release
        v2 = ['EDF41/M0', 'EDF41/M2', 'EDF41/M1', 'EDF41/M3', 'EDF41/M4']   # M1 and M2 swapped in the third
        with patch.object(campaign, 'released_layouts', return_value=[v1, v2, now]):
            text = campaign.packs_ini((campaign.PACK41,), {campaign.PACK41.group: rows}).decode()
            m1, m2 = campaign.layout_id('EDF41', v1), campaign.layout_id('EDF41', v2)
            self.assertIn(f'\r\nOlder={m1},{m2}\r\n', text)
            self.assertIn(f'\r\nFrom{m1}=4:0,1,3,4\r\n', text)
            self.assertIn(f'\r\nFrom{m2}=5:0,2,1,3,4\r\n', text)
            self.assertIn(f'\r\nLayout={campaign.layout_id("EDF41", now)}\r\n', text)
            many = [['EDF41/M0', f'X{j}'] for j in range(6)] + [now]
            with patch.object(campaign, 'released_layouts', return_value=many):
                plain = campaign.older_layouts(campaign.PACK41, rows, False)
                self.assertEqual(len(plain), campaign.MAX_FROM)
                self.assertEqual([k for _, k in plain], many[-1 - campaign.MAX_FROM:-1], 'the most recent ones')
                self.assertEqual(len(campaign.older_layouts(campaign.PACK41, rows, True)), campaign.MAX_FROM - 1)

    def test_legacy_rows_recorded_kept_and_written(self) -> None:
        """A save's rows are the list's (docs/mission-list-re.md §1): an older install that left M003 out had M004 at
        row 2. The list this tool wrote last is recorded as the pack's legacy layout when it differs (a list that is
        already the definition records nothing), kept in the manifest by every later install and past an uninstall,
        and written as Legacy (each old row's row now) for the plugin to move such a save once."""
        rows = [{'key': f'EDF41/M00{i}'} for i in range(1, 6)]
        rows[2]['missing'] = 'x'
        old = ['EDF41/M001', 'EDF41/M002', 'EDF41/M004', 'EDF41/M005']
        with tempfile.TemporaryDirectory(prefix='edf6-legacy-') as root:
            os.makedirs(os.path.join(root, 'Mods'))
            by_pack = {campaign.PACK41.group: rows}
            with patch.object(campaign, 'installed_keys', return_value=old):
                legacy = campaign.legacy_layouts(root, {}, (campaign.PACK41,), by_pack)
            self.assertEqual(legacy, {'EDF41': old})
            with patch.object(campaign, 'installed_keys', return_value=[r['key'] for r in rows]):
                self.assertEqual(campaign.legacy_layouts(root, {}, (campaign.PACK41,), by_pack), {}, 'already the definition')
                self.assertEqual(campaign.legacy_layouts(root, {'legacy': legacy}, (campaign.PACK41,), by_pack), legacy,
                                 'a recorded layout is kept: a save may not have been loaded since')
            self.assertEqual(campaign.legacy_map(old, rows), [0, 1, 3, 4])
            text = campaign.packs_ini((campaign.PACK41,), by_pack, legacy).decode()
            self.assertIn('\r\nLegacy=4:0,1,3,4\r\n', text)
            # an uninstall keeps it for the next install (the saves stay)
            files = {'X': {'sha': 'nothing', 'original': None}}
            modfiles.save_json(os.path.join(root, 'Mods', campaign.MANIFEST), {'files': files, 'legacy': legacy})
            with patch.object(modfiles, 'refuse_while_running', lambda: None):
                campaign.remove(root)
            self.assertFalse(campaign.installed(root))
            with patch.object(campaign, 'installed_keys', return_value=None):
                self.assertEqual(campaign.legacy_layouts(root, {}, (campaign.PACK41,), by_pack), legacy)


if __name__ == '__main__':
    unittest.main()
