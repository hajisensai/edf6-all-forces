"""Custom mech loadouts (docs/feature-2026-11-21-mech-custom.md; the player, 2026-11-21: "机甲弄成自定义化的 四个武器槽位+
一个机体槽位+加一个核心"): the parts and the rules, the one Python source.

  SupportVehicle_OWN_MECH=<request>,<frame>,<right arm>,<left arm>,<right shoulder>,<left shoulder>,<core>
  SupportVehicle_OWN_MECH=EWEAPON378,NIX_RED,EMLASER,EMLASER,PBEAM,MIRAGE,-

  request   one of REQUESTS: the stock player mech request this loadout replaces (only a call of that very request gets
            it); its armour, movement, aim, tail and level stay the request's
  frame     one of FRAMES (the model), - the request's own
  weapons   four slots, - empty (at least one weapon): the right / left arm a hand-held (row 0 / 1) or forearm weapon
            (row 2 / 3), the right / left shoulder row 4 / 5 (every stock request uses exactly these six rows, one arm
            weapon a side)
  core      one of CORES (the request's thruster block [2]), - the request's own
  A weapon or core never above the request's level (the lowest level of the stock requests carrying it).

The tables were read from Root.cpk (every request of WEAPONTABLE category 309 whose vehicle is a 504 / 612 / 608 mech,
their weapon SGOs' and WEAPONTEXT's four-language names); tests/mech_parts_test.py compares them with Root.cpk when the
game is there. tools/gen_mech_parts.py writes src/mech_parts.inc (the C++ tables) from this module; src/mech_loadout.h
and src/support_loadout.h hold the same rules (tests/mech_parts_test.py runs both on the same values).

The generated file EDF6VC_LO_MECH_<variant>.SGO (src/support_loadout.h VehicleVariantFile): the frame's SGO with the
request's setup block as its vehicle_setup and mission_setup ([2] the core's, [4] the eight weapon rows), and a resource
list of its weapons.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import dsgo


@dataclass(frozen=True)
class Frame:
    token: str
    file: str                       # Root.cpk OBJECT/<file>
    labels: tuple[str, ...]         # en, zh-CN, zh-TW, ja


@dataclass(frozen=True)
class Weapon:
    token: str
    labels: tuple[str, ...]
    slot: str                       # 'hand' (rows 0 / 1), 'arm' (rows 2 / 3), 'shoulder' (rows 4 / 5)
    stem: str                       # WEAPON/<stem>[_r|_l].sgo
    single: bool = False            # one file for both sides (no _r / _l)
    right: bool = True
    left: bool = True
    level: float = 0.0              # the lowest level of the stock requests carrying it
    params_r: list = field(default_factory=list)   # its recoil pair on that side, from that request
    params_l: list = field(default_factory=list)


@dataclass(frozen=True)
class Core:
    token: str                      # the request it is taken from (the lowest level carrying this thruster block)
    labels: tuple[str, ...]
    level: float
    thrust: list                    # setup [2]


@dataclass(frozen=True)
class Request:
    token: str
    file: str                       # Root.cpk WEAPON/<file>
    labels: tuple[str, ...]
    level: float                    # WEAPONTABLE column 4
    vehicle: str                    # the frame file its container makes
    frame: str
    slots: tuple[str, ...]          # its stock weapons, SLOTS order ('-' none)
    core: str
    armor: list
    move: list
    aim: list
    tail: list


FRAMES = (
    Frame('BEGARUTA', 'V504_BEGARUTA.SGO', ('Nix (standard)', '尼克斯（原色）', '尼克斯（原色）', 'ニクス（標準）')),
    Frame('BEGARUTA_BLUE', 'V504_BEGARUTA_BLUE.SGO', ('Nix (blue)', '尼克斯（蓝）', '尼克斯（藍）', 'ニクス（青）')),
    Frame('BEGARUTA_RED', 'V504_BEGARUTA_RED.SGO', ('Nix (red)', '尼克斯（红）', '尼克斯（紅）', 'ニクス（赤）')),
    Frame('BEGARUTA_GOLD', 'V504_BEGARUTA_GOLD.SGO', ('Nix (metal coat)', '尼克斯（金属涂层）', '尼克斯（金屬塗層）', 'ニクス（メタルコート）')),
    Frame('NIX', 'V612_NIX.SGO', ('Eiren (standard)', '艾伦（原色）', '艾倫（原色）', 'エイレン（標準）')),
    Frame('NIX_BLACK', 'V612_NIX_BLACK.SGO', ('Eiren (black)', '艾伦（黑）', '艾倫（黑）', 'エイレン（黒）')),
    Frame('NIX_RED', 'V612_NIX_RED.SGO', ('Eiren (red)', '艾伦（红）', '艾倫（紅）', 'エイレン（赤）')),
    Frame('OLDROBOT', 'V608_OLDROBOT.SGO', ('Gravis', '古拉比斯', '古拉比斯', 'グラビス')),
)
WEAPONS = (
    Weapon('MACHINEGUN', ('Machine Gun', '机枪', '機鎗', 'マシンガン'), 'hand', 'v_504_h_machinegun01', single=True, right=True, left=True, level=0.100000001, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('FLAME', ('Combat Burner', '战斗火枪', '戰鬥火鎗', 'コンバットバーナー'), 'hand', 'v_504begaruta_flamethrower01', single=True, right=True, left=True, level=0.100000001, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('SABER', ('Power Saber', '动力光刃', '動力光刃', 'パワーセイバー'), 'hand', 'v_504_h_dlc_laserblade', single=True, right=True, left=True, level=4.25, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('ROCKETCANNON', ('Rocket Cannon', '火箭炮', '火箭砲', 'ロケット砲'), 'arm', 'v_504begaruta_cannon01', single=False, right=True, left=True, level=0.100000001, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('HEAVYREVCANNON', ('Heavy Revolver Cannon', '重型转轮式加农炮', '重型轉輪式加農砲', 'ヘビーリボルバーカノン'), 'arm', 'v_504begaruta_cannon02solid', single=False, right=True, left=True, level=0.810000002, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('HEAVYREVOLVER', ('Heavy Revolver', '重型转轮枪', '重型轉輪鎗', 'ヘビーリボルバー'), 'arm', 'v_504begaruta_cannon02solid_dlc', single=False, right=True, left=True, level=4.0, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('REVOLVER', ('Revolver Cannon', '转轮式加农炮', '轉輪式加農砲', 'リボルバーカノン'), 'arm', 'v_504begaruta_gatling01', single=False, right=True, left=True, level=0.649999976, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('REVOLVER_RS', ('New Revolver Cannon', '转轮式加农炮ＲＳ', '轉輪式加農砲ＲＳ', 'リボルバーカノンＲＳ'), 'arm', 'v_504begaruta_gatling02', single=False, right=True, left=True, level=2.52999997, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('GRENADE', ('Large Grenade Launcher', '大型榴弹发射器', '大型榴彈發射器', '大型グレネードランチャー'), 'arm', 'v_504begaruta_grenade01', single=False, right=True, left=True, level=0.200000003, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('REVROCKET', ('Revolver Rocket Cannon', '转轮式火箭加农炮', '轉輪式火箭加農砲', 'リボルバーロケットカノン'), 'arm', 'v_504begaruta_rocket01', single=False, right=True, left=True, level=2.0999999, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('EMLASER', ('Electromagnetic Laser Cannon', '电磁激光炮', '電磁雷射砲', '電磁レーザー砲'), 'arm', 'v_612_a_laserrifle', single=False, right=True, left=True, level=0.75, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('HYPERLASER', ('Hyper Laser Cannon', '终极激光炮', '終極雷射砲', 'ハイパーレーザー砲'), 'arm', 'v_612_a_laserrifle_dlc2', single=False, right=True, left=True, level=4.5, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('MISSILE', ('Missile Pod', '飞弹发射架', '飛彈發射架', 'ミサイルポッド'), 'shoulder', 'v_504begaruta_missile01', single=False, right=True, left=True, level=0.649999976, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('HOWITZER', ('Shoulder Howitzer', '肩射榴弹炮', '肩射榴彈砲', 'ショルダーハウィツァー'), 'shoulder', 'v_504begaruta_longcannon01', single=False, right=True, left=True, level=0.810000002, params_r=[25.0, 0.0250000004], params_l=[25.0, 0.0250000004]),
    Weapon('HOWITZER_S', ('Shoulder Howitzer S', '肩射榴弹炮Ｓ', '肩射榴彈砲Ｓ', 'ショルダーハウィツァーＳ'), 'shoulder', 'v_504begaruta_longcannon015', single=False, right=True, left=True, level=3.5, params_r=[20.0, 0.0250000004], params_l=[20.0, 0.0250000004]),
    Weapon('DISPERSAL', ('Dispersal Howitzer', '散射榴弹炮', '散射榴彈砲', '拡散榴弾砲'), 'shoulder', 'v_504begaruta_longcannon02', single=False, right=True, left=True, level=2.0999999, params_r=[15.0, 0.00600000005], params_l=[15.0, 0.00600000005]),
    Weapon('CANISTER', ('Canister Shot', '散弹炮', '散彈砲', '散弾砲'), 'shoulder', 'v_504begaruta_longspreadcannon01', single=False, right=True, left=True, level=0.419999987, params_r=[25.0, 0.0250000004], params_l=[25.0, 0.0250000004]),
    Weapon('EXPLOSION', ('Explosion', '炸裂炮', '炸裂砲', 'エクスプロージョン'), 'shoulder', 'v_504begaruta_spreadgrenade01', single=False, right=True, left=True, level=0.200000003, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('GRENADEPOD', ('Grenade Pod', '榴弹发射架', '榴彈發射架', 'グレネードポッド'), 'shoulder', 'v_504begaruta_grenade_pod01', single=False, right=True, left=True, level=0.100000001, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('GRENADEPOD_DLC', ('Grenade Pod', '榴弹荚舱', '榴彈莢艙', 'グレネードポッド'), 'shoulder', 'v_504begaruta_grenade_pod01_dlc', single=False, right=True, left=True, level=4.0, params_r=[2.0, 0.00600000005], params_l=[2.0, 0.00600000005]),
    Weapon('POWERDYNE_MC', ('Power Dyne MC', '动力达因ＭＣ', '動力達因ＭＣ', 'パワーダインＭＣ'), 'shoulder', 'v_504begaruta_dlc_longcannon015', single=False, right=True, left=True, level=4.25, params_r=[2.0, 0.00600000005], params_l=[2.0, 0.00600000005]),
    Weapon('POWERDYNE_GC', ('Powerdyne GC', '动力达因ＧＣ', '動力達因ＧＣ', 'パワーダインＧＣ'), 'shoulder', 'v_504begaruta_dlc2_longcannon015', single=False, right=False, left=True, level=4.5, params_r=[10.0, 0.100000001], params_l=[10.0, 0.100000001]),
    Weapon('PBEAM', ('Particle Beam Cannon', '粒子光束加农炮', '粒子光束加農砲', '粒子ビームキャノン砲'), 'shoulder', 'v_612_laser_cannon01', single=False, right=True, left=False, level=2.75, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
    Weapon('MIRAGE', ('Mirage Pod', '幻象荚舱', '幻象莢艙', 'ミラージュポッド'), 'shoulder', 'v_612_homing_laser01', single=False, right=False, left=True, level=1.75, params_r=[0.0, 0.0], params_l=[0.0, 0.0]),
)
CORES = (
    Core('DLC_VEHICLE_V504_BEGARUTA_GOLD', ('Powered Exoskeleton Nix Metal Coat', '战斗机体 尼克斯 金属涂层', '戰鬥機體 尼克斯 金屬塗層', 'コンバットフレーム ニクス メタルコート'), 0.100000001, [20.0, 1.5, 45.0, 240.0, 0.280000001, 50.0]),
    Core('EWEAPON359', ('Nix Red Coat', '尼克斯 红色号', '尼克斯 紅色號', 'ニクス レッドボディ'), 0.419999987, [20.0, 3.0, 50.0, 240.0, 0.540000021, 70.0]),
    Core('EWEAPON361', ('Powered Exoskeleton Eiren', '战斗机体 艾伦', '戰鬥機體 艾倫', 'コンバットフレーム エイレン'), 0.75, [20.0, 2.5, 65.0, 240.0, 0.280000001, 50.0]),
    Core('EWEAPON362', ('Nix Battle Cannon', '尼克斯 战斗加农炮机体', '尼克斯 戰鬥加農砲機體', 'ニクス バトルキャノン'), 0.810000002, [10.0, 1.0, 25.0, 480.0, 0.340000004, 30.0]),
    Core('EWEAPON364', ('Nix Missile Gun', '尼克斯 炮弹枪机体', '尼克斯 砲彈鎗機體', 'ニクス ミサイルガン'), 1.39999998, [15.0, 1.5, 30.0, 240.0, 0.280000001, 50.0]),
    Core('EWEAPON365', ('Powered Exoskeleton Nix C2', '战斗机体 尼克斯Ｃ２', '戰鬥機體 尼克斯Ｃ２', 'コンバットフレーム ニクスＣ２'), 1.63, [20.0, 1.5, 45.0, 420.0, 0.349999994, 50.0]),
    Core('EWEAPON368', ('Nix Red Shadow', '尼克斯 赤影号', '尼克斯 赤影號', 'ニクス レッドシャドゥ'), 1.79999995, [20.0, 3.0, 60.0, 240.0, 0.540000021, 60.0]),
    Core('EWEAPON369', ('Nix Buster Cannon', '尼克斯 毁灭加农炮机体', '尼克斯 毀滅加農砲機體', 'ニクス バスターキャノン'), 2.0999999, [10.0, 1.0, 25.0, 580.0, 0.340000004, 40.0]),
    Core('EWEAPON370', ('Powered Exoskeleton Nix C3', '战斗机体 尼克斯Ｃ３', '戰鬥機體 尼克斯Ｃ３', 'コンバットフレーム ニクスＣ３'), 2.29999995, [20.0, 1.5, 45.0, 420.0, 0.419999987, 50.0]),
    Core('EWEAPON372', ('Nix Red Armor', '尼克斯 红装甲机体', '尼克斯 紅裝甲機體', 'ニクス レッドアーマー'), 2.75, [30.0, 3.0, 60.0, 240.0, 0.540000021, 80.0]),
    Core('EWEAPON374', ('Nix Destroy Cannon', '尼克斯 歼灭加农炮机体', '尼克斯 殲滅加農砲機體', 'ニクス デストロイキャノン'), 3.00999999, [10.0, 1.0, 25.0, 760.0, 0.340000004, 40.0]),
    Core('EWEAPON375', ('Nix Red Guard', '尼克斯 红防护机体', '尼克斯 紅防護機體', 'ニクス レッドガード'), 3.20000005, [40.0, 3.0, 60.0, 240.0, 0.540000021, 80.0]),
    Core('EWEAPON376', ('Nix Grenadier EZ', '尼克斯 掷弹机体ＥＺ', '尼克斯 擲彈機體ＥＺ', 'ニクス グレネーダーＥＺ'), 3.3499999, [20.0, 1.5, 45.0, 540.0, 0.419999987, 50.0]),
    Core('MPACK_A_WEAPON075', ('Powered Exoskeleton Gravis', '战斗机体 古拉比斯', '戰鬥機體 古拉比斯', 'コンバットフレーム グラビス'), 4.0, [20.0, 1.25, 45.0, 760.0, 0.340000004, 50.0]),
    Core('MPACK_A_WEAPON076', ('Saber Eiren', '光刃·艾伦', '光刃‧艾倫', 'セイバー·エイレン'), 4.25, [40.0, 3.0, 60.0, 240.0, 0.340000004, 80.0]),
)
REQUESTS = (
    Request('DLC_VEHICLE_V504_BEGARUTA_GOLD', 'DLC_VEHICLE_V504_BEGARUTA_GOLD.SGO', ('Powered Exoskeleton Nix Metal Coat', '战斗机体 尼克斯 金属涂层', '戰鬥機體 尼克斯 金屬塗層', 'コンバットフレーム ニクス メタルコート'), 0.100000001, 'V504_BEGARUTA_GOLD.SGO', 'BEGARUTA_GOLD', ('FLAME', 'FLAME', 'GRENADEPOD', 'GRENADEPOD'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [1.0, 1.20000005], [5.5, 0.00499999989, 20.0, 0.0599999987], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON356', 'EWEAPON356.SGO', ('Powered Exoskeleton Nix A', '战斗机体 尼克斯Ａ', '戰鬥機體 尼克斯Ａ', 'コンバットフレーム ニクスＡ'), 0.100000001, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('MACHINEGUN', 'ROCKETCANNON', '-', '-'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [0.800000012, 1.20000005], [5.5, 0.00499999989, 20.0, 0.0599999987], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON357', 'EWEAPON357.SGO', ('Nix Grenadier', '尼克斯 掷弹机体', '尼克斯 擲彈機體', 'ニクス グレネーダー'), 0.200000003, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('GRENADE', '-', '-', 'EXPLOSION'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [1.14999998, 1.39999998], [5.5, 0.00499999989, 20.0, 0.0599999987], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON358', 'EWEAPON358.SGO', ('Powered Exoskeleton Nix A2', '战斗机体 尼克斯Ａ２', '戰鬥機體 尼克斯Ａ２', 'コンバットフレーム ニクスＡ２'), 0.300000012, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('MACHINEGUN', 'ROCKETCANNON', '-', '-'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [1.20000005, 1.45000005], [5.5, 0.00499999989, 20.0, 0.0599999987], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON359', 'EWEAPON359.SGO', ('Nix Red Coat', '尼克斯 红色号', '尼克斯 紅色號', 'ニクス レッドボディ'), 0.419999987, 'V504_BEGARUTA_RED.SGO', 'BEGARUTA_RED', ('FLAME', 'MACHINEGUN', 'CANISTER', 'CANISTER'), 'EWEAPON359',
            [1.29999995, 1.54999995], [6.0, 0.0149999997, 20.0, 0.0599999987], [[60.0, 0.100000001, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON360', 'EWEAPON360.SGO', ('Powered Exoskeleton Nix B', '战斗机体 尼克斯Ｂ', '戰鬥機體 尼克斯Ｂ', 'コンバットフレーム ニクスＢ'), 0.649999976, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('REVOLVER', 'REVOLVER', 'MISSILE', 'MISSILE'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [1.54999995, 1.20000005], [5.5, 0.00499999989, 20.0, 0.0599999987], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON361', 'EWEAPON361.SGO', ('Powered Exoskeleton Eiren', '战斗机体 艾伦', '戰鬥機體 艾倫', 'コンバットフレーム エイレン'), 0.75, 'V612_NIX.SGO', 'NIX', ('EMLASER', 'EMLASER', '-', '-'), 'EWEAPON361',
            [1.70000005, 2.0], [7.5, 0.00499999989, 20.0, 0.119999997], [[15.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON362', 'EWEAPON362.SGO', ('Nix Battle Cannon', '尼克斯 战斗加农炮机体', '尼克斯 戰鬥加農砲機體', 'ニクス バトルキャノン'), 0.810000002, 'V504_BEGARUTA.SGO', 'BEGARUTA', ('HEAVYREVCANNON', 'HEAVYREVCANNON', 'HOWITZER', 'HOWITZER'), 'EWEAPON362',
            [1.85000002, 1.89999998], [4.0, 0.00499999989, 10.0, 0.00999999978], [[10.0, 0.00999999978, 0.0199999996]], [0.0500000007, 0.200000003]),
    Request('EWEAPON363', 'EWEAPON363.SGO', ('Powered Exoskeleton Nix C1', '战斗机体 尼克斯Ｃ１', '戰鬥機體 尼克斯Ｃ１', 'コンバットフレーム ニクスＣ１'), 1.10000002, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('REVOLVER', 'ROCKETCANNON', 'MISSILE', '-'), 'DLC_VEHICLE_V504_BEGARUTA_GOLD',
            [3.29999995, 3.29999995], [6.5, 0.00499999989, 25.0, 0.0599999987], [[30.0, 0.0500000007, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON364', 'EWEAPON364.SGO', ('Nix Missile Gun', '尼克斯 炮弹枪机体', '尼克斯 砲彈鎗機體', 'ニクス ミサイルガン'), 1.39999998, 'V504_BEGARUTA.SGO', 'BEGARUTA', ('REVOLVER', 'REVOLVER', 'MISSILE', 'MISSILE'), 'EWEAPON364',
            [6.19999981, 4.30000019], [5.5, 0.00499999989, 20.0, 0.0599999987], [[45.0, 0.100000001, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON365', 'EWEAPON365.SGO', ('Powered Exoskeleton Nix C2', '战斗机体 尼克斯Ｃ２', '戰鬥機體 尼克斯Ｃ２', 'コンバットフレーム ニクスＣ２'), 1.63, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('REVOLVER', 'ROCKETCANNON', 'MISSILE', '-'), 'EWEAPON365',
            [8.60000038, 5.30000019], [6.5, 0.00499999989, 25.0, 0.0599999987], [[30.0, 0.0500000007, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON366', 'EWEAPON366.SGO', ('Nix Grenadier M2', '尼克斯 掷弹机体Ｍ２', '尼克斯 擲彈機體Ｍ２', 'ニクス グレネーダーＭ２'), 1.75, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('GRENADE', 'MACHINEGUN', 'EXPLOSION', 'EXPLOSION'), 'EWEAPON365',
            [10.3000002, 5.30000019], [6.5, 0.00499999989, 25.0, 0.0599999987], [[30.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON367', 'EWEAPON367.SGO', ('Powered Exoskeleton Eiren II', '战斗机体 艾伦Ⅱ', '戰鬥機體 艾倫Ⅱ', 'コンバットフレーム エイレンⅡ'), 1.75, 'V612_NIX.SGO', 'NIX', ('EMLASER', 'EMLASER', '-', 'MIRAGE'), 'EWEAPON361',
            [12.0, 7.0], [10.0, 0.00499999989, 35.0, 0.119999997], [[20.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON368', 'EWEAPON368.SGO', ('Nix Red Shadow', '尼克斯 赤影号', '尼克斯 赤影號', 'ニクス レッドシャドゥ'), 1.79999995, 'V504_BEGARUTA_RED.SGO', 'BEGARUTA_RED', ('FLAME', 'FLAME', 'CANISTER', 'CANISTER'), 'EWEAPON368',
            [11.0, 6.0], [7.5, 0.0149999997, 30.0, 0.0599999987], [[70.0, 0.100000001, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON369', 'EWEAPON369.SGO', ('Nix Buster Cannon', '尼克斯 毁灭加农炮机体', '尼克斯 毀滅加農砲機體', 'ニクス バスターキャノン'), 2.0999999, 'V504_BEGARUTA.SGO', 'BEGARUTA', ('REVROCKET', 'REVROCKET', 'DISPERSAL', 'DISPERSAL'), 'EWEAPON369',
            [16.7999992, 8.0], [4.5, 0.00499999989, 10.0, 0.00999999978], [[25.0, 0.00999999978, 0.0199999996]], [0.0500000007, 0.200000003]),
    Request('EWEAPON370', 'EWEAPON370.SGO', ('Powered Exoskeleton Nix C3', '战斗机体 尼克斯Ｃ３', '戰鬥機體 尼克斯Ｃ３', 'コンバットフレーム ニクスＣ３'), 2.29999995, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('REVOLVER', 'FLAME', 'HOWITZER', 'MISSILE'), 'EWEAPON370',
            [23.0, 10.0], [6.5, 0.00499999989, 25.0, 0.0599999987], [[40.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON371', 'EWEAPON371.SGO', ('Nix Revolver Custom', '尼克斯 转轮式特制机体', '尼克斯 轉輪式特製機體', 'ニクス リボルバーカスタム'), 2.52999997, 'V504_BEGARUTA.SGO', 'BEGARUTA', ('REVOLVER_RS', 'REVOLVER_RS', 'MISSILE', 'MISSILE'), 'EWEAPON364',
            [30.2999992, 12.0], [5.5, 0.00499999989, 20.0, 0.0599999987], [[70.0, 0.100000001, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON372', 'EWEAPON372.SGO', ('Nix Red Armor', '尼克斯 红装甲机体', '尼克斯 紅裝甲機體', 'ニクス レッドアーマー'), 2.75, 'V504_BEGARUTA_RED.SGO', 'BEGARUTA_RED', ('FLAME', 'MACHINEGUN', 'CANISTER', 'CANISTER'), 'EWEAPON372',
            [32.0, 15.0], [7.5, 0.0149999997, 35.0, 0.0599999987], [[70.0, 0.100000001, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON373', 'EWEAPON373.SGO', ('Powered Exoskeleton Eiren III', '战斗机体 艾伦Ⅲ', '戰鬥機體 艾倫Ⅲ', 'コンバットフレーム エイレンⅢ'), 2.75, 'V612_NIX.SGO', 'NIX', ('EMLASER', 'EMLASER', 'PBEAM', '-'), 'EWEAPON361',
            [30.0, 15.0], [12.5, 0.00499999989, 40.0, 0.119999997], [[25.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON374', 'EWEAPON374.SGO', ('Nix Destroy Cannon', '尼克斯 歼灭加农炮机体', '尼克斯 殲滅加農砲機體', 'ニクス デストロイキャノン'), 3.00999999, 'V504_BEGARUTA.SGO', 'BEGARUTA', ('REVROCKET', 'REVROCKET', 'DISPERSAL', 'DISPERSAL'), 'EWEAPON374',
            [63.0, 21.0], [5.5, 0.00499999989, 10.0, 0.00999999978], [[45.0, 0.00999999978, 0.0199999996]], [0.0500000007, 0.200000003]),
    Request('EWEAPON375', 'EWEAPON375.SGO', ('Nix Red Guard', '尼克斯 红防护机体', '尼克斯 紅防護機體', 'ニクス レッドガード'), 3.20000005, 'V504_BEGARUTA_RED.SGO', 'BEGARUTA_RED', ('FLAME', 'FLAME', 'CANISTER', 'CANISTER'), 'EWEAPON375',
            [64.0, 25.0], [9.0, 0.0149999997, 45.0, 0.0599999987], [[90.0, 0.100000001, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('EWEAPON376', 'EWEAPON376.SGO', ('Nix Grenadier EZ', '尼克斯 掷弹机体ＥＺ', '尼克斯 擲彈機體ＥＺ', 'ニクス グレネーダーＥＺ'), 3.3499999, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('GRENADE', 'GRENADE', 'EXPLOSION', 'EXPLOSION'), 'EWEAPON376',
            [83.0, 25.0], [7.5, 0.00499999989, 35.0, 0.0599999987], [[60.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON377', 'EWEAPON377.SGO', ('Powered Exoskeleton Nix ZC', '战斗机体 尼克斯ＺＣ', '戰鬥機體 尼克斯ＺＣ', 'コンバットフレーム ニクスＺＣ'), 3.5, 'V504_BEGARUTA_BLUE.SGO', 'BEGARUTA_BLUE', ('REVOLVER', 'FLAME', 'HOWITZER_S', 'MISSILE'), 'EWEAPON376',
            [87.0, 25.0], [7.5, 0.00499999989, 35.0, 0.0599999987], [[60.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('EWEAPON378', 'EWEAPON378.SGO', ('Powered Exoskeleton Eiren IV', '战斗机体 艾伦Ⅳ', '戰鬥機體 艾倫Ⅳ', 'コンバットフレーム エイレンⅣ'), 3.5, 'V612_NIX.SGO', 'NIX', ('EMLASER', 'EMLASER', 'PBEAM', 'MIRAGE'), 'EWEAPON361',
            [60.0, 25.0], [15.0, 0.00499999989, 60.0, 0.119999997], [[45.0, 0.0500000007, 0.100000001]], [0.0199999996, 0.100000001]),
    Request('MPACK_A_WEAPON075', 'MPACK_A_WEAPON075.SGO', ('Powered Exoskeleton Gravis', '战斗机体 古拉比斯', '戰鬥機體 古拉比斯', 'コンバットフレーム グラビス'), 4.0, 'V608_OLDROBOT.SGO', 'OLDROBOT', ('HEAVYREVOLVER', 'HEAVYREVOLVER', 'GRENADEPOD_DLC', 'GRENADEPOD_DLC'), 'MPACK_A_WEAPON075',
            [65.0, 25.0], [10.0, 0.00499999989, 20.0, 0.00999999978], [[80.0, 0.0199999996, 0.0199999996]], [0.0500000007, 0.200000003]),
    Request('MPACK_A_WEAPON076', 'MPACK_A_WEAPON076.SGO', ('Saber Eiren', '光刃·艾伦', '光刃‧艾倫', 'セイバー·エイレン'), 4.25, 'V612_NIX_BLACK.SGO', 'NIX_BLACK', ('SABER', 'SABER', 'POWERDYNE_MC', 'POWERDYNE_MC'), 'MPACK_A_WEAPON076',
            [65.0, 25.0], [12.0, 0.100000001, 60.0, 0.200000003], [[240.0, 0.100000001, 0.100000001]], [0.0500000007, 0.200000003]),
    Request('MPACK_B_WEAPON109', 'MPACK_B_WEAPON109.SGO', ('Eiren Assault', '艾伦·突击号', '艾倫‧突擊號', 'エイレン·アサルト'), 4.5, 'V612_NIX_RED.SGO', 'NIX_RED', ('HYPERLASER', 'HYPERLASER', 'EXPLOSION', 'POWERDYNE_GC'), 'EWEAPON375',
            [75.0, 25.0], [12.0, 0.0500000007, 65.0, 0.100000001], [[90.0, 0.100000001, 0.100000001]], [0.0199999996, 0.100000001]),
)

SLOTS = ('RIGHT_ARM', 'LEFT_ARM', 'RIGHT_SHOULDER', 'LEFT_SHOULDER')
SLOT_LABELS = (('Right arm', '右臂', '右臂', '右腕'), ('Left arm', '左臂', '左臂', '左腕'),
               ('Right shoulder', '右肩', '右肩', '右肩'), ('Left shoulder', '左肩', '左肩', '左肩'))
FIELDS = 7   # request, frame, four weapons, core
NONE = '-'
VEHICLE, MECH = 1 << 62, 1 << 61   # support_loadout.h kVariantVehicle, kVariantMech
ROWS = 8                           # the frames' vehicle_weapon_setting rows

FRAME_BY = {f.token: f for f in FRAMES}
WEAPON_BY = {w.token: w for w in WEAPONS}
CORE_BY = {c.token: c for c in CORES}
REQUEST_BY = {r.token: r for r in REQUESTS}


class Invalid(ValueError):
    """A value the plugin refuses: args (Chinese, English)."""


def f32(x: float) -> float:
    """The float32 the game holds for a table number (the tables print them to 9 digits)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def cents(level: float) -> int:
    """A level in hundredths (every stock level is a whole number of them): the comparisons both sides make."""
    return int(round(level * 100))


def fits(w: Weapon, slot: int) -> bool:
    """`w` goes into SLOTS[slot] (its mount and that side's file)."""
    side = w.right if slot % 2 == 0 else w.left
    return side and (w.slot in ('hand', 'arm') if slot < 2 else w.slot == 'shoulder')


def allowed(w: Weapon, slot: int, request: Request) -> bool:
    return fits(w, slot) and cents(w.level) <= cents(request.level)


def core_allowed(c: Core, request: Request) -> bool:
    return cents(c.level) <= cents(request.level)


def row_of(w: Weapon, slot: int) -> int:
    """Its row of the eight (a hand-held arm weapon 0 / 1, a forearm one 2 / 3, a shoulder 4 / 5)."""
    if slot < 2:
        return slot if w.slot == 'hand' else 2 + slot
    return 2 + slot


def weapon_path(w: Weapon, slot: int) -> str:
    side = 'r' if slot % 2 == 0 else 'l'
    return f'app:/weapon/{w.stem}.sgo' if w.single else f'app:/weapon/{w.stem}_{side}.sgo'


def weapon_params(w: Weapon, slot: int) -> list:
    return [f32(x) for x in (w.params_r if slot % 2 == 0 else w.params_l)]


@dataclass(frozen=True)
class Mech:
    """One loadout (src/mech_loadout.h MechLoadout): tokens, '' for '-' (frame / core: the request's own)."""
    request: str
    frame: str = ''
    weapons: tuple[str, ...] = ('', '', '', '')
    core: str = ''

    def frame_file(self) -> str:
        return FRAME_BY[self.frame or REQUEST_BY[self.request].frame].file

    def core_request(self) -> Request:
        return REQUEST_BY[self.core or REQUEST_BY[self.request].core]


def check(m: Mech) -> None:
    """Raises Invalid unless `m` is one the plugin accepts (support_loadout.h ParseVehicleLoadout's rules)."""
    req = REQUEST_BY.get(m.request)
    if req is None:
        raise Invalid(f'未知的机甲请求：{m.request}', f'not a mech request: {m.request}')
    if m.frame and m.frame not in FRAME_BY:
        raise Invalid(f'未知的机体：{m.frame}', f'not a frame: {m.frame}')
    if len(m.weapons) != len(SLOTS):
        raise Invalid('机甲有 4 个武器槽', 'a mech has 4 weapon slots')
    for slot, t in enumerate(m.weapons):
        if not t:
            continue
        w = WEAPON_BY.get(t)
        if w is None:
            raise Invalid(f'未知的机甲武器：{t}', f'not a mech weapon: {t}')
        if not fits(w, slot):
            raise Invalid(f'{SLOT_LABELS[slot][1]}装不了：{t}', f'the {SLOT_LABELS[slot][0].lower()} cannot take: {t}')
        if not allowed(w, slot, req):
            raise Invalid(f'武器等级高于底座请求：{t}', f'a weapon above the request\'s level: {t}')
    if not any(m.weapons):
        raise Invalid('至少装一件武器', 'at least one weapon')
    if m.core:
        c = CORE_BY.get(m.core)
        if c is None:
            raise Invalid(f'未知的核心：{m.core}', f'not a core: {m.core}')
        if not core_allowed(c, req):
            raise Invalid(f'核心等级高于底座请求：{m.core}', f'a core above the request\'s level: {m.core}')


def parse(tokens: list[str]) -> Mech:
    """A SupportVehicle_OWN_MECH value's tokens (support_loadout.py _tokens) into a Mech; Invalid when the plugin would
    refuse it."""
    if len(tokens) != FIELDS:
        raise Invalid('机甲写成 底座请求,机体,右臂,左臂,右肩,左肩,核心（7 项，空槽写 -）',
                      'a mech is REQUEST,FRAME,RIGHT_ARM,LEFT_ARM,RIGHT_SHOULDER,LEFT_SHOULDER,CORE (7 items, - empty)')
    t = ['' if x.strip() == NONE else x.strip().upper() for x in tokens]
    if not t[0]:
        raise Invalid('机甲的第一项是底座请求', 'a mech\'s first item is its request')
    m = Mech(t[0], t[1], tuple(t[2:6]), t[6])
    check(m)
    return m


def format_mech(m: Mech) -> str:
    return ','.join(x or NONE for x in (m.request, m.frame, *m.weapons, m.core))


def _index(table: tuple, token: str) -> int:
    """1-based, 0 for ''."""
    return 0 if not token else [x.token for x in table].index(token) + 1


def variant(m: Mech) -> int:
    """support_loadout.h LoadoutVariant of a mech: bit 62 vehicle, 61 mech; bits 0-5 the request, 6-9 the frame, 10-13 the
    core, 14-37 the four weapons, 6 bits each (1-based indexes, 0 = none / the request's own)."""
    v = VEHICLE | MECH | _index(REQUESTS, m.request) | _index(FRAMES, m.frame) << 6 | _index(CORES, m.core) << 10
    for slot, t in enumerate(m.weapons):
        v |= _index(WEAPONS, t) << (14 + 6 * slot)
    return v


def of_variant(v: int) -> Mech | None:
    """support_loadout.h LoadoutOfVariant of a mech: None when it does not decode (the parser's rules)."""
    bits = v & ~(1 << 63)
    flags = VEHICLE | MECH
    if bits & flags != flags or (bits & ~flags) >> 38:   # src/mech_loadout.h kMechVariantBits
        return None

    def pick(table: tuple, i: int) -> str | None:
        return '' if not i else table[i - 1].token if i <= len(table) else None

    req = pick(REQUESTS, bits & 63)
    frame, core = pick(FRAMES, bits >> 6 & 15), pick(CORES, bits >> 10 & 15)
    weapons = tuple(pick(WEAPONS, bits >> (14 + 6 * s) & 63) for s in range(len(SLOTS)))
    if not req or frame is None or core is None or None in weapons:
        return None
    m = Mech(req, frame, weapons, core)
    try:
        check(m)
    except Invalid:
        return None
    return m


# ------------------------------------------------------------------------------------------------ the file


def _setup(game, file: str) -> dsgo.Node:  # noqa: ANN001 - rootcpk.Game
    """A stock request's setup block (Ammo_CustomParameter[4][3]): six entries."""
    setup = dsgo.parse(game.read('WEAPON', file)).root.get('Ammo_CustomParameter').items[4].items[3]
    if not isinstance(setup, dsgo.Node) or len(setup.items) != 6:
        raise ValueError(f'{file} 的设定块不是原版机甲请求的 6 项（被别的 MOD 改过？）')
    return setup


def weapons_of(m: Mech) -> list[tuple[int, str, list]]:
    """(row, weapon path, recoil pair) of each filled slot."""
    out = []
    for slot, t in enumerate(m.weapons):
        if t:
            w = WEAPON_BY[t]
            out.append((row_of(w, slot), weapon_path(w, slot), weapon_params(w, slot)))
    return out


def setup_of(game, m: Mech) -> list:  # noqa: ANN001
    """The setup block the mech is made with (plain values): the request's, [2] its core's, [4] its eight weapon rows."""
    base = dsgo.to_py(_setup(game, REQUEST_BY[m.request].file))
    core = dsgo.to_py(_setup(game, m.core_request().file))[2]
    rows: list = [[0.0] for _ in range(ROWS)]
    for row, path, params in weapons_of(m):
        rows[row] = [path, list(params)]
    return [base[0], base[1], core, base[3], rows, base[5]]


def _dsgo(v: object) -> object:
    return dsgo.Node([_dsgo(c) for c in v]) if isinstance(v, list) else v


def _sgo(v: object) -> object:
    import sgo
    if isinstance(v, list):
        return [_sgo(c) for c in v]
    return sgo.Float(struct.pack('<f', v)) if isinstance(v, float) else v


def mech_sgo(game, m: Mech) -> bytes:  # noqa: ANN001
    """EDF6VC_LO_MECH_<variant>.SGO: the frame (SGO or DSGO, written back in its own format) with the mech's setup
    (vehicle_setup and mission_setup, which the summon applies: src/player_vehicle.cpp) and its weapons' preload list."""
    check(m)
    data = game.read('OBJECT', m.frame_file())
    setup, resource = setup_of(game, m), [path for _, path, _ in weapons_of(m)]
    if data[:4] == b'DSGO':
        doc = dsgo.parse(data)
        rows = doc.root.get('vehicle_weapon_setting')
        if not isinstance(rows, dsgo.Node) or len(rows.items) != ROWS:
            raise ValueError(f'{m.frame_file()} 不是 8 行挂点的机甲（被别的 MOD 改过？）')
        for key in ('vehicle_setup', 'mission_setup'):
            doc.root.set(key, _dsgo(setup))
        doc.root.set('resource', _dsgo(resource))
        return dsgo.write(doc)
    import sgo
    version, members = sgo.read(data)
    rows = members.get('vehicle_weapon_setting')
    if not isinstance(rows, list) or len(rows) != ROWS:
        raise ValueError(f'{m.frame_file()} 不是 8 行挂点的机甲（被别的 MOD 改过？）')
    for key in ('vehicle_setup', 'mission_setup'):
        members[key] = _sgo(setup)
    members['resource'] = list(resource)
    return sgo.write_depth_first(version, members)


def check_sgo(made: bytes, frame: bytes, m: Mech) -> None:
    """A mech file differs from its frame only in its setup (both members, equal) and its preload list."""
    import sgo
    a, b = sgo.load(data=made), sgo.load(data=frame)
    assert set(a) == set(b) | {'mission_setup', 'resource'}, 'the frame\'s members, the setup and the preloads'
    for key in b:
        if key != 'vehicle_setup':
            assert a[key] == b[key], key
    assert a['vehicle_setup'] == a['mission_setup'], 'one setup'
    rows = a['mission_setup'][4]
    assert len(rows) == ROWS
    want = {row: path for row, path, _ in weapons_of(m)}
    got = {i: r[0] for i, r in enumerate(rows) if isinstance(r, list) and r and isinstance(r[0], str)}
    assert got == want, (got, want)
    assert sorted(a['resource']) == sorted(want.values()), a['resource']
