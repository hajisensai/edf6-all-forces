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


def mab_set_strings(b: bytes, changes: dict[int, str]) -> bytes:
    """EDF6's 0x83 block `b` with the string field at each address in `changes` (a record's +0 name / +4 node, a
    track's +8, a key's +0) now naming its new string. The string area is laid out again as EDF6 lays its own: every
    string still pointed at, once, NUL-ended, in UTF-16 order, nothing else (edf5port.fit_locators: a 4.1 / EDF5
    weapon's locator node 'mdl' -> its EDF6 model's root; tools/selftest.py ported_weapon_locators_fit). ValueError on
    another layout, or an address that is not a string field."""
    lay = mab_layout(b)
    if lay.flag != RELATIVE:
        raise ValueError(f'MAB 标志是 {lay.flag:#x}，不是 EDF6 的 0x83')
    fields = [(at, base, to) for at, base, to in lay.offsets if to >= lay.strings_at]
    if not set(changes) <= {at for at, _b, _t in fields}:
        raise ValueError(f'MAB 里 {sorted(set(changes) - {at for at, _b, _t in fields})} 不是字符串偏移')
    value = {at: changes.get(at, _text_at(b, to)) for at, _b, to in fields}
    data, where = b'', {}
    for s in sorted(set(value.values()), key=lambda s: s.encode('utf-16le')):
        where[s] = lay.strings_at + len(data)
        data += s.encode('utf-16le') + b'\0\0'
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
