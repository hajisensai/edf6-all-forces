"""Named points of a mission's MISSION.RMPA (big-endian).

Found by matching records to the name table, not a full format decode:
a point record has +0 id, +4 position (x, y, z), +0x14 a second point it faces (x, y, z),
+0x24 name length in UTF-16 units, +0x28 name offset relative to the record start.
Names are big-endian UTF-16, NUL-terminated, in a table at the end of the file.
The header holds four (present, offset) pairs from +8: routes, shapes, cameras, points. Only records in the
points section are points: a route waypoint carries a name too, but other fields where a point has its
position (at +0x18 an offset into the route data), and the range's air targets once wrote 80.0 there
(2026-10-03): the game read past the file loading the mission and crashed (EDF+6F3B10).
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


def _points_section(data: bytes) -> int:
    """Where the points section starts (the header's fourth pair), or the file's end if it has none."""
    present, at = struct.unpack_from('>II', data, 0x20)
    return at if present and 0 < at < len(data) else len(data)


def _records(data: bytes) -> list[tuple[int, Point]]:
    """Every point record (in the points section): its offset and the point."""
    names = _names(data)
    # The name table follows the shape-type names; offsets before it are record bytes that
    # happen to decode as text.
    table = data.find('Cylinder'.encode('utf-16-be'))
    if table > 0:
        names = {k: v for k, v in names.items() if k >= table}
    out: list[tuple[int, Point]] = []
    for c in range(_points_section(data), len(data) - 0x2C, 4):
        length, rel = struct.unpack_from('>II', data, c + 0x24)
        if not rel or not 0 < length < 64:
            continue
        name = names.get(c + rel)
        if name is None or len(name) != length:
            continue
        pos = struct.unpack_from('>3f', data, c + 4)
        face = struct.unpack_from('>3f', data, c + 0x14)
        if all(abs(v) < 1e5 for v in pos + face):
            out.append((c, Point(name, pos, face)))
    return out


def points(data: bytes) -> list[Point]:
    found: dict[str, Point] = {}
    for _, p in _records(data):
        found.setdefault(p.name, p)
    return list(found.values())


def raised(data: bytes, names: set[str], dy: float) -> bytes:
    """`data` with the points named in `names` (every record of each) and the points they face dy higher."""
    out = bytearray(data)
    hit = set()
    for c, p in _records(data):
        if p.name in names:
            struct.pack_into('>f', out, c + 8, p.pos[1] + dy)
            struct.pack_into('>f', out, c + 0x18, p.face[1] + dy)
            hit.add(p.name)
    assert hit == names, f'points not found: {sorted(names - hit)}'
    return bytes(out)


def moved(data: bytes, to: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]]) -> bytes:
    """`data` with each point named in `to` (every record of it) at a new (position, point it faces)."""
    out = bytearray(data)
    hit = set()
    for c, p in _records(data):
        if p.name in to:
            pos, face = to[p.name]
            struct.pack_into('>3f', out, c + 4, *pos)
            struct.pack_into('>3f', out, c + 0x14, *face)
            hit.add(p.name)
    assert hit == set(to), f'points not found: {sorted(set(to) - hit)}'
    return bytes(out)
