"""The support's air carrier and its blast / doll drone carriers in the model pack's Helicarrier (docs/feature-support-jet-
models.md §6; the maintainer, 2026-11-10: 「没有模型来源的支援机型 直接去网站上拿呗，不用管授权」): built at install from the
model folder `helicarrier/` (tools/prep_helicarrier.py made it out of the Sketchfab "Helicarrier", CC BY 4.0: its
SOURCE.txt) and the player's own Root.cpk (only read):

  Mods/OBJECT/EDF6VC_HELICARRIER.MRAB            the model (pylib/helicarrier_model.py: the hull, its four lift fans as
                                                 the bones the plugin tilts, a nozzle under each), its pilot's
                                                 first-person cockpit grafted in (pylib/cockpit_fits_plugin.py) and its
                                                 LODs (pylib/model_lod.py)
  Mods/OBJECT/EDF6VC_HELICARRIER_AIRFRAME.SHKT   its collision, measured off it as built (pylib/aircraft_collision.py)
  Mods/OBJECT/EDF6VC_HELICARRIER.SGO             the support carrier (src/jet_internal.h Body::supCarrier)
  Mods/OBJECT/EDF6VC_HELICARRIER_BLAST.SGO       the support blast drone carrier (Body::supBlastCarrier)
  Mods/OBJECT/EDF6VC_HELICARRIER_DOLL.SGO        the support doll drone carrier (Body::supDollCarrier)
                                                 each its carrier kind's arms and durability under a mark of its own
                                                 (pylib/vcobjects.py HELICARRIER_JETS, HELICARRIER_MARKS)

Its drones leave and dock under its middle (src/jet_carrier.cpp kLaunchBelow 25 m, src/jet_internal.h kDockBelow 15 m:
under the vehicle's position, the collision box's centre, as for the V508 one, whose box is 0.3 m taller): check_drone_bay
holds the model to it (the points under the belly, clear of the hull by DOCK_CLEAR, inside the hull's footprint).

Without the model folder none of it is written (the installer says so once): the carrier supports and calls are then not
deployed (src/support_dispatch.cpp refuses them at planning; the model pack is part of the install, CLAUDE.md). Built in
memory first, written atomically and recorded in the ledger as this tool's; --remove releases them.

  python tools/make_helicarrier.py [game dir]                  write / refresh
  python tools/make_helicarrier.py [game dir] --out DIR        build and check only, the files written under DIR
  python tools/make_helicarrier.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import aircraft_collision  # noqa: E402
import helicarrier_model as hm  # noqa: E402
from i18n import tr  # noqa: E402
import jet_models  # noqa: E402
import ledger  # noqa: E402
import make_jets  # noqa: E402
import model_lod  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

OWNER = 'helicarrier'   # pylib/ledger.py
SGOS = {'EDF6VC_HELICARRIER.SGO': 'edf6tr_helicarrier', 'EDF6VC_HELICARRIER_BLAST.SGO': 'edf6tr_helicarrier_blast',
        'EDF6VC_HELICARRIER_DOLL.SGO': 'edf6tr_helicarrier_doll'}
AIRFRAME = aircraft_collision.HELICARRIER_FILES[hm.ARCHIVE]
# The drones' place (m under the vehicle's position): read off the source.
DRONE_SOURCES = {'kLaunchBelow': os.path.join(HERE, '..', 'src', 'jet_carrier.cpp'),
                 'kDockBelow': os.path.join(HERE, '..', 'src', 'jet_internal.h')}
DOCK_CLEAR = 5.0   # m at least between the hull's lowest point and a drone's launch / dock point (the drone: 5.7 m long,
                   # 1.3 m high; the V508 carrier's dock is 6.5 m under its hull)


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}; none without the model folder."""
    game = vc.Game(root)
    if hm.model_dir() is None:
        print(tr(f'未找到空中航母模型（{hm.MODEL_SUBDIR}/{hm.MANIFEST}，可用环境变量 EDF6VC_MODELS 指定模型目录）：'
                 '空中航母 / 自爆无人机母舰 / 人偶无人机母舰的支援与呼叫不会出动。放好模型包后重新安装即可。',
                 f'The air carrier model was not found ({hm.MODEL_SUBDIR}/{hm.MANIFEST}; the environment variable '
                 'EDF6VC_MODELS can name the model folder): the air carrier, blast drone carrier and doll drone carrier '
                 'supports and calls will not deploy. Put the model pack in place and install again.'))
        return {}
    arc, _info, md = hm.build_archive(game)
    files: dict[str, bytes] = {f'OBJECT/{hm.ARCHIVE}': arc}
    jet_models._MODELS_MADE[(id(game), hm.ARCHIVE)] = md   # what jet_sgo measures (model_box, _model_of): this one, built once
    # its collision measured off it as built, about its whole model's box centre (the vehicle's position as its SGO has it)
    files[f'OBJECT/{AIRFRAME}'] = aircraft_collision.airframe(game, md, jet_models.model_box(game, hm.ARCHIVE)[0])
    check_drone_bay(game)
    for file, name in SGOS.items():
        files[f'OBJECT/{file}'] = vc.jet_sgo(game, name)
    import cockpits
    # its pilot's first-person cockpit (pylib/cockpits.py), then its model merged by material and given its LODs
    files = model_lod.finish(cockpits.graft_files(game, files))
    check(files, game)   # what is written is held to the same contract
    return files


def drone_offsets() -> tuple[float, ...]:
    """(kLaunchBelow, kDockBelow): m under the carrier's position its drones leave and dock (DRONE_SOURCES)."""
    out = []
    for name, path in DRONE_SOURCES.items():
        with open(path, encoding='utf-8') as h:
            m = re.search(name + r'=([0-9.]+)f', h.read())
        if not m:
            raise ValueError(f'{path}: no {name}')
        out.append(float(m.group(1)))
    return tuple(out)


def check_drone_bay(game: vc.Game) -> None:
    """The drones' launch and dock point (drone_offsets under the vehicle's position, the whole model's box centre) is under
    the hull by DOCK_CLEAR or more and inside its footprint (x, z within the box): they leave from under its middle, not
    out of its side or through its hull."""
    (cx, cy, cz), (hx, hy, hz) = jet_models.model_box(game, hm.ARCHIVE)
    for below in drone_offsets():
        clear = (cy - hy) - (cy - below)
        if clear < DOCK_CLEAR or abs(cx) > hx or abs(cz) > hz:
            raise ValueError(f'无人机出入点（载具原点下 {below} m）离船底只有 {clear:.2f} m，或不在船体投影内（{cx}, {cz}）')


def check(files: dict[str, bytes], game: vc.Game) -> None:
    """Every SGO names its model (and the V506's CAS) and its airframe, carries its mark, durability and arms, every
    weapon on a bone its model has; the archive passes helicarrier_model.check_archive; nothing else is written."""
    want = set(names())
    assert set(files) == want, sorted(set(files) ^ want)
    assert not model_lod.check_files(files), model_lod.check_files(files)   # the LOD tables model_lod.finish adds
    folder = hm.model_dir()
    hm.check_archive(files[f'OBJECT/{hm.ARCHIVE}'], hm.nozzles(hm.read_manifest(folder)))
    md = mdb_read(next(f for f in rab_read(files[f'OBJECT/{hm.ARCHIVE}']).files if f.name.lower() == hm.MDB).data)
    bones = {md.name_of(b.name) for b in md.bones}
    for file, name in SGOS.items():
        jet, kind = vc.JETS[name], vc.JETS[vc.HELICARRIER_JETS[name]]
        v = sgo.load(data=files[f'OBJECT/{file}'])
        assert jet.file == hm.ARCHIVE and jet.cas is None, (file, jet.file, jet.cas)
        assert v['xgs_scene_object_class'] == 'Vehicle506_Helicopter', (file, v['xgs_scene_object_class'])
        assert v['animation_model'][0][:2] == list(vc.HELICARRIER_MODEL) and \
            v['animation_model'][1] == 'app:/object/v506_heli.cas', (file, v['animation_model'][:2])
        setup = v['mission_setup']
        assert setup[1][0] == jet.mark == vc.HELICARRIER_MARKS[kind.mark], (file, setup[1][0])
        assert v['game_object_durability'] == jet.durability == kind.durability, file
        held = [w[0] for w in setup[3] if w[0] != 'app:/weapon/v_fuel01.sgo']   # vcobjects.with_fuel puts the tank in
        assert held == list(jet.weapons) == list(kind.weapons), (file, setup[3])
        assert v['ragdoll'][0] == 'app:/object/' + AIRFRAME.lower(), (file, v['ragdoll'][0])
        assert v[vc.AIRFRAME_MEMBER] == 1, f'{file}: its body is not its airframe compound'
        missing = {b for b, _ in v['vehicle_weapon_setting']} - bones
        assert not missing, f'{file}: model lacks weapon bones {missing}'
        make_jets.check_own_model(files, file)
    make_jets.check_weapon_bones(files, game)


def names() -> list[str]:
    """Every path under Mods this tool writes (with the model folder)."""
    return [f'OBJECT/{hm.ARCHIVE}', f'OBJECT/{AIRFRAME}'] + [f'OBJECT/{f}' for f in SGOS]


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
