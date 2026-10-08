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
weapons EDF6VC_CALL_PJET_* (tools/call_weapons.py) bring, and EDF6VC_FLY_<KIND>.SGO: the plugin's other aircraft
(interceptor, fighter, multirole, gunship, drone, the three air carriers) parked empty, which EDF6VC_CALL_FLY_* bring.
Also the gunship (EDF6VC_JET_GUNSHIP.SGO: the strike jet in BOMBER401's model with the gunship's own mark and a gunner seat), the
blast / doll drone carriers (EDF6VC_JET_BLAST_CARRIER / _DOLL_CARRIER.SGO: the carrier with their marks) and the
impact charges a crash or a ground vehicle's ram sets off (src/jet_bay.cpp ImpactDamage): EDF6VC_IMPACT_08 / _16 / _32 / _64 / _02 / _04 / _12.SGO, and the gunship's
side guns' rounds (src/jet_bay.cpp GunShot): the long-range cannon's EDF6VC_GUNSHIP_CANNON.SGO and the 25 mm
gatling's EDF6VC_GUNSHIP_GATLING.SGO.
Also the Primer creatures, enemies (src/primer.cpp, docs/primer-plan.md): EDF6VC_CENTIPEDE / _DRAGONFLY.SGO, their
own models EDF6VC_CENTIPEDE / _DRAGONFLY.MRAB (pylib/centipede_model.py, pylib/dragonfly_model.py) and their guns
EDF6VC_PRIMER_SPIT / _NEEDLE.SGO.
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

import copy
import os
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import jet_models  # noqa: E402
import mdb_jet  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'jets'   # pylib/ledger.py


def request_file(kind: str) -> str:
    """The Mods/OBJECT file of the requested twin of the NPC jet `kind` (vcobjects.REQUEST_KINDS): EDF6VC_FLY_<KIND>.SGO."""
    short = kind.removeprefix('edf6tr_jet_').removesuffix('_mission')
    return f'EDF6VC_FLY_{short.upper()}.SGO'


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
    # The plugin's other aircraft the Air Raider requests empty to fly (EDF6VC_CALL_FLY_*, tools/call_weapons.py): each
    # one's requested twin (vcobjects.REQUEST_KINDS), with vehicle_setup as well as mission_setup. The gunship's gets
    # the NPC gunship's gunner seat (with_gunner_seat).
    **{request_file(k): vc.request_name(k) for k in vc.REQUEST_KINDS},
    # The Primer creatures (src/primer.cpp, docs/primer-plan.md): enemies a mission places.
    'EDF6VC_CENTIPEDE.SGO': 'edf6tr_centipede_mission',
    'EDF6VC_DRAGONFLY.SGO': 'edf6tr_dragonfly_mission',
}
# Their own models (pylib/jet_models.py).
MODEL_FILES = sorted({vc.JETS[j].file for j in FILES.values() if vc.JETS[j].file})
# The strike jets that take over a BOMBER401 or BOMBER501_2 (src/jet.cpp kJetSgo): that bomber's own model,
# its mesh bone, and a box round its fuselage (bomber401: wings 52 m across but a fuselage about 5 x 4 x 16 m
# centred 2.14 m up; bomber501_2, BOMBER501's mesh in another paint, None: measured off its model, jet_models.fuselage_box
# (it was the strike jet's old hand-made box, cut at the origin: the fuselage's lower 1.29 m and 2.26 m at each end
# outside it). Both are only made in the air (vcobjects.jet_sgo airborne): their boxes are their models' as they are.
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
    # Appended (src/jet_bay.cpp kCharges keeps this order): the ground vehicles' ram (src/vehicleram.cpp: a foot, a fist,
    # a hull's slab are a few metres) and the jets' own size (a 12 m strike jet had a 16 m blast). The plugin picks the
    # one nearest the part's size; an install without these takes the nearest of the four above.
    'EDF6VC_IMPACT_02.SGO': 2.0,
    'EDF6VC_IMPACT_04.SGO': 4.0,
    'EDF6VC_IMPACT_12.SGO': 12.0,
}
IMPACT_LIFE = 6
IMPACT_SPEED = 10.0
# The gunship's long-range side cannon (src/jet_bay.cpp kCannonSgo / CannonShot, README 炮舰机的机炮; the user 2026-10-05:
# "炮舰机应该加装远距离机炮", an AC-130's 30-40 mm side gun): an impact charge (impact_charge) made a 40 mm HE round:
# CANNON_SPEED m a frame (960 m/s, a 40 mm Bofors' ~880, the AC-130J's 30 mm ~1080), no fall, CANNON_LIFE frames (past
# CANNON_REACH, src/jet_bay.cpp kCannonReach: a round aimed at the ground within it meets the ground; one aimed past it
# is gone without a burst), a CANNON_RADIUS m blast (a few metres, 3 m or more: as the drill's, it breaks buildings,
# docs/drill-re.md §3), not penetrating (it bursts on what it meets first). The stock round is the whale's huge solid
# shot (#7 AmmoSize 10, blue): a thin orange tracer here (#7 CANNON_SIZE, #8 CANNON_HIT: a hit radius of their product,
# #12 AmmoColor CANNON_COLOR). Its fire and hit sounds stay the stock cannon's. The plugin writes its damage (60 a round
# times the gunship's tier) and fires it straight from the gunship at the aim point.
CANNON_FILE = 'EDF6VC_GUNSHIP_CANNON.SGO'
CANNON_RADIUS = 4.0
CANNON_SPEED = 16.0
CANNON_LIFE = 170
CANNON_REACH = 2500.0
CANNON_SIZE = 0.8
CANNON_HIT = 2.0
CANNON_COLOR = (6.0, 3.0, 0.6, 1.0)
# The gunship's 25 mm gatling (src/jet_bay.cpp kGatlingSgo, README 炮舰机的机炮; the user 2026-10-07: 「炮舰机的机炮等」
# 应该有的挂载都要有, an AC-130's GAU-12/U beside the 40 mm and the 105 mm): the cannon's round made a 25 mm HEI round:
# GATLING_SPEED m a frame (1020 m/s, the GAU-12's ~1040), no fall, GATLING_LIFE frames (past GATLING_REACH, src/jet_bay.cpp
# kGatlingReach: shorter than the cannon's, a lighter round), a GATLING_RADIUS m blast (under 3 m: unlike the cannon's it
# does not break buildings, docs/drill-re.md §3; a 25 mm shell's fragments reach a couple of metres), not penetrating, a
# thinner, yellower tracer (#7 GATLING_SIZE, #8 GATLING_HIT, #12 GATLING_COLOR). The plugin writes its damage (12 a round, a
# fifth of the cannon's, ten a second, times the gunship's tier), scatters the rounds round the aim (kGatlingSpread) and
# fires them straight from the gunship.
GATLING_FILE = 'EDF6VC_GUNSHIP_GATLING.SGO'
GATLING_RADIUS = 2.0
GATLING_SPEED = 17.0
GATLING_LIFE = 100
GATLING_REACH = 1500.0
GATLING_SIZE = 0.4
GATLING_HIT = 2.0
GATLING_COLOR = (6.0, 4.5, 1.0, 1.0)


@dataclass(frozen=True)
class GunRound:
    """A gunship side gun's round (src/jet_bay.cpp kSideGuns): its file, blast (m), speed (m a frame), life (frames), the
    gun's reach (m), thickness (#7 AmmoSize x #8 AmmoHitSizeAdjust) and tracer colour (#12)."""
    file: str
    radius: float
    speed: float
    life: int
    reach: float
    size: float
    hit: float
    color: tuple[float, float, float, float]


CANNON = GunRound(CANNON_FILE, CANNON_RADIUS, CANNON_SPEED, CANNON_LIFE, CANNON_REACH, CANNON_SIZE, CANNON_HIT, CANNON_COLOR)
GATLING = GunRound(GATLING_FILE, GATLING_RADIUS, GATLING_SPEED, GATLING_LIFE, GATLING_REACH, GATLING_SIZE, GATLING_HIT,
                   GATLING_COLOR)
SIDE_GUNS: tuple[GunRound, ...] = (CANNON, GATLING)   # src/jet_bay.cpp kSideGuns' order
# Where the gunship's rounds leave (src/gunmuzzle.h; the user, 2026-10-06: 「炮舰机的机炮会打到自己身上」): off its airframe,
# the stock bomber401 model's whole box [centre, half extents] (jet_models.model_box, the vehicle's axes and origin), on
# the line from the box's centre to the aim, a round's hit radius (#7 AmmoSize x #8 AmmoHitSizeAdjust) and a margin past
# it. From the vehicle's origin, the belly's floor, the line to a target inside the banked circle crossed the plane.
# check_gunship_muzzle holds these to the game's model and the stock shell; tools/selftest.py holds src/gunmuzzle.h to them.
GUNSHIP_AIRFRAME = ((0.0, 2.136, 0.0), (25.938, 2.009, 8.078))
SHELL_STOCK = 'DEMOGUNSHIPFIREE25.SGO'   # the gunship's shells (src/jet_bay.cpp kGunshipSgo), fired as the game has them
SHELL_HIT = 10.0                         # its hit radius: #7 10 x #8 1
# The helis the Air Raider's call weapons bring (src/jet.cpp HeliLaunch, tools/call_weapons.py): the stock
# call-in helis made script-placeable (vcobjects.as_mission_sgo), so RideAi(true) gives them their weapons.
HELIS: dict[str, str] = {
    'EDF6VC_HELI_410.SGO': 'VEHICLE410_HELI',
    'EDF6VC_HELI_506.SGO': 'V506_HELI',
}
# The medic heli (the user, 2026-10-06: 「增加救护直升机，射的子弹射到队友会回血，自瞄也是锁队友」; src/heli.cpp Medic):
# the call-in Brute 410 made script-placeable like EDF6VC_HELI_410, its door guns' weapon MEDIC_GUN_STOCK swapped for
# MEDIC_GUN_FILE. That gun fires the stock healing round (the user, 2026-10-06: 「不是有加血的子弹吗」): the Ranger's Reverse
# Shooter T1's (MEDIC_ROUND_STOCK, aWeapon213 「リバースシューターＴ１」: a PlasmaBullet01 with a negative AmmoDamage and a
# blast, healing the friends round where it bursts; the game has no other healing round, the Reverser's own is a flame
# with a negative damage too). Its class, model, size, colour, hit sound and fuse (MEDIC_ROUND_FIELDS) on the door gun,
# the door gun's flight kept (its speed, life and drop: its reach, which the plugin's aim reads off the weapon). The heal is
# MEDIC_GUN_HEAL a round over MEDIC_GUN_BLAST m (T1: 1000 over 15 m, 5 rounds a reload; this one 2 a second), more rounds
# (the plugin refills no called heli's guns: MEDIC_GUN_AMMO is two minutes of healing a gun). A round with a blast skips a
# friend's body (docs/bullet-pass-re.md §3.2 step 4), so it bursts on the ground at their feet, where the gunners aim.
# The plugin knows a medic by this negative damage (src/heli.cpp HealingGun): its gunners then aim at hurt friends, never
# enemies, and the player's gun in one is aimed for them (ini MedicGunnerAim).
MEDIC_HELI_FILE = 'EDF6VC_HELI_MEDIC.SGO'
MEDIC_GUN_FILE = 'EDF6VC_MEDIC_GUN.SGO'
MEDIC_GUN_STOCK = 'V_410HELI_GATLING01.SGO'
MEDIC_ROUND_STOCK = 'AWEAPON213.SGO'
MEDIC_ROUND_FIELDS = ('AmmoClass', 'AmmoModel', 'AmmoSize', 'AmmoHitSizeAdjust', 'AmmoColor', 'Ammo_CustomParameter',
                      'AmmoHitSe', 'AmmoIsPenetration')
MEDIC_GUN_HEAL = 150.0       # HP a round, at the base tier (the call's tier multiplies it as it does the stock 200 damage)
MEDIC_GUN_BLAST = 8.0        # m
MEDIC_GUN_AMMO = 240.0
MEDIC_GUN_NAMES = {'ja': 'メディックガン', 'en': 'Medic Gun', 'cn': '救護機砲', 'kr': '메딕 건', 'sc': '救护机炮'}
MODEL_FILE = vc.JET_ELEVON_FILE
MODEL = vc.JET_ELEVON_MODEL


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, made from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    out: dict[str, bytes] = {}
    import aircraft_collision
    for name, data in aircraft_collision.assets(game).items():
        out[f'OBJECT/{name}'] = data
    for name, data in vc.jet_guns(game).items():
        out[f'WEAPON/{name}'] = data
    for name, data in vc.portal_lasers(game).items():
        out[f'OBJECT/{name}'] = data
    out[f'OBJECT/{MODEL_FILE}'] = jet_models.elevon_archive(game)   # the elevon bomber, grounded
    for name, data in jet_models.build(game).items():
        out[f'OBJECT/{name}'] = data
    for model, name in jet_models.ANIMATIONS.items():
        out[f'OBJECT/{name}'] = jet_models.animation(game, model)
    jet_models.check_nozzles(game)   # the plugin's flames (src/booster.cpp kJetNozzles) on these models' exits
    import primer_fighter_model   # the Primer fighter's own model (not a jet_models recipe)
    arc = primer_fighter_model.build(game)
    primer_fighter_model.check(arc)
    out[f'OBJECT/{vc.JETS["edf6tr_jet_primer_fighter_mission"].file}'] = arc
    for name, jet in FILES.items():
        data = vc.jet_sgo(game, jet, MODEL)
        if vc.JETS[jet].mark == GUNSHIP_MARK:   # the requested gunship: the gunner seat the NPC gunship has
            data = with_gunner_seat(data, game.read('OBJECT', GUNNER_STOCK))
            check_gunner_seat(data, name)
        out[f'OBJECT/{name}'] = data
    for name in BOMBERS:
        out[f'OBJECT/{name}'] = bomber_sgo(game, name)
    gunship = with_mark(bomber_sgo(game, 'EDF6VC_BOMBER401.SGO'), GUNSHIP_MARK)
    gunship = with_gunner_seat(gunship, game.read('OBJECT', GUNNER_STOCK))
    check_gunner_seat(gunship)
    out[f'OBJECT/{GUNSHIP_FILE}'] = gunship
    for name, data in impact_charges(game).items():
        out[f'OBJECT/{name}'] = data
    for gun in SIDE_GUNS:
        data = gun_round(game, gun)
        check_gun_round(data, gun)
        out[f'OBJECT/{gun.file}'] = data
    check_gunship_muzzle(game)
    for name, stock in HELIS.items():
        out[f'OBJECT/{name}'] = vc.as_mission_sgo(game.read('OBJECT', stock + '.SGO'))
    out[f'WEAPON/{MEDIC_GUN_FILE}'] = medic_gun(game.read('WEAPON', MEDIC_GUN_STOCK), game.read('WEAPON', MEDIC_ROUND_STOCK))
    out[f'OBJECT/{MEDIC_HELI_FILE}'] = medic_heli(out[f'OBJECT/EDF6VC_HELI_410.SGO'])
    return out


def medic_gun(stock: bytes, round_stock: bytes) -> bytes:
    """MEDIC_GUN_FILE from the stock door gun `stock` (MEDIC_GUN_STOCK's bytes) and the Reverse Shooter T1 `round_stock`
    (MEDIC_ROUND_STOCK's): see MEDIC_HELI_FILE."""
    import dsgo
    doc, rnd = dsgo.parse(stock), dsgo.parse(round_stock).root
    r = doc.root
    if r.get('AmmoClass') != 'SolidBullet01' or not r.get('AmmoDamage') > 0 or r.get('AmmoExplosion') != 0:
        raise ValueError(f'{MEDIC_GUN_STOCK} 不是预期的 410 门炮（实弹、无爆炸、正伤害）')
    heal = rnd.get('AmmoDamage')
    if rnd.get('AmmoClass') != 'PlasmaBullet01' or not (isinstance(heal, dsgo.Node) and heal.items[0] < 0):
        raise ValueError(f'{MEDIC_ROUND_STOCK} 不是预期的回复射手（PlasmaBullet01、负伤害）')
    for key in MEDIC_ROUND_FIELDS:
        r.set(key, copy.deepcopy(rnd.get(key)))
    for key, value in (('AmmoDamage', -MEDIC_GUN_HEAL), ('AmmoExplosion', MEDIC_GUN_BLAST), ('AmmoCount', MEDIC_GUN_AMMO)):
        r.set(key, value)
    for lang, name in MEDIC_GUN_NAMES.items():
        r.set(f'name.{lang}', name)
    return dsgo.write(doc)


def medic_heli(heli410: bytes) -> bytes:
    """MEDIC_HELI_FILE from EDF6VC_HELI_410.SGO's bytes: its door guns' weapon MEDIC_GUN_FILE, the rest as it is."""
    import sgo
    version, m = sgo.read(heli410)
    stock, ours = f'app:/weapon/{MEDIC_GUN_STOCK.lower()}', f'app:/weapon/{MEDIC_GUN_FILE.lower()}'
    weapons = m['mission_setup'][3]
    swapped = 0
    for w in weapons:
        if isinstance(w, list) and w and isinstance(w[0], str) and w[0].lower() == stock:
            w[0] = ours
            swapped += 1
    if swapped != 1:
        raise ValueError(f'EDF6VC_HELI_410 的 mission_setup 武器表里 {stock} 出现 {swapped} 次（应为 1）')
    return sgo.write(version, m)


def bomber_sgo(game: vc.Game, name: str) -> bytes:
    """The strike jet in BOMBERS[name]'s stock model, its box BOMBERS' or (None) its model's fuselage, as it is."""
    model, body, rigid = BOMBERS[name]
    if rigid is None:
        rigid = jet_models.fuselage_box(game, model[1].removesuffix('.mdb'))
    return vc.jet_sgo(game, 'edf6tr_jet_strike_mission', model, body, rigid, airborne=True)


def names() -> list[str]:
    """Every path under Mods this tool writes (whether or not installed)."""
    import aircraft_collision
    objects = [*FILES, *BOMBERS, GUNSHIP_FILE, *IMPACT_FILES, *(g.file for g in SIDE_GUNS), *HELIS, MEDIC_HELI_FILE, MODEL_FILE, *MODEL_FILES,
               *vc.PORTAL_LASER_FILES, *aircraft_collision.FILES.values(), *jet_models.ANIMATIONS.values()]
    return [f'OBJECT/{n}' for n in objects] + [f'WEAPON/{n}' for n in (*vc.JET_WEAPON_FILES, MEDIC_GUN_FILE)]


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


def check_gunner_seat(data: bytes, name: str = GUNSHIP_FILE) -> None:
    """Re-read the gunship SGO and raise GunnerSeatError unless: its mark is GUNSHIP_MARK; it has exactly two seats;
    seat 0 is the pilot's (PILOT_KEYS); the gunner seat has the pilot's three locators, GUNNER_POSE, a class mask every
    class passes (15) and GUNNER_KEYS; no weapon sits on the gunner seat (every vehicle_weapon_setting on seat 0 or -1),
    one setting a weapon of mission_setup."""
    import sgo

    def need(ok: bool, msg: str) -> None:
        if not ok:
            raise GunnerSeatError(f'{name}: {msg}')

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


def _same(got: float | None, want: float) -> bool:
    """Whether an SGO number (a 32-bit float) is `want`, to its precision."""
    return got is not None and abs(got - want) <= 1e-6 * max(1.0, abs(want))


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


def gun_round(game: vc.Game, gun: GunRound) -> bytes:
    """A gunship side gun's round (CANNON, GATLING): an impact charge with the gun's blast, speed and life, its tracer's
    thickness and colour."""
    import sgo
    version, m = sgo.read(impact_charge(game, gun.radius, gun.speed, gun.life))
    p = m['indirect_fire_param']
    if (_number(p[7]) is None or _number(p[8]) is None or not isinstance(p[12], list) or len(p[12]) != len(gun.color)
            or any(_number(c) is None for c in p[12])):
        raise ValueError(f'{IMPACT_STOCK} 的弹体粗细 / 颜色不是预期的样子')
    p[7], p[8] = float(gun.size), float(gun.hit)
    p[12] = [float(c) for c in gun.color]
    return sgo.write(version, m)


class GunshipMuzzleError(Exception):
    """The gunship's model or stock shell is not what src/gunmuzzle.h fires from (check_gunship_muzzle)."""


def check_gunship_muzzle(game: vc.Game) -> None:
    """Raise GunshipMuzzleError unless the stock bomber401's whole box is GUNSHIP_AIRFRAME and the stock shell's hit radius
    (#7 x #8) SHELL_HIT: the numbers src/gunmuzzle.h puts the gunship's muzzle off its airframe with."""
    import sgo
    box = jet_models.model_box(game, 'bomber401')
    if any(not _same(a, b) for got, want in zip(box, GUNSHIP_AIRFRAME) for a, b in zip(got, want)):
        raise GunshipMuzzleError(f'bomber401 的整机包围盒是 {box}，不是 GUNSHIP_AIRFRAME {GUNSHIP_AIRFRAME}')
    p = sgo.read(game.read('OBJECT', SHELL_STOCK))[1].get('indirect_fire_param')
    hit = _number(p[7]) * _number(p[8]) if isinstance(p, list) and len(p) == 19 and None not in (_number(p[7]), _number(p[8])) else None
    if hit is None or not _same(hit, SHELL_HIT):
        raise GunshipMuzzleError(f'{SHELL_STOCK} 的命中半径是 {hit}，不是 SHELL_HIT {SHELL_HIT}')


class GunRoundError(Exception):
    """A gunship side gun's round is not what the plugin fires (check_gun_round)."""


def check_gun_round(data: bytes, gun: GunRound) -> None:
    """Re-read side gun `gun`'s round and raise GunRoundError unless it is a DemoIndirectFire firing one SolidBullet01
    with no scatter (the plugin scatters the gatling's), no gap and no wait, gun.speed m a frame with no fall for gun.life
    frames (at least gun.reach), a gun.radius m blast, not penetrating, gun.size x gun.hit thick in gun.color, its damage
    the plugin's (0)."""
    import sgo

    def need(ok: bool, msg: str) -> None:
        if not ok:
            raise GunRoundError(f'{gun.file}: {msg}')

    _, m = sgo.read(data)
    p = m.get('indirect_fire_param')
    need(m.get('xgs_scene_object_class') == 'DemoIndirectFire' and isinstance(p, list) and len(p) == 19, '不是 DemoIndirectFire')
    need(p[4] == 'SolidBullet01', f'弹种应是 SolidBullet01，实际 {p[4]!r}')
    need([_number(x) for x in p[0]] == [0.0, 0.0], '应无散布')
    want = {2: 1, 3: 0, 5: gun.speed, 6: 0, 7: gun.size, 8: gun.hit, 9: gun.radius, 10: gun.life, 11: 0, 15: 0}
    got = {i: _number(p[i]) for i in want}
    need(all(_same(got[i], v) for i, v in want.items()), f'参数不符：{got}')
    need(len(p[12]) == len(gun.color) and all(_same(_number(c), v) for c, v in zip(p[12], gun.color)), '曳光颜色不符')
    need(gun.speed * gun.life >= gun.reach, f'飞不到 {gun.reach:.0f} m 的射程')
    need(_number(m.get('indirect_fire_damage')) == 0.0, '伤害应由插件写（SGO 里为 0）')


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
