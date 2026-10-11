# map_legacy: EDF4.1 / EDF5 .MAC -> EDF6 layout

- `pylib/map_legacy.py`: `mac_from_legacy(old_mac, scene_sgos='keep') -> bytes`; CLI `python pylib/map_legacy.py OLD.MAC NEW.MAC [--edf6-scene]`.
- Checked against the installed games by `tools/selftest.py` `map_legacy_real`. The full study (every pair, the lossless round trip) ran as a developer script outside the repo; its results are below.
- Uses `pylib/bigmap.py` (Marc.build, MapB, MapO, verify).

All offsets are int32, little-endian. "self" means relative to the field itself, "struct" relative to the start of the struct that holds the field, "abs" relative to the start of the member. Sources: EDF.dll fixups 0x11FA40 (MAPB), 0x1210A0 (def), 0x120A40 (record), 0x11F6C0 / 0x11F1A0 (point blocks), 0x11FE90 (MAPO), 0x120D90 (object), 0x11F410 (named entry), 0x11EF30 (+0x58 struct), plus statistics over every shipped MAC (18 EDF4.1, 24 EDF5, 50 EDF6). The fixups byte-swap fields but follow only a few offsets, so pointer-ness was settled from the data: a field counts as an offset only when it hits a block or string start in every file.

## MARC (container)

Read leniently: EDF4.1 fills directory words +0x0C/+0x10/+0x14 (bigmap.Marc.parse rejects that), and old data is packed without 16-byte alignment. Member names, order and bytes are kept. Output comes from `bigmap.Marc.build` (EDF6's layout).

## map.mapb

Header 0x90 (the same in all three games):

| off | meaning |
|---|---|
| 0x00 | 'MAPB', 0x04 version 2 |
| 0x08 / 0x0C | string table (abs) / length in UTF-16 units. Always the last block |
| 0x10 / 0x14 | 8-byte pair array (abs) / count (0 in every file) |
| 0x18 / 0x1C | defs (abs) / count |
| 0x20 / 0x24 | records (abs) / count |
| 0x28..0x77, 0x80, 0x88 | 12 header-slot SGO refs, each (self offset, 0). EDF4.1/5: 0x28..0x50 = six weather lighting SGOs (Brightness, Fog*, Light*...), 0x70 = grass parameters, 0x88 = {SceneType}. EDF6: 0x28..0x68 and 0x80 are 0; 0x88 and 0x70 hold empty SGOs |
| 0x78 | point block A (abs); 0x7C point block B (abs) |

Def (old 0x38, EDF6 0x3C). **A dword is inserted at +0x20, not +0x24**: decoding the 1179 defs that EDF5 and EDF6 share by name gives fully identical values with +0x20 as the new field, and 178 mismatches if +0x24 is assumed. EDF6 writes 1 in +0x20 for 165 defs (all in maps EDF5 does not have) and 0 elsewhere. The converter writes 0.

| old | EDF6 | meaning |
|---|---|---|
| +00 | +00 | type |
| +04 / +08 | +04 / +08 | SGO (self) / 0 |
| +0C | +0C | model name (self -> strtab) |
| +10 | +10 | int (-1) |
| +14 | +14 | name (self -> strtab) or 0 |
| +18, +1C | +18, +1C | floats (+1C near cull distance) |
| - | +20 | new int (0 / 1) |
| +20 | +24 | name (self -> strtab), e.g. `*_gr.mdx` |
| +24 | +28 | float |
| +28 | +2C | name (self -> strtab), e.g. `*_lod.mdx` |
| +2C | +30 | float |
| +30, +34 | +34, +38 | ints (0) |

Record (old 0x44, EDF6 0x48): +00 def (struct), +04 position, +10 rotation, +1C..+28 ints, +2C child list (struct) -> {+0 offset (struct) to int32 record indices, +4 count}, +30 int, +34 GUID, EDF6 +44 appended (0 in every EDF6 record).

Point block A (at 0x78): {+0 vec3 array (struct of the block), +4 count, +8 array of 0x0C structs (struct of the block), +C count}; each 0x0C struct: {+0 name (self -> strtab), +4 offset (struct) to 8-byte entries (4 x u16), +8 count}.
Point block B (at 0x7C, electric wires): {+0 0x10-byte points (vec3 + int), +4 n, +8 u16 pairs, +C n, +10 int list, +14 n}; **every int in the list is a self offset to a wire name in the string table** (`elec_wire_default.elec` ...). Earlier notes missed the A / B name pointers; relocating without them breaks those strings.

SGO blocks: every SGO in a MAPB (def SGOs and slot SGOs) is self-contained: header, nodes, name table and its own strings follow each other, and no offset leaves the block. Blocks are 4-aligned and tile the area between the point blocks and the string table exactly (checked in all 92 files).

Writer order (what changed between the old writers and EDF6's, checked on all 50 EDF6 maps):
- hdr, defs, child lists, records, block A (arrays, then its header), block B (arrays, then header): same order in all games.
- SGO area: EDF6 = def SGOs in def index order, then slot 0x88, then slot 0x70. The old writers used an unrelated order (0x88 first or between defs, def SGOs not in index order).
- String table: EDF6 = first-use order (each def's four names in def order, then block A's names, then block B's), deduplicated. The old writers sorted it.

## map.mapo

Header: old 0x58, EDF6 0x60 (the first block starts right after it; that tells the layouts apart).

| off | meaning |
|---|---|
| 0x08 / 0x0C | per-record entries, 0x1C each, count = record count |
| 0x10 / 0x14 | 0x0C (int, int, float) array ("iif") |
| 0x18 / 0x1C | unused (0 everywhere) |
| 0x20 / 0x24 | objects: EDF4.1 0x38, **EDF5 and EDF6 0x3C** (EDF5 already has the +0x38 dword) |
| 0x28 / 0x2C | int32 array, each a self offset to an object |
| 0x30, 0x38, 0x40 (+ counts) | vec3 / vec4 / vec4 arrays |
| 0x48 / 0x4C | string pool, UTF-16 units: sorted by code unit, deduplicated, always the last block |
| 0x50 / 0x54 | named entries, 0x0C each (empty in EDF4.1/5, one per record in EDF6) |
| 0x58 / 0x5C | EDF6 only: 0x0C structs {+0 name (struct), +4 count, +8 items (struct)}; items 0x14 {+0 int, +4 count, +8 sub-entries (item), +C float, +10 float}; sub-entries 8 bytes |

Per-record entry (0x1C): +00 grass pair (struct) -> {count, offset (pair)} -> count x 4 bytes; +04 slice of the 0x28 array (struct), +08 count; +0C float; +10 / +14 strings (struct -> pool); **+18 element of the 0x10 iif array** (struct), not of the 0x58 table. EDF4.1 leaves uninitialised bytes (1.0f, 0, 0, -1.0f ...) in the entries of records with no objects (+08 = 0); EDF5/EDF6 write those as zero (except a valid grass pair), so the converter zeroes them.

Object (struct-relative fields): +00 int (kind), +04 string, +08 vec4a element, +0C vec3 element, +10 vec4b element, +14 / +18 / +20 / +28 / +30 strings, +1C / +24 / +2C floats, +34 int, +38 int (absent in EDF4.1).

Named entry (0x0C): +0 name (struct -> pool) = decimal record index, +4 list offset (struct), +8 count; each list item {count, offset (item)} -> (float, int) pairs. EDF6 writes an empty list's offset as the write cursor (the position the list data would start at).

EDF6 order: hdr, vec3, vec4a, vec4b, objects, object pointers, entries, iif, grass pairs + data, 0x58 structs + items + sub-entries, named entries, named lists, pool. The old order is the same without the two EDF6 tables.

## What the converter does

- MAPB: defs and records resized (zero dwords at def +0x20 and record +0x44), every offset mapped through the old -> new address map, SGO area and string table rebuilt in EDF6's order. `scene_sgos='keep'` (default, lossless) carries the header-slot SGOs over; `'edf6'` writes them as EDF6 does (slots zero, two empty SGOs). That data lives in EDF6's .MAE files, so EDF6's own copies of these maps no longer have it.
- MAPO: header +8 bytes (0x58 / 0x5C = 0, 0), EDF4.1 objects get the +0x38 dword, EDF4.1 garbage entries zeroed, named table appended before the pool (index names, empty lists), pool rebuilt with the names merged and sorted, every string offset re-pointed.
- Other members unchanged.

## Results (the study, 2026-10-10)

EDF5 -> EDF6: 21 maps exist in both (IG_2000MCITY, IG_BASE502, IG_CAVE501/503/504, IG_EDFROOM01/02, IG_HEIGEN507, IG_KAIGAN502, IG_SANGAKU506, IG_TESTLIGHTMAP, IG_TEST_BLUEFLD, NW_DANTI01, NW_EUROPE01, NW_HENDEN, NW_JYOUSUICITY, NW_KITAGUNICITY, NW_SEIYU, NW_SEIYU_NIGHT, NW_SUIDEN, NW_TRAINCITY).
- Whole MAC byte-identical: 0 of 21. EDF6 re-exported every one of these archives: it dropped the per-map `<name>.rmpa` member, changed `map.rmpa`, changed or added `.trb` / `.mab` / `.dds` members and reordered the directory. Those are other members, outside this conversion.
- map.mapo byte-identical: 3 (IG_CAVE501, IG_CAVE503, IG_CAVE504), in both modes.
- map.mapb byte-identical: 2 in `edf6` mode (IG_CAVE503, IG_TESTLIGHTMAP), 0 in `keep` mode (the slot SGOs are the only difference).
- Every other difference is content, shown by decoding both sides with the EDF6 tables. Typical: EDF6 added `CrashType` to def SGOs; added defs, records and objects; reordered block A/B points; cleared some object collision names; filled the named lists and the 0x58 table; added pool strings. No layout differences (equal decoded values but different bytes) in any pair.
- Lossless check (old-table decode of the input vs EDF6-table decode of the output): ok for all 21 pairs, both modes.

EDF4.1: all 16 mission maps convert in both modes, re-parse with bigmap.MapB / bigmap.MapO, pass bigmap.verify and the lossless check. IG_TESTLIGHTMAP 4.1 -> EDF6 differs only in content: def SGOs, block A/B points, 646 objects (collision names, vec4b), and the EDF6-only tables.

## Open points

- `.mab` members inside the EDF5 maps are flag 0x03 with 2 lists; EDF6's copies are flag 0x83 with 3 lists (`pylib/mab_legacy.mab_from_edf5` converts the flag but not the extra list). Whether EDF6 loads 0x03 blocks was not proven here, so they stay as they are.
- Not run in game.
