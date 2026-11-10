"""The support's gunship and paratroop plane in the model pack's C-130s (docs/feature-support-jet-models.md §6; the
maintainer, 2026-11-10: 「没有模型来源的支援机型 直接去网站上拿呗，不用管授权」): built at install from the model folder
`c130/` (tools/prep_c130.py made it out of FlightGear's C-130 Hercules, GPL v2: its SOURCE.txt) and the player's own
Root.cpk (only read):

  Mods/OBJECT/EDF6VC_C130_<MODEL>.MRAB            each model (pylib/c130_model.py), its seats' first-person cockpits
                                                  grafted in (pylib/cockpit_fits_plugin.py) and its LODs
                                                  (pylib/model_lod.py)
  Mods/OBJECT/EDF6VC_C130_<MODEL>_AIRFRAME.SHKT   its collision, measured off it as built (pylib/aircraft_collision.py)
  Mods/OBJECT/EDF6VC_C130_GUNSHIP.SGO             the support gunship (src/jet_internal.h Body::c130Gunship, mark
                                                  vcobjects.C130_GUNSHIP_MARK): the AC-130, the gunship's arms and
                                                  durability, its side gunner's seat (tools/make_jets.py with_gunner_seat)
  Mods/OBJECT/EDF6VC_C130_TRANSPORT.SGO           the paratroop plane (Body::transportPlane): the C-130, the strike jet's
                                                  mark, arms and durability, its twelve passengers behind the pilot
                                                  (make_jets.TRANSPORT_PLANE_PASSENGERS)

Without the model folder none of it is written (the installer says so once): the gunship support and calls and the
paratroop plane are then not deployed (src/support_dispatch.cpp refuses them at planning; the model pack is part of
the install, CLAUDE.md). Built in memory first, written atomically and recorded in the ledger as this tool's; --remove
releases them.

  python tools/make_c130.py [game dir]                  write / refresh
  python tools/make_c130.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_c130.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import aircraft_collision  # noqa: E402
import c130_model  # noqa: E402
import jet_models  # noqa: E402
import ledger  # noqa: E402
import make_jets  # noqa: E402
import model_lod  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

OWNER = 'c130'   # pylib/ledger.py
GUNSHIP_SGO = 'EDF6VC_C130_GUNSHIP.SGO'
TRANSPORT_SGO = 'EDF6VC_C130_TRANSPORT.SGO'
SGOS = {GUNSHIP_SGO: (vc.C130_GUNSHIP_JET, 'gunship'), TRANSPORT_SGO: (vc.C130_TRANSPORT_JET, 'transport')}
# The AC-130's whole box (jet_models.model_box of its archive: [centre, half extents], the vehicle's axes about its
# origin): src/gunmuzzle.h kC130Gunship, which its rounds leave off (as make_jets.GUNSHIP_AIRFRAME the bomber401's);
# check_muzzle holds the model to it, tools/selftest.py gunship_muzzle_wired the header.
C130_AIRFRAME = ((0.0, 5.815, 0.0), (20.15, 5.815, 14.82))
AIRFRAME_FIT = 0.01   # m


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}; none without the model folder."""
    game = vc.Game(root)
    folder = c130_model.model_dir()
    if folder is None:
        print(f'未找到 C-130 模型（{c130_model.MODEL_SUBDIR}/{c130_model.MANIFEST}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
              '炮舰机支援 / 炮舰机呼叫 / 引导打击与伞兵运输机不会出动。放好模型包后重新安装即可。')
        return {}
    files: dict[str, bytes] = {}
    for model in c130_model.MODELS:
        arc, _info, md = c130_model.build_archive(game, folder, model)
        name = c130_model.archive_name(model)
        files[f'OBJECT/{name}'] = arc
        jet_models._MODELS_MADE[(id(game), name)] = md   # what jet_sgo measures (model_box, _model_of): this one, built once
        # its collision measured off it as built, about its whole model's box centre (the vehicle's position as its SGO
        # has it: vcobjects.jet_sgo)
        files[f'OBJECT/{aircraft_collision.C130_FILES[name]}'] = aircraft_collision.airframe(
            game, md, jet_models.model_box(game, name)[0])
    check_muzzle(game)
    stock = game.read('OBJECT', make_jets.GUNNER_STOCK)
    gunship = make_jets.with_gunner_seat(vc.jet_sgo(game, vc.C130_GUNSHIP_JET), stock)
    make_jets.check_gunner_seat(gunship, GUNSHIP_SGO, vc.C130_GUNSHIP_MARK)
    files[f'OBJECT/{GUNSHIP_SGO}'] = gunship
    plane = make_jets.with_passenger_seats(vc.jet_sgo(game, vc.C130_TRANSPORT_JET), 0, make_jets.TRANSPORT_PLANE_PASSENGERS, stock)
    make_jets.check_passenger_seats(plane, 1 + make_jets.TRANSPORT_PLANE_PASSENGERS, TRANSPORT_SGO, 1)
    files[f'OBJECT/{TRANSPORT_SGO}'] = plane
    import cockpits
    # its seats' first-person cockpits (pylib/cockpits.py), then its models merged by material and given their LODs
    files = model_lod.finish(cockpits.graft_files(game, files))
    check(files, game)   # what is written is held to the same contract
    return files


def check_muzzle(game: vc.Game) -> None:
    """The AC-130's whole box (jet_models.model_box) is C130_AIRFRAME within AIRFRAME_FIT."""
    box = jet_models.model_box(game, c130_model.archive_name('gunship'))
    off = max(abs(a - b) for got, want in zip(box, C130_AIRFRAME) for a, b in zip(got, want))
    if off > AIRFRAME_FIT:
        raise make_jets.GunshipMuzzleError(f'AC-130 的整机包围盒是 {box}，不是 C130_AIRFRAME {C130_AIRFRAME}')


def check(files: dict[str, bytes], game: vc.Game) -> None:
    """Every SGO names its model (and the V506's CAS) and its airframe, carries its mark, durability and arms, every
    weapon on a bone its model has; the gunship's gunner seat and the plane's passenger seats; every archive passes
    c130_model.check_archive; nothing else is written."""
    want = set(names())
    assert set(files) == want, sorted(set(files) ^ want)
    assert not model_lod.check_files(files), model_lod.check_files(files)   # the LOD tables model_lod.finish adds
    for file, (name, model) in SGOS.items():
        jet = vc.JETS[name]
        v = sgo.load(data=files[f'OBJECT/{file}'])
        assert jet.file == c130_model.archive_name(model) and jet.cas is None, (file, jet.file, jet.cas)
        assert v['xgs_scene_object_class'] == 'Vehicle506_Helicopter', (file, v['xgs_scene_object_class'])
        assert v['animation_model'][0][:2] == list(jet.model) and v['animation_model'][1] == 'app:/object/v506_heli.cas', \
            (file, v['animation_model'][:2])
        setup = v['mission_setup']
        assert setup[1][0] == jet.mark and v['game_object_durability'] == jet.durability, file
        held = [w[0] for w in setup[3] if w[0] != 'app:/weapon/v_fuel01.sgo']   # vcobjects.with_fuel puts the tank in
        assert held == list(jet.weapons), (file, setup[3])
        assert v['ragdoll'][0] == 'app:/object/' + aircraft_collision.C130_FILES[jet.file].lower(), (file, v['ragdoll'][0])
        assert v[vc.AIRFRAME_MEMBER] == 1, f'{file}: its body is not its airframe compound'
        make_jets.check_own_model(files, file)
        c130_model.check_archive(files[f'OBJECT/{jet.file}'], model)
        md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{jet.file}']).files
                           if f.name.lower() == c130_model.mdb_name(model)).data)
        missing = {b for b, _ in v['vehicle_weapon_setting']} - {md.name_of(b.name) for b in md.bones}
        assert not missing, f'{file}: model lacks weapon bones {missing}'
    make_jets.check_gunner_seat(files[f'OBJECT/{GUNSHIP_SGO}'], GUNSHIP_SGO, vc.C130_GUNSHIP_MARK)
    make_jets.check_passenger_seats(files[f'OBJECT/{TRANSPORT_SGO}'], 1 + make_jets.TRANSPORT_PLANE_PASSENGERS, TRANSPORT_SGO, 1)
    make_jets.check_weapon_bones(files, game)


def names() -> list[str]:
    """Every path under Mods this tool writes (with the model folder)."""
    out = []
    for model in c130_model.MODELS:
        name = c130_model.archive_name(model)
        out += [f'OBJECT/{name}', f'OBJECT/{aircraft_collision.C130_FILES[name]}']
    return out + [f'OBJECT/{f}' for f in SGOS]


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released."""
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
