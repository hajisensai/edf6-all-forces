"""Developer tool: EDF4.1's player soldiers as extra EDF6 outfits (pylib/outfit41.py, docs/edf41-outfits.md)
-> edf41port/outfits/<Mods path> and edf41port/outfits.json (committed, as edf41port/objects: the player needs no 4.1).

  python -B tools/make_edf41_outfits.py [--edf41 DIR] [--edf6 DIR] [--only ranger]

Every class of outfit41.SPECS goes through the same pipeline; a class it refuses is left out and said why. The files
are new names only (OBJECT/EDF41_P_<CLASS>.SGO / .MRAB, MENUOBJECT/EDF41_P_<CLASS>.SGO): nothing stock is replaced, the
plugin hands the game these paths for the local player who picked the outfit (src/outfit41.cpp).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import outfit41  # noqa: E402
import rabpool  # noqa: E402
from make_edf41_objects import STEAM, archives  # noqa: E402

OUT = os.path.join(HERE, '..', 'edf41port', 'outfits.json')
FILES = os.path.join(HERE, '..', 'edf41port', 'outfits')


def reader(root: str):
    games = archives(root)

    def read(rel: str) -> bytes:
        rel = rel.upper()
        if rel not in games:
            raise ValueError(f'{root} has no {rel}')
        return games[rel].read(*rel.rsplit('/', 1))
    return read


def build(edf41: str, edf6: str, only: tuple[str, ...] = ()) -> tuple[dict, dict[str, bytes]]:
    read41, read6 = reader(edf41), reader(edf6)
    classes, skipped, files = [], [], {}
    for key, spec in outfit41.SPECS.items():
        if only and key not in only:
            continue
        try:
            res = outfit41.build(spec, read41, read6)
            arch = res.files[f'OBJECT/{spec.out}.MRAB']
            models = [f.name for f in __import__('mdb').rab_read(arch).files if f.name.lower().endswith('.mdb')]
            faults = outfit41.check_archive(arch, models)
            if faults:
                raise ValueError('; '.join(faults[:5]))
        except ValueError as e:
            skipped.append({'class': key, 'reason': str(e)})
            continue
        files.update(res.files)
        classes.append({'class': key, 'soldier_class': spec.soldier_class, 'battle': f'OBJECT/{spec.out}.SGO',
                        'menu': f'MENUOBJECT/{spec.out}.SGO', 'files': sorted(res.files), 'report': res.report})
    data = {'source': 'EDF4.1 Root.cpk player soldiers on EDF6 player SGOs; tools/make_edf41_outfits.py',
            'classes': classes, 'skipped': skipped,
            'files': {r: hashlib.sha256(b).hexdigest() for r, b in sorted(files.items())}}
    return data, files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    ap.add_argument('--edf6', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 6'))
    ap.add_argument('--out', help='write outfits/ and outfits.json under this folder instead of edf41port/')
    ap.add_argument('--only', action='append', default=[], metavar='CLASS', help=f'one of {sorted(outfit41.SPECS)}')
    a = ap.parse_args()
    data, files = build(a.edf41, a.edf6, tuple(a.only))
    out_json, out_files = (os.path.join(a.out, 'outfits.json'), os.path.join(a.out, 'outfits')) if a.out else (OUT, FILES)
    shutil.rmtree(out_files, ignore_errors=True)
    for rel, b in rabpool.pack(files).items():
        path = os.path.join(out_files, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(b)
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print(f'{out_json}: {len(data["classes"])} classes, {len(files)} files ({sum(map(len, files.values())) / 1e6:.1f} MB)')
    for c in data['classes']:
        r = c['report']
        print(f"  {c['class']}: {r['bones']} bones, added {len(r['added'])}, folded {len(r['folded'])}, "
              f"turned {sorted(r['turned'])}, worst joint {r['joint_error_max'] * 100:.1f} cm")
    for s in data['skipped']:
        print('  skipped', s['class'], s['reason'][:300])
    return 0


if __name__ == '__main__':
    sys.exit(main())
