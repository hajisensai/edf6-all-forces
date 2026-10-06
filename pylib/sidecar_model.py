"""The sidecar motorcycle (边三轮摩托, tools/make_sidecar.py, src/sidecar.cpp): the stock Freed bike (V503_BIKE, class
Vehicle503_Bike) with a sidecar built from primitives in the bike's own material, read from the player's own Root.cpk
(read only). Python with numpy (pylib/procmesh.py, as the Primer models).

    build_model(game)        -> (Mdb, Rab, info)    the bike's model with the sidecar
    build(game)              -> bytes               the finished EDF6VC_SIDECAR.MRAB
    build_collision(game)    -> (bytes, info)       the bike's ragdoll (its collision) with the sidecar's floor
    check(arc, host_bones)   / check_collision(shkt)  raise SidecarCheckError on any failure
    soldier_reach(game)      -> float               how far a soldier's legs and hips reach round their feet

Frame: the bike's model frame is the vehicle's (the model is rooted at veh+0x60 as every vehicle's): +y up, +z
forward, +x the vehicle's LEFT (the stock convention: te_ik_l, the left hand's IK bone, is at +x; the drill notes:
"+X 为车左"). The sidecar is on the right: x < 0.

What is built (docs/sidecar-re.md §2), all from primitives (pylib/procmesh.py) in the bike's own material (its paint,
its dark metal and its black, each one even patch of the bike's own texture: nothing is copied in):
  * the tub (TUB): a boat-shaped body in the way of the wartime sidecars (BMW R75, Ural): a plan of a superellipse
    (TUB_HALF_WIDTH wide each side of TUB_X, a sharp nose at TUB_NOSE, a blunt tail at TUB_TAIL), straight sides down
    to a round bilge and a flat bottom TUB_BOTTOM over the ground, walls WALL thick with a rim, an inner floor at
    FLOOR_Y, a crowned deck over the nose from DECK_Z, an armoured shield plate standing on the deck's edge, a beaded
    cockpit rim, and a bench seat with a back at the tail. The gunner stands in the cockpit at GUNNER_POINT, feet on
    the floor: the sides (RIM_Y) come up to their hips, the shield to their chest;
  * its frame: two arms from the bike to the tub, a stub axle from the tub to the wheel, a stay to the mudguard;
  * the sidecar's wheel: the bike's own front tyre (its triangles on the front_tire bone) copied to WHEEL_CENTRE,
    outboard of the tub, under a mudguard. It is drawn only: the bike's physics keeps its two wheels (car_base_wheel
    names front_tire and rear_tire; a third wheel the BikeBase class would steer and drive is not something the SGO
    can add and the class's balance law would fight), and the plugin holds the outfit level instead (src/sidecar.cpp);
  * the marker bone MARKER_BONE (kind 3, no geometry) at GUNNER_POINT: what tells the plugin a sidecar bike from a
    stock Freed bike, and where its gunner stands.
Everything new is skinned to the stock `body` bone (one influence): it moves with the bike's body and needs no pose
of its own (a bone the vehicle's animation does not know is never posed on screen: docs/drill-re.md §5.4).

The collision (build_collision): the bike's chassis is the ragdoll's RagDollProxys.body compound (14 convex hulls, the
proxy frame = the model frame less the body bone's bind, BODY_PROXY). One hull, FLOOR_HULL (the rear seat's top cover,
overlapped by the hulls round it), is moved and scaled onto FLOOR, the slab of the tub's plan from its bottom up to
its inner floor; the compound's bounding tree and box follow. The hull is turned half round its long axis on the way
(x and y negated: a rotation, not a mirror): its flat bottom face becomes the slab's top, the floor the gunner stands
on. The tub's walls are drawn only (one convex hull cannot hold a hollow; a wall a soldier stands between would
squeeze their capsule, and the plugin holds them in the tub anyway: docs/sidecar-re.md §2). Nothing else in the file
changes (sizes, items, the mass distributions: the bike weighs and balances as before).
"""
from __future__ import annotations

import math
import struct
from dataclasses import replace

import numpy as np

import graft_pure as g
import procmesh as pm
from mdb import Mdb, Mesh, bind_world, cmpl_compress, cmpl_decompress, ident, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write

HOST_ARC, HOST_MDB = 'V503_BIKE.MRAB', 'v503_bike.mdb'
HOST_RAGDOLL = 'RAGDOLL_V503_BIKE.SHKT'
OUT_ARC = 'EDF6VC_SIDECAR.MRAB'
OUT_RAGDOLL = 'EDF6VC_SIDECAR_RAGDOLL.SHKT'
HOST_MATERIAL = 'v503_bike'             # the bike's body material (its paint, metal and rubber in one texture)
BODY_BONE = 'body'
TYRE_BONE = 'front_tire'
MARKER_BONE = 'edf6vc_sidecar'          # src/sidecar.cpp kMarkerBone (tools/selftest.py holds them equal)
SOLDIER_ARC, SOLDIER_MDB = 'P505_RANGER.MRAB', 'p505_ranger.mdb'   # the Ranger (soldier_reach)
SOLDIER_REACH = 0.30       # m: soldier_reach of the Ranger (0.296 measured; tools/make_sidecar.py holds it at or over)

Vec3 = tuple[float, float, float]
# The tub (model frame, m). Its plan: |x - TUB_X|^n / a^n + |z - TUB_WAIST_Z|^n / L^n = 1, n = NOSE_EXP ahead of the
# waist (a pointed nose), TAIL_EXP behind it (a blunt tail). Its inner side (x -0.54) is 0.14 m clear of the bike's
# widest point (|x| 0.396), its bottom 0.20 m over the ground (the bike's wheels stand at y 0).
TUB_X, TUB_HALF_WIDTH = -1.0, 0.46
TUB_OUT, TUB_IN = -1.46, -0.54          # TUB_X -/+ TUB_HALF_WIDTH (src/sidecar.cpp kTubOut is the outer side)
TUB_WAIST_Z, TUB_NOSE, TUB_TAIL = 0.40, 1.50, -0.60
NOSE_EXP, TAIL_EXP = 2.2, 3.5
TUB_BOTTOM = 0.20                       # the outer bottom
FLOOR_Y = 0.30                          # the inner floor: what the gunner stands on
RIM_Y = 1.18                            # the cockpit's rim: 0.88 m over the floor, a soldier's hips (koshi 0.888)
NOSE_RIM_Y = 0.82                       # the rim at the nose (it falls from DECK_Z on)
TAIL_RIM_Y, TAIL_DROP_Z = 1.10, -0.30   # the rim at the tail (it falls behind TAIL_DROP_Z): a rounded tail
DECK_Z = 0.85                           # the deck covers the tub from here to the nose
DECK_CROWN = 0.10
WALL, BILGE = 0.04, 0.22                # the walls' thickness, the outer bilge's radius
TUMBLE = 0.05                           # the sides lean in this much from the bilge's top to the rim
KEEL_Z, KEEL_RISE = 0.95, 0.12          # the bottom rises KEEL_RISE from KEEL_Z to the nose (a boat's forefoot)
SHIELD_TOP = 1.48                       # the shield plate's top: a soldier's chest (mune 1.26 over the floor)
TUB: tuple[Vec3, Vec3] = ((TUB_OUT, TUB_BOTTOM, TUB_TAIL), (TUB_IN, SHIELD_TOP, TUB_NOSE))
# The collision slab (FLOOR_HULL): the tub's plan, from its bottom up to its floor.
FLOOR: tuple[Vec3, Vec3] = ((TUB_OUT, TUB_BOTTOM, TUB_TAIL), (TUB_IN, FLOOR_Y, TUB_NOSE))
# Where the gunner stands: on the floor in the cockpit, between the seat and the deck (src/sidecar.cpp kGunnerX/Y/Z).
GUNNER_POINT: Vec3 = (TUB_X, FLOOR_Y, 0.35)
# The bench seat (cushion, back) at the tail.
SEAT_FRONT, SEAT_TOP = -0.06, 0.66
# The sidecar wheel: its hub outboard of the tub (the tyre TYRE_HALF_WIDTH each side of it, WHEEL_GAP clear of the
# tub's side), on the ground as the bike's (TYRE_RADIUS is the front tyre's: the hub at that height puts its bottom
# on y 0); the mudguard over it MUDGUARD_GAP clear of the tyre.
TYRE_RADIUS, TYRE_HALF_WIDTH = 0.393, 0.153
WHEEL_GAP, MUDGUARD_GAP = 0.06, 0.06
WHEEL_CENTRE: Vec3 = (-1.673, TYRE_RADIUS, 0.30)
# The ragdoll's body proxy sits at the body bone's bind position (RagDollProxys.body, no rotation).
BODY_PROXY: Vec3 = (0.0, 0.72, 0.669)
FLOOR_HULL = 7             # the body compound's instance turned into the floor slab (the rear seat cover)
# The colours each part takes from the bike's texture (procmesh: the even patch nearest it).
PAINT, METAL, RUBBER = (30, 48, 118), (120, 125, 135), (22, 22, 25)   # navy, blue-grey steel, black


class SidecarCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O."""
    if not ok:
        raise SidecarCheckError(msg)


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{len(hits)} members {name}')
    return hits[0]


# ------------------------------------------------------------------------------------------ the tub's shape

def rim_y(z: float) -> float:
    """The rim's height at `z`: RIM_Y along the cockpit, falling to NOSE_RIM_Y at the nose under the deck and
    (eased) to TAIL_RIM_Y at the tail."""
    if z > DECK_Z:
        return RIM_Y + (NOSE_RIM_Y - RIM_Y) * min(1.0, (z - DECK_Z) / (TUB_NOSE - DECK_Z))
    if z < TAIL_DROP_Z:
        t = min(1.0, (TAIL_DROP_Z - z) / (TAIL_DROP_Z - TUB_TAIL))
        return RIM_Y + (TAIL_RIM_Y - RIM_Y) * (1.0 - math.cos(math.pi * t)) / 2.0
    return RIM_Y


def bottom_y(z: float, inner: bool = False) -> float:
    """The outer bottom's height at `z` (TUB_BOTTOM, rising toward the nose from KEEL_Z), or the inner floor's
    (FLOOR_Y, as far over the outer bottom where that rises)."""
    rise = KEEL_RISE * max(0.0, (z - KEEL_Z) / (TUB_NOSE - KEEL_Z)) ** 2
    return max(FLOOR_Y, TUB_BOTTOM + rise + (FLOOR_Y - TUB_BOTTOM)) if inner else TUB_BOTTOM + rise


def _plan(front: bool, inset: float) -> tuple[float, float, float]:
    """(half width, half length, exponent) of the plan's front or back half, `inset` in from the outer side."""
    if front:
        return TUB_HALF_WIDTH - inset, TUB_NOSE - TUB_WAIST_Z - inset, NOSE_EXP
    return TUB_HALF_WIDTH - inset, TUB_WAIST_Z - TUB_TAIL - inset, TAIL_EXP


def outline(theta: float, inset: float = 0.0) -> tuple[float, float]:
    """(x, z) of the plan `inset` in from the outer side at angle `theta` (0: the outer side, x < TUB_X... going
    round through the nose)."""
    c, s = math.cos(theta), math.sin(theta)
    a, L, n = _plan(s >= 0.0, inset)
    return (TUB_X - a * math.copysign(abs(c) ** (2.0 / n), c), TUB_WAIST_Z + L * math.copysign(abs(s) ** (2.0 / n), s))


def half_width(z: float, inset: float = 0.0) -> float:
    """The plan's half width at `z`, `inset` in from the outer side (0 past its ends)."""
    a, L, n = _plan(z >= TUB_WAIST_Z, inset)
    v = abs(z - TUB_WAIST_Z) / L
    return a * (1.0 - v ** n) ** (1.0 / n) if v < 1.0 else 0.0


# ------------------------------------------------------------------------------------------ primitives

def _flip_outward(part: pm.Part, start: int, ref, outward: bool = True) -> None:  # noqa: ANN001 - point -> point
    """Turn the triangles added since `start` so that most face away from (outward) or toward `ref(centroid)`: the
    winding the stock meshes use (cross(b - a, c - a) along the face's normal, out of the solid)."""
    P = np.array(part.pos)
    T = np.array(part.tris[start:], dtype=np.int64)
    if not len(T):
        return
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    n = np.cross(b - a, c - a)
    cen = (a + b + c) / 3.0
    away = cen - np.array([ref(x) for x in cen])
    score = float(np.sign(np.einsum('ij,ij->i', n, away)).sum())
    if (score < 0) == outward:
        part.tris[start:] = [(t[0], t[2], t[1]) for t in part.tris[start:]]


def _facing(part: pm.Part, tris: list[int], want: np.ndarray) -> None:
    """Turn each of the triangles `tris` (indices into part.tris) so that its normal goes along `want`."""
    for i in tris:
        a, b, c = (np.asarray(part.pos[k]) for k in part.tris[i])
        if float(np.dot(np.cross(b - a, c - a), want)) < 0.0:
            t = part.tris[i]
            part.tris[i] = (t[0], t[2], t[1])


def _cap_ends(part: pm.Part, start: int, centres: tuple[int, int], ends: tuple[np.ndarray, np.ndarray]) -> None:
    """The end caps among the triangles since `start` (those fanned round the cap centres `centres`) turned to face
    out of the ends: along `ends` (the way out of the first end, the way out of the last)."""
    for m, want in zip(centres, ends):
        _facing(part, [i for i in range(start, len(part.tris)) if m in part.tris[i]], want)


def _axis_ref(c: np.ndarray) -> np.ndarray:
    """The tub's inside below a point: its centre line at the point's height, inside its ends."""
    return np.array([TUB_X, c[1], min(max(c[2], TUB_TAIL + 0.45), TUB_NOSE - 0.45)])


def _path_ref(path: list[Vec3]):  # noqa: ANN202
    """The nearest point of the polyline `path`."""
    pts = [np.array(p, dtype=np.float64) for p in path]

    def ref(c: np.ndarray) -> np.ndarray:
        best, bd = pts[0], math.inf
        for a, b in zip(pts, pts[1:]):
            d = b - a
            t = min(1.0, max(0.0, float(np.dot(c - a, d) / max(np.dot(d, d), 1e-12))))
            q = a + d * t
            dd = float(np.dot(c - q, c - q))
            if dd < bd:
                best, bd = q, dd
        return best
    return ref


def hexahedron(part: pm.Part, corners: list[Vec3], skin: pm.Skin) -> None:
    """A six-faced solid of 8 corners (0-3 the bottom ring, 4-7 the top ring over them), each face flat (its own 4
    vertices), wound out of the solid."""
    C = [np.array(p, dtype=np.float64) for p in corners]
    centre = sum(C) / 8.0
    for f in ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        start = len(part.tris)
        q = [part.add(C[i], skin, uv) for i, uv in zip(f, ((0, 0), (1, 0), (1, 1), (0, 1)))]
        part.tris += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
        _flip_outward(part, start, lambda _c: centre)


def tube(part: pm.Part, path: list[Vec3], radius: float, skin: pm.Skin, segs: int = 10) -> None:
    """procmesh.tube (capped) along `path`, wound out of it."""
    start = len(part.tris)
    pm.tube(part, path, [radius] * len(path), [skin] * len(path), segs)
    _flip_outward(part, start, _path_ref(path))
    P = [np.array(p, dtype=np.float64) for p in path]
    _cap_ends(part, start, (len(part.pos) - 2, len(part.pos) - 1), (P[0] - P[1], P[-1] - P[-2]))


# ------------------------------------------------------------------------------------------ the sidecar's parts

RING = 72          # points round the plan
DECK_ROWS, DECK_COLS = 12, 15


def _shell(part: pm.Part, skin: pm.Skin, inner: bool) -> list[int]:
    """The tub's outer shell (rim, sides leaning in TUMBLE toward it, round bilge, bottom) or its inner one (WALL in,
    down to the floor); returns the rim ring's vertex indices. A level is (inset, height over the bottom or None
    for the rim): the heights of the bottom and the rim follow z (bottom_y, rim_y)."""
    base, radius = (WALL, BILGE - WALL) if inner else (0.0, BILGE)
    levels: list[tuple[float, float | None]] = [(base + TUMBLE, None)]
    levels += [(base + TUMBLE * (1.0 - f) ** 2, None if f is None else f) for f in (0.35, 0.7)]   # f: way down the side
    levels += [(base, -1.0)]                                                                    # the bilge's top
    levels += [(base + radius * (1.0 - math.cos(math.radians(a))), radius * (1.0 - math.sin(math.radians(a))))
               for a in (18.0, 36.0, 54.0, 72.0, 90.0)]
    thetas = [2.0 * math.pi * k / RING for k in range(RING)]
    rows = []
    start = len(part.tris)
    for j, (inset, y) in enumerate(levels):
        row = []
        for k, th in enumerate(thetas):
            x, z = outline(th, inset)
            z0 = outline(th)[1]
            top, low = rim_y(z0), bottom_y(z0, inner)
            if j == 0:
                yy = top
            elif 0 < j < 3:          # down the side, from the rim to the bilge's top
                yy = top + (low + radius - top) * float(y)
            elif y == -1.0:
                yy = low + radius
            else:
                yy = low + float(y)
            row.append(part.add((x, yy, z), skin, (k / RING, j / len(levels))))
        rows.append(row)
    pm.grid(part, rows, True)
    _flip_outward(part, start, _axis_ref, outward=not inner)
    centre = part.add((TUB_X, bottom_y(TUB_WAIST_Z, inner), TUB_WAIST_Z), skin, (0.5, 1.0))
    last = rows[-1]
    cap = len(part.tris)
    part.tris += [(centre, last[i], last[(i + 1) % RING]) for i in range(RING)]
    _facing(part, list(range(cap, len(part.tris))), np.array([0.0, 1.0 if inner else -1.0, 0.0]))   # floor up, bottom down
    return rows[0]


def tub_body(skin: pm.Skin) -> pm.Part:
    """The tub's shells joined at the rim, and the deck over the nose (its top, its underside and its front edge)."""
    part = pm.Part(0, PAINT)
    outer = _shell(part, skin, False)
    inner = _shell(part, skin, True)
    start = len(part.tris)
    for i in range(RING):
        j = (i + 1) % RING
        part.tris += [(outer[i], inner[i], inner[j]), (outer[i], inner[j], outer[j])]
    _flip_outward(part, start, lambda c: c - np.array([0.0, 1.0, 0.0]))     # the rim faces up
    # The deck: rows across the nose from DECK_Z, crowned, just over the rim and over the outer side.
    zs = [DECK_Z + (TUB_NOSE - 0.004 - DECK_Z) * k / (DECK_ROWS - 1) for k in range(DECK_ROWS)]
    for under in (False, True):
        rows = []
        for r, z in enumerate(zs):
            w = half_width(z, TUMBLE)
            row = []
            for c in range(DECK_COLS):
                t = -1.0 + 2.0 * c / (DECK_COLS - 1)
                y = rim_y(z) + 0.005 + DECK_CROWN * (1.0 - t * t) * (1.0 - 0.5 * (z - DECK_Z) / (TUB_NOSE - DECK_Z))
                # The underside meets the top at the sides (on the rim): the deck is closed but at its front edge.
                row.append(part.add((TUB_X + t * w, y - (0.025 * (1.0 - t ** 4) if under else 0.0), z), skin,
                                    (c / DECK_COLS, r / DECK_ROWS)))
            rows.append(row)
        start = len(part.tris)
        pm.grid(part, rows, False)
        _flip_outward(part, start, lambda c: c - np.array([0.0, 1.0, 0.0]), outward=not under)
        if not under:
            top_front = rows[0]
        else:
            start = len(part.tris)
            for c in range(DECK_COLS - 1):
                part.tris += [(top_front[c], rows[0][c], rows[0][c + 1]), (top_front[c], rows[0][c + 1], top_front[c + 1])]
            _flip_outward(part, start, lambda c: c + np.array([0.0, 0.0, 1.0]))   # the edge faces the cockpit
    return part


def shield(skin: pm.Skin) -> pm.Part:
    """The armoured plate on the deck's front edge, leaning back toward the gunner."""
    part = pm.Part(0, METAL)
    z0, z1, t = DECK_Z + 0.02, DECK_Z - 0.10, 0.025
    y0 = rim_y(DECK_Z) - 0.03
    w0, w1 = half_width(DECK_Z, TUMBLE) - 0.05, 0.30
    hexahedron(part, [(TUB_X - w0, y0, z0), (TUB_X + w0, y0, z0), (TUB_X + w0, y0, z0 + t), (TUB_X - w0, y0, z0 + t),
                      (TUB_X - w1, SHIELD_TOP, z1), (TUB_X + w1, SHIELD_TOP, z1), (TUB_X + w1, SHIELD_TOP, z1 + t),
                      (TUB_X - w1, SHIELD_TOP, z1 + t)], skin)
    return part


def seat(skin: pm.Skin) -> pm.Part:
    """The bench at the tail: its cushion on the floor and its back leaning on the tail's wall."""
    part = pm.Part(0, RUBBER)
    w = 0.26
    back = TUB_TAIL + WALL + 0.10      # the cushion's back: clear of the tail's wall at the cushion's corners
    hexahedron(part, [(TUB_X - w, FLOOR_Y, back), (TUB_X + w, FLOOR_Y, back), (TUB_X + w, FLOOR_Y, SEAT_FRONT),
                      (TUB_X - w, FLOOR_Y, SEAT_FRONT), (TUB_X - w, SEAT_TOP, back), (TUB_X + w, SEAT_TOP, back),
                      (TUB_X + w, SEAT_TOP, SEAT_FRONT), (TUB_X - w, SEAT_TOP, SEAT_FRONT)], skin)
    b = 0.22
    top = rim_y(back - 0.05) - 0.05    # under the rim where the tail falls
    hexahedron(part, [(TUB_X - b, SEAT_TOP, back), (TUB_X + b, SEAT_TOP, back), (TUB_X + b, SEAT_TOP, back + 0.08),
                      (TUB_X - b, SEAT_TOP, back + 0.08), (TUB_X - b, top, back - 0.05),
                      (TUB_X + b, top, back - 0.05), (TUB_X + b, top, back + 0.03), (TUB_X - b, top, back + 0.03)], skin)
    return part


def rim_bead(skin: pm.Skin) -> pm.Part:
    """A rubber bead along the cockpit's rim, from the deck's edge on one side round the tail to the other."""
    part = pm.Part(0, RUBBER)
    path = []
    for k in range(RING):
        th = 2.0 * math.pi * k / RING
        x, z = outline(th, TUMBLE + 0.022)
        if z <= DECK_Z:
            path.append((x, rim_y(outline(th)[1]) + 0.01, z))
    # outline() runs from the outer side through the nose, the inner side and the tail: start the path after the gap.
    gap = max(range(len(path) - 1), key=lambda i: abs(path[i + 1][2] - path[i][2]) + abs(path[i + 1][0] - path[i][0]))
    path = path[gap + 1:] + path[:gap + 1]
    tube(part, path, 0.022, skin, 8)
    return part


def wheel_mount(skin: pm.Skin) -> pm.Part:
    """The frame: two arms from the bike to the tub's inner side, the stub axle from the tub's outer side to the hub,
    the mudguard's stay."""
    part = pm.Part(0, METAL)
    wx, wy, wz = WHEEL_CENTRE
    for z, y_bike, y_tub in ((1.25, 0.52, 0.50), (-0.40, 0.72, 0.52)):
        tube(part, [(-0.22, y_bike, z), (TUB_X + half_width(z) - 0.02, y_tub, z)], 0.035, skin)
    tube(part, [(TUB_X - half_width(wz) + 0.02, wy, wz), (wx, wy, wz)], 0.04, skin)
    r = TYRE_RADIUS + MUDGUARD_GAP
    tube(part, [(TUB_X - half_width(wz) + 0.02, wy + r + 0.02, wz), (wx + TYRE_HALF_WIDTH + 0.03, wy + r + 0.02, wz)], 0.025, skin)
    return part


def mudguard(skin: pm.Skin) -> pm.Part:
    """A mudguard over the sidecar's wheel: a crowned steel strip MUDGUARD_GAP over the tyre, from just ahead of the
    hub over the top to behind it."""
    part = pm.Part(0, PAINT)
    wx, wy, wz = WHEEL_CENTRE
    r, t, hw = TYRE_RADIUS + MUDGUARD_GAP, 0.018, TYRE_HALF_WIDTH + 0.03
    section = [(-hw, 0.0), (-hw, t), (0.0, t + 0.025), (hw, t), (hw, 0.0), (0.0, 0.025)]   # (across, out from r)
    rows, centres = [], []
    steps = 24
    start = len(part.tris)
    for k in range(steps + 1):
        phi = math.radians(-12.0 + 204.0 * k / steps)       # 0: ahead of the hub, 90: over it
        cz, cy = math.cos(phi), math.sin(phi)
        rows.append([part.add((wx + dx, wy + (r + dr) * cy, wz + (r + dr) * cz), skin, (i / len(section), k / steps))
                     for i, (dx, dr) in enumerate(section)])
        centres.append((wx, wy + (r + t / 2) * cy, wz + (r + t / 2) * cz))
    pm.grid(part, rows, True)
    _flip_outward(part, start, _path_ref(centres))
    for row, c in ((rows[0], centres[0]), (rows[-1], centres[-1])):
        m = part.add(c, skin, (0.5, 0.5))
        part.tris += [(m, row[i], row[(i + 1) % len(row)]) for i in range(len(row))]
    C = [np.array(c) for c in centres]
    _cap_ends(part, start, (len(part.pos) - 2, len(part.pos) - 1), (C[0] - C[1], C[-1] - C[-2]))
    return part


def sidecar_parts(body: int) -> dict[str, pm.Part]:
    """Every new part, by name, skinned wholly to `body`."""
    skin = [(body, 1.0)]
    return {'tub': tub_body(skin), 'shield': shield(skin), 'seat': seat(skin), 'rim bead': rim_bead(skin),
            'frame': wheel_mount(skin), 'mudguard': mudguard(skin)}


# ------------------------------------------------------------------------------------------ the model

def insert_marker(md: Mdb) -> tuple[Mdb, int]:
    """`md` with MARKER_BONE (kind 3, bounded, no geometry) under the body bone at GUNNER_POINT, placed at the end of
    the body's subtree (preorder kept): every later bone, parent link and the object's bone shifted. Every skinned
    vertex is on a bone before it (checked)."""
    pi = md.bone_index(BODY_BONE)
    _req(pi >= 0, f'no bone {BODY_BONE}')
    at = max(g.subtree(md, pi)) + 1
    for o in md.objects:
        for me in o.meshes:
            if me.flags[1]:
                bi, _bw = g.skin_columns(me)
                _req(all(int(x) < at for i4 in bi for x in i4), 'a skinned vertex on a bone the insertion moves')
    shift = lambda i: i + 1 if i >= at else i  # noqa: E731
    names = list(md.names)
    name = g._name_index(names, MARKER_BONE)
    w = bind_world(md)
    world = g.translation(GUNNER_POINT)
    bones = [replace(b, index=shift(b.index), parent=shift(b.parent) if b.parent >= 0 else -1) for b in md.bones]
    from mdb import Bone
    from mdb_jet import link
    bones.insert(at, Bone(at, pi, -1, -1, name, 0, 3, 0, 1, 0, 0, mmul(world, inverse_affine(w[pi])),
                          g.translation((-GUNNER_POINT[0], -GUNNER_POINT[1], -GUNNER_POINT[2])),
                          [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    link(bones)
    objects = [replace(o, bone=shift(o.bone)) for o in md.objects]
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None), at


def box_map(lo: Vec3, hi: Vec3, to: tuple[Vec3, Vec3]) -> tuple[Vec3, Vec3]:
    """(scale, offset) per axis taking the box lo..hi onto `to`: p -> p * scale + offset."""
    s = tuple((to[1][c] - to[0][c]) / (hi[c] - lo[c]) for c in range(3))
    o = tuple(to[0][c] - lo[c] * s[c] for c in range(3))
    return s, o  # type: ignore[return-value]


def part_meshes(host: Mdb, albedos: dict, parts: list[pm.Part]) -> list[Mesh]:
    """The parts as meshes of the host's HOST_MATERIAL in its own vertex layout (its first mesh of that material:
    position, frames, two UV channels, one bone), one mesh per colour; each colour's UVs in the even patch of the
    bike's texture nearest it (procmesh.uv_boxes), the occlusion channel at the same spot."""
    mat = next((m.index for m in host.materials if host.name_of(m.name) == HOST_MATERIAL), -1)
    _req(mat >= 0, f'{HOST_MDB}: no material {HOST_MATERIAL}')
    tmpl = next(me for o in host.objects for me in o.meshes if me.material == mat)
    _req(tmpl.flags[1] == 1, f'{HOST_MDB}: {HOST_MATERIAL} is not skinned')
    by_tint: dict[tuple[int, int, int], pm.Part] = {}
    for p in parts:
        m = by_tint.setdefault(p.tint, pm.Part(mat, p.tint))
        base = len(m.pos)
        m.pos += p.pos
        m.skin += p.skin
        m.uv += p.uv
        m.tris += [(a + base, b + base, c + base) for a, b, c in p.tris]
    out = []
    for tint, p in by_tint.items():
        _req(len(p.pos) < 65536, f'{len(p.pos)} vertices in one mesh')
        vdata, idx = pm.pack(p, tmpl, pm.uv_boxes(host, albedos, tint), 1.0)
        out.append(Mesh(tmpl.flags, mat, tmpl.unk08, tmpl.vsize, tmpl.elems, 0, vdata, idx))
    return out


def wheel_meshes(host: Mdb, body: int) -> list[tuple[int, Mesh]]:
    """The bike's front tyre (its triangles wholly on the front_tire bone) re-skinned to `body` and moved so that its
    hub, the front_tire bone's bind position, is at WHEEL_CENTRE (no turn: its axle stays across the bike)."""
    tyre = host.bone_index(TYRE_BONE)
    hub = bind_world(host)[tyre][12:15]
    _req(abs(hub[1] - TYRE_RADIUS) < 0.01, f'{TYRE_BONE} hub at {hub}: TYRE_RADIUS is {TYRE_RADIUS}')
    off = (WHEEL_CENTRE[0] - hub[0], WHEEL_CENTRE[1] - hub[1], WHEEL_CENTRE[2] - hub[2])
    got = g.extract_meshes(host, lambda _o, _m, _me: True, {tyre: body}, 1.0, off)
    _req(bool(got), f'nothing on {TYRE_BONE}')
    return got


def build_model(game) -> tuple[Mdb, object, dict]:  # noqa: ANN001 - rootcpk.Game
    """(model, host Rab, info)."""
    rab = rab_read(game.read('OBJECT', HOST_ARC))
    host = mdb_read(member(rab, HOST_MDB).data)
    _req(len(host.objects) == 1, f'{HOST_MDB}: {len(host.objects)} objects')
    wheel = wheel_meshes(host, host.bone_index(BODY_BONE))
    md, marker = insert_marker(host)
    body = md.bone_index(BODY_BONE)
    parts = sidecar_parts(body)
    new = part_meshes(md, pm.albedos(rab, md), list(parts.values()))
    first = len(md.objects[0].meshes)
    md = g.append_meshes(md, [(me.material, me) for me in new], {me.material: me.material for me in new}, obj=0)
    md = g.append_meshes(md, wheel, {m: m for m, _me in wheel}, obj=0)
    o = md.objects[0]
    md = replace(md, objects=[replace(o, meshes=o.meshes[:first] + [replace(me, mesh_index=first + k)
                                                                   for k, me in enumerate(o.meshes[first:])])])
    md = g.recompute_bounds(md, {body})
    boxes = {}
    for name, p in parts.items():
        P = np.array(p.pos)
        boxes[name] = (P.min(axis=0).round(3).tolist(), P.max(axis=0).round(3).tolist())
    info = {'marker bone': marker, 'meshes': len(md.objects[0].meshes), 'part meshes': len(new),
            'wheel meshes': len(wheel), 'triangles': sum(len(p.tris) for p in parts.values()), 'parts': boxes}
    return md, rab, info


def build_with_info(game) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    md, rab, info = build_model(game)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(rab, HOST_MDB).stored = stored
    return rab_write(rab), md, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_SIDECAR.MRAB."""
    return build_with_info(game)[0]


def stock_bones(game) -> list[str]:  # noqa: ANN001 - rootcpk.Game
    md = mdb_read(member(rab_read(game.read('OBJECT', HOST_ARC)), HOST_MDB).data)
    return [md.name_of(b.name) for b in md.bones]


def soldier_reach(game) -> float:  # noqa: ANN001 - rootcpk.Game
    """How far round a standing soldier's feet (their origin) their legs and hips reach under the rim: the largest
    horizontal distance from their vertical axis of the Ranger's vertices lower than the rim is over the floor (bind
    pose). The tub's walls must stand at least this far from GUNNER_POINT (check: walls)."""
    md = mdb_read(member(rab_read(game.read('OBJECT', SOLDIER_ARC)), SOLDIER_MDB).data)
    top = RIM_Y - FLOOR_Y
    reach = 0.0
    for o in md.objects:
        for me in o.meshes:
            for x, y, z in g.mesh_positions(me):
                if y < top:
                    reach = max(reach, math.hypot(x, z))
    _req(reach > 0.0, f'{SOLDIER_MDB}: nothing under {top} m')
    return reach


# ------------------------------------------------------------------------------------------ the model's checks

def _triangles(md: Mdb, meshes: list[Mesh]) -> np.ndarray:
    """(M, 3, 3) the meshes' triangles in the model frame (skinned meshes are stored in it)."""
    out = []
    for me in meshes:
        P = np.array(g.mesh_positions(me), dtype=np.float64)
        T = np.frombuffer(me.indices, dtype='<u2').reshape(-1, 3).astype(np.int64)
        out.append(P[T])
    return np.concatenate(out) if out else np.zeros((0, 3, 3))


def _ray_hits(tris: np.ndarray, o: np.ndarray, d: np.ndarray) -> np.ndarray:
    """Distances along the ray o + t d (t > 0) at which it crosses each triangle (inf where it misses)."""
    e1, e2 = tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    p = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = o - tris[:, 0]
    u = np.einsum('ij,ij->i', s, p) * inv
    q = np.cross(s, e1)
    v = (q @ d) * inv
    t = np.einsum('ij,ij->i', e2, q) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-6)
    return np.where(hit, t, np.inf)


def _outward_share(tris: np.ndarray, ref) -> float:  # noqa: ANN001 - point -> point
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    cen = tris.mean(axis=1)
    away = cen - np.array([ref(c) for c in cen])
    return float((np.einsum('ij,ij->i', n, away) > 0).mean())


def parts_triangles(parts: list[pm.Part]) -> np.ndarray:
    """(M, 3, 3) the parts' triangles (sidecar_parts: the model frame), as the built model holds them."""
    return np.concatenate([np.array(p.pos)[np.array(p.tris, dtype=np.int64)] for p in parts])


def check_tub(T: np.ndarray, reach: float | None = None) -> dict:
    """The tub as the gunner meets it, over the sidecar's triangles T (M, 3, 3; model frame): a floor (an upward face)
    at FLOOR_Y straight under GUNNER_POINT; from the point, at six heights from the floor up to the lowest rim
    (TAIL_RIM_Y), a ray in each of 24 ways across meets the sidecar (walled all round), none nearer than `reach`
    (soldier_reach: legs and hips clear of the walls); the sides beside the point up to RIM_Y; every face a ray meets
    first, from the point out or from 3 m round the tub in, is wound to face it (the side drawn). Returns what it
    measured."""
    # The floor under the point: the nearest face straight down from just over the point is an upward face at FLOOR_Y.
    g0 = np.array(GUNNER_POINT, dtype=np.float64)
    down = _ray_hits(T, g0 + [0.0, 0.05, 0.0], np.array([0.0, -1.0, 0.0]))
    k = int(np.argmin(down))
    _req(math.isfinite(down[k]) and abs(down[k] - 0.05) < 2e-3, f'no floor under the gunner ({down[k]:.3f} m down)')
    fn = np.cross(T[k, 1] - T[k, 0], T[k, 2] - T[k, 0])
    _req(fn[1] / np.linalg.norm(fn) > 0.99, f'the face under the gunner is not an upward floor: {fn}')
    # Walled round: rays across from the point at heights up to the rim.
    # Every face a ray meets first faces the ray (the side shown: wound out of the solid, as the stock meshes):
    # from the gunner out to the walls, and from all round the tub in toward it.
    FN = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    nearest, walled, lowest_side, facing = math.inf, 0, math.inf, [0, 0]

    def seen(o: np.ndarray, d: np.ndarray) -> float:
        hits = _ray_hits(T, o, d)
        k = int(np.argmin(hits))
        if math.isfinite(hits[k]):
            facing[0] += 1
            facing[1] += int(float(np.dot(FN[k], d)) < 0.0)
        return float(hits[k])
    for h in np.linspace(0.05, TAIL_RIM_Y - FLOOR_Y - 0.03, 6):   # all round up to the lowest rim (the tail's)
        for a in np.linspace(0.0, 2.0 * math.pi, 24, endpoint=False):
            d = np.array([math.cos(a), 0.0, math.sin(a)])
            t = seen(g0 + [0.0, h, 0.0], d)
            _req(math.isfinite(t), f'open at {h:.2f} m over the floor toward {math.degrees(a):.0f} deg')
            nearest = min(nearest, t)
            walled += 1
            if h > TUB_BOTTOM:     # from outside, 3 m off, in toward the tub's centre line
                seen(np.array([TUB_X, h + FLOOR_Y, TUB_WAIST_Z]) - 3.0 * d, d)
    side = half_width(GUNNER_POINT[2], TUMBLE + WALL / 2)
    for x in (TUB_X - side, TUB_X + side):     # the sides beside the gunner: up to the rim
        t = float(_ray_hits(T, np.array([x, SHIELD_TOP + 0.5, GUNNER_POINT[2]]), np.array([0.0, -1.0, 0.0])).min())
        lowest_side = min(lowest_side, SHIELD_TOP + 0.5 - t)
    _req(lowest_side > RIM_Y - 0.03, f'the side beside the gunner tops out at {lowest_side:.3f}, the rim is {RIM_Y}')
    _req(facing[0] > 200 and facing[1] == facing[0], f'{facing[0] - facing[1]} of {facing[0]} faces seen from behind')
    if reach is not None:
        _req(nearest >= reach, f'a wall {nearest:.3f} m from the gunner, their legs and hips reach {reach:.3f}')
    return {'floor y': round(float(g0[1] + 0.05 - down[k]), 4), 'nearest wall': round(nearest, 3), 'rays walled': walled,
            'side top': round(lowest_side, 3), 'faces seen': facing[0], 'faces seen from behind': facing[0] - facing[1]}


def check(arc: bytes, host_bones: list[str] | None = None, reach: float | None = None) -> dict:
    """The archive opens and its model round-trips; every skin bone's bind x inverse bind is identity; the stock bones keep
    their names and order with MARKER_BONE (no geometry) at GUNNER_POINT right after the body's subtree; every new
    vertex is on the body bone alone and in the bike's own material (no texture added). The tub: its vertices fill
    TUB, clear of the bike; a floor (upward faces) at FLOOR_Y under the gunner's point; round the gunner, from the
    floor up to the rim, every way across is walled (a ray from the point hits the sidecar), no wall nearer than
    `reach` (soldier_reach: their legs and hips do not go through it) and the sides no lower than RIM_Y; its outer
    shell wound outward. The wheel's vertices sit round WHEEL_CENTRE within the tyre's radius, its lowest on the
    ground (y 0, as the bike's own wheels), clear of the tub, under the mudguard. Returns what it measured."""
    rab = rab_read(arc)
    md = mdb_read(member(rab, HOST_MDB).data)
    _req(mdb_write(md) == cmpl_decompress(member(rab, HOST_MDB).data), 'the model does not round-trip')
    w = bind_world(md)
    for b in md.bones:
        if b.kind != 3:
            continue   # the stock IK and object bones' inverse binds are not their binds' (te_ik_l: 1.37 off)
        err = max(abs(x - y) for x, y in zip(mmul(w[b.index], b.inv_bind), ident()))
        _req(err < 1e-4, f'{md.name_of(b.name)}: bind x inverse bind off by {err}')
    names = [md.name_of(b.name) for b in md.bones]
    m = md.bone_index(MARKER_BONE)
    body = md.bone_index(BODY_BONE)
    _req(m > 0 and md.bones[m].parent == body and md.bones[m].kind == 3, f'{MARKER_BONE}: {m}')
    _req(m == max(g.subtree(md, body)) and m - 1 in g.subtree(md, body), f'{MARKER_BONE} not at the end of the body subtree')
    if host_bones is not None:
        _req(names[:m] + names[m + 1:] == host_bones, f'stock bones changed: {names}')
    _req(max(abs(a - b) for a, b in zip(w[m][12:15], GUNNER_POINT)) < 1e-4, f'{MARKER_BONE} at {w[m][12:15]}')
    pts = g.skinned_points(md)
    _req(m not in pts, f'vertices on {MARKER_BONE}')
    o = md.objects[0]
    have = {f.name.lower() for f in rab.files}
    for t in md.textures:
        _req(t.filename.lower() in have, f'texture {t.filename} missing from the archive')
    # The new meshes: everything on the body bone wholly inside the sidecar's side (x under the bike's widest point
    # less the arms' bike end) is the sidecar's.
    new, bike_right = [], 0.0
    for me in o.meshes:
        P = g.mesh_positions(me)
        if not (me.flags[1] and P and max(p[0] for p in P) < -0.18):
            bike_right = min([bike_right] + [p[0] for p in P])
        else:
            bi, bw = g.skin_columns(me)
            _req(all(int(i[0]) == body and abs(x[0] - 1.0) < 1e-6 for i, x in zip(bi, bw)), 'a sidecar vertex off the body bone')
            new.append(me)
    _req(bool(new), 'no sidecar meshes')
    T = _triangles(md, new)
    allp = T.reshape(-1, 3)
    tub = allp[(allp[:, 0] >= TUB_OUT - 1e-3) & (allp[:, 0] <= TUB_IN + 1e-3)]
    lo, hi = tub.min(axis=0), tub.max(axis=0)
    _req(all(abs(lo[c] - TUB[0][c]) < 3e-3 and abs(hi[c] - TUB[1][c]) < 3e-3 for c in range(3)), f'the tub at {lo}..{hi}, TUB is {TUB}')
    _req(bike_right - hi[0] > 0.1, f'the tub {bike_right - hi[0]:.3f} m off the bike (its right side at x {bike_right:.3f})')
    tub_measured = check_tub(T, reach)
    # The wheel: the vertices outboard of the tub round the hub within the tyre's reach are the tyre's (and the axle).
    hub = np.array(WHEEL_CENTRE)
    out = allp[allp[:, 0] < TUB_OUT - 1e-3]
    rr = np.hypot(out[:, 1] - hub[1], out[:, 2] - hub[2])
    wheel = out[(rr <= TYRE_RADIUS + 0.02) & (np.abs(out[:, 0] - hub[0]) <= TYRE_HALF_WIDTH + 1e-3)]
    _req(len(wheel) > 50, f'{len(wheel)} wheel vertices')
    wr = float(np.hypot(wheel[:, 1] - hub[1], wheel[:, 2] - hub[2]).max())
    _req(abs(wr - TYRE_RADIUS) < 0.02, f'the wheel reaches {wr:.3f} m from its hub, the tyre {TYRE_RADIUS}')
    low = float(wheel[:, 1].min())
    _req(abs(low) < 0.02, f'the wheel stands at y {low:.3f}, the ground is 0')
    inner_x = float(wheel[:, 0].max())
    _req(inner_x < TUB_OUT - 0.03, f'the wheel reaches x {inner_x:.3f}, into the tub (its side {TUB_OUT})')
    guard = out[(rr > wr + 0.005) & (out[:, 1] > hub[1]) & (np.abs(out[:, 0] - hub[0]) <= TYRE_HALF_WIDTH + 0.04)]
    _req(len(guard) > 50 and float(np.hypot(guard[:, 1] - hub[1], guard[:, 2] - hub[2]).min()) >= TYRE_RADIUS + 0.03,
         'no mudguard clear over the wheel')
    return {'bones': len(md.bones), 'marker': m, 'sidecar meshes': len(new), 'triangles': len(T),
            'clear of the bike': round(bike_right - float(hi[0]), 3),
            'tub': [lo.round(3).tolist(), hi.round(3).tolist()], **tub_measured, 'wheel vertices': len(wheel), 'wheel radius': round(wr, 4),
            'wheel low': round(low, 4), 'wheel inner x': round(inner_x, 3),
            'mudguard over tyre': round(float(np.hypot(guard[:, 1] - hub[1], guard[:, 2] - hub[2]).min()) - TYRE_RADIUS, 3)}


# ------------------------------------------------------------------------------------------ the collision

class _Shkt:
    """The bike's ragdoll tagfile, the parts this edit reads and writes (offsets from pylib/hktag.py's type table)."""

    def __init__(self, data: bytes) -> None:
        from hkcms import bodies
        from hktag import Tag
        self.buf = bytearray(data)
        self.tag = t = Tag(bytes(data))
        self.body = bodies(t)['RagDollProxys.body']
        C = 'hknpCompoundShape'
        typ, self.at, _ = t.item(self.body)
        _req(typ == C, f'RagDollProxys.body is a {typ}')
        _, self.inst_at, self.inst_n = t.item(t.u32(self.at + t.offset(C, 'instances')))
        self.isize = t.size('hknpShapeInstance')
        bvd = self.at + t.offset(C, 'boundingVolumeData')
        self.simd = t.u32(bvd + t.offset('hknpCompoundShapeData', 'simdTree') + t.offset('hkcdSimdTree', 'nodes'))
        _req(self.buf[bvd + t.offset('hknpCompoundShapeData', 'type')] == 2, 'the compound has no simd tree')
        self.aabb_at = self.at + t.offset(C, 'aabb')
        self.radius_at = self.at + t.offset(C, 'boundingRadius')

    def instance(self, k: int) -> tuple[int, int]:
        """(instance offset, its convex shape's offset)."""
        t = self.tag
        ia = self.inst_at + k * self.isize
        typ, sat, _ = t.item(t.u32(ia + t.offset('hknpShapeInstance', 'shape')))
        _req(typ == 'hknpConvexShape', f'instance {k} is a {typ}')
        return ia, sat

    def identity(self, k: int) -> bool:
        ia, _ = self.instance(k)
        return struct.unpack_from('<7f', self.buf, ia) == (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0) and \
            struct.unpack_from('<3f', self.buf, ia + 32) == (1.0, 1.0, 1.0)

    def hull(self, k: int) -> tuple[float, list[int], list[int]]:
        """(convex radius, the vertices' and the planes' offsets)."""
        t = self.tag
        _, sat = self.instance(k)
        H = sat + t.offset('hknpConvexShape', 'hull')
        _, va, vn = t.item(t.u32(H + t.offset('hknpConvexHull', 'vertices')))
        _, pa, pn = t.item(t.u32(H + t.offset('hknpConvexHull', 'planes')))
        r = struct.unpack_from('<f', self.buf, sat + t.offset('hknpConvexShape', 'convexRadius'))[0]
        return r, [va + 12 * j for j in range(vn)], [pa + 16 * j for j in range(pn)]

    def vertices(self, k: int) -> list[Vec3]:
        _, vs, _ = self.hull(k)
        return [struct.unpack_from('<3f', self.buf, a) for a in vs]

    def planes(self, k: int) -> list[tuple[float, float, float, float]]:
        _, _, ps = self.hull(k)
        return [struct.unpack_from('<4f', self.buf, a) for a in ps]

    def box(self, k: int) -> tuple[list[float], list[float]]:
        """The instance's box as its tree leaf holds it: its hull's vertices' box grown by its convex radius."""
        r = self.hull(k)[0]
        V = self.vertices(k)
        return [min(v[c] for v in V) - r for c in range(3)], [max(v[c] for v in V) + r for c in range(3)]

    def leaves(self) -> dict[int, tuple[list[float], list[float]]]:
        """Every leaf lane of the simd tree: instance -> its box as stored."""
        t = self.tag
        _, at, n = t.item(self.simd)
        N = 'hkcdSimdTree::Node'
        size, o_data, o_leaf = t.size(N), t.offset(N, 'data'), t.offset(N, 'isLeaf')
        out = {}
        for k in range(n):
            if not self.buf[at + size * k + o_leaf]:
                continue
            f = struct.unpack_from('<24f', self.buf, at + size * k)
            data = struct.unpack_from('<4I', self.buf, at + size * k + o_data)
            for lane in range(4):
                lo = [f[0 + lane], f[8 + lane], f[16 + lane]]
                hi = [f[4 + lane], f[12 + lane], f[20 + lane]]
                if lo[0] <= hi[0]:
                    out[data[lane]] = (lo, hi)
        return out


def floor_box() -> tuple[Vec3, Vec3]:
    """FLOOR in the body proxy's frame."""
    return (tuple(FLOOR[0][c] - BODY_PROXY[c] for c in range(3)),   # type: ignore[return-value]
            tuple(FLOOR[1][c] - BODY_PROXY[c] for c in range(3)))


def hull_map(V: list[Vec3], radius: float) -> tuple[Vec3, Vec3]:
    """(scale, offset) per axis taking the hull's vertex box, turned half round its long axis (x and y negated: the
    flat bottom face up), onto the floor slab less the convex radius (the shell the radius adds stays inside it)."""
    lo, hi = floor_box()
    to = (tuple(lo[c] + radius for c in range(3)), tuple(hi[c] - radius for c in range(3)))
    flip = (-1.0, -1.0, 1.0)
    F = [tuple(v[c] * flip[c] for c in range(3)) for v in V]
    flo = tuple(min(f[c] for f in F) for c in range(3))
    fhi = tuple(max(f[c] for f in F) for c in range(3))
    s, o = box_map(flo, fhi, to)  # type: ignore[arg-type]
    return tuple(s[c] * flip[c] for c in range(3)), o   # type: ignore[return-value]


def build_collision(game) -> tuple[bytes, dict]:  # noqa: ANN001 - rootcpk.Game
    """The bike's ragdoll with its body compound's FLOOR_HULL moved onto the floor slab (see the module doc): its
    vertices mapped p -> p * scale + offset (a positive determinant: a turn and a stretch, no mirror), each plane
    (n, d) of n.p + d = 0 rewritten exactly for the map (n' = n / scale, renormalised with d'), its tree leaf, the
    tree's inner boxes, the compound's box and bounding radius grown to hold it."""
    s = _Shkt(game.read('OBJECT', HOST_RAGDOLL))
    _req(s.inst_n == 14, f'RagDollProxys.body has {s.inst_n} hulls')
    _req(all(s.identity(k) for k in range(s.inst_n)), 'a body hull with a transform of its own')
    stored = s.leaves()
    _req(sorted(stored) == list(range(s.inst_n)), f'tree leaves {sorted(stored)}')
    radius, vs, ps = s.hull(FLOOR_HULL)
    V = s.vertices(FLOOR_HULL)
    scale, off = hull_map(V, radius)
    _req(scale[0] * scale[1] * scale[2] > 0.0, 'the map mirrors the hull')
    for a, v in zip(vs, V):
        struct.pack_into('<3f', s.buf, a, *(v[c] * scale[c] + off[c] for c in range(3)))
    for a in ps:
        n0, n1, n2, d = struct.unpack_from('<4f', s.buf, a)
        n = [n0 / scale[0], n1 / scale[1], n2 / scale[2]]
        L = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        d2 = d - (n[0] * off[0] + n[1] * off[1] + n[2] * off[2])
        struct.pack_into('<4f', s.buf, a, n[0] / L, n[1] / L, n[2] / L, d2 / L)
    _refresh_tree(s, FLOOR_HULL)
    lo, hi = s.box(FLOOR_HULL)
    box = struct.unpack_from('<8f', s.buf, s.aabb_at)
    struct.pack_into('<8f', s.buf, s.aabb_at, min(box[0], lo[0]), min(box[1], lo[1]), min(box[2], lo[2]), box[3],
                     max(box[4], hi[0]), max(box[5], hi[1]), max(box[6], hi[2]), box[7])
    far = max(math.sqrt(sum(c * c for c in v)) for v in s.vertices(FLOOR_HULL)) + radius
    old = struct.unpack_from('<f', s.buf, s.radius_at)[0]
    struct.pack_into('<f', s.buf, s.radius_at, max(old, far))
    return bytes(s.buf), {'hull': FLOOR_HULL, 'scale': scale, 'offset': off, 'box': (lo, hi), 'bounding radius': max(old, far)}


def _refresh_tree(s: _Shkt, k: int) -> None:
    """The simd tree's leaf lane of instance `k` := its box (Shkt.box), every inner lane := the union of its child
    node's lanes; the other leaves keep what they hold."""
    t = s.tag
    _, at, n = t.item(s.simd)
    N = 'hkcdSimdTree::Node'
    size, o_data, o_leaf = t.size(N), t.offset(N, 'data'), t.offset(N, 'isLeaf')

    def node(i: int) -> tuple[list[float], list[float]]:
        base = at + size * i
        f = list(struct.unpack_from('<24f', s.buf, base))
        data = struct.unpack_from('<4I', s.buf, base + o_data)
        leaf = s.buf[base + o_leaf]
        lo_all, hi_all = [math.inf] * 3, [-math.inf] * 3
        for lane in range(4):
            if f[lane] > f[4 + lane]:
                continue       # an empty lane (+FLT_MAX / -FLT_MAX)
            if leaf:
                if data[lane] == k:
                    lo, hi = s.box(k)
                else:
                    lo, hi = [f[lane], f[8 + lane], f[16 + lane]], [f[4 + lane], f[12 + lane], f[20 + lane]]
            else:
                lo, hi = node(data[lane])
            f[lane], f[8 + lane], f[16 + lane] = lo
            f[4 + lane], f[12 + lane], f[20 + lane] = hi
            lo_all = [min(a, b) for a, b in zip(lo_all, lo)]
            hi_all = [max(a, b) for a, b in zip(hi_all, hi)]
        struct.pack_into('<24f', s.buf, base, *f)
        return lo_all, hi_all
    _req(n >= 2, 'an empty simd tree')
    node(1)


def check_collision(data: bytes, stock: bytes | None = None) -> dict:
    """The edited ragdoll: the floor hull's vertices fill the floor slab (less its convex radius) with a flat
    top (its old bottom face) at the box's top; every one of its planes holds its face's vertices (each plane has at
    least three vertices on it) and has every vertex on its inner side; every tree leaf holds its instance's box, every
    inner lane the union under it; the compound's box holds every hull. With `stock`: nothing but the floor hull's
    vertices and planes, the tree's boxes, the compound's box and bounding radius differs. Returns what it measured."""
    s = _Shkt(data)
    radius = s.hull(FLOOR_HULL)[0]
    V = s.vertices(FLOOR_HULL)
    lo, hi = floor_box()
    vlo = [min(v[c] for v in V) for c in range(3)]
    vhi = [max(v[c] for v in V) for c in range(3)]
    _req(all(abs(vlo[c] - (lo[c] + radius)) < 1e-4 and abs(vhi[c] - (hi[c] - radius)) < 1e-4 for c in range(3)),
         f'floor hull {vlo}..{vhi}, the box {lo}..{hi}')
    top = [v for v in V if abs(v[1] - vhi[1]) < 0.01]   # the old bottom face: within 2 mm of level
    _req(len(top) >= 4, f'{len(top)} vertices on the floor slab top: not a flat floor')
    floor = (max(v[0] for v in top) - min(v[0] for v in top)) * (max(v[2] for v in top) - min(v[2] for v in top))
    for p in s.planes(FLOOR_HULL):
        _req(abs(p[0] * p[0] + p[1] * p[1] + p[2] * p[2] - 1.0) < 1e-4, f'plane {p} not unit')
        dist = [p[0] * v[0] + p[1] * v[1] + p[2] * v[2] + p[3] for v in V]
        _req(max(dist) < 1e-4, f'plane {p}: a vertex {max(dist):.5f} m outside it')
        _req(sum(1 for d in dist if abs(d) < 1e-4) >= 3, f'plane {p}: fewer than three vertices on it')
    leaves = s.leaves()
    for k in range(s.inst_n):
        blo, bhi = s.box(k)
        llo, lhi = leaves[k]
        _req(all(llo[c] <= blo[c] + 1e-5 and lhi[c] >= bhi[c] - 1e-5 for c in range(3)), f'tree leaf {k} misses its hull')
    box = struct.unpack_from('<8f', s.buf, s.aabb_at)
    for k in range(s.inst_n):
        blo, bhi = s.box(k)
        _req(all(box[c] <= blo[c] + 1e-5 and box[4 + c] >= bhi[c] - 1e-5 for c in range(3)), f'the compound box misses hull {k}')
    if stock is not None:
        st = _Shkt(stock)
        _req(len(st.buf) == len(s.buf), 'the file changed size')
        _, vs, ps = s.hull(FLOOR_HULL)
        allowed = set()
        for a in vs:
            allowed |= set(range(a, a + 12))
        for a in ps:
            allowed |= set(range(a, a + 16))
        _, tat, tn = s.tag.item(s.simd)
        size = s.tag.size('hkcdSimdTree::Node')
        for i in range(tn):
            allowed |= set(range(tat + size * i, tat + size * i + 96))
        allowed |= set(range(s.aabb_at, s.aabb_at + 32)) | set(range(s.radius_at, s.radius_at + 4))
        diff = [i for i in range(len(s.buf)) if s.buf[i] != st.buf[i] and i not in allowed]
        _req(not diff, f'{len(diff)} bytes changed outside the floor edit (first at {diff[:4]})')
        for k in range(s.inst_n):
            if k != FLOOR_HULL:
                _req(s.vertices(k) == st.vertices(k) and s.planes(k) == st.planes(k), f'hull {k} changed')
                _req(leaves[k] == st.leaves()[k], f'tree leaf {k} changed')
    return {'hull box': (vlo, vhi), 'top y (model)': vhi[1] + BODY_PROXY[1] + radius, 'floor m2': round(floor, 3),
            'radius': radius}
