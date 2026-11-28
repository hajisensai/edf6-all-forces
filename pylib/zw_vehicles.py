"""The Vehicler's (class 4, ZwVehicler) vehicles (docs/newclass-re/vehicler-plan.md §1.2, plan-2026-11-28-zero-worldline-gear
§1): one entry per vehicle its vehicle slot can carry. Its weapon row (tools/zw_weapons.py, category 400) is only the
tag of the choice; the vehicle is the plugin's own Mods/OBJECT SGO here, made by tools/make_zw_vehicles.py from a
stock hull, and src/zw_vehicles.inc (tools/gen_zw_vehicles.py) tells the plugin which row is which vehicle.

Not vcobjects.GroundVehicle / GROUND_VEHICLES: those are the requested vehicles (Call.ground), whose request row, family
levels and store kinds call_weapons and gen_stores build from; these are no request, have no family and carry their own
guns. Their own registry keeps them out of every request path.

Stage 2 models: every vehicle is its stock hull's model (`state` 'placeholder', vehicler-plan.md: the real ones are being
found); `todo` lists what the vehicle is meant to have and this stage does not build.

  setup        the vehicle_setup = mission_setup the plugin applies (support_spawn ApplyMissionSetup: 0x62D6E0 reads
               mission_setup): [0] [durability, damage] multipliers, [1][1] the top speed in m/s and [1][2] the drive
               force factor (TankBase 0x67FB00, docs/vehicle-top-speed-re.md), [2] the guns
  envelope     (half width, half length, height) about the native origin in metres, the stock MDB's bounds rounded out
               (src/support_spawn.h kSupportVehicles does the same; tests/zw_vehicles_test.py reads them back)
  curve        the stock request curve its guns' damage goes by with the support level (src/support_level.h Curve,
               support_firepower.h: the plugin marks the vehicle as it marks a ground support tank), its growth over the
               campaign: the SGO damage is the level-1 tank's order, the level multiplies it (0.7 .. 23 on 'blacker').
               The HP does not grow (support_firepower.h: the game's own, unlevelled)
"""
from __future__ import annotations

from dataclasses import dataclass, field

LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
CURVES = ('eros', 'blacker', 'grape')   # src/support_level.h enum class Curve
LOGISTICS_GAUGE = 40   # the logistics gauge's size (src/zw_vehicle_catalog.h kZwLogisticsGauge): one full slot
NAME_KEYS = {'JA': 'name.ja', 'EN': 'name.en', 'CN': 'name.cn', 'KR': 'name.kr', 'SC': 'name.sc'}

# The native vehicle classes a hull may be, and their vtables (EDF.dll 0x678CCB46): what src/zw_vehicles.inc gives the
# plugin, derived from the built SGO's xgs_scene_object_class (vehicler-plan.md §1.2: not typed into C++ by hand). The same
# constants as src/support_spawn.h kSupportVehicles[0] (Vehicle505_Tank) and autoturret/src/turret.h kFlakVtable.
CLASS_VTABLES: dict[str, int] = {'Vehicle505_Tank': 0x17DADB0, 'Vehicle603_Flak': 0x17DC620,
                                  'Vehicle506_Helicopter': 0x17DB238,   # src/layout.h kVt506
                                  'Vehicle510_Maser': 0x17DB9D8}        # src/vehicleram.h 510_Maser
# Hull classes whose vehicle_setup is [multipliers, params, flight, guns] (V506_HELI): their params are the stock flight
# model's (no ground top speed / drive force at [1][1] / [1][2]): the vehicle keeps them and `speed` / `force` are 0.
FLYING = ('Vehicle506_Helicopter',)
# Stock weapons a hull's setup carries past its guns, kept as they are (V506_HELI's fuel gauge, damage 0: not a gun).
KEPT_WEAPONS = ('v_fuel01',)
# Native vehicle gun classes a Gun may be made from (Weapon_VehicleMaser: the Maser's beam, V510_MASER_THUNDER01).
GUN_CLASSES = ('Weapon_VehicleShoot', 'Weapon_VehicleMaser')

# When a vehicle is unlocked (its row's owned bit, weapon_records.h flag bit 0; src/zw_unlock_logic.h):
#   start  owned by every save (WEAPONTABLE acquire 1, as V1..V3)
#   mid    a third of the EDF6 story cleared (any class, any difficulty: mission_packs.h ModeSave content 0)
#   late   two thirds cleared
#   final  its last mission cleared
UNLOCKS = ('start', 'mid', 'late', 'final')
# What the plugin runs for a vehicle beside the native hull (src/zw_vehicle_behavior.*): '' nothing,
# 'heat' the beam heats with firing and overheats (V7), 'fallout' a radiation zone where it is destroyed (V8).
BEHAVIORS = ('', 'heat', 'fallout')

# LockonTargetType of a gun that aims at air targets by itself: the auto-turret's air mark (common/edf/weapon.h kMarkAir;
# EDF6AutoTurret aims a Vehicle603_Flak's guns marked with it at fliers).
MARK_AIR = 7301.0


@dataclass(frozen=True)
class Gun:
    file: str                    # its Mods/WEAPON SGO, without extension
    stock: str                   # the stock WEAPON SGO it is made from, without extension
    values: dict[str, float]     # members set over the stock's (each one must be there)
    names: dict[str, str]        # lang -> its name (the vehicle HUD's weapon name)
    cls: str = 'Weapon_VehicleShoot'   # the stock weapon's xgs_scene_object_class (GUN_CLASSES)

    @property
    def path(self) -> str:
        """As the vehicle's setup names it (the stock setups spell weapon paths in lower case)."""
        return f'app:/weapon/{self.file.lower()}.sgo'

    @property
    def rel(self) -> str:
        return f'WEAPON/{self.file.upper()}.SGO'


@dataclass(frozen=True)
class ZwVehicle:
    row: str                     # its tools/zw_weapons.py row (category 400)
    sgo: str                     # its Mods/OBJECT SGO, without extension
    stock: str                   # the stock OBJECT SGO (the hull and its model), without extension
    cls: str                     # the stock hull's xgs_scene_object_class (CLASS_VTABLES)
    seats: int                   # the stock hull's vehicle_riding_position rows, the driver's included
    durability: float            # game_object_durability
    speed: float                 # setup[1][1], m/s
    force: float                 # setup[1][2]
    guns: tuple[Gun, ...]        # in the stock setup's weapon order, one for each stock weapon
    envelope: tuple[float, float, float]
    logistics_cost: int          # its price: logistics points a resupply of it costs, by vehicle (the maintainer 2026-11-28:
                                 # light ones cheap, heavy dear; HSTV-L / M247 about half the gauge, MBT-70 a full one)
    curve: str = 'blacker'       # src/support_level.h Curve its guns' damage goes by (CURVES)
    state: str = 'placeholder'   # its model: 'placeholder' (the stock hull's) until its own is made
    todo: tuple[str, ...] = field(default=())
    unlock: str = 'start'        # UNLOCKS
    capture: bool = False        # also unlocked by boarding an intact one left in a mission (src/zw_unlock.cpp)
    behavior: str = ''           # BEHAVIORS

    @property
    def rel(self) -> str:
        return f'OBJECT/{self.sgo.upper()}.SGO'

    @property
    def path(self) -> str:
        return f'app:/Object/{self.sgo.upper()}.SGO'

    @property
    def vtable(self) -> int:
        return CLASS_VTABLES[self.cls]

    @property
    def flying(self) -> bool:
        return self.cls in FLYING


# setup[0], the [durability, damage] multipliers: 1 / 1, every number below the vehicle's own (no growth yet: plan §3
# stage 4 is the unlock and growth).
MULTIPLIERS = (1.0, 1.0)

HSTVL_GUN = Gun('EDF6VC_ZW_HSTVL_GUN', 'V_505TANK_CANNON01', {
    # 75 mm ARES hypervelocity gun: a fast, flat shot, a quick reload, a small burst
    'AmmoCount': 40.0, 'FireInterval': 45.0, 'AmmoSpeed': 9.0, 'AmmoGravityFactor': 0.3, 'AmmoAlive': 200.0,
    'AmmoDamage': 220.0, 'AmmoExplosion': 4.0, 'AmmoSize': 1.0,
}, {'JA': '75mm ARES 超高速砲', 'EN': '75 mm ARES Hypervelocity Gun', 'CN': '75mm ARES 超高速砲',
    'KR': '75mm ARES 초고속포', 'SC': '75mm ARES 超高速炮'})

# M247's twin 40 mm L/70: the stock flak pair's barrels (one weapon each side), aimed at fliers by the radar (MARK_AIR)
M247_GUN = {'AmmoCount': 580.0, 'FireInterval': 10.0, 'AmmoSpeed': 15.0, 'AmmoGravityFactor': 0.2, 'AmmoAlive': 100.0,
            'AmmoDamage': 14.0, 'AmmoExplosion': 2.0, 'AmmoSize': 0.4, 'LockonTargetType': MARK_AIR}
M247_GUN_L = Gun('EDF6VC_ZW_M247_GUN_L', 'V603_FLAK_GUN01_L', M247_GUN, {
    'JA': '40mm L/70 連装機関砲（左）', 'EN': '40 mm L/70 Twin Autocannon (Left)', 'CN': '40mm L/70 雙聯機砲（左）',
    'KR': '40mm L/70 연장 기관포(좌)', 'SC': '40mm L/70 双联机炮（左）'})
M247_GUN_R = Gun('EDF6VC_ZW_M247_GUN_R', 'V603_FLAK_GUN01_R', M247_GUN, {
    'JA': '40mm L/70 連装機関砲（右）', 'EN': '40 mm L/70 Twin Autocannon (Right)', 'CN': '40mm L/70 雙聯機砲（右）',
    'KR': '40mm L/70 연장 기관포(우)', 'SC': '40mm L/70 双联机炮（右）'})

MBT70_GUN = Gun('EDF6VC_ZW_MBT70_GUN', 'V_505TANK_CANNON01', {
    # 152 mm XM150: a heavy shell, a wide burst, a slow reload
    'AmmoCount': 30.0, 'FireInterval': 150.0, 'AmmoSpeed': 4.0, 'AmmoGravityFactor': 0.8, 'AmmoAlive': 400.0,
    'AmmoDamage': 600.0, 'AmmoExplosion': 12.0, 'AmmoSize': 2.0,
}, {'JA': '152mm XM150 主砲', 'EN': '152 mm XM150 Main Gun', 'CN': '152mm XM150 主砲', 'KR': '152mm XM150 주포',
    'SC': '152mm XM150 主炮'})

OBJ279_GUN = Gun('EDF6VC_ZW_OBJ279_GUN', 'V_505TANK_CANNON01', {
    # 130 mm M-65: the heaviest shell, the slowest reload
    'AmmoCount': 30.0, 'FireInterval': 180.0, 'AmmoSpeed': 5.0, 'AmmoGravityFactor': 0.6, 'AmmoAlive': 400.0,
    'AmmoDamage': 800.0, 'AmmoExplosion': 10.0, 'AmmoSize': 1.8,
}, {'JA': '130mm M-65 主砲', 'EN': '130 mm M-65 Main Gun', 'CN': '130mm M-65 主砲', 'KR': '130mm M-65 주포',
    'SC': '130mm M-65 主炮'})
OBJ760_GUN = Gun('EDF6VC_ZW_OBJ760_GUN', 'V_505TANK_CANNON01', {
    # 73 mm 2A28 Grom: a light low-pressure gun, quick
    'AmmoCount': 60.0, 'FireInterval': 40.0, 'AmmoSpeed': 6.0, 'AmmoGravityFactor': 0.5, 'AmmoAlive': 200.0,
    'AmmoDamage': 180.0, 'AmmoExplosion': 5.0, 'AmmoSize': 1.0,
}, {'JA': '73mm 2A28 グロム低圧砲', 'EN': '73 mm 2A28 Grom Gun', 'CN': '73mm 2A28 雷霆低壓砲',
    'KR': '73mm 2A28 그롬 저압포', 'SC': '73mm 2A28 雷霆低压炮'})
AH56_GATLING = {'AmmoCount': 1500.0, 'FireInterval': 4.0, 'AmmoSpeed': 5.0, 'AmmoAlive': 50.0, 'AmmoDamage': 16.0,
                'AmmoExplosion': 1.0}
AH56_GUN_L = Gun('EDF6VC_ZW_AH56_GUN_L', 'V_506HELI_GATLING01_L', AH56_GATLING, {
    'JA': '30mm 機関砲（左）', 'EN': '30 mm Cannon (Left)', 'CN': '30mm 機砲（左）', 'KR': '30mm 기관포(좌)',
    'SC': '30mm 机炮（左）'})
AH56_GUN_R = Gun('EDF6VC_ZW_AH56_GUN_R', 'V_506HELI_GATLING01_R', AH56_GATLING, {
    'JA': '30mm 機関砲（右）', 'EN': '30 mm Cannon (Right)', 'CN': '30mm 機砲（右）', 'KR': '30mm 기관포(우)',
    'SC': '30mm 机炮（右）'})
AH56_TOW = Gun('EDF6VC_ZW_AH56_TOW', 'V_506HELI_MISSILE01', {
    'AmmoCount': 12.0, 'FireInterval': 240.0, 'AmmoDamage': 400.0, 'AmmoExplosion': 8.0,
}, {'JA': 'TOW 対戦車ミサイル', 'EN': 'TOW Anti-Tank Missiles', 'CN': 'TOW 反戰車飛彈', 'KR': 'TOW 대전차 미사일',
    'SC': 'TOW 反坦克导弹'})
SZHATIE_BEAM = Gun('EDF6VC_ZW_1K17_BEAM', 'V_510_MASER_THUNDER01', {
    # the beam's per-tick damage; heat (src/zw_heat_logic.h) raises it the longer it fires, then overheats
    'AmmoDamage': 8.0, 'AmmoCount': 7000.0,
}, {'JA': '1K17 レーザー照射器', 'EN': '1K17 Laser Projector', 'CN': '1K17 雷射照射器', 'KR': '1K17 레이저 조사기',
    'SC': '1K17 激光照射器'}, cls='Weapon_VehicleMaser')
TV8_GUN = Gun('EDF6VC_ZW_TV8_GUN', 'V_505TANK_CANNON01', {
    # 90 mm T208: a medium gun on a nuclear powerplant
    'AmmoCount': 45.0, 'FireInterval': 90.0, 'AmmoSpeed': 6.0, 'AmmoGravityFactor': 0.5, 'AmmoAlive': 300.0,
    'AmmoDamage': 450.0, 'AmmoExplosion': 7.0, 'AmmoSize': 1.4,
}, {'JA': '90mm T208 主砲', 'EN': '90 mm T208 Main Gun', 'CN': '90mm T208 主砲', 'KR': '90mm T208 주포',
    'SC': '90mm T208 主炮'})
VZ9_RING = {'AmmoCount': 2400.0, 'FireInterval': 3.0, 'AmmoSpeed': 6.0, 'AmmoAlive': 60.0, 'AmmoDamage': 20.0,
            'AmmoExplosion': 1.5}
VZ9_RING_L = Gun('EDF6VC_ZW_VZ9_RING_L', 'V_506HELI_GATLING01_L', VZ9_RING, {
    'JA': '環状速射砲（左）', 'EN': 'Ring Cannon (Left)', 'CN': '環形速射砲（左）', 'KR': '환형 속사포(좌)',
    'SC': '环形速射炮（左）'})
VZ9_RING_R = Gun('EDF6VC_ZW_VZ9_RING_R', 'V_506HELI_GATLING01_R', VZ9_RING, {
    'JA': '環状速射砲（右）', 'EN': 'Ring Cannon (Right)', 'CN': '環形速射砲（右）', 'KR': '환형 속사포(우)',
    'SC': '环形速射炮（右）'})
VZ9_PULSE = Gun('EDF6VC_ZW_VZ9_PULSE', 'V_506HELI_MISSILE01', {
    'AmmoCount': 30.0, 'FireInterval': 150.0, 'AmmoDamage': 350.0, 'AmmoExplosion': 10.0,
}, {'JA': '誘導弾', 'EN': 'Guided Rounds', 'CN': '誘導彈', 'KR': '유도탄', 'SC': '诱导弹'})

VEHICLES: tuple[ZwVehicle, ...] = (
    # V1 HSTV-L: the fast one (V505's 10 m/s -> 22; the stock 18 m/s requests' force)
    ZwVehicle('ZW_V1_HSTVL', 'EDF6VC_ZW_HSTVL', 'V505_TANK', 'Vehicle505_Tank', 1, durability=700.0, speed=22.0,
              force=0.025, guns=(HSTVL_GUN,), envelope=(1.61, 5.60, 2.63), logistics_cost=20),
    # V2 M247: early anti-air (the stock flak hull; its stock requests run 10..30 m/s)
    ZwVehicle('ZW_V2_M247', 'EDF6VC_ZW_M247', 'V603_FLAK', 'Vehicle603_Flak', 1, durability=550.0, speed=12.0,
              force=0.025, guns=(M247_GUN_L, M247_GUN_R), envelope=(1.56, 4.11, 3.76), logistics_cost=20),
    # V3 MBT-70: the balanced main tank, heavier and slower than V1 (the stock 14 m/s requests' force)
    ZwVehicle('ZW_V3_MBT70', 'EDF6VC_ZW_MBT70', 'V505_TANK', 'Vehicle505_Tank', 1, durability=1400.0, speed=14.0,
              force=0.0125, guns=(MBT70_GUN,), envelope=(1.61, 5.60, 2.63), logistics_cost=40,
              todo=('液压悬挂升降（需求原文 V3；原版无对应机制，要单独逆向立项，本阶段不带）',
                    '橡树棍（Shillelagh）炮射导弹')),
    # V4 Object 279: the heaviest tank, slow, a 130 mm gun; captured too (an intact one in the ruins)
    ZwVehicle('ZW_V4_OBJ279', 'EDF6VC_ZW_OBJ279', 'V505_TANK', 'Vehicle505_Tank', 1, durability=2600.0, speed=9.0,
              force=0.0125, guns=(OBJ279_GUN,), envelope=(1.61, 5.60, 2.63), logistics_cost=40, unlock='mid',
              capture=True,
              todo=('四履带底盘与抗核爆外形（真模型）',
                    '关卡里的完好 279：废墟布点要任务包放置（缴获判定与解锁已接，见 vehicler-v4v9-plan.md）')),
    # V5 Object 760: the hovercraft, the fastest, a light 73 mm gun
    ZwVehicle('ZW_V5_OBJ760', 'EDF6VC_ZW_OBJ760', 'V505_TANK', 'Vehicle505_Tank', 1, durability=650.0, speed=26.0,
              force=0.025, guns=(OBJ760_GUN,), envelope=(1.61, 5.60, 2.63), logistics_cost=25, unlock='mid',
              todo=('气垫漂移手感（原版无气垫底盘，用坦克底盘代替）', '炮射反坦克导弹')),
    # V6 AH-56 Cheyenne: the first aircraft (the stock helicopter's flight model), 30 mm gun pair and TOW missiles
    ZwVehicle('ZW_V6_AH56', 'EDF6VC_ZW_AH56', 'V506_HELI', 'Vehicle506_Helicopter', 1, durability=900.0, speed=0.0,
              force=0.0, guns=(AH56_GUN_L, AH56_GUN_R, AH56_TOW), envelope=(5.28, 8.32, 4.45), logistics_cost=35,
              unlock='mid', todo=('复合推力尾桨与机鼻 30mm 炮塔（真模型）', '直升机补给（送车兵空投）')),
    # V7 1K17 Szhatie: the laser, a beam that heats the longer it fires and overheats (src/zw_heat_logic.h)
    ZwVehicle('ZW_V7_1K17', 'EDF6VC_ZW_1K17', 'V510_MASER', 'Vehicle510_Maser', 1, durability=1100.0, speed=8.0,
              force=0.025, guns=(SZHATIE_BEAM,), envelope=(5.88, 20.97, 12.42), logistics_cost=30, unlock='late',
              behavior='heat',
              todo=('真模型：原版 Maser 车体很大（约 42m 长），1K17 应为履带车大小', '热度 HUD 条')),
    # V8 TV-8: the nuclear tank, very tough; destroyed it leaves a small radiation zone (src/zw_vehicle_behavior.cpp)
    ZwVehicle('ZW_V8_TV8', 'EDF6VC_ZW_TV8', 'V505_TANK', 'Vehicle505_Tank', 1, durability=3200.0, speed=11.0,
              force=0.0125, guns=(TV8_GUN,), envelope=(1.61, 5.60, 2.63), logistics_cost=40, unlock='late',
              behavior='fallout', todo=('单体舱炮塔外形（真模型）', '辐射区的地面特效（现为周期爆炸伤害）')),
    # V9 VZ-9 Avrocar: the saucer, climbs and hovers (the helicopter's flight model)
    ZwVehicle('ZW_V9_VZ9', 'EDF6VC_ZW_VZ9', 'V506_HELI', 'Vehicle506_Helicopter', 1, durability=1500.0, speed=0.0,
              force=0.0, guns=(VZ9_RING_L, VZ9_RING_R, VZ9_PULSE), envelope=(5.28, 8.32, 4.45), logistics_cost=40,
              unlock='final', todo=('360° 环形炮：原版直升机炮只朝机头，环形射界要逆向立项', '飞碟真模型')),
)
BY_ROW = {v.row: v for v in VEHICLES}


def of_row(row_id: str) -> ZwVehicle | None:
    return BY_ROW.get(row_id)


def files() -> list[str]:
    """Every Mods file the vehicles are: their OBJECT SGOs and their guns' WEAPON SGOs (each once)."""
    out: list[str] = []
    for v in VEHICLES:
        for rel in (v.rel, *(g.rel for g in v.guns)):
            if rel not in out:
                out.append(rel)
    return out
