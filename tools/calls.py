"""The Air Raider's call weapons: the one table both sides are made from. tools/call_weapons.py writes the
weapon rows and SGOs from it; tools/gen_calls.py writes src/calls.inc (the plugin's kCalls, kCallLabels and
kCallRows, src/airstrike.cpp) from it; tools/selftest_calls.py checks that the generated file is current, that
the hand-kept copies elsewhere agree, and that the order rules below hold (CI runs both).

Row order: saves refer to weapons by their row in the shared weapon table, so a row, once installed, never
moves. tools/call_weapons.py keeps every installed row where it is (by its id) and appends the ones a table
lacks at its end, in CALLS order. A new call is only ever appended to CALLS, never inserted, and none is ever
removed (RELEASED: each order a release installed must stay a prefix of CALLS).
"""
from __future__ import annotations

from dataclasses import dataclass

ID_PREFIX = 'EDF6VC_CALL_'
# An uninstalled call's row (tools/call_weapons.py retire): its template's stock row under this id, so the row
# keeps its index (no save loses or gains a weapon) and the plugin's GrantCalls (ID_PREFIX only) leaves it be.
RETIRED_PREFIX = 'EDF6VC_RETIRED_'


@dataclass(frozen=True)
class Call:
    id: str
    mark: float          # a call: AmmoHitSizeAdjust, the plugin's call marker; a vehicle request: the jet's mark
    kind: str            # key into KINDS
    follow: bool         # escorts the caller instead of holding the marked point
    count: int           # what it brings; also the stock bomber fallback's plane count (Ammo_CustomParameter[2][1])
    reload: float        # ReloadTime[0], the base of the star curve
    level: float         # WEAPONTABLE column 4, same units as docs/weapons.csv level_raw
    brings: str          # 'jets' (JetLaunch), 'helis' (HeliLaunch), 'sub' (SubLaunch), 'vehicle' (a vehicle request)
    log: str = ''        # its name in the plugin's log
    fuel_sec: int = 0    # the plugin's fuel limit for what it brings (0: none)
    role: str = ''       # brings 'jets': the JetRole (src/crew.h)
    body: str = ''       # brings 'helis': the HeliBody (src/crew.h)
    # A vehicle request (template eWeapon394): the OBJECT SGO it brings (tools/make_jets.py, no extension) and
    # the pylib/vcobjects.py JETS entry it is made like; `mark` is then that jet's mark (its speed gain k).
    vehicle: str = ''
    jet: str = ''

    @property
    def flown(self) -> bool:
        """The plugin launches what it brings at the call (kCalls); a vehicle request is the game's own."""
        return self.brings != 'vehicle'

    @property
    def modal(self) -> bool:
        """It comes in a guard and a follow version, and its name says which."""
        return self.brings in ('jets', 'helis')


CALLS: tuple[Call, ...] = (
    Call('EDF6VC_CALL_INTERCEPTOR', 7101, 'interceptor', False, 2, 900, 0.3, 'jets', 'interceptors (guard)', 240, 'interceptor'),
    Call('EDF6VC_CALL_INTERCEPTOR_F', 7102, 'interceptor', True, 2, 1035, 0.5, 'jets', 'interceptors (follow)', 240, 'interceptor'),
    Call('EDF6VC_CALL_STRIKE', 7103, 'strike', False, 3, 1500, 0.5, 'jets', 'strike jets (guard)', 240, 'strike'),
    Call('EDF6VC_CALL_STRIKE_F', 7104, 'strike', True, 3, 1725, 0.7, 'jets', 'strike jets (follow)', 240, 'strike'),
    Call('EDF6VC_CALL_MULTIROLE', 7105, 'multirole', False, 3, 1800, 0.8, 'jets', 'multirole jets (guard)', 300, 'multirole'),
    Call('EDF6VC_CALL_MULTIROLE_F', 7106, 'multirole', True, 3, 2070, 1.0, 'jets', 'multirole jets (follow)', 300, 'multirole'),
    Call('EDF6VC_CALL_FIGHTER', 7107, 'fighter', False, 4, 2000, 1.0, 'jets', 'fighters (guard)', 300, 'fighter'),
    Call('EDF6VC_CALL_FIGHTER_F', 7108, 'fighter', True, 4, 2300, 1.2, 'jets', 'fighters (follow)', 300, 'fighter'),
    Call('EDF6VC_CALL_CARRIER', 7109, 'carrier', False, 1, 3000, 1.8, 'jets', 'carrier (guard)', 600, 'carrier'),
    Call('EDF6VC_CALL_CARRIER_F', 7110, 'carrier', True, 1, 3450, 2.0, 'jets', 'carrier (follow)', 600, 'carrier'),
    Call('EDF6VC_CALL_HELI', 7111, 'heli', False, 2, 1600, 0.4, 'helis', 'Brute helis (guard)', 360, body='brute410'),
    Call('EDF6VC_CALL_HELI_F', 7112, 'heli', True, 2, 1800, 0.6, 'helis', 'Eros helis (follow)', 360, body='eros506'),
    # Appended 2026-10-04: carriers whose drones blow themselves up next to the enemy.
    Call('EDF6VC_CALL_BLAST_CARRIER', 7113, 'blast_carrier', False, 1, 3300, 2.0, 'jets', 'blast drone carrier (guard)', 600,
         'blastCarrier'),
    Call('EDF6VC_CALL_BLAST_CARRIER_F', 7114, 'blast_carrier', True, 1, 3800, 2.2, 'jets', 'blast drone carrier (follow)', 600,
         'blastCarrier'),
    Call('EDF6VC_CALL_DOLL_CARRIER', 7115, 'doll_carrier', False, 1, 3600, 2.2, 'jets', 'doll drone carrier (guard)', 600,
         'dollCarrier'),
    Call('EDF6VC_CALL_DOLL_CARRIER_F', 7116, 'doll_carrier', True, 1, 4100, 2.4, 'jets', 'doll drone carrier (follow)', 600,
         'dollCarrier'),
    # Appended 2026-10-04: the submarine carrier (src/subcarrier.cpp): one, it stays the mission following the
    # player (it surfaces kSubAhead past the marker, its 1664 m hull clear of the caller; three at most).
    Call('EDF6VC_CALL_SUB', 7117, 'sub', True, 1, 7200, 3.0, 'sub', 'submarine carrier'),
    # Appended 2026-10-04: the jets the player flies (src/playerjet.cpp kKinds), vehicle requests.
    Call('EDF6VC_CALL_PJET_FIGHTER', 7201, 'pjet_fighter', False, 0, 6000, 1.0, 'vehicle',
         vehicle='EDF6VC_PJET_FIGHTER', jet='edf6tr_pjet_fighter_mission'),
    Call('EDF6VC_CALL_PJET_STRIKE', 7202, 'pjet_strike', False, 0, 6500, 0.8, 'vehicle',
         vehicle='EDF6VC_PJET_STRIKE', jet='edf6tr_pjet_strike_mission'),
    # Appended 2026-10-04: the gunship (src/jet.cpp GunshipFire), a bomber401 circling and shelling. 0.7.0 had
    # put these two before the player jets', which moved those rows (WITHDRAWN).
    Call('EDF6VC_CALL_GUNSHIP', 7118, 'gunship', False, 1, 2600, 1.2, 'jets', 'gunship (guard)', 600, 'gunship'),
    Call('EDF6VC_CALL_GUNSHIP_F', 7119, 'gunship', True, 1, 3000, 1.4, 'jets', 'gunship (follow)', 600, 'gunship'),
)
IDS: tuple[str, ...] = tuple(c.id for c in CALLS)
FLOWN: tuple[Call, ...] = tuple(c for c in CALLS if c.flown)   # the plugin's kCalls, in this order

# Every order of CALLS a commit on main installed (git log -p tools/call_weapons.py), frozen: each must stay a
# prefix of CALLS (tools/selftest_calls.py). A release that adds calls adds its own order here.
RELEASED: dict[str, tuple[str, ...]] = {
    '94808aa (the first 12 calls)': IDS[:12],
    'a1c8dbb (blast and doll drone carriers)': IDS[:16],
    '2ce755c (submarine carrier)': IDS[:17],
    '5d1a3ed / 9233829 (player jets)': IDS[:19],
    '0.7.1 (the gunship after the player jets)': IDS[:21],
}
# Orders that broke the rule and shipped: 063bf99 (0.7.0) inserted the gunship's rows before the player jets'.
# An install of it holds all of its ids, only in another order: tools/call_weapons.py keeps every installed row
# where it is (by id), so it upgrades in place.
WITHDRAWN: dict[str, tuple[str, ...]] = {
    '063bf99 (0.7.0)': IDS[:17] + ('EDF6VC_CALL_GUNSHIP', 'EDF6VC_CALL_GUNSHIP_F',
                                  'EDF6VC_CALL_PJET_FIGHTER', 'EDF6VC_CALL_PJET_STRIKE'),
}


def retired_id(call_id: str) -> str:
    return RETIRED_PREFIX + call_id[len(ID_PREFIX):]


def slot_of(row_id: str) -> str | None:
    """The call a weapon table row belongs to (its own row, or the placeholder an uninstall left), else None."""
    upper = row_id.upper()
    for c in CALLS:
        if upper in (c.id.upper(), retired_id(c.id).upper()):
            return c.id
    return None


# Per kind: name and what it does, per language (KR reuses EN).
KINDS: dict[str, dict[str, tuple[str, str]]] = {
    'pjet_fighter': {
        'SC': ('玩家战斗机', '请求一架由你自己驾驶的战斗机，空着送到信号弹处：两门机炮和导弹，轻快，转弯最急。'
                         '前推左摇杆或按上升键加油门，后拉减油门；右摇杆或鼠标转弯和俯仰。'),
        'CN': ('玩家戰鬥機', '請求一架由你自己駕駛的戰鬥機，空著送到信號彈處：兩門機砲和飛彈，輕快，轉彎最急。'
                         '前推左搖桿或按上升鍵加油門，後拉減油門；右搖桿或滑鼠轉彎和俯仰。'),
        'JA': ('戦闘機（自操縦）', '自分で操縦する戦闘機を信号弾の位置へ要請する。機関砲2門とミサイル、軽快で旋回が鋭い。'
                              '左スティック前か上昇でスロットルを上げ、後ろで下げる。右スティックかマウスで旋回と上下。'),
        'EN': ('Fighter (Fly It)', 'Requests a fighter you fly yourself, delivered empty to the flare: two guns and '
                                   'missiles, light and the tightest turner. Left stick forward or ascend opens the '
                                   'throttle, back closes it; the right stick or mouse turns and pitches.'),
    },
    'pjet_strike': {
        'SC': ('玩家攻击机', '请求一架由你自己驾驶的攻击机，空着送到信号弹处：两门机炮和导弹，更耐打，速度和转弯不如战斗机。'
                         '操作同玩家战斗机。'),
        'CN': ('玩家攻擊機', '請求一架由你自己駕駛的攻擊機，空著送到信號彈處：兩門機砲和飛彈，更耐打，速度和轉彎不如戰鬥機。'
                         '操作同玩家戰鬥機。'),
        'JA': ('攻撃機（自操縦）', '自分で操縦する攻撃機を信号弾の位置へ要請する。機関砲2門とミサイル、頑丈だが速度と旋回は'
                              '戦闘機に劣る。操作は戦闘機（自操縦）と同じ。'),
        'EN': ('Strike Jet (Fly It)', 'Requests a strike jet you fly yourself, delivered empty to the flare: two guns '
                                      'and missiles, tougher but slower and wider turning than the fighter. Flown like '
                                      'the fighter.'),
    },
    'interceptor': {
        'SC': ('截击机', '呼叫截击机，优先攻击空中目标；比制空战斗机飞得更快更高，并从更远处发射导弹。'),
        'CN': ('截擊機', '呼叫截擊機，優先攻擊空中目標；比制空戰鬥機飛得更快更高，並從更遠處發射飛彈。'),
        'JA': ('迎撃機', '迎撃機を要請する。空中の敵を優先して攻撃する。制空戦闘機より速く高く飛び、遠くからミサイルを撃つ。'),
        'EN': ('Interceptors', 'Calls interceptors that attack flying targets first, faster and higher than fighters, firing their missiles from farther out.'),
    },
    'strike': {
        'SC': ('对地攻击机', '呼叫对地攻击机，优先攻击地面目标，俯冲投弹。'),
        'CN': ('對地攻擊機', '呼叫對地攻擊機，優先攻擊地面目標，俯衝投彈。'),
        'JA': ('対地攻撃機', '対地攻撃機を要請する。地上の敵を優先し、急降下して爆撃する。'),
        'EN': ('Strike Fighters', 'Calls strike fighters that attack ground targets first, diving onto them with bombs.'),
    },
    'multirole': {
        'SC': ('多用途机', '呼叫多用途战斗机，攻击最近的目标，空中地面皆可。'),
        'CN': ('多用途機', '呼叫多用途戰鬥機，攻擊最近的目標，空中地面皆可。'),
        'JA': ('マルチロール機', 'マルチロール機を要請する。空中・地上を問わず最も近い敵を攻撃する。'),
        'EN': ('Multirole Fighters', 'Calls multirole fighters that attack the nearest target, in the air or on the ground.'),
    },
    'fighter': {
        'SC': ('制空战斗机', '呼叫制空战斗机，优先攻击空中目标。'),
        'CN': ('制空戰鬥機', '呼叫制空戰鬥機，優先攻擊空中目標。'),
        'JA': ('制空戦闘機', '制空戦闘機を要請する。空中の敵を優先して攻撃する。'),
        'EN': ('Air Superiority Fighters', 'Calls air superiority fighters that attack flying targets first.'),
    },
    'carrier': {
        'SC': ('无人机母舰', '呼叫无人机母舰：在空中盘旋，派出无人机攻击范围内的敌人。'),
        'CN': ('無人機母艦', '呼叫無人機母艦：在空中盤旋，派出無人機攻擊範圍內的敵人。'),
        'JA': ('無人機母艦', '無人機母艦を要請する。上空を旋回し、範囲内の敵へ無人機を送り込む。'),
        'EN': ('Drone Carrier', 'Calls a drone carrier that hovers overhead and sends its drones at enemies in range.'),
    },
    'blast_carrier': {
        'SC': ('自爆无人机母舰', '呼叫自爆无人机母舰：悬停在空中，放出近炸无人机，冲到敌人身边自爆。'),
        'CN': ('自爆無人機母艦', '呼叫自爆無人機母艦：懸停在空中，放出近炸無人機，衝到敵人身邊自爆。'),
        'JA': ('自爆ドローン母艦', '自爆ドローン母艦を要請する。上空に滞空し、敵に突っ込んで近接起爆するドローンを放つ。'),
        'EN': ('Blast Drone Carrier', 'Calls a carrier that hovers overhead and sends drones that dive at the enemy and blow up next to it.'),
    },
    'sub': {
        'SC': ('潜水母舰支援', '呼叫潜水母舰在信号弹前方浮上：舰身可以站人，炮塔机炮和导弹自动攻击，导弹在舰内装填，随玩家移动，留到任务结束（同时最多 3 艘）。'),
        'CN': ('潛水母艦支援', '呼叫潛水母艦在信號彈前方浮上：艦身可以站人，砲塔機砲和飛彈自動攻擊，飛彈在艦內裝填，隨玩家移動，留到任務結束（同時最多 3 艘）。'),
        'JA': ('潜水母艦支援', '潜水母艦を信号弾の先に浮上させる。甲板に乗れ、砲塔の機関砲とミサイルで自動攻撃する。ミサイルの装填は潜水母艦内でおこなわれる。プレイヤーに随伴し、作戦終了まで留まる（同時に3隻まで）。'),
        'EN': ('Submarine Carrier', 'Surfaces a submarine carrier past the flare: stand on its deck while its turret guns and missiles attack on their own (missiles reload aboard). It follows you for the rest of the mission (three at most).'),
    },
    'gunship': {
        'SC': ('炮舰机', '呼叫炮舰机：在目标点上空大圈盘旋，从机上向附近的地面敌人持续炮击，不俯冲。'),
        'CN': ('砲艦機', '呼叫砲艦機：在目標點上空大圈盤旋，從機上向附近的地面敵人持續砲擊，不俯衝。'),
        'JA': ('ガンシップ', 'ガンシップを要請する。上空を大きく旋回しながら、付近の地上の敵へ機上から砲撃を続ける。急降下はしない。'),
        'EN': ('Fixed-wing Gunship', 'Calls a fixed-wing gunship that circles wide overhead and keeps shelling nearby ground enemies from the air, without diving.'),
    },
    'doll_carrier': {
        'SC': ('人偶无人机母舰', '呼叫人偶无人机母舰：放出挂着会唱歌跳舞的人偶的无人机，慢慢飞到敌人中间吸引火力，然后自爆。'),
        'CN': ('人偶無人機母艦', '呼叫人偶無人機母艦：放出掛著會唱歌跳舞的人偶的無人機，慢慢飛到敵人中間吸引火力，然後自爆。'),
        'JA': ('人形ドローン母艦', '人形ドローン母艦を要請する。歌って踊る人形を吊るしたドローンが敵の中へ進み、注意を引いてから自爆する。'),
        'EN': ('Doll Drone Carrier', 'Calls a carrier whose drones carry a singing, dancing doll into the enemy, draw their fire, and blow up.'),
    },
    'heli': {
        'SC': ('武装直升机', '呼叫武装直升机，攻击附近的敌人。'),
        'CN': ('武裝直升機', '呼叫武裝直升機，攻擊附近的敵人。'),
        'JA': ('武装ヘリ', '武装ヘリを要請する。付近の敵を攻撃する。'),
        'EN': ('Gunships', 'Calls gunships that attack nearby enemies.'),
    },
}
MODES: dict[str, tuple[tuple[str, str], tuple[str, str]]] = {  # (hold, follow): (name suffix, sentence)
    'SC': (('·守点', '守在标记的地点上空。'), ('·跟随', '跟随呼叫者行动。')),
    'CN': (('·守點', '守在標記的地點上空。'), ('·跟隨', '跟隨呼叫者行動。')),
    'JA': (('（拠点）', 'マーカーで指定した地点の上空を守る。'), ('（随伴）', '要請した隊員に随伴する。')),
    'EN': ((' (Hold)', 'They hold the marked point.'), (' (Escort)', 'They escort the caller.')),
}
NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件；未安装时为普通 KM6 轰炸。',
    'CN': '需要 EDF6VehicleCrew 插件；未安裝時為普通 KM6 轟炸。',
    'JA': 'EDF6VehicleCrew プラグインが必要。未導入時は通常の KM6 による爆撃になる。',
    'EN': 'Needs the EDF6VehicleCrew plugin; without it this is a plain KM6 bomber call.',
}


VEHICLE_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件和 tools/make_jets.py 写入的 EDF6VC_PJET_*.SGO。',
    'CN': '需要 EDF6VehicleCrew 插件和 tools/make_jets.py 寫入的 EDF6VC_PJET_*.SGO。',
    'JA': 'EDF6VehicleCrew プラグインと tools/make_jets.py が書き出す EDF6VC_PJET_*.SGO が必要。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the EDF6VC_PJET_*.SGO files tools/make_jets.py writes.',
}



RETIRED_NOTE: dict[str, tuple[str, str]] = {   # an uninstalled call's row: (name suffix, description)
    'SC': ('（已卸载）', 'EDF6VehicleCrew 已卸载。本行只为保住武器表的行号（存档按行号记武器），效果与原版的{stock}相同。'),
    'CN': ('（已解除安裝）', 'EDF6VehicleCrew 已解除安裝。本行只為保住武器表的行號（存檔按行號記武器），效果與原版的{stock}相同。'),
    'JA': ('（アンインストール済み）', 'EDF6VehicleCrew はアンインストール済み。武器表の行番号を保つための行（セーブは行番号で'
                              '武器を記録する）で、効果は原版の{stock}と同じ。'),
    'EN': (' (uninstalled)', "EDF6VehicleCrew is uninstalled. This row only keeps the weapon table's row numbers (saves "
                             'record weapons by row); it works as the stock {stock}.'),
}


def _lang(lang: str) -> str:
    return 'EN' if lang == 'KR' else lang


def call_name(call: Call, lang: str) -> str:
    lang = _lang(lang)
    name = KINDS[call.kind][lang][0]
    return name + MODES[lang][call.follow][0] if call.modal else name


def call_description(call: Call, lang: str) -> str:
    lang = _lang(lang)
    if call.brings == 'vehicle':
        return KINDS[call.kind][lang][1] + '\n\n' + VEHICLE_NOTES[lang]
    if not call.modal:
        return KINDS[call.kind][lang][1] + '\n\n' + NOTES[lang]
    sep = ' ' if lang == 'EN' else ''
    return KINDS[call.kind][lang][1] + sep + MODES[lang][call.follow][1] + '\n\n' + NOTES[lang]


def retired_name(call: Call, lang: str) -> str:
    return call_name(call, lang) + RETIRED_NOTE[_lang(lang)][0]


def retired_description(lang: str, stock_name: str) -> str:
    """`stock_name`: the template's own name in that language (the stock weapon the row now is)."""
    return RETIRED_NOTE[_lang(lang)][1].format(stock=stock_name)
