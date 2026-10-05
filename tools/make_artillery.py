"""Writes the self-propelled artillery (pylib/vcobjects.py GROUND_VEHICLES['artillery']) into <game>/Mods:

  Mods/OBJECT/EDF6VC_ARTILLERY.MRAB    the KG6 Kepler's model with its turret replaced: the E551's turret housing and the
                                       Armed Barga's two back cannons side by side on it, each gun on the Kepler's gun
                                       bones (elevation, recoil) (pylib/artillery_model.py; built from the player's own
                                       Root.cpk)
  Mods/OBJECT/EDF6VC_ARTILLERY.SGO     the Kepler's vehicle (Vehicle603_Flak: its turret, its twin guns) with that model
                                       and the guns below
  Mods/WEAPON/EDF6VC_HOWITZER_L/R.SGO  the Kepler's guns made howitzers: a large high-explosive shell each on a ballistic
                                       arc (GrenadeBullet01, impact fuse), both firing together (two shells a salvo),
                                       a slow reload; LockonTargetType kMarkGround (EDF6AutoTurret aims them at ground
                                       targets on the shells' arc)

The request is tools/calls.py EDF6VC_CALL_ARTILLERY (tools/call_weapons.py, the Kepler's request as its template).
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_artillery.py [game dir]            write / refresh
  python tools/make_artillery.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import artillery_model  # noqa: E402
import dsgo  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'artillery'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['artillery']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = f'{VEHICLE.sgo}.MRAB'
MODEL_MDB = 'v603_flak.mdb'   # the host's own model file name, inside the archive
STOCK_GUNS = ('V603_FLAK_GUN01_L.SGO', 'V603_FLAK_GUN01_R.SGO')
MARK_GROUND = 7302.0   # EDF6AutoTurret's ground-attack mark (autoturret/src/turret.h kMarkGround)
SHELL_MODEL = 'app:/WEAPON/bullet_grenade.rab'
# A 155 mm-class shell cut to the game's world: ~240 m/s off the muzzle on a ballistic arc (about 4 km at 45 deg in the
# game's gravity: the world's whole width), a 25 m blast; one a gun every 5 s, both guns together. Base values the
# request's tier multiplies.
SHELLS = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoModel': SHELL_MODEL, 'AmmoSpeed': 4.0, 'AmmoGravityFactor': 1.0,
    'AmmoAlive': 1200.0, 'AmmoOwnerMove': 0.0, 'AmmoDamage': 2500.0, 'AmmoExplosion': 25.0, 'AmmoHitImpulseAdjust': 0.5,
    'AmmoSize': 1.2, 'AmmoHitSizeAdjust': 1.0, 'AmmoIsPenetration': 0.0, 'AmmoCount': 40.0,
    'FireCount': 1.0, 'FireBurstCount': 1.0, 'FireInterval': 300.0, 'FireAccuracy': 0.01, 'FireRecoil': 2.0,
    'AmmoColor': [3.0, 2.0, 1.0, 1.0],
    # GrenadeBullet01: [0] 0 = burst on impact, [3] 0 = no bounce, [4] / [5] its trail (param, frames).
    'Ammo_CustomParameter': [0.0, -0.004, 1.0, 0.0, 0.05, 20.0],
    'AmmoHitSe': [0.0, 'common_damages_explode_L', 1.0, 1.0, 1.0, 400.0],
    'FireSe': [0.0, 'weapon_VHC_tank403_cannon', 1.0, 1.0, 1.0, 120.0],
    # EDF6AutoTurret's gun lock-on (autoturret/tools/build.py gun_lockon).
    'LockonType': 0.0, 'LockonTargetType': MARK_GROUND, 'LockonRange': 0.0, 'Lockon_DistributionType': 0.0,
}
NAMES = {'ja': '榴弾砲', 'en': 'Howitzer', 'cn': '榴彈砲', 'kr': 'Howitzer', 'sc': '榴弹炮'}


def _node(v: object) -> object:
    """A python value as dsgo writes it: lists as Nodes, ints as floats."""
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    if isinstance(v, int) and not isinstance(v, bool):
        return float(v)
    return v


def howitzer_sgo(game: vc.Game, stock: str) -> bytes:
    doc = dsgo.parse(game.read('WEAPON', stock))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Weapon_VehicleShoot':
        raise ValueError(f'{stock} 不是预期的 Kepler 炮')
    for key, value in SHELLS.items():
        if r.get(key) is None:
            raise ValueError(f'{stock} 缺少 {key}')
        r.set(key, _node(value))
    res = r.get('resource')
    if SHELL_MODEL not in res.items:
        res.items.append(SHELL_MODEL)
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    return dsgo.write(doc)


def vehicle_sgo(game: vc.Game) -> bytes:
    doc = dsgo.parse(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 Vehicle603_Flak')
    model = r.get('animation_model')
    model.items[0].items[0] = f'app:/Object/{MODEL_FILE.lower()}'
    model.items[0].items[1] = MODEL_MDB
    setup = r.get('vehicle_setup')
    guns = setup.items[2].items
    if len(guns) != len(VEHICLE.weapons):
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(guns)} guns, the artillery has {len(VEHICLE.weapons)}')
    for entry, path in zip(guns, VEHICLE.weapons):
        entry.items[0] = path
    r.set('game_object_durability', VEHICLE.durability)
    return dsgo.write(doc)


def check(files: dict[str, bytes]) -> None:
    """The SGO names this tool's model and guns, its guns' bones are the model's, and the guns are marked."""
    from mdb import mdb_read, rab_read
    v = dsgo.to_py(dsgo.parse(files[f'OBJECT/{SGO_FILE}']).root)
    assert v['animation_model'][0] == [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB], v['animation_model'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{MODEL_FILE}']).files if f.name.lower() == MODEL_MDB.lower()).data)
    names = {md.name_of(b.name) for b in md.bones}
    missing = {b for b, _ in v['vehicle_weapon_setting']} - names
    assert not missing, f'model lacks gun bones {missing}'
    for w in VEHICLE.weapons:
        g = dsgo.to_py(dsgo.parse(files[f'WEAPON/{w.split("/")[-1].upper()}']).root)
        assert g['LockonTargetType'] == MARK_GROUND and g['LockonType'] == 0.0 and g['AmmoClass'] == 'GrenadeBullet01', g


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    arc = artillery_model.build(game)
    artillery_model.check(arc)
    files = {f'OBJECT/{MODEL_FILE}': arc, f'OBJECT/{SGO_FILE}': vehicle_sgo(game)}
    for stock, path in zip(STOCK_GUNS, VEHICLE.weapons):
        files[f'WEAPON/{path.split("/")[-1].upper()}'] = howitzer_sgo(game, stock)
    check(files)
    return files


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
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
