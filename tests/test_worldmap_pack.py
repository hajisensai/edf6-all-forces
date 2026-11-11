"""The world map's base map in the model pack (docs/plan-2026-11-10-worldmap.md §2): tools/worldmap_basemap.py tiles
builds the release folder (the JPEG tile pyramid in one file, index, credit, nothing else) from eight small synthetic
sources, every tile in place and decodable at its level's size and showing its part of the Earth; tools/worldmap_pack.py
installs it under Mods as its ledger owner, refuses a missing / wrong / foreign file without a substitute, and releases
what it installed (the DXT1 textures of TEXTURE_VERSION 1 included). With the developer's model folder present also
that folder is consistent (its index, the real pyramid's table)."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib')]

import build_release  # noqa: E402
import ledger  # noqa: E402
import obj_model  # noqa: E402
import worldmap  # noqa: E402
import worldmap_basemap as wb  # noqa: E402
import worldmap_pack  # noqa: E402

SMALL_TILE = 8      # sources of 8 * 16 = 128 px


def colour(tile: str) -> tuple[int, int, int]:
    c, r = wb.Source(tile, '').cell
    return (40 + 60 * c, 200 - 120 * r, 90)


@contextlib.contextmanager
def small():
    with mock.patch.object(worldmap, 'TILE', SMALL_TILE):
        yield


def sources() -> dict:
    from PIL import Image
    side = SMALL_TILE * wb.per_source(worldmap.TILE_LEVELS - 1)
    return {s.tile: Image.new('RGB', (side, side), colour(s.tile)) for s in wb.SOURCES}


def build_small(work: str) -> str:
    """The release folder from eight flat-coloured sources; returns its path."""
    out = os.path.join(work, worldmap.TEXTURE_DIR)
    with small():
        wb.write_release(wb.pack(wb.pyramid(sources())), out)
    return out


def tiles_name() -> str:
    return worldmap.TILES_FILE.rsplit('/', 1)[1]


class Build(unittest.TestCase):
    def test_release_folder(self) -> None:
        from PIL import Image
        with tempfile.TemporaryDirectory() as work:
            out = build_small(work)
            self.assertEqual(set(os.listdir(out)), {tiles_name(), 'SOURCE.txt', worldmap.TEXTURE_INDEX},
                             'only the shipped files, never the sources')
            data = Path(out, tiles_name()).read_bytes()
            with open(os.path.join(out, worldmap.TEXTURE_INDEX), encoding='utf-8') as f:
                index = json.load(f)
            self.assertEqual(index, {'version': worldmap.TEXTURE_VERSION,
                                     'files': {tiles_name(): {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}}})
            with small():
                table = worldmap.tile_table(data)
            self.assertEqual(len(table), worldmap.tile_count())
            self.assertEqual(worldmap.tile_count(), 2731)
            for (z, x, y), (off, size) in table.items():
                im = Image.open(io.BytesIO(data[off:off + size]))
                self.assertEqual(im.format, 'JPEG')
                self.assertEqual(im.size, (SMALL_TILE, SMALL_TILE if z else SMALL_TILE // 2), (z, x, y))
                if z >= 2:      # one source per tile from level 2 on: its colour, west to east, north to south
                    cols = worldmap.tile_grid(z)[0]
                    tile = 'ABCD'[x * 4 // cols] + str(y * 2 // worldmap.tile_grid(z)[1] + 1)
                    got = im.convert('RGB').getpixel((SMALL_TILE // 2, SMALL_TILE // 2))
                    self.assertTrue(all(abs(a - b) <= 8 for a, b in zip(got, colour(tile))), ((z, x, y), got, tile))
            credit = Path(out, 'SOURCE.txt').read_text(encoding='utf-8')
            self.assertIn('NASA Earth Observatory', credit)
            self.assertIn(wb.PAGE_URL, credit)

    def test_wrong_size_refused(self) -> None:
        from PIL import Image
        with small():
            images = {s.tile: Image.new('RGB', (8, 8)) for s in wb.SOURCES}
            with self.assertRaises(SystemExit):
                wb.pyramid(images)

    def test_not_this_layout(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            data = Path(build_small(work), tiles_name()).read_bytes()
        with self.assertRaises(ValueError):
            worldmap.tile_table(data)       # built with an 8 px tile, read as the real 1350
        with small(), self.assertRaises(ValueError):
            worldmap.tile_table(data[:1000])


class Install(unittest.TestCase):
    def run_install(self, game: str, folder: str | None) -> tuple[list[str], list[tuple[str, str]]]:
        with mock.patch.object(worldmap_pack.obj_model, 'model_dir', return_value=folder):
            return worldmap_pack.install(game, say=lambda _s: None)

    def test_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            game = os.path.join(work, 'game')
            os.makedirs(game)
            written, skipped = self.run_install(game, pack)
            self.assertEqual(skipped, [])
            led = ledger.Ledger(game)
            self.assertEqual(led.owned_by(worldmap_pack.OWNER), [ledger.key(worldmap.TILES_FILE)])
            self.assertEqual(Path(led.disk(worldmap.TILES_FILE)).read_bytes(), Path(pack, tiles_name()).read_bytes())
            self.assertEqual(len(written), 1)
            again, skipped = self.run_install(game, pack)
            self.assertEqual((again, skipped), ([], []), 'the same bytes are not written again')
            deleted, kept = worldmap_pack.remove(game)
            self.assertEqual((len(deleted), kept), (1, []))
            self.assertFalse(os.path.exists(led.disk(worldmap.TILES_FILE)))

    def test_version_1_textures_released(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            game = os.path.join(work, 'game')
            os.makedirs(game)
            old = [ledger.Ledger(game).put(worldmap_pack.OWNER, rel, b'dxt1')
                   for rel in ('UI/EDF6VC_WORLDMAP_WORLD.DDS', 'UI/EDF6VC_WORLDMAP_JAPAN.DDS')]
            written, skipped = self.run_install(game, pack)
            self.assertEqual((len(written), skipped), (1, []))
            self.assertFalse(any(os.path.exists(p) for p in old), 'the old DXT1 textures go')

    def check_refused(self, game: str, folder: str | None, needle: str) -> None:
        old = ledger.Ledger(game).put(worldmap_pack.OWNER, worldmap.TILES_FILE, b'old tiles')
        written, skipped = self.run_install(game, folder)
        self.assertEqual(written, [])
        self.assertEqual(len(skipped), 1, skipped)
        self.assertIn(needle, skipped[0][1])
        self.assertFalse(os.path.exists(old), 'what was installed before is released, nothing stands in for it')
        self.assertEqual(ledger.Ledger(game).owned_by(worldmap_pack.OWNER), [])

    def test_missing_folder(self) -> None:
        with tempfile.TemporaryDirectory() as game:
            self.check_refused(game, None, worldmap.TEXTURE_INDEX)

    def test_other_version(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            p = os.path.join(pack, worldmap.TEXTURE_INDEX)
            index = json.loads(Path(p).read_text(encoding='utf-8'))
            index['version'] = 1
            Path(p).write_text(json.dumps(index), encoding='utf-8')
            game = os.path.join(work, 'game')
            os.makedirs(game)
            self.check_refused(game, pack, '版本')

    def test_wrong_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            data = bytearray(Path(pack, tiles_name()).read_bytes())
            data[-1] ^= 0xFF
            Path(pack, tiles_name()).write_bytes(bytes(data))
            game = os.path.join(work, 'game')
            os.makedirs(game)
            self.check_refused(game, pack, '内容不对')

    def test_foreign_file_kept(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            game = os.path.join(work, 'game')
            os.makedirs(game)
            theirs = ledger.Ledger(game).disk(worldmap.TILES_FILE)
            os.makedirs(os.path.dirname(theirs), exist_ok=True)
            Path(theirs).write_bytes(b'another mod')
            written, skipped = self.run_install(game, pack)
            self.assertEqual(written, [])
            self.assertEqual(len(skipped), 1)
            self.assertEqual(Path(theirs).read_bytes(), b'another mod')


class Wiring(unittest.TestCase):
    def test_release_and_installer(self) -> None:
        self.assertIn(worldmap.TEXTURE_DIR, build_release.RELEASE_MODELS)
        self.assertIn(worldmap_pack.OWNER, ledger.OWNERS)
        inst = (ROOT / 'tools' / 'installer.py').read_text(encoding='utf-8')
        self.assertIn('worldmap_pack.install(game)', inst)
        self.assertIn('worldmap_pack.remove,', inst)
        inc = (ROOT / 'src' / 'worldmap.inc').read_text(encoding='utf-8')
        self.assertIn(f'kTile={worldmap.TILE},kTileLevels={worldmap.TILE_LEVELS},kTilesFormat={worldmap.TILES_FORMAT};', inc)
        self.assertIn(f'kTilesMagic[5]="{worldmap.TILES_MAGIC.decode()}"', inc)
        self.assertIn(f'kTilesFile=L"{worldmap.TILES_FILE}"', inc)
        self.assertTrue(worldmap.TILES_FILE.startswith('Plugins/EDF6VehicleCrew.'), 'beside the plugin that reads it')

    def test_sources(self) -> None:
        self.assertEqual(sorted(s.tile for s in wb.SOURCES), sorted(c + r for c in 'ABCD' for r in '12'))
        self.assertTrue(all(len(s.sha256) == 64 for s in wb.SOURCES))
        self.assertEqual(wb.SOURCE_PX, worldmap.TILE * wb.per_source(worldmap.TILE_LEVELS - 1),
                         'the deepest level is the sources\' own resolution')
        self.assertEqual(wb.SOURCE_PX * wb.COLUMNS, 360 * 240)


class DeveloperFolder(unittest.TestCase):
    """The real release folder, where the developer's model roots have it (not on CI)."""

    def test_folder(self) -> None:
        folder = obj_model.model_dir(worldmap.TEXTURE_DIR)
        if folder is None:
            self.skipTest(f'no {worldmap.TEXTURE_DIR} in {obj_model.model_roots()}')
        entries, why = worldmap_pack._index(folder)
        self.assertIsNone(why)
        self.assertEqual(sorted(os.listdir(folder)), sorted([tiles_name(), 'SOURCE.txt', worldmap.TEXTURE_INDEX]))
        path = os.path.join(folder, tiles_name())
        self.assertEqual(os.path.getsize(path), entries[tiles_name()]['size'])
        with open(path, 'rb') as f:
            head = f.read(16 + 12 * worldmap.tile_count())
        table = worldmap.tile_table(head, os.path.getsize(path))
        self.assertEqual(len(table), worldmap.tile_count())


if __name__ == '__main__':
    unittest.main()
