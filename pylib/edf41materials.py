"""Isolated EDF4.1 diffuse compatibility shaders, derived from the player's EDF6.

Only the proved ``base + reflect * (1-base)`` MAD multiplier becomes zero.
All other PBR calculations, shader stages, signatures, techniques and metadata
stay byte-identical. Alpha shaders restore the original diffuse RGBA together.
No game files are written here; callers must pass only EDF4.1-owned models.
A registered gold coat (pylib/goldcoat.py, the BM03 Vegalta gold) is converted to EDF6's
own metal-gold BRDF material instead, in the same pass (transform_models, archives only).
"""
from __future__ import annotations

from collections.abc import Iterable
import hashlib
import math
import os
import struct

import goldcoat
import mdb
import rootcpk
import sgo

PREFIX = 'EDF41_'
SOURCE_HASHES = {
    'SND_MECH': '1256766c15b1c7f8dac0834c3a9ad36d3cc5fad38f3e51122f1b4bd76c807821',
    'SND_MECH_CATAPILLAR': '018db0cf519eb3948453b0026dd5b4d32c12f1b0f033ba4b39c07c1e3324f2bf',
    'SND_CHARA_BASIC': 'fb682d58d789a3a830703cc3e098dbccb0c52ba7951afdb5f92ae008d12edc7b',
    'SND_CHARA_ALPHA': '3b3f279b2b7767f1d65a5bb22936d2ffd13758ae84117b897f50690ff9584aa0',
    'SND_CHARA_2COLORCHANGE': '352aae5236f38631a82705ce0f5584c64ca1c948f6c0ac5ae236b21ea13c0bf8',
    'SND_CHARA_2COLORCHANGE_ALPHA': '6d7a65cf9f137a1e0e554566eb6a5bb8d8a7b182dfbe42fe29519bfd8b90fa7d',
    'SND_UFO_LIGHTSCROLL': '01eb1dec26ed84f5fd19fd08498f359cf0a9959f00cf726d6b1a47524cbe2ba5',
    'SND_UFO_LIGHTSCROLL2': '31ad3d6ade2074ef0a4048762d36c5d317b718afded71c313bf25ae817122d9e',
    'SND_UFO_LIGHTSCROLL2_ALPHA': '12aced090975abf2506dfea1805bc665e768ede6317cb2dfdef97afc633ab0f4',
    'SND_MECH_LIGHT': '5179a4349552fa1ae570387a9dcc34fb47800f23b439ff3d1c5df40d3a59bf7b',
    'SND_DINO_LIGHTSCROLL': '7ef7bb39934266438792f8ac69d61f3b342b07d5ac6bebfea0aa455be8e3f309',
    'SND_SOFTMECH': '203925c6f52097e3d1ade77a4babe35db273210db8068d13aac63f6faa0df004',
    'SND_COMMON_BASIC': 'e79e082fa1f88d48e95e86fcec63b3f1861ae2e4909bd902cb546def60adade4',
    'SND_COMMON_LIGHT_NOOCC': '4263a3edee1161d9162e569f7804321e4641527f5d2e05b3fdcaf1715f074f6d',
}
SUPPORTED = frozenset(SOURCE_HASHES)
# Exact complete instruction tokens, keyed by the original PS hash. These were
# derived from D3DDisassemble, not offsets/searches into arbitrary shader bytes.
_PATCHES = {
    'e00e1f0198b3a65d22394a3bcfda65a1cdbe07479d871999879d673f68819212': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100796, 1, 0x100246, 2),
    'e4553b9a6777916dde3a1449cedc677da85052fa21f5740cfc65e3f28ef25145': (0x9000032, 0x100072, 2, 0x100006, 1, 0x100246, 2, 0x100246, 4),
    'b7eeb633adf3dbc984be1d50de6c4175b491902b6af7a36385261a69fb354fce': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 0, 0x100386, 2),
    'ea7635220ca7417e7791643ef84b50424a32c1e0e1c9a6bfd007359f48e26bf0': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 2, 0x100246, 3),
    '576e4dc66d7ae71a0fc991342f0839dddab225d0371d1100d7410662683a3d83': (0x9000032, 0x100072, 2, 0x100006, 1, 0x100246, 2, 0x100246, 4),
    'e0aa811649b5345e7b396becfd374c426caf484f77cf39a32993aab6afe61999': (0x9000032, 0x1000F2, 1, 0x100FF6, 0, 0x100E46, 1, 0x100E46, 2),
    '78ab17fc888eeb2c7f15a6aa6dff79130994aa5eec2b7e8a45ecbd3304efdf03': (0x9000032, 0x1000F2, 4, 0x100006, 1, 0x100936, 5, 0x100936, 4),
    '53989acfb8e3f38cd352fc9c1002c556323a20f06160bf88ed6b3afddd741dc1': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 2, 0x100246, 0),
    'a3babf75d9423ff7601ba8f0ecb085c2e40d834b0836b6a1c013263904005f16': (0x9000032, 0x100072, 4, 0x100006, 1, 0x100246, 4, 0x100246, 5),
    '80c883c0c90bce289424e04086e39655ea68d1f888081214e9819d1b5cbd548d': (0x9000032, 0x1000F2, 1, 0x100006, 0, 0x100E46, 2, 0x100E46, 1),
    '043ac11005ca58de67f54b61227a505fa6da5563c1645d7468e2facd502eae2d': (0x9000032, 0x1000F2, 4, 0x100006, 1, 0x100936, 4, 0x100936, 5),
    '7d1b25ca240fd481acc1d1ed6d6442cbd1f7e17606b7437fb99ab72c18ddbdb3': (0x9000032, 0x102072, 0, 0x100006, 0, 0x100796, 0, 0x100246, 2),
    'd408206682ab851cae1c852d991ea069a3b55e312810ad52a7509518558aa03c': (0x9000032, 0x102072, 0, 0x100006, 0, 0x100796, 0, 0x100246, 2),
    'b34c1d4cae0162c93b9b9378051b83c9b599d9630c8ed00815f56e1e094da47b': (0x9000032, 0x1000F2, 4, 0x100006, 1, 0x100936, 6, 0x100936, 4),
    'bd0031282e78b4463c430f7096460052a8f8a6a171ab60b3274d0bc3a5cffb17': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 2, 0x100246, 3),
    'c773a66823bb40734d8f78201245706db4e8b09719c06db7f7153e68ad716cc9': (0x9000032, 0x100072, 4, 0x100006, 1, 0x100246, 4, 0x100246, 5),
    '9ce5c0bef0385383ad1d0ca80c7f73fac53f10f39280a17a9aab0c62699fc3b3': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 2, 0x100246, 3),
    '4d9ef3b86c2ef61888459cc90ff27e462a63f3819b0a18ec8248da4e830b92db': (0x9000032, 0x102072, 0, 0x100FF6, 0, 0x100246, 2, 0x100246, 3),
    '38f354003558cb17e6f0ce9a0b5ed71e3ddfc13277f9bd9f155d38d7b963670d': (0x9000032, 0x1000B2, 4, 0x100006, 1, 0x100C46, 4, 0x100846, 6),
    '5bd78aeeb621c220f9221a8061813e56bd24bc87cdfee7f3ece020de89a7685b': (0x9000032, 0x102072, 0, 0x100006, 0, 0x100796, 0, 0x100246, 2),
    'd04f24a2e7d9b3837b357d90ccef6232807e3af3bb7175a47921635b55933b27': (0x9000032, 0x100072, 2, 0x100006, 1, 0x100246, 2, 0x100246, 4),
}
_MASK = 0xFFFFFFFF
_MD5_K = tuple(int(abs(math.sin(i + 1)) * 2**32) for i in range(64))
_MD5_S = (7, 12, 17, 22) * 4 + (5, 9, 14, 20) * 4 + (4, 11, 16, 23) * 4 + (6, 10, 15, 21) * 4


def _compress(state: tuple[int, ...], block: bytes) -> tuple[int, ...]:
    """RFC1321 MD5 compression; DXBC has a different final-block layout."""
    words = struct.unpack('<16I', block)
    a, b, c, d = state
    for i in range(64):
        if i < 16:
            f, at = (b & c) | (~b & d), i
        elif i < 32:
            f, at = (d & b) | (~d & c), (5 * i + 1) % 16
        elif i < 48:
            f, at = b ^ c ^ d, (3 * i + 5) % 16
        else:
            f, at = c ^ (b | ~d), (7 * i) % 16
        value = (a + f + _MD5_K[i] + words[at]) & _MASK
        shift = _MD5_S[i]
        value = ((value << shift) | (value >> (32 - shift))) & _MASK
        a, b, c, d = d, (b + value) & _MASK, b, c
    return tuple((x + y) & _MASK for x, y in zip(state, (a, b, c, d)))


def dxbc_checksum(data: bytes) -> bytes:
    """DXBC checksum of bytes after the 20-byte magic/checksum prefix.

    Independent MD5 compression implementation; the DXBC final-block layout is
    documented by AMD's DXBCChecksum and corroborated against stock containers.
    This is an integrity checksum, not a security primitive.
    """
    if len(data) < 32 or data[:4] != b'DXBC':
        raise ValueError('not a DXBC container')
    payload = data[20:]
    length = len(payload) * 8
    if length > _MASK:
        raise ValueError('DXBC exceeds the checksum length range')
    end = len(payload) // 64 * 64
    blocks = bytearray(payload[:end])
    tail = payload[end:]
    if len(tail) >= 56:
        blocks += tail + b'\x80' + bytes(63 - len(tail))
        final = bytearray(64)
        struct.pack_into('<I', final, 0, length)
    else:
        final = bytearray(struct.pack('<I', length) + tail + b'\x80')
        final += bytes(64 - len(final))
    struct.pack_into('<I', final, 60, (length >> 2) | 1)
    blocks += final
    state = (0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476)
    for offset in range(0, len(blocks), 64):
        state = _compress(state, blocks[offset:offset + 64])
    return struct.pack('<4I', *state)


def _code(data: bytes) -> tuple[int, int]:
    if (len(data) < 36 or data[:4] != b'DXBC' or
            struct.unpack_from('<I', data, 24)[0] != len(data)):
        raise ValueError('invalid DXBC header/size')
    count = struct.unpack_from('<I', data, 28)[0]
    if not 0 < count <= (len(data) - 32) // 4:
        raise ValueError('invalid DXBC chunk table')
    chunks = []
    code = []
    for offset in struct.unpack_from(f'<{count}I', data, 32):
        if offset < 32 + count * 4 or offset + 8 > len(data):
            raise ValueError('DXBC chunk outside container')
        length = struct.unpack_from('<I', data, offset + 4)[0]
        if offset + 8 + length > len(data):
            raise ValueError('truncated DXBC chunk')
        chunks.append((offset, offset + 8 + length))
        if data[offset:offset + 4] in (b'SHDR', b'SHEX'):
            code.append((offset + 8, length))
    if any(a[1] > b[0] for a, b in zip(sorted(chunks), sorted(chunks)[1:])) or len(code) != 1:
        raise ValueError('overlapping DXBC chunks or ambiguous shader program')
    return code[0]


def patch_pixel_shader(data: bytes) -> bytes:
    """Patch one reviewed PS; reject unknown game/compiler output, never guess."""
    pattern = _PATCHES.get(hashlib.sha256(data).hexdigest())
    if pattern is None or data[4:20] != dxbc_checksum(data):
        raise ValueError('EDF6 pixel shader differs from the reviewed material contract')
    offset, size = _code(data)
    if size < 8 or size % 4 or struct.unpack_from('<I', data, offset + 4)[0] * 4 != size:
        raise ValueError('invalid tokenized shader size')
    words = struct.unpack_from(f'<{size // 4}I', data, offset)
    hits = []
    at = 2
    while at < len(words):
        token = words[at]
        length = words[at + 1] if token & 0x7FF == 53 else (token >> 24) & 0x7F
        if not length or at + length > len(words):
            raise ValueError('invalid shader instruction length')
        if tuple(words[at:at + length]) == pattern:
            hits.append(offset + at * 4)
        at += length
    if len(hits) != 1:
        raise ValueError('reviewed diffuse MAD is absent or ambiguous')
    out = bytearray(data)
    # Original first source is an unextended two-DWORD TEMP scalar selector.
    # Immediate32 scalar [token, float32] has the same width: no offsets change.
    struct.pack_into('<2I', out, hits[0] + 12, 0x4001, 0)
    out[4:20] = dxbc_checksum(out)
    return bytes(out)


def source_name(name: str) -> str | None:
    name = name.upper()
    if name.startswith(PREFIX):
        name = name[len(PREFIX):]
    return name if name in SUPPORTED else None


def shader_name(name: str) -> str:
    """Return a private alias only for reviewed old material shader families."""
    source = source_name(name)
    return PREFIX + source if source is not None else name


def shader_path(name: str) -> str:
    source = source_name(name)
    if source is None:
        raise ValueError(f'unsupported EDF4.1 material shader: {name}')
    return f'SHADER/DX11/{PREFIX}{source}.SGO'


def build_shader(name: str, data: bytes) -> bytes:
    source = source_name(name)
    if source is None or hashlib.sha256(data).hexdigest() != SOURCE_HASHES[source]:
        raise ValueError(f'{name}: EDF6 shader differs from the reviewed source')
    _, document = sgo.read(data)
    replacements = {}
    for technique, passes in document.items():
        if not technique.startswith(('Deferred', 'Forward')):
            continue
        for entry in passes:
            if len(entry) != 5 or not isinstance(entry[4], bytes):
                raise ValueError(f'{name}/{technique}: unrecognised shader pass')
            pixel = entry[4]
            if pixel not in replacements:
                replacements[pixel] = patch_pixel_shader(pixel)
    if not replacements:
        raise ValueError(f'{name}: no reviewed material pixel shader')
    out = data
    for before, after in replacements.items():
        if not out.count(before):
            raise ValueError(f'{name}: SGO does not contain its pixel-shader blob')
        out = out.replace(before, after)
    # No SGO reserialisation: VS, technique names, signatures, metadata, padding,
    # and non-material passes remain exactly where they were in the stock file.
    return out


def build_shaders(root6: str, names: Iterable[str] | None = None) -> dict[str, bytes]:
    wanted = set(SUPPORTED if names is None else names)
    if not wanted:
        return {}
    game = rootcpk.Game(root6, 'DX11.cpk')
    out = {}
    for name in sorted(wanted):
        source = source_name(name)
        if source is None:
            raise ValueError(f'unsupported EDF4.1 material shader: {name}')
        out[shader_path(source)] = build_shader(source, game.read('SHADER/DX11', source + '.SGO'))
    return out


def _models(rel: str, data: bytes) -> list[tuple[str, bytes]]:
    ext = os.path.splitext(rel)[1].upper()
    if ext == '.MDB':
        return [(rel, mdb.cmpl_decompress(data))]
    if ext not in ('.RAB', '.MRAB'):
        return []
    archive = mdb.rab_read(data)
    if mdb.rab_write(archive) != data:
        raise ValueError(f'{rel}: archive does not round-trip unchanged')
    return [(f.name, f.data) for f in archive.files if f.name.lower().endswith('.mdb')]


def needed_names(files: dict[str, bytes]) -> set[str]:
    """Reviewed source shader names actually referenced by these model files."""
    return {name for rel, data in files.items() for _, raw in _models(rel, data)
            for mat in mdb.mdb_read(raw).materials if (name := source_name(mat.shader)) is not None}


def _transform_mdb(raw: bytes) -> bytes:
    model = mdb.mdb_read(raw)
    if mdb.mdb_write(model) != raw or model.version != 0x20:
        raise ValueError('material compatibility requires a round-trippable EDF6 model')
    changed = False
    for material in model.materials:
        name = shader_name(material.shader)
        if name != material.shader:
            changed = True
            material.shader = name
    return mdb.mdb_write(model) if changed else raw


def transform_models(rel: str, data: bytes) -> bytes:
    """Alias reviewed materials in an EDF4.1-owned model/archive (an archive's gold coats converted first, see
    pylib/goldcoat.py), or return data."""
    ext = os.path.splitext(rel)[1].upper()
    if ext == '.MDB':
        raw = mdb.cmpl_decompress(data)
        result = _transform_mdb(raw)
        return data if result == raw else mdb.cmpl_compress(result) if data[:4] == b'CMPL' else result
    if ext not in ('.RAB', '.MRAB'):
        return data
    archive = mdb.rab_read(data)
    if mdb.rab_write(archive) != data:
        raise ValueError(f'{rel}: archive does not round-trip unchanged')
    changed = False
    for member in list(archive.files):
        if member.name.lower().endswith('.mdb'):
            raw = member.data
            result = raw
            if goldcoat.coats_of(member.name):
                # A gold coat (pylib/goldcoat.py) goes to EDF6's own metal-gold BRDF material first: its textures
                # join the archive, and the material leaves the aliased legacy shader family.
                model = mdb.mdb_read(raw)
                if mdb.mdb_write(model) != raw:
                    raise ValueError(f'{rel}/{member.name}: model does not round-trip unchanged')
                model, notes = goldcoat.apply(archive, model, member.name)
                for note in notes:
                    print('  ' + note)
                result = mdb.mdb_write(model)
            result = _transform_mdb(result)
            if result != raw:
                changed = True
                member.stored = mdb.cmpl_compress(result) if member.stored[:4] == b'CMPL' else result
    return mdb.rab_write(archive) if changed else data
