"""Little-endian SGO read/modify/write, for the range's derived vehicles (lib/sgo.py stays a read-only
dumper). A node is 12 bytes {type, a, b}: 0 array (a children at node + b), 1 int (at node + 8),
2 float (at node + 8), 3 UTF-16 string (a characters at node + b; a = 0: empty), 4 raw block (a bytes
at node + b). File: header {'SGO\\0', version, count, data offset, name count, name table offset}, the
top-level nodes at the data offset in member order, names {string offset from the entry, member}
sorted by name.

Values come back as Python objects that keep the exact bytes: list (array), int, Float (the 4 bytes
as stored), str, bytes (raw). A plain Python float is accepted when writing.
Layout written: header, top nodes, arrays (breadth first), name table, raw blocks (16-aligned, as the
stock files place them), strings (deduplicated).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Float:
    raw: bytes

    @property
    def value(self) -> float:
        return struct.unpack('<f', self.raw)[0]


Value = list | int | Float | float | str | bytes


def _utf16_at(buf: bytes, off: int, count: int) -> str:
    return buf[off:off + count * 2].decode('utf-16le')


def _name_at(buf: bytes, off: int) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def _node(buf: bytes, pos: int) -> Value:
    typ, a, b = struct.unpack_from('<iiI', buf, pos)
    if typ == 0:
        return [_node(buf, pos + b + i * 12) for i in range(a)]
    if typ == 1:
        return struct.unpack_from('<i', buf, pos + 8)[0]
    if typ == 2:
        return Float(bytes(buf[pos + 8:pos + 12]))
    if typ == 3:
        return _utf16_at(buf, pos + b, a) if a else ''
    if typ == 4:
        return bytes(buf[pos + b:pos + b + a])
    raise ValueError(f'SGO 节点类型 {typ} @ {pos:#x}')


def read(data: bytes) -> tuple[int, dict[str, Value]]:
    """(version, members in file order)."""
    if data[:4] != b'SGO\0':
        raise ValueError('不是小端 SGO')
    version, count, data_off, name_count, name_off = struct.unpack_from('<5I', data, 4)
    names: dict[int, str] = {}
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from('<iI', data, p)
        names[idx] = _name_at(data, p + rel)
    if sorted(names) != list(range(count)):
        raise ValueError('SGO 名表与成员数不符')
    return version, {names[i]: _node(data, data_off + i * 12) for i in range(count)}


def write(version: int, members: dict[str, Value]) -> bytes:
    """The SGO for `members` (written in name order, like the stock files)."""
    order = sorted(members)
    header = 0x20
    buf = bytearray(header + len(order) * 12)
    # Pass 1: lay out arrays breadth first; remember every scalar/string/raw node to patch later.
    strings: list[tuple[int, str]] = []
    raws: list[tuple[int, bytes]] = []
    queue: list[tuple[int, Value]] = [(header + i * 12, members[n]) for i, n in enumerate(order)]
    while queue:
        nxt: list[tuple[int, Value]] = []
        for pos, v in queue:
            if isinstance(v, list):
                at = len(buf)
                buf += bytes(len(v) * 12)
                struct.pack_into('<iiI', buf, pos, 0, len(v), at - pos if v else 0)
                nxt += [(at + i * 12, c) for i, c in enumerate(v)]
            elif isinstance(v, bool):
                raise TypeError('SGO 没有布尔类型')
            elif isinstance(v, int):
                struct.pack_into('<iii', buf, pos, 1, 4, v)
            elif isinstance(v, Float):
                struct.pack_into('<ii', buf, pos, 2, 4)
                buf[pos + 8:pos + 12] = v.raw
            elif isinstance(v, float):
                struct.pack_into('<iif', buf, pos, 2, 4, v)
            elif isinstance(v, str):
                strings.append((pos, v))
            elif isinstance(v, bytes):
                raws.append((pos, v))
            else:
                raise TypeError(f'SGO 不支持 {type(v).__name__}')
        queue = nxt
    name_off = len(buf)
    buf += bytes(len(order) * 8)
    for pos, blob in raws:
        while len(buf) % 16:
            buf.append(0)
        at = len(buf)
        buf += blob
        struct.pack_into('<iiI', buf, pos, 4, len(blob), at - pos)
    while len(buf) % 2:
        buf.append(0)
    pool: dict[str, int] = {}

    def place(s: str) -> int:
        if s not in pool:
            pool[s] = len(buf)
            buf.extend(s.encode('utf-16le') + b'\0\0')
        return pool[s]

    for pos, s in strings:
        if s:
            struct.pack_into('<iiI', buf, pos, 3, len(s), place(s) - pos)
        else:
            struct.pack_into('<iiI', buf, pos, 3, 0, 0)
    for i, n in enumerate(order):
        p = name_off + i * 8
        struct.pack_into('<iI', buf, p, place(n) - p, i)
    struct.pack_into('<4s5I', buf, 0, b'SGO\0', version, len(order), header, len(order), name_off)
    while len(buf) % 16:
        buf.append(0)
    return bytes(buf)


def replace_utf16(blob: bytes, at: int, old: str, new: str) -> bytes:
    """`blob` with the NUL-terminated UTF-16 string `old` at `at` shortened in place to `new` (the rest
    of its room filled with NULs)."""
    o = old.encode('utf-16le')
    if blob[at:at + len(o) + 2] != o + b'\0\0' or len(new) > len(old):
        raise ValueError(f'{old!r} 不在 {at:#x} 或新名更长')
    n = new.encode('utf-16le')
    out = bytearray(blob)
    out[at:at + len(o)] = n + bytes(len(o) - len(n))
    return bytes(out)
