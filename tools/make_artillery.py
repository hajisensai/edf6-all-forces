"""Writes the self-propelled artillery (pylib/vcobjects.py GROUND_VEHICLES['artillery']) into <game>/Mods:

  Mods/OBJECT/EDF6VC_ARTILLERY.MRAB    the user's twin-gun tank model (models/twin_tank/twin_tank.obj: an E551 hull,
                                       a Balam turret and barrels with its texture) on the KG6 Kepler's skeleton
                                       (pylib/artillery_model.py; the chassis is the stock E551's own meshes,
                                       materials and textures from the player's own Root.cpk). Without the model
                                       folder it is not made, and the vehicle keeps the Kepler's own model.
  Mods/OBJECT/EDF6VC_ARTILLERY.SGO     the Kepler's vehicle (Vehicle603_Flak: its turret, its twin guns) with that model
                                       and the guns below
  Mods/WEAPON/EDF6VC_HOWITZER_L/R.SGO  the Kepler's guns made howitzers: a large high-explosive shell each on a ballistic
                                       arc (GrenadeBullet01, impact fuse), both firing together (two shells a salvo),
                                       a slow reload; LockonTargetType kMarkGround (EDF6AutoTurret aims them at ground
                                       targets on the shells' arc). Shell and ejected casing sized to the model's
                                       bore (BORE).

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
# The calibre: the twin tank's muzzle mouth, inscribed diameter (artillery_model.Barrel.bore, 0.350 m; build() checks
# the model still has it). The shell (bullet_grenade.rab: 0.063 m across, 0.139 m long at AmmoSize 1, scaled
# uniformly; the stock Barga / E551 DLC cannons use it at 5) is 90 % of the bore across, and its contact sphere
# (AmmoSize x AmmoHitSizeAdjust) stays the 1.2 m it was. The casing the gun throws out (the Kepler gun's ShellCase:
# ShellCase401l.rab, 0.070 m across, 0.206 m long, a Havok body) is scaled by ShellCase_CustomParameter {scale}
# (stock: the Galleon cannons' 2.0) to the same 90 % of the bore.
BORE = 0.35
SHELL_SIZE = round(0.9 * BORE / 0.063, 2)          # 5.0
CASE_SCALE = round(0.9 * BORE / 0.070, 2)          # 4.5
HIT_SPHERE = 1.2
CASE_MODEL = 'app:/Weapon/ShellCase401l.rab'
# A 155 mm-class shell cut to the game's world: ~240 m/s off the muzzle on a ballistic arc (about 4 km at 45 deg in the
# game's gravity: the world's whole width), a 25 m blast; one a gun every 5 s, both guns together. Base values the
# request's tier multiplies.
SHELLS = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoModel': SHELL_MODEL, 'AmmoSpeed': 4.0, 'AmmoGravityFactor': 1.0,
    'AmmoAlive': 1200.0, 'AmmoOwnerMove': 0.0, 'AmmoDamage': 2500.0, 'AmmoExplosion': 25.0, 'AmmoHitImpulseAdjust': 0.5,
    'AmmoSize': SHELL_SIZE, 'AmmoHitSizeAdjust': round(HIT_SPHERE / SHELL_SIZE, 4), 'AmmoIsPenetration': 0.0,
    'AmmoCount': 40.0,
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
    if dsgo.to_py(r.get('ShellCase'))[1] != CASE_MODEL:
        raise ValueError(f'{stock} 的弹壳不是 {CASE_MODEL}')
    r.set('ShellCase_CustomParameter', dsgo.Node([CASE_SCALE], {0: 'scale'}))
    res = r.get('resource')
    if SHELL_MODEL not in res.items:
        res.items.append(SHELL_MODEL)
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    return dsgo.write(doc)


def vehicle_sgo(game: vc.Game, own_model: bool | None = None) -> bytes:
    """The vehicle: the Kepler's, with this tool's model when `own_model` (else the Kepler's own), and its guns.
    None: as install writes it (the model with the twin tank's folder there): what the test range places
    (testrange/gen.py GROUND_MISSION calls this with the game alone)."""
    if own_model is None:
        own_model = artillery_model.model_dir() is not None
    doc = dsgo.parse(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 Vehicle603_Flak')
    if own_model:
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
    """The SGO names this tool's model when it made one (else the Kepler's) and its guns; the model has the guns'
    bones and the track materials / parameter the SGO scrolls; the guns are marked, their shells and casings sized
    to BORE."""
    from mdb import mdb_read, rab_read
    v = dsgo.to_py(dsgo.parse(files[f'OBJECT/{SGO_FILE}']).root)
    own = f'OBJECT/{MODEL_FILE}' in files
    want = [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB] if own else ['app:/Object/v603_flak.mrab', MODEL_MDB]
    assert v['animation_model'][0] == want, v['animation_model'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    if own:
        md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{MODEL_FILE}']).files
                           if f.name.lower() == MODEL_MDB.lower()).data)
        missing = {b for b, _ in v['vehicle_weapon_setting']} - {md.name_of(b.name) for b in md.bones}
        assert not missing, f'model lacks gun bones {missing}'
        # the tracks scroll: every (material, parameter) the SGO's tank_caterpillar_animation animates is in the model
        params = {(md.name_of(m.name), p.name) for m in md.materials for p in m.params}
        scroll = [tuple(x) for x in v['tank_caterpillar_animation']]
        assert scroll and not set(scroll) - params, f'model lacks the scrolled track materials {set(scroll) - params}'
    for w in VEHICLE.weapons:
        g = dsgo.to_py(dsgo.parse(files[f'WEAPON/{w.split("/")[-1].upper()}']).root)
        assert g['LockonTargetType'] == MARK_GROUND and g['LockonType'] == 0.0 and g['AmmoClass'] == 'GrenadeBullet01', g
        assert abs(g['AmmoSize'] * 0.063 - 0.9 * BORE) < 0.01, g['AmmoSize']
        assert abs(g['AmmoSize'] * g['AmmoHitSizeAdjust'] - HIT_SPHERE) < 1e-3, g['AmmoHitSizeAdjust']
        assert g['ShellCase'][1] == CASE_MODEL, g['ShellCase']
        assert abs(g['ShellCase_CustomParameter']['scale'] * 0.070 - 0.9 * BORE) < 0.01, g['ShellCase_CustomParameter']


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    twin tank's model folder (artillery_model.model_dir(); without it no model is made, with a message)."""
    game = vc.Game(root)
    folder = artillery_model.model_dir()
    files: dict[str, bytes] = {}
    if folder is None:
        print('  没有找到双管坦克模型（models/twin_tank/twin_tank.obj），自行榴弹炮先用 Kepler 原版外形。')
    else:
        arc, _md, info = artillery_model.build_with_info(game, folder)
        artillery_model.check(arc)
        for b in info['barrels'].values():
            if abs(b.bore - BORE) > 0.02:
                raise ValueError(f'炮口内径 {b.bore:.3f} m，炮弹按 {BORE} m 设计：请更新 BORE')
        files[f'OBJECT/{MODEL_FILE}'] = arc
    files[f'OBJECT/{SGO_FILE}'] = vehicle_sgo(game, folder is not None)
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
