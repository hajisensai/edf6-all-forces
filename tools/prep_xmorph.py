"""Developer-only: X-Morph: Defense's human units (pylib/xm_units.py) into the model folders the installer builds them
from, read from the developer's own copy of the game (its packs, read only; docs/xmorph-port.md). The folders go into
the model pack (obj_model.DEV_MODELS, like sazabi / dreadnought), never into the public repository:

    python tools/prep_xmorph.py tracked|emplacement [unit ...] [--out DIR] [--game DIR]

    tracked     every unit of the 'tracked' family (or the named ones: 'xm_medium_tank_regular' or
                'medium_tank_regular'), whatever its status: <out>/xm_<name>/ in tools/prep_wt_tracked.py's format
                (model.json FORMAT 1, model.npz, textures/, LICENSE.txt), which pylib/wt_tracked.py builds on the V603
    emplacement the forts' guns: <out>/xm_<name>/ in tools/prep_glb_model.py's format (parts by role, pivots), which
                pylib/emplacement_model.py puts on its fortified base (prepare_emplacement)

It needs numpy, Pillow and scipy. The packs are data: nothing in them runs.

How a unit becomes a V603 (everything is read off its entity blueprint and skeleton; a new unit needs no entry here
unless its blueprint misleads, MOUTHS):
    the main weapon     the blueprint's first weapon aimed by a TurretComp turret whose yaw is a bone
    cannon_main         that turret's yaw bone and every bone under it (but the gun), and the hull's bones that stand
                        on the turret's ring (a radar mast the hull carries over the turret's axis)
    cannon_l            the turret's pitch bone and everything under it but the recoil bones (one gun: one cradle)
    cannon_slide_l, _r  the main weapon's muzzles' recoil bones (or the gun's recoil* bones): two, by side; one, left
    muzzles             the main weapon's muzzle attachments (or bones): two, by side; one, both sides
    doppler_radar,      no mesh: the native accessory proxies, fitted to the turret's top slice, its left and right
    tracking_radar      halves (the private CAS keeps them still on the turret)
    tire_moveA..H       synthetic road wheels inside the track belts (X-Morph's wheels are baked into the hull):
                        cylinders in the hull's texture, hidden in the belt, for the V603's contact radius and stations
    catapi_body         the belts' running faces, under the native track materials' names (v603_kyata_L / _R)
    xm_<arm>, _gun      an arm with its own turret (xm_units.Arm.turret, its gunner seat): the blueprint's turret of
                        its weapon, its yaw bone's subtree (but the pitch bone's) and its pitch bone's subtree, two
                        visual leaves of `body` at those bones' binds (src/seat_turret.inc turns them)
    body                the rest (the hull, its own turrets and launchers: they stay put)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'pylib')]

import obj_model as om          # noqa: E402
import prep_wt_tracked as prep  # noqa: E402
import xm_model as xm           # noqa: E402
import xm_pack as xp            # noqa: E402
import xm_units                 # noqa: E402

SCALE = 1.0          # X-Morph's metres are real (its civilian cars: 4.4 x 1.8 x 1.45 m); its tanks are just big
GROUND = 0.15        # m: a hull triangle wholly under this height is the belts' ground run
LOW_BELT = 0.35      # m: a slab face reaching under this height is the belt's lower run (between ground pads)
BELT_HOLE = 0.30     # m: a stretch of the slab this short without low geometry still joins two runs (a hole between faces)
GAP = 0.20           # m: between sibling proxies' collision (tracked_collision._sweep's M61 gap: skins + margin)
TOP_SLICE = 0.30     # the turret's top this fraction of its height: the two radar proxies' room
WHEEL_STATIONS = 'ABCDEFGH'
WHEEL_SEGMENTS = 12

# A unit whose blueprint's main weapon fires from somewhere its guns are not: (left, right) attachments. The AA tank's
# main weapon names att_muzzle_3, which sits between its two gun pods at the turret's middle; att_muzzle_1 / _2 (on
# barrel_1, y +-1.52) are the pods' mouths.
MOUTHS = {'xm_medium_tank_aa': ('att_muzzle_1', 'att_muzzle_2')}

LICENSE = """X-Morph: Defense (c) EXOR Studios.

{title}: converted by tools/prep_xmorph.py from the developer's own copy of the game ({entity}, {mesh}) for the
EDF6 All Forces model pack. These are the game's assets, not this project's: they are not in the public repository and
are not licensed for redistribution; the model pack carries them for the user's own installation only.
"""


class PrepError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise PrepError(message)


# ------------------------------------------------------------------------------------------ the rig

def main_weapon(um: xm.UnitModel) -> tuple[xm.Weapon, str, str]:
    """(weapon, yaw bone, pitch bone) of the blueprint's first turret-aimed weapon whose turret turns a bone."""
    turrets = um.turrets()
    for w in um.weapons():
        yaw, pitch = turrets.get(w.turret, (None, None))
        if yaw in um.bind and pitch in um.bind:
            return w, yaw, pitch
    raise PrepError(f'{um.entity}: no weapon on a turret with yaw and pitch bones')


def meshed(um: xm.UnitModel, bones) -> list[str]:  # noqa: ANN001
    have = set(um.tri_bone)
    return [b for b in bones if b in have]


def arm_turrets(um: xm.UnitModel, unit: xm_units.Unit) -> dict[str, tuple[str, str]]:
    """The unit's arms with their own turret (Arm.turret): arm key -> the blueprint's (yaw bone, pitch bone) of the
    turret its weapon is aimed by."""
    turrets, weapons = um.turrets(), {w.name: w for w in um.weapons()}
    out = {}
    for arm in unit.arms:
        if not arm.turret:
            continue
        _req(len(arm.weapons) == 1 and arm.weapons[0] in weapons, f'{unit.key}: arm {arm.key}: one blueprint weapon a turret')
        yaw, pitch = turrets.get(weapons[arm.weapons[0]].turret, (None, None))
        _req(yaw in um.bind and pitch in um.bind and pitch in um.subtree(yaw),
             f'{unit.key}: arm {arm.key}: its turret has no yaw bone over a pitch bone ({yaw}, {pitch})')
        out[arm.key] = (yaw, pitch)
    return out


def rig(um: xm.UnitModel, key: str, unit: xm_units.Unit | None = None) -> dict:
    """The bone assignment (module doc): X-Morph bone -> V603 bone, the gun's sides and mouths."""
    w, yaw, pitch = main_weapon(um)
    bone: dict[str, str] = {}
    own = arm_turrets(um, unit) if unit else {}
    for k, (ty, tp) in own.items():
        tyb, tpb = xm_units.turret_bones(k)
        for b in um.subtree(tp):
            bone[b] = tpb
        for b in um.subtree(ty):
            bone.setdefault(b, tyb)
    gun = [b for b in um.subtree(pitch) if b not in bone]
    recoils = []
    for _att, r in w.muzzles:
        if r and r in um.bind and r not in recoils:
            recoils.append(r)
    if not recoils:
        recoils = [b for b in gun if b.startswith('recoil')]
    recoils = meshed(um, recoils)
    _req(len(recoils) <= 2, f'{key}: {len(recoils)} recoil bones')
    names = MOUTHS.get(key) or [a for a, _r in w.muzzles]
    mouths = [um.point(n) for n in names]
    _req(1 <= len(mouths) <= 2, f'{key}: {len(mouths)} muzzles on the main weapon')
    if len(mouths) == 2:
        left, right = sorted(mouths, key=lambda p: -p[0])        # +x is the vehicle's left
    else:
        left = right = mouths[0]
    sides = sorted(recoils, key=lambda b: -um.bind[b][0])
    slides = dict(zip(sides, ['cannon_slide_l', 'cannon_slide_r'])) if len(sides) == 2 else \
        {b: 'cannon_slide_l' for b in sides}
    for r, slide in slides.items():
        for b in um.subtree(r):
            bone[b] = slide
    for b in gun:
        bone.setdefault(b, 'cannon_l')
    # the turret: the yaw bone's subtree, and hull bones standing on its ring (over its axis, inside its radius)
    axis = um.bind[yaw]
    ring = um.bone_points([b for b in um.subtree(yaw) if b not in gun])
    _req(len(ring) > 0, f'{key}: the turret {yaw} has no mesh')
    radius = float(np.max(np.hypot(ring[:, 0] - axis[0], ring[:, 2] - axis[2])))
    for b in um.subtree(yaw):
        bone.setdefault(b, 'cannon_main')
    for b in um.bones:
        if b in bone or b == um.bones[0] or yaw in _ancestors(um, b):
            continue
        p = um.bind[b]
        if np.hypot(p[0] - axis[0], p[2] - axis[2]) < radius and p[1] > axis[1]:
            for c in um.subtree(b):
                bone.setdefault(c, 'cannon_main')
    for b in um.bones:
        bone.setdefault(b, 'body')
    barrel = {'left': None, 'right': None}
    for side, slide in (('left', 'cannon_slide_l'), ('right', 'cannon_slide_r' if len(sides) == 2 else 'cannon_slide_l')):
        nodes = [b for b in meshed(um, um.bones) if bone[b] == slide] or meshed(um, gun)
        _req(bool(nodes), f'{key}: the gun has no mesh')
        barrel[side] = nodes[0]
    return {'weapon': w.name, 'yaw': yaw, 'pitch': pitch, 'bone': bone, 'recoils': sides, 'barrel': barrel,
            'mouths': {'left': left, 'right': right}, 'muzzle_names': list(names), 'turrets': own}


def _ancestors(um: xm.UnitModel, b: str) -> list[str]:
    out = []
    while um.parent[b] is not None:
        b = um.parent[b]
        out.append(b)
    return out


# ------------------------------------------------------------------------------------------ the running gear

def _merge(intervals: list[tuple[float, float]], tol: float = 0.3) -> list[tuple[float, float]]:
    out: list[list[float]] = []
    for a, b in sorted(intervals):
        if out and a <= out[-1][1] + tol:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return [(a, b) for a, b in out]


def running_gear(um: xm.UnitModel, bone: dict[str, str]) -> dict:
    """Per side ('l': +x, 'r': -x): the belt's ground run (lateral band, its runs along z), its top, the eight
    stations' wheels (centre, radius, half width) and the hull triangles that are the belt's running faces."""
    P, T = um.positions, um.triangles
    hull = np.asarray([bone[b] == 'body' for b in um.tri_bone])
    tri = P[T]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-20)
    low = hull & (tri[:, :, 1].max(axis=1) < GROUND)
    out = {}
    for side, sign in (('l', 1.0), ('r', -1.0)):
        g = low & (tri[:, :, 0].mean(axis=1) * sign > 0.2)
        _req(bool(g.any()), f'{um.entity}: no ground run on the {side} side')
        lat = tri[g][:, :, 0].reshape(-1) * sign
        band = (float(lat.min()), float(lat.max()))
        # the belt: hull triangles inside the band's lateral slab
        slab = hull & (tri[:, :, 0] * sign >= band[0] - 0.05).all(axis=1) & (tri[:, :, 0] * sign <= band[1] + 0.05).all(axis=1)
        # its runs: the ground faces' stretches, joined where the slab's geometry stays low between them (a belt whose
        # running face lifts off the ground between two contact pads), split where it does not (separate track pods)
        runs = _merge([(float(t[:, 2].min()), float(t[:, 2].max())) for t in tri[g]], tol=0.0)
        lowz = tri[slab & (tri[:, :, 1].min(axis=1) < LOW_BELT)][:, :, 2]
        joined = [list(runs[0])]
        for a, b in runs[1:]:
            gap = np.arange(joined[-1][1], a, 0.1)
            covered = all(((lowz.min(axis=1) <= z + BELT_HOLE) & (lowz.max(axis=1) >= z - BELT_HOLE)).any() for z in gap)
            if covered:
                joined[-1][1] = b
            else:
                joined.append([a, b])
        runs = [(a, b) for a, b in joined]
        z0, z1 = runs[0][0], runs[-1][1]
        mid = slab & (tri[:, :, 2].min(axis=1) >= z0) & (tri[:, :, 2].max(axis=1) <= z1)
        # its top: the lowest upward face over the run (the belt's upper run; the hull over a belt as wide as it,
        # sponsons and skirts, is higher), the median over the run's length
        up = mid & (n[:, 1] > 0.7) & (tri[:, :, 1].min(axis=1) > 2 * GROUND)
        bins = np.linspace(z0, z1, 9)
        tops = []
        for a, b in zip(bins[:-1], bins[1:]):
            s = up & (tri[:, :, 2].mean(axis=1) >= a) & (tri[:, :, 2].mean(axis=1) < b)
            if s.any():
                tops.append(float(tri[s][:, :, 1].mean(axis=1).min()))
        _req(bool(tops), f'{um.entity}: no belt over the {side} run')
        top = float(np.median(tops))
        length = sum(b - a for a, b in runs)
        radius = min(0.42 * length / len(WHEEL_STATIONS), 0.4 * top)
        # stations front (A) to rear (H), evenly along the ground runs' length
        stations = []
        for k in range(len(WHEEL_STATIONS)):
            s = (k + 0.5) / len(WHEEL_STATIONS) * length
            for a, b in sorted(runs, key=lambda r: -r[1]):
                if s <= b - a:
                    stations.append(b - s)
                    break
                s -= b - a
        centre_x = sign * (band[0] + band[1]) / 2
        half = 0.3 * (band[1] - band[0])
        wheels = [((centre_x, radius, z), radius, half) for z in stations]
        # the running faces: the slab's faces not facing sideways, from the run's front slope to its rear one
        reach = 1.5 * top
        belt = slab & (tri[:, :, 1].max(axis=1) <= top + 0.05) & (np.abs(n[:, 0]) < 0.5) & \
            (tri[:, :, 2].min(axis=1) >= z0 - reach) & (tri[:, :, 2].max(axis=1) <= z1 + reach)
        out[side] = {'band': band, 'runs': runs, 'top': top, 'wheels': wheels, 'belt': belt,
                     'ground_uv': um.uvs[T[np.flatnonzero(g)[0], 0]]}
    return out


def wheel_mesh(centre, radius: float, half: float) -> tuple[np.ndarray, np.ndarray]:  # noqa: ANN001
    """A closed cylinder across the vehicle (axis x): (positions, triangles), wound outward."""
    import procmesh
    part = procmesh.Part(0)
    x, y, z = centre
    procmesh.hard_cylinder(part, (x - half, y, z), (x + half, y, z), radius, [(0, 1.0)], WHEEL_SEGMENTS)
    return np.asarray(part.pos), np.asarray(part.tris, dtype=np.int64)


# ------------------------------------------------------------------------------------------ the Source

def tracked(packs: xp.Packs, unit: xm_units.Unit, staging: str) -> tuple[prep.Vehicle, prep.Source, dict]:
    """The unit as prep_wt_tracked's Vehicle and Source (its textures written as PNGs into `staging`)."""
    um = xm.load(packs, unit.entity)
    key = unit.key
    r = rig(um, key, unit)
    bone = r['bone']
    gear = running_gear(um, bone)
    # objects: one per X-Morph bone, the belts' running faces, the synthetic wheels; named as Source names them (its
    # OBJ objects: '<node>_<object number>', prep_wt_tracked.node() drops the number)
    nodes = list(um.bones)
    tri_obj = np.asarray([nodes.index(b) for b in um.tri_bone], dtype=np.int64)
    for side in 'lr':
        nodes.append(f'track_{side}')
        tri_obj[gear[side]['belt']] = len(nodes) - 1
    positions = [um.positions * SCALE]
    uvs = [um.uvs]
    tri_v = [um.triangles]
    tri_m = [um.tri_material]
    base = len(um.positions)
    hull_mat = int(um.tri_material[np.flatnonzero(um.tri_bone == um.bones[0])[0]])
    obj = [tri_obj]
    for side in 'lr':
        for letter, (centre, radius, half) in zip(WHEEL_STATIONS, gear[side]['wheels']):
            p, t = wheel_mesh(centre, radius, half)
            nodes.append(f'wheel_{side}_{letter}')
            positions.append(p * SCALE)
            uvs.append(np.tile(gear[side]['ground_uv'], (len(p), 1)))   # hidden: one texel of the belt's
            tri_v.append(t + base)
            tri_m.append(np.full(len(t), hull_mat))
            obj.append(np.full(len(t), len(nodes) - 1))
            base += len(p)
    uv = np.vstack(uvs)
    src_uv = np.column_stack((uv[:, 0], -uv[:, 1]))   # prepare() writes (u, -v): its OBJ sources' v is negated
    tv = np.vstack(tri_v)
    # textures: each material's diffuse (and normal) as PNGs where prepare() reads its MTL's files
    mtl = {}
    for m in um.materials:
        stem = m.split('/')[-1]
        imgs = xm.texture_images(packs, m)
        imgs['diffuse'].save(os.path.join(staging, f'{stem}.png'), optimize=True)
        mtl[m] = {'map_Kd': f'{stem}.png'}
        if 'normal' in imgs:
            imgs['normal'].save(os.path.join(staging, f'{stem}_normal.png'), optimize=True)
            mtl[m]['map_normal'] = f'{stem}_normal.png'
    src = prep.Source(os.path.join(staging, os.path.basename(um.mesh_path)), np.vstack(positions), src_uv, tv,
                      tv.copy(), np.concatenate(obj), np.concatenate(tri_m), [f'{n}_{k}' for k, n in enumerate(nodes)],
                      list(um.materials), mtl)

    # bones: by node name (exact)
    rules = [(r'^wheel_([lr])_([A-H])$', 'tire_move{1}_{0}'), (r'^track_([lr])$', 'catapi_body')]
    rules += [('^' + re.escape(b) + '$', v) for b, v in bone.items() if v != 'body']
    yaw, pitch = r['yaw'], r['pitch']
    turret_nodes = meshed(um, [b for b, v in bone.items() if v == 'cannon_main'])
    gun_nodes = meshed(um, [b for b, v in bone.items() if v in ('cannon_l', 'cannon_slide_l', 'cannon_slide_r')])
    rx = lambda nodes: '^(' + '|'.join(re.escape(n) for n in nodes) + ')$'   # noqa: E731

    # collision: the gun split in two siblings (sideways between two mouths apart, else along the bore), the turret's
    # top slice the radars' (left / right halves), the rest of the turret cannon_main's
    left, right = r['mouths']['left'], r['mouths']['right']
    clip: dict = {}
    gun_pts = um.bone_points(gun_nodes) * SCALE
    if abs(left[0] - right[0]) * SCALE > 0.5:
        mid = (left[0] + right[0]) / 2 * SCALE
        clip['cannon_l'] = (0, 'min', mid + GAP / 2)
        clip['cannon_r'] = (0, 'max', mid - GAP / 2)
        split = ('sideways', mid)
    else:
        slides = meshed(um, r['recoils'])
        cradle = meshed(um, [b for b in gun_nodes if bone[b] == 'cannon_l'])
        if slides and cradle:
            s = float(um.bone_points(slides)[:, 2].min()) * SCALE
        else:
            s = float(gun_pts[:, 2].min() + gun_pts[:, 2].max()) / 2
        clip['cannon_l'] = (2, 'min', s + GAP / 2)        # the barrel's front
        clip['cannon_r'] = (2, 'max', s - GAP / 2)        # the cradle behind it
        split = ('along the bore', s)
    tur = um.bone_points(turret_nodes) * SCALE
    cut = float(tur[:, 1].max() - TOP_SLICE * (tur[:, 1].max() - tur[:, 1].min()))
    x0 = float(um.bind[yaw][0]) * SCALE
    clip['cannon_main'] = (1, 'max', cut - GAP / 2)
    clip['doppler_radar'] = [(1, 'min', cut + GAP / 2), (0, 'min', x0 + GAP / 2)]
    clip['tracking_radar'] = [(1, 'min', cut + GAP / 2), (0, 'max', x0 - GAP / 2)]
    collision = {'cannon_main': [rx(turret_nodes)], 'cannon_l': [rx(gun_nodes)], 'cannon_r': [rx(gun_nodes)],
                 'doppler_radar': [rx(turret_nodes)], 'tracking_radar': [rx(turret_nodes)]}

    p = um.bind[pitch] * SCALE
    v = prep.Vehicle(
        kind=key, folder=xm_units.folder(key), title=f'{unit.title_en or key} (X-Morph: Defense)',
        source_url=f'X-Morph: Defense (EXOR Studios), entities/units/{unit.entity}.ent',
        rules=rules, turret=tuple(float(c) for c in um.bind[yaw] * SCALE),
        trunnion={'cannon_l': (float(p[1]), float(p[2])), 'cannon_r': (float(p[1]), float(p[2]))},
        barrels={'left': r['barrel']['left'], 'right': r['barrel']['right']},
        collision=collision, clip=clip, hull=um.bones[0],
        mouths={s: tuple(float(c) for c in r['mouths'][s] * SCALE) for s in ('left', 'right')})
    report = {'entity': unit.entity, 'mesh': um.mesh_path, 'weapon': r['weapon'], 'yaw': yaw, 'pitch': pitch,
              'turrets': r['turrets'],
              'muzzles': r['muzzle_names'], 'recoils': r['recoils'],
              'bones': {b: bone[b] for b in um.bones}, 'gun_split': split, 'radar_slice_y': cut,
              'running_gear': {s: {'band_x': gear[s]['band'], 'runs_z': gear[s]['runs'], 'belt_top': gear[s]['top'],
                                   'wheel_radius': gear[s]['wheels'][0][1], 'belt_faces': int(gear[s]['belt'].sum())}
                               for s in 'lr'}}
    return v, src, report


def arm_muzzles(packs: xp.Packs, unit: xm_units.Unit) -> dict[str, list[dict]]:
    """The unit's other weapons' muzzles (xm_units.Arm: muzzle slot xm_<key>), model space, on the arm's holder bone:
    each of its game weapons' Muzzle attachments (or bones), in the blueprint's order."""
    um = xm.load(packs, unit.entity)
    weapons = {w.name: w for w in um.weapons()}
    out = {}
    for arm in unit.arms:
        missing = [w for w in arm.weapons if w not in weapons]
        _req(not missing, f'{unit.key}: arm {arm.key}: no weapon {missing} in its blueprint')
        points = [um.point(m) * SCALE for w in arm.weapons for m, _r in weapons[w].muzzles]
        _req(bool(points), f'{unit.key}: arm {arm.key} has no muzzle')
        out[f'xm_{arm.key}'] = [{'bone': arm.holder_bone, 'point': [float(c) for c in p]} for p in points]
    return out


def prepare_tracked(packs: xp.Packs, unit: xm_units.Unit, out_root: str) -> dict:
    out = os.path.join(out_root, xm_units.folder(unit.key))
    with tempfile.TemporaryDirectory(prefix='prep_xmorph_') as staging:
        v, src, report = tracked(packs, unit, staging)
        if os.path.isdir(out):
            shutil.rmtree(out)
        os.makedirs(out)
        info = prep.prepare(v, src, out)
    _req(not info['winding_flipped'], f'{unit.key}: the hull winds clockwise (the frame change is a rotation)')
    info['muzzles'].update(arm_muzzles(packs, unit))
    # the arms' own turrets: two visual leaves of the hull each, at the blueprint's yaw and pitch bones' binds
    um = xm.load(packs, unit.entity)
    for k, (ty, tp) in report['turrets'].items():
        for name, src in zip(xm_units.turret_bones(k), (ty, tp)):
            info['visual_bones'].append({'name': name, 'parent': 'body', 'origin': [float(c) for c in um.bind[src] * SCALE]})
    # the conversion's record (wt_tracked reads neither key)
    info['xmorph'] = {**report, 'scale': SCALE}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1, sort_keys=False)
        f.write('\n')
    with open(os.path.join(out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(LICENSE.format(title=v.title, entity=f'entities/units/{unit.entity}.ent', mesh=report['mesh']))
    return info


# ------------------------------------------------------------------------------------------ emplacements

ROLES = {'body': 'base', 'cannon_main': 'turret', 'cannon_l': 'gun', 'cannon_slide_l': 'gun', 'cannon_slide_r': 'gun'}


def prepare_emplacement(packs: xp.Packs, unit: xm_units.Unit, out_root: str) -> dict:
    """An emplacement (a fort's gun) into tools/prep_glb_model.py's folder format, which pylib/emplacement_model.py
    puts on its fortified base: the rig's bones (rig()) as roles (the hull's: base, the turret's: turret, the gun's:
    gun), one part a role and material; the pivots off the skeleton (the turret's yaw bone its ring, the gun's pitch
    bone its trunnion) and the main weapon's mouths."""
    import prep_glb_model as glb
    um = xm.load(packs, unit.entity)
    r = rig(um, unit.key)
    out = os.path.join(out_root, xm_units.folder(unit.key))
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'textures'))
    glb.plating(out)
    role = np.asarray([ROLES[r['bone'][b]] for b in um.tri_bone], dtype=object)
    P = um.positions * SCALE
    materials, arrays, parts = [], {}, []
    for mi, m in enumerate(um.materials):
        stem = f'{unit.key}_{mi}'
        imgs = xm.texture_images(packs, m)
        imgs['diffuse'].save(os.path.join(out, 'textures', f'{stem}.png'), optimize=True)
        materials.append({'name': f'edf6vc_{stem}', 'template': 'v603_body', 'albedo': f'textures/{stem}.png',
                          'source': m})
        if 'normal' in imgs:      # DirectX green already (xm_model.normal_rgb), as the folders keep it
            imgs['normal'].save(os.path.join(out, 'textures', f'{stem}_normal.png'), optimize=True)
            materials[-1]['normal'] = f'textures/{stem}_normal.png'
    for rl in glb.ROLES:
        for mi in range(len(um.materials)):
            sel = np.flatnonzero((role == rl) & (um.tri_material == mi))
            if not len(sel):
                continue
            tri = um.triangles[sel]
            idx, inv = np.unique(tri, return_inverse=True)
            pos, t = P[idx], inv.reshape(-1, 3)
            nrm = np.zeros_like(pos)          # the faces' own, per vertex (the mesh's normals are not read)
            fn = np.cross(pos[t[:, 1]] - pos[t[:, 0]], pos[t[:, 2]] - pos[t[:, 0]])
            for c in range(3):
                np.add.at(nrm, t[:, c], fn)
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
            k = len(parts)
            arrays[f'p{k}_pos'] = pos.astype(np.float32)
            arrays[f'p{k}_nrm'] = nrm.astype(np.float32)
            arrays[f'p{k}_uv'] = um.uvs[idx].astype(np.float32)   # Ogre's top-left origin, as the folders keep them
            arrays[f'p{k}_tri'] = t.astype(np.uint32)
            parts.append({'name': f'{rl}.{materials[mi]["name"]}', 'role': rl, 'material': mi,
                          'triangles': int(len(sel)), 'vertices': int(len(idx))})
    _req({p['role'] for p in parts} >= {'base', 'turret', 'gun'}, f'{unit.key}: a role without mesh {parts}')
    yaw, pitch = um.bind[r['yaw']] * SCALE, um.bind[r['pitch']] * SCALE
    mouths = {s: [float(c) for c in r['mouths'][s] * SCALE] for s in ('left', 'right')}
    pivots = {'ring': [float(yaw[0]), float(yaw[2])], 'trunnion': [float(pitch[1]), float(pitch[2])], 'muzzles': mouths}
    np.savez_compressed(os.path.join(out, 'model.npz'), **arrays)
    used = np.unique(um.triangles)
    info = {'format': glb.FORMAT, 'kind': unit.key, 'title': f'{unit.title_en or unit.key} (X-Morph: Defense)',
            'prepared_from': um.mesh_path, 'scale': SCALE, 'yaw': 0.0, 'materials': materials, 'parts': parts,
            'pivots': pivots, 'triangles': int(sum(p['triangles'] for p in parts)),
            'size': (P[used].max(0) - P[used].min(0)).tolist(),
            'xmorph': {'entity': unit.entity, 'weapon': r['weapon'], 'yaw': r['yaw'], 'pitch': r['pitch'],
                       'muzzles': r['muzzle_names'], 'bones': {b: r['bone'][b] for b in um.bones}}}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1)
        f.write('\n')
    with open(os.path.join(out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(LICENSE.format(title=info['title'], entity=f'entities/units/{unit.entity}.ent', mesh=um.mesh_path))
    return info


PREPARERS = {'tracked': prepare_tracked, 'emplacement': prepare_emplacement}


def select(family: str, names: list[str]) -> list[xm_units.Unit]:
    units = [u for u in xm_units.UNITS.values() if u.family == family]
    if not names:
        return units
    want = {n if n.startswith('xm_') else 'xm_' + n for n in names}
    unknown = want - {u.key for u in units}
    _req(not unknown, f'not {family} units: {sorted(unknown)}')
    return [u for u in units if u.key in want]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('family', choices=sorted(PREPARERS))
    ap.add_argument('units', nargs='*', help='unit keys (default: the whole family)')
    ap.add_argument('--out', default=om.DEV_MODELS, help=f'the models root (default {om.DEV_MODELS})')
    ap.add_argument('--game', default=xp.STEAM_DIR, help='the X-Morph: Defense folder')
    a = ap.parse_args(argv)
    packs = xp.Packs(a.game)
    try:
        for u in select(a.family, a.units):
            info = PREPARERS[a.family](packs, u, a.out)
            print(json.dumps({k: info[k] for k in ('kind', 'triangles', 'materials', 'origins', 'muzzles', 'pivots')
                              if k in info} | {'parts': len(info['parts'])}, indent=1, default=str))
    finally:
        packs.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
