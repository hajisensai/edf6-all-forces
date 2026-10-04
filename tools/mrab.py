"""List the bones of the models inside an EDF6 .MRAB/.RAB archive.

Formats (worked out from the files and EDF.dll, TimeDateStamp 0x678CCB46):

RAB ('SSA\\0'): header u32 at 0x14 = file count, 0x18 = file table offset. Each file entry is 0x20 bytes:
  +0 i32 name offset (relative to the entry, UTF-16LE), +4 u32 stored size, +8 u32 folder index,
  +0xC u32 flag, +0x18 u64 data offset (from file start).
  Each stored file is either raw or 'CMPL' compressed (the textures are; the .mdb models usually too).

CMPL: 'CMPL' + u32 big-endian output size, then Okumura LZSS: 4 KiB window zero-filled, write position
  0xFEE, one flag byte per 8 items (LSB first, 1 = literal byte), a reference is two bytes
  b0 b1 -> offset = b0<<4 | b1>>4, length = (b1 & 0xF) + 3. Decoder in EDF.dll: 0x3F540
  (CMPL check 0x3F7F0, size read 0x3F810, encoder 0x3F350).

MDB ('MDB0', version 0x20): u32 header [2] = name count, [3] = name table offset (each entry an i32
  offset relative to itself -> UTF-16LE string), [4] = bone count, [5] = bone table offset.
  Bone = 0xC0 bytes: +0 index, +4 parent, +8 next sibling, +0xC first child, +0x10 name index,
  +0x20 4x4 local matrix, +0x60 4x4 matrix (bind/inverse), +0xA0 vec4 half extents,
  +0xB0 vec4 bounds centre (model space; zero for the root).

Usage: python tools/mrab.py BOMBER501.MRAB [V506_HELI.MRAB ...]
  Names without a path are read from OBJECT/ in the game's Root.cpk (testrange/lib readers).
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'testrange', 'lib'))
import gamedir  # noqa: E402
GAME = gamedir.find_or_dev()


def cmpl_decompress(data: bytes) -> bytes:
    if data[:4] != b'CMPL':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    win = bytearray(4096)
    r = 0xFEE
    out = bytearray()
    i = 8
    flags = 0
    while len(out) < size and i < len(data):
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            c = data[i]
            i += 1
            out.append(c)
            win[r] = c
            r = (r + 1) & 0xFFF
            continue
        b0, b1 = data[i], data[i + 1]
        i += 2
        off = (b0 << 4) | (b1 >> 4)
        for k in range((b1 & 0xF) + 3):
            c = win[(off + k) & 0xFFF]
            out.append(c)
            win[r] = c
            r = (r + 1) & 0xFFF
    return bytes(out[:size])


def _wstr(b: bytes, p: int) -> str:
    e = p
    while e + 1 < len(b) and b[e:e + 2] != b'\0\0':
        e += 2
    return b[p:e].decode('utf-16le', 'replace')


def rab_files(b: bytes) -> list[tuple[str, bytes]]:
    assert b[:4] == b'SSA\0', 'not a RAB archive'
    n, table = struct.unpack_from('<II', b, 0x14)
    out = []
    for i in range(n):
        e = table + i * 0x20
        name_off, size = struct.unpack_from('<iI', b, e)
        data_off = struct.unpack_from('<Q', b, e + 0x18)[0]
        out.append((_wstr(b, e + name_off), b[data_off:data_off + size]))
    return out


@dataclass
class Bone:
    index: int
    parent: int
    name: str
    half: tuple[float, float, float]
    centre: tuple[float, float, float]
    local_pos: tuple[float, float, float]


def mdb_bones(m: bytes) -> list[Bone]:
    m = cmpl_decompress(m)
    assert m[:4] == b'MDB0', 'not an MDB model'
    h = struct.unpack_from('<6I', m, 0)
    names = [_wstr(m, h[3] + i * 4 + struct.unpack_from('<i', m, h[3] + i * 4)[0]) for i in range(h[2])]
    bones = []
    for k in range(h[4]):
        p = h[5] + k * 0xC0
        idx, parent, _sib, _child, ni = struct.unpack_from('<5i', m, p)
        local = struct.unpack_from('<16f', m, p + 0x20)
        bones.append(Bone(idx, parent, names[ni] if 0 <= ni < len(names) else f'#{ni}',
                          struct.unpack_from('<3f', m, p + 0xA0), struct.unpack_from('<3f', m, p + 0xB0),
                          local[12:15]))
    return bones


def _load(arg: str) -> bytes:
    if os.path.exists(arg):
        with open(arg, 'rb') as h:
            return h.read()
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, '..', 'testrange', 'lib'))
    sys.path.insert(0, os.path.join(here, '..', 'testrange'))
    from gen import Game  # noqa: E402
    return Game(GAME).read('OBJECT', arg)


def main(argv: list[str]) -> None:
    for arg in argv:
        print(f'== {arg}')
        for name, data in rab_files(_load(arg)):
            if not name.lower().endswith('.mdb'):
                continue
            print(f'  {name}')
            for bn in mdb_bones(data):
                print(f'    [{bn.index:2}] parent {bn.parent:2}  {bn.name:<22} '
                      f'pos {bn.local_pos[0]:7.2f} {bn.local_pos[1]:7.2f} {bn.local_pos[2]:7.2f}  '
                      f'half {bn.half[0]:6.2f} {bn.half[1]:6.2f} {bn.half[2]:6.2f}  '
                      f'centre {bn.centre[0]:6.2f} {bn.centre[1]:6.2f} {bn.centre[2]:6.2f}')


if __name__ == '__main__':
    main(sys.argv[1:])
