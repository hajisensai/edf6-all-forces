"""Newly authored low-poly BMPT with a T-72 chassis and Terminator weapon module.

Original procedural exterior: no downloaded mesh and no stock hull relabelled
BMPT. Proportions/configuration follow US Army ARMOR, Summer 2022, Grau and
Bartles: 6.7 x 3.8 x 3.4 m, twin 30 mm cannon and four Ataka missile tubes.
The original V603 physical bone binds are retained; a private CAS fixes only
the optic bones' four int16 T/R references (eight bytes) at bind, so their real
collision proxies carry the visible assemblies in death.
Per-vehicle contact shapes remain a documented convex approximation requiring
game acceptance, not a measured T-72 collision hull.
"""
# Proportion/configuration reference: https://www.benning.army.mil/Armor/eARMOR/content/issues/2022/Summer/3Grau_Barttles22.pdf
from __future__ import annotations

import os

import tracked_combat_model as t
from tracked_combat_model import HOST_ARC, HOST_MDB, HOST_CAS, TrackedCombatCheckError

OUT_ARC = 'EDF6VC_BMPT.MRAB'
OUT_CAS = 'EDF6VC_BMPT.CAS'
DIMENSIONS = (6.7, 3.8, 3.4)
CAMERA = {'model_eye': (0., 2.61, .48), 'model_look_at': (0., 2.22, 7.),
          'third_person_distance': 8., 'third_person_height': 3.25}
DRIVE_SCALE = 1.0
MUZZLES = {'left': [('cannon_slide_l', (.747214973, 2.217999995, 3.58))],
           'right': [('cannon_slide_r', (-.747214973, 2.217999995, 3.58))],
           'missiles': [('cannon_main', (s*1.38, y, 1.02)) for s in (1, -1) for y in (2.35, 2.71)]}

# Spent cases leave a port on the authored receiver, in model bind space.
EJECTIONS = {'left': ('cannon_slide_l', (.882215, 2.248, -.20)),
             'right': ('cannon_slide_r', (-.882215, 2.248, -.20))}


def parts(a: t.Assembly) -> None:
    t.chassis(a, 'BCDEFG', *DIMENSIONS[:2], 1.43, False)
    turret = a.part('bmpt_turntable_and_low_turret', 'cannon_main')
    t.cylinder(turret, (0, 1.43, .30), (0, 1.70, .30), 1.05, 24)
    t.hexahedron(turret, [(-.94, 1.66, -.95), (.94, 1.66, -.95), (.94, 1.66, 1.02), (-.94, 1.66, 1.02)],
                 [(-.67, 2.10, -.68), (.67, 2.10, -.68), (.67, 2.10, .70), (-.67, 2.10, .70)])
    t.box(turret, (0, 2.12, -.26), (.38, .10, .47))
    # Independent left/right elevation and recoil. Unlike the stock flak, each
    # side is a single long 30 mm tube with recognisable gas block/muzzle brake.
    for side in ('l', 'r'):
        x, y, z = a.origin(f'cannon_{side}')
        mount = a.part(f'30mm_cradle_{side}', f'cannon_{side}')
        t.box(mount, (x, y-.09, z+.34), (.17, .18, .57))
        t.cylinder(mount, (x-.23, y, z), (x+.23, y, z), .225, 16)
        gun = a.part(f'30mm_barrel_{side}', f'cannon_slide_{side}', t.METAL_TINT)
        t.barrel(gun, (x, y), z+.38, 3.58, .058, .030)
        t.cylinder(gun, (x, y, .02), (x, y, .57), .11, 16)
        t.cylinder(gun, (x, y, 3.31), (x, y, 3.53), .095, 16, cap=False)
        brace = a.part(f'30mm_receiver_{side}', f'cannon_slide_{side}', t.DARK_TINT)
        t.box(brace, (x, y, z+.24), (.13, .115, .42))
        t.box(brace, (x+.095, y+.06, 1.02), (.035, .045, .50))
    # Four Ataka tubes: two on each side. The tube/pod assembly follows turret
    # traverse only (cannon_main), separate from the cannon elevation/recoil.
    pods = a.part('ataka_missile_pod_brackets', 'cannon_main')
    for s in (1, -1):
        t.box(pods, (s*1.10, 2.28, -.18), (.15, .10, .70))
        t.box(pods, (s*1.20, 2.52, -.23), (.10, .43, .16))
    tubes = a.part('four_ataka_launch_tubes', 'cannon_main', (100, 104, 67))
    for k, (bone, p) in enumerate(MUZZLES['missiles']):
        x, y, mouth_z = p
        t.barrel(tubes, (x, y), -1.18, mouth_z, .147, .232)
        t.cylinder(tubes, (x, y, -1.23), (x, y, -1.16), .169, 16)
        for z in (-.84, .62):
            t.cylinder(tubes, (x, y, z-.045), (x, y, z+.045), .164, 16, cap=False)
        a.muzzle(f'missile_{k+1}', bone, p, .130)
    sight = a.part('commander_panorama_and_gunner_sights', 'doppler_radar')
    t.cylinder(sight, (0, 2.19, -.45), (0, 2.84, -.45), .12, 16)
    t.box(sight, (0, 2.94, -.45), (.22, .13, .24))
    gunner = a.part('bmpt_gunner_sight', 'tracking_radar')
    t.box(gunner, (-.38, 2.42, .31), (.19, .18, .22))
    auxiliary = a.part('bmpt_auxiliary_sight', 'cannon_main')
    t.box(auxiliary, (.31, 2.44, .30), (.10, .16, .20))
    glass = a.part('panoramic_optics_glass', 'doppler_radar', (30, 56, 58))
    t.box(glass, (0, 2.96, -.204), (.15, .068, .009))
    gunner_glass = a.part('gunner_optics_glass', 'tracking_radar', (30, 56, 58))
    t.box(gunner_glass, (-.38, 2.43, .536), (.13, .08, .009))
    fittings = a.part('smoke_launchers_and_antenna', 'cannon_main', t.DARK_TINT)
    for s in (-1, 1):
        for z in (-.58, -.28, .02):
            t.cylinder(fittings, (s*.75, 1.99, z), (s*1.03, 2.23, z+.15), .065, 12)
    t.rail(fittings, (.56, 2.10, -.65), (.56, 3.40, -.65), .011)
    grenade = a.part('hull_grenade_launchers', 'body', t.DARK_TINT)
    for s in (-1, 1):
        t.box(grenade, (s*.92, 1.58, 2.36), (.14, .14, .21))
        t.cylinder(grenade, (s*.92, 1.62, 2.47), (s*.92, 1.62, 2.98), .045, 12)
    for key in ('left', 'right'):
        bone, p = MUZZLES[key][0]
        a.muzzle(key, bone, p, .030)


def _weapon_muzzles(a) -> dict:
    return {'left': [(a.muzzles['left']['bone'], a.muzzles['left']['local'])],
            'right': [(a.muzzles['right']['bone'], a.muzzles['right']['local'])],
            'missiles': [(a.muzzles[f'missile_{i}']['bone'], a.muzzles[f'missile_{i}']['local']) for i in range(1,5)]}


def muzzle_local(game) -> dict:
    host, _rab = t.stock(game)
    a = t.Assembly(host)
    parts(a)
    return _weapon_muzzles(a)


def ejection_local(game) -> dict:
    host, _rab = t.stock(game)
    a = t.Assembly(host)
    for key, (bone, position) in EJECTIONS.items():
        a.muzzle(key, bone, position, 0.0)
    return {key: record['local'] for key, record in a.muzzles.items()}


def build_with_info(game):
    arc, md, info = t.build_with_info(game, parts, 'BMPT Terminator (procedural exterior)',
                                    DIMENSIONS, CAMERA, DRIVE_SCALE)
    a = t.Assembly(md)
    parts(a)
    info['muzzle_details'] = info['muzzles']
    info['muzzles'] = _weapon_muzzles(a)
    info['visible_barrels'] = 2
    info['road_wheels_per_side'] = 6
    info['missile_tubes'] = 4
    return arc, md, info


def build(game) -> bytes:
    return build_with_info(game)[0]


def check(arc: bytes, host=None) -> dict:
    result = t.check(arc, parts, host)
    result['visible_barrels'] = 2
    result['road_wheels_per_side'] = 6
    result['missile_tubes'] = 4
    return result


def animation(game, md=None) -> bytes:
    return t.animation(game, md)


def main(argv: list[str]) -> int:
    import json
    import rootcpk
    folder = argv[1] if len(argv)>1 else 'tmp/bmpt-model'
    os.makedirs(folder, exist_ok=True)
    arc, md, info = build_with_info(rootcpk.default())
    with open(os.path.join(folder, OUT_ARC), 'wb') as f:
        f.write(arc)
    t.dump_obj(md, os.path.join(folder, 'bmpt.obj'))
    with open(os.path.join(folder, 'bmpt.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: info[k] for k in ('triangles', 'bone_count', 'visible_barrels', 'missile_tubes', 'muzzles')}, indent=2))
    return 0


if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv))
