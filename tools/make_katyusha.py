"""Writes the Katyusha rocket truck (pylib/vcobjects.py GROUND_VEHICLES['katyusha']) into <game>/Mods:

  Mods/OBJECT/EDF6VC_KATYUSHA.MRAB          the Naegling's model with the V607 truck in place of its hull and tracks,
                                            its rocket rack on the truck bed (a BM-13 rail pack: 8 rails, 16 rockets
                                            in place of the Naegling's box), the truck's tyres on its wheel bones
                                            (pylib/katyusha_model.py; built from the player's own Root.cpk), its
                                            elevation ram telescopic (a rod bone of its own)
  Mods/OBJECT/EDF6VC_KATYUSHA.SGO           the Naegling's vehicle (Vehicle402_Rocket: its turret, its wheels) with that
                                            model, the truck's wheel radii and the rockets below
  Mods/WEAPON/EDF6VC_KATYUSHA_ROCKETS.SGO   the Naegling's launcher made a multiple rocket launcher: unguided rockets (the
                                            Goliath's rocket model at the M-13's length) leaving from the 16 rockets on
                                            the rails (a muzzle at each, fired in order: each rocket leaves its rail)
                                            on a ballistic arc (GrenadeBullet01, impact fuse, smoke trail), a 16-round
                                            salvo, 10 of them; LockonTargetType kMarkLofted
                                            (EDF6AutoTurret: an NPC crew's aimed by itself at ground targets on the
                                            rockets' high arc; EDF6VehicleCrew: the player's lifted onto the arc to where
                                            the camera looks, and the impact point on the HUD while the player rides it)

The request is tools/calls.py EDF6VC_CALL_KATYUSHA (tools/call_weapons.py, the Naegling's request as its template).
The vehicle's launcher elevates to PITCH_STOP_DEG (the Naegling's 50 raised) so the rockets can be lobbed on the high
arc; the rockets' speed and life fit that arc (pylib/ballistics.py envelope, check()). The model's elevation ram is
telescopic for that range (pylib/katyusha_model.py RAM_ROD; EDF6VehicleCrew src/katyusha.cpp poses it), and the
player aims with the camera while EDF6VehicleCrew lifts the launcher alone onto the arc (src/katyusha.cpp).

Built in memory first, then written atomically and recorded in the ledger as this tool's (pylib/ledger.py); --remove
releases them. No shared table is touched here.

  python tools/make_katyusha.py [game dir]            write / refresh
  python tools/make_katyusha.py [game dir] --remove   release them
"""
from __future__ import annotations

import math
import os
import struct
import sys
from dataclasses import replace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ballistics  # noqa: E402
import dsgo  # noqa: E402
import katyusha_model  # noqa: E402
import ledger  # noqa: E402
import mab  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'katyusha'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['katyusha']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = f'{VEHICLE.sgo}.MRAB'
MODEL_MDB = 'Vehicle402_Rocket.mdb'   # the host's own model file name, inside the archive
STOCK_WEAPON = 'V_402ROCKET_ROCKETCANNON.SGO'
MUZZLE_NODE = 'v_Null'   # the weapon model's root: its muzzles hang on it (set_muzzles)
STOCK_MUZZLES = 10       # the Naegling's: 01..10 on MUZZLE_NODE
# The truck's tyres (pylib/katyusha_model.py: each axle's tyres on two of the Naegling's wheel bones, the hubs 0.534 m
# up): car_base_wheel [8] is a wheel's radius, the hub's height over the ground (the Naegling's 0.528 / 0.452).
TYRE_RADIUS = 0.534
# EDF6AutoTurret's lofted mark (autoturret/src/turret.h kMarkLofted; EDF6VehicleCrew src/launcher.cpp reads it too):
# EDF6AutoTurret aims an NPC crew's launcher at ground targets on the rockets' HIGH arc (the low one when the high one
# is past the elevation stop); the player's it leaves alone: EDF6VehicleCrew lifts it onto that arc to the ground
# point the camera looks at (src/katyusha.cpp) and shows where the rockets land. A stock game, or an EDF6AutoTurret
# older than the mark, fires the launcher as any no-lock gun.
MARK_LOFTED = 7303.0
# The launcher's elevation stop, degrees up: car_base_constraint_data's Rocketcannon_main hinge limit [1, -stop, 0]
# (pitch negative-up; the Naegling's -50 lifts the rack 50 deg, the V603 flak's [1, -60, 5] is its aim axis
# -1.047..0.087 rad, autoturret/docs/re-notes.md). 80 deg leaves the high arc 1/3..1 of the most range; the rack
# swung there stays 0.36 m over the truck bed (pylib/katyusha_model.py build_model: the rack's pivot is at its rear).
# One copy, the model's: its telescopic ram is built for this stroke (katyusha_model.check_ram holds it together
# from 0 up to it).
PITCH_STOP_DEG = katyusha_model.PITCH_STOP_DEG
STOCK_PITCH_LIMIT = [1.0, -50.0, 0.0]
# The camera (game_object_camera_setting [0] look-at / [1] eye, pylib/vcobjects.py camera_view): the Naegling's level
# (0, 4, 0) / (0, 4, -10.5) raised over the launcher, looking down onto the ground ahead.
CAMERA = ([0.0, 4.5, 4.0], [0.0, 8.5, -15.0])
# The rockets (BM-21 Grad, cut to the game's world): 120 m/s off the rail (2.0 m a frame: the game's muzzle speed is
# AmmoSpeed x 60, ballistics.py), lobbed: the most range ~980 m at 45 deg, the high arc from ~335 m (at the 80 deg
# stop) out to it, 11.5..16 s in the air on flat ground (ballistics.envelope, in the game's measured 14.7 m/s^2);
# they live 25 s, so a lofted rocket never expires in the air, even onto a target well below. A salvo is the rack's
# 16 rockets (FireBurstCount = the model's ROCKET_COUNT, one per muzzle: the weapon fires muzzle n % 16 for its n-th
# round, so a salvo takes every rocket off its rail once, in order, and EDF6VehicleCrew shows the rack emptying,
# src/katyusha.cpp), 4 frames apart; the next one 4 s after its last rocket (FireInterval 240, the Naegling's own),
# while the rack is loaded again. AmmoCount 160 = 10 salvos, the load as before (the stock ReloadTime -1 kept: spent
# for good, as every stock vehicle's): the 40-round ripples every 10 s were 160 rockets in 4 salvos, ~0.053 rockets a
# frame firing on; 16 every 4 s after a 1 s salvo is the same rate and the same damage per rocket.
# FireAccuracy is the cone's half angle in radians (fire 0x691B02 -> 0x4E820: polar angle uniform in [0, it]): 0.02
# spreads a salvo ~28 m either side at 45 deg, ~38 m at the stop.
# The look: the Goliath's / Grant's unguided rocket (WEAPON/bullet_rocket.rab, the stock rocket launchers' AmmoModel)
# with a long smoke trail. The class stays GrenadeBullet01: no motor or guidance (MissileBullet01, the class the stock
# rocket launchers fire, accelerates and steers by its CustomParameter), gravity on its velocity as above, an impact
# fuse, and the round the plugins already know. Base values the request's tier multiplies: its vehicle setup's
# [durability, damage] multipliers (tools/call_weapons.py request_tier), not the speed.
# Its size: AmmoSize scales the model uniformly (tools/make_artillery.py, the shell): bullet_rocket.rab is a slim
# finned rocket ROCKET_MODEL_LEN long at size 1, so ROCKET_SIZE flies the M-13's length (katyusha_model.ROCKET_LEN,
# the rockets on the rails; rockets_sgo measures the stock model), and AmmoHitSizeAdjust keeps its contact sphere
# (AmmoSize x AmmoHitSizeAdjust) the ROCKET_CONTACT it was at size 1.5.
ROCKET_MODEL = 'app:/WEAPON/bullet_rocket.rab'
ROCKET_MODEL_LEN = vc.STORE_MODELS['bullet_rocket']   # 1.052: the jets' Hydra 70 flies the same model
ROCKET_SIZE = round(katyusha_model.ROCKET_LEN / ROCKET_MODEL_LEN, 2)   # 1.34
ROCKET_CONTACT = 1.5
ROCKETS = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoModel': ROCKET_MODEL, 'AmmoSpeed': 2.0, 'AmmoGravityFactor': 1.0,
    'AmmoAlive': 1500.0, 'AmmoOwnerMove': 0.0, 'AmmoDamage': 300.0, 'AmmoExplosion': 12.0, 'AmmoHitImpulseAdjust': 0.3,
    'AmmoSize': ROCKET_SIZE, 'AmmoHitSizeAdjust': round(ROCKET_CONTACT / ROCKET_SIZE, 4), 'AmmoCount': 10.0 * katyusha_model.ROCKET_COUNT, 'FireCount': 1.0,
    'FireBurstCount': float(katyusha_model.ROCKET_COUNT), 'FireBurstInterval': 4.0, 'FireInterval': 240.0, 'FireAccuracy': 0.02,
    # EDF6AutoTurret's gun lock-on (autoturret/tools/build.py gun_lockon): no lock (type 0, range 0), its mark in
    # LockonTargetType, distribution 0.
    'LockonType': 0.0, 'LockonTargetType': MARK_LOFTED, 'LockonRange': 0.0, 'Lockon_DistributionType': 0.0,
    # GrenadeBullet01: [0] 0 = burst on impact, [3] 0 = no bounce, [4] / [5] its smoke trail (param, frames): the
    # motor's smoke hangs 2 s behind it.
    'Ammo_CustomParameter': [0.0, -0.004, 1.0, 0.0, 0.05, 120.0],
}
NAMES = {'ja': 'カチューシャ ロケット弾', 'en': 'Katyusha Rockets', 'cn': '喀秋莎火箭彈', 'kr': 'Katyusha Rockets',
         'sc': '喀秋莎火箭弹'}


def _node(v: object) -> object:
    """A python value as dsgo writes it: lists as Nodes, ints as floats."""
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    if isinstance(v, int) and not isinstance(v, bool):
        return float(v)
    return v


def rockets_sgo(game: vc.Game) -> bytes:
    doc = dsgo.parse(game.read('WEAPON', STOCK_WEAPON))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Weapon_VehicleShoot' or r.get('AmmoClass') != 'MissileBullet01':
        raise ValueError(f'{STOCK_WEAPON} 不是预期的 Naegling 发射器')
    length = vc.model_length(game.read('WEAPON', ROCKET_MODEL.rsplit('/', 1)[1].upper()))   # KeyError: not there
    if abs(length * ROCKET_SIZE - katyusha_model.ROCKET_LEN) > 0.02:
        raise ValueError(f'{ROCKET_MODEL} 长 {length:.3f} m，AmmoSize {ROCKET_SIZE} 飞出去不是 M-13 的长度')
    stock_model = r.get('AmmoModel')
    for key, value in ROCKETS.items():
        if r.get(key) is None:
            raise ValueError(f'{STOCK_WEAPON} 缺少 {key}')
        r.set(key, _node(value))
    # The weapon's preload list: the rocket model in the missile model's place (the game loads what it names).
    res = r.get('resource')
    res.items = [x for x in res.items if str(x).lower() != str(stock_model).lower()]
    res.items.insert(0, ROCKET_MODEL)
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    model = r.get('animation_model')
    stock_z = {struct.unpack_from('<f', model.items[2].data, at + 8)[0] for _n, _node, at in vc.mab_muzzles(model.items[2].data)}
    model.items[2] = dsgo.Blob(set_muzzles(model.items[2].data), model.items[2].kind)
    # The back blast stays where it was, at the rails' back end: MuzzleFlash_CustomParameter [4] is its offset along
    # the muzzle (the stock -4.6 puts the Naegling's at its box's back face, 4.6 m behind the box's front, where its
    # muzzles were); the muzzles moved back from there to the rockets' tails, so the offset shrinks by as much.
    flash = r.get('MuzzleFlash_CustomParameter').items[4]
    if len(stock_z) != 1 or [float(v) for v in flash.items[:2]] != [0.0, 0.0]:
        raise ValueError(f'{STOCK_WEAPON} 的炮口 z {stock_z} / 尾焰偏移 {flash.items} 不是预期的')
    flash.items[2] = round(stock_z.pop() + float(flash.items[2]) - katyusha_model.ROCKET_TAIL, 4)
    return dsgo.write(doc)


def set_muzzles(block: bytes) -> bytes:
    """The Naegling launcher's MAB block with its 10 muzzles (vcobjects.mab_muzzles: 5 x 2 on the box's front face,
    on the weapon model's root `v_Null`, which hangs on Rocketcannon_main: vehicle_weapon_setting) replaced by the
    Katyusha's 16 (katyusha_model.MUZZLES: one at each rocket's tail, in the order they are fired, the launcher's
    frame), each a copy of the stock 01 but its name and position. The block is written anew (pylib/mab.py): the
    stock one has records for 10 only."""
    m = mab.mab_read(block)
    found = [(x.name, x.node) for x in m.locators]
    if found != [(f'{n + 1:02d}', MUZZLE_NODE) for n in range(STOCK_MUZZLES)]:
        raise ValueError(f'{STOCK_WEAPON} 的炮口不是预期的 01..{STOCK_MUZZLES:02d} / {MUZZLE_NODE}: {found}')
    m.locators = [replace(m.locators[0], name=name, pos=(*p, 1.0)) for name, p in katyusha_model.MUZZLES]
    return mab.mab_write(m)


def _value(v: object) -> object:
    """An SGO value with its floats as python floats (sgo.Float -> float), for comparing."""
    if isinstance(v, list):
        return [_value(x) for x in v]
    return v.value if isinstance(v, sgo.Float) else v


def _pitch_limit(constraints: list) -> list:  # noqa: ANN001 - sgo values
    """The Rocketcannon_main hinge's limit node [1, -up, down] (degrees, pitch negative-up)."""
    hinge = [c for c in constraints if c[0] == 'Rocketcannon_main']
    if len(hinge) != 1:
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(hinge)} Rocketcannon_main constraints')
    return hinge[0][3][1][1]


def _raise_launcher(constraints: list) -> None:  # noqa: ANN001 - sgo values
    """The launcher elevates to PITCH_STOP_DEG (the stock Naegling's hinge stops at 50 deg)."""
    limit = _pitch_limit(constraints)
    if _value(limit) != STOCK_PITCH_LIMIT:
        raise ValueError(f'{VEHICLE.stock}.SGO: Rocketcannon_main limit {_value(limit)}, expected {STOCK_PITCH_LIMIT}')
    limit[1] = -PITCH_STOP_DEG


def vehicle_sgo(game: vc.Game) -> bytes:
    version, m = sgo.read(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    if m.get('xgs_scene_object_class') != 'Vehicle402_Rocket':
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 Vehicle402_Rocket')
    model = m['animation_model']
    m['animation_model'] = [[f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB], model[1], model[2]]
    setup = m['vehicle_setup']
    if len(setup[2]) != len(VEHICLE.weapons):
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(setup[2])} weapons, the Katyusha has {len(VEHICLE.weapons)}')
    setup[2] = [[w, entry[1], entry[2]] for w, entry in zip(VEHICLE.weapons, setup[2])]
    for wheel in m['car_base_wheel']:
        wheel[8] = TYRE_RADIUS
    _raise_launcher(m['car_base_constraint_data'])
    cam = m['game_object_camera_setting']
    m['game_object_camera_setting'] = [list(CAMERA[0]), list(CAMERA[1]), cam[2], cam[3]]
    m['game_object_durability'] = VEHICLE.durability
    return sgo.write(version, m)


def check(files: dict[str, bytes]) -> None:
    """The SGO names this tool's model and weapon, its wheels are the model's bones, its launcher elevates to the stop,
    and the weapon is marked, looks like a rocket and lives long enough for its high arc."""
    from mdb import mdb_read, rab_read
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    assert v['animation_model'][0] == [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB], v['animation_model'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{MODEL_FILE}']).files if f.name.lower() == MODEL_MDB.lower()).data)
    names = {md.name_of(b.name) for b in md.bones}
    missing = {w[0] for w in v['car_base_wheel']} - names
    assert not missing, f'model lacks wheel bones {missing}'
    assert _value(_pitch_limit(v['car_base_constraint_data'])) == [1.0, -PITCH_STOP_DEG, 0.0]
    cam = _value(v['game_object_camera_setting'])
    assert [cam[0], cam[1]] == [list(CAMERA[0]), list(CAMERA[1])], cam
    vc.check_artillery_camera(CAMERA)
    w = dsgo.to_py(dsgo.parse(files[f'WEAPON/{vc.KATYUSHA_ROCKETS}']).root)
    assert w['LockonTargetType'] == MARK_LOFTED and w['LockonType'] == 0.0 and w['AmmoClass'] == 'GrenadeBullet01', w
    assert w['AmmoModel'] == ROCKET_MODEL and ROCKET_MODEL in w['resource'], (w['AmmoModel'], w['resource'])
    assert 'app:/WEAPON/bullet_missile.rab' not in w['resource'], w['resource']
    assert abs(w['AmmoSize'] * w['AmmoHitSizeAdjust'] - ROCKET_CONTACT) < 1e-3, (w['AmmoSize'], w['AmmoHitSizeAdjust'])
    # The muzzles are the model's: one at each rocket's tail, in the order of the rockets' bones (katyusha_model.
    # check_launcher finds each rocket on its bone there), as the game reads them (vcobjects.mab_muzzles), all on the
    # weapon model's root; a salvo is one round from each, a load whole salvos (the weapon fires muzzle n % count for
    # its n-th round since its full load: every salvo takes the rockets in the same order).
    block = dsgo.parse(files[f'WEAPON/{vc.KATYUSHA_ROCKETS}']).root.get('animation_model').items[2].data
    assert mab.mab_write(mab.mab_read(block)) == block
    found = vc.mab_muzzles(block)
    got = [(n, struct.unpack_from('<3f', block, at)) for n, _node, at in found]
    assert {node for _n, node, _at in found} == {MUZZLE_NODE}, found
    assert [n for n, _ in got] == [n for n, _ in katyusha_model.MUZZLES] and all(
        max(abs(a - b) for a, b in zip(p, q)) < 1e-5 for (_n, p), (_m, q) in zip(got, katyusha_model.MUZZLES)), got
    katyusha_model.check_launcher(md, got)
    assert w['FireBurstCount'] == len(got) == katyusha_model.ROCKET_COUNT, (w['FireBurstCount'], len(got))
    assert w['AmmoCount'] > 0 and w['AmmoCount'] % w['FireBurstCount'] == 0, w['AmmoCount']
    # ...and the round leaves where its rocket sat: bullet_rocket.rab runs from its origin (the muzzle) forward to its
    # nose, ROCKET_LEN at AmmoSize (rockets_sgo measured it), as the rocket runs from its tail (the muzzle).
    assert abs(w['AmmoSize'] * ROCKET_MODEL_LEN - katyusha_model.ROCKET_LEN) < 0.02, w['AmmoSize']
    # The high arc fits: a rocket at the elevation stop lands (on flat ground) well inside its life, and the high arc
    # spans a useful band of the range.
    env = ballistics.envelope(w['AmmoSpeed'], ballistics.drop_per_frame(factor=w['AmmoGravityFactor']),
                              math.radians(PITCH_STOP_DEG))
    assert env['high_min_time'] * ballistics.FPS * 1.25 < w['AmmoAlive'], (env, w['AmmoAlive'])
    assert env['high_min_range'] < 0.5 * env['max_range'], env


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    arc = katyusha_model.build(game)
    katyusha_model.check(arc)
    files = {f'OBJECT/{MODEL_FILE}': arc, f'OBJECT/{SGO_FILE}': vehicle_sgo(game),
             f'WEAPON/{vc.KATYUSHA_ROCKETS}': rockets_sgo(game)}
    check(files)
    return files


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
