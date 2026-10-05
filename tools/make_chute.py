"""Writes the player's parachute canopy (src/playerjet.cpp Chute*) into <game>/Mods:

  Mods/OBJECT/EDF6VC_CHUTE.MRAB   a dome canopy with its suspension lines in the Grape's seat fabric
                                  (pylib/chute_model.py; built from the player's own Root.cpk)
  Mods/OBJECT/EDF6VC_CHUTE.SGO    the stock far-off plant's FarEventObject with that model: render only, no physics
                                  (pylib/chute_model.py sgo)

The plugin preloads the SGO at a mission's start when the file is there and keeps one over the player while their
parachute is open. Built in memory first, then written atomically and recorded in the ledger as this tool's
(pylib/ledger.py); --remove releases them. No shared table is touched here.

  python tools/make_chute.py [game dir]            write / refresh
  python tools/make_chute.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import chute_model  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'chute'   # pylib/ledger.py


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    arc = chute_model.build(game)
    chute_model.check(arc)
    return {f'OBJECT/{chute_model.OUT_ARC}': arc, f'OBJECT/{chute_model.SGO_FILE}': chute_model.sgo(game)}


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    for path in install(root, build(root)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
