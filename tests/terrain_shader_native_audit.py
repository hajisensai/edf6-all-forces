"""Read installed terrain shaders and optionally sample the repaired source DDS on D3D11 WARP.

No game process or game writes. Exit 77 means Windows or original resources are absent.
--probe is terrain_sampler_native_test.exe; temporary DDS files are removed on exit.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'tests')]
import edf41_maps  # noqa: E402
import fmb_build  # noqa: E402
import gamedir  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
import sk_map  # noqa: E402
from edf41_material_shader_contract_audit import disassemble  # noqa: E402

# Reviewed complete programs, not guessed opcodes. An engine update needs a fresh audit.
CURRENT_PS_SHA = '38c06e615c108e1a39010bc5b6facfa8aae642cc60fd3a3a6a08c361b42218ef'
SOURCE_PS_SHA = '44d4d972a0f004b85a76011e4cc2fcc58b7cc22a4f73cf01b96e6afd13703dae'


def check_shaders(root6: str, root41: str) -> None:
    game = rootcpk.Game(root6, 'DX11.cpk')
    variants = 0
    assert fmb_build.MAX_LAYERS == 15
    for count in range(1, fmb_build.MAX_LAYERS + 1):
        for render_pass in range(3):
            name = f'MAP_FIELD_RENDER{count}_1_14_{render_pass}.SGO'
            data = game.read('MAP/SHADER/DX11', name)
            pixel = sgo.read(data)[1]['Deferred'][0][4]
            assembly = disassemble(pixel)
            instructions = assembly[assembly.index('ps_5_0'):]
            # Pass 2 drops the normal texture, so its structured registers shift by one.
            first = 1 if render_pass == 2 else 2
            for register, stride in zip(range(first, first + 4), (80, 16, 80, 64)):
                declaration = f'dcl_resource_structured t{register}, {stride}'
                assert declaration in instructions, (name, declaration)
            # These variants actually read every layer and sample its colours/normal.
            # In particular, 8..15 are real programs, despite the stock-map sample's smaller leaf lists.
            assert instructions.count('sample_indexable(texture2darray)') >= (2 if render_pass == 2 else 3) * count, name
            assert instructions.count('ld_structured_indexable') >= (7 if render_pass == 2 else 10) * count, name
            variants += 1
            if count == 1 and render_pass == 0:
                assert hashlib.sha256(pixel).hexdigest() == CURRENT_PS_SHA
                assert 'l(48), t4.xxxx\nutof r1.z, r0.w' in instructions
                assert 'r1.xyz, r1.xyzx, t0.xyzw, s1' in instructions
                assert 'mul r1.xyz, r1.xyzx, r2.yzwy' in instructions
                assert not any(line.startswith(('if_', 'movc ', 'ine ', 'ieq ')) for line in instructions.splitlines())
    raw = (Path(root41) / 'EDF41.exe').read_bytes()
    start = 0x9BE120
    assert raw[start:start + 4] == b'DXBC', 'reviewed source terrain shader moved'
    size = struct.unpack_from('<I', raw, start + 24)[0]
    pixel = raw[start:start + size]
    assert hashlib.sha256(pixel).hexdigest() == SOURCE_PS_SHA
    source = disassemble(pixel)
    # Byte 1 of the packed texture indices is 255 when the optional second colour is absent.
    assert 'ine r0.yz, r0.zzwz, l(0, 255, 255, 0)\n    if_nz r0.y\n      mul r1.xyz, r2.xyzx, r1.xyzx\n    endif' in source
    print(f'PASS {variants} shipped layer/pass variants, native structured layouts, and source optional-multiplier branch')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root6 = gamedir.find()
    parser.add_argument('--edf6', default=root6)
    parser.add_argument('--edf41', default=gamedir.find_other(gamedir.EDF41, root6))
    parser.add_argument('--probe', type=Path)
    args = parser.parse_args()
    if os.name != 'nt' or not args.edf6 or not args.edf41:
        print('SKIP original EDF4.1/EDF6 Windows resources are unavailable')
        return 77
    check_shaders(args.edf6, args.edf41)
    if args.probe:
        source = edf41_maps.Archives(args.edf41)
        original = next(file.data for file in mdb.rab_read(source.read('SK_KAIGAN01.RAB')).files
                        if file.name.lower().endswith('.cols.dds'))
        repaired, neutral = sk_map.with_identity_multiplier(original)
        assert neutral == 11
        with tempfile.TemporaryDirectory(prefix='edf6-terrain-sampler-') as temp:
            old = Path(temp) / 'source.dds'
            fixed = Path(temp) / 'repaired.dds'
            old.write_bytes(original)
            fixed.write_bytes(repaired)
            for color in (8, 9):
                subprocess.run([str(args.probe), str(old), str(color), str(fixed)], check=True)
        print('PASS real source sand slices, all mips, and production identity DDS on WARP')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
