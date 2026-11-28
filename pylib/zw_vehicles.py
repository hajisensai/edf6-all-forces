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
CLASS_VTABLES: dict[str, int] = {'Vehicle505_Tank': 0x17DADB0, 'Vehicle603_Flak': 0x17DC620}

# LockonTargetType of a gun that aims at air targets by itself: the auto-turret's air mark (common/edf/weapon.h kMarkAir;
# EDF6AutoTurret aims a Vehicle603_Flak's guns marked with it at fliers).
MARK_AIR = 7301.0


@dataclass(frozen=True)
class Gun:
    file: str                    # its Mods/WEAPON SGO, without extension
    stock: str                   # the stock WEAPON SGO it is made from, without extension
    values: dict[str, float]     # members set over the stock's (each one must be there)
    names: dict[str, str]        # lang -> its name (the vehicle HUD's weapon name)

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

    @property
    def rel(self) -> str:
        return f'OBJECT/{self.sgo.upper()}.SGO'

    @property
    def path(self) -> str:
        return f'app:/Object/{self.sgo.upper()}.SGO'

    @property
    def vtable(self) -> int:
        return CLASS_VTABLES[self.cls]


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
