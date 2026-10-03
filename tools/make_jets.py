"""Writes the airstrike takeovers' jet SGOs (src/airstrike.cpp, src/jet.cpp) into <game>/Mods/OBJECT:
EDF6VC_JET_STRIKE.SGO and EDF6VC_JET_FIGHTER.SGO, made from this machine's own V506_HELI.SGO and
BOMBER501 model exactly like the test range's jets (testrange/gen.py: jet_sgo). Without them the plugin
leaves the stock bombers alone.

  python tools/make_jets.py [game dir]            write / refresh
  python tools/make_jets.py [game dir] --remove   delete them (only these two files)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'testrange'))
import gen  # noqa: E402

# file -> the testrange jet it is made like
FILES: dict[str, str] = {
    'EDF6VC_JET_STRIKE.SGO': 'edf6tr_jet_strike_mission',
    'EDF6VC_JET_FIGHTER.SGO': 'edf6tr_jet_fighter_mission',
}


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else gen.DEFAULT_GAME
    out = gen.object_dir(root)
    if '--remove' in argv:
        for name in FILES:
            path = os.path.join(out, name)
            if os.path.exists(path):
                os.remove(path)
                print('删除', path)
        return 0
    game = gen.Game(root)
    os.makedirs(out, exist_ok=True)
    for name, jet in FILES.items():
        data = gen.jet_sgo(game, jet)
        path = os.path.join(out, name)
        with open(path, 'wb') as f:
            f.write(data)
        print('写入', path, len(data), '字节')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
