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
Also the gunship (EDF6VC_JET_GUNSHIP.SGO: the strike jet in BOMBER401's model with the gunship's own mark and a gunner seat), the
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
    'EDF6VC_JET_PRIMER_FIGHTER.SGO': 'edf6tr_jet_primer_fighter_mission',
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
# Its crew (src/playerjet_crew.inc, README 炮舰机): seat 0 the pilot (the V506's own seat), seat GUNNER_SEAT the side
# gunner, a second vehicle_riding_position entry (the V506 has one; the seat count, veh+0x618, is the number of entries,
# each parsed by 0x62B430). An entry: [door locator (seat +0x1E0: the board reach, CanRideSeat 0x6346D0), seat locator
# (+0x1F0), [locator, x, y], pose (+0x18), class mask (+0x30), a number (+0x258), key-config row (+0x2B4: HumanBase
# 0x57339A reads the keyboard's triggers off key row (human+0xD40 x 16 + this): 5 a heli pilot's, 6 a gunner's)].
# The V506 MAB has the locators of its one seat only, and a name the model lacks is a null locator in the vehicle
# init (vcobjects.JET_MAB_ROOT): the gunner's seat shares the pilot's three locators (boarded from the same door; no
# one sees into the bomber). Its pose, class mask, number and key row are the stock door gunner's
# (GUNNER_STOCK's seat GUNNER_STOCK_SEAT: a pose every class has, mask 15). No weapon is put on it: every
# vehicle_weapon_setting keeps seat 0 (or -1, the fuel tank); its gun is the plugin's shells (src/jet_bay.cpp).
GUNNER_SEAT = 1
GUNNER_STOCK = 'VEHICLE410_HELI.SGO'
GUNNER_STOCK_SEAT = 1
GUNNER_POSE = '410_HELI_GUNNER_L'
GUNNER_KEYS = 6     # the key-config row of a gunner's seat
PILOT_KEYS = 5      # ...and of a heli pilot's (the V506's seat 0)
# Impact charges (src/jet_bay.cpp kCharges, ImpactDamage): the missions' gunship cannon round (DemoIndirectFire with a
# SolidBullet01, a stock pairing) made a charge that bursts on what it meets. indirect_fire_param (the IFC's parser
# 0x2B5F40, docs/carrier-laser-re.md §3): no scatter (#0), one round (#2), no gap (#3), no wait before it (#15),
# IMPACT_SPEED m a frame (#5), no gravity (#6), IMPACT_LIFE frames of life (#10), not penetrating (#11), its blast
# (#9 AmmoExplosion) the file's radius in metres. The plugin fires it straight down from kImpactDrop over the impact:
# into the ground under it, or through the enemy rammed in the air. Its damage the plugin writes (IFC +0xDC).
# (It was a GrenadeBullet01 that bursts at the end of its life: no stock DemoIndirectFire fires one, and it reads a
# bullet model the IFC never hands it, +0x1170 null: the game crashed in its first frame, the first ram, 2026-10-05
# 12:35, dump EDF6.exe.54488 at EDF+0x1100B90 from GrenadeBullet01's update 0x2648F0.)
IMPACT_STOCK = 'DEMOGUNSHIPFIRESOLID.SGO'
IMPACT_FILES: dict[str, float] = {
    'EDF6VC_IMPACT_08.SGO': 8.0,
    'EDF6VC_IMPACT_16.SGO': 16.0,
    'EDF6VC_IMPACT_32.SGO': 32.0,
    'EDF6VC_IMPACT_64.SGO': 64.0,
}
IMPACT_LIFE = 6
IMPACT_SPEED = 10.0
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
    out[f'OBJECT/{MODEL_FILE}'] = jet_models.elevon_archive(game)   # the elevon bomber, grounded
    for name, data in jet_models.build(game).items():
        out[f'OBJECT/{name}'] = data
    jet_models.check_nozzles(game)   # the plugin's flames (src/booster.cpp kJetNozzles) on these models' exits
    import primer_fighter_model   # the Primer fighter's own model (not a jet_models recipe)
    arc = primer_fighter_model.build(game)
    primer_fighter_model.check(arc)
    out[f'OBJECT/{vc.JETS["edf6tr_jet_primer_fighter_mission"].file}'] = arc
    for name, jet in FILES.items():
        out[f'OBJECT/{name}'] = vc.jet_sgo(game, jet, MODEL)
    for name, (model, body, rigid) in BOMBERS.items():
        out[f'OBJECT/{name}'] = vc.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid)
    model, body, rigid = BOMBERS['EDF6VC_BOMBER401.SGO']
    gunship = with_mark(vc.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid), GUNSHIP_MARK)
    gunship = with_gunner_seat(gunship, game.read('OBJECT', GUNNER_STOCK))
    check_gunner_seat(gunship)
    out[f'OBJECT/{GUNSHIP_FILE}'] = gunship
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


class GunnerSeatError(Exception):
    """The gunship's gunner seat is not what the plugin expects (check_gunner_seat)."""


def with_gunner_seat(data: bytes, stock: bytes) -> bytes:
    """The gunship SGO `data` (one seat, the pilot's) with its gunner seat after it (see GUNNER_SEAT): the pilot's
    entry with `stock`'s door gunner's pose, class mask, number and key-config row (`stock`: GUNNER_STOCK's bytes)."""
    import copy
    import sgo
    version, m = sgo.read(data)
    seats = m.get('vehicle_riding_position')
    if not (isinstance(seats, list) and len(seats) == 1 and isinstance(seats[0], list) and len(seats[0]) == 7):
        raise ValueError('炮舰机的 vehicle_riding_position 不是预期的一个座位')
    door = sgo.read(stock)[1].get('vehicle_riding_position')
    if not (isinstance(door, list) and len(door) > GUNNER_STOCK_SEAT and len(door[GUNNER_STOCK_SEAT]) == 7):
        raise ValueError(f'{GUNNER_STOCK} 没有第 {GUNNER_STOCK_SEAT} 号座位')
    gunner = door[GUNNER_STOCK_SEAT]
    if gunner[3] != GUNNER_POSE or gunner[6] != GUNNER_KEYS:
        raise ValueError(f'{GUNNER_STOCK} 的炮手座位不是预期的样子（{gunner[3]!r} / {gunner[6]!r}）')
    seat = copy.deepcopy(seats[0])
    seat[3:7] = copy.deepcopy(gunner[3:7])
    seats.append(seat)
    return sgo.write(version, m)


def check_gunner_seat(data: bytes) -> None:
    """Re-read the gunship SGO and raise GunnerSeatError unless: its mark is GUNSHIP_MARK; it has exactly two seats;
    seat 0 is the pilot's (PILOT_KEYS); the gunner seat has the pilot's three locators, GUNNER_POSE, a class mask every
    class passes (15) and GUNNER_KEYS; no weapon sits on the gunner seat (every vehicle_weapon_setting on seat 0 or -1),
    one setting a weapon of mission_setup."""
    import sgo

    def need(ok: bool, msg: str) -> None:
        if not ok:
            raise GunnerSeatError(f'{GUNSHIP_FILE}: {msg}')

    _, m = sgo.read(data)
    setup = m.get('mission_setup')
    need(isinstance(setup, list) and _number(setup[1][0]) == GUNSHIP_MARK, 'mission_setup 的标记不是炮舰机的')
    seats = m.get('vehicle_riding_position')
    need(isinstance(seats, list) and len(seats) == GUNNER_SEAT + 1, f'应有 {GUNNER_SEAT + 1} 个座位')
    pilot, gunner = seats[0], seats[GUNNER_SEAT]
    need(pilot[6] == PILOT_KEYS, f'驾驶座的按键行不是 {PILOT_KEYS}')
    need(gunner[:3] == pilot[:3], '炮手座的定位点（上车口 / 座位 / 第三个）应与驾驶座的相同（模型里只有这一组）')
    need(gunner[3] == GUNNER_POSE and gunner[4] == 15 and gunner[6] == GUNNER_KEYS,
         f'炮手座应是 {GUNNER_POSE} / 职业掩码 15 / 按键行 {GUNNER_KEYS}，实际 {gunner[3:]!r}')
    settings = m.get('vehicle_weapon_setting')
    need(isinstance(settings, list) and len(settings) == len(setup[3]), '武器挂点与 mission_setup 的武器数不符')
    need(all(_number(w[1]) in (0.0, -1.0) for w in settings), '有武器挂在炮手座上（炮手的炮弹是插件的）')


def _number(v) -> float | None:
    """An SGO number node's value (pylib/sgo.py: int, or Float keeping its bytes), else None."""
    import sgo
    if isinstance(v, sgo.Float):
        return v.value
    return float(v) if isinstance(v, int) else None


def impact_charge(game: vc.Game, radius: float, speed: float = IMPACT_SPEED, life: int = IMPACT_LIFE) -> bytes:
    """An impact charge (see IMPACT_FILES) from the stock gunship round: a `radius` m blast, `speed` m a frame for
    `life` frames (also tools/make_drill.py's drill charge)."""
    import sgo
    version, m = sgo.read(game.read('OBJECT', IMPACT_STOCK))
    p = m.get('indirect_fire_param')
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'SolidBullet01' or not isinstance(p[0], list) or len(p[0]) != 2
            or any(_number(p[i]) is None for i in (2, 3, 5, 6, 9, 10, 11, 15)) or 'indirect_fire_damage' not in m):
        raise ValueError(f'{IMPACT_STOCK} 不是预期的炮舰炮弹')
    for i, value in ((2, 1), (3, 0), (5, speed), (6, 0), (9, radius), (10, life), (11, 0), (15, 0)):
        p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
    p[0] = [0.0, 0.0]   # no scatter
    m['indirect_fire_damage'] = 0.0   # the plugin sets the damage (IFC +0xDC)
    return sgo.write(version, m)


def impact_charges(game: vc.Game) -> dict[str, bytes]:
    """The impact charges (IMPACT_FILES) from the stock gunship round."""
    return {name: impact_charge(game, radius) for name, radius in IMPACT_FILES.items()}


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
