"""Cockpit kits for aircraft (pylib/cockpit_kits.py merges KITS; the conventions are its docstring's: a kit is a
function of the room round its eye, returning parts in the eye's frame laid out inside that room).

The stock helicopters have no interior: seen from a seat, culled as the game culls, the fuselage vanishes and only a
few frame strips, the rotor and the guns remain (tools/cabin_view.py). So a helicopter kit supplies the whole cabin:
the windshield's frame round an open view ahead, the instrument panel under its glare shield, the seat, the cyclic,
the collective, the pedals, the side consoles and the overhead panel, the tub's walls below the window sills and the
bulkhead behind. The panel's screens (cockpits.Screen) are where the plugin draws the live read-outs.

A fuselage is round, the room (cockpits.clearance) is measured straight left, right, up and down from the eye: a box
of that size would stick out of the hull at its corners. So the cabins here have a fuselage's section (Section): the
full width only at the eye's height (the sills, the door), narrower at the floor and the roof, chamfered between, and
a pilot's tub narrows toward the nose.

    heli_gunship      an attack helicopter's cockpit (V506 / V602 / 409) after the AH-64D pilot's station: the
                      photographed panel face (two MFDs: flight, weapons; the up-front display: status), its standby
                      instruments and lower centre panel, the hood, side consoles, an armoured seat; a high sill; the
                      helmet's display (IHADSS) as the 'hud' screen, no combiner glass in the view
    heli_utility      a transport's cockpit (410) after the UH-60M's: the photographed four-MFD panel in two halves
                      (status, flight, map, a spare), the centre console, the glare shield, the overhead console, a
                      low sill (no centre windshield post: it would stand on the line of sight)
    heli_door_left/_right  a door gunner in a transport's cabin at the left / right door, looking out of it (the
                      fit's yaw turns the eye to the door: 90 / -90): the door's frame, the cabin's walls round it,
                      the floor, the roof, the gunner's seat, the gun's control grips under the sight's 'weapons'
                      screen; the cockpit bulkhead with its passage toward the nose, the troop cabin's end toward the
                      tail, the opposite door open across the cabin. No stock helicopter has a crewed door gun (the
                      410's guns are remote pods on its stub wings: cockpit_kits.remote_display), so no stock fit uses
                      them; they wait for a transport with real door gunners (its walls are still drawn, not photos)

The panels, consoles, hoods and seats are photographs of real cockpits on faces turned to the eye (the eye never moves
inside the cockpit, so they read as the real thing): textures/air_uh60_*, air_ah64_*, air_seat.png, cut from public
domain / CC BY photographs by tools/prep_cockpit_air.py (credits: <model dir>/cockpits/CREDITS_air.txt); the walls and
the overhead panel are drawn (textures/air_tub, air_overhead: tools/make_cockpit_textures_air.py); the rest are the
shared textures (tools/make_cockpit_textures.py). Only the remote gun pods' seats (410) use cockpit_kits.remote_display.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import obj_model as om
from cockpit_room import TEX, Cabin, _room
from cockpit_shapes import basis, box, cylinder, frame, merge, panel, quad
from cockpits import Kit, KitMaterial, Screen

Vec3 = tuple[float, float, float]
Vec2 = tuple[float, float]

# The air panel texture's three screen bays (tools/make_cockpit_textures_air.py air_panel): each bay's centre u, their
# centre v, and the screen's share of the panel's width and height.
# The displays on the photographed panels (tools/prep_cockpit_air.py SCREENS, measured on the photographs): texture ->
# display -> its (u0, v0, u1, v1) on the texture.
PHOTO_SCREENS: dict[str, dict[str, tuple[float, float, float, float]]] = {
    'air_uh60_panel_l': {'mfd1': (0.0542, 0.1358, 0.3735, 0.6173), 'mfd2': (0.4639, 0.0988, 0.7831, 0.5802)},
    'air_uh60_panel_r': {'mfd3': (0.259, 0.1235, 0.5723, 0.6173), 'mfd4': (0.6627, 0.2099, 0.9518, 0.679)},
    'air_ah64_panel': {'mfd_l': (0.0945, 0.3706, 0.4461, 0.8263), 'mfd_r': (0.5867, 0.3706, 0.9383, 0.8263),
                       'eufd': (0.4662, 0.0289, 0.8037, 0.2141)},
}
TILE = 0.8   # m a wall texture's repeat covers


def _materials(*names: str) -> dict[str, KitMaterial]:
    return {n: KitMaterial(TEX.format(n)) for n in names}


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a: Vec3, b: Vec3, k: float = 1.0) -> Vec3:
    return (a[0] + k * b[0], a[1] + k * b[1], a[2] + k * b[2])


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _unit(a: Vec3) -> Vec3:
    n = math.sqrt(_dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def beam(name: str, material: str, a: Vec3, b: Vec3, w: float, h: float | None = None,
         up: Vec3 = (0.0, 1.0, 0.0)) -> om.Part:
    """A square-section bar from a to b, `w` wide (`h` deep: w), its faces outward (a canopy bow, a pillar, a rail)."""
    h = w if h is None else h
    f = _unit(_sub(b, a))
    hint = up if abs(_dot(f, up)) < 0.95 else (1.0, 0.0, 0.0)
    r = _unit(_cross(hint, f))
    u = _cross(f, r)
    corners = [_add(_add((0.0, 0.0, 0.0), r, sx * w / 2), u, sy * h / 2)
               for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    faces = []
    for k in range(4):
        p, q = corners[k], corners[(k + 1) % 4]
        faces.append(quad(f'{name}s{k}', material, _add(a, p), _add(a, q), _add(b, q), _add(b, p)))
    faces.append(quad(f'{name}e0', material, *(_add(a, c) for c in reversed(corners))))
    faces.append(quad(f'{name}e1', material, *(_add(b, c) for c in corners)))
    return merge(faces, name)


def wall(name: str, material: str, pts: list[Vec3], toward: Vec3 = (0.0, 0.0, 0.0), tile: float = TILE) -> om.Part:
    """A flat convex polygon through `pts`, its front turned to `toward` (the eye: a cabin's wall seen from inside),
    its texture laid flat on it, one repeat per `tile` metres."""
    n = (0.0, 0.0, 0.0)
    for k, p in enumerate(pts):   # Newell's normal
        q = pts[(k + 1) % len(pts)]
        n = _add(n, ((p[1] - q[1]) * (p[2] + q[2]), (p[2] - q[2]) * (p[0] + q[0]), (p[0] - q[0]) * (p[1] + q[1])))
    if _dot(n, _sub(toward, pts[0])) < 0:
        pts, n = list(reversed(pts)), (-n[0], -n[1], -n[2])
    right, up, _ = basis(n)
    verts = [om.Vertex(p, _unit(n), (_dot(p, right) / tile, -_dot(p, up) / tile)) for p in pts]
    return om.Part(name, material, verts, [(0, k, k + 1) for k in range(1, len(pts) - 1)])


@dataclass(frozen=True)
class Section:
    """A fuselage's section (`w_left` / `w_right` the half widths at the widest, from the centre line `mid`): the
    floor `floor_w` of that wide at `floor`, the chine at `chine` (the full width), `top_w` of it at `top` (a wall
    leaning in: 1, upright), the roof `roof_w` wide at `roof`. side(+1 / -1) gives one side's corners bottom to top
    as (x, y)."""
    mid: float
    w_left: float
    w_right: float
    floor: float
    chine: float
    top: float
    roof: float
    floor_w: float = 0.5
    roof_w: float = 0.42
    top_w: float = 1.0

    def side(self, sign: int, scale: float = 1.0) -> list[Vec2]:
        w = (self.w_left if sign > 0 else self.w_right) * scale
        x = lambda k: self.mid + sign * w * k   # noqa: E731
        return [(x(self.floor_w), self.floor), (x(1.0), self.chine), (x(self.top_w), self.top), (x(self.roof_w), self.roof)]

    def ring(self, scale: float = 1.0) -> list[Vec2]:
        """The whole section round, left side up, right side down."""
        return self.side(1, scale) + list(reversed(self.side(-1, scale)))


def _screen(parts: list[om.Part], screens: list[Screen], kind: str, centre: Vec3, facing: Vec3, width: float,
            height: float) -> None:
    """A live read-out: the Screen and its dark glass part (textures/screen.png), the same size, 4 mm proud of
    whatever it sits on (toward the eye)."""
    f = _unit(facing)
    c = _add(centre, f, 0.004)
    parts.append(panel(f'screen_{kind}', 'screen', c, width, height, f))
    screens.append(Screen(kind, _add(c, f, 0.001), f, width, height))


# ------------------------------------------------------------------------------------------------------- pilots
def _photo_panel(parts: list[om.Part], screens: list[Screen], name: str, texture: str, centre: Vec3, width: float,
                 height: float, kinds: dict[str, str]) -> None:
    """A panel face with a photographed panel's texture (`width` x `height`, facing the eye from `centre`) and a
    live read-out on each of its displays named in `kinds` (PHOTO_SCREENS[texture]: display -> screen kind)."""
    facing = _unit((-centre[0], -centre[1], -centre[2]))
    parts.append(panel(name, texture, centre, width, height, facing))
    right, up, _ = basis(facing)
    for display, kind in kinds.items():
        u0, v0, u1, v1 = PHOTO_SCREENS[texture][display]
        c = _add(_add(centre, right, ((u0 + u1) / 2 - 0.5) * width), up, (0.5 - (v0 + v1) / 2) * height)
        _screen(parts, screens, kind, c, facing, (u1 - u0) * width, (v1 - v0) * height)


def _hood(parts: list[om.Part], texture: str, mid: float, top: float, lip: float, width: float, depth: float) -> None:
    """The glare shield over a panel whose top edge is at `top`: a hood from its lip at `lip` (toward the eye)
    `depth` forward, narrowing toward the nose with the fuselage."""
    y = top + 0.025
    hw, hf = width / 2 + 0.02, 0.36 * width
    corners = [(mid + hw, y, lip), (mid - hw, y, lip), (mid - hf, y, lip + depth), (mid + hf, y, lip + depth)]
    parts.append(_stretched(wall('glare_shield', texture, corners, toward=(mid, 1.0, lip))))
    parts.append(beam('glare_lip', 'rubber', (mid + hw, top + 0.012, lip), (mid - hw, top + 0.012, lip), 0.02, 0.025))


def _stretched(part: om.Part) -> om.Part:
    """A four-cornered wall with its texture stretched once over it (its first two corners along the top)."""
    verts = [om.Vertex(v.pos, v.normal, uv) for v, uv in zip(part.verts, ((1.0, 0.0), (0.0, 0.0), (0.0, 1.0),
                                                                         (1.0, 1.0)))]
    return om.Part(part.name, part.material, verts, list(part.tris))


def _ah64_panel(parts: list[om.Part], screens: list[Screen], mid: float, top: float, z: float, room_w: float,
                floor: float) -> float:
    """An AH-64D pilot's station (the photographed one, tools/prep_cockpit_air.py): the panel face with its two
    MFDs (flight left, weapons right) under the up-front display's strip (status), the standby instruments on the
    right, the lower centre panel under it down toward the floor (at `floor` there), the big hood over it all; plain
    plates fill the panel's width to the walls. Returns the hood's lip z."""
    w = min(0.44, room_w - 0.16)
    yc = top - w / 2
    _photo_panel(parts, screens, 'panel', 'air_ah64_panel', (mid, yc, z), w, w,
                 {'mfd_l': 'flight', 'mfd_r': 'weapons', 'eufd': 'status'})
    side = (room_w - w) / 2
    if side > 0.03:
        for sign, s in ((1, 'l'), (-1, 'r')):
            x = mid + sign * (w / 2 + side / 2)
            parts.append(panel(f'panel_wing_{s}', 'air_tub', (x, yc, z + 0.02), side, w, _unit((-x, -yc, -z)),
                               uv=(0, 0, side / 0.4, w / 0.4)))
        sw = min(0.17, side - 0.01)
        if sw > 0.08:
            x = mid - (w / 2 + sw / 2 + 0.005)
            parts.append(panel('standby', 'air_ah64_standby', (x, yc - 0.06, z + 0.01), sw, sw / 2,
                               _unit((-x, 0.06 - yc, -z))))
    low = min(0.32, top - w - floor - 0.06)
    if low > 0.08:
        parts.append(panel('lower_centre', 'air_ah64_lower', (mid, top - w - 0.01 - low / 2, z + 0.06), low / 2, low,
                           _unit((0.0, 0.35, -1.0))))
    lip = z - 0.12
    _hood(parts, 'air_uh60_hood', mid, top, lip, room_w - 0.02, 0.28)
    return lip


def _uh60_panel(parts: list[om.Part], screens: list[Screen], mid: float, top: float, z: float, room_w: float) -> float:
    """A UH-60M's instrument panel (the photographed one): its two halves meeting in a shallow V before the eye,
    four MFDs (status, flight, map; the fourth a spare), the centre console sloping down under it toward the
    floor, the glare shield over it. Returns the glare shield's lip z."""
    w = min(1.25, room_w - 0.04)
    h = w / 2 / 2.05                       # the photographed halves' aspect
    yc = top - h / 2
    for sign, s, tex, kinds in ((1, 'l', 'air_uh60_panel_l', {'mfd1': 'status', 'mfd2': 'flight'}),
                                (-1, 'r', 'air_uh60_panel_r', {'mfd3': 'map'})):
        _photo_panel(parts, screens, f'panel_{s}', tex, (mid + sign * w / 4, yc, z - w / 4 * 0.18), w / 2, h, kinds)
    pw = min(0.42, w * 0.4)
    ya, za, yb, zb = top - h - 0.01, z, top - h - 0.45, z - 0.22
    corners = [(mid + pw / 2, ya, za), (mid - pw / 2, ya, za), (mid - pw / 2, yb, zb), (mid + pw / 2, yb, zb)]
    parts.append(_stretched(wall('pedestal', 'air_uh60_pedestal', corners, toward=(mid, 0.3, 0.0))))
    lip = z - 0.13
    _hood(parts, 'air_uh60_hood', mid, top, lip, w, 0.3)
    return lip


def _tub(parts: list[om.Part], sec: Section, back: float, front: float, roof_front: float, taper: float,
         rise: float, drop: float) -> None:
    """The cabin's shell: the floor, the side walls (a chamfer from the floor to the chine, then up to the sills),
    the foot well's front wall at `front`, the rear bulkhead at -`back`, the roof back from `roof_front` (the
    windshield is open ahead of it, the side windows above the sills; it slopes down `drop` to its front). Toward
    the nose the walls narrow to `taper` of the width and the floor rises `rise`."""
    def at(z: float, x: float, y: float) -> Vec3:
        return (x, y, z)

    rear = sec.side(1) + sec.side(-1)
    nose = [(sec.mid + (x - sec.mid) * taper, y + (rise if y == sec.floor else 0.0)) for x, y in
            sec.side(1) + sec.side(-1)]
    lf, lc, lt, _lr, rf, rc, rt, _rr = range(8)
    names = (('floor', lf, rf), ('chamfer_l', lf, lc), ('wall_l', lc, lt), ('chamfer_r', rf, rc), ('wall_r', rc, rt))
    for name, i, j in names:
        parts.append(wall(name, 'air_tub', [at(-back, *rear[i]), at(-back, *rear[j]), at(front, *nose[j]),
                                            at(front, *nose[i])]))
    well = [nose[k] for k in (lf, lc, lt, rt, rc, rf)]
    parts.append(wall('foot_well', 'air_tub', [at(front, *p) for p in well]))
    parts.append(wall('bulkhead', 'air_tub', [at(-back, *p) for p in sec.ring()]))
    l_roof, r_roof = sec.side(1)[3], sec.side(-1)[3]
    parts.append(wall('roof', 'air_tub', [at(-back, *l_roof), at(-back, *r_roof),
                                          at(roof_front, r_roof[0], r_roof[1] - drop),
                                          at(roof_front, l_roof[0], l_roof[1] - drop)]))


def _canopy(parts: list[om.Part], sec: Section, glare: Vec3, back: float, roof_front: float, taper: float,
            front: float, drop: float) -> None:
    """The windshield's and windows' frame round the open view: the front pillars from the glare shield's corners
    (`glare`: its left corner) to the roof's front, the windshield's top bow, the sill rails, the door pillars
    beside the head and behind it (each from the sill in to the roof's edge), the bows across. No centre post, even
    where the real aircraft has one (the UH-60): it would stand on the line of sight."""
    m = 'canopy_frame'
    gx, gy, gz = glare
    zs = gz - 0.22

    def wide(z: float) -> float:   # the tub's width share at z (it narrows toward the nose)
        return 1.0 + (taper - 1.0) * max(0.0, min(1.0, (z + back) / (front + back)))

    def low(z: float) -> float:    # how far the roof has sloped down at z
        return drop * max(0.0, min(1.0, (z + back) / (roof_front + back)))

    for sign, s in ((1, 'l'), (-1, 'r')):
        _f, _c, top, roof = sec.side(sign)
        g = (sec.mid + sign * (gx - sec.mid), gy, gz)
        sill_front = (sec.mid + (top[0] - sec.mid) * wide(zs) - sign * 0.02, top[1], zs)
        parts.append(beam(f'pillar_a_{s}', m, g, (roof[0] - sign * 0.02, roof[1] - 0.02 - drop, roof_front), 0.035))
        parts.append(beam(f'sill_{s}', m, g, sill_front, 0.03))
        parts.append(beam(f'rail_{s}', m, sill_front, (top[0] - sign * 0.02, top[1], -back), 0.03))
        for name, z in (('b', -0.12), ('c', -back + 0.04)):
            parts.append(beam(f'pillar_{name}_{s}', m, (top[0] - sign * 0.025, top[1], z),
                              (roof[0] - sign * 0.02, roof[1] - 0.02 - low(z), z), 0.05))
    rl, rr = sec.side(1)[3], sec.side(-1)[3]
    for name, z, w in (('bow_front', roof_front, 0.05), ('bow_mid', -0.12, 0.045)):
        parts.append(beam(name, m, (rl[0] - 0.02, rl[1] - 0.025 - low(z), z), (rr[0] + 0.02, rr[1] - 0.025 - low(z), z),
                          w))


def _controls(parts: list[om.Part], floor: float, collective_x: float, stick_z: float, pedal_z: float,
              pedal_floor: float) -> None:
    """The cyclic between the knees (from the floor at `floor`), the collective by the left hand, the pedals in the
    foot well (its floor at `pedal_floor`)."""
    parts.append(cylinder('cyclic_boot', 'rubber', (0.0, floor, stick_z + 0.02), (0.0, floor + 0.1, stick_z), 0.05, 10))
    top = (0.0, -0.47, stick_z - 0.05)
    parts.append(cylinder('cyclic', 'metal_dark', (0.0, floor + 0.1, stick_z), top, 0.014, 8))
    parts.append(cylinder('cyclic_grip', 'rubber', top, (0.0, -0.35, stick_z - 0.07), 0.024, 10))
    parts.append(box('cyclic_head', 'metal_dark', (0.0, -0.335, stick_z - 0.075), (0.045, 0.04, 0.05)))
    parts.append(box('cyclic_trigger', 'paint_olive', (0.0, -0.37, stick_z - 0.045), (0.012, 0.03, 0.02)))
    x = collective_x
    parts.append(box('collective_box', 'metal_dark', (x, -0.78, -0.16), (0.07, 0.08, 0.2)))
    parts.append(cylinder('collective', 'metal_dark', (x, -0.74, -0.22), (x, -0.6, 0.2), 0.016, 8))
    parts.append(cylinder('collective_grip', 'rubber', (x, -0.61, 0.15), (x, -0.585, 0.27), 0.026, 10))
    parts.append(box('collective_head', 'metal_dark', (x, -0.575, 0.29), (0.06, 0.05, 0.06)))
    for sign, s in ((1, 'l'), (-1, 'r')):
        px = sign * 0.15
        parts.append(panel(f'pedal_{s}', 'rubber', (px, pedal_floor + 0.14, pedal_z), 0.09, 0.17, (0, 0.55, -1)))
        parts.append(beam(f'pedal_arm_{s}', 'metal_dark', (px, pedal_floor + 0.02, pedal_z + 0.1),
                          (px, pedal_floor + 0.08, pedal_z + 0.03), 0.02))


def _overhead(parts: list[om.Part], mid: float, roof: float, z0: float, z1: float, width: float) -> None:
    """The overhead panel under the roof (breakers, engine levers), leaning toward the eye (its labels read looking
    up and ahead)."""
    length = z1 - z0
    parts.append(box('overhead_box', 'air_tub', (mid, roof - 0.035, (z0 + z1) / 2), (width, 0.07, length),
                     faces='xXzZ'))
    parts.append(panel('overhead', 'air_overhead', (mid, roof - 0.07, (z0 + z1) / 2), width, length,
                       _unit((0.0, -1.0, -0.35)), up=(0, 0, -1)))


def _consoles(parts: list[om.Part], sec: Section, z0: float, z1: float, width: float, share: float,
              texture: str) -> None:
    """The side consoles under the sills: a block along each wall from the chine up, its top (`texture`: radio,
    navigation and lighting controls) leaning toward the seat; `share` the tub's width share at its front end (it
    narrows toward the nose)."""
    length, top = z1 - z0, sec.top - 0.1
    h = top - sec.chine
    for sign, s in ((1, 'l'), (-1, 'r')):
        w = (sec.w_left if sign > 0 else sec.w_right) * share
        x = sec.mid + sign * (w - width / 2 - 0.01)
        parts.append(box(f'console_{s}', 'air_tub', (x, top - h / 2, (z0 + z1) / 2), (width, h, length),
                         faces=('X' if sign > 0 else 'x') + 'zZ'))
        parts.append(panel(f'console_top_{s}', texture, (x, top + 0.003, (z0 + z1) / 2), length, width,
                           (-0.25 * sign, 1.0, 0.0), up=(-sign, 0, 0), uv=(0, 0, max(1.0, round(length / 0.3)), 1)))


def _seat(parts: list[om.Part], half: float, pan: float, back_z: float) -> None:
    """The pilot's armoured seat (mesh cushions in an armour shell): its pan `pan` below the eye, its back at
    `back_z`, `half` its half width."""
    parts.append(box('seat_pan', 'air_seat', (0.0, pan + 0.04, back_z + 0.24), (2 * half - 0.06, 0.08, 0.42)))
    parts.append(box('seat_back', 'air_seat', (0.0, pan + 0.4, back_z + 0.03), (2 * half - 0.08, 0.66, 0.08)))
    parts.append(box('seat_head', 'air_seat', (0.0, pan + 0.86, back_z + 0.02), (0.26, 0.16, 0.08)))
    for sign, s in ((1, 'l'), (-1, 'r')):
        parts.append(box(f'seat_wing_{s}', 'metal_dark', (sign * (half - 0.015), pan + 0.25, back_z + 0.16),
                         (0.03, 0.42, 0.34)))
    parts.append(box('seat_shell', 'metal_dark', (0.0, pan + 0.4, back_z - 0.02), (2 * half, 0.74, 0.03)))


def _helmet_hud(screens: list[Screen]) -> None:
    """The helmet's display (the AH-64's IHADSS: no combiner glass in the cockpit), its symbols over the view ahead:
    a 'hud' screen with no part, so the forward view stays open."""
    screens.append(Screen('hud', (0.0, -0.02, 0.9), (0.0, 0.0, -1.0), 0.36, 0.27))


def _pilot(cabin: Cabin, utility: bool) -> Kit:
    """A helicopter pilot's cockpit: an AH-64D pilot's station (gunship) or a UH-60M's (utility), centred on the
    eye. The panel's top stays 10 degrees under the line of sight and the windshield's frame at the view's edges:
    the view ahead is for flying and aiming."""
    name = 'heli_utility' if utility else 'heli_gunship'
    side = 0.9 if utility else 0.6
    r = _room(cabin, {'left': side, 'right': side, 'up': 0.7, 'down': 1.08, 'ahead': 1.5, 'back': 0.75},
              {'left': 0.26, 'right': 0.26, 'up': 0.2, 'down': 0.7, 'ahead': 0.65, 'back': 0.3}, name)
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    mid = (L - R) / 2
    sill = -0.42 if utility else -0.3
    sec = Section(mid, L, R, -D, -D + 0.4 * (sill + D), sill, U)
    taper, rise = 0.72, 0.3
    parts: list[om.Part] = []
    screens: list[Screen] = []
    z = min(0.75 if utility else 0.7, A - 0.25)
    front = min(A, z + 0.2)
    top = -0.13 * z / 0.7                  # 10 degrees under the line of sight
    nose_w = 1.0 + (taper - 1.0) * (z + B) / (front + B)   # the tub's width share at the panel
    room_w = 0.92 * nose_w * (L + R)
    if utility:
        lip = _uh60_panel(parts, screens, mid, top, z, room_w)
    else:
        lip = _ah64_panel(parts, screens, mid, top, z, room_w, -D + rise * (z + 0.06 + B) / (front + B))
        _helmet_hud(screens)
    width = room_w - 0.02
    roof_front = min(0.3 if utility else 0.12, z - 0.2)
    drop = 0.3 * U
    _tub(parts, sec, B, front, roof_front, taper, rise, drop)
    _canopy(parts, sec, (mid + width / 2, top + 0.03, lip), B, roof_front, taper, front, drop)
    cw = max(0.08, min(0.2, min(L, R) - 0.3))
    seat_back = -min(B, 0.45) + 0.06
    share = lambda zz: 1.0 + (taper - 1.0) * max(0.0, min(1.0, (zz + B) / (front + B)))   # noqa: E731
    _consoles(parts, sec, seat_back, z - 0.12, cw, share(z - 0.12), 'air_uh60_pedestal' if utility else
              'air_ah64_console')
    half = max(0.18, min(0.26, min(L, R) - cw - 0.04))
    _seat(parts, half, -0.8, seat_back)
    pedal_z = min(z + 0.12, front - 0.12)
    rise_at = lambda zz: -D + rise * max(0.0, min(1.0, (zz + B) / (front + B)))   # noqa: E731
    _controls(parts, rise_at(0.34), min(half + 0.05, L - cw - 0.04), 0.34, pedal_z, rise_at(pedal_z))
    if utility and U > 0.22:   # a gunship's canopy is glass overhead
        width_o = min(0.5, 2 * sec.roof_w * min(L, R) - 0.06)
        _overhead(parts, mid, U - drop * 0.75, -0.12, roof_front - 0.04, width_o)
    used = {p.material for p in parts}
    return Kit(parts, _materials(*sorted(used)), screens=tuple(screens))


def heli_gunship(cabin: Cabin) -> Kit:
    return _pilot(cabin, utility=False)


def heli_utility(cabin: Cabin) -> Kit:
    return _pilot(cabin, utility=True)


# --------------------------------------------------------------------------------------------------- door gunners
def _door(cabin: Cabin, door_left: bool) -> Kit:
    """A door gunner seated in a transport's cabin, facing out of the open door. In the eye's frame ahead is out of
    the door, back across the cabin, and the fuselage runs left-right (the left door's gunner has the nose on his
    right). The cabin's section (y, z: across) is a Section turned on its side: z plays a section's x."""
    name = 'heli_door_left' if door_left else 'heli_door_right'
    nose = -1.0 if door_left else 1.0      # eye-frame x toward the nose
    r = _room(cabin, {'left': 1.1, 'right': 1.1, 'up': 1.3, 'down': 1.3, 'ahead': 0.7, 'back': 2.5},
              {'left': 0.5, 'right': 0.5, 'up': 0.4, 'down': 0.7, 'ahead': 0.3, 'back': 0.4}, name)
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts: list[om.Part] = []
    screens: list[Screen] = []
    half = (A + B) / 2
    centre = (A - B) / 2                   # the cabin's centre line (z)
    side_top = min(0.3, U - 0.35)         # the side walls lean in up to here, the roof chamfers in above
    lean = min(0.2, 0.15 * half)
    sec = Section(centre, half, half, -D, -D + 0.3, side_top, U, floor_w=0.55, roof_w=0.6, top_w=1 - lean / half)
    # A section point (z, y) at x: the eye frame's (x, y, z).
    ring = sec.ring()                      # door side (+z) up, then the far side down

    def pt(x: float, p: Vec2) -> Vec3:
        return (x, p[1], p[0])

    door_side, far_side = sec.side(1), sec.side(-1)
    # The fuselage-wise walls: the floor, the chamfers, the roof, and the two door walls round their openings.
    for wname, i, j, mat in (('floor', 0, 7, 'rubber'), ('chamfer_d', 0, 1, 'air_tub'), ('chamfer_f', 7, 6, 'air_tub'),
                             ('roof_slope_d', 2, 3, 'air_tub'), ('roof_slope_f', 5, 4, 'air_tub'),
                             ('roof', 3, 4, 'air_tub')):
        parts.append(wall(wname, mat, [pt(L, ring[i]), pt(L, ring[j]), pt(-R, ring[j]), pt(-R, ring[i])],
                          tile=0.6 if mat == 'rubber' else TILE))
    dl, dr = min(0.55, L - 0.15), -min(0.55, R - 0.15)
    for tag, (bot, top) in (('d', (door_side[1], door_side[2])), ('f', (far_side[1], far_side[2]))):
        y0, y1 = bot[1], top[1]
        dt = y1 - 0.06

        def at(x: float, y: float) -> Vec3:   # the leaning wall's point at x, y
            return (x, y, bot[0] + (top[0] - bot[0]) * (y - y0) / (y1 - y0))

        for wname, x0, x1 in ((f'wall_{tag}_l', dl, L), (f'wall_{tag}_r', -R, dr)):
            parts.append(wall(wname, 'air_tub', [at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)]))
        parts.append(wall(f'wall_{tag}_top', 'air_tub', [at(dr, dt), at(dl, dt), at(dl, y1), at(dr, y1)]))
        along = _unit(_sub(at(0.0, y1), at(0.0, y0)))
        inward = _unit(_cross((1.0, 0.0, 0.0), along)) if tag == 'd' else _unit(_cross(along, (1.0, 0.0, 0.0)))
        if _dot(inward, (0.0, 0.0, centre - bot[0])) < 0:
            inward = (-inward[0], -inward[1], -inward[2])
        c = _add(at((dl + dr) / 2, (y0 + dt) / 2), inward, 0.03)
        parts.append(frame(f'door_frame_{tag}', 'canopy_frame', c, dl - dr + 0.08, math.dist(at(0, y0), at(0, dt)) + 0.08,
                           0.05, inward, 0.05, up=along))
    # The door slid back along the outside toward the tail (seen through the opening's edge), a grab handle on the
    # nose side of the opening, a rail along the roof.
    tail = -nose
    edge = dl if tail > 0 else dr
    bot, top = door_side[1], door_side[2]

    def door_at(x: float, y: float, inset: float) -> Vec3:   # on the door's leaning wall, `inset` inside it
        return (x, y, bot[0] + (top[0] - bot[0]) * (y - bot[1]) / (top[1] - bot[1]) - inset)

    room_tail = (L if tail > 0 else R) - abs(edge) - 0.03
    if room_tail > 0.2:
        slid = min(0.8, room_tail)
        x0, x1 = edge, edge + tail * slid
        y0, y1 = sec.chine + 0.04, side_top - 0.06
        parts.append(wall('door_slid', 'paint_olive', [door_at(x0, y0, 0.05), door_at(x1, y0, 0.05),
                                                       door_at(x1, y1, 0.05), door_at(x0, y1, 0.05)]))
    gx = (dr if tail > 0 else dl) - tail * 0.06
    parts.append(beam('grab_handle', 'metal_dark', door_at(gx, -0.35, 0.07), door_at(gx, side_top - 0.12, 0.07), 0.025))
    parts.append(beam('roof_rail', 'metal_dark', (L - 0.05, U - 0.1, centre), (-R + 0.05, U - 0.1, centre), 0.03))
    # The cabin's two ends: toward the nose the cockpit bulkhead with its passage, toward the tail the cabin's end.
    for wname, x in (('end_nose', nose * (L if nose > 0 else R)), ('end_tail', tail * (L if tail > 0 else R))):
        parts.append(wall(wname, 'air_tub', [pt(x, p) for p in ring]))
    xn = nose * ((L if nose > 0 else R) - 0.01)
    pw, ph = min(0.55, half * 0.6), min(1.5, side_top + D - 0.1)
    parts.append(wall('passage', 'rubber', [(xn, -D + 0.05, centre - pw / 2), (xn, -D + 0.05, centre + pw / 2),
                                            (xn, -D + 0.05 + ph, centre + pw / 2), (xn, -D + 0.05 + ph, centre - pw / 2)]))
    # The troop bench along the far wall, toward the tail.
    bx = tail * ((L if tail > 0 else R) - 0.45)
    parts.append(box('bench', 'paint_olive', (bx, -D + 0.42, -B + 0.3), (0.8, 0.05, 0.4)))
    parts.append(box('bench_back', 'paint_olive', (bx, -D + 0.75, -B + 0.12), (0.8, 0.5, 0.03)))
    # The gunner's seat behind the eye, facing the door.
    sz = -0.35
    parts.append(box('seat_pan', 'paint_olive', (0.0, -0.78, sz + 0.2), (0.48, 0.06, 0.4)))
    parts.append(box('seat_back', 'paint_olive', (0.0, -0.4, sz - 0.02), (0.48, 0.7, 0.05)))
    parts.append(box('seat_frame', 'metal_dark', (0.0, (-0.81 - D) / 2, sz + 0.2), (0.4, D - 0.81, 0.04)))
    # The gun's control: a pedestal before the seat, two spade grips, the sight's screen on top.
    gz = min(0.36, A - 0.12)
    parts.append(cylinder('ctrl_post', 'metal_dark', (0.0, -D + 0.2, gz - 0.02), (0.0, -0.42, gz), 0.035, 10))
    parts.append(box('ctrl_head', 'metal_dark', (0.0, -0.38, gz), (0.3, 0.08, 0.1)))
    for sign, s in ((1, 'l'), (-1, 'r')):
        x = sign * 0.17
        parts.append(cylinder(f'spade_{s}', 'rubber', (x, -0.46, gz - 0.06), (x, -0.32, gz - 0.06), 0.022, 10))
        parts.append(beam(f'spade_arm_{s}', 'metal_dark', (x, -0.38, gz), (x, -0.38, gz - 0.06), 0.02))
    sight_c = (0.0, -0.22, gz + 0.04)
    facing = _unit((0.0, 0.22, -gz - 0.04))
    parts.append(box('sight_box', 'metal_dark', _add(sight_c, facing, -0.03), (0.22, 0.17, 0.05)))
    _screen(parts, screens, 'weapons', sight_c, facing, 0.18, 0.13)
    return Kit(parts, _materials('air_tub', 'screen', 'rubber', 'metal_dark', 'canopy_frame', 'paint_olive'),
               screens=tuple(screens))


def heli_door_left(cabin: Cabin) -> Kit:
    return _door(cabin, door_left=True)


def heli_door_right(cabin: Cabin) -> Kit:
    return _door(cabin, door_left=False)


KITS: dict = {
    'heli_gunship': heli_gunship,
    'heli_utility': heli_utility,
    'heli_door_left': heli_door_left,
    'heli_door_right': heli_door_right,
}
