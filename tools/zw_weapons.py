"""The new player classes' weapon rows (docs/plan-2026-11-28-new-classes-impl.md §1, §3 stage 1): 载具手 (class 4,
ZwVehicler) and 刻印剑士 (class 5, ZwRuneBlade), as rows of the shared weapon table that tools/call_weapons.py stacks
after the call weapons and the ported ones (ported_weapons.py), in the same transaction and manifest: kept where an
install put them by id, a placeholder on uninstall (EDF6VC_RETIRED_<id>, its template's stock row) like those.

Their row ids are what module A's SoldierInit names as the slots' default weapons (ZW_*, not EDF6VC_*), and their
categories (400.. / 500..) are only in the new classes' slots: SLOTS here is the one place they are listed
(tools/gen_weapon_series.py puts them after CONFIG.SGO's four classes; src/soldier_classes* must say the same).

Every row is acquire 1 (WEAPONTABLE column 5: a new save owns it, as the stock aSupportNone / AssultRifle01 are): a
class's default weapon has to be owned, or the stock 0xDC550 rewrites the slot. Nothing drops them in missions yet
(column 6 empty; plan §3 stage 4 is the unlock). The SGOs are built at install from the player's Root.cpk (the stock
templates below, edited), so no game file is in this repository:
  none rows  the stock aSupportNone's (Weapon_Accessory, 'No Equipment'), as plan §3 asks
  Gyrojet01  Cascade 0's (RocketBullet01: Ammo_CustomParameter [mode, a, b, accel], docs/edf41-ufomother301.md: the
             rocket leaves the muzzle at AmmoSpeed and speeds up by accel each frame), six rounds, single shots
  RuneCarbine01  Blazer's (LaserBullet02, the Ranger's ray gun), bolt-action single shots of a blue-violet beam that
             goes through what it hits (AmmoIsPenetration)
Their models are the templates' (placeholders: tools/make_zw_classes.py ASSETS).

RELEASED freezes the order: rows never move, so IDS only ever grows at its end.
"""
from __future__ import annotations

import copy
import hashlib
import os
import sys
from dataclasses import dataclass
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import calls  # noqa: E402
import dsgo  # noqa: E402
from dsgo import Node  # noqa: E402

LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
ACQUIRE = 1.0   # column 5: owned by a new save (a class default must be owned: 0xDC550)
SOURCE = 1.0    # column 3: 1 every non-pack weapon
PACK = 0.0      # column 8
LEVEL = 0.0     # column 4
STAR_CAP = 1.0  # column 7 (the stock aSupportNone's)

# The new classes' slots, class 4 then 5 (plan §1): (slot name, categories, one row may be in two slots of the class).
# The categories are new numbers (the stock ones end at 334), each only in its own slot.
SLOTS: tuple[tuple[tuple[str, tuple[int, ...], bool], ...], ...] = (
    (('ZW_SlotVehicle', (400,), False), ('ZW_SlotPistol', (401,), False)),
    (('ZW_SlotMelee', (500,), False), ('ZW_SlotRanged', (501,), False), ('ZW_SlotSupport', (502,), False)),
)
FIRST_CLASS = 4
CATEGORY_KEYS = {400: 'WeaponCategory_ZwVehicle', 401: 'WeaponCategory_ZwPistol', 500: 'WeaponCategory_ZwMelee',
                 501: 'WeaponCategory_ZwRanged', 502: 'WeaponCategory_ZwSupport'}


@dataclass(frozen=True)
class Weapon:
    id: str
    category: int
    template: str                       # the stock row (and SGO) it is made from
    kind: str                           # 'none' | 'gyrojet' | 'carbine'
    text: dict[str, tuple[str, str]]    # lang -> (name, description); a none row keeps the template's texts


NONE_TEMPLATE = 'aSupportNone'
WEAPONS: tuple[Weapon, ...] = (
    Weapon('ZW_VehicleNone', 400, NONE_TEMPLATE, 'none', {}),
    Weapon('ZW_Gyrojet01', 401, 'aWeapon109', 'gyrojet', {
        'SC': ('陀螺喷射手枪（试作型）', '载具手的随身手枪。发射小型火箭弹，弹头离膛时很慢，随后一路加速。\n'
                                  '装弹 6 发，逐发射击，命中时小范围爆炸。\n\n试作型，模型暂用火箭弹发射器代替。'),
        'CN': ('陀螺噴射手槍（試作型）', '載具手的隨身手槍。發射小型火箭彈，彈頭離膛時很慢，隨後一路加速。\n'
                                  '裝彈 6 發，逐發射擊，命中時小範圍爆炸。\n\n試作型，模型暫用火箭彈發射器代替。'),
        'JA': ('ジャイロジェット・ピストル（試作型）', 'ヴィークラーの携行拳銃。小型ロケット弾を撃ち出す。弾頭は銃口を'
                                          'ゆっくり離れ、そこから加速していく。\n装弾数 6 発の単発射撃で、着弾時に小さく爆発する。'
                                          '\n\n試作型。モデルは仮にロケットランチャーを使用。'),
        'EN': ('Gyrojet Pistol (Prototype)', "The Vehicler's sidearm. It fires small rockets that leave the muzzle "
                                             'slowly and speed up from there.\nSix rounds, single shots; each round '
                                             'explodes on impact in a small radius.\n\nPrototype: a rocket '
                                             "launcher's model stands in for its own."),
        'KR': ('자이로젯 권총(시제형)', '비클러의 휴대 권총. 소형 로켓탄을 발사하며, 탄두는 총구를 천천히 떠난 뒤 점점 '
                                 '가속한다.\n장탄수 6발, 단발 사격. 착탄 시 좁은 범위에 폭발한다.\n\n시제형: 모델은 임시로 '
                                 '로켓 런처를 사용한다.'),
    }),
    Weapon('ZW_MeleeNone', 500, NONE_TEMPLATE, 'none', {}),
    Weapon('ZW_RuneCarbine01', 501, 'aWeapon014', 'carbine', {
        'SC': ('晨钟（试作型）', '刻印剑士的远程卡宾枪。栓动式，逐发射出蓝紫色的刻印光束，可贯穿敌人。\n'
                            '每发之后要拉一次枪栓，射速慢但单发威力高。\n\n试作型，模型暂用光耀者代替。'),
        'CN': ('晨鐘（試作型）', '刻印劍士的遠程卡賓槍。栓動式，逐發射出藍紫色的刻印光束，可貫穿敵人。\n'
                            '每發之後要拉一次槍栓，射速慢但單發威力高。\n\n試作型，模型暫用光耀者代替。'),
        'JA': ('暁鐘（試作型）', 'ルーンブレイドの遠距離カービン。ボルトアクション式で、青紫のルーン光線を一発ずつ放ち、'
                            '敵を貫通する。\n一発ごとにボルトを引くため連射は遅いが、一発の威力は高い。\n\n試作型。'
                            'モデルは仮にブレイザーを使用。'),
        'EN': ('Matins Bell (Prototype)', "The Rune Blade's long-range carbine. Bolt action: it fires one blue-violet "
                                          'rune beam at a time that goes through the enemies it hits.\nThe bolt is '
                                          'worked after every shot, so it fires slowly, but each shot hits hard.\n\n'
                                          "Prototype: the Blazer's model stands in for its own."),
        'KR': ('새벽종(시제형)', '룬 블레이드의 원거리 카빈. 볼트 액션식으로 청자색 룬 광선을 한 발씩 쏘며 적을 관통한다.\n'
                             '한 발마다 노리쇠를 당겨야 해서 연사는 느리지만 한 발의 위력은 높다.\n\n시제형: 모델은 '
                             '임시로 블레이저를 사용한다.'),
    }),
    Weapon('ZW_SupportNone', 502, NONE_TEMPLATE, 'none', {}),
)
IDS: tuple[str, ...] = tuple(w.id for w in WEAPONS)
BY_ID = {w.id: w for w in WEAPONS}

# Every order of IDS a release installed: (how many, sha256 of those ids joined by '\n'), frozen (tools/selftest.py,
# tests/zw_classes_test.py): a later weapon is only ever added at the end.
RELEASED: dict[str, tuple[int, str]] = {
    'new classes stage 1 (2026-11-28)': (5, '8d36b020ae73501fe550abbd106c121977f4caa1fbfe7e53f78729c5f8679e1d'),
}


def ids_hash(ids) -> str:  # noqa: ANN001 - a sequence of ids
    return hashlib.sha256('\n'.join(ids).encode('utf-8')).hexdigest()


def retired_id(row_id: str) -> str:
    return calls.RETIRED_PREFIX + row_id


_SLOTS = {k.upper(): w.id for w in WEAPONS for k in (w.id, retired_id(w.id))}


def slot_of(row_id: str) -> str | None:
    """The weapon of ours a table row belongs to (its own row, or the placeholder an uninstall left), else None."""
    return _SLOTS.get(row_id.upper())


def sgo_file(w: Weapon) -> str:
    return f'WEAPON/{w.id.upper()}.SGO'


def files(w: Weapon) -> list[str]:
    return [sgo_file(w)]


def class_of(category: int) -> int:
    for k, slots in enumerate(SLOTS):
        if any(category in cats for _, cats, _ in slots):
            return FIRST_CLASS + k
    raise ValueError(f'category {category} is not a new class slot')


# ---------------------------------------------------------------- the SGOs

# Gyrojet01 (frames at 60/s; scalars: a prototype has no star growth)
GYROJET = {
    'AmmoCount': 6.0, 'FireInterval': 20.0, 'ReloadTime': 120.0, 'FireAccuracy': 0.01, 'FireRecoil': 0.15,
    'AmmoSpeed': 0.2, 'AmmoAlive': 240.0, 'AmmoDamage': 120.0, 'AmmoExplosion': 2.0, 'AmmoSize': 0.15,
}
GYROJET_CUSTOM = [0.0, 20.0, 100.0, 0.02]   # RocketBullet01 [mode 0, trail a, trail b, accel per frame]
# RuneCarbine01
CARBINE = {
    'AmmoCount': 5.0, 'FireInterval': 45.0, 'ReloadTime': 150.0, 'FireAccuracy': 0.0, 'FireRecoil': 0.3,
    'AmmoSpeed': 30.0, 'AmmoAlive': 20.0, 'AmmoDamage': 300.0, 'AmmoIsPenetration': 1.0, 'AmmoSize': 0.25,
}
CARBINE_COLOR = [0.9, 0.6, 2.8, 1.0]   # blue-violet (the plan's RuneBlade colour (0.6, 0.4, 1), HDR-bright as Blazer's)
NAME_KEYS = {'JA': 'name.ja', 'EN': 'name.en', 'CN': 'name.cn', 'KR': 'name.kr', 'SC': 'name.sc'}


def build_sgo(w: Weapon, template: bytes) -> bytes:
    """Its SGO from its template's stock one (a DSGO)."""
    if template[:4] != b'DSGO':
        raise ValueError(f'{w.template}: not a DSGO')
    doc = dsgo.parse(template)
    root = doc.root
    if w.kind == 'none':
        if root.get('xgs_scene_object_class') != 'Weapon_Accessory':
            raise ValueError(f'{w.template} is not a Weapon_Accessory')
        return dsgo.compact(doc)
    for lang, key in NAME_KEYS.items():
        root.get(key)   # KeyError: not the template we read
        root.set(key, w.text[lang][0])
    if w.kind == 'gyrojet':
        if root.get('AmmoClass') != 'RocketBullet01':
            raise ValueError(f'{w.template}: AmmoClass {root.get("AmmoClass")}, want RocketBullet01')
        for k, v in GYROJET.items():
            root.get(k)
            root.set(k, v)
        root.set('Ammo_CustomParameter', Node(list(GYROJET_CUSTOM)))
    elif w.kind == 'carbine':
        if root.get('AmmoClass') != 'LaserBullet02':
            raise ValueError(f'{w.template}: AmmoClass {root.get("AmmoClass")}, want LaserBullet02')
        for k, v in CARBINE.items():
            root.get(k)
            root.set(k, v)
        root.set('AmmoColor', Node(list(CARBINE_COLOR)))
    else:
        raise ValueError(w.kind)
    return dsgo.compact(doc)


def build(stock: Callable[[str], bytes]) -> dict[str, bytes]:
    """{Mods path: SGO} of every weapon; stock(rel) reads the EDF6 file at WEAPON/<NAME>.SGO."""
    return {sgo_file(w): build_sgo(w, stock(f'WEAPON/{w.template.upper()}.SGO')) for w in WEAPONS}


# ---------------------------------------------------------------- the rows


def table_row(template: Node, w: Weapon) -> Node:
    row = copy.deepcopy(template)
    if row.items[0] != w.template:
        raise ValueError(f'{w.id}: template row is {row.items[0]}, want {w.template}')
    stock_path = template.items[1]
    prefix = stock_path[:stock_path.rfind('/') + 1]   # app:/weapon/
    suffix = stock_path[stock_path.rfind('.'):]       # .sgo
    row.items[0] = w.id
    row.items[1] = f'{prefix}{w.id}{suffix}'
    row.items[2] = float(w.category)
    row.items[3] = SOURCE
    row.items[4] = LEVEL
    row.items[5] = ACQUIRE
    row.items[6] = Node([])
    row.items[7] = STAR_CAP
    row.items[8] = PACK
    return row


def _value(text: str, value: str) -> str:
    return text.replace('$0', value) if '$0' in text else value


# Each stat line of the template's text row (by its index) -> the value shown (the row's own unit text keeps '$0').
GYROJET_STATS = {0: '6', 1: '3.0', 2: '120', 3: '2.0', 4: '2.0', 5: 'A'}         # Cascade: capacity .. accuracy
CARBINE_STATS = {0: '5', 1: '1.3', 2: '300', 3: '2.5', 4: '600', 5: '1800', 6: 'S++'}   # Blazer


def text_row(template: Node, w: Weapon, lang: str) -> Node:
    """Its text row: the template's for a none row; else its own name and description, and the template's stat lines
    with their values (fixed strings: these weapons do not grow with stars)."""
    row = copy.deepcopy(template)
    if w.kind == 'none':
        return row
    row.items[0], row.items[1] = w.text[lang]
    stats = GYROJET_STATS if w.kind == 'gyrojet' else CARBINE_STATS
    lines = row.items[2].items
    for i, value in stats.items():
        line = lines[i]
        fmt = line.items[1]
        if '$0' in fmt:
            line.items[:] = [line.items[0], _value(fmt, value)]
        elif '/' in fmt or fmt[:1].isdigit():   # a literal value ("60.0/sec", "999"): its number replaced
            num = ''.join(ch for ch in fmt if ch.isdigit() or ch == '.')
            line.items[:] = [line.items[0], fmt.replace(num, value, 1) if num else value]
        else:
            line.items[:] = [line.items[0], value]
    return row


def retired_table_row(template: Node, w: Weapon) -> Node:
    """The placeholder for an uninstalled row: its template's stock row under the placeholder id, not owned by a new
    save (acquire 0: a stock initial weapon's acquire would hand every new save the placeholder)."""
    row = copy.deepcopy(template)
    row.items[0] = retired_id(w.id)
    row.items[5] = 0.0
    return row


def retired_text_row(template: Node, w: Weapon, lang: str) -> Node:
    row = copy.deepcopy(template)
    note = calls.RETIRED_NOTE[calls._lang(lang)]
    name = w.text[lang][0] if w.text else str(template.items[0])
    row.items[0] = name + note[0]
    row.items[1] = calls.retired_description(lang, str(template.items[0]))
    return row
