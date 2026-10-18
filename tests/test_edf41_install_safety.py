"""EDF4.1 install failures are local to an asset and never overwrite externally edited files."""
from __future__ import annotations

from contextlib import redirect_stdout
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
