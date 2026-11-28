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
             rocket leaves the muzzle at AmmoSpeed and speeds up by accel each frame), six rounds, single shots, its
             damage growing with its flight (AmmoDamageReduce a > 1: src/vehicler_foot_logic.h)
  vehicle rows  the Vehicler's vehicle choices (docs/newclass-re/vehicler-plan.md §1.2): as a none row, inert Weapon_Accessory
             clones of aSupportNone (no fire, empty custom_parameter), only the tag of which vehicle the class brings;
             the vehicle is pylib/zw_vehicles.py's (row -> vehicle: src/zw_vehicles.inc), named and described here
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
    kind: str                           # 'none' | 'gyrojet' | 'carbine' | 'vehicle'
    text: dict[str, tuple[str, str]]    # lang -> (name, description); a none row keeps the template's texts
    vehicle: str = ''                   # a vehicle row's vehicle (pylib/zw_vehicles.py ZwVehicle.row: its own id)


NONE_TEMPLATE = 'aSupportNone'
WEAPONS: tuple[Weapon, ...] = (
    Weapon('ZW_VehicleNone', 400, NONE_TEMPLATE, 'none', {}),
    Weapon('ZW_Gyrojet01', 401, 'aWeapon109', 'gyrojet', {
        'SC': ('陀螺喷射手枪', '载具手的随身手枪。发射小型火箭弹，弹头离膛时很慢，随后一路加速。\n'
                                  '装弹 6 发，逐发射击，命中时小范围爆炸。贴脸威力很低，飞得越远威力越高（最高 5 倍）。'
                                  '\n\n模型暂用火箭弹发射器代替。'),
        'CN': ('陀螺噴射手槍', '載具手的隨身手槍。發射小型火箭彈，彈頭離膛時很慢，隨後一路加速。\n'
                                  '裝彈 6 發，逐發射擊，命中時小範圍爆炸。貼臉威力很低，飛得越遠威力越高（最高 5 倍）。'
                                  '\n\n模型暫用火箭彈發射器代替。'),
        'JA': ('ジャイロジェット・ピストル', 'ヴィークラーの携行拳銃。小型ロケット弾を撃ち出す。弾頭は銃口を'
                                          'ゆっくり離れ、そこから加速していく。\n装弾数 6 発の単発射撃で、着弾時に小さく爆発する。'
                                          '至近距離では威力が低く、遠くまで飛ぶほど威力が上がる（最大 5 倍）。'
                                          '\n\nモデルは仮にロケットランチャーを使用。'),
        'EN': ('Gyrojet Pistol', "The Vehicler's sidearm. It fires small rockets that leave the muzzle "
                                             'slowly and speed up from there.\nSix rounds, single shots; each round '
                                             'explodes on impact in a small radius. Weak point-blank, it hits '
                                             'harder the farther it has flown (up to five times).\n\nA rocket '
                                             "launcher's model stands in for its own."),
        'KR': ('자이로젯 권총', '비클러의 휴대 권총. 소형 로켓탄을 발사하며, 탄두는 총구를 천천히 떠난 뒤 점점 '
                                 '가속한다.\n장탄수 6발, 단발 사격. 착탄 시 좁은 범위에 폭발한다. '
                                 '근거리에서는 위력이 낮고, 멀리 날아갈수록 위력이 오른다(최대 5배).\n\n모델은 임시로 '
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
    # the Vehicler's vehicles (vehicler stage 2): their numbers are pylib/zw_vehicles.py's, the texts say them
    Weapon('ZW_V1_HSTVL', 400, NONE_TEMPLATE, 'vehicle', {
        'SC': ('HSTV-L 轻型战车', '载具手的高速战车，开局坐在车里入场。\n75 mm 超高速炮弹道平直、装填快，'
                                '单发威力与装甲较弱。极速约 80 km/h，三辆车里最快。\n\n模型暂用布莱克战车代替。'),
        'CN': ('HSTV-L 輕型戰車', '載具手的高速戰車，開局坐在車裡入場。\n75 mm 超高速砲彈道平直、裝填快，'
                                '單發威力與裝甲較弱。極速約 80 km/h，三輛車裡最快。\n\n模型暫用布萊克戰車代替。'),
        'JA': ('HSTV-L 軽戦車', 'ヴィークラーの高速戦車。乗車した状態で出撃する。\n75mm 超高速砲は弾道が低く装填も'
                             '速いが、一発の威力と装甲は控えめ。最高速度は約 80km/h で、3 台の中で最も速い。\n\n'
                             'モデルは仮にブラッカーを使用。'),
        'EN': ('HSTV-L Light Tank', "The Vehicler's fast tank; the mission starts with them in it.\nIts 75 mm "
                                    'hypervelocity gun fires flat and reloads quickly, but each shot and its armour are '
                                    'light. Top speed about 80 km/h, the fastest of the three.\n\n'
                                    "The Blacker's model stands in for its own."),
        'KR': ('HSTV-L 경전차', '비클러의 고속 전차. 탑승한 상태로 출격한다.\n75mm 초고속포는 탄도가 낮고 장전이 빠르지만 '
                             '한 발의 위력과 장갑은 약하다. 최고 속도 약 80km/h로 세 대 중 가장 빠르다.\n\n'
                             '모델은 임시로 블래커를 사용한다.'),
    }, vehicle='ZW_V1_HSTVL'),
    Weapon('ZW_V2_M247', 400, NONE_TEMPLATE, 'vehicle', {
        'SC': ('M247 约克中士自行高炮', '早期的对空战车，开局坐在车里入场。\n双联 40 mm 机炮由雷达自动瞄准空中目标，'
                                   '对地也能射击，装甲较薄。\n\n模型暂用原版对空战车代替。'),
        'CN': ('M247 約克中士自走高砲', '早期的對空戰車，開局坐在車裡入場。\n雙聯 40 mm 機砲由雷達自動瞄準空中目標，'
                                   '對地也能射擊，裝甲較薄。\n\n模型暫用原版對空戰車代替。'),
        'JA': ('M247 サージェント・ヨーク', '初期の対空戦車。乗車した状態で出撃する。\n40mm 連装機関砲はレーダーで空中の'
                                     '目標を自動で狙う。地上にも撃てるが、装甲は薄い。\n\nモデルは仮に原作の対空戦車を'
                                     '使用。'),
        'EN': ('M247 Sergeant York', 'An early anti-air vehicle; the mission starts with them in it.\nIts radar aims the '
                                     'twin 40 mm autocannons at air targets by itself; they fire at the ground too. Its '
                                     "armour is thin.\n\nThe game's own anti-air tank's model stands in for its own."),
        'KR': ('M247 서전트 요크', '초기의 대공 전차. 탑승한 상태로 출격한다.\n40mm 연장 기관포는 레이더로 공중 목표를 자동으로 '
                               '조준한다. 지상에도 쏠 수 있지만 장갑이 얇다.\n\n모델은 임시로 원작의 대공 전차를 사용한다.'),
    }, vehicle='ZW_V2_M247'),
    Weapon('ZW_V3_MBT70', 400, NONE_TEMPLATE, 'vehicle', {
        'SC': ('MBT-70 主战坦克', '均衡的主战坦克，开局坐在车里入场。\n152 mm 主炮威力大、爆炸范围广，装填慢；'
                               '装甲厚，速度中等。液压悬挂升降尚未实装。\n\n模型暂用布莱克战车代替。'),
        'CN': ('MBT-70 主戰坦克', '均衡的主戰坦克，開局坐在車裡入場。\n152 mm 主砲威力大、爆炸範圍廣，裝填慢；'
                               '裝甲厚，速度中等。液壓懸吊升降尚未實裝。\n\n模型暫用布萊克戰車代替。'),
        'JA': ('MBT-70 主力戦車', 'バランスの取れた主力戦車。乗車した状態で出撃する。\n152mm 主砲は威力と爆発範囲が大きい'
                               'が装填は遅い。装甲は厚く、速度は中程度。油気圧サスペンションの昇降は未実装。\n\n'
                               'モデルは仮にブラッカーを使用。'),
        'EN': ('MBT-70 Main Battle Tank', 'A balanced main battle tank; the mission starts with them in it.\nIts 152 mm '
                                          'gun hits hard over a wide burst but reloads slowly; thick armour, middling '
                                          "speed. Raising and lowering on its hydraulic suspension is not in yet.\n\n"
                                          "The Blacker's model stands in for its own."),
        'KR': ('MBT-70 주력 전차', '균형 잡힌 주력 전차. 탑승한 상태로 출격한다.\n152mm 주포는 위력과 폭발 범위가 크지만 장전이 '
                                '느리다. 장갑은 두껍고 속도는 중간. 유기압 서스펜션 승강은 아직 구현되지 않았다.\n\n'
                                '모델은 임시로 블래커를 사용한다.'),
    }, vehicle='ZW_V3_MBT70'),
)
IDS: tuple[str, ...] = tuple(w.id for w in WEAPONS)
BY_ID = {w.id: w for w in WEAPONS}

# Every order of IDS a release installed: (how many, sha256 of those ids joined by '\n'), frozen (tools/selftest.py,
# tests/zw_classes_test.py): a later weapon is only ever added at the end.
RELEASED: dict[str, tuple[int, str]] = {
    'new classes stage 1 (2026-11-28)': (5, '8d36b020ae73501fe550abbd106c121977f4caa1fbfe7e53f78729c5f8679e1d'),
    'vehicler stage 2 (2026-11-28)': (8, 'c71ed7b9f776f865c51730e0475e07d978ec28a7d20c338672329efdda5709a1'),
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

# Gyrojet01 (frames at 60/s; scalars: no star growth until stage 4; plan §1.3, docs/newclass-re/vehicler-plan.md M5). Speed
# 0.2 m/frame (12 m/s) at the muzzle, +0.02 a frame (72 m/s²): 15 m after 0.5 s, 255 m when it expires (150 frames).
GYROJET = {
    'AmmoCount': 6.0, 'FireInterval': 20.0, 'ReloadTime': 120.0, 'FireAccuracy': 0.01, 'FireRecoil': 0.15,
    'AmmoSpeed': 0.2, 'AmmoAlive': 150.0, 'AmmoDamage': 40.0, 'AmmoExplosion': 2.0, 'AmmoSize': 0.1,
}
GYROJET_CUSTOM = [0.0, 12.0, 36.0, 0.02]   # RocketBullet01 [mode 0, trail a, trail b, accel per frame]: a thin, short trail
# AmmoDamageReduce [a, b]: the core's falloff 0x231080 gives damage * (a + (1 - a) * (1 - t)^b), t = age / AmmoAlive. a > 1 is
# growth (src/vehicler_foot_logic.h: the plugin widens the stock gate, which only refreshes a < 0.99): 40 point-blank, ~98 at
# 15 m, ~142 at 48 m, 200 at the end of its life.
GYROJET_DAMAGE_REDUCE = [5.0, 2.0]


def gyrojet_range(speed: float = GYROJET['AmmoSpeed'], accel: float = GYROJET_CUSTOM[3],
                  alive: float = GYROJET['AmmoAlive']) -> float:
    """Metres a round flies in its life (frame by frame: it moves at its speed, then speeds up by accel)."""
    return sum(speed + accel * f for f in range(int(alive)))


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
    if w.kind in ('none', 'vehicle'):
        if root.get('xgs_scene_object_class') != 'Weapon_Accessory':
            raise ValueError(f'{w.template} is not a Weapon_Accessory')
        if dsgo.to_py(root.get('custom_parameter')) != []:
            raise ValueError(f'{w.template}: custom_parameter is not empty (an equipment effect)')
        for lang, key in NAME_KEYS.items() if w.kind == 'vehicle' else ():
            root.get(key)
            root.set(key, w.text[lang][0])
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
        root.get('AmmoDamageReduce')
        root.set('AmmoDamageReduce', Node(list(GYROJET_DAMAGE_REDUCE)))
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
GYROJET_STATS = {0: '6', 1: '3.0', 2: '40-200', 3: '2.0', 4: '2.0', 5: 'A'}      # Cascade: capacity .. accuracy
CARBINE_STATS = {0: '5', 1: '1.3', 2: '300', 3: '2.5', 4: '600', 5: '1800', 6: 'S++'}   # Blazer


def text_row(template: Node, w: Weapon, lang: str) -> Node:
    """Its text row: the template's for a none row; else its own name and description, and the template's stat lines
    with their values (fixed strings: these weapons do not grow with stars)."""
    row = copy.deepcopy(template)
    if w.kind == 'none':
        return row
    row.items[0], row.items[1] = w.text[lang]
    if w.kind == 'vehicle':   # the template's own lines (No Equipment's): the vehicle's numbers are in its description
        return row
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
