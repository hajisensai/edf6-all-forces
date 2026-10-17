"""A new EDF6 .FMB terrain piece from a heightfield (the SK maps of EDF4.1, tools/edf41_maps.py).

Layout and evidence: docs/fmb-format.md (the reader / editor is pylib/fmb.py), docs/edf41-sk-terrain.md (what this
writer adds). The file is built the way EDF6's own regular-grid pieces are (IG_HAIKYOPLANE01 is the template: every
leaf a square block of grid cells, one draw batch, the splat "mass" grid, the layer list), so nothing here invents a
field: each is either computed (AABBs, counts, offsets), copied from the donor piece (the two constant trailing
tables), or has a stock value (flags 1 / 2, the 0x3C mesh record layout).

  build(donor, heights, weights, x0, z0, step, layers) -> bytes

heights (rows, cols) float: the vertex lattice, vertex (i, j) at x0 + j * step, z0 + i * step, y = heights[i, j]
weights (rows, cols, L) float: blend weights of the L layers at every vertex
layers: the L layers' texture slices and tiling (Layer); see layer_table for how they become EDF6's records.

Shading: EDF6's terrain pixel shader (MAP_FIELD_RENDER*_14_*, in EDF6's DX11.cpk) takes the layer weights from the
mass grid: one 0x50 record per cell = the list index (u32) + four corners' 16 weight bytes (slot 0..15 = the layers
of the leaf's list, 0xFF = unused slot); the four corners are (i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1) with i
the column (x). A leaf's mesh record says how many slots its list has (+0x24, mesh_flags) and which record of the
file's shader table (header +0x20, shader_table) draws it (+0x28, shader_index).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

import numpy as np

import fmb

MAGIC = fmb.MAGIC
VERSION = fmb.VERSION
LEAF_CELLS = 20          # cells per leaf side (21 x 21 vertices)
HEADER = 0x2C
MESH_SIZE = fmb.MESH_STRIDE
LAYER_SIZE = 0x50
ORI_SIZE = 0x20
SPLAT_SIZE = 0x50
SLOTS = 16
SHADER_AT = 0x28   # mesh record: the shader table record of the leaf (shader_index)
MAX_LAYERS = 5   # layers per leaf: the most this writer lists (DX11.cpk's MAP_FIELD_RENDER<layers>_<mode>_14_<pass> go to 15 layers, modes 1..7)


@dataclass(frozen=True)
class Layer:
    """One terrain layer: slice `color` of the map's colour array tiled every `tile_color` metres, multiplied by
    slice `multip` (-1: none) tiled every `tile_multip` metres, shaded with normal slice `normal`."""
    color: int
    multip: int
    tile_color: float
    tile_multip: float
    normal: int = 0


# ----------------------------------------------------------------------------------------------- CMPL, fast


def _lzss_items(data: bytes, tries: int = 24) -> list[fmb.Item]:
    """Greedy LZSS items for the whole of `data`, looking at the nearest `tries` earlier occurrences of each 3 byte
    key only (fmb._lzss_items looks at every one in the 4 KB window: 0.5 s a leaf on the splat grids' zero runs)."""
    items: list[fmb.Item] = []
    pos, n = 0, len(data)
    while pos < n:
        best_len, best_src = 0, -1
        want = min(18, n - pos)
        if want >= 3:
            key = data[pos:pos + 3]
            lo = max(0, pos - 4095)
            s = data.rfind(key, lo, pos + 2)
            left = tries
            while s >= lo and left:
                ln = 3
                while ln < want and data[s + ln] == data[pos + ln]:
                    ln += 1
                if ln > best_len:
                    best_len, best_src = ln, s
                    if ln == want:
                        break
                s = data.rfind(key, lo, s + 2) if s > lo else -1
                left -= 1
        if best_len >= 3:
            items.append((pos, best_len, best_src))
            pos += best_len
        else:
            items.append((pos, 1, -1))
            pos += 1
    return items


def cmpl_fast(data: bytes) -> bytes:
    """A CMPL stream (the game's LZSS, docs/fmb-format.md) of `data`: greedy matches, not Okumura's choice; it
    decodes to exactly `data` with the game's decoder (fmb.cmpl_decode, mdb.cmpl_decompress: checked by selftest)."""
    return b'CMPL' + struct.pack('>I', len(data)) + fmb._emit(data, _lzss_items(data))


# ----------------------------------------------------------------------------------------------- leaves


def _corner_weights(w: np.ndarray, used: list[int]) -> np.ndarray:
    """(rows, cols, 16) uint8: the used layers' weights per vertex in slot order, each vertex summing to 255
    (largest remainder), the unused slots 0."""
    out = np.zeros(w.shape[:2] + (SLOTS,), dtype=np.int64)
    sel = w[:, :, used].astype(np.float64)
    tot = sel.sum(axis=2, keepdims=True)
    sel = np.where(tot > 0, sel / np.maximum(tot, 1e-12), 0.0)
    sel[(tot[:, :, 0] <= 0)] = 0.0
    sel[tot[:, :, 0] <= 0, 0] = 1.0       # nothing painted: the first layer of the list
    raw = sel * 255.0
    base = np.floor(raw).astype(np.int64)
    short = 255 - base.sum(axis=2)
    frac = raw - base
    order = np.argsort(-frac, axis=2)
    rank = np.argsort(order, axis=2)
    base += (rank < short[:, :, None]).astype(np.int64)
    out[:, :, :len(used)] = base
    return out.astype(np.uint8)


def mesh_flags(used: list[int]) -> int:
    """The dword at mesh +0x24 (the dword at +0 is the leaf's layer count): (layers in the mesh << 8) | (slots of its
    longest list); one list per leaf here, so both are the layers used. The dword after it (+0x28) is the leaf's
    shader: shader_index."""
    n = len(used)
    return n * 0x100 + n


def shader_index(counts: list[int], n: int) -> int:
    """Mesh +0x28: the index into the file's shader table (shader_table(counts), header +0x20) of the record
    (n, 1, pass 0, 0) - the pass-0 draw of an n-layer leaf. Every one of the 9753 meshes of EDF6's 46 stock FMBs points
    at the record (layers << 8 of its +0x24, mode, pass 0, flag 0) of its own file's table; none at a flag 6 record or
    another pass. (This was once read as the number of texture slices the leaf's layers use, which happened to fit the
    two stock files it was read off; the SK pieces built that way pointed at flag 6 records or other layer counts.)"""
    cs = sorted(set(counts))
    return len(cs) + 1 + cs.index(n)


def leaf_blob(heights: np.ndarray, normals: np.ndarray, weights: np.ndarray, x0: float, z0: float, step: float,
              r0: int, c0: int, cells: tuple[int, int], layers: list[Layer], list_ids: list[int]) -> tuple[bytes, list[int]]:
    """One leaf (the lattice cells r0.. , c0.. of the given size) as its decompressed payload, and its layer list."""
    nr, nc = cells
    h = heights[r0:r0 + nr + 1, c0:c0 + nc + 1]
    n = normals[r0:r0 + nr + 1, c0:c0 + nc + 1]
    w = weights[r0:r0 + nr + 1, c0:c0 + nc + 1]
    total = w.reshape(-1, w.shape[2]).sum(0)
    ranked = sorted((k for k in list_ids if w[:, :, k].max() > 1.0 / 510.0), key=lambda k: -total[k])
    used = sorted(ranked[:MAX_LAYERS]) or [list_ids[int(total.argmax())]]
    rows, cols = h.shape
    xs = x0 + (c0 + np.arange(cols)) * step
    zs = z0 + (r0 + np.arange(rows)) * step
    verts = np.zeros((rows, cols, 6), dtype='<f4')
    verts[:, :, 0] = xs[None, :]
    verts[:, :, 1] = h
    verts[:, :, 2] = zs[:, None]
    verts[:, :, 3:] = n
    verts = verts.reshape(-1, 6)
    # quad (r, c): A (c, r) B (c + 1, r) C (c, r + 1) D (c + 1, r + 1); the stock winding (u x w points down in
    # (x, z) as the stock files' do): A B D and A D C
    ii, jj = np.meshgrid(np.arange(nr), np.arange(nc), indexing='ij')
    a = (ii * cols + jj).ravel()
    b = a + 1
    c = a + cols
    d = c + 1
    idx = np.stack([a, b, d, a, d, c], axis=1).ravel().astype('<u2')
    ntri = len(idx) // 3
    # splat grid: one record per cell (list 0), the four corners
    corner = _corner_weights(w, used)
    rec = np.zeros((nr, nc, 5, 16), dtype=np.uint8)
    rec[:, :, 1] = corner[:-1, :-1]
    rec[:, :, 2] = corner[:-1, 1:]
    rec[:, :, 3] = corner[1:, :-1]
    rec[:, :, 4] = corner[1:, 1:]
    splat = rec.reshape(-1, 5 * 16)
    layermap = bytes(used + [0xFF] * (SLOTS - len(used)))
    flags = mesh_flags(used)
    mn = verts[:, :3].min(0)
    mx = verts[:, :3].max(0)
    box = fmb.aabb_from_minmax([float(v) for v in mn], [float(v) for v in mx])
    base = 0x30
    idx_at = base + MESH_SIZE
    w_at = idx_at + idx.nbytes
    lm_at = w_at + SPLAT_SIZE * nr * nc
    v_at = lm_at + len(layermap)
    out = bytearray()
    out += struct.pack('<I6fIIiIi', 1, *box, ntri, len(verts), v_at, 1, base)
    mesh = struct.pack('<IIi2f2I2f2I2I2i', len(used), len(idx), idx_at - base, step, step, nc, nr,
                       x0 + c0 * step, z0 + r0 * step, flags, 0, nr * nc, 1, w_at - base, lm_at - base)
    assert len(mesh) == MESH_SIZE, len(mesh)
    out += mesh
    out += idx.tobytes()
    out += splat.tobytes()
    out += layermap
    out += verts.tobytes()
    return bytes(out), used


# ----------------------------------------------------------------------------------------------- the file


def lattice_normals(h: np.ndarray, step: float) -> np.ndarray:
    """Unit normals of the lattice from central differences (one sided at the border)."""
    gz, gx = np.gradient(h, step)
    n = np.stack([-gx, np.ones_like(h), -gz], axis=2)
    return (n / np.linalg.norm(n, axis=2, keepdims=True)).astype(np.float32)


def layer_table(layers: list[Layer]) -> tuple[bytes, list[float]]:
    """The trailing layer table (donor header +0x10): [N, 0x10, M, offset] + N records of 0x50 + M orientation records
    of 0x20, and the M tile sizes. A record: orientation of the colour / multiplier tile (the stock's first two
    dwords), three unused axis pairs -1, paint mode 0, colour slice, -1, -1, multiplier slice, -1, -1, normal slice,
    -1, -1, the 6.0 every stock layer has, 1.0 x 3 (as the single layer pieces). An orientation: rotation 90 degrees
    about x (the xz plane), the tile size in metres on all axes, no offset."""
    tiles: list[float] = []
    for l in layers:
        for t in (l.tile_color, l.tile_multip if l.multip >= 0 else l.tile_color):
            if t not in tiles:
                tiles.append(t)
    recs = bytearray()
    for l in layers:
        recs += struct.pack('<20i', tiles.index(l.tile_color), tiles.index(l.tile_multip if l.multip >= 0 else l.tile_color), -1, -1, -1, -1,
                            0, l.color, -1, -1, l.multip, -1, -1, l.normal, -1, -1, 0, 0, 0, 0)
        struct.pack_into('<4f', recs, len(recs) - 16, 6.0, 1.0, 1.0, 1.0)
    oris = bytearray()
    for t in tiles:
        oris += struct.pack('<8f', 90.0, 0.0, 0.0, t, t, t, 0.0, 0.0)
    head = struct.pack('<4I', len(layers), 16, len(tiles), 16 + LAYER_SIZE * len(layers))
    return head + bytes(recs) + bytes(oris), tiles


def shader_table(counts: list[int]) -> bytes:
    """Donor header +0x20 table (0x15 records in the 4-layer pieces, 9 in the single layer ones): per pass 0 / 1 / 2
    the slot counts of the leaves at flag 6, the smallest again at 6, then the counts at flag 0 (stock
    ENKEILEFT: counts 2, 3, 4 -> (2,3,4 @6), (2 @6), (2,3,4 @0) x 3 passes = 21; haikyo: count 1 -> 9)."""
    cs = sorted(set(counts))
    out = bytearray()
    for idx in range(3):
        for c in cs:
            out += struct.pack('<7I', c, 1, idx, 6, 0, 0, 0)
        out += struct.pack('<7I', cs[0], 1, idx, 6, 0, 0, 0)
        for c in cs:
            out += struct.pack('<7I', c, 1, idx, 0, 0, 0, 0)
    return bytes(out)


def _aligned(n: int) -> int:
    return (n + 3) & ~3


def build(donor: bytes, heights: np.ndarray, weights: np.ndarray, x0: float, z0: float, step: float,
          layers: list[Layer], leaf_cells: int = LEAF_CELLS) -> bytes:
    """The FMB of the lattice (see the module doc). `donor` is a stock single-layer piece with the two constant
    trailing tables (IG_HAIKYOPLANE01.FMB)."""
    rows, cols = heights.shape
    if (rows - 1) % leaf_cells or (cols - 1) % leaf_cells:
        raise ValueError('the lattice does not divide into leaves')
    normals = lattice_normals(heights, step)
    ny, nx = (rows - 1) // leaf_cells, (cols - 1) // leaf_cells
    all_layers = list(range(len(layers)))
    blobs: dict[tuple[int, int], tuple[bytes, list[int], tuple]] = {}
    for i in range(ny):
        for j in range(nx):
            blob, used = leaf_blob(heights, normals, weights, x0, z0, step, i * leaf_cells, j * leaf_cells,
                                   (leaf_cells, leaf_cells), layers, all_layers)
            lo = (x0 + j * leaf_cells * step, float(heights[i * leaf_cells:(i + 1) * leaf_cells + 1,
                                                              j * leaf_cells:(j + 1) * leaf_cells + 1].min()),
                  z0 + i * leaf_cells * step)
            blobs[(i, j)] = (blob, used, lo)
    # the post order AABB tree: split the leaf rectangle along its longer side
    chunks: list[tuple[int, tuple[int, int] | None, int, int, np.ndarray, np.ndarray]] = []
    # (kind, leaf key, left_start, right_start, min, max)

    def leaf_box(key: tuple[int, int]) -> tuple[np.ndarray, np.ndarray]:
        blob = blobs[key][0]
        vo = struct.unpack_from('<i', blob, 0x24)[0]
        nv = struct.unpack_from('<I', blob, 0x20)[0]
        v = np.frombuffer(blob, dtype='<f4', count=6 * nv, offset=vo).reshape(-1, 6)[:, :3]
        return v.min(0).astype(np.float64), v.max(0).astype(np.float64)

    def emit(i0: int, i1: int, j0: int, j1: int) -> tuple[int, int, np.ndarray, np.ndarray]:
        """(root chunk, first chunk, min, max) of the leaves i0..i1-1 x j0..j1-1."""
        if i1 - i0 == 1 and j1 - j0 == 1:
            mn, mx = leaf_box((i0, j0))
            chunks.append((fmb.KIND_LEAF_CMPL, (i0, j0), 0, 0, mn, mx))
            return len(chunks) - 1, len(chunks) - 1, mn, mx
        if i1 - i0 >= j1 - j0:
            m = (i0 + i1) // 2
            l, r = (i0, m, j0, j1), (m, i1, j0, j1)
        else:
            m = (j0 + j1) // 2
            l, r = (i0, i1, j0, m), (i0, i1, m, j1)
        _, ls, lmn, lmx = emit(*l)
        _, rs, rmn, rmx = emit(*r)
        mn, mx = np.minimum(lmn, rmn), np.maximum(lmx, rmx)
        chunks.append((fmb.KIND_NODE, None, ls, rs, mn, mx))
        return len(chunks) - 1, ls, mn, mx

    emit(0, ny, 0, nx)
    count = len(chunks)
    counts = [len(b[1]) for b in blobs.values()]
    for key, (blob, used, lo) in blobs.items():      # each leaf's shader record (mesh +0x28), now that all are known
        b = bytearray(blob)
        struct.pack_into('<I', b, 0x30 + SHADER_AT, shader_index(counts, len(used)))
        blobs[key] = (bytes(b), used, lo)
    streams = {key: cmpl_fast(b[0]) for key, b in blobs.items()}
    pieces: list[bytes] = []
    for kind, key, ls, rs, mn, mx in chunks:
        box = fmb.aabb_from_minmax([float(v) for v in mn], [float(v) for v in mx])
        hdr = struct.pack('<i6f', kind, *box)
        if kind == fmb.KIND_NODE:
            pieces.append(hdr + struct.pack('<ii', ls, rs) + bytes(fmb.NODE_SIZE - 0x24))
        else:
            stream = streams[key]
            pieces.append(hdr + struct.pack('<iI', 0x24, len(stream)) + stream)
    # patch the leaf payload AABB (+4) with the chunk's: build did it from the vertices, the same rule
    table_at = HEADER
    pos = _aligned(table_at + 4 * count)
    offsets = []
    body = bytearray()
    for p in pieces:
        offsets.append(pos + len(body))
        body += p
        body += bytes(_aligned(len(body)) - len(body))
    tail = pos + len(body)
    t1, _ = layer_table(layers)
    t4 = shader_table(counts)
    t2 = donor[struct.unpack_from('<I', donor, 0x14)[0]:struct.unpack_from('<I', donor, 0x18)[0]]
    t3 = donor[struct.unpack_from('<I', donor, 0x18)[0]:struct.unpack_from('<I', donor, 0x20)[0]]
    t2_at = tail + len(t1)
    t3_at = t2_at + len(t2)
    t4_at = t3_at + len(t3)
    maxblob = max(len(b[0]) for b in blobs.values())
    head = struct.pack('<4sIII7I', MAGIC, VERSION, count, HEADER, tail, t2_at, t3_at, 0x100, t4_at, len(t4) // 28,
                       maxblob)
    out = bytearray(head)
    assert len(out) == HEADER
    out += struct.pack('<%dI' % count, *offsets)
    out += bytes(pos - len(out))
    out += body
    out += t1 + t2 + t3 + t4
    return bytes(out)
