"""The plants of an EDF6 terrain piece (grass, flowers, trees, small objects grown over a `.fmep` ground): the FMB's
trailing table 2 says what grows in which cell, the map's mapo says how high each one stands. Written for the SK maps of
EDF4.1 (pylib/sk_map.py), whose field carried its own vegetation table that EDF6 has no reader for.

Layout (EDF6's 46 stock FMBs; docs/fmb-format.md "Plants"):

  FMB table 2 (header +0x14):  {u32 groups, i32 0x08} then per group 0x28:
      i32 name (UTF-16, from the group), f32 cell, u32 cols, u32 rows, f32 origin x, f32 origin z, u32 types,
      i32 kind bytes, i32 jitter bytes, i32 type records   (offsets from the group)
    kind bytes: cols x rows (row = z, column = x) type index per cell, 0xFF nothing; jitter bytes: the cell's index
    into table 3; each array padded to a multiple of 4 after one more byte; then the group's type records (0x40):
      u32 kind (0 grass card, 1 model), i32 name, i32 file (UTF-16, from the record), u32 0, f32 editor colour[4],
      u32 0, f32 min size[3], f32 max size[3], i32 parameters SGO (from the record, 0 none)
    and their SGOs. The strings sit after table 4's records at the end of the file.
  FMB table 3 (header +0x18, 0x100 records): {f32 angle, f32 x, f32 z in [0, 1) within the cell, f32 size in [0, 1)}.
  mapo: the ground record's entry +0 -> {count, 8} -> count x f32: the world height of each occupied cell's plant,
    groups in order, cells row major, at x = (column + jitter.x) * cell + origin x, z likewise.

EDF.dll: map::sync::grass_init (0x119E20) grows every piece through MapObject_Fmex_GrowGrasses (0x13FF70 ->
0x140570): for each group, row, column with a kind byte != 0xFF it reads the next height (one per occupied cell of
every group, the plant drawn or not), the jitter record of the cell's jitter byte (+4 / +8 offsets, +0 angle, +0xC
size between min and max) and the type record. Checked on IG_HEIGEN507: 159441 heights = the occupied cells of its 4
groups, and 240 of 240 sampled heights are the ground under the jittered position within 5 cm.

    Group, PlantType                                         what a group holds
    table2(groups, pool_at) -> (bytes, pool bytes)          pool_at: the pool's offset from table 2's start
    with_plants(fmb, groups, jitters) -> bytes              the FMB with table 2 / 3 replaced, the pool appended
    read_table2(fmb) -> list[Group]                         the reader (selftest, stock files)
    heights_at(lattice, x0, z0, step, x, z) -> array        the ground of pylib/fmb_build.py's mesh under (x, z)
    plant_heights(group, jitters, ground) -> array          the mapo data of a group
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Callable

import numpy as np

import map_legacy  # the SGO extent walker

GROUP = 0x28
TYPE = 0x40
JITTERS = 0x100


@dataclass(frozen=True)
class PlantType:
    kind: int                                   # 0 grass card (texture `file` in grass.merge), 1 model
    name: str                                   # the editor's name
    file: str                                   # 'sk_grassleaf01_1.dds', 'Tree012_17m_Keyaki.tree', '.mls'
    color: tuple[float, float, float, float]    # the editor's colour
    size_min: tuple[float, float, float]
    size_max: tuple[float, float, float]
    params: bytes | None = None                 # an SGO ('colors', 'uv'), or None


@dataclass
class Group:
    name: str
    cell: float
    origin: tuple[float, float]
    types: list[PlantType]
    kind: np.ndarray        # (rows, cols) uint8, 0xFF nothing
    jitter: np.ndarray      # (rows, cols) uint8


def _pad(n: int) -> int:
    return (n + 3) & ~3


def _arr(a: np.ndarray) -> bytes:
    raw = np.ascontiguousarray(a, dtype=np.uint8).tobytes()
    return raw + bytes(_pad(len(raw) + 1) - len(raw))


def table2(groups: list[Group], pool_at: int) -> tuple[bytes, bytes]:
    """Table 2 for `groups` and the string pool its offsets name, the pool to be placed `pool_at` bytes after table
    2's start (after tables 3 and 4)."""
    pool = bytearray()
    where: dict[str, int] = {}

    def string(s: str) -> int:
        if s not in where:
            where[s] = len(pool)
            pool.extend(s.encode('utf-16le') + b'\0\0')
        return pool_at + where[s]

    head = bytearray(struct.pack('<Ii', len(groups), 8) + bytes(GROUP * len(groups)))
    body = bytearray()
    base = len(head)
    for g, grp in enumerate(groups):
        rec = 8 + GROUP * g
        rows, cols = grp.kind.shape
        if grp.jitter.shape != grp.kind.shape:
            raise ValueError('kind and jitter grids differ in size')
        used = set(np.unique(grp.kind).tolist()) - {0xFF}
        if used and max(used) >= len(grp.types):
            raise ValueError(f'group {grp.name}: a cell names type {max(used)} of {len(grp.types)}')
        b_at = base + len(body)
        body += _arr(grp.kind)
        c_at = base + len(body)
        body += _arr(np.where(grp.kind == 0xFF, 0, grp.jitter))
        d_at = base + len(body)
        body += bytes(TYPE * len(grp.types))
        recs = []
        for t in grp.types:
            s_at = 0
            if t.params:
                s_at = base + len(body)
                body += t.params + bytes(_pad(len(t.params)) - len(t.params))
            recs.append(s_at)
        struct.pack_into('<if2I2f4i', head, rec, string(grp.name) - rec, grp.cell, cols, rows, grp.origin[0],
                         grp.origin[1], len(grp.types), b_at - rec, c_at - rec, d_at - rec)
        for k, (t, s_at) in enumerate(zip(grp.types, recs)):
            at = d_at + TYPE * k - base
            tr = d_at + TYPE * k
            struct.pack_into('<IiiI4fI3f3fi', body, at, t.kind, string(t.name) - tr, string(t.file) - tr, 0, *t.color,
                             0, *t.size_min, *t.size_max, (s_at - tr) if s_at else 0)
    return bytes(head + body), bytes(pool)


def _ws(b: bytes, at: int) -> str:
    end = at
    while b[end:end + 2] != b'\0\0':
        end += 2
    return b[at:end].decode('utf-16le')


def read_table2(fmb: bytes) -> list[Group]:
    """The plant groups of an FMB (table2's input form)."""
    t2 = struct.unpack_from('<I', fmb, 0x14)[0]
    n, off = struct.unpack_from('<Ii', fmb, t2)
    out = []
    for g in range(n):
        r = t2 + off + GROUP * g
        name, cell, cols, rows, ox, oz, nt, b, c, d = struct.unpack_from('<if2I2f4i', fmb, r)
        kind = np.frombuffer(fmb, dtype=np.uint8, count=cols * rows, offset=r + b).reshape(rows, cols).copy()
        jit = np.frombuffer(fmb, dtype=np.uint8, count=cols * rows, offset=r + c).reshape(rows, cols).copy()
        types = []
        for k in range(nt):
            t = r + d + TYPE * k
            kd, n1, n2, _z = struct.unpack_from('<IiiI', fmb, t)
            color = struct.unpack_from('<4f', fmb, t + 0x10)
            mm = struct.unpack_from('<6f', fmb, t + 0x24)
            so = struct.unpack_from('<i', fmb, t + 0x3C)[0]
            params = None
            if so:
                params = bytes(fmb[t + so:map_legacy.sgo_extent(fmb, t + so)])
            types.append(PlantType(kd, _ws(fmb, t + n1) if n1 else '', _ws(fmb, t + n2) if n2 else '', color,
                                   mm[:3], mm[3:], params))
        out.append(Group(_ws(fmb, r + name), cell, (ox, oz), types, kind, jit))
    return out


def with_plants(fmb: bytes, groups: list[Group], jitters: bytes) -> bytes:
    """`fmb` with table 2 made of `groups`, table 3 = `jitters` (0x100 records of 16 bytes) and the strings after table
    4's records; tables 1 and 4 and every chunk stay where they are. Whatever followed table 4's records (the strings of
    the table 2 being replaced) is dropped."""
    if len(jitters) != 16 * JITTERS:
        raise ValueError('table 3 is 0x100 records of 16 bytes')
    t1, t2, t3, n3, t4, n4 = struct.unpack_from('<6I', fmb, 0x10)
    if n3 != JITTERS or not t1 <= t2 <= t3 <= t4:
        raise ValueError('not the trailing table layout of a stock FMB')
    shader = fmb[t4:t4 + 28 * n4]
    new2, pool = b'', b''
    for _ in range(2):        # the pool's place depends on table 2's size, which does not depend on the pool's place
        new2, pool = table2(groups, len(new2) + len(jitters) + len(shader))
    out = bytearray(fmb[:t2]) + new2 + jitters + shader + pool
    struct.pack_into('<II', out, 0x18, t2 + len(new2), JITTERS)
    struct.pack_into('<I', out, 0x20, t2 + len(new2) + len(jitters))
    return bytes(out)


def heights_at(lattice: np.ndarray, x0: float, z0: float, step: float, x: np.ndarray, z: np.ndarray) -> np.ndarray:
    """The height of pylib/fmb_build.py's ground mesh at (x, z): the lattice's quad (r, c) is the triangles A B D and
    A D C (A = (r, c), B = (r, c + 1), C = (r + 1, c), D = (r + 1, c + 1)), so a point is interpolated in the triangle
    it falls in, as the drawn ground (and the collision of the same lattice) has it."""
    rows, cols = lattice.shape
    u = (np.asarray(x, dtype=np.float64) - x0) / step
    v = (np.asarray(z, dtype=np.float64) - z0) / step
    c = np.clip(np.floor(u).astype(np.int64), 0, cols - 2)
    r = np.clip(np.floor(v).astype(np.int64), 0, rows - 2)
    fu = np.clip(u - c, 0.0, 1.0)
    fv = np.clip(v - r, 0.0, 1.0)
    a, b = lattice[r, c], lattice[r, c + 1]
    cc, d = lattice[r + 1, c], lattice[r + 1, c + 1]
    upper = a + fu * (b - a) + fv * (d - b)       # A B D: fu >= fv
    lower = a + fv * (cc - a) + fu * (d - cc)     # A D C
    return np.where(fu >= fv, upper, lower)


def plant_heights(group: Group, jitters: bytes, ground: Callable[[np.ndarray, np.ndarray], np.ndarray]) -> np.ndarray:
    """The heights (float32) the mapo holds for `group`: the ground under each occupied cell's jittered position, row
    major."""
    jit = np.frombuffer(jitters, dtype='<f4').reshape(JITTERS, 4)
    rows, cols = np.nonzero(group.kind != 0xFF)
    j = jit[group.jitter[rows, cols]]
    x = (cols + j[:, 1].astype(np.float64)) * group.cell + group.origin[0]
    z = (rows + j[:, 2].astype(np.float64)) * group.cell + group.origin[1]
    return np.asarray(ground(x, z), dtype='<f4')
