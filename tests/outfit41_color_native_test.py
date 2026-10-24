"""Render real outfit pixel shaders and compressed EDF4.1 textures on native D3D11 WARP.

python -B tests/outfit41_color_native_test.py --probe build/outfit41_color_warp_test.exe
The probe is compiled from outfit41_color_warp_test.cpp with d3d11.lib/d3dcompiler.lib.
Both games are read-only. Extracted bytecode and DDS files exist only in a temporary directory.
Exit 77 means an installed game or Windows is unavailable; no source-formula substitute runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'tests')]

import edf41_outfits as shipped  # noqa: E402
import edf41materials  # noqa: E402
import gamedir  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from edf41_material_shader_contract_audit import archives, disassemble  # noqa: E402

KINDS = ('albedo', 'normal', 'param_cm0_cm1_occ', 'param_reflect_specpow_specint')
SHADERS = ('SND_CHARA_2COLORCHANGE', 'SND_CHARA_2COLORCHANGE_ALPHA')


def blob(value: bytes) -> bytes:
    return struct.pack('<I', len(value)) + value


def parameters(material: mdb.Material) -> bytes:
    return struct.pack('<I', len(material.params)) + b''.join(
        blob(param.name.encode('ascii')) + blob(struct.pack('<4f', *param.value))
        for param in material.params)


def shaders(root41: str, root6: str) -> dict[str, tuple[bytes, bytes, bytes]]:
    old = rootcpk.Game(root41, 'DX11.cpk')
    new = rootcpk.Game(root6, 'DX11.cpk')
    result = {}
    for name in SHADERS:
        source = old.read('SHADER/DX11', name + '.SGO')
        current = new.read('SHADER/DX11', name + '.SGO')
        alias = edf41materials.build_shader(name, current)
        versions = [sgo.read(data)[1] for data in (source, current, alias)]
        # Forward remains covered as actual bytecode, even though this test reads
        # the Deferred albedo attachment, whose colour contract is independent
        # of the two engines' different light/environment/shadow bindings.
        for technique, passes in versions[1].items():
            if not technique.startswith(('Deferred', 'Forward')):
                continue
            for index, entry in enumerate(passes):
                before = disassemble(entry[4]).splitlines()
                after = disassemble(versions[2][technique][index][4]).splitlines()
                differences = [(a, b) for a, b in zip(before, after) if a != b]
                if len(before) != len(after) or len(differences) != 1:
                    raise AssertionError(f'{name}/{technique}: alias changes more than the reviewed reflection MAD')
                a, b = differences[0]
                if not a.startswith('mad ') or 'l(0.000000)' not in b or a.split(',')[2:] != b.split(',')[2:]:
                    raise AssertionError(f'{name}/{technique}: alias changes colour contract')
        result[name] = tuple(document['Deferred'][0][4] for document in versions)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root6 = gamedir.find()
    parser.add_argument('--edf6', default=root6)
    parser.add_argument('--edf41', default=gamedir.find_other(gamedir.EDF41, root6))
    parser.add_argument('--probe', type=Path, default=ROOT / 'build/outfit41_color_warp_test.exe')
    args = parser.parse_args()
    if os.name != 'nt' or not args.edf41 or not args.edf6:
        print('SKIP actual outfit WARP test requires Windows and installed EDF4.1 / EDF6')
        return 77
    if not args.probe.is_file():
        parser.error(f'compile the native WARP probe first: {args.probe}')
    code = shaders(args.edf41, args.edf6)
    index = archives(args.edf41)
    records = {}
    coverage = {}
    max_parity = 0.0
    max_alpha = 0.0
    unique = {}
    material_count = 0
    texture_count = 0
    with tempfile.TemporaryDirectory(prefix='edf41-outfit-warp-') as directory:
        for row in shipped.registry()['classes']:
            source_rel, *source_names = row['report']['edf41']
            original = mdb.rab_read(index[source_rel.upper()].read(*source_rel.rsplit('/', 1)))
            output_rel = next(rel for rel in row['files'] if rel.endswith('.MRAB'))
            converted = mdb.rab_read(shipped.read(output_rel))
            source_models = {member.name.casefold(): member for member in original.files if member.name.casefold().endswith('.mdb')}
            source_textures = {member.name.casefold(): member.data for member in original.files if member.name.casefold().endswith('.dds')}
            target_textures = {member.name.casefold(): member.data for member in converted.files if member.name.casefold().endswith('.dds')}
            target_models = [member for member in converted.files if member.name.casefold().endswith('.mdb')]
            if len(target_models) != len(source_names) or len(target_models) != 3:
                raise AssertionError(f'{row["class"]}: expected body and both LODs')
            coverage[row['class']] = [0.0, 0.0]
            for lod, (name, target_member) in enumerate(zip(source_names, target_models)):
                source_model = mdb.mdb_read(source_models[name.casefold()].data)
                target_model = mdb.mdb_read(target_member.data)
                if len(source_model.materials) != len(target_model.materials):
                    raise AssertionError(f'{row["class"]}/{lod}: material ordering changed')
                used = {mesh.material for obj in target_model.objects for mesh in obj.meshes if mesh.material != -1}
                if any(i < 0 or i >= len(target_model.materials) for i in used):
                    raise AssertionError(f'{row["class"]}/{lod}: invalid drawn material index')
                lod_records = 0
                for material_index in sorted(used):
                    target = target_model.materials[material_index]
                    shader = target.shader.upper()
                    if shader not in SHADERS:
                        continue
                    source = source_model.materials[material_index]
                    if source.shader.upper() != shader:
                        raise AssertionError('source and generated outfit changed dye shader family')
                    for channel in ('change_color0', 'change_color1'):
                        before = next(param for param in source.params if param.name == channel)
                        after = next(param for param in target.params if param.name == channel)
                        if (before.type, after.type, before.value) != (0x302, 0x402, after.value):
                            raise AssertionError(f'{row["class"]}/{lod}: active colour value/type changed')
                    textures = []
                    for kind in KINDS:
                        old_binding = next(binding for binding in source.textures if binding.kind == kind)
                        new_binding = next(binding for binding in target.textures if binding.kind == kind)
                        old_texture = source_model.textures[old_binding.texture].filename.casefold()
                        new_texture = target_model.textures[new_binding.texture].filename.casefold()
                        if old_texture != new_texture or source_textures[old_texture] != target_textures[new_texture]:
                            raise AssertionError(f'{row["class"]}/{lod}: {kind} source DDS changed')
                        textures.append(target_textures[new_texture])
                        texture_count += 1
                    data = struct.pack('<I', 0x31434c4f) + b''.join(blob(program) for program in code[shader])
                    data += parameters(source) + parameters(target) + b''.join(blob(texture) for texture in textures)
                    digest = hashlib.sha256(data).hexdigest()
                    if digest not in unique:
                        fixture = Path(directory, digest + '.bin')
                        fixture.write_bytes(data)
                        run = subprocess.run([str(args.probe.resolve()), str(fixture)], capture_output=True, text=True, check=False)
                        if run.returncode:
                            raise AssertionError(f'{row["class"]}/{lod}/{material_index} {shader}: {run.stdout}{run.stderr}')
                        unique[digest] = json.loads(run.stdout)
                    result = unique[digest]
                    coverage[row['class']][0] = max(coverage[row['class']][0], result['primary'])
                    coverage[row['class']][1] = max(coverage[row['class']][1], result['secondary'])
                    max_parity = max(max_parity, result['parity'])
                    max_alpha = max(max_alpha, result['alpha'])
                    records[(row['class'], lod, material_index)] = result
                    material_count += 1
                    lod_records += 1
                if not lod_records:
                    raise AssertionError(f'{row["class"]}/{lod}: no used dye material was rendered')
        for name, changes in coverage.items():
            if min(changes) < 0.01:
                raise AssertionError(f'{name}: actual mask must produce independently visible changes for both saved palette slots: {changes}')
            print(f'WARP {name}: all 3 LODs; independent palette0/1 max RGB change={changes[0]:.6f}/{changes[1]:.6f}')
    if set(coverage) != {'ranger', 'wingdiver', 'airraider', 'fencer'}:
        raise AssertionError('all four source soldier classes must be rendered')
    print(f'PASS actual DXBC, actual BC1/BC3 DDS, actual material defaults: {material_count} used colour materials, '
          f'{texture_count} source texture comparisons, {len(unique)} distinct native fixtures; '
          f'{len(unique) * 3 * 4 * 4096} pixel invocations; max original41-vs-alias6 RGB error={max_parity:.8f}, '
          f'palette alpha change={max_alpha:.8f}')
    print('This proves the file and shader pigment contract; native HQ/battle colour uploads and real-game lighting require separate validation.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
