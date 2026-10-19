"""Optional SHFB multipliers and complete FMB layer lists preserve authored terrain colour."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from unittest.mock import patch
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import bigmap
import edf41_maps
import fmb
import fmb_build as build
import gamedir
import ledger
import map_legacy
import mdb
import sgo
import sk_map
import sk_terrain


def array_dds(width=8, height=4, mips=4, slices=2, fmt=71) -> bytes:
    header = bytearray(148)
    header[:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)
    struct.pack_into('<2I', header, 12, height, width)
    struct.pack_into('<I', header, 28, mips)
    struct.pack_into('<2I4s', header, 76, 32, 4, b'DX10')
    struct.pack_into('<I', header, 108, 0x401008)
    struct.pack_into('<5I', header, 128, fmt, 3, 0, slices, 0)
    payload = bytearray()
    for index in range(slices):
        for level in range(mips):
            blocks = max(1, ((width >> level) + 3) // 4) * max(1, ((height >> level) + 3) // 4)
            # Valid independent BC1 fixtures; every slice and mip has distinct bytes.
            payload += struct.pack('<HHI', 0x6300 + index, 0x4200 + level, 0x55555555) * blocks
    return bytes(header + payload)


def field(layers, names) -> sk_terrain.Shfb:
    return sk_terrain.Shfb(2, 2, (2, 2), (2, 2), (1, 1), (1, 1), 0, 0,
                          np.zeros((2, 2)), np.zeros((2, 2), dtype=np.uint8), layers, [], names)


def corner_from_blob(blob, used, row, col, cells=20) -> np.ndarray:
    leaf = fmb._parse_leaf_blob(0, blob)
    mesh = leaf.meshes[0]
    cell_row, cell_col = min(row, cells - 1), min(col, cells - 1)
    which = (2 if row > cell_row else 0) + (1 if col > cell_col else 0)
    at = mesh.weight_off + (cell_row * cells + cell_col) * 80 + 16 + which * 16
    weights = np.frombuffer(blob, dtype=np.uint8, count=16, offset=at)
    result = np.zeros(max(used) + 1)
    for slot, layer in enumerate(used):
        result[layer] = weights[slot] / 255.0
    return result


class MultiplierContract(unittest.TestCase):
    def test_identity_preserves_all_authored_bytes_and_every_mip(self):
        for fmt in (71, 72):
            with self.subTest(format=fmt):
                original = array_dds(fmt=fmt)
                fixed, index = sk_map.with_identity_multiplier(original)
                self.assertEqual(index, 2)
                self.assertEqual(struct.unpack_from('<I', fixed, 140)[0], 3)
                self.assertEqual(fixed[:140], original[:140])
                self.assertEqual(fixed[144:len(original)], original[144:])
                stride = (len(original) - 148) // 2
                self.assertEqual(fixed[len(original):], struct.pack('<HHI', 0xFFFF, 0, 0) * (stride // 8))
                self.assertEqual(len(fixed), len(original) + stride)

    def test_malformed_or_unsupported_arrays_are_refused(self):
        source = array_dds()
        invalid = [source[:147], source[:-1], source + b'junk', array_dds(fmt=77), array_dds(mips=5)]
        for at, value in ((76, 24), (80, 0), (132, 4), (136, 4), (140, 0)):
            changed = bytearray(source)
            struct.pack_into('<I', changed, at, value)
            invalid.append(bytes(changed))
        for data in invalid:
            with self.assertRaises(ValueError):
                sk_map.with_identity_multiplier(data)
        with self.assertRaisesRegex(ValueError, 'no room'):
            sk_map.with_identity_multiplier(array_dds(width=1, height=1, mips=1, slices=2048))

    def test_missing_multiplier_requires_explicit_identity_binding(self):
        s = field([sk_terrain.Layer(0, -1, (.3, .3), (0, 0))], ['sand'])
        with self.assertRaisesRegex(ValueError, 'identity'):
            sk_map.layers_of(s)
        layer = sk_map.layers_of(s, neutral_slice=1)[0]
        self.assertEqual((layer.color, layer.multip), (0, 1))
        self.assertEqual(layer.tile_color, layer.tile_multip)
        with self.assertRaisesRegex(ValueError, 'identity'):
            sk_map.layers_of(s, neutral_slice=0)

    def test_authored_multiplier_and_binding_order_are_preserved(self):
        names = ['sand', 'normal_n', 'pattern']
        slices = sk_map.color_slices(names, {'pattern': 0, 'sand': 1})
        self.assertEqual(slices, {0: 1, 2: 0})
        s = field([sk_terrain.Layer(0, 2, (.5, .5), (.05, .05))], names)
        layer = sk_map.layers_of(s, slices=slices)[0]
        self.assertEqual((layer.color, layer.multip, layer.tile_color), (1, 0, 2))
        self.assertAlmostEqual(layer.tile_multip, 20)
        for binding in ({'sand': 1}, {'sand': 0, 'pattern': 0}, {'unknown': 0}):
            with self.assertRaises(ValueError):
                sk_map.color_slices(names, binding)

    def test_fmb_refuses_invalid_primary_samples_or_zero_tiles(self):
        for layer in (build.Layer(0, -1, 2, 0), build.Layer(-1, 0, 2, 2), build.Layer(0, 0, 2, 0)):
            with self.assertRaises(ValueError):
                build.layer_table([layer])
        table, tiles = build.layer_table([build.Layer(8, 11, 3, 3)])
        self.assertEqual(struct.unpack_from('<i', table, 16 + 0x28)[0], 11)
        self.assertEqual(tiles, [3])

    def test_archive_uses_identity_only_when_required(self):
        original = array_dds()
        source = mdb.Rab(0x100, ['TEXTURE'], [mdb.RabFile('field.cols.dds', 0, 0, original),
                                                    mdb.RabFile('unrelated.dds', 0, 0, b'untouched')])
        flat = mdb.rab_write(mdb.Rab(0x100, ['TEXTURE'], [mdb.RabFile(sk_map.FLAT_NORMAL, 0, 0, b'flat normal')]))
        class Reader:
            def read(self, name):
                return flat
        for required in (False, True):
            result = mdb.rab_read(sk_map.convert_rab(mdb.rab_write(source), 'field.cols.dds', 'test', Reader(),
                                                    neutral_multiplier=required))
            by = {entry.name: entry for entry in result.files}
            self.assertEqual(by['unrelated.dds'].stored, b'untouched')
            self.assertEqual(by['test_normal.dds'].data, b'flat normal')
            color = by['test_color.dds'].data
            self.assertEqual(color, sk_map.with_identity_multiplier(original)[0] if required else original)


class CompleteLayerLists(unittest.TestCase):
    def test_all_thirteen_active_layers_survive_serialized_leaf(self):
        weights = np.full((3, 3, 13), 1 / 13, dtype=np.float32)
        weights[1, 1] = 0
        weights[1, 1, 12] = 1
        layers = [build.Layer(i, 0, 2, 2) for i in range(13)]
        heights = np.zeros((3, 3), dtype=np.float32)
        normals = np.zeros((3, 3, 3), dtype=np.float32)
        normals[:, :, 1] = 1
        blob, used = build.leaf_blob(heights, normals, weights, 0, 0, 4, 0, 0, (2, 2), layers, list(range(13)))
        self.assertEqual(used, list(range(13)))
        self.assertEqual(corner_from_blob(blob, used, 1, 1, 2)[12], 1)
        self.assertEqual(fmb._parse_leaf_blob(0, blob).meshes[0].grid[6], 13 * 0x101)

    def test_exceeding_shipped_shader_limit_is_refused_instead_of_truncated(self):
        heights = np.zeros((2, 2), dtype=np.float32)
        normals = np.zeros((2, 2, 3), dtype=np.float32)
        weights = np.full((2, 2, 16), 1 / 16, dtype=np.float32)
        with self.assertRaisesRegex(ValueError, 'shipped shaders'):
            build.leaf_blob(heights, normals, weights, 0, 0, 4, 0, 0, (1, 1),
                            [build.Layer(i, 0, 2, 2) for i in range(16)], list(range(16)))


class ConvertedMapUpgrade(unittest.TestCase):
    def test_previous_installed_map_rebuilds_once_without_changing_source_or_owner(self):
        name = 'SK_KAIGAN01'
        converted = []
        class Source:
            def fingerprint(self, file):
                return [len(file), 0x41]
        def convert(current, _old, _new):
            converted.append(current)
            return {rel: (str(edf41_maps.VERSION).encode() if rel.endswith('.FMB') else rel.encode())
                    for rel in edf41_maps.files_of(current)}
        with tempfile.TemporaryDirectory(prefix='edf41-terrain-upgrade-') as root, \
                patch.object(edf41_maps, 'MAPS', {name: edf41_maps.MAPS[name]}), \
                patch.object(edf41_maps, 'Archives', lambda _root: Source()), \
                patch.object(edf41_maps, 'convert', convert), patch.object(edf41_maps, 'workers', lambda: 1):
            with patch.object(edf41_maps, 'VERSION', 3):
                written, skipped = edf41_maps.install(root, 'source', lambda _message: None)
                self.assertFalse(skipped)
                self.assertEqual(len(written), len(edf41_maps.files_of(name)))
            self.assertGreaterEqual(edf41_maps.VERSION, 4)
            written, skipped = edf41_maps.install(root, 'source', lambda _message: None)
            self.assertFalse(skipped)
            self.assertEqual(written, [str(Path(root) / 'Mods' / 'MAP' / sk_map.fmb_file(name))])
            self.assertEqual(converted, [name, name])
            stamp = json.loads((Path(root) / 'Mods' / edf41_maps.STAMP).read_text(encoding='utf-8'))
            self.assertEqual(stamp[name]['version'], edf41_maps.VERSION)
            self.assertEqual(edf41_maps.install(root, 'source', lambda _message: None), ([], []))
            self.assertEqual(converted, [name, name])
            ownership = ledger.Ledger(root)
            self.assertTrue(all(ownership.owners(rel) == [edf41_maps.OWNER] for rel in edf41_maps.files_of(name)))
            deleted, kept = edf41_maps.remove(root)
            self.assertEqual(set(deleted), {ownership.disk(rel) for rel in edf41_maps.files_of(name)})
            self.assertFalse(kept)


class NativeSourceContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root6 = gamedir.find()
        cls.root41 = gamedir.find_other(gamedir.EDF41, cls.root6)
        if not cls.root6 or not cls.root41:
            raise unittest.SkipTest('real EDF4.1/EDF6 resources unavailable; pure format tests still run')
        cls.source = edf41_maps.Archives(cls.root41)

    def load(self, name):
        mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(self.source.read(name + '.MAC')))
        raw = next(data for file, data in mac.files if file.endswith('.shfb'))
        return mac, sk_terrain.parse(raw)

    def test_real_sand_slice_and_identity_preserve_every_original_mip(self):
        mac, source = self.load('SK_KAIGAN01')
        bindings = sgo.load(data=next(data for name, data in mac.files if name.endswith('.cols.sgo')))
        original = next(file.data for file in mdb.rab_read(self.source.read('SK_KAIGAN01.RAB')).files
                        if file.name.endswith('.cols.dds'))
        fixed, neutral = sk_map.with_identity_multiplier(original)
        self.assertEqual(neutral, 11)
        slices = sk_map.color_slices(source.layer_names, bindings)
        layers = sk_map.layers_of(source, neutral, slices)
        self.assertEqual([(i, l.color, l.multip) for i, l in enumerate(layers) if source.layers[i].multip < 0],
                         [(0, 8, 11), (4, 9, 11)])
        self.assertEqual(hashlib.sha256(fixed[148:len(original)]).digest(), hashlib.sha256(original[148:]).digest())
        for i, layer in enumerate(layers):
            if i not in (0, 4):
                self.assertEqual(layer.multip, slices[source.layers[i].multip])

    def test_real_coast_and_mountain_layers_are_not_silently_deleted(self):
        for name, r0, c0, original_r, original_c, wanted in (
                ('SK_KAIGAN01', 200, 140, 438, 320, (0, 4)),
                ('SK_SANGAKU01', 320, 200, 640, 408, (7,))):
            with self.subTest(map=name):
                _mac, source = self.load(name)
                weights = source.blend[::2, ::2]
                weights = weights / np.maximum(weights.sum(2, keepdims=True), 1e-9)
                heights = sk_terrain.lattice(source)
                layers = sk_map.layers_of(source, len(sk_map.color_slices(source.layer_names))
                                          if any(l.multip < 0 for l in source.layers) else None)
                blob, used = build.leaf_blob(heights, build.lattice_normals(heights, 4), weights, -1000, -1000, 4,
                                            r0, c0, (20, 20), layers, list(range(len(layers))))
                self.assertTrue(set(wanted) <= set(used))
                row, col = original_r // 2 - r0, original_c // 2 - c0
                actual = corner_from_blob(blob, used, row, col)
                expected = weights[original_r // 2, original_c // 2]
                for layer in wanted:
                    self.assertLessEqual(abs(actual[layer] - expected[layer]), 1 / 255)
                self.assertAlmostEqual(float(actual.sum()), 1)
                if name == 'SK_SANGAKU01':
                    self.assertEqual(len(used), 13)
                    self.assertEqual(actual[7], 1)


if __name__ == '__main__':
    unittest.main()
