"""Developer tool: EDF4.1's offline story as EDF6 mission data (plan P6) -> edf41port/missions.json + edf41port/missions/.

  python -B tools/make_edf41_missions.py [--edf5 DIR] [--edf41 DIR] [--zh DIR] [--dlc]

Players need not own EDF5 (the scripts come from it) or EDF4.1 (the text and pictures): this runs once on a machine
that has both, and the output (about 2 MB) is committed. EDF4.1's own scripts are for its engine; EDF5 ships all 89 of
the story's missions recompiled for EDF5's (MISSION/EDF4.1/<id>/, the runtime EDF6 still runs EDF5's missions on:
MISSION/EDF5_OLD_SCRIPT), so those are the ones taken.

Per mission, in 4.1's story order (MISSION/MISSIONLIST.SGO):
  missions/<id>/MISSION.BVM, BEFORE.BVM   EDF5's, as they are
  missions/<id>/MISSION.RMPA, BEFORE.RMPA  EDF5's, in EDF6's format (pylib/rmpa_legacy.py; the old one crashes EDF6's
                                          reader, EDF+0x6F3D25)
  missions.json: id, files, title / brief per EDF6 language from 4.1's MISSIONTEXT.SGO (rows [JA title, JA brief,
                 EN title, EN brief, ...]): JA, EN its own; SC the Simplified Chinese community patch's (--zh: its
                 mods folder, default <EDF4.1>/mods; pylib/edf41zh.py, rows checked against 4.1's), CN that in
                 Traditional (OpenCC; EN's without it); KR EN's (4.1 has no Korean). 'source' names the patch file.
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
import edf41dlc  # noqa: E402
import edf41zh  # noqa: E402
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


def texts_of(text: list, zh: tuple[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    """(title, brief) per EDF6 language of a 4.1 MISSIONTEXT row and the patch's Chinese (title, brief) of it."""
    title = {lang: text[2].strip() for lang in LANGS}
    brief = {lang: text[3] for lang in LANGS}
    title['JA'], brief['JA'] = text[0], text[1]
    title['SC'], brief['SC'] = zh
    cn = (edf41zh.traditional(zh[0]), edf41zh.traditional(zh[1]))
    if cn[0] is not None and cn[1] is not None:
        title['CN'], brief['CN'] = cn
    return title, brief


def source_zh(note: str) -> str:
    cn = 'CN its OpenCC s2tw (pylib/edf41zh.py)' if edf41zh.available() else 'CN English (no OpenCC)'
    return f'SC: {edf41zh.PATCH} {note}; {cn}; KR English.'


def build(edf5: str, edf41: str, zh: str) -> tuple[dict, dict[str, bytes], bytes]:
    """(missions.json, {rel under missions/: bytes}, thumbnails.rab); `zh` the Chinese patch's mods folder."""
    g5, g41 = archives(edf5), archives(edf41)
    rows = sgo.plain(sgo.read(_read(g41, 'MISSION/MISSIONLIST.SGO'))[1])['table']
    texts = sgo.plain(sgo.read(_read(g41, 'MISSION/MISSIONTEXT.SGO'))[1])['table']
    pictures = {f.name.upper(): f for f in mdb.rab_read(_read(g41, 'MISSION/MISSIONIMAGE.RAB')).files}
    if len(rows) != len(texts):
        raise SystemExit(f'4.1 lists {len(rows)} missions but has {len(texts)} texts')
    chinese, note = edf41zh.mission_texts(zh, texts)
    files: dict[str, bytes] = {}
    missions = []
    thumbs = []
    for (_path, mid), text, zh_text in zip(rows, texts, chinese):
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
        title, brief = texts_of(text, zh_text)
        missions.append({'id': mid, 'files': names, 'title': title, 'brief': brief,
                         'sha': {n: hashlib.sha256(files[f'{mid}/{n}']).hexdigest() for n in names}})
        thumbs.append(mdb.RabFile(f'{mid}.dds', 0, 0, mdb.cmpl_compress(thumbnail(pictures[f'{mid}.DDS'.upper()].data))))
    rab = mdb.rab_read(_read(g41, 'MISSION/MISSIONIMAGE.RAB'))
    rab.files = thumbs
    data = {'source': 'scripts: EDF5 MISSION/EDF4.1 (RMPA in EDF6 format); text, pictures: EDF4.1 MISSION/MISSION*. '
                      + source_zh(note),
            'missions': missions}
    return data, files, mdb.rab_write(rab)


def build_dlc(dlc: 'edf41dlc.Dlc', zh: str) -> tuple[dict, dict[str, bytes], bytes]:
    """One 4.1 DLC mission pack as build() makes the story: (missions.json, {rel under missions/: bytes}, thumbnails).

    Its missions are the pack's MissionList (the folders it ships beside them that no list names are not played).
    EDF5 has no recompile of these, so the scripts are 4.1's own converted by pylib/bvm41.py, the pack's mounted paths
    (mp01:/Object/...) rewritten to where the object converter installs them (pylib/edf41dlc.py); RMPA as the story's.
    Text and pictures are the pack's own, laid out like the story's (rows [JA title, JA brief, EN title, EN brief, ...]),
    the Chinese the patch's <app id>/mission/MISSIONTEXT.SGO (`zh` its mods folder)."""
    import bvm41
    rows = sgo.plain(sgo.read(dlc.read('MISSION/MISSIONLIST.SGO'))[1])['table']
    texts = sgo.plain(sgo.read(dlc.read('MISSION/MISSIONTEXT.SGO'))[1])['table']
    image = mdb.rab_read(dlc.read('MISSION/MISSIONIMAGE.RAB'))
    pictures = {f.name.upper(): f for f in image.files}
    if len(rows) != len(texts):
        raise SystemExit(f'{dlc.mount} lists {len(rows)} missions but has {len(texts)} texts')
    chinese, note = edf41zh.mission_texts(zh, texts, dlc.app_id)
    files: dict[str, bytes] = {}
    missions = []
    refused = []   # missions whose script calls a 4.1 native EDF6's script runtime has no entry for (bvm41)
    thumbs = []
    for (_path, mid), text, zh_text in zip(rows, texts, chinese):
        names = []
        mine: dict[str, bytes] = {}
        try:
            for name in SCRIPTS:
                inner = f'MISSION/{mid}/{name}'
                if inner.upper() not in dlc.files:
                    continue
                data = dlc.read(inner)
                if name.endswith('.RMPA'):
                    data = rmpa_legacy.rmpa_from_legacy(data)
                else:
                    data = bvm41.to_edf5(data, rewrite=edf41dlc.rewrite)
                mine[f'{mid}/{name}'] = data
                names.append(name)
        except bvm41.UnsupportedNative as e:   # not shipped; missions.json says why (the planner reports it)
            refused.append({'id': mid, 'why': str(e)})
            print(dlc.mount, mid, 'refused:', e)
            continue
        files.update(mine)
        if 'MISSION.BVM' not in names:
            raise SystemExit(f'{dlc.mount} has no script of its mission {mid}')
        title, brief = texts_of(text, zh_text)
        missions.append({'id': mid, 'files': names, 'title': title, 'brief': brief,
                         'sha': {n: hashlib.sha256(files[f'{mid}/{n}']).hexdigest() for n in names}})
        thumbs.append(mdb.RabFile(f'{mid}.dds', 0, 0, mdb.cmpl_compress(thumbnail(pictures[f'{mid}.DDS'.upper()].data))))
    image.files = thumbs
    data = {'source': f'EDF4.1 DLC {dlc.app_id} ({dlc.mount}:): scripts converted by pylib/bvm41.py (mounted paths as '
                      'pylib/edf41dlc.py installs them), RMPA in EDF6 format; text, pictures: the pack\'s MISSION/MISSION*. '
                      + source_zh(note),
            'missions': missions, 'refused': refused}
    return data, files, mdb.rab_write(image)


def write(out: str, data: dict, files: dict[str, bytes], rab: bytes) -> None:
    """A pack's data under `out` (edf41port, or edf41port/mp0N): missions/, missions_thumbnails.rab, missions.json."""
    for rel, blob in files.items():
        path = os.path.join(out, 'missions', *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(blob)
    with open(os.path.join(out, 'missions_thumbnails.rab'), 'wb') as f:
        f.write(rab)
    text = json.dumps(data, ensure_ascii=False, indent=1) + '\n'
    with open(os.path.join(out, 'missions.json'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(out, len(data['missions']), 'missions,', sum(len(b) for b in files.values()) >> 10, 'KB scripts,',
          len(rab) >> 10, 'KB thumbnails')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--edf5', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 5'))
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    ap.add_argument('--dlc', action='store_true', help="the 4.1 DLC mission packs (edf41port/mp01, mp02) instead of "
                                                      'the story: needs a 4.1 with its DLC installed, not EDF5')
    ap.add_argument('--zh', help="the Simplified Chinese patch's mods folder (default: <EDF4.1>/mods, a patched 4.1)")
    args = ap.parse_args(argv[1:])
    zh = args.zh or edf41zh.default_dir(args.edf41)
    if args.dlc:
        packs = edf41dlc.find(args.edf41)
        if not packs:
            raise SystemExit(f'no 4.1 DLC mission pack in {args.edf41}\\DLC')
        for mount, dlc in packs.items():
            write(os.path.join(OUT, mount), *build_dlc(dlc, zh))
        return 0
    write(OUT, *build(args.edf5, args.edf41, zh))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
