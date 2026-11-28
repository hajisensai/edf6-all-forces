"""Writes the new player classes' files (docs/plan-2026-11-28-new-classes-impl.md §1) into <game>/Mods:
  Mods/OBJECT/ZW_VEHICLER_0N.SGO, ZW_RUNEBLADE_0N.SGO (N=1..4)   the battle SGOs SoldierInit [2] names: the stock
        Ranger's four (CONFIG.SGO SoldierInit[0][2], in order) with xgs_scene_object_class set to the class's factory
        (ZwVehicler / ZwRuneBlade, src/soldier_factory*: both AssultSoldier-based)
  Mods/MENUOBJECT/ZW_VEHICLER_0N.SGO, ZW_RUNEBLADE_0N.SGO        the menu SGOs [5][1]: the Ranger's four, as they are
  Mods/DEFAULTPACKAGE/CUSTOMCOLORZWVEHICLER.SGO, CUSTOMCOLORZWRUNEBLADE.SGO   [5][0]: the Ranger's colour sets
Every model is the Ranger's for now: ASSETS marks each file 'placeholder' (plan §3: not a delivery). Built from the
player's own Root.cpk (only read), written atomically and recorded in the ledger as this tool's (pylib/ledger.py
'zwclass'); --remove releases them. No shared table is touched here: the weapon rows are tools/zw_weapons.py's
(call_weapons stacks them), the texts (TEXTS: class, weapon category and slot names, the keys SoldierInit and
SoldierWeaponCategory name) go into the game's text tables through their one writer, make_edf5_campaign (text_table ->
add_texts): with the mission packs' when those are built, else alone (install_texts), so they are there whatever packs
are installed.
  python tools/make_zw_classes.py [game dir]            write / refresh
  python tools/make_zw_classes.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'zwclass'   # pylib/ledger.py
CONFIG = ('DEFAULTPACKAGE', 'CONFIG.SGO')
TEMPLATE_CLASS = 0   # SoldierInit's Ranger: the base of both (AssultSoldier)
APPEARANCES = 4


@dataclass(frozen=True)
class SoldierClass:
    type: int
    factory: str     # xgs_scene_object_class, the name src/soldier_factory* registers
    stem: str        # zw_vehicler -> OBJECT/zw_vehicler_0N.sgo, MENUOBJECT/zw_vehicler_0N.SGO

    @property
    def name_key(self) -> str:
        return f'SoldierType_{self.factory}'

    def battle(self, n: int) -> str:
        return f'OBJECT/{self.stem.upper()}_0{n}.SGO'

    def menu(self, n: int) -> str:
        return f'MENUOBJECT/{self.stem.upper()}_0{n}.SGO'

    @property
    def custom_color(self) -> str:
        return f'DEFAULTPACKAGE/CUSTOMCOLOR{self.factory.upper()}.SGO'

    # the paths as SoldierInit writes them (plan §1)
    def battle_path(self, n: int) -> str:
        return f'app:/object/{self.stem}_0{n}.sgo'

    def menu_path(self, n: int) -> str:
        return f'APP:/MENUOBJECT/{self.stem}_0{n}.SGO'

    @property
    def custom_color_path(self) -> str:
        return f'App:/DefaultPackage/CustomColor{self.factory}.sgo'


CLASSES: tuple[SoldierClass, ...] = (SoldierClass(4, 'ZwVehicler', 'zw_vehicler'),
                                     SoldierClass(5, 'ZwRuneBlade', 'zw_runeblade'))


def files_of(c: SoldierClass) -> list[str]:
    return ([c.battle(n) for n in range(1, APPEARANCES + 1)] + [c.menu(n) for n in range(1, APPEARANCES + 1)]
            + [c.custom_color])


# Every file this tool writes -> (what it is made from, its state). 'placeholder': the Ranger's model / colours stand in
# (plan §3 stage 1); a class's own art replaces the entry when it is made.
ASSETS: dict[str, tuple[str, str]] = {
    **{c.battle(n): (f'SoldierInit[0][2][{n - 1}]', 'placeholder') for c in CLASSES for n in range(1, APPEARANCES + 1)},
    **{c.menu(n): (f'SoldierInit[0][5][1][{n - 1}]', 'placeholder') for c in CLASSES for n in range(1, APPEARANCES + 1)},
    **{c.custom_color: ('SoldierInit[0][5][0]', 'placeholder') for c in CLASSES},
}

# The text keys (TEXTTABLE_STEAM.<LANG>.TXT_SGO; LANG as the game names them: CN traditional, SC simplified).
TEXTS: dict[str, dict[str, str]] = {
    'SoldierType_ZwVehicler': {'SC': '载具手', 'CN': '載具手', 'JA': 'ヴィークラー', 'EN': 'Vehicler', 'KR': '비클러'},
    'SoldierType_ZwRuneBlade': {'SC': '刻印剑士', 'CN': '刻印劍士', 'JA': 'ルーンブレイド', 'EN': 'Rune Blade',
                                'KR': '룬 블레이드'},
    'WeaponCategory_ZwVehicle': {'SC': '载具', 'CN': '載具', 'JA': 'ビークル', 'EN': 'Vehicles', 'KR': '탈것'},
    'WeaponCategory_ZwPistol': {'SC': '手枪', 'CN': '手槍', 'JA': 'ピストル', 'EN': 'Pistols', 'KR': '권총'},
    'WeaponCategory_ZwMelee': {'SC': '近战武器', 'CN': '近戰武器', 'JA': '近接武器', 'EN': 'Melee Weapons',
                               'KR': '근접 무기'},
    'WeaponCategory_ZwRanged': {'SC': '远程武器', 'CN': '遠程武器', 'JA': '遠距離武器', 'EN': 'Ranged Weapons',
                                'KR': '원거리 무기'},
    'WeaponCategory_ZwSupport': {'SC': '支援装备', 'CN': '支援裝備', 'JA': 'サポート装備', 'EN': 'Support Equipment',
                                 'KR': '지원 장비'},
    'ZW_SlotVehicle': {'SC': '载具', 'CN': '載具', 'JA': 'ビークル', 'EN': 'Vehicle', 'KR': '탈것'},
    'ZW_SlotPistol': {'SC': '手枪', 'CN': '手槍', 'JA': 'ピストル', 'EN': 'Pistol', 'KR': '권총'},
    'ZW_SlotMelee': {'SC': '近战', 'CN': '近戰', 'JA': '近接', 'EN': 'Melee', 'KR': '근접'},
    'ZW_SlotRanged': {'SC': '远程', 'CN': '遠程', 'JA': '遠距離', 'EN': 'Ranged', 'KR': '원거리'},
    'ZW_SlotSupport': {'SC': '支援装备', 'CN': '支援裝備', 'JA': 'サポート装備', 'EN': 'Support Equipment',
                       'KR': '지원 장비'},
}
TEXT_LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')


def add_texts(members: dict, lang: str) -> None:
    """The new classes' text keys into a text table's members (other keys untouched)."""
    for key, by_lang in TEXTS.items():
        members[key] = by_lang[lang]


def _config(game: vc.Game) -> list:
    init = sgo.plain(sgo.read(game.read(*CONFIG))[1])['SoldierInit']
    ranger = init[TEMPLATE_CLASS]
    if ranger[0] != TEMPLATE_CLASS or ranger[1] != 'SoldierType_Ranger':
        raise ValueError(f'CONFIG.SGO SoldierInit[{TEMPLATE_CLASS}] is {ranger[:2]}, not the Ranger')
    return ranger


def _rel(path: str) -> tuple[str, str]:
    """'app:/object/p505_ranger.sgo' -> ('OBJECT', 'P505_RANGER.SGO')."""
    folder, name = path.split(':/', 1)[1].split('/')
    return folder.upper(), name.upper()


def battle_sgo(template: bytes, c: SoldierClass) -> bytes:
    if template[:4] != b'DSGO':
        raise ValueError('the Ranger battle SGO is not a DSGO')
    doc = dsgo.parse(template)
    if doc.root.get('xgs_scene_object_class') != 'AssultSoldier':
        raise ValueError(f'the Ranger battle SGO is a {doc.root.get("xgs_scene_object_class")}, not an AssultSoldier')
    doc.root.set('xgs_scene_object_class', c.factory)
    return dsgo.compact(doc)


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    ranger = _config(game)
    battles, (color, menus) = ranger[2], ranger[5][:2]
    if len(battles) != APPEARANCES or len(menus) != APPEARANCES:
        raise ValueError(f'the Ranger has {len(battles)} battle / {len(menus)} menu SGOs, want {APPEARANCES}')
    out: dict[str, bytes] = {}
    for c in CLASSES:
        for n in range(1, APPEARANCES + 1):
            out[c.battle(n)] = battle_sgo(game.read(*_rel(battles[n - 1])), c)
            out[c.menu(n)] = game.read(*_rel(menus[n - 1]))
        out[c.custom_color] = game.read(*_rel(color))
    if set(out) != set(ASSETS):
        raise AssertionError('build and ASSETS disagree')
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


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
