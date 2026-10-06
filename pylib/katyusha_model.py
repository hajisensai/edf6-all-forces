"""KATYUSHA.MRAB, built from the player's own Root.cpk (pylib + graft_pure; the launcher's parts with procmesh, so
numpy and PIL, both in the installer).

    build(game) -> bytes     the finished archive (game: rootcpk.Game, read only)
    check(arc)  -> None      raises KatyushaCheckError on any self-check failure

The model: the Naegling (OBJECT/VEHICLE402_ROCKET.MRAB, Vehicle402_Rocket.mdb) with its hull and track geometry
removed and the V607 robo-truck's truck (OBJECT/V607_ROBOTRUCK.MRAB, object 0 mesh 0: body + 6 tires; the robot
and its cradle on the bed dropped) grafted in, with the bed's two sideboards removed; the rocket rack
(Rocketcannon_base subtree) translated onto the bed,
its launcher box (the geometry on Rocketcannon_main) replaced by a BM-13 rail pack: 8 I-beam rails, 16 M-13 rockets
(launcher_parts; the weapon's muzzles at the rails' front ends: MUZZLES). The V607 stays the truck: of Root.cpk's
other trucks the pickups (V610 / V611) are 5.4 m with a 1.5 m bed and two axles, the kei truck (V512) 4.1 m, the
tractor (V513) a civilian cab with no bed; the V607 is a three-axle military truck with a long bed, as the BM-13's.

Kept for the stock 402_Rocket class / VEHICLE402_ROCKET.SGO: every bone (names, parents, order, kinds, links), the
turntable and ram geometry and every stock material / texture. Changed binds (local + inverse bind consistently): the 3 rack
bones (translated together) and the 12 car wheel bones tire_moveA..F_l/r (front axle -> A, middle -> C, rear -> E,
each tire skinned to its bone at the tire centre; B / D / F put on A / C / E with no geometry). The truck body is
skinned to `body`, the Naegling's hull bone. The catapi (track) object is dropped.

The elevation ram (the Naegling's Rocketcannon_prop: a hydraulic cylinder on the turntable, its rod's eye in the
launcher's belly by the launcher's pivot) is made telescopic: the rod, its end collar and its eye go onto a bone of
their own, RAM_ROD (inserted after the prop under Rocketcannon_base, preorder kept, later bones and blend indices
renumbered), pivoting at the eye, and the rod is lengthened into the cylinder by the stroke + RAM_OVERLAP. The stock
class turns the whole prop by the launcher's elevation about the cylinder's pivot (0x5FDDA0: asin of the launcher's
forward on the turntable's up, plus its bind angle): the eye stays inside the launcher only up to ~52 deg
(ram_report), so past the Naegling's 50 deg the rod hung in the air. EDF6VehicleCrew (src/katyusha.cpp) aims both
bones at each other's pivot every frame instead (ram_pose: the cylinder from its pivot P towards the eye E, the rod
from E back along the same line), over the whole 0..PITCH_STOP_DEG.
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import replace

import numpy as np

import graft_pure as g
import procmesh as pm
from graft_pure import Vec3
from mdb import Bone, Mdb, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import link, vertex_table

HOST_ARC, HOST_MDB = 'VEHICLE402_ROCKET.MRAB', 'Vehicle402_Rocket.mdb'
DONOR_ARC, DONOR_MDB = 'V607_ROBOTRUCK.MRAB', 'v607_robotruck.mdb'

TRUCK_SCALE = 1.0               # the donor truck is 8.14 m long: already the ~8 m target
DONOR_TRUCK_MESHES = {(0, 0)}   # donor object 0 mesh 0 = the truck; meshes 1/4 = robot cradle, 2/3 = robot
# V607 donor-space landmarks of its removable bed sides (measured from the actual mesh).
# Keep the bed/fenders below 1.62 m, the cab/headboard ahead of 1.05 m, and the tailgate
# crossing the truck's centre. The panels and their top-mounted rear latches are outboard.
SIDEBOARD_INNER_X, SIDEBOARD_BOTTOM_Y, SIDEBOARD_FRONT_Z = 1.23, 1.62, 1.05
CAB_CLEARANCE = 0.15            # m between the rack's front (0 elevation) and the cab / headboard
RAIL_CLEARANCE = 0.03           # m between the rack main box's underside and the bed side rails' top
# donor tire bone -> host wheel bone (A front .. F rear; _l = +x on both models)
WHEEL_MAP = {'tireF_l': 'tire_moveA_l', 'tireF_r': 'tire_moveA_r',
             'tireB0_l': 'tire_moveC_l', 'tireB0_r': 'tire_moveC_r',
             'tireB1_l': 'tire_moveE_l', 'tireB1_r': 'tire_moveE_r'}
ALL_WHEELS = [f'tire_move{a}_{s}' for a in 'ABCDEF' for s in 'lr']
RACK = ['Rocketcannon_base', 'Rocketcannon_main', 'Rocketcannon_prop']
# The launcher's elevation stop, degrees up (tools/make_katyusha.py writes it into the hinge limit; the ram is built for
# it: its stroke is the eye's travel from 0 up to here).
PITCH_STOP_DEG = 80.0
# The ram (module docstring): the rod's bone; the distance along the ram from the cylinder's pivot (m) past which a
# piece's centre puts it on the rod (the cylinder and its gland are centred within ~0.75 m, the rod's pieces from
# ~1.0 m); how much rod stays inside the cylinder at full stroke.
RAM_ROD = 'edf6vc_ram_rod'
RAM_SPLIT = 0.85
RAM_OVERLAP = 0.08
# The ram's pieces per side: the cylinder and its gland (the prop's), the rod, its end collar and its eye (the rod's).
RAM_PIECES, ROD_PIECES = 5, 3
OBJECT_BONES = ['Vehicle402_Rocket', 'Vehicle_Rocketcannon']   # their bounds are recomputed (object boxes)


class KatyushaCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise KatyushaCheckError(msg)


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def object_positions(md: Mdb, obj: int) -> list[Vec3]:
    return [p for me in md.objects[obj].meshes for p in g.mesh_positions(me)]


# ------------------------------------------------------------------------------------------ measurements

def truck_geometry(md: Mdb, obj: int, body: int) -> dict[str, float]:
    """Bed / cab landmarks measured off the grafted truck (host space): the z where the cab / headboard starts
    (first point above 2.6 m), the bed floor height (median over 0.2 m slabs of the highest centre point), the
    side rail top, the truck's rear / front z."""
    Q: list[Vec3] = []
    for me in md.objects[obj].meshes:
        bi, _bw = g.skin_columns(me)
        Q += [p for p, i in zip(g.mesh_positions(me), bi) if int(i[0]) == body]
    cab_z = min(p[2] for p in Q if p[1] > 2.6)
    zmin = min(p[2] for p in Q)
    bed = [p for p in Q if p[2] < cab_z - 0.2 and p[2] > zmin + 0.3]
    centre = [p for p in bed if abs(p[0]) < 0.9]
    start, stop, step = min(p[2] for p in bed), cab_z - 0.2, 0.2
    n = -int(-((stop - start) / step) // 1)                # numpy.arange length: ceil((stop - start) / step)
    slabs = []
    for k in range(n):
        z = start + k * step
        ys = [p[1] for p in centre if z <= p[2] < z + 0.2]
        if ys:
            slabs.append(max(ys))
    slabs.sort()
    m = len(slabs)
    floor = slabs[m // 2] if m % 2 else (slabs[m // 2 - 1] + slabs[m // 2]) / 2
    rails = max(p[1] for p in bed if abs(p[0]) > 1.2)
    return {'cab_z': cab_z, 'bed_floor_y': floor, 'rail_top_y': rails, 'rear_z': zmin, 'front_z': max(p[2] for p in Q)}


def sideboard_triangle(points: list[Vec3]) -> bool:
    """A complete bed-side face in the original V607 coordinates; never a wheel or bed floor."""
    return (all(p[0] > SIDEBOARD_INNER_X for p in points) or all(p[0] < -SIDEBOARD_INNER_X for p in points)) and all(
        p[1] > SIDEBOARD_BOTTOM_Y and p[2] < SIDEBOARD_FRONT_Z for p in points)


def remove_sideboards(md: Mdb, body: int, offset: Vec3) -> tuple[Mdb, int]:
    """Remove the grafted truck's two sideboards, keeping original rows of every retained face."""
    meshes, removed = [], 0
    for me in md.objects[0].meshes:
        _keys, rows = vertex_table(me)
        points, (bi, _bw) = g.mesh_positions(me), g.skin_columns(me)
        kept = []
        for tri in g.triangles(me):
            donor = [tuple((points[v][c] - offset[c]) / TRUCK_SCALE for c in range(3)) for v in tri]
            if all(int(bi[v][0]) == body for v in tri) and sideboard_triangle(donor):
                removed += 1
            else:
                kept.append(tri)
        new = g.rebuild_mesh(me, rows, kept)
        if new is not None:
            meshes.append(new)
    _req(removed >= 200, f'only {removed} V607 sideboard faces identified; donor layout changed')
    obj = replace(md.objects[0], meshes=meshes)
    return replace(md, objects=[obj, *md.objects[1:]], buffer_order=None), removed


# ------------------------------------------------------------------------------------------ the elevation ram

def _unit(v: Vec3) -> Vec3:
    n = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5
    _req(n > 1e-9, 'zero vector')
    return (v[0] / n, v[1] / n, v[2] / n)


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def launcher_point(p: Vec3, M: Vec3, theta: float) -> Vec3:
    """Point `p` of the launcher (bind, model space) with the launcher raised `theta` rad about its pivot `M` (nose up:
    its forward (0, 0, 1) turns to (0, sin, cos), as the stock hinge and src/katyusha.cpp LoftRows turn it)."""
    dy, dz = p[1] - M[1], p[2] - M[2]
    c, s = math.cos(theta), math.sin(theta)
    return (p[0], M[1] + dy * c + dz * s, M[2] - dy * s + dz * c)


def ram_angle(P: Vec3, E: Vec3) -> float:
    """The ram's angle from P to E, rad up from the turntable's backward (-z): the eye is behind the cylinder."""
    return math.atan2(E[1] - P[1], -(E[2] - P[2]))


def ram_pose(P: Vec3, E: Vec3, M: Vec3, theta: float) -> tuple[float, Vec3, float]:
    """The ram with the launcher raised `theta` rad (P the cylinder's pivot, E the rod's eye at bind, M the launcher's
    pivot; model space, the turntable's frame): (its turn from bind, rad, far end rising; where the eye is now; its
    length P..eye). Both bones turn by the same angle (src/katyusha.cpp RamTurn), so the rod slides along the
    cylinder's axis."""
    e = launcher_point(E, M, theta)
    return ram_angle(P, e) - ram_angle(P, E), e, math.dist((P[1], P[2]), (e[1], e[2]))


def ram_turn(p: Vec3, pivot: Vec3, d: float) -> Vec3:
    """`p` turned `d` rad about the x axis through `pivot`, the far (backward) end rising (src/katyusha.cpp RamTurn)."""
    dy, dz = p[1] - pivot[1], p[2] - pivot[2]
    c, s = math.cos(d), math.sin(d)
    return (p[0], pivot[1] + dy * c - dz * s, pivot[2] + dy * s + dz * c)


def _pieces(me, bone: int) -> list[list[int]]:  # noqa: ANN001 - mdb.Mesh
    """The connected pieces (vertex lists, by shared triangles) of `me` whose vertices are skinned to `bone`."""
    bi, _bw = g.skin_columns(me)
    par = list(range(me.nverts))

    def root(a: int) -> int:
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for t in g.triangles(me):
        par[root(t[0])] = root(t[1])
        par[root(t[1])] = root(t[2])
    out: dict[int, list[int]] = {}
    for v in range(me.nverts):
        if int(bi[v][0]) == bone:
            out.setdefault(root(v), []).append(v)
    return list(out.values())


def stroke(P: Vec3, E: Vec3, M: Vec3, stop_deg: float = PITCH_STOP_DEG) -> float:
    """How much longer the ram gets from 0 to `stop_deg` (m; it only lengthens on the way: checked)."""
    lengths = [ram_pose(P, E, M, math.radians(k))[2] for k in range(0, int(stop_deg) + 1)]
    _req(all(b >= a - 1e-6 for a, b in zip(lengths, lengths[1:])), 'the ram does not only lengthen')
    return lengths[-1] - lengths[0]


def _ram_geometry(md: Mdb) -> dict:
    """The stock prop's pieces and pivots in `md` (bind, model space): the cylinder's pivot P, the launcher's pivot M,
    the ram's axis u (the prop's local x, towards the eye), the rod's eye E (the mean of each side's farthest rod
    piece, on the axis), the mesh holding the prop (object k, mesh j), the axial distance of each of its vertices,
    the rod's pieces per side."""
    prop, main = md.bone_index('Rocketcannon_prop'), md.bone_index('Rocketcannon_main')
    w = bind_world(md)
    P: Vec3 = (w[prop][12], w[prop][13], w[prop][14])
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    u = _unit((w[prop][0], w[prop][1], w[prop][2]))   # the prop's local x: along the ram, towards the eye
    hits = [(k, j) for k, o in enumerate(md.objects) for j, me in enumerate(o.meshes) if me.flags[1] and _pieces(me, prop)]
    _req(len(hits) == 1, f"the prop's geometry is in {len(hits)} meshes, expected 1")
    k, j = hits[0]
    me = md.objects[k].meshes[j]
    pos = g.mesh_positions(me)
    axial = [_dot((p[0] - P[0], p[1] - P[1], p[2] - P[2]), u) for p in pos]
    pieces = _pieces(me, prop)
    rod = [pc for pc in pieces if sum(axial[v] for v in pc) / len(pc) > RAM_SPLIT]
    _req(len(pieces) == 2 * RAM_PIECES and len(rod) == 2 * ROD_PIECES,
         f'the ram has {len(pieces)} pieces, {len(rod)} past {RAM_SPLIT} m (expected {2 * RAM_PIECES}, {2 * ROD_PIECES})')
    sides = [[pc for pc in rod if (pos[pc[0]][0] > P[0]) == left] for left in (True, False)]
    _req(all(len(sd) == ROD_PIECES for sd in sides), "the rod's pieces are not three a side")
    near = lambda pc: min(axial[v] for v in pc)  # noqa: E731
    # The eye: each side's farthest piece; its centre (both sides' mean, on the prop's axis) is the rod's pivot.
    eyes = [max(sd, key=near) for sd in sides]
    eye_at = sum(sum(axial[v] for v in pc) / len(pc) for pc in eyes) / len(eyes)
    E: Vec3 = (P[0] + u[0] * eye_at, P[1] + u[1] * eye_at, P[2] + u[2] * eye_at)
    return {'P': P, 'M': M, 'u': u, 'E': E, 'k': k, 'j': j, 'axial': axial, 'rod': rod, 'sides': sides,
            'near': near, 'eye_x': sorted(sum(pos[v][0] for v in pc) / len(pc) for pc in eyes)}


def split_ram(md: Mdb, info: dict) -> Mdb:
    """The prop's rod, end collar and eye onto RAM_ROD, a new bone at the eye (on the prop's axis) under
    Rocketcannon_base right after the prop; the rod lengthened into the cylinder by the stroke + RAM_OVERLAP. Every
    bone after the insertion is renumbered (blend indices too). info['ram'] gets the pivots and the stroke."""
    prop, base = md.bone_index('Rocketcannon_prop'), md.bone_index('Rocketcannon_base')
    w = bind_world(md)
    rg = _ram_geometry(md)
    P, M, u, E, k, j = rg['P'], rg['M'], rg['u'], rg['E'], rg['k'], rg['j']
    axial, rod, sides, near = rg['axial'], rg['rod'], rg['sides'], rg['near']
    rise = stroke(P, E, M)
    reach = rise + RAM_OVERLAP
    # The rod proper is each side's piece nearest the cylinder: its front ring goes `reach` into the cylinder.
    rods = [min(sd, key=near) for sd in sides]
    stretch = {v for pc in rods for v in pc if axial[v] < near(pc) + 0.01}
    on_rod = {v for pc in rod for v in pc}

    at = max(g.subtree(md, base)) + 1
    _req(at == prop + 1, 'the prop is not the last bone under the launcher\'s base')
    names = list(md.names)
    name = g._name_index(names, RAM_ROD)
    bind = g.translation(E)
    shift = lambda i: i + 1 if i >= at else i  # noqa: E731
    bones = [replace(b, index=shift(b.index), parent=shift(b.parent) if b.parent >= 0 else -1) for b in md.bones]
    bones.insert(at, Bone(at, base, -1, -1, name, 0, 3, 0, 1, 0, 0, mmul(bind, g.inverse_affine_general(w[base])),
                          g.translation((-E[0], -E[1], -E[2])), [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    link(bones)
    objects = []
    for ko, o in enumerate(md.objects):
        meshes = []
        for jo, m in enumerate(o.meshes):
            if not m.flags[1]:
                meshes.append(m)
                continue
            keys, rows = vertex_table(m)
            pk, bk = g._pos_key(keys), g._bi_key(keys)
            bi, bw = g.skin_columns(m)
            ours = (ko, jo) == (k, j)
            for v, r in enumerate(rows):
                idx = [shift(int(x)) for x in r[bk]]
                if ours and v in on_rod:
                    _req(g.influences(bi[v], bw[v]) == {prop}, f'ram vertex {v} not on the prop alone')
                    idx[0] = at
                if ours and v in stretch:
                    p = r[pk]
                    r[pk] = (p[0] - u[0] * reach, p[1] - u[1] * reach, p[2] - u[2] * reach) + tuple(p[3:])
                r[bk] = tuple(idx)
            meshes.append(g.rebuild_mesh(m, rows, g.triangles(m)) or m)
        objects.append(replace(o, bone=shift(o.bone), meshes=meshes))
    info['ram'] = {'P': P, 'E': E, 'M': M, 'stroke': rise, 'rod_into_cylinder': reach}
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None)


def ram_report(md: Mdb, stop_deg: float = PITCH_STOP_DEG, step: int = 5) -> list[dict[str, float]]:
    """Per elevation 0..stop_deg (every `step` deg) the ram of `md` (with RAM_ROD) as src/katyusha.cpp poses it: its
    length P..eye, where the rod's front is along it (m from the cylinder's pivot) against the cylinder's span, and
    how far the eye's vertices stray outside the launcher's own box (in the launcher's frame; 0: inside); and, to
    compare, how far the stock pose (the whole prop turned by the elevation about P, rod and all) puts the eye out."""
    names = {md.name_of(b.name): b.index for b in md.bones}
    w = bind_world(md)
    pts = g.skinned_points(md)
    prop, rod, main = names['Rocketcannon_prop'], names[RAM_ROD], names['Rocketcannon_main']
    P: Vec3 = (w[prop][12], w[prop][13], w[prop][14])
    E: Vec3 = (w[rod][12], w[rod][13], w[rod][14])
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    u = _unit((E[0] - P[0], E[1] - P[1], E[2] - P[2]))
    cyl = [_dot((p[0] - P[0], p[1] - P[1], p[2] - P[2]), u) for p in pts[prop]]
    rod_rel = [_dot((p[0] - E[0], p[1] - E[1], p[2] - E[2]), u) for p in pts[rod]]
    eye = [p for p, a in zip(pts[rod], rod_rel) if a > -0.2]
    box = [(min(p[c] for p in pts[main]), max(p[c] for p in pts[main])) for c in range(3)]

    def outside(q: Vec3, theta: float) -> float:
        b = launcher_point(q, M, -theta)   # back into the launcher's frame
        return max(max(lo - b[c], b[c] - hi, 0.0) for c, (lo, hi) in enumerate(box))
    out = []
    for deg in range(0, int(stop_deg) + 1, step):
        t = math.radians(deg)
        d, e, length = ram_pose(P, E, M, t)
        posed = [ram_turn((q[0] + e[0] - E[0], q[1] + e[1] - E[1], q[2] + e[2] - E[2]), e, d) for q in eye]
        stock = [ram_turn(q, P, t) for q in eye]
        out.append({'deg': deg, 'length': length, 'rod_front': length + min(rod_rel), 'cyl_front': min(cyl),
                    'cyl_back': max(cyl), 'eye_out': max(outside(q, t) for q in posed),
                    'stock_eye_out': max(outside(q, t) for q in stock)})
    return out


# ------------------------------------------------------------------------------------------ the BM-13 launcher

# The launcher (Rocketcannon_main's geometry) is a BM-13's rail pack, made here in place of the Naegling's box from
# pylib/procmesh.py parts in the box's own material and texture (two even patches of it: the frame dark, the rockets
# lighter). In the launcher's frame (m from its pivot, the bone's origin; its rotation is the model's, checked):
#  - RAILS I-beam rails RAIL_PITCH apart from RAIL_BACK to RAIL_FRONT (the Naegling box's front face, where the stock
#    muzzles are), their centres RAIL_Y over the pivot;
#  - two M-13 rockets on each rail, one hung over it and one under it, loaded at its rear (they go on at the breech end
#    and run the rail's length when fired), tail at ROCKET_TAIL;
#  - the rails on CROSS_Z cross-members and two longitudinal beams between the rails, the beams on brackets down to
#    the pivot tube, and a shaft through the ram's two eyes (the rod's pivot, _ram_geometry) under the rails.
# The weapon's muzzles (the MAB locators of EDF6VC_KATYUSHA_ROCKETS.SGO, tools/make_katyusha.py) are MUZZLES: at the
# rails' front ends, on the rockets' axes. check_launcher finds the rails, rockets and muzzles in the built model.
RAILS = 8
RAIL_PITCH = 0.30
RAIL_BACK, RAIL_FRONT = -0.25, 4.40
RAIL_Y = 0.40
RAIL_H, RAIL_FLANGE, RAIL_FLANGE_T, RAIL_WEB_T = 0.14, 0.07, 0.012, 0.014
# M-13: 132 mm across, 1.41 m long; the fins' span 0.28 m (four blades, 45 deg off the rail so they clear it).
ROCKET_R, ROCKET_LEN, ROCKET_GAP, ROCKET_TAIL = 0.066, 1.41, 0.006, 0.55
FIN_R, FIN_LEN, FIN_T = 0.14, 0.22, 0.006
CROSS_Z = (-0.18, 2.10, 3.25, 4.28)
CROSS_W, CROSS_H = 0.08, 0.09
BEAM_X, BEAM_W = 0.30, 0.07
BEAM_BACK, BEAM_FRONT = -0.22, 4.32
PIVOT_R, PIVOT_HALF, BRACKET_LOW = 0.05, 0.80, -0.05
EYE_SHAFT_R = 0.035
TINTS = {'frame': (64, 66, 64), 'rocket': (120, 124, 116)}
SEGS = 10
# How far the launcher, raised anywhere from 0 to PITCH_STOP_DEG, stays over the truck's bed floor (m, check).
BED_CLEARANCE = 0.10


def rail_xs() -> list[float]:
    """The rails' x in the launcher's frame, left (+x) to right."""
    return [((RAILS - 1) / 2 - i) * RAIL_PITCH for i in range(RAILS)]


def rocket_ys() -> tuple[float, float]:
    """The axes' heights of the rocket over a rail and of the one under it."""
    off = RAIL_H / 2 + ROCKET_GAP + ROCKET_R
    return RAIL_Y + off, RAIL_Y - off


def muzzle_points() -> list[tuple[str, Vec3]]:
    """The weapon's 10 muzzle locators (their stock names, the order the weapon fires them in), at the rails' front
    ends: the 8 upper rockets' axes from the outside in, left and right in turn, then the two outer lower ones."""
    xs, (up, low) = rail_xs(), rocket_ys()
    order = [(xs[i], up) for k in range(RAILS // 2) for i in (k, RAILS - 1 - k)] + [(xs[0], low), (xs[-1], low)]
    return [(f'{n + 1:02d}', (x, y, RAIL_FRONT)) for n, (x, y) in enumerate(order)]


MUZZLES = muzzle_points()


def _box(part, c: Vec3, axes: tuple[Vec3, Vec3, Vec3], half: Vec3, skin) -> None:  # noqa: ANN001 - procmesh.Part
    """A box at `c` along the right-handed unit `axes`, `half` its half sizes: 4 vertices a face (flat shading), each
    face wound outward (the stock winding: (b - a) x (c - a) along the outward normal)."""
    for k in range(3):
        u, v = axes[(k + 1) % 3], axes[(k + 2) % 3]
        hu, hv = half[(k + 1) % 3], half[(k + 2) % 3]
        for s in (1.0, -1.0):
            n = tuple(axes[k][i] * half[k] * s for i in range(3))
            corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)] if s > 0 else [(-1, -1), (-1, 1), (1, 1), (1, -1)]
            ids = [part.add(tuple(c[i] + n[i] + a * hu * u[i] + b * hv * v[i] for i in range(3)), skin,
                            ((a + 1) / 2, (b + 1) / 2)) for a, b in corners]
            part.tris += [(ids[0], ids[1], ids[2]), (ids[0], ids[2], ids[3])]


def _tube(part, c: Vec3, axes: tuple[Vec3, Vec3, Vec3], stations: list[tuple[float, float]], skin) -> None:  # noqa: ANN001
    """A round tube from `c` along axes[2] (axes right-handed): (distance, radius) `stations`, its sides wound outward
    and its two ends capped with vertices of their own (the sides shade smooth, the caps flat)."""
    u, v, w = axes

    def at(t: float, r: float, k: int) -> Vec3:
        a = 2 * math.pi * k / SEGS
        return tuple(c[i] + w[i] * t + r * (math.cos(a) * u[i] + math.sin(a) * v[i]) for i in range(3))
    rings = [[part.add(at(t, r, k), skin, (k / SEGS, n / (len(stations) - 1))) for k in range(SEGS)]
             for n, (t, r) in enumerate(stations)]
    for a, b in zip(rings, rings[1:]):
        for k in range(SEGS):
            q = (k + 1) % SEGS
            part.tris += [(a[k], a[q], b[q]), (a[k], b[q], b[k])]
    for (t, r), out in ((stations[0], -1), (stations[-1], 1)):
        mid = part.add(tuple(c[i] + w[i] * t for i in range(3)), skin, (0.5, 0.5))
        ring = [part.add(at(t, r, k), skin, (0.5 + 0.5 * math.cos(2 * math.pi * k / SEGS),
                                             0.5 + 0.5 * math.sin(2 * math.pi * k / SEGS))) for k in range(SEGS)]
        for k in range(SEGS):
            q = (k + 1) % SEGS
            part.tris.append((mid, ring[k], ring[q]) if out > 0 else (mid, ring[q], ring[k]))


X_AXES: tuple[Vec3, Vec3, Vec3] = ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))   # a tube along x
Z_AXES: tuple[Vec3, Vec3, Vec3] = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def _rocket(part, x: float, y: float, skin) -> None:  # noqa: ANN001 - procmesh.Part
    """An M-13 along the launcher, its tail at ROCKET_TAIL: the body, a blunt ogive nose, four fins at the tail."""
    r = ROCKET_R
    _tube(part, (x, y, ROCKET_TAIL), Z_AXES,
          [(0.0, 0.8 * r), (0.03, r), (ROCKET_LEN - 0.30, r), (ROCKET_LEN - 0.12, 0.75 * r), (ROCKET_LEN, 0.25 * r)], skin)
    mid = (r - 0.005 + FIN_R) / 2
    for k in range(4):
        a = math.radians(45 + 90 * k)
        d, t = (math.cos(a), math.sin(a), 0.0), (-math.sin(a), math.cos(a), 0.0)
        _box(part, (x + d[0] * mid, y + d[1] * mid, ROCKET_TAIL + FIN_LEN / 2), (d, t, (0.0, 0.0, 1.0)),
             ((FIN_R - r + 0.005) / 2, FIN_T / 2, FIN_LEN / 2), skin)


def launcher_parts(eye: Vec3, eye_x: float, bone: int, material: int) -> list:
    """The launcher's parts (procmesh.Part, in the launcher's frame, skinned to `bone`): the frame and the rockets.
    `eye`: the ram's eye (the rod's pivot) in the launcher's frame, `eye_x` the eyes' |x|."""
    skin = [(bone, 1.0)]
    frame, rockets = (pm.Part(material, TINTS[k]) for k in ('frame', 'rocket'))
    length, mid_z = RAIL_FRONT - RAIL_BACK, (RAIL_FRONT + RAIL_BACK) / 2
    for x in rail_xs():
        for y, hx, hy in ((RAIL_Y + (RAIL_H - RAIL_FLANGE_T) / 2, RAIL_FLANGE / 2, RAIL_FLANGE_T / 2),
                          (RAIL_Y, RAIL_WEB_T / 2, RAIL_H / 2 - RAIL_FLANGE_T),
                          (RAIL_Y - (RAIL_H - RAIL_FLANGE_T) / 2, RAIL_FLANGE / 2, RAIL_FLANGE_T / 2)):
            _box(frame, (x, y, mid_z), Z_AXES, (hx, hy, length / 2), skin)
        for y in rocket_ys():
            _rocket(rockets, x, y, skin)
    under = RAIL_Y - RAIL_H / 2
    span = max(abs(x) for x in rail_xs()) + RAIL_FLANGE / 2
    for z in CROSS_Z:
        _box(frame, (0.0, under - CROSS_H / 2, z), Z_AXES, (span, CROSS_H / 2, CROSS_W / 2), skin)
    for x in (BEAM_X, -BEAM_X):
        _box(frame, (x, under - CROSS_H / 2, (BEAM_FRONT + BEAM_BACK) / 2), Z_AXES,
             (BEAM_W / 2, CROSS_H / 2, (BEAM_FRONT - BEAM_BACK) / 2), skin)
        low, high = BRACKET_LOW, under - CROSS_H
        _box(frame, (x, (low + high) / 2, 0.0), Z_AXES, (BEAM_W / 2, (high - low) / 2, 2 * PIVOT_R - 0.01), skin)
    _tube(frame, (-PIVOT_HALF, 0.0, 0.0), X_AXES, [(0.0, PIVOT_R), (2 * PIVOT_HALF, PIVOT_R)], skin)
    half = eye_x + 0.06
    _tube(frame, (-half, eye[1], eye[2]), X_AXES, [(0.0, EYE_SHAFT_R), (2 * half, EYE_SHAFT_R)], skin)
    return [frame, rockets]


def replace_launcher(md: Mdb, host0: Mdb, host_rab, info: dict) -> Mdb:  # noqa: ANN001 - mdb.Rab
    """`md` (the stock model stripped to its rack, stock binds) with the Naegling's launcher box (every triangle on
    Rocketcannon_main) taken out and the BM-13 rail pack (launcher_parts) put in its mesh's material and layout, into
    the rack's object. The turntable and the ram are left as they are. info['launcher'] gets what was built."""
    main = md.bone_index('Rocketcannon_main')
    w = bind_world(md)
    _req(max(abs(a - b) for a, b in zip(w[main][:12], (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))) < 1e-5,
         "Rocketcannon_main's axes are not the model's")
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    before = {i: len(p) for i, p in g.skinned_points(md).items()}
    md = g.strip_geometry(md, set(range(len(md.bones))) - {main}, drop_empty_objects=False)
    after = {i: len(p) for i, p in g.skinned_points(md).items()}
    _req(main not in after and all(after.get(i) == n for i, n in before.items() if i != main),
         'the launcher box shares triangles with the turntable or the ram')
    rg = _ram_geometry(md)
    eye = (rg['E'][0] - M[0], rg['E'][1] - M[1], rg['E'][2] - M[2])
    eye_x = max(abs(x - M[0]) for x in rg['eye_x'])
    tmpl = md.objects[rg['k']].meshes[rg['j']]
    albedo = pm.albedos(host_rab, host0)
    meshes = []
    for part in launcher_parts(eye, eye_x, main, tmpl.material):
        part.pos = [p + np.array(M) for p in part.pos]
        vdata, idx = pm.pack(part, tmpl, pm.uv_boxes(host0, albedo, part.tint), 1.0)
        meshes.append(replace(tmpl, vdata=vdata, indices=idx))
    objects = list(md.objects)
    objects[rg['k']] = replace(objects[rg['k']], meshes=objects[rg['k']].meshes + meshes)
    info['launcher'] = {'rails': RAILS, 'rail_length': RAIL_FRONT - RAIL_BACK, 'rockets': 2 * RAILS,
                        'vertices': sum(me.nverts for me in meshes), 'eye': eye}
    return replace(md, objects=objects, buffer_order=None)


def _solids(me, bone: int) -> list[list[int]]:  # noqa: ANN001 - mdb.Mesh
    """The pieces (vertex lists) of `me` on `bone`, joined by shared triangles and by shared positions (a flat-shaded
    box's faces have vertices of their own, at its corners)."""
    pos = g.mesh_positions(me)
    first: dict[Vec3, int] = {}
    par = list(range(me.nverts))

    def root(a: int) -> int:
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for t in g.triangles(me):
        par[root(t[0])] = root(t[1])
        par[root(t[1])] = root(t[2])
    for v, p in enumerate(pos):
        par[root(v)] = root(first.setdefault(p, v))
    bi, _bw = g.skin_columns(me)
    out: dict[int, list[int]] = {}
    for v in range(me.nverts):
        if int(bi[v][0]) == bone:
            out.setdefault(root(v), []).append(v)
    return list(out.values())


def _components(md: Mdb, bone: int, origin: Vec3) -> list[tuple[Vec3, Vec3]]:
    """The (low, high) corners of every solid piece of geometry on `bone` (_solids), relative to `origin`."""
    out = []
    for o in md.objects:
        for me in o.meshes:
            if not me.flags[1]:
                continue
            pos = g.mesh_positions(me)
            for pc in _solids(me, bone):
                q = [(pos[v][0] - origin[0], pos[v][1] - origin[1], pos[v][2] - origin[2]) for v in pc]
                out.append((tuple(min(p[c] for p in q) for c in range(3)), tuple(max(p[c] for p in q) for c in range(3))))
    return out


def check_launcher(md: Mdb, muzzles: list[tuple[str, Vec3]] | None = None, tol: float = 0.01) -> dict:
    """The launcher's geometry, found in `md` (the launcher's frame; positions are stored as half floats, so `tol`):
    RAILS I-beam rails along it (each three long pieces stacked flange / web / flange, the web the narrowest), one
    front end and one rear end for all, RAIL_PITCH apart; 2 x RAILS rocket bodies, one over and one under each rail;
    every muzzle (default MUZZLES) at a rail's front end, on the axis of one of its rockets. Returns what it found."""
    main = md.bone_index('Rocketcannon_main')
    w = bind_world(md)
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    comps = _components(md, main, M)
    longs: dict[float, list[tuple[Vec3, Vec3]]] = {}
    for lo, hi in comps:
        if hi[2] - lo[2] > 4.0:
            longs.setdefault(round((lo[0] + hi[0]) / 2, 2), []).append((lo, hi))
    rails = {}
    for x, pcs in longs.items():
        if len(pcs) != 3:
            continue
        pcs.sort(key=lambda b: b[0][1])
        width = [hi[0] - lo[0] for lo, hi in pcs]
        stacked = all(abs(a[1][1] - b[0][1]) < tol for a, b in zip(pcs, pcs[1:]))
        same = all(abs(lo[2] - pcs[0][0][2]) < tol and abs(hi[2] - pcs[0][1][2]) < tol for lo, hi in pcs)
        if stacked and same and width[1] < min(width[0], width[2]):
            rails[x] = (pcs[0][0][1], pcs[2][1][1], pcs[0][0][2], pcs[0][1][2])   # bottom, top, back, front
    _req(len(rails) == RAILS, f'{len(rails)} I-beam rails, expected {RAILS}: {sorted(rails)}')
    xs = sorted(rails)
    _req(all(abs(b - a - RAIL_PITCH) < tol for a, b in zip(xs, xs[1:])), f'rails not {RAIL_PITCH} m apart: {xs}')
    fronts, backs = [r[3] for r in rails.values()], [r[2] for r in rails.values()]
    _req(max(fronts) - min(fronts) < tol and max(backs) - min(backs) < tol, 'the rails do not end together')
    bodies = [(lo, hi) for lo, hi in comps if abs(hi[2] - lo[2] - ROCKET_LEN) < 0.05 and hi[0] - lo[0] < 2 * ROCKET_R + tol]
    on: dict[float, list[float]] = {x: [] for x in xs}
    for lo, hi in bodies:
        cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
        rail = [x for x in xs if abs(x - cx) < tol]
        _req(len(rail) == 1, f'a rocket at x {cx:.3f} on no rail')
        bottom, top = rails[rail[0]][:2]
        _req(lo[1] > top - tol or hi[1] < bottom + tol, f'a rocket at x {cx:.3f} cuts its rail')
        on[rail[0]].append(cy)
    _req(len(bodies) == 2 * RAILS and all(len(ys) == 2 and min(ys) < rails[x][0] < rails[x][1] < max(ys) for x, ys in on.items()),
         f'{len(bodies)} rockets, expected one over and one under each of the {RAILS} rails')
    front = sum(fronts) / len(fronts)
    for name, p in (MUZZLES if muzzles is None else muzzles):
        rail = [x for x in xs if abs(x - p[0]) < tol]
        _req(len(rail) == 1 and abs(p[2] - front) < tol and any(abs(y - p[1]) < tol for y in on[rail[0]]),
             f"muzzle {name} {p} is not at a rail's front end on a rocket's axis (rails end at z {front:.3f})")
    return {'rails': len(rails), 'rail_x': xs, 'rail_length': front - sum(backs) / len(backs), 'rail_front': front,
            'rockets': len(bodies), 'rocket_y': sorted({round(y, 3) for ys in on.values() for y in ys})}


def launcher_clearance(md: Mdb, stop_deg: float = PITCH_STOP_DEG, step: int = 5) -> list[tuple[int, float]]:
    """Per elevation 0..stop_deg (every `step` deg): how far the raised launcher's lowest point is over the truck's bed
    floor (m)."""
    main, body = md.bone_index('Rocketcannon_main'), md.bone_index('body')
    w = bind_world(md)
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    floor = truck_geometry(md, 0, body)['bed_floor_y']
    pts = g.skinned_points(md)[main]
    return [(deg, min(launcher_point(p, M, math.radians(deg))[1] for p in pts) - floor)
            for deg in range(0, int(stop_deg) + 1, step)]


# ------------------------------------------------------------------------------------------ build

def build_model(game) -> tuple[Mdb, Mdb, object, object, dict]:  # noqa: ANN001 - rootcpk.Game
    """(new model, stock host model, host Rab, donor Rab, info) - the archive is not assembled yet."""
    host_raw, donor_raw = game.read('OBJECT', HOST_ARC), game.read('OBJECT', DONOR_ARC)
    host_rab, donor_rab = rab_read(host_raw), rab_read(donor_raw)
    host0 = mdb_read(member(host_rab, HOST_MDB).data)
    donor = mdb_read(member(donor_rab, DONOR_MDB).data)
    info: dict = {}

    hb = {host0.name_of(b.name): b.index for b in host0.bones}
    db = {donor.name_of(b.name): b.index for b in donor.bones}
    for n in ['body'] + RACK + ALL_WHEELS + OBJECT_BONES:
        _req(n in hb, f'host bone {n} missing')
    for n in ['body'] + list(WHEEL_MAP):
        _req(n in db, f'donor bone {n} missing')
    rack = g.subtree(host0, hb['Rocketcannon_base'])
    hull_root = hb['body']        # the hull's skin bone (stock: body x4630 + mudguard / tire bones)

    # 1. strip: keep only the rack; object 0 (hull) kept empty for the truck, object 2 (catapi tracks) dropped
    md = g.strip_geometry(host0, rack, drop_empty_objects=False)
    _req([len(o.meshes) for o in md.objects] == [0, 1, 0], 'unexpected stock object / mesh layout')
    md = replace(md, objects=md.objects[:2])
    # 1b. the Naegling's launcher box out, the BM-13 rail pack in (stock binds: the placement below moves it with the rack)
    md = replace_launcher(md, host0, host_rab, info)

    # 2. donor truck in host space: body -> host body, tires -> wheel bones; lowest point onto y = 0, x centred
    bone_map = {db['body']: hull_root} | {db[d]: hb[h] for d, h in WHEEL_MAP.items()}
    sel = lambda k, j, me: (k, j) in DONOR_TRUCK_MESHES  # noqa: E731
    probe = g.extract_meshes(donor, sel, bone_map, TRUCK_SCALE)
    P = [p for _m, me in probe for p in g.mesh_positions(me)]
    xs, ys = [p[0] for p in P], [p[1] for p in P]
    offset: Vec3 = (-(min(xs) + max(xs)) / 2, -min(ys), 0.0)
    truck = g.extract_meshes(donor, sel, bone_map, TRUCK_SCALE, offset)
    info['truck_offset'] = offset

    # 3. wheel bones onto the truck axles: used ones on their tire's centre, the rest on the nearest used axle (same side)
    dw = bind_world(donor)
    centre = {h: tuple(dw[db[d]][12 + c] * TRUCK_SCALE + offset[c] for c in range(3)) for d, h in WHEEL_MAP.items()}
    hw = bind_world(md)
    wheel_pos: dict[str, Vec3] = {}
    for name in ALL_WHEELS:
        if name in centre:
            wheel_pos[name] = centre[name]  # type: ignore[assignment]
        else:
            z0 = hw[hb[name]][14]
            near = min((h for h in centre if h.endswith(name[-1])), key=lambda h: abs(centre[h][2] - z0))
            wheel_pos[name] = centre[near]  # type: ignore[assignment]
    for name, p in wheel_pos.items():
        md = g.set_bone_origin(md, hb[name], p)
    info['wheels'] = wheel_pos

    # 4. donor materials + truck meshes into object 0 (Vehicle402_Rocket, kind-2 bone)
    md, mat_map = g.merge_materials(md, donor, {m for m, _me in truck})
    md = g.append_meshes(md, truck, mat_map, obj=0)

    # 5. rack onto the bed: front (0 elevation) CAB_CLEARANCE behind the cab, main box underside above the rails
    tg = truck_geometry(md, 0, hull_root)
    # Measure first so removing a visual panel cannot lower the rig or change its poses.
    md, info['sideboard_faces_removed'] = remove_sideboards(md, hull_root, offset)
    w = bind_world(md)
    base_o = w[hb['Rocketcannon_base']][12:15]
    ahead = max(p[2] for p in object_positions(md, 1)) - base_o[2]
    main_under = min(p[1] for p in g.skinned_points(md)[hb['Rocketcannon_main']]) - base_o[1]
    new_base = (0.0, max(tg['bed_floor_y'], tg['rail_top_y'] + RAIL_CLEARANCE - main_under),
                tg['cab_z'] - CAB_CLEARANCE - ahead)
    delta: Vec3 = (new_base[0] - base_o[0], new_base[1] - base_o[1], new_base[2] - base_o[2])
    md = g.move_bones(md, rack, delta)
    info['truck'] = tg
    info['rack_delta'] = delta

    # 6. the elevation ram made telescopic: the rod on a bone of its own (the bones from the prop's next renumbered)
    md = split_ram(md, info)

    # 7. bounds of what carries new geometry (body, used wheels, the ram, object bones); relink
    md = g.recompute_bounds(md, {md.bone_index(n) for n in ['body', 'Rocketcannon_main', 'Rocketcannon_prop', RAM_ROD]
                                 + list(centre)})
    md = g.relink(md)
    info['mat_map'] = mat_map
    return md, host0, host_rab, donor_rab, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished KATYUSHA.MRAB (the stock host archive, its model replaced, the donor textures added)."""
    md, _host0, host_rab, donor_rab, info = build_model(game)
    data = mdb_write(md)
    tex_files = sorted({md.textures[x.texture].filename for m in info['mat_map'].values() for x in md.materials[m].textures})
    g.copy_texture_members(host_rab, donor_rab, tex_files)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(host_rab, HOST_MDB).stored = stored
    return rab_write(host_rab)


# ------------------------------------------------------------------------------------------ check

# Fingerprint of the stock Vehicle402_Rocket.mdb skeleton (Root.cpk OBJECT/VEHICLE402_ROCKET.MRAB):
# per bone (name, parent, sibling, child, child_count, kind, depth_delta, bounded, sha256[:16] of the float32
# local + inv_bind (+ half + centre for bones whose bounds are kept)). Bones whose bind is moved carry their stock
# model-space bind instead (rotation rows 0-2 and translation, 5 decimals). Generated by make_fingerprint().
STOCK_BONES: list[tuple] = []          # filled below
STOCK_MOVED: dict[str, list[float]] = {}


def _bone_hash(b, with_bounds: bool) -> str:  # noqa: ANN001 - mdb.Bone
    raw = struct.pack('<32f', *b.local, *b.inv_bind)
    if with_bounds:
        raw += struct.pack('<8f', *b.half, *b.centre)
    return hashlib.sha256(raw).hexdigest()[:16]


def _bounds_kept(name: str) -> bool:
    return name not in ['body'] + OBJECT_BONES + [WHEEL_MAP[d] for d in WHEEL_MAP]


def make_fingerprint(host0: Mdb) -> tuple[list[tuple], dict[str, list[float]]]:
    """(STOCK_BONES, STOCK_MOVED) of a stock host model (used once to generate the constants above)."""
    w = bind_world(host0)
    bones, moved = [], {}
    for b in host0.bones:
        n = host0.name_of(b.name)
        mv = n in RACK or n in ALL_WHEELS
        bones.append((n, b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded,
                      None if mv else _bone_hash(b, _bounds_kept(n))))
        if mv:
            moved[n] = [round(x, 5) for x in w[b.index]]
    return bones, moved


def check(arc: bytes) -> None:
    """Re-read `arc` and raise KatyushaCheckError unless: the archive and model round-trip; < 256 bones; every mesh's
    vertex buffer, index count / range, blend indices (< bone count, skin bones only), weights and material index
    are valid; every material texture (HD and .lod) is an archive member; every bone's bind x inverse bind is the
    identity; the bone list (names, parents, kinds) is the stock one with RAM_ROD after the prop, the links follow the
    parents, every bone outside the rack / wheel bones bit-identical to stock, the rack bones moved by one common
    translation with their stock rotations, the wheel bones at stock rotation, each used one at the centre of its
    tire's vertices; the model sits on y = 0; the ram holds together over 0..PITCH_STOP_DEG (check_ram)."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    nb = len(md.bones)
    _req(nb < 256, f'{nb} bones')
    files = {f.name.lower() for f in rab.files}
    lowest = None
    for o in md.objects:
        _req(0 <= o.bone < nb and md.bones[o.bone].kind == 2, f'object {md.name_of(o.name)} not on a kind-2 bone')
        _req(len(o.meshes) > 0, f'object {md.name_of(o.name)} has no mesh')
        for j, me in enumerate(o.meshes):
            nv = me.nverts
            tag = f'{md.name_of(o.name)} mesh {j}'
            _req(me.vsize > 0 and len(me.vdata) == nv * me.vsize, f'{tag}: vertex buffer size')
            _req(0 < nv < 0x10000, f'{tag}: {nv} vertices')
            _req(len(me.indices) % 6 == 0 and me.indices, f'{tag}: index count')
            idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
            _req(max(idx) < nv, f'{tag}: index {max(idx)} >= {nv} vertices')
            _req(0 <= me.material < len(md.materials), f'{tag}: material {me.material}')
            bi, bw = g.skin_columns(me)
            _req(len(bi) == nv and len(bw) == nv, f'{tag}: skin columns')
            for r, wt in zip(bi, bw):
                _req(all(int(i) < nb for i in r), f'{tag}: blend index >= {nb}')
                _req(md.bones[int(r[0])].kind == 3, f'{tag}: vertex skinned to non-skin bone {int(r[0])}')
                _req(abs(sum(wt) - 1.0) < 1e-3, f'{tag}: weights sum {sum(wt)}')
            low = min(p[1] for p in g.mesh_positions(me))
            lowest = low if lowest is None else min(lowest, low)
    _req(lowest is not None and abs(lowest) < 2e-3, f'lowest vertex y = {lowest}, not on the ground')
    for m in md.materials:
        for x in m.textures:
            _req(0 <= x.texture < len(md.textures), f'material {md.name_of(m.name)}: texture {x.texture}')
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'material {md.name_of(m.name)}: texture member {want} missing')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    # skeleton vs stock: the stock bones in their order with RAM_ROD after the prop; each stock bone's name, parent,
    # kind and bounds flag the stock one's; the links what the parents make of them (relink)
    stock_names = [s[0] for s in STOCK_BONES]
    at = stock_names.index('Rocketcannon_prop') + 1
    names = [md.name_of(b.name) for b in md.bones]
    _req(names == stock_names[:at] + [RAM_ROD] + stock_names[at:], f'bones {names}: not the stock ones with {RAM_ROD}')
    back = lambda i: i - 1 if i > at else i  # noqa: E731 - model index -> stock index (the rod excluded)
    linked = g.relink(md).bones
    deltas = []
    pts = g.skinned_points(md)
    for b in md.bones:
        n = md.name_of(b.name)
        lk = linked[b.index]
        _req((b.sibling, b.child, b.child_count, b.depth_delta) == (lk.sibling, lk.child, lk.child_count, lk.depth_delta),
             f'bone {b.index} {n}: links do not follow its parents')
        if b.index == at:
            continue
        s = STOCK_BONES[back(b.index)]
        _req((n, back(b.parent) if b.parent >= 0 else -1, b.kind, b.bounded) == (s[0], s[1], s[5], s[7]),
             f'bone {b.index} {n}: name / parent / kind differ from stock {s[:8]}')
        if s[8] is not None:
            _req(_bone_hash(b, _bounds_kept(n)) == s[8], f'bone {n}: matrices / bounds differ from stock')
            continue
        ref = STOCK_MOVED[n]
        _req(max(abs(w[b.index][k] - ref[k]) for k in range(12)) < 1e-4, f'bone {n}: rotation differs from stock')
        if n in RACK:
            deltas.append([w[b.index][12 + c] - ref[12 + c] for c in range(3)])
        elif b.index in pts:
            q = pts[b.index]
            mid = [(min(p[c] for p in q) + max(p[c] for p in q)) / 2 for c in range(3)]
            _req(all(abs(mid[c] - w[b.index][12 + c]) < 0.01 for c in range(3)), f'wheel {n}: not at its tire centre')
        else:
            _req(not any(b.index in (int(r[0]) for r in g.skin_columns(me)[0]) for o in md.objects for me in o.meshes),
                 f'wheel {n}: unexpected geometry')
    _req(len(deltas) == len(RACK), 'rack bones missing')
    _req(all(max(abs(d[c] - deltas[0][c]) for c in range(3)) < 1e-4 for d in deltas), 'rack bones moved apart')
    check_ram(md)
    check_launcher(md)
    low = min(launcher_clearance(md), key=lambda r: r[1])
    _req(low[1] >= BED_CLEARANCE, f'the launcher raised {low[0]} deg is {low[1]:.3f} m over the bed (< {BED_CLEARANCE})')
    for n in ALL_WHEELS:
        _req(any(md.bone_index(n) == k for k in pts) == (n in WHEEL_MAP.values()), f'wheel {n}: geometry mismatch')


def check_ram(md: Mdb) -> None:
    """RAM_ROD is a skin bone under Rocketcannon_base with the model's axes, on the prop's axis (the line the ram
    slides along), carrying the rod; and at every 5 deg from 0 to PITCH_STOP_DEG, posed as src/katyusha.cpp poses it
    (ram_report), the rod's front stays inside the cylinder (RAM_OVERLAP from its back end at the most, never past
    its front) and the eye inside the launcher's box: the ram neither parts nor comes off the launcher."""
    names = {md.name_of(b.name): b.index for b in md.bones}
    rod, prop, base = names.get(RAM_ROD, -1), names['Rocketcannon_prop'], names['Rocketcannon_base']
    _req(rod >= 0 and md.bones[rod].parent == base and md.bones[rod].kind == 3, f'{RAM_ROD} not a skin bone under the base')
    w = bind_world(md)
    _req(max(abs(a - b) for a, b in zip(w[rod][:12], (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))) < 1e-5, f"{RAM_ROD}: axes not the model's")
    pts = g.skinned_points(md)
    _req(len(pts.get(rod, [])) > 0 and len(pts.get(prop, [])) > 0, "the ram's cylinder or rod has no geometry")
    P, E = w[prop][12:15], w[rod][12:15]
    axis = _unit((w[prop][0], w[prop][1], w[prop][2]))
    off = [E[c] - P[c] - axis[c] * _dot((E[0] - P[0], E[1] - P[1], E[2] - P[2]), axis) for c in range(3)]
    _req(max(abs(x) for x in off) < 1e-4, f"{RAM_ROD} is off the prop's axis by {off}")
    for r in ram_report(md):
        _req(r['cyl_front'] < r['rod_front'] <= r['cyl_back'] - RAM_OVERLAP + 1e-4,
             f"{r['deg']} deg: the rod's front {r['rod_front']:.3f} m out of the cylinder ({r['cyl_front']:.3f}..{r['cyl_back']:.3f})")
        _req(r['eye_out'] < 1e-3, f"{r['deg']} deg: the ram's eye {r['eye_out']:.3f} m outside the launcher")


STOCK_BONES = [
    ('mdl', -1, -1, 1, 2, 0, 1, 0, '7fe77e43245cd060'),
    ('Vehicle402_Rocket', 0, 4, 2, 2, 2, 1, 0, '1084ffb78a77cdde'),
    ('Vehicle_Rocketcannon', 1, 3, -1, 0, 2, 0, 0, '1084ffb78a77cdde'),
    ('catapi', 1, -1, -1, 0, 2, -1, 0, '70c440320f06c33f'),
    ('body', 0, -1, 5, 30, 3, 1, 1, '1084ffb78a77cdde'),
    ('Rocketcannon_base', 4, 8, 6, 2, 3, 1, 1, None),
    ('Rocketcannon_main', 5, 7, -1, 0, 3, 0, 1, None),
    ('Rocketcannon_prop', 5, -1, -1, 0, 3, -1, 1, None),
    ('catapi_body', 4, 21, 9, 12, 3, 1, 1, '005edd2a14f7d634'),
    ('catapiA_l', 8, 10, -1, 0, 3, 0, 1, 'd1c8c079024a9e6f'),
    ('catapiA_r', 8, 11, -1, 0, 3, 0, 1, 'ada2f94ad95c05b4'),
    ('catapiB_l', 8, 12, -1, 0, 3, 0, 1, 'e94140c41dc03d47'),
    ('catapiB_r', 8, 13, -1, 0, 3, 0, 1, '3d41cb7452be29b0'),
    ('catapiC_l', 8, 14, -1, 0, 3, 0, 1, '002498a76e549e35'),
    ('catapiC_r', 8, 15, -1, 0, 3, 0, 1, 'af569597678aa3cf'),
    ('catapiD_l', 8, 16, -1, 0, 3, 0, 1, '0e04b281d64f5126'),
    ('catapiD_r', 8, 17, -1, 0, 3, 0, 1, 'b6dc00118f50599d'),
    ('catapiE_l', 8, 18, -1, 0, 3, 0, 1, '9ebff6dff33f23cf'),
    ('catapiE_r', 8, 19, -1, 0, 3, 0, 1, '9cb62a2867b829cc'),
    ('catapiF_l', 8, 20, -1, 0, 3, 0, 1, '42e164875f8ab644'),
    ('catapiF_r', 8, -1, -1, 0, 3, -1, 1, 'ef585c4445ac47c1'),
    ('mudGuard_l', 4, 22, -1, 0, 3, 0, 1, 'c556310364ede72a'),
    ('mudGuard_r', 4, 23, -1, 0, 3, 0, 1, '3fe4b4935c4a9a26'),
    ('tire_lockA_l', 4, 24, -1, 0, 3, 0, 1, 'ed8e0d8552b3a3fc'),
    ('tire_lockA_r', 4, 25, -1, 0, 3, 0, 1, '55d4433b37fe9cab'),
    ('tire_lockB_l', 4, 26, -1, 0, 3, 0, 1, 'e72836b6f5ecc4c8'),
    ('tire_lockB_r', 4, 27, -1, 0, 3, 0, 1, '9d8c8d178c8b16af'),
    ('tire_lockC_l', 4, 28, -1, 0, 3, 0, 1, 'b0cacf1b65ff6399'),
    ('tire_lockC_r', 4, 29, -1, 0, 3, 0, 1, 'b88661344c6c8200'),
    ('tire_lockD_l', 4, 30, -1, 0, 3, 0, 1, '681219cd8dcd4687'),
    ('tire_lockD_r', 4, 31, -1, 0, 3, 0, 1, 'c27ba5a1f769f7b3'),
    ('tire_lockE_l', 4, 32, -1, 0, 3, 0, 1, '42ec3e89b2410317'),
    ('tire_lockE_r', 4, 33, -1, 0, 3, 0, 1, '8573caa6042ff499'),
    ('tire_lockF_l', 4, 34, -1, 0, 3, 0, 1, '05f176d78ed98b49'),
    ('tire_lockF_r', 4, 35, -1, 0, 3, 0, 1, '3fda4c6c2a4f3252'),
    ('tire_lockG_l', 4, 36, -1, 0, 3, 0, 1, 'b78707e33dfcd8ec'),
    ('tire_lockG_r', 4, 37, -1, 0, 3, 0, 1, '7933f9ef27292b27'),
    ('tire_moveA_l', 4, 38, -1, 0, 3, 0, 1, None),
    ('tire_moveA_r', 4, 39, -1, 0, 3, 0, 1, None),
    ('tire_moveB_l', 4, 40, -1, 0, 3, 0, 1, None),
    ('tire_moveB_r', 4, 41, -1, 0, 3, 0, 1, None),
    ('tire_moveC_l', 4, 42, -1, 0, 3, 0, 1, None),
    ('tire_moveC_r', 4, 43, -1, 0, 3, 0, 1, None),
    ('tire_moveD_l', 4, 44, -1, 0, 3, 0, 1, None),
    ('tire_moveD_r', 4, 45, -1, 0, 3, 0, 1, None),
    ('tire_moveE_l', 4, 46, -1, 0, 3, 0, 1, None),
    ('tire_moveE_r', 4, 47, -1, 0, 3, 0, 1, None),
    ('tire_moveF_l', 4, 48, -1, 0, 3, 0, 1, None),
    ('tire_moveF_r', 4, -1, -1, 0, 3, 2, 1, None),
]
STOCK_MOVED = {
    'Rocketcannon_base': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.00845, -2.43447, 1.0],
    'Rocketcannon_main': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.36551, -3.33157, 1.0],
    'Rocketcannon_prop': [-0.0, 0.2041, -0.97895, 0.0, 0.0, 0.97895, 0.2041, 0.0, 1.0, 0.0, -0.0, 0.0, -0.00929, 2.34301, -1.8332, 1.0],
    'tire_moveA_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.5285, 2.37727, 1.0],
    'tire_moveA_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.5285, 2.37727, 1.0],
    'tire_moveB_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, 1.38061, 1.0],
    'tire_moveB_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, 1.38061, 1.0],
    'tire_moveC_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, 0.44228, 1.0],
    'tire_moveC_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, 0.44228, 1.0],
    'tire_moveD_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, -0.52622, 1.0],
    'tire_moveD_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, -0.52622, 1.0],
    'tire_moveE_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, -1.46454, 1.0],
    'tire_moveE_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, -1.46454, 1.0],
    'tire_moveF_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.47801, -2.34379, 1.0],
    'tire_moveF_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.47801, -2.34379, 1.0],
}
