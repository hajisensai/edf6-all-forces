"""EDF4.1's SK mission maps (the sk_* ones, a heightfield `<model>_fld2mgrid.shfb` for ground) in EDF6's form: the
terrain a map piece of EDF6's own kind, an FMB mesh (pylib/fmb_build.py) with its collision in the map's MAD
(pylib/hkcms_build.py). tools/edf41_maps.py calls this for the maps in SK_MAPS; docs/edf41-sk-terrain.md has the
research (what EDF6 has, what the field holds, every choice and what is open).

  terrain(mac, rab, e6, map_name) -> Terrain      the MAC / RAB edited, the FMB, and the lattice the collision needs
"""
from __future__ import annotations

import os
import re
import struct
import sys
from dataclasses import dataclass
from typing import Protocol

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bigmap  # noqa: E402
import fmb_build  # noqa: E402
import fmex  # noqa: E402
import hkcms_build  # noqa: E402
import mdb  # noqa: E402
import sgo  # noqa: E402
import sk_terrain  # noqa: E402

DONOR_FMB = 'IG_HAIKYOPLANE01.FMB'    # a stock single-layer regular-grid piece: the constant trailing tables
FLAT_NORMAL_RAB = 'IG_CAVE603.RAB'    # its RAB holds EDF6's flat normal map
FLAT_NORMAL = 'ig_flat_normal.dds'
STEP = sk_terrain.STEP_SAMPLES        # lattice step in samples (4 m)
CULL_NEAR, CULL_FAR = 100.0, 1000.0   # def +0x18 / +0x1C of every stock .fmep def
SGO_VERSION = 258                     # externals.sgo's, in every stock map


class Reader(Protocol):
    def read(self, name: str) -> bytes: ...


@dataclass
class Terrain:
    """What the SK conversion makes of a map."""
    piece: str                  # lower case model name of the terrain piece: 'sk_heigen02_fld2mgrid'
    fmb: bytes                  # the piece's FMB (MAP/<PIECE>.FMB)
    mac: bigmap.Marc            # the MAC with the field's members gone, externals.sgo added, the def and record edited
    rab: bytes                  # the map's RAB with the colour and normal textures the terrain loads
    heights: np.ndarray         # the vertex lattice (rows, cols), world metres (the record's height folded in)
    x0: float                   # world x / z of lattice vertex (0, 0)
    z0: float
    step: float
    shfb: sk_terrain.Shfb


def find_shfb(mac: bigmap.Marc) -> str:
    names = [n for n, _ in mac.files if n.lower().endswith('.shfb')]
    if len(names) != 1:
        raise ValueError(f'{len(names)} .shfb members in the map')
    return names[0]


def piece_of(map_name: str) -> str:
    """The terrain piece's model name: the map's own (4.1 names some after another map: SK_SANGAKU01's field is
    sk_sangaku05_fld2mgrid), so that the files a map installs are known without reading it (files_of)."""
    return map_name.lower() + '_fld2mgrid'


def fmb_file(map_name: str) -> str:
    """The piece's file name in Mods/MAP."""
    return piece_of(map_name).upper() + '.FMB'


def color_slices(names: list[str]) -> dict[int, int]:
    """The field's texture index (into the material names it lists) -> slice of the colour array `.cols.dds`: the
    array holds every name that is not a normal map (`_n` / `_nm`), in the list's order (checked on the 7 maps: the
    slice count of each cols.dds equals the number of such names, and no layer's colour or multiplier index points at
    a normal map)."""
    out: dict[int, int] = {}
    for i, n in enumerate(names):
        if not n.lower().endswith(('_n', '_nm')):
            out[i] = len(out)
    return out


def layers_of(s: sk_terrain.Shfb) -> list[fmb_build.Layer]:
    """EDF6's layer records for the field's layers: the same colour and multiplier slices of the same array, tiled
    1 / scale metres (the field's texture matrices scale the world position by it)."""
    slice_of = color_slices(s.layer_names)
    out = []
    for l in s.layers:
        if l.color not in slice_of or (l.multip >= 0 and l.multip not in slice_of):
            raise ValueError(f'a layer paints with a normal map ({l.color}, {l.multip}) in {s.layer_names}')
        out.append(fmb_build.Layer(slice_of[l.color], slice_of[l.multip] if l.multip >= 0 else -1,
                                   1.0 / l.scale_color[0], (1.0 / l.scale_multip[0]) if l.multip >= 0 else 0.0))
    return out


def edit_mapb(mapb: bytes, shfb_name: str, piece: str) -> bytes:
    """The map's mapb with the field's def made a terrain piece: its name `<piece>.fmep` (in the string table and
    the def's own SGO, which both spell it: .shfb and .fmep are the same length), its cull distances the stock
    .fmep defs', the placement at the origin (the FMB holds world coordinates, as every stock piece). Returns the
    bytes and (through bigmap) the field's record is the only one that moved."""
    old, new = shfb_name.encode('utf-16le'), (piece + '.fmep').encode('utf-16le')
    if len(old) != len(new):
        raise ValueError(f'{shfb_name} and {piece}.fmep differ in length (the name is replaced in place)')
    if old not in mapb:
        raise ValueError(f'{shfb_name} is not in the mapb')
    b = bytearray(mapb.replace(old, new))
    mb = bigmap.MapB(bytes(b))
    defs = [k for k in range(mb.def_n) if mb.def_name(k) == piece + '.fmep']
    if len(defs) != 1:
        raise ValueError('the field is not one def')
    d = mb.def_off + defs[0] * bigmap.DEF
    struct.pack_into('<2f', b, d + 0x18, CULL_NEAR, CULL_FAR)
    moved = 0
    for i in range(mb.rec_n):
        if mb.def_index(i) == defs[0]:
            struct.pack_into('<3f', b, mb.rec_off + i * bigmap.REC + 4, 0.0, 0.0, 0.0)
            moved += 1
    if moved != 1:
        raise ValueError(f'{moved} records place the field')
    return bytes(b)


def edit_mapo(mapo: bytes, record: int, plant_heights: np.ndarray | None = None) -> bytes:
    """The map's mapo with the ground record's entry as a stock .fmep piece's: its +0 points at the pair {count, 8}
    followed by the count heights of the piece's plants (pylib/fmex.py: one f32 per occupied cell of the FMB's plant
    groups; IG_HEIGEN507's ground: 159441), or the empty {0, 8} of a piece without plants (IG_CAVE503: entry 0 +0 = 28,
    the pair right after the entry array) - the loader reads it for every piece, and the 4.1 entry of a field has none.
    The pair goes at the end of the file: nothing else moves, no offset between two blocks changes."""
    mo = bigmap.MapO(mapo)
    at = (len(mapo) + 3) & ~3
    data = np.asarray(plant_heights if plant_heights is not None else [], dtype='<f4')
    out = bytearray(mapo) + bytes(at - len(mapo)) + struct.pack('<ii', len(data), 8) + data.tobytes()
    ent = mo.ent_off + record * bigmap.ENT
    if struct.unpack_from('<i', out, ent)[0]:
        raise ValueError('the ground record has a grass pair already')
    struct.pack_into('<i', out, ent, at - ent)
    return bytes(out)


GRASS_GROUP = '草・花'   # the stock grass group's name (IG_HEIGEN507 group 0)
GRASS_COLOR = (0.0, 1.0, 0.502, 0.0)    # an editor colour, as the stock grass types carry one


def grass_group(veg: sk_terrain.Vegetation, ox: float, oz: float) -> fmex.Group:
    """The field's grass and flowers (vegetation types of kind 1) as one EDF6 plant group over the field: the same cells
    (row = z, column = x from the field's origin; selftest edf41_sk_grass_real: the mean slope under the grass cells of
    SK_VALLEY01 / SK_SANGAKU01 is 0.10 / 0.09 against 0.57 / 0.64 for all cells this way round, 0.55 / 0.45 transposed) and
    jitters, a type per grass texture. The 4.1 size is a base and a random extra on it; EDF6 draws between a min and a
    max, taken as base - extra .. base + extra (the second triple is 0 for the flowers, so they keep one size).
    The models (trees, bushes, small objects: kind 2) are model_groups', and SK_CRATER01's kind 1 entry naming a tree
    (sk_tree10m01.trb), which no cell uses, is left out."""
    grass = [k for k, t in enumerate(veg.types) if t.kind == 1 and t.name.lower().endswith('.dds')]
    kind = np.full(veg.kind.shape, 0xFF, dtype=np.uint8)
    types = []
    for new, k in enumerate(grass):
        t = veg.types[k]
        kind[veg.kind == k] = new
        lo = tuple(max(0.0, a - b) for a, b in zip(t.size, t.extra))
        hi = tuple(a + b for a, b in zip(t.size, t.extra))
        types.append(fmex.PlantType(0, t.name.rsplit('.', 1)[0], t.name, GRASS_COLOR, lo, hi, t.params))
    return fmex.Group(GRASS_GROUP, veg.cell, (ox, oz), types, kind, veg.jitter)


# the stock model groups (IG_HEIGEN507 / IG_KAIGAN502 groups 1..3), each type kind 1 named <model>.tree (the MAC's
# <model>.trb) or <model>.mls (the MAC's <model>.mlb), size (1, 1, 1) as 4.1 gives (GrowObjects 0x140230 draws the
# model at the cell's jittered position turned by the jitter's angle, the type's size unread)
TREE_GROUP, BUSH_GROUP, SMALL_GROUP = '樹木', '低木', '小物ランダム'
MODEL_FILE = {'.trb': '.tree', '.mlb': '.mls'}


def model_groups(veg: sk_terrain.Vegetation, ox: float, oz: float, members: set[str]) -> list[fmex.Group]:
    """The field's models (vegetation kind 2: `.trb` trees and bushes, `.mlb` small objects) as EDF6 plant groups, laid
    out as the stock ones: trees, bushes (a `bush` model), small objects; a group without cells is left out. Every model
    must be a member of the MAC (`members`, lower case: 4.1's maps carry them; their .trb in EDF6's form,
    map_legacy.trb_from_legacy)."""
    split: dict[str, list[int]] = {TREE_GROUP: [], BUSH_GROUP: [], SMALL_GROUP: []}
    for k, t in enumerate(veg.types):
        if t.kind != 2:
            continue
        name = t.name.lower()
        ext = name[name.rfind('.'):]
        if ext not in MODEL_FILE:
            raise ValueError(f'vegetation model {t.name}: not a .trb / .mlb')
        if name not in members:
            raise ValueError(f'vegetation model {t.name} is not in the MAC')
        split[SMALL_GROUP if ext == '.mlb' else BUSH_GROUP if 'bush' in name else TREE_GROUP].append(k)
    out = []
    for gname, ks in split.items():
        kind = np.full(veg.kind.shape, 0xFF, dtype=np.uint8)
        types = []
        for new, k in enumerate(ks):
            t = veg.types[k]
            kind[veg.kind == k] = new
            stem, ext = t.name.rsplit('.', 1)
            types.append(fmex.PlantType(1, stem, stem + MODEL_FILE['.' + ext.lower()], GRASS_COLOR, t.size, t.size,
                                        t.params))
        if (kind != 0xFF).any():
            out.append(fmex.Group(gname, veg.cell, (ox, oz), types, kind, veg.jitter))
    return out


def field_origin(mapb: bytes, shfb_name: str) -> tuple[float, float, float]:
    """The placement of the field in the (4.1 layout, converted) mapb: its sample (0, 0)."""
    mb = bigmap.MapB(mapb)
    for i in range(mb.rec_n):
        if mb.def_name(mb.def_index(i)) == shfb_name:
            return mb.pos(i)
    raise ValueError('the field is not placed')


def convert_rab(rab: bytes, cols_name: str, map_name: str, e6: Reader) -> bytes:
    """The map's RAB with `<cols>.cols.dds` (the field's colour array) as `<map>_color.dds` and EDF6's flat normal
    as `<map>_normal.dds`: the two textures EDF6's terrain looks up by the map's name (EDF.dll 0x152A06: map name +
    '_color.dds' / '_normal.dds', 0x18DD30). Both go in the TEXTURE folder where 4.1's cols.dds already was."""
    r = mdb.rab_read(rab)
    cols = [f for f in r.files if f.name == cols_name]
    if len(cols) != 1:
        raise ValueError(f'{cols_name} is not in the RAB')
    flat = [f for f in mdb.rab_read(e6.read(FLAT_NORMAL_RAB)).files if f.name == FLAT_NORMAL]
    if len(flat) != 1:
        raise ValueError(f'{FLAT_NORMAL} is not in {FLAT_NORMAL_RAB}')
    base = map_name.lower()
    cols[0].name = f'{base}_color.dds'
    r.files = [f for f in r.files if not f.name.lower().endswith('.norm.dds')]
    mdb.insert_member(r, mdb.RabFile(f'{base}_normal.dds', cols[0].folder, cols[0].flag, flat[0].stored))
    return mdb.rab_write(r)


def terrain(mac: bigmap.Marc, rab: bytes, e6: Reader, map_name: str) -> Terrain:
    """Map `map_name`'s terrain piece (see the module doc). `mac` is the converted MAC (map_legacy), `rab` the converted
    RAB; neither is changed."""
    shfb_name = find_shfb(mac)
    piece = piece_of(map_name)
    members = dict(mac.files)
    ox, oy, oz = field_origin(members['map.mapb'], shfb_name)
    s = sk_terrain.parse(members[shfb_name])
    step = s.cell[0] * STEP
    heights = sk_terrain.lattice(s) + oy
    weights = s.blend[::STEP, ::STEP]
    weights = weights / np.maximum(weights.sum(axis=2, keepdims=True), 1e-9)
    fmb_bytes = fmb_build.build(e6.read(DONOR_FMB), heights, weights, ox, oz, step, layers_of(s))
    veg = sk_terrain.vegetation(members[shfb_name], s.extent[0])
    plants = [grass_group(veg, ox, oz)] + model_groups(veg, ox, oz, {n.lower() for n, _ in mac.files})
    fmb_bytes = fmex.with_plants(fmb_bytes, plants, veg.jitters)
    grass_heights = np.concatenate([fmex.plant_heights(g, veg.jitters,
                                                       lambda x, z: fmex.heights_at(heights, ox, oz, step, x, z))
                                    for g in plants])
    # the field's own table (the .shfb member, 4.1's vegetation with it) is dropped: EDF6 has no reader for it
    out = bigmap.Marc(mac.version, [(n, d) for n, d in mac.files
                                    if not n.lower().endswith(('.shfb', '.cols.sgo', '.norm.sgo'))])
    mapb = edit_mapb(members['map.mapb'], shfb_name, piece)
    out.put('map.mapb', mapb)
    mbx = bigmap.MapB(mapb)
    ground = [i for i in range(mbx.rec_n) if mbx.def_name(mbx.def_index(i)) == piece + '.fmep']
    out.put('map.mapo', edit_mapo(members['map.mapo'], ground[0], grass_heights))
    at = [n for n, _ in out.files].index('map.mapb')
    out.files.insert(at, ('externals.sgo', sgo.write_depth_first(SGO_VERSION, {'resource': [piece + '.fmb']})))
    cols_name = [f.name for f in mdb.rab_read(rab).files if f.name.lower().endswith('.cols.dds')]
    if len(cols_name) != 1:
        raise ValueError('the RAB has no single colour array')
    return Terrain(piece, fmb_bytes, out, convert_rab(rab, cols_name[0], map_name, e6), heights, ox, oz, step, s)


# ------------------------------------------------------------------------------------------------ object bodies

BODY_NAME = re.compile(rb'[a-z_][a-z0-9_]{2,60}\.[A-Za-z0-9_|]{2,80}')


def legacy_body_names(hkt41: bytes) -> dict[str, list[str]]:
    """The static bodies of a 4.1 MAD's collision (a Havok 2014 binary tagfile; read by its strings): model -> the body
    names '<model>.<node>' it holds. Each body's name is a string of the file (a stray string that looks like one is
    dropped later: it names no model of the map's RAB)."""
    out: dict[str, list[str]] = {}
    for m in sorted(set(BODY_NAME.findall(hkt41))):
        name = m.decode('ascii')
        out.setdefault(name.split('.', 1)[0], []).append(name)
    return out


def model_triangles(rab: mdb.Rab, model: str) -> np.ndarray | None:
    """Every triangle of `<model>.mdb` of the RAB in model space (n, 3, 3), None when the RAB has no such model: a mesh's
    vertices are in its bone's frame, so each goes through the bone's bind pose (pylib/mdb.py bind_world)."""
    hit = [f for f in rab.files if f.name.lower() == model + '.mdb']
    if not hit:
        return None
    md = mdb.mdb_read(hit[0].stored)
    world = mdb.bind_world(md)
    parts = []
    for o in md.objects:
        for me in o.meshes:
            pos = mdb.read_elem(me, 'position')
            if not pos or not me.indices:
                continue
            p = np.array(pos, dtype=np.float64)[:, :3]
            m = np.array(world[o.bone], dtype=np.float64).reshape(4, 4)
            p = p @ m[:3, :3] + m[3, :3]
            parts.append(p[np.frombuffer(me.indices, dtype='<u2').reshape(-1, 3)])
    return np.concatenate(parts) if parts else None


def object_bodies(hkt41: bytes, rab: bytes) -> tuple[list[hkcms_build.Body], list[str]]:
    """The object collision of a 4.1 map as EDF6 bodies: for every body name the 4.1 MAD holds whose model the RAB has,
    one body (the first of its names) with all the model's triangles: the map loader finds an object's bodies by the
    model prefix of their names ('<model>.', docs/map-format.md) and places them all with the object's matrix, so a
    model's pieces are one body here (the union is the same). Returns the bodies and the models the RAB does not hold (their bodies are left out)."""
    r = mdb.rab_read(rab)
    have = {f.name.lower()[:-4] for f in r.files if f.name.lower().endswith('.mdb')}
    bodies: list[hkcms_build.Body] = []
    missing: list[str] = []
    for model, names in legacy_body_names(hkt41).items():
        if model not in have and model[1:] in have:
            # the string before a name is its length (one byte): a length that is a letter ('f' = 102) was read as the
            # name's first character
            model, names = model[1:], [n[1:] for n in names]
        if model not in have:
            missing.append(model)
            continue
        tris = model_triangles(r, model)
        if tris is not None:
            bodies.append(hkcms_build.Body(names[0], tris))
    return bodies, missing


# ------------------------------------------------------------------------------------------------ collision

DONOR_MAD = 'IG_HEIGEN601.MAD'   # EDF6's own ground collision: the type tables, libraries and physics system the writer keeps


def collision(t: Terrain, hkt41: bytes, e6: Reader) -> tuple[bytes, list[str]]:
    """The map's MAD: the terrain lattice as the ground body and the 4.1 map's object bodies rebuilt from the RAB's
    models (object_bodies), written by pylib/hkcms_build.py on EDF6's own collision file as the donor. Returns the
    MAD and the models whose bodies were left out."""
    donor = bigmap.Marc.parse(e6.read(DONOR_MAD)).get('collision.hkt')
    bodies, missing = object_bodies(hkt41, t.rab)
    hkt = hkcms_build.build_collision(donor, t.piece, t.heights, t.x0, t.z0, t.step, bodies)
    return hkcms_build.write_mad(hkt), missing
