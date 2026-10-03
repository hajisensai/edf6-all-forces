"""Named points of a mission's MISSION.RMPA (big-endian).

Found by matching records to the name table, not a full format decode:
a point record has +0 id, +4 position (x, y, z), +0x14 a second point it faces (x, y, z),
+0x24 name length in UTF-16 units, +0x28 name offset relative to the record start.
Names are big-endian UTF-16, NUL-terminated, in a table at the end of the file.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Point:
    name: str
    pos: tuple[float, float, float]
    face: tuple[float, float, float]


def _names(data: bytes) -> dict[int, str]:
    """Every NUL-terminated big-endian UTF-16 string at an even offset, by offset."""
    out: dict[int, str] = {}
    i = 0
    while i + 2 <= len(data):
        j = i
        while j + 2 <= len(data) and data[j:j + 2] != b'\x00\x00':
            j += 2
        if j > i:
            try:
                s = data[i:j].decode('utf-16-be')
                if s.isprintable():
                    out[i] = s
            except UnicodeDecodeError:
                pass
        i = j + 2
    return out


def points(data: bytes) -> list[Point]:
    names = _names(data)
    # The name table follows the shape-type names; offsets before it are record bytes that
    # happen to decode as text.
    table = data.find('Cylinder'.encode('utf-16-be'))
    if table > 0:
        names = {k: v for k, v in names.items() if k >= table}
    found: dict[str, Point] = {}
    for c in range(0, len(data) - 0x2C, 4):
        length, rel = struct.unpack_from('>II', data, c + 0x24)
        if not rel or not 0 < length < 64:
            continue
        name = names.get(c + rel)
        if name is None or len(name) != length:
            continue
        pos = struct.unpack_from('>3f', data, c + 4)
        face = struct.unpack_from('>3f', data, c + 0x14)
        if all(abs(v) < 1e5 for v in pos + face):
            found.setdefault(name, Point(name, pos, face))
    return list(found.values())
