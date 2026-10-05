"""EDF6 map archive (.MAC) tool: decode/re-pack MARC, append offset copies of map pieces.

Usage:
    python bigmap.py IN.MAC OUT.MAC  SRC:DX:DY:DZ [SRC:DX:DY:DZ ...]
    python bigmap.py IN.MAC --info
    python bigmap.py IN.MAC --roundtrip

Every offset rule used here was read from EDF.dll (TimeDateStamp 0x678CCB46): the
endian-fixup routines 0x11FA40 (MAPB) / 0x11FE90 (MAPO) enumerate every field, the
MARC directory walk is at 0x1186E8, the record loop is 0x11E1E0 / 0x11A3F0.
See docs/map-format.md.
"""
from __future__ import annotations

import struct
import sys
import uuid
from dataclasses import dataclass, field

ALIGN = 0x10
MARC_HDR = 0x24


def _align(n: int, a: int = ALIGN) -> int:
    return (n + a - 1) & ~(a - 1)


def _i32(b: bytes | bytearray, o: int) -> int:
    return struct.unpack_from('<i', b, o)[0]


def _put(b: bytearray, o: int, v: int) -> None:
    struct.pack_into('<i', b, o, v)


def _wstr(b: bytes | bytearray, o: int) -> str:
    e = o
    while b[e:e + 2] != b'\0\0':
        e += 2
    return bytes(b[o:e]).decode('utf-16le')


# ----------------------------------------------------------------------------- MARC

@dataclass
class Marc:
    version: int
    files: list[tuple[str, bytes]] = field(default_factory=list)   # directory order

    @classmethod
    def parse(cls, d: bytes) -> 'Marc':
        if d[:4] != b'MARC':
            raise ValueError('not a MARC archive')
        (ver, data_off, data_len, size_sum, dir_rel, count,
         str_off, str_wchars) = struct.unpack_from('<8i', d, 4)
        if data_off != MARC_HDR:
            raise ValueError('unexpected data offset %#x' % data_off)
        dir_off = 0x14 + dir_rel            # 0x1186F0: lea rax,[hdr+0x14]; add *rax
        files = []
        for i in range(count):
            e = dir_off + i * 0x18
            name_rel, off, size, z0, z1, z2 = struct.unpack_from('<6i', d, e)
            if z0 or z1 or z2:
                raise ValueError('directory entry %d has nonzero reserved words' % i)
            files.append((_wstr(d, e + name_rel), bytes(d[off:off + size])))
        return cls(ver, files)

    def _layout(self):
        pos = MARC_HDR
        offs = []
        for i, (_, data) in enumerate(self.files):
            if i:
                pos = _align(pos)
            offs.append(pos)
            pos += len(data)
        str_off = _align(pos)
        names = sorted({n for n, _ in self.files})
        table = bytearray()
        name_at = {}
        for n in names:
            name_at[n] = str_off + len(table)
            table += n.encode('utf-16le') + b'\0\0'
        dir_off = _align(str_off + len(table))
        return offs, str_off, bytes(table), name_at, dir_off

    def build(self) -> bytes:
        offs, str_off, table, name_at, dir_off = self._layout()
        out = bytearray(dir_off + 0x18 * len(self.files))
        struct.pack_into('<4s8i', out, 0, b'MARC', self.version, MARC_HDR,
                         str_off - MARC_HDR, sum(len(d) for _, d in self.files),
                         dir_off - 0x14, len(self.files), str_off, len(table) // 2)
        for (name, data), off in zip(self.files, offs):
            out[off:off + len(data)] = data
        out[str_off:str_off + len(table)] = table
        for i, ((name, data), off) in enumerate(zip(self.files, offs)):
            e = dir_off + i * 0x18
            struct.pack_into('<6i', out, e, name_at[name] - e, off, len(data), 0, 0, 0)
        return bytes(out)

    def get(self, name: str) -> bytes:
        for n, d in self.files:
            if n == name:
                return d
        raise KeyError(name)

    def put(self, name: str, data: bytes) -> None:
        for i, (n, _) in enumerate(self.files):
            if n == name:
                self.files[i] = (n, data)
                return
        raise KeyError(name)


# ----------------------------------------------------------------------------- MAPB

REC = 0x48
DEF = 0x3C


@dataclass
class MapB:
    raw: bytes

    def __post_init__(self):
        b = self.raw
        if b[:4] != b'MAPB' or _i32(b, 4) != 2:
            raise ValueError('not MAPB v2')
        self.def_off, self.def_n = _i32(b, 0x18), _i32(b, 0x1C)
        self.rec_off, self.rec_n = _i32(b, 0x20), _i32(b, 0x24)

    def record(self, i: int) -> bytes:
        o = self.rec_off + i * REC
        return self.raw[o:o + REC]

    def rec_abs(self, i: int, field_rel: int) -> int | None:
        """Absolute (mapb-relative) target of a record-base-relative offset field (+00 def, +2C list)."""
        o = self.rec_off + i * REC
        v = _i32(self.raw, o + field_rel)
        return None if v == 0 else o + v

    def def_index(self, i: int) -> int:
        a = self.rec_abs(i, 0)
        k, r = divmod(a - self.def_off, DEF)
        if r or not 0 <= k < self.def_n:
            raise ValueError('record %d def offset does not hit the def table' % i)
        return k

    def def_name(self, k: int) -> str:
        o = self.def_off + k * DEF
        return _wstr(self.raw, o + 0xC + _i32(self.raw, o + 0xC))

    def def_type(self, k: int) -> int:
        return _i32(self.raw, self.def_off + k * DEF)

    def pos(self, i: int) -> tuple[float, float, float]:
        return struct.unpack_from('<3f', self.raw, self.rec_off + i * REC + 4)

    def guid(self, i: int) -> bytes:
        o = self.rec_off + i * REC
        return self.raw[o + 0x34:o + 0x44]


def _rebase_record(rec: bytes, old_base: int, new_base: int) -> bytearray:
    """Re-point the record-base-relative offsets (+00 def, +2C int list) after moving the record."""
    r = bytearray(rec)
    for f in (0x00, 0x2C):
        v = _i32(r, f)
        if v:
            _put(r, f, old_base + v - new_base)
    return r


# ----------------------------------------------------------------------------- MAPO

ENT = 0x1C      # per-record entry, array at hdr+0x08, count hdr+0x0C
OBJ = 0x0C      # per-record named entry, array at hdr+0x50, count hdr+0x54
ENT_OFFSET_FIELDS = (0x00, 0x04, 0x10, 0x14, 0x18)   # all relative to the entry base
OBJ_OFFSET_FIELDS = (0x00, 0x04)               # +0 name string, +4 sub-list; relative to entry base


@dataclass
class MapO:
    raw: bytes

    def __post_init__(self):
        b = self.raw
        if b[:4] != b'MAPO':
            raise ValueError('not MAPO')
        self.ent_off, self.ent_n = _i32(b, 0x08), _i32(b, 0x0C)
        self.pool_off, self.pool_n = _i32(b, 0x48), _i32(b, 0x4C)
        self.obj_off, self.obj_n = _i32(b, 0x50), _i32(b, 0x54)

    def ent(self, i: int) -> bytes:
        o = self.ent_off + i * ENT
        return self.raw[o:o + ENT]

    def obj(self, i: int) -> bytes:
        o = self.obj_off + i * OBJ
        return self.raw[o:o + OBJ]

    def obj_name(self, i: int) -> str:
        o = self.obj_off + i * OBJ
        v = _i32(self.raw, o)
        return '' if v == 0 else _wstr(self.raw, o + v)

    def ent_str(self, i: int, f: int) -> str:
        o = self.ent_off + i * ENT
        v = _i32(self.raw, o + f)
        return '' if v == 0 else _wstr(self.raw, o + v)


def _rebase(entry: bytes, fields, old_base: int, new_base: int) -> bytearray:
    r = bytearray(entry)
    for f in fields:
        v = _i32(r, f)
        if v:
            _put(r, f, old_base + v - new_base)
    return r


# ----------------------------------------------------------------------------- make_tiled

Copy = tuple[int, float, float, float]


def _new_guid(src: bytes, idx: int, d: tuple[float, float, float], taken: set[bytes]) -> bytes:
    salt = 0
    while True:
        g = uuid.uuid5(uuid.NAMESPACE_OID, 'edf6-bigmap:%s:%d:%r:%d' % (src.hex(), idx, d, salt)).bytes
        if g not in taken:
            taken.add(g)
            return g
        salt += 1


def children(mb: MapB, i: int) -> list[int]:
    """Record +2C: base-relative offset to {int32 off (from this struct), int32 count} -> int32 record indices."""
    a = mb.rec_abs(i, 0x2C)
    if a is None:
        return []
    off, cnt = struct.unpack_from('<ii', mb.raw, a)
    return list(struct.unpack_from('<%di' % cnt, mb.raw, a + off))


def expand_children(mb: MapB, copies: list[Copy]) -> list[Copy]:
    """Add (child, same offset) for every +2C child of every copied record, recursively, no duplicates."""
    out: list[Copy] = []
    seen: set[Copy] = set()
    todo = list(copies)
    while todo:
        c = todo.pop(0)
        if c in seen:
            continue
        seen.add(c)
        out.append(c)
        todo.extend((ch, c[1], c[2], c[3]) for ch in children(mb, c[0]))
    return out


def make_tiled(mac_bytes: bytes, copies: list[Copy], include_children: bool = True,
               warnings: list[str] | None = None) -> bytes:
    """Append one placement record per (source_record_index, dx, dy, dz).

    Each copy reuses the source's def (same model, same collision system name), gets
    position+offset, a fresh GUID, and its own MAPO entry (+08 array, 0x1C) and named
    entry (+50 array, 0xC, name = decimal record index) cloned from the source; whatever
    those entries point at (strings, 0x3C object structs, sub-lists) is shared with the
    source. +2C child lists are remapped to the copied children (include_children adds them).
    """
    warn = warnings if warnings is not None else []
    marc = Marc.parse(mac_bytes)
    mb = MapB(marc.get('map.mapb'))
    mo = MapO(marc.get('map.mapo'))
    n = mb.rec_n
    if mo.ent_n != n or mo.obj_n != n:
        raise ValueError('mapo per-record tables (%d,%d) != record count %d' % (mo.ent_n, mo.obj_n, n))
    for c in copies:
        if not 0 <= c[0] < n:
            raise IndexError('source record %d out of range' % c[0])
    if include_children:
        copies = expand_children(mb, copies)
    k = len(copies)
    new_index = {c: n + j for j, c in enumerate(copies)}

    # ---- MAPB: move the record array to the end, append the copies, then new child lists.
    b = bytearray(mb.raw)
    new_rec = _align(len(b))
    b += bytes(new_rec - len(b))
    taken = {mb.guid(i) for i in range(n)}
    recs = bytearray()
    for i in range(n):
        recs += _rebase_record(mb.record(i), mb.rec_off + i * REC, new_rec + i * REC)
    lists: list[tuple[int, list[int]]] = []
    for j, (src, dx, dy, dz) in enumerate(copies):
        idx = n + j
        r = _rebase_record(mb.record(src), mb.rec_off + src * REC, new_rec + idx * REC)
        x, y, z = struct.unpack_from('<3f', r, 4)
        struct.pack_into('<3f', r, 4, x + dx, y + dy, z + dz)
        r[0x34:0x44] = _new_guid(mb.guid(src), idx, (dx, dy, dz), taken)
        kids = children(mb, src)
        if kids:
            mapped = []
            for ch in kids:
                t = new_index.get((ch, dx, dy, dz))
                if t is None:
                    warn.append('copy of %d: child %d not copied, copy still links the original' % (src, ch))
                    t = ch
                mapped.append(t)
            lists.append((idx, mapped))
        recs += r
    b += recs
    for idx, mapped in lists:
        at = _align(len(b), 4)
        b += bytes(at - len(b))
        b += struct.pack('<ii', 8, len(mapped)) + struct.pack('<%di' % len(mapped), *mapped)
        _put(b, new_rec + idx * REC + 0x2C, at - (new_rec + idx * REC))
    _put(b, 0x20, new_rec)
    _put(b, 0x24, n + k)

    # ---- MAPO: new name strings (extending the string pool, which is the last block),
    #      then the two per-record arrays rebuilt at the end.
    o = bytearray(mo.raw)
    pool_end = mo.pool_off + 2 * mo.pool_n
    o += bytes(_align(len(o), 2) - len(o))
    extends_pool = pool_end == len(o)
    name_at = {}
    for j in range(k):
        name_at[n + j] = len(o)
        o += str(n + j).encode('utf-16le') + b'\0\0'
    if extends_pool:
        _put(o, 0x4C, (len(o) - mo.pool_off) // 2)
    else:
        warn.append('mapo string pool is not the last block; new names placed outside it')
    new_ent = _align(len(o))
    o += bytes(new_ent - len(o))
    for i in range(n):
        o += _rebase(mo.ent(i), ENT_OFFSET_FIELDS, mo.ent_off + i * ENT, new_ent + i * ENT)
    for j, (src, *_d) in enumerate(copies):
        o += _rebase(mo.ent(src), ENT_OFFSET_FIELDS, mo.ent_off + src * ENT, new_ent + (n + j) * ENT)
    new_obj = _align(len(o))
    o += bytes(new_obj - len(o))
    for i in range(n):
        o += _rebase(mo.obj(i), OBJ_OFFSET_FIELDS, mo.obj_off + i * OBJ, new_obj + i * OBJ)
    for j, (src, *_d) in enumerate(copies):
        base = new_obj + (n + j) * OBJ
        e = _rebase(mo.obj(src), OBJ_OFFSET_FIELDS, mo.obj_off + src * OBJ, base)
        _put(e, 0, name_at[n + j] - base)
        o += e
    _put(o, 0x08, new_ent)
    _put(o, 0x0C, n + k)
    _put(o, 0x50, new_obj)
    _put(o, 0x54, n + k)

    marc.put('map.mapb', bytes(b))
    marc.put('map.mapo', bytes(o))
    return marc.build()


# ----------------------------------------------------------------------------- verification

def verify(mac_bytes: bytes, orig: bytes | None = None) -> list[str]:
    """Re-parse and check every cross reference the loader follows. Returns problems (empty = ok).

    With orig, also checks that the first N records/entries resolve to exactly the same
    defs, positions, strings and sub-structures as in the original, and that new records'
    GUIDs are unique (shipped maps themselves contain duplicate GUIDs, e.g. NW_DLCMAP601).
    """
    p: list[str] = []
    marc = Marc.parse(mac_bytes)
    if marc.build() != mac_bytes:
        p.append('MARC re-pack is not byte identical')
    mb = MapB(marc.get('map.mapb'))
    mo = MapO(marc.get('map.mapo'))
    n = mb.rec_n
    if mb.rec_off + n * REC > len(mb.raw):
        p.append('record array overruns mapb')
    if mo.ent_n != n:
        p.append('mapo +0C count %d != records %d' % (mo.ent_n, n))
    if mo.obj_n != n:
        p.append('mapo +54 count %d != records %d' % (mo.obj_n, n))
    if mo.pool_off + 2 * mo.pool_n > len(mo.raw):
        p.append('mapo string pool overruns')
    for i in range(n):
        try:
            if not mb.def_name(mb.def_index(i)):
                p.append('record %d: empty def name' % i)
        except Exception as ex:   # noqa: BLE001 - report, keep checking
            p.append('record %d: %s' % (i, ex))
        a = mb.rec_abs(i, 0x2C)
        if a is not None:
            if not 0 <= a < len(mb.raw):
                p.append('record %d: +2C list out of range' % i)
            elif any(not 0 <= c < n for c in children(mb, i)):
                p.append('record %d: +2C child index out of range' % i)
        eo = mo.ent_off + i * ENT
        for f in ENT_OFFSET_FIELDS:
            v = _i32(mo.raw, eo + f)
            if v and not 0 <= eo + v < len(mo.raw):
                p.append('mapo ent %d field %#x out of range' % (i, f))
        oo = mo.obj_off + i * OBJ
        v = _i32(mo.raw, oo + 4)
        if v and not 0 <= oo + v < len(mo.raw):
            p.append('mapo obj %d list out of range' % i)
        if mo.obj_name(i) != str(i):
            p.append('mapo obj %d name %r != %r' % (i, mo.obj_name(i), str(i)))
    if orig is None:
        return p
    om = Marc.parse(orig)
    ob, omo = MapB(om.get('map.mapb')), MapO(om.get('map.mapo'))
    if [x for x, _ in om.files] != [x for x, _ in marc.files]:
        p.append('archive file list changed')
    for (nm, d0), (_, d1) in zip(om.files, marc.files):
        if nm not in ('map.mapb', 'map.mapo') and d0 != d1:
            p.append('%s changed' % nm)
    if mb.raw[:0x20] != ob.raw[:0x20] or mb.raw[0x28:ob.rec_off] != ob.raw[0x28:ob.rec_off]:
        p.append('mapb bytes before the old record array changed')
    base_n = ob.rec_n
    for i in range(base_n):
        if (mb.def_index(i), mb.pos(i), mb.guid(i), children(mb, i)) != \
           (ob.def_index(i), ob.pos(i), ob.guid(i), children(ob, i)):
            p.append('original record %d changed' % i)
        if mb.record(i)[4:0x2C] != ob.record(i)[4:0x2C] or mb.record(i)[0x30:] != ob.record(i)[0x30:]:
            p.append('original record %d bytes changed' % i)
        for f in ENT_OFFSET_FIELDS:
            a0 = _i32(omo.raw, omo.ent_off + i * ENT + f)
            a1 = _i32(mo.raw, mo.ent_off + i * ENT + f)
            t0 = omo.ent_off + i * ENT + a0 if a0 else None
            t1 = mo.ent_off + i * ENT + a1 if a1 else None
            if t0 != t1:
                p.append('original mapo ent %d field %#x retargeted' % (i, f))
        if mo.obj(i)[8:] != omo.obj(i)[8:] or mo.obj_name(i) != omo.obj_name(i):
            p.append('original mapo obj %d changed' % i)
    old = {ob.guid(i) for i in range(base_n)}
    seen: set[bytes] = set()
    for i in range(base_n, n):
        g = mb.guid(i)
        if g in old or g in seen:
            p.append('new record %d GUID collides' % i)
        seen.add(g)
    return p


def info(mac_bytes: bytes) -> str:
    marc = Marc.parse(mac_bytes)
    lines = ['MARC v%d, %d files' % (marc.version, len(marc.files))]
    for nme, d in marc.files:
        lines.append('  %-36s %9d' % (nme, len(d)))
    mb = MapB(marc.get('map.mapb'))
    mo = MapO(marc.get('map.mapo'))
    lines.append('mapb: %d defs @%#x, %d records @%#x' % (mb.def_n, mb.def_off, mb.rec_n, mb.rec_off))
    lines.append('mapo: ent %d @%#x, obj %d @%#x, pool %d wchars @%#x' %
                 (mo.ent_n, mo.ent_off, mo.obj_n, mo.obj_off, mo.pool_n, mo.pool_off))
    for i in range(mb.rec_n):
        k = mb.def_index(i)
        if mb.def_type(k) == 3:
            x, y, z = mb.pos(i)
            lines.append('  rec %3d type3 def %2d %-34s pos (%.1f, %.1f, %.1f)' % (i, k, mb.def_name(k), x, y, z))
    return '\n'.join(lines)


def _parse_copy(s: str) -> tuple[int, float, float, float]:
    a = s.split(':')
    if len(a) != 4:
        raise SystemExit('copy spec must be SRC:DX:DY:DZ, got %r' % s)
    return int(a[0]), float(a[1]), float(a[2]), float(a[3])


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = open(argv[1], 'rb').read()
    if argv[2:] == ['--info']:
        print(info(src))
        return 0
    if argv[2:] == ['--roundtrip']:
        ok = Marc.parse(src).build() == src
        print('roundtrip', 'identical' if ok else 'DIFFERENT')
        return 0 if ok else 1
    if len(argv) < 4:
        print(__doc__)
        return 2
    warns: list[str] = []
    out = make_tiled(src, [_parse_copy(s) for s in argv[3:]], warnings=warns)
    for w in warns:
        print('WARNING:', w)
    probs = verify(out, src)
    for q in probs:
        print('PROBLEM:', q)
    if probs:
        return 1
    open(argv[2], 'wb').write(out)
    print('wrote %s (%d bytes, %d records)' % (argv[2], len(out), MapB(Marc.parse(out).get('map.mapb')).rec_n))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
