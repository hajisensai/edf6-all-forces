"""Read-only EDF4.1 material/shader contract audit against installed game archives.

This does not launch a game, write Mods, or produce replacement shaders.  It checks
that conversion preserves the source material and prints the actual Deferred
pixel-shader instructions responsible for the changed colour interpretation.
Run with ``python -B tests/edf41_material_shader_contract_audit.py`` on Windows.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import gamedir  # noqa: E402
import legacy_assets  # noqa: E402
import mdb  # noqa: E402
import mdb_legacy  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

SHADERS = ('SND_MECH.SGO', 'SND_CHARA_2COLORCHANGE.SGO')
SAMPLES = ('OBJECT/VEHICLE301_TANK.RAB', 'OBJECT/ARMYANDENGINEER.MRAB', 'OBJECT/HECTOR.MRAB')


def disassemble(data: bytes) -> str:
    """Use Microsoft's shipped disassembler; release its ID3DBlob on every path."""
    if os.name != 'nt':
        raise RuntimeError('shader audit requires Windows d3dcompiler_47.dll')
    library = ctypes.WinDLL('d3dcompiler_47.dll')
    call = library.D3DDisassemble
    call.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint,
                     ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    call.restype = ctypes.c_long
    raw = ctypes.create_string_buffer(data)
    blob = ctypes.c_void_p()
    result = call(raw, len(data), 0, None, ctypes.byref(blob))
    if result < 0 or not blob.value:
        raise ValueError(f'D3DDisassemble failed: HRESULT {result & 0xFFFFFFFF:#x}')
    table = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table[2])
    try:
        get_pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(table[3])
        get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(table[4])
        return ctypes.string_at(get_pointer(blob), get_size(blob)).decode('utf-8').rstrip('\0')
    finally:
        release(blob)


def archives(root: str) -> dict[str, rootcpk.Game]:
    out: dict[str, rootcpk.Game] = {}
    for name in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk'):
        if (Path(root) / name).is_file():
            game = rootcpk.Game(root, name)
            for folder, filename in game.cpk.index:
                out.setdefault(f'{folder}/{filename}'.upper(), game)
    return out


def material_contract(root: str) -> None:
    index = archives(root)
    material_count, model_count, texture_count = 0, 0, 0
    for rel in SAMPLES:
        archive = mdb.rab_read(index[rel].read(*rel.rsplit('/', 1)))
        for file in archive.files:
            if file.name.lower().endswith('.dds'):
                if legacy_assets._member(file.name, file.stored, False) != file.stored:
                    raise AssertionError(f'{rel}/{file.name}: DDS changed during conversion')
                texture_count += 1
                continue
            if not file.name.lower().endswith('.mdb'):
                continue
            source = mdb.mdb_read(file.data)
            target = mdb.mdb_read(mdb_legacy.mdb_from_legacy(file.data))
            if source.textures != target.textures or len(source.materials) != len(target.materials):
                raise AssertionError(f'{rel}/{file.name}: material/texture tables changed')
            model_count += 1
            for old, new in zip(source.materials, target.materials):
                material_count += 1
                if ((old.shader, old.textures, old.b2, old.b3, old.unk) !=
                        (new.shader, new.textures, new.b2, new.b3, new.unk)):
                    raise AssertionError(f'{rel}/{file.name}: shader, sampler, or material header changed')
                if len(old.params) != len(new.params):
                    raise AssertionError(f'{rel}/{file.name}: parameters added or removed')
                for before, after in zip(old.params, new.params):
                    if (before.name, before.type + 0x100) != (after.name, after.type):
                        raise AssertionError(f'{rel}/{file.name}: parameter identity/type changed')
                    # The converter may clear uninitialised *padding*, never an active component.
                    count = min(4, after.type >> 8)
                    if before.value[:count] != after.value[:count]:
                        raise AssertionError(f'{rel}/{file.name}: active {before.name} value changed')
            if file.name.lower() in ('vehicle301_tank.mdb', 'engineer.mdb', 'hector301.mdb'):
                print(f'MATERIAL {rel}/{file.name}')
                for material in source.materials:
                    values = {p.name: p.value[0] for p in material.params if p.name in
                              ('specular_pow', 'fall_off_amount', 'fall_off_scale', 'fall_off_offset')}
                    print(f'  {source.name_of(material.name)}: {material.shader} {values}')
    print(f'PASS conversion preserves {model_count} models / {material_count} materials / {texture_count} DDS members')


def shader_contract(label: str, root: str) -> None:
    game = rootcpk.Game(root, 'DX11.cpk')
    for name in SHADERS:
        data = game.read('SHADER/DX11', name)
        document = sgo.read(data)[1]
        bytecode = document['Deferred'][0][4]
        assembly = disassemble(bytecode)
        instructions = assembly[assembly.index('ps_5_0'):].splitlines()
        outputs = [line for line in instructions if line.startswith('dcl_output ')]
        print(f'SHADER {label} {name} SGO_SHA256={hashlib.sha256(data).hexdigest()}')
        print(f'  techniques={list(document)}; Deferred outputs={outputs}')
        # Print the producer instructions, not just the final write to o0.
        writes = [i for i, line in enumerate(instructions) if re.match(r'(?:mul|mad) o0\.xyz,', line)]
        if len(writes) != 1:
            raise AssertionError(f'{label}/{name}: expected one base-colour output, got {len(writes)}')
        at = writes[0]
        for line in instructions[max(0, at - 5):at + 1]:
            print('  ' + line)
        expected_op = 'mad' if label == 'EDF6' else 'mul'
        if not instructions[at].startswith(expected_op + ' o0.xyz,'):
            raise AssertionError(f'{label}/{name}: stock base-colour producer changed; review shader semantics')
        expected_rts = 5 if label == 'EDF6' else 6 if label == 'EDF5' else 4
        if len(outputs) != expected_rts:
            raise AssertionError(f'{label}/{name}: Deferred G-buffer output contract changed')
        parameters = assembly.split('// cbuffer xgl_user_param', 1)[1].split('// Resource Bindings:', 1)[0]
        for line in parameters.splitlines():
            if 'metallic;' in line or 'roughness;' in line:
                print('  ' + line.strip())
        print('  NOTE: output count/instructions establish the file contract, not full rendered lighting parity')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root6 = gamedir.find()
    parser.add_argument('--edf41', default=gamedir.find_other(gamedir.EDF41, root6))
    parser.add_argument('--edf5', default=gamedir.find_other(gamedir.EDF5, root6))
    parser.add_argument('--edf6', default=root6)
    args = parser.parse_args()
    if not args.edf41 or not args.edf6:
        parser.error('EDF4.1 and EDF6 installations are required; specify --edf41 and --edf6')
    material_contract(args.edf41)
    for label, root in (('EDF4.1', args.edf41), ('EDF5', args.edf5), ('EDF6', args.edf6)):
        if root:
            shader_contract(label, root)
    print('PASS read-only material/shader contract audit; no game or repository assets were written')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
