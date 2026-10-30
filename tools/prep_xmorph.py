"""Developer-only: X-Morph: Defense's human units (pylib/xm_units.py) into the model folders the installer builds them
from, read from the developer's own copy of the game (its packs, read only; docs/xmorph-port.md). The folders go into
the model pack (obj_model.DEV_MODELS, like sazabi / dreadnought), never into the public repository:

    python tools/prep_xmorph.py tracked|wheeled|emplacement [unit ...] [--out DIR] [--game DIR]

    tracked     every unit of the 'tracked' family that names a like (or the named ones: 'xm_medium_tank_regular' or
                'medium_tank_regular'), whatever its status: <out>/xm_<name>/ in tools/prep_wt_tracked.py's format
                (model.json FORMAT 1, model.npz, textures/, LICENSE.txt), which pylib/wt_tracked.py builds on the V603
    wheeled     the 'wheeled' family the same way, on the same V603 (its real wheels on the wheel stations, wheels())
    emplacement the forts' guns: <out>/xm_<name>/ in tools/prep_glb_model.py's format (parts by role, pivots), which
                pylib/emplacement_model.py puts on its fortified base (prepare_emplacement)

It needs numpy, Pillow and scipy. The packs are data: nothing in them runs.

How a unit becomes a V603 (everything is read off its entity blueprint and skeleton; a new unit needs no entry here
unless its blueprint misleads, MOUTHS):
    the main weapon     the blueprint's first weapon aimed by a TurretComp turret whose yaw is a bone (a weapon no
                        turret aims, the missile launcher's: the blueprint's one turret that turns a bone)
    cannon_main         that turret's yaw bone and every bone under it (but the gun), and the hull's bones that stand
                        on the turret's ring (a radar mast the hull carries over the turret's axis)
                        (a turret whose yaw and pitch are one bone, the launcher's rack: a synthetic turntable at
                        that bone, hidden in the rack)
    cannon_l            the turret's pitch bone and everything under it but the recoil bones (one gun: one cradle)
    cannon_slide_l, _r  the main weapon's muzzles' recoil bones (or the gun's recoil* bones): two, by side; one, left
    muzzles             the main weapon's muzzle attachments (or bones): two, by side; one, both sides
    doppler_radar,      no mesh: the native accessory proxies, fitted to the turret's top slice, its left and right
    tracking_radar      halves (the private CAS keeps them still on the turret)
    tire_moveA..H       synthetic road wheels inside the track belts (X-Morph's wheels are baked into the hull):
                        cylinders in the hull's texture, hidden in the belt, for the V603's contact radius and stations
    catapi_body         the belts' running faces, under the native track materials' names (v603_kyata_L / _R)
    body                the rest (the hull, its own turrets and launchers: they stay put)

A wheeled unit (wheels()): its wheels are the hull's round pieces on the ground (one connected piece each: X-Morph's
wheels are baked into the hull, or on an axle bone across both sides), per side front to rear. The V603's six sprung
stations B..G a side are spread over them (the first on the front wheel, the last on the rear one; a wheel with
several stations is one wheel to the springs, as the Katyusha's axles on the Naegling), A on the front wheel and H on
the rear one (drawn only). Each wheel's own mesh rides its first station; its other stations get a cylinder hidden in
the tyre (WHEEL_HIDDEN of its radius: the contact the tyre's). The belts are a sliver hidden in each side's front hub
under the two track materials (the class binds both names; make_tracked_combat STATIC_TRACKS stops their scroll). It
turns as the V603 does (skid steering), not by its front wheels.
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
# A wheeled unit's wheels (wheels()): a hull piece touching the ground (lower than WHEEL_GROUND) as tall as it is long
# (within WHEEL_ROUND of its size), at least WHEEL_MIN tall, narrower than WHEEL_WIDE, off the middle.
WHEEL_GROUND = 0.2
WHEEL_ROUND = 0.15
WHEEL_MIN = 0.5
WHEEL_WIDE = 1.0
WHEEL_HIDDEN = 0.98     # a wheel's other stations' hidden cylinders: of its radius (inside the tread)
SPRUNG = 'BCDEFG'       # the V603's sprung stations (tracked_collision._wheel_overrides); A and H are drawn only
SLIVER = 0.03           # m: the hidden belts' radius and half width
TURNTABLE = (0.5, 0.6, 0.1)   # m: a one-bone turret's turntable: its radius, its foot and top under the pivot

# A unit whose blueprint's main weapon fires from somewhere its guns are not: (left, right) attachments. The AA tank's
# main weapon names att_muzzle_3, which sits between its two gun pods at the turret's middle; att_muzzle_1 / _2 (on
# barrel_1, y +-1.52) are the pods' mouths.
MOUTHS = {'xm_medium_tank_aa': ('att_muzzle_1', 'att_muzzle_2'),
          # The missile launcher's four tubes (att_muzzle_1..4, a missile at a time): the upper pair (x +-0.66, y 3.11;
          # _3 / _4 the lower pair, y 1.98) are the native holders' two; the lower tubes ride the rack.
          'xm_medium_missile_launcher': ('att_muzzle_1', 'att_muzzle_2')}

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
    """(weapon, yaw bone, pitch bone) of the blueprint's first turret-aimed weapon whose turret turns a bone; a weapon
    no turret aims (the missile launcher's ManualWeaponController) takes the blueprint's one turret that does."""
    turrets = um.turrets()
    boned = [(y, p) for y, p in turrets.values() if y in um.bind and p in um.bind]
    for w in um.weapons():
        if w.turret:
            yaw, pitch = turrets.get(w.turret, (None, None))
        else:
            yaw, pitch = boned[0] if len(boned) == 1 else (None, None)
        if yaw in um.bind and pitch in um.bind:
            return w, yaw, pitch
    raise PrepError(f'{um.entity}: no weapon on a turret with yaw and pitch bones')


def hull_bone(um: xm.UnitModel) -> str:
    """The hull's bone: 'body' (the APC's skeleton starts with its doors' roots), else the skeleton's first."""
    return 'body' if 'body' in um.bind else um.bones[0]


def meshed(um: xm.UnitModel, bones) -> list[str]:  # noqa: ANN001
    have = set(um.tri_bone)
    return [b for b in bones if b in have]


def rig(um: xm.UnitModel, key: str) -> dict:
    """The bone assignment (module doc): X-Morph bone -> V603 bone, the gun's sides and mouths."""
    w, yaw, pitch = main_weapon(um)
    gun = um.subtree(pitch)
    names = MOUTHS.get(key) or [a for a, _r in w.muzzles]
    recoils = []
    for att, r in w.muzzles:
        if att in names and r and r in um.bind and r not in recoils:
            recoils.append(r)
    if not recoils:
        recoils = [b for b in gun if b.startswith('recoil')]
    recoils = meshed(um, recoils)
    _req(len(recoils) <= 2, f'{key}: {len(recoils)} recoil bones')
    mouths = [um.point(n) for n in names]
    _req(1 <= len(mouths) <= 2, f'{key}: {len(mouths)} muzzles on the main weapon')
    if len(mouths) == 2:
        left, right = sorted(mouths, key=lambda p: -p[0])        # +x is the vehicle's left
    else:
        left = right = mouths[0]
    bone: dict[str, str] = {}
    sides = sorted(recoils, key=lambda b: -um.bind[b][0])
    slides = dict(zip(sides, ['cannon_slide_l', 'cannon_slide_r'])) if len(sides) == 2 else \
        {b: 'cannon_slide_l' for b in sides}
    for r, slide in slides.items():
        for b in um.subtree(r):
            bone[b] = slide
    for b in gun:
        bone.setdefault(b, 'cannon_l')
    # the turret: the yaw bone's subtree, and hull bones standing on its ring (over its axis, inside its radius); a
    # turret on one bone (its yaw and pitch: the launcher's rack, the gun) a synthetic turntable under that bone
    axis = um.bind[yaw]
    turntable = None
    if yaw == pitch:
        r0, foot, top = TURNTABLE
        turntable = {'centre': [float(axis[0]), float(axis[2])], 'y': [float(axis[1] - foot), float(axis[1] - top)],
                     'radius': r0}
        ring = np.asarray([[axis[0] + r0, axis[1], axis[2]]])
    else:
        ring = um.bone_points([b for b in um.subtree(yaw) if b not in gun])
    _req(len(ring) > 0, f'{key}: the turret {yaw} has no mesh')
    radius = float(np.max(np.hypot(ring[:, 0] - axis[0], ring[:, 2] - axis[2])))
    for b in um.subtree(yaw):
        bone.setdefault(b, 'cannon_main')
    for b in um.bones:
        if b in bone or b == hull_bone(um) or um.parent[b] is None or yaw in _ancestors(um, b):
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
            'mouths': {'left': left, 'right': right}, 'muzzle_names': list(names), 'turntable': turntable}


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


def pieces(um: xm.UnitModel) -> np.ndarray:
    """(t,) each triangle's connected piece (corners welded at 1 mm: the mesh splits its vertices at UV seams)."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    _, vid = np.unique(np.round(um.positions / 1e-3).astype(np.int64), axis=0, return_inverse=True)
    t = vid.reshape(-1)[um.triangles]
    n = int(t.max()) + 1
    rows, cols = np.concatenate([t[:, 0], t[:, 1]]), np.concatenate([t[:, 1], t[:, 2]])
    _, label = connected_components(coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n)), directed=False)
    return label[t[:, 0]]


def wheels(um: xm.UnitModel, bone: dict[str, str]) -> dict:
    """Per side ('l': +x, 'r': -x), front to rear: the hull's wheels (module doc), each {'tris': its triangles,
    'centre', 'radius', 'half' (width / 2)}; and the stations A..H each on its wheel ('stations': letter -> index)."""
    piece = pieces(um)
    hull = np.asarray([bone[b] == 'body' for b in um.tri_bone])
    out = {side: [] for side in 'lr'}
    for c in np.unique(piece[hull]):
        sel = hull & (piece == c)
        pts = um.positions[np.unique(um.triangles[sel])]
        lo, hi = pts.min(0), pts.max(0)
        tall, long_, wide = hi[1] - lo[1], hi[2] - lo[2], hi[0] - lo[0]
        x = (lo[0] + hi[0]) / 2
        if lo[1] < WHEEL_GROUND and tall >= WHEEL_MIN and abs(tall - long_) <= WHEEL_ROUND * max(tall, long_) \
                and wide < WHEEL_WIDE and abs(x) > wide / 2:
            radius = float(min(tall, long_)) / 2
            centre = (float(x), float(lo[1] + hi[1]) / 2, float(lo[2] + hi[2]) / 2)
            out['l' if x > 0 else 'r'].append({'tris': sel, 'centre': centre, 'radius': radius,
                                               'half': float(wide) / 2})
    for side in 'lr':
        _req(len(out[side]) >= 2, f'{um.entity}: {len(out[side])} wheels on the {side} side')
        out[side].sort(key=lambda w: -w['centre'][2])
    _req(len(out['l']) == len(out['r']), f'{um.entity}: {len(out["l"])} wheels left, {len(out["r"])} right')
    n = len(out['l'])
    stations = {'A': 0, 'H': n - 1}
    stations.update({s: int(round(k * (n - 1) / (len(SPRUNG) - 1))) for k, s in enumerate(SPRUNG)})
    return {'sides': out, 'stations': dict(sorted(stations.items()))}


# ------------------------------------------------------------------------------------------ the running gear's objects

def tracked_gear(um: xm.UnitModel, bone: dict[str, str]) -> tuple[list, list, dict]:
    """A tracked hull's (objects, meshes, report): the belts' running faces (their hull triangles' node) and the
    synthetic road wheels (running_gear)."""
    gear = running_gear(um, bone)
    objects = [(f'track_{side}', gear[side]['belt']) for side in 'lr']
    meshes = []
    for side in 'lr':
        for letter, (centre, radius, half) in zip(WHEEL_STATIONS, gear[side]['wheels']):
            # hidden: one texel of the belt's
            meshes.append((f'wheel_{side}_{letter}', *wheel_mesh(centre, radius, half), gear[side]['ground_uv']))
    report = {s: {'band_x': gear[s]['band'], 'runs_z': gear[s]['runs'], 'belt_top': gear[s]['top'],
                  'wheel_radius': gear[s]['wheels'][0][1], 'belt_faces': int(gear[s]['belt'].sum())} for s in 'lr'}
    return objects, meshes, {'running_gear': report}


def wheeled_gear(um: xm.UnitModel, bone: dict[str, str]) -> tuple[list, list, dict]:
    """A wheeled hull's (objects, meshes, report): each wheel's own triangles on its first station, a hidden cylinder
    on each of its other stations, a hidden belt sliver in each side's front hub (module doc)."""
    w = wheels(um, bone)
    objects, meshes = [], []
    for side in 'lr':
        first: set[int] = set()
        for letter, i in w['stations'].items():
            wheel = w['sides'][side][i]
            uv = um.uvs[um.triangles[np.flatnonzero(wheel['tris'])[0], 0]]
            if i not in first:
                first.add(i)
                objects.append((f'wheel_{side}_{letter}', wheel['tris']))
            else:
                meshes.append((f'wheel_{side}_{letter}',
                               *wheel_mesh(wheel['centre'], WHEEL_HIDDEN * wheel['radius'], 0.6 * wheel['half']), uv))
        hub = w['sides'][side][0]
        uv = um.uvs[um.triangles[np.flatnonzero(hub['tris'])[0], 0]]
        meshes.append((f'track_{side}', *wheel_mesh(hub['centre'], SLIVER, SLIVER), uv))
    report = {'wheels': {s: [{'centre': list(x['centre']), 'radius': x['radius'], 'width': 2 * x['half'],
                              'triangles': int(x['tris'].sum())} for x in w['sides'][s]] for s in 'lr'},
              'stations': w['stations']}
    return objects, meshes, report


def turntable_mesh(t: dict) -> tuple[np.ndarray, np.ndarray]:
    """A one-bone turret's turntable (rig()): an upright cylinder under the pivot, wound outward."""
    import procmesh
    part = procmesh.Part(0)
    x, z = t['centre']
    procmesh.hard_cylinder(part, (x, t['y'][0], z), (x, t['y'][1], z), t['radius'], [(0, 1.0)], WHEEL_SEGMENTS)
    return np.asarray(part.pos), np.asarray(part.tris, dtype=np.int64)


GEAR = {'tracked': tracked_gear, 'wheeled': wheeled_gear}


# ------------------------------------------------------------------------------------------ the Source

def tracked(packs: xp.Packs, unit: xm_units.Unit, staging: str) -> tuple[prep.Vehicle, prep.Source, dict]:
    """The unit (a tracked or a wheeled hull: GEAR) as prep_wt_tracked's Vehicle and Source (its textures written as
    PNGs into `staging`)."""
    um = xm.load(packs, unit.entity)
    key = unit.key
    r = rig(um, key)
    bone = r['bone']
    objects, meshes, gear_report = GEAR[unit.family](um, bone)
    if r['turntable']:
        p, t = turntable_mesh(r['turntable'])
        meshes.append(('turntable', p, t, um.uvs[um.triangles[np.flatnonzero(um.tri_bone == r['yaw'])[0], 0]]))
    # objects: one per X-Morph bone, the running gear's (hull triangles of their own node, synthetic meshes); named
    # as Source names them (its OBJ objects: '<node>_<object number>', prep_wt_tracked.node() drops the number)
    nodes = list(um.bones)
    tri_obj = np.asarray([nodes.index(b) for b in um.tri_bone], dtype=np.int64)
    for name, sel in objects:
        nodes.append(name)
        tri_obj[sel] = len(nodes) - 1
    positions = [um.positions * SCALE]
    uvs = [um.uvs]
    tri_v = [um.triangles]
    tri_m = [um.tri_material]
    base = len(um.positions)
    hull_mat = int(um.tri_material[np.flatnonzero(um.tri_bone == hull_bone(um))[0]])
    obj = [tri_obj]
    synthetic = {}
    for name, p, t, uv in meshes:
        nodes.append(name)
        positions.append(p * SCALE)
        uvs.append(np.tile(uv, (len(p), 1)))
        tri_v.append(t + base)
        tri_m.append(np.full(len(t), hull_mat))
        obj.append(np.full(len(t), len(nodes) - 1))
        synthetic[name] = p * SCALE
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
    rules = [(r'^wheel_([lr])_([A-H])$', 'tire_move{1}_{0}'), (r'^track_([lr])$', 'catapi_body'),
             (r'^turntable$', 'cannon_main')]
    rules += [('^' + re.escape(b) + '$', v) for b, v in bone.items() if v != 'body']
    yaw, pitch = r['yaw'], r['pitch']
    turret_nodes = meshed(um, [b for b, v in bone.items() if v == 'cannon_main']) + \
        (['turntable'] if r['turntable'] else [])
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
    tur = np.vstack([um.bone_points(turret_nodes) * SCALE, synthetic.get('turntable', np.zeros((0, 3)))])
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
        collision=collision, clip=clip, hull=hull_bone(um),
        mouths={s: tuple(float(c) for c in r['mouths'][s] * SCALE) for s in ('left', 'right')})
    report = {'entity': unit.entity, 'mesh': um.mesh_path, 'weapon': r['weapon'], 'yaw': yaw, 'pitch': pitch,
              'muzzles': r['muzzle_names'], 'recoils': r['recoils'],
              'bones': {b: bone[b] for b in um.bones}, 'gun_split': split, 'radar_slice_y': cut,
              'turntable': r['turntable'], **gear_report}
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
        out[f'xm_{arm.key}'] = [{'bone': arm.bone, 'point': [float(c) for c in p]} for p in points]
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
    # the conversion's record (wt_tracked reads neither key)
    info['xmorph'] = {**report, 'scale': SCALE}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1, sort_keys=False)
        f.write('\n')
    with open(os.path.join(out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(LICENSE.format(title=v.title, entity=f'entities/units/{unit.entity}.ent', mesh=report['mesh']))
    return info


# ------------------------------------------------------------------------------------------ cars

CAR_FORMAT = 1           # model.json 'format' of a car folder: pylib/car_model.py refuses another


def axle_bones(n: int) -> list[str]:
    """The V512's wheel bones of n axles a side, front to rear (without the side): the front axle its steered pair
    (tireF), the rear one its driven pair (tireB), the axles between them drawn only (tireM1.., car_model's visual
    bones: they turn at their own radius, car_base_draw_wheel_info)."""
    _req(n >= 2, f'{n} axles')
    return ['tireF', *(f'tireM{k}' for k in range(1, n - 1)), 'tireB']


def prepare_car(packs: xp.Packs, unit: xm_units.Unit, out_root: str) -> dict:
    """An unarmed vehicle (the 'car' family: pylib/car_model.py builds it on the V512's chassis) as a car folder:
    <out>/xm_<name>/ model.json (CAR_FORMAT), model.npz (p<k>_pos / _nrm / _uv / _tri a part, as the tracked folders'),
    textures/, LICENSE.txt. Its parts: each wheel (wheels(): the hull's round pieces on the ground) on its axle's bone
    (axle_bones), the rest on 'body'. Every bone of the unit's skeleton is the hull's (a car has no turret)."""
    um = xm.load(packs, unit.entity)
    bone = {b: 'body' for b in um.bones}
    w = wheels(um, bone)
    n = len(w['sides']['l'])
    tri_bone = np.full(len(um.triangles), 'body', dtype=object)
    gear = {}
    for side in 'lr':
        for name, wheel in zip(axle_bones(n), w['sides'][side]):
            tri_bone[wheel['tris']] = f'{name}_{side}'
            gear[f'{name}_{side}'] = {'centre': [float(c) * SCALE for c in wheel['centre']],
                                      'radius': wheel['radius'] * SCALE, 'half': wheel['half'] * SCALE}
    P = um.positions.astype(np.float64) * SCALE
    normals, normal_of = prep.corner_normals(P, um.triangles)
    out = os.path.join(out_root, xm_units.folder(unit.key))
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'textures'))
    materials = []
    for m in um.materials:
        stem = m.split('/')[-1]
        imgs = xm.texture_images(packs, m)
        entry = {'name': f'xm_{stem}', 'albedo': f'textures/{stem}.png'}
        imgs['diffuse'].save(os.path.join(out, entry['albedo']), optimize=True)
        if 'normal' in imgs:
            entry['normal'] = f'textures/{stem}_normal.png'
            imgs['normal'].save(os.path.join(out, entry['normal']), optimize=True)
        materials.append(entry)
    arrays: dict[str, np.ndarray] = {}
    parts = []
    for b, mat in sorted({(b, int(m)) for b, m in zip(tri_bone, um.tri_material)}):
        sel = np.flatnonzero((tri_bone == b) & (um.tri_material == mat))
        corner = np.stack((um.triangles[sel], normal_of[sel]), axis=-1).reshape(-1, 2)
        uniq, inverse = np.unique(corner, axis=0, return_inverse=True)
        k = len(parts)
        arrays[f'p{k}_pos'] = P[uniq[:, 0]].astype(np.float32)
        arrays[f'p{k}_nrm'] = normals[uniq[:, 1]].astype(np.float32)
        arrays[f'p{k}_uv'] = um.uvs[uniq[:, 0]].astype(np.float32)
        arrays[f'p{k}_tri'] = inverse.reshape(-1, 3).astype(np.uint32)
        parts.append({'name': f'{b}.{materials[mat]["name"]}', 'bone': b, 'material': mat, 'triangles': int(len(sel)),
                      'vertices': int(len(uniq))})
    np.savez_compressed(os.path.join(out, 'model.npz'), **arrays)
    title = f'{unit.title_en or unit.key} (X-Morph: Defense)'
    info = {'format': CAR_FORMAT, 'kind': unit.key, 'title': title,
            'source': f'X-Morph: Defense (EXOR Studios), entities/units/{unit.entity}.ent',
            'prepared_from': os.path.basename(um.mesh_path), 'materials': materials, 'parts': parts,
            'wheels': gear, 'bounds': [P.min(0).tolist(), P.max(0).tolist()], 'triangles': int(len(um.triangles)),
            'xmorph': {'entity': unit.entity, 'mesh': um.mesh_path, 'axles': n, 'scale': SCALE}}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1)
        f.write('\n')
    with open(os.path.join(out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(LICENSE.format(title=title, entity=f'entities/units/{unit.entity}.ent', mesh=um.mesh_path))
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


PREPARERS = {'tracked': prepare_tracked, 'wheeled': prepare_tracked, 'emplacement': prepare_emplacement,
             'car': prepare_car}


def select(family: str, names: list[str]) -> list[xm_units.Unit]:
    # the family's rows that name a `like` (their EDF counterpart: a row that has none is not ported yet)
    units = [u for u in xm_units.UNITS.values() if u.family == family and u.like]
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
            print(json.dumps({k: info[k] for k in ('kind', 'triangles', 'materials', 'origins', 'muzzles', 'pivots', 'wheels')
                              if k in info} | {'parts': len(info['parts'])}, indent=1, default=str))
    finally:
        packs.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
