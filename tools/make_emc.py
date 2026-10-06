"""Writes the EMC's charged beam (src/emc.cpp, docs/emc-re.md, pylib/vcobjects.py EMC_*) into <game>/Mods/OBJECT:

  EDF6VC_EMC_BEAM.SGO    the beam: the satellite laser made thick and blue, a round a frame, penetrating
  EDF6VC_EMC_SIGHT.SGO   the charge's glow: the same thin and dim, silent, no damage
  EDF6VC_EMC_BREAK.SGO   a break charge: a 12 m blast (it breaks the buildings on the beam's line)
  EDF6VC_EMC_BLAST.SGO   the blast at the beam's end (its radius the plugin's EmcBlastRadius)

Nothing of the EMC itself changes: the stock V510_MASER and its weapon stay as they are, and without these files (or
with EmcBeam=0) its trigger fires the stock 1000-round burst. Built in memory from the player's own Root.cpk (only read),
written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_emc.py [game dir]               write / refresh
  python tools/make_emc.py [game dir] --out DIR     build and check only, the files written under DIR
  python tools/make_emc.py [game dir] --remove      release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'emc'   # pylib/ledger.py


def names() -> list[str]:
    """Every path under Mods this tool writes."""
    return [f'OBJECT/{n}' for n in vc.EMC_FILES]


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked (vcobjects.check_emc), from the game's Root.cpk."""
    game = vc.Game(root)
    rounds = vc.emc_rounds(game)
    vc.check_emc(rounds, game)
    return {f'OBJECT/{n}': data for n, data in rounds.items()}


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | {ledger.key(n) for n in names()}), writer=True)


def write_out(files: dict[str, bytes], out: str) -> list[str]:
    """The files under `out` (a scratch folder, never the game's): for an offline build and check."""
    paths = []
    for rel, data in files.items():
        p = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as h:
            h.write(data)
        paths.append(p)
    return paths


def main(argv: list[str]) -> int:
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] != '--out')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    files = build(root)
    for path in write_out(files, out) if out else install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
