"""SGO read / modify / write (SGO format per edf-tools SGO.cpp), and a plain view of any SGO or DSGO file.

  python -B pylib/sgo.py FILE.SGO [name-regex]     print the top-level values (SGO or DSGO; arrays expanded)
  python -B pylib/sgo.py roundtrip FILE.SGO        read FILE and write it back in both layouts below; reports
                                                   whether the bytes are identical

A node is 12 bytes {int type, int a, uint b}: 0 array (a children at node + b), 1 int (at node + 8),
2 float (at node + 8), 3 UTF-16 string (a characters at node + b; a = 0: empty), 4 raw block (a bytes at
node + b). File: header {'SGO\\0' (big-endian files: '\\0OGS'), version, count, data offset, name count,
name table offset}, the top-level nodes at the data offset in member order, names {string offset from
the entry, member} sorted by name.

read() takes either byte order; the writers write little-endian. Values come back as Python objects that
keep the exact bytes: list (array), int, Float (the 4 bytes as stored, little-endian), str, bytes (raw).
A plain Python float is accepted when writing.

The two writers lay the same values out differently; each caller keeps the one its files have always been
written with, so a rebuild writes the same bytes (neither is what the stock files have throughout):
  write()              arrays breadth first, name table, raw blocks (16-aligned), strings in first-use
                       order (deduplicated), file padded to 16 (pylib/vcobjects.py's derived vehicles)
  write_depth_first()  arrays depth first, name table, raw blocks, one pool of names and string values
                       sorted by UTF-16 code unit; header 0x1c = the raw blocks' offset (the layout most
                       stock files have; autoturret/tools/titan_ai.py)
DSGO files are pylib/dsgo.py's; load() reads both formats.
"""
from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Float:
    raw: bytes   # little-endian

    @property
    def value(self) -> float:
        return struct.unpack('<f', self.raw)[0]


Value = list | int | Float | float | str | bytes


def _utf16z(buf: bytes, off: int, codec: str) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode(codec)


def read(data: bytes) -> tuple[int, dict[str, Value]]:
    """(version, members in file order), from an SGO of either byte order."""
    if data[:4] == b'SGO\0':
        e, codec = '<', 'utf-16le'
    elif data[:4] == b'\0OGS':
        e, codec = '>', 'utf-16be'
    else:
        raise ValueError(f'不是 SGO（magic {data[:4]!r}）')

    def node(pos: int) -> Value:
        typ, a, b = struct.unpack_from(e + 'iiI', data, pos)
        if typ == 0:
            return [node(pos + b + i * 12) for i in range(a)]
        if typ == 1:
            return struct.unpack_from(e + 'i', data, pos + 8)[0]
        if typ == 2:
            return Float(struct.pack('<f', struct.unpack_from(e + 'f', data, pos + 8)[0]) if e == '>'
                         else bytes(data[pos + 8:pos + 12]))
        if typ == 3:
            return data[pos + b:pos + b + a * 2].decode(codec) if a else ''
        if typ == 4:
            return bytes(data[pos + b:pos + b + a])
        raise ValueError(f'SGO 节点类型 {typ} @ {pos:#x}')

    version, count, data_off, name_count, name_off = struct.unpack_from(e + '5I', data, 4)
    names: dict[int, str] = {}
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from(e + 'iI', data, p)
        names[idx] = _utf16z(data, p + rel, codec)
    if sorted(names) != list(range(count)):
        raise ValueError('SGO 名表与成员数不符')
    return version, {names[i]: node(data_off + i * 12) for i in range(count)}


def _scalar(buf: bytearray, pos: int, v: Value) -> bool:
    """Writes an int or float node at pos; False for any other value."""
    if isinstance(v, bool):
        raise TypeError('SGO 没有布尔类型')
    if isinstance(v, int):
        struct.pack_into('<iiI', buf, pos, 1, 4, v & 0xFFFFFFFF)
    elif isinstance(v, Float):
        struct.pack_into('<ii', buf, pos, 2, 4)
        buf[pos + 8:pos + 12] = v.raw
    elif isinstance(v, float):
        struct.pack_into('<iif', buf, pos, 2, 4, v)
    else:
        return False
    return True


def _ordinal(s: str) -> list[int]:
    return [ord(c) for c in s]


def write(version: int, members: dict[str, Value]) -> bytes:
    """The SGO for `members` (written in name order, like the stock files), arrays breadth first."""
    order = sorted(members)
    header = 0x20
    buf = bytearray(header + len(order) * 12)
    # Pass 1: lay out arrays breadth first; remember every string/raw node to patch later.
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
            elif isinstance(v, str):
                strings.append((pos, v))
            elif isinstance(v, bytes):
                raws.append((pos, v))
            elif not _scalar(buf, pos, v):
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


def write_depth_first(version: int, members: dict[str, Value]) -> bytes:
    """The SGO for `members` (name order), arrays depth first and one sorted pool of names and strings."""
    order = sorted(members, key=_ordinal)
    out = bytearray(0x20 + len(order) * 12)
    strings: list[tuple[int, str]] = []   # (node offset, text)
    raws: list[tuple[int, bytes]] = []    # (node offset, bytes)

    def emit(pos: int, v: Value) -> None:
        if isinstance(v, list):
            struct.pack_into('<iiI', out, pos, 0, len(v), 0)
        elif isinstance(v, str):
            struct.pack_into('<iiI', out, pos, 3, len(v), 0)
            strings.append((pos, v))
        elif isinstance(v, bytes):
            struct.pack_into('<iiI', out, pos, 4, len(v), 0)
            raws.append((pos, v))
        elif not _scalar(out, pos, v):
            raise TypeError(f'SGO 不支持 {type(v).__name__}')

    def layout(pos: int, v: Value) -> None:
        if not isinstance(v, list):
            return
        block = len(out)
        struct.pack_into('<I', out, pos + 8, block - pos)
        out.extend(bytes(len(v) * 12))
        for i, child in enumerate(v):
            emit(block + i * 12, child)
        for i, child in enumerate(v):
            layout(block + i * 12, child)

    for i, n in enumerate(order):
        emit(0x20 + i * 12, members[n])
    for i, n in enumerate(order):
        layout(0x20 + i * 12, members[n])
    name_off = len(out)
    out += bytes(len(order) * 8)
    raw_off = len(out)
    for pos, blob in raws:
        struct.pack_into('<I', out, pos + 8, len(out) - pos)
        out += blob
    placed: dict[str, int] = {}
    for text in sorted(set(order) | {s for _, s in strings}, key=_ordinal):
        placed[text] = len(out)
        out += text.encode('utf-16le') + bytes(2)
    for i, n in enumerate(order):
        entry = name_off + i * 8
        struct.pack_into('<iI', out, entry, placed[n] - entry, i)
    for pos, text in strings:
        struct.pack_into('<I', out, pos + 8, placed[text] - pos)
    struct.pack_into('<4s7I', out, 0, b'SGO\0', version, len(order), 0x20, len(order), name_off, 0, raw_off)
    return bytes(out)


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


def plain(v: Value) -> object:
    """A value as plain Python: floats rounded to 6 places, raw blocks as 'raw:<hex>'."""
    if isinstance(v, list):
        return [plain(c) for c in v]
    if isinstance(v, Float):
        return round(v.value, 6)
    if isinstance(v, bytes):
        return 'raw:' + v.hex()
    return v


def load(path: str | None = None, data: bytes | None = None) -> object:
    """Top-level value of an SGO or DSGO file as plain Python (a dict of named values for both)."""
    buf = data if data is not None else open(path, 'rb').read()
    if buf[:4] == b'DSGO':
        import dsgo
        return dsgo.to_py(dsgo.parse(buf).root)
    return {k: plain(v) for k, v in read(buf)[1].items()}


def main(argv: list[str]) -> int:
    if argv[:1] == ['roundtrip']:
        data = open(argv[1], 'rb').read()
        version, members = read(data)
        for writer in (write, write_depth_first):
            again = writer(version, members)
            print(writer.__name__, 'identical' if again == data else f'different ({len(again)} vs {len(data)} bytes)')
            if read(again) != (version, members):
                raise SystemExit('parsed values differ after writing')
        return 0
    values = load(argv[0])
    pat = re.compile(argv[1], re.I) if len(argv) > 1 else None
    for k, v in (values.items() if isinstance(values, dict) else enumerate(values)):
        if pat is None or pat.search(str(k)):
            print(k, '=', v)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
