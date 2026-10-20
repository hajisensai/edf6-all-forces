"""A Havok 2020.1 tagfile seen at byte offsets: the map collision editors (pylib/hkcms.py) patch the DATA bytes of a
`collision.hkt` in place, which keeps every size, item and type entry as it was (format notes in docs/map-collision.md).
The type table is pylib/hkfile.py's (one TBDY parser); hkfile is also what reads and writes a tagfile as a graph.

Container: big-endian u32 (top 2 bits: 0 = has children, 1 = leaf; low 30 bits: size including the 8-byte header)
plus a 4CC. TAG0 { SDKV, DATA, TYPE { TST1, TNA1, FST1, TBDY, TPAD }, INDX { ITEM } }.
  * DATA holds every object in its *compact* tag layout (not the native one): a pointer or an array is a u32
    item index, the element count of an array lives in the ITEM entry. TBDY gives each type's compact size and
    member offsets.
  * ITEM: 12 bytes per item, u32 (type index | flags << 24), u32 offset into DATA, u32 count. Item 0 is null.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

import hkfile


def sections(b: bytes, o: int = 0, end: int | None = None, path: str = '') -> dict[str, tuple[int, int]]:
    """'/TAG0/DATA' -> (payload start, end) for every section."""
    end = len(b) if end is None else end
    out: dict[str, tuple[int, int]] = {}
    for tag, (at, size) in hkfile.sections(b, o, end).items():
        p = path + '/' + tag
        out[p] = (at + 8, at + size)
        if struct.unpack_from('>I', b, at)[0] >> 30 == 0:
            out.update(sections(b, at + 8, at + size, p))
    return out


@dataclass
class Member:
    name: str
    flags: int
    offset: int
    type: int        # type index


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
        self.table = hkfile.TypeTable.read(b, hkfile.sections(b, *sec['/TAG0/TYPE']))
        self._number = {id(t): i + 1 for i, t in enumerate(self.table.types)}
        i0, i1 = sec['/TAG0/INDX/ITEM']
        self.items = [struct.unpack_from('<III', b, o) for o in range(i0, i1, 12)]
        self._by_name = {t.full_name: i + 1 for i, t in enumerate(self.table.types)}
        self._members: dict[int, dict[str, Member]] = {}

    def _type(self, t: int) -> hkfile.Type | None:
        return self.table.types[t - 1] if 0 < t <= len(self.table.types) else None

    def tname(self, t: int) -> str:
        ty = self._type(t)
        return ty.full_name if ty is not None else '?%d' % t

    def type_index(self, name: str) -> int:
        return self._by_name[name]

    def members(self, t: int) -> dict[str, Member]:
        """Every member of type `t` including its parents', by name."""
        if t not in self._members:
            ty = self._type(t)
            self._members[t] = {} if ty is None else {
                m.name: Member(m.name, m.flags, m.offset, self._number[id(m.type)]) for m in ty.all_members()}
        return self._members[t]

    def offset(self, type_name: str, member: str) -> int:
        return self.members(self.type_index(type_name))[member].offset

    def size(self, type_name: str) -> int:
        s = self.table.types[self.type_index(type_name) - 1].eff_size
        if not s:
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
