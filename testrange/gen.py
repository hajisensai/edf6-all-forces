"""EDF6 test range: builds a mission script from the chosen vehicles and enemy waves and installs it
over one offline mission (see SLOTS) through EDFModLoader's file redirect.

The range is the open plain of mission M045 (map ig_Heigen601): its point file (MISSION.RMPA) is
copied next to the generated script, so every point the script names exists. Both files are made on
this machine from the player's own Root.cpk and never distributed.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import struct
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'lib'))
import cpk  # noqa: E402
import crilayla  # noqa: E402
import rmpa  # noqa: E402

DEFAULT_GAME = r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
SOURCE = 'M045'            # the plain whose map and points the range uses
MAP = 'app:/Map/ig_Heigen601.mac'
WEATHER = 'cloudy'
MARKER = 'EDF6TestRange.txt'


@dataclass(frozen=True)
class Slot:
    mission: str
    item: int          # position in the offline mission list (1-based)
    label: str


# Slots, by their position in the in-game offline mission list: item N is entry N-1 of
# MISSION/MISSIONLIST.OFFLINE.LIST.SGO, and its title is entry N-1 of MISSIONLIST.OFFLINE.TXT.*.SGO
# (item 14 「转机」 = RM015, item 2 「非法入侵者」 = M001). The range only borrows the slot: its map and
# points are always M045's (SOURCE). The opening missions are the ruined world, where the Air Raider's
# vehicle and air support requests are accepted but never arrive; in item 14 they do.
SLOTS = [
    Slot('RM015', 14, '列表第 14 项「转机」（RM015）：空袭兵能呼叫载具和空中支援'),
    Slot('M001', 2, '列表第 2 项「非法入侵者」（M001）：新存档也能进，但前期剧情叫不来载具和空中支援'),
]
DEFAULT_SLOT = SLOTS[0].mission


def slot_of(mission: str) -> Slot:
    return next(x for x in SLOTS if x.mission == mission)

# (sgo, label). Only SGOs with a `mission_setup` block (weapon set-up for script-placed vehicles):
# CreateVehicle2 reads it, and a player call-in SGO without it crashes the game (EDF.dll+0x52E44).
# Stock missions place the `_mission` variants. No helicopter has one, so the range makes its own
# (see DERIVED): the call-in SGO with `vehicle_setup` renamed to `mission_setup` (same layout).
# Four heli bodies: V506 Eros (and its DLC No. 6 body), VEHICLE409 Nereid, VEHICLE410 Brute, V602 Heron
# (a DSGO; class Vehicle506_Helicopter like the Eros, two nose gatlings and a missile).
VEHICLES: list[tuple[str, str]] = [
    ('edf6tr_v506_heli_mission', '直升机 506 Eros（测试场生成）'),
    ('edf6tr_v506_heli_edf6benefits_mission', '直升机 Eros No.6（测试场生成）'),
    ('edf6tr_vehicle409_heli_mission', '直升机 409 Nereid（测试场生成）'),
    ('edf6tr_vehicle410_heli_mission', '直升机 410 Brute（测试场生成）'),
    ('edf6tr_v602_heli_mission', '直升机 602 Heron（测试场生成）'),
    ('edf6tr_jet_strike_mission', '对地攻击机（插件驾驶，测试场生成）'),
    ('edf6tr_jet_fighter_mission', '制空战斗机（插件驾驶，测试场生成）'),
    ('edf6tr_jet_interceptor_mission', '截击机（远程导弹，插件驾驶，测试场生成）'),
    ('edf6tr_jet_multirole_mission', '多用途战斗机（插件驾驶，测试场生成）'),
    ('edf6tr_jet_carrier_mission', '空中航母（放攻击无人机，插件驾驶，测试场生成）'),
    ('edf6tr_jet_blast_carrier_mission', '自爆无人机母舰（插件驾驶，测试场生成）'),
    ('edf6tr_jet_doll_carrier_mission', '人偶无人机母舰（插件驾驶，测试场生成）'),
    ('edf6tr_sub_carrier_mission', '航空潜舰（插件驾驶，原尺寸 1664 米，放在最远的点；测试场生成）'),
    ('edf6tr_vehicle401_striker_mission', '装甲车 Grape 401（NPC 搭乘原版 AI；测试场生成）'),
    ('edf6tr_vehicle502_groundrobo_mission', '多足机 Depth Crawler 502（插件驾驶；测试场生成）'),
    ('vehicle403_tank_mission', '坦克 403（AutoTurret 副炮）'),
    ('vehicle404_bigtank', '大型坦克 404（AutoTurret 副炮）'),
    ('v505_tank_mission', '坦克 505'),
    ('v603_flak_mission', '高射炮车 603（AutoTurret）'),
    ('v510_maser_mission', 'EMC 510'),
    ('v605_barga_cannon_mission', '巴尔加炮 605'),
    ('v504_begaruta_mission', '机甲 Begaruta 504'),
    ('vehicle407_bigbegaruta_mission', '大型机甲 407'),
    ('v612_nix_g_mission', '机甲 Nix G 612'),
    ('v614_proteus_mk2_mission', '机甲 Proteus 614'),
    ('v608_oldrobot_g_mission', '旧型机甲 608'),
    ('v515_retrobalam_mission', '巨型机甲 Balam 515（VehicleImpact）'),
    ('v503_bike', '摩托 503'),
    ('v613_bike', '摩托 613'),
    ('v512_keitruck', '轻卡车 512'),
]

# Vehicles the range generates into Mods/OBJECT (name -> stock SGO it is made from). The DERIVED_PREFIX
# marks them as ours: install/uninstall only ever touch files with it.
DERIVED_PREFIX = 'edf6tr_'
DERIVED: dict[str, str] = {
    'edf6tr_v506_heli_mission': 'V506_HELI',
    'edf6tr_v506_heli_edf6benefits_mission': 'V506_HELI_EDF6BENEFITS',
    'edf6tr_vehicle409_heli_mission': 'VEHICLE409_HELI',
    'edf6tr_vehicle410_heli_mission': 'VEHICLE410_HELI',
    'edf6tr_v602_heli_mission': 'V602_HELI',
    'edf6tr_jet_strike_mission': 'V506_HELI',
    'edf6tr_jet_fighter_mission': 'V506_HELI',
    'edf6tr_jet_interceptor_mission': 'V506_HELI',
    'edf6tr_jet_multirole_mission': 'V506_HELI',
    'edf6tr_jet_carrier_mission': 'V506_HELI',
    'edf6tr_jet_blast_carrier_mission': 'V506_HELI',
    'edf6tr_jet_doll_carrier_mission': 'V506_HELI',
    'edf6tr_sub_carrier_mission': 'V506_HELI',
    # Ground vehicles with no stock `_mission` SGO: the call-in one, vehicle_setup renamed (same layout).
    'edf6tr_vehicle401_striker_mission': 'VEHICLE401_STRIKER',
    'edf6tr_vehicle502_groundrobo_mission': 'VEHICLE502_GROUNDROBO',
}
# Vehicles too big for a spot by the player (the 1664 m submarine): they take the farthest free points.
BIG = frozenset({'edf6tr_sub_carrier_mission'})


@dataclass(frozen=True)
class Jet:
    mark: float        # mission_setup[1][0], the speed gain k: how EDF6VehicleCrew (src/jet.cpp) tells a jet
    durability: float
    weapons: tuple[str, ...]
    # Its own model (tools/jet_models.py writes the archive, `file`, into Mods/OBJECT), or None: the bomber
    # (JET_MODEL / JET_ELEVON_MODEL). `body`: the mesh bone; `anchor`: the bone the V506 locators, weapons
    # and dead effect hang on (it replaces their names in place, so it is at most 4 characters: `body`).
    # The model's root bone is always JET_ROOT_BONE (see JET_MAB_BONES).
    model: tuple[str, str] | None = None
    file: str | None = None
    body: str = 'bomber501'
    anchor: str = 'mdl'
    rigid: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # The bone each weapon hangs on (vehicle_weapon_setting), in `weapons` order; empty: all on `anchor`.
    weapon_bones: tuple[str, ...] = ()


# Jets (src/jet.cpp, docs/jet-model-re.md): the V506 heli body (rigid body, HP, weapons, crash) with the
# BOMBER501 model, flown by the plugin. The 506 fires 0x2020 -> weapons 0 and 1, 0x2021 -> weapon 2.
# The guns are the 506's gatlings with a jet's reach (jet_guns): stock they fly 4 m a frame for 40 frames,
# 160 m, inside every role's gun pass (src/jet.cpp kKinds gunOpen 350-500 m, Fire takes the nearer of the
# two): a jet diving at 160 m/s had 0.3 s between their reach and its pull-out, and 7 of a drone's 130 gun
# chances fired on 2026-10-03 (the rest held, the nose not yet on the lead); the strike, interceptor and
# multirole jets fired none. Faster and longer lived they reach JET_GUN_REACH; damage and rate stay stock.
JET_GUN_FILES = {'EDF6VC_JET_GUN_L.SGO': 'V_506HELI_GATLING01_L.SGO', 'EDF6VC_JET_GUN_R.SGO': 'V_506HELI_GATLING01_R.SGO'}
JET_GUN_SPEED, JET_GUN_ALIVE = 10.0, 60.0   # m a frame, frames: 600 m/s, 600 m
JET_GUN_REACH = JET_GUN_SPEED * JET_GUN_ALIVE
_GUNS = tuple('app:/weapon/' + f.lower() for f in JET_GUN_FILES)
_MISSILE = 'app:/weapon/v_506heli_missile01.sgo'
_ARMS = _GUNS + (_MISSILE,)
# The blast drones' charge (src/jet.cpp Detonate: weapon 2, fired by 0x2021 once next to the enemy): the
# 409's unguided bomb (GrenadeBullet01) made a point charge (docs/decoy-blast-re.md 1.4): CP#0 = 1 bursts
# when its life runs out (0x26543E), CP#3 = 0 no bounce, CP#5 = 0 no random life; it barely moves, lives
# JET_BLAST_ALIVE frames, so it goes off where the drone is. One round, one shot. (damage, radius m).
JET_BLAST_STOCK = 'V_409HELI_BOMB01.SGO'
JET_BLAST_FILES: dict[str, tuple[float, float]] = {
    'EDF6VC_BLAST_CHARGE.SGO': (1200.0, 15.0),   # the blast drone: fast, many
    'EDF6VC_DOLL_CHARGE.SGO': (3000.0, 25.0),    # the doll drone: slow, draws the enemy in first
}
JET_BLAST_ALIVE = 2.0
_BLAST = tuple('app:/weapon/' + f.lower() for f in JET_BLAST_FILES)
JET_WEAPON_FILES = (*JET_GUN_FILES, *JET_BLAST_FILES)
# Model sizes and boxes: tools/jet_models.py (bind-pose vertices after scaling).
JETS: dict[str, Jet] = {
    'edf6tr_jet_strike_mission': Jet(7001.0, 1500.0, _ARMS),
    'edf6tr_jet_fighter_mission': Jet(7002.0, 1000.0, _ARMS),
    # bomber501_2 (dark paint) with elevons, x 0.65: 16 m across
    'edf6tr_jet_interceptor_mission': Jet(7003.0, 900.0, _ARMS, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                          'EDF6VC_INTERCEPTOR.MRAB', 'bomber501', rigid=((0.0, 0.22, 1.69), (1.3, 1.04, 8.45))),
    # bomber401 x 0.5: 26 m across
    'edf6tr_jet_multirole_mission': Jet(7004.0, 1300.0, _ARMS, ('app:/object/edf6vc_multirole.mrab', 'bomber401.mdb'),
                                        'EDF6VC_MULTIROLE.MRAB', 'bomber401', rigid=((0.0, 1.07, 0.0), (1.25, 1.0, 4.0))),
    # the EDF transport x 1.6: 59 x 77 m; it never fires (its drones do)
    'edf6tr_jet_carrier_mission': Jet(7005.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                      'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    # the same carrier sending blast / doll drones (src/jet.cpp kCarrierMarks)
    'edf6tr_jet_blast_carrier_mission': Jet(7009.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                            'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    'edf6tr_jet_doll_carrier_mission': Jet(7010.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                           'EDF6VC_CARRIER.MRAB', 'body', rigid=((0.0, 6.75, -3.11), (7.09, 6.77, 38.42))),
    # the airstrike drone x 3: 5.7 m long; only carriers launch it (tools/make_jets.py EDF6VC_JET_DRONE.SGO)
    'edf6tr_jet_drone': Jet(7006.0, 300.0, _ARMS, ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body',
                            rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    # Blast and doll drones (src/jet.cpp Role::blast / doll): the drone with a charge for its missile; only
    # the blast and doll carriers launch them (their guns never fire).
    'edf6tr_jet_blast': Jet(7007.0, 250.0, _GUNS + (_BLAST[0],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body', rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    'edf6tr_jet_doll': Jet(7008.0, 800.0, _GUNS + (_BLAST[1],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                           'EDF6VC_DRONE.MRAB', 'body', 'body', rigid=((0.0, -0.47, 1.08), (1.75, 1.04, 2.83))),
    # the submarine carrier (src/subcarrier.cpp, tools/make_sub.py, docs/subcarrier-re.md): the mission
    # object EV603_MARINE's model at its own size, 1664 m long; the box is its hull up to the main deck (the
    # tower above is not solid). Guns on its forward turrets' (left) barrels, the missile on its missile bay.
    'edf6tr_sub_carrier_mission': Jet(7101.0, 30000.0, _ARMS, ('app:/object/edf6vc_sub.mrab', 'ev603_marine.mdb'),
                                      'EDF6VC_SUB.MRAB', 'body', 'body', rigid=((0.0, 13.25, -7.58), (121.0, 179.83, 832.0)),
                                      weapon_bones=('gunA_tilt_l', 'gunB_tilt_l', 'missle_l')),
}
JET_BASE: dict[str, str] = {'edf6tr_jet_drone': 'V506_HELI', 'edf6tr_jet_blast': 'V506_HELI',
                            'edf6tr_jet_doll': 'V506_HELI'}   # jets that are no range vehicle
JET_MODEL = ['app:/object/bomber501.mrab', 'bomber501.mdb']
# The bomber with elevon bones (tools/make_jets.py writes it): the jets use it when it is installed.
JET_ELEVON_FILE = 'EDF6VC_JET.MRAB'
JET_ELEVON_MODEL = ['app:/object/edf6vc_jet.mrab', 'bomber501.mdb']
JET_ROOT_BONE = 'mdl'
# The V506 MAB block's locator parent names (UTF-16, block offsets), shortened in place to JET_ROOT_BONE:
# the bomber has only `mdl` and `bomber501` (docs/jet-model-re.md §1, §3.3).
JET_MAB_BONES = ((0x360, 'body'), (0x372, 'rotor'), (0x37E, 'tailRotor'))
# The fourth parent name, the root, stays: (0x36A, 'mdl') has no room for a longer name, so every jet model's
# root bone is JET_ROOT_BONE (tools/jet_models.py renames the drone's `pd607_Drone_airstrike`). A model
# without it leaves the riding-position locators (vehicle_riding_position) without a parent: the vehicle
# init (0x62B430, from 0x629450) then reads a null locator (EDF+0x62B619), CreateObject comes back with a
# half-made vehicle, and its first crash step reads a dead effect never set up (EDF+0x5F866C; 2026-10-03,
# a carrier's drone).
JET_MAB_ROOT = (0x36A, JET_ROOT_BONE)
# Fuselage only (half extents; the 25 m wingspan left out so low passes do not scrape), centre as the model.
JET_RIGID_BODY = [[0.0, 0.34, 2.6], [2.0, 1.6, 13.0]]


def _rebone(v, names: set[str], to: str = JET_ROOT_BONE):
    """`v` with every string in `names` replaced by `to` (deep)."""
    if isinstance(v, list):
        return [_rebone(c, names, to) for c in v]
    return to if isinstance(v, str) and v in names else v


JET_BODY_BONE = 'bomber501'


def _jet_ragdoll(blob: bytes, body: str = JET_BODY_BONE) -> bytes:
    """The ragdoll's embedded binding SGO with every model-side bone one the bomber has.
    RagdollController::BindDependency (0x6E6A50): each animation_from_ragdoll entry looks its model bone
    up (0x6E7B98); found, the proxy's record gets the bone (+0x60, first entry wins) and the bone gets the
    proxy (+8, last entry wins). Every proxy must end up with a bone: the loop at 0x6E8280 reads each
    record's +0x60 unchecked (crashed 2026-10-03 with the V506 bone names, then with only the body proxy
    bound). So every proxy is bound to the fuselage bone, the body proxy last so it is what drives it
    (the rotor proxies spin); ragdoll_from_animation has them all follow it."""
    import sgowrite
    version, inner = sgowrite.read(blob)
    inner['ragdoll_from_animation'] = [[[body, e[0][1]]] + e[1:] for e in inner['ragdoll_from_animation']]
    drive: dict[str, list] = {}
    for e in inner['animation_from_ragdoll']:
        drive.setdefault(e[0][0], [[e[0][0], body]] + e[1:])   # globalSRT: a second body entry, dropped
    body = drive.pop('RagDollProxys.body')
    inner['animation_from_ragdoll'] = list(drive.values()) + [body]
    return sgowrite.write(version, inner)


def jet_sgo(game: Game, name: str, model: list[str] | None = None, body: str = JET_BODY_BONE,
            rigid: list[list[float]] | None = None) -> bytes:
    """`model`: the model archive and file (default JET_MODEL, the stock bomber); `body`: its mesh bone, which
    the root and the ragdoll drive; `rigid`: the collision box [centre, half extents] (default JET_RIGID_BODY).
    A jet with its own model (Jet.model) always flies it: these three come from the Jet then."""
    import sgowrite
    jet = JETS[name]
    root = anchor = JET_ROOT_BONE   # root: see JET_MAB_ROOT
    if jet.model is not None:
        model, body, rigid = list(jet.model), jet.body, [list(x) for x in jet.rigid] if jet.rigid else None
        anchor = jet.anchor
    version, m = sgowrite.read(game.read('OBJECT', (DERIVED.get(name) or JET_BASE[name]) + '.SGO'))
    at, want = JET_MAB_ROOT
    if m['animation_model'][2][at:at + 2 * len(want) + 2] != want.encode('utf-16le') + b'\0\0':
        raise ValueError(f'V506 MAB 的根骨骼名不在 {at:#x}')
    if 'vehicle_setup' not in m or 'mission_setup' in m:
        raise ValueError('V506_HELI 没有 vehicle_setup')
    setup = m.pop('vehicle_setup')
    setup[1][0] = jet.mark
    stock = {w[0]: w for w in setup[3]}   # each weapon keeps its stock per-weapon parameters
    setup[3] = [stock.get(w, [w, [0.0001, 0.1]]) for w in jet.weapons] + [stock['app:/weapon/v_fuel01.sgo']]
    m['mission_setup'] = setup
    m['game_object_durability'] = jet.durability
    model_ref = model
    model = m['animation_model']
    mab = model[2]
    for at, old in JET_MAB_BONES:
        mab = sgowrite.replace_utf16(mab, at, old, anchor)
    m['animation_model'] = [list(JET_MODEL if model_ref is None else model_ref), model[1], mab]
    m['animation_model_bone_mapping'] = [root, body]
    bones = {'body', 'rotor', 'tailRotor'}
    m['vehicle_weapon_setting'] = [[b, 0] for b in (jet.weapon_bones or (anchor,) * len(jet.weapons))] + [[anchor, -1]]
    m['vehicle_dead_effect'] = _rebone(m['vehicle_dead_effect'], bones, anchor)
    m['roter_contact_damage_scale'] = 0.0
    m['heli_contact_damage_scale'] = 0.0005
    rb = m['heli_rigid_body']
    box = JET_RIGID_BODY if rigid is None else rigid
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0], _jet_ragdoll(rag[1], body)]
    return sgowrite.write(version, m)

# (sgo, label, flying)
ENEMIES: list[tuple[str, str, bool]] = [
    ('giantant01', '巨蚁', False),
    ('e650_giantant01', '巨蚁（EDF6）', False),
    ('giantspider01', '巨蜘蛛', False),
    ('giantantqueen', '母体（女王巨蚁）', False),
    ('e651_spider01_light', '蜘蛛（轻）', False),
    ('e503_frog_af_leader', '青蛙兵', False),
    ('e503_armorfrog_af', '装甲青蛙', False),
    ('e601_martian_gs', '火星人', False),
    ('giantbee01charge', '巨蜂（飞）', True),
    ('e507_goldufo', '金色 UFO（飞）', True),
    ('e515_imperialufo', '帝国 UFO（飞）', True),
    ('dragonsmall401', '小龙（飞）', True),
    ('shootingtarget', '训练靶子（地面 + 空中，不动）', False),
]
# The targets (enemy TARGET): groups of per_wave on the target spots (target_spots), every other spot raised
# TARGET_AIR m (the written MISSION.RMPA, rmpa.raised) for targets in the air, the jets' fighters' prey. One
# target a raised spot stayed up there on 2026-10-03 (EDF6VehicleCrew.log: its jets' targets marked (air)).
TARGET = 'shootingtarget'
TARGET_AIR = 80.0


@dataclass
class Waves:
    enabled: bool = True
    enemy: str = 'giantant01'
    per_wave: int = 8
    max_alive: int = 12      # a new wave comes once fewer than this many enemies live
    first_delay: float = 30.0  # seconds before the first wave (time to try the vehicles)
    interval: float = 20.0
    level: float = 1.0


@dataclass
class Plan:
    vehicles: dict[str, int] = field(default_factory=lambda: {
        'vehicle403_tank_mission': 1, 'v603_flak_mission': 1, 'v504_begaruta_mission': 1})
    vehicle_level: float = 1.0
    waves: Waves = field(default_factory=Waves)
    loadout: dict = field(default_factory=dict)
    slot: str = DEFAULT_SLOT
    # Vehicles spawned with an NPC driver (CreateFriend, as stock missions spawn allied vehicles). An
    # NPC-piloted heli is flown by the plugin, several of them in formation.
    friends: dict[str, int] = field(default_factory=dict)


def placements(plan: Plan) -> list[tuple[str, bool]]:
    """(sgo, NPC-driven) for every vehicle to place, empty ones first, the BIG ones last."""
    chosen = ([(s, False) for s, n in plan.vehicles.items() for _ in range(max(0, n))] +
              [(s, True) for s, n in plan.friends.items() for _ in range(max(0, n))])
    return [c for c in chosen if c[0] not in BIG] + [c for c in chosen if c[0] in BIG]


def small_count(plan: Plan) -> int:
    return sum(1 for s, _ in placements(plan) if s not in BIG)


def save_plan(path: str, plan: Plan) -> None:
    data = {'vehicles': plan.vehicles, 'vehicle_level': plan.vehicle_level,
            'waves': plan.waves.__dict__, 'loadout': plan.loadout, 'slot': plan.slot,
            'friends': plan.friends}
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def load_plan(path: str) -> Plan:
    try:
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
    except (OSError, ValueError):
        return Plan()
    plan = Plan()
    known = {sgo for sgo, _ in VEHICLES}
    plan.vehicles = {k: int(v) for k, v in data.get('vehicles', plan.vehicles).items() if k in known}
    plan.friends = {k: int(v) for k, v in data.get('friends', {}).items() if k in known}
    plan.vehicle_level = float(data.get('vehicle_level', plan.vehicle_level))
    plan.waves = Waves(**{k: v for k, v in data.get('waves', {}).items() if k in Waves.__dataclass_fields__})
    plan.loadout = data.get('loadout', {})
    if data.get('slot') in {x.mission for x in SLOTS}:
        plan.slot = data['slot']
    return plan


class Game:
    """Read-only view of the game's Root.cpk."""

    def __init__(self, root: str) -> None:
        self.root = root
        self.cpk = cpk.Cpk(os.path.join(root, 'Root.cpk'))

    def read(self, folder: str, name: str) -> bytes:
        for (d, n), e in self.cpk.index.items():
            if d.upper() == folder.upper() and n.upper() == name.upper():
                with open(self.cpk.path, 'rb') as h:
                    h.seek(self.cpk.base + int(e['FileOffset']))
                    data = h.read(int(e['FileSize']))
                return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data
        raise KeyError(f'{folder}/{name}')


@dataclass
class Layout:
    player: rmpa.Point
    vehicle_points: list[rmpa.Point]
    enemy_points: list[rmpa.Point]
    far_points: list[rmpa.Point] = field(default_factory=list)   # free points past the enemy ring


# How far out vehicle spots are taken, ring by ring, until there are as many as wanted: the plain has 12
# within 160 m, 21 within 300 m, 39 within 450 m and 44 within 800 m.
SPOT_RINGS = (160.0, 300.0, 450.0, 800.0)


def layout(points: list[rmpa.Point], need: int = 0) -> Layout:
    """Vehicle spots: flat points from 30 m off the player start, 15 m apart, nearest first, out to
    160 m or as far out (SPOT_RINGS) as `need` of them takes; enemy spots 180-450 m, those a vehicle
    takes left out (all of them when that leaves none)."""
    player = next(p for p in points if p.name == 'プレイヤー')
    by_distance = sorted(points, key=lambda p: math.dist(p.pos, player.pos))
    spots: list[rmpa.Point] = []
    for far in SPOT_RINGS:
        for p in by_distance:
            d = math.dist(p.pos, player.pos)
            if (30 <= d <= far and abs(p.pos[1] - player.pos[1]) < 4 and p not in spots
                    and all(math.dist(p.pos, s.pos) >= 15 for s in spots)):
                spots.append(p)
        if len(spots) >= need:
            break
    taken = {p.name for p in spots[:need]}
    ring = [p for p in by_distance if 180 <= math.dist(p.pos, player.pos) <= 450]
    enemies = [p for p in ring if p.name not in taken] or ring
    far = [p for p in by_distance if math.dist(p.pos, player.pos) > 450 and p.name not in taken]
    return Layout(player, spots, enemies, far)


def spots_for(plan: Plan, lay: Layout) -> list[tuple[str, bool, rmpa.Point]]:
    """Each placement's point: the small ones the vehicle spots, nearest first; the BIG ones the farthest
    free points (taken out of the target spots)."""
    chosen = placements(plan)
    small = [c for c in chosen if c[0] not in BIG]
    big = [c for c in chosen if c[0] in BIG]
    if len(small) > len(lay.vehicle_points):
        raise ValueError(f'载具太多：这张地图玩家附近只有 {len(lay.vehicle_points)} 个空位')
    if len(big) > len(lay.far_points):
        raise ValueError(f'大型载具太多：这张地图远处只有 {len(lay.far_points)} 个空位')
    far = lay.far_points[::-1][:len(big)]
    lay.far_points = [p for p in lay.far_points if p not in far]
    return ([(s, npc, p) for (s, npc), p in zip(small, lay.vehicle_points)] +
            [(s, npc, p) for (s, npc), p in zip(big, far)])


def target_spots(lay: Layout) -> list[rmpa.Point]:
    """Where targets (TARGET) stand: the enemy spots and the free points past them (the plain has 48 points,
    the vehicles take most: 7 enemy spots were too few)."""
    return lay.enemy_points + lay.far_points


def air_targets(lay: Layout) -> list[rmpa.Point]:
    """The target spots raised for targets in the air (TARGET): every other one."""
    return target_spots(lay)[1::2]


def _q(text: str) -> str:
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def script(plan: Plan, lay: Layout) -> str:
    """The mission script. Same skeleton as the stock generated scripts (event 0 = Main)."""
    chosen = placements(plan)
    placed = spots_for(plan, lay)
    w = plan.waves
    targets = w.enemy == TARGET
    flying = next((f for s, _, f in ENEMIES if s == w.enemy), False)
    preload = sorted({f'app:/object/{s}.sgo' for s, _ in chosen} | ({f'app:/object/{w.enemy}.sgo'} if w.enabled else set()))
    lines = [
        '//',
        '// EDF6 test range, generated by testrange/gen.py (EDF6VehicleCrew). Delete this folder to restore mission 1.',
        '//',
        '',
        '#include "app:/Mission/AsCommon.h"',
        '',
        'Talkers g_soldiers;',
        '',
        'class __EventData {',
        '\tuint m_counter  = 0;',
        '\tbool m_is_break = false;',
        '\tbool m_is_pass = false;',
        '\tinternal_SyncCounter m_game_thread_sync;',
        '\tinternal_SyncCounter m_voice_thread_sync;',
        '\tMutex m_mutex;',
        '};',
        '',
        '__EventData __0000_data;',
        '',
        'const uint __END_SYNC = 0x7fffffff;',
        '',
        'void Main()',
        '{',
        '\tif( IsBoolState(0,::__0000_data.m_is_break,::__0000_data.m_is_break) ) return;',
        '\t::__0000_data.m_counter++;',
        '\tconst uint32 MAX_EVENT_ID = 1;',
        '\tinternal_Initialize(MAX_EVENT_ID);',
        '\tinternal_GetEventState(0).SetName("開始");',
        '\tMutex_LockObject@ __internal_thread_lock = ::__0000_data.m_mutex.Lock();',
        '\tinternal_InitEventThread(0, ::__0000_data.m_counter, "Main", "開始");',
        '',
        '\tBeginLoading();',
        f'\tPreloadMap({_q(MAP)}, {_q(WEATHER)}, -1);',
        '\tPreload("app:/ui/UiResourceGroup_InMission.sgo", -1);',
        '\tPreload("app:/ui/UiResourceGroup_MissionCleared.sgo", -1);',
        '\tPreload("app:/ui/UiResourceGroup_MissionFailed.sgo", -1);',
        *[f'\tPreload({_q(p)}, -1);' for p in preload],
        '\tPreloadPlayerResource();',
        '\tWaitPreload();',
        '\tOnline_WaitStart();',
        '\tEndLoading();',
        '',
        '\tWaitVoice();',
        '\tg_soldiers = GetTalkers();',
        '\t::__0000_data.m_game_thread_sync.Reset();',
        '\t::__0000_data.m_voice_thread_sync.Reset();',
        '\t::internal_CreateThread("__0000_voice_event");',
        '',
        '\tMain_usercode();',
        '',
        '\t::__0000_data.m_game_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_voice_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
        'void Main_usercode()',
        '{',
        f'\tMap({_q(MAP)}, {_q(WEATHER)});',
        f'\tCreatePlayer({_q(lay.player.name)});',
    ]
    for sgo, npc, point in placed:
        path = _q('app:/object/' + sgo + '.sgo')
        if npc:   # last argument: does it join the player's squad (no: the plugin flies/drives it)
            lines.append(f'\tCreateFriend({_q(point.name)}, {path}, {plan.vehicle_level:.2f}, false);')
        else:
            lines.append(f'\tCreateVehicle2({_q(point.name)}, {path}, {plan.vehicle_level:.2f});')
    if w.enabled and target_spots(lay) and targets:
        spots = target_spots(lay)
        per = max(1, int(w.per_wave))
        pts = ', '.join(_q(p.name) for p in spots)
        lines += [
            '',
            '\t// Targets, per_wave to a spot within 30 m, until max_alive stand (at most one group a spot;',
            '\t// every other spot is up in the air).',
            f'\tarray<string> spots = {{ {pts} }};',
            '\tuint next = 0;',
            f'\tWait({w.first_delay:.1f});',
            '\twhile( true ) {',
            f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) + {per} <= {min(int(w.max_alive), len(spots) * per)} ) {{',
            f'\t\t\tCreateEnemyGroup(spots[next % spots.length()], 30, {_q("app:/object/" + w.enemy + ".sgo")}, {per}, {w.level:.2f}, true);',
            '\t\t\tnext++;',
            '\t\t}',
            '\t\tWait(1.0);',
            '\t}',
        ]
    elif w.enabled and lay.enemy_points:
        spawn = 'CreateEnemyGroup'
        pts = ', '.join(_q(p.name) for p in lay.enemy_points)
        lines += [
            '',
            '\t// Enemy waves: once fewer than max_alive enemies live, the next spot gets a new group.',
            f'\tarray<string> spots = {{ {pts} }};',
            '\tuint next = 0;',
            f'\tWait({w.first_delay:.1f});',
            '\twhile( true ) {',
            f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) < {int(w.max_alive)} ) {{',
            f'\t\t\t{spawn}(spots[next % spots.length()], 20, {_q("app:/object/" + w.enemy + ".sgo")}, {int(w.per_wave)}, {w.level:.2f}, true);',
            '\t\t\tnext++;',
            f'\t\t\tWait({w.interval:.1f});',
            '\t\t}',
            '\t\tWait(1.0);',
            '\t}',
        ]
        if flying:
            lines.insert(lines.index('\twhile( true ) {'), '\t// flying enemies are spawned at ground points too; they take off on their own')
    lines += [
        '\t// The mission must never return on its own (stock scripts end the same way); leave from the pause menu.',
        '\twhile( true ) sys_Yield();',
        '}',
        '',
        'void __0000_voice_event()',
        '{',
        '\t::__0000_data.m_voice_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_game_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
    ]
    return '\n'.join(lines)


def as_mission_sgo(data: bytes) -> bytes:
    """A call-in vehicle SGO turned into a script-placeable one: its `vehicle_setup` name becomes
    `mission_setup` (same length, same value layout) and the name table is re-sorted. Little-endian SGO:
    header {count, data offset, name count, name table offset} at 8, names {string offset from the entry,
    member index}. DSGO: see _sort_dsgo_names."""
    old, new = 'vehicle_setup'.encode('utf-16le') + b'\0\0', 'mission_setup'.encode('utf-16le') + b'\0\0'
    if data[:4] not in (b'SGO\0', b'DSGO'):
        raise ValueError('不是小端 SGO / DSGO')
    if data.count(old) != 1 or new in data:
        raise ValueError('vehicle_setup 不唯一或已有 mission_setup')
    buf = bytearray(data.replace(old, new))
    if data[:4] == b'DSGO':
        _sort_dsgo_names(buf)
        return bytes(buf)
    _, _, name_count, name_off = struct.unpack_from('<4I', buf, 8)
    entries = []
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from('<iI', buf, p)
        entries.append((_utf16_at(buf, p + rel), p + rel, idx))
    for i, (_, at, idx) in enumerate(sorted(entries)):
        p = name_off + i * 8
        struct.pack_into('<iI', buf, p, at - p, idx)
    return _without_ai_obstacle(bytes(buf))


def _without_ai_obstacle(data: bytes) -> bytes:
    """`data` without its `ai_obstacle` member. A script-placed vehicle is AI-driven, and only then does the
    game (EDF+62C990, under EDF+6747F2's flag test) look each ai_obstacle name up in the vehicle's collision
    bodies (+0xE40) and read the result without a null check. The call-in Grape 401 lists one the placed
    vehicle does not have (2026-10-04: EXCEPTION at EDF+62CB9C, rbx=5 entries, r15=4); the stock game never
    places a 401 from a script, so it never hit it. Without the member the loop is skipped (as for the 502,
    which has none). SGOs without it come back unchanged."""
    import sgowrite
    version, m = sgowrite.read(data)
    if 'ai_obstacle' not in m:
        return data
    del m['ai_obstacle']
    return sgowrite.write(version, m)


def _sort_dsgo_names(buf: bytearray) -> None:
    """Re-sort the top-level dictionary's name table of a DSGO (see lib/sgo.py): node 0 at the node
    table is that dictionary, {name table offset, name count, ...} at node + value; names are {string
    offset from the entry, member position}, kept sorted like the stock files."""
    table = struct.unpack_from('<I', buf, 4)[0]
    raw, typ = struct.unpack_from('<QI', buf, table)
    if typ != 3:
        raise ValueError('DSGO 顶层不是字典')
    d = table + raw
    name_off, name_count = struct.unpack_from('<II', buf, d)
    entries = []
    for k in range(name_count):
        e = d + name_off + k * 8
        so, member = struct.unpack_from('<II', buf, e)
        entries.append((_utf16_at(buf, e + so), e + so, member))
    for k, (_, at, member) in enumerate(sorted(entries)):
        e = d + name_off + k * 8
        struct.pack_into('<II', buf, e, at - e, member)


def _utf16_at(buf: bytes, off: int) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def vehicle_sgo(game: Game, sgo_name: str, jet_model: list[str] | None = None) -> bytes:
    """The SGO bytes the mission will load for this vehicle (generated ones are made here)."""
    if sgo_name in JETS:
        return jet_sgo(game, sgo_name, jet_model)
    stock = DERIVED.get(sgo_name)
    if stock:
        return as_mission_sgo(game.read('OBJECT', stock + '.SGO'))
    return game.read('OBJECT', sgo_name.upper() + '.SGO')


def has_mission_setup(game: Game, sgo_name: str) -> bool:
    import sgo
    try:
        values = sgo.load(data=vehicle_sgo(game, sgo_name))
    except (KeyError, ValueError):
        return False
    return isinstance(values, dict) and 'mission_setup' in values


def object_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'OBJECT')


def weapon_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'WEAPON')


def jet_guns(game: Game) -> dict[str, bytes]:
    """The jets' guns (JET_GUN_FILES): the stock gatling with JET_GUN_SPEED and JET_GUN_ALIVE; and the
    blast drones' charges (JET_BLAST_FILES)."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'autoturret', 'tools'))
    import dsgo
    out = {}
    for name, stock in JET_GUN_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', stock))
        r = doc.root
        if r.get('AmmoClass') != 'SolidBullet01' or r.get('AmmoSpeed') * r.get('AmmoAlive') >= JET_GUN_REACH:
            raise ValueError(f'{stock} 不是预期的直升机机炮')
        r.set('AmmoSpeed', JET_GUN_SPEED)
        r.set('AmmoAlive', JET_GUN_ALIVE)
        out[name] = dsgo.write(doc)
    for name, (damage, radius) in JET_BLAST_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', JET_BLAST_STOCK))
        r = doc.root
        cp = r.get('Ammo_CustomParameter')
        if r.get('AmmoClass') != 'GrenadeBullet01' or len(cp.items) != 6:
            raise ValueError(f'{JET_BLAST_STOCK} 不是预期的直升机炸弹')
        cp.items[0], cp.items[3], cp.items[5] = 1.0, 0.0, 0.0
        for key, value in (('AmmoCount', 1.0), ('FireCount', 1.0), ('FireBurstCount', 1.0), ('FireInterval', 1.0),
                           ('AmmoSpeed', 0.01), ('AmmoGravityFactor', 0.0), ('AmmoAlive', JET_BLAST_ALIVE),
                           ('AmmoDamage', damage), ('AmmoExplosion', radius)):
            r.set(key, value)
        out[name] = dsgo.write(doc)
    return out


def write_jet_guns(game_root: str, game: Game) -> list[str]:
    """Writes the jets' guns into Mods/WEAPON (only these EDF6VC_ files); returns the paths."""
    os.makedirs(weapon_dir(game_root), exist_ok=True)
    paths = []
    for name, data in jet_guns(game).items():
        path = os.path.join(weapon_dir(game_root), name)
        with open(path, 'wb') as f:
            f.write(data)
        paths.append(path)
    return paths


# The teleportation ships' portal laser (src/carrierlaser.cpp): two DemoIndirectFire objects (the class of
# the missions' DEMOSATELLITELASER*: an IndirectFireControl at +0x170 that fires indirect_fire_param's
# rounds at its own position, docs/carrier-laser-re.md), made from DEMOSATELLITELASER18.SGO. The plugin
# fires them from the ship's hatch (IFC +0x2F9 / +0x300) at its target and sets their damage itself.
# indirect_fire_param (index: meaning, from the IFC's parser 0x2B5F40): 2 rounds, 3 frames between rounds,
# 4 bullet class, 5 speed (m a frame), 7 beam size, 9 hit impulse, 10 life (frames), 11 penetrates,
# 12 colour, 14 explosion, 15 frames before the first round, 16 fire sound looped, 17 fire sound, 18 hit sound.
PORTAL_LASER_STOCK = 'DEMOSATELLITELASER18.SGO'
# name -> (rounds, gap, size, life, colour, fire sound once)
PORTAL_LASER_FILES: dict[str, tuple[int, int, float, int, tuple[float, float, float, float], bool]] = {
    # The aim light: a thin red beam, a round every frame living 6 (so it follows the aim), 12.5 s of rounds at
    # most (the charge is 12 s, src/carrierlaser.cpp kChargeMs; the plugin ends it sooner), no damage (the
    # plugin sets 0).
    'EDF6VC_PORTAL_SIGHT.SGO': (750, 0, 1.5, 6, (3.0, 0.15, 0.1, 1.0), True),
    # The main shot: one wide violet beam living 45 frames (0.75 s); its damage is CarrierLaserDamage.
    'EDF6VC_PORTAL_LASER.SGO': (1, 0, 8.0, 45, (2.5, 0.4, 3.0, 1.0), False),
}


def portal_lasers(game: Game) -> dict[str, bytes]:
    """The portal laser's two DemoIndirectFire SGOs (PORTAL_LASER_FILES)."""
    import sgowrite
    out = {}
    for name, (rounds, gap, size, life, colour, once) in PORTAL_LASER_FILES.items():
        version, m = sgowrite.read(game.read('OBJECT', PORTAL_LASER_STOCK))
        p = m['indirect_fire_param']
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or p[4] != 'LaserBullet02'):
            raise ValueError(f'{PORTAL_LASER_STOCK} 不是预期的卫星激光')
        p[2], p[3], p[7], p[9], p[10] = rounds, gap, size, 0.0, life
        p[12] = list(colour)
        p[14], p[15], p[16] = 0, 0, 0
        if isinstance(p[17], list) and p[17]:
            p[17][0] = 1.0 if once else 0   # 1: the fire sound once for all rounds (the player's satellite)
        m['indirect_fire_damage'] = 0.0     # the plugin sets the damage (IFC +0xDC)
        out[name] = sgowrite.write(version, m)
    return out


def write_portal_lasers(game_root: str, game: Game) -> list[str]:
    """Writes the portal laser's SGOs into Mods/OBJECT (only these EDF6VC_ files); returns the paths."""
    os.makedirs(object_dir(game_root), exist_ok=True)
    paths = []
    for name, data in portal_lasers(game).items():
        path = os.path.join(object_dir(game_root), name)
        with open(path, 'wb') as f:
            f.write(data)
        paths.append(path)
    return paths


def _write_derived(game_root: str, game: Game, wanted: set[str]) -> None:
    """Makes Mods/OBJECT hold exactly the generated vehicles in `wanted` (files with our prefix only)."""
    _remove_derived(game_root, keep=wanted)
    jet_models(game_root, game, {JETS[n].file for n in wanted if n in JETS and JETS[n].file})
    if wanted & JETS.keys():
        write_jet_guns(game_root, game)
    elevons = os.path.isfile(os.path.join(object_dir(game_root), JET_ELEVON_FILE))
    for name in sorted(wanted):
        os.makedirs(object_dir(game_root), exist_ok=True)
        with open(os.path.join(object_dir(game_root), name.upper() + '.SGO'), 'wb') as f:
            f.write(vehicle_sgo(game, name, JET_ELEVON_MODEL if elevons else None))


def jet_models(game_root: str, game: Game, files: set[str]) -> None:
    """Writes the jet model archives in `files` (tools/jet_models.py, EDF6VC_ files of tools/make_jets.py)
    that Mods/OBJECT does not have yet."""
    missing = {f for f in files if not os.path.isfile(os.path.join(object_dir(game_root), f))}
    if not missing:
        return
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
    import jet_models
    os.makedirs(object_dir(game_root), exist_ok=True)
    models = {f: r for f, r in {**jet_models.MODELS, **jet_models.SUB_MODELS}.items() if f in missing}
    for f, data in jet_models.build(game, models).items():
        if f in missing:
            with open(os.path.join(object_dir(game_root), f), 'wb') as out:
                out.write(data)


def _remove_derived(game_root: str, keep: set[str] = frozenset()) -> bool:
    d = object_dir(game_root)
    if not os.path.isdir(d):
        return False
    gone = [f for f in os.listdir(d) if f.lower().startswith(DERIVED_PREFIX) and f.lower()[:-4] not in keep]
    for f in gone:
        os.remove(os.path.join(d, f))
    if not os.listdir(d):
        os.rmdir(d)
    return bool(gone)


def mission_dir(game_root: str, mission: str) -> str:
    return os.path.join(game_root, 'Mods', 'MISSION', 'EDF6', mission)


def ours(game_root: str, mission: str) -> bool:
    return os.path.isfile(os.path.join(mission_dir(game_root, mission), MARKER))


def installed(game_root: str) -> Slot | None:
    """The slot the range is installed in now, if any."""
    return next((x for x in SLOTS if ours(game_root, x.mission)), None)


def install(game_root: str, plan: Plan) -> list[str]:
    """Writes the range over plan.slot (and removes it from the other slot). Refuses to touch a
    folder another mod put there."""
    out = mission_dir(game_root, plan.slot)
    if os.path.isdir(out) and os.listdir(out) and not ours(game_root, plan.slot):
        raise RuntimeError(f'{out} 已有别的 mod 的文件，不覆盖。请先手动处理。')
    game = Game(game_root)
    for sgo_name in {s for s, _ in placements(plan)}:
        if not has_mission_setup(game, sgo_name):
            raise RuntimeError(f'{sgo_name} 没有 mission_setup，不能由脚本放置（会让游戏崩溃）')
    points_file = game.read(f'MISSION/EDF6/{SOURCE}', 'MISSION.RMPA')
    lay = layout(rmpa.points(points_file), small_count(plan))
    text = script(plan, lay)
    placed = [(s, npc, p) for s, npc, p in spots_for(plan, layout(rmpa.points(points_file), small_count(plan)))]
    if plan.waves.enabled and plan.waves.enemy == TARGET:
        points_file = rmpa.raised(points_file, {p.name for p in air_targets(lay)}, TARGET_AIR)
    os.makedirs(out, exist_ok=True)
    _write_derived(game_root, game, {s for s, _ in placements(plan) if s in DERIVED})
    with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
        f.write(b'\xef\xbb\xbf' + text.encode('utf-8'))
    with open(os.path.join(out, 'MISSION.RMPA'), 'wb') as f:
        f.write(points_file)
    with open(os.path.join(out, MARKER), 'w', encoding='utf-8') as f:
        f.write('EDF6 测试场（EDF6VehicleCrew/testrange）。删除本目录即恢复这一关。\n')
    for other in SLOTS:
        if other.mission != plan.slot:
            _remove(game_root, other.mission)
    return [f'{p.name}: {s}{"（NPC 驾驶）" if npc else ""}' for s, npc, p in placed]


def uninstall(game_root: str) -> bool:
    return any([_remove(game_root, x.mission) for x in SLOTS] + [_remove_derived(game_root)])


def _remove(game_root: str, mission: str) -> bool:
    out = mission_dir(game_root, mission)
    if not ours(game_root, mission):
        return False
    shutil.rmtree(out)
    parent = os.path.dirname(out)
    for d in (parent, os.path.dirname(parent)):   # Mods/MISSION/EDF6, Mods/MISSION if now empty
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    return True
