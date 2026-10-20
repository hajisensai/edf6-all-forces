"""Writes the Dreadnought (src/dreadnought.cpp, docs/dreadnought-plan.md, pylib/vcobjects.py JETS[DREADNOUGHT_JET]) and its
hangar's fighters into <game>/Mods:

  Mods/OBJECT/EDF6VC_DN_<MODEL>.MRAB            the dreadnought and the four fighters: the model folder
                                                (tools/prep_dreadnought.py) on the V506 frame (pylib/dreadnought_model.py)
  Mods/OBJECT/EDF6VC_DN_<MODEL>_AIRFRAME.SHKT   each one's collision, measured off its model (pylib/aircraft_collision.py)
  Mods/OBJECT/EDF6VC_DREADNOUGHT.SGO            the V506 body (vcobjects.jet_sgo) with the dreadnought's model, mark,
                                                durability and arms (the submarine carrier's: tools/make_jets.py writes
                                                them), its door beside it
  Mods/OBJECT/EDF6VC_DN_<FIGHTER>.SGO           the fighters, NPC jets (src/jet_internal.h kBodies Body::dn*)

Without the model folder (pylib/obj_model.py model_dir: $EDF6VC_MODELS, `models` next to the installer, the developer's
folder) none of it can be built: the dreadnought's SGO is then the stock V506_HELI.SGO as it is, so its request
(tools/calls.py EDF6VC_CALL_DREADNOUGHT, always installed: the weapon table's rows never move) brings a plain N9 Eros
heli, which the plugin (no Dreadnought mark) leaves alone; no fighter SGO is written (the plugin's hangar then launches
none). Said once when it happens.
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_dreadnought.py [game dir]                  write / refresh
  python tools/make_dreadnought.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_dreadnought.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import aircraft_collision  # noqa: E402
import dreadnought_model as dm  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

OWNER = 'dreadnought'   # pylib/ledger.py
JET = vc.JETS[vc.DREADNOUGHT_JET]
SGO_FILE = 'EDF6VC_DREADNOUGHT.SGO'
STOCK_SGO = 'V506_HELI.SGO'
FIGHTER_SGOS = {k: vc.dn_file(k, 'SGO') for k in vc.DREADNOUGHT_FIGHTERS}


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    Dreadnought's model folder."""
    game = vc.Game(root)
    folder = dm.model_dir()
    if folder is None:
        print(f'未找到无畏舰模型（{dm.MODEL_SUBDIR}/{dm.MANIFEST}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              '无畏舰请求先叫来普通的 N9 Eros 直升机，机库没有舰载机；放好模型后重新安装即可。')
        files = {f'OBJECT/{SGO_FILE}': game.read('OBJECT', STOCK_SGO)}
    else:
        files = {}
        for model in dm.MODELS:
            arc, _info = dm.build_archive(game, folder, model)
            files[f'OBJECT/{dm.archive_name(model)}'] = arc
        files.update({f'OBJECT/{n}': d for n, d in aircraft_collision.assets_of(game, aircraft_collision.DN_FILES).items()})
        files[f'OBJECT/{SGO_FILE}'] = vc.jet_sgo(game, vc.DREADNOUGHT_JET)
        for kind, (name, *_rest) in vc.DREADNOUGHT_FIGHTERS.items():
            files[f'OBJECT/{FIGHTER_SGOS[kind]}'] = vc.jet_sgo(game, name)
    check(files)
    return files


def check(files: dict[str, bytes]) -> None:
    """With the model: every SGO names its model (and the V506's CAS) and its airframe, carries its mark, durability and
    arms, every weapon on a bone its model has; every archive passes dreadnought_model.check_archive. Without: the
    dreadnought's SGO is the stock heli's, and nothing else is written."""
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    if f'OBJECT/{dm.archive_name(dm.SHIP)}' not in files:
        assert v['animation_model'][0] == ['app:/object/v506_heli.mrab', 'v506_heli.mdb'], v['animation_model'][0]
        assert set(files) == {f'OBJECT/{SGO_FILE}'}, sorted(files)
        return
    assert JET.file == dm.archive_name(dm.SHIP) and JET.model == (f'app:/object/{JET.file.lower()}', dm.mdb_name(dm.SHIP))
    jets = {SGO_FILE: (vc.DREADNOUGHT_JET, dm.SHIP)}
    jets.update({FIGHTER_SGOS[k]: (name, k) for k, (name, *_r) in vc.DREADNOUGHT_FIGHTERS.items()})
    for file, (name, model) in jets.items():
        jet = vc.JETS[name]
        v = sgo.load(data=files[f'OBJECT/{file}'])
        assert jet.file == dm.archive_name(model) and jet.cas is None, (file, jet.file, jet.cas)
        assert v['xgs_scene_object_class'] == 'Vehicle506_Helicopter', (file, v['xgs_scene_object_class'])
        assert v['animation_model'][0] == list(jet.model) and v['animation_model'][1] == 'app:/object/v506_heli.cas', \
            (file, v['animation_model'][:2])
        setup = v['vehicle_setup'] if jet.player else v['mission_setup']
        assert setup[1][0] == jet.mark and v['game_object_durability'] == jet.durability, file
        held = [w[0] for w in setup[3] if w[0] != 'app:/weapon/v_fuel01.sgo']   # vcobjects.with_fuel puts the tank in
        assert held == list(jet.weapons), (file, setup[3])
        assert v['ragdoll'][0] == 'app:/object/' + aircraft_collision.DN_FILES[jet.file].lower(), (file, v['ragdoll'][0])
        assert f'OBJECT/{aircraft_collision.DN_FILES[jet.file]}' in files, file
        arc = files[f'OBJECT/{jet.file}']
        dm.check_archive(arc, model)
        md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == dm.mdb_name(model)).data)
        names = {md.name_of(b.name) for b in md.bones}
        missing = {b for b, _ in v['vehicle_weapon_setting']} - names
        assert not missing, f'{file}: model lacks weapon bones {missing}'


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
