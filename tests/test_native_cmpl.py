"""native/cmpl.cpp against mdb's pure Python CMPL codec: the same bytes for every input, the same errors."""
from pathlib import Path
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import mdb


def inputs() -> list[bytes]:
    rng = random.Random(4096)
    cases = [b'', b'\x00', b'A', b'AB' * 9, bytes(17), bytes(18), bytes(19), bytes(5000)]
    for size in (4095, 4096, 4097, 4114, 8209, 70001):   # around the window (N) and window + lookahead (N + F)
        cases.append(rng.randbytes(size))
    # long matches (the tree's mlen >= F branch), matches across the window's wrap, few-symbol text
    cases.append(b''.join(rng.choice((b'mesh', b'bone', b'\x00\x00\x80\x3f', b'EDF6')) for _ in range(30000)))
    cases.append(bytes(rng.choice(b'\x00\x01\xff') for _ in range(50000)))
    block = rng.randbytes(300)
    cases.append(b''.join(block if rng.random() < 0.6 else rng.randbytes(rng.randrange(1, 40)) for _ in range(400)))
    # model-like: float vertex rows with small perturbations
    cases.append(b''.join(struct.pack('<3f', rng.randrange(64) / 8, 1.0, rng.randrange(8) / 4) for _ in range(20000)))
    return cases


class NativeCmplTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertTrue(mdb.native_cmpl(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_compress_matches_reference(self) -> None:
        for data in inputs():
            with self.subTest(size=len(data)):
                self.assertEqual(mdb.cmpl_compress(data), mdb.cmpl_compress_py(data))

    def test_decompress_matches_reference(self) -> None:
        for data in inputs():
            stream = mdb.cmpl_compress_py(data)
            with self.subTest(size=len(data)):
                self.assertEqual(mdb.cmpl_decompress(stream), data)
                self.assertEqual(mdb.cmpl_decompress(stream), mdb.cmpl_decompress_py(stream))

    def test_bytearray_and_memoryview_inputs(self) -> None:
        data = random.Random(7).randbytes(9000)
        stream = mdb.cmpl_compress_py(data)
        self.assertEqual(mdb.cmpl_compress(bytearray(data)), stream)
        self.assertEqual(mdb.cmpl_compress(memoryview(data)), stream)
        self.assertEqual(mdb.cmpl_decompress(bytearray(stream)), data)

    def test_errors_and_passthrough_match_reference(self) -> None:
        stream = mdb.cmpl_compress_py(random.Random(11).randbytes(3000))
        for cut in (len(stream) - 1, len(stream) // 2, 9, 8):
            with self.subTest(cut=cut):
                with self.assertRaises(ValueError) as fast:
                    mdb.cmpl_decompress(stream[:cut])
                with self.assertRaises(ValueError) as slow:
                    mdb.cmpl_decompress_py(stream[:cut])
                self.assertEqual(str(fast.exception), str(slow.exception))
        with self.assertRaises(ValueError):
            mdb.cmpl_decompress(b'CMPL\x00')
        self.assertEqual(mdb.cmpl_decompress(b'raw bytes'), b'raw bytes')


if __name__ == '__main__':
    unittest.main()
