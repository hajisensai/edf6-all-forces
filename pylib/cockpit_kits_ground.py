"""Cockpit kits for ground vehicles: real cabs cut out of modelled vehicle interiors (an MRAP's, an armoured car's, a
cab-over truck's, a pickup's: tools/prep_cockpit_ground.py, credits in <model dir>/cockpits/CREDITS_ground.txt; the
user, 2026-10-13: "飞机要有真正座舱是举例子，其他所有载具都应该有", "你塞的这些图片太抽象了，全删了吧"), live screens laid onto
a stock interior (the kei truck's and the trailer truck's cabs and the troop bays are modelled in the stock models
already), an open-air rider's tablet and a bike rider's screens beside the stock dashboard, each in a real gauge's
case. A turret's gunner takes cockpit_kits.tank_gunner. pylib/cockpit_kits.py merges KITS; its docstring has the
conventions: a kit is a function of the room round its eye, returning parts in the eye's frame (+x left, +y up, +z
ahead, metres) laid out inside that room.

A real cab (CABS) is its source vehicle's driver's station as the seated driver sees it: dashboard, steering wheel,
pillars, doors, roof, seats, its glass left out (the windscreen is open: the road is seen through it, the vehicle's
own hull is not drawn from inside). It is scaled down about the eye where the vehicle's cab is smaller than the
source's (no smaller than CabSpec.least), and cut at the room's walls: what would cross the vehicle's hull is left
out (from inside the hull is not drawn, so the world shows there).

Every screen (cockpits.Screen) faces the eye, sits below the line of sight within about 45 degrees of straight
ahead, and has a part of the same size in the `screen` texture under it; the plugin draws the live read-out there.
"""
from __future__ import annotations

import functools
import math
import os
from dataclasses import dataclass

import numpy as np

from cockpit_room import MARGIN, Cabin, CabinTooSmall, _materials
from cockpit_shapes import basis, frame, panel, two_sided
import cockpits
from cockpits import Kit, KitFn, KitMaterial, Screen, Vec3
import obj_model as om


def _toward_eye(c: Vec3) -> Vec3:
    """The facing of a face at `c` that looks at the eye (the origin)."""
    n = math.sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2])
    return (-c[0] / n, -c[1] / n, -c[2] / n)


def _screen(kind: str, c: Vec3, w: float, h: float, wall: Vec3 | None = (0.0, 0.0, -1.0),
            name: str | None = None) -> tuple[om.Part, Screen]:
    """A live screen centred near `c`, turned to the eye: its glass part and its Screen. `wall`: the facing of the
    surface it is mounted on (None: free standing); the screen comes off that surface toward the eye far enough that
    its turned corners do not sink into it."""
    f = _toward_eye(c)
    if wall is not None:
        r, u, _ = basis(f)
        n = wall
        depth = 0.5 * w * abs(sum(r[k] * n[k] for k in range(3))) + 0.5 * h * abs(sum(u[k] * n[k] for k in range(3)))
        c = tuple(c[k] + (depth + 0.006) * n[k] for k in range(3))
        f = _toward_eye(c)
    return panel(name or f'screen_{kind}', 'screen', c, w, h, f), Screen(kind, c, f, w, h)


def _backing(name: str, s: Screen, rim: float, behind: float) -> om.Part:
    """The dark case behind a screen: `rim` wider all round, `behind` metres further from the eye, turned as it is."""
    c = tuple(s.centre[k] - behind * s.facing[k] for k in range(3))
    return panel(name, 'metal_dark', c, s.width + 2 * rim, s.height + 2 * rim, s.facing)


# ------------------------------------------------------------------------------------------ real cabs

@dataclass(frozen=True)
class CabSpec:
    """A real cab: `asset` (parts/<asset>/cab.npz, tools/prep_cockpit_ground.py) in the eye's frame at full size;
    `screens`: the live read-outs (kind, centre in the eye's frame, width, height), on the source's instrument
    cluster and centre console; `tuck`, `knee`: how much of its width the room loses below the eye (squeeze; a hull
    that narrows between its tracks or wheels)."""
    asset: str
    screens: tuple[tuple[str, Vec3, float, float], ...]
    tuck: float = 0.0
    knee: float = 0.0


@functools.lru_cache(maxsize=None)
def _cab_mesh(asset: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, tuple[str, ...],
                                   np.ndarray]:
    """A real cab's positions, normals, UVs, triangles, each triangle's material, the materials' texture files (under
    parts/<asset>/, '' none) and base colours (sRGB)."""
    path = os.path.join(cockpits.asset_root(), 'parts', asset, 'cab.npz')
    with np.load(path, allow_pickle=False) as z:
        return (z['pos'].astype(np.float64), z['nrm'].astype(np.float64), z['uv'].astype(np.float64), z['tris'],
                z['mat'], tuple(str(t) for t in z['textures']), z['colours'])


SOFT = 0.8   # of the way to a wall, where the cab starts being drawn in toward the eye
ROUND = 4    # the room's shape the cab is drawn into: |x/X|^4 + |y/Y|^4 + |z/Z|^4 = 1, a box with rounded corners (a
             # hull's corners are bevelled: the room is measured straight out, its box corners can lie outside)


def squeeze(p: np.ndarray, lo: np.ndarray, hi: np.ndarray, tuck: float = 0.0, knee: float = 0.0) -> np.ndarray:
    """Eye-frame points `p` (n, 3) drawn in toward the eye so they all lie inside the room lo..hi round it, looking
    the same from the eye: each point moves along its own sight line. The room is a box with rounded corners (ROUND)
    whose sides come in by `tuck` of their width from `knee` metres below the eye down (0: at the floor, coming in
    all the way down from the eye; a tracked hull narrows between its tracks under the sponsons); r, how far
    out a point is (1 on the room's surface), stays where it is up to SOFT and is compressed smoothly beyond, to just
    under 1 however far out the point was. Order along a sight line is kept (no surface comes in front of one it was
    behind), the cab stays whole (no triangle is cut), and nothing crosses the walls."""
    def inside(q: np.ndarray) -> np.ndarray:   # the room's measure at points q: <= 1 inside
        ext = np.where(q >= 0, hi, -lo)
        side = 1.0 - tuck * np.clip(-q[:, 1] / (knee or -lo[1]), 0.0, 1.0)
        ext = ext * np.stack([side, np.ones(len(q)), np.ones(len(q))], 1)
        return np.sum((np.abs(q) / ext) ** ROUND, axis=1) ** (1.0 / ROUND)
    # How far out each point is: 1 / the scale at which its sight line meets the room's surface (the measure grows
    # along a sight line, so a bisection finds it; without a tuck it is the measure itself).
    top = 1.0 / np.maximum(inside(p), 1e-12)      # the untucked room is met no later than this
    a, b = np.zeros(len(p)), top
    for _ in range(30 if tuck else 0):
        m = (a + b) / 2
        ok = inside(p * m[:, None]) <= 1.0
        a, b = np.where(ok, m, a), np.where(ok, b, m)
    r = 1.0 / np.maximum(a if tuck else top, 1e-12)
    out = np.where(r > SOFT, SOFT + (1 - SOFT) * (1 - np.exp(-(r - SOFT) / (1 - SOFT))), r)
    return p * (out / np.maximum(r, 1e-12))[:, None]


def _cab_parts(asset: str, lo: Vec3, hi: Vec3, tuck: float,
               knee: float) -> tuple[list[om.Part], dict[str, KitMaterial]]:
    """The cab drawn in to lie inside lo..hi (squeeze), one part per material."""
    pos, nrm, uv, tris, mat, textures, colours = _cab_mesh(asset)
    q = squeeze(pos, np.array(lo), np.array(hi), tuck, knee)
    parts, mats = [], {}
    for m in np.unique(mat):
        t = tris[mat == m]
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        verts = [om.Vertex(tuple(map(float, q[i])), tuple(map(float, nrm[i])), (float(uv[i, 0]), float(uv[i, 1])))
                 for i in used]
        name = f'cab{m}'
        parts.append(om.Part(name, name, verts, [tuple(map(int, r)) for r in inv.reshape(-1, 3)]))
        tex = textures[m]
        mats[name] = KitMaterial(f'parts/{asset}/{tex}' if tex else tuple(int(c) for c in colours[m]))
    return parts, mats


def _first_hit(corners: np.ndarray, d: np.ndarray, beyond: float) -> float:
    """How far the ray from the eye along the unit `d` goes before it meets one of the triangles `corners`, not
    counting hits nearer than `beyond` (inf: none)."""
    v0, e1, e2 = corners[:, 0], corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]
    p = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    u = np.einsum('ij,ij->i', -v0, p) * inv
    q = np.cross(-v0, e1)
    v = (q @ d) * inv
    t = np.einsum('ij,ij->i', e2, q) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > beyond)
    return float(t[hit].min()) if hit.any() else math.inf


CAB_LEAST = {'left': 0.15, 'right': 0.15, 'up': 0.1, 'down': 0.3, 'ahead': 0.35, 'back': 0.05}
OPEN_SIDE = 3.0   # m: how far the cab may reach where the vehicle has no wall (the cut's own box ends it sooner)
PROUD = 0.008     # m: a screen this far in front of the panel it is laid on


def _laid_on(corners: np.ndarray, c: Vec3, w: float, h: float) -> Vec3:
    """Where a screen meant for `c` (w x h, turned to the eye) goes on the cab at full size: along its sight line,
    PROUD in front of the panel its centre and corners look at (hits nearer than 0.85 of the way are things in front
    of the panel, a steering wheel's rim: they stay in front of the screen)."""
    n = math.sqrt(sum(x * x for x in c))
    f = np.array(c) / n
    r, u, _ = basis(_toward_eye(c))
    depth = math.inf
    for a, b in ((0, 0), (-1, -1), (-1, 1), (1, -1), (1, 1)):
        pt = np.array(c) + a * w / 2 * np.array(r) + b * h / 2 * np.array(u)
        d = pt / np.linalg.norm(pt)
        depth = min(depth, _first_hit(corners, d, 0.85 * float(np.linalg.norm(pt))) * float(d @ f))
    k = (depth - PROUD) / n if math.isfinite(depth) else 1.0
    return (c[0] * k, c[1] * k, c[2] * k)


def _panel_clearance(corners: np.ndarray, c: Vec3, w: float, h: float) -> float:
    """Offset along the facing that clears the complete display rectangle after cab deformation.

    Clip nearby cab triangles to the screen footprint and inspect every resulting vertex, including edge
    intersections. Centre/corner rays miss a thin raised rim through the middle of a display. Objects more than
    15% of the eye distance in front remain occluders (the same steering-wheel exclusion as _laid_on).
    """
    r,u,f = (np.array(v) for v in basis(_toward_eye(c)))
    local = (corners-np.array(c)) @ np.stack((r,u,f)).T
    lo,hi = np.array((-w/2,-h/2)),np.array((w/2,h/2))
    overlap = ((local[:,:,:2].max(1)>=lo) & (local[:,:,:2].min(1)<=hi)).all(1)
    # A separate object in front, such as the wheel, must not pull a monitor off its dashboard.
    nearby = local[:,:,2].max(1)<.15*math.sqrt(sum(x*x for x in c))
    front = -math.inf
    for triangle in local[overlap & nearby]:
        poly = list(triangle)
        for axis,bound,sign in ((0,lo[0],1),(0,hi[0],-1),(1,lo[1],1),(1,hi[1],-1)):
            if not poly: break
            clipped = []
            for i,cur in enumerate(poly):
                prev = poly[i-1]
                dc,dp = sign*(cur[axis]-bound),sign*(prev[axis]-bound)
                if (dc>=0)!=(dp>=0): clipped.append(prev+(cur-prev)*(dp/(dp-dc)))
                if dc>=0: clipped.append(cur)
            poly = clipped
        if poly: front=max(front,max(v[2] for v in poly))
    return max(0.0,front+PROUD)


def real_cab(cabin: Cabin, spec: CabSpec) -> Kit:
    """A real cab in the room round the eye: drawn in toward the eye where it reaches past the walls (squeeze: from
    the seat it looks as it was), its screens laid on the source's instruments (_laid_on) and drawn in with it."""
    room = {}
    for side, least in CAB_LEAST.items():
        r = cabin[side] - MARGIN if math.isfinite(cabin[side]) else OPEN_SIDE
        if r < least:
            raise CabinTooSmall(f'real_cab {spec.asset}: {side} {cabin[side]:.2f} m (needs {least + MARGIN:.2f}): '
                                f'{cabin}')
        room[side] = r
    lo = (-room['right'], -room['down'], -room['back'])
    hi = (room['left'], room['up'], room['ahead'])
    cab, cab_mats = _cab_parts(spec.asset, lo, hi, spec.tuck, spec.knee)
    cab_corners = np.concatenate([np.array([v.pos for v in p.verts])[p.tris] for p in cab])
    pos, _, _, tris, _, _, _ = _cab_mesh(spec.asset)
    full_corners = pos[tris]
    parts: list[om.Part] = []
    screens: list[Screen] = []
    for kind, c, w, h in spec.screens:
        # Then drawn in with the cab, as far as its most drawn-in corner is: it stays in front of its panel.
        full = np.array(_laid_on(full_corners, c, w, h))
        r, u, _ = basis(_toward_eye(c))
        pts = np.array([full + a * w / 2 * np.array(r) + b * h / 2 * np.array(u)
                        for a, b in ((0, 0), (-1, -1), (-1, 1), (1, -1), (1, 1))])
        drawn = np.linalg.norm(squeeze(pts, np.array(lo), np.array(hi), spec.tuck, spec.knee), axis=1)
        k = float(np.min(drawn / np.linalg.norm(pts, axis=1)))
        size = float(np.linalg.norm(full)) / math.sqrt(sum(x * x for x in c))
        centre = tuple(map(float, full*k))
        width,height = w*k*size,h*k*size
        lift = _panel_clearance(cab_corners,centre,width,height)
        facing = _toward_eye(centre)
        centre = tuple(centre[i]+lift*facing[i] for i in range(3))
        p, s = _screen(kind, centre, width, height, None)
        parts += [p, _backing(f'bezel_{kind}', s, 0.008 * k, 0.003 * k)]
        screens.append(s)
    mats = _materials('screen', 'metal_dark')
    mats.update(cab_mats)
    return Kit(parts + cab, mats, screens=tuple(screens))


CABS: dict[str, CabSpec] = {
    # An MRAP's driver (Oshkosh M-ATV, from Squad; 42manako): its dashboard with the gauges behind the wheel, 'status'
    # on the switch panel left of them, 'weapons' on the control box right of them. The Grape's hull narrows under
    # the sponsons 0.18 m below the eye (to 0.42 m left of it): the room's sides come in there.
    'apc_cab': CabSpec('sf-oshkosh-matv',
                       (('status', (0.195, -0.2, 0.495), 0.1, 0.08), ('weapons', (-0.43, -0.184, 0.49), 0.1, 0.08)),
                       tuck=0.55, knee=0.12),
    # An armoured car's (GAZ-2330 Tigr, from Squad; 42manako; its steering wheel the ATF Dingo's): the high, wide
    # windscreen, the gauge panel behind the wheel, 'status' on the dashboard right of it, 'weapons' on the switch
    # panel by the centre. The Naegling's hull narrows between its tracks 0.3 m below the eye (the lower side wall 0.36 m left).
    'tracked_cab': CabSpec('sf-gaz-tigr',
                           (('status', (-0.32, -0.27, 0.58), 0.1, 0.07), ('weapons', (-0.55, -0.19, 0.59), 0.13, 0.07)),
                           tuck=0.65, knee=0.25),
    # A cab-over truck's (ARHAN P880, a MAZ; Mayess; mirrored, driven from the right): the big windscreen, the
    # cluster behind the wheel ('status'), the dashboard's display ('weapons').
    'truck_cab': CabSpec('sf-arhan-p880',
                         (('status', (-0.15, -0.27, 0.63), 0.18, 0.08), ('weapons', (0.3, -0.233, 0.5), 0.16, 0.07))),
    # A pickup's (Lightbody '85, DanielZhabotinsky; mirrored, driven from the right as the EDF's Japanese trucks
    # are): the gauges behind the wheel ('status'), the radio in the centre stack ('weapons').
    'pickup_cab': CabSpec('sf-lightbody-85',
                          (('status', (0.018, -0.29, 0.85), 0.2, 0.08), ('weapons', (0.36, -0.37, 0.87), 0.16, 0.08))),
}


# ------------------------------------------------------------------------------------------ stock interiors

def stock_dash(cabin: Cabin, status: Vec3, weapons: Vec3 | None = None) -> Kit:
    """Live screens laid onto a cab the stock model has modelled already (dashboard, wheel, seats): a 'status' screen
    standing on the stock dashboard at `status` (eye frame; beside the stock instruments, not over them), a
    'weapons' one at `weapons` for a vehicle with arms, each in a dark bezel. The stock interior is the rest of the
    view; the room only checks that the eye is in a closed cab."""
    if not all(math.isfinite(cabin[s]) for s in ('left', 'right', 'up', 'ahead')):
        raise CabinTooSmall(f'stock_dash: the eye is not in a closed cab: {cabin}')
    parts: list[om.Part] = []
    screens: list[Screen] = []
    for kind, c, w, h in (('status', status, 0.18, 0.1), ('weapons', weapons, 0.16, 0.1)):
        if c is None:
            continue
        p, s = _screen(kind, c, w, h, None)
        parts += [p, _backing(f'bezel_{kind}', s, 0.012, 0.008)]
        screens.append(s)
    return Kit(parts, _materials('screen', 'metal_dark'), screens=tuple(screens))


def troop_bay(cabin: Cabin, aside: float = 0.0) -> Kit:
    """A seat in a troop bay the stock model has modelled (benches, walls, the bulkhead): a 'status' screen on the
    bulkhead ahead, below the line of sight and `aside` toward the eye's left, so the passenger sees where the vehicle
    is and how it fares. Its size grows with the bulkhead's distance (it looks the same from every row); the rear
    row's screens go inboard (aside) so they do not lie on the front row's."""
    if not math.isfinite(cabin['ahead']) or cabin['ahead'] < 0.4:
        raise CabinTooSmall(f'troop_bay: no bulkhead ahead: {cabin}')
    z = cabin['ahead'] - MARGIN
    w, h = 0.16 * z, 0.1 * z
    p, s = _screen('status', (aside, -0.16 * z, z), w, h)
    bezel = frame('bezel', 'metal_dark', (aside, s.centre[1], z - 0.004), w + 0.04, h + 0.04, 0.02, (0, 0, -1), 0.015)
    return Kit([p, bezel], _materials('screen', 'metal_dark'), screens=(s,))


def open_rider(cabin: Cabin) -> Kit:
    """A rider in the open (on a truck's bed, its tailgate): a small 'status' tablet in his hands, low before him,
    two-sided (seen from outside too, where the rider holds it). The vehicle's own model is the rest of the view."""
    z = min(0.42, cabin['ahead'] - 0.06) if math.isfinite(cabin['ahead']) else 0.42
    p, s = _screen('status', (0.0, -0.22, z), 0.16, 0.1, None)
    parts = [two_sided(p), two_sided(_backing('tablet', s, 0.012, 0.006))]
    return Kit(parts, _materials('metal_dark', 'screen'), frozenset(q.name for q in parts), (s,))


GAUGE = 'sf-stage6-speedo'      # a moped's digital speedometer (tools/prep_cockpit_ground.py), its LCD ahead of the eye
GAUGE_LCD = (0.0, -0.0003, 0.1669, 0.1248)   # the LCD's centre in the asset's frame and its width (metres at full size)


def _gauge(name: str, s: Screen) -> tuple[list[om.Part], dict[str, KitMaterial]]:
    """The speedometer's case round the screen `s`: its LCD under the screen, scaled to the screen's width, turned as
    it is (two-sided: a bike's dashboard is seen from outside too)."""
    pos, nrm, uv, tris, mat, textures, colours = _cab_mesh(GAUGE)
    cx, cy, cz, width = GAUGE_LCD
    k = s.width / width
    r, u, f = (np.array(v) for v in basis(s.facing))
    centre = np.array(s.centre) - 0.002 * np.array(s.facing)   # the LCD just behind the read-out
    # Asset frame: +x the viewer's left (-r), +y up (u), +z away from the viewer (-f).
    q = centre + k * ((pos[:, :1] - cx) * -r + (pos[:, 1:2] - cy) * u + (pos[:, 2:] - cz) * -f)
    n = nrm[:, :1] * -r + nrm[:, 1:2] * u + nrm[:, 2:] * -f
    parts, mats = [], {}
    for m in np.unique(mat):
        t = tris[mat == m]
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        verts = [om.Vertex(tuple(map(float, q[i])), tuple(map(float, n[i])), (float(uv[i, 0]), float(uv[i, 1])))
                 for i in used]
        key = f'gauge{m}'
        parts.append(two_sided(om.Part(f'{name}_{key}', key, verts, [tuple(map(int, x)) for x in inv.reshape(-1, 3)])))
        mats[key] = KitMaterial(f'parts/{GAUGE}/{textures[m]}' if textures[m] else tuple(int(c) for c in colours[m]))
    return parts, mats


def bike_rider(cabin: Cabin, ahead: float, drop: float, spread: float) -> Kit:  # noqa: ARG001 - open air
    """A bike's rider: the stock bike has its dashboard on the handlebars already; a small 'status' screen left of
    its gauges and a 'weapons' one right of them (`spread` out from the middle, `ahead` before the eye, `drop` below
    it), each in a real digital speedometer's case (_gauge), two-sided (seen from outside too)."""
    parts, screens = [], []
    mats = _materials('screen')
    for kind, x in (('status', spread), ('weapons', -spread)):
        p, s = _screen(kind, (x, -drop, ahead), 0.11, 0.05, None)
        case, case_mats = _gauge(f'case_{kind}', s)
        parts += [two_sided(p)] + case
        mats.update(case_mats)
        screens.append(s)
    return Kit(parts, mats, frozenset(q.name for q in parts), tuple(screens))


def _cab_kit(name: str, spec: CabSpec) -> KitFn:
    def kit(cabin: Cabin) -> Kit:
        return real_cab(cabin, spec)
    kit.__name__ = name
    kit.__doc__ = f'{name}: the real cab {spec.asset} (real_cab).'
    return kit


KITS: dict[str, KitFn] = {
    'sedan_cab': lambda cabin: stock_dash(cabin, (0.22, -0.29, 0.43)),
    **{name: _cab_kit(name, spec) for name, spec in CABS.items()},
    'kei_cab': _cab_kit('kei_cab', CABS['pickup_cab']),
    'trailer_cab': _cab_kit('trailer_cab', CABS['truck_cab']),
    'troop_bay': troop_bay,
    'troop_bay_l': functools.partial(troop_bay, aside=-0.33),   # a left-hand seat's: toward the middle
    'troop_bay_r': functools.partial(troop_bay, aside=0.33),
    'open_rider': open_rider,
    'bike_rider': functools.partial(bike_rider, ahead=0.6, drop=0.33, spread=0.15),
}
