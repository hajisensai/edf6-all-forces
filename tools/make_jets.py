"""Writes the airstrike takeovers' jet SGOs (src/airstrike.cpp, src/jet.cpp) into <game>/Mods/OBJECT:
EDF6VC_JET_STRIKE.SGO and EDF6VC_JET_FIGHTER.SGO (and EDF6VC_BOMBER401 / _501_2.SGO, the strike jet in those
bombers' own models), made from this machine's own V506_HELI.SGO and
BOMBER501 model exactly like the test range's jets (testrange/gen.py: jet_sgo), and EDF6VC_JET.MRAB: the
stock BOMBER501.MRAB with its model split into elevon bones the plugin moves (tools/mdb_jet.py,
docs/mdb-format.md), which only these two SGOs use (the stock bombers keep theirs), and their guns into
<game>/Mods/WEAPON: EDF6VC_JET_GUN_L / _R.SGO and the blast drones' charges EDF6VC_BLAST_CHARGE /
EDF6VC_DOLL_CHARGE.SGO (gen.jet_guns). Without the SGOs the plugin leaves the stock
bombers alone.
Also the player jets (src/playerjet.cpp): EDF6VC_PJET_FIGHTER / _STRIKE.SGO, which the Air Raider's call
weapons EDF6VC_CALL_PJET_* (tools/call_weapons.py) bring.
Also the gunship (EDF6VC_JET_GUNSHIP.SGO: the strike jet in BOMBER401's model with the gunship's own mark), the
blast / doll drone carriers (EDF6VC_JET_BLAST_CARRIER / _DOLL_CARRIER.SGO: the carrier with their marks) and the
impact charges a crash sets off (src/jet_bay.cpp ImpactDamage): EDF6VC_IMPACT_08 / _16 / _32 / _64.SGO.
Also the teleportation ships' portal laser (src/carrierlaser.cpp) into <game>/Mods/OBJECT:
EDF6VC_PORTAL_SIGHT.SGO (the aim light) and EDF6VC_PORTAL_LASER.SGO (the main beam) (gen.portal_lasers).

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
    'EDF6VC_JET_BLAST.SGO': 'edf6tr_jet_blast',
    'EDF6VC_JET_DOLL.SGO': 'edf6tr_jet_doll',
    # The blast and doll drone carriers: the carrier with the marks that name its drones (src/jet_internal.h
    # kBodies), so a carrier is what its mark says however its entry was made.
    'EDF6VC_JET_BLAST_CARRIER.SGO': 'edf6tr_jet_blast_carrier_mission',
    'EDF6VC_JET_DOLL_CARRIER.SGO': 'edf6tr_jet_doll_carrier_mission',
    # The jets the player flies (src/playerjet.cpp), which the Air Raider's EDF6VC_CALL_PJET_* weapons bring
    # (tools/call_weapons.py): with vehicle_setup as well as mission_setup.
    'EDF6VC_PJET_FIGHTER.SGO': 'edf6tr_pjet_fighter_mission',
    'EDF6VC_PJET_STRIKE.SGO': 'edf6tr_pjet_strike_mission',
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
# The gunship (src/jet_internal.h Body::gunship, kKinds' gunship row): the strike jet in BOMBER401's model
# (BOMBERS' recipe) with a mark of its own, so the plugin knows it for a gunship from its body alone.
GUNSHIP_FILE = 'EDF6VC_JET_GUNSHIP.SGO'
GUNSHIP_MARK = 7011.0
# Impact charges (src/jet_bay.cpp kCharges, ImpactDamage): the missions' whale gunship round (DemoIndirectFire,
# RocketBullet01, docs/mission-airstrike-re.md) made one round (indirect_fire_param #2), no gap (#3), no wait
# before it (#15), IMPACT_LIFE frames of life (#10) and its blast the file's radius in metres (#14,
# docs/carrier-laser-re.md §3: the indices' reading is M); its damage the plugin writes (IFC +0xDC).
IMPACT_STOCK = 'DEMOGUNSHIPFIREE25.SGO'
IMPACT_FILES: dict[str, float] = {
    'EDF6VC_IMPACT_08.SGO': 8.0,
    'EDF6VC_IMPACT_16.SGO': 16.0,
    'EDF6VC_IMPACT_32.SGO': 32.0,
    'EDF6VC_IMPACT_64.SGO': 64.0,
}
IMPACT_LIFE = 2
# The helis the Air Raider's call weapons bring (src/jet.cpp HeliLaunch, tools/call_weapons.py): the stock
# call-in helis made script-placeable (gen.as_mission_sgo), so RideAi(true) gives them their weapons.
HELIS: dict[str, str] = {
    'EDF6VC_HELI_410.SGO': 'VEHICLE410_HELI',
    'EDF6VC_HELI_506.SGO': 'V506_HELI',
}
MODEL_FILE = gen.JET_ELEVON_FILE
MODEL = gen.JET_ELEVON_MODEL


def with_mark(data: bytes, mark: float) -> bytes:
    """A jet SGO (gen.jet_sgo) with another mark in mission_setup (the speed gain k the plugin reads)."""
    import sgowrite
    version, m = sgowrite.read(data)
    m['mission_setup'][1][0] = mark
    return sgowrite.write(version, m)


def _number(v) -> float | None:
    """An SGO number node's value (sgowrite: int, or Float keeping its bytes), else None."""
    import sgowrite
    if isinstance(v, sgowrite.Float):
        return v.value
    return float(v) if isinstance(v, int) else None


def impact_charges(game: gen.Game) -> dict[str, bytes]:
    """The impact charges (IMPACT_FILES) from the stock gunship round."""
    import sgowrite
    out = {}
    for name, radius in IMPACT_FILES.items():
        version, m = sgowrite.read(game.read('OBJECT', IMPACT_STOCK))
        p = m.get('indirect_fire_param')
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or p[4] != 'RocketBullet01' or (_number(p[14]) or 0.0) <= 0.0
                or any(_number(p[i]) is None for i in (2, 3, 10, 15)) or 'indirect_fire_damage' not in m):
            raise ValueError(f'{IMPACT_STOCK} 不是预期的炮舰炮弹')
        for i, value in ((2, 1), (3, 0), (10, IMPACT_LIFE), (14, radius), (15, 0)):
            p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
        m['indirect_fire_damage'] = 0.0   # the plugin sets the damage (IFC +0xDC)
        out[name] = sgowrite.write(version, m)
    return out


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else gen.DEFAULT_GAME
    out = gen.object_dir(root)
    if '--remove' in argv:
        paths = [os.path.join(out, n) for n in [*FILES, *BOMBERS, GUNSHIP_FILE, *IMPACT_FILES, *HELIS, MODEL_FILE, *MODEL_FILES,
                                                *gen.PORTAL_LASER_FILES]]
        for path in paths + [os.path.join(gen.weapon_dir(root), n) for n in gen.JET_WEAPON_FILES]:
            if os.path.exists(path):
                os.remove(path)
                print('删除', path)
        return 0
    game = gen.Game(root)
    os.makedirs(out, exist_ok=True)
    for path in gen.write_jet_guns(root, game):
        print('写入', path)
    for path in gen.write_portal_lasers(root, game):
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
    model, body, rigid = BOMBERS['EDF6VC_BOMBER401.SGO']
    data = with_mark(gen.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid), GUNSHIP_MARK)
    path = os.path.join(out, GUNSHIP_FILE)
    with open(path, 'wb') as f:
        f.write(data)
    print('写入', path, len(data), '字节')
    for name, data in impact_charges(game).items():
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
