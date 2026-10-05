"""Writes a bigger test-range map (the user, 2026-10-04/05: "a map of many plains stitched together, real physics all
over") into <game>/Mods:

  Mods/MAP/IG_HEIGEN601.MAC   the plain (ig_Heigen601, the test range's map) with its whole block copied around it:
                              the ground (ig_heigen507_2, 2500 m square), the four edge pieces round it (the 500 m
                              ring that closes it off: 3500 m across) and its far-only ground, every copy a placement
                              record of its own (pylib/bigmap.py: the game builds each like any map piece, its model
                              and its collision, by name, at its own place); the far mountain ring is sunk out of
                              sight (its ring would stand inside the bigger map). A RADIUS of 1 is 3 x 3 blocks
                              (+-5250 m), 2 is 5 x 5 (+-8750 m).
  Mods/Plugins/EDF6VehicleCrew.ini  BigWorld (the physics world's half size, src/bigworld.cpp) set to cover it; --remove
                              sets it back to 0.

Only the test range's map: missions on other maps are untouched. The navmesh still covers only the middle block (the
ground units' paths), the map's own move area is widened by the plugin with BigWorld.

  python tools/make_bigmap.py [game dir] [--radius N]   write / refresh
  python tools/make_bigmap.py [game dir] --remove       release it
"""
from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import bigmap  # noqa: E402
import cpk  # noqa: E402
import crilayla  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'bigmap'   # pylib/ledger.py
MAP_FILE = 'IG_HEIGEN601.MAC'
ARCHIVE = 'Chunk02.cpk'
BLOCK = 3500.0           # m: the ground (2500) and its edge ring (500 a side)
SINK = -40000.0          # m: the far mountain ring's drop (out of the far camera's 20 km)
WORLD_MARGIN = 750.0     # m of physics world past the last block's edge
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')


def stock_map(root: str) -> bytes:
    """The stock map archive, from the game's Chunk02.cpk (only read)."""
    c = cpk.Cpk(os.path.join(root, ARCHIVE))
    e = c.index[('MAP', MAP_FILE)]
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
    stock = stock_map(root)
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
    led = ledger.Ledger(root)
    paths = [led.put(OWNER, f'MAP/{MAP_FILE}', data)]
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
