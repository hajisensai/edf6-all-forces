"""Render reviewed finite resident sound fallbacks from the EDF4.1 registry.

make_edf41_weapons.py chooses and records targets from the real resident bank.
This renderer and --check need no games, and never rewrite sound-bank metadata.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import edf41_sound_fallbacks  # noqa: E402

REGISTRY = ROOT / 'edf41port' / 'weapons.json'
OUT = ROOT / 'src' / 'edf41_sound_fallbacks.inc'


def expected() -> str:
    data = json.loads(REGISTRY.read_text(encoding='utf-8'))
    return edf41_sound_fallbacks.render(data['sound_fallbacks'])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    wanted = expected()
    if args.check:
        if not OUT.is_file() or OUT.read_text(encoding='utf-8') != wanted:
            print(f'{OUT}: stale; run python tools/gen_edf41_sound_fallbacks.py')
            return 1
        return 0
    OUT.write_text(wanted, encoding='utf-8', newline='\n')
    print(OUT)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
