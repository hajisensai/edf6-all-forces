"""Havok 2020.1 binary tagfiles (TAG0: an object's .shkt physics, a map's collision.hkt) as an object graph, read and
written back with the rules of Havok's own writer: an unchanged file is written byte for byte as it was (every stock
EDF6 SHKT and IG_HEIGEN601's collision.hkt: tests/test_hkfile.py), and a changed one is laid out as Havok would have
written it (items numbered and placed in the order the writer finds them, nothing unreachable left behind).

Ported from EDF6 SHKT Editor's EdfHavok.Tagfile (HkTagfile.cs, HkTypeTable.cs, HkType.cs, HkLayout.cs, HkBlock.cs,
HkValue.cs, HkCompare.cs, PackedInt.cs, Section.cs; https://codeberg.org/Ktaro/EDF6_SHKT_Editor v1, 8ade8fd),
Copyright (c) 2026 Ktaro, MIT License (pylib/LICENSE.edf6-shkt-editor). Format notes: pylib/hktag.py, that project's
EDF6_SHKT_Format_Spec section 3.

    read(data) -> Tagfile          every item as a Block, references resolved to Blocks
    Tagfile.write() -> bytes       the file again (Havok's layout rules, below)
    Tagfile.new(type, count)       a zero-filled block of a type of the file's table (Tagfile.types.import_type copies
                                   one from another file's table first)
    Block[i] / Block.value -> Value     typed access: value['member'], value[2] (a tuple element), .target,
                                   .set_target(), .f32() / .set_f32(), .int() / .set_int(), .vec() / .set_vec()
    content_equal(a, b)            structural equality of two blocks (item numbers ignored)
    Physics(tagfile)               an EDF6 physics file's systems, bodies, joints and ragdoll skeleton (views onto
                                   the blocks: what is set on them is written by Tagfile.write)

Layout rules of the writer (HkTagfile.cs):
  * Objects (pointer targets) are numbered and laid out in the order they are found, breadth first from the root.
    Each object is followed by the arrays and strings reachable from it without going through another pointer (its
    "closure"), breadth first.
  * Inside one item references are visited member by member across the elements (every element's first slot, then
    every element's second slot, ...) in chunks of 1024 elements; strings are deferred to the end of that item.
  * Every item is aligned to its type's alignment (zero padding); DATA is padded to 16 bytes.
  * Strings with the same content share one item; empty arrays (and empty strings) are written as null.
"""
from __future__ import annotations

import struct
from typing import Iterator

# ------------------------------------------------------------------------------------------ packed ints, sections


def read_packed(b: bytes | bytearray | memoryview, p: int, tna: bool) -> tuple[int, int]:
    """(value, next offset) of a TNA1 / TBDY packed int (PackedInt.cs: big-endian, length in the first byte's top
    bits; 0xE8 + u32 in TNA1 only)."""
    c = b[p]
    if c < 0x80:
        return c, p + 1
    if c < 0xC0:
        return ((c & 0x3F) << 8) | b[p + 1], p + 2
    if c < 0xE0:
        return ((c & 0x1F) << 16) | (b[p + 1] << 8) | b[p + 2], p + 3
    if tna and c == 0xE8:
        return int.from_bytes(b[p + 1:p + 5], 'big'), p + 5
    return ((c & 0x1F) << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3], p + 4


def write_packed(o: bytearray, v: int, tna: bool) -> None:
    """The shortest form (the game files use it)."""
    if v < 0x80:
        o.append(v)
    elif v < 0x4000:
        o += bytes((0x80 | (v >> 8), v & 0xFF))
    elif v < 0x200000:
        o += bytes((0xC0 | (v >> 16), (v >> 8) & 0xFF, v & 0xFF))
    elif v < (0x8000000 if tna else 0x20000000):
        o += bytes((0xE0 | (v >> 24), (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
    elif tna:
        o.append(0xE8)
        o += v.to_bytes(4, 'big')
    else:
        raise ValueError(f'tagfile: {v:#x} does not fit a TBDY packed int')


def sections(b: bytes | bytearray, start: int, end: int) -> dict[str, tuple[int, int]]:
    """tag -> (header offset, size with the 8-byte header) of the sections between start and end (one level)."""
    out: dict[str, tuple[int, int]] = {}
    p = start
    while p < end:
        h = struct.unpack_from('>I', b, p)[0]
        size = h & 0x3FFFFFFF
        if size < 8 or p + size > end:
            raise ValueError(f'tagfile: broken section header at {p:#x}')
        out[bytes(b[p + 4:p + 8]).decode('latin-1')] = (p, size)
        p += size
    return out


def _begin(o: bytearray, tag: str) -> int:
    at = len(o)
    o += bytes(4) + tag.encode('ascii')
    return at


def _end(o: bytearray, at: int, kind: int, pad: int) -> None:
    """Pads the payload to a multiple of 4 with `pad` and fills in the header (kind 0: has sections, 1: data)."""
    while (len(o) - at) % 4:
        o.append(pad)
    struct.pack_into('>I', o, at, (kind << 30) | (len(o) - at))


# ------------------------------------------------------------------------------------------ types

VOID, OPAQUE, BOOL, STRING, INT, FLOAT, POINTER, RECORD, ARRAY = range(9)    # the low 5 bits of subTypeFlags


class TemplateParam:
    __slots__ = ('name', 'type_value', 'value')

    def __init__(self, name: str, type_value: 'Type | None' = None, value: int = 0) -> None:
        self.name, self.type_value, self.value = name, type_value, value

    @property
    def is_type(self) -> bool:
        return self.name.startswith('t')


class Member:
    __slots__ = ('name', 'flags', 'offset', 'type')

    def __init__(self, name: str, flags: int, offset: int, type_: 'Type') -> None:
        self.name, self.flags, self.offset, self.type = name, flags, offset, type_


class Type:
    """A declaration (name + template arguments, TNA1) and, when the file defines it, its body (TBDY), every field as
    stored (which optional ones are present included) so that the table writes back byte for byte."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.template: list[TemplateParam] = []
        self.has_body = False
        self.parent: Type | None = None
        self.body_flags = 0           # 0x01 subTypeFlags, 0x02 pointee, 0x04 version, 0x08 size / alignment,
        self.subtype_flags = 0        # 0x10 abstract, 0x20 members, 0x40 interfaces
        self.pointee: Type | None = None
        self.version = 0
        self.size = 0
        self.align_raw = 0            # low 12 bits the alignment, the rest flags
        self.abstract = 0
        self.member_count_flags = 0   # the upper 16 bits of the member count
        self.members: list[Member] = []
        self.interfaces: list[tuple[Type | None, int]] = []

    def _first(self, flag: int) -> 'Type | None':
        t: Type | None = self
        while t is not None:
            if t.body_flags & flag:
                return t
            t = t.parent
        return None

    @property
    def eff_subtype(self) -> int:
        t = self._first(0x01)
        return t.subtype_flags if t else 0

    @property
    def kind(self) -> int:
        return self.eff_subtype & 0x1F

    @property
    def is_tuple(self) -> bool:
        return self.kind == ARRAY and bool(self.eff_subtype & 0x20)

    @property
    def tuple_count(self) -> int:
        return self.eff_subtype >> 8

    @property
    def is_signed(self) -> bool:
        return bool(self.eff_subtype & 0x200)

    @property
    def eff_pointee(self) -> 'Type | None':
        t: Type | None = self
        while t is not None:
            if t.body_flags & 0x02 and t.pointee is not None:
                return t.pointee
            t = t.parent
        return None

    @property
    def eff_size(self) -> int:
        t = self._first(0x08)
        return t.size if t else 0

    @property
    def eff_align(self) -> int:
        t = self._first(0x08)
        return max(1, t.align_raw & 0xFFF) if t else 1

    def all_members(self) -> list[Member]:
        chain = []
        t: Type | None = self
        while t is not None:
            chain.append(t)
            t = t.parent
        return [m for t in reversed(chain) for m in t.members]

    def member(self, name: str) -> Member | None:
        t: Type | None = self
        while t is not None:
            for m in t.members:
                if m.name == name:
                    return m
            t = t.parent
        return None

    def derives_from(self, name: str) -> bool:
        t: Type | None = self
        while t is not None:
            if t.name == name:
                return True
            t = t.parent
        return False

    @property
    def full_name(self) -> str:
        """The name with template arguments (hkArray<hknpMaterial,hkContainerHeapAllocator>): a type's identity across
        files."""
        if not self.template:
            return self.name
        args = [(p.type_value.full_name if p.type_value else '-') if p.is_type else str(p.value) for p in self.template]
        return f'{self.name}<{",".join(args)}>'

    def __repr__(self) -> str:
        return f'<Type {self.full_name}>'


def _read_strings(b: bytes) -> list[str]:
    out, p = [], 0
    while p < len(b) and b[p] != 0xFF:
        z = b.index(0, p)
        out.append(b[p:z].decode('utf-8'))
        p = z + 1
    return out


class TypeTable:
    """The TYPE section (TST1 / TNA1 / FST1 / TBDY / TPAD). The order of the types, of the two string tables and of
    the TBDY records is kept as read; types imported from another file are appended."""

    def __init__(self) -> None:
        self.types: list[Type] = []           # TNA1 index of types[i] is i + 1
        self.body_order: list[Type] = []
        self.type_strings: list[str] = []
        self.field_strings: list[str] = []
        self.by_name: dict[str, Type] = {}

    def index_of(self, t: Type | None) -> int:
        return 0 if t is None else self.types.index(t) + 1

    def find(self, full_name: str) -> Type | None:
        return self.by_name.get(full_name)

    def get(self, full_name: str) -> Type:
        t = self.by_name.get(full_name)
        if t is None:
            raise KeyError(f'tagfile: no type {full_name!r} in the type table')
        return t

    def add(self, t: Type) -> Type:
        """A new type at the end of the table (its body, if it has one, last in TBDY)."""
        if t.full_name in self.by_name:
            raise ValueError(f'tagfile: type {t.full_name} is already in the table')
        self._add(t)
        return t

    def _add(self, t: Type) -> None:
        self.types.append(t)
        self.by_name.setdefault(t.full_name, t)
        if t.has_body:
            self.body_order.append(t)

    def import_type(self, foreign: Type) -> Type:
        """The type of this table with `foreign`'s full name, or a copy of it (and of every type it refers to)."""
        return self._clone(foreign, {})

    def _clone(self, f: Type, done: dict[int, Type]) -> Type:
        if id(f) in done:
            return done[id(f)]
        mine = self.by_name.get(f.full_name)
        if mine is not None:
            done[id(f)] = mine
            return mine
        t = Type(f.name)
        for k in ('has_body', 'body_flags', 'subtype_flags', 'version', 'size', 'align_raw', 'abstract',
                  'member_count_flags'):
            setattr(t, k, getattr(f, k))
        done[id(f)] = t
        for p in f.template:          # first: the full name (the identity) depends on them
            t.template.append(TemplateParam(p.name, self._clone(p.type_value, done) if p.type_value else None,
                                            p.value))
        self._add(t)
        t.parent = self._clone(f.parent, done) if f.parent else None
        t.pointee = self._clone(f.pointee, done) if f.pointee else None
        t.members = [Member(m.name, m.flags, m.offset, self._clone(m.type, done)) for m in f.members]
        t.interfaces = [(self._clone(i, done) if i else None, v) for i, v in f.interfaces]
        return t

    @staticmethod
    def read(file: bytes, secs: dict[str, tuple[int, int]]) -> 'TypeTable':
        def payload(tag: str) -> bytes:
            at, size = secs[tag]
            return bytes(file[at + 8:at + size])
        table = TypeTable()
        table.type_strings = _read_strings(payload('TST1'))
        table.field_strings = _read_strings(payload('FST1'))
        tna = payload('TNA1')
        n, p = read_packed(tna, 0, True)
        types: list[Type | None] = [None] * n
        pending: list[tuple[TemplateParam, int]] = []
        for i in range(1, n):
            k, p = read_packed(tna, p, True)
            t = Type(table.type_strings[k])
            m, p = read_packed(tna, p, True)
            for _ in range(m):
                k, p = read_packed(tna, p, True)
                param = TemplateParam(table.type_strings[k])
                v, p = read_packed(tna, p, True)
                if param.is_type:
                    pending.append((param, v))
                else:
                    param.value = v
                t.template.append(param)
            types[i] = t
        for param, v in pending:
            param.type_value = types[v] if v else None

        def T(i: int) -> Type | None:
            return types[i] if i else None
        tb = payload('TBDY')
        p = 0
        while p < len(tb):
            idx, p = read_packed(tb, p, False)
            if idx == 0:
                continue              # trailing padding
            t = types[idx]
            assert t is not None
            t.has_body = True
            table.body_order.append(t)
            v, p = read_packed(tb, p, False)
            t.parent = T(v)
            t.body_flags, p = read_packed(tb, p, False)
            if t.body_flags & ~0x7F:
                raise ValueError(f'tagfile: type {t.name} has body flags {t.body_flags:#x} (not supported)')
            if t.body_flags & 0x01:
                t.subtype_flags, p = read_packed(tb, p, False)
            if t.body_flags & 0x02 and (t.subtype_flags & 0xFF) >= 6:
                v, p = read_packed(tb, p, False)
                t.pointee = T(v)
            if t.body_flags & 0x04:
                t.version, p = read_packed(tb, p, False)
            if t.body_flags & 0x08:
                t.size, p = read_packed(tb, p, False)
                t.align_raw, p = read_packed(tb, p, False)
            if t.body_flags & 0x10:
                t.abstract, p = read_packed(tb, p, False)
            if t.body_flags & 0x20:
                mc, p = read_packed(tb, p, False)
                t.member_count_flags = mc >> 16
                for _ in range(mc & 0xFFFF):
                    k, p = read_packed(tb, p, False)
                    flags, p = read_packed(tb, p, False)
                    if flags & 0x80:
                        raise ValueError(f'tagfile: member {t.name}.{table.field_strings[k]} has flags {flags:#x}')
                    off, p = read_packed(tb, p, False)
                    v, p = read_packed(tb, p, False)
                    mt = T(v)
                    if mt is None:
                        raise ValueError(f'tagfile: member {t.name}.{table.field_strings[k]} has no type')
                    t.members.append(Member(table.field_strings[k], flags, off, mt))
            if t.body_flags & 0x40:
                ic, p = read_packed(tb, p, False)
                for _ in range(ic):
                    a, p = read_packed(tb, p, False)
                    v, p = read_packed(tb, p, False)
                    t.interfaces.append((T(a), v))
        for i in range(1, n):
            t = types[i]
            assert t is not None
            table.types.append(t)
            table.by_name.setdefault(t.full_name, t)
        return table

    def write(self, o: bytearray) -> None:
        """The TYPE section, its header included."""
        def index(strings: list[str]) -> dict[str, int]:
            d: dict[str, int] = {}
            for i, s in enumerate(strings):
                d.setdefault(s, i)
            return d
        ti, fi = index(self.type_strings), index(self.field_strings)

        def intern(strings: list[str], d: dict[str, int], s: str) -> None:
            if s not in d:
                d[s] = len(strings)
                strings.append(s)
        for t in self.types:
            intern(self.type_strings, ti, t.name)
            for p in t.template:
                intern(self.type_strings, ti, p.name)
            for m in t.members:
                intern(self.field_strings, fi, m.name)
        number = {id(t): i + 1 for i, t in enumerate(self.types)}

        def TI(t: Type | None) -> int:
            if t is None:
                return 0
            if id(t) not in number:
                raise ValueError(f'tagfile: type {t.full_name} is referenced but not in the type table')
            return number[id(t)]
        start = _begin(o, 'TYPE')
        s = _begin(o, 'TST1')
        for x in self.type_strings:
            o += x.encode('utf-8') + b'\0'
        _end(o, s, 1, 0xFF)
        s = _begin(o, 'TNA1')
        write_packed(o, len(self.types) + 1, True)
        for t in self.types:
            write_packed(o, ti[t.name], True)
            write_packed(o, len(t.template), True)
            for p in t.template:
                write_packed(o, ti[p.name], True)
                write_packed(o, TI(p.type_value) if p.is_type else p.value, True)
        _end(o, s, 1, 0)
        s = _begin(o, 'FST1')
        for x in self.field_strings:
            o += x.encode('utf-8') + b'\0'
        _end(o, s, 1, 0xFF)
        s = _begin(o, 'TBDY')
        for t in self.body_order:
            for v in (TI(t), TI(t.parent), t.body_flags):
                write_packed(o, v, False)
            if t.body_flags & 0x01:
                write_packed(o, t.subtype_flags, False)
            if t.body_flags & 0x02 and (t.subtype_flags & 0xFF) >= 6:
                write_packed(o, TI(t.pointee), False)
            if t.body_flags & 0x04:
                write_packed(o, t.version, False)
            if t.body_flags & 0x08:
                write_packed(o, t.size, False)
                write_packed(o, t.align_raw, False)
            if t.body_flags & 0x10:
                write_packed(o, t.abstract, False)
            if t.body_flags & 0x20:
                write_packed(o, (t.member_count_flags << 16) | len(t.members), False)
                for m in t.members:
                    for v in (fi[m.name], m.flags, m.offset, TI(m.type)):
                        write_packed(o, v, False)
            if t.body_flags & 0x40:
                write_packed(o, len(t.interfaces), False)
                for it, v in t.interfaces:
                    write_packed(o, TI(it), False)
                    write_packed(o, v, False)
        _end(o, s, 1, 0)
        s = _begin(o, 'TPAD')
        _end(o, s, 1, 0)
        _end(o, start, 0, 0)


# ------------------------------------------------------------------------------------------ blocks and values

REF_POINTER, REF_ARRAY, REF_STRING = 0, 1, 2     # what a reference slot (a u32 item index in DATA) points at
_SLOTS: dict[int, tuple[tuple[int, int], ...]] = {}
_SLOT_TYPES: dict[int, Type] = {}                # keeps the types of _SLOTS alive (ids stay unique)


def ref_slots(t: Type) -> tuple[tuple[int, int], ...]:
    """(offset in one element, REF_*) of every reference slot of `t`, in member order (parents' members first,
    tuples element by element: HkLayout.cs)."""
    got = _SLOTS.get(id(t))
    if got is not None and _SLOT_TYPES[id(t)] is t:
        return got
    out: list[tuple[int, int]] = []

    def collect(ty: Type, off: int) -> None:
        k = ty.kind
        if k == POINTER:
            out.append((off, REF_POINTER))
        elif k == STRING:
            out.append((off, REF_STRING))
        elif k == ARRAY and not ty.is_tuple:
            out.append((off, REF_ARRAY))
        elif k == ARRAY:
            e = ty.eff_pointee
            if e is None:
                raise ValueError(f'tagfile: tuple type {ty.full_name} has no element type')
            size = e.eff_size
            for i in range(ty.tuple_count):
                collect(e, off + i * size)
        elif k == RECORD:
            for m in ty.all_members():
                collect(m.type, off + m.offset)
    collect(t, 0)
    _SLOTS[id(t)] = got = tuple(out)
    _SLOT_TYPES[id(t)] = t
    return got


class Block:
    """One item: an object (a pointer target, count 1) or the elements of an array or a string. `data` holds the
    item's bytes as in the file (padding included); reference slots are held in `refs` (element * slots + slot), the
    indices `data` holds there are rewritten on write."""

    __slots__ = ('type', 'count', 'data', 'refs', 'slots', 'size')

    def __init__(self, type_: Type, count: int, data: bytes | bytearray | None = None) -> None:
        self.type = type_
        self.count = count
        self.size = type_.eff_size
        n = self.size * count
        if data is not None and len(data) != n:
            raise ValueError(f'tagfile: {len(data)} bytes for {count} x {type_.full_name} ({n} bytes)')
        self.data = bytearray(n) if data is None else bytearray(data)
        self.slots = ref_slots(type_)
        self.refs: list[Block | None] = [None] * (len(self.slots) * count)

    def __getitem__(self, element: int) -> 'Value':
        if not 0 <= element < self.count:
            raise IndexError(f'{self!r}: element {element}')
        return Value(self, element * self.size, self.type)

    def __len__(self) -> int:
        return self.count

    def __iter__(self) -> Iterator['Value']:
        return (self[e] for e in range(self.count))

    @property
    def value(self) -> 'Value':
        """The object itself (element 0)."""
        return self[0]

    def text(self) -> str:
        """A char item without its NUL."""
        z = self.data.find(0)
        return self.data[:z if z >= 0 else len(self.data)].decode('utf-8', 'replace')

    def ref_index(self, byte_offset: int) -> int:
        element, inside = divmod(byte_offset, self.size) if self.size else (0, byte_offset)
        for s, (off, _) in enumerate(self.slots):
            if off == inside:
                return element * len(self.slots) + s
        raise ValueError(f'tagfile: offset {byte_offset} of {self.type.full_name} is not a reference slot')

    def resized(self, count: int) -> 'Block':
        """A copy with `count` elements: the first ones (data and references) kept, new ones zero."""
        b = Block(self.type, count)
        n = min(count, self.count)
        b.data[:n * self.size] = self.data[:n * self.size]
        b.refs[:n * len(self.slots)] = self.refs[:n * len(self.slots)]
        return b

    def copy_element(self, src: int, target: 'Block', to: int) -> None:
        """Element `src` (data and references) over element `to` of `target` (a block of the same type)."""
        if target.type is not self.type:
            raise ValueError(f'tagfile: {self.type.full_name} element into {target.type.full_name}')
        n, ns = self.size, len(self.slots)
        target.data[to * n:(to + 1) * n] = self.data[src * n:(src + 1) * n]
        target.refs[to * ns:(to + 1) * ns] = self.refs[src * ns:(src + 1) * ns]

    def copy(self) -> 'Block':
        """A shallow copy (the same reference targets)."""
        b = Block(self.type, self.count, self.data)
        b.refs = list(self.refs)
        return b

    def __repr__(self) -> str:
        return f'<{self.type.full_name}[{self.count}]>'


_F = struct.Struct('<f')


class Value:
    """A typed view of one value inside a block: a whole element, a member or a tuple element (HkValue.cs)."""

    __slots__ = ('block', 'offset', 'type')

    def __init__(self, block: Block, offset: int, type_: Type) -> None:
        self.block, self.offset, self.type = block, offset, type_

    def __getitem__(self, key: str | int) -> 'Value':
        if isinstance(key, str):
            m = self.type.member(key)
            if m is None:
                raise KeyError(f'{self.type.full_name} has no member {key!r}')
            return Value(self.block, self.offset + m.offset, m.type)
        if not self.type.is_tuple:
            raise TypeError(f'{self.type.full_name} is not a fixed-size array')
        if not 0 <= key < self.type.tuple_count:
            raise IndexError(key)
        e = self.type.eff_pointee
        assert e is not None
        return Value(self.block, self.offset + key * e.eff_size, e)

    def has(self, member: str) -> bool:
        return self.type.member(member) is not None

    @property
    def raw(self) -> memoryview:
        return memoryview(self.block.data)[self.offset:self.offset + self.type.eff_size]

    # ---- references

    @property
    def target(self) -> Block | None:
        """What this pointer / array / string slot refers to (None: null)."""
        return self.block.refs[self.block.ref_index(self.offset)]

    def set_target(self, b: Block | None) -> None:
        self.block.refs[self.block.ref_index(self.offset)] = b

    @property
    def count(self) -> int:
        """An array slot's element count (0 when null)."""
        t = self.target
        return t.count if t is not None else 0

    def string(self) -> str:
        t = self.target
        return t.text() if t is not None else ''

    # ---- scalars

    def int(self) -> int:
        n = self.type.eff_size
        if n not in (1, 2, 4, 8):
            raise TypeError(f'{self.type.full_name} is not an integer of 1 / 2 / 4 / 8 bytes')
        return int.from_bytes(self.block.data[self.offset:self.offset + n], 'little', signed=self.type.is_signed)

    def set_int(self, v: int) -> None:
        n = self.type.eff_size
        self.block.data[self.offset:self.offset + n] = (v & ((1 << 8 * n) - 1)).to_bytes(n, 'little')

    def f32(self) -> float:
        """A float32, or an hkHalf16 (the upper 16 bits of a float32, not IEEE half)."""
        n = self.type.eff_size
        if n == 4:
            return _F.unpack_from(self.block.data, self.offset)[0]
        if n == 2:
            return struct.unpack('<f', struct.pack('<I', struct.unpack_from('<H', self.block.data, self.offset)[0] << 16))[0]
        raise TypeError(f'{self.type.full_name} is not a float32 or an hkHalf16')

    def set_f32(self, v: float) -> None:
        """For an hkHalf16 truncated to the upper 16 bits, as Havok does."""
        n = self.type.eff_size
        if n == 4:
            _F.pack_into(self.block.data, self.offset, v)
        elif n == 2:
            struct.pack_into('<H', self.block.data, self.offset, struct.unpack('<I', struct.pack('<f', v))[0] >> 16)
        else:
            raise TypeError(f'{self.type.full_name} is not a float32 or an hkHalf16')

    def vec(self, n: int | None = None) -> tuple[float, ...]:
        """The first n (default: every) float32 of a float tuple (hkVector4, hkQuaternion, hkFloat3, ...)."""
        n = self.type.eff_size // 4 if n is None else n
        return struct.unpack_from(f'<{n}f', self.block.data, self.offset)

    def set_vec(self, v) -> None:   # noqa: ANN001 - floats
        v = list(v)
        if 4 * len(v) > self.type.eff_size:
            raise ValueError(f'{len(v)} floats into {self.type.full_name}')
        struct.pack_into(f'<{len(v)}f', self.block.data, self.offset, *v)

    def __repr__(self) -> str:
        return f'<{self.type.full_name} @{self.block!r}+{self.offset}>'


# ------------------------------------------------------------------------------------------ the file

class Tagfile:
    def __init__(self, types: TypeTable, root: Block, sdk: bytes = b'20200100',
                 items: list[Block | None] | None = None) -> None:
        self.types, self.root, self.sdk = types, root, sdk
        self.items: list[Block | None] = items if items is not None else [None, root]   # as read, by item number

    # ---- making blocks

    def new(self, type_: str | Type, count: int = 1, data: bytes | bytearray | None = None) -> Block:
        """A block of a type of this file's table (a Type of another file is imported first)."""
        t = self.types.get(type_) if isinstance(type_, str) else self.types.import_type(type_)
        return Block(t, count, data)

    def string(self, text: str) -> Block:
        b = text.encode('utf-8') + b'\0'
        return Block(self.types.get('char'), len(b), b)

    def blocks(self) -> list[Block]:
        """Every block reachable from the root, once each."""
        seen = {id(self.root)}
        out, todo = [], [self.root]
        while todo:
            b = todo.pop()
            out.append(b)
            for r in b.refs:
                if r is not None and id(r) not in seen:
                    seen.add(id(r))
                    todo.append(r)
        return out

    # ---- reading

    @staticmethod
    def read(file: bytes) -> 'Tagfile':
        file = bytes(file)
        if len(file) < 8 or file[4:8] != b'TAG0':
            raise ValueError('not a Havok 2020 tagfile (TAG0)')
        top = sections(file, 0, len(file))
        if top.get('TAG0', (1, 0))[0] != 0:
            raise ValueError('not a Havok 2020 tagfile (TAG0)')
        sub = sections(file, 8, top['TAG0'][1])
        sdk = file[sub['SDKV'][0] + 8:sub['SDKV'][0] + sub['SDKV'][1]]
        types = TypeTable.read(file, sections(file, sub['TYPE'][0] + 8, sum(sub['TYPE'])))
        indx = sections(file, sub['INDX'][0] + 8, sum(sub['INDX']))
        item_at, item_size = indx['ITEM']
        data0 = sub['DATA'][0] + 8
        n = (item_size - 8) // 12
        blocks: list[Block | None] = [None] * n
        for i in range(1, n):
            flags, off, count = struct.unpack_from('<IiI', file, item_at + 8 + 12 * i)
            t = types.types[(flags & 0xFFFFFF) - 1]
            size = t.eff_size * count
            blocks[i] = Block(t, count, file[data0 + off:data0 + off + size])
        for i in range(1, n):
            b = blocks[i]
            assert b is not None
            if not b.slots:
                continue
            ns, d, size = len(b.slots), b.data, b.size
            for e in range(b.count):
                for s, (off, _) in enumerate(b.slots):
                    idx = struct.unpack_from('<I', d, e * size + off)[0]
                    if idx >= n:
                        raise ValueError(f'tagfile: item {i} refers to item {idx} of {n}')
                    b.refs[e * ns + s] = blocks[idx] if idx else None
        if n < 2 or blocks[1] is None:
            raise ValueError('tagfile: no root object')
        return Tagfile(types, blocks[1], sdk, blocks)

    # ---- writing

    CHUNK = 1024          # Havok's writer visits a large array's elements in chunks of this many

    def write(self) -> bytes:
        index: dict[int, int] = {}
        order: list[Block | None] = [None]
        is_object = [False]
        strings: dict[bytes, Block] = {}
        groups: list[list[Block]] = []

        def canonical(b: Block | None, kind: int) -> Block | None:
            if b is None or b.count == 0:
                return None
            if kind != REF_STRING:
                return b
            return strings.setdefault(bytes(b.data), b)     # strings are shared by content

        def assign(b: Block, obj: bool) -> bool:
            if id(b) in index:
                return False
            index[id(b)] = len(order)
            order.append(b)
            is_object.append(obj)
            return True

        objects = [self.root]

        def visit(b: Block, lo: int, hi: int, want_strings: bool, closure: list[Block]) -> None:
            ns = len(b.slots)
            refs = b.refs
            for s, (_, kind) in enumerate(b.slots):
                if (kind == REF_STRING) != want_strings:
                    continue
                for e in range(lo, hi):
                    t = canonical(refs[e * ns + s], kind)
                    if t is None:
                        continue
                    obj = kind == REF_POINTER
                    if assign(t, obj):
                        (objects if obj else closure).append(t)

        assign(self.root, True)
        oi = 0
        while oi < len(objects):
            closure = [objects[oi]]
            ci = 0
            while ci < len(closure):
                b = closure[ci]
                if b.slots:
                    for c0 in range(0, b.count, self.CHUNK):
                        visit(b, c0, min(b.count, c0 + self.CHUNK), False, closure)
                    visit(b, 0, b.count, True, closure)
                ci += 1
            groups.append(closure)
            oi += 1
        number = {id(t): i + 1 for i, t in enumerate(self.types.types)}
        for b in order[1:]:
            assert b is not None
            if id(b.type) not in number:
                raise ValueError(f'tagfile: type {b.type.full_name} is not in this file\'s type table '
                                 '(make blocks with Tagfile.new)')
        offsets = [0] * len(order)
        cursor = 0
        for g in groups:
            for b in g:
                a = b.type.eff_align
                cursor = (cursor + a - 1) // a * a
                offsets[index[id(b)]] = cursor
                cursor += len(b.data)
        data = bytearray((cursor + 15) & ~15)
        for i in range(1, len(order)):
            b = order[i]
            assert b is not None
            at = offsets[i]
            data[at:at + len(b.data)] = b.data
            ns, size = len(b.slots), b.size
            for e in range(b.count):
                for s, (off, kind) in enumerate(b.slots):
                    t = canonical(b.refs[e * ns + s], kind)
                    struct.pack_into('<I', data, at + e * size + off, index[id(t)] if t is not None else 0)
        o = bytearray()
        tag0 = _begin(o, 'TAG0')
        s = _begin(o, 'SDKV')
        o += self.sdk
        _end(o, s, 1, 0)
        s = _begin(o, 'DATA')
        o += data
        _end(o, s, 1, 0)
        self.types.write(o)
        indx = _begin(o, 'INDX')
        s = _begin(o, 'ITEM')
        o += bytes(12)
        for i in range(1, len(order)):
            b = order[i]
            assert b is not None
            o += struct.pack('<IiI', number[id(b.type)] | (0x10000000 if is_object[i] else 0x20000000), offsets[i],
                             b.count)
        _end(o, s, 1, 0)
        _end(o, indx, 0, 0)
        _end(o, tag0, 0, 0)
        return bytes(o)

    # ---- the parts every EDF6 file has

    def root_variant(self, class_name: str) -> Block:
        """The object the root container (hkRootLevelContainer) names with `class_name`."""
        nv = self.root.value['namedVariants'].target
        if nv is not None:
            for v in nv:
                if v['className'].string() == class_name:
                    t = v['variant'].target
                    if t is not None:
                        return t
        raise KeyError(f'tagfile: no root variant {class_name}')


def read(data: bytes) -> Tagfile:
    return Tagfile.read(data)


def content_equal(a: Block | None, b: Block | None) -> bool:
    """The two blocks hold the same type, count and bytes, and their references lead to equal content (item numbers
    ignored; a shared object compared once: HkCompare.cs)."""
    seen: set[tuple[int, int]] = set()
    todo = [(a, b)]
    while todo:
        x, y = todo.pop()
        if x is None or y is None:
            if x is not y:
                return False
            continue
        if x.type.full_name != y.type.full_name or x.count != y.count:
            return False
        if (id(x), id(y)) in seen:
            continue
        seen.add((id(x), id(y)))
        mask = bytearray(x.data)
        other = bytearray(y.data)
        for e in range(x.count):
            for off, _ in x.slots:
                p = e * x.size + off
                mask[p:p + 4] = other[p:p + 4] = bytes(4)
        if mask != other:
            return False
        todo.extend(zip(x.refs, y.refs))
    return True


# ------------------------------------------------------------------------------------------ EDF6 physics files

BODY = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
TRANSFORM_ATOMS = ('transforms', 'suspensionBase')   # the hkpSetLocalTransformsConstraintAtom of each joint type


class Body:
    """One body cinfo (an element of a system's bodyCinfos)."""

    def __init__(self, system: 'System', index: int, value: Value) -> None:
        self.system, self.index, self.value = system, index, value

    name = property(lambda self: self.value['name'].string())
    shape = property(lambda self: self.value['shape'].target)
    mass = property(lambda self: self.value['mass'].f32())
    filter_info = property(lambda self: self.value['collisionFilterInfo'].int())
    material_id = property(lambda self: self.value['materialId'].int())
    motion_properties_id = property(lambda self: self.value['motionPropertiesId'].int())
    motion_type = property(lambda self: self.value['motionType'].int())
    mass_distribution = property(lambda self: self.value['massDistribution'].target)

    @property
    def position(self) -> tuple[float, float, float]:
        return self.value['position'].vec(3)          # type: ignore[return-value]

    @property
    def orientation(self) -> tuple[float, float, float, float]:
        return self.value['orientation'].vec(4)       # type: ignore[return-value]

    def set_position(self, p) -> None:   # noqa: ANN001 - 3 floats
        self.value['position'].set_vec(list(p)[:3])

    def set_shape(self, b: Block | None) -> None:
        self.value['shape'].set_target(b)

    def __repr__(self) -> str:
        return f'<body {self.index} {self.name!r}>'


class Joint:
    """One constraint cinfo: bodies A (the child) and B (the parent) by index, its constraint data, and the
    hkpSetLocalTransformsConstraintAtom (the joint in A's and in B's frame) when its type has one."""

    def __init__(self, index: int, value: Value) -> None:
        self.index, self.value = index, value
        self.data = value['constraintData'].target
        self.transforms: Value | None = None
        if self.data is not None and self.data.value.has('atoms'):
            atoms = self.data.value['atoms']
            for name in TRANSFORM_ATOMS:
                if atoms.has(name) and atoms[name].type.derives_from('hkpSetLocalTransformsConstraintAtom'):
                    self.transforms = atoms[name]
                    break

    kind = property(lambda self: self.data.type.full_name if self.data is not None else '')
    body_a = property(lambda self: self.value['bodyA'].int())
    body_b = property(lambda self: self.value['bodyB'].int())

    def pivot(self, side: str) -> tuple[float, float, float] | None:
        """transformA's / transformB's translation (side 'A' / 'B'); None without local transforms."""
        if self.transforms is None:
            return None
        return self.transforms['transform' + side]['translation'].vec(3)    # type: ignore[return-value]

    def set_pivot(self, side: str, p) -> None:   # noqa: ANN001 - 3 floats
        assert self.transforms is not None
        self.transforms['transform' + side]['translation'].set_vec(list(p)[:3])


class System:
    """A physics system (hknpPhysicsSystemData, or hknpRagdollData with its skeleton)."""

    def __init__(self, index: int, block: Block) -> None:
        self.index, self.block = index, block
        v = block.value
        cinfos = v['bodyCinfos'].target
        self.bodies = [Body(self, j, cinfos[j]) for j in range(cinfos.count)] if cinfos is not None else []
        joints = v['constraintCinfos'].target
        self.joints = [Joint(j, joints[j]) for j in range(joints.count)] if joints is not None else []
        self.materials = v['materials'].count
        self.motion_properties = v['motionProperties'].count
        self.skeleton = v['skeleton'].target if v.has('skeleton') else None

    kind = property(lambda self: self.block.type.full_name)

    def body(self, name: str) -> Body:
        for b in self.bodies:
            if b.name == name:
                return b
        raise KeyError(f'no body {name!r}')

    # ---- the ragdoll skeleton (hknpRagdollData)

    def parents(self) -> list[int]:
        a = self.skeleton.value['parentIndices'].target if self.skeleton is not None else None
        return [a[k].int() for k in range(a.count)] if a is not None else []

    def bone_bodies(self) -> list[int]:
        a = self.block.value['boneToBodyMap'].target if self.block.value.has('boneToBodyMap') else None
        return [a[k].int() for k in range(a.count)] if a is not None else []

    def reference_pose(self) -> list[Value]:
        """Each skeleton bone's reference pose (hkQsTransform: translation, rotation, scale)."""
        a = self.skeleton.value['referencePose'].target if self.skeleton is not None else None
        return list(a) if a is not None else []


class Physics:
    """The systems of an EDF6 physics file (the root's hknpPhysicsSceneData)."""

    def __init__(self, tf: Tagfile) -> None:
        self.tf = tf
        self.scene = tf.root_variant('hknpPhysicsSceneData')
        systems = self.scene.value['systemDatas'].target
        self.systems = [System(k, systems[k].target) for k in range(systems.count)] if systems is not None else []

    @staticmethod
    def read(data: bytes) -> 'Physics':
        return Physics(Tagfile.read(data))

    def bodies(self) -> list[Body]:
        return [b for s in self.systems for b in s.bodies]

    def body(self, name: str) -> Body:
        for b in self.bodies():
            if b.name == name:
                return b
        raise KeyError(f'no body {name!r}')

    def write(self) -> bytes:
        return self.tf.write()


def clone(root: Block) -> Block:
    """A deep copy of `root` and of everything it reaches (what is shared stays shared within the copy)."""
    done: dict[int, Block] = {}
    todo = [root]
    while todo:
        b = todo.pop()
        if id(b) in done:
            continue
        done[id(b)] = b.copy()
        todo.extend(r for r in b.refs if r is not None and id(r) not in done)
    for c in done.values():
        c.refs = [done[id(r)] if r is not None else None for r in c.refs]
    return done[id(root)]


def changed_blocks(a: Block, b: Block) -> list[tuple[Block, Block, list[int]]] | None:
    """Walking two graphs of the same structure (types, counts and references alike) in step: (block of a, block of
    b, the byte offsets that differ outside reference slots) of every pair whose bytes differ; None when the
    structures differ."""
    seen: set[tuple[int, int]] = set()
    out = []
    todo: list[tuple[Block | None, Block | None]] = [(a, b)]
    while todo:
        x, y = todo.pop()
        if x is None or y is None:
            if x is not y:
                return None
            continue
        if (id(x), id(y)) in seen:
            continue
        seen.add((id(x), id(y)))
        if x.type.full_name != y.type.full_name or x.count != y.count:
            return None
        slot = set()
        for e in range(x.count):
            for off, _ in x.slots:
                slot.update(range(e * x.size + off, e * x.size + off + 4))
        diff = [i for i in range(len(x.data)) if x.data[i] != y.data[i] and i not in slot] if x.data != y.data else []
        if diff:
            out.append((x, y, diff))
        todo.extend(zip(x.refs, y.refs))
    return out
