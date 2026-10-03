"""The weapon list the game will actually use, and the forced-loadout file the plugin reads.

Weapon ids are WEAPONTABLE.SGO row numbers, so a mod that rewrites the table shifts them. Choices are
kept by SGO name and turned into row numbers at install time, against Mods/WEAPON/WEAPONTABLE.SGO
when a mod provides one, else the stock table. Layout and slot rules: docs/loadout-re.md.
"""
from __future__ import annotations

import os
from dataclasses import dataclass

import gen
import sgo

CLASSES = ['游骑兵 Ranger', '飞行翼 Wing Diver', '空袭兵 Air Raider', '重装兵 Fencer']
SLOT_NAMES = [
    ['主武器 1', '主武器 2', '投掷 / 放置', '支援装备 / 载具'],
    ['主武器 1', '主武器 2', '特殊武器', '能量核心'],
    ['主武器 1', '主武器 2', '主武器 3', '支援装备', '载具'],
    ['武器 1 左手', '武器 1 右手', '武器 2 左手', '武器 2 右手', '辅助装备 1', '辅助装备 2'],
]

# WeaponTable category -> (class, slots it may sit in), from CONFIG.SGO SoldierInit (docs/weapons.csv).
# The hundreds digit is not the class: 2xx are Fencer (3), 3xx are Air Raider (2).
_RANGES = [
    (range(0, 7), 0, (0, 1)), (range(7, 11), 0, (3,)), (range(20, 24), 0, (2,)),
    (range(100, 107), 1, (0, 1)), (range(110, 117), 1, (2,)), (range(120, 121), 1, (3,)),
    (range(200, 206), 3, (0, 1, 2, 3)), (range(206, 210), 3, (4, 5)),
    (range(302, 303), 2, (3,)), (range(303, 304), 2, (0, 1, 2)), (range(305, 306), 2, (0, 1, 2)),
    (range(306, 310), 2, (4,)), (range(310, 315), 2, (0, 1, 2)), (range(320, 321), 2, (4,)),
    (range(330, 335), 2, (3,)),
]
CATEGORIES = {c: (cls, slots) for r, cls, slots in _RANGES for c in r}

LOADOUT_FILE = ('Mods', 'Plugins', 'EDF6TestRange.loadout.ini')


@dataclass(frozen=True)
class Weapon:
    id: int
    sgo: str
    name: str
    cls: int
    slots: tuple[int, ...]

    def label(self) -> str:
        return f'{self.name}  [{self.sgo}]'


def _read(game: gen.Game, name: str) -> bytes:
    override = os.path.join(game.root, 'Mods', 'WEAPON', name)
    if os.path.isfile(override):
        with open(override, 'rb') as f:
            return f.read()
    return game.read('WEAPON', name)


def load(game_root: str) -> list[Weapon]:
    game = gen.Game(game_root)
    rows = sgo.load(data=_read(game, 'WEAPONTABLE.SGO'))['table']
    names: list[str] = []
    for lang in ('SC', 'EN', 'JA'):
        try:
            text = sgo.load(data=_read(game, f'WEAPONTEXT.{lang}.SGO'))['text_table']
        except KeyError:
            continue
        if len(text) == len(rows):
            names = [t[0] if t and isinstance(t[0], str) else '' for t in text]
            break
    out = []
    for i, row in enumerate(rows):
        cat = CATEGORIES.get(int(row[2]))
        if cat is None:
            continue
        name = (names[i] if names else '') or row[0]
        out.append(Weapon(i, row[0], name.replace('\n', ' '), cat[0], cat[1]))
    return out


def for_slot(weapons: list[Weapon], cls: int, slot: int) -> list[Weapon]:
    return [w for w in weapons if w.cls == cls and slot in w.slots]


def loadout_path(game_root: str) -> str:
    return os.path.join(game_root, *LOADOUT_FILE)


def write_loadout(game_root: str, loadout: dict) -> list[str]:
    """Writes the file the plugin reads; returns what it will equip. Names that no longer fit their
    slot (a weapon mod changed the table) are refused here, never handed to the game."""
    path = loadout_path(game_root)
    if not loadout.get('enabled'):
        remove_loadout(game_root)
        return []
    cls = int(loadout['class'])
    weapons = load(game_root)
    by_name = {w.sgo: w for w in weapons}
    lines = ['; Written by the EDF6 test range launcher (testrange/). Delete to turn the forced loadout off.',
             '[Loadout]', 'Enabled=1', f'Class={cls}']
    shown = [CLASSES[cls]]
    for i, name in enumerate(loadout.get('slots', [])[:len(SLOT_NAMES[cls])]):
        if not name:
            continue
        w = by_name.get(name)
        if w is None or w.cls != cls or i not in w.slots:
            raise ValueError(f'{SLOT_NAMES[cls][i]}：{name} 不能装在这一格（武器表可能被别的 mod 改过）')
        lines.append(f'Slot{i}={w.id}')
        shown.append(f'{SLOT_NAMES[cls][i]}：{w.name}')
    stars = int(loadout.get('stars', -1))
    lines.append(f'Stars={stars}')
    refill = bool(loadout.get('refill', False))
    lines.append(f'Refill={int(refill)}')
    if refill:
        shown.append('开局补满：武器弹药、空袭兵载具（不用先攒点数）')
    if stars >= 0:
        shown.append(f'星级全部 {stars}')
    with open(path, 'w', encoding='utf-16') as f:   # GetPrivateProfile* reads UTF-16 with a BOM
        f.write('\n'.join(lines) + '\n')
    return shown


def remove_loadout(game_root: str) -> None:
    try:
        os.remove(loadout_path(game_root))
    except FileNotFoundError:
        pass
