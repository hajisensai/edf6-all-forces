"""Havok 2020.1 tagfile reader (the map collision `collision.hkt` inside <map>.MAD; format notes in
docs/map-collision.md). Read-only: the editors (pylib/hkcms.py) patch the DATA bytes in place, which keeps every
size, item and type entry as it was.

Container: big-endian u32 (top 2 bits: 0 = has children, 1 = leaf; low 30 bits: size including the 8-byte header)
plus a 4CC. TAG0 { SDKV, DATA, TYPE { TST1, TNA1, FST1, TBDY, TPAD }, INDX { ITEM } }.
  * DATA holds every object in its *compact* tag layout (not the native one): a pointer or an array is a u32
    item index, the element count of an array lives in the ITEM entry. TBDY gives each type's compact size and
    member offsets.
  * ITEM: 12 bytes per item, u32 (type index | flags << 24), u32 offset into DATA, u32 count. Item 0 is null.
  * TST1 / FST1: NUL separated type / field names. TNA1: varint count, then per type (from index 1) a name index
    and its template arguments ('t...' = type index, 'v...' = value).
  * TBDY: per type: varint type, parent, optionals; then (by bit) 0x01 format, 0x02 subtype, 0x04 version,
    0x08 size + alignment, 0x10 flags, 0x20 members (count, low 16 bits; per member name, flags, offset, type),
    0x40 interfaces (pairs), 0x80 attribute.
Varints are big-endian with a length prefix in the top bits of the first byte (0xxxxxxx, 10xxxxxx +1,
110xxxxx +2, 11100xxx +3, 11101xxx +4, else +8).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field


def varint(b: bytes, o: int) -> tuple[int, int]:
    """(value, next offset)."""
    c = b[o]
    if c < 0x80:
        return c, o + 1
    if c < 0xC0:
        return ((c & 0x3F) << 8) | b[o + 1], o + 2
    if c < 0xE0:
        return ((c & 0x1F) << 16) | (b[o + 1] << 8) | b[o + 2], o + 3
    if c < 0xE8:
        return ((c & 0x07) << 24) | int.from_bytes(b[o + 1:o + 4], 'big'), o + 4
    if c < 0xF0:
        return ((c & 0x07) << 32) | int.from_bytes(b[o + 1:o + 5], 'big'), o + 5
    return int.from_bytes(b[o + 1:o + 9], 'big'), o + 9


def sections(b: bytes, o: int = 0, end: int | None = None, path: str = '') -> dict[str, tuple[int, int]]:
    """'/TAG0/DATA' -> (payload start, end) for every section."""
    end = len(b) if end is None else end
    out: dict[str, tuple[int, int]] = {}
    while o < end:
        w = struct.unpack_from('>I', b, o)[0]
        size = w & 0x3FFFFFFF
        if size < 8:
            raise ValueError('tagfile section at %#x has size %d' % (o, size))
        p = path + '/' + b[o + 4:o + 8].decode('latin1')
        out[p] = (o + 8, o + size)
        if w >> 30 == 0:
            out.update(sections(b, o + 8, o + size, p))
        o += size
    return out


@dataclass
class Member:
    name: str
    flags: int
    offset: int
    type: int


@dataclass
class Body:
    parent: int
    size: int | None = None
    members: list[Member] = field(default_factory=list)


class Tag:
    """One parsed tagfile. `data0` is the absolute offset of DATA in `b`."""

    def __init__(self, b: bytes) -> None:
        if b[4:8] != b'TAG0':
            raise ValueError('not a Havok tagfile')
        self.b = b
        sec = sections(b)
        for need in ('/TAG0/DATA', '/TAG0/TYPE/TST1', '/TAG0/TYPE/TNA1', '/TAG0/TYPE/FST1', '/TAG0/TYPE/TBDY',
                     '/TAG0/INDX/ITEM'):
            if need not in sec:
                raise ValueError('tagfile without ' + need)
        self.data0 = sec['/TAG0/DATA'][0]
        t0, t1 = sec['/TAG0/TYPE/TST1']
        self.tstr = [s.decode('latin1') for s in b[t0:t1].split(b'\0')]
        f0, f1 = sec['/TAG0/TYPE/FST1']
        self.fstr = [s.decode('latin1') for s in b[f0:f1].split(b'\0')]
        self.types: list[tuple[str, list[tuple[str, int]]]] = [('', [])]
        n0, _ = sec['/TAG0/TYPE/TNA1']
        cnt, o = varint(b, n0)
        for _ in range(1, cnt):
            ni, o = varint(b, o)
            nt, o = varint(b, o)
            args = []
            for _ in range(nt):
                a, o = varint(b, o)
                v, o = varint(b, o)
                args.append((self.tstr[a], v))
            self.types.append((self.tstr[ni], args))
        self.bodies = self._bodies(*sec['/TAG0/TYPE/TBDY'])
        i0, i1 = sec['/TAG0/INDX/ITEM']
        self.items = [struct.unpack_from('<III', b, o) for o in range(i0, i1, 12)]
        self._by_name = {self.tname(i): i for i in range(1, len(self.types))}

    def _bodies(self, o: int, end: int) -> dict[int, Body]:
        b = self.b
        out: dict[int, Body] = {}
        while o < end:
            ti, o = varint(b, o)
            if ti == 0:
                continue
            parent, o = varint(b, o)
            opt, o = varint(b, o)
            body = Body(parent)
            for bit in (1, 2, 4):
                if opt & bit:
                    _, o = varint(b, o)
            if opt & 8:
                body.size, o = varint(b, o)
                _, o = varint(b, o)
            if opt & 0x10:
                _, o = varint(b, o)
            if opt & 0x20:
                n, o = varint(b, o)
                for _ in range(n & 0xFFFF):
                    nm, o = varint(b, o)
                    fl, o = varint(b, o)
                    off, o = varint(b, o)
                    ty, o = varint(b, o)
                    body.members.append(Member(self.fstr[nm], fl, off, ty))
            if opt & 0x40:
                n, o = varint(b, o)
                for _ in range(2 * n):
                    _, o = varint(b, o)
            if opt & 0x80:
                _, o = varint(b, o)
            out[ti] = body
        return out

    def tname(self, t: int) -> str:
        if t <= 0 or t >= len(self.types):
            return '?%d' % t
        n, args = self.types[t]
        if args:
            n += '<' + ','.join(self.tname(v) if k.startswith('t') else str(v) for k, v in args) + '>'
        return n

    def type_index(self, name: str) -> int:
        return self._by_name[name]

    def members(self, t: int) -> dict[str, Member]:
        """Every member of type `t` including its parents', by name."""
        body = self.bodies.get(t)
        if body is None:
            return {}
        out = self.members(body.parent) if body.parent else {}
        out.update({m.name: m for m in body.members})
        return out

    def offset(self, type_name: str, member: str) -> int:
        return self.members(self.type_index(type_name))[member].offset

    def size(self, type_name: str) -> int:
        s = self.bodies[self.type_index(type_name)].size
        if s is None:
            raise ValueError(type_name + ' has no size')
        return s

    # ------------------------------------------------------------------- items

    def item(self, idx: int) -> tuple[str, int, int]:
        """(type name, absolute offset in b, count)."""
        f, o, n = self.items[idx]
        return self.tname(f & 0xFFFFFF), self.data0 + o, n

    def u32(self, at: int) -> int:
        return struct.unpack_from('<I', self.b, at)[0]

    def cstr(self, idx: int) -> str:
        if idx == 0:
            return ''
        _, o, n = self.item(idx)
        return self.b[o:o + n].split(b'\0')[0].decode('utf-8', 'replace')

    def root_variant(self, class_name: str) -> int:
        """Absolute offset of the root container's variant of `class_name`."""
        nv = self.u32(self.item(1)[1])
        _, o, n = self.item(nv)
        for k in range(n):
            at = o + 12 * k
            if self.cstr(self.u32(at + 4)) == class_name:
                return self.item(self.u32(at + 8))[1]
        raise ValueError('no root variant ' + class_name)
