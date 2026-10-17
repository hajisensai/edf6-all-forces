"""EDF4.1 / EDF5 MAB blocks (u32 0x08 = 0x03) turned into EDF6's (0x83): the same bytes with every offset made
relative to the struct that holds it.

    mab_from_edf5(b) -> bytes       the 0x83 block; ValueError on anything not laid out as below
    mab_layout(b) -> Layout         the block's structs and offset fields (either flag), checked as below
    mab_set_strings(b, changes) -> bytes   EDF6's block with some string fields renamed, its string area laid again

The block (pylib/mab.py reads EDF6's simplest ones); i32 offsets, the flag decides what they count from:
  0x00 b'MAB\\0', u32 0x04 0x0F, u32 0x08 the flag, u16 0x0C the lists' count (4.1/5/6 weapons and objects: 3 or 4,
       EDF6's appeal_animation blocks 0), u16 0x0E the tracks' count, u16 0x10 the vec4s' count, u16 0x12 the
       strings' bytes, u32 0x14 the lists' headers (0x24), 0x18 the tracks (= the records' end), 0x1C the vec4s,
       0x20 the strings (= len - strings' bytes); these four always from the block's start;
  the lists' headers, 8 bytes each: u16 its kind (= its index), u16 its records' count, i32 its first record (an empty
       list's: where its records would start, i.e. the next list's);
  the records, 0x20 each, every list's after the one before: i32 +0 name, +4 node (strings), i32 +8 the record's
       kind, +0xC a vec4; +0x10 a vec4 (kinds 1, 2) or an f32 (kinds 0, 3, 4); +0x14 a vec4 (kinds 2, 3, 4) or -1
       (kinds 0, 1); i32 +0x18 0; i32 +0x1C an SGO block;
  the tracks, 0x10 each: i32 +0 its first key, i32 +4 the keys' end (every track's: the last key's end), i32 +8 its
       name, i32 +0xC its keys' count; the keys, 0x10 each, every track's after the one before: i32 +0 a string,
       +4 an f32, i32 +8 0, i32 +0xC an SGO block;
  0xBA up to 16 bytes; the vec4s; the records' and keys' SGO blocks (SGO files of their own, offsets inside them from
       their own start: unchanged); the strings, NUL-ended UTF-16.
Flag 0x03 (every MAB in EDF4.1's and EDF5's Root.cpk): the struct offsets above count from the block's start.
Flag 0x83 (every MAB in EDF6's): they count from the start of the 8- or 16-byte struct that holds them (a list header,
a record, a track, a key); -1 stays -1. Nothing moves: the layout, the vec4s, the SGO blocks and the strings are
the same bytes, except that EDF6's string area has 0 in every byte no string covers, where EDF5's often still holds
the leftovers of an earlier string (EDF5 WEAPON/aWeapon078.SGO's: '^' and half a character at 0x63A).

Evidence (2026-10-10, jobs/4bf89026/tmp/mab/verify.py + classify.py): every 0x03 block of EDF4.1's Root.cpk (1153)
and EDF5's (1652) and every 0x83 block of EDF6's (2487) passes mab_layout, and every 0x03 one converts. 1318 EDF5 /
EDF6 pairs (OBJECT / MENUOBJECT ... by file name and node, WEAPON by the port's EDF5->EDF6 weapon table): 592 come out
byte for byte (340 of them only with the string-area zeroing), 0 raise; the other 726, decoded through mab_layout on
both sides, all differ in content EDF6 changed, none in encoding: 416 only locator vec4 values (re-authored IK /
muzzle positions; EDF5 aWeapon078 -> EDF6 aWeapon083: two IK vec4s), 191 locator names / nodes, 114 the records' /
tracks' / keys' / SGO blocks' counts, 5 keys' f32s / SGO blocks.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

LEGACY = 0x03
RELATIVE = 0x83
RECORD = 0x20
TRACK = 0x10
KEY = 0x10
PAD = 0xBA

# record kind -> (+0x10 is a vec4, +0x14 is a vec4 (else -1))
_RECORD_KINDS: dict[int, tuple[bool, bool]] = {0: (False, False), 1: (True, False), 2: (True, True),
                                               3: (False, True), 4: (False, True)}


@dataclass
class Layout:
    flag: int
    # (where the field is, the struct it counts from under flag 0x83, what it points at (from the block's start))
    offsets: list[tuple[int, int, int]] = field(default_factory=list)
    strings: list[int] = field(default_factory=list)      # every string pointed at
    strings_at: int = 0


def _i32(b: bytes, at: int) -> int:
    return struct.unpack_from('<i', b, at)[0]


def mab_layout(b: bytes) -> Layout:
    """The block's offset fields, checked against the layout in the module docstring (either flag)."""
    if len(b) < 0x24 or b[:4] != b'MAB\0':
        raise ValueError('不是 MAB 块')
    v4, flag, nlists, ntracks, nvec, nstr, heads, tracks, vecs, strs = struct.unpack_from('<IIHHHHIIII', b, 4)
    if v4 != 0x0F or flag not in (LEGACY, RELATIVE):
        raise ValueError(f'MAB 头 0x04/0x08 是 {v4:#x}/{flag:#x}：不认识')
    if heads != 0x24 or strs + nstr != len(b):
        raise ValueError('MAB 头的表头偏移 / 字符串区大小不对')
    out = Layout(flag, strings_at=strs)
    vec_end = vecs + 16 * nvec

    def field_at(at: int, base: int) -> int:
        v = _i32(b, at)
        to = v + (base if flag == RELATIVE else 0)
        out.offsets.append((at, base, to))
        return to

    def vec(at: int, base: int) -> None:
        to = field_at(at, base)
        if not vecs <= to < vec_end or (to - vecs) % 16:
            raise ValueError(f'MAB {at:#x} 的坐标偏移不在坐标区')

    def text(at: int, base: int) -> None:
        to = field_at(at, base)
        if not strs <= to < len(b) or (to - strs) % 2:
            raise ValueError(f'MAB {at:#x} 的字符串偏移不在字符串区')
        out.strings.append(to)

    def blob(at: int, base: int) -> None:
        to = field_at(at, base)
        if not vec_end <= to < strs or b[to:to + 4] != b'SGO\0':
            raise ValueError(f'MAB {at:#x} 的 SGO 块偏移不指向坐标区与字符串区之间的 SGO')

    pos = heads + 8 * nlists
    for i in range(nlists):
        h = heads + 8 * i
        kind, n = struct.unpack_from('<HH', b, h)
        if kind != i or field_at(h + 4, h) != pos:
            raise ValueError(f'MAB 第 {i} 张表的类别 / 首记录不对')
        for r in range(pos, pos + RECORD * n, RECORD):
            rkind = _i32(b, r + 8)
            if rkind not in _RECORD_KINDS:
                raise ValueError(f'MAB 记录 {r:#x} 的类别 {rkind}：不认识')
            vec_10, vec_14 = _RECORD_KINDS[rkind]
            text(r, r)
            text(r + 4, r)
            vec(r + 0xC, r)
            if vec_10:
                vec(r + 0x10, r)
            if vec_14:
                vec(r + 0x14, r)
            elif _i32(b, r + 0x14) != -1:
                raise ValueError(f'MAB 记录 {r:#x} +0x14 不是 -1')
            if _i32(b, r + 0x18):
                raise ValueError(f'MAB 记录 {r:#x} +0x18 不是 0')
            blob(r + 0x1C, r)
        pos += RECORD * n
    if pos != tracks:
        raise ValueError('MAB 的记录不止于头 0x18')
    key = keys = tracks + TRACK * ntracks
    counts = [_i32(b, t + 0xC) for t in range(tracks, keys, TRACK)]
    if any(n < 0 for n in counts):
        raise ValueError('MAB 轨道的键数为负')
    keys_end = keys + KEY * sum(counts)
    for t, n in zip(range(tracks, keys, TRACK), counts):
        if field_at(t, t) != key or field_at(t + 4, t) != keys_end:
            raise ValueError(f'MAB 轨道 {t:#x} 的键偏移不对')
        text(t + 8, t)
        key += KEY * n
    for k in range(keys, keys_end, KEY):
        text(k, k)
        if _i32(b, k + 8):
            raise ValueError(f'MAB 键 {k:#x} +8 不是 0')
        blob(k + 0xC, k)
    if vecs != (keys_end + 15) // 16 * 16 or any(x != PAD for x in b[keys_end:vecs]):
        raise ValueError('MAB 键之后不是到 16 对齐的 0xBA')
    if vec_end > strs or (vec_end < strs and vec_end not in {to for _a, _b, to in out.offsets}):
        raise ValueError('MAB 坐标区之后不紧接被引用的 SGO 块')
    for s in out.strings:
        e = s
        while e < len(b) and b[e:e + 2] != b'\0\0':
            e += 2
        if e >= len(b):
            raise ValueError(f'MAB 字符串 {s:#x} 没有结尾')
    return out


def _string_cover(b: bytes, lay: Layout) -> bytearray:
    cover = bytearray(len(b) - lay.strings_at)
    for s in set(lay.strings):
        e = s
        while b[e:e + 2] != b'\0\0':
            e += 2
        cover[s - lay.strings_at:e + 2 - lay.strings_at] = b'\1' * (e + 2 - s)
    return cover


def _text_at(b: bytes, at: int) -> str:
    e = at
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[at:e].decode('utf-16le')


def mab_locator_nodes(b: bytes) -> list[tuple[str, str]]:
    """(name, node) of every record of the block, every list's, in order; ValueError on a layout mab_layout refuses."""
    mab_layout(b)
    nlists = struct.unpack_from('<H', b, 0x0C)[0]
    head, end = struct.unpack_from('<II', b, 0x14)
    flag = struct.unpack_from('<I', b, 8)[0]
    base = (lambda r: r) if flag == RELATIVE else (lambda r: 0)
    return [(_text_at(b, base(r) + _i32(b, r)), _text_at(b, base(r) + _i32(b, r + 4)))
            for r in range(head + 8 * nlists, end, RECORD)]


def _sjis_units(s: str) -> int:
    try:
        return len(s.encode('cp932'))
    except UnicodeEncodeError:   # no Shift-JIS form: its UTF-16 length (no EDF6 block has one)
        return len(s.encode('utf-16le')) // 2


def _string_area(texts: set[str], at: int) -> tuple[bytes, dict[str, int]]:
    """(the string area, {string: where it starts}) the way EDF6's exporter writes it: each string once, in the order
    of its Shift-JIS bytes, in (its Shift-JIS bytes + 1) UTF-16 units, NUL-filled past its text (a kanji name takes
    twice its characters: '排莢' 10 bytes, not 6). Measured on every weapon MAB of EDF6's Root.cpk, 1676 blocks, all
    reproduced (UTF-16 order fails 132, unpadded slots 382)."""
    data, where = b'', {}
    for s in sorted(texts, key=lambda s: s.encode('cp932', 'replace')):
        where[s] = at + len(data)
        raw = s.encode('utf-16le')
        data += raw + b'\0' * (2 * (_sjis_units(s) + 1) - len(raw))
    return data, where


def mab_set_strings(b: bytes, changes: dict[int, str]) -> bytes:
    """EDF6's 0x83 block `b` with the string field at each address in `changes` (a record's +0 name / +4 node, a
    track's +8, a key's +0) now naming its new string. The string area is laid out again as EDF6 lays its own
    (_string_area: every block of EDF6's weapons comes back byte for byte when nothing changes, tools/selftest.py
    mab_strings_relaid_as_the_game); edf5port.fit_locators moves a 4.1 / EDF5 weapon's locators onto bones its EDF6
    model has. ValueError on another layout, or an address that is not a string field."""
    lay = mab_layout(b)
    if lay.flag != RELATIVE:
        raise ValueError(f'MAB 标志是 {lay.flag:#x}，不是 EDF6 的 0x83')
    fields = [(at, base, to) for at, base, to in lay.offsets if to >= lay.strings_at]
    if not set(changes) <= {at for at, _b, _t in fields}:
        raise ValueError(f'MAB 里 {sorted(set(changes) - {at for at, _b, _t in fields})} 不是字符串偏移')
    value = {at: changes.get(at, _text_at(b, to)) for at, _b, to in fields}
    data, where = _string_area(set(value.values()), lay.strings_at)
    if len(data) > 0xFFFF:
        raise ValueError('MAB 字符串区超过 u16')
    out = bytearray(b[:lay.strings_at] + data)
    struct.pack_into('<H', out, 0x12, len(data))
    for at, base, _to in fields:
        struct.pack_into('<i', out, at, where[value[at]] - base)
    return bytes(out)


def mab_from_edf5(b: bytes) -> bytes:
    """EDF4.1 / EDF5's 0x03 block `b` as EDF6's 0x83 block (module docstring); ValueError on another layout."""
    lay = mab_layout(b)
    if lay.flag != LEGACY:
        raise ValueError(f'MAB 标志是 {lay.flag:#x}，不是 EDF5 的 0x03')
    out = bytearray(b)
    struct.pack_into('<I', out, 8, RELATIVE)
    for at, base, to in lay.offsets:
        struct.pack_into('<i', out, at, to - base)
    for i, c in enumerate(_string_cover(b, lay)):
        if not c:
            out[lay.strings_at + i] = 0
    back = mab_layout(bytes(out))
    if [(a, t) for a, _b, t in back.offsets] != [(a, t) for a, _b, t in lay.offsets]:
        raise ValueError('MAB 转换后读回的偏移不一致')
    return bytes(out)


@dataclass(frozen=True)
class Locator:
    """A MAB record: `name` (what the game looks it up by) on bone `node`. Kinds 0 / 1 / 2 / 3 / 4 as in the module
    docstring: `a` is the +0xC vec4 (the position in the node's frame), `b` the +0x10 vec4 (kinds 1, 2: the extent;
    EDF6_Berserker's muzzles have 0.1, 0.1, 5.0) or f32 (kinds 0, 3, 4: a radius or scale, 1.0 for an eye position), `c`
    the +0x14 vec4 (kinds 2, 3, 4: Euler radians, R = Rz Ry Rx; the locator's +Z is the way a muzzle fires)."""
    name: str
    node: str
    kind: int
    a: tuple[float, float, float, float]
    b: tuple[float, float, float, float] | float
    c: tuple[float, float, float, float] | None = None


def _text_at(b: bytes, at: int) -> str:
    e = at
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[at:e].decode('utf-16le')


def _records(b: bytes) -> tuple[Layout, list[tuple[int, int]]]:
    """(layout, [(list index, record address)] for every record, in file order)."""
    lay = mab_layout(b)
    nlists, = struct.unpack_from('<H', b, 0x0C)
    heads = struct.unpack_from('<I', b, 0x14)[0]
    out = []
    pos = heads + 8 * nlists
    for i in range(nlists):
        n, = struct.unpack_from('<H', b, heads + 8 * i + 2)
        out += [(i, r) for r in range(pos, pos + RECORD * n, RECORD)]
        pos += RECORD * n
    return lay, out


def mab_locators(b: bytes) -> list[tuple[int, Locator]]:
    """[(list index, Locator)] for every record of the block, in file order."""
    lay, recs = _records(b)
    rel = lay.flag == RELATIVE

    def at(r: int, off: int) -> int:
        return r + _i32(b, r + off) if rel else _i32(b, r + off)

    out = []
    for i, r in recs:
        kind = _i32(b, r + 8)
        vec_10, vec_14 = _RECORD_KINDS[kind]
        a = struct.unpack_from('<4f', b, at(r, 0xC))
        bb = struct.unpack_from('<4f', b, at(r, 0x10)) if vec_10 else struct.unpack_from('<f', b, r + 0x10)[0]
        c = struct.unpack_from('<4f', b, at(r, 0x14)) if vec_14 else None
        out.append((i, Locator(_text_at(b, at(r, 0)), _text_at(b, at(r, 4)), kind, a, bb, c)))
    return out


def mab_events(b: bytes) -> dict[str, list[tuple[str, float]]]:
    """{track name (a CAS clip's name): [(key string, f32 time), ...]} of the block (either flag): the animation events the
    game raises as the clip plays past each key's time (DragonBig's 火炎開始 / 火炎終了 on fly_attack)."""
    lay = mab_layout(b)
    rel = lay.flag == RELATIVE
    ntracks, = struct.unpack_from('<H', b, 0x0E)
    tracks, = struct.unpack_from('<I', b, 0x18)

    def target(field_at: int, base: int) -> int:
        return _i32(b, field_at) + (base if rel else 0)

    out: dict[str, list[tuple[str, float]]] = {}
    for i in range(ntracks):
        t = tracks + TRACK * i
        first, count = target(t, t), _i32(b, t + 0xC)
        keys = [(first + KEY * j) for j in range(count)]
        out[_text_at(b, target(t + 8, t))] = [(_text_at(b, target(k, k)), struct.unpack_from('<f', b, k + 4)[0])
                                              for k in keys]
    return out


def mab_event_params(b: bytes) -> dict[str, list[tuple[str, float, dict]]]:
    """mab_events with each key's parameters: {track: [(key string, f32 time, {name: value})]}. A key's +0xC is the
    offset of a small SGO (from the key under flag 0x83, from the block's start otherwise; 0: none) holding the
    event's arguments (UfoRobo401's 攻撃判定 {collision, damage, quake, scale}, se {name}, 砲撃開始 {charge_frame}).
    Floats come back as Python floats."""
    import sgo
    lay = mab_layout(b)
    rel = lay.flag == RELATIVE
    ntracks, = struct.unpack_from('<H', b, 0x0E)
    tracks, = struct.unpack_from('<I', b, 0x18)

    def target(field_at: int, base: int) -> int:
        return _i32(b, field_at) + (base if rel else 0)

    def plain(v: object) -> object:
        if isinstance(v, sgo.Float):
            return v.value
        if isinstance(v, list):
            return [plain(x) for x in v]
        if isinstance(v, dict):
            return {k: plain(x) for k, x in v.items()}
        return v

    out: dict[str, list[tuple[str, float, dict]]] = {}
    for i in range(ntracks):
        t = tracks + TRACK * i
        first, count = target(t, t), _i32(b, t + 0xC)
        keys = []
        for j in range(count):
            k = first + KEY * j
            at = _i32(b, k + 0xC)
            params = {}
            if at:
                start = k + at if rel else at
                if b[start:start + 4] != b'SGO\0':
                    raise ValueError(f'MAB event key at {k:#x}: no SGO at {start:#x}')
                params = plain(sgo.read(b[start:])[1])
            keys.append((_text_at(b, target(k, k)), struct.unpack_from('<f', b, k + 4)[0], params))
        out[_text_at(b, target(t + 8, t))] = keys
    return out


def mab_add_locators(b: bytes, new: list[Locator]) -> bytes:
    """The MAB block `b` (either flag) with the records `new` added at the end of list 0, the locator list.

    Why: EDF.dll finds a locator by name in list 0 (0x6BADD0) and uses the answer without asking whether there was
    one: EDF6_Berserker_Large reads 目の基準位置 (its eye height) in its constructor and one muzzle per gun_control
    entry in every gun's setup (0x6D4B90); a block without them gives a null record and a read through it. A 4.1
    object has neither (its guns are separate objects), so the ported one needs them added.

    Layout: the new records go after list 0's own (the lists' records are contiguous, in list order), their vec4s
    after the block's last vec4, their strings after its last string. Everything past an insertion point moves, so
    every offset field is read through mab_layout, the struct holding it and its target moved the same way, and
    written again (relative to the struct under flag 0x83, from the block's start under 0x03); the list headers'
    first-record fields and the header's own four offsets are recomputed. Each new record's SGO block is the one the
    first existing record already points at (shared, as every record of EDF4.1's blocks does: 4.1 Hector's records
    all point at one). A name or node that is already a string of the block points at it, else the string is added.
    Nothing existing changes but position: mab_locators of the result starts with the old records."""
    if not new:
        return b
    lay, recs = _records(b)
    rel = lay.flag == RELATIVE
    nlists, ntracks, nvec, nstr = struct.unpack_from('<4H', b, 0x0C)
    heads, tracks, vecs, strs = struct.unpack_from('<4I', b, 0x14)
    n0, = struct.unpack_from('<H', b, heads + 2)
    if not recs:
        raise ValueError('MAB has no record: no SGO block to share')
    first = recs[0][1]
    blob = _i32(b, first + 0x1C) + (first if rel else 0)
    p0 = heads + 8 * nlists + RECORD * n0
    vec_end = vecs + 16 * nvec
    k = len(new)
    texts = {_text_at(b, s): s for s in lay.strings}
    add_text = bytearray()
    new_text: dict[str, int] = {}
    for loc in new:
        for s in (loc.name, loc.node):
            if s not in texts and s not in new_text:
                new_text[s] = len(add_text)
                add_text += s.encode('utf-16le') + b'\0\0'
    vec_list: list[tuple[float, ...]] = []
    for loc in new:
        vec_10, vec_14 = _RECORD_KINDS[loc.kind]
        vec_list.append(loc.a)
        if vec_10:
            vec_list.append(loc.b)       # type: ignore[arg-type]
        if vec_14:
            vec_list.append(loc.c)       # type: ignore[arg-type]
    m = len(vec_list)
    grow_rec, grow_vec = RECORD * k, 16 * m

    def f(x: int) -> int:
        """Where the byte that was at `x` is now."""
        return x if x < p0 else x + grow_rec if x < vec_end else x + grow_rec + grow_vec

    out = bytearray(b[:p0]) + bytes(grow_rec) + b[p0:vec_end] + bytes(grow_vec) + b[vec_end:] + add_text
    new_strs = f(strs)
    struct.pack_into('<4H', out, 0x0C, nlists, ntracks, nvec + m, nstr + len(add_text))
    struct.pack_into('<4I', out, 0x14, heads, f(tracks), f(vecs), new_strs)
    moved_heads = {heads + 8 * i + 4 for i in range(nlists)}
    for at_, base, to in lay.offsets:
        if at_ in moved_heads:
            continue
        struct.pack_into('<i', out, f(at_), f(to) - (f(base) if rel else 0))
    pos = heads + 8 * nlists
    for i in range(nlists):
        h = heads + 8 * i
        count = struct.unpack_from('<H', b, h + 2)[0] + (k if i == 0 else 0)
        struct.pack_into('<HH', out, h, i, count)
        struct.pack_into('<i', out, h + 4, pos - h if rel else pos)
        pos += RECORD * count
    vec_at = f(vecs) + 16 * nvec
    for i, v in enumerate(vec_list):
        struct.pack_into('<4f', out, vec_at + 16 * i, *v)
    next_vec = vec_at
    str_at = new_strs + (len(b) - strs)
    for j, loc in enumerate(new):
        r = p0 + RECORD * j

        def put(off: int, to: int) -> None:
            struct.pack_into('<i', out, r + off, to - r if rel else to)

        def string(s: str) -> int:
            return f(texts[s]) if s in texts else str_at + new_text[s]

        put(0, string(loc.name))
        put(4, string(loc.node))
        struct.pack_into('<i', out, r + 8, loc.kind)
        vec_10, vec_14 = _RECORD_KINDS[loc.kind]
        put(0xC, next_vec)
        next_vec += 16
        if vec_10:
            put(0x10, next_vec)
            next_vec += 16
        else:
            struct.pack_into('<f', out, r + 0x10, float(loc.b))        # type: ignore[arg-type]
        if vec_14:
            put(0x14, next_vec)
            next_vec += 16
        else:
            struct.pack_into('<i', out, r + 0x14, -1)
        struct.pack_into('<i', out, r + 0x18, 0)
        put(0x1C, f(blob))
    result = bytes(out)
    mab_layout(result)
    return result


def mab_retarget(b: bytes, name: str, node: str) -> bytes:
    """The MAB block `b` with every record of list 0 called `name` moved onto bone `node` (its position, extent and
    angles are in the bone's frame, so they are kept as they are).

    Why: a locator keeps its place in the model but not its bone when the bone is replaced by a finer chain: EDF6's
    UfoSmall507 fires from the locator 銃口 and expects it on its `gun` bone, which the gun's aim turns, while 4.1's
    UFO401 hangs every locator on `center`. Only the record's node field changes; the string `node` is the block's
    own when it has it, else appended past the last string (the string area is the block's end; only the header's
    size of it grows, nothing moves). ValueError when no record of list 0 has that name."""
    lay, recs = _records(b)
    rel = lay.flag == RELATIVE
    texts = {_text_at(b, s): s for s in lay.strings}
    out = bytearray(b)
    if node not in texts:
        texts[node] = len(out)
        text = node.encode('utf-16le') + b'\0\0'
        out += text
        nlists, ntracks, nvec, nstr = struct.unpack_from('<4H', b, 0x0C)
        struct.pack_into('<4H', out, 0x0C, nlists, ntracks, nvec, nstr + len(text))
    hits = [r for i, r in recs if i == 0 and _text_at(b, r + _i32(b, r) if rel else _i32(b, r)) == name]
    if not hits:
        raise ValueError(f'MAB has no locator {name}')
    for r in hits:
        struct.pack_into('<i', out, r + 4, texts[node] - r if rel else texts[node])
    result = bytes(out)
    mab_layout(result)
    return result
