"""Writes the Sazabi (src/sazabi.cpp, docs/gundam-plan.md, pylib/vcobjects.py JETS[SAZABI_JET]) into <game>/Mods:

  Mods/OBJECT/EDF6VC_SAZABI.MRAB     the mech: the user's model folder (tools/prep_sazabi.py) on the plugin's
                                     skeleton, in the Blacker's hull material and three glowing copies of
                                     Retro-Balam's light (pylib/sazabi_model.py)
  Mods/WEAPON/EDF6VC_SZ_RIFLE.SGO / _MISSILE.SGO   its beam rifle and shield missiles (vcobjects.sazabi_weapons)
  Mods/OBJECT/EDF6VC_SZ_MEGA.SGO / _CHARGE.SGO / _FUNNEL.SGO   the beams the plugin fires (vcobjects.sazabi_rounds)
  Mods/OBJECT/EDF6VC_SAZABI.SGO      the V506 body (vcobjects.jet_sgo) with that model, the Sazabi's mark,
                                     durability and arms, its box measured off the model, its door beside it

Without the model folder (pylib/obj_model.py model_dir: $EDF6VC_MODELS, `models` next to the installer, the
developer's folder) the Sazabi cannot be built: its SGO is then the stock V506_HELI.SGO as it is, so its request
(tools/calls.py EDF6VC_CALL_SAZABI, always installed: the weapon table's rows never move) brings a plain N9 Eros heli,
which the plugin (no Sazabi mark) leaves alone. Said once when it happens.
Built in memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_sazabi.py [game dir]                  write / refresh
  python tools/make_sazabi.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_sazabi.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import ledger  # noqa: E402
import sazabi_model as sz  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

OWNER = 'sazabi'   # pylib/ledger.py
JET = vc.JETS[vc.SAZABI_JET]
SGO_FILE = 'EDF6VC_SAZABI.SGO'
STOCK_SGO = 'V506_HELI.SGO'


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read) and the
    Sazabi's model folder."""
    game = vc.Game(root)
    folder = sz.model_dir()
    if folder is None:
        print(f'未找到沙扎比模型（{sz.MODEL_SUBDIR}/{sz.OBJ_FILE}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              '沙扎比请求先叫来普通的 N9 Eros 直升机；放好模型后重新安装即可。')
        files = {f'OBJECT/{SGO_FILE}': game.read('OBJECT', STOCK_SGO)}
    else:
        arc, _info = sz.build_archive(game, folder)
        files = {f'OBJECT/{sz.OUT_ARC}': arc, f'OBJECT/{SGO_FILE}': vc.jet_sgo(game, vc.SAZABI_JET),
                 **{f'WEAPON/{n}': d for n, d in vc.sazabi_weapons(game).items()},
                 **{f'OBJECT/{n}': d for n, d in vc.sazabi_rounds(game).items()}}
    check(files)
    return files


def check(files: dict[str, bytes]) -> None:
    """With the model: the SGO names this tool's model (and the V506's CAS), carries the Sazabi's mark, durability and
    arms, every weapon on a bone the model has, and the model passes sazabi_model.check_archive. Without: the SGO is
    the stock heli's."""
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    if f'OBJECT/{sz.OUT_ARC}' not in files:
        assert v['animation_model'][0] == ['app:/object/v506_heli.mrab', 'v506_heli.mdb'], v['animation_model'][0]
        return
    assert JET.model == (f'app:/object/{sz.OUT_ARC.lower()}', sz.OUT_MDB) and JET.file == sz.OUT_ARC, JET
    assert JET.cas is None, JET.cas   # the V506's: a CAS naming our bones would write them back to bind (docs/sazabi-re.md §2)
    assert v['xgs_scene_object_class'] == 'Vehicle506_Helicopter', v['xgs_scene_object_class']
    assert v['animation_model'][0] == list(JET.model) and v['animation_model'][1] == 'app:/object/v506_heli.cas',         v['animation_model'][:2]
    assert v['vehicle_setup'][1][0] == JET.mark and v['game_object_durability'] == JET.durability
    assert [w[0] for w in v['vehicle_setup'][3][:len(JET.weapons)]] == list(JET.weapons), v['vehicle_setup'][3]
    arc = files[f'OBJECT/{sz.OUT_ARC}']
    sz.check_archive(arc)
    md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == sz.OUT_MDB).data)
    names = {md.name_of(b.name) for b in md.bones}
    missing = {b for b, _ in v['vehicle_weapon_setting']} - names
    assert not missing, f'model lacks weapon bones {missing}'
    held = {w.split('/')[-1].upper() for w in JET.weapons}
    assert held <= {k.split('/')[-1] for k in files if k.startswith('WEAPON/')}, f'arms not written: {held}'
    assert all(f'OBJECT/{n}' in files for n in vc.SAZABI_ROUND_FILES), 'beams not written'


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
