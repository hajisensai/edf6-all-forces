"""EDF4.1 install failures are local to an asset and never overwrite externally edited files."""
from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]
import edf41_maps as maps
import edf41_sounds as sounds
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


if __name__ == '__main__':
    unittest.main()
