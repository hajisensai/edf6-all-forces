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
             goes through what it hits (AmmoIsPenetration; at most 2 enemies: src/runeblade_weapons_logic.h, its rounds
             marked by AmmoHitSizeAdjust)
  tuned rows the Rune Blade's other weapons (plan §2.2, §2.3): each its Tune, the stock template's SGO with those keys set
             (scalars), its stat lines fixed strings, its rounds marked where the plugin has to find them (RUNE_MARKS).
             Their run-time behaviour (combos, the ring, the seven circles, the light pillars) is not here:
             docs/newclass-re/runeblade-weapons-plan.md says what each needs; until it is in, the row is acquire 0 (LOCKED)
Their models are the templates' (placeholders: tools/make_zw_classes.py ASSETS).

RELEASED freezes the order: rows never move, so IDS only ever grows at its end.
"""
from __future__ import annotations

import copy
import hashlib
import os
import struct
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
class Tune:
    """A 'tuned' row's SGO (the Rune Blade's weapons, docs/newclass-re/runeblade-weapons-plan.md): its template's, these
    keys set. Scalars (no star growth until plan §3 stage 4); a key left out keeps the template's (its star curve)."""
    ammo_class: str                     # the template's AmmoClass (checked: not the template we read otherwise)
    params: dict[str, object]           # SGO key -> value (every key must be in the template)
    stats: dict[int, tuple[str, ...]]   # text line index -> the values of its $0, $1 (a line without $: its number)
    color: tuple[float, ...] = ()       # AmmoColor (empty: the template's)
    mark: int = 0                       # AmmoHitSizeAdjust = the float of bits RUNE_MARK_BASE | mark (0: the template's)


@dataclass(frozen=True)
class Weapon:
    id: str
    category: int
    template: str                       # the stock row (and SGO) it is made from
    kind: str                           # 'none' | 'gyrojet' | 'carbine' | 'vehicle' | 'tuned'
    text: dict[str, tuple[str, str]]    # lang -> (name, description); a none row keeps the template's texts
    vehicle: str = ''                   # a vehicle row's vehicle (pylib/zw_vehicles.py ZwVehicle.row: its own id)
    tune: Tune | None = None            # a 'tuned' row's SGO
    # WEAPONTABLE column 5. 0: no save owns it, so nobody can equip it (a row whose runtime is not in yet: the Rune Blade's
    # melee, Seven Stars and the beacon, docs/newclass-re/runeblade-weapons-plan.md §0); a class default must be 1.
    acquire: float = ACQUIRE


NONE_TEMPLATE = 'aSupportNone'

# The Rune Blade's weapons (plan §2): one blue-violet for every trace (the plan's RuneBlade colour (0.6, 0.4, 1), HDR-bright),
# and the bullet marks the plugin will tell their rounds apart by (src/runeblade_weapons_logic.h kMarkBase: AmmoHitSizeAdjust
# about 1.0005, the hit radius as good as unchanged). Every stock AmmoHitSizeAdjust near 1.0 is exactly 1.0 (tools/calls.py
# THROW_MARK_BASE); the thrown drones' / gravity gun's / boarding gun's marks are under 0x3F801000, the barrages'
# 0x3F801000..0x3F801FFF, the Andryusha's 0x3F8F4A36/7: 0x3F802000..0x3F8020FF is free.
RUNE_COLOR = (0.9, 0.6, 2.8, 1.0)
RUNE_MARK_BASE = 0x3F802000
RUNE_MARKS = {'carbine': 1, 'saber': 2, 'lance': 3, 'ring': 4, 'hammer': 5, 'iai': 6, 'beacon': 7}
LOCKED = 0.0   # acquire of a row whose runtime is not in yet (docs/newclass-re/runeblade-weapons-plan.md §0)

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
        'SC': ('刻印卡宾枪「晨钟」', '刻印剑士的主力远程武器。栓动式，逐发射出蓝紫色的刻印光束，最多贯穿 2 个敌人。\n'
                              '每发之后要拉一次枪栓，射速慢但单发威力高；飞行中也能射击，不耗以太槽。\n\n'
                              '模型暂用光耀者代替。'),
        'CN': ('刻印卡賓槍「晨鐘」', '刻印劍士的主力遠程武器。栓動式，逐發射出藍紫色的刻印光束，最多貫穿 2 個敵人。\n'
                              '每發之後要拉一次槍栓，射速慢但單發威力高；飛行中也能射擊，不耗以太槽。\n\n'
                              '模型暫用光耀者代替。'),
        'JA': ('ルーンカービン「暁鐘」', 'ルーンブレイドの主力遠距離武器。ボルトアクション式で、青紫のルーン光線を一発ずつ放ち、'
                                 '敵を 2 体まで貫通する。\n一発ごとにボルトを引くため連射は遅いが、一発の威力は高い。'
                                 '飛行中も撃て、エーテルゲージは消費しない。\n\nモデルは仮にブレイザーを使用。'),
        'EN': ('Rune Carbine "Matins Bell"', "The Rune Blade's main long-range weapon. Bolt action: it fires one "
                                             'blue-violet rune beam at a time that goes through up to two enemies.\nThe '
                                             'bolt is worked after every shot, so it fires slowly, but each shot hits '
                                             'hard; it fires in flight too, without using the ether gauge.\n\n'
                                             "The Blazer's model stands in for its own."),
        'KR': ('룬 카빈 「새벽종」', '룬 블레이드의 주력 원거리 무기. 볼트 액션식으로 청자색 룬 광선을 한 발씩 쏘며 적을 '
                                '최대 2체까지 관통한다.\n한 발마다 노리쇠를 당겨야 해서 연사는 느리지만 한 발의 위력은 '
                                '높다. 비행 중에도 쏠 수 있고 에테르 게이지를 쓰지 않는다.\n\n모델은 임시로 블레이저를 '
                                '사용한다.'),
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
    # the Rune Blade's weapons (plan §2.2, §2.3; docs/newclass-re/runeblade-weapons-plan.md: what each still needs at run time)
    Weapon('ZW_RuneSaber01', 500, 'hWeapon002', 'tuned', {
        'SC': ('刻印军刀「破晓」', '刻印剑士的基础近战武器。三段连斩：横斩、回斩、上挑；空中按下则急坠下劈。\n'
                             '每刀留下一道弧形光痕，第三段的光痕停留 0.5 秒持续造成伤害。出手快、范围中等，适合清理小型敌人。'
                             '\n\n模型暂用力场之刃代替。'),
        'CN': ('刻印軍刀「破曉」', '刻印劍士的基礎近戰武器。三段連斬：橫斬、迴斬、上挑；空中按下則急墜下劈。\n'
                             '每刀留下一道弧形光痕，第三段的光痕停留 0.5 秒持續造成傷害。出手快、範圍中等，適合清理小型敵人。'
                             '\n\n模型暫用力場之刃代替。'),
        'JA': ('ルーンサーベル「黎明」', 'ルーンブレイドの基本近接武器。横斬り、返し斬り、斬り上げの三段斬り。空中では急降下'
                                 '斬りになる。\n一太刀ごとに弧状の光痕が残り、三段目の光痕は 0.5 秒とどまってダメージを与え'
                                 '続ける。出が速く範囲は中程度で、小型の敵の掃討に向く。\n\nモデルは仮にフォース・ブレードを'
                                 '使用。'),
        'EN': ('Rune Saber "Daybreak"', "The Rune Blade's basic melee weapon. A three-cut combo: a sweep, a return cut "
                                        'and a rising cut; in the air, a plunging downward cut instead.\nEach cut leaves an '
                                        'arc of light; the third one lingers for 0.5 s and keeps hurting. Quick, with a '
                                        "middling reach: for clearing small enemies.\n\nThe Force Blade's model stands in "
                                        'for its own.'),
        'KR': ('룬 세이버 「여명」', '룬 블레이드의 기본 근접 무기. 가로 베기, 되돌려 베기, 올려 베기의 3단 연속 베기. 공중에서는 '
                                '급강하 내려 베기가 된다.\n한 번 벨 때마다 호 모양의 빛 자국이 남고, 3단째의 빛 자국은 0.5초 '
                                '머물며 계속 피해를 준다. 빠르고 범위는 중간이라 소형 적 소탕에 알맞다.\n\n모델은 임시로 포스 '
                                '블레이드를 사용한다.'),
    }, tune=Tune('ShockWaveBullet01', {
        'AmmoCount': 30.0, 'ReloadTime': 90.0, 'AmmoDamage': 90.0, 'AmmoSpeed': 6.0, 'AmmoAlive': 8.0, 'AmmoSize': 2.0,
    }, {0: ('90', '48'), 1: ('180', '48'), 4: ('30',), 5: ('1.5',)}, RUNE_COLOR, RUNE_MARKS['saber']),
        acquire=LOCKED),
    Weapon('ZW_RuneLance01', 500, 'hPileBanker01', 'tuned', {
        'SC': ('以太骑枪「天穹」', '蓄力后沿视线方向冲刺突刺，地面和空中都能使用；空中刺完可以接踏阵追击。\n'
                             '刺中时刻印光阵炸开，直线贯穿多个敌人。适合远距离突进。\n\n模型暂用爆破长枪代替。'),
        'CN': ('以太騎槍「天穹」', '蓄力後沿視線方向衝刺突刺，地面和空中都能使用；空中刺完可以接踏陣追擊。\n'
                             '刺中時刻印光陣炸開，直線貫穿多個敵人。適合遠距離突進。\n\n模型暫用爆破長槍代替。'),
        'JA': ('エーテルランス「天穹」', 'チャージ後、視線の方向へ突進して突き刺す。地上でも空中でも使え、空中で突いた後は'
                                 '踏陣で追撃できる。\n命中するとルーンの光陣が弾け、一直線に複数の敵を貫く。遠距離からの'
                                 '突撃向き。\n\nモデルは仮にブラストホール・スピアを使用。'),
        'EN': ('Ether Lance "Firmament"', 'Charge it, then dash along your line of sight and thrust; on the ground or in '
                                          'the air, and an airborne thrust can be followed by a rune step.\nOn a hit a rune '
                                          'circle bursts and the thrust goes straight through several enemies. For long '
                                          "lunges.\n\nThe Blasthole Spear's model stands in for its own."),
        'KR': ('에테르 랜스 「천궁」', '차지한 뒤 시선 방향으로 돌진해 찌른다. 지상과 공중 모두 쓸 수 있고, 공중에서 찌른 뒤에는 '
                                 '답진으로 추격할 수 있다.\n명중하면 룬 광진이 터지며 일직선으로 여러 적을 관통한다. 원거리 '
                                 '돌격용.\n\n모델은 임시로 블래스트홀 스피어를 사용한다.'),
    }, tune=Tune('PileBunkerBullet01', {
        'AmmoCount': 12.0, 'ReloadTime': 120.0, 'AmmoDamage': 700.0, 'AmmoSpeed': 10.0, 'AmmoAlive': 6.0,
        'AmmoIsPenetration': 1.0,
    }, {0: ('700',), 1: ('875',), 3: ('12',), 4: ('2.0',)}, RUNE_COLOR, RUNE_MARKS['lance']), acquire=LOCKED),
    Weapon('ZW_RuneRing01', 500, 'hWeapon002', 'tuned', {
        'SC': ('刻印回旋刃「群星」', '近身时双刃乱舞；按键把双刃合成光环掷出，绕一圈后飞回。\n'
                              '被切过的敌人身上留下小刻印，光环飞回时依次引爆。适合中距离群伤和对空。\n\n'
                              '回旋飞行尚未实装，暂以缓慢扩散的光刃代替；模型暂用力场之刃代替。'),
        'CN': ('刻印迴旋刃「群星」', '近身時雙刃亂舞；按鍵把雙刃合成光環擲出，繞一圈後飛回。\n'
                              '被切過的敵人身上留下小刻印，光環飛回時依次引爆。適合中距離群傷和對空。\n\n'
                              '迴旋飛行尚未實裝，暫以緩慢擴散的光刃代替；模型暫用力場之刃代替。'),
        'JA': ('ルーンリング「群星」', '接近時は双刃で乱舞し、ボタンで双刃を光輪に合わせて投げる。光輪はひと回りして戻って'
                               'くる。\n斬った敵には小さなルーンが残り、光輪が戻るときに順に起爆する。中距離の集団戦と対空'
                               '向き。\n\n旋回飛行は未実装で、ゆっくり広がる光刃で代用。モデルは仮にフォース・ブレードを使用。'),
        'EN': ('Rune Ring "Stars"', 'Up close, a flurry of twin blades; at the press of a button the blades join into a '
                                    'ring of light that is thrown, circles and flies back.\nWhat it cuts is left with a '
                                    'small rune, set off one after another as the ring returns. For crowds at middle '
                                    'range and for air targets.\n\nThe circling flight is not in yet: a slow, wide wave of '
                                    "light stands in for it; the Force Blade's model stands in for its own."),
        'KR': ('룬 링 「군성」', '근접해서는 쌍검으로 난무하고, 버튼을 누르면 쌍검을 빛의 고리로 합쳐 던진다. 고리는 한 바퀴 '
                             '돌아 되돌아온다.\n벤 적에게는 작은 룬이 남고, 고리가 돌아올 때 차례로 폭발한다. 중거리 '
                             '다수전과 대공용.\n\n선회 비행은 아직 구현되지 않아 천천히 퍼지는 빛의 칼날로 대신한다. 모델은 '
                             '임시로 포스 블레이드를 사용한다.'),
    }, tune=Tune('ShockWaveBullet01', {
        'AmmoCount': 20.0, 'ReloadTime': 120.0, 'AmmoDamage': 60.0, 'AmmoSpeed': 3.0, 'AmmoAlive': 40.0, 'AmmoSize': 3.0,
    }, {0: ('60', '120'), 1: ('120', '120'), 4: ('20',), 5: ('2.0',)}, RUNE_COLOR, RUNE_MARKS['ring']),
        acquire=LOCKED),
    Weapon('ZW_RuneHammer01', 500, 'hWeapon007', 'tuned', {
        'SC': ('以太战锤「坠星」', '地面横扫可击退敌人；空中蓄力后垂直下砸，落点越高伤害越大，但会耗尽以太槽。\n'
                             '落地时巨大光阵向外扩散成冲击波。高风险的范围大招。\n\n模型暂用火山之锤代替。'),
        'CN': ('以太戰錘「墜星」', '地面橫掃可擊退敵人；空中蓄力後垂直下砸，落點越高傷害越大，但會耗盡以太槽。\n'
                             '落地時巨大光陣向外擴散成衝擊波。高風險的範圍大招。\n\n模型暫用火山之錘代替。'),
        'JA': ('エーテルハンマー「墜星」', '地上では薙ぎ払いで敵を吹き飛ばす。空中ではチャージ後に真下へ叩きつけ、高い所から'
                                   'ほど威力が上がるが、エーテルゲージを使い切る。\n着地すると巨大な光陣が衝撃波となって'
                                   '広がる。ハイリスクな範囲攻撃。\n\nモデルは仮にボルケーン・ハンマーを使用。'),
        'EN': ('Ether Hammer "Falling Star"', 'On the ground a knock-back sweep; in the air, charge it and smash straight '
                                              'down: the higher you fall from, the harder it hits, but it empties the ether '
                                              'gauge.\nLanding spreads a huge rune circle outward as a shock wave. A '
                                              "high-risk area attack.\n\nThe Vulcan Hammer's model stands in for its own."),
        'KR': ('에테르 해머 「추성」', '지상에서는 휩쓸기로 적을 날려 버린다. 공중에서는 차지 후 수직으로 내려찍으며, 높은 곳에서 '
                                 '떨어질수록 위력이 오르지만 에테르 게이지를 모두 쓴다.\n착지하면 거대한 광진이 충격파가 되어 '
                                 '퍼진다. 고위험 범위 공격.\n\n모델은 임시로 볼케인 해머를 사용한다.'),
    }, tune=Tune('ShockWaveBullet01', {
        'AmmoCount': 3.0, 'ReloadTime': 480.0, 'AmmoDamage': 250.0,
    }, {0: ('250', '40'), 1: ('1250',), 2: ('3000',), 5: ('3',), 6: ('8.0',)}, RUNE_COLOR, RUNE_MARKS['hammer']),
        acquire=LOCKED),
    Weapon('ZW_RuneIai01', 500, 'hWeapon008', 'tuned', {
        'SC': ('刻时居合「残光」', '按住进入架势，鞘口的表盘刻印一格格亮起；满蓄后拔刀，斩出一道横贯前方的光线。\n'
                             '被切到的敌人身上浮出表盘刻印，约 1 秒后同时爆开。单发威力最高，用来对付巨型敌人。\n\n'
                             '模型暂用发电机之刃代替。'),
        'CN': ('刻時居合「殘光」', '按住進入架勢，鞘口的錶盤刻印一格格亮起；滿蓄後拔刀，斬出一道橫貫前方的光線。\n'
                             '被切到的敵人身上浮出錶盤刻印，約 1 秒後同時爆開。單發威力最高，用來對付巨型敵人。\n\n'
                             '模型暫用發電機之刃代替。'),
        'JA': ('刻時居合「残光」', '押し続けると構えに入り、鞘口の文字盤ルーンが一目盛りずつ灯る。溜めきって抜刀すると、'
                             '前方を横一文字に貫く光線を放つ。\n斬られた敵には文字盤のルーンが浮かび、約 1 秒後に一斉に'
                             '弾ける。単発威力は最大で、巨大な敵向き。\n\nモデルは仮にダイナモ・ブレードを使用。'),
        'EN': ('Chrono Iai "Afterglow"', 'Hold to take a stance as the dial runes at the scabbard light up one by one; '
                                         'fully charged, the draw cuts a line of light straight across the front.\nWhat it '
                                         'cuts shows a dial rune that bursts with all the others about 1 s later. The '
                                         "strongest single blow, for giants.\n\nThe Dynamo Blade's model stands in for its "
                                         'own.'),
        'KR': ('각시 거합 「잔광」', '누르고 있으면 자세를 잡고, 칼집 입구의 문자판 룬이 한 칸씩 켜진다. 완전히 모은 뒤 칼을 뽑으면 '
                                '전방을 가로지르는 빛의 선을 벤다.\n베인 적에게는 문자판 룬이 떠오르고 약 1초 뒤 한꺼번에 '
                                '터진다. 단발 위력이 가장 높아 거대한 적을 상대한다.\n\n모델은 임시로 다이나모 블레이드를 '
                                '사용한다.'),
    }, tune=Tune('ShockWaveBullet01', {
        'AmmoCount': 3.0, 'ReloadTime': 300.0, 'AmmoDamage': 600.0, 'AmmoSpeed': 12.0, 'AmmoAlive': 10.0,
    }, {0: ('600', '120'), 1: ('1200', '120'), 2: ('3000', '120'), 5: ('3',), 6: ('5.0',)}, RUNE_COLOR,
        RUNE_MARKS['iai']), acquire=LOCKED),
    Weapon('ZW_RuneRevolver01', 501, 'aWeapon014', 'tuned', {
        'SC': ('刻印左轮「萤火」', '六发装的刻印左轮，可快速连射。射出拖着光尾的萤火弹，命中时爆出小光点。\n'
                             '适合近中距离补刀，配合近战使用。\n\n模型暂用光耀者代替。'),
        'CN': ('刻印左輪「螢火」', '六發裝的刻印左輪，可快速連射。射出拖著光尾的螢火彈，命中時爆出小光點。\n'
                             '適合近中距離補刀，配合近戰使用。\n\n模型暫用光耀者代替。'),
        'JA': ('ルーンリボルバー「蛍火」', '6 連発のルーンリボルバーで、素早く連射できる。光の尾を引く蛍火弾を撃ち、命中すると'
                                   '小さな光が弾ける。\n近・中距離のとどめ用で、近接攻撃と組み合わせて使う。\n\n'
                                   'モデルは仮にブレイザーを使用。'),
        'EN': ('Rune Revolver "Firefly"', 'A six-shot rune revolver that fires quickly. Its firefly rounds trail light '
                                          'and burst into a small spark where they hit.\nFor finishing enemies off at close '
                                          "to middle range, alongside the melee weapon.\n\nThe Blazer's model stands in for "
                                          'its own.'),
        'KR': ('룬 리볼버 「반딧불」', '6연발 룬 리볼버로 빠르게 연사할 수 있다. 빛의 꼬리를 끄는 반딧불탄을 쏘며, 명중하면 작은 '
                                 '빛이 터진다.\n근·중거리 마무리용으로 근접 공격과 함께 쓴다.\n\n모델은 임시로 블레이저를 '
                                 '사용한다.'),
    }, tune=Tune('LaserBullet02', {
        'AmmoCount': 6.0, 'FireInterval': 6.0, 'ReloadTime': 90.0, 'FireAccuracy': 0.01, 'FireRecoil': 0.1,
        'AmmoSpeed': 12.0, 'AmmoAlive': 12.0, 'AmmoDamage': 45.0, 'AmmoIsPenetration': 0.0, 'AmmoSize': 0.2,
    }, {0: ('6',), 1: ('10.0',), 2: ('45',), 3: ('1.5',), 4: ('144',), 5: ('720',), 6: ('A',)}, RUNE_COLOR)),
    Weapon('ZW_RuneStars01', 501, 'pWeapon186', 'tuned', {
        'SC': ('光阵浮游炮「七星」', '按住开火，身边依次浮起刻印光阵，最多 7 个；松手时一齐射出追踪光矢。会消耗以太槽。\n'
                              '光阵按北斗七星排布，7 道光矢划着弧线追向敌人。对空、对群，飞行中的主要输出。\n\n'
                              '模型暂用海市蜃楼代替。'),
        'CN': ('光陣浮游砲「七星」', '按住開火，身邊依次浮起刻印光陣，最多 7 個；鬆手時一齊射出追蹤光矢。會消耗以太槽。\n'
                              '光陣按北斗七星排布，7 道光矢劃著弧線追向敵人。對空、對群，飛行中的主要輸出。\n\n'
                              '模型暫用海市蜃樓代替。'),
        'JA': ('光陣浮遊砲「七星」', '射撃ボタンを押し続けると、身の回りにルーンの光陣が最大 7 つまで順に浮かぶ。離すと追尾する'
                               '光の矢を一斉に放つ。エーテルゲージを消費する。\n光陣は北斗七星の形に並び、7 本の光の矢が弧を'
                               '描いて敵を追う。対空・対集団で、飛行中の主力。\n\nモデルは仮にミラージュを使用。'),
        'EN': ('Rune Array "Seven Stars"', 'Hold fire and rune circles rise around you one by one, up to seven; let go '
                                           'and they loose homing arrows of light together. It uses the ether gauge.\nThe '
                                           'circles stand as the Big Dipper, and the seven arrows arc after the enemies. '
                                           "For air targets and crowds: the main weapon in flight.\n\nThe Mirage's model "
                                           'stands in for its own.'),
        'KR': ('광진 부유포 「칠성」', '사격 버튼을 누르고 있으면 주위에 룬 광진이 최대 7개까지 차례로 떠오르고, 떼면 추적하는 빛의 '
                                 '화살을 한꺼번에 쏜다. 에테르 게이지를 소모한다.\n광진은 북두칠성 모양으로 늘어서고, 7발의 빛의 '
                                 '화살이 호를 그리며 적을 쫓는다. 대공·대집단용으로 비행 중의 주력.\n\n모델은 임시로 미라주를 '
                                 '사용한다.'),
    }, tune=Tune('HomingLaserBullet01', {
        'AmmoCount': 1.0, 'ReloadTime': 90.0, 'FireCount': 7.0, 'EnergyChargeRequire': [-1.0, -1.0], 'AmmoDamage': 80.0,
    }, {0: ('80', '7'), 5: ('----',), 6: ('1.5',)}, RUNE_COLOR), acquire=LOCKED),
    Weapon('ZW_EtherBeacon01', 502, 'eWeapon006', 'tuned', {
        'SC': ('以太炮击信标', '打出一发信标，后方的大口径以太炮照着信标做一轮大范围炮击。要靠造成伤害积攒点数，次数有限。\n'
                           '信标落地展开巨大光阵，几秒后天上落下一排光柱。\n\n光柱特效尚未实装，暂为原版炮击；'
                           '模型暂用原版信号弹代替。'),
        'CN': ('以太砲擊信標', '打出一發信標，後方的大口徑以太砲照著信標做一輪大範圍砲擊。要靠造成傷害積攢點數，次數有限。\n'
                           '信標落地展開巨大光陣，幾秒後天上落下一排光柱。\n\n光柱特效尚未實裝，暫為原版砲擊；'
                           '模型暫用原版信號彈代替。'),
        'JA': ('エーテル砲撃ビーコン', 'ビーコンを撃ち込むと、後方の大口径エーテル砲がそこへ広範囲の砲撃を一巡行う。'
                               'ダメージを与えてポイントを貯める必要があり、回数は限られる。\nビーコンが着地すると巨大な光陣が'
                               '広がり、数秒後に空から光の柱が並んで降る。\n\n光の柱の演出は未実装で、今は原作の砲撃。'
                               'モデルは仮に原作の信号弾を使用。'),
        'EN': ('Ether Barrage Beacon', 'Fire a beacon and the big ether guns behind the lines lay a wide barrage on it. '
                                       'It is paid for with points earned by dealing damage, so it can be used only so '
                                       'often.\nThe beacon spreads a huge rune circle where it lands; seconds later a row '
                                       'of light pillars falls from the sky.\n\nThe light pillars are not in yet: the '
                                       "game's own barrage stands in; so does its marker's model."),
        'KR': ('에테르 포격 비컨', '비컨을 쏘면 후방의 대구경 에테르포가 그곳에 넓은 범위의 포격을 한 차례 가한다. 피해를 주어 '
                               '포인트를 모아야 하며 횟수가 한정된다.\n비컨이 착지하면 거대한 광진이 펼쳐지고, 몇 초 뒤 하늘에서 '
                               '빛의 기둥이 줄지어 떨어진다.\n\n빛의 기둥 연출은 아직 구현되지 않아 지금은 원작의 포격이다. '
                               '모델은 임시로 원작의 신호탄을 사용한다.'),
    }, tune=Tune('SmokeCandleBullet01', {
        'AmmoCount': 1.0, 'ReloadTime': 4000.0, 'AmmoDamage': 1000.0,
    }, {0: ('1000',), 2: ('4000',)}, (0.6, 0.4, 1.0, 0.5), RUNE_MARKS['beacon']), acquire=LOCKED),
)
IDS: tuple[str, ...] = tuple(w.id for w in WEAPONS)
BY_ID = {w.id: w for w in WEAPONS}

# Every order of IDS a release installed: (how many, sha256 of those ids joined by '\n'), frozen (tools/selftest.py,
# tests/zw_classes_test.py): a later weapon is only ever added at the end.
RELEASED: dict[str, tuple[int, str]] = {
    'new classes stage 1 (2026-11-28)': (5, '8d36b020ae73501fe550abbd106c121977f4caa1fbfe7e53f78729c5f8679e1d'),
    'vehicler stage 2 (2026-11-28)': (8, 'c71ed7b9f776f865c51730e0475e07d978ec28a7d20c338672329efdda5709a1'),
    'runeblade weapons (2026-11-28)': (16, '852c3f9924b7d95bb254d8c6c3775c8f0de27bf01d5b7ab6668c1c4d2e77738b'),
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
        root.get('AmmoHitSizeAdjust')
        root.set('AmmoHitSizeAdjust', rune_mark(RUNE_MARKS['carbine']))   # its 2-enemy pierce limit's tag
    elif w.kind == 'tuned':
        t = w.tune
        if t is None:
            raise ValueError(f'{w.id}: a tuned row without its Tune')
        if root.get('AmmoClass') != t.ammo_class:
            raise ValueError(f'{w.template}: AmmoClass {root.get("AmmoClass")}, want {t.ammo_class}')
        for k, v in t.params.items():
            root.get(k)   # KeyError: a key the template does not have
            root.set(k, Node(list(v)) if isinstance(v, (list, tuple)) else float(v))
        if t.color:
            root.get('AmmoColor')
            root.set('AmmoColor', Node(list(t.color)))
        if t.mark:
            if root.get('AmmoHitSizeAdjust') != 1.0:   # the mark sits where the stock value is exactly 1.0
                raise ValueError(f'{w.template}: AmmoHitSizeAdjust {root.get("AmmoHitSizeAdjust")}, want 1.0 to mark it')
            root.set('AmmoHitSizeAdjust', rune_mark(t.mark))
    else:
        raise ValueError(w.kind)
    return dsgo.compact(doc)


def rune_mark(n: int) -> float:
    """AmmoHitSizeAdjust as the float of bits RUNE_MARK_BASE | n (src/runeblade_weapons_logic.h IsMark / MarkOf)."""
    if not 0 < n < 0x100:
        raise ValueError(f'rune mark {n}')
    return struct.unpack('<f', struct.pack('<I', RUNE_MARK_BASE | n))[0]


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
    row.items[5] = w.acquire
    row.items[6] = Node([])
    row.items[7] = STAR_CAP
    row.items[8] = PACK
    return row


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
    if w.kind == 'tuned' and w.tune is not None:
        stats: dict[int, tuple[str, ...]] = w.tune.stats
    else:
        stats = {i: (v,) for i, v in (GYROJET_STATS if w.kind == 'gyrojet' else CARBINE_STATS).items()}
    lines = row.items[2].items
    for i, values in stats.items():
        line = lines[i]
        fmt = line.items[1]
        value = values[0]
        if '$0' in fmt:
            for k, v in enumerate(values):
                fmt = fmt.replace(f'${k}', v)
            if '$' in fmt:   # a value of the line left out: its curve is dropped below, so it would show '$1'
                raise ValueError(f'{w.id} {lang} line {i}: {line.items[1]!r} wants more than {values}')
            line.items[:] = [line.items[0], fmt]
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
