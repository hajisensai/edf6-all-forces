"""Read-only metadata audit of the actual AC/BVM selection and EDF5 native-AC candidates.

  python -B tools/audit_mission_scripts.py --game <EDF6> --mission EDF5_OLD_SCRIPT/M000
  python -B tools/audit_mission_scripts.py --game <EDF6> --edf5-candidates --json

No script source is printed or extracted. This cannot establish compilation, mission completion or sync.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'pylib'))
import mission_script as script
import rootcpk


class Source:
    def __init__(self, game: str, mods: bool = True) -> None:
        self.game = rootcpk.Game(game)
        self.mods = Path(game) / 'Mods' if mods else None
        self.index = {f'{d}/{n}'.upper(): (d, n) for d, n in self.game.cpk.index}

    def read(self, relative: str) -> script.File | None:
        if self.mods is not None:
            path = self.mods.joinpath(*relative.split('/'))
            if path.is_file():
                return script.File(path.read_bytes(), f'Mods/{relative}')
        key = self.index.get(relative.upper())
        if key is None:
            return None
        return script.File(self.game.read(*key), f'Root.cpk:{relative}')


def candidate_report(source: Source) -> list[dict]:
    catalog = json.loads((HERE.parent / 'edf5campaign' / 'missions.json').read_text(encoding='utf-8'))
    main = {row['path'].upper() for row in catalog['main']}
    candidates = sorted(d.rsplit('/', 1)[-1] for d, n in source.game.cpk.index
                        if d.upper().startswith('MISSION/EDF5_NEW_SCRIPT/') and n.upper() == 'MISSION.AC'
                        and d.rsplit('/', 1)[-1].upper() in main)
    result = []
    for key in candidates:
        old = f'MISSION/EDF5_OLD_SCRIPT/{key}'
        new = f'MISSION/EDF5_NEW_SCRIPT/{key}'
        a, b = source.read(old + '/MISSION.BVM'), source.read(new + '/MISSION.AC')
        if a is None or b is None:
            result.append({'key': key, 'error': 'paired script missing'})
            continue
        try:
            result.append({'key': key, **script.compare(a, b, source.read(old + '/MISSION.RMPA'),
                                                       source.read(new + '/MISSION.RMPA'))})
        except (ValueError, IndexError, KeyError, UnicodeError) as error:
            result.append({'key': key, 'error': f'{type(error).__name__}: {error}'})
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    parser.add_argument('--mission', action='append', default=[], help='mission key relative to MISSION/')
    parser.add_argument('--edf5-candidates', action='store_true', help='compare existing AC with the paired legacy BVM')
    parser.add_argument('--root-only', action='store_true', help='ignore Mods for a stock archive baseline')
    parser.add_argument('--json', action='store_true', help='emit metadata as JSON to stdout')
    args = parser.parse_args(argv)
    if not args.mission and not args.edf5_candidates:
        parser.error('provide --mission or --edf5-candidates')
    source = Source(args.game, mods=not args.root_only)
    report = {'contract': 'EDF.dll 0x678CCB46', 'evidence': 'static_only',
              'source_scope': 'Root.cpk' if args.root_only else 'Root.cpk + Mods',
              'missions': [script.audit(source.read, key) for key in args.mission]}
    if args.edf5_candidates:
        report['edf5_candidates'] = candidate_report(source)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        for mission in report['missions']:
            for name, selected in mission['scripts'].items():
                print(mission['key'], name, selected['state'], selected.get('format', ''), selected.get('source', ''))
                if selected.get('parse_error'):
                    print('  ERROR:', selected['parse_error'])
                metadata = selected.get('metadata', {})
                if metadata.get('missing_signatures'):
                    print('  unknown native signatures:', ', '.join(metadata['missing_signatures']))
        if args.edf5_candidates:
            candidates = report['edf5_candidates']
            print('EDF5 AC candidates:', len(candidates))
            print('level scope review:', ', '.join(c['key'] for c in candidates
                                                  if c.get('level_semantics') == 'requires_team_scope_review'))
            print('candidate errors:', dict(Counter(c['error'] for c in candidates if 'error' in c)))
        print('Static metadata only; engine compile / single-player / multiplayer remain unverified.')
    selected = [v for m in report['missions'] for v in m['scripts'].values()]
    return int(any(v['state'] in ('missing', 'blocked_before_gate') or 'parse_error' in v for v in selected)
               or any('error' in c for c in report.get('edf5_candidates', [])))


if __name__ == '__main__':
    raise SystemExit(main())
