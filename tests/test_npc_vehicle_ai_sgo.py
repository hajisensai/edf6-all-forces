"""Stock NPC aim preparation uses the real stock writer and only temporary game folders."""
from __future__ import annotations

import copy
from dataclasses import replace
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import ledger
import make_optics
import mdb
import modfiles
import dsgo
import rootcpk
import sgo
import vehicle_optics

OBJECT = 'V607_ROBOTRUCK.SGO'
REL = 'OBJECT/' + OBJECT
MODEL = 'OBJECT/EDF6VC_OPTIC_V607_ROBOTRUCK.MRAB'
KEY = 'v607_aim_info'
DONORS = ('V607_ROBOTRUCK_AI.SGO', 'V607_ROBOTRUCK_F_AI.SGO',
          'V607_ROBOTRUCK_G_AI.SGO', 'V607_ROBOTRUCK_M_AI.SGO')
AIM = [[0.1, 'robo_kawan_l', 'lefthand_aim_root', '左手射線チェック', 1.0],
       [0.1, 'robo_weaponShoulder_r', 'rightsholder_aim_root', 0.0, 0.0]]


def stock() -> dict:
    return {'xgs_scene_object_class': 'Vehicle607_RoboTruck',
            'animation_model': [['app:/object/v607_robotruck.mrab', 'v607_robotruck.mdb'],
                                'app:/object/v607_robotruck.cas', b'official MAB locator data'],
            'car_aim_node': ['body', 'robo_spine', 'robo_body'],
            'vehicle_weapon_setting': [['robo_kawan_l', 0.0], ['robo_weaponShoulder_r', 0.0]],
            'vehicle_setup': [[1.0, 1.0], [['app:/weapon/player_flame.sgo'], ['app:/weapon/player_missile.sgo']]],
            'third_party': [12.0, 'keep existing fields']}


class StockAimInstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix='edf6-stock-aim-')
        self.addCleanup(self.temp.cleanup)
        self.root = self.temp.name
        self.source = sgo.write(0x102, stock())
        self.geometry = b'audited source MDB bytes'
        self.archive = self.model_archive(self.geometry)
        self.donors = {name: sgo.write(0x102, stock() | {KEY: copy.deepcopy(AIM)}) for name in DONORS}
        case = self

        class Game:
            def names(self, folder):
                return [OBJECT] if folder == 'OBJECT' else []

            def read(self, folder, name):
                if name.upper() == 'V607_ROBOTRUCK.MRAB':
                    return case.archive
                return case.source if name.upper() == OBJECT else case.donors[name.upper()]

        self.game = Game()
        for change in (patch.object(make_optics.rootcpk, 'Game', return_value=self.game),
                       patch.object(vehicle_optics.cockpits, 'assets_present', return_value=False),
                       patch.object(vehicle_optics, 'MODELS', tuple(
                           replace(spec, digest=modfiles.sha256(self.geometry)) if spec.stem == 'V607_ROBOTRUCK' else spec
                           for spec in vehicle_optics.MODELS))):
            change.start()
            self.addCleanup(change.stop)

    def path(self, rel=REL) -> Path:
        return Path(self.root, 'Mods', *rel.split('/'))

    @staticmethod
    def model_archive(geometry: bytes, texture=b'custom texture') -> bytes:
        return mdb.rab_write(mdb.Rab(0x100, ['TEXTURE', 'MODEL'], [
            mdb.RabFile('body.dds', 0, 0, texture), mdb.RabFile('v607_robotruck.mdb', 1, 0, geometry)]))

    def put(self, data: bytes) -> None:
        self.path().parent.mkdir(parents=True, exist_ok=True)
        self.path().write_bytes(data)

    def values(self) -> dict:
        return sgo.load(data=self.path().read_bytes())

    def install(self) -> dict[str, bytes]:
        files = make_optics.build_stock_redirects(self.root)
        make_optics.install_stock_redirects(self.root, files)
        return files

    def test_without_cockpit_assets_the_stock_pass_still_prepares_native_aim(self) -> None:
        files = self.install()
        self.assertIn(REL, files)
        self.assertEqual(self.values()[KEY], sgo.load(data=self.donors[DONORS[0]])[KEY])
        self.assertEqual(self.values()['vehicle_setup'], sgo.load(data=self.source)['vehicle_setup'])
        self.assertEqual(ledger.Ledger(self.root).owners(REL), [make_optics.OWNER])
        self.assertFalse(self.path(MODEL).exists())

    def test_two_installs_keep_the_ai_only_claim_and_uninstall_removes_our_new_file(self) -> None:
        self.install()
        first = self.path().read_bytes() if self.path().exists() else None
        second = self.install()
        self.assertIn(REL, second, 'an existing AI-only claim must not become an obsolete stock output')
        self.assertEqual(self.path().read_bytes(), first)
        self.assertEqual(ledger.Ledger(self.root).owners(REL), [make_optics.OWNER])
        make_optics.remove(self.root)
        self.assertFalse(self.path().exists())

    def test_existing_mod_fields_and_player_weapons_survive_install_and_exact_undo(self) -> None:
        values = stock()
        values['vehicle_setup'] = [['my durability'], [['app:/weapon/custom.sgo']]]
        values['custom_tuning'] = [987.0, b'opaque mod data']
        original = sgo.write(0x102, values)
        self.put(original)
        self.install()
        made = self.values()
        self.assertIn(KEY, made)
        del made[KEY]
        self.assertEqual(made, sgo.load(data=original))
        self.install()
        make_optics.remove(self.root)
        self.assertEqual(self.path().read_bytes(), original)

    def test_a_predeclared_table_is_never_overwritten_or_claimed_without_optics(self) -> None:
        original = sgo.write(0x102, stock() | {KEY: ['user table']})
        self.put(original)
        self.assertNotIn(REL, self.install())
        self.assertEqual(self.path().read_bytes(), original)
        self.assertFalse(ledger.Ledger(self.root).owners(REL))

    def test_custom_model_or_mab_or_mount_contract_is_left_untouched(self) -> None:
        for changed in ('class', 'model', 'mab', 'mount'):
            with self.subTest(changed=changed):
                values = stock()
                if changed == 'class':
                    values['xgs_scene_object_class'] = 'Vehicle_Car'
                elif changed == 'model':
                    values['animation_model'][0][0] = 'app:/object/other_mod.mrab'
                elif changed == 'mab':
                    values['animation_model'][2] = b'custom locator data'
                else:
                    values['vehicle_weapon_setting'][1][0] = 'new weapon mount'
                original = sgo.write(0x102, values)
                self.put(original)
                self.assertNotIn(REL, self.install())
                self.assertEqual(self.path().read_bytes(), original)
                self.assertFalse(ledger.Ledger(self.root).owners(REL))

    def test_another_registered_writer_is_not_overlaid_by_a_second_owner(self) -> None:
        led = ledger.Ledger(self.root)
        led.put('calls', REL, self.source)
        self.assertNotIn(REL, self.install())
        self.assertEqual(self.path().read_bytes(), self.source)
        self.assertEqual(ledger.Ledger(self.root).owners(REL), ['calls'])

    def test_same_path_replacement_geometry_is_not_assigned_the_official_table(self) -> None:
        model = self.path('OBJECT/V607_ROBOTRUCK.MRAB')
        model.parent.mkdir(parents=True, exist_ok=True)
        for archive in (self.model_archive(b'foreign skeleton'), b'unknown archive'):
            with self.subTest(archive=archive[:10]):
                model.write_bytes(archive)
                self.assertNotIn(REL, self.install())
                self.assertFalse(self.path().exists())
                self.assertEqual(model.read_bytes(), archive)
                self.assertFalse(ledger.Ledger(self.root).owners(REL))

    def test_same_path_texture_only_changes_keep_the_compatible_geometry(self) -> None:
        model = self.path('OBJECT/V607_ROBOTRUCK.MRAB')
        model.parent.mkdir(parents=True, exist_ok=True)
        archive = self.model_archive(self.geometry, b'user texture replacement')
        model.write_bytes(archive)
        self.install()
        self.assertIn(KEY, self.values())
        self.install()
        make_optics.remove(self.root)
        self.assertFalse(self.path().exists())
        self.assertEqual(model.read_bytes(), archive)

    def test_private_model_name_alone_or_modified_generated_bytes_do_not_prove_geometry(self) -> None:
        for registered in (False, True):
            with self.subTest(registered=registered):
                if registered:
                    make_optics.install_models(self.root, {MODEL: b'our original generated model'})
                self.path(MODEL).parent.mkdir(parents=True, exist_ok=True)
                self.path(MODEL).write_bytes(b'foreign private-name geometry')
                values = stock()
                values['animation_model'][0][0] = 'app:/object/edf6vc_optic_v607_robotruck.mrab'
                self.put(sgo.write(0x102, values))
                self.install()
                self.assertNotIn(KEY, self.values())
                self.assertEqual(self.path(MODEL).read_bytes(), b'foreign private-name geometry')
                make_optics.remove(self.root)
                self.path().unlink()
                self.path(MODEL).unlink()

    def test_reinstall_withdraws_our_table_after_model_mab_mount_or_geometry_changes(self) -> None:
        for changed in ('model', 'mab', 'mount', 'geometry', 'custom_table'):
            with self.subTest(changed=changed):
                self.install()
                values = self.values()
                if changed == 'model':
                    values['animation_model'][0][0] = 'app:/object/other_mod.mrab'
                elif changed == 'mount':
                    values['vehicle_weapon_setting'][1][0] = 'other_mount'
                elif changed == 'geometry':
                    self.path('OBJECT/V607_ROBOTRUCK.MRAB').write_bytes(self.model_archive(b'other bones'))
                else:
                    values['animation_model'][2] = b'other locators'
                if changed == 'custom_table':
                    values[KEY] = ['user replacement table']
                values['later_setting'] = ['preserve after table withdrawal']
                self.put(sgo.write(0x102, values))
                values = self.values()
                self.assertNotIn(REL, self.install())
                after = self.values()
                if changed == 'custom_table':
                    self.assertEqual(after[KEY], values[KEY])
                else:
                    self.assertNotIn(KEY, after)
                    del values[KEY]
                self.assertEqual(after, values)
                self.assertFalse(ledger.Ledger(self.root).owners(REL))
                make_optics.remove(self.root)
                self.assertEqual(self.values(), values)
                self.path().unlink()
                self.path('OBJECT/V607_ROBOTRUCK.MRAB').unlink(missing_ok=True)

    def test_current_contract_change_after_readonly_plan_still_releases_the_old_claim(self) -> None:
        self.install()
        plan = make_optics.build_stock_redirects(self.root)
        values = self.values()
        values['animation_model'][2] = b'modified after build'
        self.put(sgo.write(0x102, values))
        values = self.values()
        make_optics.install_stock_redirects(self.root, plan)
        after = self.values()
        self.assertNotIn(KEY, after)
        del values[KEY]
        self.assertEqual(after, values)
        self.assertFalse(ledger.Ledger(self.root).owners(REL))

    def test_later_non_aim_edits_survive_and_only_our_field_is_removed(self) -> None:
        self.install()
        version, values = sgo.read(self.path().read_bytes())
        values['later_mod_setting'] = [456.0, 'do not restore an old snapshot']
        edited = sgo.write(version, values)
        self.put(edited)
        self.install()
        self.assertEqual(self.path().read_bytes(), edited)
        make_optics.remove(self.root)
        after = self.values()
        self.assertNotIn(KEY, after)
        self.assertEqual(after['later_mod_setting'], [456.0, 'do not restore an old snapshot'])

    def test_later_aim_edit_or_deletion_is_never_reset_or_removed_as_ours(self) -> None:
        for delete in (False, True):
            with self.subTest(delete=delete):
                self.install()
                version, values = sgo.read(self.path().read_bytes())
                if delete:
                    del values[KEY]
                else:
                    values[KEY] = ['user replacement']
                edited = sgo.write(version, values)
                self.put(edited)
                self.install()
                self.assertEqual(self.path().read_bytes(), edited)
                make_optics.remove(self.root)
                self.assertEqual(self.path().read_bytes(), edited)
                self.path().unlink()

    def test_optional_visual_redirect_and_ai_preparation_share_one_transaction(self) -> None:
        with patch.object(vehicle_optics.cockpits, 'assets_present', return_value=True):
            make_optics.install_models(self.root, {MODEL: b'synthetic model transaction fixture'})
            self.install()
            self.assertIn(KEY, self.values())
            self.assertIn('edf6vc_optic_', self.values()['animation_model'][0][0])
            self.assertEqual(ledger.Ledger(self.root).owners(REL), [make_optics.OWNER])
            self.install()
            make_optics.remove(self.root)
            self.assertFalse(self.path().exists())
            self.assertFalse(self.path(MODEL).exists())

    def test_donor_disagreement_fails_before_any_output_is_written(self) -> None:
        self.donors[DONORS[-1]] = sgo.write(0x102, stock() | {KEY: ['unproved variant']})
        with self.assertRaisesRegex(ValueError, 'variants disagree'):
            self.install()
        self.assertFalse(self.path().exists())
        self.assertFalse(Path(self.root, 'Mods', make_optics.MANIFEST).exists())

    def test_interrupted_field_write_keeps_its_journal_for_reinstall_and_undo(self) -> None:
        original_save = make_optics._save
        calls = 0
        def fail_after_data(root, state):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('interrupted final journal save')
            original_save(root, state)
        with patch.object(make_optics, '_save', side_effect=fail_after_data):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                self.install()
        state = json.loads(Path(self.root, 'Mods', make_optics.MANIFEST).read_text())
        self.assertTrue(state['files'][REL]['npc_ai_added'])
        self.assertIn('pending', state['files'][REL])
        self.install()
        make_optics.remove(self.root)
        self.assertFalse(self.path().exists())

    def test_dsgo_unnamed_nodes_and_opaque_mod_fields_survive_field_undo(self) -> None:
        def node(value):
            if isinstance(value, dict):
                return dsgo.Node([node(item) for item in value.values()], dict(enumerate(value)))
            if isinstance(value, list):
                return dsgo.Node([node(item) for item in value])
            return dsgo.Blob(value) if isinstance(value, bytes) else value
        self.source = dsgo.write(dsgo.Document(node(stock()), []))
        self.donors = {name: dsgo.write(dsgo.Document(node(stock() | {KEY: AIM}), [])) for name in DONORS}
        original = dsgo.parse(self.source)
        original.root.items.append(dsgo.Blob(b'pre-existing unnamed node'))
        self.put(dsgo.write(original))
        self.install()
        current = dsgo.parse(self.path().read_bytes())
        current.root.items.append(dsgo.Blob(b'later unnamed node'))
        self.put(dsgo.write(current))
        make_optics.remove(self.root)
        expected = copy.deepcopy(original.root)
        expected.items.append(dsgo.Blob(b'later unnamed node'))
        self.assertEqual(dsgo.parse(self.path().read_bytes()).root, expected)


@unittest.skipUnless(Path(rootcpk.DEFAULT_GAME, 'Root.cpk').is_file(), 'read-only Root.cpk unavailable')
class NativeRootDonorTests(unittest.TestCase):
    def test_actual_root_donors_prepare_and_uninstall_without_cockpit_models(self) -> None:
        native = rootcpk.Game(rootcpk.DEFAULT_GAME)
        class Source:
            def names(self, folder):
                return [OBJECT] if folder == 'OBJECT' else []   # make_optics also scans WEAPON (the gold weapons)
            def read(self, folder, name):
                return native.read(folder, name)
        source = Source()
        before = sgo.load(data=source.read('OBJECT', OBJECT))
        expected = sgo.load(data=source.read('OBJECT', DONORS[0]))[KEY]
        with tempfile.TemporaryDirectory(prefix='edf6-native-aim-') as root, \
                patch.object(make_optics.rootcpk, 'Game', return_value=source), \
                patch.object(vehicle_optics.cockpits, 'assets_present', return_value=False):
            files = make_optics.build_stock_redirects(root)
            make_optics.install_stock_redirects(root, files)
            path = Path(root, 'Mods', *REL.split('/'))
            after = sgo.load(data=path.read_bytes())
            self.assertEqual(after.pop(KEY), expected)
            self.assertEqual(after, before)
            make_optics.remove(root)
            self.assertFalse(path.exists())


def run_checks() -> None:
    import io
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(StockAimInstallTests))
    if not result.wasSuccessful():
        raise AssertionError(stream.getvalue())


if __name__ == '__main__':
    unittest.main()
