# EDF6 map archive (.MAC) - format notes for the bigmap tool

EDF.dll TimeDateStamp 0x678CCB46, RVAs. Confidence: H = read from code or checked against all 45 shipped
MAC files, M = inferred, L = guess. All values are little-endian int32 unless noted.

## MARC container (H)

| off | meaning |
|---|---|
| 0x00 | `'MARC'` |
| 0x04 | version 5 |
| 0x08 | data start = 0x24 (header size) |
| 0x0C | data region length = string table offset - 0x24 (includes padding) |
| 0x10 | sum of all file sizes (no padding) |
| 0x14 | directory offset, relative to this field (dir = 0x14 + value; code 0x1186F0) |
| 0x18 | file count |
| 0x1C | string table offset (absolute) |
| 0x20 | string table length in UTF-16 units |

- File data: first file at 0x24, each next file at the next 16-byte boundary. Padding is zero.
- String table: at align16(end of last file), the unique file names, sorted (plain code point order), UTF-16LE, NUL terminated.
- Directory: at align16(end of string table), 0x18 bytes per file: `name offset (relative to the entry)`, `data offset (absolute)`, `size`, 0, 0, 0. Order is the builder's order (not sorted) and is kept.
- Lookup is a linear, case-sensitive wcscmp over the directory (0x118713 / 0x11878A for `map.mapo` / `map.mapb`). No hashes, no checksums.
- The encoder in bigmap.py re-packs all 45 shipped MAC files byte-identically (verification.txt).

## map.mapb (H)

The full field list comes from the endian-fixup 0x11FA40 (record fixup 0x120A40, def fixup 0x1210A0).

Header (offsets relative to the mapb start unless noted):

| off | meaning |
|---|---|
| 0x00 | `'MAPB'`, 0x04 version 2 |
| 0x08/0x0C | UTF-16 string table offset / length in units (names of defs) |
| 0x10/0x14 | int-pair array offset / pair count (empty in HEIGEN601) |
| 0x18/0x1C | def table offset / count, 0x3C bytes each |
| 0x20/0x24 | placement record offset / count, 0x48 bytes each |
| 0x28..0x77 | 10 (offset, count) pairs, byte-swapped only; HEIGEN601 has (0x92A0,0) at 0x70 |
| 0x78 | offset of a point/route block {vec3 off, n, list off, n} (fixup 0x11F6C0), 38 points here |
| 0x7C | offset of a second point block {0x10-byte points, u16 pairs, int list} (fixup 0x11F1A0) |

Neither 0x78 nor 0x7C is sized or indexed by the record count (both 38 here). M: they look like AI/marker points.

Def (0x3C): +00 type (3 = terrain/far), +04/+08 embedded SGO param block (offset relative to +04, size), +0C name
offset relative to +0C (e.g. `ig_heigen507_2.fmep`), +10 -1, +18 float, +1C near cull distance, +28/+30 floats.

Record (0x48):

| off | meaning |
|---|---|
| 0x00 | def offset, relative to the record start |
| 0x04 | position x, y, z |
| 0x10 | rotation x, y, z in degrees |
| 0x1C | int: variant/seed; if 0 the loader uses the sum of the 4 GUID dwords instead (0x11B27E). Small values 1..8 on .mosb in city maps |
| 0x20, 0x24, 0x28, 0x30 | ints (zero in HEIGEN601) |
| 0x2C | offset relative to the record start to {int off (relative to this struct), int count} -> int32 record indices. These are child records (e.g. billboards on a building in NW_KOUSOUBLD601, positions adjacent). Zero in HEIGEN601 |
| 0x34 | 16-byte GUID |
| 0x44 | far-only flag |

GUIDs are not required to be unique: NW_DLCMAP601/602 ship 4546 guardrails with one GUID (H, data). The tool still gives copies fresh GUIDs.

## map.mapo (H)

Fields from fixup 0x11FE90. Every offset inside the header is relative to the mapo start; every offset inside an entry
is relative to that entry's start.

| off | meaning |
|---|---|
| 0x08/0x0C | per-record entry array, 0x1C bytes, count == record count |
| 0x10/0x14 | (int, int, float) array (empty here) |
| 0x20/0x24 | 0x3C object structs (204 here = non-.fmep records) |
| 0x28/0x2C | int32 array, each a self-relative pointer to a 0x3C struct |
| 0x30/0x34, 0x38/0x3C, 0x40/0x44 | vec3 / vec4 / vec4 arrays (colour palette etc.) |
| 0x48/0x4C | UTF-16 string pool (sorted, deduplicated), always the last block |
| 0x50/0x54 | per-record named entries, 0x0C bytes, count == record count |
| 0x58/0x5C | 0x0C structs with nested lists (7 here) |

Per-record entry (0x1C, indexed by record index: `imul r8, idx, 0x1c` at 0x116DAB / 0x116FD7 / 0x11741A / 0x11AAEB ...):
+00 offset to an {int, int} pair (only on .fmep pieces, e.g. record 0 = (159441, 8)), +04/+08 offset into the 0x28 int
array / count 1 (-> the record's 0x3C struct), +0C float (1000 on rocks), +10/+14 offsets to strings
(`sk_rock601a_m_gr.mdb` / `.hkt`), +18 offset into the 0x58 struct array (non-zero in city maps; values unique
and 12-byte spaced, so it is an offset). The placement code stores this entry pointer in the desc (0x150AF0, desc+0x30)
and the base Preload object keeps it at obj+0x18 together with the record index at obj+0x3C (0x150BD0).

Per-record named entry (0x0C, indexed by record index at 0x118D95): +00 offset to a string that is the decimal record
index ("0", "1", ...; true in all 45 maps), +04/+08 offset/count of a list of {count, offset} -> (float, int) pairs.

## Loader path (H)

- 0x117F90 finds `map.mapo` / `map.mapb` in the MARC, fixes them up (0x11FE90 / 0x11FA40), sets loader+0x18 / +0x10,
  loads the MAD collision (`collision.hkt`, 0x190770), then calls 0x11E1E0.
- 0x11E1E0 resizes the per-record vector at loader+0x118 (data +0x120) to mapb+0x24 entries of 16 bytes, then runs up
  to 4 parallel tasks (0x114A40) over record indices 0..count-1; each calls 0x1169B0 for one record. No fixed maximum;
  shipped maps reach 13251 records.
- 0x1169B0 picks the object class from the def name extension (.mosb, .fmep -> .fmb, .mdx/HAS_ param) and passes
  matrix + mapo entry + index. Pass 2 0x11A3F0 walks the same indices again and reads loader+0x120[idx].
- Collision is name-keyed: Preload_Fmex (0x1527D0) looks up `<model>` + `hkt` in the MAD collision container
  (0x18DD30 at 0x152E91) and builds the bodies with the object's own world matrix (0x13F400, matrix at obj+0x1200).
  The MAD collision.hkt holds systems named like `ig_heigen507_2.ig_Heigen507_2`. No record GUID appears in the MAD,
  the navmesh or anywhere else in the MAC (checked byte-for-byte). So a copy with the same def gets its own collision.
- Only two users of the mapb count in the map module (0x11A3F0, 0x11E1E0) and one user of the mapo +0x50 table
  (0x118CD0). Neither sorts by GUID or position. No sector/grid structure keyed by record position exists in mapb/mapo.

## What make_tiled does

1. MAPB: copies the whole record array to the end (align 16), re-bases +00 and +2C of every record, appends one
   record per copy (same def, position + offset, new GUID, children remapped to the copied children; children are added
   automatically), writes new +2C child lists after it, sets +0x20/+0x24. The old array stays as dead bytes.
2. MAPO: appends the new names ("209", "210", ...) to the string pool (the last block) and bumps +0x4C, then rebuilds the
   0x1C array and the 0x0C named array at the end (old entries re-based, copies cloned from the source with re-based
   offsets, so they share the source's strings / 0x3C struct / lists), sets +0x08/+0x0C/+0x50/+0x54.
3. MARC re-pack with the two changed files; all other files byte-identical.

## Terrain extents (H, from per-chunk AABBs in the .FMB files: table of chunk offsets at 0x30, count at 0x08; each chunk
starts with int kind, centre vec3, half-extent vec3)

| piece | X | Z | Y |
|---|---|---|---|
| ig_heigen507_2 (ground, record 0) | -1250 .. 1250 | -1250 .. 1250 | -44.8 .. 21.4 |
| enkeibottom (203) | -1750 .. 1250 | 1250 .. 1750 | -16.6 .. 13.9 |
| enkeileft (204) | -1750 .. -1250 | -1750 .. 1250 | -44.8 .. 10.4 |
| enkeiright (205) | 1250 .. 1750 | -1250 .. 1750 | -33.3 .. 11.8 |
| enkeiup (206) | -1250 .. 1750 | -1750 .. -1250 | -37.9 .. 1.8 |

The four enkei pieces form a 500 m pinwheel ring around the 2500 m ground; the whole block is 3500 x 3500.
(Y corrected 2026-10-05 from the collision meshes, docs/map-collision.md; the earlier Y column, 24..219 etc., was
misread. The ring is low ground; the horizon's mountains are the far-only ig_farmt ring.)
All five sit at position (0,0,0) in the map, so these are world extents.

Tiling options: ground-only copies at multiples of 2500 make a continuous flat field, but the original enkei hills then
stand on the neighbouring tiles' edges (drop them by not copying, or accept the ridge). Copying the whole block
(0, 203-206, plus far 207/208) at multiples of 3500 keeps every piece intact, with enkei hills between tiles; the outer
enkei edges of neighbours meet at +-1750 but their heights are not designed to match (H: up to 33.9 m apart, seen
in game as see-through steps; tools/make_bigmap.py makes the block periodic, docs/map-collision.md).

## Unknowns / risks

- mapo per-record +00 pair on .fmep records (record 0: (159441, 8)) and `grass.merge.sgo`: grass for the ground piece
  may be stored in world coordinates; copies share it, so copied ground may have no grass or duplicate it (L).
- Shared 0x3C structs / lists are assumed read-only at runtime (M: the fixup writes them only when byte-swapping).
- Nothing here changes the physics broadphase (+-3000 default), `move_limit` (+-999), navmesh or far-camera limits;
  see docs/bigworld-re.md.
- Not run in game.
