"""Developer tool: which map every playable mission loads -> worldmap/missions.json (the world map's mission table,
docs/plan-2026-11-10-worldmap.md §3).

  python -B tools/make_worldmap.py [GAME_DIR]

Read only. Sources, per pack, in list order:
  EDF6 story, mission packs 1 / 2   GAME_DIR's Root.cpk MISSION/MISSIONLIST[_DLCn].OFFLINE.LIST.SGO (field 2 = the row's
                                     key, field 1 = its folder), each folder's MISSION.AC (the script's source text):
                                     the first Map("app:/Map/<name>.mac", ...).
  EDF5 story, mission packs 1 / 2   edf5campaign/missions.json (the ported rows, tools/make_edf5_campaign.py: key
                                     EDF5_OLD_SCRIPT/<path>), Root.cpk MISSION/EDF5_OLD_SCRIPT/<path>/MISSION.BVM: its
                                     first app:/Map/ reference (make_edf5_campaign.first_map).
  EDF4.1 story, mission packs 1 / 2 edf41port[/mp01|/mp02]/missions.json (key EDF41[MP01|MP02]/<id>), the committed
                                     MISSION.BVM beside it.
Only the keys and the map names are written (no mission text, no script). The plugin finds a row's place by its key
(field 2, EDF.dll 0xE0D80), never by its row number, so reordered or appended lists keep their places. The map -> real
place table is pylib/worldmap.py PLACES; tools/gen_worldmap.py turns both into src/worldmap.inc.
"""
from __future__ import annotations

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import dsgo  # noqa: E402
import rootcpk  # noqa: E402

DEFAULT_GAME = r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
OUT = os.path.join(ROOT, 'worldmap', 'missions.json')
AC_MAP = re.compile(r'\bMap\(\s*"app:/map/([\w.]+?)\.mac"', re.I)
EDF6_LISTS = (('edf6', ''), ('edf6dlc1', '_DLC1'), ('edf6dlc2', '_DLC2'))
EDF5_GROUPS = (('edf5', 'main'), ('edf5dlc1', 'dlc1'), ('edf5dlc2', 'dlc2'))
EDF41_PACKS = (('edf41', '', 'EDF41'), ('edf41mp01', 'mp01', 'EDF41MP01'), ('edf41mp02', 'mp02', 'EDF41MP02'))


def ac_map(text: str) -> str | None:
    """The first map a mission script's source loads (lower case, no .mac)."""
    m = AC_MAP.search(text)
    return m.group(1).lower() if m else None


def bvm_map(bvm: bytes) -> str | None:
    import make_edf5_campaign
    name = make_edf5_campaign.first_map(bvm)
    return name.lower().removesuffix('.mac') if name else None


def edf6(game: rootcpk.Game) -> list[dict]:
    out = []
    for pack, suffix in EDF6_LISTS:
        rows = dsgo.to_py(dsgo.parse(game.read('MISSION', f'MISSIONLIST{suffix}.OFFLINE.LIST.SGO')).root)['table']
        missions = []
        for row in rows:
            folder = 'MISSION/' + row[1].split('app:/Mission/', 1)[1]
            missions.append({'key': row[2], 'map': ac_map(game.read(folder, 'MISSION.AC').decode('utf-8', 'replace'))})
        out.append({'pack': pack, 'missions': missions})
    return out


def edf5(game: rootcpk.Game) -> list[dict]:
    with open(os.path.join(ROOT, 'edf5campaign', 'missions.json'), encoding='utf-8') as f:
        text = json.load(f)
    out = []
    for pack, group in EDF5_GROUPS:
        missions = [{'key': f'EDF5_OLD_SCRIPT/{m["path"]}',
                     'map': bvm_map(game.read(f'MISSION/EDF5_OLD_SCRIPT/{m["path"]}', 'MISSION.BVM'))}
                    for m in text[group]]
        out.append({'pack': pack, 'missions': missions})
    return out


def edf41() -> list[dict]:
    out = []
    for pack, sub, folder in EDF41_PACKS:
        base = os.path.join(ROOT, 'edf41port', sub)
        with open(os.path.join(base, 'missions.json'), encoding='utf-8') as f:
            listed = json.load(f)['missions']
        missions = []
        for m in listed:
            with open(os.path.join(base, 'missions', m['id'], 'MISSION.BVM'), 'rb') as f:
                missions.append({'key': f'{folder}/{m["id"]}', 'map': bvm_map(f.read())})
        out.append({'pack': pack, 'missions': missions})
    return out


def build(game_dir: str) -> dict:
    game = rootcpk.Game(game_dir)
    packs = edf6(game) + edf5(game) + edf41()
    missing = [f'{p["pack"]}:{m["key"]}' for p in packs for m in p['missions'] if not m['map']]
    if missing:
        raise SystemExit(f'no map found for {missing}')
    return {'source': 'tools/make_worldmap.py: EDF6 Root.cpk MISSION.AC / EDF5_OLD_SCRIPT MISSION.BVM, edf41port BVM; '
                      'the first map each mission script loads', 'packs': packs}


def write(data: dict, path: str = OUT) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')


def main(argv: list[str]) -> int:
    data = build(argv[1] if len(argv) > 1 else DEFAULT_GAME)
    write(data)
    print(f'{OUT}: ' + ', '.join(f'{p["pack"]} {len(p["missions"])}' for p in data['packs']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
