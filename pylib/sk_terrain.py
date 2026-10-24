"""EDF4.1's SK terrain (`<model>_fld2mgrid.shfb`, the heightfield of the sk_* mission maps) read into arrays.

EDF6 has no counterpart of this class (EDF.dll holds no SHFB loader; its terrain is an FMB mesh, docs/fmb-format.md):
tools/edf41_maps.py turns the field into EDF6 terrain (pylib/fmb_build.py, pylib/hkcms_build.py). Layout, read from
EDF41.exe (the endian fixup 0x1400F37A0 names every field, the terrain constructor 0x1400F4B00 and the vertex shader
embedded at 0x9C6030 say what the arrays are) and checked against the placed objects of SK_HEIGEN02:

  0x00 'MAPB', 0x04 3 (the same magic as a map's mapb, a different file)
  0x08 / 0x0C   samples in x / z (1001: 2000 m at 2 m)         0x10 / 0x14  row stride of the stored field (1025)
  0x18 / 0x1C   cell size in x / z (2.0 m)                     0x20 / 0x24  blocks in x / z (33)
  0x28 / 0x2C   cells per block side (32)                      0x30 / 0x34  lowest / highest height (f32)
  0x38 ...        offset / count pairs (offsets from the file start; the offsets inside the structs are self relative):
    0x38  blocks (33 x 33 x 0x20): two f32, 32, 32, then offsets to the block's two u32 lists (the per-vertex start
          index into the layer list, 33 x 33 each)
    0x3C / 0x40   layer parameter structs (count 6, 0x14 each: 2 texture settings of 0x30 + an SGO)
    0x44 / 0x48   u32 offsets (count 8) to the names of the layers' materials (UTF-16, in the string pool)
    0x4C          vegetation table (0x14 header, entries of 0x28, cells, 256 jitters of 0x10: vegetation())
    0x50          W x H x (u16 height, u16 material): the height is an IEEE half float in metres (the shader
                  does f16tof32 on the low half: heights -10.2266 .. 47.25 in SK_HEIGEN02 are the header's
                  0x30 / 0x34 exactly), the material is the layer 0..5 of the vertex
    0x54 / 0x58   (u16 layer, u16 weight half float) pairs, the layers' blend list the block lists index
    0x5C / 0x60   UTF-16 string pool (count in code units): the dependencies' names
  The field's origin is the placement record of the map's mapb that names the .shfb (SK_HEIGEN02: (-1000, 0, -1000));
  sample (row, col) is at origin + (col * cell, height, row * cell).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

import map_legacy  # its SGO extent walker

MAGIC = b'MAPB'
VERSION = 3


@dataclass(frozen=True)
class Layer:
    """One of the field's layers (the 0x14 struct at 0x3C + 0x14 * k: a texture setting for the colour and one for the
    multiplier, 0x30 bytes each): the slice of the map's colour array (`<model>.cols.dds`, 8 slices) the layer draws
    with, the slice that is multiplied over it, and each one's tiling (uv = metres * scale)."""
    color: int
    multip: int
    scale_color: tuple[float, float]
    scale_multip: tuple[float, float]


@dataclass
class Shfb:
    width: int
    height: int
    cell: tuple[float, float]
    stride: tuple[int, int]
    blocks: tuple[int, int]
    block_cells: tuple[int, int]
    lowest: float
    highest: float
    heights: np.ndarray                  # (height, width) float32 metres, row = z index, column = x index
    material: np.ndarray                 # (height, width) uint8, the layer 0..5 each vertex was painted with
    layers: list[Layer] = field(default_factory=list)
    names: list[str] = field(default_factory=list)       # the dependency pool (models, textures)
    layer_names: list[str] = field(default_factory=list)  # the 8 materials the layers draw with
    blend: np.ndarray | None = None      # (height, width, layers) float32 blend weights per vertex, summing to 1

    @property
    def extent(self) -> tuple[float, float]:
        """Metres in x and z the samples span."""
        return ((self.width - 1) * self.cell[0], (self.height - 1) * self.cell[1])


def _utf16z(b: bytes, o: int) -> str:
    e = o
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[o:e].decode('utf-16le')


def _rel(b: bytes, base: int) -> int:
    """The target of a self relative offset stored at `base` (0 = none)."""
    rel = struct.unpack_from('<i', b, base)[0]
    return base + rel if rel else 0


def parse(b: bytes) -> Shfb:
    """The field of a .shfb (ValueError when it is not one, or its arrays do not fit the file)."""
    if b[:4] != MAGIC or struct.unpack_from('<I', b, 4)[0] != VERSION:
        raise ValueError('not a SHFB (MAPB version 3)')
    w, h, sw, sh = struct.unpack_from('<4I', b, 8)
    cx, cz = struct.unpack_from('<2f', b, 0x18)
    bx, bz, cbx, cbz = struct.unpack_from('<4I', b, 0x20)
    lo, hi = struct.unpack_from('<2f', b, 0x30)
    field_at = struct.unpack_from('<I', b, 0x50)[0]
    if field_at + 4 * w * h > len(b):
        raise ValueError('SHFB field does not fit the file')
    raw = np.frombuffer(b, dtype='<u2', count=2 * w * h, offset=field_at).reshape(h, w, 2)
    heights = raw[:, :, 0].copy().view('<f2').astype(np.float32)
    layers: list[Layer] = []
    n_layers, at = struct.unpack_from('<I', b, 0x40)[0], struct.unpack_from('<I', b, 0x3C)[0]
    for k in range(n_layers):
        item = at + 0x14 * k
        count, sub = struct.unpack_from('<I', b, item)[0], item + struct.unpack_from('<i', b, item + 4)[0]
        first = sub
        second = sub + 0x30 if count > 1 else None
        layers.append(Layer(struct.unpack_from('<I', b, first)[0], struct.unpack_from('<I', b, second)[0] if second else -1,
                            struct.unpack_from('<2f', b, first + 0x20),
                            struct.unpack_from('<2f', b, second + 0x20) if second else (0.0, 0.0)))
    pool_n, pool = struct.unpack_from('<I', b, 0x5C)[0], struct.unpack_from('<I', b, 0x60)[0]
    names: list[str] = []
    if pool:
        names = [s for s in b[pool:pool + 2 * pool_n].decode('utf-16le').split('\0') if s]
    layer_names = []
    cnt, lst = struct.unpack_from('<I', b, 0x48)[0], struct.unpack_from('<I', b, 0x44)[0]
    for k in range(cnt):
        layer_names.append(_utf16z(b, lst + 4 * k + struct.unpack_from('<i', b, lst + 4 * k)[0]))
    return Shfb(w, h, (cx, cz), (sw, sh), (bx, bz), (cbx, cbz), lo, hi, heights, raw[:, :, 1].astype(np.uint8),
                layers, names, layer_names, _blend(b, w, h, (bx, bz), (cbx, cbz), max(n_layers, 1)))


def _blend(b: bytes, w: int, h: int, blocks: tuple[int, int], cells: tuple[int, int], n_layers: int) -> np.ndarray:
    """The per-vertex layer weights. Block (by, bx) (index by * blocks_x + bx) covers the vertices
    (by * cells + i, bx * cells + j) and holds two lists (offsets from the block's own start, 0x10 and 0x18): the
    layers it uses (list 1), and for each of its vertices (row major, its own cell counts at +8 / +0xC, plus one)
    the start of its run in the pair list at 0x54: one (slot, weight) pair per used slot (slot = index into list 1,
    weight an f16) and a closing pair of weight 0. The vertex's stored material is the slot of the largest weight
    (checked against every vertex of SK_HEIGEN02)."""
    pairs_at, pairs_n = struct.unpack_from('<I', b, 0x54)[0], struct.unpack_from('<I', b, 0x58)[0]
    pairs = np.frombuffer(b, dtype='<u2', count=2 * pairs_n, offset=pairs_at).reshape(-1, 2)
    slot = pairs[:, 0].astype(np.int64)
    weight = pairs[:, 1].copy().view('<f2').astype(np.float32)
    out = np.zeros((h, w, n_layers), dtype=np.float32)
    blocks_at = struct.unpack_from('<I', b, 0x38)[0]
    for k in range(blocks[0] * blocks[1]):
        at = blocks_at + 0x20 * k
        by, bx = divmod(k, blocks[0])
        r0, c0 = by * cells[1], bx * cells[0]
        if r0 >= h or c0 >= w:
            continue
        cx, cz, o1, n1, o2, n2 = struct.unpack_from('<2I', b, at + 8) + struct.unpack_from('<iIiI', b, at + 0x10)
        nc, nr = cx + 1, cz + 1
        if n2 != nc * nr:
            raise ValueError(f'SHFB block {k}: {n2} vertices for {nc} x {nr}')
        used = np.frombuffer(b, dtype='<u4', count=n1, offset=at + o1)
        start = np.frombuffer(b, dtype='<u4', count=n2, offset=at + o2).astype(np.int64)
        end = np.minimum(np.append(start[1:], start[-1] + n1 + 1), start + n1 + 1)   # <= its slots and the closing pair
        for v in range(n2):
            i, j = divmod(v, nc)
            if r0 + i >= h or c0 + j >= w:
                continue
            out[r0 + i, c0 + j] = 0.0     # a vertex on a block's edge is in both blocks: the later one wins
            for p in range(start[v], end[v]):
                if weight[p] > 0 and slot[p] < n1:
                    out[r0 + i, c0 + j, used[slot[p]]] += weight[p]
    return out


@dataclass(frozen=True)
class VegType:
    """One kind of plant of the field's vegetation table (a 0x28 entry): kind 1 = a grass / flower card drawn with
    the texture `name` (`sk_grassleaf01_1.dds`, one of the map's grass.merge.sgo names), 2 = a model (`.trb` tree,
    `.mlb` small objects); its size (x, y, z) and the random extra on it (the second triple; 0 for the models and the
    flowers); the SGO of its parameters (`colors`, `uv`), bytes as stored, or None."""
    kind: int
    name: str
    size: tuple[float, float, float]
    extra: tuple[float, float, float]
    params: bytes | None


@dataclass
class Vegetation:
    """The field's vegetation table (header 0x4C): `kind` (N x N, row = z, column = x like the heights) the plant type of
    each cell (0xFF none), `jitter` its index into the 256 random records `jitters` (16 bytes each: an angle, the
    offset in x and z within the cell in [0, 1), a size factor in [0, 1)), cells of `cell` metres from the field's
    origin over its whole extent (SK_HEIGEN02 2000 x 2000 of 1 m, SK_VALLEY01 1000 x 1000 of 2 m)."""
    types: list[VegType]
    kind: np.ndarray
    jitter: np.ndarray
    jitters: bytes
    cell: float


def vegetation(b: bytes, extent: float) -> Vegetation:
    """The vegetation table of a .shfb (sk_terrain.parse's file; `extent` the field's size in metres). Layout, from the
    seven SK maps (selftest edf41_sk_grass_real): header {u32 resolution (2: 1 m cells, 1: 2 m), i32 types (0x14),
    u32 type count, i32 cells, i32 jitters} with offsets from the header; a type is {u32 kind, i32 name (from the
    entry, UTF-16), u32 0, f32 size[3], f32 extra[3], i32 params SGO (from the entry, wrapping modulo 2^32: the SGOs
    sit at the start of the file)}; the cells are (type, jitter) byte pairs, row major; 256 jitter records of 0x10."""
    vt = struct.unpack_from('<I', b, 0x4C)[0]
    _res, toff, nt, coff, joff = struct.unpack_from('<IiIii', b, vt)
    types = []
    for k in range(nt):
        t = vt + toff + 0x28 * k
        kind, name_rel, _z = struct.unpack_from('<iiI', b, t)
        size = struct.unpack_from('<3f', b, t + 0x0C)
        extra = struct.unpack_from('<3f', b, t + 0x18)
        rel = struct.unpack_from('<i', b, t + 0x24)[0]
        params = None
        if rel:
            at = (t + rel) & 0xFFFFFFFF
            if b[at:at + 4] != b'SGO\0':
                raise ValueError(f'vegetation type {k}: no SGO at {at:#x}')
            params = bytes(b[at:map_legacy.sgo_extent(b, at)])
        types.append(VegType(kind, _utf16z(b, t + name_rel), size, extra, params))
    n_cells = (joff - coff) // 2
    side = int(round(n_cells ** 0.5))
    if side * side != n_cells or joff - coff != 2 * n_cells:
        raise ValueError(f'vegetation cells: {joff - coff} bytes are not a square of byte pairs')
    cells = np.frombuffer(b, dtype=np.uint8, count=2 * n_cells, offset=vt + coff).reshape(side, side, 2)
    return Vegetation(types, cells[:, :, 0].copy(), cells[:, :, 1].copy(), bytes(b[vt + joff:vt + joff + 0x1000]),
                      extent / side)


STEP_SAMPLES = 2   # the EDF6 terrain's vertex lattice: every 2nd sample = 4 m (tools/edf41_maps.py says why)


def lattice(s: Shfb, step_samples: int = STEP_SAMPLES) -> np.ndarray:
    """The heights on the EDF6 terrain's vertex lattice: (rows, cols) float64, every `step_samples`-th sample from
    sample (0, 0); vertex (i, j) sits at origin + (j * step, h, i * step) with step = step_samples * cell. The last
    row / column must fall on a sample: (samples - 1) % step_samples == 0 (ValueError)."""
    if (s.width - 1) % step_samples or (s.height - 1) % step_samples:
        raise ValueError('the sample count does not divide into the lattice')
    return s.heights[::step_samples, ::step_samples].astype(np.float64)
