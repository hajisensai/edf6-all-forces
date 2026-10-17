# EDF4.1's SK maps on EDF6: a heightfield turned into EDF6 terrain

Seven of EDF4.1's mission maps (SK_HEIGEN02, SK_VALLEY01, SK_KAIGAN01, SK_SANGAKU01, SK_SANGAKU02, SK_STEPMOUNTAIN01,
SK_CRATER01) have a ground EDF6 cannot load: a heightfield, `<model>_fld2mgrid.shfb`, drawn by 4.1's own terrain class.
`tools/edf41_maps.py` converts them at install like the nine other 4.1 maps (`SK_MAPS`); this is what was found and what
the converter does. Code: `pylib/sk_terrain.py` (the field), `pylib/fmb_build.py` (the render mesh), `pylib/hkcms_build.py`
(the collision), `pylib/sk_map.py` (the map around them). Tests: `tools/selftest.py` sk_terrain_real, fmb_build_real,
hkcms_build_real, edf41_sk_maps_real. Not run in game (see "What to check in game").

## 1. What EDF6 has

EDF6 has no heightfield terrain. EDF.dll holds no SHFB loader (no `shfb` / `fld2mgrid` / `cols.sgo` string, UTF-8 or
UTF-16; the only height field names are Havok class names). Its terrain is the FMB piece (docs/fmb-format.md): a mesh of
leaves placed by a `.fmep` def, textured by layers of a colour / normal texture array named after the map
(`<map>_color.dds`, `<map>_normal.dds`, EDF.dll 0x152A06 builds the names, 0x18DD30 finds them in the map's RAB), shaded
by `MAP_FIELD_RENDER<n>_<m>_14_<pass>` (DX11.cpk), with the collision as a compound of compressed meshes in the map's
MAD, found by the model name (`<model>.<node>` bodies, docs/map-format.md). The Havok migration the plugin runs on 4.1's
collision (src/legacymap.cpp, hkpPhysicsMigrationUtils) does not support height fields, so converting the field to EDF6's
own kinds was the only route: **(b)**, mesh + compressed mesh collision, with the field's layers as EDF6 terrain layers.
Both games' terrain pixel shaders are the same design (a colour array + normal array, per-layer tiled textures with a
multiplier, blend weights at 4 corners per cell), which is why the layers carry over.

## 2. The field (`pylib/sk_terrain.py`)

Decoded from EDF41.exe: the endian fixup 0x1400F37A0 lists every field and offset, the terrain constructor 0x1400F4B00
uploads the arrays, and the vertex shader embedded at 0x9C6030 (disassembled with the d3dcompiler_47.dll 4.1 ships) says
what they are. Header (u32 / f32 at): 0x08/0x0C samples 1001 x 1001, 0x18/0x1C cell 2.0 m (all seven maps: 2000 m), 0x20 33
blocks of 32 cells, 0x30/0x34 lowest / highest height, 0x38 the blocks (0x20 each: two lists), 0x3C/0x40 the layers,
0x44/0x48 the material names, 0x50 the field, 0x54/0x58 the blend pair list, 0x5C/0x60 the UTF-16 name pool.

* **Heights**: 0x50 holds W x H x (u16, u16). The shader does `f16tof32` on the low half: **the height is an IEEE half
  float in metres** (SK_HEIGEN02: -10.2266 .. 47.25, the header's -10.228 .. 47.264). The high half is the vertex's
  material (the layer of its largest weight, 99.9995 % of all vertices).
* **Placement**: the map's mapb places the `.shfb` as a record: SK_HEIGEN02 (-1000, 0, -1000), SK_VALLEY01 (-1000, 118.4,
  -1000). Sample (row, col) is at that origin + (col * 2, h, row * 2): **row = z, column = x**. Proof: the objects the
  maps place were snapped to the field, and with that placement the median distance of every placed object to the decoded
  ground is 0.000 m (SK_HEIGEN02), 0.005 (KAIGAN01), 0.011 (SANGAKU01), 0.023 (SANGAKU02), 0.025 (STEPMOUNTAIN01), 0.28
  (CRATER01), 0.29 (VALLEY01); 74 - 100 % lie within a metre (sk_terrain_real checks HEIGEN02 and VALLEY01).
* **Layers**: 0x3C holds one 0x14 struct per layer (2 texture settings of 0x30: the texture index and the tiling `uv =
  metres * scale`; a layer may have just the colour). The indices are into the names list at 0x44 (the materials, `_n` /
  `_nm` are normal maps); the colour array `.cols.dds` (a DX10 BC1 array: SK_HEIGEN02 8 slices, 1024^2) holds exactly the
  names that are not normal maps in the list's order, and no layer's colour or multiplier index names a normal map.
* **Blend weights**: the pair list at 0x54 holds (slot, f16 weight) runs; block (by, bx) (0x850 + 0x20 * k) lists the layers
  it uses and, per vertex, the start of the vertex's run (one pair per used layer and a closing weight 0).

## 3. Route and file layouts

**Lattice.** Every 2nd sample: 501 x 501 vertices every 4 m (`sk_terrain.STEP_SAMPLES`; 2 m would be 4x the triangles,
the mesh and the collision files, and the install time; the field's relief is smooth at that scale, and render and
collision are the same lattice so a vehicle never floats or sinks against the picture). World coordinates in the files, the
ground record at the origin (as every stock piece), the field record's height folded into the vertices.

**`MAP/<MAP>_FLD2MGRID.FMB`** (`fmb_build.py`, built like IG_HAIKYOPLANE01, the stock single layer regular grid piece,
which is also the donor of the two constant trailing tables). 625 leaves of 20 x 20 cells (80 m, 21 x 21 vertices, 800
triangles wound as the donor's, normals from the lattice's gradient), a post-order AABB tree split along the longer side;
each leaf is a 0x30 header (triangle count at 0x1C), one 0x3C mesh record, the indices, the splat grid (**one 0x50 record
per 4 m cell: list index 0 + the four corners' 16 weight bytes, each corner summing to 255**), one 16 byte layer list and
the vertices. A leaf lists its layers by weight, at most 5 (the most any stock leaf lists; the shader variants go to 7); its
mesh record carries the layer count, `n * 0x101` and at +0x28 the index of its shader record in the file's shader
table: the (n, 1, pass 0, flag 0) record, as every one of the 9753 stock meshes (until 2026-10-17 this was written as the
number of texture slices, which pointed the leaves at flag 6 or other-count records: the black blocks of the feedback,
docs/feedback-2026-10-17-edf41-maps.md). The trailing tables: the layer table (one 0x50 record per layer: orientation of the colour / multiplier
tile, colour slice, multiplier slice (-1 none), normal slice 0, the stock 6.0 and 1.0 x 3; the orientations are rotation 90
degrees about x with the tile size = 1 / scale), the shader table (stock pattern: per pass the counts at flag 6, the smallest
again, the counts at 0), two constant tables from the donor. Compression is a greedy LZSS (the game's CMPL), not Okumura's
choice (0.04 s a leaf instead of 15 s per 1.26 MB). 10.5 MB, 20 s.

**`MAP/<MAP>.MAC`**: the legacy conversion (map_legacy), its ground def made `<map>_fld2mgrid.fmep` (the name replaced in the
string table and the def's SGO: the same length) with the stock cull distances (100 / 1000) and the record at the origin, the
mapo entry of that record given its plant heights (`{count, 8}` + count floats at the end of the file, pylib/fmex.py), an
`externals.sgo` `{'resource': ['<map>_fld2mgrid.fmb']}` (written with `sgo.write_depth_first`, which reproduces all 50
stock ones), the `.shfb`, `.cols.sgo` and `.norm.sgo` members dropped. map.rmpa as for the other maps (rmpa_legacy).

**`MAP/<MAP>.RAB`**: the legacy conversion with the colour array renamed `<map>_color.dds` (TEXTURE folder where 4.1 had
it) and EDF6's own flat normal map (`ig_flat_normal.dds` of IG_CAVE603.RAB) as `<map>_normal.dds`. 4.1's normal arrays
(`.norm.dds`, 4 slices on some maps, 1 on others) are dropped: which slice goes with which layer is not decoded.

**`MAP/<MAP>.MAD`** (`hkcms_build.py`): the ground as one body `<piece>.<piece>`, a compound of 2500 tiles of 10 x 10
cells, one compressed mesh each (one section, one quad per cell, all vertices shared), written on the player's own
IG_HEIGEN601 collision as the donor (its type tables, libraries and physics system; nothing of it is stored here), plus the
objects' bodies: the names of the 4.1 MAD's bodies (a Havok 2014 binary tagfile, read by its strings) whose model the RAB
holds, one body of that name with all of the model's triangles (the models' `_hk` meshes are the collision pieces; the
loader finds an object's bodies by their `<model>.` prefix and places them with the object's matrix, so one body is the
same collision). Nothing of the 4.1 collision stays: EDF6 loads the file like any of its maps, no migration. 19 MB, 17 s.

## 4. Verified offline

Everything below is in the selftests, on the real files.
* The field: dimensions, half float range == the header, material == argmax of the weights, weights sum to 1, the colour
  array's slice count == the names that are not normal maps, objects stand on the ground (section 2).
* The FMB parses with `fmb.parse` (table, 4-aligned chunks, tail offsets), `fmb.selftest` passes (leaf and node AABB bit
  exact; both reference CMPL decoders agree; the identity and +5 m and local edits round trip), every leaf's header and
  mesh record fields are in the stock leaf's places with the stock's values where it has them (counts, rel offsets, grid
  fields, cell / origin, the layer-count words, a 16 byte layer list), the four corners of every splat cell sum to 255, the
  trailing tables have the stock sizes and the donor's constant bytes.
* The MAD: `hkcms_build.selftest`: the writer re-emits the donor and 33 stock MADs byte for byte, a built collision parses
  with hktag / hkcms, its triangles decode to the lattice within 1e-5 m, all face up, `bound_problems` == [], and a tile mesh
  has the stock ground tile's item structure; its bytes differ from a stock mesh only in data fields.
* The map: `bigmap.verify`, the def / record / mapo entry / externals / members as above, the FMB's vertices == the lattice,
  the MAD's ground compound has 2500 instances and its objects are listed, the RAB's textures.

## 5. What to check in game (not run)

1. The map loads at all (SK_HEIGEN02 first: the simplest). The three things it can die on that offline cannot tell: the
   FMB loader (a leaf the stock files do not have: 441 vertices / 20 x 20 splat cells), the shader permutation of a
   multi-layer leaf (a layer count / slice count word that is wrong would pick a shader variant that does not match the
   leaf), the collision body name (`<piece>.<piece>`: no collision at all if the loader does not find it).
2. The ground shows the field's layers (grass / soil / cliff blend with the colour array), not black or one layer.
3. Vehicles and soldiers stand on and drive on the ground (render and collision are one lattice) and rocks, knolls and
   the bridge of SK_VALLEY01 collide.
4. AI: the navmesh is the donor EDF6 map's (as for the other converted maps), which does not match this ground: ground units
   walk the donor's routes (open issue, unchanged).

## 6. Open

* Vegetation: 4.1's field holds its plants in its own table (sk_terrain.vegetation); EDF6 grows plants over a `.fmep`
  piece from the FMB's table 2 / 3 and the heights in the mapo (pylib/fmex.py, reverse engineered 2026-10-17). The grass
  and flowers (kind 1 `.dds`) are converted as one group; the trees, bushes and small objects (kind 2 `.trb` / `.mlb`)
  are not yet (the `.trb` / `.mlb` members stay in the MAC unused): the group format is the same, what is unproven is
  4.1's `.trb` on EDF6's GrowObjects path.
* Normal maps of the layers (`.norm.dds`), the layers' `m_correction`-like value (the stock 6.0 is used) and the ground's
  surface materials (4.1's per-vertex material to EDF6's collision palette: `hkcms_build.build_collision(values=)` takes
  them, the value meanings are not known) are not carried.
* `triangleIsInterior` bits of the collision meshes are all 0 (stock sets some); the tiles are all shared vertices and 100
  quads, which stock meshes of that size do not have (they mix packed and shared). Both decode correctly and pass the
  bounds checks, neither is proven at run time.
* 4.1 models whose `.mdb` is not in the map's RAB (SK_KAIGAN01 and SK_SANGAKU02's `sk_knoll0*`): their collision bodies are
  left out.
* Install time: about a minute a map on the developer's machine (legacy conversion 11 s, field -> FMB 23 s, MAD 17 s, the
  rest copying); seven maps about 8 minutes.
