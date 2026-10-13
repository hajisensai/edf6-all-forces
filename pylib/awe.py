"""CRI AFS2 (.AWB) headers and EDF's AWBE (.AWE) cue lists: read both versions, write EDF6's (version 2).

An AWB holds the waves; its AWE names them: per cue (sorted by name, the game searches it) the id of its wave in the
AWB. EDF5 and EDF4.1 write version 1 (names, ids); EDF6 refuses it (EDF.dll 0x7A8050 checks 0x0200) and wants version 2,
which adds a float per cue, the cue's length in seconds (what the subtitle stays up for). Layout, measured on EDF6's own
lists (a parse and a rebuild of each gives the same bytes, tools/selftest.py):

  'AWBE', u16 version, u16 0, u32 count, u32 header size (0x14 / 0x18), u32 idsOff, [u32 floatOff (version 2)]
  u32 nameOff[count]  (from the entry's own position)   u16 id[count] at idsOff   f32 seconds[count] at floatOff
  the names, NUL-terminated, in order, to the end of the file (one more NUL where that makes the size odd) (one more NUL when that makes the size odd)
"""
from __future__ import annotations

import struct
from typing import NamedTuple


class Cue(NamedTuple):
    name: str
    wave: int          # id of the cue's wave in the AWB
    seconds: float     # 0.0 in a version 1 list


def read_awe(data: bytes) -> list[Cue]:
    """Every cue of an AWE's bytes (version 1 or 2), in file order."""
    if data[:4] != b'AWBE':
        raise ValueError(f'not an AWE: {bytes(data[:4])!r}')
    version, _zero, count, head, ids_at = struct.unpack_from('<HHIII', data, 4)
    if version not in (0x100, 0x200):
        raise ValueError(f'AWE version {version:#x}')
    secs = struct.unpack_from(f'<{count}f', data, struct.unpack_from('<I', data, 0x14)[0]) if version == 0x200 else (0.0,) * count
    ids = struct.unpack_from(f'<{count}H', data, ids_at)
    out = []
    for i in range(count):
        at = head + 4 * i + struct.unpack_from('<I', data, head + 4 * i)[0]
        out.append(Cue(bytes(data[at:data.index(b'\0', at)]).decode('ascii'), ids[i], secs[i]))
    return out


def write_awe2(cues: list[Cue]) -> bytes:
    """EDF6's version 2 list of `cues`, sorted by name."""
    cues = sorted(cues, key=lambda c: c.name.encode('ascii'))
    n = len(cues)
    ids_at = 0x18 + 4 * n
    floats_at = ids_at + 2 * n
    at = floats_at + 4 * n
    offs = []
    for i, c in enumerate(cues):
        offs.append(at - (0x18 + 4 * i))
        at += len(c.name) + 1
    return b''.join([
        b'AWBE', struct.pack('<HHIIII', 0x200, 0, n, 0x18, ids_at, floats_at),
        struct.pack(f'<{n}I', *offs), struct.pack(f'<{n}H', *(c.wave for c in cues)),
        struct.pack(f'<{n}f', *(c.seconds for c in cues)),
        b''.join(c.name.encode('ascii') + b'\0' for c in cues), b'\0' * (at & 1)])   # padded to an even size


def afs2_waves(data: bytes) -> dict[int, tuple[int, int]]:
    """An AWB's wave id -> (start, end) in `data`: offsets are stored unaligned, a wave starts at the next multiple of
    the alignment (header u16 at 12)."""
    if data[:4] != b'AFS2':
        raise ValueError(f'not an AWB: {bytes(data[:4])!r}')
    off_size, id_size = data[5], data[6]
    count, align = struct.unpack_from('<IH', data, 8)
    ids = struct.unpack_from(f'<{count}{"H" if id_size == 2 else "I"}', data, 16)
    offs = struct.unpack_from(f'<{count + 1}{"I" if off_size == 4 else "Q"}', data, 16 + count * id_size)
    return {w: ((offs[i] + align - 1) // align * align, offs[i + 1]) for i, w in enumerate(ids)}


def hca_seconds(head: bytes) -> float:
    """The playing time of a HCA wave from its header: the blocks' samples less the mute at the start and the end."""
    for tag in (b'fmt\0', bytes(c | 0x80 for c in b'fmt') + b'\0'):
        at = head.find(tag)
        if at >= 0:
            break
    else:
        raise ValueError('no fmt chunk in the HCA header')
    rate = int.from_bytes(head[at + 5:at + 8], 'big')
    blocks, mute_head, mute_tail = struct.unpack_from('>IHH', head, at + 8)
    return (blocks * 1024 - mute_head - mute_tail) / rate
