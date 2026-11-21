"""BC7 colour maps (docs/model-quality-1121-bc7.md): native/bc7.cpp against pylib/bc7.py's reference encoder (the same
bytes for every image, however many threads), the blocks decoded by bc7.py and by Pillow alike, the quality against
DXT1 (PSNR), texfile's DX10 headers and HD / .lod pairs, and the stock BC7 maps read back (Root.cpk, skipped without)."""
from pathlib import Path
import ctypes
import io
import math
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import bc7  # noqa: E402
import edf6native  # noqa: E402
import texfile  # noqa: E402
import texfile_accel  # noqa: E402

REPO = Path(__file__).resolve().parents[1]


def images() -> list[texfile.Image]:
    rng = random.Random(7121)
    out = []
    for w, h in ((1, 1), (3, 2), (4, 4), (5, 7), (17, 11), (36, 21)):
        for mode in range(5):
            raw = bytearray()
            for y in range(h):
                for x in range(w):
                    if mode == 0:
                        px = [rng.randrange(256) for _ in range(3)]
                    elif mode == 1:
                        px = [(x * 7 + y * 3) % 256, (x * 2 + 200 - y) % 256, (y * 5) % 256]
                    elif mode == 2:
                        px = [40, 90, 200] if (x // 2 + y // 3) % 2 else [210, 30, 60]
                    elif mode == 3:
                        k = rng.randrange(-60, 61)
                        px = [max(0, min(255, 128 + k + rng.randrange(-4, 5))), max(0, min(255, 128 - k)),
                              max(0, min(255, 128 + k // 2))]
                    else:
                        px = [100 + (x > y) * 90, 100, 60 + 4 * x] if (x + y) % 9 else [250, 250, 250]
                    raw += bytes(px + [rng.randrange(256)])     # alpha ignored
            out.append(texfile.Image(w, h, raw))
    return out


def pil_rgba(dds: bytes):  # noqa: ANN201 - numpy (h, w, 4)
    import numpy as np
    from PIL import Image
    with Image.open(io.BytesIO(dds)) as im:
        return np.asarray(im.convert('RGBA'))


def psnr(a, b) -> float:  # noqa: ANN001
    import numpy as np
    mse = float(np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2))
    return 99.0 if mse == 0 else 10 * math.log10(255 ** 2 / mse)


class NativeBc7(unittest.TestCase):
    def setUp(self) -> None:
        self.assertIsNotNone(edf6native.lib(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_levels_match_reference_for_any_thread_count(self) -> None:
        import numpy as np
        lib = edf6native.lib()
        for img in images():
            with self.subTest(w=img.width, h=img.height):
                ref = bc7.encode_level(bytes(img.rgba), img.width, img.height)
                px = np.frombuffer(bytes(img.rgba), np.uint8).reshape(img.height, img.width, 4)
                self.assertEqual(texfile_accel._bc7_level(px), ref)
                for threads in (1, 2, 5):
                    out = ctypes.create_string_buffer(len(ref))
                    self.assertEqual(lib.edf6_bc7_level(bytes(img.rgba), img.width, img.height, out, threads), len(ref))
                    self.assertEqual(out.raw, ref)

    def test_dds_matches_reference(self) -> None:
        for img in images()[-10:]:
            with self.subTest(w=img.width, h=img.height):
                self.assertEqual(texfile_accel.bc7_dds(img), texfile.bc7_dds(img))

    def test_pillow_decodes_what_bc7_py_decodes_and_alpha_is_opaque(self) -> None:
        import numpy as np
        for img in images()[-5:]:
            dds = texfile_accel.bc7_dds(img)
            mine = np.frombuffer(bc7.decode_level(dds[148:], img.width, img.height), np.uint8).reshape(img.height,
                                                                                                      img.width, 4)
            self.assertTrue(np.array_equal(pil_rgba(dds), mine))
            self.assertTrue((mine[..., 3] == 255).all())

    def test_decoder_matches_pillow_in_every_mode(self) -> None:
        import numpy as np
        rng = random.Random(77)
        raw = bytearray()
        for k in range(512):
            b = bytearray(rng.randbytes(16))
            mode = k % 8
            b[0] = (b[0] & ~((1 << (mode + 1)) - 1) & 0xFF) | (1 << mode)
            raw += b
        w, h = 4 * 32, 4 * 16
        dds = texfile._dx10_header(w, h, 1, texfile.DXGI_BC7_UNORM, len(raw)) + bytes(raw)
        mine = np.frombuffer(bc7.decode_level(bytes(raw), w, h), np.uint8).reshape(h, w, 4)
        self.assertTrue(np.array_equal(pil_rgba(dds), mine))

    def test_flat_colours_are_exact(self) -> None:
        for rgb in ((0, 0, 0), (255, 255, 255), (128, 64, 2), (37, 200, 118)):
            img = texfile.Image(8, 8, bytearray(bytes((*rgb, 255)) * 64))
            dds = texfile_accel.bc7_dds(img)
            self.assertEqual(set(bc7.decode_block(dds[148:164])), {(*rgb, 255)})

    def test_quality_over_dxt1(self) -> None:
        """BC7 against the DXT1 encoder on a model's own colour map (repository) and a synthetic one."""
        import numpy as np
        from PIL import Image
        with Image.open(REPO / 'models/wt_bmpt/textures/bmpt_terminator_body_c_ussr_camo_green.jpg') as im:
            photo = np.asarray(im.convert('RGBA'))[512:768, 512:768]
        y, x = np.mgrid[0:128, 0:128]
        synth = np.dstack(((x * 2) % 256, (y * 3 + (x > 64) * 80) % 256, ((x + y) * 5) % 256,
                           np.full_like(x, 255))).astype(np.uint8)
        for name, rgba, gain in (('photo', photo, 6.0), ('synthetic', synth, 6.0)):
            img = texfile.Image(rgba.shape[1], rgba.shape[0], bytearray(np.ascontiguousarray(rgba).tobytes()))
            p1 = psnr(rgba[..., :3], pil_rgba(texfile_accel.dxt1_dds(img))[..., :3])
            p7 = psnr(rgba[..., :3], pil_rgba(texfile_accel.bc7_dds(img))[..., :3])
            with self.subTest(name=name, bc1=round(p1, 2), bc7=round(p7, 2)):
                self.assertGreater(p7, p1 + gain)


class DdsLayout(unittest.TestCase):
    def test_header_and_pair_as_stock(self) -> None:
        img = texfile.Image(64, 32, bytearray(bytes((90, 120, 30, 255)) * (64 * 32)))
        dds = texfile.bc7_dds(img)
        info = texfile.dds_info(dds)
        self.assertEqual((info.width, info.height, info.mips, info.fourcc, info.dxgi), (64, 32, 7, b'DX10', 98))
        self.assertEqual(struct.unpack_from('<5I', dds, 128), (98, 3, 0, 1, 0))
        self.assertEqual(struct.unpack_from('<7I', dds, 4), (124, 0xA1007, 32, 64, 16 * 8 * 16, 1, 7))
        self.assertEqual(struct.unpack_from('<I', dds, 108)[0], 0x401008)
        self.assertEqual(len(dds), 148 + 16 * (16 * 8 + 8 * 4 + 4 * 2 + 2 + 1 + 1 + 1))
        hd, lod = texfile.texture_pair(dds)
        li = texfile.dds_info(lod)
        self.assertEqual((li.width, li.height, li.mips, li.dxgi), (32, 16, 5, 98))     # 16 px on the shorter side
        self.assertEqual(lod[148:], dds[148 + 16 * 16 * 8:][:len(lod) - 148])
        self.assertIsNone(texfile.pair_problem(hd, lod))
        self.assertEqual(texfile.pair_problem(hd, texfile.dds_tail(dds, 1, 6)), '.lod has 6 mip levels, the stock layout 5')
        self.assertIn('HD', texfile.pair_problem(hd, texfile.solid_dxt1((1, 2, 3), 16)))

    def test_refuses_other_dx10_textures(self) -> None:
        dds = bytearray(texfile.bc7_dds(texfile.Image(4, 4, bytearray(64))))
        for at, value in ((128, 99), (128, 71), (132, 4), (136, 4), (140, 6)):     # sRGB, BC1, 3D, cube, array
            bad = bytearray(dds)
            struct.pack_into('<I', bad, at, value)
            with self.subTest(at=at, value=value), self.assertRaises(texfile.TextureError):
                texfile.dds_info(bytes(bad))


def _stock_game():  # noqa: ANN202
    try:
        import rootcpk
        return rootcpk.default()
    except Exception:  # noqa: BLE001 - no game here: the stock comparisons skip
        return None


class Stock(unittest.TestCase):
    """The game's own BC7 colour maps (Root.cpk, read only): the layout texfile writes, decoded as Pillow does."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.game = _stock_game()
        if cls.game is None:
            raise unittest.SkipTest('no EDF6 Root.cpk on this machine')

    def test_stock_albedo_reads_back(self) -> None:
        import mdb
        import numpy as np
        for archive, stem in (('N603_COLONEL.MRAB', 'n603_colonel_body_col'), ('GIANTANT.MRAB', 'ariwing-col')):
            rab = mdb.rab_read(self.game.read('OBJECT', archive))
            files = {f.name.lower(): mdb.cmpl_decompress(f.data) for f in rab.files}
            hd, lod = files[f'{stem}.dds'.lower()], files[f'{stem}.lod.dds'.lower()]
            with self.subTest(archive=archive):
                a, b = texfile.dds_info(hd), texfile.dds_info(lod)
                self.assertEqual((a.fourcc, a.dxgi, b.dxgi), (b'DX10', 98, 98))
                self.assertIsNone(texfile.pair_problem(hd, lod))
                top = max(1, (a.width + 3) // 4) * max(1, (a.height + 3) // 4) * 16
                self.assertEqual(hd[:148], texfile._dx10_header(a.width, a.height, a.mips, 98, top))   # what we write
                ltop = max(1, (b.width + 3) // 4) * max(1, (b.height + 3) // 4) * 16
                self.assertEqual(lod[:148], texfile._dx10_header(b.width, b.height, b.mips, 98, ltop))
                self.assertEqual((b.width, b.height, b.mips), (texfile.dds_info(texfile.texture_pair(hd)[1]).width,
                                                               texfile.dds_info(texfile.texture_pair(hd)[1]).height,
                                                               texfile.dds_info(texfile.texture_pair(hd)[1]).mips))
                mine = np.frombuffer(bc7.decode_level(lod[148:], b.width, b.height), np.uint8)
                self.assertTrue(np.array_equal(pil_rgba(lod).reshape(-1), mine))
                modes = {next(m for m in range(8) if lod[148 + 16 * k] >> m & 1) for k in range(len(lod[148:]) // 16)
                         if lod[148 + 16 * k]}
                self.assertGreater(len(modes), 2)          # the stock encoder uses several modes: all decoded


if __name__ == '__main__':
    unittest.main()
