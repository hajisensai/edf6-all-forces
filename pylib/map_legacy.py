"""Convert an EDF4.1 / EDF5 map archive (.MAC) to EDF6's MAPB/MAPO layout.

    python map_legacy.py OLD.MAC NEW.MAC [--edf6-scene]

mac_from_legacy(old_mac) parses the old MARC leniently (EDF4.1 fills the reserved directory words, old
data is not 16-aligned), re-lays map.mapb and map.mapo in EDF6's layout and repacks with
bigmap.Marc.build. Every other member is kept byte-identical and in directory order. Field tables and
the evidence for them: docs/edf41-map-mac-format.md; tools/selftest.py map_legacy_real checks it against the games.

MAPB (old and new writers emit the blocks in the same order; struct sizes and two orders changed):
* def 0x38 -> 0x3C: a dword inserted at +0x20 (old +0x20.. move to +0x24..; EDF6 writes 0 there for
  every def it shares with EDF5); record 0x44 -> 0x48: a dword appended at +0x44 (0).
* every offset field (header, def +04 SGO / +0C +14 +20 +28 names, record +00 def / +2C child list,
  child list, the 0x78 and 0x7C blocks) is mapped through the old->new address map.
* SGO area rebuilt: each SGO block (header, nodes, names, its own strings; self-contained) copied
  verbatim, def SGOs in def index order, then the header-slot SGOs (+0x88 first, +0x70 last), each block
  4-aligned -- EDF6's order (the old writers used an unrelated order).
* string table rebuilt in first-use order (def names in def order, each def's four names in field
  order, then the 0x78 block's names, then the 0x7C block's), deduplicated -- EDF6's order (old: sorted).
* scene_sgos='edf6' empties the header slots as every EDF6 map has them (lighting/grass/SceneType data
  live in EDF6's .MAE files); the default 'keep' carries them over.

MAPO:
* header 0x58 -> 0x60: new pair +0x58/+0x5C (0x0C structs; written empty: 0, 0).
* EDF4.1's 0x38 object structs get EDF5/6's +0x38 dword (0).
* the per-record named table (+0x50/+0x54) EDF6 fills is created: one 0x0C entry per record, name =
  decimal record index, empty (float,int) list whose offset is the write cursor after the table (that is
  how EDF6 writes empty lists), placed after the grass data and before the pool.
* the string pool (always the last block) is rebuilt sorted by UTF-16 code unit and deduplicated with the
  new names merged in, every string offset re-pointed (entry +10/+14, object +04/+14/+18/+20/+28/+30).
* EDF4.1 leaves uninitialised bytes (a matrix row: 1.0f, 0, 0, ...) in the entries of records that have
  no 0x3C objects (count +08 == 0); EDF5/EDF6 write those entries as zero (only a valid +00 grass pair
  survives), so they are zeroed.
"""
from __future__ import annotations

import bisect
import struct
import sys
from dataclasses import dataclass, field
from typing import Callable

import bigmap

# ----------------------------------------------------------------------------- small helpers


def i32(b: bytes | bytearray, o: int) -> int:
    return struct.unpack_from('<i', b, o)[0]


def put(b: bytearray, o: int, v: int) -> None:
    struct.pack_into('<i', b, o, v)


def wstr_end(b: bytes | bytearray, o: int) -> int:
    """Offset just past the NUL of the UTF-16 string at o."""
    e = o
    while b[e:e + 2] != b'\0\0':
        e += 2
    return e + 2


def wstr(b: bytes | bytearray, o: int) -> str:
    return bytes(b[o:wstr_end(b, o) - 2]).decode('utf-16le')


# ----------------------------------------------------------------------------- lenient MARC


def parse_marc_lenient(d: bytes) -> tuple[int, list[tuple[str, bytes]]]:
    """(version, [(name, data)] in directory order). Ignores the reserved directory words (EDF4.1 fills
    them) and does not require aligned data."""
    if d[:4] != b'MARC':
        raise ValueError('not a MARC archive')
    ver, data_off, _dlen, _ssum, dir_rel, count, _soff, _sw = struct.unpack_from('<8i', d, 4)
    if data_off != bigmap.MARC_HDR:
        raise ValueError('unexpected MARC data offset %#x' % data_off)
    dir_off = 0x14 + dir_rel
    files: list[tuple[str, bytes]] = []
    for i in range(count):
        e = dir_off + i * 0x18
        name_rel, off, size = struct.unpack_from('<3i', d, e)
        if not 0 <= off <= off + size <= len(d):
            raise ValueError('MARC entry %d out of range' % i)
        files.append((wstr(d, e + name_rel), bytes(d[off:off + size])))
    return ver, files


# ----------------------------------------------------------------------------- relocation engine


@dataclass
class Resize:
    """An array of fixed-size structs whose element grows from old to new bytes; ins(off) maps an
    old in-struct byte offset to the new one, fill(old_elem) builds the new element."""
    start: int
    count: int
    old: int
    new: int
    ins: Callable[[int], int]
    fill: Callable[[bytes], bytes]

    @property
    def end(self) -> int:
        return self.start + self.count * self.old


@dataclass
class Relayout:
    """Builds a new buffer from an old one: resized arrays are re-emitted, replaced spans get new
    bytes, everything else is copied. amap() maps any old address (including one-past-the-end
    addresses) to the new one."""
    old: bytes
    resizes: list[Resize] = field(default_factory=list)
    replaces: list[tuple[int, int, bytes]] = field(default_factory=list)   # (start, end, new bytes)

    def _spans(self) -> list[tuple[int, int, object]]:
        sp: list[tuple[int, int, object]] = [(r.start, r.end, r) for r in self.resizes if r.count]
        sp += [(a, e, data) for a, e, data in self.replaces]
        sp.sort(key=lambda t: (t[0], t[1]))
        for (a0, e0, _), (a1, _e1, _) in zip(sp, sp[1:]):
            if a1 < e0:
                raise ValueError('overlapping relayout spans %#x..%#x / %#x' % (a0, e0, a1))
        return sp

    def build(self) -> bytes:
        sp = self._spans()
        out = bytearray()
        pos = 0
        self._starts: list[int] = []
        self._map: list[tuple[int, int, int, object]] = []    # (old start, old end, new start, span)
        for a, e, obj in sp:
            out += self.old[pos:a]
            self._map.append((a, e, len(out), obj))
            if isinstance(obj, Resize):
                for k in range(obj.count):
                    el = obj.fill(self.old[a + k * obj.old:a + (k + 1) * obj.old])
                    assert len(el) == obj.new
                    out += el
            else:
                out += obj
            pos = e
        out += self.old[pos:]
        self._starts = [m[0] for m in self._map]
        return bytes(out)

    def new_start(self, old_start: int, data: bytes) -> int:
        """New address of the replaced span that started at old_start with these new bytes."""
        for a, _e, ns, obj in self._map:
            if a == old_start and isinstance(obj, bytes) and obj == data:
                return ns
        raise KeyError(old_start)

    def amap(self, addr: int) -> int:
        """New address of old address addr. An address equal to the end of a span (or to the position
        of a pure insertion) maps to just after the span's new bytes."""
        i = bisect.bisect_right(self._starts, addr) - 1
        if i < 0:
            return addr
        a, e, ns, obj = self._map[i]
        if addr >= e:                               # after this span: constant shift
            nend = ns + (obj.count * obj.new if isinstance(obj, Resize) else len(obj))
            return addr - e + nend
        if isinstance(obj, Resize):
            k, r = divmod(addr - a, obj.old)
            return ns + k * obj.new + obj.ins(r)
        if addr == a:
            return ns
        raise ValueError('pointer into a replaced span (%#x)' % addr)


# A pointer field: (location of the int32 in the old buffer, base it is relative to in the old buffer).
Ptr = tuple[int, int]


def apply_ptrs(new: bytearray, rl: Relayout, ptrs: list[Ptr]) -> None:
    """Rewrite every pointer: new value = amap(target) - amap(base), stored at amap(location)."""
    seen: set[int] = set()
    for loc, base in ptrs:
        if loc in seen:
            continue
        seen.add(loc)
        v = i32(rl.old, loc)
        if v == 0:
            continue
        put(new, rl.amap(loc), rl.amap(base + v) - rl.amap(base))


# ----------------------------------------------------------------------------- SGO pointer walk


def sgo_ptrs(b: bytes, s: int) -> list[Ptr]:
    """Every offset field of the SGO at s: header data/name/raw offsets (relative to the SGO), name
    entries (relative to the entry) and array / string / raw nodes (relative to the node)."""
    if b[s:s + 4] != b'SGO\0':
        raise ValueError('no SGO at %#x' % s)
    _ver, count, data_off, name_count, name_off = struct.unpack_from('<5i', b, s + 4)
    p: list[Ptr] = [(s + 0x0C, s), (s + 0x14, s), (s + 0x1C, s)]
    for i in range(name_count):
        p.append((s + name_off + 8 * i, s + name_off + 8 * i))
    todo = [s + data_off + 12 * i for i in range(count)]
    while todo:
        n = todo.pop()
        typ, a, _off = struct.unpack_from('<iiI', b, n)
        if typ in (0, 3, 4):
            p.append((n + 8, n))
        if typ == 0 and a:
            base = n + i32(b, n + 8)
            todo += [base + 12 * j for j in range(a)]
    return p


# ----------------------------------------------------------------------------- MAPB

MAPB_DEF_OLD, MAPB_DEF_NEW = 0x38, 0x3C
MAPB_REC_OLD, MAPB_REC_NEW = 0x44, 0x48
MAPB_SLOTS = tuple(range(0x28, 0x78, 8)) + (0x80, 0x88)   # self-relative SGO refs, each followed by 0
MAPB_ABS = (0x08, 0x10, 0x18, 0x20, 0x78, 0x7C)            # mapb-relative offsets


def _mapb_layout_fits(b: bytes, legacy: bool) -> bool:
    """True when every record's def pointer hits a def of this size and the record array ends exactly
    where the next block starts (child lists, point blocks, SGOs or the string table)."""
    dsz = MAPB_DEF_OLD if legacy else MAPB_DEF_NEW
    rsz = MAPB_REC_OLD if legacy else MAPB_REC_NEW
    do, dn, ro, rn = i32(b, 0x18), i32(b, 0x1C), i32(b, 0x20), i32(b, 0x24)
    end = ro + rn * rsz
    if end > len(b):
        return False
    nxt = [x for x in (i32(b, 0x08), i32(b, 0x78), i32(b, 0x7C)) if x >= ro]
    nxt += [f + i32(b, f) for f in MAPB_SLOTS if i32(b, f)]
    for blk, fields in ((i32(b, 0x78), (0, 8)), (i32(b, 0x7C), (0, 8, 0x10))):
        if blk:
            nxt += [blk + i32(b, blk + f) for f in fields]
    for k in range(rn):
        r = ro + k * rsz
        t, rem = divmod(r + i32(b, r) - do, dsz)
        if rem or not 0 <= t < dn:
            return False
        if i32(b, r + 0x2C):
            nxt.append(r + i32(b, r + 0x2C))
    after = [x for x in nxt if x >= ro]
    return not after or min(after) == end


def mapb_is_legacy(b: bytes) -> bool:
    """True for the EDF4.1/EDF5 layout (def 0x38, record 0x44), False for EDF6's (0x3C, 0x48)."""
    if b[:4] != b'MAPB' or i32(b, 4) != 2:
        raise ValueError('not MAPB v2')
    fits = [lg for lg in (True, False) if _mapb_layout_fits(b, lg)]
    if len(fits) != 1:
        raise ValueError('cannot tell the MAPB layout (fits: %r)' % fits)
    return fits[0]


def mapb_def_str_fields(legacy: bool) -> tuple[int, ...]:
    """Self-relative strtab pointers of a def, in the order EDF6 lays their strings out: model name,
    then the three other model names (the +0x14 one and the two LOD names)."""
    return (0x0C, 0x14, 0x20, 0x28) if legacy else (0x0C, 0x14, 0x24, 0x2C)


def mapb_ptrs(b: bytes, legacy: bool) -> tuple[list[Ptr], list[Ptr]]:
    """(offset fields that are not strtab pointers, strtab pointers in EDF6's first-use order), each as
    (location, base). strtab users: defs (+0C/+14/+20/+28 old, +0C/+14/+24/+2C new), the 0x78 block's
    0x0C structs (+00) and the 0x7C block's int list (each entry), all self-relative."""
    dsz = MAPB_DEF_OLD if legacy else MAPB_DEF_NEW
    rsz = MAPB_REC_OLD if legacy else MAPB_REC_NEW
    p: list[Ptr] = [(f, 0) for f in MAPB_ABS]
    sp: list[Ptr] = []
    sgos: list[int] = []
    for f in MAPB_SLOTS:
        p.append((f, f))
        if i32(b, f):
            sgos.append(f + i32(b, f))
    do, dn = i32(b, 0x18), i32(b, 0x1C)
    for k in range(dn):
        d = do + k * dsz
        p.append((d + 4, d + 4))
        sp += [(d + f, d + f) for f in mapb_def_str_fields(legacy)]
        if i32(b, d + 4):
            sgos.append(d + 4 + i32(b, d + 4))
    ro, rn = i32(b, 0x20), i32(b, 0x24)
    for k in range(rn):
        r = ro + k * rsz
        p += [(r, r), (r + 0x2C, r)]
        if i32(b, r + 0x2C):
            h = r + i32(b, r + 0x2C)
            p.append((h, h))
    a = i32(b, 0x78)
    if a:
        p += [(a, a), (a + 8, a)]
        so, sn = i32(b, a + 8), i32(b, a + 12)
        for k in range(sn):
            s = a + so + 12 * k
            p.append((s + 4, s))
            sp.append((s, s))
    bb = i32(b, 0x7C)
    if bb:
        p += [(bb, bb), (bb + 8, bb), (bb + 0x10, bb)]
        lo, ln = bb + i32(b, bb + 0x10), i32(b, bb + 0x14)
        sp += [(lo + 4 * k, lo + 4 * k) for k in range(ln)]
    for s in dict.fromkeys(sgos):
        p += sgo_ptrs(b, s)
    return p, sp


def strtab_strings(b: bytes) -> list[tuple[int, str]]:
    """(offset, text) of every string in the MAPB string table (+0x08 offset, +0x0C length in units)."""
    so, sn = i32(b, 0x08), i32(b, 0x0C)
    out: list[tuple[int, str]] = []
    p = so
    while p < so + 2 * sn:
        e = wstr_end(b, p)
        out.append((p, b[p:e - 2].decode('utf-16le')))
        p = e
    return out


def _def_fill(e: bytes) -> bytes:
    return e[:0x20] + b'\0\0\0\0' + e[0x20:]


def _rec_fill(e: bytes) -> bytes:
    return e + b'\0\0\0\0'


def empty_sgo(version: int) -> bytes:
    """An SGO with no members, as EDF6 writes it into the 0x70 / 0x88 slots."""
    return struct.pack('<4s7i', b'SGO\0', version, 0, 0x20, 0, 0x20, 0, 0x20)


def sgo_extent(b: bytes, s: int) -> int:
    """End of the SGO block at s: header, nodes, name table and its own strings / raw blocks. Every
    MAPB SGO is self-contained (no offset leaves its block), which is checked here."""
    end = s
    for loc, base in sgo_ptrs(b, s):
        end = max(end, loc + 4)
        v = i32(b, loc)
        if not v:
            continue
        t = base + v
        if t < s:
            raise ValueError('SGO at %#x points before itself' % s)
        if loc >= s + 0x20 and loc == base + 8:               # array / string / raw node
            typ, a = struct.unpack_from('<ii', b, base)
            end = max(end, t + 12 * a if typ == 0 else t + 2 * a + 2 if typ == 3 else t + a)
        elif loc >= s + 0x20:                                 # name entry
            end = max(end, wstr_end(b, t))
        else:                                                 # header offsets
            end = max(end, t)
    _v, count, data_off, name_count, name_off = struct.unpack_from('<5i', b, s + 4)
    return max(end, s + data_off + 12 * count, s + name_off + 8 * name_count)


def _pad4(n: int) -> int:
    return (n + 3) & ~3


def mapb_from_legacy(b: bytes, scene_sgos: str = 'keep') -> bytes:
    """MAPB re-laid for EDF6.

    Layout written (EDF6's): header, defs (0x3C), child lists, records (0x48), the 0x78 / 0x7C point
    blocks (all carried over in their old order, re-based), then the SGO area rebuilt the EDF6 way --
    every def's SGO in def index order, then the header-slot SGOs (+0x88 first, +0x70 last), each block
    4-aligned -- then the string table rebuilt in first-use order (def names in def order, then the
    0x78 block's names, then the 0x7C block's; the old writers sorted it).

    scene_sgos: 'keep' carries the header-slot SGOs over (six weather lighting SGOs at +0x28..+0x50,
    grass parameters at +0x70, SceneType at +0x88); 'edf6' writes them as every EDF6 map has them
    (+0x28..+0x68 and +0x80 zero, empty SGOs at +0x88 and +0x70): EDF6 keeps that data in the .MAE files.
    """
    if scene_sgos not in ('keep', 'edf6'):
        raise ValueError(scene_sgos)
    if not mapb_is_legacy(b):
        return b
    do, dn = i32(b, 0x18), i32(b, 0x1C)
    rl = Relayout(b)
    rl.resizes.append(Resize(do, dn, MAPB_DEF_OLD, MAPB_DEF_NEW,
                             lambda o: o if o < 0x20 else o + 4, _def_fill))
    rl.resizes.append(Resize(i32(b, 0x20), i32(b, 0x24), MAPB_REC_OLD, MAPB_REC_NEW,
                             lambda o: o, _rec_fill))
    ptrs, sptrs = mapb_ptrs(b, True)

    # -- SGO area: [first SGO, string table) must be exactly the SGO blocks, 4-aligned
    def_sgo = {k: do + k * MAPB_DEF_OLD + 4 + i32(b, do + k * MAPB_DEF_OLD + 4)
               for k in range(dn) if i32(b, do + k * MAPB_DEF_OLD + 4)}
    slot_sgo = {f: f + i32(b, f) for f in MAPB_SLOTS if i32(b, f)}
    starts = sorted(set(def_sgo.values()) | set(slot_sgo.values()))
    if len(starts) != len(def_sgo) + len(slot_sgo):
        raise ValueError('MAPB SGO shared by two users')
    so, sn = i32(b, 0x08), i32(b, 0x0C)
    lo = starts[0] if starts else so
    ext = {s: sgo_extent(b, s) for s in starts}
    pos = lo
    for s in starts:
        if s != _pad4(pos):
            raise ValueError('MAPB SGO area has a gap at %#x' % pos)
        pos = ext[s]
    if starts and _pad4(pos) != so:
        raise ValueError('MAPB SGO area does not end at the string table')
    sgo_locs: set[int] = set(MAPB_SLOTS) | {do + k * MAPB_DEF_OLD + 4 for k in range(dn)}
    for s in starts:
        sgo_locs |= {loc for loc, _ in sgo_ptrs(b, s)}
    ptrs = [p for p in ptrs if p[0] not in sgo_locs]

    area = bytearray()
    new_def_sgo: dict[int, int] = {}
    new_slot_sgo: dict[int, int] = {}

    def emit(blk: bytes) -> int:
        at = len(area)
        area.extend(blk)
        area.extend(bytes(_pad4(len(area)) - len(area)))
        return at

    for k in range(dn):
        if k in def_sgo:
            s = def_sgo[k]
            new_def_sgo[k] = emit(b[s:ext[s]])
    if scene_sgos == 'edf6':
        ver = i32(b, slot_sgo[0x88] + 4) if 0x88 in slot_sgo else 0x102
        new_slot_sgo[0x88] = emit(empty_sgo(ver))
        new_slot_sgo[0x70] = emit(empty_sgo(ver))
    else:
        for f in (0x88, 0x80) + tuple(range(0x28, 0x78, 8)):
            if f in slot_sgo:
                new_slot_sgo[f] = emit(b[slot_sgo[f]:ext[slot_sgo[f]]])
    area_b = bytes(area)

    # -- string table: first-use order, deduplicated; unreferenced old strings kept after the rest
    strs = strtab_strings(b)
    at = {p: t for p, t in strs}
    order: list[str] = []
    for loc, base in sptrs:
        v = i32(b, loc)
        if v:
            if base + v not in at:
                raise ValueError('mapb string pointer at %#x misses the string table' % loc)
            order.append(at[base + v])
    order = list(dict.fromkeys(order + [t for _, t in strs]))
    tab_rel: dict[str, int] = {}
    tab = bytearray()
    for t in order:
        tab_rel[t] = len(tab)
        tab += t.encode('utf-16le') + b'\0\0'
    tab_b = bytes(tab)
    if so + 2 * sn != len(b):
        raise ValueError('MAPB string table is not the last block')

    rl.replaces.append((lo, so, area_b))
    rl.replaces.append((so, so + 2 * sn, tab_b))
    new = bytearray(rl.build())
    apply_ptrs(new, rl, ptrs)

    area_at = rl.new_start(lo, area_b)
    new_so = rl.new_start(so, tab_b)
    put(new, 0x08, new_so)
    put(new, 0x0C, len(tab_b) // 2)
    for loc, base in sptrs:
        v = i32(b, loc)
        if v:
            put(new, rl.amap(loc), new_so + tab_rel[at[base + v]] - rl.amap(base))
    for f in MAPB_SLOTS:
        put(new, f, area_at + new_slot_sgo[f] - f if f in new_slot_sgo else 0)
    for k in range(dn):
        d4 = rl.amap(do + k * MAPB_DEF_OLD + 4)
        put(new, d4, area_at + new_def_sgo[k] - d4 if k in new_def_sgo else 0)
    return bytes(new)


# ----------------------------------------------------------------------------- MAPO

MAPO_HDR_OLD, MAPO_HDR_NEW = 0x58, 0x60
MAPO_ENT, MAPO_NAMED, MAPO_S58 = 0x1C, 0x0C, 0x0C
OBJ_OLD41, OBJ_NEW = 0x38, 0x3C
# header (offset field, element size) of the plain arrays; all offsets mapo-relative
MAPO_ARRAYS = ((0x08, MAPO_ENT), (0x10, 0x0C), (0x20, OBJ_NEW), (0x28, 4), (0x30, 0x0C), (0x38, 0x10),
               (0x40, 0x10), (0x48, 2), (0x50, MAPO_NAMED), (0x58, MAPO_S58))
OBJ_STR_FIELDS = (0x04, 0x14, 0x18, 0x20, 0x28, 0x30)      # relative to the object, into the pool
OBJ_VEC_FIELDS = (0x08, 0x0C, 0x10)                        # vec4a / vec3 / vec4b, relative to the object
ENT_STR_FIELDS = (0x10, 0x14)
ENT_OFF_FIELDS = (0x00, 0x04, 0x18)                        # grass pair, objptr slice, iif element


def mapo_layout(o: bytes) -> tuple[int, int]:
    """(header size, object size) of a MAPO."""
    if o[:4] != b'MAPO':
        raise ValueError('not MAPO')
    starts = [i32(o, f) for f, _ in MAPO_ARRAYS if i32(o, f) and i32(o, f + 4)]
    hdr = min(starts) if starts else len(o)
    if hdr not in (MAPO_HDR_OLD, MAPO_HDR_NEW):
        raise ValueError('unexpected MAPO header size %#x' % hdr)
    oo, on = i32(o, 0x20), i32(o, 0x24)
    po = i32(o, 0x28)
    osz = (po - oo) // on if on else OBJ_NEW
    if osz not in (OBJ_OLD41, OBJ_NEW):
        raise ValueError('unexpected MAPO object size %#x' % osz)
    return hdr, osz


def pool_strings(o: bytes) -> list[tuple[int, str]]:
    """(offset, text) of every string in the MAPO pool (+0x48 offset, +0x4C length in units)."""
    po, pn = i32(o, 0x48), i32(o, 0x4C)
    out: list[tuple[int, str]] = []
    p = po
    while p < po + 2 * pn:
        e = wstr_end(o, p)
        out.append((p, o[p:e - 2].decode('utf-16le')))
        p = e
    return out


def _u16key(s: str) -> bytes:
    return s.encode('utf-16be')


def mapo_from_legacy(o: bytes, n_records: int) -> bytes:
    """MAPO re-laid for EDF6 (see the module docstring); an EDF6 MAPO is returned unchanged."""
    hdr, osz = mapo_layout(o)
    if hdr == MAPO_HDR_NEW:
        return o
    o = bytearray(o)
    eo, en = i32(o, 0x08), i32(o, 0x0C)
    if en != n_records:
        raise ValueError('mapo entry count %d != record count %d' % (en, n_records))
    if i32(o, 0x50) or i32(o, 0x54):
        raise ValueError('legacy mapo already has a +0x50 table')
    po, pn = i32(o, 0x48), i32(o, 0x4C)
    if pn == 0:
        po = len(o)                              # empty pool (offset written as 0)
    if po + 2 * pn != len(o):
        raise ValueError('mapo string pool is not the last block')
    oo, on = i32(o, 0x20), i32(o, 0x24)

    # -- EDF4.1 uninitialised entries (records without objects): zero all but a valid grass pair.
    lo_pair = eo + en * MAPO_ENT
    for k in range(en):
        e = eo + k * MAPO_ENT
        if i32(o, e + 8) == 0:
            v = i32(o, e)
            keep = v and lo_pair <= e + v and e + v + 8 <= po
            o[e:e + MAPO_ENT] = struct.pack('<i', v if keep else 0) + bytes(MAPO_ENT - 4)

    # -- old string pool and every pointer into it
    strs = pool_strings(o)
    at = {p: s for p, s in strs}
    str_ptrs: list[Ptr] = []
    for k in range(en):
        e = eo + k * MAPO_ENT
        str_ptrs += [(e + f, e) for f in ENT_STR_FIELDS]
    for k in range(on):
        ob = oo + k * osz
        str_ptrs += [(ob + f, ob) for f in OBJ_STR_FIELDS]
    for loc, base in str_ptrs:
        v = i32(o, loc)
        if v and base + v not in at:
            raise ValueError('mapo string pointer at %#x does not hit the pool' % loc)

    # -- new pool: old strings + decimal record names, sorted by UTF-16 code unit, deduplicated
    names = [str(i) for i in range(n_records)]
    pool = sorted(set(at.values()) | set(names), key=_u16key)
    pool_bytes = b''.join(s.encode('utf-16le') + b'\0\0' for s in pool)
    pool_rel: dict[str, int] = {}
    q = 0
    for s in pool:
        pool_rel[s] = q
        q += 2 * len(s) + 2

    # -- non-string pointers (resolved through the relayout address map)
    ptrs: list[Ptr] = [(f, 0) for f, _ in MAPO_ARRAYS if f < MAPO_HDR_OLD and f != 0x48]
    for k in range(en):
        e = eo + k * MAPO_ENT
        ptrs += [(e + f, e) for f in ENT_OFF_FIELDS]
        v = i32(o, e)
        if v and i32(o, e + v):                  # grass pair {count, offset rel pair} -> data
            ptrs.append((e + v + 4, e + v))
    for k in range(on):
        ob = oo + k * osz
        ptrs += [(ob + f, ob) for f in OBJ_VEC_FIELDS]
    pp, pc = i32(o, 0x28), i32(o, 0x2C)
    ptrs += [(pp + 4 * k, pp + 4 * k) for k in range(pc)]

    # -- relayout: header grows by 8 (new +58/+5C pair), objects 0x38 -> 0x3C, the old pool is
    #    replaced by [named table][new pool]
    named_size = n_records * MAPO_NAMED
    rl = Relayout(bytes(o))
    rl.replaces.append((MAPO_HDR_OLD, MAPO_HDR_OLD, b'\0' * 8))
    if osz == OBJ_OLD41:
        rl.resizes.append(Resize(oo, on, OBJ_OLD41, OBJ_NEW, lambda x: x, lambda el: el + b'\0\0\0\0'))
    tail = bytes(named_size) + pool_bytes
    rl.replaces.append((po, len(o), tail))
    new = bytearray(rl.build())
    apply_ptrs(new, rl, ptrs)
    named_at = rl.new_start(po, tail)
    new_pool = named_at + named_size

    def str_target(loc: int, base: int) -> None:
        v = i32(o, loc)
        if v:
            put(new, rl.amap(loc), new_pool + pool_rel[at[base + v]] - rl.amap(base))

    for loc, base in str_ptrs:
        str_target(loc, base)
    put(new, 0x48, new_pool)
    put(new, 0x4C, len(pool_bytes) // 2)
    put(new, 0x50, named_at if n_records else 0)
    put(new, 0x54, n_records)
    put(new, 0x58, 0)
    put(new, 0x5C, 0)
    for k in range(n_records):
        e = named_at + k * MAPO_NAMED
        struct.pack_into('<iii', new, e, new_pool + pool_rel[names[k]] - e, new_pool - e, 0)
    return bytes(new)


# ----------------------------------------------------------------------------- MAC


def mac_from_legacy(old_mac: bytes, scene_sgos: str = 'keep') -> bytes:
    """An EDF4.1/EDF5 .MAC re-laid for EDF6 (map.mapb + map.mapo converted, all other members kept).
    scene_sgos: see mapb_from_legacy."""
    ver, files = parse_marc_lenient(old_mac)
    names = [n for n, _ in files]
    if names.count('map.mapb') != 1 or names.count('map.mapo') != 1:
        raise ValueError('archive needs exactly one map.mapb and one map.mapo')
    d = dict(files)
    mb = mapb_from_legacy(d['map.mapb'], scene_sgos)
    mo = mapo_from_legacy(d['map.mapo'], i32(mb, 0x24))
    out = [(n, mb if n == 'map.mapb' else mo if n == 'map.mapo' else data) for n, data in files]
    return bigmap.Marc(ver, out).build()


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if a != '--edf6-scene']
    if len(args) != 2:
        print(__doc__)
        return 2
    with open(args[0], 'rb') as h:
        out = mac_from_legacy(h.read(), 'edf6' if '--edf6-scene' in argv else 'keep')
    probs = bigmap.verify(out)
    for p in probs:
        print('PROBLEM:', p)
    if probs:
        return 1
    with open(args[1], 'wb') as h:
        h.write(out)
    print('wrote %s (%d bytes)' % (args[1], len(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
