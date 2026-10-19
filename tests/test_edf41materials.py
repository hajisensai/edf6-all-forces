"""Diffuse compatibility regressions; native stock shader checks run when EDF6 exists."""
from __future__ import annotations

import copy
import ctypes
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'tests')]
import edf41materials as materials  # noqa: E402
import edf41_materials as planner  # noqa: E402
import gamedir  # noqa: E402
import ledger  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402


def model(shader: str = 'snd_Mech') -> bytes:
    material = mdb.Material(0, 1, 2, 0, shader,
                           [mdb.MatParam([0.2, 0.4, 0.8, 1.0], (0, 0), 'diffuse', 0x402)],
                           [mdb.MatTex(0, 'albedo', (1, 2, 3, 4, 5))], 3)
    return mdb.mdb_write(mdb.Mdb(0x20, ['metal'], [], [], [material],
                                 [mdb.Texture(0, 'paint', 'paint.dds', 0)]))


class ModelContract(unittest.TestCase):
    def test_only_reviewed_material_shader_changes(self) -> None:
        data = model()
        result = materials.transform_models('OBJECT/PRIVATE.MDB', data)
        source, target = mdb.mdb_read(data), mdb.mdb_read(result)
        self.assertEqual(target.materials[0].shader, 'EDF41_SND_MECH')
        target.materials[0].shader = source.materials[0].shader
        self.assertEqual(target, source)
        self.assertEqual(materials.transform_models('OBJECT/PRIVATE.MDB', result), result)
        self.assertEqual(materials.needed_names({'OBJECT/PRIVATE.MDB': result}), {'SND_MECH'})

    def test_unsupported_shader_and_nonmodel_unchanged(self) -> None:
        data = model('snd_BRDF_Common')
        self.assertEqual(materials.transform_models('OBJECT/PRIVATE.MDB', data), data)
        self.assertEqual(materials.transform_models('OBJECT/PRIVATE.SGO', b'unchanged'), b'unchanged')
        self.assertEqual(materials.needed_names({'OBJECT/PRIVATE.MDB': data}), set())

    def test_archive_order_textures_and_animations_unchanged(self) -> None:
        archive = mdb.Rab(0x100, ['TEXTURE', 'MODEL', 'ANIMATION'], [
            mdb.RabFile('paint.dds', 0, 0, b'DDS opaque test bytes'),
            mdb.RabFile('private.mdb', 1, 0, model()),
            mdb.RabFile('private.cas', 2, 0, b'CAS untouched bytes')])
        data = mdb.rab_write(archive)
        result = materials.transform_models('OBJECT/PRIVATE.MRAB', data)
        target = mdb.rab_read(result)
        self.assertEqual(target.folders, archive.folders)
        self.assertEqual([f.name for f in target.files], [f.name for f in archive.files])
        self.assertEqual(target.files[0], archive.files[0])
        self.assertEqual(target.files[2], archive.files[2])
        self.assertEqual(materials.transform_models('OBJECT/PRIVATE.MRAB', result), result)

    def test_unknown_shader_container_refused(self) -> None:
        with self.assertRaises(ValueError):
            materials.patch_pixel_shader(b'DXBC' + bytes(100))
        with self.assertRaises(ValueError):
            materials.build_shader('snd_Mech', b'changed stock file')

    def test_md5_compression_against_standard_library(self) -> None:
        # Independently check the compression core at block-boundary lengths;
        # real stock DXBC checks below cover its distinct final-block layout.
        for length in (0, 1, 4, 55, 56, 59, 60, 63, 64, 119, 120, 123, 124, 128):
            payload = bytes((i * 17) % 256 for i in range(length))
            padded = payload + b'\x80'
            padded += bytes((56 - len(padded)) % 64)
            padded += struct.pack('<Q', length * 8)
            state = (0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476)
            for offset in range(0, len(padded), 64):
                state = materials._compress(state, padded[offset:offset + 64])
            self.assertEqual(struct.pack('<4I', *state), hashlib.md5(payload).digest())


class PlannerOwnership(unittest.TestCase):
    def test_disabled_uses_originals_and_does_not_read_game(self) -> None:
        data = {'OBJECT/PRIVATE.MDB': model()}
        with patch.object(materials, 'build_shaders', side_effect=AssertionError('must not read stock')):
            self.assertEqual(planner.apply(data, 'missing-game', False), data)

    def test_foreign_alias_and_user_edits_are_refused(self) -> None:
        rel, shader = materials.shader_path('snd_Mech'), b'planned shader'
        data = {'OBJECT/PRIVATE.MDB': model()}
        with tempfile.TemporaryDirectory() as root, patch.object(materials, 'build_shaders', return_value={rel: shader}):
            led = ledger.Ledger(root)
            Path(led.disk(rel)).parent.mkdir(parents=True)
            Path(led.disk(rel)).write_bytes(shader)
            with self.assertRaisesRegex(ValueError, 'another mod'):
                planner.apply(data, root)
            # A byte-identical known EDF4.1 owner may share the shader.
            led.record('edf41', rel, hashlib.sha256(shader).hexdigest())
            result = planner.apply(data, root)
            self.assertEqual(result[rel], shader)
            self.assertNotEqual(result['OBJECT/PRIVATE.MDB'], data['OBJECT/PRIVATE.MDB'])
            Path(led.disk(rel)).write_bytes(b'user edit')
            with self.assertRaisesRegex(ValueError, 'edited'):
                planner.apply(data, root)
            self.assertEqual(Path(led.disk(rel)).read_bytes(), b'user edit')

    def test_range_dependency_can_outlive_writer_and_be_readopted(self) -> None:
        rel, shader = materials.shader_path('snd_Mech'), b'planned shader'
        data = {'OBJECT/PRIVATE.MDB': model()}
        for consumer in ('testrange', 'testrange_sub'):
            with self.subTest(consumer=consumer), tempfile.TemporaryDirectory() as root, \
                    patch.object(materials, 'build_shaders', return_value={rel: shader}):
                led = ledger.Ledger(root)
                led.put('edf41', rel, shader)
                led.need(consumer, rel)
                led.release('edf41')
                self.assertEqual(ledger.Ledger(root).owners(rel), [consumer])
                self.assertEqual(planner.apply(data, root)[rel], shader)
                # A dependency is permitted only for the exact planned shader;
                # edits and another feature's competing writer stay refused.
                led.put('jets', rel, shader)
                with self.assertRaisesRegex(ValueError, 'another mod'):
                    planner.apply(data, root)

    def test_another_feature_owner_is_not_taken_over(self) -> None:
        rel, shader = materials.shader_path('snd_Mech'), b'planned shader'
        with tempfile.TemporaryDirectory() as root, patch.object(materials, 'build_shaders', return_value={rel: shader}):
            ledger.Ledger(root).put('jets', rel, shader)
            with self.assertRaisesRegex(ValueError, 'another mod'):
                planner.apply({'OBJECT/PRIVATE.MDB': model()}, root)


class NativeShaderContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.game = gamedir.find()
        if os.name != 'nt' or cls.game is None:
            raise unittest.SkipTest('native shader contract requires Windows and installed EDF6')

    def test_real_shader_abi_and_driver_acceptance(self) -> None:
        from edf41_material_shader_contract_audit import disassemble
        library = ctypes.WinDLL('d3d11.dll')
        create = library.D3D11CreateDevice
        create.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint,
                           ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                           ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint),
                           ctypes.POINTER(ctypes.c_void_p)]
        create.restype = ctypes.c_long
        device, context, feature = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_uint()
        self.assertGreaterEqual(create(None, 5, None, 0, None, 0, 7,
                                       ctypes.byref(device), ctypes.byref(feature), ctypes.byref(context)), 0)

        def release(obj: ctypes.c_void_p) -> None:
            if obj.value:
                table = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
                ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table[2])(obj)

        table = ctypes.cast(device, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        make = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                 ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))(table[15])
        try:
            game = rootcpk.Game(self.game, 'DX11.cpk')
            checked = set()
            for name in sorted(materials.SUPPORTED):
                data = game.read('SHADER/DX11', name + '.SGO')
                patched = materials.build_shader(name, data)
                old_version, old = sgo.read(data)
                new_version, new = sgo.read(patched)
                self.assertEqual((new_version, list(new), len(patched)), (old_version, list(old), len(data)))
                expected = copy.deepcopy(old)
                for technique, passes in old.items():
                    for index, entry in enumerate(passes):
                        if not technique.startswith(('Deferred', 'Forward')):
                            continue
                        original, result = entry[4], new[technique][index][4]
                        expected[technique][index][4] = result
                        self.assertEqual(original[4:20], materials.dxbc_checksum(original))
                        self.assertEqual(result[4:20], materials.dxbc_checksum(result))
                        if original in checked:
                            continue
                        checked.add(original)
                        first, second = disassemble(original).splitlines(), disassemble(result).splitlines()
                        differences = [(a, b) for a, b in zip(first, second) if a != b]
                        self.assertEqual(len(first), len(second))
                        self.assertEqual(len(differences), 1)
                        before, after = differences[0]
                        self.assertTrue(before.startswith('mad '), before)
                        self.assertIn('l(0.000000)', after)
                        self.assertEqual(before.split(',')[0], after.split(',')[0])
                        self.assertEqual(before.split(',')[2:], after.split(',')[2:])
                        # All reflection/PBR instructions and signatures survive;
                        # the changed scalar multiplier alone makes MAD return b.
                        for base in (0.0, 0.2, 0.5, 1.0):
                            for reflection in (0.0, 0.25, 0.8, 1.0):
                                self.assertEqual(0.0 * (1.0 - base) + base, base)
                                self.assertGreaterEqual(reflection * (1.0 - base) + base, base)
                        obj, raw = ctypes.c_void_p(), ctypes.create_string_buffer(result)
                        hr = make(device, raw, len(result), None, ctypes.byref(obj))
                        self.assertGreaterEqual(hr, 0, f'{name}/{technique}: {hr & 0xFFFFFFFF:#x}')
                        release(obj)
                self.assertEqual(new, expected)  # VS, every other stage/pass and metadata unchanged.
            self.assertEqual({hashlib.sha256(pixel).hexdigest() for pixel in checked}, set(materials._PATCHES))
        finally:
            release(context)
            release(device)


if __name__ == '__main__':
    unittest.main()
