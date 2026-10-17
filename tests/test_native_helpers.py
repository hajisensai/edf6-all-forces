"""native/crilayla.cpp, lzss_greedy.cpp and aabb_tree.cpp against their pure Python references (crilayla.decompress_py,
fmb_build.cmpl_fast_py, hkcms_build._section_tree_py / simd_tree_py): the same bytes for every input, the same errors
where the native one hands an input back. No game needed; with EDF6 found (pylib/gamedir.py), some of its real CPK
entries too."""
from pathlib import Path
import os
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import numpy as np

import crilayla
import edf6native
import fmb_build
import hkcms_build
import mdb


def cri_stream(tokens: list) -> tuple[bytes, bytes]:
    """A CRILAYLA entry of literals (int) and back references ((distance, length): overlapping runs, extended lengths)
    and what it decodes to (tools/test_installer_incremental.py's fixture encoder)."""
    bits = []
    decoded = bytearray()
    for token in tokens:
        if isinstance(token, int):
            bits.append('0' + format(token, '08b'))
            decoded.append(token)
            continue
        distance, length = token
        bits.append('1' + format(distance - 3, '013b'))
        remaining = length - 3
        for width in (2, 3, 5, 8):
            part = min(remaining, (1 << width) - 1)
            bits.append(format(part, f'0{width}b'))
            remaining -= part
            if part != (1 << width) - 1:
                break
        else:
            while remaining >= 255:
                bits.append('1' * 8)
                remaining -= 255
            bits.append(format(remaining, '08b'))
        for _ in range(length):
            decoded.append(decoded[-distance])
    text = ''.join(bits)
    text += '0' * (-len(text) % 8)
    stream = bytes(int(text[i:i + 8], 2) for i in range(0, len(text), 8))[::-1]
    prefix = bytes(random.Random(len(stream)).randrange(256) for _ in range(256))
    return (b'CRILAYLA' + struct.pack('<II', len(decoded), len(stream)) + stream + prefix,
            prefix + bytes(decoded[::-1]))


def cri_cases() -> list[tuple[bytes, bytes]]:
    rng = random.Random(41)
    cases = [cri_stream([7]), cri_stream([1, 2, 3, (3, 3)]), cri_stream([9, 8, 7, (3, 300)])]
    for n in (10, 200, 3000, 20000):
        tokens: list = []
        size = 0
        for _ in range(n):
            if size >= 3 and rng.random() < 0.45:
                distance = rng.randrange(3, min(size, 8194) + 1)
                length = rng.choice((3, 4, 6, 9, 10, 13, 40, 41, 296, 297, 600, rng.randrange(3, 2000)))
                tokens.append((distance, length))
                size += length
            else:
                tokens.append(rng.randrange(256))
                size += 1
        cases.append(cri_stream(tokens))
    return cases


def native_crilayla(entry: bytes) -> bytes | None:
    """The DLL's own answer (None: handed back to the reference), to know the native path is the one tested."""
    import ctypes
    size = struct.unpack_from('<I', entry, 8)[0]
    out = ctypes.create_string_buffer(0x100 + size)
    return out.raw if edf6native.lib().edf6_crilayla_decompress(entry, len(entry), out, size) else None


def outcome(fn, *args):
    try:
        return 'ok', fn(*args)
    except Exception as e:  # noqa: BLE001 - the two must fail alike, whatever the error
        return type(e).__name__, str(e)


class NativeCrilaylaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertIsNotNone(edf6native.lib(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_matches_reference(self) -> None:
        for entry, want in cri_cases():
            with self.subTest(size=len(want)):
                self.assertEqual(crilayla.decompress_py(entry), want)
                self.assertEqual(native_crilayla(entry), want)
                self.assertEqual(crilayla.decompress(entry), want)

    def test_cut_and_corrupt_entries_behave_as_the_reference(self) -> None:
        entry, _ = cri_cases()[4]
        rng = random.Random(5)
        bad = [entry[:n] for n in (0x0F, 0x10, 0x20, len(entry) - 0x100, len(entry) - 1)]
        bad.append(entry[:8] + struct.pack('<II', 0xFFFFFFF0, 12) + entry[16:])        # a corrupt size
        bad.append(entry[:8] + struct.pack('<I', 0) + entry[12:])                       # nothing to decode
        bad.append(entry[:12] + struct.pack('<I', 0) + entry[16:])                      # no stream
        for _ in range(40):                                                             # flipped stream bytes
            b = bytearray(entry)
            for _ in range(3):
                b[rng.randrange(0x10, len(entry) - 0x100)] ^= 1 << rng.randrange(8)
            bad.append(bytes(b))
        bad.append(b'NOTLAYLA' + entry[8:])
        for data in bad:
            with self.subTest(head=data[:16].hex(), size=len(data)):
                self.assertEqual(outcome(crilayla.decompress, data), outcome(crilayla.decompress_py, data))

    def test_real_cpk_entries(self) -> None:
        import gamedir
        game = gamedir.find()
        if not game or not os.path.isfile(os.path.join(game, 'Root.cpk')):
            self.skipTest('EDF6 not found')
        import rootcpk
        g = rootcpk.Game(game)
        packed = [k for k, e in g.cpk.index.items() if int(e['ExtractSize']) != int(e['FileSize'])]
        for folder, name in random.Random(6).sample(packed, min(12, len(packed))):
            e = g.cpk.index[(folder, name)]
            with open(g.cpk.path, 'rb') as h:
                h.seek(g.cpk.base + int(e['FileOffset']))
                data = h.read(int(e['FileSize']))
            with self.subTest(file=f'{folder}/{name}'):
                self.assertEqual(native_crilayla(data), crilayla.decompress_py(data))


def lzss_inputs() -> list[bytes]:
    rng = random.Random(18)
    cases = [b'', b'\x00', b'AB', b'ABC', b'ABCABC', bytes(17), bytes(18), bytes(19), bytes(5000), b'abc' * 3000]
    for size in (4095, 4096, 4097, 4114, 9000):
        cases.append(rng.randbytes(size))
    # the terrain leaves' shape: float rows with repeats, and the splat grids' zero runs (many keys, many tries)
    cases.append(b''.join(struct.pack('<3f', rng.randrange(64) / 8, 1.0, rng.randrange(8) / 4) for _ in range(9000)))
    cases.append(b''.join(bytes(rng.choice((0, 0, 0, 255, 128))) * rng.randrange(1, 30) for _ in range(3000)))
    cases.append(bytes(rng.choice(b'\x00\x01\xff') for _ in range(30000)))
    block = rng.randbytes(200)
    cases.append(b''.join(block if rng.random() < 0.5 else rng.randbytes(rng.randrange(1, 30)) for _ in range(300)))
    return cases


class NativeLzssTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertIsNotNone(edf6native.lib(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_matches_reference_and_decodes(self) -> None:
        for data in lzss_inputs():
            with self.subTest(size=len(data)):
                fast = fmb_build.cmpl_fast(data)
                self.assertEqual(fast, fmb_build.cmpl_fast_py(data))
                self.assertEqual(mdb.cmpl_decompress_py(fast), data)

    def test_calls_in_a_row(self) -> None:
        # the key table is kept between calls: a call must not see an earlier one's keys
        a, b = lzss_inputs()[-1], lzss_inputs()[-3]
        for data in (a, b, a, b[:100], a):
            self.assertEqual(fmb_build.cmpl_fast(data), fmb_build.cmpl_fast_py(data))


def boxes_of(rng: np.random.Generator, m: int, flat: int | None = None) -> np.ndarray:
    lo = rng.uniform(-500, 500, (m, 3))
    hi = lo + rng.uniform(0, 30, (m, 3))
    if flat is not None:
        lo[:, flat] = hi[:, flat] = 12.5   # every box at one height: a zero extent
    return np.stack([lo, hi], 1)


def domain_of(boxes: np.ndarray) -> np.ndarray:
    return np.array([boxes[:, 0].min(0), boxes[:, 1].max(0)])


def native_tree(boxes: np.ndarray, domain: np.ndarray) -> bool:
    """Whether the DLL itself builds this tree (rather than handing it back)."""
    import ctypes
    b, d = np.ascontiguousarray(boxes, dtype=np.float64), np.ascontiguousarray(domain, dtype=np.float64)
    out = ctypes.create_string_buffer(4 * (2 * len(b) - 1))
    return bool(edf6native.lib().edf6_section_tree(b.ctypes.data, len(b), d.ctypes.data, out))


def native_simd(boxes: np.ndarray, data: list[int]) -> bool:
    import ctypes
    lib = edf6native.lib()
    b, d = np.ascontiguousarray(boxes, dtype=np.float64), np.ascontiguousarray(data, dtype=np.uint32)
    out = ctypes.create_string_buffer(128 * lib.edf6_simd_tree_nodes(len(b)))
    return bool(lib.edf6_simd_tree(b.ctypes.data, len(b), d.ctypes.data, out))


class NativeSectionTreeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertIsNotNone(edf6native.lib(), 'build/pylib/edf6cmpl.dll missing: run build.cmd first')

    def test_matches_reference(self) -> None:
        rng = np.random.default_rng(127)
        for m in (1, 2, 3, 5, 16, 31, 64, 100, 127):
            for flat in (None, 1):
                b = boxes_of(rng, m, flat)
                with self.subTest(m=m, flat=flat):
                    self.assertTrue(native_tree(b, domain_of(b)))
                    self.assertEqual(hkcms_build._section_tree(b, domain_of(b)), hkcms_build._section_tree_py(b, domain_of(b)))

    def test_real_meshes(self) -> None:
        # the terrain tiles and object soups as hkcms_build makes them (quantised boxes, many equal centres)
        rng = np.random.default_rng(3)
        for _ in range(30):
            verts = np.round(rng.uniform(0, 40, (60, 3)), 1)
            verts[:, 1] = np.where(rng.random(60) < 0.5, 2.0, verts[:, 1])
            prims = rng.integers(0, 60, (rng.integers(1, 128), 4))
            prims[:, 3] = np.where(rng.random(len(prims)) < 0.5, prims[:, 2], prims[:, 3])
            mesh = hkcms_build.make_mesh(verts, prims)
            self.assertTrue(native_tree(mesh.boxes, mesh.domain))
            self.assertEqual(hkcms_build._section_tree(mesh.boxes, mesh.domain),
                             hkcms_build._section_tree_py(mesh.boxes, mesh.domain))

    def test_refused_inputs_fall_back(self) -> None:
        rng = np.random.default_rng(9)
        big = boxes_of(rng, 200)   # leaves over 127: the reference's ValueError (a byte over 255)
        self.assertEqual(outcome(hkcms_build._section_tree, big, domain_of(big))[0],
                         outcome(hkcms_build._section_tree_py, big, domain_of(big))[0])
        self.assertEqual(outcome(hkcms_build._section_tree, big, domain_of(big))[0], 'ValueError')
        nan = boxes_of(rng, 8)
        dom = domain_of(nan)
        nan[3, 0, 2] = np.nan   # numpy's min propagates it: the reference's bytes, whatever they are
        self.assertEqual(outcome(hkcms_build._section_tree, nan, dom), outcome(hkcms_build._section_tree_py, nan, dom))

    def test_simd_tree_matches_reference(self) -> None:
        rng = np.random.default_rng(128)
        for n in (1, 2, 3, 4, 5, 8, 9, 15, 16, 17, 63, 64, 65, 127, 300, 1000):
            for flat in (None, 0):
                b = boxes_of(rng, n, flat)
                data = [int(v) for v in rng.integers(0, 2**31, n)]
                with self.subTest(n=n, flat=flat):
                    self.assertTrue(native_simd(b, data))
                    self.assertEqual(hkcms_build.simd_tree(b, data), hkcms_build.simd_tree_py(b, data))
        # float32 rounding at the edges: values a float32 holds exactly, and halfway ones
        b = np.round(boxes_of(rng, 40), 0) + np.array([0.0, 2.0**-30])[None, :, None]
        self.assertEqual(hkcms_build.simd_tree(b, list(range(40))), hkcms_build.simd_tree_py(b, list(range(40))))

    def test_simd_tree_refused_inputs_fall_back(self) -> None:
        nan = boxes_of(np.random.default_rng(1), 6)
        nan[2, 1, 1] = np.nan
        self.assertEqual(outcome(hkcms_build.simd_tree, nan, list(range(6))), outcome(hkcms_build.simd_tree_py, nan, list(range(6))))
        empty = np.zeros((0, 2, 3))
        self.assertEqual(outcome(hkcms_build.simd_tree, empty, [])[0], outcome(hkcms_build.simd_tree_py, empty, [])[0])


if __name__ == '__main__':
    unittest.main()
