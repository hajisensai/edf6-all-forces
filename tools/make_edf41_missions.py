"""Developer tool: EDF4.1's offline story as EDF6 mission data (plan P6) -> edf41port/missions.json + edf41port/missions/.

  python -B tools/make_edf41_missions.py [--edf5 DIR] [--edf41 DIR]

Players need not own EDF5 (the scripts come from it) or EDF4.1 (the text and pictures): this runs once on a machine
that has both, and the output (about 2 MB) is committed. EDF4.1's own scripts are for its engine; EDF5 ships all 89 of
the story's missions recompiled for EDF5's (MISSION/EDF4.1/<id>/, the runtime EDF6 still runs EDF5's missions on:
MISSION/EDF5_OLD_SCRIPT), so those are the ones taken.

Per mission, in 4.1's story order (MISSION/MISSIONLIST.SGO):
  missions/<id>/MISSION.BVM, BEFORE.BVM   EDF5's, as they are
  missions/<id>/MISSION.RMPA, BEFORE.RMPA  EDF5's, in EDF6's format (pylib/rmpa_legacy.py; the old one crashes EDF6's
                                          reader, EDF+0x6F3D25)
  missions.json: id, files, title / brief per EDF6 language from 4.1's MISSIONTEXT.SGO (rows [JA title, JA brief,
                 EN title, EN brief, ...]): JA its own, every other language EN's (4.1 has no Chinese or Korean)
  thumbnails.rab: 4.1's picture of the mission (MISSIONIMAGE.RAB, 660x330 RGBA) at EDF6's 440x220 DXT1, as <id>.dds
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import mdb  # noqa: E402
import rmpa_legacy  # noqa: E402
import sgo  # noqa: E402
from make_edf41_objects import STEAM, archives  # noqa: E402

OUT = os.path.join(HERE, '..', 'edf41port')
LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')
SCRIPTS = ('MISSION.BVM', 'MISSION.RMPA', 'BEFORE.BVM', 'BEFORE.RMPA')
THUMB_SIZE = (440, 220)


def _read(games: dict, rel: str) -> bytes:
    return games[rel.upper()].read(*rel.rsplit('/', 1))


def thumbnail(dds: bytes) -> bytes:
    """4.1's mission picture as EDF6's: 440x220 DXT1 (MISSIONLIST.OFFLINE.IMAGE.RAB's)."""
    from PIL import Image
    img = Image.open(io.BytesIO(dds)).convert('RGB').resize(THUMB_SIZE, Image.LANCZOS)
    out = io.BytesIO()
    img.save(out, 'DDS', pixel_format='DXT1')
    return out.getvalue()


def build(edf5: str, edf41: str) -> tuple[dict, dict[str, bytes], bytes]:
    """(missions.json, {rel under missions/: bytes}, thumbnails.rab)."""
    g5, g41 = archives(edf5), archives(edf41)
    rows = sgo.plain(sgo.read(_read(g41, 'MISSION/MISSIONLIST.SGO'))[1])['table']
    texts = sgo.plain(sgo.read(_read(g41, 'MISSION/MISSIONTEXT.SGO'))[1])['table']
    pictures = {f.name.upper(): f for f in mdb.rab_read(_read(g41, 'MISSION/MISSIONIMAGE.RAB')).files}
    if len(rows) != len(texts):
        raise SystemExit(f'4.1 lists {len(rows)} missions but has {len(texts)} texts')
    files: dict[str, bytes] = {}
    missions = []
    thumbs = []
    for (_path, mid), text in zip(rows, texts):
        names = []
        for name in SCRIPTS:
            rel = f'MISSION/EDF4.1/{mid}/{name}'
            if rel.upper() not in g5:
                continue
            data = _read(g5, rel)
            if name.endswith('.RMPA'):
                data = rmpa_legacy.rmpa_from_legacy(data)
            files[f'{mid}/{name}'] = data
            names.append(name)
        if 'MISSION.BVM' not in names:
            raise SystemExit(f'EDF5 has no script of 4.1 mission {mid}')
        title = {lang: text[2].strip() for lang in LANGS}
        brief = {lang: text[3] for lang in LANGS}
        title['JA'], brief['JA'] = text[0], text[1]
        missions.append({'id': mid, 'files': names, 'title': title, 'brief': brief,
                         'sha': {n: hashlib.sha256(files[f'{mid}/{n}']).hexdigest() for n in names}})
        thumbs.append(mdb.RabFile(f'{mid}.dds', 0, 0, mdb.cmpl_compress(thumbnail(pictures[f'{mid}.DDS'.upper()].data))))
    rab = mdb.rab_read(_read(g41, 'MISSION/MISSIONIMAGE.RAB'))
    rab.files = thumbs
    data = {'source': 'scripts: EDF5 MISSION/EDF4.1 (RMPA in EDF6 format); text, pictures: EDF4.1 MISSION/MISSION*.',
            'missions': missions}
    return data, files, mdb.rab_write(rab)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--edf5', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 5'))
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    args = ap.parse_args(argv[1:])
    data, files, rab = build(args.edf5, args.edf41)
    for rel, blob in files.items():
        path = os.path.join(OUT, 'missions', *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(blob)
    with open(os.path.join(OUT, 'missions_thumbnails.rab'), 'wb') as f:
        f.write(rab)
    text = json.dumps(data, ensure_ascii=False, indent=1) + '\n'
    with open(os.path.join(OUT, 'missions.json'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(len(data['missions']), 'missions,', sum(len(b) for b in files.values()) >> 10, 'KB scripts,', len(rab) >> 10,
          'KB thumbnails')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
