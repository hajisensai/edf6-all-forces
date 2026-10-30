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
    pylib/vcobjects.py            GROUND_VEHICLES (derived from its `like`'s: the native gun pair) and TRACKED
    pylib/wt_tracked.py           FOLDERS (folder())
    pylib/ledger.py               OWNERS          pylib/buildcache.py   GROUPS, COCKPIT_GROUPS, MODEL_INPUTS, generator()
    pylib/cockpit_fits_plugin.py  FITS (eye)      tools/calls.py        CALLS (after), KINDS (texts), GROUND_NOTES_BY_KIND
    tools/make_tracked_combat.py  LABELS, GUNS, NAMES, WT_ONLY and the kind sets over its `like`'s
    tools/make_xmorph.py          the one generator of every unit (builder(key)); tools/build_release.py RELEASE_MODELS
    testrange/gen.py              GROUND_MISSION (the range's own tables follow vcobjects.TRACKED)

Families (the chassis an EDF6 class drives; tools/prep_xmorph.py has one preparer per family):
    tracked      V603 (Vehicle603_Flak) through pylib/wt_tracked.py, like the War Thunder vehicles. It fires its
                 `like`'s native gun pair (the two V603 holders: GUNS / NAMES / the shell over the `like`'s); the
                 `like`'s stores (missiles, a second belt) are vehicle-specific store rows (vcobjects STORES, with
                 their own muzzles) and are not inherited
    wheeled      V603 too, the same way (tools/prep_xmorph.py wheeled: its real wheels on the V603's wheel stations;
                 it turns as the V603, skid steering); the Grape's wheeled class (Vehicle401_Striker) has one weapon
                 holder and a skeleton of its own, which no generator here builds a model on
    mech         planned: the Proteus / Balam walkers
    emplacement  a fort's gun on pylib/emplacement_model.py's fortified base (the V603, tools/prep_xmorph.py emplacement)
    heli, jet    planned: the V506 frame / the jets (pylib/jet_models.py)
    infantry     planned
    boss         planned: the game's bosses (several bodies each)
    civilian     planned: the maps' traffic and bystanders (their meshes through an inherited blueprint)
    structure    planned: buildings without a weapon (the barracks): the maps' props
"""
from __future__ import annotations

from dataclasses import dataclass, field

FAMILIES = ('tracked', 'wheeled', 'mech', 'emplacement', 'heli', 'jet', 'infantry', 'boss', 'civilian', 'structure')
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
DAMAGE = {'anti_core': 120.0, 'artillery': 120.0, 'laser': 120.0, 'anti_player': 30.0, 'emp': 30.0}
BLAST = 1.5


@dataclass(frozen=True)
class Arm:
    """A unit's weapon besides its main one (the game's other Weapon blocks: missile pods, machine-gun turrets, the EMP
    flak), an independent secondary on the seat's second trigger (vcobjects STORES <UNIT>_<KEY>, src/payload.cpp
    IndependentSecondary: fired with the main gun, the unit's arms cycled among themselves). It leaves from the game's
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
    arms: tuple[Arm, ...] = ()      # its other weapons (Arm)
    camera: dict = field(default_factory=dict)  # its vehicle camera over the `like`'s (make_tracked_combat WT_ONLY)
    eye: tuple[float, ...] = ()  # the gunner's sight camera on the turret (cockpit_fits_plugin, remote_display), m
    durability: float = 0.0  # vcobjects GroundVehicle.durability
    reload: int = 0          # its request (tools/calls.py Call): ReloadTime base
    level: float = 0.0       # ... and the weapon table level
    # The call id its request row comes right after in tools/calls.py CALLS: rows are only ever appended (saves keep
    # weapons by row), so a newly ported unit names the call that ended CALLS when it was ported, never an earlier one.
    after: str = ''


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
       # models/xm_medium_tank_regular: hull y 0..2.17, 9.6 m long; the turret roof y 3.0..3.4 (z -3.2..2.5; a mast
       # to y 5.2 at z -1..0), the gun's axis y 2.25, its muzzle z 7.5: the sight camera over the roof's front.
       camera={'model_eye': (0., 3.2, 0.5), 'model_look_at': (0., 2.8, 9.), 'third_person_distance': 11.,
               'third_person_height': 4.6},
       eye=(0.0, 3.55, 2.0), durability=2500.0, reload=10000, level=1.5, after='EDF6VC_CALL_SORTIE_GUIDED'),
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
       # models/xm_medium_tank_advanced: 12.8 m long, the turret y 3.3..5.5 (its roof ~5.2), z -4.5..1.7; the gun's axis
       # y 4.3, its muzzle z 7.0
       camera={'model_eye': (0., 5.0, -0.3), 'model_look_at': (0., 4.6, 9.), 'third_person_distance': 11.,
               'third_person_height': 5.0},
       eye=(0.0, 5.35, 1.25), durability=5000.0, reload=14000, level=2.5, after='EDF6VC_CALL_XM_MEDIUM_TANK_REGULAR'),
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
       # models/xm_medium_tank_aa: 8.8 m long; the turret at the back (z -3.0..-1.95, roof y ~6.0), its two guns at
       # x +-1.5, y 4.0
       camera={'model_eye': (0., 5.6, -2.4), 'model_look_at': (0., 5.0, 7.), 'third_person_distance': 8.,
               'third_person_height': 5.6},
       eye=(0.0, 6.15, -2.1), durability=1500.0, reload=10000, level=1.8, after='EDF6VC_CALL_XM_MEDIUM_TANK_ADVANCED'),
    # The heavy tank: a twin heavy gun (enemy_bullet_big_anti_core, a pair every 3 s), machine-gun and missile turrets
    # (not ported), seven times the battle tank's armour (1200). The Apocalypse's twin gun, the two barrels in turn.
    _u('ground/large_tank', 'tracked', 'apocalypse', 'ported', 'Heavy Tank',
       texts={'SC': ('X-Morph 重型坦克', '请求一辆人类军队的重型坦克：炮塔上的双管重炮为主攻击，两根炮管每三秒齐射一次；副武器（切换）为机枪塔（十发连射）、'
                                     '防空导弹（五发连射）和车头火箭；装甲厚重。'),
              'CN': ('X-Morph 重型坦克', '請求一輛人類軍隊的重型坦克：砲塔上的雙管重砲為主攻擊，兩根砲管每三秒齊射一次；副武器（切換）為機槍塔（十發連射）、'
                                     '防空飛彈（五發連射）和車頭火箭；裝甲厚重。'),
              'JA': ('X-Morph 重戦車', '人類軍の重戦車を要請する。砲塔の連装重砲（3 秒ごとに 2 門斉射）を主攻撃とする。副武器（切替）は機銃塔（10 発連射）、'
                                    '対空ミサイル（5 発連射）、車体前部のロケット。装甲は厚い。'),
              'EN': ('X-Morph Heavy Tank', "Requests the human army's heavy tank: its turret's twin heavy gun on the primary "
                                           'attack, both barrels every 3 s; on the secondary (switched) its machine-gun '
                                           'turret (bursts of ten), AA missiles (bursts of five) and front rockets; '
                                           'heavy armour.')},
       gun=('Twin Heavy Gun', '双管重炮', '連装重砲', '雙管重砲'),
       # main_gun: fire_rate 3, both barrels (enemy_bullet_big_anti_core: 30 anti_core, no Area): the pair every 3 s
       # (the game's 0.25 s between them: here together)
       guns={'AmmoCount': 40.0, 'AmmoDamage': 3600.0, 'AmmoExplosion': 0.0, 'FireInterval': 180.0}, alternate=False,
       arms=(Arm('mg', ('machinegun',), 'gun', ('Machine gun', '機銃', '機槍', '机枪', '기관총'), 600, 'cannon_l',
                 # machinegun: fire_rate 1.5, ten 0.07 s apart (enemy_bullet_small_fast: speed 100, 5 anti_player)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 150.0, 'AmmoSpeed': 20.0, 'AmmoAlive': 60.0, 'FireInterval': 90.0,
                          'FireBurstCount': 10.0, 'FireBurstInterval': 4.0}),
             Arm('aam', ('missile_turret',), 'air', ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 40,
                 # missile_turret: fire_rate 4, five 0.3 s apart (enemy_missile_anti_aa_small: speed 50, 8 anti_player)
                 missile={'model': 'AA missile', 'look': ('bullet_missile', 1.2), 'burn': 2.0, 'top': 350.0,
                          'accel': 300.0, 'max_g': 35.0, 'nav': 4.0, 'life': 5.0, 'damage': 240.0, 'blast': 3.0,
                          'lock_range': 1500.0, 'lock_cone': 0.8, 'lock_time': 15.0, 'burst': 5.0, 'burst_gap': 18.0,
                          'interval': 240.0}),
             Arm('rockets', ('front_missile_1', 'front_missile_2'), 'air', ('Rockets', 'ロケット', '火箭', '火箭', '로켓'), 40,
                 # front_missile_1 / _2: fire_rate 2, two 0.4 s apart each (enemy_rocket_small: speed 60, accel 50,
                 # turning 15 deg/s: barely guided; 10 anti_player): both pods, four every 2 s
                 missile={'model': 'Rocket', 'look': ('bullet_rocket', 1.0), 'burn': 1.5, 'top': 400.0,
                          'accel': 300.0, 'max_g': 6.0, 'nav': 2.0, 'life': 4.0, 'damage': 300.0, 'blast': 3.0,
                          'lock_range': 1200.0, 'lock_cone': 0.4, 'lock_time': 20.0, 'burst': 2.0, 'burst_gap': 24.0,
                          'interval': 120.0})),
       # models/xm_large_tank: 17.8 m long, the turret y 3.2..7.5 (roof ~6.7), z -2.3..3.3; the guns at x +-1.07,
       # y 4.55, their muzzles z 10.8
       camera={'model_eye': (0., 6.4, 0.6), 'model_look_at': (0., 5.2, 14.), 'third_person_distance': 15.,
               'third_person_height': 6.8},
       eye=(0.0, 6.85, 2.9), durability=9000.0, reload=18000, level=3.5, after='EDF6VC_CALL_XM_MEDIUM_TANK_AA'),
    # The EMP tank: the heavy tank's twin gun on a bigger hull with EMP flak, gun turrets and missiles (not ported), the
    # most armour of the tracked units (2000).
    _u('ground/large_tank_emp', 'tracked', 'apocalypse', 'ported', 'EMP Tank',
       texts={'SC': ('X-Morph EMP 坦克', '请求一辆人类军队的 EMP 坦克：比重型坦克更大的车体，炮塔上的双管重炮为主攻击，每三秒两门齐射两轮；'
                                      '副武器（切换）为 EMP 高射弹、两座防空机炮塔（十五发连射）与导弹塔（二十发连射）；履带单位中装甲最厚。'),
              'CN': ('X-Morph EMP 坦克', '請求一輛人類軍隊的 EMP 坦克：比重型坦克更大的車體，砲塔上的雙管重砲為主攻擊，每三秒兩門齊射兩輪；'
                                      '副武器（切換）為 EMP 高射彈、兩座防空機砲塔（十五發連射）與飛彈塔（二十發連射）；履帶單位中裝甲最厚。'),
              'JA': ('X-Morph EMP 戦車', '人類軍の EMP 戦車を要請する。重戦車より大きな車体で、砲塔の連装重砲（3 秒ごとに 2 門斉射を 2 回）を'
                                      '主攻撃とする。副武器（切替）は EMP 炸裂弾、対空機関砲塔 2 基（15 発連射）、ミサイル塔（20 発連射）。装甲は装軌車両で最も厚い。'),
              'EN': ('X-Morph EMP Tank', "Requests the human army's EMP tank: a hull bigger than the heavy tank's, its "
                                         "turret's twin heavy gun on the primary attack, both barrels twice every 3 s; "
                                         'on the secondary (switched) EMP flak, two AA gun turrets (bursts of '
                                         'fifteen) and a missile turret (bursts of twenty); the heaviest armour of '
                                         'the tracked units.')},
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
             Arm('aa', ('aa_left_turret', 'aa_right_turret'), 'gun', ('AA guns', '対空機関砲', '防空機砲', '防空机炮', '대공 기관포'), 600, 'cannon_l',
                 # aa_left_turret / aa_right_turret: fire_rate 5, fifteen 0.1 s apart each (enemy_bullet_medium: 10
                 # anti_player)
                 shell=('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg'),
                 numbers={'AmmoDamage': 300.0, 'AmmoSpeed': 12.0, 'AmmoAlive': 90.0, 'FireInterval': 300.0,
                          'FireBurstCount': 15.0, 'FireBurstInterval': 6.0, 'FireCount': 2.0}),
             Arm('aam', ('missile_turret',), 'air', ('AA missiles', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), 80,
                 # missile_turret: fire_rate 5, twenty 0.1 s apart (enemy_missile_anti_aa_small: 8 anti_player)
                 missile={'model': 'AA missile', 'look': ('bullet_missile', 1.2), 'burn': 2.0, 'top': 350.0,
                          'accel': 300.0, 'max_g': 35.0, 'nav': 4.0, 'life': 5.0, 'damage': 240.0, 'blast': 3.0,
                          'lock_range': 1500.0, 'lock_cone': 0.8, 'lock_time': 15.0, 'burst': 20.0, 'burst_gap': 6.0,
                          'interval': 300.0})),
       # models/xm_large_tank_emp: 18.2 m long, the turret y 4.6..8.9 (roof ~8.85), z -1.1..4.9; the guns at x +-0.49,
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
       # models/xm_medium_artillery_regular: 11.6 m long; the turret at the back (z -5.1..-0.9, roof ~3.3), the gun's
       # axis y 2.45, its muzzle z 5.5
       camera={'model_eye': (0., 3.2, -2.9), 'model_look_at': (0., 2.9, 8.), 'third_person_distance': 10.,
               'third_person_height': 4.4},
       eye=(0.0, 3.45, -1.3), durability=1500.0, reload=14000, level=2.5, after='EDF6VC_CALL_XM_LARGE_TANK_EMP'),
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
       # models/xm_medium_shield_generator: 11.7 m long; the laser turret is small (x +-0.27, y 3.4..3.9, z 1.3..1.8),
       # its muzzle y 4.2, z 3.1
       camera={'model_eye': (0., 4.0, 1.5), 'model_look_at': (0., 3.8, 9.), 'third_person_distance': 11.,
               'third_person_height': 5.0},
       eye=(0.0, 4.1, 1.5), durability=4000.0, reload=12000, level=2.2, after='EDF6VC_CALL_XM_MEDIUM_ARTILLERY_REGULAR'),
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
       eye=(0.0, 4.1, -4.6), durability=800.0, reload=16000, level=2.5, traverse=30.0,
       after='EDF6VC_CALL_XM_AA_CANNON_MACHINEGUN'),
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
       eye=(0.0, 4.95, 1.7), durability=3700.0, reload=12000, level=2.0,
       after='EDF6VC_CALL_XM_MEDIUM_MISSILE_LAUNCHER'),
    # The cargo truck: no weapon in the game (no WeaponComp; its skeleton's turret bones have no mesh): not ported.
    _u('ground/medium_truck_cargo', 'wheeled', title='Cargo Truck'),
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
       eye=(0.0, 3.35, 0.1), durability=800.0, reload=6000, level=0.8, after='EDF6VC_CALL_XM_MEDIUM_APC'),
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
    _u('ground/small_mech', 'mech', title='Light Mech'),
    _u('ground/large_mech', 'mech', title='Heavy Mech'),
    _u('ground/small_infantry', 'infantry', title='Infantry'),
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
       # models/xm_aa_cannon (on the base, 1.3 m up): the gun's trunnion y 4.7 + 1.3, z -0.63; its muzzles x +-0.85,
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
    _u('air/small_helicopter_core', 'heli', title='Light Helicopter'),
    _u('air/medium_helicopter_core', 'heli', title='Helicopter'),
    _u('air/large_helicopter_core', 'heli', title='Heavy Helicopter'),
    _u('air/large_helicopter_transport', 'heli', title='Transport Helicopter'),
    _u('air/large_gunship_core', 'heli', title='Gunship'),
    _u('air/medium_fighter_aa', 'jet', title='Fighter'),
    _u('air/medium_gunship_aa', 'jet', title='Attack Plane'),
    _u('air/large_gunship_aa', 'jet', title='Heavy Attack Plane'),
    _u('air/large_bomber_core', 'jet', title='Bomber'),
    # bosses
    _u('bosses/four_leg_mech', 'boss'),
    _u('bosses/giant_mech_minotaur', 'boss'),
    _u('bosses/gunship', 'boss'),
    _u('bosses/gunship_drone', 'boss'),
    _u('bosses/highway_tank', 'boss'),
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
# The families with their EDF-side builder (tools/make_xmorph.py over make_tracked_combat: all on the V603, a tracked
# or wheeled unit through pylib/wt_tracked.py, an emplacement through pylib/emplacement_model.py's fortified base).
PORTABLE = ('tracked', 'wheeled', 'emplacement')
V603 = ('tracked', 'wheeled', 'emplacement')
# The hulls (pylib/wt_tracked.py builds their model folders on the V603: tools/prep_xmorph.py tracked / wheeled)
HULLS = ('tracked', 'wheeled')
WHEELED_FLOOR = 800.0    # a wheeled row's durability at least (its comment)


def check(u: Unit) -> None:
    """A ported row carries everything its registries read (a planned one nothing it would need)."""
    if u.status != 'ported':
        return
    why = [what for what, ok in (
        ('family', u.family in PORTABLE), ('like', bool(u.like)), ('title_en', bool(u.title_en)),
        ('texts', set(u.texts) == set(LANGS) and all(len(t) == 2 and all(t) for t in u.texts.values())),
        ('gun', len(u.gun) == 4 and all(u.gun)), ('eye', len(u.eye) == 3), ('durability', u.durability > 0),
        ('reload', u.reload > 0), ('level', u.level > 0),
        ('a wheeled durability >= WHEELED_FLOOR', u.family != 'wheeled' or u.durability >= WHEELED_FLOOR),
        ('after', u.after.startswith('EDF6VC_CALL_'))) if not ok]
    if why:
        raise ValueError(f'{u.key}: a ported unit without {", ".join(why)} (pylib/xm_units.py)')


for _unit in UNITS.values():
    check(_unit)


def ported(family: str | None = None) -> list[Unit]:
    return [u for u in UNITS.values() if u.status == 'ported' and (family is None or u.family == family)]


def folder(key: str) -> str:
    return UNITS[key].key


def v603() -> list[Unit]:
    """The ported units make_tracked_combat builds on the V603 (V603: tracked hulls and emplacements), in table order:
    what every V603 registry (vcobjects GROUND_VEHICLES / TRACKED, the requests, the cockpits, the test range) lists."""
    return [u for u in ported() if u.family in V603]


def hulls() -> list[Unit]:
    """The ported units pylib/wt_tracked.py builds from their model folders (HULLS), in table order."""
    return [u for u in ported() if u.family in HULLS]


def deployed(key: str) -> bool:
    """Its model folder is on this machine (obj_model.model_dir: the model pack's, EDF6VC_MODELS, the developer's),
    whole (model.json and model.npz): only then is the unit built and installed (CLAUDE.md: no stand-in)."""
    import os

    import obj_model
    d = obj_model.model_dir(folder(key))
    return d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz'))


def text(u: Unit, lang: str) -> tuple[str, str]:
    """Its request's (name, description) in `lang` (tools/calls.py KINDS): the row's, its model's source after it."""
    name, what = u.texts[lang]
    return name, what + SOURCE[lang]


def label(u: Unit) -> str:
    """What the installer and the test range call it (make_tracked_combat LABELS, vcobjects TRACKED)."""
    return u.texts['SC'][0]
