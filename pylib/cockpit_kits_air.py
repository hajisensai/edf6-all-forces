"""Cockpit kits for aircraft (pylib/cockpit_kits.py merges KITS; the conventions are its docstring's: a kit is a
function of the room round its eye, returning parts in the eye's frame laid out inside that room).

The stock helicopters have no interior: seen from a seat, culled as the game culls, the fuselage vanishes and only a
few frame strips, the rotor and the guns remain (tools/cabin_view.py). So a helicopter kit supplies the whole cabin,
and the pilots' kits are real cockpits: 3D models of the real aircraft's crew station (Sketchfab, CC BY), cut down by
tools/prep_cockpit_air.py to what the seated eye sees (no crew figures, no canopy glass: the view out is open) and
stored in the eye's frame in <model dir>/cockpits/parts/<asset>/cockpit.glb with their textures (credits:
<model dir>/cockpits/CREDITS_air.txt).

    heli_gunship      an attack helicopter's cockpit (V506 / V602 / 409): an AH-64E's front station (its canopy
                      frame, glare shield, the two MFDs, the TEDAC on its stalk, the up-front display, the side
                      consoles, the floor and controls); the rear (pilot's) station would look through the front
                      canopy's frame posts. Screens: the left MFD 'flight', the right 'weapons', the TEDAC 'map', the
                      up-front display 'status'; the helmet's display (IHADSS) the 'hud', no combiner glass in the view
    heli_utility      a transport's cockpit (410): a UH-60M's pilot station (the right seat): the four-MFD panel
                      across the cockpit, the centre console, the overhead frame, the copilot's seat beside. Screens:
                      the pilot's outer MFD 'flight', his inner one 'map', the copilot's inner one 'status'
    heli_door_left/_right  a door gunner in a transport's cabin at the left / right door, looking out of it (the
                      fit's yaw turns the eye to the door: 90 / -90): the door's frame, the cabin's walls round it,
                      the floor, the roof, the gunner's seat, the gun's control grips under the sight's 'weapons'
                      screen; the cockpit bulkhead with its passage toward the nose, the troop cabin's end toward the
                      tail, the opposite door open across the cabin. No stock helicopter has a crewed door gun (the
                      410's guns are remote pods on its stub wings: cockpit_kits.remote_display), so no stock fit uses
                      them; they wait for a transport with real door gunners

A real cockpit is scaled about the eye to the room the vehicle has round it (cockpits.clearance): from the seat a
scaled cockpit looks the same (every part keeps its direction from the eye), only nearer. The scale is the one that
keeps it inside the room left, right, up and down; ahead and behind, what is past the room is left out (the nose
beyond the glare shield, the seat's back). The screens (cockpits.Screen) sit on the model's displays, measured on it.

The door gunners' walls are drawn (textures/air_tub: tools/make_cockpit_textures_air.py; the rest the shared
textures, tools/make_cockpit_textures.py).
"""
from __future__ import annotations

import functools
import math
import os
from dataclasses import dataclass

import numpy as np

import cockpits
import obj_model as om
from cockpit_room import TEX, Cabin, _room
from cockpit_shapes import basis, box, cylinder, frame, merge, panel, quad
from cockpits import Kit, KitMaterial, Screen

Vec3 = tuple[float, float, float]
Vec2 = tuple[float, float]

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


def _helmet_hud(screens: list[Screen]) -> None:
    """The helmet's display (the AH-64's IHADSS: no combiner glass in the cockpit), its symbols over the view ahead:
    a 'hud' screen with no part, so the forward view stays open."""
    screens.append(Screen('hud', (0.0, -0.02, 0.9), (0.0, 0.0, -1.0), 0.36, 0.27))


# ------------------------------------------------------------------------------------------------- real cockpits
@dataclass(frozen=True)
class RealCockpit:
    """A crew station's model (tools/prep_cockpit_air.py ASSETS: `asset` its file under cockpits/, in the eye's frame,
    metres, at full size) and its displays at full size: (screen kind, centre, the display's normal toward the eye,
    width, height), measured on the model from the eye. `least`: the room the eye must have (cockpit_room._room)."""
    asset: str
    screens: tuple[tuple[str, Vec3, Vec3, float, float], ...]
    least: Cabin
    helmet_hud: bool = False


REAL: dict[str, RealCockpit] = {
    'heli_gunship': RealCockpit(
        'parts/sf-ah64e-cpg/cockpit.glb',
        (('flight', (0.3406, -0.3964, 0.5635), (-0.173, 0.253, -0.952), 0.155, 0.155),
         ('weapons', (-0.3361, -0.3949, 0.5538), (0.173, 0.256, -0.951), 0.155, 0.155),
         ('map', (-0.0185, -0.1989, 0.2964), (0.0, 0.39, -0.921), 0.13, 0.13),
         ('status', (0.3101, -0.1984, 0.51), (-0.301, 0.309, -0.902), 0.155, 0.075)),
        {'left': 0.25, 'right': 0.25, 'up': 0.2, 'down': 0.5, 'ahead': 0.6, 'back': 0.15}, helmet_hud=True),
    'heli_utility': RealCockpit(
        'parts/sf-uh60m-pilot/cockpit.glb',
        (('flight', (-0.0952, -0.389, 0.6856), (0.036, 0.0, -0.999), 0.19, 0.115),
         ('map', (0.1846, -0.3954, 0.7005), (0.036, 0.0, -0.999), 0.19, 0.125),
         ('status', (0.6874, -0.3895, 0.7004), (-0.035, 0.0, -0.999), 0.19, 0.125)),
        {'left': 0.3, 'right': 0.2, 'up': 0.2, 'down': 0.5, 'ahead': 0.6, 'back': 0.15}),
}
SCALED_BY = ('left', 'right', 'up', 'down')   # the sides a real cockpit is scaled to fit; ahead and back it is cut


def _srgb(base: tuple[float, ...]) -> tuple[int, int, int]:
    return tuple(max(24, int(round(255 * min(1.0, max(0.0, c)) ** (1 / 2.2)))) for c in base[:3])  # type: ignore[return-value]


@functools.lru_cache(maxsize=None)
def _real_mesh(asset: str) -> tuple[tuple[tuple[str, KitMaterial, np.ndarray, np.ndarray, np.ndarray, np.ndarray], ...],
                                    tuple[tuple[str, float], ...]]:
    """A prepared cockpit (cockpit.glb in the eye's frame): per material its key, KitMaterial (its PNG beside the
    file, else its colour), positions, normals, UVs, triangles; and how far it reaches each way from the eye."""
    import gltf
    path = os.path.join(cockpits.asset_root(), asset)
    if not os.path.isfile(path):
        raise cockpits.CockpitAssetsMissing(f'{path} is missing (tools/prep_cockpit_air.py makes it)')
    s = gltf.read(path)
    rel = os.path.dirname(asset)
    raw, _ = gltf._container(path)
    images = [img['uri'] for img in raw.get('images', [])]
    out = []
    for k, (m, spec) in enumerate(zip(s.materials, raw['materials'])):
        tris = s.triangles[s.material == k]
        if not len(tris):
            continue
        used, inv = np.unique(tris.reshape(-1), return_inverse=True)
        tex = spec.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        mat = KitMaterial(f'{rel}/{images[tex["index"]]}' if tex is not None else _srgb(m.base))
        uv = s.uvs[used] if s.uvs is not None else np.zeros((len(used), 2))
        out.append((m.name, mat, s.positions[used], s.normals[used], uv, inv.reshape(-1, 3)))
    p = s.positions
    reach = (('left', p[:, 0].max()), ('right', -p[:, 0].min()), ('up', p[:, 1].max()), ('down', -p[:, 1].min()),
             ('ahead', p[:, 2].max()), ('back', -p[:, 2].min()))
    return tuple(out), tuple((side, float(v)) for side, v in reach)


def _real(cabin: Cabin, name: str) -> Kit:
    """The real cockpit REAL[name] scaled about the eye to fit the room left, right, up and down, cut to it ahead and
    behind (a triangle not wholly inside is left out), its screens on its displays."""
    spec = REAL[name]
    meshes, reach = _real_mesh(spec.asset)
    ext = dict(reach)
    r = _room(cabin, ext, spec.least, name)
    k = min(1.0, *(r[side] / ext[side] for side in SCALED_BY))
    lo = np.array([-r['right'], -r['down'], -r['back']])
    hi = np.array([r['left'], r['up'], r['ahead']])
    parts: list[om.Part] = []
    mats: dict[str, KitMaterial] = {}
    for key, mat, pos, nrm, uv, tris in meshes:
        q = pos * k
        inside = ((q >= lo) & (q <= hi)).all(1)
        t = tris[inside[tris].all(1)]
        if not len(t):
            continue
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        verts = [om.Vertex(tuple(map(float, q[i])), tuple(map(float, nrm[i])), (float(uv[i, 0]), float(uv[i, 1])))
                 for i in used]
        key = f'real_{key}'
        parts.append(om.Part(key, key, verts, [tuple(map(int, row)) for row in inv.reshape(-1, 3)]))
        mats[key] = mat
    screens: list[Screen] = []
    for kind, c, facing, w, h in spec.screens:
        _screen(parts, screens, kind, (c[0] * k, c[1] * k, c[2] * k), facing, w * k, h * k)
    if spec.helmet_hud:
        _helmet_hud(screens)
    mats.update(_materials('screen'))
    return Kit(parts, mats, screens=tuple(screens))


def heli_gunship(cabin: Cabin) -> Kit:
    return _real(cabin, 'heli_gunship')


def heli_utility(cabin: Cabin) -> Kit:
    return _real(cabin, 'heli_utility')


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
