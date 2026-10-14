"""Cockpit kits for the plugin's own vehicles (jets, gunship, transports; pylib/cockpit_kits.py merges KITS; the
conventions are its docstring's: a kit is a function of the room round its eye, returning parts in the eye's frame
laid out inside that room). The plugin's other vehicles take the families' kits (pylib/cockpit_fits_plugin.py: the
Katyusha's truck cab, the sidecar's bike, the artillery's turret gunner, a camera's monitor for the drones, the
drill, the Sazabi and the gunship's side gunner; the transport helicopter's pilot sits in the stock 410's cockpit).

Every cockpit here is a real one, cut out of a modelled aircraft (Sketchfab, CC BY 4.0; the user, 2026-10-13: "飞机座舱
拿不到模型吗", the photographs on boxes were "太抽象了"): tools/prep_cockpit_plugin.py keeps each model's cockpit round
its seat's eye, in the eye's frame and in metres, as parts/<slug>/cockpit.npz with its textures (credits:
<model dir>/cockpits/CREDITS_plugin.txt).

    jet_f16           the F-16D Block 60's front cockpit: the up-front controls and the two MFDs (weapons left, map
                      right), the threat scope ('status'), the data entry display ('flight'), the side consoles, the
                      side stick, the ejection seat; the HUD's two posts round the line of sight, its glass left out
                      (the 'hud' screen: the plugin draws its symbols on the world through it)
    jet_j10s          the J-10S's front cockpit (the plugin's other fighters: a modern three-display panel, the
                      F-35's stand-in): the three MFDs (map, flight, weapons), the up-front panel's displays
                      ('status'), the HUD's posts (the 'hud' screen between them); the windscreen's arch left out
    plane_deck        the C-130J's flight deck from the left (pilot's) seat: its glass panel (the pilot's PFD:
                      'flight', his nav display: 'map', the engine display: 'status'), the yoke, the pedestal, the
                      copilot's seat, the overhead console, the side windows' frames; the view ahead over the glare
                      shield kept open (its HUD and the windscreen's posts there left out)
    plane_deck_gun    the same deck for a gunship's pilot: the copilot's nav display is his 'weapons'
    plane_hold        the C-130J's cargo hold round a passenger on the right-hand troop seats, looking across: the
                      red troop seats along both walls, the roller floor, the walls' insulation and cargo nets, the
                      roof's ribs and lights
    heli_cabin        the UH-60M's troop cabin from a forward-facing troop seat: the seats round him, the cabin's
                      windows, the gunners' seats and the cockpit ahead
    passenger         another passenger in a hold another seat's kit draws: an eye only

A vehicle's room is not the real aircraft's (the stock bombers' and transports' hulls the plugin flies are of their own
size): a cockpit is cut at the room's walls (each triangle clipped there, MARGIN inside the hull) and, where the room
is smaller than the cockpit needs (Model.full), scaled down about the eye, so from the seat it looks the same, only
the walls are nearer. A hold that other passengers' eyes sit in has a fixed scale (Model.scale): their places in it
(pylib/cockpit_fits_plugin.py) are measured on the same model at the same scale. The view ahead stays open for flying
and aiming (the user: "这是为游戏服务的"): the hull itself is not drawn from inside (the game culls its backs), the
cockpits' canopy bows, HUD glass and windscreen posts before the line of sight are left out when the model is cut.
A fighter's cockpit and the flight deck are closed tubs (Model.sill, _tub): a floor and walls up to the canopy's sill
round the cut model, so looking down or aside under the sill the eye meets the cockpit, never the world through the
undrawn hull (a cut takes the model's floor away where the room is shallower than the real cockpit; cockpits.leaks
checks it).
"""
from __future__ import annotations

import functools
import math
import os
from dataclasses import dataclass, replace

import numpy as np

import cockpits
import obj_model as om
from cockpit_room import TEX, Cabin, _room
from cockpit_shapes import merge, panel
from cockpits import Kit, KitFn, KitMaterial, Screen

Vec3 = tuple[float, float, float]
SIDES = ('left', 'right', 'up', 'down', 'ahead', 'back')
SCREEN_PROUD = 0.003   # m a screen's read-out stands before the display it is drawn on (no z-fighting)


@dataclass(frozen=True)
class ScreenAt:
    """A live read-out on the model's own display (measured on the cut model, full size, eye frame): its centre, the
    display's facing (toward the eye), width and height."""
    kind: str
    centre: Vec3
    facing: Vec3
    width: float
    height: float


@dataclass(frozen=True)
class Model:
    """A kit cut from parts/<slug>/cockpit.npz: `want` how far its walls go at full size (the cut box; the room may
    pull them in), `least` the room it cannot do without (cockpit_room._room), `full` the room it needs at full size
    (a side with less scales it down, no smaller than `least_scale`; `scale`: always that scale), its screens."""
    slug: str
    want: Cabin
    least: Cabin
    full: Cabin
    screens: tuple[ScreenAt, ...] = ()
    least_scale: float = 0.6
    scale: float | None = None
    caps: tuple[str, ...] = ()          # sides ('left', 'right', 'back') whose cut is closed by a plain wall
    cap_texture: str = 'metal_dark'     # the closing wall's texture (textures/<name>.png)
    sill: float | None = None           # a closed tub's canopy sill, degrees of pitch (Kit.sill; _tub)


@functools.lru_cache(maxsize=None)
def _cockpit(slug: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, tuple[str, ...],
                                 np.ndarray]:
    """The cut model in its eye's frame at full size: positions, normals, UVs, triangles, each triangle's material,
    the materials' texture files (under parts/<slug>/, '' none) and sRGB base colours."""
    with np.load(os.path.join(cockpits.asset_root(), 'parts', slug, 'cockpit.npz'), allow_pickle=False) as z:
        return (z['pos'].astype(np.float64), z['nrm'].astype(np.float64), z['uv'].astype(np.float64),
                z['tris'].astype(np.int64), z['mat'].astype(np.int64), tuple(str(t) for t in z['textures']),
                z['colours'])


def _clip(pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray, lo: np.ndarray, hi: np.ndarray
          ) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """The triangles cut to the box lo..hi: wholly inside kept, wholly beyond one of its planes left out, the rest
    clipped at its planes (new corners interpolated, the pieces fanned). Returns positions, normals, UVs, triangles and
    each triangle's source triangle."""
    c = pos[tris]
    inside = ((c >= lo) & (c <= hi)).all((1, 2))
    beyond = ((c < lo).all(1) | (c > hi).all(1)).any(1)
    cross = np.flatnonzero(~inside & ~beyond)
    out_p, out_n, out_t, out_tri, out_src = [pos], [nrm], [uv], [tris[inside]], [np.flatnonzero(inside)]
    base = len(pos)
    planes = [(axis, bound, sign) for axis in range(3) for bound, sign in ((lo[axis], 1.0), (hi[axis], -1.0))]
    new_p, new_n, new_t, new_tri, new_src = [], [], [], [], []
    for k in cross:
        poly = [(pos[i], nrm[i], uv[i]) for i in tris[k]]
        for axis, bound, sign in planes:
            if not poly:
                break
            nxt = []
            for j, cur in enumerate(poly):
                prev = poly[j - 1]
                dc, dp = sign * (cur[0][axis] - bound), sign * (prev[0][axis] - bound)
                if (dc >= 0) != (dp >= 0):
                    t = dp / (dp - dc)
                    nxt.append(tuple(prev[m] + t * (cur[m] - prev[m]) for m in range(3)))
                if dc >= 0:
                    nxt.append(cur)
            poly = nxt
        if len(poly) < 3:
            continue
        first = base + len(new_p)
        for p, n, t in poly:
            new_p.append(p)
            new_n.append(n)
            new_t.append(t)
        for j in range(1, len(poly) - 1):
            new_tri.append((first, first + j, first + j + 1))
            new_src.append(k)
    if new_p:
        out_p.append(np.array(new_p))
        out_n.append(np.array(new_n))
        out_t.append(np.array(new_t))
        out_tri.append(np.array(new_tri, dtype=np.int64))
        out_src.append(np.array(new_src, dtype=np.int64))
    return (np.concatenate(out_p), np.concatenate(out_n), np.concatenate(out_t), np.concatenate(out_tri),
            np.concatenate(out_src))


def _parts(slug: str, k: float, lo: np.ndarray, hi: np.ndarray) -> tuple[list[om.Part], dict[str, KitMaterial]]:
    """The cut model scaled `k` about the eye and clipped to lo..hi, one part per material."""
    pos, nrm, uv, tris, mat, textures, colours = _cockpit(slug)
    p, n, t, tri, src = _clip(pos * k, nrm, uv, tris, lo, hi)
    m_of = mat[src]
    parts, mats = [], {}
    for m in np.unique(m_of):
        sel = tri[m_of == m]
        used, inv = np.unique(sel.reshape(-1), return_inverse=True)
        nn = n[used] / np.maximum(np.linalg.norm(n[used], axis=1, keepdims=True), 1e-12)
        verts = [om.Vertex((float(a), float(b), float(c)), (float(d), float(e), float(f)), (float(u), float(v)))
                 for (a, b, c), (d, e, f), (u, v) in zip(p[used], nn, t[used])]
        name = f'm{m}'
        parts.append(om.Part(name, name, verts, [tuple(map(int, r)) for r in inv.reshape(-1, 3)]))
        mats[name] = KitMaterial(f'parts/{slug}/{textures[m]}' if textures[m] else tuple(int(c) for c in colours[m]))
    return parts, mats


CAP_AXES = {'left': (0, 1.0), 'right': (0, -1.0), 'back': (2, -1.0)}


def _caps(parts: list[om.Part], mats: dict[str, KitMaterial], model: Model, lo: np.ndarray, hi: np.ndarray) -> None:
    """A plain wall over each of `model.caps` where the model is cut there (a hold cut short of its real ends would
    open on the world: the hull is not drawn from inside), as wide and high as the model's cut section, facing the
    eye."""
    if not model.caps or not parts:
        return
    pts = np.array([v.pos for p in parts for v in p.verts])
    a, b = pts.min(0), pts.max(0)
    for side in model.caps:
        axis, sign = CAP_AXES[side]
        at = hi[axis] if sign > 0 else lo[axis]
        if abs((b[axis] if sign > 0 else a[axis]) - at) > 0.01:   # the model ends short of the wall: nothing cut
            continue
        u, v = [k for k in range(3) if k != axis]
        centre = [0.0, 0.0, 0.0]
        centre[axis] = at - sign * 0.002
        centre[u], centre[v] = (a[u] + b[u]) / 2, (a[v] + b[v]) / 2
        size = {u: b[u] - a[u], v: b[v] - a[v]}
        facing = [0.0, 0.0, 0.0]
        facing[axis] = -sign
        width, height = (size[2], size[1]) if axis == 0 else (size[0], size[1])
        parts.append(panel(f'cap_{side}', 'cap', tuple(centre), width, height, tuple(facing),
                           uv=(0.0, 0.0, width / 0.8, height / 0.8)))
        mats['cap'] = KitMaterial(TEX.format(model.cap_texture))


TUB_TEXTURE, TUB_STRIPS, TUB_INSET = 'air_tub', 12, 0.002   # textures/<name>.png; strips a wall's top is drawn in


def _tub_wall(name: str, axis: int, at: float, along: int, span: tuple[float, float], floor: float, roof: float,
              sill: float) -> list[om.Part]:
    """The wall in the plane `axis` = `at` (eye frame), from `span[0]` to `span[1]` along the axis `along`, from the
    floor up to where the sill's cone (pitch `sill` degrees from the eye) meets it (no higher than the roof), in
    TUB_STRIPS upright strips (on a flat wall that cone is a curve), its front toward the eye."""
    slope = math.tan(math.radians(sill))
    ts = np.linspace(span[0], span[1], TUB_STRIPS + 1)
    tops = [min(roof, max(floor, slope * math.hypot(at, t))) for t in ts]
    facing = [0.0, 0.0, 0.0]
    facing[axis] = -math.copysign(1.0, at)

    def pt(t: float, y: float) -> Vec3:
        p = [0.0, y, 0.0]
        p[axis], p[along] = at, t
        return (p[0], p[1], p[2])
    out = []
    for k in range(TUB_STRIPS):
        if max(tops[k], tops[k + 1]) - floor > 1e-4:
            out += _face(f'{name}{k}', 'tub', [pt(ts[k], floor), pt(ts[k + 1], floor), pt(ts[k + 1], tops[k + 1]),
                                               pt(ts[k], tops[k])], tuple(facing))
    return out


def _tub(lo: np.ndarray, hi: np.ndarray, sill: float) -> om.Part:
    """A closed cockpit tub round the eye in the box lo..hi (the room the model is cut to; the user, 2026-10-14:
    "固定翼的底部是空白，完全能看过去" and "照着人家完全照搬": a real cockpit's tub, as DCS and War Thunder draw theirs): the
    floor, and the four walls up to the canopy's sill (pitch `sill` from the eye), TUB_INSET inside the box, all facing
    the eye. The model cut to the box stands in it; where the cut took the model's own floor or side walls away (a room
    smaller than the real cockpit), or the model never had them (it was modelled to be seen through glass from above),
    the tub is what the eye meets instead of the world: the hull itself is not drawn from inside."""
    (x0, y0, z0), (x1, y1, z1) = lo + TUB_INSET, hi - TUB_INSET
    parts = _face('tub_floor', 'tub', [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0.0, 1.0, 0.0))
    for name, axis, at, along, span in (('tub_left', 0, x1, 2, (z0, z1)), ('tub_right', 0, x0, 2, (z0, z1)),
                                        ('tub_back', 2, z0, 0, (x0, x1)), ('tub_front', 2, z1, 0, (x0, x1))):
        parts += _tub_wall(name, axis, at, along, span, y0, y1, sill)
    return merge(parts, 'tub')


def _face(name: str, material: str, pts: list[Vec3], facing: Vec3) -> list[om.Part]:
    """The flat convex polygon `pts` (its corners in order; repeated ones count once) with its front toward
    `facing`, UVs laid flat on it one repeat per 0.8 m; none when it has no area."""
    ring = [p for k, p in enumerate(pts) if math.dist(p, pts[k - 1]) > 1e-6]
    if len(ring) < 3:
        return []
    n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if len(ring) == 4 and np.linalg.norm(n) < 1e-9:
        ring = ring[1:] + ring[:1]
        n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if np.linalg.norm(n) < 1e-9:
        return []
    if float(np.dot(n, facing)) < 0:
        ring = ring[::-1]
    axes = [i for i in range(3) if abs(facing[i]) < 0.5]
    f = tuple(float(c) for c in np.array(facing) / np.linalg.norm(facing))
    verts = [om.Vertex(p, f, (p[axes[0]] / 0.8, -p[axes[1]] / 0.8)) for p in ring]
    return [om.Part(name, material, verts, [(0, k, k + 1) for k in range(1, len(ring) - 1)])]


def _model_kit(cabin: Cabin, name: str, model: Model) -> Kit:
    """`model` laid out in the room round the eye: scaled (Model.full / scale), cut at the walls."""
    r = _room(cabin, dict(model.want), dict(model.least), name)   # raises where the room is too small
    if model.scale is not None:
        k = model.scale
    else:
        k = max(model.least_scale, min(1.0, *(r[side] / need for side, need in model.full.items())))
    wall = r
    lo = np.array([-wall['right'], -wall['down'], -wall['back']])
    hi = np.array([wall['left'], wall['up'], wall['ahead']])
    parts, mats = _parts(model.slug, k, lo, hi)
    _caps(parts, mats, model, lo, hi)
    if model.sill is not None:
        parts.append(_tub(lo, hi, model.sill))
        mats['tub'] = KitMaterial(TEX.format(TUB_TEXTURE))
    screens = []
    for s in model.screens:
        f = np.array(s.facing) / np.linalg.norm(s.facing)
        c = np.array(s.centre) * k + SCREEN_PROUD * f
        if (c >= lo).all() and (c <= hi).all():
            screens.append(Screen(s.kind, tuple(map(float, c)), tuple(map(float, f)), s.width * k, s.height * k))
    return Kit(parts, mats, screens=tuple(screens), sill=model.sill)


def _kit(name: str, model: Model, doc: str) -> KitFn:
    def kit(cabin: Cabin) -> Kit:
        return _model_kit(cabin, name, model)
    kit.__name__, kit.__doc__ = name, doc
    return kit


# ---------------------------------------------------------------------------------------------------- the models
FIGHTER_WANT = {'left': 0.62, 'right': 0.62, 'up': 0.5, 'down': 1.15, 'ahead': 1.8, 'back': 0.45}
FIGHTER_LEAST = {'left': 0.3, 'right': 0.3, 'up': 0.06, 'down': 0.4, 'ahead': 0.6, 'back': 0.15}
# A fighter's panel and consoles: the side consoles 0.5 m out, the MFDs 0.3-0.45 m down and 0.7-0.9 m ahead.
FIGHTER_FULL = {'left': 0.5, 'right': 0.5, 'down': 0.6, 'ahead': 0.95}
# A fighter's canopy sill seen from the seat (a bubble canopy's rail at the shoulder, 0.25 m under the eye and 0.5 m
# out): under it the tub (the floor, the side consoles' walls, the seat's bulkhead), over it the canopy.
FIGHTER_SILL = -25.0

# The F-16D's displays (tools/prep_cockpit_plugin.py F16D; measured on the cut model by casting rays from the eye):
# the two MFDs, the threat warning scope, the data entry display, the HUD's glass between its posts.
F16 = Model('plugin_f16d', FIGHTER_WANT, FIGHTER_LEAST, FIGHTER_FULL, (
    ScreenAt('weapons', (0.178, -0.309, 0.889), (0.0, 0.113, -0.994), 0.11, 0.11),
    ScreenAt('map', (-0.179, -0.309, 0.889), (0.0, 0.113, -0.994), 0.11, 0.11),
    ScreenAt('status', (0.156, -0.179, 0.893), (0.0, 0.113, -0.994), 0.052, 0.052),
    ScreenAt('flight', (-0.133, -0.104, 0.733), (0.018, 0.259, -0.966), 0.064, 0.028),
    ScreenAt('hud', (0.0, 0.05, 0.70), (0.0, 0.0, -1.0), 0.14, 0.15),
), sill=FIGHTER_SILL)
# The J-10S's: its three MFDs, the up-front control panel's displays, the HUD's glass between its posts.
J10 = Model('plugin_j10s', FIGHTER_WANT, FIGHTER_LEAST, FIGHTER_FULL, (
    ScreenAt('map', (0.157, -0.440, 0.708), (0.0, 0.174, -0.985), 0.115, 0.105),
    ScreenAt('flight', (-0.010, -0.454, 0.705), (0.0, 0.174, -0.985), 0.115, 0.095),
    ScreenAt('weapons', (-0.177, -0.440, 0.708), (0.0, 0.174, -0.985), 0.115, 0.105),
    ScreenAt('status', (-0.016, -0.242, 0.567), (0.0, -0.276, -0.961), 0.062, 0.07),
    ScreenAt('hud', (0.0, -0.095, 0.625), (0.0, 0.0, -1.0), 0.16, 0.13),
), sill=FIGHTER_SILL)

# The same cockpit for a seat in a narrow canopy bump (the multirole's: closed only 0.23 m either side of the eye and
# 0.18 m over it, seen along the diagonals): the walls that close, the cockpit scaled to them (from the seat it looks
# the same, its consoles cut where a full-size one's would be 0.5 m out).
J10_NARROW = replace(J10, want={**FIGHTER_WANT, 'left': 0.25, 'right': 0.25, 'up': 0.2, 'back': 0.38},
                     least={**FIGHTER_LEAST, 'left': 0.15, 'right': 0.15}, least_scale=0.4)

# The C-130J's flight deck from the left seat (tools/prep_cockpit_plugin.py C130_DECK): the windscreens 1-1.3 m ahead,
# the overhead console 0.3-0.9 m up; a lower roof scales it down (the stock bomber's cockpit bump: 0.64 m over the eye).
# DECK_SILL: the side windows' lower frames seen from the left seat; under them the deck's floor and walls are closed.
# Its displays: the pilot's PFD, the engine display, the two displays right of it (nav, the copilot's).
DECK_SILL = -25.0
DECK_SCREENS = (
    ScreenAt('flight', (0.022, -0.475, 0.854), (0.0, 0.0, -1.0), 0.17, 0.14),
    ScreenAt('status', (-0.483, -0.530, 0.851), (0.0, 0.0, -1.0), 0.17, 0.23),
    ScreenAt('map', (-0.727, -0.531, 0.851), (0.0, 0.0, -1.0), 0.2, 0.25),
)
DECK = Model('plugin_c130_deck', {'left': 1.4, 'right': 3.2, 'up': 1.2, 'down': 1.3, 'ahead': 2.3, 'back': 1.4},
             {'left': 0.3, 'right': 0.5, 'up': 0.2, 'down': 0.5, 'ahead': 0.7, 'back': 0.3},
             {'left': 1.2, 'up': 1.0, 'down': 1.1, 'ahead': 1.4}, DECK_SCREENS, least_scale=0.5, sill=DECK_SILL)
DECK_GUN = replace(DECK, screens=DECK_SCREENS + (
    ScreenAt('weapons', (-1.018, -0.577, 0.856), (0.0, 0.0, -1.0), 0.22, 0.28),))

# The C-130J's hold (C130_HOLD) in the stock bomber's hull at HOLD_SCALE (its section there is 2.5 m high: a real
# hold's 3 m does not fit; from the seat it looks the same): the walls where the hull closes round the first
# passenger's eye (pylib/cockpit_fits_plugin.py), the hold's cut ends closed. HOLD_SEATS: the other passengers' places
# on the two troop-seat runs at full size, in the first one's eye frame (along the hold: + toward the tail; the far run
# 2.3 m across, facing back), one seat (0.45 m) apart.
HOLD_SCALE = 0.76
HOLD = Model('plugin_c130_hold', {'left': 1.35, 'right': 1.0, 'up': 1.48, 'down': 0.98, 'ahead': 2.18, 'back': 0.38},
             {'left': 0.5, 'right': 0.5, 'up': 0.4, 'down': 0.5, 'ahead': 0.8, 'back': 0.15}, {}, scale=HOLD_SCALE,
             caps=('left', 'right'))
HOLD_ALONG = (0.45, -0.45, 0.9, -0.9, 1.35)          # the first run's other seats (the first passenger's at 0)
HOLD_FAR_ALONG = (0.0, 0.45, -0.45, 0.9, -0.9, 1.35)  # the far run's
HOLD_ACROSS = 2.30                                    # the far run's eyes, ahead of the first one's

# The UH-60M's troop cabin (UH60_CABIN) in the 410's hull at CABIN_SCALE, the walls where the hull closes round the
# first passenger's eye, its cut end behind closed. CABIN_SEATS: the other passengers' places at full size in the first
# one's eye frame (x left, z ahead) and their facing (0 ahead, 180 back): the rest of his forward-facing row (one seat,
# 0.5 m, apart), the gunners' row before it (facing back), the row behind it, back to back with it (its passengers look
# out of the cabin's side windows: facing back they would face the cut end 0.3 m off).
CABIN_SCALE = 0.6
CABIN = Model('plugin_uh60_cabin', {'left': 0.96, 'right': 0.66, 'up': 0.71, 'down': 0.81, 'ahead': 1.96, 'back': 0.81},
              {'left': 0.4, 'right': 0.3, 'up': 0.3, 'down': 0.4, 'ahead': 0.6, 'back': 0.3}, {}, scale=CABIN_SCALE,
              caps=('back',))
CABIN_SEATS: tuple[tuple[float, float, float], ...] = (
    (1.0, 0.0, 0.0), (0.5, 0.0, 0.0), (-0.5, 0.0, 0.0),
    (0.89, 1.38, 180.0), (0.23, 1.38, 180.0), (-0.47, 1.38, 180.0),
    (1.0, -0.57, 90.0), (0.5, -0.57, 90.0), (-0.5, -0.57, -90.0),
)

# kit name -> (its model, its doc): the kits cut from real models (tests/cockpit_build_test.py lays each out in the
# rooms its model accepts).
MODELS: dict[str, tuple[Model, str]] = {
    'jet_f16': (F16, "The F-16D Block 60's front cockpit (see the module's docstring)."),
    'jet_j10s': (J10, "The J-10S's front cockpit, a modern fighter's (see the module's docstring)."),
    'jet_j10s_narrow': (J10_NARROW, "The J-10S's front cockpit in a narrow canopy bump."),
    'plane_deck': (DECK, "The C-130J's flight deck from the pilot's seat."),
    'plane_deck_gun': (DECK_GUN, "The C-130J's flight deck for a gunship's pilot."),
    'plane_hold': (HOLD, "The C-130J's cargo hold round its first passenger."),
    'heli_cabin': (CABIN, "The UH-60M's troop cabin round its first passenger."),
}
KITS: dict[str, KitFn] = {name: _kit(name, model, doc) for name, (model, doc) in MODELS.items()}


def passenger(cabin: Cabin) -> Kit:
    """Another passenger in a hold another seat's kit (plane_hold, heli_cabin) draws: only his eye."""
    del cabin
    return Kit([], {})


KITS['passenger'] = passenger
