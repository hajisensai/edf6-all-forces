"""BC5U normal maps (pylib/texfile_accel.py bc5_normal_dds, texfile.solid_bc5): the stock layout, the HD / .lod pair,
round-trip error against DXT1 on synthetic and real normal maps, determinism, and the green-axis measurement
(docs/texture-quality.md). The stock comparisons read the game's Root.cpk when it is there (read only) and are
skipped without it."""
from pathlib import Path
import struct
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import texfile  # noqa: E402
import texfile_accel as fa  # noqa: E402

WT_NORMAL = ROOT / 'models' / 'wt_bmpt' / 'textures' / 'bmpt_terminator_body_n_n.jpg'


def bumps(size: int, seed: int, grooves: bool = True) -> np.ndarray:
    """A DirectX-green normal map (H x W x 3 uint8) of a random height field: round bumps and panel grooves."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    h = np.zeros((size, size))
    for _ in range(24):
        cx, cy, r, a = rng.uniform(0, size), rng.uniform(0, size), rng.uniform(3, size / 6), rng.uniform(-3, 3)
        h += a * np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * r * r))
    if grooves:
        h -= 2.0 * (np.abs(((x + 7) % 37) - 18) < 1) + 2.0 * (np.abs(((y + 3) % 41) - 20) < 1)
    gx = np.gradient(h, axis=1)
    gy = np.gradient(h, axis=0)
    n = np.stack((-gx, -gy, np.ones_like(h)), -1)          # +Y = +row (DirectX)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip(np.rint(n * 127.5 + 127.5), 0, 255).astype(np.uint8)


def image(rgb: np.ndarray) -> texfile.Image:
    h, w = rgb.shape[:2]
    rgba = np.concatenate((rgb, np.full((h, w, 1), 255, np.uint8)), -1)
    return texfile.Image(w, h, bytearray(rgba.tobytes()))


def angles(img: texfile.Image, xy: np.ndarray) -> np.ndarray:
    """Degrees between the image's unit normals and the decoded X, Y (Z rebuilt as the shader does)."""
    ref = fa.normal_vectors(img)
    x, y = xy[..., 0], xy[..., 1]
    n = np.stack((x, y, np.sqrt(np.clip(1 - x * x - y * y, 0, 1))), -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.degrees(np.arccos(np.clip((n * ref).sum(-1), -1, 1)))


def errors(img: texfile.Image) -> tuple[np.ndarray, np.ndarray]:
    """(BC5, DXT1) top-level angular errors of `img`."""
    w, h = img.width, img.height
    bc5 = fa.decode_bc5(fa.bc5_normal_dds(img)[128:], w, h) * (2 / 255) - 1
    dxt1 = fa.decode_dxt1(fa.dxt1_dds(img)[128:], w, h)[..., :2] * (2 / 255) - 1
    return angles(img, bc5), angles(img, dxt1)


class Bc5Layout(unittest.TestCase):
    def test_header_and_chain_match_the_stock_layout(self) -> None:
        dds = fa.bc5_normal_dds(image(bumps(256, 1)))
        info = texfile.dds_info(dds)
        self.assertEqual((info.width, info.height, info.mips, info.fourcc), (256, 256, 9, b'BC5U'))
        flags, height, width, linear = struct.unpack_from('<4I', dds, 8)
        self.assertEqual((flags, linear), (0xA1007, 64 * 64 * 16))          # every stock BC5U file: 0xA1007
        self.assertEqual(struct.unpack_from('<I', dds, 108)[0], texfile.CAPS)
        self.assertEqual(struct.unpack_from('<II4s', dds, 76), (32, 4, b'BC5U'))
        self.assertEqual(len(dds), 128 + sum(max(1, (256 >> k) // 4) ** 2 * 16 for k in range(9)))

    def test_texture_pair_slices_it_as_stock(self) -> None:
        for size in ((2048, 2048), (512, 256), (64, 64), (16, 16)):
            rgb = np.zeros((size[1], size[0], 3), np.uint8) + np.array((128, 128, 255), np.uint8)
            hd, lod = texfile.texture_pair(fa.bc5_normal_dds(image(rgb)))
            self.assertIsNone(texfile.pair_problem(hd, lod), size)
            self.assertEqual(texfile.dds_info(lod).fourcc, b'BC5U')

    def test_odd_sizes_encode_and_decode(self) -> None:
        img = image(bumps(64, 2)[:19, :37])
        dds = fa.bc5_normal_dds(img)
        self.assertEqual(texfile.dds_info(dds).mips, 6)                     # 37x19 .. 1x1
        bc5, dxt1 = errors(img)
        self.assertLess(bc5.mean(), dxt1.mean())

    def test_solid_matches_the_encoder_of_a_flat_map(self) -> None:
        rgb = np.zeros((32, 32, 3), np.uint8) + np.array((128, 128, 255), np.uint8)
        self.assertEqual(texfile.solid_bc5(texfile.FLAT_NORMAL_XY, 32), fa.bc5_normal_dds(image(rgb)))
        flat = fa.decode_bc5(texfile.solid_bc5()[128:], 16, 16)
        self.assertTrue((flat == 128).all())

    def test_deterministic(self) -> None:
        img = image(bumps(128, 3))
        self.assertEqual(fa.bc5_normal_dds(img), fa.bc5_normal_dds(img))

    def test_two_value_blocks_are_exact(self) -> None:
        """A block of two values (or one) is representable exactly in the 8-value mode: no error."""
        rng = np.random.default_rng(4)
        values = np.where(rng.random((64, 16)) < 0.5, 40, 200).astype(np.uint8)
        values[:8] = 77
        out = np.frombuffer(fa._bc4_blocks(values), np.uint8).reshape(-1, 8)
        for k in range(len(values)):
            decoded = fa.decode_bc4(out[k].tobytes(), 4, 4).reshape(16)
            np.testing.assert_array_equal(decoded, values[k])

    def test_mips_stay_unit_length(self) -> None:
        dds = fa.bc5_normal_dds(image(bumps(64, 5)))
        pos, w = 128 + 16 * 16 * 16, 32
        xy = fa.decode_bc5(dds[pos:], w, w) * (2 / 255) - 1
        self.assertLessEqual(float((xy ** 2).sum(-1).max()), 1.02)


class Bc5Quality(unittest.TestCase):
    def test_synthetic_normals_beat_dxt1(self) -> None:
        bc5, dxt1 = errors(image(bumps(256, 6)))
        self.assertLess(bc5.mean(), 0.5)
        self.assertLess(bc5.mean() * 3, dxt1.mean())
        self.assertLess(np.percentile(bc5, 99) * 2, np.percentile(dxt1, 99))

    @unittest.skipUnless(WT_NORMAL.is_file(), 'the War Thunder BMPT model folder is not in this checkout')
    def test_real_normal_map_beats_dxt1(self) -> None:
        from PIL import Image
        with Image.open(WT_NORMAL) as im:
            rgb = np.asarray(im.convert('RGB'))[:512, :512]
        bc5, dxt1 = errors(image(np.ascontiguousarray(rgb)))
        self.assertLess(bc5.mean(), 1.0)
        self.assertLess(bc5.mean() * 3, dxt1.mean())


class GreenAxis(unittest.TestCase):
    def test_score_sign_and_conversion(self) -> None:
        dx = bumps(128, 7)
        self.assertGreater(fa.green_down_score(dx), 0.3)
        gl = dx.copy()
        gl[..., 1] = 255 - gl[..., 1]
        self.assertLess(fa.green_down_score(gl), -0.3)
        np.testing.assert_array_equal(fa.directx_normals(gl, 'up', 'gl'), dx)
        np.testing.assert_array_equal(fa.directx_normals(dx, 'down', 'dx'), dx)
        with self.assertRaises(texfile.TextureError):
            fa.directx_normals(gl, 'down', 'gl declared dx')
        with self.assertRaises(texfile.TextureError):
            fa.directx_normals(dx, 'sideways', 'bad')


def _stock_game():  # noqa: ANN202
    try:
        import rootcpk
        return rootcpk.default()
    except Exception:  # noqa: BLE001 - no game here: the stock comparisons skip
        return None


class Stock(unittest.TestCase):
    """The format and the green, against the game's own vehicles (Root.cpk, read only)."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.game = _stock_game()
        if cls.game is None:
            raise unittest.SkipTest('no EDF6 Root.cpk on this machine')

    def member(self, archive: str, name: str) -> bytes:
        import mdb
        rab = mdb.rab_read(self.game.read('OBJECT', archive))
        return next(f.data for f in rab.files if f.name == name)

    def test_stock_bc5_layout_and_green(self) -> None:
        for archive, name in (('V601_TANK.MRAB', 'v601_body_nor.DDS'), ('V603_FLAK.MRAB', 'v603_body_nor.DDS'),
                              ('V505_TANK.MRAB', 'v505_tank_body_nor.DDS')):
            stock = self.member(archive, name)
            info = texfile.dds_info(stock)
            ours = fa.bc5_normal_dds(image(np.zeros((info.height, info.width, 3), np.uint8) + 128))
            self.assertEqual(stock[:128], ours[:128], name)              # byte for byte the same header
            self.assertEqual(len(stock), len(ours), name)                # the same mip chain
            xy = fa.decode_bc5(stock[128:], info.width, info.height)
            rgb = np.concatenate((xy, np.full(xy.shape[:2] + (1,), 255.0)), -1).astype(np.uint8)
            self.assertGreater(fa.green_down_score(rgb), 0.3, name)      # DirectX green, as our maps are made

    def test_reencoding_a_stock_map_stays_close(self) -> None:
        stock = self.member('V601_TANK.MRAB', 'v601_body_nor.DDS')
        xy = fa.decode_bc5(stock[128:], 2048, 2048)[:512, :512] * (2 / 255) - 1
        z = np.sqrt(np.clip(1 - (xy ** 2).sum(-1), 0, 1))
        rgb = np.clip(np.rint(np.dstack((xy, z)) * 127.5 + 127.5), 0, 255).astype(np.uint8)
        bc5, dxt1 = errors(image(rgb))
        self.assertLess(bc5.mean(), 0.5)
        self.assertLess(bc5.mean() * 3, dxt1.mean())


if __name__ == '__main__':
    unittest.main()
