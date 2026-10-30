"""The missions the test site's level plan starts from (testhub/src/missions.json).

  python -B tools/make_level_list.py [GAME_DIR]

EDF4.1's story and two DLC packs (edf41port/**/missions.json), then EDF5's campaign
(main, DLC1, DLC2 in the order tools/make_edf5_campaign.py appends it; titles from
edf5campaign/missions.json), then EDF6's offline list (MISSION/MISSIONLIST.OFFLINE.LIST.SGO, 147 rows, its SC/JA
titles): the plan reads the games in story order. In the game's list EDF5 comes after EDF6; `n` keeps that.
Each entry:
  key     the mission folder, prefixed by game/pack to avoid shared mission ids
  series  EDF4.1 / EDF4.1 DLC1 / EDF4.1 DLC2 / EDF6 / EDF5 / EDF5 DLC1 / EDF5 DLC2
  n       its combined-list row (EDF5 follows EDF6's 147), null for separate packs or unavailable missions
  sc, ja  the titles (only titles: the briefings stay in the game)
  missing why an EDF5 mission is not in the game (its map or objects are not in EDF6), else absent
  pack_n  EDF4.1's position in its separate mission pack (not in the combined EDF6/EDF5 list)
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import dsgo  # noqa: E402
import make_edf5_campaign as edf5  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

OUT = os.path.join(HERE, '..', 'testhub', 'src', 'missions.json')
SERIES = {'main': 'EDF5', 'dlc1': 'EDF5 DLC1', 'dlc2': 'EDF5 DLC2'}


def title(row: list) -> str:
    """Row [title, briefing]; some EDF6 titles are padded with '†' to a fixed width."""
    return str(row[0]).rstrip('†').strip()


def edf6_rows(game: rootcpk.Game) -> list[dict[str, object]]:
    table = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    texts = {lang: list(sgo.read(game.read('MISSION', f'MISSIONLIST.OFFLINE.TXT.{lang}.SGO'))[1]['table'])
             for lang in ('SC', 'JA')}
    if any(len(rows) != len(table) for rows in texts.values()):
        raise SystemExit('任务列表和标题表行数不一致')
    return [{'key': str(r.items[2]), 'series': 'EDF6', 'n': i + 1, 'sc': title(texts['SC'][i]), 'ja': title(texts['JA'][i])}
            for i, r in enumerate(table)]


def edf5_rows(root: str, first: int) -> list[dict[str, object]]:
    """Every EDF5 mission, numbered as the campaign installs the available ones."""
    with open(edf5.TEXT, encoding='utf-8') as f:
        text = json.load(f)
    p = edf5.plan(root)
    order = {(r['group'], r['path']): first + i for i, r in enumerate(r for r in p['rows'] if edf5.available(r))}
    why = {(g, path): reason for g, path, reason in p['skipped']}
    out = []
    for group, series in SERIES.items():
        for m in text[group]:
            row: dict[str, object] = {'key': 'EDF5/' + m['path'], 'series': series, 'n': order.get((group, m['path'])),
                                      'sc': m['title']['SC'], 'ja': m['title']['JA']}
            if (group, m['path']) in why:
                row['missing'] = why[(group, m['path'])]
            out.append(row)
    return out


def edf41_rows() -> list[dict[str, object]]:
    """Use the same committed titles, order and folders as the installer's three 4.1 packs."""
    out = []
    for p41, series in zip(edf5.PACKS41, ('EDF4.1', 'EDF4.1 DLC1', 'EDF4.1 DLC2')):
        for i, m in enumerate(edf5.edf41_text(p41.data)['missions']):
            out.append({'key': f'{p41.folder}/{m["id"]}', 'series': series, 'n': None, 'pack_n': i + 1,
                        'sc': m['title']['SC'], 'ja': m['title']['JA']})
    return out


def build(root: str) -> list[dict[str, object]]:
    six = edf6_rows(rootcpk.Game(root))
    return edf41_rows() + edf5_rows(root, len(six) + 1) + six


def main(argv: list[str]) -> int:
    rows = build(argv[1] if len(argv) > 1 else rootcpk.DEFAULT_GAME)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('[\n' + ',\n'.join(json.dumps(r, ensure_ascii=False) for r in rows) + '\n]\n')
    print(f'{len(rows)} missions ({sum(1 for r in rows if r["n"])} in the combined list, '
          f'{sum(1 for r in rows if r.get("pack_n"))} in 4.1 packs) -> {os.path.relpath(OUT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
