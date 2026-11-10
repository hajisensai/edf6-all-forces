"""The world map's base textures in the model pack (docs/plan-2026-11-10-worldmap.md §2): tools/worldmap_basemap.py
textures builds the release folder from small synthetic images (DXT1, full mip chain, index, credit, nothing else), and
tools/worldmap_pack.py installs it under Mods as its ledger owner, refuses a missing / wrong / foreign set without a
substitute, and releases what it installed. With the developer's model folder present also that folder is
consistent (its index, the two 4096 textures)."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
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
import texfile  # noqa: E402
import worldmap  # noqa: E402
import worldmap_basemap as wb  # noqa: E402
import worldmap_pack  # noqa: E402

SMALL = {'world': (64, 32), 'japan': (64, 64)}


def build_small(work: str) -> str:
    """The release folder from two synthetic derived PNGs; returns its path."""
    from PIL import Image
    src = os.path.join(work, 'worldmap')
    os.makedirs(src)
    for kind, (w, h) in SMALL.items():
        im = Image.new('RGB', (w, h), (10, 40, 120))
        for x in range(w // 2):
            im.putpixel((x, x % h), (200, 180, 90))
        im.save(os.path.join(src, wb.TEXTURE_SOURCES[kind]))
    with mock.patch.dict(wb.TEXTURE_SIZES, SMALL):
        wb.textures(src)
    return os.path.join(work, worldmap.TEXTURE_DIR)


def full_chain(w: int, h: int) -> int:
    n = 1
    while w > 1 or h > 1:
        w, h, n = max(1, w // 2), max(1, h // 2), n + 1
    return n


class Build(unittest.TestCase):
    def test_release_folder(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            out = build_small(work)
            names = set(worldmap_pack.files()) | {'SOURCE.txt', worldmap.TEXTURE_INDEX}
            self.assertEqual(set(os.listdir(out)), names, 'only the shipped files, never the sources')
            with open(os.path.join(out, worldmap.TEXTURE_INDEX), encoding='utf-8') as f:
                index = json.load(f)
            self.assertEqual(index['version'], worldmap.TEXTURE_VERSION)
            for kind, rel in worldmap.TEXTURES.items():
                name = rel.rsplit('/', 1)[1]
                data = Path(out, name).read_bytes()
                self.assertEqual(index['files'][name], {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)})
                info = texfile.dds_info(data)
                w, h = SMALL[kind]
                self.assertEqual((info.width, info.height, info.fourcc), (w, h, b'DXT1'))
                self.assertEqual(info.mips, full_chain(w, h), 'a full mip chain to 1 x 1')
                self.assertEqual(struct.unpack_from('<I', data, 0x6C)[0], texfile.CAPS, 'the stock mipmapped caps')
                body = sum(max(1, (max(1, w >> i) + 3) // 4) * max(1, (max(1, h >> i) + 3) // 4) * 8
                           for i in range(info.mips))
                self.assertEqual(len(data), 128 + body, 'the legacy header, no DX10 header')
            credit = Path(out, 'SOURCE.txt').read_text(encoding='utf-8')
            self.assertIn('NASA Earth Observatory', credit)
            self.assertIn(wb.PAGE_URL, credit)

    def test_wrong_size_refused(self) -> None:
        from PIL import Image
        with tempfile.TemporaryDirectory() as work:
            src = os.path.join(work, 'worldmap')
            os.makedirs(src)
            for kind in SMALL:
                Image.new('RGB', (8, 8)).save(os.path.join(src, wb.TEXTURE_SOURCES[kind]))
            with mock.patch.dict(wb.TEXTURE_SIZES, SMALL), self.assertRaises(SystemExit):
                wb.textures(src)


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
            self.assertEqual(sorted(led.owned_by(worldmap_pack.OWNER)), sorted(ledger.key(r) for r in worldmap.TEXTURES.values()))
            for name, rel in worldmap_pack.files().items():
                self.assertEqual(Path(led.disk(rel)).read_bytes(), Path(pack, name).read_bytes())
            self.assertEqual(len(written), 2)
            again, skipped = self.run_install(game, pack)
            self.assertEqual((again, skipped), ([], []), 'the same bytes are not written again')
            deleted, kept = worldmap_pack.remove(game)
            self.assertEqual((len(deleted), kept), (2, []))
            self.assertFalse(any(os.path.exists(led.disk(r)) for r in worldmap.TEXTURES.values()))

    def check_refused(self, game: str, folder: str | None, needle: str) -> None:
        old = ledger.Ledger(game).put(worldmap_pack.OWNER, worldmap.TEXTURES['world'], b'old texture')
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
            index['version'] = worldmap.TEXTURE_VERSION + 1
            Path(p).write_text(json.dumps(index), encoding='utf-8')
            game = os.path.join(work, 'game')
            os.makedirs(game)
            self.check_refused(game, pack, '版本')

    def test_wrong_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            name = next(iter(worldmap_pack.files()))
            data = bytearray(Path(pack, name).read_bytes())
            data[-1] ^= 0xFF
            Path(pack, name).write_bytes(bytes(data))
            game = os.path.join(work, 'game')
            os.makedirs(game)
            self.check_refused(game, pack, '内容不对')

    def test_foreign_file_kept(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            pack = build_small(work)
            game = os.path.join(work, 'game')
            os.makedirs(game)
            led = ledger.Ledger(game)
            theirs = led.disk(worldmap.TEXTURES['japan'])
            os.makedirs(os.path.dirname(theirs), exist_ok=True)
            Path(theirs).write_bytes(b'another mod')
            written, skipped = self.run_install(game, pack)
            self.assertEqual(written, [])
            self.assertEqual(len(skipped), 1)
            self.assertEqual(Path(theirs).read_bytes(), b'another mod')
            self.assertFalse(os.path.exists(led.disk(worldmap.TEXTURES['world'])), 'both or neither')


class Wiring(unittest.TestCase):
    def test_release_and_installer(self) -> None:
        self.assertIn(worldmap.TEXTURE_DIR, build_release.RELEASE_MODELS)
        self.assertIn(worldmap_pack.OWNER, ledger.OWNERS)
        inst = (ROOT / 'tools' / 'installer.py').read_text(encoding='utf-8')
        self.assertIn('worldmap_pack.install(game)', inst)
        self.assertIn('worldmap_pack.remove,', inst)
        inc = (ROOT / 'src' / 'worldmap.inc').read_text(encoding='utf-8')
        for kind, name in (('world', 'kWorldTexture'), ('japan', 'kJapanTexture')):
            self.assertIn(f'{name}=L"{worldmap.app_path(worldmap.TEXTURES[kind])}"', inc)
        self.assertEqual(worldmap.app_path('UI/EDF6VC_WORLDMAP_WORLD.DDS'), 'app:/UI/EDF6VC_WORLDMAP_WORLD.dds')


class DeveloperFolder(unittest.TestCase):
    """The real release folder, where the developer's model roots have it (not on CI)."""

    def test_folder(self) -> None:
        folder = obj_model.model_dir(worldmap.TEXTURE_DIR)
        if folder is None:
            self.skipTest(f'no {worldmap.TEXTURE_DIR} in {obj_model.model_roots()}')
        entries, why = worldmap_pack._index(folder)
        self.assertIsNone(why)
        self.assertEqual(sorted(os.listdir(folder)), sorted([*worldmap_pack.files(), 'SOURCE.txt', worldmap.TEXTURE_INDEX]))
        for kind, rel in worldmap.TEXTURES.items():
            name = rel.rsplit('/', 1)[1]
            with open(os.path.join(folder, name), 'rb') as f:
                head = f.read(128)
            self.assertEqual(os.path.getsize(os.path.join(folder, name)), entries[name]['size'])
            info = texfile.dds_info(head + bytes(8))
            self.assertEqual((info.width, info.height, info.fourcc), (*wb.TEXTURE_SIZES[kind], b'DXT1'))
            self.assertEqual(info.mips, full_chain(info.width, info.height))


if __name__ == '__main__':
    unittest.main()
