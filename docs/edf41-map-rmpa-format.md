# EDF5 / EDF4.1 RMPA -> EDF6 RMPA

`rmpa_legacy.rmpa_from_legacy(old) -> bytes` converts an EDF5 / EDF4.1 RMPA (a mission's `MISSION.RMPA` /
`BEFORE.RMPA` / `AFTER.RMPA`, or a MAC's `map.rmpa`) to EDF6's format. An input that already is EDF6's comes back
unchanged. `decode5` / `decode6` read the two formats completely (every non-zero byte of all 567 legacy and 865
EDF6 RMPAs in the three games is accounted for); `encode6` writes EDF6's layout and reproduces all 865 EDF6
files byte for byte from their decoded trees.

Code: `pylib/rmpa_legacy.py`. Checked against the installed games by `tools/selftest.py` `rmpa_legacy_real`.
The study behind the tables (EDF.dll's and EDF5.exe's RMPA walkers run under an emulator, field-by-field
diffs of every pair) ran as developer scripts outside the repo; its results are below.

## Sources of the tables

* EDF6: EDF.dll loader `0x6F2D90` (magic `\0PMR`, version 1, then four (count, offset) pairs), section walkers
  `0x6F35E0` routes, `0x6F3300` shapes, `0x6F3470` cameras, `0x6F3810` points; below them `0x6F3980` waypoint,
  `0x6F2980` route, `0x6F2480` shape set, `0x6F1E70` shape, `0x6F2600` camera set, `0x6F2270` camera,
  `0x6F2860` / `0x6F30E0` timeline, `0x6F2B50` point set, `0x6F2170` property list, `0x6F3D20` vec4 swap,
  `0x4B1A0` UTF-16 string swap (the crash sites: `0x6F3D25` is the vec4 swap reading past the data, `0x4B2AB`
  the string swap of a shape set name reached from `0x6F253D`).
* EDF5: EDF5.exe loader `0x3CEDF0`, walkers `0x3CF170` / `0x3CFA00` / `0x3D0360` (routes), `0x3CF3A0` /
  `0x3CFC20` / `0x3D0650` / `0x3D0D30` (shapes), `0x3CF5C0` / `0x3CFE40` / `0x3D08E0` / `0x3D0FE0` /
  `0x3D0DE0` (cameras), `0x3CF7E0` / `0x3D0140` / `0x3D0B90` (points), string swap `0x5BE150`.
* The walkers byte-swap every field the game uses (ints, floats, strings), so running them under emulation on
  the files gives the layout; fields they skip are the raw / little-endian ones below. Decoding all files with
  these tables leaves no unexplained non-zero byte.

All offsets are relative to the struct that holds them unless noted; all values big-endian unless noted;
strings are UTF-16BE, NUL-terminated; (n, off) = count and offset of an array.

## EDF6 format

Header (0x28): `+0 '\0PMR'`, `+4 1`, `+8 (n, off)` route sections (0x1C), `+10 (n, off)` shape sections (0x14),
`+18 (n, off)` camera sections (0x14), `+20 (n, off)` point sections (0x14); offsets from the file start.
EDF6 always writes exactly one section of each kind (empty ones too).

| struct | size | fields |
|---|---|---|
| route section | 0x1C | +0 -1, +4 name len, +8 name, +C (n, off) routes, +14 (n, off) waypoints (all routes' waypoints in one array) |
| route | 0x18 | +0 -1, +4 name len, +8 name, +C (n, off) int list of waypoint indices, +14 offset to the section's waypoint array |
| waypoint | 0x30 | +0 index (section-wide), +4 (n, off) int list of linked waypoint indices (section-wide), +C id, +10 name len, +14 name, +18 vec4 position, +28 property list |
| shape / camera / point section | 0x14 | +0 -1, +4 name len, +8 name, +C (n, off) sets |
| shape set | 0x14 | +0 0, +4 name len, +8 name, +C (n, off) shapes |
| shape | 0x24 | +0 type len, +4 type (`Rectangle`, `Sphere`, `Cylinder`...), +8 name len, +C name, +10 id, +14 (n, off) shape data, +1C property list |
| shape data | 0x38 | +0 vec4, +10 vec4, +20 vec4, +30 float, +34 float |
| point set | 0x14 | +0 -1, +4 name len, +8 name, +C (n, off) points |
| point | 0x34 | +0 id, +4 vec4 position, +14 vec4 the point it faces, +24 name len, +28 name, +2C property list |
| camera set | 0x24 | +0, +4 name len, +8 name, +C (n, off) cameras, +14 int, +18 offset to timeline 1, +1C int, +20 offset to timeline 2 |
| camera | 0x68 | +0 int, +4 int, +8 int, +C float, +10 float, +14/+24/+34/+44 vec4, +54 float, +58 name len, +5C name, +60 property list |
| timeline | 0x0C | +0 float, +4 (n, off) keys of 0x1C: float, float, int, float, float, float, float |
| property list | 8 | +0 n, +4 offset (from the list header) to n entries of 8 bytes: **little-endian** offsets (from the entry) to a name string and a value string (EDF.dll reads them with plain `movsxd`, unswapped) |

Properties seen: waypoints `rmpa_float_WayPointWidth` = the width as text (`-1`, `10`...; every value in both
games is integral); every shape `Usage` = `通常`.

Layout EDF6 writes (`Writer` / `encode6`): header, then for each kind in the order routes, shapes, cameras,
points: the section headers, then depth first. An array of records is written at the cursor and the cursor is
then aligned to 16; then each record's children follow in field order, each non-empty child array / int list /
property list aligned to 16 after it. An empty list's offset is the cursor at the moment it would have been
written (no padding consumed). Route section: waypoints first (each waypoint's links then properties), then the
routes (each route's index list). Shape: data, then properties. Camera set: cameras (each camera's properties),
timeline 1, timeline 2 (no EDF6 file has a camera, so this order is only the DLL's field order). After the last
struct: one string pool, deduplicated, sorted by UTF-16 code unit, 2-aligned, nothing after it.

## EDF5 / EDF4.1 format

Same header, but the section headers are 0x20 and EDF5 omits empty sections (count 0); `+28`, `+2C` are 0.
Every named object carries an int "refs" list that is empty in every file of both games.

| struct | size | fields |
|---|---|---|
| route / shape / point section | 0x20 | +0 (n, off) sets (0x20), +8 (n, off) refs, +10 id, +14 name len, +18 name, +1C raw (0x00010000) |
| camera section | 0x20 | +0 (n, off) refs, +8 id, +C raw, +10 name len, +14 name, +18 (n, off) camera sets (0x30) |
| set (route / shape set / point set) | 0x20 | +0 raw flags (0, 0x00010000, 0x01010000), +4 (n, off) refs, +C id, +10 name len, +14 name, +18 (n, off) items |
| waypoint | 0x3C | +0 index **within its route**, +4 (n, off) links (indices within the route), +C (n, off) refs, +14 id, +18 SGO size, +1C SGO offset, +20 name len, +24 name, +28 vec4 position, +38 raw flags |
| shape | 0x30 | +0 raw flags, +4 type len, +8 type, +C name len, +10 name, +14 (n, off) refs, +1C id, +20 (n, off) shape data (0x3C), +28 SGO size, +2C SGO offset (little-endian, always 0) |
| shape data | 0x3C | as EDF6's 0x38, then +38 raw |
| point | 0x40 | +0 (n, off) refs, +8 id, +C vec4 position, +1C vec4 faces, +2C raw flags, +30 name len, +34 name, +38 SGO size, +3C SGO offset (little-endian, always 0) |
| camera set | 0x30 | +0 (n, off) refs, +8 id, +C raw, +10 name len, +14 name, +18 (n, off) cameras (0x74), +20 int, +24 offset to timeline 1, +28 int, +2C offset to timeline 2 |
| camera | 0x74 | +0 int, +4 int, +8 (n, off) refs, +10 id, +14 float, +18 float, +1C/+2C/+3C/+4C vec4, +5C float, +60 raw, +64 name len, +68 name, +6C SGO size, +70 SGO offset (both little-endian; an empty 0x20-byte SGO) |
| timeline | 0x0C | as EDF6 |
| SGO | | a little-endian SGO blob (pylib/sgo.py); waypoints carry `rmpa_float_WayPointWidth` (float) |

## EDF5 -> EDF6 mapping (`tree_from_legacy`)

* Missing sections become one empty EDF6 section (name `""`, `+0 = -1`).
* Routes: the sets become routes; their waypoints are concatenated into the section's one array; waypoint
  index and links become section-wide (`route base + local index`); the route gets the index list of its
  waypoints. Waypoint id, name, position kept; the SGO becomes properties (`rmpa_float_WayPointWidth`,
  integral floats printed without a fraction; a non-integral value would be printed by Python's shortest float32
  repr, never seen in the games).
* Shape sets / point sets: `+0` becomes 0 (shape sets) / -1 (point sets); shapes and points keep id, type,
  name, positions, data (the 4 raw bytes after each shape data are dropped); every shape gets `Usage = 通常`.
* Cameras: `i0, i4, id -> +8, f14 -> +C, f18 -> +10, 4 vec4, f5C -> +54, name`, timelines unchanged, the empty
  SGO becomes an empty property list; camera set `+20 -> +14`, `+28 -> +1C`.
* Dropped: refs lists (always empty), section / set ids, the raw flag words, EDF5's per-route waypoint
  numbering.
* `map.rmpa` only: 200 links in 3 EDF5 maps and 107 in 3 EDF4.1 maps, all in decorative routes
  (`cable_elec_wire_*`, `grass_nw_GrassDens4`), are >= their route's waypoint count. They name a waypoint of
  another route by a numbering the file does not record (checked against EDF6's re-export of the same maps:
  neither base+link, absolute index nor id matches). They are dropped (listed in the tree's `dropped_links`);
  every remaining link is a valid section-wide index.

## Results (the study, 2026-10-10)

* [0] All 865 EDF6 RMPAs decode only as EDF6, all 567 EDF5/EDF4.1 RMPAs (missions, both games' map.rmpa and
  `<map>.rmpa` members, EDF4.1's own files) only as EDF5, each with every byte accounted for.
* [1] decode6 -> encode6 reproduces 865 of 865 EDF6 RMPAs byte for byte, so any remaining byte difference
  below is a value difference, not layout.
* [2] The 146 missions EDF6 ships under `MISSION/EDF5_OLD_SCRIPT`: **65 byte-identical**. The other 81 differ
  only in content, found by decoding both sides and comparing every field (groups matched by name):
  - 70 floats in 53 files differ in one mantissa bit (69 of them bit 14, in values whose low 7 bits are all set
    and bit 15 set; one -70.8875 vs -71.8875). Not derivable from EDF5's bytes: 143 other floats with exactly
    that bit pattern are unchanged.
  - 17 floats in 15 files: +0.0 in EDF5, -0.0 in EDF6, while 24945 other +0.0 stay +0.0.
  - 47 names in 16 files (45 waypoints, 2 shapes of M075's set `作業用`): EDF5 has a name, EDF6 the empty
    string. Each of the 45 waypoint names repeats the previous waypoint's name (EDF5's exporter repeating the
    last name for an unnamed waypoint), but 2361 other waypoints that repeat the previous name keep it in EDF6,
    so the emptied ones cannot be told apart from EDF5's bytes (`chknames.py`).
  - 19 extra empty groups in EDF6 (point sets like `注目` / `memo`, shape sets like `イベント`, routes like
    `グレイ巡回ルート2`) that EDF5's writer omitted; their names are not in EDF5's file.
  - 1 shape (M061) without `Usage` in EDF6.
  EDF.dll's walker (emulated) runs cleanly over all 146 converted files and faults on all 146 EDF5 originals.
* [3] EDF4.1 (EDF5's `MISSION/EDF4.1`): 126 of 126 `MISSION.RMPA` (and 7 of 7 `BEFORE.RMPA`, plus
  `M002/AFTER.RMPA` in out/) convert; each output decodes with decode6 with nothing left over, EDF.dll's walker
  runs cleanly over it, and every point (set, name, id, position, facing), waypoint (route, name, id, position,
  links), shape (type, name, id, data) and camera (name, vectors, timelines) equals the input's.
* [4] `map.rmpa` of the 21 maps EDF5 and EDF6 share: **1 byte-identical** (IG_CAVE503). EDF6 re-saved the
  others in its editor: object ids shifted by one constant per map, waypoints / routes / point groups / shapes
  added (e.g. `NavMeshSeed` routes), link lists of 8 maps in another order (same sets), floats as above. No
  layout difference (see [1]). The 16 EDF4.1 mission maps' `map.rmpa`: 16 of 16 convert, decode, walk and keep
  everything (except the dropped cross-route links above).

## Open points

* Not run in game. The emulated walker is EDF.dll's own swap pass (the code that crashed); the later reader
  that builds routes/shapes from the swapped data was not traced.
* Cameras: no EDF6 RMPA has one, so the EDF6 camera layout comes from EDF.dll's walker only (M014 and M192
  of EDF4.1 have cameras; their conversions pass the walker).
* The dropped cross-route wire/grass links (map.rmpa only).
* EDF5's MACs also hold a `<map>.rmpa` member that EDF6's MACs no longer have; it converts like any RMPA but
  whether EDF6 reads it was not checked.
