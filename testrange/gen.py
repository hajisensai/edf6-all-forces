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
import sys
from dataclasses import dataclass, field, replace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))   # the ground vehicles' builders (GROUND_MISSION)
import jet_models  # noqa: E402
import ledger  # noqa: E402
import recoil  # noqa: E402
import rmpa  # noqa: E402
import sazabi_model  # noqa: E402
# The generated jets and vehicles are shared with tools/make_jets.py and tools/make_sub.py (pylib/vcobjects.py).
from vcobjects import (DEFAULT_GAME, SAZABI_JET, JET_ELEVON_FILE, JET_ELEVON_MODEL, JETS, PARKED_KINDS, Game,  # noqa: E402,F401
                       as_mission_sgo, jet_guns, jet_sgo, object_dir, parked_name, weapon_dir)

OWNER = 'testrange'   # pylib/ledger.py: the files the range writes or uses
MARKER = 'EDF6TestRange.txt'


@dataclass(frozen=True)
class Slot:
    mission: str
    item: int          # position in the offline mission list (1-based)
    label: str


# Slots, by their position in the in-game offline mission list: item N is entry N-1 of
# MISSION/MISSIONLIST.OFFLINE.LIST.SGO, and its title is entry N-1 of MISSIONLIST.OFFLINE.TXT.*.SGO
# (item 14 「转机」 = RM015, item 2 「非法入侵者」 = M001). The range only borrows the slot: its map and
# points are the site's (SITES). The opening missions are the ruined world, where the Air Raider's
# vehicle and air support requests are accepted but never arrive; in item 14 they do.
SLOTS = [
    Slot('RM015', 14, '列表第 14 项「转机」（RM015）：空袭兵能呼叫载具和空中支援'),
    Slot('M001', 2, '列表第 2 项「非法入侵者」（M001）：新存档也能进，但前期剧情叫不来载具和空中支援'),
]
DEFAULT_SLOT = SLOTS[0].mission


def slot_of(mission: str) -> Slot:
    return next(x for x in SLOTS if x.mission == mission)


@dataclass(frozen=True)
class Site:
    source: str        # stock mission whose map, weather and points (MISSION.RMPA) the range uses
    map: str
    weather: str
    label: str


# Only missions with a 'プレイヤー' point and room for vehicles around it: of the city missions RM016B
# (TrainCity) has the most, 11 flat spots within 800 m and 11 enemy spots.
SITES = [
    Site('M045', 'app:/Map/ig_Heigen601.mac', 'cloudy', '平原（M045）'),
    Site('RM016B', 'app:/Map/nw_TrainCity.mac', 'finecloud', '城区（RM016B 列车城）'),
]
DEFAULT_SITE = SITES[0].source


def site_of(source: str) -> Site:
    return next(x for x in SITES if x.source == source)

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
    ('edf6tr_jet_enemy_fighter_mission', '敌方战斗机（插件驾驶，敌方阵营：攻击你和友军飞机，测试场生成）'),
    ('edf6tr_jet_primer_fighter_mission', '星导者扑翼战斗机（插件驾驶，敌方阵营，测试场生成）'),
    ('edf6tr_jet_multirole_mission', '多用途战斗机（插件驾驶，测试场生成）'),
    ('edf6tr_jet_carrier_mission', '空中航母（放攻击无人机，插件驾驶，测试场生成）'),
    ('edf6tr_jet_blast_carrier_mission', '自爆无人机母舰（插件驾驶，测试场生成）'),
    ('edf6tr_jet_doll_carrier_mission', '人偶无人机母舰（插件驾驶，测试场生成）'),
    ('edf6tr_sub_carrier_mission', '航空潜舰（插件驾驶，原尺寸 1664 米，放在最远的点；测试场生成）'),
    ('edf6tr_pjet_fighter_mission', '玩家战斗机（自己驾驶，空着停放；测试场生成）'),
    ('edf6tr_pjet_strike_mission', '玩家攻击机（自己驾驶，空着停放；测试场生成）'),
    (SAZABI_JET, '沙扎比 MSN-04（自己驾驶，空着停放；要有沙扎比模型目录）'),
    # The NPC kinds parked for the player (BOARDABLE_PARKED): each one's parked twin (vcobjects Jet.parked: the whole
    # plane is solid, its boarding point at its side on the ground, every class may fly it).
    (parked_name('edf6tr_jet_fighter_mission'), '制空战斗机·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_interceptor_mission'), '截击机·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_strike_mission'), '对地攻击机·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_multirole_mission'), '多用途战斗机·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_carrier_mission'), '空中航母·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_blast_carrier_mission'), '自爆无人机母舰·停放（自己驾驶；测试场生成）'),
    (parked_name('edf6tr_jet_doll_carrier_mission'), '人偶无人机母舰·停放（自己驾驶；测试场生成）'),
    ('edf6tr_vehicle401_striker_mission', '装甲车 Grape 401（NPC 搭乘原版 AI；测试场生成）'),
    ('edf6tr_katyusha_mission', '喀秋莎火箭炮车（自己驾驶；测试场生成）'),
    ('edf6tr_artillery_mission', '自行榴弹炮（自己驾驶；测试场生成）'),
    ('edf6tr_drill_mission', '钻头战车（自己驾驶；测试场生成）'),
    ('edf6tr_sidecar_mission', '边三轮摩托（自己驾驶或坐边车；测试场生成）'),
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
    'edf6tr_katyusha_mission': 'EDF6VC_KATYUSHA',     # GROUND_MISSION: made from our own SGO, not a stock one
    'edf6tr_artillery_mission': 'EDF6VC_ARTILLERY',
    'edf6tr_drill_mission': 'EDF6VC_DRILL',
    'edf6tr_sidecar_mission': 'EDF6VC_SIDECAR',
    'edf6tr_v506_heli_mission': 'V506_HELI',
    'edf6tr_v506_heli_edf6benefits_mission': 'V506_HELI_EDF6BENEFITS',
    'edf6tr_vehicle409_heli_mission': 'VEHICLE409_HELI',
    'edf6tr_vehicle410_heli_mission': 'VEHICLE410_HELI',
    'edf6tr_v602_heli_mission': 'V602_HELI',
    'edf6tr_jet_strike_mission': 'V506_HELI',
    'edf6tr_jet_fighter_mission': 'V506_HELI',
    'edf6tr_jet_interceptor_mission': 'V506_HELI',
    'edf6tr_jet_enemy_fighter_mission': 'V506_HELI',
    'edf6tr_jet_primer_fighter_mission': 'V506_HELI',
    'edf6tr_jet_multirole_mission': 'V506_HELI',
    'edf6tr_jet_carrier_mission': 'V506_HELI',
    'edf6tr_jet_blast_carrier_mission': 'V506_HELI',
    'edf6tr_jet_doll_carrier_mission': 'V506_HELI',
    'edf6tr_sub_carrier_mission': 'V506_HELI',
    'edf6tr_pjet_fighter_mission': 'V506_HELI',
    'edf6tr_pjet_strike_mission': 'V506_HELI',
    SAZABI_JET: 'V506_HELI',   # the Sazabi: its own model on the V506 body (vcobjects JETS)
    **{parked_name(k): 'V506_HELI' for k in PARKED_KINDS},
    # Ground vehicles with no stock `_mission` SGO: the call-in one, vehicle_setup renamed (same layout).
    'edf6tr_vehicle401_striker_mission': 'VEHICLE401_STRIKER',
    'edf6tr_vehicle502_groundrobo_mission': 'VEHICLE502_GROUNDROBO',
}
# Vehicles too big for a spot by the player (the 1664 m submarine): they take the farthest free points.
BIG = frozenset({'edf6tr_sub_carrier_mission'})


# The Primer creatures (see PRIMER_FILES): what the range places, at most how many a wave, in all, and how far
# over the ground spot it comes out (the centipede crawls: just over it; the dragonfly flies).
PRIMER_KINDS: dict[str, tuple[int, int, float]] = {'edf6vc_centipede': (16, 64, 4.0), 'edf6vc_dragonfly': (4, 16, 40.0)}
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
    ('edf6vc_centipede', '星导者·百足龙虫（插件；会合体成长龙）', False),
    ('edf6vc_dragonfly', '星导者·蜻蜓空优机（插件，飞）', True),
]
# The Primer creatures (src/primer.cpp, docs/primer-plan.md) are placed like a jet (CreateFriend; the plugin turns
# them to the enemy's side). Their SGOs are the installer's (tools/make_jets.py), which the range only uses
# (PRIMER_FILES). per_wave of them (at most PRIMER_KINDS' first) come once no enemy is left, at most its second
# in all: without the plugin they stay friends and would pile up.
PRIMER_FILES = ('OBJECT/EDF6VC_CENTIPEDE.SGO', 'OBJECT/EDF6VC_DRAGONFLY.SGO', 'OBJECT/EDF6VC_CENTIPEDE.MRAB',
                'OBJECT/EDF6VC_DRAGONFLY.MRAB', 'WEAPON/EDF6VC_PRIMER_SPIT.SGO', 'WEAPON/EDF6VC_PRIMER_NEEDLE.SGO',
                'WEAPON/EDF6VC_PRIMER_BARB.SGO', 'WEAPON/EDF6VC_PRIMER_STING.SGO')
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


# Enemy jets in waves (an air battle): once fewer than max_alive enemies are left, per_wave more enemy fighters come in
# from the far points (CreateFriend: the plugin puts them on the enemy team on first sight, src/jet.cpp CrewPlaced).
# It counts every enemy, so with it on the ground waves and targets are left out (one loop: a script has one Main).
AIR_ENEMY = 'edf6tr_jet_enemy_fighter_mission'


@dataclass
class AirWaves:
    enabled: bool = False
    per_wave: int = 2
    max_alive: int = 6
    first_delay: float = 20.0
    interval: float = 15.0


@dataclass
class Plan:
    vehicles: dict[str, int] = field(default_factory=lambda: {
        'vehicle403_tank_mission': 1, 'v603_flak_mission': 1, 'v504_begaruta_mission': 1})
    vehicle_level: float = 1.0
    waves: Waves = field(default_factory=Waves)
    loadout: dict = field(default_factory=dict)
    slot: str = DEFAULT_SLOT
    site: str = DEFAULT_SITE
    # Vehicles spawned with an NPC driver (CreateFriend, as stock missions spawn allied vehicles). An
    # NPC-piloted heli is flown by the plugin, several of them in formation.
    friends: dict[str, int] = field(default_factory=dict)
    air: AirWaves = field(default_factory=AirWaves)
    scenario: str = ''   # '' the waves above; GRAND the grand battle (grand_battle)


# An air battle (the test range's 「空战」 button): the player's fighter, a few friendly jets, eight enemy fighters to
# start with and more in waves; no ground enemies.
def air_battle(plan: Plan) -> Plan:
    plan.vehicles = {'edf6tr_pjet_fighter_mission': 1}
    plan.friends = {'edf6tr_jet_fighter_mission': 2, 'edf6tr_jet_interceptor_mission': 1, AIR_ENEMY: 8}
    plan.waves.enabled = False
    plan.air = AirWaves(enabled=True, per_wave=2, max_alive=8, first_delay=30.0, interval=10.0)
    return plan


# The grand battle (the user, 2026-10-05: "the mothership, teleport ships, the Primers' fighters against our jets and
# NPCs; a fierce battle in the sky above, a fierce one on the plain below, on one map"): on the big map
# (tools/make_bigmap.py) the mothership high up, the Primers' new ship and two teleport ships lower down, enemy jets
# and UFOs coming in waves in the sky while Martians and Berserkers come in waves on the plain; on our side the
# player's fighter, NPC fighters, interceptors and strike jets, NPC tanks and soldier squads. The sky and the ground
# run in threads of their own (GRAND_THREADS), each topping its side up to its own cap.
GRAND = 'grand'
GRAND_SHIPS = (('app:/object/e511_mothership_edf6.sgo', 900.0), ('app:/object/e611_timeship.sgo', 400.0),
               ('app:/object/e508_carrier.sgo', 250.0), ('app:/object/e508_carrier.sgo', 250.0))
GRAND_GROUND = ('app:/object/e601_martian_gs.sgo', 'app:/object/e602_berserker.sgo', 'app:/object/e601_martian_ls.sgo')
# The sky's waves: a pair of jets (CreateFriend: the plugin puts each on the enemy team on first sight, its body hostile)
# or a group of UFOs, in turn; the Primers' flapping fighters among them.
PRIMER_FIGHTER = 'edf6tr_jet_primer_fighter_mission'
GRAND_AIR = ('app:/object/' + AIR_ENEMY + '.sgo', 'app:/object/e507_goldufo.sgo', 'app:/object/' + PRIMER_FIGHTER + '.sgo',
             'app:/object/e605_spinnerufo.sgo')
GRAND_SQUADS = 3
# Shield Bearers on the plain (src/shield.cpp: slow rounds and planes through their shields, fast ones stopped).
GRAND_BEARER = 'app:/object/e513_shieldbearer.sgo'
GRAND_BEARERS = 2
GRAND_SOLDIERS = ('app:/object/AiArmySoldier_S_AF_Leader.sgo', 'app:/object/AiArmySoldier_S_Follower1.sgo')
GRAND_GROUND_CAP, GRAND_AIR_CAP = 30, 14   # enemies on each side it tops up to (counted together: the cap is the sum)
# Far points the sky's waves spawn on besides the ships' (GrandSky makes its jets on them, on the ground).
GRAND_SKY_SPAWNS = 2


# The plugin's aircraft the player boards (src/playerjet_kinds.h kBoardable) that have a range SGO: the grand battle
# parks one of each empty, on the ground for the player. An NPC-flown one is in the air at once (the user, 2026-10-05:
# the air carrier, placed as an NPC-flown friend for them to fly, "flew straight off", 405 m off its spot chasing
# targets): only H calls it down. (The bombers, the gunship and the drones have none: called or launched.)
# Each is its kind's parked twin (vcobjects PARKED_KINDS, Jet.parked): parked as the NPC SGO, its fuselage box let the
# player walk through the wings and its door was under the middle of the plane (「飞机缺少实体」「空母缺少登机口」,
# 2026-10-05); the NPC-flown ones keep their own SGO.
BOARDABLE_PARKED = tuple(parked_name(k) for k in PARKED_KINDS)


def grand_battle(plan: Plan) -> Plan:
    # Parked jets the player can board, several of each (the user, 2026-10-05: more planes on the ground to get in).
    # Every vehicle we added that the player drives (the user, 2026-10-05): the player's jets, the Katyusha and the
    # howitzer, the drill tank, the helicopters the range makes placeable, the tanks and the flak EDF6AutoTurret arms, the Depth Crawler;
    # and one of each of the plugin's other aircraft the player can fly (BOARDABLE_PARKED), parked empty.
    plan.vehicles = {'edf6tr_pjet_fighter_mission': 4, 'edf6tr_pjet_strike_mission': 3,
                     **{sgo: 1 for sgo in BOARDABLE_PARKED},
                     'edf6tr_katyusha_mission': 2, 'edf6tr_artillery_mission': 2, 'edf6tr_drill_mission': 2,
                     # One sidecar: the plain (M045) has 48 points; a second one (38 placements) took a far point the
                     # ships and the sky's spawns need (grand_points: 5 left of 6, the install stopped, 2026-10-06).
                     'edf6tr_sidecar_mission': 1,
                     'edf6tr_v506_heli_mission': 1, 'edf6tr_vehicle409_heli_mission': 1, 'edf6tr_vehicle410_heli_mission': 1,
                     'edf6tr_v602_heli_mission': 1, 'vehicle403_tank_mission': 1, 'vehicle404_bigtank': 1,
                     'v603_flak_mission': 1, 'edf6tr_vehicle502_groundrobo_mission': 1}
    # Our NPC-flown jets in the battle (H calls the nearest down to the player, README); the multirole and the carrier
    # are parked above instead (they were here only for the player to fly).
    plan.friends = {'edf6tr_jet_fighter_mission': 2, 'edf6tr_jet_interceptor_mission': 1, 'edf6tr_jet_strike_mission': 2,
                    'vehicle403_tank_mission': 3}
    if sazabi_model.model_dir():   # the Sazabi (tools/make_sazabi.py) only with its model: without it there is none
        # The plain's 37 spots are all taken: the second drill tank gives up its spot.
        plan.vehicles[SAZABI_JET] = 1
        plan.vehicles['edf6tr_drill_mission'] = 1
    plan.waves.enabled = False
    plan.air = AirWaves(enabled=False)
    plan.scenario = GRAND
    return plan


# The test range the installer writes (tools/installer.py; the user, 2026-10-06, on the 0.8.0 pack's range, which was the
# grand battle: 「测试场的怪给我去掉吧，留下靶子。然后载具再补充一下我们新加的」): no enemy at all, only targets (TARGET, the
# waves' target loop: a group comes back once one is shot down, every other spot up in the air), and every vehicle we
# added or reworked that the player drives and the range can place: the grand battle's (the player's jets, every
# boardable kind parked, the Katyusha, the howitzer, the drill tank, the sidecar, the helicopters, the tanks and the flak
# EDF6AutoTurret arms, the Depth Crawler) and RANGE_REWORKED.
# Room: the plain (M045) has 36 flat vehicle spots out to 450 m (layout: 15 m apart) and they are not all room (a
# carrier keeps 50 m clear, a jet 15-22 m: footprint). On the real plain (tools/selftest.py
# target_range_fits_the_real_plain) the grand battle's 37 placements and these three overlap by up to 6.2 m, so the
# range keeps one of each ground vehicle the grand battle has two of (the player's jets stay four and three: one a
# player online), leaves out the NPC tanks (an empty tank gets an NPC driver anyway, README 「NPC 自动上车」) and keeps one
# NPC-flown fighter of five (H calls it down, README; a second one overlaps the Nix by 2.3 m).
# The stock vehicles the plugin reworks while the player rides them: the EMC's charged beam (src/emc.cpp), the Nix's
# torso (src/nix.cpp), the Proteus's two seats and modes (src/proteus.cpp).
RANGE_REWORKED = ('v510_maser_mission', 'v612_nix_g_mission', 'v614_proteus_mk2_mission')
RANGE_FRIENDS = {'edf6tr_jet_fighter_mission': 1}
RANGE_WAVES = Waves(enabled=True, enemy=TARGET, per_wave=4, max_alive=24, first_delay=5.0, interval=5.0)


def target_range(plan: Plan) -> Plan:
    """The installer's range (see RANGE_REWORKED): targets only, every vehicle we added."""
    fleet = grand_battle(Plan())
    plan.vehicles = {**{s: n if s in JETS else min(n, 1) for s, n in fleet.vehicles.items()},
                     **{s: 1 for s in RANGE_REWORKED}}
    plan.friends = dict(RANGE_FRIENDS)
    plan.waves = replace(RANGE_WAVES)
    plan.air = AirWaves(enabled=False)
    plan.scenario = ''
    return plan


def grand_points(lay: Layout) -> list[tuple[str, float, rmpa.Point]]:
    """(ship SGO, height, its point): the farthest free points, raised (install writes them up in MISSION.RMPA)."""
    far = lay.far_points[::-1]
    if len(far) < len(GRAND_SHIPS) + GRAND_SKY_SPAWNS:
        raise ValueError('大混战：这张地图远处的空闲点位不够放舰船')
    return [(sgo, dy, p) for (sgo, dy), p in zip(GRAND_SHIPS, far)]


def placements(plan: Plan) -> list[tuple[str, bool]]:
    """(sgo, NPC-driven) for every vehicle to place, empty ones first, the BIG ones last. A player jet
    (Jet.player) is always placed empty: it is the player's to fly, and an NPC in it would be flown as a heli."""
    chosen = ([(s, False) for s, n in plan.vehicles.items() for _ in range(max(0, n))] +
              [(s, not (s in JETS and (JETS[s].player or JETS[s].parked))) for s, n in plan.friends.items()
               for _ in range(max(0, n))])
    return [c for c in chosen if c[0] not in BIG] + [c for c in chosen if c[0] in BIG]


def small_count(plan: Plan) -> int:
    return sum(1 for s, _ in placements(plan) if s not in BIG)


def save_plan(path: str, plan: Plan) -> None:
    data = {'vehicles': plan.vehicles, 'vehicle_level': plan.vehicle_level,
            'waves': plan.waves.__dict__, 'air': plan.air.__dict__, 'loadout': plan.loadout, 'slot': plan.slot,
            'site': plan.site, 'friends': plan.friends, 'scenario': plan.scenario}
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
    plan.air = AirWaves(**{k: v for k, v in data.get('air', {}).items() if k in AirWaves.__dataclass_fields__})
    plan.loadout = data.get('loadout', {})
    if data.get('slot') in {x.mission for x in SLOTS}:
        plan.slot = data['slot']
    if data.get('site') in {x.source for x in SITES}:
        plan.site = data['site']
    plan.scenario = GRAND if data.get('scenario') == GRAND else ''
    return plan



@dataclass
class Layout:
    player: rmpa.Point
    vehicle_points: list[rmpa.Point]
    enemy_points: list[rmpa.Point]
    far_points: list[rmpa.Point] = field(default_factory=list)   # free points past the enemy ring
    # What spots_for placed (and the player's start, a SPOT), each with its footprint: no target group comes out on them.
    taken: list[tuple[rmpa.Point, float]] = field(default_factory=list)


# How far out vehicle spots are taken, ring by ring, until there are as many as wanted: the plain has 12
# within 160 m, 21 within 300 m, 39 within 450 m and 44 within 800 m.
SPOT_RINGS = (160.0, 300.0, 450.0, 800.0)


# How many far points a range of targets keeps for them (target_ranged): RANGE_WAVES' groups, three on the ground and
# three in the air.
TARGET_SPOTS = 6


def target_ranged(plan: Plan) -> bool:
    """Does `plan` put up targets (TARGET in the waves) rather than enemies? (The air waves leave them out, AirWaves.)"""
    return plan.waves.enabled and plan.waves.enemy == TARGET and not plan.air.enabled


def far_reserved(plan: Plan) -> int:
    """How many of the farthest points `plan` keeps for itself (no vehicle stands on them): the grand battle's ships
    and its sky's spawns, or a range's targets (TARGET_SPOTS)."""
    if plan.scenario == GRAND:
        return len(GRAND_SHIPS) + GRAND_SKY_SPAWNS
    return TARGET_SPOTS if target_ranged(plan) else 0


def layout(points: list[rmpa.Point], need: int = 0, reserve: int = 0) -> Layout:
    """Vehicle spots: flat points from 30 m off the player start, 15 m apart, nearest first, out to
    160 m or as far out (SPOT_RINGS) as `need` of them takes; enemy spots 180-450 m, the nearest `need` vehicle
    spots left out (all of them when that leaves none; spots_for then drops the ones it really took: spaced may
    take farther ones of them for a wide vehicle). The `reserve` farthest points past 450 m are never vehicle spots
    (far_reserved: the 800 m ring once handed the grand battle's ship points to its jets, 2026-10-06)."""
    player = next(p for p in points if p.name == 'プレイヤー')
    by_distance = sorted(points, key=lambda p: math.dist(p.pos, player.pos))
    outer = [p for p in by_distance if math.dist(p.pos, player.pos) > 450]
    kept = {p.name for p in outer[len(outer) - reserve:]} if reserve else set()
    spots: list[rmpa.Point] = []
    for far in SPOT_RINGS:
        for p in by_distance:
            d = math.dist(p.pos, player.pos)
            if (30 <= d <= far and abs(p.pos[1] - player.pos[1]) < 4 and p not in spots and p.name not in kept
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
    points = spaced(small, lay.vehicle_points, lay.player)
    # What the vehicles stand on is no enemy's or ship's point (layout left out only the nearest `need`).
    lay.enemy_points = [p for p in lay.enemy_points if p not in points] or lay.enemy_points
    lay.far_points = [p for p in lay.far_points if p not in points]
    far = lay.far_points[::-1][:len(big)]
    lay.far_points = [p for p in lay.far_points if p not in far]
    lay.taken = [(lay.player, SPOT)] + [(p, footprint(s)) for (s, _), p in zip(small + big, points + far)]
    return [(s, npc, p) for (s, npc), p in zip(small, points)] + [(s, npc, p) for (s, npc), p in zip(big, far)]


# Each placement keeps a circle of its footprint's radius clear round its point (m): two stand at least the sum of
# theirs apart, so nothing is placed inside another (overlapping bodies are pushed apart, up, at the mission's start:
# the air carrier, 33 m from a jet, began 11 m over its spot on 2026-10-05). The vehicle spots are 15 m apart (layout),
# which keeps two SPOT ones apart already: a tank or a car takes the nearest spot left, as before.
SPOT = 7.5
# Parked jets stand at least JET_GAP m apart (the fighter is 16 m across, the strike jet's wings wider): several
# parked jets side by side would lock wings at the start (the grand battle parks a dozen, the user, 2026-10-05).
JET_GAP = 30.0
# The other vehicles kept JET_GAP apart like the jets: the helicopters' rotors.
WIDE = frozenset({'edf6tr_v506_heli_mission', 'edf6tr_v506_heli_edf6benefits_mission', 'edf6tr_vehicle409_heli_mission',
                  'edf6tr_vehicle410_heli_mission', 'edf6tr_v602_heli_mission'})
# Stock vehicles longer than a SPOT, by how far their model reaches from its origin across the ground (m, the bind pose's
# vertices in the SGO's model, Root.cpk; tools/selftest.py footprint_covers_the_stock_models measures them again): the
# EMC's barrel 21.0 (v510_maser.mdb), the big tank's 16.6 (in WIDE before: 15), the Proteus's legs 12.0. Each its reach,
# rounded up.
REACH: dict[str, float] = {'v510_maser_mission': 21.5, 'vehicle404_bigtank': 17.0, 'v614_proteus_mk2_mission': 12.5,
                           'vehicle407_bigbegaruta_mission': 11.5,
                           # the Sazabi: its shield and funnel packs 11.1 m out to a side (its tubes reach 15.1 m back,
                           # but 13 m up: over every vehicle parked round it)
                           SAZABI_JET: 12.0}
# The air carriers (pylib/vcobjects.py JETS: the EDF transport x 1.6, 59 x 77 m): half its diagonal (48.5 m), whichever
# way it faces, and a margin.
CARRIER_MODEL = 'EDF6VC_CARRIER.MRAB'
CARRIER_RADIUS = 50.0


# A jet in the elevon bomber (the strike jet's and the fighter's model, 24.8 m across, its nose 17.9 m ahead of its origin:
# jet_models.model_box) reaches 21.7 m from its point whichever way it faces; a whole-model box (a player jet's, a
# parked one's: vcobjects Jet.player / parked) that far out would lock with a neighbour's JET_GAP off.
ELEVON_RADIUS = 22.0


def footprint(sgo: str) -> float:
    """The radius (m) `sgo` keeps clear round its point (see SPOT)."""
    if sgo in REACH:
        return REACH[sgo]
    jet = JETS.get(sgo)
    if jet and jet.file == CARRIER_MODEL:
        return CARRIER_RADIUS
    if jet and jet.file is None and (jet.player or jet.parked):
        return ELEVON_RADIUS
    return JET_GAP / 2 if jet or sgo in WIDE else SPOT


def _apart(a: rmpa.Point, b: rmpa.Point) -> float:
    return ((a.pos[0] - b.pos[0]) ** 2 + (a.pos[2] - b.pos[2]) ** 2) ** 0.5


def overlap(p: rmpa.Point, r: float, taken: list[tuple[rmpa.Point, float]]) -> float:
    """How far (m) a footprint of radius `r` at `p` reaches into the worst of `taken`'s, OVERLAP_SLACK and less
    counted as none (the spots are 15 m apart in 3-D, a little less across the ground)."""
    worst = max([0.0] + [r + rt - _apart(p, t) for t, rt in taken])
    return worst if worst > OVERLAP_SLACK else 0.0


OVERLAP_SLACK = 1.0


def spaced(chosen: list[tuple[str, bool]], points: list[rmpa.Point], player: rmpa.Point | None = None) -> list[rmpa.Point]:
    """A point for each of `chosen` (in order): the player's (empty) ones before the NPC-driven ones, each group the
    widest first (footprint). Each takes, of the spots left, the one whose footprint reaches least into those taken
    so far (and the player's start, a SPOT); of those, the one whose footprint covers fewest of the other spots left
    (a SPOT's room round each: what a wide one stands over no other vehicle can use, so the carrier takes a spot off
    by itself rather than the cluster round the player); of those, the nearest. For a SPOT one every spot covers none
    (they are 15 m apart): the nearest left, as before."""
    left = list(points)
    taken: list[tuple[rmpa.Point, float]] = [(player, SPOT)] if player else []
    out: list[rmpa.Point | None] = [None] * len(chosen)
    for i in sorted(range(len(chosen)), key=lambda k: (chosen[k][1], -footprint(chosen[k][0]))):
        r = footprint(chosen[i][0])

        def cost(n: int) -> tuple[float, int, int]:
            q = left[n]
            covered = sum(1 for o in left if o is not q and overlap(q, r, [(o, SPOT)]) > 0.0)
            return overlap(q, r, taken), covered, n
        p = left.pop(min(range(len(left)), key=cost))
        taken.append((p, r))
        out[i] = p
    return out


# A target group comes out within this many m of its spot (script: CreateEnemyGroup's radius).
TARGET_SPREAD = 30.0


def target_spots(lay: Layout) -> list[rmpa.Point]:
    """Where targets (TARGET) stand: the enemy spots and the free points past them (the plain has 48 points,
    the vehicles take most: 7 enemy spots were too few), but none whose group (TARGET_SPREAD round it) would come out
    on a placed vehicle or the player's start (lay.taken, spots_for: with the vehicles out to 376 m on the plain, a target
    group on a free point among them would come out on the parked jets). All of them when every one would."""
    every = lay.enemy_points + lay.far_points
    return [p for p in every if overlap(p, TARGET_SPREAD, lay.taken) == 0.0] or every


def air_targets(lay: Layout) -> list[rmpa.Point]:
    """The target spots raised for targets in the air (TARGET): every other one."""
    return target_spots(lay)[1::2]


def _q(text: str) -> str:
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


SPAWN_BATCH, SPAWN_GAP = 4, 0.25


def script(plan: Plan, lay: Layout) -> str:
    """The mission script. Same skeleton as the stock generated scripts (event 0 = Main)."""
    chosen = placements(plan)
    site = site_of(plan.site)
    placed = spots_for(plan, lay)
    w = plan.waves
    air = plan.air
    if air.enabled:   # one loop: the air battle's (see AirWaves)
        w = Waves(**{**w.__dict__, 'enabled': False})
    targets = w.enemy == TARGET
    flying = next((f for s, _, f in ENEMIES if s == w.enemy), False)
    grand = plan.scenario == GRAND
    preload = sorted({f'app:/object/{s}.sgo' for s, _ in chosen} | ({f'app:/object/{w.enemy}.sgo'} if w.enabled else set())
                     | ({f'app:/object/{AIR_ENEMY}.sgo'} if air.enabled else set())
                     | ({s for s, _ in GRAND_SHIPS} | set(GRAND_GROUND) | set(GRAND_AIR) | set(GRAND_SOLDIERS) | {GRAND_BEARER}
                        if grand else set()))
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
        f'\tPreloadMap({_q(site.map)}, {_q(site.weather)}, -1);',
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
        f'\tMap({_q(site.map)}, {_q(site.weather)});',
        f'\tCreatePlayer({_q(lay.player.name)});',
    ]
    # The NPC vehicles come out SPAWN_BATCH at a time, SPAWN_GAP s apart: 37 of them created in the first frame
    # (each a model, a body and a plugin entry set up at once) were the hitch the mission started with.
    # The player's own vehicles first, all at once: only the NPC ones wait.
    friends = 0
    for sgo, npc, point in sorted(placed, key=lambda e: bool(e[1])):
        path = _q('app:/object/' + sgo + '.sgo')
        if npc:   # last argument: does it join the player's squad (no: the plugin flies/drives it)
            if friends and friends % SPAWN_BATCH == 0:
                lines.append(f'\tWait({SPAWN_GAP:.2f});')
            friends += 1
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
            f'\t\t\tCreateEnemyGroup(spots[next % spots.length()], {TARGET_SPREAD:.0f}, {_q("app:/object/" + w.enemy + ".sgo")}, {per}, {w.level:.2f}, true);',
            '\t\t\tnext++;',
            '\t\t}',
            '\t\tWait(1.0);',
            '\t}',
        ]
    elif w.enabled and lay.enemy_points and w.enemy in PRIMER_KINDS:
        most, total, _ = PRIMER_KINDS[w.enemy]
        per = max(1, min(int(w.per_wave), most))
        pts = ', '.join(_q(p.name) for p in lay.enemy_points)
        lines += [
            '',
            '\t// The Primer creatures (EDF6VehicleCrew turns each to the enemy): the next',
            f'\t// {per} once no enemy is left, at most {total} in all.',
            f'\tarray<string> spots = {{ {pts} }};',
            '\tuint next = 0;',
            '\tuint made = 0;',
            f'\tWait({w.first_delay:.1f});',
            '\twhile( true ) {',
            f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) == 0 && made < {total} ) {{',
            f'\t\t\tfor( uint i = 0; i < {per}; i++ ) {{',
            f'\t\t\t\tCreateFriend(spots[next % spots.length()], {_q("app:/object/" + w.enemy + ".sgo")}, {w.level:.2f}, false);',
            '\t\t\t\tnext++;',
            '\t\t\t\tmade++;',
            '\t\t\t}',
            f'\t\t\tWait({max(w.interval, 10.0):.1f});',
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
    if air.enabled and (lay.far_points or lay.enemy_points):
        pts = ', '.join(_q(p.name) for p in (lay.far_points or lay.enemy_points))
        enemy = _q('app:/object/' + AIR_ENEMY + '.sgo')
        lines += [
            '',
            '\t// Enemy jets in waves (AirWaves): once fewer than max_alive enemies are left, per_wave more from the far points.',
            f'\tarray<string> air = {{ {pts} }};',
            '\tuint nextAir = 0;',
            f'\tWait({air.first_delay:.1f});',
            '\twhile( true ) {',
            f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) < {int(air.max_alive)} ) {{',
            f'\t\t\tfor( int k = 0; k < {max(1, int(air.per_wave))}; k++ ) {{',
            f'\t\t\t\tCreateFriend(air[nextAir % air.length()], {enemy}, {plan.vehicle_level:.2f}, false);',
            '\t\t\t\tnextAir++;',
            '\t\t\t}',
            f'\t\t\tWait({air.interval:.1f});',
            '\t\t}',
            '\t\tWait(1.0);',
            '\t}',
        ]
    if grand:
        lines += grand_main(plan, lay)
    lines += [
        '\t// The mission must never return on its own (stock scripts end the same way); leave from the pause menu.',
        '\twhile( true ) sys_Yield();',
        '}',
        '',
        *(grand_threads(plan, lay) if grand else []),
        'void __0000_voice_event()',
        '{',
        '\t::__0000_data.m_voice_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_game_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
    ]
    return '\n'.join(lines)


def grand_main(plan: Plan, lay: Layout) -> list[str]:
    """The grand battle's opening in Main: the ships up on their raised points, the soldier squads by the player, the
    two threads started."""
    out = ['', '\t// The grand battle (GRAND): the ships, the squads, then the sky and the ground in threads of their own.']
    for sgo, _, p in grand_points(lay):
        out.append(f'\tCreateEnemy({_q(p.name)}, {_q(sgo)}, {plan.vehicle_level:.2f}, true);')
    for p in lay.enemy_points[-GRAND_BEARERS:]:
        out.append(f'\tCreateEnemy({_q(p.name)}, {_q(GRAND_BEARER)}, {plan.vehicle_level:.2f}, true);')
    spots = lay.enemy_points[:GRAND_SQUADS] or [lay.player]
    for p in spots:
        out.append(f'\tCreateFriendSquad({_q(lay.player.name)}, 40, {_q(GRAND_SOLDIERS[0])}, {_q(GRAND_SOLDIERS[1])}, 6, '
                   f'{plan.vehicle_level:.2f}, false);')
    out += ['\t::internal_CreateThread("GrandGround");', '\t::internal_CreateThread("GrandSky");']
    return out


def grand_threads(plan: Plan, lay: Layout) -> list[str]:
    """The two threads: the ground tops itself up with groups on the enemy spots, the sky with enemy jets and UFOs on
    the far points (and up high), each every few seconds while the enemies number under the caps."""
    ground = ', '.join(_q(p.name) for p in lay.enemy_points) or _q(lay.player.name)
    ships = {p.name for _, _, p in grand_points(lay)}
    far = ', '.join(_q(p.name) for p in lay.far_points if p.name not in ships) or ground
    gk = ', '.join(_q(x) for x in GRAND_GROUND)
    ak = ', '.join(_q(x) for x in GRAND_AIR)
    lv = f'{plan.vehicle_level:.2f}'
    return [
        'void GrandGround()',
        '{',
        f'\tarray<string> spots = {{ {ground} }};',
        f'\tarray<string> kinds = {{ {gk} }};',
        '\tuint next = 0;',
        '\tWait(15.0);',
        '\twhile( true ) {',
        f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) < {GRAND_GROUND_CAP + GRAND_AIR_CAP} ) {{',
        f'\t\t\tCreateEnemyGroup(spots[next % spots.length()], 30, kinds[next % kinds.length()], 6, {lv}, true);',
        '\t\t\tnext++;',
        '\t\t}',
        '\t\tWait(8.0);',
        '\t}',
        '}',
        '',
        'void GrandSky()',
        '{',
        f'\tarray<string> spots = {{ {far} }};',
        f'\tarray<string> kinds = {{ {ak} }};',
        '\tuint next = 0;',
        '\tWait(20.0);',
        '\twhile( true ) {',
        f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) < {GRAND_GROUND_CAP + GRAND_AIR_CAP} ) {{',
        '\t\t\tstring k = kinds[next % kinds.length()];',
        f'\t\t\tif( k.findFirst("edf6tr_jet") >= 0 ) {{ CreateFriend(spots[next % spots.length()], k, {lv}, false); '
        f'CreateFriend(spots[(next + 1) % spots.length()], k, {lv}, false); }}',
        f'\t\t\telse CreateEnemyGroup(spots[next % spots.length()], 40, k, 3, {lv}, true);',
        '\t\t\tnext++;',
        '\t\t}',
        '\t\tWait(10.0);',
        '\t}',
        '}',
        '',
    ]


# Our ground vehicles made placeable (the user, 2026-10-05: every vehicle we added on the map, to drive): the call-in
# SGO their own tool makes (tools/make_katyusha.py, make_artillery.py: its model and weapons are what that tool, and
# the installer, write) turned into a mission one (as_mission_sgo).
GROUND_MISSION: dict[str, str] = {'edf6tr_katyusha_mission': 'make_katyusha', 'edf6tr_artillery_mission': 'make_artillery',
                                  'edf6tr_drill_mission': 'make_drill', 'edf6tr_sidecar_mission': 'make_sidecar'}


# The stock ground vehicles the range makes placeable from their call-in SGO (DERIVED), and the player call whose gun
# mounts they take (the first tier: its guns are the call-in SGO's). The call-in OBJECT's own vehicle_setup is not what
# the player gets (the call carries its own setup), and its recoil is not the call's: the Grape's [0.3, 5.5] against
# the call's [0.01, 0.1]; the helis' [0.0001, 0.1] / [0.01, 0.1] against [0, 0] (docs/recoil-re.md). The Nereid (409)
# already agrees; the Depth Crawler's mounts carry no recoil.
PLAYER_CALLS: dict[str, str] = {
    'edf6tr_vehicle401_striker_mission': 'EVEHICLE_STRIKER01',     # Grape
    'edf6tr_v506_heli_mission': 'EWEAPON394',                       # N9 Eros
    # Eros No. 6: its own call (DLC_VEHICLE_HELI_EDF6BENEFITS) carries napalm where the call-in SGO has the Eros's
    # missile, so it takes the call that mounts the same guns.
    'edf6tr_v506_heli_edf6benefits_mission': 'EWEAPON394',
    'edf6tr_v602_heli_mission': 'AWEAPON371',                       # Heron YG10E (the YG10 has no missile)
    'edf6tr_vehicle410_heli_mission': 'AWEAPON367',                 # HU04 Brute
}


def player_mounts(game: Game, sgo_name: str) -> list[recoil.Mount | None] | None:
    """The gun mounts (weapon, recoil) of the call that brings this vehicle to the player: ours (GROUND_MISSION) are
    requested by their own call, which tools/call_weapons.py makes from the vehicle's stock request
    (vcobjects.GROUND_VEHICLES); the stock ones by PLAYER_CALLS. None: no player call to follow."""
    if sgo_name in GROUND_MISSION:
        import call_weapons
        key = sgo_name.removeprefix(DERIVED_PREFIX).removesuffix('_mission')
        call = next(c for c in call_weapons.CALLS if c.ground == key)
        template = game.read('WEAPON', call_weapons.template_of(call).upper() + '.SGO')
        # The tier only scales the multipliers; the mounts are the request's whatever the level.
        return recoil.mounts_of(call_weapons.weapon_sgo(template, call, [(0.0, 1.0, 1.0)]), call.id)
    if sgo_name in PLAYER_CALLS:
        return recoil.mounts_of(game.read('WEAPON', PLAYER_CALLS[sgo_name] + '.SGO'), PLAYER_CALLS[sgo_name])
    return None


def _with_player_recoil(game: Game, sgo_name: str, data: bytes) -> bytes:
    """`data` with every mission_setup gun mount's recoil the player call's (pylib/recoil.py), when there is one."""
    mounts = player_mounts(game, sgo_name)
    return data if mounts is None else recoil.align_data(data, mounts, sgo_name)[0]


def vehicle_sgo(game: Game, sgo_name: str, jet_model: list[str] | None = None) -> bytes:
    """The SGO bytes the mission will load for this vehicle (generated ones are made here)."""
    if sgo_name in JETS:
        return jet_sgo(game, sgo_name, jet_model)
    if sgo_name in GROUND_MISSION:
        import importlib
        data = importlib.import_module(GROUND_MISSION[sgo_name]).vehicle_sgo(game)
        # The Freed bike's SGO (the sidecar's) is a script's already: it has its mission_setup next to vehicle_setup.
        data = data if 'mission_setup'.encode('utf-16le') + b'\0\0' in data else as_mission_sgo(data)
        return _with_player_recoil(game, sgo_name, data)
    stock = DERIVED.get(sgo_name)
    if stock:
        return _with_player_recoil(game, sgo_name, as_mission_sgo(game.read('OBJECT', stock + '.SGO')))
    return game.read('OBJECT', sgo_name.upper() + '.SGO')


def has_mission_setup(game: Game, sgo_name: str) -> bool:
    import sgo
    try:
        values = sgo.load(data=vehicle_sgo(game, sgo_name))
    except (KeyError, ValueError):
        return False
    return isinstance(values, dict) and 'mission_setup' in values


def _write_derived(game_root: str, game: Game, wanted: set[str], uses: tuple[str, ...] = ()) -> None:
    """Makes Mods/OBJECT hold exactly the generated vehicles in `wanted` (files with our prefix only), with the
    models and guns they use, and records in the ledger (pylib/ledger.py) every file the range now needs:
    what it wrote and what it uses from tools/make_jets.py (the elevon bomber, `uses`: the Primer creatures'). What it
    needed before and does not now is released, so a model or gun nobody else needs goes with it."""
    led = ledger.Ledger(game_root)
    before = set(led.owned_by(OWNER))
    jets = wanted & JETS.keys()
    held = {ledger.key(f'OBJECT/{name.upper()}.SGO') for name in wanted}
    for rel in uses:
        led.need(OWNER, rel)
        held.add(ledger.key(rel))
    for f in sorted({JETS[n].file for n in jets if JETS[n].file}):
        held.add(_need_model(led, game, f))
    if jets:
        for name, data in jet_guns(game).items():
            led.put(OWNER, f'WEAPON/{name}', data)
            held.add(ledger.key(f'WEAPON/{name}'))
    if SAZABI_JET in jets:
        from vcobjects import sazabi_weapons, sazabi_rounds
        # A standalone range needs both the mounted weapons and plugin-fired beams. Register these
        # even after a full install, so removing that install cannot break a range still using them.
        for folder, files in (('WEAPON', sazabi_weapons(game)), ('OBJECT', sazabi_rounds(game))):
            for name, data in files.items():
                rel = f'{folder}/{name}'
                led.put(OWNER, rel, data)
                held.add(ledger.key(rel))
    elevon = f'OBJECT/{JET_ELEVON_FILE}'
    if any(JETS[n].model is None for n in jets) and not os.path.isfile(led.disk(elevon)):
        # The default bomber's shape is measured on the grounded elevon model.
        # A standalone range installation must generate that same model too.
        led.put(OWNER, elevon, jet_models.elevon_archive(game))
    elevons = os.path.isfile(led.disk(elevon))
    if elevons and any(JETS[n].model is None for n in jets):
        led.need(OWNER, elevon)
        held.add(ledger.key(elevon))
    import aircraft_collision
    for name in sorted(jets):
        key = aircraft_collision.model_key(JETS[name], JET_ELEVON_MODEL if elevons else None)
        if key is not False:
            rel = f'OBJECT/{aircraft_collision.FILES[key]}'
            # Rebuild from the same Root.cpk/model recipe, never reuse a stale
            # loose shape with a different mesh or a previous collision version.
            led.put(OWNER, rel, aircraft_collision.build(game, key))
            held.add(ledger.key(rel))
    for name in sorted(wanted):
        led.put(OWNER, f'OBJECT/{name.upper()}.SGO', vehicle_sgo(game, name, JET_ELEVON_MODEL if elevons else None))
    led.release(OWNER, sorted(before - held))
    _remove_legacy(game_root, keep=wanted)


def _need_model(led: ledger.Ledger, game: Game, file: str) -> str:
    """A jet model archive (pylib/jet_models.py): the one tools/make_jets.py or an earlier install wrote, else
    made now; recorded as the range's either way. Returns its ledger key."""
    rel = f'OBJECT/{file}'
    if os.path.isfile(led.disk(rel)):
        led.need(OWNER, rel)
    elif file == jet_models.SAZABI_FILE:   # the installer's (tools/make_sazabi.py), else made from its model folder
        folder = sazabi_model.model_dir()
        if folder is None:
            raise RuntimeError('没有沙扎比模型目录（models/sazabi）：测试场放不了沙扎比')
        led.put(OWNER, rel, sazabi_model.build_archive(game, folder)[0])
    elif file in jet_models.GENERATED_FILES:
        led.put(OWNER, rel, jet_models.generated(game, file))
    else:
        recipe = {**jet_models.MODELS, **jet_models.SUB_MODELS}[file]
        led.put(OWNER, rel, jet_models.build(game, {file: recipe})[file])
    return ledger.key(rel)


def _remove_legacy(game_root: str, keep: set[str] = frozenset()) -> bool:
    """Our prefixed vehicles a range from before the ledger wrote (the ledger does not know them)."""
    d = object_dir(game_root)
    if not os.path.isdir(d):
        return False
    led = ledger.Ledger(game_root)
    gone = [f for f in os.listdir(d) if f.lower().startswith(DERIVED_PREFIX) and f.lower()[:-4] not in keep
            and not led.owners(f'OBJECT/{f}')]
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


def spawned(plan: Plan) -> set[str]:
    """Every object SGO name (no app:/object/, no .sgo) the mission's script creates: the placed vehicles, the air
    waves' enemy jet, the grand battle's sky. The generated ones among them must be written (_write_derived) and the
    placed ones checked for a mission_setup: a script that creates an SGO the game cannot find ends the game (2026-10-05
    10:40, dump EDF6.exe.79680: the grand battle's CreateFriend of the enemy fighter, never written, faulted in the
    game's own error stop)."""
    names = {x for x, _ in placements(plan)}
    if plan.air.enabled:
        names.add(AIR_ENEMY)
    if plan.scenario == GRAND:
        names |= {sgo.removeprefix('app:/object/').removesuffix('.sgo') for sgo in GRAND_AIR}
    return names


def install(game_root: str, plan: Plan) -> list[str]:
    """Writes the range over plan.slot (and removes it from the other slot). Refuses to touch a
    folder another mod put there."""
    out = mission_dir(game_root, plan.slot)
    if os.path.isdir(out) and os.listdir(out) and not ours(game_root, plan.slot):
        raise RuntimeError(f'{out} 已有别的 mod 的文件，不覆盖。请先手动处理。')
    game = Game(game_root)
    for sgo_name in spawned(plan) & (DERIVED.keys() | JETS.keys() | {x for x, _ in placements(plan)}):
        if not has_mission_setup(game, sgo_name):
            raise RuntimeError(f'{sgo_name} 没有 mission_setup，不能由脚本放置（会让游戏崩溃）')
    points_file = game.read(f'MISSION/EDF6/{plan.site}', 'MISSION.RMPA')
    lay = layout(rmpa.points(points_file), small_count(plan), far_reserved(plan))
    text = script(plan, lay)
    placed = [(s, npc, p) for s, npc, p in
              spots_for(plan, layout(rmpa.points(points_file), small_count(plan), far_reserved(plan)))]
    if plan.scenario == GRAND:   # the ships' points up in the air
        for _, dy, p in grand_points(lay):
            points_file = rmpa.raised(points_file, {p.name}, dy)
    if target_ranged(plan):
        points_file = rmpa.raised(points_file, {p.name for p in air_targets(lay)}, TARGET_AIR)
    if plan.waves.enabled and plan.waves.enemy in PRIMER_KINDS:
        points_file = rmpa.raised(points_file, {p.name for p in lay.enemy_points}, PRIMER_KINDS[plan.waves.enemy][2])
    primer = plan.waves.enabled and plan.waves.enemy in PRIMER_KINDS
    if primer and not all(os.path.isfile(os.path.join(game_root, 'Mods', *rel.split('/'))) for rel in PRIMER_FILES):
        raise RuntimeError('没有星导者生物的机体（Mods/OBJECT/EDF6VC_CENTIPEDE / _DRAGONFLY.SGO）：先运行 EDF6VehicleCrew 安装器选「安装」')
    os.makedirs(out, exist_ok=True)
    _write_derived(game_root, game, {x for x in spawned(plan) if x in DERIVED},
                   PRIMER_FILES if primer else ())
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
    """Removes the range's missions and releases its files: generated vehicles, and the models and guns no other
    tool needs (pylib/ledger.py)."""
    deleted, _ = ledger.Ledger(game_root).release(OWNER)
    return any([_remove(game_root, x.mission) for x in SLOTS] + [_remove_legacy(game_root), bool(deleted)])


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
