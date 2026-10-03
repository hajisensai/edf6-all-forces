"""Writes the airstrike takeovers' jet SGOs (src/airstrike.cpp, src/jet.cpp) into <game>/Mods/OBJECT:
EDF6VC_JET_STRIKE.SGO and EDF6VC_JET_FIGHTER.SGO (and EDF6VC_BOMBER401 / _501_2.SGO, the strike jet in those
bombers' own models), made from this machine's own V506_HELI.SGO and
BOMBER501 model exactly like the test range's jets (testrange/gen.py: jet_sgo), and EDF6VC_JET.MRAB: the
stock BOMBER501.MRAB with its model split into elevon bones the plugin moves (tools/mdb_jet.py,
docs/mdb-format.md), which only these two SGOs use (the stock bombers keep theirs), and their guns into
<game>/Mods/WEAPON: EDF6VC_JET_GUN_L / _R.SGO (gen.jet_guns). Without the SGOs the plugin leaves the stock
bombers alone.

  python tools/make_jets.py [game dir]            write / refresh
  python tools/make_jets.py [game dir] --remove   delete them (only the files this script writes)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'testrange'))
import gen  # noqa: E402
sys.path.insert(0, HERE)
import mdb_jet  # noqa: E402  (tools/mdb_jet.py)

# file -> the testrange jet it is made like
FILES: dict[str, str] = {
    'EDF6VC_JET_STRIKE.SGO': 'edf6tr_jet_strike_mission',
    'EDF6VC_JET_FIGHTER.SGO': 'edf6tr_jet_fighter_mission',
    'EDF6VC_JET_INTERCEPTOR.SGO': 'edf6tr_jet_interceptor_mission',
    'EDF6VC_JET_MULTIROLE.SGO': 'edf6tr_jet_multirole_mission',
    'EDF6VC_JET_CARRIER.SGO': 'edf6tr_jet_carrier_mission',
    'EDF6VC_JET_DRONE.SGO': 'edf6tr_jet_drone',
}
# Their own models (tools/jet_models.py).
MODEL_FILES = sorted({gen.JETS[j].file for j in FILES.values() if gen.JETS[j].file})
# The strike jets that take over a BOMBER401 or BOMBER501_2 (src/jet.cpp kJetSgo): that bomber's own model,
# its mesh bone, and a box round its fuselage (bomber401: wings 52 m across but a fuselage about 5 x 4 x 16 m
# centred 2.14 m up; bomber501_2 is BOMBER501's mesh in another paint: the strike jet's box).
BOMBERS: dict[str, tuple[list[str], str, list[list[float]] | None]] = {
    'EDF6VC_BOMBER401.SGO': (['app:/object/bomber401.mrab', 'bomber401.mdb'], 'bomber401', [[0.0, 2.14, 0.0], [2.5, 2.0, 8.0]]),
    'EDF6VC_BOMBER501_2.SGO': (['app:/object/bomber501.mrab', 'bomber501_2.mdb'], 'bomber501', None),
}
# The helis the Air Raider's call weapons bring (src/jet.cpp HeliLaunch, tools/call_weapons.py): the stock
# call-in helis made script-placeable (gen.as_mission_sgo), so RideAi(true) gives them their weapons.
HELIS: dict[str, str] = {
    'EDF6VC_HELI_410.SGO': 'VEHICLE410_HELI',
    'EDF6VC_HELI_506.SGO': 'V506_HELI',
}
MODEL_FILE = gen.JET_ELEVON_FILE
MODEL = gen.JET_ELEVON_MODEL


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else gen.DEFAULT_GAME
    out = gen.object_dir(root)
    if '--remove' in argv:
        paths = [os.path.join(out, n) for n in [*FILES, *BOMBERS, *HELIS, MODEL_FILE, *MODEL_FILES]]
        for path in paths + [os.path.join(gen.weapon_dir(root), n) for n in gen.JET_GUN_FILES]:
            if os.path.exists(path):
                os.remove(path)
                print('删除', path)
        return 0
    game = gen.Game(root)
    os.makedirs(out, exist_ok=True)
    for path in gen.write_jet_guns(root, game):
        print('写入', path)
    arc = mdb_jet.jet_archive()[0]
    path = os.path.join(out, MODEL_FILE)
    with open(path, 'wb') as f:
        f.write(arc)
    print('写入', path, len(arc), '字节')
    import jet_models  # noqa: E402  (tools/jet_models.py)
    for name, data in jet_models.build(game).items():
        path = os.path.join(out, name)
        with open(path, 'wb') as f:
            f.write(data)
        print('写入', path, len(data), '字节')
    for name, jet in FILES.items():
        data = gen.jet_sgo(game, jet, MODEL)
        path = os.path.join(out, name)
        with open(path, 'wb') as f:
            f.write(data)
        print('写入', path, len(data), '字节')
    for name, (model, body, rigid) in BOMBERS.items():
        data = gen.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid)
        path = os.path.join(out, name)
        with open(path, 'wb') as f:
            f.write(data)
        print('写入', path, len(data), '字节')
    for name, stock in HELIS.items():
        data = gen.as_mission_sgo(game.read('OBJECT', stock + '.SGO'))
        path = os.path.join(out, name)
        with open(path, 'wb') as f:
            f.write(data)
        print('写入', path, len(data), '字节')
    # The test range's jets, when installed, get the elevon model too.
    for jet in gen.DERIVED.keys() & gen.JETS.keys():
        path = os.path.join(out, jet.upper() + '.SGO')
        if os.path.isfile(path):
            data = gen.jet_sgo(game, jet, MODEL)
            with open(path, 'wb') as f:
                f.write(data)
            print('更新', path, len(data), '字节')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
