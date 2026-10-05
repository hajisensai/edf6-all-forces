"""Writes the drill tank (pylib/vcobjects.py GROUND_VEHICLES['drill'], src/drill.cpp) into <game>/Mods:

  Mods/OBJECT/EDF6VC_DRILL.MRAB         EDF: Iron Rain's drill tank on the Blacker's skeleton, its drill on a bone of its
                                        own (pylib/drill_model.py; built from the user's OBJ and the player's own
                                        Root.cpk)
  Mods/OBJECT/EDF6VC_DRILL.SGO          the Blacker's vehicle (Vehicle505_Tank: tracks, one weapon holder) with that model,
                                        DRILL_DURABILITY base, its camera raised and pulled back for the taller hull
  Mods/WEAPON/EDF6VC_DRILL_BIT.SGO      its one weapon, the Blacker's cannon made to fire nothing that shows or hurts (the
                                        plugin takes the player's trigger for the drill; an NPC driver's AI may still pull
                                        it): no damage, no blast, a round that lives one frame, silent
  Mods/OBJECT/EDF6VC_DRILL_CHARGE.SGO   the drill's bite (vcobjects DRILL_CHARGE_*: src/jet_bay.cpp DrillCharge)

Without the OBJ (pylib/drill_model.py model_dir: $EDF6VC_MODELS, D:\\APP\\edf6-models, or `models` next to the
installer) the drill tank cannot be built: its SGO then names the stock Blacker model and the stock Blacker cannon, so
the request (tools/calls.py EDF6VC_CALL_DRILL, always installed: the weapon table's rows never move) brings a plain
Blacker, and the plugin, finding no drill bone, leaves it alone. Said once when it happens.
The request is tools/calls.py EDF6VC_CALL_DRILL (tools/call_weapons.py, the Blacker E1's request as its template).
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_drill.py [game dir]                  write / refresh
  python tools/make_drill.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_drill.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import drill_model  # noqa: E402
import dsgo  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'drill'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['drill']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = drill_model.OUT_ARC
MODEL_MDB = drill_model.HOST_MDB          # the host's own model file name, inside the archive
STOCK_MODEL = ['app:/Object/v505_tank.mrab', MODEL_MDB]
STOCK_GUN = 'V_505TANK_CANNON01.SGO'
BIT_FILE = VEHICLE.weapons[0].split('/')[-1].upper()
CAMERA = ([0.0, 5.5, 0.0], [0.0, 5.5, -18.0])   # the Blacker's (0, 4, 0) / (0, 4, -15.5), for a 4.6 m tall hull
# The bit: the cannon's shot made a nothing (with the plugin the trigger never reaches it; an NPC's AI may fire it).
BIT = {'AmmoDamage': 0.0, 'AmmoExplosion': 0.0, 'AmmoAlive': 1.0, 'AmmoSpeed': 0.01, 'AmmoSize': 0.01,
       'AmmoHitImpulseAdjust': 0.0, 'FireRecoil': 0.0, 'AmmoColor': [0.0, 0.0, 0.0, 0.0]}
SILENT = ('FireSe', 'AmmoHitSe')            # their volume (entry 2) 0
NAMES = {'ja': 'ドリル', 'en': 'Drill', 'cn': '鑽頭', 'kr': 'Drill', 'sc': '钻头'}


def _node(v: object) -> object:
    """A python value as dsgo writes it: lists as Nodes, ints as floats."""
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    if isinstance(v, int) and not isinstance(v, bool):
        return float(v)
    return v


def bit_sgo(game: vc.Game, plain: bool) -> bytes:
    """The drill tank's one weapon: the Blacker's cannon, firing nothing (`plain`: the stock cannon, for the fallback)."""
    doc = dsgo.parse(game.read('WEAPON', STOCK_GUN))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Weapon_VehicleShoot':
        raise ValueError(f'{STOCK_GUN} 不是预期的坦克炮')
    if plain:
        return dsgo.write(doc)
    for key, value in BIT.items():
        if r.get(key) is None:
            raise ValueError(f'{STOCK_GUN} 缺少 {key}')
        r.set(key, _node(value))
    for key in SILENT:
        se = r.get(key)
        if se is None or len(se.items) < 3:
            raise ValueError(f'{STOCK_GUN}: {key} 不是预期的格式')
        se.items[2] = 0.0
    for lang, name in NAMES.items():
        if r.get(f'name.{lang}') is not None:
            r.set(f'name.{lang}', name)
    return dsgo.write(doc)


def vehicle_sgo(game: vc.Game, model: list[str]) -> bytes:
    version, m = sgo.read(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    if m.get('xgs_scene_object_class') != 'Vehicle505_Tank':
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 Vehicle505_Tank')
    am = m['animation_model']
    m['animation_model'] = [list(model), am[1], am[2]]
    setup = m['vehicle_setup']
    if len(setup[2]) != len(VEHICLE.weapons):
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(setup[2])} weapons, the drill tank has {len(VEHICLE.weapons)}')
    setup[2] = [[w, entry[1], entry[2]] for w, entry in zip(VEHICLE.weapons, setup[2])]
    cam = m['game_object_camera_setting']
    m['game_object_camera_setting'] = [list(CAMERA[0]), list(CAMERA[1]), cam[2], cam[3]]
    m['game_object_durability'] = VEHICLE.durability
    return sgo.write(version, m)


def charge_sgo(game: vc.Game) -> bytes:
    import make_jets
    return make_jets.impact_charge(game, vc.DRILL_CHARGE_RADIUS, vc.DRILL_CHARGE_SPEED, vc.DRILL_CHARGE_LIFE)


def check(files: dict[str, bytes], game: vc.Game | None = None) -> None:
    """The SGO names this tool's model (or, without one, the stock Blacker's) and weapon; with the model, its weapon
    bone is there and the model passes drill_model.check; the bit fires nothing; the charge's blast breaks buildings."""
    from mdb import mdb_read, rab_read
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    built = f'OBJECT/{MODEL_FILE}' in files
    want = [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB] if built else STOCK_MODEL
    assert v['animation_model'][0] == want, v['animation_model'][0]
    assert [w[0] for w in v['vehicle_setup'][2]] == list(VEHICLE.weapons)
    assert v['xgs_scene_object_class'] == 'Vehicle505_Tank' and v['game_object_durability'] == VEHICLE.durability
    if built:
        arc = files[f'OBJECT/{MODEL_FILE}']
        drill_model.check(arc, drill_model.stock_bones(game) if game is not None else None)
        md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == MODEL_MDB.lower()).data)
        names = {md.name_of(b.name) for b in md.bones}
        missing = {b for b, _ in v['vehicle_weapon_setting']} - names
        assert not missing, f'model lacks weapon bones {missing}'
        b = dsgo.to_py(dsgo.parse(files[f'WEAPON/{BIT_FILE}']).root)
        assert all(b[k] == x for k, x in BIT.items() if not isinstance(x, list)) and b['FireSe'][2] == 0.0, b
    c = sgo.load(data=files[f'OBJECT/{vc.DRILL_CHARGE_FILE}'])
    p = c['indirect_fire_param']
    assert c['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'SolidBullet01', c
    # A blast of 3 m or more sets the break-building bit (core init 0x232103: AmmoExplosion >= 3.0, docs/drill-re.md §3).
    assert p[9] >= 3.0 and p[9] == vc.DRILL_CHARGE_RADIUS and p[2] == 1 and c['indirect_fire_damage'] == 0.0, p


def build(root: str, models: str | None = None) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    drill tank's OBJ (`models`: its folder's parent, default drill_model.model_dir())."""
    game = vc.Game(root)
    obj = drill_model.obj_path(models)
    files = {f'OBJECT/{vc.DRILL_CHARGE_FILE}': charge_sgo(game)}
    if obj is None:
        print(f'未找到钻头战车模型（{drill_model.MODEL_SUBDIR}/{drill_model.OBJ_FILE}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              '钻头战车请求先叫来普通的 Blacker（ブラッカー）坦克；放好模型后重新安装即可。')
        files[f'OBJECT/{SGO_FILE}'] = vehicle_sgo(game, STOCK_MODEL)
        files[f'WEAPON/{BIT_FILE}'] = bit_sgo(game, plain=True)
    else:
        files[f'OBJECT/{MODEL_FILE}'] = drill_model.build(game, obj)
        files[f'OBJECT/{SGO_FILE}'] = vehicle_sgo(game, [f'app:/Object/{MODEL_FILE.lower()}', MODEL_MDB])
        files[f'WEAPON/{BIT_FILE}'] = bit_sgo(game, plain=False)
    check(files, game)
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


def write_out(files: dict[str, bytes], out: str) -> list[str]:
    """The files under `out` (a scratch folder, never the game's): for an offline build and check."""
    paths = []
    for rel, data in files.items():
        p = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as h:
            h.write(data)
        paths.append(p)
    return paths


def main(argv: list[str]) -> int:
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] != '--out')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    files = build(root)
    for path in write_out(files, out) if out else install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
