"""An EDF4.1 / EDF5 object SGO in the form EDF6 loads: the same file with every MAB block it holds turned from the
earlier games' absolute offsets (0x03) into EDF6's relative ones (0x83, pylib/mab_legacy.py mab_from_edf5).

    convert(data) -> bytes      ValueError on a file that is not an SGO or a MAB block mab_legacy refuses

EDF6 still reads the earlier games' SGO format (v0x102: 143 of its own 300 OBJECT files sampled, 2026-10-10), so the
file is not re-encoded: sgo.write_depth_first does not lay out 174 of 4.1's 233 OBJECT SGOs again byte for byte.
mab_from_edf5 keeps a block's length (nothing moves, only what the offsets count from), so each block is replaced in
place and every other byte stays 4.1's.
"""
from __future__ import annotations

import struct

import mab_legacy
import sgo


def _blobs(v: object) -> list[bytes]:
    if isinstance(v, bytes):
        return [v]
    if isinstance(v, dict):
        return [b for x in v.values() for b in _blobs(x)]
    if isinstance(v, list):
        return [b for x in v for b in _blobs(x)]
    return []


def mab_blocks(data: bytes) -> list[bytes]:
    """The earlier games' MAB blocks (flag 0x03) the SGO `data` holds, each once."""
    if data[:4] != b'SGO\0':
        raise ValueError('not an SGO')
    _version, members = sgo.read(data)
    out: list[bytes] = []
    for b in _blobs(members):
        if b[:4] == b'MAB\0' and struct.unpack_from('<I', b, 8)[0] == mab_legacy.LEGACY and b not in out:
            out.append(b)
    return out


def convert(data: bytes) -> bytes:
    out = bytearray(data)
    for block in mab_blocks(data):
        at = data.find(block)
        if at < 0 or data.find(block, at + 1) >= 0:
            raise ValueError('a MAB block not found exactly once in its SGO')
        new = mab_legacy.mab_from_edf5(block)
        assert len(new) == len(block)
        out[at:at + len(block)] = new
    return bytes(out)
