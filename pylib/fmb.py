"""EDF6 .FMB terrain-piece reader / height editor.

Layout and evidence: docs/fmb-format.md.  Short version: an FMB is a post-order binary AABB tree of
chunks.  Every leaf (kind 2) holds one CMPL blob; inside it the *render geometry* is a plain triangle
list -- float32 position + float32 normal per vertex (stride 0x18) and u16 indices -- plus a large
1 m splat (texture blend-weight) grid that has nothing to do with heights.  So heights are edited by
rewriting the vertex floats, then re-deriving normals, the leaf AABB (stored twice) and every node
AABB above it, re-compressing the leaf and re-laying out the file (chunks are 4-byte aligned, the
offset table and the trailing-table header offsets move with them).

API:
  decode(fmb) -> list[tuple[float, float, float]]        every vertex (x, y, z), world == model space
  parse(fmb) -> Fmb                                      leaves with vertices / triangles, nodes, layout
  set_heights(fmb, fn) -> bytes                          y := fn(x, y_old, z), everything kept consistent
  selftest(fmb) -> list[str]                             round-trip checks (raises AssertionError)

Commands:
  python pylib/fmb.py info FILE.FMB
  python pylib/fmb.py selftest FILE.FMB [--recompress N]  identity round trip byte for byte, plus N leaves
                                                          re-encoded with the full (slow) Okumura encoder
  python pylib/fmb.py obj FILE.FMB OUT.obj                 dump the render mesh as Wavefront OBJ
"""
from __future__ import annotations

import math
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Callable

from mdb import cmpl_compress, cmpl_decompress  # noqa: E402  (pylib)

MAGIC = b'FMB\0'
VERSION = 0x110
KIND_NODE = 0
KIND_LEAF_RAW = 1     # uncompressed leaf payload stored in place (handled by the loader; not seen in stock HEIGEN files)
KIND_LEAF_CMPL = 2
NODE_SIZE = 0x24
VERT_STRIDE = 0x18    # float3 position + float3 normal
MESH_STRIDE = 0x3C
HDR_OFFSET_FIELDS = (0x10, 0x14, 0x18, 0x20)   # absolute offsets into the trailing tables (move with the chunks)


# --------------------------------------------------------------------------------------------- float32


def f32(x: float) -> float:
    """Round a Python float to the nearest float32 (add/mul of two float32 values done in double and
    rounded once equals the float32 operation, so this reproduces the game tool's arithmetic)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def aabb_from_minmax(mn: list[float], mx: list[float]) -> tuple[float, ...]:
    """(centre xyz, half-extent xyz) exactly as the stock files store it: c=(mn+mx)*0.5, h=(mx-mn)*0.5 in float32."""
    c = [f32(f32(a + b) * 0.5) for a, b in zip(mn, mx)]
    h = [f32(f32(b - a) * 0.5) for a, b in zip(mn, mx)]
    return tuple(c + h)


# --------------------------------------------------------------------------------------------- CMPL decode


def cmpl_decode(data: bytes) -> bytes:
    """Same output as mdb.cmpl_decompress (and the game's 0x3F540), ~5x faster: ring-buffer offsets are
    turned into absolute back-references so non-overlapping copies become slice copies.  Like the game,
    it stops when the input (stored size - 8) is exhausted."""
    if data[:4] != b'CMPL':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    out = bytearray()
    i, n = 8, len(data)
    while i < n:
        flags = data[i]
        i += 1
        for bit in range(8):
            if i >= n:
                break
            if flags >> bit & 1:
                out.append(data[i])
                i += 1
                continue
            b0, b1 = data[i], data[i + 1]
            i += 2
            off, ln = (b0 << 4) | (b1 >> 4), (b1 & 0xF) + 3
            pos = len(out)
            d = ((0xFEE + pos) - off) & 0xFFF or 0x1000
            q = pos - d
            if q >= 0 and d >= ln:
                out += out[q:q + ln]
            else:
                for k in range(ln):
                    out.append(out[q + k] if q + k >= 0 else 0)
    if len(out) != size:
        raise ValueError(f'CMPL decoded {len(out)} bytes, header says {size}')
    return bytes(out)


# --------------------------------------------------------------------------------------------- model


@dataclass
class Mesh:
    """One draw batch of a leaf: u16 triangle list + a splat-weight grid (stride 0x50) + layer map."""
    base: int                      # offset of the 0x3C mesh record inside the leaf blob
    flags: int                     # +0x00 (2 in stock files)
    indices: list[int]             # +0x04 count, +0x08 rel offset -> u16
    grid: tuple[float | int, ...]  # +0x0C: cell x, cell z (f32), cols, rows (u32), origin x, origin z (f32), fmt, layers
    weight_count: int              # +0x2C count of 0x50-byte splat records, +0x34 rel offset
    weight_off: int
    layermap_count: int            # +0x30 count of 0x10-byte layer-index records, +0x38 rel offset
    layermap_off: int


@dataclass
class Leaf:
    index: int                     # chunk table index
    blob: bytes                    # decompressed (kind-1 layout) payload
    vert_off: int                  # offset of the vertex array inside blob
    verts: list[list[float]]       # [x, y, z, nx, ny, nz]
    meshes: list[Mesh]

    def triangles(self) -> list[tuple[int, int, int]]:
        out: list[tuple[int, int, int]] = []
        for m in self.meshes:
            ix = m.indices
            out += [(ix[k], ix[k + 1], ix[k + 2]) for k in range(0, len(ix) - 2, 3)]
        return out


@dataclass
class Chunk:
    index: int
    offset: int                    # in the source file
    end: int                       # end of its data (before alignment padding)
    kind: int
    aabb: tuple[float, ...]        # centre xyz, half xyz as stored in the chunk header
    children: tuple = ()           # node: (left_start, right_start); left child = right_start-1, right child = index-1
    stored: bytes = b''            # leaf: the CMPL stream as stored


@dataclass
class Fmb:
    raw: bytes
    count: int
    table_off: int
    chunks: list[Chunk]
    order: list[int]               # chunk indices in file order
    chunks_end: int                # end of the last chunk (aligned) == start of the trailing tables
    leaves: dict[int, Leaf] = field(default_factory=dict)


def _parse_leaf_blob(index: int, blob: bytes) -> Leaf:
    nv, voff, nm, moff = struct.unpack_from('<IiIi', blob, 0x20)
    verts = [list(struct.unpack_from('<6f', blob, voff + k * VERT_STRIDE)) for k in range(nv)]
    meshes = []
    for m in range(nm):
        b = moff + m * MESH_STRIDE
        flags, ni, io = struct.unpack_from('<IIi', blob, b)
        grid = struct.unpack_from('<2f2I2f2I', blob, b + 0x0C)
        wc, lc, wo, lo = struct.unpack_from('<IIii', blob, b + 0x2C)
        meshes.append(Mesh(b, flags, list(struct.unpack_from('<%dH' % ni, blob, b + io)), grid, wc, b + wo, lc, b + lo))
    return Leaf(index, blob, voff, verts, meshes)


def parse(fmb: bytes) -> Fmb:
    if fmb[:4] != MAGIC or struct.unpack_from('<I', fmb, 4)[0] != VERSION:
        raise ValueError('not an FMB v0x110')
    count, table_off = struct.unpack_from('<Ii', fmb, 8)
    offs = struct.unpack_from('<%dI' % count, fmb, table_off)
    chunks = []
    for i, o in enumerate(offs):
        kind = struct.unpack_from('<i', fmb, o)[0]
        aabb = struct.unpack_from('<6f', fmb, o + 4)
        if kind == KIND_NODE:
            chunks.append(Chunk(i, o, o + NODE_SIZE, kind, aabb, struct.unpack_from('<2i', fmb, o + 0x1C)))
        elif kind == KIND_LEAF_CMPL:
            rel, size = struct.unpack_from('<iI', fmb, o + 0x1C)
            chunks.append(Chunk(i, o, o + rel + size, kind, aabb, (), fmb[o + rel:o + rel + size]))
        elif kind == KIND_LEAF_RAW:
            chunks.append(Chunk(i, o, -1, kind, aabb))
        else:
            raise ValueError(f'chunk {i} @{o:#x}: unknown kind {kind:#x}')
    order = sorted(range(count), key=lambda i: offs[i])
    tail = struct.unpack_from('<I', fmb, 0x10)[0]
    for n, i in enumerate(order):        # raw leaves: data runs to the next chunk (or the trailing tables)
        if chunks[i].kind == KIND_LEAF_RAW:
            chunks[i].end = offs[order[n + 1]] if n + 1 < count else tail
    pos = _align4(table_off + 4 * count)
    for i in order:                      # the layout must be exactly: table, chunks (4-aligned, zero padded), tables
        c = chunks[i]
        if c.offset != pos or any(fmb[c.end:_align4(c.end)]):
            raise ValueError(f'unexpected layout at chunk {i} ({c.offset:#x}, expected {pos:#x})')
        pos = _align4(c.end)
    if pos != tail:
        raise ValueError(f'chunks end at {pos:#x}, header says {tail:#x}')
    f = Fmb(fmb, count, table_off, chunks, order, pos)
    for c in chunks:
        if c.kind == KIND_LEAF_CMPL:
            f.leaves[c.index] = _parse_leaf_blob(c.index, cmpl_decode(c.stored))
        elif c.kind == KIND_LEAF_RAW:
            f.leaves[c.index] = _parse_leaf_blob(c.index, fmb[c.offset:c.end])
    return f


def _align4(n: int) -> int:
    return (n + 3) & ~3


def decode(fmb: bytes) -> list[tuple[float, float, float]]:
    """Every render vertex of every leaf as world (x, y, z).  Vertices on leaf borders appear once per leaf."""
    return points(parse(fmb))


def points(f: Fmb) -> list[tuple[float, float, float]]:
    """Positions from an already decoded terrain, including its float32-quantized edits."""
    return [(v[0], v[1], v[2]) for i in sorted(f.leaves) for v in f.leaves[i].verts]


# --------------------------------------------------------------------------------------------- normals


def _weld_key(x: float, z: float) -> tuple[int, int]:
    # border vertices of neighbouring leaves differ by up to ~5e-4 (1624.9996 vs 1625.0); a 1 cm grid welds them
    return (round(x * 100.0), round(z * 100.0))


def compute_normals(leaves: list[Leaf], only: set[tuple[int, int]] | None = None) -> int:
    """Area-weighted vertex normals over the whole piece, welded across leaves by (x, z).
    The stock normals were baked from a denser source mesh (mean 0.2 deg / max 0.8 deg from this), so
    by default only vertices whose weld key is in `only` are rewritten.  Returns the number rewritten."""
    acc: dict[tuple[int, int], list[float]] = {}
    for lf in leaves:
        v = lf.verts
        for a, b, c in lf.triangles():
            p0, p1, p2 = v[a], v[b], v[c]
            ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
            wx, wy, wz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
            n = (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)
            if n[1] < 0:      # terrain faces up; the index winding is consistent but keep the sum robust
                n = (-n[0], -n[1], -n[2])
            for k in (a, b, c):
                s = acc.setdefault(_weld_key(v[k][0], v[k][2]), [0.0, 0.0, 0.0])
                s[0] += n[0]
                s[1] += n[1]
                s[2] += n[2]
    done = 0
    for lf in leaves:
        for v in lf.verts:
            key = _weld_key(v[0], v[2])
            if only is not None and key not in only:
                continue
            s = acc.get(key)
            if not s:
                continue
            ln = math.sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])
            if ln > 0:
                v[3], v[4], v[5] = f32(s[0] / ln), f32(s[1] / ln), f32(s[2] / ln)
                done += 1
    return done


# --------------------------------------------------------------------------------------------- CMPL splice


Item = tuple[int, int, int]   # (output pos, length, source pos); length 1 + source -1 = literal


def _cmpl_groups(stream: bytes) -> list[tuple[int, int, int, int]]:
    """(in_start, in_end, out_start, out_end) of every flag group (flag byte + up to 8 items)."""
    groups = []
    i, out, n = 8, 0, len(stream)
    while i < n:
        i0, o0 = i, out
        flags = stream[i]
        i += 1
        for bit in range(8):
            if i >= n:
                break
            if flags >> bit & 1:
                i += 1
                out += 1
            else:
                out += (stream[i + 1] & 0xF) + 3
                i += 2
        groups.append((i0, i, o0, out))
    return groups


def _dirty(old: bytes, new: bytes, block: int = 4096) -> list[tuple[int, int]]:
    """Byte ranges [a, b) where old and new differ (block-wise scan, then exact edges)."""
    runs: list[tuple[int, int]] = []
    for s in range(0, len(new), block):
        if old[s:s + block] == new[s:s + block]:
            continue
        e = min(s + block, len(new))
        k = s
        while k < e:
            if old[k] != new[k]:
                j = k
                while j < e and old[j] != new[j]:
                    j += 1
                if runs and runs[-1][1] == k:
                    runs[-1] = (runs[-1][0], j)
                else:
                    runs.append((k, j))
                k = j
            else:
                k += 1
    return runs


def _lzss_items(data: bytes, start: int, end: int) -> list[Item]:
    """Greedy LZSS items for data[start:end] with data[:start] already in the decoder's ring buffer.
    Only distances 1..4095 to bytes actually written are used, so the zeroed initial window is never read."""
    items: list[Item] = []
    pos = start
    while pos < end:
        best_len, best_src = 0, -1
        lo = max(0, pos - 4095)
        want = min(18, end - pos)
        if want >= 3:
            key = data[pos:pos + 3]
            s = data.rfind(key, lo, pos + 2)          # nearest candidate first
            while s >= lo:
                ln = 3
                while ln < want and data[s + ln] == data[pos + ln]:
                    ln += 1
                if ln > best_len:
                    best_len, best_src = ln, s
                    if ln == want:
                        break
                s = data.rfind(key, lo, s + 2) if s > lo else -1
        if best_len >= 3:
            items.append((pos, best_len, best_src))
            pos += best_len
        else:
            items.append((pos, 1, -1))
            pos += 1
    return items


def _pad_items(items: list[Item]) -> bool:
    """Split matches until len(items) % 8 == 0 (needed when original groups follow).  False if impossible."""
    while len(items) % 8:
        for k in range(len(items) - 1, -1, -1):
            p, ln, src = items[k]
            if ln >= 4:                                # peel the last byte off as a literal: +1 item
                items[k:k + 1] = [(p, ln - 1, src), (p + ln - 1, 1, -1)]
                break
        else:
            for k in range(len(items) - 1, -1, -1):
                p, ln, src = items[k]
                if ln == 3:                            # 3-byte match -> 3 literals: +2 items
                    items[k:k + 1] = [(p, 1, -1), (p + 1, 1, -1), (p + 2, 1, -1)]
                    break
            else:
                return False
    return True


def _emit(data: bytes, items: list[Item]) -> bytes:
    out = bytearray()
    for g in range(0, len(items), 8):
        flags = 0
        body = bytearray()
        for bit, (p, ln, src) in enumerate(items[g:g + 8]):
            if src < 0:
                flags |= 1 << bit
                body.append(data[p])
            else:
                off = (0xFEE + src) & 0xFFF
                body += bytes((off >> 4, ((off & 0xF) << 4) | (ln - 3)))
        out.append(flags)
        out += body
    return bytes(out)


def cmpl_reencode(old_stream: bytes, old: bytes, new: bytes, exact: bool = False) -> bytes:
    """CMPL stream for `new` (same length as `old`, whose CMPL stream is `old_stream`).
    Unchanged -> the original bytes.  exact -> full Okumura encoder (byte-identical to what the game tool
    would write, ~15 s per 1.26 MB leaf).  Otherwise an incremental re-encode: every original flag group
    whose output and 4 KB look-back window are unchanged is copied verbatim (it decodes to the same bytes),
    the rest is greedily re-encoded; a re-encoded run that is followed by copied groups is padded to whole
    8-item groups.  For a height edit only the payload AABB (+0x04) and the trailing vertex array change,
    so this touches a few KB and takes milliseconds.  The result decodes to exactly `new`."""
    if new == old:
        return old_stream
    if len(new) != len(old):
        raise ValueError('blob size changed')
    if exact:
        return cmpl_compress(new)
    dirty = _dirty(old, new)
    groups = _cmpl_groups(old_stream)

    def clean(g: tuple[int, int, int, int]) -> bool:
        lo = max(0, g[2] - 4096)
        return not any(a < g[3] and b > lo for a, b in dirty)

    ok = [clean(g) for g in groups]
    out = bytearray(old_stream[:8])
    k, n = 0, len(groups)
    while k < n:
        if ok[k]:
            out += old_stream[groups[k][0]:groups[k][1]]
            k += 1
            continue
        j = k
        while j < n and not ok[j]:
            j += 1
        while True:                       # re-encode groups[k:j]; grow into clean groups until padding fits
            items = _lzss_items(new, groups[k][2], groups[j - 1][3])
            if j == n or _pad_items(items):
                break
            j += 1
        out += _emit(new, items)
        k = j
    return bytes(out)


# --------------------------------------------------------------------------------------------- writer


def _leaf_blob(lf: Leaf) -> bytes:
    b = bytearray(lf.blob)
    mn = [min(v[k] for v in lf.verts) for k in range(3)]
    mx = [max(v[k] for v in lf.verts) for k in range(3)]
    struct.pack_into('<6f', b, 4, *aabb_from_minmax(mn, mx))
    for k, v in enumerate(lf.verts):
        struct.pack_into('<6f', b, lf.vert_off + k * VERT_STRIDE, *v)
    return bytes(b)


def _subtree_minmax(f: Fmb) -> dict[int, tuple[list[float], list[float]]]:
    mm: dict[int, tuple[list[float], list[float]]] = {}
    for c in f.chunks:   # post-order: children always precede their node
        if c.kind == KIND_NODE:
            left, right = mm[c.children[1] - 1], mm[c.index - 1]
            mm[c.index] = ([min(a, b) for a, b in zip(left[0], right[0])], [max(a, b) for a, b in zip(left[1], right[1])])
        else:
            v = f.leaves[c.index].verts
            mm[c.index] = ([min(p[k] for p in v) for k in range(3)], [max(p[k] for p in v) for k in range(3)])
    return mm


def build(f: Fmb, exact_cmpl: bool = False) -> bytes:
    """Serialise `f` (leaf vertices possibly edited) back to an FMB."""
    raw = f.raw
    mm = _subtree_minmax(f)
    pieces: dict[int, bytes] = {}
    for c in f.chunks:
        aabb = struct.pack('<6f', *aabb_from_minmax(*mm[c.index]))
        hdr = struct.pack('<i', c.kind) + aabb
        if c.kind == KIND_NODE:
            pieces[c.index] = hdr + raw[c.offset + 0x1C:c.end]
        elif c.kind == KIND_LEAF_RAW:
            pieces[c.index] = _leaf_blob(f.leaves[c.index])
        else:
            lf = f.leaves[c.index]
            stream = cmpl_reencode(c.stored, lf.blob, _leaf_blob(lf), exact_cmpl)
            rel = struct.unpack_from('<i', raw, c.offset + 0x1C)[0]
            pieces[c.index] = hdr + struct.pack('<iI', rel, len(stream)) + raw[c.offset + 0x24:c.offset + rel] + stream
    out = bytearray(raw[:_align4(f.table_off + 4 * f.count)])
    for i in f.order:
        struct.pack_into('<I', out, f.table_off + 4 * i, len(out))
        out += pieces[i]
        out += bytes(_align4(len(out)) - len(out))
    delta = len(out) - f.chunks_end
    for fo in HDR_OFFSET_FIELDS:
        v = struct.unpack_from('<I', raw, fo)[0]
        if f.chunks_end <= v <= len(raw):
            struct.pack_into('<I', out, fo, v + delta)
    out += raw[f.chunks_end:]
    return bytes(out)


def set_heights(fmb: bytes, fn: Callable[[float, float, float], float], normals: str = 'affected',
                exact_cmpl: bool = False, *, batch_fn: Callable | None = None) -> bytes:
    """New FMB with every vertex y replaced by float32(fn(x, y_old, z)).
    normals: 'affected' (default) recomputes normals of vertices touching a moved vertex's triangles,
    'all' recomputes every normal, 'none' keeps the stored ones.  Leaf + node AABBs are always re-derived
    (bit-identical to the stock values when nothing moved).  exact_cmpl: re-encode changed leaves with the
    full Okumura encoder instead of the fast tail splice (both decode identically in the game).
    batch_fn, when supplied, returns all new heights for one leaf's vertices in their original order."""
    f = parse(fmb)
    apply_heights(f, fn, normals, batch_fn=batch_fn)
    return build(f, exact_cmpl)


def apply_heights(f: Fmb, fn: Callable[[float, float, float], float], normals: str = 'affected',
                  *, batch_fn: Callable | None = None) -> None:
    """Edit decoded terrain once; keep the same float32 rounding, normals and AABB builder."""
    leaves = [f.leaves[i] for i in sorted(f.leaves)]
    moved: set[tuple[int, int]] = set()
    for lf in leaves:
        heights = list(batch_fn(lf.verts)) if batch_fn is not None and lf.verts else [fn(v[0], v[1], v[2]) for v in lf.verts]
        if len(heights) != len(lf.verts):
            raise ValueError('batch height count differs from vertex count')
        for v, height in zip(lf.verts, heights):
            y = f32(float(height))
            if struct.pack('<f', y) != struct.pack('<f', v[1]):
                v[1] = y
                moved.add(_weld_key(v[0], v[2]))
    if normals == 'all':
        compute_normals(leaves)
    elif normals == 'affected' and moved:
        touched: set[tuple[int, int]] = set()
        for lf in leaves:
            v = lf.verts
            for tri in lf.triangles():
                keys = [_weld_key(v[k][0], v[k][2]) for k in tri]
                if any(k in moved for k in keys):
                    touched.update(keys)
        compute_normals(leaves, touched)
    elif normals not in ('affected', 'none'):
        raise ValueError(normals)


# --------------------------------------------------------------------------------------------- checks


def selftest(fmb: bytes, recompress: int = 0) -> list[str]:
    """Raises AssertionError on failure; returns human-readable result lines."""
    log: list[str] = []
    t = time.time()
    f = parse(fmb)
    nv = sum(len(lf.verts) for lf in f.leaves.values())
    log.append(f'parsed {f.count} chunks ({len(f.leaves)} leaves, {nv} vertices) in {time.time() - t:.1f}s')
    mm = _subtree_minmax(f)
    bad = [c.index for c in f.chunks if struct.pack('<6f', *aabb_from_minmax(*mm[c.index])) != struct.pack('<6f', *c.aabb)]
    bad += [i for i, lf in f.leaves.items() if lf.blob[4:28] != struct.pack('<6f', *f.chunks[i].aabb)]
    assert not bad, f'AABB formula mismatch at chunks {bad[:10]}'
    log.append('every leaf/node AABB == float32 (min+max)*0.5, (max-min)*0.5 of its subtree vertices (bit exact)')
    out = set_heights(fmb, lambda x, y, z: y)
    assert out == fmb, 'identity set_heights did not reproduce the file'
    log.append(f'identity set_heights: {len(out)} bytes, byte-identical')
    leaf_ids = sorted(f.leaves)
    for i in leaf_ids[:recompress]:
        t = time.time()
        assert cmpl_compress(f.leaves[i].blob) == f.chunks[i].stored, f'Okumura re-encode differs at leaf {i}'
        log.append(f'leaf {i}: full Okumura re-encode byte-identical ({time.time() - t:.1f}s)')
    # global edit: +5 m everywhere.  parse() re-validates the whole layout (table, alignment, header +0x10).
    t = time.time()
    out = set_heights(fmb, lambda x, y, z: y + 5.0)
    took = time.time() - t
    up = parse(out)
    for i in leaf_ids:
        assert cmpl_decompress(up.chunks[i].stored) == up.leaves[i].blob, f'leaf {i}: reference decoder disagrees'
        for a, b in zip(f.leaves[i].verts, up.leaves[i].verts):
            assert b[1] == f32(a[1] + 5.0) and b[0] == a[0] and b[2] == a[2], f'leaf {i}: vertex not moved by 5'
    for c in f.chunks:
        assert abs(up.chunks[c.index].aabb[1] - c.aabb[1] - 5.0) < 1e-3 and abs(up.chunks[c.index].aabb[4] - c.aabb[4]) < 1e-3, \
            f'chunk {c.index}: AABB not shifted'
    dev = max(math.degrees(math.acos(min(1.0, sum(p * q for p, q in zip(a[3:], b[3:])))))
              for i in leaf_ids for a, b in zip(f.leaves[i].verts, up.leaves[i].verts))
    log.append(f'+5 m edit: {len(out)} bytes (was {len(fmb)}), built in {took:.1f}s; every leaf decodes with the '
               f'reference decoder; all vertices/AABBs moved by 5; recomputed normals vs stock max {dev:.2f} deg')
    # local edit: a 20 m bump of radius 150 m around one vertex -> only nearby leaves and normals change
    cx, _, cz = f.leaves[leaf_ids[len(leaf_ids) // 2]].verts[0][:3]
    bump = parse(set_heights(fmb, lambda x, y, z: y + 20.0 * max(0.0, 1.0 - math.hypot(x - cx, z - cz) / 150.0)))
    same = sum(bump.chunks[i].stored == f.chunks[i].stored for i in leaf_ids)
    nchg = sum(a[3:] != b[3:] for i in leaf_ids for a, b in zip(f.leaves[i].verts, bump.leaves[i].verts))
    ychg = sum(a[1] != b[1] for i in leaf_ids for a, b in zip(f.leaves[i].verts, bump.leaves[i].verts))
    assert 0 < same < len(leaf_ids) and ychg > 0
    log.append(f'local bump at ({cx:.0f}, {cz:.0f}): {ychg} vertices moved, {nchg} normals rewritten, '
               f'{same}/{len(leaf_ids)} leaf streams untouched')
    return log


def info(fmb: bytes) -> list[str]:
    f = parse(fmb)
    h = struct.unpack_from('<11I', fmb, 0)
    pts = decode(fmb)
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    nt = sum(len(lf.triangles()) for lf in f.leaves.values())
    out = [f'header: count {h[2]} table@{h[3]:#x} tables@{h[4]:#x},{h[5]:#x},{h[6]:#x} n={h[7]:#x} @{h[8]:#x} n={h[9]:#x} maxblob {h[10]:#x}',
           f'{len(f.leaves)} leaves, {f.count - len(f.leaves)} nodes, {len(pts)} vertices, {nt} triangles',
           f'bounds x {lo[0]:.2f}..{hi[0]:.2f}  y {lo[1]:.2f}..{hi[1]:.2f}  z {lo[2]:.2f}..{hi[2]:.2f}']
    for i in sorted(f.leaves)[:3]:
        lf = f.leaves[i]
        out.append(f'leaf {i}: {len(lf.verts)} verts, meshes ' + ', '.join(
            f'{len(m.indices)} idx grid {m.grid[2]}x{m.grid[3]} @({m.grid[4]:g},{m.grid[5]:g})' for m in lf.meshes))
    return out


def to_obj(fmb: bytes) -> str:
    f = parse(fmb)
    lines, base = [], 1
    for i in sorted(f.leaves):
        lf = f.leaves[i]
        lines.append(f'o leaf{i}')
        lines += [f'v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}' for v in lf.verts]
        lines += [f'vn {v[3]:.6f} {v[4]:.6f} {v[5]:.6f}' for v in lf.verts]
        lines += [f'f {a + base}//{a + base} {b + base}//{b + base} {c + base}//{c + base}' for a, b, c in lf.triangles()]
        base += len(lf.verts)
    return '\n'.join(lines) + '\n'


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[0] not in ('info', 'selftest', 'obj'):
        print(__doc__)
        return 2
    data = open(argv[1], 'rb').read()
    if argv[0] == 'info':
        print('\n'.join(info(data)))
    elif argv[0] == 'selftest':
        n = int(argv[argv.index('--recompress') + 1]) if '--recompress' in argv else 0
        for line in selftest(data, n):
            print(line)
        print('SELFTEST OK')
    else:
        open(argv[2], 'w').write(to_obj(data))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
