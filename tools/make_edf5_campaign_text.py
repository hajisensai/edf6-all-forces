"""Developer tool: EDF5's mission titles and briefings -> edf5campaign/missions.json (the EDF5 campaign's text).

  python -B tools/make_edf5_campaign_text.py [EDF5_DIR]

Players need not own EDF5: this runs once on a machine that has it, and the JSON is committed. It reads EDF5's
Root.cpk only (MISSION/[DLCn_]MISSIONLIST.OFFLINE.{LIST,TXT.<lang>}.SGO); EDF5 ships CN (traditional), EN, JA and
KR, and EDF6's SC is CN converted to simplified Chinese with OpenCC (pip install opencc-python-reimplemented).

EDF5's Root.cpk keeps its table of contents at the end of the file and its file offsets count from the end of the
2048-byte header block (measured: every CRILAYLA block found in the file sits at 2048 + FileOffset of a compressed
entry). EDF6's offsets count from the TOC instead (pylib/cpk.py), so the base is chosen by where the TOC is.
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cpk  # noqa: E402
import crilayla  # noqa: E402
import sgo  # noqa: E402

DEFAULT_EDF5 = r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 5'
OUT = os.path.join(HERE, '..', 'edf5campaign', 'missions.json')
LANGS = ('CN', 'EN', 'JA', 'KR')
GROUPS = (('main', ''), ('dlc1', 'DLC1_'), ('dlc2', 'DLC2_'))


class Edf5Root:
    """EDF5's Root.cpk, read only."""

    def __init__(self, path: str) -> None:
        self.c = cpk.Cpk(path)
        h = self.c.header
        if int(h['TocOffset']) > int(h['ContentOffset']):
            self.c.base = 2048
        self.idx = {(d.upper(), n.upper()): e for (d, n), e in self.c.index.items()}

    def read(self, folder: str, name: str) -> bytes:
        e = self.idx[(folder.upper(), name.upper())]
        with open(self.c.path, 'rb') as f:
            f.seek(self.c.base + int(e['FileOffset']))
            data = f.read(int(e['FileSize']))
        return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data


def table(root: Edf5Root, name: str) -> list:
    return sgo.plain(sgo.read(root.read('MISSION', name))[1])['table']


def build(edf5_dir: str) -> dict:
    import opencc
    t2s = opencc.OpenCC('t2s')
    root = Edf5Root(os.path.join(edf5_dir, 'Root.cpk'))
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


def main(argv: list[str]) -> int:
    data = build(argv[1] if len(argv) > 1 else DEFAULT_EDF5)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print(OUT, {g: len(data[g]) for g, _ in GROUPS})
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
