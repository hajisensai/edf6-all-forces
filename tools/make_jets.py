"""Writes the airstrike takeovers' jet SGOs (src/airstrike.cpp, src/jet.cpp) into <game>/Mods/OBJECT:
EDF6VC_JET_STRIKE.SGO and EDF6VC_JET_FIGHTER.SGO (and EDF6VC_BOMBER401 / _501_2.SGO, the strike jet in those
bombers' own models), made from this machine's own V506_HELI.SGO and
BOMBER501 model exactly like the test range's jets (pylib/vcobjects.py jet_sgo), and EDF6VC_JET.MRAB: the
stock BOMBER501.MRAB with its model split into elevon bones the plugin moves (pylib/mdb_jet.py,
docs/mdb-format.md), which only these two SGOs use (the stock bombers keep theirs), and their guns into
<game>/Mods/WEAPON: EDF6VC_JET_GUN_L / _R.SGO, the stores EDF6VC_<KIND>_<rounds>.SGO (pylib/vcobjects.py STORES: missiles
with a jet's launch and motor sounds, bombs)
and the blast drones' charges EDF6VC_BLAST_CHARGE / EDF6VC_DOLL_CHARGE.SGO (vcobjects.jet_guns). The jets' rotor
sound is silenced (vcobjects.JET_ROTOR_SE_ROWS): the plugin plays their engine (src/jetsound.cpp). Without the SGOs the plugin leaves the stock
bombers alone.
Also the player jets (src/playerjet.cpp): EDF6VC_PJET_FIGHTER / _STRIKE.SGO, which the Air Raider's call
weapons EDF6VC_CALL_PJET_* (tools/call_weapons.py) bring.
Also the gunship (EDF6VC_JET_GUNSHIP.SGO: the strike jet in BOMBER401's model with the gunship's own mark), the
blast / doll drone carriers (EDF6VC_JET_BLAST_CARRIER / _DOLL_CARRIER.SGO: the carrier with their marks) and the
impact charges a crash sets off (src/jet_bay.cpp ImpactDamage): EDF6VC_IMPACT_08 / _16 / _32 / _64.SGO.
Also the teleportation ships' portal laser (src/carrierlaser.cpp) into <game>/Mods/OBJECT:
EDF6VC_PORTAL_SIGHT.SGO (the aim light) and EDF6VC_PORTAL_LASER.SGO (the main beam) (vcobjects.portal_lasers).

Every file is built in memory first (build: nothing is written unless all of it could be made), then written
atomically and recorded in the ledger as this tool's (pylib/ledger.py); --remove releases them, so a file
another tool still uses (the guns, which make_sub and the test range use too; the models the test range's jets
fly) stays. The test range's own jets are its own: it gives them the elevon model when it installs them.

  python tools/make_jets.py [game dir]            write / refresh
  python tools/make_jets.py [game dir] --remove   delete them (only the files this script writes)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import jet_models  # noqa: E402
import mdb_jet  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'jets'   # pylib/ledger.py

# file -> the testrange jet it is made like
FILES: dict[str, str] = {
    'EDF6VC_JET_STRIKE.SGO': 'edf6tr_jet_strike_mission',
    'EDF6VC_JET_FIGHTER.SGO': 'edf6tr_jet_fighter_mission',
    'EDF6VC_JET_INTERCEPTOR.SGO': 'edf6tr_jet_interceptor_mission',
    'EDF6VC_JET_ENEMY_FIGHTER.SGO': 'edf6tr_jet_enemy_fighter_mission',
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
# Their own models (pylib/jet_models.py).
MODEL_FILES = sorted({vc.JETS[j].file for j in FILES.values() if vc.JETS[j].file})
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
# docs/mission-airstrike-re.md) made a charge that bursts where it is set off. indirect_fire_param (the IFC's
# parser 0x2B5F40, docs/carrier-laser-re.md §3): one round (#2), no gap (#3), no wait before it (#15), the bullet
# class GrenadeBullet01 (#4: a RocketBullet01 never bursts at the end of its life, only on what it meets; a
# GrenadeBullet01 with Ammo_CustomParameter #0 = 1 does, docs/decoy-blast-re.md 1.4), slow (#5 IMPACT_SPEED m a
# frame), no gravity (#6), not penetrating (#11), IMPACT_LIFE frames of life (#10), its blast (#9 AmmoExplosion)
# the file's radius in metres; #13 the grenade's custom parameter: [1 bursts at the end of its life, 0 no gravity,
# 1, 0 no bounce, 0, 0 no random extra life]. Its damage the plugin writes (IFC +0xDC).
IMPACT_STOCK = 'DEMOGUNSHIPFIREE25.SGO'
IMPACT_FILES: dict[str, float] = {
    'EDF6VC_IMPACT_08.SGO': 8.0,
    'EDF6VC_IMPACT_16.SGO': 16.0,
    'EDF6VC_IMPACT_32.SGO': 32.0,
    'EDF6VC_IMPACT_64.SGO': 64.0,
}
IMPACT_LIFE = 2
IMPACT_SPEED = 0.25
IMPACT_CUSTOM: list = [1, 0.0, 1.0, 0.0, 0.0, 0]
# The helis the Air Raider's call weapons bring (src/jet.cpp HeliLaunch, tools/call_weapons.py): the stock
# call-in helis made script-placeable (vcobjects.as_mission_sgo), so RideAi(true) gives them their weapons.
HELIS: dict[str, str] = {
    'EDF6VC_HELI_410.SGO': 'VEHICLE410_HELI',
    'EDF6VC_HELI_506.SGO': 'V506_HELI',
}
MODEL_FILE = vc.JET_ELEVON_FILE
MODEL = vc.JET_ELEVON_MODEL


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, made from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    out: dict[str, bytes] = {}
    for name, data in vc.jet_guns(game).items():
        out[f'WEAPON/{name}'] = data
    for name, data in vc.portal_lasers(game).items():
        out[f'OBJECT/{name}'] = data
    out[f'OBJECT/{MODEL_FILE}'] = mdb_jet.jet_archive(game.read('OBJECT', 'BOMBER501.MRAB'))[0]
    for name, data in jet_models.build(game).items():
        out[f'OBJECT/{name}'] = data
    for name, jet in FILES.items():
        out[f'OBJECT/{name}'] = vc.jet_sgo(game, jet, MODEL)
    for name, (model, body, rigid) in BOMBERS.items():
        out[f'OBJECT/{name}'] = vc.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid)
    model, body, rigid = BOMBERS['EDF6VC_BOMBER401.SGO']
    out[f'OBJECT/{GUNSHIP_FILE}'] = with_mark(vc.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid), GUNSHIP_MARK)
    for name, data in impact_charges(game).items():
        out[f'OBJECT/{name}'] = data
    for name, stock in HELIS.items():
        out[f'OBJECT/{name}'] = vc.as_mission_sgo(game.read('OBJECT', stock + '.SGO'))
    return out


def names() -> list[str]:
    """Every path under Mods this tool writes (whether or not installed)."""
    objects = [*FILES, *BOMBERS, GUNSHIP_FILE, *IMPACT_FILES, *HELIS, MODEL_FILE, *MODEL_FILES, *vc.PORTAL_LASER_FILES]
    return [f'OBJECT/{n}' for n in objects] + [f'WEAPON/{n}' for n in vc.JET_WEAPON_FILES]


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files (and ones an install from before the ledger left): (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | {ledger.key(n) for n in names()}), writer=True)


def with_mark(data: bytes, mark: float) -> bytes:
    """A jet SGO (vc.jet_sgo) with another mark in mission_setup (the speed gain k the plugin reads)."""
    import sgo
    version, m = sgo.read(data)
    m['mission_setup'][1][0] = mark
    return sgo.write(version, m)


def _number(v) -> float | None:
    """An SGO number node's value (pylib/sgo.py: int, or Float keeping its bytes), else None."""
    import sgo
    if isinstance(v, sgo.Float):
        return v.value
    return float(v) if isinstance(v, int) else None


def impact_charges(game: vc.Game) -> dict[str, bytes]:
    """The impact charges (IMPACT_FILES) from the stock gunship round."""
    import sgo
    out = {}
    for name, radius in IMPACT_FILES.items():
        version, m = sgo.read(game.read('OBJECT', IMPACT_STOCK))
        p = m.get('indirect_fire_param')
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or p[4] != 'RocketBullet01' or not isinstance(p[13], list)
                or any(_number(p[i]) is None for i in (2, 3, 5, 6, 9, 10, 11, 15)) or 'indirect_fire_damage' not in m):
            raise ValueError(f'{IMPACT_STOCK} 不是预期的炮舰炮弹')
        for i, value in ((2, 1), (3, 0), (5, IMPACT_SPEED), (6, 0), (9, radius), (10, IMPACT_LIFE), (11, 0), (15, 0)):
            p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
        p[4] = 'GrenadeBullet01'
        p[13] = list(IMPACT_CUSTOM)
        m['indirect_fire_damage'] = 0.0   # the plugin sets the damage (IFC +0xDC)
        out[name] = sgo.write(version, m)
    return out


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
