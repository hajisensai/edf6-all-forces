"""Writes a bigger test-range map (the user, 2026-10-04/05: "a map of many plains stitched together, real physics all
over") into <game>/Mods:

  Mods/MAP/IG_HEIGEN601.MAC   the plain (ig_Heigen601, the test range's map) with its whole block copied around it:
                              the ground (ig_heigen507_2, 2500 m square), the four edge pieces round it (the 500 m
                              ring that closes it off: 3500 m across) and its far-only ground, every copy a placement
                              record of its own (pylib/bigmap.py: the game builds each like any map piece, its model
                              and its collision, by name, at its own place); the far mountain ring is sunk out of
                              sight (its ring would stand inside the bigger map). A RADIUS of 1 is 3 x 3 blocks
                              (+-5250 m), 2 is 5 x 5 (+-8750 m).
  Mods/MAP/IG_HEIGEN601.MAD   its collision, Mods/MAP/IG_HEIGEN601.RAB its far-only ground, Mods/MAP/
  IG_HEIGEN601_ENKEI*.FMB     its four edge pieces' terrain: the block made seamless (pylib/seams.py). The stock
                              block was never meant to repeat, its east and west (north and south) edges differ by up
                              to 33 m, so neighbouring copies met in see-through steps; each of these three is moved
                              near the block's outer edge so the opposite edges match, the middle ground untouched.
  Mods/Plugins/EDF6VehicleCrew.ini  BigWorld (the physics world's half size, src/bigworld.cpp) set to cover it; --remove
                              sets it back to 0.

Only the test range's map: missions on other maps are untouched. The navmesh still covers only the middle block (the
ground units' paths), the map's own move area is widened by the plugin with BigWorld.

  python tools/make_bigmap.py [game dir] [--radius N]   write / refresh
  python tools/make_bigmap.py [game dir] --remove       release it
  python tools/make_bigmap.py [game dir] --check        build everything in memory and report the seams (writes nothing)
"""
from __future__ import annotations

import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import bigmap  # noqa: E402
import cpk  # noqa: E402
import crilayla  # noqa: E402
import fmb  # noqa: E402
import hkcms  # noqa: E402
import hktag  # noqa: E402
import ledger  # noqa: E402
import mdb  # noqa: E402
import seams  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'bigmap'   # pylib/ledger.py
MAP_FILE = 'IG_HEIGEN601.MAC'
COLLISION_FILE = 'IG_HEIGEN601.MAD'
MODELS_FILE = 'IG_HEIGEN601.RAB'
FAR_MODEL = 'ig_far_heigen507_2.mdb'     # the far-only ground (record ig_far_heigen507_2.mdx), in MODELS_FILE
RING_TAG = 'enkei'                       # the four edge pieces: ig_heigen601_enkei{up,bottom,left,right}
FAR_EDGE_EPS = 12.0      # m: the far ground is decimated, its edge vertices stray up to 11 m off the edge line
FAR_CORNER_TOL = 3.0     # m: and two of its corners have no vertex
RING_FILES = [f'IG_HEIGEN601_ENKEI{s}.FMB' for s in ('BOTTOM', 'LEFT', 'RIGHT', 'UP')]   # their near terrain
SEAM_TOL = {'collision': 0.01, 'terrain': 0.01, 'far': 1.0}   # m: largest opposite-edge difference left (quantisation, half floats)
ARCHIVE = 'Chunk02.cpk'
BLOCK = 3500.0           # m: the ground (2500) and its edge ring (500 a side)
SINK = -40000.0          # m: the far mountain ring's drop (out of the far camera's 20 km)
WORLD_MARGIN = 750.0     # m of physics world past the last block's edge
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')


def stock_file(root: str, name: str) -> bytes:
    """A stock MAP file, from the game's Chunk02.cpk (only read)."""
    c = cpk.Cpk(os.path.join(root, ARCHIVE))
    e = c.index[('MAP', name)]
    with open(c.path, 'rb') as h:
        h.seek(c.base + int(e['FileOffset']))
        data = h.read(int(e['FileSize']))
    return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data


def pieces(mac: bytes) -> tuple[list[int], int | None]:
    """The block's records (the ground, its edge pieces, its far-only ground: the always-loaded terrain, def type 3)
    and the far mountain ring's, by their models' names."""
    mb = bigmap.MapB(bigmap.Marc.parse(mac).get('map.mapb'))
    block, ring = [], None
    for i in range(mb.rec_n):
        k = mb.def_index(i)
        name = mb.def_name(k).lower()
        if mb.def_type(k) != 3 or 'heigen' not in name:
            continue
        if 'farmt' in name:
            ring = i
        else:
            block.append(i)
    if not block:
        raise ValueError(f'{MAP_FILE}: no terrain pieces found')
    return block, ring


def sink(mac: bytes, i: int) -> bytes:
    """`mac` with record `i` moved SINK down."""
    import struct
    marc = bigmap.Marc.parse(mac)
    raw = bytearray(marc.get('map.mapb'))
    mb = bigmap.MapB(bytes(raw))
    at = mb.rec_off + i * bigmap.REC + 4
    x, y, z = struct.unpack_from('<3f', raw, at)
    struct.pack_into('<3f', raw, at, x, y + SINK, z)
    marc.put('map.mapb', bytes(raw))
    return marc.build()


def build_map(root: str, radius: int) -> bytes:
    if not 1 <= radius <= 3:
        raise ValueError('radius 1..3')
    stock = stock_file(root, MAP_FILE)
    block, ring = pieces(stock)
    copies = [(i, gx * BLOCK, 0.0, gz * BLOCK) for gx in range(-radius, radius + 1) for gz in range(-radius, radius + 1)
              if (gx, gz) != (0, 0) for i in block]
    out = bigmap.make_tiled(stock, copies)
    problems = bigmap.verify(out, stock)   # every original record as it was, every copy resolving
    if problems:
        raise ValueError(f'{MAP_FILE}: ' + '; '.join(problems[:5]))
    if ring is not None:
        out = sink(out, ring)
        problems = bigmap.verify(out)
        if problems:
            raise ValueError(f'{MAP_FILE} (the ring sunk): ' + '; '.join(problems[:5]))
    return out


def edge_gap(pts: np.ndarray, eps: float) -> float:
    """Largest height difference between opposite block edges, sampled every 25 m along their whole length (points
    within eps of an edge line count as on it; heights in between linear, held flat past the last one)."""
    worst = 0.0
    ts = np.linspace(-seams.HALF, seams.HALF, 141)
    for ax in (0, 2):
        prof = []
        for side in (-1, 1):
            m = np.abs(pts[:, ax] - side * seams.HALF) < eps
            t, y = pts[m, 2 - ax], pts[m, 1]
            k = np.argsort(t)
            prof.append(np.interp(ts, t[k], y[k]))
        worst = max(worst, float(np.abs(prof[0] - prof[1]).max()))
    return worst


def seamless_collision(mad: bytes) -> tuple[bytes, dict[str, float]]:
    """The map's collision with the four edge pieces' meshes made periodic; every box re-checked."""
    marc = bigmap.Marc.parse(mad)
    hkt = marc.get('collision.hkt')
    tag = hktag.Tag(hkt)
    buf = bytearray(hkt)
    bodies = hkcms.bodies(tag)
    ring = sorted(n for n in bodies if RING_TAG in n.lower())
    if len(ring) != 4:
        raise ValueError(f'{COLLISION_FILE}: expected 4 edge pieces, found {ring}')
    comps = [hkcms.Compound(tag, buf, bodies[n]) for n in ring]
    before = np.concatenate([c.triangles().reshape(-1, 3) for c in comps])
    field = seams.Field(before)
    moved = sum(c.set_heights(field.heights) for c in comps)
    new = hktag.Tag(bytes(buf))
    problems = [p for n in ring for p in hkcms.bound_problems(new, bytes(buf), bodies[n])]
    if problems:
        raise ValueError(f'{COLLISION_FILE}: boxes no longer hold their triangles: ' + '; '.join(problems[:5]))
    after = np.concatenate([hkcms.Compound(new, bytearray(buf), bodies[n]).triangles().reshape(-1, 3) for n in ring])
    marc.put('collision.hkt', bytes(buf))
    return marc.build(), {'meshes': moved, 'before': edge_gap(before, seams.EDGE_EPS),
                          'after': edge_gap(after, seams.EDGE_EPS)}


def seamless_far(rab_bytes: bytes) -> tuple[bytes, dict[str, float]]:
    """The map's model archive with the far-only ground made periodic (every other member's bytes kept)."""
    rab = mdb.rab_read(rab_bytes)
    hits = [f for f in rab.files if f.name.lower() == FAR_MODEL]
    if len(hits) != 1:
        raise ValueError(f'{MODELS_FILE}: {FAR_MODEL} found {len(hits)} times')
    f = hits[0]
    md = mdb.mdb_read(f.data)
    before = seams.mdb_points(md)
    field = seams.Field(before, edge_eps=FAR_EDGE_EPS, corner_tol=FAR_CORNER_TOL)
    moved = seams.apply_mdb(md, field)
    after = seams.mdb_points(md)
    f.stored = mdb.cmpl_compress(mdb.mdb_write(md))
    return mdb.rab_write(rab), {'vertices': moved, 'before': edge_gap(before, FAR_EDGE_EPS),
                                'after': edge_gap(after, FAR_EDGE_EPS)}


def seamless_terrain(stock: dict[str, bytes]) -> tuple[dict[str, bytes], dict[str, float]]:
    """The four ring pieces' near terrain (FMB, pylib/fmb.py) made periodic: one field from all four (an outer edge
    runs through two pieces), normals of the moved triangles recomputed, the trees' boxes re-derived."""
    pts = {name: np.array(fmb.decode(data), dtype=np.float64) for name, data in stock.items()}
    before = np.concatenate(list(pts.values()))
    field = seams.Field(before)
    out = {name: fmb.set_heights(data, lambda x, y, z: y + float(field.delta(x, z))) for name, data in stock.items()}
    after = np.concatenate([np.array(fmb.decode(data), dtype=np.float64) for data in out.values()])
    return out, {'vertices': int((np.abs(after[:, 1] - before[:, 1]) > 1e-4).sum()),
                 'before': edge_gap(before, seams.EDGE_EPS), 'after': edge_gap(after, seams.EDGE_EPS)}


def build_seams(root: str) -> tuple[dict[str, bytes], dict[str, dict[str, float]]]:
    """MAP file name -> its seamless bytes, and per part the seam report; raises if a seam is left."""
    files: dict[str, bytes] = {}
    report: dict[str, dict[str, float]] = {}
    files[COLLISION_FILE], report['collision'] = seamless_collision(stock_file(root, COLLISION_FILE))
    files[MODELS_FILE], report['far'] = seamless_far(stock_file(root, MODELS_FILE))
    terrain, report['terrain'] = seamless_terrain({name: stock_file(root, name) for name in RING_FILES})
    files.update(terrain)
    for part, r in report.items():
        if r['after'] > SEAM_TOL[part]:
            raise ValueError(f'{part}: opposite edges still differ by {r["after"]:.3f} m')
    return files, report


def world_half(radius: int) -> float:
    return (radius + 0.5) * BLOCK + WORLD_MARGIN


def set_big_world(root: str, value: float) -> str:
    """The plugin's ini with BigWorld=value (the line kept where it is; added under [EDF6VehicleCrew] if missing)."""
    path = os.path.join(root, 'Mods', INI)
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    line = f'BigWorld={value:.0f}'
    text, n = re.subn(r'(?m)^BigWorld=.*$', line, text)
    if not n:
        text = text.rstrip('\n') + '\n' + line + '\n'
    import modfiles
    modfiles.atomic_write(path, text.encode('utf-8'))
    return path


def install(root: str, radius: int = 1) -> list[str]:
    data = build_map(root, radius)
    files, _ = build_seams(root)
    led = ledger.Ledger(root)
    paths = [led.put(OWNER, f'MAP/{MAP_FILE}', data)]
    paths += [led.put(OWNER, f'MAP/{name}', blob) for name, blob in files.items()]
    paths.append(set_big_world(root, world_half(radius)))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    deleted, kept = led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
    if os.path.isfile(os.path.join(root, 'Mods', INI)):
        set_big_world(root, 0)
    return deleted, kept


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = next((a for a in args if not a.isdigit()), vc.DEFAULT_GAME)
    radius = 1
    if '--radius' in argv:
        radius = int(argv[argv.index('--radius') + 1])
    if '--check' in argv:
        _, report = build_seams(root)
        for part, r in report.items():
            print(f'{part}: 对边高差 {r["before"]:.2f} m -> {r["after"]:.3f} m',
                  {k: v for k, v in r.items() if k not in ('before', 'after')})
        return 0
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        print('BigWorld=0')
        return 0
    for path in install(root, radius):
        print('写入', path)
    print(f'{2 * radius + 1} x {2 * radius + 1} 块，物理世界 ±{world_half(radius):.0f} m（BigWorld）')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
