"""The quad Vulcan: the M163 with four M61s (the user, 2026-10-24: 「火神炮做一个四管的」, then 「我说的四管是4联装转轮
机枪」): its upgrade, four six-barrel 20 mm rotary cannon in a 2 x 2 block on the one elevating cradle.

No such vehicle was built; it is made from the M163's War Thunder model (models/wt_m163, pylib/wt_tracked.py): derive()
turns that model's spec into this one, the M61's six barrels and clamps (its visual rotor's piece) copied to the four
corners of a CLUSTER_STEP square round the one gun's axis, each copy on a visual rotor of its own (ROTORS, under the
gun's slide like the M163's one: they all lift with the cradle, src/vulcan_rotor.cpp spins each about its own axis).
The cradle, the receiver and the feed stay the M163's. Its two weapon holders a side each (tools/make_tracked_combat.py:
the left pair's barrels fire from the left column's centre, the right pair's from the right's) and FireCount rounds a
shot. Without the M163's folder there is no quad Vulcan (its procedural M163 has nothing to copy from).
"""
from __future__ import annotations

import copy
import os

import numpy as np

import tracked_combat_model as t
import wt_tracked as wt
from tracked_combat_model import HOST_ARC, HOST_MDB, HOST_CAS, TrackedCombatCheckError  # noqa: F401 - the modules' API

KIND = 'quadvulcan'
OUT_ARC = 'EDF6VC_QUADVULCAN.MRAB'
OUT_CAS = 'EDF6VC_QUADVULCAN.CAS'
SOURCE_ROTOR = 'edf6vc_m61_rotor'
ROTORS = (SOURCE_ROTOR, 'edf6vc_m61_rotor_2', 'edf6vc_m61_rotor_3', 'edf6vc_m61_rotor_4')   # src/vulcan_rotor.h kBones
# m between the four guns' axes (x and y): the M61's barrel ring is 0.18 m across, its clamps a little more; at 0.30 m
# the four clusters clear each other and stay inside the M163 cradle's face (0.81 x 0.52 m).
CLUSTER_STEP = 0.30
# Each rotor's offset from the M163's one gun axis (x: the vehicle's left, y: up), in ROTORS' order: upper left, upper
# right, lower left, lower right; the left column fires the 'left' holders, the right column the 'right' ones.
OFFSETS = tuple((sx * CLUSTER_STEP / 2, sy * CLUSTER_STEP / 2) for sy in (1, -1) for sx in (1, -1))
COLUMN = {'left': CLUSTER_STEP / 2, 'right': -CLUSTER_STEP / 2}
CAMERA = {'model_eye': (-.23, 2.38, -.20), 'model_look_at': (.0, 2.31, 5.),
          'third_person_distance': 6.5, 'third_person_height': 2.9}

# A box's 12 triangles, outward, over its corners numbered by bits (x 1, y 2, z 4: set = the high side).
BOX_TRIS = ((0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6), (0, 1, 4), (1, 5, 4),
            (2, 6, 3), (3, 6, 7), (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5))

_DERIVED: dict[int, wt.Spec] = {}


class QuadVulcanError(TrackedCombatCheckError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise QuadVulcanError(message)


def derive(base: wt.Spec) -> wt.Spec:
    """The quad Vulcan's spec from the M163's (wt_tracked.load('vulcan')): parts, visual bones, muzzles, case ports and
    the barrels' collision piece, four guns' worth."""
    if id(base) in _DERIVED:
        return _DERIVED[id(base)]
    _req(base.info.get('kind') == 'vulcan', 'the quad Vulcan is made from the M163 model')
    info = copy.deepcopy(base.info)
    arrays = dict(base.arrays)
    rotor = [k for k, p in enumerate(info['parts']) if p['bone'] == SOURCE_ROTOR]
    _req(len(rotor) == 1, f'the M163 model has {len(rotor)} pieces on {SOURCE_ROTOR}, one expected')
    k0 = rotor[0]
    source = info['parts'][k0]
    pos, nrm, uv, tri = base.part(k0)
    shift = lambda a, off: (a + np.asarray((off[0], off[1], 0.0), dtype=a.dtype)).astype(a.dtype)   # noqa: E731
    for i, (bone, off) in enumerate(zip(ROTORS, OFFSETS)):
        k = k0 if i == 0 else len(info['parts'])
        if i:
            info['parts'].append(dict(source))
        info['parts'][k].update(name=f'{bone}.{source["name"].split(".", 1)[1]}', bone=bone)
        arrays[f'p{k}_pos'], arrays[f'p{k}_nrm'], arrays[f'p{k}_uv'], arrays[f'p{k}_tri'] = shift(pos, off), nrm, uv, tri
    (visual,) = [b for b in info['visual_bones'] if b['name'] == SOURCE_ROTOR]
    ox, oy, oz = visual['origin']
    info['visual_bones'] = [b for b in info['visual_bones'] if b['name'] != SOURCE_ROTOR] + [
        {'name': bone, 'parent': visual['parent'], 'origin': [ox + dx, oy + dy, oz]} for bone, (dx, dy) in zip(ROTORS, OFFSETS)]
    for side in ('left', 'right'):
        (row,) = info['muzzles'][side]
        x, y, z = row['point']
        info['muzzles'][side] = [{'bone': row['bone'], 'point': [x + COLUMN[side], y, z]}]
        e = info['ejections'][side]
        e['point'] = [e['point'][0] + COLUMN[side], *e['point'][1:]]
    # The barrels' physics proxy (the M163's cannon_l: its one cluster's hull) holds all four clusters: the box round
    # them (four hulls together are more than one convex piece may hold, hkshape.write_convex_mesh).
    # Lengthwise it keeps the M163's target (the barrels ahead of the cradle: tracked_collision's sibling gap).
    cp, (t0, t1) = arrays['c_cannon_l_pos'], info['collision']['cannon_l']['target']
    half = CLUSTER_STEP / 2
    lo = np.array((cp[:, 0].min() - half, cp[:, 1].min() - half, t0[2]))
    hi = np.array((cp[:, 0].max() + half, cp[:, 1].max() + half, t1[2]))
    arrays['c_cannon_l_pos'] = np.array([[(lo, hi)[i >> k & 1][k] for k in range(3)] for i in range(8)], dtype=cp.dtype)
    arrays['c_cannon_l_tri'] = np.array(BOX_TRIS, dtype=arrays['c_cannon_l_tri'].dtype)
    box = info['collision']['cannon_l']
    box['target'] = [lo.tolist(), hi.tolist()]
    box['source_triangles'] = int(sum(p['triangles'] for p in info['parts'] if p['bone'] in ROTORS))
    box['hull_triangles'] = len(BOX_TRIS)
    info['kind'] = KIND
    info['title'] = 'Quad M61 Vulcan (M163 War Thunder model, four guns)'
    info['triangles'] = int(sum(p['triangles'] for p in info['parts']))
    spec = wt.Spec(base.folder, info, arrays)
    _DERIVED.clear()
    _DERIVED[id(base)] = spec
    return spec


def available() -> bool:
    return wt.available(KIND)


def _need() -> None:
    _req(available(), 'the quad Vulcan needs the M163 War Thunder model folder (models/wt_m163)')


def parts(a: t.Assembly) -> None:
    _need()
    wt.parts(a, KIND)


def muzzle_local(game) -> dict:  # noqa: ANN001 - rootcpk.Game
    _need()
    return wt.muzzle_local(game, KIND)


def ejection_local(game) -> dict:  # noqa: ANN001
    _need()
    return wt.ejection_local(game, KIND)


def build_with_info(game):  # noqa: ANN001, ANN201
    _need()
    arc, md, info = wt.build_with_info(game, KIND)
    info.update(visible_barrels=24, road_wheels_per_side=5, rotor_bones=list(ROTORS),
                barrel_rotation='four independent visual rotors; actual shot activity drives native pre-worldbuild pose')
    return arc, md, info


def build(game) -> bytes:  # noqa: ANN001
    return build_with_info(game)[0]


def check(arc: bytes, host=None) -> dict:  # noqa: ANN001
    _need()
    result = wt.check(arc, KIND, host)
    result.update(visible_barrels=24, road_wheels_per_side=5)
    return result


def animation(game, md=None) -> bytes:  # noqa: ANN001
    _need()
    return wt.animation(game, KIND, md)


def collision_layout() -> dict | None:
    _need()
    return wt.collision_layout(KIND)


def main(argv: list[str]) -> int:
    import json
    import rootcpk
    folder = argv[1] if len(argv) > 1 else 'tmp/quadvulcan-model'
    os.makedirs(folder, exist_ok=True)
    arc, md, info = build_with_info(rootcpk.default())
    with open(os.path.join(folder, OUT_ARC), 'wb') as f:
        f.write(arc)
    t.dump_obj(md, os.path.join(folder, 'quadvulcan.obj'))
    print(json.dumps({k: info[k] for k in ('triangles', 'bone_count', 'visible_barrels', 'muzzles')}, indent=2))
    return 0


if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv))
