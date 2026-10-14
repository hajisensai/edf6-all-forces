"""Codec identity, cache invalidation and compressed archive regressions."""
from pathlib import Path
import os
import random
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import mdb
import obj_model
import texfile
import texfile_accel as fast


class TextureAccelerationTests(unittest.TestCase):
    def test_png_decoder_fast_path_matches_reference_dds(self) -> None:
        from PIL import Image
        rng = random.Random(817)
        with tempfile.TemporaryDirectory() as folder, patch.object(fast, '_dds_cache', fast._ByteCache(1024 * 1024)):
            for mode in ('RGB', 'RGBA'):
                path = Path(folder, mode + '.png')
                image = Image.frombytes(mode, (17, 19), rng.randbytes(17 * 19 * len(mode)))
                image.save(path)
                expected = texfile.dxt1_dds(texfile.decode_png(path.read_bytes()))
                self.assertEqual(fast.texture_dds(str(path)), expected)

    def test_exact_mips_blocks_ties_and_odd_dimensions(self) -> None:
        rng = random.Random(913)
        for width, height in ((1, 1), (1, 9), (13, 1), (2, 3), (3, 2), (4, 4), (5, 7), (17, 11), (64, 63), (257, 129)):
            for mode in range(3):
                with self.subTest(width=width, height=height, mode=mode):
                    raw = bytearray(rng.randrange(256) if mode == 0 else (i % 17 if mode == 1 else 128)
                                    for i in range(width * height * 4))
                    image = texfile.Image(width, height, raw)
                    self.assertEqual(fast.dxt1_dds(image), texfile.dxt1_dds(image))

    def test_cache_shares_identical_content_at_different_paths(self) -> None:
        from PIL import Image
        with tempfile.TemporaryDirectory() as folder, patch.object(fast, '_dds_cache', fast._ByteCache(1024 * 1024)):
            a, b = Path(folder, 'a.png'), Path(folder, 'b.png')
            Image.new('RGB', (32, 32), (33, 44, 55)).save(a)
            b.write_bytes(a.read_bytes())
            with patch.object(fast, 'dxt1_dds', wraps=fast.dxt1_dds) as encode:
                self.assertEqual(fast.texture_dds(str(a)), fast.texture_dds(str(b)))
                self.assertEqual(encode.call_count, 1)

    def test_content_change_invalidates_same_path_size_and_mtime(self) -> None:
        with tempfile.TemporaryDirectory() as folder, patch.object(fast, '_dds_cache', fast._ByteCache(1024 * 1024)):
            path = Path(folder, 'same.dds')
            first = texfile.solid_dxt1((10, 20, 30), 32)
            second = texfile.solid_dxt1((30, 40, 50), 32)
            self.assertEqual(len(first), len(second))
            path.write_bytes(first)
            before = path.stat()
            self.assertEqual(fast.texture_dds(str(path)), first)
            path.write_bytes(second)
            os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
            self.assertEqual(fast.texture_dds(str(path)), second)

    def test_cached_compression_preserves_complete_rab_bytes(self) -> None:
        def archive() -> mdb.Rab:
            return mdb.Rab(1, ['MODEL'], [mdb.RabFile('test.mdb', 0, 0, b'model')])
        dds = texfile.solid_dxt1((10, 20, 30), 64)
        old = archive()
        obj_model.add_texture(old, 'test.dds', dds)
        with patch.object(fast, '_packed_cache', fast._ByteCache(1024 * 1024)), \
                patch.object(fast, 'cmpl_compress', wraps=mdb.cmpl_compress) as encode:
            for _ in range(2):
                new = archive()
                obj_model.add_texture(new, 'test.dds', dds, compressor=fast.compress_texture)
                self.assertEqual(mdb.rab_write(new), mdb.rab_write(old))
            self.assertEqual(encode.call_count, 2)  # HD + LOD once each

    def test_cache_is_bounded_and_evicts_least_recently_used(self) -> None:
        cache = fast._ByteCache(8)
        cache.put('a', b'aaaa')
        cache.put('b', b'bbbb')
        self.assertEqual(cache.get('a'), b'aaaa')
        cache.put('c', b'cccc')
        self.assertIsNone(cache.get('b'))
        self.assertEqual(cache.size, 8)
        cache.put('large', b'0' * 9)
        self.assertIsNone(cache.get('large'))
        self.assertEqual(cache.size, 8)


if __name__ == '__main__':
    unittest.main()
