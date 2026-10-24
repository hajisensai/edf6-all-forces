"""Build the M163 Vulcan / quad Vulcan / BMPT from authored geometry and the player's native V603 contracts.

Root.cpk is read-only. build(root, kind) returns complete {Mods-relative path: bytes};
--out writes only there. Installation/removal use the same ownership ledger as other vehicles.
The bodies are deliberately authored models, not renamed stock vehicle meshes or downloaded assets.
"""
from __future__ import annotations

import argparse
import copy
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent / 'pylib'), str(HERE)]

import cockpits
import dsgo
import ledger
import emplacement_model
import vcobjects as vc

# The fixed emplacements (pylib/emplacement_model.py: the Dreadnought's secondary turrets and the real guns on their
# bases).
EMPLACEMENTS = tuple(emplacement_model.KINDS)
# The kinds firing the M163's 20 mm HE round (its store's template, VULCAN_HE) from their own holders.
M61_ROUNDS = ('quadvulcan', 'ciws_phalanx', 'gdf005')   # the GDF-005's 35 mm too: that HE round's template
KINDS = tuple(vc.TRACKED)
# What the installer says it builds, each kind (tools/installer.py: its make_<kind> tool, in this order).
LABELS = {'vulcan': 'M163 火神炮车（HE / AP 双弹链）', 'bmpt': 'BMPT（双 30 mm 机炮与反坦克导弹）',
          'quadvulcan': '四联装火神防空车（四门 M61）',
          'emp_mid': '无畏舰副炮炮台·中型（双联速射炮）', 'emp_short': '无畏舰副炮炮台·短管（双联重炮）',
          'emp_long': '无畏舰副炮炮台·长管（双联远程炮）',
          'tunguska': '2S6 通古斯卡（双 30 mm 双管炮与 9M311 导弹）',
          'pzh2000': 'PzH 2000（155 mm 榴弹炮，炮塔可转）',
          'sturmtiger': '突击虎（380 mm 火箭臼炮）', 'ciws_phalanx': '密集阵近防炮（M61A1 六管 20 mm）', 'flak88': '88 毫米高射炮（Flak 18/36）', 'apocalypse': '天启坦克（双管重炮与防空导弹）', 'gdf005': 'GDF-005 双联 35 mm 高炮（空爆弹）', 'laser': '激光防空炮台', 'railgun': '电磁炮炮台', 'tos1': 'TOS-1 喷火兵（温压火箭齐射）', 'patriot': 'MIM-104 爱国者（PAC-2 齐射与 PAC-3 导弹）'
}
# The kinds on the M163's one M61 cradle: every gun holder hangs on its left slide (the quad's four guns lift together).
ONE_CRADLE = ('vulcan', 'quadvulcan') + EMPLACEMENTS
# The kinds with a procedural model when their War Thunder folder is missing (the quad Vulcan has only its M163's).
PROCEDURAL = ('vulcan', 'bmpt')
STOCK_GUNS = ('V603_FLAK_GUN01_L.SGO', 'V603_FLAK_GUN01_R.SGO')
STOCK_VEHICLE = 'V603_FLAK.SGO'
# 20 / 30 mm gameplay values. EDF updates at 60 Hz; these are gameplay weapons,
# not a claim that the simulation reproduces every real firing mode or vehicle characteristic.
GUNS = {
    'bmpt': {
        'AmmoCount': 425.0, 'AmmoDamage': 22.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 120.0,
        'AmmoSize': 0.15, 'AmmoHitSizeAdjust': 0.3, 'AmmoGravityFactor': 0.15,
        'AmmoExplosion': 1.2, 'AmmoIsPenetration': 0.0, 'AmmoOwnerMove': 1.0,
        'FireInterval': 12.0, 'FireRecoil': 0.03, 'FireAccuracy': 0.004,
        'LockonTargetType': 7302.0,
    },
    # The quad Vulcan's HE (its native pair, each holder a column of two M61s: FireCount rounds a shot, both columns on
    # the one trigger every frame): the M163's HE belt's numbers (gun_sgo), four guns' worth of rounds a frame.
    'quadvulcan': {
        'AmmoCount': 1500.0, 'AmmoDamage': 4.0, 'AmmoSpeed': 17.0, 'AmmoAlive': 120.0, 'AmmoSize': 0.15,
        'AmmoHitSizeAdjust': 0.2, 'AmmoGravityFactor': 0.15, 'AmmoExplosion': 0.6, 'AmmoIsPenetration': 0.0,
        'AmmoOwnerMove': 1.0, 'FireInterval': 1.0, 'FireCount': 2.0, 'FireBurstCount': 1.0, 'FireBurstInterval': 0.0,
        'FireAccuracy': 0.008, 'FireRecoil': 0.006, 'LockonTargetType': 7301.0,
    },
    # The emplacements' twin guns (each barrel at FireInterval, the pair in turn: vcobjects GroundVehicle.alternate),
    # firing the E551's shell (EMPLACEMENT_SHELL): the SecMid a quick medium gun, the SecShort a slow heavy one, the
    # SecLong a flat fast one that reaches far.
    'emp_mid': {
        'AmmoCount': 240.0, 'AmmoDamage': 120.0, 'AmmoSpeed': 9.0, 'AmmoAlive': 160.0, 'AmmoSize': 0.8,
        'AmmoHitSizeAdjust': 0.5, 'AmmoGravityFactor': 0.35, 'AmmoExplosion': 4.0, 'AmmoIsPenetration': 0.0,
        'AmmoOwnerMove': 0.0, 'FireInterval': 12.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.003, 'LockonTargetType': 7302.0,
    },
    'emp_short': {
        'AmmoCount': 60.0, 'AmmoDamage': 700.0, 'AmmoSpeed': 7.0, 'AmmoAlive': 300.0, 'AmmoSize': 1.4,
        'AmmoHitSizeAdjust': 0.8, 'AmmoGravityFactor': 0.8, 'AmmoExplosion': 11.0, 'AmmoIsPenetration': 0.0,
        'AmmoOwnerMove': 0.0, 'FireInterval': 60.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.004, 'LockonTargetType': 7302.0,
    },
    'emp_long': {
        'AmmoCount': 90.0, 'AmmoDamage': 450.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 240.0, 'AmmoSize': 1.0,
        'AmmoHitSizeAdjust': 0.6, 'AmmoGravityFactor': 0.2, 'AmmoExplosion': 7.0, 'AmmoIsPenetration': 0.0,
        'AmmoOwnerMove': 0.0, 'FireInterval': 40.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.0015, 'LockonTargetType': 7302.0,
    },
}
# The 2S6's guns (its native pair, both together; GroundVehicle slots): the 2A38M's 30 mm HE-FRAG.
# The Phalanx's M61A1 (both native holders on its one gun, in turn; its six barrels spin: src/vulcan_rotor.cpp): the
# M163's 20 mm HE at a CIWS's rate, an anti-air gun's mark (EDF6AutoTurret: air targets first).
GUNS['ciws_phalanx'] = {
    'AmmoCount': 1550.0, 'AmmoDamage': 5.0, 'AmmoSpeed': 18.0, 'AmmoAlive': 120.0, 'AmmoSize': 0.15,
    'AmmoHitSizeAdjust': 0.25, 'AmmoGravityFactor': 0.1, 'AmmoExplosion': 0.8, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 1.0, 'FireInterval': 1.0, 'FireBurstCount': 1.0, 'FireBurstInterval': 0.0,
    'FireAccuracy': 0.008, 'FireRecoil': 0.0, 'LockonTargetType': 7301.0,
}
# The Flak 18/36's 88 mm (both holders on its one barrel, in turn): a time-fused HE shell, its burst wide, an anti-air
# gun's mark (EDF6AutoTurret: air targets first; it still knocks out ground targets).
GUNS['flak88'] = {
    'AmmoCount': 60.0, 'AmmoDamage': 320.0, 'AmmoSpeed': 13.0, 'AmmoAlive': 220.0, 'AmmoSize': 0.9,
    'AmmoHitSizeAdjust': 0.7, 'AmmoGravityFactor': 0.3, 'AmmoExplosion': 7.0, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 0.0, 'FireInterval': 60.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.004, 'LockonTargetType': 7301.0,
}
# The Apocalypse's twin heavy guns (in turn): a big HE shell a barrel every 90 frames, a ground gun's mark.
GUNS['apocalypse'] = {
    'AmmoCount': 50.0, 'AmmoDamage': 900.0, 'AmmoSpeed': 14.0, 'AmmoAlive': 200.0, 'AmmoSize': 1.0,
    'AmmoHitSizeAdjust': 0.6, 'AmmoGravityFactor': 0.25, 'AmmoExplosion': 7.0, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 0.0, 'FireInterval': 90.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.003, 'LockonTargetType': 7302.0,
}
# The GDF-005's twin 35 mm KDA (both together: GroundVehicle.alternate off): programmable air-burst (AHEAD) rounds, a
# wide burst against air targets, an anti-air gun's mark.
GUNS['gdf005'] = {
    'AmmoCount': 280.0, 'AmmoDamage': 30.0, 'AmmoSpeed': 17.5, 'AmmoAlive': 130.0, 'AmmoSize': 0.22,
    'AmmoHitSizeAdjust': 0.5, 'AmmoGravityFactor': 0.12, 'AmmoExplosion': 3.0, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 1.0, 'FireInterval': 6.0, 'FireBurstCount': 1.0, 'FireBurstInterval': 0.0,
    'FireAccuracy': 0.004, 'FireRecoil': 0.0, 'LockonTargetType': 7301.0,
}
# The laser turret's beam (both holders on its projector, in turn): fast, straight, a pulse every 8 frames a barrel,
# an anti-air gun's mark.
GUNS['laser'] = {
    'AmmoCount': 400.0, 'AmmoDamage': 70.0, 'AmmoSpeed': 40.0, 'AmmoAlive': 60.0, 'AmmoSize': 0.6,
    'AmmoHitSizeAdjust': 0.6, 'AmmoGravityFactor': 0.0, 'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0,
    'AmmoOwnerMove': 0.0, 'FireInterval': 8.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.001, 'LockonTargetType': 7301.0,
}
# The railgun's slug (both holders on its one barrel, in turn): near-instant, flat, through everything in its path, a
# heavy blow every 90 frames; the laser cannon's streak shows its path. A ground gun's mark.
GUNS['railgun'] = {
    'AmmoCount': 30.0, 'AmmoDamage': 2500.0, 'AmmoSpeed': 60.0, 'AmmoAlive': 60.0, 'AmmoSize': 1.2,
    'AmmoHitSizeAdjust': 0.5, 'AmmoGravityFactor': 0.0, 'AmmoExplosion': 3.0, 'AmmoIsPenetration': 1.0,
    'AmmoOwnerMove': 0.0, 'FireInterval': 180.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.0005, 'LockonTargetType': 7302.0,
}
# The TOS-1's thermobaric rockets: a salvo of FireBurstCount a holder (the two in turn), spread, lobbed (EDF6AutoTurret's
# lofted mark), each a wide blast; the E551's shell as the rocket.
GUNS['tos1'] = {
    'AmmoCount': 12.0, 'AmmoDamage': 1400.0, 'AmmoSpeed': 6.0, 'AmmoAlive': 600.0, 'AmmoSize': 0.8,
    'AmmoHitSizeAdjust': 0.8, 'AmmoGravityFactor': 0.6, 'AmmoExplosion': 18.0, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 0.0, 'FireInterval': 600.0, 'FireBurstCount': 12.0, 'FireBurstInterval': 4.0, 'FireRecoil': 0.0,
    'FireAccuracy': 0.03, 'LockonTargetType': 7303.0,
}
# The Patriot's native pair: PAC-2 salvoes, the Titan's side missile (its round, its lock-on) from the pack's tubes, two
# a holder, the holders in turn; an anti-air mark. Its PAC-3 (vcobjects PATRIOT_SAM) rides the secondary.
GUNS['patriot'] = {
    'AmmoCount': 16.0, 'AmmoDamage': 1600.0, 'AmmoAlive': 900.0, 'AmmoSize': 2.0, 'AmmoHitSizeAdjust': 1.0,
    'AmmoExplosion': 14.0, 'AmmoIsPenetration': 0.0, 'AmmoOwnerMove': 0.0, 'FireInterval': 300.0,
    'FireBurstCount': 2.0, 'FireBurstInterval': 20.0, 'FireRecoil': 0.0, 'FireAccuracy': 0.02,
    'LockonTargetType': 7301.0,
}
GUNS['tunguska'] = {
    'AmmoCount': 950.0, 'AmmoDamage': 14.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 120.0, 'AmmoSize': 0.18,
    'AmmoHitSizeAdjust': 0.3, 'AmmoGravityFactor': 0.15, 'AmmoExplosion': 1.5, 'AmmoIsPenetration': 0.0,
    'AmmoOwnerMove': 1.0, 'FireInterval': 2.0, 'FireRecoil': 0.02, 'FireAccuracy': 0.005, 'LockonTargetType': 7302.0,
}
# The PzH 2000's 155 mm: the plugin's howitzer's shell (make_artillery.howitzer_sgo: its round, trail, sound,
# casing), a longer gun's numbers; the Sturmtiger's 380 mm rocket mortar: slow, a huge blast. Each barrel's
# FireInterval, the two native holders on the one gun in turn (GroundVehicle.alternate): a round every half of it.
GUNS['pzh2000'] = {'AmmoCount': 30.0, 'AmmoDamage': 3200.0, 'AmmoSpeed': 5.0, 'AmmoExplosion': 28.0,
                   'FireInterval': 240.0, 'FireAccuracy': 0.006}
GUNS['sturmtiger'] = {'AmmoCount': 7.0, 'AmmoDamage': 6000.0, 'AmmoSpeed': 3.2, 'AmmoExplosion': 40.0,
                      'FireInterval': 900.0, 'FireAccuracy': 0.01, 'AmmoGravityFactor': 0.7}
HOWITZERS = ('pzh2000', 'sturmtiger')      # built on make_artillery.howitzer_sgo
# A kind whose turret hinge (and its seat's yaw axis) has stops (make_artillery.turret_constraint): [1, min, max] deg.
TURRET_LIMITS = {'sturmtiger': [1.0, -10.0, 10.0]}
# The kinds built from their War Thunder model folders alone (wt_tracked.Model) and their vehicle cameras.
WT_ONLY = {'tunguska': {'model_eye': (0., 2.9, .9), 'model_look_at': (0., 2.6, 7.),
                        'third_person_distance': 8., 'third_person_height': 3.6},
           'pzh2000': {'model_eye': (0., 3.2, -1.), 'model_look_at': (0., 2.8, 9.),
                       'third_person_distance': 10., 'third_person_height': 4.0},
           'sturmtiger': {'model_eye': (0., 3.0, 1.), 'model_look_at': (0., 2.4, 8.),
                          'third_person_distance': 9., 'third_person_height': 3.6},
           'apocalypse': {'model_eye': (0., 4.2, 1.1), 'model_look_at': (0., 3.4, 10.),
                          'third_person_distance': 14., 'third_person_height': 6.0},
           'tos1': {'model_eye': (0., 3.4, -1.), 'model_look_at': (0., 2.6, 9.),
                    'third_person_distance': 10., 'third_person_height': 4.2}}
# The emplacements' round, sound and flash: the E551's cannon's (the Kepler gun's numbers and muzzle locator stay).
EMPLACEMENT_SHELL = 'V_505TANK_CANNON01.SGO'
DREADNOUGHT_GUNS = ('emp_mid', 'emp_short', 'emp_long')
# The emplacements firing that shell (the Kepler gun's numbers are theirs: GUNS), with its sound, flash and a big case;
# the laser turret the Brute heli's laser cannon's beam (its round, sound and flash).
CANNON_SHELL = DREADNOUGHT_GUNS + ('flak88', 'apocalypse', 'laser', 'railgun', 'tos1', 'patriot')
SHELL_OF = {'laser': 'V_506HELI_LASERCANNON01_L.SGO', 'railgun': 'V_506HELI_LASERCANNON01_L.SGO',
            'patriot': 'V_404BIGTANK_SIDEMISSILE_L.SGO'}   # else EMPLACEMENT_SHELL
# A missile shell's own model, flight and lock-on, taken with SHELL_KEYS (the Titan's side missile locks on natively).
MISSILE_SHELL_KEYS = ('AmmoModel', 'AmmoSpeed', 'AmmoGravityFactor', 'LockonType', 'LockonRange', 'LockonAngle',
                      'LockonTime', 'LockonHoldTime')
MISSILE_SHELL = ('patriot',)
SHELL_KEYS = ('AmmoClass', 'Ammo_CustomParameter', 'AmmoColor', 'AmmoHitSe', 'AmmoHitImpulseAdjust', 'FireSe',
              'MuzzleFlash', 'MuzzleFlash_CustomParameter')
# The BMPT's second belt (vcobjects STORES BMPT_AP / BMPT_AP_R): the 2A42 is a dual-feed gun, HE-FRAG-T (GUNS) or the
# 3UBR8 APDS-T. Over GUNS['bmpt']: penetrating, no burst, faster (3UBR8 ~1120 m/s against the HE-FRAG's ~960: 16 x 1.17
# m a frame), flatter, more on one target; the AP box is the smaller (real BMPT: 850 rounds of both, 160 of them AP).
BMPT_AP_BELT = {'AmmoCount': 150.0, 'AmmoDamage': 30.0, 'AmmoSpeed': 18.7, 'AmmoGravityFactor': 0.1,
                'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0}
# The quad Vulcan's AP belt (STORES QVULCAN_AP / QVULCAN_AP_R): the M163's AP numbers over its HE.
QUADVULCAN_AP_BELT = {'AmmoCount': 1000.0, 'AmmoDamage': 6.0, 'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0}
AP_BELTS = {'bmpt': BMPT_AP_BELT, 'quadvulcan': QUADVULCAN_AP_BELT}
NAMES = {
    'bmpt': ('BMPT 30 mm Autocannon', '\u0042\u004d\u0050\u0054 30mm\u673a\u70ae',
             '\u0042\u004d\u0050\u0054 30mm\u6a5f\u95a2\u7832', '\u0042\u004d\u0050\u0054 30mm\u6a5f\u7832'),
    'quadvulcan': ('Quad M61 HE', '四联 M61 高爆弹', '4連装 M61 榴弾', '四聯 M61 高爆彈'),
    'emp_mid': ('SecMid Twin Gun', '中型双联速射炮', '中型連装速射砲', '中型雙聯速射砲'),
    'emp_short': ('SecShort Twin Heavy Gun', '短管双联重炮', '短砲身連装重砲', '短管雙聯重砲'),
    'emp_long': ('SecLong Twin Long Gun', '长管双联远程炮', '長砲身連装遠距離砲', '長管雙聯遠程砲'),
    'flak88': ('8.8 cm Flak 18/36', '88mm 高射炮', '8.8cm 高射砲', '88mm 高射砲'),
    'railgun': ('Railgun', '电磁炮', 'レールガン', '電磁砲'),
    'laser': ('Laser Projector', '激光发射器', 'レーザー照射器', '雷射發射器'),
    'patriot': ('MIM-104 PAC-2', 'MIM-104 PAC-2', 'MIM-104 PAC-2', 'MIM-104 PAC-2'),
    'gdf005': ('35 mm KDA AHEAD', '35mm KDA 空爆弹', '35mm KDA 空中炸裂弾', '35mm KDA 空爆彈'),
    'ciws_phalanx': ('M61A1 20 mm CIWS', 'M61A1 20mm 近防炮', 'M61A1 20mm 近接防御火器', 'M61A1 20mm 近防砲'),
    'tunguska': ('2A38M 30 mm', '2A38M 30mm 机炮', '2A38M 30mm 機関砲', '2A38M 30mm 機砲'),
    'apocalypse': ('Twin Heavy Gun', '双管重炮', '連装重砲', '雙管重砲'),
    'tos1': ('Thermobaric Rockets', '温压火箭', 'サーモバリック・ロケット', '溫壓火箭'),
    'pzh2000': ('155 mm L52 Howitzer', '155mm L52 榴弹炮', '155mm L52 榴弾砲', '155mm L52 榴彈砲'),
    'sturmtiger': ('380 mm Rocket Mortar', '380mm 火箭臼炮', '380mm ロケット臼砲', '380mm 火箭臼砲'),
}


def _node(value: object) -> object:
    if isinstance(value, (tuple, list)):
        return dsgo.Node([_node(x) for x in value])
    return float(value) if isinstance(value, int) and not isinstance(value, bool) else value


def model_module(kind: str):
    if kind == 'vulcan':
        import vulcan_model
        return vulcan_model
    if kind == 'bmpt':
        import bmpt_model
        return bmpt_model
    if kind == 'quadvulcan':
        import quadvulcan_model
        return quadvulcan_model
    if kind in EMPLACEMENTS:
        return emplacement_model.MODELS[kind]
    if kind in WT_ONLY:
        import wt_tracked
        return wt_tracked.Model(kind, WT_ONLY[kind])
    raise ValueError(f'unsupported tracked combat vehicle: {kind}')


def _rename(r: dsgo.Node, en: str, sc: str, ja: str, cn: str) -> None:
    for lang, name in {'en': en, 'sc': sc, 'ja': ja, 'cn': cn, 'kr': en}.items():
        if f'name.{lang}' in r.names.values():
            r.set(f'name.{lang}', name)


def set_muzzles(data: bytes, points: list[tuple[str, tuple[float, float, float]]]) -> bytes:
    """Move the native locator positions without discarding other MAB lists.

    The gun's casing/effect lists are real MAB lists too; mab.mab_read only
    supports the special one-list launcher shape. Each template here already
    has the required count (one gun muzzle / four missile tubes). The authored
    point's string is the vehicle holder bone, not the V_NULL locator's name.
    """
    found = vc.mab_muzzles(data)
    if len(found) != len(points) or not 0 < len(points) <= 16:
        raise ValueError(f'muzzle count differs: native {len(found)}, authored {len(points)}')
    out = bytearray(data)
    heads, end = struct.unpack_from('<2I', data, 0x14)
    first = heads + 8 * struct.unpack_from('<H', data, 0x0C)[0]
    for (_name, _node, offset), (_bone, point) in zip(found, points):
        users = [rec for rec in range(first, end, 0x20) for field in (0xC, 0x10, 0x14)
                 if rec + struct.unpack_from('<i', data, rec + field)[0] == offset]
        if len(users) != 1:
            raise ValueError('muzzle position is shared with another locator; cannot move it independently')
        struct.pack_into('<3f', out, offset, *point)
    return bytes(out)


def set_ejection(data: bytes, position: tuple[float, float, float]) -> bytes:
    """Relocate the native casing list without altering its velocity/rotation SGO."""
    if data[:4] != b'MAB\0' or struct.unpack_from('<H', data, 0xC)[0] < 3:
        raise ValueError('gun MAB has no native casing list')
    heads, end = struct.unpack_from('<2I', data, 0x14)
    header = heads + 2 * 8
    kind, count, first = struct.unpack_from('<HHi', data, header)
    if kind != 2 or count != 1:
        raise ValueError('gun MAB casing list is not a single kind-2 locator')
    record = header + first
    offset = record + struct.unpack_from('<i', data, record + 0xC)[0]
    records = heads + 8 * struct.unpack_from('<H', data, 0xC)[0]
    users = [rec for rec in range(records, end, 0x20) for field in (0xC, 0x10, 0x14)
             if rec + struct.unpack_from('<i', data, rec + field)[0] == offset]
    if len(users) != 1:
        raise ValueError('casing position is shared with another MAB locator')
    out = bytearray(data)
    struct.pack_into('<3f', out, offset, *position)
    return bytes(out)


def _weapon_muzzles(doc: dsgo.Document, points: list[tuple[str, tuple[float, float, float]]]) -> None:
    animation = doc.root.get('animation_model')
    block = animation.items[2]
    animation.items[2] = dsgo.Blob(set_muzzles(block.data, points), block.kind)


def gun_sgo(game: vc.Game, kind: str, stock: str, side: str,
            points: list[tuple[str, tuple[float, float, float]]], belt: str | None = None) -> bytes:
    if kind == 'vulcan':
        name = vc.store_file('VULCAN_HE' if side == 'left' else 'VULCAN_AP', 2000)
        doc = dsgo.parse(vc.store_sgo(game, name))
        values = {
            'AmmoDamage': 4.0 if side == 'left' else 6.0,
            'AmmoSpeed': 17.0, 'AmmoAlive': 120.0, 'AmmoSize': 0.15,
            'AmmoHitSizeAdjust': 0.2, 'AmmoGravityFactor': 0.15,
            'AmmoExplosion': 0.6 if side == 'left' else 0.0,
            'AmmoIsPenetration': 0.0 if side == 'left' else 1.0,
            'AmmoOwnerMove': 1.0, 'FireInterval': 1.0,
            'FireBurstCount': 1.0, 'FireBurstInterval': 0.0,
            'FireAccuracy': 0.006, 'FireRecoil': 0.006, 'LockonTargetType': 7301.0,
        }
        for key, value in values.items():
            if key not in doc.root.names.values():
                raise ValueError(f'{name}: missing {key}')
            doc.root.set(key, value)
    elif belt is not None:
        # The BMPT's APDS belt: its own store file (one switch choice for both barrels, vcobjects STORES BMPT_AP*),
        # the native gun's numbers with BMPT_AP_BELT over them, the store's name.
        doc = dsgo.parse(vc.store_sgo(game, belt))
        native = dsgo.parse(game.read('WEAPON', stock)).root
        for key, value in {**GUNS[kind], **AP_BELTS[kind]}.items():
            if doc.root.get(key) is None:
                raise ValueError(f'{belt}: missing {key}')
            doc.root.set(key, value)
        for key in ('MuzzleFlash', 'MuzzleFlash_CustomParameter', 'ShellCase'):
            doc.root.set(key, copy.deepcopy(native.get(key)))
        doc.root.set('animation_model', copy.deepcopy(native.get('animation_model')))
    else:
        # The quad Vulcan's HE is the M163's HE round (its store's template), on the native pair's holders; the
        # howitzers' the plugin's howitzer shell.
        if kind in HOWITZERS:
            import make_artillery
            doc = dsgo.parse(make_artillery.howitzer_sgo(game, stock))
        else:
            doc = dsgo.parse(vc.store_sgo(game, vc.store_file('VULCAN_HE', 2000)) if kind in M61_ROUNDS
                             else game.read('WEAPON', stock))
        for key, value in GUNS[kind].items():
            if doc.root.get(key) is None:
                raise ValueError(f'{stock}: missing {key}')
            doc.root.set(key, value)
        _rename(doc.root, *NAMES[kind])
        if kind in CANNON_SHELL:
            source = SHELL_OF.get(kind, EMPLACEMENT_SHELL)
            shell = dsgo.parse(game.read('WEAPON', source)).root
            for key in SHELL_KEYS:
                if shell.get(key) is None or doc.root.get(key) is None:
                    raise ValueError(f'{source} / {stock}: missing {key}')
                doc.root.set(key, copy.deepcopy(shell.get(key)))
            for key in MISSILE_SHELL_KEYS if kind in MISSILE_SHELL else ():
                if shell.get(key) is None or doc.root.get(key) is None:
                    raise ValueError(f'{source} / {stock}: missing {key}')
                doc.root.set(key, copy.deepcopy(shell.get(key)))
    if doc.root.get('xgs_scene_object_class') != 'Weapon_VehicleShoot':
        raise ValueError(f'{stock}: not a native vehicle gun')
    # Each shot is a finite one-shot cue. A continuous native loop needs a stop
    # event and cannot safely be introduced by replacing only the firing cue.
    fire = doc.root.get('FireSe')
    if isinstance(fire, dsgo.Node) and len(fire.items) == 6 and kind not in CANNON_SHELL + HOWITZERS:
        fire.items[:] = [0.0, 'weapon_VHC_heli302_gatling', 0.55, 1.2 if kind in ONE_CRADLE else 0.85, 1.0, 50.0]
    _weapon_muzzles(doc, points)
    # 20/30 mm cases must leave the authored receiver, not the Kepler's
    # floating ejection port; preserve the native velocity/rotation lists.
    block = doc.root.get('animation_model').items[2]
    ejection = model_module(kind).ejection_local(game)[side]
    doc.root.get('animation_model').items[2] = dsgo.Blob(set_ejection(block.data, ejection), block.kind)
    case = (1.6 if kind in CANNON_SHELL else 0.4 if kind in ONE_CRADLE else
            _howitzer_case() if kind in HOWITZERS else 0.64)
    doc.root.set('ShellCase_CustomParameter', dsgo.Node([case], {0: 'scale'}))
    if kind in ONE_CRADLE and kind not in CANNON_SHELL:
        reference = dsgo.parse(game.read('WEAPON', STOCK_GUNS[0])).root
        doc.root.set('MuzzleFlash', reference.get('MuzzleFlash'))
        doc.root.set('MuzzleFlash_CustomParameter', copy.deepcopy(reference.get('MuzzleFlash_CustomParameter')))
    return dsgo.compact(doc)


# Each holder's part (vehicle weapons in order), the registry's (GroundVehicle.slots): the BMPT's native guns' sides, the
# missile tubes, then the second belt's sides (on the same moving barrels) and the thermobaric missile (the same tubes);
# the quad Vulcan's HE columns then its AP columns. The plugin's twin-barrel cadence (vcobjects.twin_barrels) pairs the
# BMPT's barrels from the same table.
def _howitzer_case() -> float:
    """The howitzers' casing scale (make_artillery.CASE_SCALE: 90 % of the plugin howitzer's bore)."""
    import make_artillery
    return make_artillery.CASE_SCALE


def slot(kind: str, index: int) -> str:
    """What holder `index` of `kind` fires from: 'left' / 'right' (a gun's barrel) or 'missiles' (the tubes)."""
    slots = vc.GROUND_VEHICLES[kind].slots
    return slots[index] if slots else ('left' if index == 0 else 'right')


def missile_sgo(game: vc.Game, points: list[tuple[str, tuple[float, float, float]]],
                store: str = vc.store_file('BMPT_ATGM', 4)) -> bytes:  # the BMPT's Ataka, the 2S6's 9M311 (`store`)
    doc = dsgo.parse(vc.store_sgo(game, store))
    _weapon_muzzles(doc, points)
    return dsgo.compact(doc)


def collision_path(kind: str) -> str:
    if kind not in KINDS:
        raise ValueError(f'unsupported tracked combat vehicle: {kind}')
    return f'OBJECT/EDF6VC_{kind.upper()}_COLLISION.SHKT'


def animation_path(kind: str) -> str:
    return f'OBJECT/{model_module(kind).OUT_CAS}'


def _assets(game: vc.Game, kind: str):
    """Share one actual model/collision build within a complete asset transaction."""
    import tracked_combat_model as tracked
    import tracked_collision
    cache = getattr(game, '_tracked_combat_assets', None)
    if cache is None:
        cache = {}
        game._tracked_combat_assets = cache
    if kind not in cache:
        model = model_module(kind)
        arc, md, _info = model.build_with_info(game)
        assembly = tracked.Assembly(md)
        model.parts(assembly)
        shkt, binding, overrides, report = tracked_collision.build(game, kind, md, assembly.parts,
                                                                   layout=model.collision_layout())
        cache[kind] = arc, md, shkt, binding, overrides, report
    return cache[kind]


def vehicle_sgo(game: vc.Game, kind: str) -> bytes:
    """Authored model and fitted physics on the native V603 animation/controllers."""
    vehicle = vc.GROUND_VEHICLES[kind]
    model = model_module(kind)
    doc = dsgo.parse(game.read('OBJECT', STOCK_VEHICLE))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError('the expected native V603 class is absent')
    animation = r.get('animation_model')
    animation.items[0].items[0] = f'app:/Object/{model.OUT_ARC.lower()}'
    animation.items[0].items[1] = model.HOST_MDB
    animation.items[1] = 'app:/' + animation_path(kind).lower()
    _arc, _md, _shkt, binding, overrides, _report = _assets(game, kind)
    ragdoll = r.get('ragdoll')
    ragdoll.items[0] = 'app:/' + collision_path(kind).lower()
    ragdoll.items[1] = dsgo.Blob(binding, ragdoll.items[1].kind)
    for key, value in overrides.items():
        r.set(key, _node(value))
    setup = r.get('vehicle_setup')
    weapons = setup.items[-1]
    if len(weapons.items) != 2:
        raise ValueError('native V603 setup must have exactly two weapons')
    native = list(weapons.items)
    weapons.items = []
    for index, path in enumerate(vehicle.weapons):
        side = slot(kind, index)
        entry = copy.deepcopy(native[1 if side == 'right' else 0 if side == 'left' else min(index, 1)])
        entry.items[0] = path
        # Both classes require a BodyRecoil variant. The BMPT's barrels (either belt) keep the native gun mount's
        # (its kick on every shot: the firing shake the request already has, call_weapons.vehicle_sgo; 2026-10-21
        # 「开火动画没了」: the entity / test range copy had none); its missiles and the M163's one M61 none.
        if kind != 'bmpt' or side not in ('left', 'right'):
            entry.items[1] = _node([0.0, 0.0])
        weapons.items.append(entry)
    setting = r.get('vehicle_weapon_setting')
    if len(setting.items) != 2:
        raise ValueError('native V603 must have two weapon mounts')
    guns = [copy.deepcopy(setting.items[0]), copy.deepcopy(setting.items[1])]
    if kind in ONE_CRADLE:
        # HE/AP are alternate belts in the same visible M61 (the quad's: its four on one cradle): the right holders
        # must follow the left slide too, throughout elevation and recoil.
        guns[1].items[0] = 'cannon_slide_l'
    # A gun barrel's holder on its side's moving bone, the missiles' on the turret (slot).
    sides = [slot(kind, i) for i in range(len(vehicle.weapons))]
    setting.items = [copy.deepcopy(guns[side == 'right']) if side != 'missiles' else _node(['cannon_main', 0.0])
                     for side in sides]
    if kind in TURRET_LIMITS:
        import make_artillery
        joint = make_artillery.turret_constraint(r)
        if dsgo.to_py(joint.items[1].items[1]) != [0.0]:
            raise ValueError(f'{kind}: the native turret hinge already has stops')
        joint.items[1].items[1] = _node(TURRET_LIMITS[kind])
    camera = r.get('game_object_camera_setting')
    camera.items[0] = _node([0.0, model.CAMERA['model_eye'][1], 2.0])
    camera.items[1] = _node([0.0, model.CAMERA['third_person_height'], -model.CAMERA['third_person_distance']])
    r.set('game_object_durability', vehicle.durability)
    # Include the fitted private SHKT in the same resource transaction.
    resource = r.get('resource') if 'resource' in r.names.values() else None
    if resource is not None:
        for path in [animation.items[0].items[0], animation.items[1], ragdoll.items[0], *vehicle.weapons]:
            if path.lower() not in {str(item).lower() for item in resource.items}:
                resource.items.append(path)
    return dsgo.compact(doc)


def _points(model, game: vc.Game) -> dict[str, list[tuple[str, tuple[float, float, float]]]]:
    points = model.muzzle_local(game)
    for side in ('left', 'right'):
        if side not in points:
            raise ValueError(f'{model.__name__}: missing {side} muzzle contract')
    return points


def check(files: dict[str, bytes], kind: str, game: vc.Game | None = None) -> None:
    vehicle = vc.GROUND_VEHICLES[kind]
    model = model_module(kind)
    expected = {f'OBJECT/{vehicle.sgo}.SGO', f'OBJECT/{model.OUT_ARC}', collision_path(kind), animation_path(kind),
                *(f'WEAPON/{path.rsplit("/", 1)[1].upper()}' for path in vehicle.weapons)}
    if set(files) != expected:
        raise ValueError(f'{kind}: incomplete generated asset set {sorted(set(files) ^ expected)}')
    model.check(files[f'OBJECT/{model.OUT_ARC}'])
    r = dsgo.parse(files[f'OBJECT/{vehicle.sgo}.SGO']).root
    settings = dsgo.to_py(r.get('vehicle_weapon_setting'))
    weapons = dsgo.to_py(r.get('vehicle_setup'))[-1]
    if len(settings) != len(vehicle.weapons) or tuple(w[0] for w in weapons) != vehicle.weapons:
        raise ValueError(f'{kind}: weapon/mount/setup contracts disagree')
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError(f'{kind}: native vehicle class changed')
    if dsgo.to_py(r.get('animation_model'))[0] != [f'app:/Object/{model.OUT_ARC.lower()}', model.HOST_MDB]:
        raise ValueError(f'{kind}: generated model is not selected')
    if r.get('animation_model').items[1] != 'app:/' + animation_path(kind).lower():
        raise ValueError(f'{kind}: private fixed-optics animation is not selected')
    for index, path in enumerate(vehicle.weapons):
        blob = files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}']
        gun = dsgo.parse(blob).root
        if gun.get('AmmoCount') <= 0 or gun.get('AmmoDamage') <= 0:
            raise ValueError(f'{kind}: nonfunctional weapon {path}')
        muzzles = vc.mab_muzzles(gun.get('animation_model').items[2].data)
        expected_count = 4 if slot(kind, index) == 'missiles' else 1
        if len(muzzles) != expected_count:
            raise ValueError(f'{kind}: {path} has {len(muzzles)} muzzle locators, expected {expected_count}')
        if dsgo.write(dsgo.parse(blob)) != blob:
            raise ValueError(f'{kind}: weapon does not round-trip')
    if game is not None:
        original = dsgo.parse(game.read('OBJECT', STOCK_VEHICLE)).root
        for key in ('car_base_rigid_body', 'car_base_constraint_data', 'tank_caterpillar_animation'):
            if key == 'car_base_constraint_data' and kind in TURRET_LIMITS:
                import make_artillery
                if dsgo.to_py(make_artillery.turret_constraint(r).items[1].items[1]) != TURRET_LIMITS[kind]:
                    raise ValueError(f'{kind}: the turret hinge lost its stops')
                continue
            if dsgo.to_py(r.get(key)) != dsgo.to_py(original.get(key)):
                raise ValueError(f'{kind}: native {key} changed without refitting its contract')
        import tracked_collision
        if files[animation_path(kind)] != model.animation(game):
            raise ValueError(f'{kind}: private optics animation payload differs')
        _arc, md, shkt, binding, overrides, report = _assets(game, kind)
        if files[collision_path(kind)] != shkt:
            raise ValueError(f'{kind}: fitted collision file differs')
        ragdoll = r.get('ragdoll')
        if ragdoll.items[0] != 'app:/' + collision_path(kind).lower() or ragdoll.items[1].data != binding:
            raise ValueError(f'{kind}: fitted collision/binding is not selected')
        for key, value in overrides.items():
            if dsgo.to_py(r.get(key)) != value:
                raise ValueError(f'{kind}: fitted {key} was not applied')
        tracked_collision.check(shkt, binding, md, overrides, report)
        points = _points(model, game)
        for index, path in enumerate(vehicle.weapons):
            gun = dsgo.parse(files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}']).root
            block = gun.get('animation_model').items[2].data
            positions = [struct.unpack_from('<3f', block, off) for _, _, off in vc.mab_muzzles(block)]
            authored = points[slot(kind, index)]
            if any(bone != settings[index][0] for bone, _ in authored):
                raise ValueError(f'{kind}: holder uses a different moving bone from the visible barrel')
            wanted = [p for _, p in authored]
            if any(abs(a-b) > 1e-5 for got, want in zip(positions, wanted) for a,b in zip(got,want)):
                raise ValueError(f'{kind}: muzzle left the authored barrel/launcher')


def authored(root: str, kind: str) -> dict[str, bytes]:
    """Every file build() writes, checked, before its seats' cockpits are grafted into the model: the authored
    geometry's contracts (check) are made on this, and readers of the weapons alone (gen_spawn_catalog) need no
    cockpit."""
    game = vc.Game(root)
    model = model_module(kind)
    vehicle = vc.GROUND_VEHICLES[kind]
    points = _points(model, game)
    arc, _md, shkt, _binding, _overrides, _report = _assets(game, kind)
    files = {f'OBJECT/{vehicle.sgo}.SGO': vehicle_sgo(game, kind),
             f'OBJECT/{model.OUT_ARC}': arc, collision_path(kind): shkt,
             animation_path(kind): model.animation(game)}
    for i, path in enumerate(vehicle.weapons):
        # The native pair (the first two), then the stores: a second belt on the same barrels, the missiles.
        name, side = path.rsplit('/', 1)[1].upper(), slot(kind, i)
        if side == 'missiles':
            files[f'WEAPON/{name}'] = missile_sgo(game, points['missiles'], name)
        else:
            files[f'WEAPON/{name}'] = gun_sgo(game, kind, STOCK_GUNS[side == 'right'], side, points[side],
                                              name if i >= 2 else None)
    check(files, kind, game)
    return files


def build(root: str, kind: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}: authored() with its seats' first-person cockpits grafted
    into the model (pylib/cockpits.py; the fits are pylib/cockpit_fits_plugin.py's, by the model's stem), as every
    generated vehicle's are: without a `vc_eye_<seat>` marker in the model a seat has no first person
    (src/fp_vehicle.cpp VehicleEye)."""
    return cockpits.graft_files(vc.Game(root), authored(root, kind))


def install(root: str, files: dict[str, bytes], kind: str) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(kind))
    paths = [led.put(kind, rel, data) for rel, data in files.items()]
    led.release(kind, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str, kind: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(kind, sorted(set(led.owned_by(kind))), writer=True)


def main(argv: list[str], kind: str | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', nargs='?', default=vc.DEFAULT_GAME)
    if kind is None:
        parser.add_argument('--kind', choices=KINDS, required=True)
    parser.add_argument('--out', type=Path, help='write generated files under this output root; never installs')
    parser.add_argument('--check', action='store_true', help='generate and validate in memory only')
    parser.add_argument('--remove', action='store_true')
    args = parser.parse_args(argv)
    kind = kind or args.kind
    if args.remove:
        if args.out or args.check:
            parser.error('--remove cannot be combined with --out/--check')
        deleted, kept = remove(args.game, kind)
        for path in deleted:
            print('deleted', path)
        for path in kept:
            print('kept modified file', path)
        return 0
    files = build(args.game, kind)
    if args.out:
        for rel, data in files.items():
            path = args.out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f'{kind}: generated and checked {len(files)} files in {args.out}')
    elif args.check:
        print(f'{kind}: generated and checked {len(files)} files in memory')
    else:
        for path in install(args.game, files, kind):
            print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
