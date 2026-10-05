"""Writes the Katyusha rocket truck (pylib/vcobjects.py GROUND_VEHICLES['katyusha']) into <game>/Mods:

  Mods/OBJECT/EDF6VC_KATYUSHA.MRAB          the Naegling's model with the V607 truck in place of its hull and tracks,
                                            its rocket rack on the truck bed, the truck's tyres on its wheel bones
                                            (pylib/katyusha_model.py; built from the player's own Root.cpk)
  Mods/OBJECT/EDF6VC_KATYUSHA.SGO           the Naegling's vehicle (Vehicle402_Rocket: its turret, its wheels) with that
                                            model, the truck's wheel radii and the rockets below
  Mods/WEAPON/EDF6VC_KATYUSHA_ROCKETS.SGO   the Naegling's launcher made a multiple rocket launcher: unguided rockets on a
                                            ballistic arc (GrenadeBullet01, impact fuse, smoke trail), a 40-round ripple,
                                            a slow reload; LockonTargetType kMarkGround (EDF6AutoTurret: aimed by itself at
                                            ground targets on the rockets' arc, as the Bohr's launcher)

The request is tools/calls.py EDF6VC_CALL_KATYUSHA (tools/call_weapons.py, the Naegling's request as its template).
Built in memory first, then written atomically and recorded in the ledger as this tool's (pylib/ledger.py); --remove
releases them. No shared table is touched here.

  python tools/make_katyusha.py [game dir]            write / refresh
  python tools/make_katyusha.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import katyusha_model  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'katyusha'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['katyusha']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = f'{VEHICLE.sgo}.MRAB'
MODEL_MDB = 'Vehicle402_Rocket.mdb'   # the host's own model file name, inside the archive
STOCK_WEAPON = 'V_402ROCKET_ROCKETCANNON.SGO'
# The truck's tyres (pylib/katyusha_model.py: each axle's tyres on two of the Naegling's wheel bones, the hubs 0.534 m
# up): car_base_wheel [8] is a wheel's radius, the hub's height over the ground (the Naegling's 0.528 / 0.452).
TYRE_RADIUS = 0.534
# EDF6AutoTurret's ground-attack mark (autoturret/src/turret.h kMarkGround): the plugin aims the gun at ground
# targets, lobbed on the round's arc.
MARK_GROUND = 7302.0
# The rockets (BM-21 Grad, cut to the game's world): ~180 m/s off the rail on a ballistic arc (about 2 km at 45 deg in
# the game's gravity), a 40-round ripple 4 frames apart, 10 s to reload; base values the request's tier multiplies.
ROCKETS = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoSpeed': 3.0, 'AmmoGravityFactor': 1.0, 'AmmoAlive': 900.0, 'AmmoOwnerMove': 0.0,
    'AmmoDamage': 300.0, 'AmmoExplosion': 12.0, 'AmmoHitImpulseAdjust': 0.3, 'AmmoSize': 1.5, 'AmmoCount': 160.0,
    'FireCount': 1.0, 'FireBurstCount': 40.0, 'FireBurstInterval': 4.0, 'FireInterval': 600.0, 'FireAccuracy': 0.035,
    # EDF6AutoTurret's gun lock-on (autoturret/tools/build.py gun_lockon): no lock (type 0, range 0), its mark in
    # LockonTargetType, distribution 0.
    'LockonType': 0.0, 'LockonTargetType': MARK_GROUND, 'LockonRange': 0.0, 'Lockon_DistributionType': 0.0,
    # GrenadeBullet01: [0] 0 = burst on impact, [3] 0 = no bounce, [4] / [5] its smoke trail (param, frames).
    'Ammo_CustomParameter': [0.0, -0.004, 1.0, 0.0, 0.05, 40.0],
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
    for key, value in ROCKETS.items():
        if r.get(key) is None:
            raise ValueError(f'{STOCK_WEAPON} 缺少 {key}')
        r.set(key, _node(value))
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    return dsgo.write(doc)


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
    m['game_object_durability'] = VEHICLE.durability
    return sgo.write(version, m)


def check(files: dict[str, bytes]) -> None:
    """The SGO names this tool's model and weapon, its wheels are the model's bones, and the weapon is marked."""
    from mdb import mdb_read, rab_read
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    assert v['animation_model'][0] == [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB], v['animation_model'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{MODEL_FILE}']).files if f.name.lower() == MODEL_MDB.lower()).data)
    names = {md.name_of(b.name) for b in md.bones}
    missing = {w[0] for w in v['car_base_wheel']} - names
    assert not missing, f'model lacks wheel bones {missing}'
    w = dsgo.to_py(dsgo.parse(files[f'WEAPON/{vc.KATYUSHA_ROCKETS}']).root)
    assert w['LockonTargetType'] == MARK_GROUND and w['LockonType'] == 0.0 and w['AmmoClass'] == 'GrenadeBullet01', w


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
