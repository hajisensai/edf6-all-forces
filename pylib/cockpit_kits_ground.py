"""Cockpit kits for ground vehicles: a driver's cab (dashboard, steering wheel or tracked-vehicle yoke, windscreen
frame, doors below the window line, roof, rear wall), live screens laid onto a stock interior (the kei truck's and the
trailer truck's cabs and the troop bays are modelled in the stock models already), an open-air rider's tablet and a
bike rider's screens beside the stock dashboard. A turret's gunner takes cockpit_kits.tank_gunner (the M1A1's station). pylib/cockpit_kits.py
merges KITS; its docstring has the conventions: a kit is a function of the room round its eye, returning parts in the
eye's frame (+x left, +y up, +z ahead, metres) laid out inside that room. Textures: tools/make_cockpit_textures.py and
tools/make_cockpit_textures_ground.py (ground_*).

Every screen (cockpits.Screen) faces the eye, sits below the line of sight within about 45 degrees of straight
ahead, and has a part of the same size in the `screen` texture under it; the plugin draws the live read-out there.
"""
from __future__ import annotations

import functools
import math
import os

from cockpit_room import MARGIN, Cabin, CabinTooSmall, _materials, _room
from cockpit_shapes import basis, box, cylinder, frame, panel, quad, two_sided
import cockpits
from cockpits import Kit, KitFn, Screen, Vec3
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


WHEEL_ASSET = 'parts/oga-steering-wheel-cc0/low_6.obj'   # Savino's CC0 wheel (tools/prep_cockpit_ground.py)
WHEEL_SCALE = 0.8                                        # its rim 0.49 m across -> 0.39 m, a car's


@functools.lru_cache(maxsize=1)
def _wheel_obj() -> tuple[tuple[Vec3, ...], tuple[Vec3, ...], tuple[tuple[int, int, int], ...]]:
    """The wheel asset's positions, normals and triangles (its own frame: the rim across x and y, +z toward the
    driver)."""
    part = om.merge(om.obj_parts(om.read_obj(os.path.join(cockpits.asset_root(), WHEEL_ASSET))), 'wheel')
    return (tuple(v.pos for v in part.verts), tuple(v.normal for v in part.verts), tuple(part.tris))


def _wheel(c: Vec3, n: Vec3) -> om.Part:
    """The steering wheel at `c`, its face turned along `n` (toward the driver), its top up."""
    a = _unit(_cross(_cross(n, (0.0, 1.0, 0.0)), n))   # the wheel's up: world up, flattened into its plane
    b = _cross(a, n)
    pos, nrm, tris = _wheel_obj()

    def turn(p: Vec3, k: float) -> Vec3:
        return tuple(k * (p[0] * b[i] + p[1] * a[i] + p[2] * n[i]) for i in range(3))  # type: ignore[return-value]
    verts = [om.Vertex(tuple(c[i] + q[i] for i in range(3)), turn(m, 1.0), (0.5, 0.5))
             for q, m in ((turn(p, WHEEL_SCALE), m) for p, m in zip(pos, nrm))]
    return om.Part('wheel', 'rubber', verts, list(tris))


def _unit(v: Vec3) -> Vec3:
    n = math.sqrt(sum(x * x for x in v))
    return (v[0] / n, v[1] / n, v[2] / n)


def _cross(p: Vec3, q: Vec3) -> Vec3:
    return (p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0])


def _steering(kind: str, c: Vec3, into: float) -> list[om.Part]:
    """A steering wheel (or a tracked vehicle's yoke) at `c` facing the eye, its column going `into` metres away from
    the eye into the dashboard."""
    n = _toward_eye(c)
    back = tuple(c[k] - n[k] * into for k in range(3))
    parts = [cylinder('column', 'metal_dark', c, back, 0.03, 10)]
    if kind == 'wheel':
        parts.append(_wheel(c, n))
    else:   # a yoke: a bar across with two upright grips
        lft, rgt = (c[0] + 0.17, c[1], c[2]), (c[0] - 0.17, c[1], c[2])
        parts.append(cylinder('yoke_bar', 'metal_dark', lft, rgt, 0.016, 8))
        for k, end in enumerate((lft, rgt)):
            parts.append(cylinder(f'yoke_grip{k}', 'rubber', end, (end[0], end[1] + 0.11, end[2] - 0.02), 0.022, 10))
    return parts


CAB_MATERIALS = ('metal_dark', 'rubber', 'screen', 'leather', 'ground_dash', 'ground_door', 'ground_headliner',
                 'ground_floor')


def vehicle_cab(cabin: Cabin, steer: str = 'wheel', rake: float = 0.6, crown: float = 1.2, floor: float = 0.75,
                tuck: float = 0.0, dash: str = 'ground_dash_console') -> Kit:
    """A driver's cab in the room round the eye: the dashboard across below the line of sight with the instrument
    cluster (a 'status' screen) behind the steering wheel (or yoke), the 'weapons' screen on the console toward the
    vehicle's middle (the side with more room), a switch panel on the other side; the raked windscreen's frame ahead
    (its opening clear: the road is seen through it), the doors up to the window line on both sides (the side windows
    open above), the roof with its edges sloping down to the doors, the floor and the rear wall with its window.
    `rake`: how much nearer the windscreen comes per metre up (it crosses the eye's height at the room's 'ahead');
    `crown`: how far the roof's edges drop per metre they come in; `floor`: the floor at most this far below the eye
    (a tracked hull narrows between its tracks); `tuck`: how far its sides come in at the floor; `dash`: the photo
    texture on the dashboard's face (tools/prep_cockpit_ground.py)."""
    r = _room(cabin, {'left': 0.95, 'right': 0.95, 'up': 0.45, 'down': floor, 'ahead': 0.95, 'back': 0.6},
              {'left': 0.2, 'right': 0.2, 'up': 0.1, 'down': 0.4, 'ahead': 0.45, 'back': 0.1}, 'vehicle_cab')
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    wide, mid = L + R, (L - R) / 2
    top = -min(0.13, 0.25 * D)                # the dashboard's top edge
    belt = top + 0.06                         # the doors' window line
    foot, head = A - rake * top, A - rake * U  # the windscreen's foot on the dashboard and its head under the roof
    face = max(0.32, min(A - 0.3, 0.6))       # the dashboard's face toward the eye
    inset = min(0.2, 0.4 * min(L, R))         # the roof's flat part in from the doors
    drop = crown * inset
    parts: list[om.Part] = []
    # The windscreen's frame: the A-pillars, the header under the roof, the cowl on the dashboard.
    tall, deep = U - top, foot - head
    slant = math.hypot(tall, deep)
    parts.append(frame('windscreen', 'metal_dark', (mid, (U + top) / 2, (foot + head) / 2 - 0.02), wide - 2 * inset,
                       slant, 0.06, (0.0, -deep / slant, -tall / slant), 0.04))
    # The dashboard: its top from the face to the windscreen, its face down to the floor.
    parts.append(panel('dash_top', 'ground_dash', (mid, top, (face + foot) / 2), wide, foot - face, (0, 1, 0)))
    lo_l, lo_r = L - tuck, -R + tuck        # the hull's sides at the floor
    parts.append(quad('dash_face', 'ground_dash', (lo_l, -D, face), (lo_r, -D, face), (-R, top, face), (L, top, face)))
    # The dashboard's face: a real dashboard's photo (`dash`) across it; the 'status' screen where the instrument
    # cluster is, behind the wheel; the 'weapons' screen toward the vehicle's middle (the side with more room), where
    # a centre console's display is.
    low = top - min(0.32, 0.8 * (top + D))
    k = (top - low) / (top + D) * tuck        # the sides come in this much at the photo's foot
    q = (L + R) / (top - low) / ASPECT[dash]   # the photo's middle, cropped to the face's shape
    if q > 1:
        ul, ur, vt, vb = 0.0, 1.0, 0.5 - 0.5 / q, 0.5 + 0.5 / q
    else:
        ul, ur, vt, vb = 0.5 - 0.5 * q, 0.5 + 0.5 * q, 0.0, 1.0
    parts.append(quad('dash_photo', dash, (L - k, low, face - 0.002), (-R + k, low, face - 0.002),
                      (-R, top, face - 0.002), (L, top, face - 0.002), ((ul, vb), (ur, vb), (ur, vt), (ul, vt))))
    side = 1.0 if L >= R else -1.0
    screens: list[Screen] = []
    for kind, c, w, h in (('status', (0.0, top - 0.075, face - 0.006), 0.24, 0.1),
                          ('weapons', (side * min(0.4, 0.6 * (L if side > 0 else R)), top - 0.11, face - 0.006), 0.22,
                           0.15)):
        p, s = _screen(kind, c, w, h)
        parts.append(p)
        screens.append(s)
    # The steering wheel (a real wheel's model) or a tracked vehicle's yoke, below the cluster.
    wz = min(0.45, face - 0.1)
    parts += _steering(steer, (0.0, top - 0.14, wz), face - wz + 0.02)
    # The doors up to the window line, an armrest on each.
    u = (A + B) / 0.6
    uv = ((0.0, 1.0), (u, 1.0), (u, 0.0), (0.0, 0.0))
    parts.append(quad('door_l', 'ground_door', (lo_l, -D, -B), (lo_l, -D, face), (L, belt, A), (L, belt, -B), uv))
    parts.append(quad('door_r', 'ground_door', (lo_r, -D, face), (lo_r, -D, -B), (-R, belt, -B), (-R, belt, A), uv))
    for name, x, f in (('door_l', L, (-1, 0, 0)), ('door_r', -R, (1, 0, 0))):
        parts.append(box(f'arm_{name}', 'leather', (x + 0.04 * f[0], belt - 0.12, (face - B) / 2),
                         (0.08, 0.05, min(0.5, face + B))))
    # The roof (its flat middle and its edges sloping down to the doors), the floor, the rear wall below its window
    # and the window's frame.
    roof_z = head - 0.02
    parts.append(panel('roof', 'ground_headliner', (mid, U, (roof_z - B) / 2), wide - 2 * inset, roof_z + B, (0, -1, 0),
                       uv=(0, 0, wide / 0.8, (roof_z + B) / 0.8)))
    edge_z = roof_z + rake * drop
    for name, x, k in (('cant_l', L, 1.0), ('cant_r', -R, -1.0)):
        hi, lo = (x - k * inset, U), (x, U - drop)
        a, b, c, d = (hi[0], hi[1], -B), (lo[0], lo[1], -B), (lo[0], lo[1], edge_z), (hi[0], hi[1], roof_z)
        parts.append(quad(name, 'ground_headliner', b, a, d, c) if k > 0 else quad(name, 'ground_headliner', a, b, c, d))
    parts.append(panel('floor', 'ground_floor', ((lo_l + lo_r) / 2, -D, (face - B) / 2), lo_l - lo_r, face + B, (0, 1, 0)))
    parts.append(quad('rear_low', 'ground_door', (lo_r, -D, -B), (lo_l, -D, -B), (L, belt, -B), (-R, belt, -B)))
    parts.append(frame('rear_window', 'metal_dark', (mid, (U - drop + belt) / 2, -B + 0.02), wide - 2 * inset,
                       U - drop - belt, 0.08, (0, 0, 1), 0.04))
    return Kit(parts, _materials(*CAB_MATERIALS, dash), screens=tuple(screens))


# The photo textures' width / height (tools/prep_cockpit_ground.py CROPS; photo_*: the main set, CREDITS_photos.txt):
# a photo is laid on a face cropped to the face's shape, never stretched.
ASPECT = {'ground_dash_console': 3.23, 'photo_mrap_dashboard': 2.0}


def _photo(name: str, tex: str, c: Vec3, w: float, h: float, facing: Vec3) -> om.Part:
    """A w x h face showing the middle of the photo `tex`, cropped to the face's proportions (not stretched)."""
    k = (w / h) / ASPECT[tex]
    uv = (0.0, 0.5 - 0.5 / k, 1.0, 0.5 + 0.5 / k) if k > 1 else (0.5 - 0.5 * k, 0.0, 0.5 + 0.5 * k, 1.0)
    return panel(name, tex, c, w, h, facing, uv=uv)


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


def bike_rider(cabin: Cabin, ahead: float, drop: float, spread: float) -> Kit:  # noqa: ARG001 - open air
    """A bike's rider: the stock bike has its dashboard on the handlebars already; a small 'status' screen left of
    its gauges and a 'weapons' one right of them (`spread` out from the middle, `ahead` before the eye, `drop` below
    it), each in a bezel, two-sided (seen from outside too)."""
    parts, screens = [], []
    for kind, x in (('status', spread), ('weapons', -spread)):
        p, s = _screen(kind, (x, -drop, ahead), 0.11, 0.065, None)
        parts += [two_sided(p), two_sided(_backing(f'bezel_{kind}', s, 0.008, 0.006))]
        screens.append(s)
    return Kit(parts, _materials('screen', 'metal_dark'), frozenset(q.name for q in parts), tuple(screens))


KITS: dict[str, KitFn] = {
    'vehicle_cab': vehicle_cab,
    'vehicle_cab_yoke': functools.partial(vehicle_cab, steer='yoke', floor=0.6, dash='photo_mrap_dashboard'),
    'tracked_cab': functools.partial(vehicle_cab, steer='yoke', floor=0.6, tuck=0.45, dash='photo_mrap_dashboard'),
    'pickup_cab': functools.partial(vehicle_cab, rake=1.2, crown=1.0),
    'apc_cab': functools.partial(vehicle_cab, tuck=0.3, crown=2.0, dash='photo_mrap_dashboard'),
    'truck_cab': functools.partial(vehicle_cab, rake=0.15, crown=0.6, dash='photo_mrap_dashboard'),
    'kei_dash': functools.partial(stock_dash, status=(0.2, -0.1, 0.42)),
    'trailer_dash': functools.partial(stock_dash, status=(0.3, -0.24, 0.62)),
    'troop_bay': troop_bay,
    'troop_bay_l': functools.partial(troop_bay, aside=-0.33),   # a left-hand seat's: toward the middle
    'troop_bay_r': functools.partial(troop_bay, aside=0.33),
    'open_rider': open_rider,
    'bike_rider': functools.partial(bike_rider, ahead=0.6, drop=0.33, spread=0.15),
}
