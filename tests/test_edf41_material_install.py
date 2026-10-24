"""EDF4.1 shader compatibility is scoped, cached and reversible at install boundaries."""
from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib'), str(ROOT / 'testrange')]
import edf41_maps as maps
import edf41_objects as objects
import edf41_outfits as outfits
import ledger
import gen
import mdb

SHADER = 'SHADER/DX11/EDF41_SND_MECH.SGO'


class MaterialInstallTests(unittest.TestCase):
    def test_object_builder_excludes_shared_archive_and_checks_source_hash(self) -> None:
        raw = {'OBJECT/OWN.MRAB': b'own', 'OBJECT/SHARED.MRAB': b'shared'}
        reg = {'objects': [{'object': 'OBJECT/OWN.SGO', 'files': list(raw)}],
               'files': {rel: hashlib.sha256(data).hexdigest() for rel, data in raw.items()},
               'shared': {'OBJECT/SHARED.MRAB': 'shared'}}
        calls = []

        def apply(files: dict, root: str) -> dict:
            calls.append(files)
            return {'OBJECT/OWN.MRAB': b'compatible', SHADER: b'shader'}

        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', return_value=reg), \
                patch.object(objects, 'unavailable', return_value=[]), \
                patch.object(objects, 'read', side_effect=lambda rel, *_: raw[rel]), \
                patch.dict(sys.modules, {'edf41_materials': SimpleNamespace(apply=apply)}):
            self.assertEqual(objects.build(root)[0], raw)
            self.assertFalse(calls)
            files, skipped = objects.build(root, material_compat=True)
            self.assertFalse(skipped)
            self.assertEqual(calls, [{'OBJECT/OWN.MRAB': b'own'}])
            self.assertEqual(files['OBJECT/SHARED.MRAB'], b'shared')
            self.assertEqual(files['OBJECT/OWN.MRAB'], b'compatible')
            self.assertEqual(files[SHADER], b'shader')
            reg['files']['OBJECT/OWN.MRAB'] = 'damaged-source'
            with self.assertRaises(ValueError):
                objects.build(root, material_compat=True)
            self.assertEqual(len(calls), 1, 'verify source provenance before shader rewriting')

    def test_outfit_disable_restores_model_and_keeps_shader_another_owner_needs(self) -> None:
        raw = {'OBJECT/EDF41_P_SOLDIER.MRAB': b'raw-model'}
        rel = next(iter(raw))
        reg = {'classes': [{'class': 'soldier', 'files': [rel]}],
               'files': {rel: hashlib.sha256(raw[rel]).hexdigest()}}
        def apply(files: dict, root: str) -> dict:
            return {rel: b'compatible-model', SHADER: b'shader'}
        with tempfile.TemporaryDirectory() as root, patch.object(outfits, 'registry', return_value=reg), \
                patch.object(outfits, 'read', side_effect=raw.__getitem__), \
                patch.dict(sys.modules, {'edf41_materials': SimpleNamespace(apply=apply)}):
            files, skipped = outfits.build(root, material_compat=True)
            self.assertFalse(skipped)
            outfits.install(root, files)
            led = ledger.Ledger(root)
            led.put(objects.OWNER, SHADER, b'shader')
            files, skipped = outfits.build(root, material_compat=False)
            self.assertFalse(skipped)
            outfits.install(root, files)
            led = ledger.Ledger(root)
            self.assertEqual(Path(led.disk(rel)).read_bytes(), b'raw-model')
            self.assertEqual(led.owners(SHADER), [objects.OWNER])
            self.assertEqual(Path(led.disk(SHADER)).read_bytes(), b'shader')

    def test_edited_shader_is_not_a_valid_map_cache_dependency(self) -> None:
        name = next(iter(maps.MAPS))
        source = {'source': [1, 2]}
        with tempfile.TemporaryDirectory() as root:
            led = ledger.Ledger(root)
            for rel in maps.files_of(name):
                led.put(maps.OWNER, rel, b'original-map')
            led.put(maps.OWNER, SHADER, b'original-shader')
            stamp = {name: {'version': maps.VERSION, 'source': source, 'material_compat': True,
                            'material_shaders': [SHADER]}}
            self.assertTrue(maps._current(led, stamp, name, source, True))
            Path(led.disk(SHADER)).write_bytes(b'user-edited-shader')
            self.assertFalse(maps._current(led, stamp, name, source, True))

    def test_map_cache_tracks_mode_and_shared_shader_dependencies(self) -> None:
        name = next(iter(maps.MAPS))
        made, adjusted = [], []
        class Archives:
            def fingerprint(self, filename: str) -> list[int]:
                return [len(filename), 1]
        def convert(current: str, *_: object) -> dict:
            made.append(current)
            return {rel: b'raw-' + rel.encode() for rel in maps.files_of(current)}
        def apply(files: dict, root: str) -> dict:
            adjusted.append(list(files))
            return {next(iter(files)): b'compatible-model', SHADER: b'shader'}
        with tempfile.TemporaryDirectory() as root, \
                patch.object(maps, 'MAPS', {name: maps.MAPS[name]}), \
                patch.object(maps, 'Archives', side_effect=lambda _: Archives()), \
                patch.object(maps, 'convert', side_effect=convert), \
                patch.dict(sys.modules, {'edf41_materials': SimpleNamespace(apply=apply)}):
            self.assertFalse(maps.install(root, 'source41', material_compat=True)[1])
            self.assertEqual(adjusted, [[f'MAP/{name}.RAB']], 'donor weather models are never rewritten')
            self.assertEqual(maps.install(root, 'source41', material_compat=True), ([], []))
            self.assertEqual(made, [name])
            led = ledger.Ledger(root)
            self.assertEqual(Path(led.disk(SHADER)).read_bytes(), b'shader')
            Path(led.disk(SHADER)).unlink()
            self.assertFalse(maps.install(root, 'source41', material_compat=True)[1])
            self.assertEqual(made, [name, name], 'a missing dependent shader invalidates the cached map')
            self.assertFalse(maps.install(root, 'source41', material_compat=False)[1])
            self.assertEqual(made, [name, name, name], 'changing the install setting invalidates only this map')
            led = ledger.Ledger(root)
            self.assertEqual(Path(led.disk(f'MAP/{name}.RAB')).read_bytes(), b'raw-' + f'MAP/{name}.RAB'.encode())
            self.assertFalse(Path(led.disk(SHADER)).exists())


class MaterialRangeDependencyTests(unittest.TestCase):
    MODEL = 'OBJECT/EDF41_RANGE_TEST.MRAB'

    @staticmethod
    def archive(alias: bool = True) -> bytes:
        shader = 'EDF41_SND_MECH' if alias else 'snd_Mech'
        material = mdb.Material(0, 0, 0, 0, shader, [], [], 0)
        raw = mdb.mdb_write(mdb.Mdb(0x20, ['metal'], [], [], [material], []))
        return mdb.rab_write(mdb.Rab(0x100, ['MODEL'], [mdb.RabFile('body.mdb', 0, 0, raw)]))

    def registry(self) -> dict:
        return {'files': {self.MODEL: 'original-source-hash'}, 'shared': {}}

    def test_range_keeps_alias_after_writer_removal_and_regeneration(self) -> None:
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', side_effect=self.registry):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.MODEL, self.archive())
            led.put(objects.OWNER, SHADER, b'compatible shader')
            gen._write_derived(root, object(), set(), (self.MODEL,))
            self.assertEqual(set(ledger.Ledger(root).owners(SHADER)), {objects.OWNER, gen.OWNER})
            ledger.Ledger(root).release(objects.OWNER)
            gen._write_derived(root, object(), set(), (self.MODEL,))
            self.assertEqual(ledger.Ledger(root).owners(SHADER), [gen.OWNER])
            self.assertEqual(Path(led.disk(SHADER)).read_bytes(), b'compatible shader')
            gen._write_derived(root, object(), set())
            self.assertFalse(Path(led.disk(self.MODEL)).exists())
            self.assertFalse(Path(led.disk(SHADER)).exists())

    def test_actual_model_switch_off_releases_only_obsolete_alias(self) -> None:
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', side_effect=self.registry):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.MODEL, self.archive())
            led.put(objects.OWNER, SHADER, b'compatible shader')
            gen._write_derived(root, object(), set(), (self.MODEL,))
            led.put(objects.OWNER, self.MODEL, self.archive(False))
            ledger.Ledger(root).release(objects.OWNER, [SHADER])
            gen._write_derived(root, object(), set(), (self.MODEL,))
            self.assertTrue(Path(led.disk(self.MODEL)).exists())
            self.assertFalse(Path(led.disk(SHADER)).exists())
            self.assertEqual(set(ledger.Ledger(root).owners(self.MODEL)), {objects.OWNER, gen.OWNER})

    def test_missing_actual_alias_refuses_before_recording_partial_range_needs(self) -> None:
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', side_effect=self.registry):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.MODEL, self.archive())
            with self.assertRaisesRegex(RuntimeError, 'EDF41_SND_MECH'):
                gen._write_derived(root, object(), set(), (self.MODEL,))
            self.assertEqual(ledger.Ledger(root).owners(self.MODEL), [objects.OWNER])
            self.assertEqual(ledger.Ledger(root).owned_by(gen.OWNER), [])

    def test_original_shader_does_not_keep_an_unreferenced_alias(self) -> None:
        with tempfile.TemporaryDirectory() as root, patch.object(objects, 'registry', side_effect=self.registry):
            led = ledger.Ledger(root)
            led.put(objects.OWNER, self.MODEL, self.archive(False))
            led.put(objects.OWNER, SHADER, b'another object uses this')
            gen._write_derived(root, object(), set(), (self.MODEL,))
            self.assertEqual(ledger.Ledger(root).owners(SHADER), [objects.OWNER])


if __name__ == '__main__':
    unittest.main()
