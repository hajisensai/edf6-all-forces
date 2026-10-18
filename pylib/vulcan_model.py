"""Newly authored low-poly M163 VADS (M113 hull and six-barrel M61 Vulcan).

Procedural exterior, not a downloaded asset or a stock vehicle renamed M163.
Reference proportions: MCIA Saudi Arabia Country Handbook, M163 entry (4.86 x
2.85 x 2.736 m, six-barrel 20 mm). Geometry uses the unchanged V603 bone pivots,
so suspension spacing and collision are approximations. The six barrels are
static: a rotating barrel animation would require a new independently driven
bone, which is deliberately not inserted into the stock ragdoll/CAS skeleton.
"""
# Proportion/configuration reference: https://info.publicintelligence.net/MCIA-SaudiArabiaHandbook.pdf
from __future__ import annotations

import math
import os

import tracked_combat_model as t
from tracked_combat_model import HOST_ARC, HOST_MDB, HOST_CAS, TrackedCombatCheckError

OUT_ARC = 'EDF6VC_VULCAN.MRAB'
OUT_CAS = 'EDF6VC_VULCAN.CAS'
DIMENSIONS = (4.86, 2.85, 2.736)
CAMERA = {'model_eye': (-.23, 2.38, -.20), 'model_look_at': (.747215, 2.218, 5.),
          'third_person_distance': 6.5, 'third_person_height': 2.9}
DRIVE_SCALE = 1.0
# Model-space firing centres. Actual visible barrel mouths form the six-point
# ring around this centre; both weapon slots follow the same physical slide
# bone through elevation and recoil. The SGO/controller must use this same bone.
MUZZLES = {'left': [('cannon_slide_l', (.747214973, 2.217999995, 2.12))],
           'right': [('cannon_slide_l', (.747214973, 2.217999995, 2.12))], 'missiles': []}

# Spent cases leave a port on the authored receiver, in model bind space.
EJECTIONS = {'left': ('cannon_slide_l', (.972215, 2.218, 0.0)),
             'right': ('cannon_slide_l', (.972215, 2.218, 0.0))}


def parts(a: t.Assembly) -> None:
    t.chassis(a, 'BCDEF', *DIMENSIONS[:2], 1.61, True)
    turret = a.part('m163_turntable_and_turret_well', 'cannon_main')
    t.cylinder(turret, (0, 1.60, .10), (0, 1.80, .10), .91, 24)
    # Open topped operator basket / shielding, rather than a tank turret.
    t.box(turret, (-.36, 2.03, -.69), (.55, .23, .10))
    t.box(turret, (-.90, 2.03, -.10), (.09, .23, .57))
    t.box(turret, (.17, 2.03, -.18), (.08, .23, .48))
    t.box(turret, (-.36, 1.95, .45), (.53, .15, .09))
    basket = a.part('operator_seat_and_safety_rails', 'cannon_main', t.DARK_TINT)
    t.box(basket, (-.37, 1.99, -.08), (.22, .07, .23))
    t.box(basket, (-.37, 2.27, -.37), (.23, .26, .055))
    for x in (-.83, .11):
        t.rail(basket, (x, 2.06, -.66), (x, 2.56, -.66))
        t.rail(basket, (x, 2.56, -.66), (x, 2.56, .34))
    t.rail(basket, (-.83, 2.56, -.66), (.11, 2.56, -.66))
    t.rail(basket, (-.83, 2.56, .34), (.11, 2.56, .34))
    ammo = a.part('m61_ammunition_drum_and_feed', 'cannon_main')
    t.cylinder(ammo, (.26, 1.93, -.52), (.26, 1.93, .40), .33, 20)
    t.box(ammo, (.54, 2.05, -.17), (.13, .15, .37))
    # The stock left hinge drives the off-centre M61 cradle and all six barrels.
    cx, cy, cz = a.origin('cannon_l')
    cradle = a.part('m61_elevation_cradle', 'cannon_l')
    t.box(cradle, (cx, cy-.21, cz+.04), (.29, .12, .38))
    for x in (cx-.25, cx+.25):
        t.cylinder(cradle, (x-.025, cy, cz), (x+.025, cy, cz), .22, 16)
    receiver = a.part('m61_receiver', 'cannon_slide_l', t.DARK_TINT)
    t.cylinder(receiver, (cx, cy, cz-.23), (cx, cy, .37), .225, 20)
    t.cylinder(receiver, (cx, cy, .22), (cx, cy, .46), .245, 20)
    barrels = a.part('six_m61_barrels', 'cannon_slide_l', t.METAL_TINT)
    # A real six-barrel ring with six open 20 mm mouths; no solid stock guns remain.
    for k in range(6):
        th = k*math.tau/6
        x, y = cx+.122*math.cos(th), cy+.122*math.sin(th)
        t.barrel(barrels, (x, y), .37, MUZZLES['left'][0][1][2], .034, .020)
        a.muzzle(f'barrel_{k+1}', 'cannon_slide_l', (x, y, 2.12), .020)
    collars = a.part('m61_barrel_clamps', 'cannon_slide_l', t.DARK_TINT)
    for z in (.57, 1.32, 1.96):
        # Annular clamps leave every barrel visible and all six bores open.
        for k in range(6):
            th = k*math.tau/6
            x, y = cx+.122*math.cos(th), cy+.122*math.sin(th)
            t.cylinder(collars, (x, y, z-.025), (x, y, z+.025), .048, 12, cap=False)
    sight = a.part('m163_optical_sight_and_range_radar', 'cannon_main')
    t.rail(sight, (-.78, 2.18, .27), (-.78, 2.64, .27), .035)
    t.box(sight, (-.78, 2.65, .27), (.17, .086, .16))
    t.cylinder(sight, (-.78, 2.65, .43), (-.78, 2.65, .48), .10, 16)
    t.box(sight, (.63, 2.50, -.30), (.25, .10, .20))
    glass = a.part('optical_glass', 'cannon_main', (36, 63, 63))
    t.box(glass, (-.78, 2.65, .485), (.105, .055, .007))
    for key in ('left', 'right'):
        bone, p = MUZZLES[key][0]
        a.muzzle(key, bone, p, .020)


def muzzle_local(game) -> dict:
    host, _rab = t.stock(game)
    a = t.Assembly(host)
    parts(a)
    return _weapon_muzzles(a)


def _weapon_muzzles(a) -> dict:
    return {key: [(a.muzzles[key]['bone'], a.muzzles[key]['local'])] if key != 'missiles' else []
            for key in ('left', 'right', 'missiles')}


def ejection_local(game) -> dict:
    host, _rab = t.stock(game)
    a = t.Assembly(host)
    for key, (bone, position) in EJECTIONS.items():
        a.muzzle(key, bone, position, 0.0)
    return {key: record['local'] for key, record in a.muzzles.items()}


def build_with_info(game):
    arc, md, info = t.build_with_info(game, parts, 'M163 VADS / M61 Vulcan (procedural exterior)',
                                    DIMENSIONS, CAMERA, DRIVE_SCALE)
    a = t.Assembly(md)
    parts(a)
    info['muzzle_details'] = info['muzzles']
    info['muzzles'] = _weapon_muzzles(a)
    info['visible_barrels'] = 6
    info['road_wheels_per_side'] = 5
    info['barrel_rotation'] = 'static six-barrel geometry; no extra bone or rotation hook'
    return arc, md, info


def build(game) -> bytes:
    return build_with_info(game)[0]


def check(arc: bytes, host=None) -> dict:
    result = t.check(arc, parts, host)
    result['visible_barrels'] = 6
    result['road_wheels_per_side'] = 5
    return result


def animation(game, md=None) -> bytes:
    return t.animation(game, md)


def main(argv: list[str]) -> int:
    import json
    import rootcpk
    folder = argv[1] if len(argv)>1 else 'tmp/vulcan-model'
    os.makedirs(folder, exist_ok=True)
    arc, md, info = build_with_info(rootcpk.default())
    with open(os.path.join(folder, OUT_ARC), 'wb') as f:
        f.write(arc)
    t.dump_obj(md, os.path.join(folder, 'm163_vulcan.obj'))
    with open(os.path.join(folder, 'm163_vulcan.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: info[k] for k in ('triangles', 'bone_count', 'visible_barrels', 'muzzles')}, indent=2))
    return 0


if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv))
