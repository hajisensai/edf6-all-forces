# EDF6 map collision (<map>.MAD) - format notes for the bigmap seams

EDF.dll TimeDateStamp 0x678CCB46. Confidence: H = checked against every compressed mesh of IG_HEIGEN601.MAD (995),
M = inferred, L = guess. Code: `pylib/hktag.py` (reader), `pylib/hkcms.py` (decode / edit), `pylib/seams.py` (the
periodic correction), `tools/make_bigmap.py --check` (runs it all on the stock files).

## Container (H)

`<map>.MAD` (Chunk02.cpk, MAP/) is a MARC (docs/map-format.md) holding one file, `collision.hkt`: a Havok 2020.1
tagfile (`TAG0`, `SDKV` = `20200100`). Section headers are big-endian u32 (top two bits 0 = has children,
1 = leaf; low 30 bits the size incl. the 8 byte header) + a 4CC:
`TAG0 { SDKV, DATA, TYPE { TST1, TNA1, FST1, TBDY, TPAD }, INDX { ITEM } }`. No hash section, no checksum.

- DATA holds every object in its **compact tag layout**: a pointer or an array is a u32 item index, the element
  count is in the item. Sizes and member offsets come from TBDY (e.g. `hkRootLevelContainer` is 4 bytes).
- ITEM: 12 bytes per item: `type | flags << 24`, offset into DATA, count. Item 0 is null.
- TNA1: varint count, then per type (from 1) a TST1 name index and template args (`t*` type, `v*` value).
- TBDY: per type: type, parent, optionals; then by bit 0x01 format, 0x02 subtype, 0x04 version, 0x08 size + align,
  0x10 flags, 0x20 members (count in the low 16 bits; name (FST1), flags, offset, type each), 0x40 interfaces
  (pairs), 0x80 attribute.
- Varints: big-endian, length in the first byte's top bits (`0xxxxxxx`, `10` +1, `110` +2, `11100` +3,
  `11101` +4, else +8).

## What IG_HEIGEN601.MAD holds (H)

One `hknpPhysicsSystemData` with 59 static bodies, each named `<model>.<node>` (the name the map loader looks up,
docs/map-format.md). The terrain is five `hknpCompoundShape` bodies of `hknpCompressedMeshShape` instances, all at
identity (no rotation / scale / translation): the ground `ig_heigen507_2` (468 meshes, 5048 triangles, one ~114 m
tile each) and the four ring pieces `ig_heigen601_enkei{up,bottom,left,right}` (127-128 meshes of one 125 m quad each,
208 triangles a piece). The collision is that coarse; the rendered terrain (FMB) is about 1 m.

| piece | X | Z | Y |
|---|---|---|---|
| ig_heigen507_2 | -1250 .. 1250 | -1250 .. 1250 | -44.8 .. 21.4 |
| enkeibottom | -1750 .. 1250 | 1250 .. 1750 | -16.6 .. 13.9 |
| enkeileft | -1750 .. -1250 | -1750 .. 1250 | -44.8 .. 10.4 |
| enkeiright | 1250 .. 1750 | -1250 .. 1750 | -33.3 .. 11.8 |
| enkeiup | -1250 .. 1750 | -1750 .. -1250 | -37.9 .. 1.8 |

The ring is low ground, not hills; the mountains on the horizon are the far-only `ig_farmt_heigen507_2_out`
(+-20 km, 193 m high 2.5-5 km out, 1.8 km at the rim).

## hknpCompressedMeshShape (H)

Compact layout (offsets read from TBDY): shape +28 `data` -> `hknpCompressedMeshShapeData` { `meshTree`
(hknpCompressedMeshShapeTree, 96 bytes), `simdTree` (hkcdSimdTree) at +96, connectivity, hasSimdTree }.
meshTree: `nodes` (Aabb5BytesCodec[]), `domain` (hkAabb), numPrimitiveKeys, bitsPerKey, maxKeyValue, ...,
`sections`, `primitives` (4 x u8), `sharedVerticesIndex` (u16[]), `packedVertices` (u32[]), `sharedVertices`
(u64[]), `primitiveDataRuns`. Section (96 bytes): `nodes` (Aabb4BytesCodec[]), `domain`, `codecParms[6]`,
`firstPackedVertexIndex`, `firstSharedVertexIndex`, `firstPrimitiveIndex`, `firstDataRunIndex`, numPackedVertices,
numPrimitives, numDataRuns, page, leafIndex, layerData, flags.

- A primitive is a quad `a b c d` (a triangle when c == d). Index < numPackedVertices: a packed vertex
  `packedVertices[firstPacked + i]`, x 11 bits / y 11 / z 10, `pos = parms[0:3] + q * parms[3:6]`. Otherwise a shared
  vertex `sharedVertices[sharedVerticesIndex[firstShared + i - numPacked]]`, x 21 / y 21 / z 22 bits over the
  meshTree domain: `pos = lo + q * (hi - lo) / (2^bits - 1)`. Every terrain mesh here has one section; the ring
  pieces use shared vertices only.
- **Compressed AABB trees** (hkcdStaticTree, section `nodes` and meshTree `nodes`): depth-first; the root's box is
  coded against the tree's domain, each child against its parent's decoded box. One byte per axis, high nibble a,
  low nibble b: `min = parent.min + a^2/226 * extent`, `max = parent.max - b^2/226 * extent` (3468 of 3468 leaves
  of the ground contain their primitive with this rule; a linear /15 rule fails 92%). Aabb4BytesCodec byte 3: even =
  leaf of primitive `byte >> 1`, odd = inner node with children `i + 1` and `i + 2 * (byte >> 1)`.
- **hkcdSimdTree** (the mesh's primitives; the compound's instances in `boundingVolumeData.simdTree`, type 2):
  128-byte nodes of four float boxes (lx hx ly hy lz hz as 4-lane vectors) + data[4] + isLeaf. Node 0 is an empty
  sentinel, node 1 the root. Inner lane = union of its child node's lanes (360 of 360), data = child node. Leaf lane =
  the object's box, data = primitive key (primitive index << 1) / instance index. Empty lanes: +FLT_MAX / -FLT_MAX.
- The compound's `aabb` is the union of its instances' domains.

## Editing heights (what hkcms.py does)

Positions change only in y. Shared vertices are re-quantized over a new meshTree domain whose y range covers the new
heights (x / z quantized values kept: the x / z domain does not change); packed vertices get new codecParms y.
Then every box that depends on them is rebuilt: section domains, section trees (re-coded tight with the rule above,
never smaller than the true box, 1 mm margin, float32 rounded outward), the one-node mesh tree, the mesh's simd tree,
the compound's simd tree and aabb. `bound_problems` re-checks every box against the triangles it must hold.
The navmesh (`<map>.NAVMESH*.HKT`) is not touched (L: ground units near the block's rim may walk a few metres off
the moved ground).

## The seams (measured on the collision)

Opposite block edges differ by up to 33.9 m (x = +-1750 at z = -1000: 24.6 m; z = +-1750 at x = 1000: 33.0 m),
which is the step neighbouring 3500 m copies met in. After `seams.Field`: 0.006 m (quantisation); the middle ground
and its seam with the ring are unchanged (largest change on |x| or |z| = 1250: 4e-6 m); the ring moves by at most
17.3 m, fading to 0 over 450 m from the outer edge.
