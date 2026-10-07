"""A weapon's MAB block (its SGO's animation_model[2]) read into its locator lists and written back, so locators can be
added (vcobjects.mab_muzzles / mab_locator only move the ones there, in place).

    mab_read(b) -> Mab       ValueError on a block of another shape than the one below
    mab_write(m) -> bytes    the block; mab_write(mab_read(b)) == b for every stock block of that shape (checked)

The block (EDF.dll's weapon constructor 0x68B62F..0x68BC97 walks list 0, the muzzles; vcobjects.mab_muzzles):
  0x00 b'MAB\\0', u32 0x04 / 0x08 (0x0F / 0x83 in every stock block), u16 0x0C the lists' count, u16 0x0E, u16 0x10
       the vec4s' count, u16 0x12 the strings' bytes, u32 0x14 the lists' headers, 0x18 the records' end, 0x1C the
       vec4s, 0x20 the strings;
  the lists' headers, 8 bytes each: u16 its kind, u16 its records' count, i32 its first record (from the header);
  the records, 0x20 each, every list's after the one before: i32 +0 its name and +4 its node (UTF-16, from the
       record), i32 +8 a kind, i32 +0xC its position, +0x10 and +0x14 two more vec4s (from the record), i32 +0x18,
       i32 +0x1C an SGO block of its own (from the record; 0 none); then 0xBA up to 16 bytes;
  the vec4s every record uses, each once, in the order of their bytes (four u32s, x first); the records' SGO blocks,
       each once, in the order the records first use them; the strings, each once, NUL-ended, in UTF-16 order.
Only a block with u16 0x0E zero (non-zero: a table more, not read) whose records are all in list 0 is read (the
Naegling launcher's, which tools/make_katyusha.py rewrites, is one); every such block in Root.cpk (176 of them,
2026-10-07) comes back byte for byte from mab_write(mab_read(b)): the writer lays a block out as the game's own are.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

Vec4 = tuple[float, float, float, float]
RECORD = 0x20
PAD = 0xBA


@dataclass
class Locator:
    name: str
    node: str
    kind: int
    pos: Vec4
    b: Vec4                # +0x10 (a muzzle's: (0.05, 0.05, 0.25, 1) in the Naegling's)
    c: Vec4                # +0x14 ((0, 0, 0, 1))
    d: int                 # +0x18 (0)
    sgo: bytes | None      # +0x1C: its SGO block


@dataclass
class Mab:
    head: tuple[int, int, int]                  # u32 0x04, u32 0x08, u16 0x0E (0)
    kinds: list[int]                            # every list's kind, in order
    locators: list[Locator] = field(default_factory=list)   # list 0's


def _text(b: bytes, at: int) -> str:
    end = at
    while b[end:end + 2] != b'\0\0':
        end += 2
    return b[at:end].decode('utf-16le')


def _vec(b: bytes, at: int) -> Vec4:
    return struct.unpack_from('<4f', b, at)  # type: ignore[return-value]


def mab_read(b: bytes) -> Mab:
    if b[:4] != b'MAB\0':
        raise ValueError('不是 MAB 块')
    v4, v8, nlists, h0e, nvec, nstr, heads, end, vecs, strings = struct.unpack_from('<IIHHHHIIII', b, 4)
    lists = [struct.unpack_from('<HHi', b, heads + 8 * k) for k in range(nlists)]
    if h0e:
        raise ValueError(f'MAB 的 0x0E 是 {h0e}（另有一种表，没有解）：不认识这种块')
    if nlists < 1 or any(n for _kind, n, _first in lists[1:]):
        raise ValueError('MAB 除了第一张表还有别的定位点：不认识这种块')
    first = heads + lists[0][2]
    if first != heads + 8 * nlists or first + RECORD * lists[0][1] != end:
        raise ValueError('MAB 的记录不紧跟在表头后')
    if vecs + 16 * nvec > strings or strings + nstr != len(b):
        raise ValueError('MAB 的坐标区 / 字符串区大小不对')
    out = Mab((v4, v8, h0e), [k for k, _n, _f in lists])
    # The SGO blocks lie between the vec4s and the strings, each up to the next one.
    starts = sorted({r + struct.unpack_from('<i', b, r + 0x1C)[0] for r in range(first, end, RECORD)
                     if struct.unpack_from('<i', b, r + 0x1C)[0]} | {strings})
    if starts[0] < vecs + 16 * nvec or any(b[a:a + 4] != b'SGO\0' for a in starts[:-1]):
        raise ValueError('MAB 记录的 SGO 块不在坐标区和字符串区之间')
    for r in range(first, end, RECORD):
        name, node, kind, pos, vb, vc, d, sgo = struct.unpack_from('<8i', b, r)
        for at in (r + pos, r + vb, r + vc):
            if not vecs <= at < vecs + 16 * nvec or (at - vecs) % 16:
                raise ValueError(f'MAB 记录 {r:#x} 的坐标不在坐标区')
        blob = b[r + sgo:starts[starts.index(r + sgo) + 1]] if sgo else None
        out.locators.append(Locator(_text(b, r + name), _text(b, r + node), kind, _vec(b, r + pos), _vec(b, r + vb),
                                    _vec(b, r + vc), d, blob))
    return out


def _vec_key(v: Vec4) -> tuple[int, ...]:
    return struct.unpack('<4I', struct.pack('<4f', *v))


def mab_write(m: Mab) -> bytes:
    heads = 0x24
    first = heads + 8 * len(m.kinds)
    end = first + RECORD * len(m.locators)
    vecs = (end + 15) // 16 * 16
    vec4s = sorted({_vec_key(v) for x in m.locators for v in (x.pos, x.b, x.c)})
    vec_at = {k: vecs + 16 * i for i, k in enumerate(vec4s)}
    blobs: list[bytes] = []
    for x in m.locators:
        if x.sgo is not None and x.sgo not in blobs:
            blobs.append(x.sgo)
    blob_at, at = {}, vecs + 16 * len(vec4s)
    for blob in blobs:
        blob_at[blob] = at
        at += len(blob)
    strings = at
    text = sorted({s for x in m.locators for s in (x.name, x.node)}, key=lambda s: s.encode('utf-16le'))
    str_at, data = {}, b''
    for s in text:
        str_at[s] = strings + len(data)
        data += s.encode('utf-16le') + b'\0\0'
    out = bytearray(strings + len(data))
    v4, v8, h0e = m.head
    struct.pack_into('<4sIIHHHHIIII', out, 0, b'MAB\0', v4, v8, len(m.kinds), h0e, len(vec4s), len(data),
                     heads, end, vecs, strings)
    for k, kind in enumerate(m.kinds):
        n = len(m.locators) if k == 0 else 0
        struct.pack_into('<HHi', out, heads + 8 * k, kind, n, (first if k == 0 else end) - (heads + 8 * k))
    for i, x in enumerate(m.locators):
        r = first + RECORD * i
        sgo = blob_at[x.sgo] - r if x.sgo is not None else 0
        struct.pack_into('<8i', out, r, str_at[x.name] - r, str_at[x.node] - r, x.kind, vec_at[_vec_key(x.pos)] - r,
                         vec_at[_vec_key(x.b)] - r, vec_at[_vec_key(x.c)] - r, x.d, sgo)
    out[end:vecs] = bytes([PAD]) * (vecs - end)
    for k, a in vec_at.items():
        struct.pack_into('<4I', out, a, *k)
    for blob, a in blob_at.items():
        out[a:a + len(blob)] = blob
    out[strings:] = data
    return bytes(out)
