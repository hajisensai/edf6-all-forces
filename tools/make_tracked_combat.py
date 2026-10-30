"""Build the M163 Vulcan / quad Vulcan / BMPT from authored geometry and the player's native V603 contracts.

Root.cpk is read-only. build(root, kind) returns complete {Mods-relative path: bytes};
--out writes only there. Installation/removal use the same ownership ledger as other vehicles.
The bodies are deliberately authored models, not renamed stock vehicle meshes or downloaded assets.
"""
from __future__ import annotations

import argparse
import copy
import os
import struct
import sys
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent / 'pylib'), str(HERE)]

import cockpits
import dsgo
import ledger
import mab_legacy
import model_lod
import emplacement_model
import vcobjects as vc
import xm_units

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
# The X-Morph: Defense units (pylib/xm_units.py, built by tools/make_xmorph.py): their `like`'s gun in every table above
# (its numbers, its shell, its kind of round) with the row's numbers over it, its own gun names, its vehicle camera over
# the `like`'s, a model folder of its own (WT_ONLY: wt_tracked.Model). What belongs to the `like`'s hull or model (the
# one-cradle M61s, turret stops, the procedural models) is not inherited.
for _xm in xm_units.v603():
    # a tracked unit is a wt_tracked.Model kind (WT_ONLY: its camera), an emplacement an emplacement_model one (KINDS)
    _kinds = WT_ONLY if _xm.family == 'tracked' else EMPLACEMENTS
    if _xm.like not in GUNS or _xm.like not in _kinds:
        raise ValueError(f'{_xm.key}: its like {_xm.like!r} is no {_xm.family} kind with a gun table (GUNS)')
    LABELS[_xm.key] = f'{xm_units.label(_xm)}（X-Morph: Defense 模型）'
    GUNS[_xm.key] = {**GUNS[_xm.like], **_xm.guns}
    NAMES[_xm.key] = _xm.gun
    if _xm.family == 'tracked':
        WT_ONLY[_xm.key] = {**WT_ONLY[_xm.like], **xm_units.camera(_xm)}
    if _xm.shell or _xm.like in SHELL_OF:
        SHELL_OF[_xm.key] = _xm.shell or SHELL_OF[_xm.like]
    M61_ROUNDS += (_xm.key,) if _xm.like in M61_ROUNDS else ()
    HOWITZERS += (_xm.key,) if _xm.like in HOWITZERS else ()
    CANNON_SHELL += (_xm.key,) if _xm.like in CANNON_SHELL else ()
    MISSILE_SHELL += (_xm.key,) if _xm.like in MISSILE_SHELL else ()
# The kinds whose track faces are a fixed part of the body's texture atlas (the X-Morph units: tools/prep_xmorph.py
# puts their belts' running faces under v603_kyata_L / _R only because the native class binds those two names): their
# SGO keeps the two tank_caterpillar_animation rows (the class reads exactly two, EDF.dll 0x67E4B0) but scrolls them at
# rate 0. Each frame the class sets a track's offset to frac(tank_caterpillar_animation_info[0] x K x track speed +
# offset) (0x67EADB: +0x2960 the rate, read at 0x67E787 from _info[0]), so a zero rate leaves the texture where it is;
# the V603's -0.43 would scroll the atlas's neighbouring art through the tread.
STATIC_TRACKS = tuple(u.key for u in xm_units.ported('tracked'))
ONE_CRADLE += tuple(u.key for u in xm_units.ported('emplacement'))   # as every emplacement: both holders on its cradle


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


# A machine gun's report (FireSe: [loop owner, cue, volume, pitch, 1, heard-at-full metres]). The gatling cue has no end
# (its ACB length is 0xFFFFFFFF, like every cue the stock weapons play in loop-owner mode: the M163's own guns, the 403's
# machine gun, the bike's gatling), so it must be played in the weapon's loop-owner mode (entry 0 = 1): the weapon starts
# it once, renews its hold (+0xE20 = FireInterval + 4) at each round and stops it (+0xE28, 8 frames' fade) once the
# trigger is let go (0x693FD3..0x69400B). Played one-shot (entry 0 = 0) each round starts another voice nothing owns and
# nothing stops: the fire sound that never ended after the trigger was released (the user, 2026-10-25). A slower gun
# (BMPT, GDF-005) takes the Striker's autocannon report instead, a finite cue played a round at a time as the stock
# weapons play theirs (vehsound.cpp calls such a gun an autocannon, a looped one a machine gun).
RAPID_FRAMES = 5   # vehmix.h kRapidFrames: the interval up to which the plugin hears a gun as a machine gun's burst
GATLING_CUE, AUTOCANNON_CUE = 'weapon_VHC_heli302_gatling', 'weapon_VHC_striker401_cannonTekkoRapid'


def fire_se(interval: float, pitch: float) -> list:
    """The FireSe entries of a replaced machine gun / autocannon firing every `interval` frames."""
    if interval <= RAPID_FRAMES:
        return [1.0, GATLING_CUE, 0.55, pitch, 1.0, 50.0]
    return [0.0, AUTOCANNON_CUE, 0.55, pitch, 1.0, 50.0]


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
    fire = doc.root.get('FireSe')
    if isinstance(fire, dsgo.Node) and len(fire.items) == 6 and kind not in CANNON_SHELL + HOWITZERS:
        fire.items[:] = fire_se(float(doc.root.get('FireInterval')), 1.2 if kind in ONE_CRADLE else 0.85)
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


GUN_SLOTS = ('left', 'right')


def slot(kind: str, index: int) -> str:
    """What holder `index` of `kind` fires from: 'left' / 'right' (a gun's barrel), else a store's own muzzles (the
    model's muzzle contract of that name: 'missiles' the tubes, an X-Morph unit's arm its 'xm_<key>')."""
    slots = vc.GROUND_VEHICLES[kind].slots
    return slots[index] if slots else ('left' if index == 0 else 'right')


def slot_points(points: list[tuple[str, tuple[float, float, float]]], count: int) -> list:
    """A store slot's authored muzzles as its weapon's `count` locators take them, in turn (the BMPT's four tubes their
    four; an X-Morph pod's one or two game muzzles round its template's four; a one-muzzle gun its first)."""
    if not points:
        raise ValueError('a store slot without muzzles')
    return [points[i % len(points)] for i in range(count)]


def store_weapon_sgo(game: vc.Game, points: list[tuple[str, tuple[float, float, float]]], store: str) -> bytes:
    """A store's weapon (vcobjects.store_sgo: the BMPT's Ataka, the 2S6's 9M311, an X-Morph unit's arm) firing from its
    slot's muzzles (slot_points)."""
    doc = dsgo.parse(vc.store_sgo(game, store))
    count = len(vc.mab_muzzles(doc.root.get('animation_model').items[2].data))
    _weapon_muzzles(doc, slot_points(points, count))
    return dsgo.compact(doc)


def collision_path(kind: str) -> str:
    if kind not in KINDS:
        raise ValueError(f'unsupported tracked combat vehicle: {kind}')
    return f'OBJECT/EDF6VC_{kind.upper()}_COLLISION.SHKT'


def animation_path(kind: str) -> str:
    return f'OBJECT/{model_module(kind).OUT_CAS}'


_ASSETS: dict[tuple, tuple] = {}


def _assets(game: vc.Game, kind: str):
    """One actual model/collision build a kind, shared by every reader in the process: it reads only the game's archive
    (vc.Game is rootcpk's: Root.cpk, read only) and the repository's model folders, so it is the same build for the
    same archive (its path, size and time). Until 2026-10-25 it was shared by one Game object only: authored(),
    vehicle_sgo() and testrange's gen each open their own, and a reader of every kind's SGOs (gen_spawn_catalog,
    tools/selftest.py spawn_catalog_archives_recorded) built each kind four or five times (85 builds, ~15 min)."""
    import tracked_combat_model as tracked
    import tracked_collision
    import wt_tracked
    st = os.stat(game.cpk.path)
    # wt_tracked.ENABLED: the War Thunder models or the procedural ones (tests switch it off): another build.
    key = (os.path.normcase(os.path.abspath(game.cpk.path)), st.st_size, st.st_mtime_ns, wt_tracked.ENABLED, kind)
    cache = _ASSETS
    if key not in cache:
        model = model_module(kind)
        arc, md, _info = model.build_with_info(game)
        assembly = tracked.Assembly(md)
        model.parts(assembly)
        shkt, binding, overrides, report = tracked_collision.build(game, kind, md, assembly.parts,
                                                                   layout=model.collision_layout())
        cache[key] = arc, md, shkt, binding, overrides, report
    return cache[key]


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
    # A gun barrel's holder on its side's moving bone, a store's on the bone its muzzles ride (the muzzle contract's:
    # the turret, the BMPT's elevating weapon module, prep_wt_tracked Vehicle.missile_bone; an X-Morph arm's
    # xm_units.Arm.bone): the round leaves along that bone's forward row (muzzle mode 0), so on the module or the gun it
    # leaves along the guns' line (slot).
    sides = [slot(kind, i) for i in range(len(vehicle.weapons))]
    points = _points(model, game)
    holders = {}
    for side in sides:
        if side not in GUN_SLOTS:
            bones = {bone for bone, _ in points.get(side, [])}
            if len(bones) != 1:
                raise ValueError(f'{kind}: the {side} muzzles ride {sorted(bones)}: one holder bone needs them on one')
            (holders[side],) = bones
    # A store on its own gunner seat (vcobjects Store.seat): its holder on that seat ([bone, seat]: VehicleBase
    # 0x62A52E puts it in that seat's list; stores.cpp Fill builds its weapon, docs/independent-seat-re.md §1).
    setting.items = [copy.deepcopy(guns[side == 'right']) if side in GUN_SLOTS else
                     _node([holders[side], float(store_seat(vehicle.weapons[i]))]) for i, side in enumerate(sides)]
    gunner_seats(r, vehicle)
    if kind in TURRET_LIMITS:
        import make_artillery
        joint = make_artillery.turret_constraint(r)
        if dsgo.to_py(joint.items[1].items[1]) != [0.0]:
            raise ValueError(f'{kind}: the native turret hinge already has stops')
        joint.items[1].items[1] = _node(TURRET_LIMITS[kind])
    if kind in STATIC_TRACKS:
        r.get('tank_caterpillar_animation_info').items[0] = 0.0
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


def store_seat(weapon: str) -> int:
    """The gunner seat a weapon's store is on (vcobjects Store.seat), 0 the driver's."""
    store = vc.store_of(weapon)
    return vc.STORES[store[0]].seat if store else 0


GUNNER_SEAT_TEXT = '410_HELI_GUNNER_L'   # the seat's name key in the game's text table: "Gunner"
GUNNER_EYE = (0.0, 2.6, -7.0)           # m: a gunner seat's camera from its turret's pitch bone (behind it, over it)
GUNNER_LOOK = (0.0, 1.0, 6.0)           # ...and the point it looks at, ahead along its guns


def gunner_seats(r, vehicle: vc.GroundVehicle) -> None:  # noqa: ANN001 - dsgo node
    """The vehicle's gunner seats (vcobjects Store.seat, docs/multi-weapon-keys.md「独立座位」): seat n a copy of the
    driver's seat row (its door, its class mask and its numbers) with its own seat and camera locators on its turret's
    bones (Store.turret: the rider on the yaw bone, the camera and its look-at on the pitch bone; the local gunner's view
    is the plugin's own, src/seat_turret.cpp SeatTurretCamera, these the stock camera's when it is not). Seats in order,
    one a gunner seat number."""
    turrets = {}
    for w in vehicle.weapons:
        store = vc.store_of(w)
        spec = vc.STORES[store[0]] if store else None
        if spec and spec.seat:
            turrets.setdefault(spec.seat, spec.turret)
    if not turrets:
        return
    if sorted(turrets) != list(range(1, len(turrets) + 1)):
        raise ValueError(f'{vehicle.sgo}: gunner seats {sorted(turrets)} are not 1..n')
    seats = r.get('vehicle_riding_position')
    if len(seats.items) != 1:
        raise ValueError(f'{vehicle.sgo}: the native V603 has one seat, this has {len(seats.items)}')
    animation = r.get('animation_model')
    block = animation.items[2].data
    locators = {loc.name: loc for _i, loc in mab_legacy.mab_locators(block)}
    driver = dsgo.to_py(seats.items[0])
    eye = locators[driver[2][0]]
    looks = [n for n in locators if n != eye.name and n.startswith(eye.name) and locators[n].node == eye.node]
    if len(looks) != 1:
        raise ValueError(f'{vehicle.sgo}: the driver camera {eye.name!r} has {len(looks)} look-at locators')
    suffix = looks[0][len(eye.name):]
    seat_loc = locators[driver[1]]
    new = []
    for n in sorted(turrets):
        yaw, pitch = turrets[n].yaw, turrets[n].pitch
        row = copy.deepcopy(seats.items[0])
        names = (f'xm_seat_{n}', f'xm_cam_{n}')
        new.append(replace(seat_loc, name=names[0], node=yaw, a=(0.0, 0.0, 0.0, 1.0)))
        new.append(replace(eye, name=names[1], node=pitch, a=(*GUNNER_EYE, 1.0)))
        new.append(replace(locators[looks[0]], name=names[1] + suffix, node=pitch, a=(*GUNNER_LOOK, 1.0)))
        row.items[1] = names[0]
        row.items[2].items[0] = names[1]
        row.items[3] = GUNNER_SEAT_TEXT
        seats.items.append(row)
    animation.items[2] = dsgo.Blob(mab_legacy.mab_add_locators(block, new), animation.items[2].kind)


def _points(model, game: vc.Game) -> dict[str, list[tuple[str, tuple[float, float, float]]]]:
    points = model.muzzle_local(game)
    for side in ('left', 'right'):
        if side not in points:
            raise ValueError(f'{model.__name__}: missing {side} muzzle contract')
    return points


def model_missing(kind: str) -> bool:
    """An emplacement whose model folder (the Dreadnought's, for its secondary turrets) is not there: build() writes
    the stock V603_FLAK.SGO under its name, as make_dreadnought writes the stock V506 (call_weapons.MODEL_GROUND)."""
    return kind in EMPLACEMENTS and emplacement_model.source_folder(kind) is None


def check(files: dict[str, bytes], kind: str, game: vc.Game | None = None) -> None:
    vehicle = vc.GROUND_VEHICLES[kind]
    if set(files) == {f'OBJECT/{vehicle.sgo}.SGO'} and kind in EMPLACEMENTS:
        # the no-model fallback (model_missing): the stock flak tank under the emplacement's name, nothing else
        r = dsgo.parse(files[f'OBJECT/{vehicle.sgo}.SGO']).root
        if 'xgs_scene_object_class' not in r.names.values() or r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
            raise ValueError(f'{kind}: the no-model fallback is not the stock V603')
        if game is not None and files[f'OBJECT/{vehicle.sgo}.SGO'] != game.read('OBJECT', STOCK_VEHICLE):
            raise ValueError(f'{kind}: the no-model fallback differs from the stock {STOCK_VEHICLE}')
        return
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
    # [archive, model] as authored(); [archive, model, LOD table] once build() has made the LODs (model_lod.finish)
    if dsgo.to_py(r.get('animation_model'))[0][:2] != [f'app:/Object/{model.OUT_ARC.lower()}', model.HOST_MDB]:
        raise ValueError(f'{kind}: generated model is not selected')
    lod_faults = model_lod.check_files({f'OBJECT/{vehicle.sgo}.SGO': files[f'OBJECT/{vehicle.sgo}.SGO'],
                                        f'OBJECT/{model.OUT_ARC}': files[f'OBJECT/{model.OUT_ARC}']})
    if lod_faults:
        raise ValueError(f'{kind}: ' + '; '.join(lod_faults))
    if r.get('animation_model').items[1] != 'app:/' + animation_path(kind).lower():
        raise ValueError(f'{kind}: private fixed-optics animation is not selected')
    for index, path in enumerate(vehicle.weapons):
        blob = files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}']
        gun = dsgo.parse(blob).root
        if gun.get('AmmoCount') <= 0 or gun.get('AmmoDamage') <= 0:
            raise ValueError(f'{kind}: nonfunctional weapon {path}')
        muzzles = vc.mab_muzzles(gun.get('animation_model').items[2].data)
        side = slot(kind, index)
        if side in GUN_SLOTS and len(muzzles) != 1 or side == 'missiles' and len(muzzles) != 4 or not muzzles:
            raise ValueError(f'{kind}: {path} has {len(muzzles)} muzzle locators for {side}')
        if dsgo.write(dsgo.parse(blob)) != blob:
            raise ValueError(f'{kind}: weapon does not round-trip')
    if game is not None:
        original = dsgo.parse(game.read('OBJECT', STOCK_VEHICLE)).root
        info = dsgo.to_py(original.get('tank_caterpillar_animation_info'))
        want = [0.0, *info[1:]] if kind in STATIC_TRACKS else info
        if dsgo.to_py(r.get('tank_caterpillar_animation_info')) != want:
            raise ValueError(f'{kind}: track scroll {dsgo.to_py(r.get("tank_caterpillar_animation_info"))}, not {want}')
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
            if slot(kind, index) not in GUN_SLOTS:
                authored = slot_points(authored, len(positions))
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
        # The native pair (the first two), then the stores: a second belt on the same barrels, the missiles, an
        # X-Morph unit's other weapons.
        name, side = path.rsplit('/', 1)[1].upper(), slot(kind, i)
        if side not in GUN_SLOTS:
            files[f'WEAPON/{name}'] = store_weapon_sgo(game, points[side], name)
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
    if model_missing(kind):
        print(f'未找到无畏舰模型（{emplacement_model.MODEL_SUBDIR}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              f'{LABELS[kind]}先叫来普通的防空车；放好模型后重新安装即可。', flush=True)
        game = vc.Game(root)
        files = {f'OBJECT/{vc.GROUND_VEHICLES[kind].sgo}.SGO': game.read('OBJECT', STOCK_VEHICLE)}
        check(files, kind, game)
        return files
    # its seats' cockpits, then every model merged by material and given its LODs (pylib/model_lod.py); what is
    # written is held to the same contract as what was authored (the model's own parts, bones and materials)
    files = model_lod.finish(cockpits.graft_files(vc.Game(root), authored(root, kind)))
    check(files, kind)
    return files


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
