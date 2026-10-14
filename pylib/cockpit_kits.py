"""The cockpit kits (pylib/cockpits.py): each a function of the room round the seat's eye (Cabin: how far the
vehicle's own surfaces are left, right, up, down, ahead and back, measured from the model, cockpits.clearance)
returning a Kit: its parts in the eye's frame (+x left, +y up, +z ahead, metres, the eye at the origin) and its
materials (textures under <model dir>/cockpits/, drawn by tools/make_cockpit_textures.py or prepared from CC0 / CC-BY
sources by tools/prep_cockpit_<family>.py; credits in <model dir>/cockpits/CREDITS_*.txt).

A kit lays itself out inside that room, MARGIN short of the hull, so no part of it shows through the vehicle from
outside; a part meant to be seen outside too (a gun shield, a canopy frame) is two-sided. Every kit leaves the view
ahead open (a vision slot, a windshield, a canopy): seen from inside, the vehicle's own hull is not drawn (its faces
point out and the game culls their backs), so the world shows through the opening and the vehicle can still be
driven and fought from the seat; the kit fills the rest of the view.
"""
from __future__ import annotations

import functools
import math
import os

import numpy as np

import cockpits
import obj_model as om
from cockpit_room import MARGIN, Cabin, _materials, _room
from cockpit_shapes import box, cylinder, frame, panel, quad, two_sided
from cockpits import Kit, KitMaterial, Screen

TANK_STATION = 'parts/sf-t62a-arrafi/station.npz'   # tools/prep_cockpit_tank.py (CREDITS_tank.txt)
# The room the station needs round the eye at full size: the eyepieces 0.42 m ahead and 0.18 m down, the gun's
# control handles and the traverse handwheel down to 0.75 m, the azimuth dial and the second eyepiece to the left,
# the breech guard to the right. A smaller turret gets it scaled down about the eye (from the eye it looks the same,
# only the walls are nearer), no smaller than TANK_LEAST_SCALE (the eyepieces then 0.25 m ahead); what still does not
# fit is cut at the walls.
TANK_FULL = {'left': 0.55, 'right': 0.55, 'down': 0.75, 'ahead': 0.7}
TANK_LEAST_SCALE = 0.6
# The view ahead (the user: "注意这是为游戏服务的"): nothing of the station inside these angles from the eye, degrees:
# 36 left and right, 17 down to 31 up (of a 70 degree view's height 65 % is open, of its width 56 %: the station
# fills the bands below and beside it). The vehicle's own hull is not drawn from inside (the game culls its backs),
# so the world shows through it.
TANK_VIEW = (36.0, -17.0, 31.0)
# The gunner's two read-outs (yaw left, pitch up, degrees; width, height, distance in metres at full size): beside
# the view, left over the turret wall's boxes, right on the breech guard's side.
TANK_SCREENS = (('weapons', 45.0, -9.0, 0.11, 0.075, 0.45), ('status', -45.0, -9.0, 0.11, 0.075, 0.45))


@functools.lru_cache(maxsize=1)
def _tank_station() -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, tuple[str, ...], np.ndarray]:
    """The gunner's station in the eye's frame at full size: positions, normals, UVs, triangles, each triangle's
    material, the materials' texture files (under parts/<slug>/, '' none) and base colours (sRGB)."""
    path = os.path.join(cockpits.asset_root(), TANK_STATION)
    with np.load(path, allow_pickle=False) as z:
        return (z['pos'].astype(np.float64), z['nrm'].astype(np.float64), z['uv'].astype(np.float64), z['tris'],
                z['mat'], tuple(str(t) for t in z['textures']), z['colours'])


def _in_cone(p: np.ndarray, yaw: tuple[float, float], pitch: tuple[float, float]) -> np.ndarray:
    """Which eye-frame points lie ahead of the eye between the yaws (degrees left, right negative) and pitches."""
    ahead = np.maximum(p[..., 2], 1e-6)
    y = np.degrees(np.arctan2(p[..., 0], ahead))
    x = np.degrees(np.arctan2(p[..., 1], np.hypot(p[..., 0], ahead)))
    return (p[..., 2] > 0.02) & (y > yaw[0]) & (y < yaw[1]) & (x > pitch[0]) & (x < pitch[1])


def _tank_parts(k: float, lo: tuple[float, float, float], hi: tuple[float, float, float],
                holes: list[tuple[tuple[float, float], tuple[float, float]]]) -> tuple[list[om.Part], dict[str, KitMaterial]]:
    """The station scaled `k` about the eye, only its triangles wholly inside lo..hi and touching none of `holes`
    (yaw and pitch ranges: the view ahead, the screens), one part per material."""
    pos, nrm, uv, tris, mat, textures, colours = _tank_station()
    q = pos * k
    corners = q[tris]
    keep = ((corners >= np.array(lo)) & (corners <= np.array(hi))).all((1, 2))
    # Points along each triangle's edges and its centre: a long thin part (a rod from the roof to the floor) crosses
    # the view with all three corners outside it.
    w = np.linspace(0.0, 1.0, 9)[:, None, None]
    samples = np.concatenate([corners[None, :, a] * (1 - w) + corners[None, :, b] * w for a, b in ((0, 1), (1, 2), (2, 0))]
                             + [corners.mean(1)[None]])
    for yaw, pitch in holes:
        keep &= ~_in_cone(samples, yaw, pitch).any(0)
    parts, mats = [], {}
    slug = os.path.dirname(TANK_STATION)
    for m in np.unique(mat[keep]):
        t = tris[keep & (mat == m)]
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        verts = [om.Vertex(tuple(map(float, q[i])), tuple(map(float, nrm[i])), (float(uv[i, 0]), float(uv[i, 1])))
                 for i in used]
        name = f'station{m}'
        parts.append(om.Part(name, name, verts, [tuple(map(int, r)) for r in inv.reshape(-1, 3)]))
        tex = textures[m]
        mats[name] = KitMaterial(f'{slug}/{tex}' if tex else tuple(int(c) for c in colours[m]))
    return parts, mats


def _tank_shell(L: float, R: float, U: float, D: float, A: float, B: float) -> tuple[list[om.Part], float, float]:
    """The cream turret round the station, open on the view (TANK_VIEW), walls at most L, R, U, D, A, B from the eye;
    with the floor's and the sides' depth ahead."""
    half, low, high = TANK_VIEW
    # The shell, seen from inside, shaped round the view: the roof ends where the view's top edge meets it, the floor
    # where its bottom edge does (or at the room's front, closed there by a front wall), the sides go as far as the
    # deeper of the two and a front wall at that depth closes them beside the view. Every line of sight outside the
    # view's angles meets the shell; inside them, the world.
    t = lambda deg: math.tan(math.radians(deg))  # noqa: E731
    zt, zb = min(A, U / t(high)), min(A, D / t(-low))
    zs = max(zt, zb)
    wx_l, wx_r = min(L, zs * t(half)), min(R, zs * t(half))
    mid = (L - R) / 2
    wide = L + R
    parts: list[om.Part] = [
        box('shell', 'paint_interior', (mid, (U - D) / 2, (zs - B) / 2), (wide, U + D, zs + B), inward=True,
            tile=0.8, faces='Z'),
        panel('roof', 'paint_interior', (mid, U, (zt - B) / 2), wide, zt + B, (0, -1, 0), (0, 0, 1),
              (0.0, 0.0, wide / 0.8, (zt + B) / 0.8)),
        panel('floor', 'paint_interior', (mid, -D, (zb - B) / 2), wide, zb + B, (0, 1, 0), (0, 0, 1),
              (0.0, 0.0, wide / 0.8, (zb + B) / 0.8)),
    ]
    # The sides as far as the view's edge meets them (a narrow turret's wall stops beside the view, not across it).
    zl, zr = min(zs, L / t(half)), min(zs, R / t(half))
    # Past there, the side walls go on to the sides' depth above and below the view (1.5 degrees into it: the
    # view's edges are curves on a wall, the pieces' edges straight).
    for name, x, z1, f in (('wall_l', L, zl, (-1.0, 0.0, 0.0)), ('wall_r', -R, zr, (1.0, 0.0, 0.0))):
        parts.append(panel(name, 'paint_interior', (x, (U - D) / 2, (z1 - B) / 2), z1 + B, U + D, f,
                           uv=(0.0, 0.0, (z1 + B) / 0.8, (U + D) / 0.8)))
        if zs - z1 > 0.005:
            edge_lo = [max(-D, -math.hypot(x, z) * t(-low - 1.5)) for z in (z1, zs)]
            edge_hi = [min(U, math.hypot(x, z) * t(high - 1.5)) for z in (z1, zs)]
            for part, (y0, y1) in (('low', ((-D, -D), edge_lo)), ('high', (edge_hi, (U, U)))):
                if max(y1[0] - y0[0], y1[1] - y0[1]) > 0.005:
                    pts = [(x, y0[0], z1), (x, y0[1], zs), (x, y1[1], zs), (x, y1[0], z1)]
                    parts += _face(f'{name}_{part}', 'paint_interior', pts, f)
    # Beside the view the roof and the floor go on to the sides' depth (a line of sight left of the view and a little
    # under its top edge meets the roof's plane past the roof's middle).
    for name, y, z0, sign in (('roof', U, zt, 1.0), ('floor', -D, zb, -1.0)):
        for side, wall, z1 in (('l', L, zl), ('r', -R, zr)):
            e0, e1 = math.copysign(z0 * t(half), wall), math.copysign(z1 * t(half), wall)
            if z1 - z0 > 0.005 and abs(wall) - abs(e0) > 0.005:
                pts = [(e0, y, z0), (wall, y, z0), (wall, y, z1), (e1, y, z1)]
                parts += _face(f'{name}_{side}', 'paint_interior', pts, (0.0, -sign, 0.0))
    yt, yb = min(U, zs * t(high)), min(D, zs * t(-low))
    fronts = [('front_l', wx_l, L, -yb, yt, zs), ('front_r', -R, -wx_r, -yb, yt, zs)]
    if zt >= A - 1e-6:
        fronts.append(('front_top', -R, L, A * t(high), U, A))
    if zb >= A - 1e-6:
        fronts.append(('front_low', -R, L, -D, -A * t(-low), A))
    for name, x0, x1, y0, y1, z in fronts:
        if x1 - x0 > 0.005 and y1 - y0 > 0.005:
            parts.append(panel(f'wall_{name}', 'paint_interior', ((x0 + x1) / 2, (y0 + y1) / 2, z), x1 - x0, y1 - y0,
                               (0, 0, -1), uv=(0.0, 0.0, (x1 - x0) / 0.8, (y1 - y0) / 0.8)))
    # The view's dark rim: a lintel under the roof's front edge, a sill on the lower edge, posts beside it.
    bar = 0.03 * min(zt, zb)   # thin: its width eats into the view
    top_y, top_z = min(U, zt * t(high)), zt
    low_y, low_z = -min(D, zb * t(-low)), zb
    rim = [('lintel', (mid, top_y - bar / 2, top_z - bar / 2), (wide - 0.01, bar, bar)),
           ('sill', (mid, low_y + bar / 2, low_z - bar / 2), (wide - 0.01, bar, bar))]
    for name, sign, edge, wall, z in (('post_l', 1.0, wx_l, L, zl), ('post_r', -1.0, wx_r, R, zr)):
        z -= bar / 2   # just outside the view's edge, inside the wall
        y0, y1 = max(-D, -z * t(-low)), min(U, z * t(high))
        rim.append((name, (sign * min(edge + bar / 2, wall - bar / 2), (y0 + y1) / 2, z), (bar, y1 - y0, bar)))
    parts += [box(f'rim_{name}', 'metal_dark', c, size) for name, c, size in rim]
    return parts, zb, zs


def tank_gunner(cabin: Cabin) -> Kit:
    """A tank turret's gunner station, the real one: cut out of a modelled tank interior (a T-62A's, Sketchfab, CC BY
    4.0: tools/prep_cockpit_tank.py, <model dir>/cockpits/CREDITS_tank.txt; the user, 2026-10-13: "坦克拿不到吗" and
    "坦克的做的什么玩意"). The gunner has lifted his head from the sight: his two eyepieces with their black brow pads
    stand at the bottom of the view (the gun's control handles, the traverse handwheel and the azimuth dial under them,
    a glance down), the fire-control boxes overhead, the breech guard with a rifle racked on it to the right (the gun's
    side: the fits put the eye left of the gun, as a T-62's gunner sits). The view ahead (TANK_VIEW) is left open: nothing of
    the station is inside it, the front wall round it is the turret's cream enamel with a dark rim.

    Turrets differ a lot (an E551's is low, a Titan's a hall): the walls go where the room lets them (no further than
    a real turret's), the station is scaled down about the eye where the room is smaller than it needs (TANK_FULL) and
    cut at the walls; the cream shell closes what the station leaves open."""
    r = _room(cabin, {'left': 0.9, 'right': 0.9, 'up': 0.35, 'down': 1.0, 'ahead': 0.8, 'back': 0.7},
              {'left': 0.15, 'right': 0.15, 'up': 0.1, 'down': 0.2, 'ahead': 0.35, 'back': 0.15}, 'tank_gunner')
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    k = max(TANK_LEAST_SCALE, min(1.0, *(r[side] / need for side, need in TANK_FULL.items())))
    half, low, high = TANK_VIEW
    parts, zb, zs = _tank_shell(L, R, U, D, A, B)
    # The read-outs, each on a dark backing, turned to the eye; the station is cut away in front of them.
    screens, holes = [], [((-half, half), (low, high))]
    for kind, yaw, pitch, w, h, dist in TANK_SCREENS:
        d = min(dist * k, zb - 0.04)
        c = _at(yaw, pitch, d)
        c = (max(-R + 0.02 + w * k / 2, min(L - 0.02 - w * k / 2, c[0])), max(-D + 0.02 + h * k / 2, c[1]), c[2])
        facing = _toward(c)
        screens.append(Screen(kind, c, facing, w * k, h * k))
        parts.append(panel(f'screen_{kind}', 'screen', c, w * k, h * k, facing))
        back = tuple(c[i] - 0.004 * facing[i] for i in range(3))
        parts.append(panel(f'backing_{kind}', 'metal_dark', back, w * k + 0.02, h * k + 0.02, facing))
        ay = math.degrees(math.atan2(w * k / 2 + 0.012, d)) + 1.0
        ap = math.degrees(math.atan2(h * k / 2 + 0.012, d)) + 1.0
        holes.append(((yaw - ay, yaw + ay), (pitch - ap, pitch + ap)))
    station, station_mats = _tank_parts(k, (-R + 0.005, -D + 0.005, -B + 0.005), (L - 0.005, U - 0.005, zs - 0.02),
                                        holes)
    mats = _materials('paint_interior', 'metal_dark', 'screen')
    mats.update(station_mats)
    return Kit(parts + station, mats, screens=tuple(screens))


def _face(name: str, material: str, pts: list[tuple[float, float, float]],
          facing: tuple[float, float, float]) -> list[om.Part]:
    """The quad on the points `pts` (in order round it; corners that coincide count once) with its front toward
    `facing`, planar UVs at 0.8 m a repeat; none when it has no area."""
    ring = [p for k, p in enumerate(pts) if math.dist(p, pts[k - 1]) > 1e-6]
    if len(ring) < 3:
        return []
    n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if len(ring) == 4 and np.linalg.norm(n) < 1e-9:      # the first three in a line: start at the next corner
        ring = ring[1:] + ring[:1]
        n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if np.linalg.norm(n) < 1e-9:
        return []
    if float(np.dot(n, facing)) < 0:
        ring = ring[::-1]
    ring = ring + ring[-1:] * (4 - len(ring))
    axes = [i for i in range(3) if abs(facing[i]) < 0.5]
    uv = tuple((p[axes[0]] / 0.8, p[axes[1]] / 0.8) for p in ring)
    return [quad(name, material, *ring, uv=uv)]


def _at(yaw: float, pitch: float, dist: float) -> tuple[float, float, float]:
    """The eye-frame point `dist` away, `yaw` degrees to the left and `pitch` up."""
    y, p = math.radians(yaw), math.radians(pitch)
    return (dist * math.sin(y) * math.cos(p), dist * math.sin(p), dist * math.cos(y) * math.cos(p))


def _toward(c: tuple[float, float, float]) -> tuple[float, float, float]:
    """The unit direction from `c` back to the eye."""
    n = math.sqrt(sum(v * v for v in c))
    return (-c[0] / n, -c[1] / n, -c[2] / n)


def gun_station(cabin: Cabin) -> Kit:
    """An open gun mount on a turret or at a door (the gun itself is the vehicle's own model): the shield before it with
    its firing window (two-sided: it is seen from outside too), the two spade grips, a small readout box. Open air
    round it, so the room only bounds how far down the grips reach."""
    down = cabin['down'] - MARGIN if math.isfinite(cabin['down']) else 0.5
    parts = []
    z = 0.75
    for name, x in (('shield_l', 0.29), ('shield_r', -0.29)):
        parts.append(two_sided(panel(name, 'paint_olive', (x, -0.1, z), 0.3, 0.5, (0, 0, -1))))
    parts.append(two_sided(panel('shield_low', 'paint_olive', (0.0, -0.29, z), 0.28, 0.12, (0, 0, -1))))
    parts.append(frame('shield_rim', 'metal_dark', (0.0, -0.1, z - 0.01), 0.9, 0.52, 0.025, (0, 0, -1), 0.03))
    gy = -min(0.3, max(0.12, down - 0.05))
    for x in (0.16, -0.16):
        parts.append(cylinder(f'spade{x}', 'rubber', (x, gy - 0.06, 0.32), (x, gy + 0.07, 0.32), 0.024, 10))
    parts.append(box('spade_bar', 'metal_dark', (0.0, gy, 0.36), (0.36, 0.03, 0.03)))
    readout = Screen('weapons', (-0.36, gy + 0.06, 0.45), (0.4, 0.3, -0.85), 0.16, 0.11)
    parts.append(two_sided(panel('readout', 'screen', readout.centre, readout.width, readout.height, readout.facing)))
    return Kit(parts, _materials('paint_olive', 'metal_dark', 'rubber', 'screen'),
               frozenset(p.name for p in parts), (readout,))   # open air: all of it is seen from outside


def remote_display(cabin: Cabin) -> Kit:
    """An unmanned weapon station's seat (the user, 2026-10-13: the Blacker's side guns "明明是无人炮塔"; "哪怕弄成显示器那种
    显示都行"): no cockpit round a gun nobody stands at. The eye is the gun's camera (the fit puts it at the gun's sight /
    camera locator, riding its pitch bone) and the whole view is the operator's monitor: the plugin draws the station's
    display over it (a sensor screen: hud.cpp RwsDisplay)."""
    del cabin   # open air round the camera: nothing to fit
    return Kit([], {}, screens=(Screen('sensor', (0.0, 0.0, 1.0), (0.0, 0.0, -1.0), 0.4, 0.3),))


KITS = {
    'tank_gunner': tank_gunner,
    'gun_station': gun_station,
    'remote_display': remote_display,
}


# Keep these below the helpers: the family modules import them from this module.
# Explicit imports also make every fixed family visible to PyInstaller.
import cockpit_kits_ground
import cockpit_kits_air
import cockpit_kits_mech
import cockpit_kits_plugin

for _mod in (cockpit_kits_ground, cockpit_kits_air, cockpit_kits_mech, cockpit_kits_plugin):
    _more = _mod.KITS
    if set(_more) & set(KITS):
        raise ValueError(f'{_mod.__name__}: kit names taken: {sorted(set(_more) & set(KITS))}')
    KITS.update(_more)
