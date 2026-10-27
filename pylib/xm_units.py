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
    wheeled      planned: a wheeled chassis
    mech         planned: the Proteus / Balam walkers
    emplacement  planned: the fixed emplacements (pylib/emplacement_model.py)
    heli, jet    planned: the V506 frame / the jets (pylib/jet_models.py)
    infantry     planned
    boss         planned: the game's bosses (several bodies each)
    civilian     planned: their entities carry no mesh of their own (a prefab's)
"""
from __future__ import annotations

from dataclasses import dataclass, field

FAMILIES = ('tracked', 'wheeled', 'mech', 'emplacement', 'heli', 'jet', 'infantry', 'boss', 'civilian')
LANGS = ('SC', 'CN', 'JA', 'EN')   # tools/calls.py KINDS' languages (KR reads EN)
# Where every request's text says its model is from (each language's), after the unit's own description.
SOURCE = {'SC': '外观取自 EXOR Studios 的游戏《X-Morph: Defense》（人类一方的单位）。',
          'CN': '外觀取自 EXOR Studios 的遊戲《X-Morph: Defense》（人類一方的單位）。',
          'JA': '外観は EXOR Studios のゲーム『X-Morph: Defense』の人類側ユニットのモデル。',
          'EN': "Model: the human side's unit from EXOR Studios' X-Morph: Defense."}


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
       texts={'SC': ('X-Morph 战斗坦克', '请求一辆人类军队的战斗坦克：中型坦克，炮塔上一门坦克炮发射高爆弹为主攻击，约每秒一发，'
                                     '装甲中等。'),
              'CN': ('X-Morph 戰鬥坦克', '請求一輛人類軍隊的戰鬥坦克：中型坦克，砲塔上一門坦克砲發射高爆彈為主攻擊，約每秒一發，'
                                     '裝甲中等。'),
              'JA': ('X-Morph バトルタンク', '人類軍のバトルタンクを要請する。中戦車で、砲塔の戦車砲（榴弾、約 1 秒に 1 発）を主攻撃とする。'
                                       '装甲は中程度。'),
              'EN': ('X-Morph Battle Tank', "Requests the human army's battle tank: a medium tank, its turret's tank gun "
                                            'firing HE shells on the primary attack, about one a second; medium armour.')},
       gun=('Tank Gun', '坦克炮', '戦車砲', '坦克砲'),
       guns={'AmmoCount': 40.0, 'AmmoDamage': 600.0, 'AmmoExplosion': 5.0, 'FireInterval': 120.0},
       # models/xm_medium_tank_regular: hull y 0..2.17, 9.6 m long; the turret roof y 3.0..3.4 (z -3.2..2.5; a mast
       # to y 5.2 at z -1..0), the gun's axis y 2.25, its muzzle z 7.5: the sight camera over the roof's front.
       camera={'model_eye': (0., 3.2, 0.5), 'model_look_at': (0., 2.8, 9.), 'third_person_distance': 11.,
               'third_person_height': 4.6},
       eye=(0.0, 3.55, 2.0), durability=2500.0, reload=10000, level=1.5, after='EDF6VC_CALL_SORTIE_GUIDED'),
    _u('ground/medium_tank_advanced', 'tracked', 'apocalypse', title='Advanced Tank'),
    _u('ground/medium_tank_aa', 'tracked', 'tunguska', title='Anti-Air Tank'),
    _u('ground/large_tank', 'tracked', 'apocalypse', title='Heavy Tank'),
    _u('ground/large_tank_emp', 'tracked', 'apocalypse', title='EMP Tank'),
    _u('ground/medium_artillery_regular', 'tracked', 'pzh2000', title='Artillery'),
    _u('ground/medium_shield_generator', 'tracked', 'apocalypse', title='Shield Generator'),
    # ground: wheeled
    _u('ground/medium_missile_launcher', 'wheeled', 'tos1', title='Missile Launcher'),
    _u('ground/medium_apc', 'wheeled', 'bmpt', title='APC'),
    _u('ground/medium_truck_cargo', 'wheeled', title='Cargo Truck'),
    _u('ground/small_humvee', 'wheeled', title='Humvee'),
    _u('ground/small_buggy', 'wheeled', title='Buggy'),
    # walkers, foot
    _u('ground/small_mech', 'mech', title='Light Mech'),
    _u('ground/large_mech', 'mech', title='Heavy Mech'),
    _u('ground/small_infantry', 'infantry', title='Infantry'),
    # forts
    _u('fort/aa_cannon', 'emplacement', 'flak88', title='AA Cannon'),
    _u('fort/aa_cannon_machinegun', 'emplacement', 'gdf005', title='AA Machine Gun'),
    _u('fort/bunker_barracks', 'emplacement', title='Barracks'),
    _u('fort/bunker_barracks_02', 'emplacement', title='Barracks'),
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
# Only the tracked family has its EDF-side builder (tools/make_xmorph.py over make_tracked_combat) so far.
PORTABLE = ('tracked',)


def check(u: Unit) -> None:
    """A ported row carries everything its registries read (a planned one nothing it would need)."""
    if u.status != 'ported':
        return
    why = [what for what, ok in (
        ('family', u.family in PORTABLE), ('like', bool(u.like)), ('title_en', bool(u.title_en)),
        ('texts', set(u.texts) == set(LANGS) and all(len(t) == 2 and all(t) for t in u.texts.values())),
        ('gun', len(u.gun) == 4 and all(u.gun)), ('eye', len(u.eye) == 3), ('durability', u.durability > 0),
        ('reload', u.reload > 0), ('level', u.level > 0), ('after', u.after.startswith('EDF6VC_CALL_'))) if not ok]
    if why:
        raise ValueError(f'{u.key}: a ported unit without {", ".join(why)} (pylib/xm_units.py)')


for _unit in UNITS.values():
    check(_unit)


def ported(family: str | None = None) -> list[Unit]:
    return [u for u in UNITS.values() if u.status == 'ported' and (family is None or u.family == family)]


def folder(key: str) -> str:
    return UNITS[key].key


def text(u: Unit, lang: str) -> tuple[str, str]:
    """Its request's (name, description) in `lang` (tools/calls.py KINDS): the row's, its model's source after it."""
    name, what = u.texts[lang]
    return name, what + SOURCE[lang]


def label(u: Unit) -> str:
    """What the installer and the test range call it (make_tracked_combat LABELS, vcobjects TRACKED)."""
    return u.texts['SC'][0]
