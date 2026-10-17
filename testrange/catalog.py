"""What the test range can place: one catalog for the range's own vehicle / enemy lists (testrange/gen.py VEHICLES,
ENEMIES, testrange/testrange.pyw) and for the plugin's placement (the M map's 「摆放」 panel in the test range and the
debug spawn tool: src/spawn_catalog.inc, written from here by tools/gen_spawn_catalog.py).

The user, 2026-10-17: 「测试场的东西该更新了……测试场支持m里面手动摆放怪物和各种载具之类的」. Rather than a third hand-copied
list, every row comes from a registry the tools already keep:
  - STOCK_VEHICLES / STOCK_ENEMIES: EDF6's own objects (Root.cpk) that the range and the stock missions place. A
    vehicle's SGO carries `mission_setup` (tests/debug_spawn_resource_audit.py reads each in Root.cpk);
  - the range's own vehicles (testrange/gen.py DERIVED: written into Mods/OBJECT by the range's install) that the player
    drives empty (gen.PLACEABLE_DERIVED);
  - the installer's vehicles (pylib/vcobjects.py GROUND_VEHICLES whose SGO already is a mission one: the civilian sedan);
  - EDF4.1's ported objects (edf41port/objects.json, installed into Mods by tools/edf41_objects.py: pre-converted, no
    EDF4.1 needed; a row is there in the game only when its files are, which the plugin and gen.install check);
  - EDF5 Mission Pack 1's fortress (edf5port/dlc/registry.json, tools/edf5_dlc.py).
tools/selftest.py spawn_catalog_* checks that nothing a registry adds is missed (each 4.1 object is a row or named in
EDF41_LEFT_OUT with why) and that src/spawn_catalog.inc is what this writes.

A row: `sgo` the object name as the game's app:/object/ path has it (no folder prefix, no .sgo; lower case; a 4.1 DLC
object keeps its folder: 'edf41mp01/giantant04'), its label, its category (the panel's tab), where its file comes from
(`source`), how far over the clicked ground it is made (`lift`), whether it flies (the range's waves) and whether the
plugin preloads it in the test range by default (`preload`; ini RangePlaceAll=1 preloads every row).
"""
from __future__ import annotations

import json
import os
import re
import sys
from dataclasses import dataclass
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
# The registries: bundled next to the frozen installer (tools/build_release.py --add-data, as tools/edf41_objects.py
# reads them), else the source tree's.
ROOT = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.normpath(os.path.join(HERE, '..'))  # type: ignore[attr-defined]
EDF41_REGISTRY = os.path.join(ROOT, 'edf41port', 'objects.json')
EDF5_DLC_REGISTRY = os.path.join(ROOT, 'edf5port', 'dlc', 'registry.json')

# The panel's tabs, in order (src/debug_spawn.h Category; aircraft and soldiers are the plugin's own rows, written in
# src/debug_spawn.h: the support calls and the support soldiers).
CATEGORIES = ('vehicle', 'ours', 'aircraft', 'enemy', 'enemy41', 'soldier')
# Where a row's file is: Root.cpk ('stock'), Mods/OBJECT written by the range's install ('range'), by the installer
# ('installer', 'edf41', 'edf5dlc').
SOURCES = ('stock', 'range', 'installer', 'edf41', 'edf5dlc')


@dataclass(frozen=True)
class Row:
    sgo: str
    label: str
    category: str
    source: str
    lift: float = 0.5
    flying: bool = False
    preload: bool = True

    @property
    def path(self) -> str:
        """The game's path of its SGO (what the plugin preloads and CreateObject reads)."""
        return f'app:/object/{self.sgo.upper()}.sgo'

    @property
    def rel(self) -> str:
        """Its SGO under Mods/ (a 'stock' row's is in Root.cpk instead)."""
        return f'OBJECT/{self.sgo.upper()}.SGO'

    @property
    def ident(self) -> str:
        """The log's id (ASCII; src/debug_spawn.h Entry::id): its SGO's name, a folder's slash as '/'."""
        return self.sgo.upper()


# EDF6's own vehicles that the stock missions place (their `_mission` SGOs, or the unarmed ones with a mission_setup).
# (sgo, label)
STOCK_VEHICLES: tuple[tuple[str, str], ...] = (
    ('vehicle403_tank_mission', '坦克 403'),
    ('vehicle404_bigtank', '大型坦克 404'),
    ('v505_tank_mission', '布莱克战车 505'),
    ('v603_flak_mission', '高射炮车 603'),
    ('v510_maser_mission', 'EMC 510'),
    ('v605_barga_cannon_mission', '巴尔加炮 605'),
    ('v507_rescuetank_ai', '救援装甲车 507'),
    ('v504_begaruta_mission', '机甲 Begaruta 504'),
    ('vehicle407_bigbegaruta_mission', '大型机甲 407'),
    ('v612_nix_g_mission', '机甲 Nix G 612'),
    ('v614_proteus_mk2_mission', '机甲 Proteus 614'),
    ('v608_oldrobot_g_mission', '旧型机甲 608'),
    ('v515_retrobalam_mission', '巨型机甲 Balam 515'),
    ('v503_bike', '摩托 503'),
    ('v613_bike', '摩托 613'),
    ('v512_keitruck', '轻卡车 512'),
)

# EDF6's own enemies: those the range's waves and the grand battle use, and the debug spawn tool's. (sgo, label, flying,
# lift: m over the clicked ground; a flier is made up there and flies off on its own, a ship is made at its height)
STOCK_ENEMIES: tuple[tuple[str, str, bool, float], ...] = (
    ('giantant01', '巨蚁', False, 0.5),
    ('e650_giantant01', '巨蚁（EDF6）', False, 0.5),
    ('giantspider01', '巨蜘蛛', False, 0.5),
    ('giantantqueen', '母体（女王巨蚁）', False, 0.5),
    ('e651_spider01_light', '蜘蛛（轻）', False, 0.5),
    ('e503_frog_af_leader', '青蛙兵（队长）', False, 0.5),
    ('e503_frog_af', '青蛙兵', False, 0.5),
    ('e503_armorfrog_af', '装甲青蛙', False, 0.5),
    ('e601_martian_gs', '火星人 GS', False, 0.5),
    ('e601_martian_ls', '火星人 LS', False, 0.5),
    ('e602_berserker', '狂战士 E602', False, 0.5),
    ('e513_shieldbearer', '护盾运载者 E513', False, 0.5),
    ('giantbee01charge', '巨蜂（飞）', True, 20.0),
    ('e507_goldufo', '金色 UFO（飞）', True, 30.0),
    ('e515_imperialufo', '帝国 UFO（飞）', True, 30.0),
    ('e605_spinnerufo', 'UFO E605（飞）', True, 30.0),
    ('dragonsmall401', '小龙（飞）', True, 30.0),
    ('e508_carrier', '传送舰（飞）', True, 250.0),
    ('e611_timeship', '星导者新舰（飞）', True, 400.0),
    ('e511_mothership_edf6', '母舰（飞）', True, 900.0),
    ('shootingtarget', '训练靶子（不动）', False, 0.5),
)

# EDF4.1's objects by class: the panel's category and words, how far up it is made, whether it flies.
EDF41_CLASSES: dict[str, tuple[str, str, float, bool]] = {
    'Vehicle505_Tank': ('vehicle', '4.1 坦克', 0.5, False),
    'Vehicle501_FortressRobo': ('vehicle', '4.1 巨型机甲 Balam', 0.5, False),
    'AntHill': ('enemy41', '4.1 蚁巢', 0.5, False),
    'BigNest': ('enemy41', '4.1 巨型巢穴', 0.5, False),
    'Deiroi': ('enemy41', '4.1 迪洛伊', 0.5, False),
    'AlienTrailer': ('enemy41', '4.1 护盾运载者', 0.5, False),   # EDF6's E513 shield bearer is this class too
    'EDF6_Berserker_Large': ('enemy41', '4.1 赫克托', 0.5, False),
    'MovingFortress': ('enemy41', '4.1 四足步行要塞', 0.5, False),
    'GiantAnt': ('enemy41', '4.1 巨蚁', 0.5, False),
    'GiantBee': ('enemy41', '4.1 巨蜂女王（飞）', 20.0, True),
    'Nephila': ('enemy41', '4.1 巨型蜘蛛', 0.5, False),
    'DragonSmall': ('enemy41', '4.1 龙（飞）', 30.0, True),
    'UfoSmall515': ('enemy41', '4.1 UFO（飞）', 40.0, True),
    'UfoSmall507': ('enemy41', '4.1 UFO（飞）', 40.0, True),
    'UfoCarrier508': ('enemy41', '4.1 运输船（飞）', 150.0, True),
    'Transporter_Formation': ('enemy41', '4.1 运输船编队（飞）', 150.0, True),
    'UfoRobo': ('enemy41', '4.1 UFO 机器人（飞船 / 机器人形态）', 150.0, True),
    'UfoMother301': ('enemy41', '4.1 母船（飞）', 300.0, True),
    'UfoMother401': ('enemy41', '4.1 地球吞噬者（飞）', 300.0, True),
}
# Registry objects that are no row, and why (selftest: every other one is).
EDF41_LEFT_OUT_CLASSES: dict[str, str] = {
    'AssultSoldier': 'a friendly 4.1 soldier: the plugin makes soldiers only as its support soldiers (support_soldier.cpp), '
                     'a script soldier needs its squad set-up',
    'SpiderNet': 'a web fitted to the 4.1 map it was made for (_FIT): no object to stand on open ground',
}
# A 4.1 mission's own copy of an object (same model, the mission's tweaks): its base object is the row.
EDF41_MISSION_VARIANT = re.compile(r'(M\d{3}|_DLC_MP?\d{3})$')
# The 4.1 vehicles' NPC-crewed twin when an empty one is a row too.
EDF41_AI_TWIN = re.compile(r'_AI$')


def _edf41_registry() -> dict:
    with open(EDF41_REGISTRY, encoding='utf-8') as f:
        return json.load(f)


def edf41_left_out(obj: dict) -> str | None:
    """Why the registry's object `obj` is no row (None: it is one)."""
    stem = obj['object'].removeprefix('OBJECT/').removesuffix('.SGO')
    if obj['class'] in EDF41_LEFT_OUT_CLASSES:
        return EDF41_LEFT_OUT_CLASSES[obj['class']]
    if EDF41_MISSION_VARIANT.search(stem.split('/')[-1]):
        return 'a 4.1 mission\'s own variant of an object that is a row'
    names = {o['object'] for o in _edf41_registry()['objects']}
    if EDF41_AI_TWIN.search(stem) and obj['object'].replace('_AI.SGO', '.SGO') in names:
        return 'the NPC-crewed twin of a vehicle that is a row (placed empty)'
    if obj['class'] not in EDF41_CLASSES:
        return None   # a new class: selftest asks for it to be named
    return None


@lru_cache(maxsize=None)
def edf41_rows() -> tuple[Row, ...]:
    """The 4.1 rows, most used in 4.1's missions first within a class. The plugin preloads by default the first of each
    class and any other whose files (but its own SGO) the rows before it bring already (a variant on the same model costs
    no more memory); the others (another 17 MB Hector model each) only with ini RangePlaceAll=1."""
    reg = _edf41_registry()
    objects = [o for o in reg['objects'] if edf41_left_out(o) is None and o['class'] in EDF41_CLASSES]
    order = {c: i for i, c in enumerate(EDF41_CLASSES)}
    objects.sort(key=lambda o: (order[o['class']], -int(o['missions']), o['object']))
    rows: list[Row] = []
    brought: set[str] = set()
    first: set[str] = set()
    for o in objects:
        category, words, lift, flying = EDF41_CLASSES[o['class']]
        stem = o['object'].removeprefix('OBJECT/').removesuffix('.SGO')
        others = {f for f in o['files'] if f != o['object']}
        preload = o['class'] not in first or others <= brought
        first.add(o['class'])
        if preload:
            brought |= set(o['files'])
        rows.append(Row(stem.lower(), f'{words} {stem.split("/")[-1]}', category, 'edf41', lift, flying, preload))
    return tuple(rows)


def edf41_files(sgo: str) -> list[str]:
    """The Mods files a 4.1 row needs (its registry entry's)."""
    rel = f'OBJECT/{sgo.upper()}.SGO'
    return next(o['files'] for o in _edf41_registry()['objects'] if o['object'] == rel)


@lru_cache(maxsize=None)
def edf5dlc_rows() -> tuple[Row, ...]:
    """EDF5 Mission Pack 1's objects (tools/edf5_dlc.py groups of kind 'object'): the DLC fortress."""
    with open(EDF5_DLC_REGISTRY, encoding='utf-8') as f:
        groups = json.load(f)['groups']
    out = []
    for name, g in groups.items():
        if g.get('kind') != 'object':
            continue
        stem = name.removeprefix('OBJECT/').removesuffix('.SGO')
        out.append(Row(stem.lower(), f'EDF5 DLC 要塞 {stem}', 'enemy', 'edf5dlc', 0.5, False, True))
    return tuple(out)


def edf5dlc_files(sgo: str) -> list[str]:
    with open(EDF5_DLC_REGISTRY, encoding='utf-8') as f:
        g = json.load(f)['groups'][f'OBJECT/{sgo.upper()}.SGO']
    return list(g['files']) + list(g.get('needs', []))


# The installer's vehicles placeable as they are (their SGO keeps the mission_setup of the stock SGO it is made from):
# (sgo, label, the files of it in Mods). The sedan (tools/make_sedan.py: V512_KEITRUCK's SGO on the city car's model).
INSTALLER_VEHICLES: tuple[tuple[str, str, tuple[str, ...]], ...] = (
    ('edf6vc_sedan', '民用轿车', ('OBJECT/EDF6VC_SEDAN.SGO', 'OBJECT/EDF6VC_SEDAN.MRAB', 'OBJECT/EDF6VC_SEDAN.SHKT')),
)


def short(label: str) -> str:
    """A range label without its notes in brackets (the panel's row: '直升机 506 Eros（测试场生成）' -> '直升机 506 Eros')."""
    return label.split('（')[0].strip()


def rows() -> list[Row]:
    """Every row, in the panel's order (category, then the registries' order). Imports the range's own module for its
    vehicles (gen.VEHICLES, gen.PLACEABLE_DERIVED)."""
    import gen
    out = [Row(s, label, 'vehicle', 'stock') for s, label in STOCK_VEHICLES]
    out += [r for r in edf41_rows() if r.category == 'vehicle']
    labels = dict(gen.VEHICLES)
    out += [Row(s, short(labels[s]), 'ours', 'range') for s in gen.PLACEABLE_DERIVED]
    out += [Row(s, label, 'ours', 'installer') for s, label, _ in INSTALLER_VEHICLES]
    out += [Row(s, label, 'enemy', 'stock', lift, flying) for s, label, flying, lift in STOCK_ENEMIES]
    out += list(edf5dlc_rows())
    out += [r for r in edf41_rows() if r.category == 'enemy41']
    return out


def files_of(row: Row) -> list[str]:
    """The Mods files a non-stock row needs (a 'range' row's SGO is the range's own: install writes it)."""
    if row.source == 'edf41':
        return edf41_files(row.sgo)
    if row.source == 'edf5dlc':
        return edf5dlc_files(row.sgo)
    if row.source == 'installer':
        return list(next(f for s, _, f in INSTALLER_VEHICLES if s == row.sgo))
    return [row.rel] if row.source == 'range' else []


def available(game_root: str, row: Row) -> bool:
    """Whether `row`'s files are in this game's Mods (a stock row: always; a range row: written by the range's install)."""
    if row.source == 'stock':
        return True
    return all(os.path.isfile(os.path.join(game_root, 'Mods', *rel.split('/'))) for rel in files_of(row))
