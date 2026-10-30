"""The human side's units of X-Morph: Defense ported to EDF6 as the player's allies (docs/xmorph-port.md): the one table
every X-Morph unit is registered in. A unit's row says where its model comes from (its entity blueprint in the packs),
which EDF chassis carries it and whose weapons it borrows; tools/prep_xmorph.py turns the model into the unit's model
folder (models/xm_<key>/, the model pack: not in the public repository), and every registry that lists vehicles takes
the ported rows from here (ported()), so a new unit is a row here and its folder, nothing else.

    UNITS: dict[key, Unit]        every human unit the game has, ported or not
    ported(family) -> [Unit]      the rows whose status is 'ported' (of a family)
    folder(key) -> 'xm_<key>'     its model folder (obj_model.model_dir)

Registration is by status, deployment by the folder (CLAUDE.md: no stand-in for a missing resource). A ported row is in
every registry on every machine (src/calls.inc, the spawn catalog, the ledger's owners, the build groups: generated files
never depend on which model folders a machine has; CI has none of them); where its folder is not there the installer
says so in one line and deploys nothing of it (tools/make_xmorph.py build: no files; tools/call_weapons.py undeployed:
its request row a placeholder; testrange/gen.py without_missing: off the range).

Who reads the rows (one loop each; tests/xmorph_registry_guard.py holds every one of them to ported()):
    pylib/vcobjects.py            GROUND_VEHICLES (derived from its `like`'s: the native gun pair; a car's the sedan's
                                  unarmed request) and TRACKED (the V603's)
    pylib/wt_tracked.py           FOLDERS (folder())
    pylib/ledger.py               OWNERS          pylib/buildcache.py   GROUPS, COCKPIT_GROUPS, MODEL_INPUTS, generator()
    pylib/cockpit_fits_plugin.py  FITS (eye)      tools/calls.py        CALLS (after), KINDS (texts), GROUND_NOTES_BY_KIND
    tools/make_tracked_combat.py  LABELS, GUNS, NAMES, WT_ONLY and the kind sets over its `like`'s
    tools/make_xmorph.py          the one generator of every unit (builder(key); CARS: the installer's car loop);
                                  tools/build_release.py RELEASE_MODELS
    testrange/gen.py              GROUND_MISSION (the range's own tables follow vcobjects.TRACKED); a car:
                                  testrange/catalog.py INSTALLER_VEHICLES (placed as its own SGO, as the sedan)

Families (the chassis an EDF6 class drives; tools/prep_xmorph.py has one preparer per family):
    tracked      V603 (Vehicle603_Flak) through pylib/wt_tracked.py, like the War Thunder vehicles. It fires its
                 `like`'s native gun pair (the two V603 holders: GUNS / NAMES / the shell over the `like`'s); the
                 `like`'s stores (missiles, a second belt) are vehicle-specific store rows (vcobjects STORES, with
                 their own muzzles) and are not inherited
    wheeled      V603 too, the same way (tools/prep_xmorph.py wheeled: its real wheels on the V603's wheel stations;
                 it turns as the V603, skid steering); the Grape's wheeled class (Vehicle401_Striker) has one weapon
                 holder and a skeleton of its own, which no generator here builds a model on
    car          an unarmed vehicle on the sedan's V512 (Vehicle_Car: its `like` is 'sedan'; pylib/car_model.py, through
                 tools/prep_xmorph.py car): its own wheels on the V512's wheel bones (steered by the front pair),
                 the driver's seat alone, no gun; the civilian rows' cars come this way too
    mech         a walker on a stock one (pylib/xm_walker.py: the light mech on the Begaruta, the heavy on the Proteus Mk2;
                 the Balam is four times the heavy mech's size), its model retargeted onto the host's skeleton
                 (pylib/xm_rig.py, tools/prep_xmorph.py mech), its weapons on the host's holders
    emplacement  a fort's gun on pylib/emplacement_model.py's fortified base (the V603, tools/prep_xmorph.py emplacement)
    heli         the stock V506 heli in its own model (pylib/xm_heli.py, pylib/xm_air_model.py; tools/prep_xmorph.py heli)
    jet          the plugin's own aircraft in its own model (pylib/vcobjects.py XM_JETS, pylib/xm_air_model.py; tools/
                 prep_xmorph.py jet): src/jet.cpp flies it (kBodies), the player boards it (kBoardable)
    infantry     a support soldier kind (src/support_call.h xmInfantry) on the Ranger's body (pylib/xm_infantry.py,
                 tools/prep_xmorph.py infantry), not a request
    boss         planned: the game's bosses (parts each: xm_model.assemble), until their chassis is known (docs/
                 xmorph-bosses.md); a boss whose chassis is one of the above is ported as that family (the JAGUAR
                 Type S, the highway_tank boss: tracked)
    civilian     planned: the maps' traffic and bystanders (their meshes through an inherited blueprint)
    structure    planned: buildings without a weapon (the barracks): the maps' props
"""
from __future__ import annotations

from dataclasses import dataclass, field

FAMILIES = ('tracked', 'wheeled', 'car', 'mech', 'emplacement', 'heli', 'jet', 'infantry', 'boss', 'civilian', 'structure')
LANGS = ('SC', 'CN', 'JA', 'EN')   # tools/calls.py KINDS' languages (KR reads EN)
# Where every request's text says its model is from (each language's), after the unit's own description.
SOURCE = {'SC': '外观取自 EXOR Studios 的游戏《X-Morph: Defense》（人类一方的单位）。',
          'CN': '外觀取自 EXOR Studios 的遊戲《X-Morph: Defense》（人類一方的單位）。',
          'JA': '外観は EXOR Studios のゲーム『X-Morph: Defense』の人類側ユニットのモデル。',
          'EN': "Model: the human side's unit from EXOR Studios' X-Morph: Defense."}


# How the game's numbers become EDF's (docs/xmorph-port.md 「武器」): the game's fire_rate is the seconds from one
# shot (or burst) to the next, its FirePattern burst_rows shots burst_rate s apart; a projectile's Damage value by its
# type (DAMAGE: the anti-core / ground rounds the battle tank's 5 = 600, the anti-player ones, which in the game hit the
# player's light flyers, a quarter of that scale), its Area radius x BLAST m.
# The units' size in EDF against the game's (docs/xmorph-port.md 「比例」): X-Morph draws its army for a top-down view,
# a battle tank 5.5 m wide and 12.7 m long with its gun (its civilian cars at their real 4.4 m), against EDF's Blacker's
# 3.2 x 9.2 m and a real battle tank's 3.7 x 9.8 m; at 0.7 it is 3.8 x 8.9 m, 2.4 m to its turret roof, and the heavy
# tank 12.5 m, the Apocalypse's class. Every model folder is made at this size (tools/prep_xmorph.py); the rows' `eye`
# and `camera` stay in the game's metres, read through eye() / camera().
SCALE = 0.7
EMPLACEMENT_BASE = 1.3   # m: an emplacement's model stands on its base this high (emplacement_model.BASE_TOP)

DAMAGE = {'anti_core': 120.0, 'artillery': 120.0, 'laser': 120.0, 'anti_player': 30.0, 'emp': 30.0}
BLAST = 1.5


@dataclass(frozen=True)
class Arm:
    """A unit's weapon besides its main one (the game's other Weapon blocks: missile pods, machine-gun turrets, the EMP
    flak), on a fire key of its own (vcobjects STORES <UNIT>_<KEY>, Store.key / auto, src/payload.cpp OwnKey: fired with
    the main gun and the unit's other arms, each on its key, never cycled with them). It leaves from the game's
    own muzzles (model.json muzzles 'xm_<key>', tools/prep_xmorph.py) on `bone`: the turret's ('cannon_main', a missile
    flies to its lock) or the gun's ('cannon_l': a gun fires along the main gun's aim; the game's own turret aims it
    by itself)."""
    key: str                 # STORES <UNIT KEY>_<KEY>, its muzzle slot xm_<key>
    weapons: tuple[str, ...]  # the blueprint's Weapon blocks it stands for (their Muzzle attachments)
    role: str                # vcobjects Store.role: 'air' / 'ground' (a missile), 'gun'
    names: tuple[str, str, str, str, str]   # en, ja, cn, sc, kr
    rounds: int
    bone: str = 'cannon_main'
    missile: dict = field(default_factory=dict)   # a missile's vcobjects.Missile fields (look: (model, length))
    shell: tuple[str, str, str] = ()              # a gun's (stock template, its AmmoClass, round kind): vcobjects.Shell
    numbers: dict = field(default_factory=dict)   # a gun's SGO numbers over the template's (Shell.extra)
    fire: int = 2            # its fire key (vcobjects Store.key: 2 the second trigger, 3..6 VehicleFire<n>Key), one a unit;
                             # 0 with a `seat` (that seat's trigger fires it)
    auto: bool = False       # an auto turret (vcobjects Store.auto): it fires itself with the auto turrets on
    # Its own gunner seat (vcobjects Store.seat, docs/multi-weapon-keys.md「独立座位」): 0 none (on the driver's key); n
    # the vehicle's seat n, one arm a seat, numbered from 1 in arms order. Needs `turret`.
    seat: int = 0
    # Its own turret (vcobjects Turret: the limits; its bones are TURRET_BONES(key), tools/prep_xmorph.py makes them of the
    # blueprint's turret for its weapon: its yaw bone's mesh and its pitch bone's), {} none: it rides `bone`.
    turret: dict = field(default_factory=dict)

    # A jet's store (Unit.air 'stores', family jet): (the vcobjects STORES kind it is made like, its Missile / Bomb
    # fields over that one's): the like's mass, drag and look, the game's damage, blast and cadence.
    store: tuple = ()

    @property
    def holder_bone(self) -> str:
        """The bone its holder and muzzles ride: its own turret's pitch bone, else `bone`."""
        return turret_bones(self.key)[1] if self.turret else self.bone


def turret_bones(arm_key: str) -> tuple[str, str]:
    """An arm's own turret's two model bones (yaw, pitch): leaves of the hull's `body` (tools/prep_xmorph.py)."""
    return f'xm_{arm_key}', f'xm_{arm_key}_gun'


@dataclass(frozen=True)
class HeliArm:
    """One of an aircraft's V506 weapons (Unit.air): a stock 506 weapon SGO under the unit's name with the game's numbers
    over it (`numbers`: SGO values), fired from the blueprint's muzzle attachments (`muzzles`: the left holder's and the
    right's for the gun pair, any number for the missile: their middle)."""
    template: str                     # Root.cpk WEAPON/<template>
    muzzles: tuple[str, ...]
    names: tuple[str, str, str, str, str]  # en, ja, cn, sc, kr (vcobjects._local's order, as Arm.names; kr: the English)
    numbers: dict = field(default_factory=dict)


@dataclass(frozen=True)
class Unit:
    key: str                 # the EDF-side kind (make_tracked_combat kind, ledger owner, build group): xm_<entity name>
    entity: str              # entities/units/<entity>.ent in the packs
    family: str
    like: str = ''           # the existing kind whose weapons / SGO tuning it starts from (tracked: make_tracked_combat)
    status: str = 'planned'  # 'ported': built and installed; 'planned': catalogued only
    title_en: str = ''       # the game's own English name (gui localisation), the EDF names derive from it
    # The EDF side, a ported row's (check() holds a ported row to having them all):
    texts: dict = field(default_factory=dict)  # lang -> (request name, what it does): tools/calls.py KINDS (+ SOURCE)
    gun: tuple[str, ...] = ()  # its gun's name (EN, SC, JA, CN): make_tracked_combat NAMES
    guns: dict = field(default_factory=dict)   # the gun's numbers over the `like`'s (make_tracked_combat GUNS)
    shell: str = ''          # its round's look and sound (a WEAPON SGO) where not the `like`'s (make_tracked_combat SHELL_OF)
    alternate: bool | None = None   # its two holders fire in turn (vcobjects GroundVehicle.alternate); None: the like's
    # Its gun pair's round is a homing missile (make_tracked_combat MISSILE_SHELL: the Titan's side missile, its look
    # and lock-on, as the Patriot's) where its `like`'s is a gun's
    homing: bool = False
    traverse: float = 0.0    # its turret's stops, deg either side (make_tracked_combat TURRET_LIMITS); 0: free
    cab: str = ''            # a car's driver's cockpit kit (pylib/cockpit_kits_ground.py KITS; cockpit_fits_plugin)
    arms: tuple[Arm, ...] = ()      # its other weapons (Arm)
    # (camera, eye: measured on the model at the game's size, metres; read through camera() / eye(), at SCALE)
    camera: dict = field(default_factory=dict)  # its vehicle camera over the `like`'s (make_tracked_combat WT_ONLY)
    # the gunner's sight camera on the turret (cockpit_fits_plugin, remote_display), m; a car's: its driver's eye
    eye: tuple[float, ...] = ()
    # The bone the eye rides: the turret (cannon_main), or the gun's pitch hinge (cannon_l) where the raised gun or its
    # mount would swing through the aim line of an eye on the turret (tests/gun_camera_sweep_guard.py, -5..60 deg).
    eye_bone: str = 'cannon_main'
    durability: float = 0.0  # vcobjects GroundVehicle.durability
    reload: int = 0          # its request (tools/calls.py Call): ReloadTime base
    level: float = 0.0       # ... and the weapon table level
    # The call id its request row comes right after in tools/calls.py CALLS: rows are only ever appended (saves keep
    # weapons by row), so a newly ported unit names the call that ended CALLS when it was ported, never an earlier one.
    after: str = ''
    # An aircraft's (family heli: pylib/xm_heli.py) weapons on the V506's holders: 'guns' the pair the gun trigger fires
    # together (veh+0x2020: holders 0 and 1), 'missile' the missile trigger's (holder 2); each a HeliArm. 'seats': the
    # passenger seats it carries over the V506's (the transport's, tools/make_jets.py with_passenger_seats).
    # A jet's (family jet: pylib/vcobjects.py XM_JETS, flown by the plugin as its other aircraft): 'mark' (src/jet_internal.h
    # kBodies, its body 'body' flying its 'role' row), 'mass' (kg, clean: vcobjects JET_MASSES, by a real jet its size),
    # 'ram' (src/playerjet_kinds.h kBoardable: m, half its size), 'guns'
    # (the gun pair's muzzle attachments, left and right) and 'gun' (their SGO numbers over the jets' gatling: empty,
    # the stock jet guns: the unit has no gun), 'stores' (Arm, each with `store`: on the missile trigger, cycled).
    air: dict = field(default_factory=dict)


def _u(entity: str, family: str, like: str = '', status: str = 'planned', title: str = '', **edf) -> Unit:
    return Unit('xm_' + entity.split('/')[-1], entity, family, like, status, title, **edf)


UNITS: dict[str, Unit] = {u.key: u for u in (
    # ground: tracked hulls
    # The battle tank: one turret gun, no missiles. Its `like` is the Apocalypse, the one V603 kind whose gun is a direct-
    # fire tank gun (the Blacker's shell, make_tracked_combat CANNON_SHELL; the PzH 2000 / Sturmtiger lob the howitzer's
    # round, the 2S6 / BMPT fire autocannon belts); the Apocalypse's missile pods are not the battle tank's and not
    # inherited (see `tracked` above). Both native holders sit on its one barrel, in turn (as the PzH 2000's): a round
    # every half FireInterval; a medium tank's numbers under the superheavy's.
    _u('ground/medium_tank_regular', 'tracked', 'apocalypse', 'ported', 'Battle Tank',
       texts={'SC': ('X-Morph 战斗坦克', '请求一辆人类军队的战斗坦克：中型坦克，炮塔上一门坦克炮为主攻击，每秒一发（与原作相同）；'
                                     '装甲中等。'),
              'CN': ('X-Morph 戰鬥坦克', '請求一輛人類軍隊的戰鬥坦克：中型坦克，砲塔上一門坦克砲為主攻擊，每秒一發（與原作相同）；'
                                     '裝甲中等。'),
              'JA': ('X-Morph バトルタンク', '人類軍のバトルタンクを要請する。中戦車で、砲塔の戦車砲（1 秒に 1 発、原作どおり）を主攻撃とする。'
                                       '装甲は中程度。'),
              'EN': ('X-Morph Battle Tank', "Requests the human army's battle tank: a medium tank, its turret's tank gun "
                                            "on the primary attack, a round a second (as in the game); medium armour.")},
       gun=('Tank Gun', '坦克炮', '戦車砲', '坦克砲'),
       # main_weapon: fire_rate 1, one round (enemy_bullet_medium_anti_core: 5 anti_core, no Area). Its active_armor
       # (a laser that shoots down incoming rounds, BeamWeaponDef on att_active_armor_muzzle) has no EDF counterpart.
       guns={'AmmoCount': 40.0, 'AmmoDamage': 600.0, 'AmmoExplosion': 0.0, 'FireInterval': 120.0},
       # xm_medium_tank_regular at the game's size: hull y 0..2.17, 9.6 m long; the turret roof y 3.0..3.4 (z -3.2..2.5; a mast
       # to y 5.2 at z -1..0), the gun's axis y 2.25, its muzzle z 7.5: the sight camera over the roof's front.
       camera={'model_eye': (0., 3.2, 0.5), 'model_look_at': (0., 2.8, 9.), 'third_person_distance': 11.,
               'third_person_height': 4.6},
       eye=(0.0, 3.55, 2.0), eye_bone='cannon_l', durability=2500.0, reload=10000, level=1.5, after='EDF6VC_CALL_SORTIE_GUIDED'),
    # The advanced tank: a laser cannon on its turret (the game's enemy_laser_medium, one a second), twice the battle
    # tank's armour (350 against 170) and a shield (not ported). The Brute's laser cannon's beam as its round
    # (make_tracked_combat SHELL_OF, as the laser turret's): straight, fast, through what it hits.
    _u('ground/medium_tank_advanced', 'tracked', 'apocalypse', 'ported', 'Advanced Tank',
       texts={'SC': ('X-Morph 先进坦克', '请求一辆人类军队的先进坦克：炮塔上一门激光炮为主攻击，与原作一样有目标就持续照射，光束穿透目标；'
                                     '装甲约为战斗坦克的两倍。'),
              'CN': ('X-Morph 先進坦克', '請求一輛人類軍隊的先進坦克：砲塔上一門雷射砲為主攻擊，與原作一樣有目標就持續照射，光束穿透目標；'
                                     '裝甲約為戰鬥坦克的兩倍。'),
              'JA': ('X-Morph アドバンスドタンク', '人類軍のアドバンスドタンクを要請する。砲塔のレーザー砲（原作どおり目標がいる間照射し続ける、貫通）を'
                                          '主攻撃とする。装甲はバトルタンクの約 2 倍。'),
              'EN': ('X-Morph Advanced Tank', "Requests the human army's advanced tank: its turret's laser cannon on the "
                                              'primary attack, a beam held on its target as in the game, through what it hits; about '
                                              "twice the battle tank's armour.")},
       gun=('Laser Cannon', '激光炮', 'レーザー砲', '雷射砲'), shell='V_506HELI_LASERCANNON01_L.SGO',
       # main_weapon: BeamWeaponDef, fire_duration 100 (on while it has a target), enemy_laser_medium (10, reach 10:
       # the tank guns' 8): a stream of pulses every 4 frames, 1200 a second (the battle tank's gun 600)
       guns={'AmmoCount': 900.0, 'AmmoDamage': 80.0, 'AmmoSpeed': 40.0, 'AmmoAlive': 20.0, 'AmmoSize': 0.6,
             'AmmoGravityFactor': 0.0, 'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0, 'FireInterval': 8.0,
             'FireAccuracy': 0.001},
       # xm_medium_tank_advanced at the game's size: 12.8 m long, the turret y 3.3..5.5 (its roof ~5.2), z -4.5..1.7; the gun's axis
       # y 4.3, its muzzle z 7.0
       camera={'model_eye': (0., 5.0, -0.3), 'model_look_at': (0., 4.6, 9.), 'third_person_distance': 11.,
               'third_person_height': 5.0},
       eye=(0.0, 5.5, 1.35), eye_bone='cannon_l', durability=5000.0, reload=14000, level=2.5, after='EDF6VC_CALL_XM_MEDIUM_TANK_REGULAR'),
    # The anti-air tank: twin flak guns either side of its turret (enemy_flak_medium, bursts) and AA missiles (not
    # ported); light armour. The Tunguska's 30 mm belt with a flak burst: fewer, harder-hitting rounds that burst.
    _u('ground/medium_tank_aa', 'tracked', 'tunguska', 'ported', 'Anti-Air Tank',
       texts={'SC': ('X-Morph 防空坦克', '请求一辆人类军队的防空坦克：炮塔两侧的高射炮为主攻击，每两秒一发、在目标附近大范围爆炸，专打空中目标；副武器为炮塔上的导弹（两发一组，自动追踪地面目标）；'
                                     '装甲较薄。'),
              'CN': ('X-Morph 防空坦克', '請求一輛人類軍隊的防空坦克：砲塔兩側的高射砲為主攻擊，每兩秒一發、在目標附近大範圍爆炸，專打空中目標；副武器為砲塔上的飛彈（兩發一組，自動追蹤地面目標）；'
                                     '裝甲較薄。'),
              'JA': ('X-Morph 対空戦車', '人類軍の対空戦車を要請する。砲塔両側の高射砲（2 秒に 1 発、目標付近で広く炸裂）を主攻撃とし、空中目標に強い。副武器は砲塔のミサイル（2 発ずつ、地上目標を追尾）。'
                                     '装甲は薄い。'),
              'EN': ('X-Morph Anti-Air Tank', "Requests the human army's anti-air tank: the flak guns either side of its "
                                              'turret on the primary attack, a shell every 2 s bursting wide by its '
                                              'target, for air targets; its turret\'s missiles on the secondary (in '
                                              'pairs, homing on ground targets); light armour.')},
       gun=('Twin Flak Gun', '双联高射炮', '連装高射砲', '雙聯高射砲'),
       # main_weapon: InstantWeaponDef, fire_rate 2, enemy_flak_medium (BombAmmoDef: bursts 0.75 s on, 25 anti_player,
       # Area 12): a fast shell bursting wide, one every 2 s (the two pods in turn)
       guns={'AmmoCount': 60.0, 'AmmoDamage': 750.0, 'AmmoSpeed': 30.0, 'AmmoExplosion': 18.0, 'FireInterval': 240.0,
             'AmmoIsPenetration': 0.0}, alternate=True,
       arms=(Arm('missile', ('core_weapon',), 'ground', ('Missiles', 'ミサイル', '飛彈', '导弹', '미사일'), 24,
                 # core_weapon: MissileWeaponDef, fire_rate 2, two 0.4 s apart: enemy_missile_anti_core_aa_towers_medium
                 # (speed 40, 4 + Area 4, homing on the core / the towers)
                 missile={'model': 'AA tower missile', 'look': ('bullet_rpg', 1.5), 'burn': 2.0, 'top': 300.0,
                          'accel': 250.0, 'max_g': 25.0, 'nav': 3.5, 'life': 5.0, 'damage': 480.0, 'blast': 6.0,
                          'lock_range': 1200.0, 'lock_cone': 0.6, 'lock_time': 20.0, 'burst': 2.0, 'burst_gap': 24.0,
                          'interval': 120.0}),),
       # xm_medium_tank_aa at the game's size: 8.8 m long; the turret at the back (z -3.0..-1.95, roof y ~6.0), its two guns at
       # x +-1.5, y 4.0
       camera={'model_eye': (0., 5.6, -2.4), 'model_look_at': (0., 5.0, 7.), 'third_person_distance': 8.,
               'third_person_height': 5.6},
       eye=(0.0, 6.15, -2.1), durability=1500.0, reload=10000, level=1.8, after='EDF6VC_CALL_XM_MEDIUM_TANK_ADVANCED'),
    # The heavy tank: a twin heavy gun (enemy_bullet_big_anti_core, a pair every 3 s), machine-gun and missile turrets
    # (not ported), seven times the battle tank's armour (1200). The Apocalypse's twin gun, the two barrels in turn.
    _u('ground/large_tank', 'tracked', 'apocalypse', 'ported', 'Heavy Tank',
       texts={'SC': ('X-Morph 重型坦克', '请求一辆人类军队的重型坦克：炮塔上的双管重炮为主攻击，两根炮管每三秒齐射一次；机枪塔（十发连射）是独立炮手座，坐上去自己瞄准开火，没人坐时是自动炮塔；'
                                     '防空导弹（五发连射）和车头火箭各用一个开火键、与主炮同时发射；装甲厚重。'),
              'CN': ('X-Morph 重型坦克', '請求一輛人類軍隊的重型坦克：砲塔上的雙管重砲為主攻擊，兩根砲管每三秒齊射一次；機槍塔（十發連射）是獨立砲手座，坐上去自己瞄準開火，沒人坐時是自動砲塔；'
                                     '防空飛彈（五發連射）和車頭火箭各用一個開火鍵、與主砲同時發射；裝甲厚重。'),
              'JA': ('X-Morph 重戦車', '人類軍の重戦車を要請する。砲塔の連装重砲（3 秒ごとに 2 門斉射）を主攻撃とする。機銃塔（10 発連射）は独立した砲手席で、乗れば自分で狙って撃ち、無人なら自動砲塔になる。'
                                    '対空ミサイル（5 発連射）と車体前部のロケットはそれぞれ専用の発射キーで主砲と同時に撃てる。装甲は厚い。'),
              'EN': ('X-Morph Heavy Tank', "Requests the human army's heavy tank: its turret's twin heavy gun on the primary "
                                           'attack, both barrels every 3 s; its machine-gun turret (bursts of ten) is a gunner seat of '
                                           'its own, an auto turret with nobody in it; AA missiles (bursts of five) and '
                                           'front rockets each on its own fire key with the gun; heavy armour.')},
       gun=('Twin Heavy Gun', '双管重炮', '連装重砲', '雙管重砲'),
       # main_gun: fire_rate 3, both barrels (enemy_bullet_big_anti_core: 30 anti_core, no Area): the pair every 3 s
       # (the game's 0.25 s between them: here together)
       guns={'AmmoCount': 40.0, 'AmmoDamage': 3600.0, 'AmmoExplosion': 0.0, 'FireInterval': 180.0}, alternate=False,
       arms=(Arm('mg', ('machinegun',), 'gun', ('Machine gun', '機銃', '機槍', '机枪', '기관총'), 600, 'cannon_l',
                 # machinegun: fire_rate 1.5, ten 0.07 s apart (enemy_bullet_small_fast: speed 100, 5 anti_player)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 150.0, 'AmmoSpeed': 20.0, 'AmmoAlive': 60.0, 'FireInterval': 90.0,
                          'FireBurstCount': 10.0, 'FireBurstInterval': 4.0},
                 # its own turret (machinegun_turret: turret_2 over att_machinegun) and gunner seat; empty, an auto
                 # turret. The missile turret keeps its key: an unmanned launcher has nobody's lock to fire on.
                 fire=0, auto=True, seat=1, turret={'low': -10.0, 'high': 60.0, 'yaw_rate': 120.0, 'pitch_rate': 90.0}),
             Arm('aam', ('missile_turret',), 'air', ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 40,
                 # missile_turret: fire_rate 4, five 0.3 s apart (enemy_missile_anti_aa_small: speed 50, 8 anti_player)
                 missile={'model': 'AA missile', 'look': ('bullet_missile', 1.2), 'burn': 2.0, 'top': 350.0,
                          'accel': 300.0, 'max_g': 35.0, 'nav': 4.0, 'life': 5.0, 'damage': 240.0, 'blast': 3.0,
                          'lock_range': 1500.0, 'lock_cone': 0.8, 'lock_time': 15.0, 'burst': 5.0, 'burst_gap': 18.0,
                          'interval': 240.0}, fire=3, auto=True),
             Arm('rockets', ('front_missile_1', 'front_missile_2'), 'air', ('Rockets', 'ロケット', '火箭', '火箭', '로켓'), 40,
                 # front_missile_1 / _2: fire_rate 2, two 0.4 s apart each (enemy_rocket_small: speed 60, accel 50,
                 # turning 15 deg/s: barely guided; 10 anti_player): both pods, four every 2 s
                 missile={'model': 'Rocket', 'look': ('bullet_rocket', 1.0), 'burn': 1.5, 'top': 400.0,
                          'accel': 300.0, 'max_g': 6.0, 'nav': 2.0, 'life': 4.0, 'damage': 300.0, 'blast': 3.0,
                          'lock_range': 1200.0, 'lock_cone': 0.4, 'lock_time': 20.0, 'burst': 2.0, 'burst_gap': 24.0,
                          'interval': 120.0}, fire=4)),
       # xm_large_tank at the game's size: 17.8 m long, the turret y 3.2..7.5 (roof ~6.7), z -2.3..3.3; the guns at x +-1.07,
       # y 4.55, their muzzles z 10.8
       camera={'model_eye': (0., 6.4, 0.6), 'model_look_at': (0., 5.2, 14.), 'third_person_distance': 15.,
               'third_person_height': 6.8},
       eye=(0.0, 6.85, 2.9), durability=9000.0, reload=18000, level=3.5, after='EDF6VC_CALL_XM_MEDIUM_TANK_AA'),
    # The EMP tank: the heavy tank's twin gun on a bigger hull with EMP flak, gun turrets and missiles (not ported), the
    # most armour of the tracked units (2000).
    _u('ground/large_tank_emp', 'tracked', 'apocalypse', 'ported', 'EMP Tank',
       texts={'SC': ('X-Morph EMP 坦克', '请求一辆人类军队的 EMP 坦克：比重型坦克更大的车体，炮塔上的双管重炮为主攻击，每三秒两门齐射两轮；'
                                      '副武器与主炮同时发射： EMP 高射弹与导弹塔（二十发连射）各用一个开火键；两座防空机炮塔（十五发连射）各有炮手座位、各自转向，无人时自动开火；履带单位中装甲最厚。'),
              'CN': ('X-Morph EMP 坦克', '請求一輛人類軍隊的 EMP 坦克：比重型坦克更大的車體，砲塔上的雙管重砲為主攻擊，每三秒兩門齊射兩輪；'
                                      '副武器與主砲同時發射： EMP 高射彈與飛彈塔（二十發連射）各用一個開火鍵；兩座防空機砲塔（十五發連射）各有砲手座位、各自轉向，無人時自動開火；履帶單位中裝甲最厚。'),
              'JA': ('X-Morph EMP 戦車', '人類軍の EMP 戦車を要請する。重戦車より大きな車体で、砲塔の連装重砲（3 秒ごとに 2 門斉射を 2 回）を'
                                      '主攻撃とする。EMP 炸裂弾とミサイル塔（20 発連射）はそれぞれ専用の発射キーで主砲と同時に撃てる。対空機関砲塔 2 基（15 発連射）はそれぞれ砲手席を持ち個別に旋回し、無人なら自動で撃つ。装甲は装軌車両で最も厚い。'),
              'EN': ('X-Morph EMP Tank', "Requests the human army's EMP tank: a hull bigger than the heavy tank's, its "
                                         "turret's twin heavy gun on the primary attack, both barrels twice every 3 s; "
                                         'each on its own fire key with the gun, EMP flak and a missile turret (bursts of '
                                         'twenty); two AA gun turrets (bursts of fifteen), each with its own gunner seat '
                                         'and turning on its own, firing by themselves when unmanned; the heaviest armour '
                                         'of the tracked units.')},
       gun=('Twin Heavy Gun', '双管重炮', '連装重砲', '雙管重砲'),
       # main_gun: fire_rate 3, muzzles_per_fire 2, two 0.25 s apart (enemy_bullet_big_anti_core: 30, no Area): both
       # barrels together, twice, every 3 s
       guns={'AmmoCount': 60.0, 'AmmoDamage': 3600.0, 'AmmoExplosion': 0.0, 'FireInterval': 180.0,
             'FireBurstCount': 2.0, 'FireBurstInterval': 15.0}, alternate=False,
       arms=(Arm('emp', ('emp_weapon',), 'gun', ('EMP flak', 'EMP 炸裂弾', 'EMP 高射彈', 'EMP 高射弹', 'EMP 작렬탄'), 12, 'cannon_l',
                 # emp_weapon: InstantWeaponDef, fire_rate 10 (enemy_flak_emp: 50 emp, Area 9) from the main barrel
                 shell=('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                 numbers={'AmmoDamage': 1500.0, 'AmmoExplosion': 13.5, 'AmmoSpeed': 30.0, 'FireInterval': 600.0,
                          'FireBurstCount': 1.0}),
             # aa_left_turret / aa_right_turret: fire_rate 5, fifteen 0.1 s apart each (enemy_bullet_medium: 10
             # anti_player); each its own turret and gunner seat (the user, 2026-10-30: 「一些应该是独立的座位」): manned, its
             # gunner turns and fires it; empty, an auto turret
             *(Arm(f'aa_{side}', (f'aa_{name}_turret',), 'gun', (f'AA gun ({side.upper()})', f'対空機関砲（{jp}）',
                                                                   f'防空機砲（{tw}）', f'防空机炮（{tw}）', f'대공 기관포 ({kr})'), 600,
                   shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                   numbers={'AmmoDamage': 300.0, 'AmmoSpeed': 12.0, 'AmmoAlive': 90.0, 'FireInterval': 300.0,
                            'FireBurstCount': 15.0, 'FireBurstInterval': 6.0, 'FireCount': 2.0},
                   fire=0, auto=True, seat=seat, turret={'low': -5.0, 'high': 80.0, 'yaw_rate': 90.0, 'pitch_rate': 60.0})
               for seat, side, name, jp, tw, kr in ((1, 'l', 'left', '左', '左', '좌'), (2, 'r', 'right', '右', '右', '우'))),
             Arm('aam', ('missile_turret',), 'air', ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 80,
                 # missile_turret: fire_rate 5, twenty 0.1 s apart (enemy_missile_anti_aa_small: 8 anti_player)
                 missile={'model': 'AA missile', 'look': ('bullet_missile', 1.2), 'burn': 2.0, 'top': 350.0,
                          'accel': 300.0, 'max_g': 35.0, 'nav': 4.0, 'life': 5.0, 'damage': 240.0, 'blast': 3.0,
                          'lock_range': 1500.0, 'lock_cone': 0.8, 'lock_time': 15.0, 'burst': 20.0, 'burst_gap': 6.0,
                          'interval': 300.0}, fire=3, auto=True)),
       # xm_large_tank_emp at the game's size: 18.2 m long, the turret y 4.6..8.9 (roof ~8.85), z -1.1..4.9; the guns at x +-0.49,
       # y 6.5, their muzzles z 9.3
       camera={'model_eye': (0., 8.6, 1.9), 'model_look_at': (0., 7.0, 13.), 'third_person_distance': 15.,
               'third_person_height': 8.0},
       eye=(0.0, 9.05, 4.5), durability=12000.0, reload=22000, level=4.5, after='EDF6VC_CALL_XM_LARGE_TANK'),
    # The artillery: a howitzer at the back of the hull (enemy_artillery_medium, a shell every 3 s), light armour. The
    # PzH 2000's lobbed 155 mm shell (make_artillery.howitzer_sgo), a smaller gun's numbers.
    _u('ground/medium_artillery_regular', 'tracked', 'pzh2000', 'ported', 'Artillery',
       texts={'SC': ('X-Morph 自行火炮', '请求一辆人类军队的自行火炮：车体后部的榴弹炮为主攻击，曲射远程炮弹，约每三秒一发；'
                                     '装甲较薄。'),
              'CN': ('X-Morph 自走砲', '請求一輛人類軍隊的自走砲：車體後部的榴彈砲為主攻擊，曲射遠程砲彈，約每三秒一發；'
                                    '裝甲較薄。'),
              'JA': ('X-Morph 自走砲', '人類軍の自走砲を要請する。車体後部の榴弾砲（曲射の遠距離砲弾、約 3 秒に 1 発）を主攻撃とする。'
                                    '装甲は薄い。'),
              'EN': ('X-Morph Artillery', "Requests the human army's artillery: the howitzer at the back of its hull on the "
                                          'primary attack, long-range shells on an arc, about one every three seconds; light '
                                          'armour.')},
       gun=('Howitzer', '榴弹炮', '榴弾砲', '榴彈砲'),
       # main_weapon: ArtilleryWeaponDef, fire_rate 3 (enemy_artillery_medium: 20 artillery, Area 12): a lobbed shell
       # every 3 s (both holders on its one gun, in turn)
       guns={'AmmoCount': 30.0, 'AmmoDamage': 2400.0, 'AmmoExplosion': 18.0, 'FireInterval': 360.0},
       # xm_medium_artillery_regular at the game's size: 11.6 m long; the turret at the back (z -5.1..-0.9, roof ~3.3), the gun's
       # axis y 2.45, its muzzle z 5.5
       camera={'model_eye': (0., 3.2, -2.9), 'model_look_at': (0., 2.9, 8.), 'third_person_distance': 10.,
               'third_person_height': 4.4},
       eye=(0.0, 3.55, -1.4), durability=1500.0, reload=14000, level=2.5, after='EDF6VC_CALL_XM_LARGE_TANK_EMP'),
    # The shield generator: a hull carrying a shield dome for the units round it (not ported) and a small laser turret
    # (enemy_laser_medium, one a second); its armour between the battle and the advanced tank (250). The laser cannon's
    # beam as its round, as the advanced tank's.
    _u('ground/medium_shield_generator', 'tracked', 'apocalypse', 'ported', 'Shield Generator',
       texts={'SC': ('X-Morph 护盾车', '请求一辆人类军队的护盾车：车顶小型激光炮塔为主攻击，有目标就持续照射、光束穿透目标'
                                    '（护盾罩未移植）；装甲中等。'),
              'CN': ('X-Morph 護盾車', '請求一輛人類軍隊的護盾車：車頂小型雷射砲塔為主攻擊，有目標就持續照射、光束穿透目標'
                                    '（護盾罩未移植）；裝甲中等。'),
              'JA': ('X-Morph シールドジェネレーター', '人類軍のシールドジェネレーターを要請する。車体上の小型レーザー砲塔（目標がいる間照射し続ける、'
                                              '貫通）を主攻撃とする（シールドは未移植）。装甲は中程度。'),
              'EN': ('X-Morph Shield Generator', "Requests the human army's shield generator: the small laser turret on its "
                                                 'hull on the primary attack, a beam held on its target, through what it hits '
                                                 ' (its shield dome is not ported); medium armour.')},
       gun=('Laser Turret', '激光炮塔', 'レーザー砲塔', '雷射砲塔'), shell='V_506HELI_LASERCANNON01_L.SGO',
       # main_weapon: the advanced tank's beam (enemy_laser_medium, BeamWeaponDef), the same stream
       guns={'AmmoCount': 900.0, 'AmmoDamage': 80.0, 'AmmoSpeed': 40.0, 'AmmoAlive': 20.0, 'AmmoSize': 0.6,
             'AmmoGravityFactor': 0.0, 'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0, 'FireInterval': 8.0,
             'FireAccuracy': 0.001},
       # xm_medium_shield_generator at the game's size: 11.7 m long; the laser turret is small (x +-0.27, y 3.4..3.9, z 1.3..1.8),
       # its muzzle y 4.2, z 3.1; the slide guards beside the gun reach y 4.4, so the eye sits above them
       camera={'model_eye': (0., 4.0, 1.5), 'model_look_at': (0., 3.8, 9.), 'third_person_distance': 11.,
               'third_person_height': 5.0},
       eye=(0.0, 4.6, 1.7), eye_bone='cannon_l', durability=4000.0, reload=12000, level=2.2, after='EDF6VC_CALL_XM_MEDIUM_ARTILLERY_REGULAR'),
    # ground: wheeled
    # Their armour: the game's max_health at the tracked units' scale (the battle tank's 170 = 2500), WHEELED_FLOOR at
    # least (the light ones' 15..40 would be a few hundred: the stock sedan's 300).
    # The missile launcher: a truck whose rack (four tubes, its one bone pitching and turning at the rack's rear)
    # fires one big missile every 5 s (enemy_missile_anti_core_launchers: 125, Area 12, flown to a point it is given).
    # The TOS-1's lofted salvo made one round (EDF6AutoTurret's lofted mark: aimed at a ground point on the arc), the
    # upper pair of tubes the native holders' (prep_xmorph MOUTHS), in turn. The rack turns +-30 deg: it pivots at its
    # rear and sweeps the cab further round.
    _u('ground/medium_missile_launcher', 'wheeled', 'tos1', 'ported', 'Missile Launcher',
       texts={'SC': ('X-Morph 导弹发射车', '请求一辆人类军队的导弹发射车：卡车后部的发射架为主攻击，每五秒曲射一枚重型导弹，落点大范围爆炸；'
                                     '发射架左右各可转 30 度；装甲很薄。'),
              'CN': ('X-Morph 飛彈發射車', '請求一輛人類軍隊的飛彈發射車：卡車後部的發射架為主攻擊，每五秒曲射一枚重型飛彈，落點大範圍爆炸；'
                                     '發射架左右各可轉 30 度；裝甲很薄。'),
              'JA': ('X-Morph ミサイルランチャー', '人類軍のミサイルランチャーを要請する。トラック後部の発射機（5 秒ごとに重ミサイルを 1 発、'
                                          '山なりに撃ち、着弾点で広く爆発）を主攻撃とする。発射機は左右 30 度まで旋回。装甲は非常に薄い。'),
              'EN': ('X-Morph Missile Launcher', "Requests the human army's missile launcher: the rack on its truck's bed on "
                                                 'the primary attack, a heavy missile every 5 s on a high arc, bursting wide '
                                                 'where it lands; the rack turns 30 deg either way; very light armour.')},
       gun=('Heavy Missiles', '重型导弹', '重ミサイル', '重型飛彈'),
       # main_weapon: MissileWeaponDef, fire_rate 5, muzzles_per_fire 1 (enemy_missile_anti_core_launchers: 125, Area
       # 12): one missile every 5 s, the two holders in turn
       guns={'AmmoCount': 16.0, 'AmmoDamage': 15000.0, 'AmmoExplosion': 18.0, 'FireInterval': 600.0,
             'FireBurstCount': 1.0, 'FireBurstInterval': 0.0, 'FireAccuracy': 0.01},
       # models/xm_medium_missile_launcher: 12.3 m long (z -7.0..5.3); the cab in front (roof ~3.4), the rack on the
       # bed y 1.5..3.7, z -7.0..0.7, its pivot y 2.37, z -5.2; the upper tubes' mouths y 3.1, z 0.8
       camera={'model_eye': (0., 4.2, -3.0), 'model_look_at': (0., 3.0, 10.), 'third_person_distance': 13.,
               'third_person_height': 5.4},
       # the eye rides the rack (cannon_l): on the turret the raised rack swung through its view from 32 deg up
       eye=(0.0, 4.1, -4.6), eye_bone='cannon_l', durability=800.0, reload=16000, level=2.5, traverse=30.0,
       after='EDF6VC_CALL_XM_LARGE_BOMBER_CORE'),
    # The APC: eight wheels, a turret with twin heavy machine guns (bursts of three, every 1.3 s) and a radar; two
    # weapons on the same guns: anti-air rounds (main_weapon, both barrels) and ground rounds (core_weapon, the
    # barrels in turn). The 2S6's 30 mm belt as its round.
    _u('ground/medium_apc', 'wheeled', 'tunguska', 'ported', 'APC',
       texts={'SC': ('X-Morph 装甲运兵车', '请求一辆人类军队的装甲运兵车：八轮，炮塔上的双联重机枪为主攻击，三发一组、两根枪管齐射（对空弹）；'
                                      '副武器为同一对枪管轮流射出的对地弹；装甲中等。'),
              'CN': ('X-Morph 裝甲運兵車', '請求一輛人類軍隊的裝甲運兵車：八輪，砲塔上的雙聯重機槍為主攻擊，三發一組、兩根槍管齊射（對空彈）；'
                                      '副武器為同一對槍管輪流射出的對地彈；裝甲中等。'),
              'JA': ('X-Morph 装甲兵員輸送車', '人類軍の装甲兵員輸送車を要請する。8 輪で、砲塔の連装重機関銃（3 発ずつ、2 門同時：対空弾）を'
                                         '主攻撃とする。副武器は同じ銃身から交互に撃つ対地弾。装甲は中程度。'),
              'EN': ('X-Morph APC', "Requests the human army's APC: eight wheels, its turret's twin heavy machine gun on the "
                                    'primary attack, bursts of three from both barrels (anti-air rounds); on the secondary '
                                    'the same barrels\' ground rounds, in turn; medium armour.')},
       gun=('Twin Heavy MG', '双联重机枪', '連装重機関銃', '雙聯重機槍'),
       # main_weapon: fire_rate 1.3, three 0.2 s apart, both muzzles (enemy_bullet_medium: 10 anti_player)
       guns={'AmmoCount': 300.0, 'AmmoDamage': 300.0, 'AmmoExplosion': 0.0, 'FireInterval': 78.0,
             'FireBurstCount': 3.0, 'FireBurstInterval': 12.0}, alternate=False,
       arms=(Arm('core', ('core_weapon',), 'gun', ('Ground rounds', '対地弾', '對地彈', '对地弹', '대지탄'), 300, 'cannon_l',
                 # core_weapon: fire_rate 1.3, three 0.2 s apart, muzzles_per_fire 1 (enemy_bullet_medium_anti_core:
                 # 5 anti_core)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 600.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 90.0, 'FireInterval': 78.0,
                          'FireBurstCount': 3.0, 'FireBurstInterval': 12.0}),),
       # models/xm_medium_apc: 10.9 m long, the hull's roof ~4.0; the turret y 3.0..4.7 (its radar to 5.8), z -0.05..
       # 2.5; the guns at x +-1.2, y 3.95, their muzzles z 5.6
       camera={'model_eye': (0., 4.7, 0.6), 'model_look_at': (0., 4.2, 10.), 'third_person_distance': 11.,
               'third_person_height': 5.6},
       # the eye on the turret's front edge: from z 1.7 the aim line met the turret's own top from 35 deg up
       # (tests/gun_camera_sweep_guard.py)
       eye=(0.0, 4.95, 2.5), durability=3700.0, reload=12000, level=2.0,
       after='EDF6VC_CALL_XM_MEDIUM_MISSILE_LAUNCHER'),
    # The cargo truck: no weapon in the game (no WeaponComp; its skeleton's turret bones have no mesh): an unarmed car
    # on the sedan's V512 (family car), its driver alone. models/xm_medium_truck_cargo: 7.0 m long, 2.65 m wide, its
    # cab z 0.8..1.8 under a roof at 3.1 m behind a 1.9 m bonnet; three axles (z 2.40, -1.07, -2.44; r 0.56): the front
    # one steered, the rear one driven, the middle one drawn only. The cab's shell inside: floor 1.52, roof 2.55, z
    # 0.46..1.54, its side windows open from y 2.0 (cockpits.clearance): a real cab kit (truck_cab) shows through them
    # and shrinks away (cockpits.fitted), so the shell is the driver's view with the open rider's tablet.
    _u('ground/medium_truck_cargo', 'car', 'sedan', 'ported', 'Cargo Truck',
       texts={'SC': ('X-Morph 运输卡车', '请求一辆人类军队的运输卡车：三轴载货卡车，没有武器，只有驾驶座；前轮转向，车身大而结实。'),
              'CN': ('X-Morph 運輸卡車', '請求一輛人類軍隊的運輸卡車：三軸載貨卡車，沒有武器，只有駕駛座；前輪轉向，車身大而結實。'),
              'JA': ('X-Morph 輸送トラック', '人類軍の輸送トラックを要請する。3 軸の貨物トラックで、武装はなく運転席のみ。'
                                       '前輪で操舵し、車体は大きく頑丈。'),
              'EN': ('X-Morph Cargo Truck', "Requests the human army's cargo truck: a three-axle lorry, unarmed, its "
                                            "driver's seat alone; it steers by its front wheels, big and sturdy.")},
       eye=(-0.5, 2.1, 1.0), cab='open_rider', durability=1500.0, reload=2400, level=0.5,
       after='EDF6VC_CALL_XM_SMALL_BUGGY'),
    # The Humvee: a machine gun on its roof's turret, anti-air rounds (main_weapon) and ground rounds (core_weapon);
    # the lightest armour. The 2S6's 30 mm belt as its round, both holders on the one gun in turn.
    _u('ground/small_humvee', 'wheeled', 'tunguska', 'ported', 'Humvee',
       texts={'SC': ('X-Morph 悍马', '请求一辆人类军队的悍马：车顶枪塔上的机枪为主攻击，三发一组（对空弹）；副武器为同一挺机枪的对地弹；'
                                  '装甲很薄、速度快。'),
              'CN': ('X-Morph 悍馬', '請求一輛人類軍隊的悍馬：車頂槍塔上的機槍為主攻擊，三發一組（對空彈）；副武器為同一挺機槍的對地彈；'
                                  '裝甲很薄、速度快。'),
              'JA': ('X-Morph ハンヴィー', '人類軍のハンヴィーを要請する。屋根の銃塔の機関銃（3 発ずつ：対空弾）を主攻撃とし、'
                                      '副武器は同じ機関銃の対地弾。装甲は非常に薄く、速い。'),
              'EN': ('X-Morph Humvee', "Requests the human army's Humvee: the machine gun on its roof's turret on the primary "
                                       'attack, bursts of three (anti-air rounds); on the secondary the same gun\'s ground '
                                       'rounds; very light armour, fast.')},
       gun=('Machine Gun', '机枪', '機関銃', '機槍'),
       # main_weapon: fire_rate 1.5, three 0.15 s apart, one muzzle (enemy_bullet_small: 5 anti_player): both holders
       # on it, in turn
       guns={'AmmoCount': 400.0, 'AmmoDamage': 150.0, 'AmmoExplosion': 0.0, 'FireInterval': 180.0,
             'FireBurstCount': 3.0, 'FireBurstInterval': 9.0}, alternate=True,
       arms=(Arm('core', ('core_weapon',), 'gun', ('Ground rounds', '対地弾', '對地彈', '对地弹', '대지탄'), 300, 'cannon_l',
                 # core_weapon: fire_rate 2, three 0.1 s apart (enemy_bullet_small_anti_core: 2 anti_core)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 240.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 90.0, 'FireInterval': 120.0,
                          'FireBurstCount': 3.0, 'FireBurstInterval': 6.0}),),
       # models/xm_small_humvee: 5.9 m long, the roof ~2.4; the turret y 2.1..3.1, z -1.2..0.8; the gun's axis y 2.64,
       # its muzzle y 2.95, z 1.6
       camera={'model_eye': (0., 3.1, -0.4), 'model_look_at': (0., 2.8, 8.), 'third_person_distance': 8.,
               'third_person_height': 3.6},
       # the eye rides the gun (cannon_l): on the turret the raised gun met its view from 33 deg up
       eye=(0.0, 3.35, 0.1), eye_bone='cannon_l', durability=800.0, reload=6000, level=0.8, after='EDF6VC_CALL_XM_MEDIUM_APC'),
    # The buggy: AA missile pods either side of its turret (bursts of five, homing on air units) and a machine gun at
    # the front (core_weapon, on an attachment of the hull: a fixed mount the game aims itself). The pods are the
    # native pair (the Titan's side missile, its lock-on: Unit.homing), the gun its secondary along the turret's aim.
    _u('ground/small_buggy', 'wheeled', 'tunguska', 'ported', 'Buggy',
       texts={'SC': ('X-Morph 越野车', '请求一辆人类军队的越野车：炮塔两侧的防空导弹为主攻击，五发连射、追踪空中目标；副武器为车头机枪'
                                   '（六发连射，对地）；装甲很薄、速度快。'),
              'CN': ('X-Morph 越野車', '請求一輛人類軍隊的越野車：砲塔兩側的防空飛彈為主攻擊，五發連射、追蹤空中目標；副武器為車頭機槍'
                                   '（六發連射，對地）；裝甲很薄、速度快。'),
              'JA': ('X-Morph バギー', '人類軍のバギーを要請する。砲塔両側の対空ミサイル（5 発連射、空中目標を追尾）を主攻撃とし、'
                                    '副武器は車体前部の機関銃（6 発連射、対地）。装甲は非常に薄く、速い。'),
              'EN': ('X-Morph Buggy', "Requests the human army's buggy: the AA missile pods either side of its turret on the "
                                      'primary attack, bursts of five homing on air targets; on the secondary its front '
                                      'machine gun (bursts of six, ground rounds); very light armour, fast.')},
       gun=('AA Missiles', '防空导弹', '対空ミサイル', '防空飛彈'), homing=True,
       # main_weapon: MissileWeaponDef, fire_rate 3, five 0.3 s apart, muzzles_per_fire 1 (enemy_missile_anti_aa_small:
       # 8 anti_player, no Area; the heavy tank's AA missiles' numbers): the two pods in turn
       guns={'AmmoCount': 40.0, 'AmmoDamage': 240.0, 'AmmoExplosion': 3.0, 'AmmoAlive': 300.0, 'AmmoSize': 0.6,
             'FireInterval': 360.0, 'FireBurstCount': 5.0, 'FireBurstInterval': 18.0, 'FireAccuracy': 0.02,
             'LockonTargetType': 7301.0}, alternate=True,
       arms=(Arm('mg', ('core_weapon',), 'gun', ('Machine gun', '機銃', '機槍', '机枪', '기관총'), 600, 'cannon_l',
                 # core_weapon: fire_rate 1.5, six 0.1 s apart (enemy_bullet_small_anti_core: 2 anti_core)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 240.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 90.0, 'FireInterval': 90.0,
                          'FireBurstCount': 6.0, 'FireBurstInterval': 6.0}),),
       # models/xm_small_buggy: 5.7 m long, its roll cage ~2.2; the turret y 1.6..2.3, z -1.1..-0.25, the pods
       # (barrel_1) y 2.0..2.8, their mouths x +-0.77, y 2.72, z 0.4
       camera={'model_eye': (0., 2.9, -0.8), 'model_look_at': (0., 2.5, 8.), 'third_person_distance': 7.5,
               'third_person_height': 3.2},
       eye=(0.0, 3.0, -0.5), durability=800.0, reload=6000, level=1.0, after='EDF6VC_CALL_XM_SMALL_HUMVEE'),
    # walkers, foot
    # The light mech: two arm guns (weapon_enemy_mg_small, one round each every 0.4 s: enemy_bullet_small_anti_core, 2
    # anti_core), 45 health (a quarter of the battle tank's). On the Begaruta
    # (V504_BEGARUTA_BLUE; pylib/xm_walker.py: its hand holders).
    _u('ground/small_mech', 'mech', '', 'ported', 'Light Mech',
       texts={'SC': ('X-Morph 轻型机甲', '请求一台人类军队的轻型机甲：双足步行机甲，两臂各一挺机枪为主攻击，两挺同时开火，'
                                     '每 0.4 秒一发（与原作相同）；装甲较薄。'),
              'CN': ('X-Morph 輕型機甲', '請求一台人類軍隊的輕型機甲：雙足步行機甲，兩臂各一挺機槍為主攻擊，兩挺同時開火，'
                                     '每 0.4 秒一發（與原作相同）；裝甲較薄。'),
              'JA': ('X-Morph ライトメック', '人類軍のライトメックを要請する。二足歩行メックで、両腕の機関銃（2 挺同時、0.4 秒に 1 発、'
                                       '原作どおり）を主攻撃とする。装甲は薄い。'),
              'EN': ('X-Morph Light Mech', "Requests the human army's light mech: a two-legged walker, the machine guns "
                                           'on its arms on the primary attack, both at once, a round every 0.4 s (as in '
                                           'the game); light armour.')},
       gun=('Arm Machine Gun', '臂载机枪', '腕部機関銃', '臂載機槍'),
       # 2 anti_core = 240 a round (DAMAGE), every 0.4 s; the round's speed 35 as the tanks' (the coax's 6 m a frame
       # template a little faster)
       guns={'AmmoCount': 600.0, 'AmmoDamage': 240.0, 'AmmoSpeed': 8.0, 'AmmoAlive': 40.0, 'FireInterval': 24.0},
       # level 1.6: the Begaruta requests' multipliers (x8.3 there) about the battle tank's (x8.1 at 1.5): 700 -> ~5800,
       # the game's 45 against the tank's 170 (20200)
       durability=700.0, reload=9000, level=1.6, after='EDF6VC_CALL_XM_MEDIUM_TRUCK_CARGO'),
    # The heavy mech: twin heavy guns on its turret (enemy_bullet_big_anti_core, 30 anti_core: four rounds 0.4 s apart,
    # the barrels in turn, every 6 s), AA missiles (three pairs 0.4 s apart every 4 s: enemy_missile_anti_aa_small, 8
    # anti_player, homing on air units), 600 health (3.5 times the battle tank's). On the Proteus Mk2 (its barrels' and
    # launcher's holders).
    _u('ground/large_mech', 'mech', '', 'ported', 'Heavy Mech',
       texts={'SC': ('X-Morph 重型机甲', '请求一台人类军队的重型机甲：大型双足步行机甲，炮塔两侧的重炮为主攻击，每六秒两门各连射两发；'
                                     '副武器为防空导弹（三轮连射，每轮两发）；装甲厚重。'),
              'CN': ('X-Morph 重型機甲', '請求一台人類軍隊的重型機甲：大型雙足步行機甲，砲塔兩側的重砲為主攻擊，每六秒兩門各連射兩發；'
                                     '副武器為防空飛彈（三輪連射，每輪兩發）；裝甲厚重。'),
              'JA': ('X-Morph ヘビーメック', '人類軍のヘビーメックを要請する。大型の二足歩行メックで、砲塔両側の重砲（6 秒ごとに 2 門が'
                                       '各 2 発連射）を主攻撃とする。副武器は対空ミサイル（2 発ずつ 3 回連射）。装甲は厚い。'),
              'EN': ('X-Morph Heavy Mech', "Requests the human army's heavy mech: a big two-legged walker, the heavy "
                                           'guns either side of its turret on the primary attack, two rounds from each '
                                           'every 6 s; AA missiles on the secondary (three pairs in a burst); heavy '
                                           'armour.')},
       gun=('Twin Heavy Cannon', '双联重炮', '連装重砲', '雙聯重砲'),
       # 30 anti_core = 3600 a round, no Area; the game's four a volley from the two barrels in turn: two from each
       guns={'AmmoCount': 60.0, 'AmmoDamage': 3600.0, 'AmmoExplosion': 0.0, 'AmmoSpeed': 6.0, 'FireInterval': 360.0,
             'FireBurstCount': 2.0, 'FireBurstInterval': 48.0},
       arms=(Arm('missiles', ('missiles',), 'air', ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 36,
                 'missile_launcher',
                 # fire_rate 4, muzzles_per_fire 2, three 0.4 s apart (8 anti_player = 240 a missile; no Area: the
                 # heavy tank's AA missiles' small blast)
                 numbers={'AmmoCount': 36.0, 'AmmoDamage': 240.0, 'AmmoExplosion': 3.0, 'FireInterval': 240.0,
                          'FireBurstCount': 3.0, 'FireBurstInterval': 24.0, 'FireCount': 2.0}),),
       durability=8800.0, reload=20000, level=2.2, after='EDF6VC_CALL_XM_SMALL_MECH'),
    # The infantry: a powered suit with a machine gun (weapon_enemy_mg_small, 5 anti_player every 0.4 s) and a
    # three-round burst against the core every 2 s, 6 health. A support soldier kind (src/support_call.h xmInfantry,
    # pylib/xm_infantry.py) on the Ranger's body: it carries the kind's EDF weapon (its own gun is left out), a
    # Ranger's durability and a half (a suit, not a man; the game's weakest unit, an eighth of the light mech).
    _u('ground/small_infantry', 'infantry', '', 'ported', 'Infantry',
       texts={'SC': ('X-Morph 步兵', '人类军队的动力装甲步兵：外观取自原作，使用所选的 EDF 武器；耐久为游骑兵的 1.5 倍。'),
              'CN': ('X-Morph 步兵', '人類軍隊的動力裝甲步兵：外觀取自原作，使用所選的 EDF 武器；耐久為遊騎兵的 1.5 倍。'),
              'JA': ('X-Morph 歩兵', '人類軍のパワードスーツ歩兵。外観は原作のもので、選んだ EDF の武器を使う。耐久はレンジャーの 1.5 倍。'),
              'EN': ('X-Morph Infantry', "The human army's powered-suit infantry: the game's look, the EDF weapon "
                                         "chosen for them; one and a half times a Ranger's durability.")},
       durability=300.0),
    # forts
    # The forts' AA guns: one mesh (a twin-barrelled gun on a round fort, 7 m tall), two weapons. The AA cannon fires
    # flak (enemy_flak_medium, one a second): the Flak 88's shell, a burst at its target; the AA machine gun bursts of
    # three (enemy_bullet_medium): the GDF-005's belt. Both on pylib/emplacement_model.py's fortified base.
    _u('fort/aa_cannon', 'emplacement', 'flak88', 'ported', 'AA Cannon',
       texts={'SC': ('X-Morph 防空炮台', '请求一座人类军队的防空炮台：固定炮台，双联高射炮两根炮管轮流发射，每秒一发、在目标附近大范围爆炸，'
                                     '专打空中目标；不能移动。'),
              'CN': ('X-Morph 防空砲台', '請求一座人類軍隊的防空砲台：固定砲台，雙聯高射砲兩根砲管輪流發射，每秒一發、在目標附近大範圍爆炸，'
                                     '專打空中目標；不能移動。'),
              'JA': ('X-Morph 対空砲台', '人類軍の対空砲台を要請する。固定砲台で、連装高射砲が 2 門交互に 1 秒に 1 発'
                                     '撃ち、目標付近で広く炸裂する。空中目標に強い。移動はできない。'),
              'EN': ('X-Morph AA Cannon', "Requests the human army's AA cannon: a fixed emplacement, its twin flak gun "
                                          'firing a shell a second, the barrels in turn, bursting wide by its target, for air targets; '
                                          'it does not move.')},
       gun=('Twin Flak Cannon', '双联高射炮', '連装高射砲', '雙聯高射砲'),
       # main_weapon: InstantWeaponDef, fire_rate 1, muzzles_per_fire 1 (the barrels in turn), enemy_flak_medium
       # (BombAmmoDef: 25 anti_player, Area 12)
       guns={'AmmoCount': 120.0, 'AmmoDamage': 750.0, 'AmmoSpeed': 30.0, 'AmmoExplosion': 18.0, 'FireInterval': 120.0},
       # xm_aa_cannon at the game's size (on the base, 1.3 m up): the gun's trunnion y 4.7 + 1.3, z -0.63; its muzzles x +-0.85,
       # z 6.7: the sight over the trunnion, behind it
       eye=(0.0, 7.0, -1.5), durability=1500.0, reload=8000, level=1.3, after='EDF6VC_CALL_XM_MEDIUM_SHIELD_GENERATOR'),
    _u('fort/aa_cannon_machinegun', 'emplacement', 'gdf005', 'ported', 'AA Machine Gun',
       texts={'SC': ('X-Morph 防空机枪炮台', '请求一座人类军队的防空机枪炮台：固定炮台，双联机炮三发一组连射，专打空中目标；不能移动。'),
              'CN': ('X-Morph 防空機槍砲台', '請求一座人類軍隊的防空機槍砲台：固定砲台，雙聯機砲三發一組連射，專打空中目標；不能移動。'),
              'JA': ('X-Morph 対空機銃砲台', '人類軍の対空機銃砲台を要請する。固定砲台で、連装機関砲が 3 発ずつ連射する。'
                                       '空中目標に強い。移動はできない。'),
              'EN': ('X-Morph AA Machine Gun', "Requests the human army's AA machine gun: a fixed emplacement, its twin "
                                               'autocannon firing bursts of three, for air targets; it does not move.')},
       gun=('Twin AA Machine Gun', '双联防空机炮', '連装対空機関砲', '雙聯防空機砲'),
       # main_weapon: fire_rate 1, three 0.2 s apart (enemy_bullet_medium: 10 anti_player), both barrels
       guns={'AmmoDamage': 300.0, 'AmmoExplosion': 0.0, 'FireBurstCount': 3.0, 'FireBurstInterval': 12.0,
             'FireInterval': 60.0},
       eye=(0.0, 7.0, -1.5), durability=1500.0, reload=7000, level=1.2, after='EDF6VC_CALL_XM_AA_CANNON'),
    # the forts' barracks: buildings (no weapon; in the game they send infantry out), the maps' props
    _u('fort/bunker_barracks', 'structure', title='Barracks'),
    _u('fort/bunker_barracks_02', 'structure', title='Barracks'),
    # air
    # helicopters: the stock V506's flight (the player flies them as the game's helis, an NPC pilot as src/heli.cpp flies
    # any 506), their model on its skeleton (pylib/xm_air_model.py), their weapons on its holders (Unit.air; the game's
    # numbers as the tracked units', docs/xmorph-port.md 「武器」). Durability: the game's max_health on the tracked
    # units' scale (about x6 at the top, x15 at the bottom), the light ones floored as the tracked ones are.
    # The light helicopter: a drone that rams the core (Behavior kamikaze, ContactDamageDealerDef const 22); it has no
    # gun. Its ram is a charge fired from its nose (22 -> 2640, a blast its size), one on each trigger.
    _u('air/small_helicopter_core', 'heli', status='ported', title='Light Helicopter',
       texts={'SC': ('X-Morph 轻型直升机', '请求一架人类军队的轻型直升机：原作里是撞向目标的自杀无人机，没有机炮；这里机炮键和导弹键各发射机首撞击炸药'
                                       '（近距离、大威力，各一枚）；机体很小、装甲很薄。'),
              'CN': ('X-Morph 輕型直升機', '請求一架人類軍隊的輕型直升機：原作裡是撞向目標的自殺無人機，沒有機砲；這裡機砲鍵和飛彈鍵各發射機首撞擊炸藥'
                                       '（近距離、大威力，各一枚）；機體很小、裝甲很薄。'),
              'JA': ('X-Morph 軽ヘリ', '人類軍の軽ヘリを要請する。原作では目標に体当たりする自爆ドローンで機銃はない。ここでは機銃キーとミサイルキーで'
                                     '機首の体当たり爆薬（近距離・高威力、各 1 発）を撃つ。機体は小さく装甲は薄い。'),
              'EN': ('X-Morph Light Helicopter', "Requests the human army's light helicopter: in the game a drone that rams its "
                                                 'target, with no gun; here its gun and missile triggers fire ram charges from '
                                                 'its nose (short range, hard-hitting, one each); very small, thin armour.')},
       gun=('Ram Charge', '撞击炸药', '体当たり爆薬', '撞擊炸藥'),
       air={'guns': HeliArm('V_506HELI_MISSILE01.SGO', ('att_muzzle_1', 'att_muzzle_1'),
                            ('Ram Charge', '体当たり爆薬', '撞擊炸藥', '撞击炸药', 'Ram Charge'),
                            {'AmmoCount': 1.0, 'AmmoDamage': 2640.0, 'AmmoExplosion': 4.5, 'FireBurstCount': 1.0,
                             'FireInterval': 60.0, 'LockonType': 0.0}),
            'missile': HeliArm('V_506HELI_MISSILE01.SGO', ('att_muzzle_1',), ('Ram Charge', '体当たり爆薬', '撞擊炸藥', '撞击炸药', 'Ram Charge'),
                               {'AmmoCount': 1.0, 'AmmoDamage': 2640.0, 'AmmoExplosion': 4.5, 'FireBurstCount': 1.0,
                                'FireInterval': 60.0, 'LockonType': 0.0})},
       # models/xm_small_helicopter_core: 5.6 x 2.3 x 4.6 m: the sight over and behind it
       eye=(0.0, 3.2, -3.5), durability=800.0, reload=6000, level=0.8, after='EDF6VC_CALL_XM_HIGHWAY_TANK'),
    # The helicopter: a coaxial rotor, four missile pods (missiles: MissileWeaponDef, fire_rate 3, four 0.3 s apart:
    # enemy_missile_anti_core_small, 3 + Area 4). The outer pods on the gun trigger, the inner on the missile trigger.
    _u('air/medium_helicopter_core', 'heli', status='ported', title='Helicopter',
       texts={'SC': ('X-Morph 直升机', '请求一架人类军队的直升机：共轴双旋翼，机翼下四个导弹巢；机炮键发射外侧两巢、导弹键发射内侧两巢的导弹'
                                     '（每三秒一轮连射，与原作相同）；装甲较薄。'),
              'CN': ('X-Morph 直升機', '請求一架人類軍隊的直升機：共軸雙旋翼，機翼下四個飛彈巢；機砲鍵發射外側兩巢、飛彈鍵發射內側兩巢的飛彈'
                                     '（每三秒一輪連射，與原作相同）；裝甲較薄。'),
              'JA': ('X-Morph ヘリ', '人類軍のヘリを要請する。二重反転ローターで、翼下に 4 基のミサイルポッドを持つ。機銃キーで外側 2 基、'
                                    'ミサイルキーで内側 2 基のミサイルを撃つ（原作どおり 3 秒ごとの連射）。装甲は薄い。'),
              'EN': ('X-Morph Helicopter', "Requests the human army's helicopter: coaxial rotors, four missile pods under its "
                                           'wings; the gun trigger fires the outer two, the missile trigger the inner two '
                                           '(a burst every 3 s, as in the game); light armour.')},
       gun=('Missile Pods', '导弹巢', 'ミサイルポッド', '飛彈巢'),
       air={'guns': HeliArm('V_506HELI_MISSILE01.SGO', ('att_left_wing_missiles_1', 'att_right_wing_missiles_1'),
                            ('Missile Pods (outer)', 'ミサイルポッド（外側）', '飛彈巢（外側）', '导弹巢（外侧）', 'Missile Pods (outer)'),
                            {'AmmoCount': 32.0, 'AmmoDamage': 360.0, 'AmmoExplosion': 6.0, 'FireBurstCount': 2.0,
                             'FireBurstInterval': 18.0, 'FireInterval': 180.0}),
            'missile': HeliArm('V_506HELI_MISSILE01.SGO', ('att_left_wing_missiles_2', 'att_right_wing_missiles_2'),
                               ('Missile Pods (inner)', 'ミサイルポッド（内側）', '飛彈巢（内側）', '导弹巢（内侧）', 'Missile Pods (inner)'),
                               {'AmmoCount': 32.0, 'AmmoDamage': 360.0, 'AmmoExplosion': 6.0, 'FireBurstCount': 4.0,
                                'FireBurstInterval': 18.0, 'FireInterval': 180.0})},
       # models/xm_medium_helicopter_core: 10.7 x 4.0 x 11.6 m, the rotors' mast at y 2.2..3.6 over the cabin
       eye=(0.0, 4.8, -3.0), durability=1500.0, reload=10000, level=1.5, after='EDF6VC_CALL_XM_SMALL_HELICOPTER_CORE'),
    # The heavy helicopter: a nose gun turret (main_gun: fire_rate 2, six 0.07 s apart: enemy_bullet_small_fast, the
    # heavy tank's machine gun's round) and four missile pods (each the helicopter's: four every 3 s). Three lift rotors;
    # the V506 animation spins the front one and the tail propeller.
    _u('air/large_helicopter_core', 'heli', status='ported', title='Heavy Helicopter',
       texts={'SC': ('X-Morph 重型直升机', '请求一架人类军队的重型直升机：三旋翼大型机，机首机炮塔（六发连射，每两秒一轮）为机炮键，'
                                       '四个导弹巢（每三秒十六发）为导弹键；装甲厚重。'),
              'CN': ('X-Morph 重型直升機', '請求一架人類軍隊的重型直升機：三旋翼大型機，機首機砲塔（六發連射，每兩秒一輪）為機砲鍵，'
                                       '四個飛彈巢（每三秒十六發）為飛彈鍵；裝甲厚重。'),
              'JA': ('X-Morph 重ヘリ', '人類軍の重ヘリを要請する。3 ローターの大型機で、機首の機銃塔（6 発連射、2 秒ごと）を機銃キー、'
                                     '4 基のミサイルポッド（3 秒ごとに 16 発）をミサイルキーで撃つ。装甲は厚い。'),
              'EN': ('X-Morph Heavy Helicopter', "Requests the human army's heavy helicopter: a big three-rotor craft, its nose "
                                                 'gun turret (bursts of six every 2 s) on the gun trigger, four missile pods '
                                                 '(sixteen every 3 s) on the missile trigger; heavy armour.')},
       gun=('Nose Gun', '机首机炮', '機首機銃', '機首機砲'),
       air={'guns': HeliArm('V_506HELI_GATLING01_{side}.SGO', ('att_turret_1', 'att_turret_1'),
                            ('Nose Gun', '機首機銃', '機首機砲', '机首机炮', 'Nose Gun'),
                            {'AmmoCount': 600.0, 'AmmoDamage': 150.0, 'AmmoSpeed': 20.0, 'AmmoAlive': 60.0,
                             'FireInterval': 120.0, 'FireBurstCount': 3.0, 'FireBurstInterval': 4.0}),
            'missile': HeliArm('V_506HELI_MISSILE01.SGO', ('att_left_wing_missiles_inner_1', 'att_right_wing_missiles_inner_1',
                                                           'att_left_wing_missiles_outer_1', 'att_right_wing_missiles_outer_1'),
                               ('Missile Pods', 'ミサイルポッド', '飛彈巢', '导弹巢', 'Missile Pods'),
                               {'AmmoCount': 96.0, 'AmmoDamage': 360.0, 'AmmoExplosion': 6.0, 'FireBurstCount': 16.0,
                                'FireBurstInterval': 4.0, 'FireInterval': 180.0})},
       # models/xm_large_helicopter_core: 13.6 x 6.6 x 19.1 m
       eye=(0.0, 8.0, -6.0), durability=6000.0, reload=16000, level=3.0, after='EDF6VC_CALL_XM_MEDIUM_HELICOPTER_CORE'),
    # The transport helicopter: tandem rotors, no weapon (it drops troops). EDF's V506 holds four weapons whatever it
    # carries, so it keeps the stock 506's (the Eros's autocannons and missiles, as they are); ten passenger seats as the
    # plugin's transport helicopter (tools/make_jets.py TRANSPORT_HELI_EXTRA): squads ride it (src/transport.cpp).
    _u('air/large_helicopter_transport', 'heli', status='ported', title='Transport Helicopter',
       texts={'SC': ('X-Morph 运输直升机', '请求一架人类军队的运输直升机：纵列双旋翼，原作里没有武器，这里保留 EDF 直升机原有的机炮和导弹；'
                                       '另有十个乘员座位，可以运送小队（与运输直升机相同）；装甲中等。'),
              'CN': ('X-Morph 運輸直升機', '請求一架人類軍隊的運輸直升機：縱列雙旋翼，原作裡沒有武器，這裡保留 EDF 直升機原有的機砲和飛彈；'
                                       '另有十個乘員座位，可以運送小隊（與運輸直升機相同）；裝甲中等。'),
              'JA': ('X-Morph 輸送ヘリ', '人類軍の輸送ヘリを要請する。タンデムローター機で、原作では非武装。ここでは EDF のヘリ本来の機関砲とミサイルを'
                                      '残す。乗員席が 10 あり、分隊を運べる（輸送ヘリと同じ）。装甲は中程度。'),
              'EN': ('X-Morph Transport Helicopter', "Requests the human army's transport helicopter: tandem rotors; unarmed in "
                                                     "the game, it keeps the EDF helicopter's own autocannons and missiles here; "
                                                     'ten passenger seats that carry a squad (as the transport helicopter); '
                                                     'medium armour.')},
       gun=('Autocannon', '机炮', '機関砲', '機砲'),
       air={'seats': 10},
       # models/xm_large_helicopter_transport: 13.8 x 5.5 x 22.7 m, the cockpit at the nose
       eye=(0.0, 7.5, -4.0), durability=3000.0, reload=9000, level=1.5, after='EDF6VC_CALL_XM_LARGE_HELICOPTER_CORE'),
    # The gunship: four pairs of rotors (the rear pair spun), two gun turrets on its back (left_turret / right_turret:
    # fire_rate 1.5, six 0.1 s apart each: enemy_bullet_small_fast) and its launcher (missile_launcher: fire_rate 5,
    # enemy_missile_anti_core_launchers: a slow homing missile, 125 + Area 12, the game's heaviest round).
    _u('air/large_gunship_core', 'heli', status='ported', title='Gunship',
       texts={'SC': ('X-Morph 武装运输机', '请求一架人类军队的武装运输机：八旋翼巨型机，背上前后两座机炮塔（各六发连射，每一秒半一轮）为机炮键，'
                                       '机腹发射器的重型导弹（每五秒一枚，大范围爆炸）为导弹键；直升机中装甲最厚。'),
              'CN': ('X-Morph 武裝運輸機', '請求一架人類軍隊的武裝運輸機：八旋翼巨型機，背上前後兩座機砲塔（各六發連射，每一秒半一輪）為機砲鍵，'
                                       '機腹發射器的重型飛彈（每五秒一枚，大範圍爆炸）為飛彈鍵；直升機中裝甲最厚。'),
              'JA': ('X-Morph ガンシップ', '人類軍のガンシップを要請する。8 ローターの巨大機で、背中の前後 2 基の機銃塔（各 6 発連射、1.5 秒ごと）を'
                                       '機銃キー、機体下の発射機の重ミサイル（5 秒に 1 発、広い爆発）をミサイルキーで撃つ。ヘリで最も装甲が厚い。'),
              'EN': ('X-Morph Gunship', "Requests the human army's gunship: a huge eight-rotor craft, the two gun turrets on its "
                                        "back (bursts of six every 1.5 s each) on the gun trigger, its launcher's heavy missile "
                                        '(one every 5 s, a wide blast) on the missile trigger; the heaviest armour of the '
                                        'helicopters.')},
       gun=('Twin Gun Turrets', '双机炮塔', '連装機銃塔', '雙機砲塔'),
       air={'guns': HeliArm('V_506HELI_GATLING01_{side}.SGO', ('att_left_turret_muzzle_1', 'att_right_turret_muzzle_1'),
                            ('Gun Turrets', '機銃塔', '機砲塔', '机炮塔', 'Gun Turrets'),
                            {'AmmoCount': 600.0, 'AmmoDamage': 150.0, 'AmmoSpeed': 20.0, 'AmmoAlive': 60.0,
                             'FireInterval': 90.0, 'FireBurstCount': 6.0, 'FireBurstInterval': 6.0}),
            'missile': HeliArm('V_506HELI_MISSILE01.SGO', ('att_bombs_1',), ('Heavy Missile', '重ミサイル', '重型飛彈', '重型导弹', 'Heavy Missile'),
                               {'AmmoCount': 8.0, 'AmmoDamage': 15000.0, 'AmmoExplosion': 18.0, 'FireBurstCount': 1.0,
                                'FireInterval': 300.0})},
       # models/xm_large_gunship_core: 23.6 x 7.4 x 25.2 m
       eye=(0.0, 11.0, -9.0), durability=12000.0, reload=22000, level=4.5, after='EDF6VC_CALL_XM_LARGE_HELICOPTER_TRANSPORT'),
    # The jets (pylib/vcobjects.py XM_JETS): the plugin flies them as its other aircraft (src/jet.cpp: an NPC pilot's,
    # their kBodies rows; the player's, their kBoardable rows), each its role's way; their requests bring one empty to fly
    # (the Air Raider's EDF6VC_CALL_FLY_* way). Their gun pair on the gun trigger, their stores on the missile trigger.
    # The fighter: an air-superiority jet with no gun (main_weapon: fire_rate 3, two 0.4 s apart from its two wing rails:
    # enemy_missile_anti_aa_small, 8 anti_player, homing on flyers). Its gun trigger fires the jets' own guns.
    _u('air/medium_fighter_aa', 'jet', status='ported', title='Fighter',
       texts={'SC': ('X-Morph 战斗机', '请求一架人类军队的战斗机，空投到信号弹处后自己驾驶：原作只挂对空导弹，这里导弹键发射翼下对空导弹'
                                     '（每三秒两枚，追踪空中目标），机炮键是本插件喷气机的机炮；机体很小、装甲很薄。'),
              'CN': ('X-Morph 戰鬥機', '請求一架人類軍隊的戰鬥機，空投到信號彈處後自己駕駛：原作只掛對空飛彈，這裡飛彈鍵發射翼下對空飛彈'
                                     '（每三秒兩枚，追蹤空中目標），機砲鍵是本插件噴射機的機砲；機體很小、裝甲很薄。'),
              'JA': ('X-Morph 戦闘機', '人類軍の戦闘機を要請し、信号弾の位置に届いた機体を自分で操縦する。原作は対空ミサイルのみで、ここではミサイル'
                                     'キーで翼下の対空ミサイル（3 秒に 2 発、空中目標を追尾）、機銃キーで本プラグインのジェット機関砲を撃つ。'
                                     '機体は小さく装甲は薄い。'),
              'EN': ('X-Morph Fighter', "Requests the human army's fighter, dropped empty at the flare for you to fly: in the "
                                        'game it carries only air-to-air missiles; here its missile trigger fires them from '
                                        'its wing rails (two every 3 s, homing on flyers) and its gun trigger the jets\' own '
                                        'cannon; very small, thin armour.')},
       gun=('Jet Cannon', '机炮', '機関砲', '機砲'),
       air={'mark': 7051.0, 'mass': 5000.0, 'body': 'xmFighter', 'role': 'fighter', 'ram': 4.0, 'guns': (), 'gun': {},
            'stores': (Arm('aam', ('main_weapon',), 'air', ('AA Missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'),
                           8, store=('AAM_S', {'damage': 240.0, 'blast': 3.0, 'burst': 2.0, 'burst_gap': 24.0, 'interval': 180.0})),)},
       # models/xm_medium_fighter_aa: 5.7 x 1.6 x 7.7 m
       eye=(0.0, 2.6, -6.5), durability=1200.0, reload=7000, level=1.0, after='EDF6VC_CALL_XM_LARGE_GUNSHIP_CORE'),
    # The attack plane: two rocket pods under its wings (main_weapon: fire_rate 2, two 0.4 s apart: enemy_rocket_small,
    # 10 anti_player, barely guided), no gun.
    _u('air/medium_gunship_aa', 'jet', status='ported', title='Attack Plane',
       texts={'SC': ('X-Morph 攻击机', '请求一架人类军队的攻击机，空投到信号弹处后自己驾驶：导弹键发射翼下火箭弹（每两秒两枚，几乎不制导），'
                                     '机炮键是本插件喷气机的机炮（原作没有机炮）；机体小、装甲薄。'),
              'CN': ('X-Morph 攻擊機', '請求一架人類軍隊的攻擊機，空投到信號彈處後自己駕駛：飛彈鍵發射翼下火箭彈（每兩秒兩枚，幾乎不制導），'
                                     '機砲鍵是本插件噴射機的機砲（原作沒有機砲）；機體小、裝甲薄。'),
              'JA': ('X-Morph 攻撃機', '人類軍の攻撃機を要請し、信号弾の位置に届いた機体を自分で操縦する。ミサイルキーで翼下のロケット弾（2 秒に 2 発、'
                                     'ほぼ無誘導）、機銃キーで本プラグインのジェット機関砲（原作に機銃はない）を撃つ。機体は小さく装甲は薄い。'),
              'EN': ('X-Morph Attack Plane', "Requests the human army's attack plane, dropped empty at the flare for you to fly: "
                                             'its missile trigger fires the rocket pods under its wings (two every 2 s, barely '
                                             'guided), its gun trigger the jets\' own cannon (it has no gun in the game); '
                                             'small, thin armour.')},
       gun=('Jet Cannon', '机炮', '機関砲', '機砲'),
       air={'mark': 7052.0, 'mass': 6000.0, 'body': 'xmAttack', 'role': 'multirole', 'ram': 5.0, 'guns': (), 'gun': {},
            'stores': (Arm('rockets', ('main_weapon',), 'rocket', ('Rockets', 'ロケット', '火箭', '火箭', '로켓'), 38,
                           store=('RKT', {'damage': 300.0, 'blast': 3.0, 'burst': 2.0, 'burst_gap': 24.0, 'interval': 120.0})),)},
       # models/xm_medium_gunship_aa: 4.8 x 2.8 x 9.2 m
       eye=(0.0, 4.0, -7.5), durability=2000.0, reload=9000, level=1.5, after='EDF6VC_CALL_XM_MEDIUM_FIGHTER_AA'),
    # The heavy attack plane: a gun turret on its back (main_gun: fire_rate 1.5, five 0.1 s apart), two miniguns in its
    # nose (miniguns: fire_rate 1.5, ten 0.08 s apart; both enemy_bullet_small: 5 anti_player), eight rocket pods
    # (fire_rate 0.25 each) and four missile rails (fire_rate 3, two 0.4 s apart each). The miniguns are its gun pair
    # (the turret, which the game aims by itself, is not ported: EDF's aircraft guns fire along the nose).
    _u('air/large_gunship_aa', 'jet', status='ported', title='Heavy Attack Plane',
       texts={'SC': ('X-Morph 重型攻击机', '请求一架人类军队的重型攻击机，空投到信号弹处后自己驾驶：机炮键是机头两挺转管机枪（每一秒半十发连射），'
                                       '导弹键在八个火箭巢（快速连射）和四条对空导弹滑轨之间切换；固定翼中火力最猛。'),
              'CN': ('X-Morph 重型攻擊機', '請求一架人類軍隊的重型攻擊機，空投到信號彈處後自己駕駛：機砲鍵是機頭兩挺轉管機槍（每一秒半十發連射），'
                                       '飛彈鍵在八個火箭巢（快速連射）和四條對空飛彈滑軌之間切換；固定翼中火力最猛。'),
              'JA': ('X-Morph 重攻撃機', '人類軍の重攻撃機を要請し、信号弾の位置に届いた機体を自分で操縦する。機銃キーで機首の 2 門のミニガン'
                                       '（1.5 秒ごとに 10 発連射）、ミサイルキーで 8 基のロケットポッド（高速連射）と 4 本の対空ミサイルを'
                                       '切り替えて撃つ。固定翼で最も火力が高い。'),
              'EN': ('X-Morph Heavy Attack Plane', "Requests the human army's heavy attack plane, dropped empty at the flare for "
                                                   'you to fly: its gun trigger fires the two miniguns in its nose (bursts of '
                                                   'ten every 1.5 s), its missile trigger its eight rocket pods (a fast '
                                                   'ripple) or its four air-to-air missile rails, cycled; the heaviest fire of '
                                                   'the fixed-wing craft.')},
       gun=('Miniguns', '转管机枪', 'ミニガン', '轉管機槍'),
       air={'mark': 7053.0, 'mass': 15000.0, 'body': 'xmHeavyAttack', 'role': 'multirole', 'ram': 7.0,
            'guns': ('att_minigun_2', 'att_minigun_1'),
            'gun': {'AmmoCount': 900.0, 'AmmoDamage': 150.0, 'FireInterval': 90.0, 'FireBurstCount': 10.0,
                    'FireBurstInterval': 5.0},
            'stores': (Arm('rockets', tuple(f'{s}_wing_missiles_{p}_{n}' for s in ('left', 'right') for p in ('inner', 'outer')
                                            for n in (1, 2)), 'rocket', ('Rockets', 'ロケット', '火箭', '火箭', '로켓'), 64,
                           store=('RKT', {'damage': 300.0, 'blast': 3.0, 'burst': 8.0, 'burst_gap': 4.0, 'interval': 60.0})),
                       Arm('aam', ('missile_left_1', 'missile_left_2', 'missile_right_1', 'missile_right_2'), 'air',
                           ('AA Missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 16,
                           store=('AAM_S', {'damage': 240.0, 'blast': 3.0, 'burst': 8.0, 'burst_gap': 12.0, 'interval': 180.0})))},
       # models/xm_large_gunship_aa: 12.3 x 4.0 x 14.5 m
       eye=(0.0, 6.0, -11.0), durability=6000.0, reload=16000, level=3.0, after='EDF6VC_CALL_XM_MEDIUM_GUNSHIP_AA'),
    # The bomber: a bomb bay (bombs: fire_rate 0.4 from two racks: enemy_bomb_big, 95 anti_core_bombs + Area 8) and two
    # gun turrets on its wings (left_turret / right_turret: fire_rate 1.3, three 0.2 s apart: enemy_bullet_small), which
    # here are its gun pair.
    _u('air/large_bomber_core', 'jet', status='ported', title='Bomber',
       texts={'SC': ('X-Morph 轰炸机', '请求一架人类军队的轰炸机，空投到信号弹处后自己驾驶：导弹键从弹舱投下重磅炸弹（每零点四秒一枚，大范围爆炸），'
                                     '机炮键是两翼的机炮塔（每一秒多三发）；固定翼中装甲最厚。'),
              'CN': ('X-Morph 轟炸機', '請求一架人類軍隊的轟炸機，空投到信號彈處後自己駕駛：飛彈鍵從彈艙投下重磅炸彈（每零點四秒一枚，大範圍爆炸），'
                                     '機砲鍵是兩翼的機砲塔（每一秒多三發）；固定翼中裝甲最厚。'),
              'JA': ('X-Morph 爆撃機', '人類軍の爆撃機を要請し、信号弾の位置に届いた機体を自分で操縦する。ミサイルキーで爆弾倉から大型爆弾'
                                     '（0.4 秒に 1 発、広い爆発）を落とし、機銃キーで両翼の機銃塔（1.3 秒ごとに 3 発）を撃つ。固定翼で最も装甲が厚い。'),
              'EN': ('X-Morph Bomber', "Requests the human army's bomber, dropped empty at the flare for you to fly: its missile "
                                       'trigger drops heavy bombs from its bay (one every 0.4 s, a wide blast), its gun trigger '
                                       'fires the gun turrets on its wings (three every 1.3 s); the heaviest armour of the '
                                       'fixed-wing craft.')},
       gun=('Wing Gun Turrets', '翼上机炮塔', '翼銃塔', '翼上機砲塔'),
       air={'mark': 7054.0, 'mass': 30000.0, 'body': 'xmBomber', 'role': 'strike', 'ram': 10.0,
            'guns': ('att_left_turret_muzzle_1', 'att_right_turret_muzzle_1'),
            'gun': {'AmmoCount': 600.0, 'AmmoDamage': 150.0, 'FireInterval': 78.0, 'FireBurstCount': 3.0,
                    'FireBurstInterval': 12.0},
            'stores': (Arm('bombs', ('bombs',), 'bomb', ('Heavy Bombs', '大型爆弾', '重磅炸彈', '重磅炸弹', '대형 폭탄'), 20,
                           store=('MK82', {'damage': 11400.0, 'blast': 12.0, 'interval': 24.0})),)},
       # models/xm_large_bomber_core: 20.1 x 2.8 x 15.0 m
       eye=(0.0, 5.0, -13.0), durability=9000.0, reload=20000, level=4.0, after='EDF6VC_CALL_XM_LARGE_GUNSHIP_AA'),
    # bosses
    # The JAGUAR Type S (the game's highway_tank boss, story_germany: 「the prototype JAGUAR Type S tank」): a 46 m tracked
    # hull of eighteen parts (xm_model.assemble: the tracks, the turrets, the pods; tools/prep_xmorph.py BELT_FLOOR), a
    # tracked unit on the V603 like the tanks. Its main turret's gun (main_turret_projectile: fire_rate 10,
    # enemy_bullet_tank_boss_projectile: 500 and a 16 m shock wave; its twin main_turret_artillery lobs the same round)
    # on the primary, its one barrel's two holders in turn (a round every half FireInterval, as the battle tank's); its
    # parts' weapons on the secondary (Arm). Not ported: the sentry drones its hull launches (left_ / right_sentries
    # fire highway_tank_drone, a drone that flies and shoots, as their ammunition: docs/xmorph-bosses.md), the barrel's
    # laser pointer (a sight, no damage), its parts destroyed one by one (EDF: one durability), the side and front
    # turrets turning on their own (they fire along the main gun, as the heavy tank's machine-gun turret).
    _u('bosses/highway_tank', 'tracked', 'apocalypse', 'ported', 'JAGUAR Type S',
       texts={'SC': ('X-Morph 美洲豹 S 型', '请求人类军队的原型超重型坦克「美洲豹 S 型」（原作首领）：全长约 32 米。主炮塔的巨炮为主攻击，'
                                       '约每十秒一发、命中处大范围冲击波；车头两组巡航火箭（各六连发）、主炮塔顶的防空导弹（十二连发）、'
                                       '四座侧炮塔的高射炮、车头迫击炮与前炮塔的激光（照射三秒）各用一个开火键、与主炮同时发射，防空导弹和高射炮可当自动炮塔；装甲极厚。'),
              'CN': ('X-Morph 美洲豹 S 型', '請求人類軍隊的原型超重型坦克「美洲豹 S 型」（原作首領）：全長約 32 公尺。主砲塔的巨砲為主攻擊，'
                                       '約每十秒一發、命中處大範圍衝擊波；車頭兩組巡航火箭（各六連發）、主砲塔頂的防空飛彈（十二連發）、'
                                       '四座側砲塔的高射砲、車頭迫擊砲與前砲塔的雷射（照射三秒）各用一個開火鍵、與主砲同時發射，防空飛彈和高射砲可當自動砲塔；裝甲極厚。'),
              'JA': ('X-Morph ジャガー S 型', '人類軍の試作超重戦車「ジャガー S 型」（原作のボス）を要請する。全長約 32 m。主砲塔の巨砲'
                                        '（約 10 秒に 1 発、着弾点に広い衝撃波）を主攻撃とする。車体前部の巡航ロケット 2 基（各 6 連射）、'
                                        '主砲塔上の対空ミサイル（12 連射）、側面砲塔 4 基の高射砲、前部の迫撃砲、前部砲塔のレーザー（3 秒照射）はそれぞれ専用の発射キーで主砲と同時に撃てる。対空ミサイルと高射砲は自動砲塔にもなる。装甲は極めて厚い。'),
              'EN': ('X-Morph JAGUAR Type S', "Requests the human army's prototype super-heavy tank JAGUAR Type S (a boss in "
                                              "the game): about 32 m long. Its main turret's great gun on the primary attack, a "
                                              'round about every 10 s with a wide shock wave where it hits; each on its own '
                                              'fire key with the gun: the two front cruise rocket pods (six each), the AA missiles on '
                                              "the main turret (twelve), the four side turrets' flak, the front mortar and "
                                              "the front turret's laser (a 3 s beam); the AA missiles and the flak can be auto turrets; extremely heavy armour.")},
       gun=('Great Gun', '巨炮', '巨砲', '巨砲'),
       # main_turret_projectile: fire_rate 10, 500 (anti_core: DAMAGE) and a ShockWave radius 16 (x BLAST)
       guns={'AmmoCount': 20.0, 'AmmoDamage': 60000.0, 'AmmoExplosion': 24.0, 'FireInterval': 1200.0},
       arms=(Arm('rockets', ('front_left_rockets', 'front_right_rockets'), 'ground',
                 ('Cruise rockets', '巡航ロケット', '巡航火箭', '巡航火箭', '순항 로켓'), 36,
                 # front_left_ / front_right_rockets: fire_rate 10, six 0.5 s apart each (enemy_missile_anti_core_big_tank:
                 # speed 30, turning 15 deg/s, 33 + Area 8, homing on the core)
                 missile={'model': 'Cruise rocket', 'look': ('bullet_missile', 2.5), 'burn': 3.0, 'top': 200.0,
                          'accel': 150.0, 'max_g': 6.0, 'nav': 3.0, 'life': 15.0, 'damage': 3960.0, 'blast': 12.0,
                          'lock_range': 1500.0, 'lock_cone': 0.6, 'lock_time': 20.0, 'burst': 6.0, 'burst_gap': 30.0,
                          'interval': 600.0}, fire=2),
             Arm('aam', ('main_turret_anti_player_missiles_left', 'main_turret_anti_player_missiles_right'), 'air',
                 ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 48,
                 # main_turret_anti_player_missiles_left / _right: fire_rate 30 / 26, twelve 0.5 s apart
                 # (enemy_missile_anti_aa_gunship: speed 40, turning 100 deg/s, 15 anti_player + Area 3)
                 missile={'model': 'AA missile', 'look': ('bullet_missile', 1.2), 'burn': 2.0, 'top': 350.0,
                          'accel': 300.0, 'max_g': 35.0, 'nav': 4.0, 'life': 5.0, 'damage': 450.0, 'blast': 4.5,
                          'lock_range': 1500.0, 'lock_cone': 0.8, 'lock_time': 15.0, 'burst': 12.0, 'burst_gap': 30.0,
                          'interval': 1680.0}, fire=3, auto=True),
             Arm('flak', ('side_left_front_turret', 'side_right_front_turret', 'side_left_back_turret',
                          'side_right_back_turret'), 'gun',
                 ('Flak', '高射砲', '高射砲', '高射炮', '고사포'), 120, 'cannon_l',
                 # side_*_turret: InstantWeaponDef, fire_rate 7..10.6 each (enemy_flak_medium_boss: bursts 0.75 s on,
                 # 20 anti_player, Area 12): the four together a shell every 2.25 s
                 shell=('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                 numbers={'AmmoDamage': 600.0, 'AmmoExplosion': 18.0, 'AmmoSpeed': 30.0, 'FireInterval': 135.0,
                          'FireBurstCount': 1.0}, fire=4, auto=True),
             Arm('mortar', ('front_mortar',), 'gun', ('Mortar', '迫撃砲', '迫擊砲', '迫击炮', '박격포'), 10, 'cannon_l',
                 # front_mortar: ArtilleryWeaponDef, fire_rate 18 (enemy_artillery_boss_tank: 333 artillery, Area 10):
                 # the grenade launcher's lobbed round
                 shell=('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                 numbers={'AmmoDamage': 39960.0, 'AmmoExplosion': 15.0, 'FireInterval': 1080.0, 'FireBurstCount': 1.0}, fire=5),
             Arm('laser', ('front_turret',), 'gun', ('Laser', 'レーザー', '雷射', '激光', '레이저'), 900, 'cannon_l',
                 # front_turret: BeamWeaponDef, fire_delay 22, fire_duration 3 (enemy_laser_highway_tank: 100 a second,
                 # through three): the advanced tank's pulse stream (a pulse every 4 frames), 3 s every 25 s
                 shell=('V_506HELI_LASERCANNON01_L.SGO', 'LaserBullet01', 'mg'),
                 numbers={'AmmoDamage': 800.0, 'AmmoSpeed': 40.0, 'AmmoAlive': 20.0, 'AmmoIsPenetration': 1.0,
                          'FireInterval': 1500.0, 'FireBurstCount': 45.0, 'FireBurstInterval': 4.0}, fire=6)),
       # models/xm_highway_tank: 46 m long, 28 m wide; the main turret's ring y 16.25 at z -1.8, its roof ~y 21, the
       # gun's trunnion y 16.2 at z 3.2, its muzzle z 23.1
       camera={'model_eye': (0., 21.5, 0.), 'model_look_at': (0., 18., 40.), 'third_person_distance': 45.,
               'third_person_height': 22.},
       eye=(0.0, 21.8, 2.0), durability=100000.0, reload=60000, level=6.0,
       # rows are only ever appended (Unit.after): after the forts' guns, the requests' end when it was ported
       after='EDF6VC_CALL_XM_AA_CANNON_MACHINEGUN'),
    _u('bosses/four_leg_mech', 'boss'),
    _u('bosses/giant_mech_minotaur', 'boss'),
    _u('bosses/gunship', 'boss'),
    _u('bosses/gunship_drone', 'boss'),
    _u('bosses/highway_tank_drone', 'boss'),
    _u('bosses/tarantula_mk_vi', 'boss'),
    _u('bosses/tarantula_mk_vii', 'boss'),
    _u('bosses/warcar', 'boss'),
    _u('bosses/warcar_escort_helicopter', 'boss'),
    # civilians
    _u('civilian/car_ambulance', 'civilian'),
    _u('civilian/car_bus_old', 'civilian'),
    _u('civilian/car_combi_olive', 'civilian'),
    _u('civilian/car_generic_silver', 'civilian'),
    _u('civilian/car_police', 'civilian'),
    _u('civilian/car_police_van', 'civilian'),
    _u('civilian/helicopter_news_01', 'civilian'),
    _u('civilian/human_male_01', 'civilian'),
    _u('civilian/truck_firetruck', 'civilian'),
)}
assert all(u.family in FAMILIES for u in UNITS.values())
assert all(u.status in ('ported', 'planned') for u in UNITS.values())
# The families with their EDF-side builder (tools/make_xmorph.py: over make_tracked_combat on the V603, a tracked or
# wheeled unit through pylib/wt_tracked.py, an emplacement through pylib/emplacement_model.py's fortified base; the
# aircraft through pylib/xm_air_model.py; a car through pylib/car_model.py on the V512; a walker through pylib/xm_walker.py,
# the infantry through pylib/xm_infantry.py).
# The families requested as vehicles (tools/calls.py: a request row each); the infantry is a support soldier kind.
REQUESTED = ('tracked', 'wheeled', 'emplacement', 'heli', 'jet', 'car', 'mech')
PORTABLE = REQUESTED + ('infantry',)
V603 = ('tracked', 'wheeled', 'emplacement')
V512 = ('car',)          # pylib/car_model.py builds them on the V512 (the sedan's chassis)
# The hulls (pylib/wt_tracked.py builds their model folders on the V603: tools/prep_xmorph.py tracked / wheeled)
HULLS = ('tracked', 'wheeled')
WHEELED_FLOOR = 800.0    # a wheeled row's durability at least (its comment)


def check(u: Unit) -> None:
    """A ported row carries everything its registries read (a planned one nothing it would need)."""
    if u.status != 'ported':
        return
    why = [what for what, ok in (
        ('family', u.family in PORTABLE), ('like', bool(u.like) or u.family not in V603), ('title_en', bool(u.title_en)),
        ('texts', set(u.texts) == set(LANGS) and all(len(t) == 2 and all(t) for t in u.texts.values())),
        ('gun', not (u.gun or u.guns or u.arms) if u.family in V512
                else (len(u.gun) == 4 and all(u.gun)) or u.family not in REQUESTED),
        ('cab', bool(u.cab) == (u.family in V512)), ('eye', len(u.eye) == 3 or u.family in ('mech', 'infantry')),
        ('durability', u.durability > 0),
        # a request row's (REQUESTED); the infantry is a support soldier kind, no request
        ('reload', u.reload > 0 or u.family not in REQUESTED), ('level', u.level > 0 or u.family not in REQUESTED),
        ('a wheeled durability >= WHEELED_FLOOR', u.family != 'wheeled' or u.durability >= WHEELED_FLOOR),
        ('after', u.after.startswith('EDF6VC_CALL_') or (u.family not in REQUESTED and not u.after))) if not ok]
    if why:
        raise ValueError(f'{u.key}: a ported unit without {", ".join(why)} (pylib/xm_units.py)')


for _unit in UNITS.values():
    check(_unit)


def ported(family: str | None = None) -> list[Unit]:
    return [u for u in UNITS.values() if u.status == 'ported' and (family is None or u.family == family)]


def jet_name(key: str) -> str:
    """A jet's NPC kind (pylib/vcobjects.py JETS: no _mission, the test range places none)."""
    return f'edf6tr_{key}'


def jet_request(key: str) -> str:
    """...and its requested twin's (vcobjects.request_name of it): what its request (tools/calls.py Call.jet) brings."""
    return f'edf6tr_{key}_request_mission'


def folder(key: str) -> str:
    return UNITS[key].key


def v603() -> list[Unit]:
    """The ported units make_tracked_combat builds on the V603 (V603: tracked hulls and emplacements), in table order:
    what every V603 registry (vcobjects GROUND_VEHICLES / TRACKED, the requests, the cockpits, the test range) lists."""
    return [u for u in ported() if u.family in V603]


def v512() -> list[Unit]:
    """The ported units pylib/car_model.py builds on the V512 (V512: the cars), in table order."""
    return [u for u in ported() if u.family in V512]


def hulls() -> list[Unit]:
    """The ported units pylib/wt_tracked.py builds from their model folders (HULLS), in table order."""
    return [u for u in ported() if u.family in HULLS]


def requested() -> list[Unit]:
    """The ported units requested as ground vehicles (REQUESTED: the V603's and the walkers), in table order: what
    tools/calls.py makes a request row of and vcobjects.GROUND_VEHICLES lists."""
    return [u for u in ported() if u.family in REQUESTED]


def deployed(key: str) -> bool:
    """Its model folder is on this machine (obj_model.model_dir: the model pack's, EDF6VC_MODELS, the developer's),
    whole (model.json and model.npz): only then is the unit built and installed (CLAUDE.md: no stand-in)."""
    import os

    import obj_model
    d = obj_model.model_dir(folder(key))
    return d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz'))


def eye(u: Unit) -> tuple[float, float, float]:
    """Its sight camera in its EDF model's frame (the row's, at SCALE; an emplacement's above its base's top)."""
    x, y, z = u.eye
    base = EMPLACEMENT_BASE if u.family == 'emplacement' else 0.0
    return (round(x * SCALE, 4), round(base + (y - base) * SCALE, 4), round(z * SCALE, 4))


def camera(u: Unit) -> dict:
    """Its vehicle camera over the `like`'s (make_tracked_combat WT_ONLY): the row's, every length at SCALE."""
    def at(v):  # noqa: ANN001, ANN202 - a length or a point
        return tuple(round(c * SCALE, 4) for c in v) if isinstance(v, tuple) else round(v * SCALE, 4)
    return {k: at(v) for k, v in u.camera.items()}


def text(u: Unit, lang: str) -> tuple[str, str]:
    """Its request's (name, description) in `lang` (tools/calls.py KINDS): the row's, its model's source after it."""
    name, what = u.texts[lang]
    return name, what + SOURCE[lang]


def label(u: Unit) -> str:
    """What the installer and the test range call it (make_tracked_combat LABELS, vcobjects TRACKED)."""
    return u.texts['SC'][0]
