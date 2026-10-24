"""EDF4.1 install failures are local to an asset and never overwrite externally edited files."""
from __future__ import annotations

from contextlib import ExitStack, redirect_stdout
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import edf41_maps as maps
import edf41_objects as objects
import edf41_sounds as sounds
import edf41src
import export_edf41_assets as exporter
import ledger
import edf41materials
import mdb
import sgo


class Archives:
    def fingerprint(self, name: str) -> list[int]:
        return [len(name), 1]


class Edf41InstallSafetyTests(unittest.TestCase):
    def test_missing_map_source_skips_one_map_and_installs_the_next(self) -> None:
        names = list(maps.MAPS)[:2]

        def source(name: str, archives: object) -> dict:
            if name == names[0]:
                raise KeyError(f'{name}.MAD')
            return {'source': [1, 2]}

        def convert(name: str, *_: object) -> dict[str, bytes]:
            return {rel: b'map' for rel in maps.files_of(name)}

        with tempfile.TemporaryDirectory() as root, \
                patch.object(maps, 'MAPS', {n: maps.MAPS[n] for n in names}), \
                patch.object(maps, 'Archives', return_value=Archives()), \
                patch.object(maps, '_source', side_effect=source), \
                patch.object(maps, 'convert', side_effect=convert):
            written, skipped = maps.install(root, 'edf41', lambda _: None)
            self.assertEqual([n for n, _ in skipped], [names[0]])
            self.assertIn(f'{names[0]}.MAD', skipped[0][1])
            self.assertEqual(len(written), len(maps.files_of(names[1])))
            self.assertTrue(all(Path(ledger.Ledger(root).disk(rel)).exists() for rel in maps.files_of(names[1])))

    def test_changed_owned_map_is_preserved_and_not_cached_as_available(self) -> None:
        name = next(iter(maps.MAPS))
        with tempfile.TemporaryDirectory() as root, \
                patch.object(maps, 'MAPS', {name: maps.MAPS[name]}), \
                patch.object(maps, 'Archives', return_value=Archives()), \
                patch.object(maps, 'convert', side_effect=lambda n, *_: {r: b'original' for r in maps.files_of(n)}):
            self.assertFalse(maps.install(root, 'edf41', lambda _: None)[1])
            rel = f'MAP/{name}.RAB'
            changed = Path(ledger.Ledger(root).disk(rel))
            changed.write_bytes(b'user changed map')
            written, skipped = maps.install(root, 'edf41', lambda _: None)
            self.assertFalse(written)
            self.assertEqual([n for n, _ in skipped], [name])
            self.assertIn(rel, skipped[0][1])
            self.assertEqual(changed.read_bytes(), b'user changed map')
            maps.remove(root)
            self.assertEqual(changed.read_bytes(), b'user changed map')

    def test_changed_owned_sound_is_preserved_even_with_matching_stamp(self) -> None:
        rel = sounds.WEAPON_REL
        with tempfile.TemporaryDirectory() as root, \
                patch.object(sounds, 'GROUPS', {'weapon': [rel]}), \
                patch.object(sounds, 'missing', return_value=[]), \
                patch.object(sounds, 'sources_of', return_value=[]), \
                patch.object(sounds, 'make', return_value={rel: b'original'}):
            self.assertFalse(sounds.install(root, 'edf41', lambda _: None)[1])
            self.assertEqual(sounds.install(root, 'edf41', lambda _: None), ([], []))
            changed = Path(ledger.Ledger(root).disk(rel))
            changed.write_bytes(b'user changed bank')
            written, skipped = sounds.install(root, 'edf41', lambda _: None)
            self.assertFalse(written)
            self.assertEqual([n for n, _ in skipped], ['weapon'])
            self.assertIn(rel, skipped[0][1])
            self.assertEqual(changed.read_bytes(), b'user changed bank')
            sounds.remove(root)
            self.assertEqual(changed.read_bytes(), b'user changed bank')


class Edf41ObjectUpgradeTests(unittest.TestCase):
    OBJ = 'OBJECT/DEPENDENT_TEST.SGO'
    MODEL = 'OBJECT/DEPENDENT_TEST.MRAB'
    PHYSICS = 'OBJECT/DEPENDENT_TEST.SHKT'
    SHADER = 'SHADER/DX11/EDF41_SND_MECH.SGO'

    def plan(self, payloads: dict) -> dict:
        return {'objects': [{'object': self.OBJ, 'files': [self.OBJ, self.MODEL, self.PHYSICS]}],
                'files': {rel: hashlib.sha256(data).hexdigest() for rel, data in payloads.items()}}

    def test_primary_writer_upgrades_and_transfers_new_closure_to_each_dependent(self) -> None:
        payloads = {self.OBJ: b'new-object', self.MODEL: b'new-model', self.PHYSICS: b'new-physics'}
        for dependent in sorted(objects.DEPENDENT_OWNERS):
            for writer_first in (True, False):
                with self.subTest(dependent=dependent, writer_first=writer_first), tempfile.TemporaryDirectory() as root, \
                        patch.object(objects, 'registry', return_value=self.plan(payloads)), \
                        patch.object(objects, 'read', side_effect=payloads.__getitem__):
                    led = ledger.Ledger(root)
                    led.put(objects.OWNER, self.OBJ, b'old-object')
                    led.need(dependent, self.OBJ)
                    files, skipped = objects.build(root)
                    self.assertFalse(skipped)
                    objects.install(root, files)
                    for rel, data in payloads.items():
                        led = ledger.Ledger(root)
                        self.assertEqual(set(led.owners(rel)), {objects.OWNER, dependent})
                        self.assertEqual(Path(led.disk(rel)).read_bytes(), data)
                    if writer_first:
                        objects.remove(root)
                        for rel in payloads:
                            self.assertTrue(Path(led.disk(rel)).exists(), 'new closure must outlive its primary writer')
                        ledger.Ledger(root).release(dependent)
                    else:
                        ledger.Ledger(root).release(dependent)
                        for rel in payloads:
                            self.assertTrue(Path(led.disk(rel)).exists())
                        objects.remove(root)
                    self.assertFalse(any(Path(led.disk(rel)).exists() for rel in payloads))

    def test_primary_upgrade_propagates_actual_shader_dependency(self) -> None:
        payloads = {self.OBJ: b'new-object', self.MODEL: b'new-model', self.PHYSICS: b'new-physics'}
        fake_materials = type('Materials', (), {'needed_names': staticmethod(lambda files: {'SND_MECH'}),
                                               'shader_path': staticmethod(lambda name: self.SHADER)})
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', return_value=self.plan(payloads)), \
                patch.dict(sys.modules, {'edf41materials': fake_materials}):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.OBJ, b'old-object')
            led.need('testrange', self.OBJ)
            objects.install(root, payloads | {self.SHADER: b'actual-used-shader'})
            self.assertEqual(set(ledger.Ledger(root).owners(self.SHADER)), {objects.OWNER, 'testrange'})
            objects.remove(root)
            self.assertEqual(Path(led.disk(self.SHADER)).read_bytes(), b'actual-used-shader')
            ledger.Ledger(root).release('testrange')
            self.assertFalse(Path(led.disk(self.SHADER)).exists())

    def test_competing_writer_spoofed_consumer_and_user_edits_are_refused(self) -> None:
        payloads = {self.OBJ: b'new-object', self.MODEL: b'new-model', self.PHYSICS: b'new-physics'}
        for owners, edit in ((('testrange',), False), (('calls',), False),
                             ((objects.OWNER, 'calls'), False), ((objects.OWNER, 'testrange'), True),
                             ((objects.OWNER,), True)):
            with self.subTest(owners=owners, edit=edit), tempfile.TemporaryDirectory() as root, \
                    patch.object(objects, 'registry', return_value=self.plan(payloads)), \
                    patch.object(objects, 'read', side_effect=payloads.__getitem__):
                led = ledger.Ledger(root)
                led.put(owners[0], self.OBJ, b'old-object')
                for owner in owners[1:]:
                    led.need(owner, self.OBJ)
                expected = b'user edited' if edit else b'old-object'
                if edit:
                    Path(led.disk(self.OBJ)).write_bytes(expected)
                files, skipped = objects.build(root)
                self.assertFalse(files)
                self.assertEqual([obj for obj, _ in skipped], [self.OBJ])
                self.assertEqual(Path(led.disk(self.OBJ)).read_bytes(), expected)


class LedgerProvenanceTests(unittest.TestCase):
    def test_writer_proof_survives_release_and_need_cannot_create_it(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            rel = 'OBJECT/WRITER.SGO'
            led = ledger.Ledger(root)
            led.put('edf41', rel, b'writer')
            led.remember_dependencies('edf41', rel, [rel, 'OBJECT/MODEL.MRAB'])
            led.need('testrange', rel)
            led.release('edf41')
            led = ledger.Ledger(root)
            self.assertTrue(led.written_by('edf41', rel))
            self.assertFalse(led.written_by('testrange', rel))
            self.assertEqual(led.dependency_closure('edf41', rel), {rel, 'OBJECT/MODEL.MRAB'})
            with self.assertRaisesRegex(ValueError, 'committed root'):
                led.remember_dependencies('testrange', rel, [rel])
            Path(led.disk(rel)).write_bytes(b'user edit')
            self.assertFalse(led.written_by('edf41', rel))

    def test_three_way_merge_preserves_independent_writer_and_closure_metadata(self) -> None:
        original = {'owners': ['edf41'], 'sha': 'one', 'writers': {'edf41': 'one'}}
        base = {'A': original}
        mine = copy.deepcopy(base)
        mine['A']['owners'].append('testrange')
        mine['A']['dependencies'] = {'edf41': {'sha': 'one', 'files': ['A', 'B']}}
        disk = copy.deepcopy(base)
        disk['A'].update(owners=['edf41', 'calls'], sha='two', writers={'edf41': 'one', 'calls': 'two'})
        merged = ledger.merge_files(base, mine, disk)['A']
        self.assertEqual(set(merged['owners']), {'edf41', 'testrange', 'calls'})
        self.assertEqual(merged['sha'], 'two')
        self.assertEqual(merged['writers'], {'edf41': 'one', 'calls': 'two'})
        self.assertEqual(merged['dependencies'], mine['A']['dependencies'])


class ObjectDependencyLifecycleTests(unittest.TestCase):
    OBJ = 'OBJECT/TEST.SGO'
    MODEL = 'OBJECT/TEST.MRAB'
    SHADER = 'SHADER/DX11/EDF41_SND_MECH.SGO'

    @staticmethod
    def model(shader: str = 'snd_Mech') -> bytes:
        material = mdb.Material(0, 0, 0, 0, shader, [], [], 0)
        raw = mdb.mdb_write(mdb.Mdb(0x20, ['metal'], [], [], [material], []))
        return mdb.rab_write(mdb.Rab(0x100, ['MODEL'], [mdb.RabFile('body.mdb', 0, 0, raw)]))

    @staticmethod
    def plan(roots: dict[str, list[str]], files: dict[str, bytes]) -> dict:
        return {'objects': [{'object': root, 'files': closure} for root, closure in roots.items()],
                'files': {rel: hashlib.sha256(data).hexdigest() for rel, data in files.items()}, 'shared': {}}

    @staticmethod
    def strip_metadata(root: str) -> None:
        path = Path(root, 'Mods', ledger.MANIFEST)
        data = json.loads(path.read_text())
        for entry in data['files'].values():
            entry.pop('writers', None)
            entry.pop('dependencies', None)
        path.write_text(json.dumps(data), encoding='utf-8')

    def test_reinstall_after_writer_release_and_legacy_variant_migration(self) -> None:
        raw = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower()}), self.MODEL: self.model()}
        compatible = {rel: edf41materials.transform_models(rel, data) for rel, data in raw.items()}
        compatible[self.SHADER] = sgo.write(0x102, {'passes': []})
        reg = self.plan({self.OBJ: list(raw)}, raw)
        for old_ledger in (False, True):
            with self.subTest(old_ledger=old_ledger), tempfile.TemporaryDirectory() as root, \
                    patch.object(objects, 'registry', return_value=reg), \
                    patch.object(objects, 'read', side_effect=lambda rel, *_: raw[rel]):
                objects.install(root, compatible)
                led = ledger.Ledger(root)
                for rel in compatible:
                    led.need('testrange', rel)
                if old_ledger:
                    self.strip_metadata(root)
                objects.remove(root)
                self.assertEqual(ledger.Ledger(root).owners(self.MODEL), ['testrange'])
                self.assertFalse(objects.unavailable(root))
                rebuilt, skipped = objects.build(root)
                self.assertFalse(skipped)
                objects.install(root, rebuilt)
                self.assertTrue(ledger.Ledger(root).written_by(objects.OWNER, self.MODEL))
                self.assertEqual(Path(led.disk(self.MODEL)).read_bytes(), raw[self.MODEL])
                self.assertFalse(Path(led.disk(self.SHADER)).exists(), 'disabled compatibility releases old alias needs')

    def test_a_consumer_writer_cannot_claim_the_material_variant_migration(self) -> None:
        raw = {self.OBJ: b'root', self.MODEL: self.model()}
        reg = self.plan({self.OBJ: list(raw)}, raw)
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', return_value=reg), \
                patch.object(objects, 'read', side_effect=lambda rel, *_: raw[rel]):
            led = ledger.Ledger(root)
            for rel, data in raw.items():
                led.put('testrange', rel, edf41materials.transform_models(rel, data))
            self.assertEqual(len(objects.unavailable(root)), 1)

    def test_replacing_model_releases_old_closure_but_retains_other_root_sharing_it(self) -> None:
        other, new = 'OBJECT/OTHER.SGO', 'OBJECT/SHARED.MRAB'
        for shared in (False, True):
            with self.subTest(shared=shared), tempfile.TemporaryDirectory() as root:
                old_root = sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower()})
                other_root = sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower()})
                old_files = {self.OBJ: old_root, self.MODEL: self.model()}
                roots = {self.OBJ: list(old_files)}
                if shared:
                    old_files[other] = other_root
                    roots[other] = [other, self.MODEL]
                with patch.object(objects, 'registry', return_value=self.plan(roots, old_files)):
                    objects.install(root, old_files)
                led = ledger.Ledger(root)
                for rel in old_files:
                    led.need('testrange', rel)
                new_files = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + new.lower()}), new: self.model()}
                roots[self.OBJ] = list(new_files)
                if shared:
                    new_files.update({other: other_root, self.MODEL: self.model()})
                with patch.object(objects, 'registry', return_value=self.plan(roots, new_files)):
                    objects.install(root, new_files)
                self.assertEqual(Path(led.disk(self.MODEL)).exists(), shared)
                if shared:
                    self.assertIn('testrange', ledger.Ledger(root).owners(self.MODEL))
                self.assertEqual(set(ledger.Ledger(root).owners(new)), {objects.OWNER, 'testrange'})

    def test_pre_metadata_upgrade_reads_previous_sgo_graph_before_replacing_it(self) -> None:
        new, shared = 'OBJECT/SHARED.MRAB', 'OBJECT/SHARED.CAS'
        old_files = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower(),
                                               'animation': 'app:/' + shared.lower()}),
                     self.MODEL: self.model(), shared: b'animation'}
        with tempfile.TemporaryDirectory() as root:
            led = ledger.Ledger(root)
            for rel, data in old_files.items():
                led.put(objects.OWNER, rel, data)
                led.need('testrange', rel)
            self.strip_metadata(root)
            new_files = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + new.lower(),
                                                   'animation': 'app:/' + shared.lower()}),
                         new: self.model(), shared: b'animation'}
            with patch.object(objects, 'registry', return_value=self.plan({self.OBJ: list(new_files)}, new_files)):
                objects.install(root, new_files)
            self.assertFalse(Path(led.disk(self.MODEL)).exists())
            self.assertEqual(set(ledger.Ledger(root).owners(shared)), {objects.OWNER, 'testrange'})

    def test_unreadable_unrelated_legacy_root_preserves_unproved_shared_dependencies(self) -> None:
        other, new = 'OBJECT/UNKNOWN.SGO', 'OBJECT/SHARED.MRAB'
        old_files = {self.OBJ: b'old root', self.MODEL: b'old model'}
        with tempfile.TemporaryDirectory() as root:
            with patch.object(objects, 'registry', return_value=self.plan({self.OBJ: list(old_files)}, old_files)):
                objects.install(root, old_files)
            led = ledger.Ledger(root)
            for rel in old_files:
                led.need('testrange', rel)
            led.put(objects.OWNER, other, b'unreadable previous schema')
            led.need('testrange', other)
            new_files = {self.OBJ: b'new root', new: b'new model'}
            with patch.object(objects, 'registry', return_value=self.plan({self.OBJ: list(new_files)}, new_files)):
                objects.install(root, new_files)
            self.assertEqual(Path(led.disk(self.MODEL)).read_bytes(), b'old model')
            self.assertEqual(ledger.Ledger(root).owners(self.MODEL), ['testrange'])

    def test_an_edited_other_root_keeps_its_new_reference_to_the_retired_model(self) -> None:
        other, other_model, new = 'OBJECT/OTHER.SGO', 'OBJECT/OTHER.MRAB', 'OBJECT/SHARED.MRAB'
        root_data = lambda rel: sgo.write(0x102, {'model': 'app:/' + rel.lower()})
        old_files = {self.OBJ: root_data(self.MODEL), self.MODEL: self.model(),
                     other: root_data(other_model), other_model: self.model()}
        roots = {self.OBJ: [self.OBJ, self.MODEL], other: [other, other_model]}
        with tempfile.TemporaryDirectory() as root:
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files)):
                objects.install(root, old_files)
            led = ledger.Ledger(root)
            for rel in old_files:
                led.need('testrange', rel)
            changed = root_data(self.MODEL)
            Path(led.disk(other)).write_bytes(changed)
            new_files = {self.OBJ: root_data(new), new: self.model()}
            roots[self.OBJ] = list(new_files)
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files | new_files)):
                objects.install(root, new_files)  # unavailable() would leave the edited other object out
            self.assertEqual(Path(led.disk(other)).read_bytes(), changed)
            self.assertTrue(Path(led.disk(self.MODEL)).exists())
            self.assertEqual(ledger.Ledger(root).owners(self.MODEL), ['testrange'])

    def test_an_edited_nested_model_keeps_a_new_private_shader_reference(self) -> None:
        other, other_model = 'OBJECT/OTHER.SGO', 'OBJECT/OTHER.MRAB'
        root_data = lambda rel: sgo.write(0x102, {'model': 'app:/' + rel.lower()})
        private = edf41materials.transform_models(self.MODEL, self.model())
        old_files = {self.OBJ: root_data(self.MODEL), self.MODEL: private,
                     other: root_data(other_model), other_model: self.model('snd_Common_Basic'),
                     self.SHADER: sgo.write(0x102, {'passes': []})}
        roots = {self.OBJ: [self.OBJ, self.MODEL], other: [other, other_model]}
        with tempfile.TemporaryDirectory() as root:
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files)):
                objects.install(root, old_files)
            led = ledger.Ledger(root)
            self.assertNotIn(self.SHADER, led.dependency_closure(objects.OWNER, other))
            for rel in old_files:
                led.need('testrange', rel)
            Path(led.disk(other_model)).write_bytes(private)
            self.assertFalse(ledger.Ledger(root).changed(other))
            new_files = {self.OBJ: root_data(self.MODEL), self.MODEL: self.model()}
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files | new_files)):
                objects.install(root, new_files)
            self.assertEqual(Path(led.disk(other_model)).read_bytes(), private)
            self.assertEqual(ledger.Ledger(root).owners(self.SHADER), ['testrange'])

    def test_consumer_of_only_other_root_acquires_its_new_model_reference(self) -> None:
        other, other_model, new = 'OBJECT/OTHER.SGO', 'OBJECT/OTHER.MRAB', 'OBJECT/SHARED.MRAB'
        foreign = 'OBJECT/FOREIGN.MRAB'
        root_data = lambda rel: sgo.write(0x102, {'model': 'app:/' + rel.lower()})
        old_files = {self.OBJ: root_data(self.MODEL), self.MODEL: self.model(),
                     other: root_data(other_model), other_model: self.model()}
        roots = {self.OBJ: [self.OBJ, self.MODEL], other: [other, other_model]}
        for edited_model in (False, True):
            with self.subTest(edited_model=edited_model), tempfile.TemporaryDirectory() as root:
                with patch.object(objects, 'registry', return_value=self.plan(roots, old_files)):
                    objects.install(root, old_files)
                led = ledger.Ledger(root)
                for rel in (other, other_model):
                    led.need('testrange', rel)
                self.assertEqual(led.owners(self.MODEL), [objects.OWNER])
                expected_model = self.model('snd_Common_Basic') if edited_model else old_files[self.MODEL]
                Path(led.disk(self.MODEL)).write_bytes(expected_model)
                changed = sgo.write(0x102, {'resource': ['app:/' + self.MODEL.lower(), 'app:/' + foreign.lower()]})
                Path(led.disk(other)).write_bytes(changed)
                Path(led.disk(foreign)).write_bytes(b'untracked foreign model')
                new_files = {self.OBJ: root_data(new), new: self.model()}
                new_roots = roots | {self.OBJ: list(new_files)}
                with patch.object(objects, 'registry', return_value=self.plan(new_roots, old_files | new_files)):
                    objects.install(root, new_files)
                    led = ledger.Ledger(root)
                    self.assertEqual(led.owners(self.MODEL), ['testrange'])
                    self.assertFalse(led.written_by('testrange', self.MODEL))
                    expected_writers = {objects.OWNER: hashlib.sha256(old_files[self.MODEL]).hexdigest()}
                    self.assertEqual(led.files[self.MODEL]['writers'], expected_writers)
                    objects.remove(root)
                self.assertEqual(Path(led.disk(other)).read_bytes(), changed)
                self.assertEqual(Path(led.disk(self.MODEL)).read_bytes(), expected_model)
                self.assertEqual(ledger.Ledger(root).owners(foreign), [])
                ledger.Ledger(root).release('testrange')
                self.assertEqual(Path(led.disk(self.MODEL)).exists(), edited_model)
                self.assertEqual(Path(led.disk(other)).read_bytes(), changed)
                self.assertEqual(Path(led.disk(foreign)).read_bytes(), b'untracked foreign model')

    def test_consumer_of_only_other_root_acquires_its_new_shader_reference(self) -> None:
        other, other_model = 'OBJECT/OTHER.SGO', 'OBJECT/OTHER.MRAB'
        root_data = lambda rel: sgo.write(0x102, {'model': 'app:/' + rel.lower()})
        private = edf41materials.transform_models(self.MODEL, self.model())
        old_files = {self.OBJ: root_data(self.MODEL), self.MODEL: private,
                     other: root_data(other_model), other_model: self.model('snd_Common_Basic'),
                     self.SHADER: sgo.write(0x102, {'passes': []})}
        roots = {self.OBJ: [self.OBJ, self.MODEL], other: [other, other_model]}
        with tempfile.TemporaryDirectory() as root:
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files)):
                objects.install(root, old_files)
            led = ledger.Ledger(root)
            for rel in (other, other_model):
                led.need('testrange', rel)
            self.assertEqual(led.owners(self.SHADER), [objects.OWNER])
            self.assertNotIn(self.SHADER, led.dependency_closure(objects.OWNER, other))
            Path(led.disk(other_model)).write_bytes(private)
            new_files = {self.OBJ: root_data(self.MODEL), self.MODEL: self.model()}
            with patch.object(objects, 'registry', return_value=self.plan(roots, old_files | new_files)):
                objects.install(root, new_files)
                self.assertEqual(ledger.Ledger(root).owners(self.SHADER), ['testrange'])
                self.assertFalse(ledger.Ledger(root).written_by('testrange', self.SHADER))
                objects.remove(root)
            self.assertEqual(Path(led.disk(other)).read_bytes(), old_files[other])
            self.assertEqual(Path(led.disk(other_model)).read_bytes(), private)
            self.assertTrue(Path(led.disk(self.SHADER)).exists())
            ledger.Ledger(root).release('testrange')
            self.assertFalse(Path(led.disk(self.SHADER)).exists())
            self.assertEqual(Path(led.disk(other_model)).read_bytes(), private)

    def test_interrupted_consumer_migration_retries_without_leaking_old_models(self) -> None:
        new = 'OBJECT/SHARED.MRAB'
        old_files = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower()}), self.MODEL: self.model()}
        new_files = {self.OBJ: sgo.write(0x102, {'model': 'app:/' + new.lower()}), new: self.model()}
        for stage in ('write', 'need', 'release'):
            for old_ledger in (False, True):
                with self.subTest(stage=stage, old_ledger=old_ledger), tempfile.TemporaryDirectory() as root:
                    with patch.object(objects, 'registry', return_value=self.plan({self.OBJ: list(old_files)}, old_files)):
                        objects.install(root, old_files)
                    led = ledger.Ledger(root)
                    for rel in old_files:
                        led.need('testrange', rel)
                    if old_ledger:
                        self.strip_metadata(root)
                    write, need, release = objects._write_files, ledger.Ledger.need, ledger.Ledger.release

                    def stop_write(*args):
                        write(*args)
                        raise RuntimeError('interrupted after writer bytes')

                    def stop_need(current, owner, rel):
                        need(current, owner, rel)
                        if owner == 'testrange' and rel == new:
                            raise RuntimeError('interrupted during consumer needs')

                    def stop_release(current, owner, rels=None, writer=False):
                        result = release(current, owner, rels, writer)
                        if owner == 'testrange' and self.MODEL in (rels or ()):
                            raise RuntimeError('interrupted after releasing old references')
                        return result

                    with patch.object(objects, 'registry', return_value=self.plan({self.OBJ: list(new_files)}, new_files)):
                        with ExitStack() as changes:
                            if stage == 'write':
                                changes.enter_context(patch.object(objects, '_write_files', side_effect=stop_write))
                            elif stage == 'need':
                                changes.enter_context(patch.object(ledger.Ledger, 'need', stop_need))
                            else:
                                changes.enter_context(patch.object(ledger.Ledger, 'release', stop_release))
                            with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                                objects.install(root, new_files)
                        self.assertIn(self.MODEL, ledger.Ledger(root).pending_dependencies(objects.OWNER, self.OBJ))
                        objects.install(root, new_files)
                    led = ledger.Ledger(root)
                    self.assertFalse(Path(led.disk(self.MODEL)).exists())
                    self.assertEqual(set(led.owners(new)), {objects.OWNER, 'testrange'})
                    self.assertFalse(led.pending_dependencies(objects.OWNER, self.OBJ))

    def test_legacy_alias_only_consumer_keeps_new_shader_after_writer_removal(self) -> None:
        canonical = 'OBJECT/PREFIXED_TEST.SGO'
        payload = sgo.write(0x102, {'model': 'app:/' + self.MODEL.lower()})
        compatible = edf41materials.transform_models(self.MODEL, self.model())
        files = {canonical: payload, self.OBJ: payload, self.MODEL: compatible, self.SHADER: b'shader'}
        reg = self.plan({canonical: [canonical, self.OBJ, self.MODEL]}, files)
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', return_value=reg):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.OBJ, payload)
            led.put(objects.OWNER, self.MODEL, self.model())
            led.need('testrange', self.OBJ)
            led.need('testrange', self.MODEL)
            self.strip_metadata(root)
            objects.install(root, files)
            self.assertEqual(set(ledger.Ledger(root).owners(self.SHADER)), {objects.OWNER, 'testrange'})
            objects.remove(root)
            self.assertEqual(Path(led.disk(self.SHADER)).read_bytes(), b'shader')
            ledger.Ledger(root).release('testrange')
            self.assertFalse(any(Path(led.disk(rel)).exists() for rel in files))


class Edf41AssetExportCheckTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory(prefix='edf41-export-check-')
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.optional = {'/'.join((*sounds.PLUGINS, name)) for name in sounds.CAPTIONS.values()}

    def write_pack(self, *, omitted: tuple[str, ...] = (), subtitles: bool = False) -> None:
        files = {}
        for rel in exporter.wanted():
            if rel in omitted or (rel in self.optional and not subtitles):
                continue
            data = rel.encode('utf-8')
            path = self.root.joinpath(*rel.split('/'))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            files[rel] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'source': [len(data), 1]}
        self.root.joinpath(edf41src.MANIFEST).write_text(
            json.dumps({'kind': edf41src.KIND, 'format': edf41src.FORMAT, 'files': files}), encoding='utf-8')

    def check_cli(self, *, full: bool = False) -> tuple[int, str]:
        output = io.StringIO()
        with redirect_stdout(output):
            result = exporter.main(['--check', str(self.root), *(['--full'] if full else [])])
        return result, output.getvalue()

    def test_old_manifest_without_hq_background_is_not_a_complete_export(self) -> None:
        missing = tuple(exporter.edf41_menu_bg.SOURCES)
        self.write_pack(omitted=missing)
        # The installer can still consume a valid subset, skipping only the groups that need missing sources.
        self.assertEqual(edf41src.check(str(self.root), full=True), [])
        code, output = self.check_cli(full=True)
        self.assertEqual(code, 1)
        for rel in missing:
            self.assertIn(rel, output)

    def test_unlisted_required_map_or_sound_is_refused(self) -> None:
        required = (maps.sources(next(iter(maps.MAPS)))[0], f'SOUND/PC/{sounds.SE_BANK}')
        for rel in required:
            with self.subTest(rel=rel), tempfile.TemporaryDirectory() as folder:
                self.root = Path(folder)
                self.write_pack(omitted=(rel,))
                code, output = self.check_cli()
                self.assertEqual(code, 1)
                self.assertIn(rel, output)

    def test_complete_export_without_optional_subtitles_passes(self) -> None:
        self.write_pack()
        code, output = self.check_cli(full=True)
        self.assertEqual(code, 0, output)

    def test_a_listed_optional_subtitle_must_still_be_intact(self) -> None:
        self.write_pack(subtitles=True)
        rel = sorted(self.optional)[0]
        self.root.joinpath(*rel.split('/')).unlink()
        code, output = self.check_cli()
        self.assertEqual(code, 1)
        self.assertIn(rel, output)

    def test_full_check_still_detects_same_size_content_changes(self) -> None:
        self.write_pack()
        rel = exporter.edf41_menu_bg.SOURCES[0]
        path = self.root.joinpath(*rel.split('/'))
        data = path.read_bytes()
        path.write_bytes(bytes([data[0] ^ 1]) + data[1:])
        self.assertEqual(self.check_cli()[0], 0)
        code, output = self.check_cli(full=True)
        self.assertEqual(code, 1)
        self.assertIn(rel, output)


if __name__ == '__main__':
    unittest.main()
