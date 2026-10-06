"""Writes the sidecar motorcycle (边三轮摩托: pylib/vcobjects.py GROUND_VEHICLES['sidecar'], src/sidecar.cpp) into
<game>/Mods:

  Mods/OBJECT/EDF6VC_SIDECAR.MRAB             the Freed bike's model with a sidecar built from primitives in the bike's
                                              own material: a boat-shaped tub (walls, deck, shield plate, seat), its
                                              arms, a third wheel (the bike's own front tyre) under a mudguard, and the
                                              marker bone the plugin knows it by (pylib/sidecar_model.py)
  Mods/OBJECT/EDF6VC_SIDECAR_RAGDOLL.SHKT     the bike's ragdoll (its collision) with one body hull moved under the
                                              tub: the bike's collision reaches under the sidecar, its top is the
                                              tub's floor, what the gunner stands on
  Mods/OBJECT/EDF6VC_SIDECAR.SGO              the Freed bike's vehicle (Vehicle503_Bike: its handling, its rider's two
                                              machine guns and fuel tank, its one seat) on that model and ragdoll,
                                              SIDECAR durability base

The gunner is not a seat of the vehicle: the plugin (src/sidecar.cpp) holds a soldier on the platform, on foot, so
they aim and fire their own weapons as they would anywhere (docs/sidecar-re.md §1: a seated human's own weapons are
switched off by the ride state; no stock seat keeps them). Everything is built from the player's own Root.cpk (read
only): nothing outside it is needed, so the sidecar is always built.
The request is tools/calls.py EDF6VC_CALL_SIDECAR (tools/call_weapons.py, the Freed bike's request aWeapon338 as its
template: a Ranger's vehicle). Built in memory first, written atomically and recorded in the ledger as this tool's;
--remove releases them.

  python tools/make_sidecar.py [game dir]                  write / refresh
  python tools/make_sidecar.py [game dir] --out DIR        build and check only, the files written under DIR
                                                          (--report: print what the checks measured)
  python tools/make_sidecar.py [game dir] --remove         release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import ledger  # noqa: E402
import sgo  # noqa: E402
import sidecar_model  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'sidecar'   # pylib/ledger.py
VEHICLE = vc.GROUND_VEHICLES['sidecar']
SGO_FILE = f'{VEHICLE.sgo}.SGO'
MODEL_FILE = sidecar_model.OUT_ARC
RAGDOLL_FILE = sidecar_model.OUT_RAGDOLL
MODEL = [f'app:/Object/{MODEL_FILE.lower()}', sidecar_model.HOST_MDB]
RAGDOLL = f'app:/object/{RAGDOLL_FILE.lower()}'
CLASS = 'Vehicle503_Bike'


def vehicle_sgo(game: vc.Game) -> bytes:
    """The sidecar's SGO: the Freed bike's (its class, handling, seat and weapons) on the sidecar's model and ragdoll,
    its durability VEHICLE.durability. The ragdoll's binding (the SGO blob after its path: proxy <-> bone by name) is
    the stock one: the model keeps every stock bone's name, and the edited ragdoll every proxy."""
    version, m = sgo.read(game.read('OBJECT', f'{VEHICLE.stock}.SGO'))
    if m.get('xgs_scene_object_class') != CLASS:
        raise ValueError(f'{VEHICLE.stock}.SGO 不是 {CLASS}')
    am = m['animation_model']
    m['animation_model'] = [list(MODEL), am[1], am[2]]
    rag = m['ragdoll']
    if not (isinstance(rag, list) and len(rag) == 2 and str(rag[0]).lower() == f'app:/object/{sidecar_model.HOST_RAGDOLL.lower()}'):
        raise ValueError(f'{VEHICLE.stock}.SGO 的 ragdoll 不是预期的 {sidecar_model.HOST_RAGDOLL}')
    m['ragdoll'] = [RAGDOLL, rag[1]]
    setup = m['vehicle_setup']
    if [w[0] for w in setup[-1]] != list(VEHICLE.weapons):
        raise ValueError(f'{VEHICLE.stock}.SGO: weapons {[w[0] for w in setup[-1]]}, the sidecar has {VEHICLE.weapons}')
    if len(m['vehicle_riding_position']) != 1:
        raise ValueError(f'{VEHICLE.stock}.SGO: {len(m["vehicle_riding_position"])} seats, the Freed bike has one')
    m['game_object_durability'] = VEHICLE.durability
    return sgo.write(version, m)


def check(files: dict[str, bytes], game: vc.Game | None = None) -> dict:
    """The SGO names this tool's model and ragdoll, the stock class, weapons and one seat; the model and the ragdoll
    pass sidecar_model's checks (with the game: against the stock bones, the Ranger's legs and hips clear of the
    tub's walls, and the stock ragdoll byte for byte outside the edit); the collision's floor slab is the model's tub
    across the ground (its plan) and its top the model's floor. Returns what the checks measured."""
    v = sgo.load(data=files[f'OBJECT/{SGO_FILE}'])
    assert v['xgs_scene_object_class'] == CLASS and v['game_object_durability'] == VEHICLE.durability, v['xgs_scene_object_class']
    assert v['animation_model'][0] == MODEL, v['animation_model'][0]
    assert v['ragdoll'][0] == RAGDOLL, v['ragdoll'][0]
    assert [w[0] for w in v['vehicle_setup'][-1]] == list(VEHICLE.weapons)
    assert len(v['vehicle_riding_position']) == 1, 'the gunner is no seat: the plugin holds them (src/sidecar.cpp)'
    reach = sidecar_model.soldier_reach(game) if game is not None else None
    assert reach is None or reach <= sidecar_model.SOLDIER_REACH, f'the Ranger reaches {reach}, SOLDIER_REACH is {sidecar_model.SOLDIER_REACH}'
    model = sidecar_model.check(files[f'OBJECT/{MODEL_FILE}'], sidecar_model.stock_bones(game) if game is not None else None, reach)
    stock = game.read('OBJECT', sidecar_model.HOST_RAGDOLL) if game is not None else None
    collision = sidecar_model.check_collision(files[f'OBJECT/{RAGDOLL_FILE}'], stock)
    (mlo, mhi), r = model['tub'], collision['radius']
    clo, chi = ([v + sidecar_model.BODY_PROXY[c] - r if k == 0 else v + sidecar_model.BODY_PROXY[c] + r
                 for c, v in enumerate(b)] for k, b in enumerate(collision['hull box']))
    for c in (0, 2):
        assert abs(clo[c] - mlo[c]) < 3e-3 and abs(chi[c] - mhi[c]) < 3e-3, f'the floor slab {clo}..{chi}, the tub {mlo}..{mhi}'
    assert abs(clo[1] - mlo[1]) < 3e-3, f'the floor slab bottom {clo[1]}, the tub bottom {mlo[1]}'
    assert abs(collision['top y (model)'] - model['floor y']) < 3e-3, (collision['top y (model)'], model['floor y'])
    return {'model': model, 'collision': collision, 'soldier reach': reach,
            'collision box (model frame)': [[round(x, 3) for x in clo], [round(x, 3) for x in chi]]}


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    shkt, _info = sidecar_model.build_collision(game)
    files = {f'OBJECT/{MODEL_FILE}': sidecar_model.build(game), f'OBJECT/{RAGDOLL_FILE}': shkt,
             f'OBJECT/{SGO_FILE}': vehicle_sgo(game)}
    build.measured = check(files, game)   # type: ignore[attr-defined]
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
    if '--report' in argv:
        import json
        print(json.dumps(build.measured, ensure_ascii=False, indent=1, default=str))   # type: ignore[attr-defined]
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
