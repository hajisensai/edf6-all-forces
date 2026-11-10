"""native/dxt1.cpp against texfile's pure Python DXT1 encoder and texfile_accel's numpy one: the same bytes for every
image (random noise, flat and two-colour blocks, gradients, odd sizes past the edges)."""
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import edf6native  # noqa: E402
import texfile  # noqa: E402
import texfile_accel  # noqa: E402


def images() -> list[texfile.Image]:
    rng = random.Random(6611)
    out = []
    for w, h in ((1, 1), (3, 2), (4, 4), (5, 7), (17, 11), (64, 63), (130, 66)):
        for mode in range(4):
            raw = bytearray()
            for y in range(h):
                for x in range(w):
                    if mode == 0:
                        px = [rng.randrange(256) for _ in range(3)]
                    elif mode == 1:
                        px = [(x * 7 + y * 3) % 256, (x * 2 + 200 - y) % 256, (y * 5) % 256]
                    elif mode == 2:
                        px = [40, 90, 200] if (x // 2 + y // 3) % 2 else [210, 30, 60]
                    else:
                        k = rng.randrange(-60, 61)
                        px = [max(0, min(255, 128 + k + rng.randrange(-4, 5))), max(0, min(255, 128 - k)),
                              max(0, min(255, 128 + k // 2))]
                    raw += bytes(px + [255])
            out.append(texfile.Image(w, h, raw))
    return out


class NativeDxt1Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertIsNotNone(edf6native.lib(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_levels_match_reference(self) -> None:
        import numpy as np
        for img in images():
            px = np.frombuffer(bytes(img.rgba), dtype=np.uint8).reshape(img.height, img.width, 4)
            with self.subTest(w=img.width, h=img.height):
                native = texfile_accel._level(px)
                self.assertEqual(native, texfile._dxt1_level(img))
                self.assertEqual(native, texfile_accel._level_numpy(px))

    def test_dds_matches_reference(self) -> None:
        for img in images()[-8:]:
            with self.subTest(w=img.width, h=img.height):
                self.assertEqual(texfile_accel.dxt1_dds(img), texfile.dxt1_dds(img))


if __name__ == '__main__':
    unittest.main()
