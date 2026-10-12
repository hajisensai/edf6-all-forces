"""Developer tool: EDF5's mission titles and briefings -> edf5campaign/missions.json (the EDF5 campaign's text), and
EDF5's mission thumbnails -> edf5campaign/thumbnails.rab (make_edf5_campaign.THUMBS).

  python -B tools/make_edf5_campaign_text.py [EDF5_DIR]

Players need not own EDF5: this runs once on a machine that has it, and the JSON is committed. It reads EDF5's
Root.cpk only (MISSION/[DLCn_]MISSIONLIST.OFFLINE.{LIST,TXT.<lang>}.SGO, .IMAGE.RAB); EDF5 ships CN (traditional), EN, JA and
KR, and EDF6's SC is CN converted to simplified Chinese with OpenCC (pip install opencc-python-reimplemented).
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import mdb  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

DEFAULT_EDF5 = r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 5'
OUT = os.path.join(HERE, '..', 'edf5campaign', 'missions.json')
THUMBS = os.path.join(HERE, '..', 'edf5campaign', 'thumbnails.rab')
LANGS = ('CN', 'EN', 'JA', 'KR')
GROUPS = (('main', ''), ('dlc1', 'DLC1_'), ('dlc2', 'DLC2_'))


def table(root: rootcpk.Game, name: str) -> list:
    return sgo.plain(sgo.read(root.read('MISSION', name))[1])['table']


def build(edf5_dir: str) -> dict:
    import opencc
    t2s = opencc.OpenCC('t2s')
    root = rootcpk.Game(edf5_dir)
    out: dict = {'source': 'EDF5 Root.cpk MISSION/*MISSIONLIST.OFFLINE.*; SC = CN via OpenCC t2s'}
    for group, prefix in GROUPS:
        rows = table(root, f'{prefix}MISSIONLIST.OFFLINE.LIST.SGO')
        texts = {lang: table(root, f'{prefix}MISSIONLIST.OFFLINE.TXT.{lang}.SGO') for lang in LANGS}
        missions = []
        for i, row in enumerate(rows):
            entry = {'path': row[0].split('app:/Mission/', 1)[1], 'progress': round(float(row[3].value), 4),
                     'title': {}, 'brief': {}}
            for lang in LANGS:
                entry['title'][lang], entry['brief'][lang] = texts[lang][i][0], texts[lang][i][1]
            entry['title']['SC'] = t2s.convert(entry['title']['CN'])
            entry['brief']['SC'] = t2s.convert(entry['brief']['CN'])
            missions.append(entry)
        out[group] = missions
    return out


def thumbnails(edf5_dir: str) -> bytes:
    """EDF5's three offline thumbnail RABs as one, members as EDF5 stores them (names unique across the three)."""
    root = rootcpk.Game(edf5_dir)
    rabs = [mdb.rab_read(root.read('MISSION', f'{prefix}MISSIONLIST.OFFLINE.IMAGE.RAB')) for _, prefix in GROUPS]
    out = rabs[0]
    out.files = [f for rab in rabs for f in rab.files]
    names = [f.name.upper() for f in out.files]
    if len(set(names)) != len(names):
        raise SystemExit('EDF5 thumbnails share a name across its lists')
    return mdb.rab_write(out)


def main(argv: list[str]) -> int:
    edf5 = argv[1] if len(argv) > 1 else DEFAULT_EDF5
    data = build(edf5)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print(OUT, {g: len(data[g]) for g, _ in GROUPS})
    rab = thumbnails(edf5)
    with open(THUMBS, 'wb') as f:
        f.write(rab)
    print(THUMBS, len(rab))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
