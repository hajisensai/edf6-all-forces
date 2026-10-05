# EDF6 `.FMB` terrain piece format

Reader / height editor: `pylib/fmb.py` (`decode`, `parse`, `set_heights`, `selftest`).
Evidence: EDF.dll (TimeDateStamp 0x678CCB46) loader disassembly + the stock files
`IG_HEIGEN601_ENKEIRIGHT.FMB`, `IG_HEIGEN601_ENKEILEFT.FMB`, `IG_HEIGEN507_2.FMB` (Chunk02.cpk `MAP/`).
Confidence in brackets: H = proven by code + byte-exact round trip, M = consistent with data, L = guess.

## Loader path (H)

| RVA | role |
|---|---|
| `0x152E1A` (Preload_Fmex) | calls the FMB loader `0x10E8B0` |
| `0x10E8B0` | checks `'FMB\0'`, version `0x110`; schedules a per-chunk job (std::function vtable `0x1769890`, `_Do_call` `0x1106A0`) |
| `0x10C7B0` | per chunk: reads kind / AABB from the file chunk; kind 2 -> CMPL size `0x3F810` + decode `0x3F6B0`/`0x3F540`; kind 1 -> payload used in place; both -> `0x10D8A0` |
| `0x10D8A0` | builds the GPU objects of one leaf (see below) |
| `0x10CE70` | parses the trailing layer table (header +0x10) into 0x50-byte material/layer records |
| `0x3F540` | CMPL decoder: Okumura LZSS, zeroed 4 KB ring, r = 0xFEE; **stops when the input (`stored size - 8`) runs out** -- no output bound, so the stored size must be exact |

## File layout (H)

```
0x00 'FMB\0'
0x04 u32 0x110
0x08 u32 count            chunk count (255 in the HEIGEN601 ENKEI pieces, 935 in HEIGEN507_2)
0x0C u32 0x2C             offset of the chunk offset table (u32[count], absolute)
0x10 u32 off              trailing layer table  (== end of the last chunk)
0x14 u32 off              trailing table 2 (small)
0x18 u32 off              trailing table 3 (0x100 records of 16 bytes: random-looking floats, grass jitter?) (L)
0x1C u32 0x100            its count
0x20 u32 off              trailing table 4 (0x15 small-int records) (L)
0x24 u32 0x15             its count
0x28 u32 max decompressed leaf size (buffer size hint)
0x2C u32 table[count]
...  chunks, in table order, each starting 4-byte aligned, gaps zero-padded
...  trailing tables up to EOF (only internal *relative* offsets; moved as one block)
```

The fields at +0x10/+0x14/+0x18/+0x20 are absolute and must be shifted when the chunk area changes size.

## Chunks: a post-order binary AABB tree (H)

Every chunk starts `i32 kind; f32 centre[3]; f32 half[3]` (AABB, model space; pieces are placed at the
origin, so model == world).

* **kind 0, node (0x24 bytes):** `+0x1C i32 left_start, +0x20 i32 right_start`. Chunks are stored in
  post order: the node's subtree is chunks `[left_start, index]`; left child = chunk `right_start - 1`,
  right child = chunk `index - 1`. The root is the last chunk, `(0, ...)`.
* **kind 2, compressed leaf:** `+0x1C i32 rel (=0x24), +0x20 u32 stored size`, then the CMPL stream
  (`'CMPL'`, u32 BE decompressed size, LZSS) at `chunk + rel`.
* **kind 1, raw leaf:** the leaf payload (below) sits directly in the chunk. The loader supports it; the
  three files studied have none (the old "weird kind 0x1100142" was a misread: the table starts at 0x2C,
  not 0x30).

**AABB rule (H, bit exact on all chunks of all three files):** for the vertices of the subtree,
`centre = f32((min+max)*0.5)`, `half = f32((max-min)*0.5)`, float32 arithmetic. Leaves store it twice
(file chunk header and payload +0x04). Nodes use the subtree's vertex min/max (not the union of the
children's rounded boxes -- that does *not* reproduce the stored values).

Leaf AABBs only bound the coarse render mesh; e.g. ENKEIRIGHT leaf 1 is `y -12.95 +- 2.68` because its
three vertices are at y -10.27 / -15.63 / -12.92. The piece's render Y range is -33.3..11.8, X
1250..1750, Z -1250..1750 (the "68..190" figure does not come from this file).

## Leaf payload (decompressed; kind-1 layout) (H)

All offsets are relative to the payload start unless stated otherwise.

```
+0x00 u32 1
+0x04 f32 centre[3], half[3]      same AABB as the chunk header
+0x1C u32 (1)
+0x20 u32 nverts                  vertex buffer: stride 0x18 = f32 pos[3] + f32 normal[3]
+0x24 i32 vert_off                (relative to the payload) -- at the very end of the payload
+0x28 u32 nmesh
+0x2C i32 mesh_off                -> nmesh records of 0x3C bytes
```

Mesh record (offsets relative to the record):

```
+0x00 u32 2
+0x04 u32 nindex, +0x08 i32 rel -> u16 indices (triangle list; IB created with count*2 bytes, 0x115FDC0)
+0x0C f32 cell_x, cell_z (1.0)    splat grid: cell size,
+0x14 u32 cols, rows (126x125)    dimensions,
+0x1C f32 origin_x, origin_z      origin (e.g. 1624, 1500),
+0x24 u32 0x202, u32 4            format / layer count (M)
+0x2C u32 nweight  (= cols*rows),  +0x34 i32 rel -> splat records, stride 0x50 (structured buffer)
+0x30 u32 nlayermap (1),           +0x38 i32 rel -> 16-byte records: global layer index per slot, 0xFF = unused
```

`0x10D8A0` creates exactly: one vertex buffer (stride 0x18) from `+0x20/+0x24`, and per mesh one u16 index
buffer, one 0x50-stride buffer and one 0x10-stride buffer, and copies the 32 grid bytes (+0x0C..+0x2B)
into the mesh object (shader constants). Nothing else is read.

### Where the heights are (H)

**Only in the vertex buffer** (`pos.y`, float32, no quantisation). The render mesh is coarse: 3..4
vertices per leaf in the ENKEI pieces (128 leaves, 464 vertices, 208 triangles for a 500 m x 3000 m
strip; some 125 m tiles are split into two leaves with one triangle each), 3..15 per leaf in HEIGEN507_2 (468 leaves, 4821 vertices for the 2500 m ground).
Vertices on leaf borders are duplicated per leaf; neighbours can differ by ~5e-4 in x/z (1624.9996 vs 1625).

### The 1 m grid is a splat (blend weight) map, not heights (H)

Each 0x50 record = one grid cell: byte 0 = small per-cell value (0..2, density/flag (L)), bytes 1..15 zero,
then four 16-byte groups at +0x10/+0x20/+0x30/+0x40 = the RGBA8 layer weights of the cell's 4 corners
`(i,j) (i+1,j) (i,j+1) (i+1,j+1)` (group 1 is group 0 shifted by one column, group 2 by one row -- checked
on every cell). Only the layers named in the layer map are non-zero, and per corner they sum to 254
(that's why the old analysis saw "two bytes that always sum to 254"). Proof it is not height: in
ENKEIRIGHT leaf 1, byte 0 is 0 in every record while the leaf spans 5.4 m in y; no other byte of the record
is ever non-zero across 127 meshes.

### Normals (H for the location, M for the generator)

Stored per vertex as float32, rounded to 6 decimals (exported from a DCC tool). Recomputing area-weighted
normals over the whole piece (welded across leaves by x/z) differs from the stored ones by 0.19 deg mean,
0.76 deg max in ENKEIRIGHT (0.65 deg ENKEILEFT, 1.91 deg HEIGEN507_2) (unwelded per-leaf normals: 1.7 deg mean, 6.7 deg max), so they were baked on a denser source
mesh with welding. `set_heights` therefore rewrites only normals of vertices whose triangles touch a moved
vertex (`normals='affected'`), or all / none on request. The index winding is clockwise seen from above;
normals point up (+y).

## Editing heights consistently (H)

`set_heights(fmb, fn)`:

1. `y := f32(fn(x, y_old, z))` for every vertex of every leaf.
2. normals of affected vertices recomputed (welded, area weighted).
3. leaf AABB (chunk header + payload +0x04) and every node AABB re-derived with the rule above.
4. leaf re-compressed: unchanged payload -> original stream; changed payload -> incremental re-encode:
   every original LZSS flag group whose output bytes *and* 4 KB look-back window are unchanged is copied
   verbatim (it must decode to the same bytes), the dirty groups (payload AABB at +0x04 -> the first ~4 KB;
   the vertex array at the end) are greedily re-encoded, padded to whole 8-item groups where original groups
   follow, never referencing the zeroed initial window. ~10 ms per leaf; the result decodes to exactly the new
   payload (checked with both decoders). `exact_cmpl=True` instead runs the full Okumura encoder
   (`mdb.cmpl_compress`, byte-identical to the game tool's choice, ~15 s per 1.26 MB leaf). Stored size at
   chunk +0x20 updated (the game decoder stops on input length, so it must be exact).
5. chunks re-laid out (4-byte alignment, zero padding), offset table rewritten, header +0x10/+0x14/+0x18/+0x20
   shifted by the size delta; trailing tables copied unchanged.

Not covered by the FMB: collision. Preload_Fmex looks up `<model>hkt` in the map's MAD container
(docs/map-format.md); raising render heights does not move the Havok collision.

## Round trip

`python pylib/fmb.py selftest FILE.FMB [--recompress N]`: AABB rule bit exact on every chunk; identity
`set_heights` byte-identical; tail splice decodes exactly (fast decoder + mdb decoder); N leaves re-encoded
with the full Okumura encoder byte-identical; a +5 m edit re-parses with the expected vertices.

Results (2026-10-05): ENKEIRIGHT, ENKEILEFT, HEIGEN507_2 all `SELFTEST OK`; identity byte-identical
(21117600 / 21951184 / 165415910 bytes); ENKEIRIGHT leaf 0 full Okumura re-encode byte-identical.
