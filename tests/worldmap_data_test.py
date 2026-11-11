"""The world map's data (docs/plan-2026-11-10-worldmap.md §3): pylib/worldmap.py places, worldmap/missions.json,
src/worldmap.inc, tools/worldmap_basemap.py's sources and pyramid layout. With the game (EDF6 Root.cpk) also that the committed mission table is
what tools/make_worldmap.py reads today."""
from __future__ import annotations

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_worldmap  # noqa: E402
import make_worldmap  # noqa: E402
import worldmap  # noqa: E402

checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    if not ok:
        raise SystemExit(f'FAIL: {what}')


def load(rel: str) -> dict:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return json.load(f)


def check_places() -> None:
    for pid, p in worldmap.PLACES.items():
        check(24.0 <= p.lat <= 46.0 and 122.0 <= p.lon <= 146.0, f'{pid}: in Japan')
        check(p.region in worldmap.REGIONS, f'{pid}: a known region')
        check(all(n.strip() for n in (p.sc, p.tc, p.en, p.ja, p.kr)), f'{pid}: named in five languages')
    for name, (pid, why) in worldmap.MAPS.items():
        check(pid in worldmap.PLACES, f'{name}: its place {pid} exists')
        check(name == name.lower() and not name.endswith('.mac') and why, f'{name}: lower case, no .mac, a reason')
    coords = {}
    for pid, p in worldmap.PLACES.items():
        check((p.lat, p.lon) not in coords, f'{pid}: not on top of {coords.get((p.lat, p.lon))}')
        coords[(p.lat, p.lon)] = pid
    check(worldmap.RANGE_PLACE in worldmap.PLACES, 'the range place exists')


def check_missions() -> None:
    data = load('worldmap/missions.json')
    packs = {p['pack']: p['missions'] for p in data['packs']}
    check(list(packs) == [p for p, _ in make_worldmap.EDF6_LISTS] + [p for p, _ in make_worldmap.EDF5_GROUPS] +
          [p for p, _, _ in make_worldmap.EDF41_PACKS], 'nine packs in order')
    check([len(packs[p]) for p, _ in make_worldmap.EDF6_LISTS] == [147, 19, 40], 'EDF6 147 / 19 / 40 (the stock lists)')
    edf5 = load('edf5campaign/missions.json')
    for pack, group in make_worldmap.EDF5_GROUPS:
        check([m['key'] for m in packs[pack]] == [f'EDF5_OLD_SCRIPT/{m["path"]}' for m in edf5[group]],
              f'{pack}: the ported rows\' keys in order (tools/make_edf5_campaign.py)')
    for pack, sub, folder in make_worldmap.EDF41_PACKS:
        listed = load(os.path.join('edf41port', sub, 'missions.json') if sub else 'edf41port/missions.json')['missions']
        check([m['key'] for m in packs[pack]] == [f'{folder}/{m["id"]}' for m in listed], f'{pack}: its keys in order')
    maps = {m['map'] for ms in packs.values() for m in ms}
    unnamed = sorted(maps - set(worldmap.MAPS))
    check(not unnamed, f'every map a mission loads has a place: {unnamed}')
    stale = sorted(set(worldmap.MAPS) - maps)
    check(not stale, f'no place for a map no mission loads: {stale}')


def check_generated() -> None:
    with open(os.path.join(ROOT, 'src', 'worldmap.inc'), encoding='utf-8') as f:
        check(f.read() == gen_worldmap.render(), 'src/worldmap.inc current (python tools/gen_worldmap.py)')
    check(len(gen_worldmap.missions()) == 483, '483 distinct keys')


def check_geometry() -> None:
    u, v = worldmap.project(0.0, 0.0)
    check(abs(u - 0.5) < 1e-9 and abs(v - 0.5) < 1e-9, 'project: the centre')
    u, v = worldmap.project(-90.0, 180.0)
    check(u == 0.0 and v == 1.0, 'project: 180 E wraps')
    a, b = (35.69, 139.75), (43.06, 141.35)
    line = worldmap.great_circle(a, b, 8)
    check(len(line) == 8 and abs(line[0][0] - a[0]) < 1e-9 and abs(line[-1][1] - b[1]) < 1e-9, 'great circle: the ends')


def check_parsers() -> None:
    check(make_worldmap.ac_map('void Main(){\n  Map("app:/Map/nw_Haikyo601.mac","evening");\n}') == 'nw_haikyo601',
          'MISSION.AC: the first Map(...)')
    check(make_worldmap.ac_map('no map') is None, 'none')


def check_basemap() -> None:
    import worldmap_basemap as wb
    check(len(wb.SOURCES) == 8 and all(len(src.sha256) == 64 for src in wb.SOURCES), 'eight pinned sources')
    check(wb.BASE_URL.startswith('https://'), 'over https')
    check(sorted(src.cell for src in wb.SOURCES) == [(c, r) for c in range(4) for r in range(2)], 'A..D x 1..2')
    check(wb.Source('D1', '').name == 'bluemarble_d1.jpg', 'the D1 name the first trial downloaded')
    check(worldmap.tile_grid(0) == (1, 1) and worldmap.tile_grid(worldmap.TILE_LEVELS - 1) == (64, 32), 'the grid')
    check(worldmap.TILE * worldmap.tile_grid(worldmap.TILE_LEVELS - 1)[0] == 360 * 240, 'the deepest level: 240 px / deg')
    check(wb.per_source(worldmap.TILE_LEVELS - 1) * worldmap.TILE == wb.SOURCE_PX, 'tiles align with the sources')
    try:
        wb.check(wb.SOURCES[0], b'not it')
        check(False, 'a wrong file refused')
    except SystemExit:
        check(True, 'a wrong file refused')


def check_against_game() -> None:
    game = os.environ.get('EDF6_GAME_DIR') or make_worldmap.DEFAULT_GAME
    if not os.path.isfile(os.path.join(game, 'Root.cpk')):
        print('worldmap_data_test: no game: the table is not re-read')
        return
    fresh = make_worldmap.build(game)
    check(fresh['packs'] == load('worldmap/missions.json')['packs'],
          'worldmap/missions.json is what the game has (python tools/make_worldmap.py)')


def main() -> int:
    check_places()
    check_missions()
    check_generated()
    check_geometry()
    check_parsers()
    check_basemap()
    check_against_game()
    print(f'worldmap_data_test: {checks} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
