"""Retractable landing gear for the jet models (pylib/jet_models.py), grafted from stock helicopter gear in the
player's own Root.cpk. Pure Python (pylib: mdb, mdb_jet, graft_pure; no numpy).

    load_donors(game)           -> Donors           the donor model (Root.cpk, read only)
    add_gear(md, spec, donors)  -> (Mdb, textures)  the model (unscaled, ungrounded) with its three gear legs
    check_gear(md, scale)       -> dict             raises GearCheckError on any failure; the numbers it measured
    skin_rigid(md)              -> Mdb              a two-bone rigid model (bomber401) as the skinned skeleton add_gear takes

Donor (read only, Root.cpk OBJECT/): VEHICLE410_HELI.MRAB Vehicle410_heli.mdb, its two front gear legs frontWheelSus_l
(+x) / frontWheelSus_r (-x): an oleo strut, a trailing arm and one wheel each (1.24 m tall, the wheel 0.6 m across),
skinned to one bone each, in one material (MaterialLibrary.helicopter6: the helicopter_body_* textures, copied into the
jet's archive). Each main leg is one of them; the nose leg is both side by side (a twin-wheel nose gear). A leg is
copied whole (its triangles, material, textures), scaled uniformly and moved (no turn, so its normals / tangents stay
valid) so that its wheel touches one ground plane `drop` under the model's lowest point and the line where it enters
the donor's body (the donor body's underside over the strut's top) lies on the jet's underside at the gear's (x, z).
(Gear doors: the stock wells' covers, Vehicle409_heli tailWheelCover_l / _r, are V-shaped shells; on a jet's underside
they either hang under it as a fairing or cut through the folded wheels: left out.)

Bones (children of the body bone, after its other children: the skeleton stays in preorder):
  gear_nose, gear_main_l, gear_main_r     (`_l` = +x, the stock vehicles' convention)
Each is hinged along its local X at the top of its strut, posed by the plugin like the elevons (local' = Rx(theta) x
bind local, row vectors; src/gear.cpp). The bind pose is the gear DOWN. Local Y = model up, local Z = minus the way it
folds: theta = LEG_UP[name] folds it level into the body, the nose aft (NOSE_FOLD), the mains inward.
The model is then grounded by jet_models (its lowest point, the wheels, onto the origin): the collision box measured
off it (vcobjects.jet_sgo) has its bottom on the wheels' contact points.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, replace

import graft_pure as g
from graft_pure import Vec3
from mdb import Bone, Mat, Mdb, Mesh, VElem, bind_world, ident, inverse_affine, mdb_read, mmul, rab_read
from mdb_jet import link, pack_vertex, vertex_table

DONOR_ARC, DONOR_MDB = 'VEHICLE410_HELI.MRAB', 'Vehicle410_heli.mdb'
DONOR_LEG = {1.0: 'frontWheelSus_l', -1.0: 'frontWheelSus_r'}   # the donor leg by the side (sign of x) it is on
DONOR_BODY = 'body'

LEGS = ('gear_nose', 'gear_main_l', 'gear_main_r')
GEAR_BONES = LEGS      # their order in the file
# src/gear.cpp kLegUp (tools/selftest.py holds them equal). A leg folds until its axis (its strut's top to its wheel's
# middle) is level: the donor leg stands 14.3 deg outboard of its top and 0.4 deg forward of it, and every model takes
# it unturned (uniform scale), so the same angles fit every model. check_gear measures each folded axis level.
LEG_UP = {'gear_nose': 1.578, 'gear_main_l': 1.821, 'gear_main_r': 1.821}
NOSE_FOLD = -1.0     # +1: the nose leg folds forward (its wheels toward +z), -1 aft


class GearCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O."""
    if not ok:
        raise GearCheckError(msg)


@dataclass(frozen=True)
class GearSpec:
    """Where a model's gear goes, in its source metres (before jet_models' scale)."""
    nose_z: float        # the nose leg at x = 0, this z ...
    nose_track: float    # ... its two struts' tops at x = +-nose_track ...
    nose_inset: float    # ... its skin line this far inside the jet's underside (a longer leg: room to fold level)
    main_x: float        # the main legs at x = +-main_x ...
    main_z: float        # ... this z
    drop: float          # every wheel this far under the model's lowest vertex


# The stock models' gear. A folded leg lies level at its hinge's height, so its hinge must sit deep enough in the fuselage
# for the wheel to clear the skin under it and shallow enough for the leg not to come out of its top (check_gear holds
# both to 5% of the leg's height; tmp searches over z / inset / drop picked these). bomber501 (the elevon bomber, 30.5 m
# long, its belly -1.29 m at z -6..-8 rising to -0.27 m at the nose): mains on the inner wing under the engine bays
# (underside -0.49 m), behind the middle of the delta, folding inward under the fuselage; the nose gear 8 m ahead of them
# where the fuselage is 2.5 m deep (it is 1.8 m at z 10: too shallow for a leg as long as the mains). bomber401 (16 m
# long, the fuselage's underside 0.13..0.73 m): mains under the wing roots, the nose gear 5.5 m ahead under the nose.
SPECS: dict[str, GearSpec] = {
    'bomber501': GearSpec(nose_z=4.0, nose_track=0.3, nose_inset=0.9, main_x=4.0, main_z=-4.0, drop=1.0),
    'bomber401': GearSpec(nose_z=5.5, nose_track=0.3, nose_inset=1.2, main_x=4.0, main_z=0.0, drop=1.0),
}


@dataclass
class Donors:
    model: Mdb
    rab: object           # mdb.Rab: its archive (the textures)


def load_donors(game) -> Donors:  # noqa: ANN001 - rootcpk.Game
    rab = rab_read(game.read('OBJECT', DONOR_ARC))
    hits = [f for f in rab.files if f.name.lower() == DONOR_MDB.lower()]
    _req(len(hits) == 1, f'{DONOR_ARC}: {len(hits)} members {DONOR_MDB}')
    return Donors(mdb_read(hits[0].data), rab)


# ------------------------------------------------------------------------------------------ geometry

def underside(P: list[Vec3], tris: list[tuple[int, int, int]], x: float, z: float) -> float | None:
    """The lowest y where the vertical line through (x, z) meets the mesh (None: it misses it)."""
    best: float | None = None
    for t in tris:
        a, b, c = P[t[0]], P[t[1]], P[t[2]]
        d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
        if abs(d) < 1e-12:
            continue
        u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d
        v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d
        if u < -1e-6 or v < -1e-6 or u + v > 1.0 + 1e-6:
            continue
        y = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
        best = y if best is None or y < best else best
    return best


def topside(P: list[Vec3], tris: list[tuple[int, int, int]], x: float, z: float) -> float | None:
    """The highest y where the vertical line through (x, z) meets the mesh (None: it misses it)."""
    u = underside([(p[0], -p[1], p[2]) for p in P], tris, x, z)
    return None if u is None else -u


def model_tris(md: Mdb, bones: set[int] | None = None) -> tuple[list[Vec3], list[tuple[int, int, int]]]:
    """Every triangle of `md` in model space (bind pose), as one point list and index triples; `bones`: only skinned
    triangles whose first vertex's first influence is one of these (rigid meshes: their object's bone)."""
    w = bind_world(md)
    P: list[Vec3] = []
    T: list[tuple[int, int, int]] = []
    for o in md.objects:
        for me in o.meshes:
            pts = g.mesh_positions(me)
            if me.flags[1]:
                owner = [int(i[0]) for i in g.skin_columns(me)[0]]
            else:
                pts = [g.xform(p, w[o.bone]) for p in pts]
                owner = [o.bone] * len(pts)
            base = len(P)
            P += pts
            T += [(base + a, base + b, base + c) for a, b, c in g.triangles(me) if bones is None or owner[a] in bones]
    return P, T


@dataclass(frozen=True)
class Part:
    """A donor leg's shape: its wheel's lowest y, the top of its strut (the leg's hinge) and the y where it enters its
    donor's body (`skin`)."""
    bone: int
    low: float
    top: Vec3
    skin: float


def part_points(md: Mdb, bone: int) -> list[Vec3]:
    return g.skinned_points(md).get(bone, [])


def leg_part(md: Mdb, name: str) -> Part:
    """The donor leg `name`: its strut's top (the mean x, z of its vertices within 5% of its height of the top) and
    the donor body's underside over that top."""
    bone = g.bone_by_name(md, name)
    P = part_points(md, bone)
    _req(len(P) > 50, f'{name}: {len(P)} vertices')
    lo, hi = min(p[1] for p in P), max(p[1] for p in P)
    near = [p for p in P if p[1] >= hi - 0.05 * (hi - lo)]
    top = (sum(p[0] for p in near) / len(near), hi, sum(p[2] for p in near) / len(near))
    BP, BT = model_tris(md, {g.bone_by_name(md, DONOR_BODY)})
    skin = underside(BP, BT, top[0], top[2])
    _req(skin is not None and lo < skin < hi, f'{name}: no donor body over the strut ({skin})')
    return Part(bone, lo, top, skin)  # type: ignore[arg-type]


# ------------------------------------------------------------------------------------------ bones

def frame(origin: Vec3, z_axis: Vec3) -> Mat:
    """A bone's bind (model space): Y = model up, Z = `z_axis` (level), X = Y x Z, at `origin`."""
    y = (0.0, 1.0, 0.0)
    z = z_axis
    x = (y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2], y[0] * z[1] - y[1] * z[0])
    return [x[0], x[1], x[2], 0.0, 0.0, 1.0, 0.0, 0.0, z[0], z[1], z[2], 0.0, origin[0], origin[1], origin[2], 1.0]


def insert_bones(md: Mdb, parent: int, new: list[tuple[str, Mat]]) -> tuple[Mdb, list[int]]:
    """`md` with skin bones (kind 3, bounded) under `parent`, bind matrices `new` (name, model-space bind), placed
    right after `parent`'s subtree (preorder kept): later bones, object bones and parents renumbered. Blend indices
    must all lie before the insertion (checked: none is renumbered). Returns the model and the new bones' indices."""
    at = max(g.subtree(md, parent)) + 1
    n = len(new)

    def moved(i: int) -> int:
        return i if i < at else i + n
    for o in md.objects:
        for me in o.meshes:
            if me.flags[1]:
                bi, _bw = g.skin_columns(me)
                _req(all(int(x) < at for i4 in bi for x in i4), 'a skinned vertex on a bone the insertion moves')
    names = list(md.names)
    w = bind_world(md)
    bones = [replace(b, index=moved(b.index), parent=moved(b.parent) if b.parent >= 0 else -1) for b in md.bones]
    added = []
    for k, (name, bind) in enumerate(new):
        local = mmul(bind, inverse_affine(w[parent]))
        added.append(Bone(at + k, parent, -1, -1, g._name_index(names, name), 0, 3, 0, 1, 0, 0, local,
                          inverse_affine(bind), [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    bones = bones[:at] + added + bones[at:]
    link(bones)
    objects = [replace(o, bone=moved(o.bone)) for o in md.objects]
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None), list(range(at, at + n))


def skin_rigid(md: Mdb) -> Mdb:
    """A stock two-bone rigid model (mdl -> X, kind 1, identity bind, one object on X with rigid meshes) as the
    elevon bomber's skeleton (mdb_jet.build): mdl -> X (kind 3, skinning its vertices) and X_mesh (kind 2, identity,
    the object's bone) under mdl; every mesh skinned to X with one influence (BLENDWEIGHT float4, BLENDINDICES
    ubyte4 appended to its layout, as mdb_jet.build does). Positions unchanged: X's bind is identity."""
    _req(len(md.bones) == 2 and md.bones[1].parent == 0 and md.bones[1].kind == 1, 'not a two-bone rigid model')
    _req(max(abs(a - b) for a, b in zip(bind_world(md)[1], ident())) < 1e-6, 'its body bone is not at identity')
    _req(len(md.objects) == 1 and md.objects[0].bone == 1 and all(not me.flags[1] for me in md.objects[0].meshes),
         'not one rigid object on its body bone')
    names = list(md.names)
    body = md.bones[1]
    mesh_name = g._name_index(names, md.name_of(body.name) + '_mesh')
    bones = [replace(md.bones[0]), replace(body, kind=3, bounded=1),
             Bone(2, 0, -1, -1, mesh_name, 0, 2, 0, 0, 0, 0, ident(), ident(), list(body.half), list(body.centre))]
    link(bones)
    meshes = []
    for me in md.objects[0].meshes:
        elems = list(me.elems) + [VElem(1, me.vsize, 0, 'BLENDWEIGHT'), VElem(21, me.vsize + 16, 0, 'BLENDINDICES')]
        _keys, rows = vertex_table(me)
        vsize = me.vsize + 20
        vdata = b''.join(pack_vertex(elems, vsize, r + [(1.0, 0.0, 0.0, 0.0), (1, 0, 0, 0)]) for r in rows)
        meshes.append(replace(me, flags=bytes([0, 1, 1, 0]), vsize=vsize, elems=elems, vdata=vdata))
    out = replace(md, names=names, bones=bones, objects=[replace(md.objects[0], bone=2, meshes=meshes)], buffer_order=None)
    return g.recompute_bounds(out, {1})


# ------------------------------------------------------------------------------------------ graft

@dataclass(frozen=True)
class Placed:
    name: str
    scale: float
    parts: tuple[tuple[int, Vec3], ...]   # (donor bone, offset): donor point p -> p * scale + offset
    bind: Mat                              # the new bone's bind (model space)


def body_bone(md: Mdb) -> int:
    """The bone the model's body is skinned to: the root's one skin child (bomber501 / bomber401)."""
    kids = [b.index for b in md.bones if b.parent == 0 and b.kind == 3]
    _req(len(kids) == 1, f'body bone: {kids}')
    return kids[0]


def plan(md: Mdb, spec: GearSpec, d: Donors) -> list[Placed]:
    """Where each donor leg goes in `md` (see the module doc)."""
    P, T = model_tris(md)
    ground = min(p[1] for p in P) - spec.drop
    legs = {sign: leg_part(d.model, DONOR_LEG[sign]) for sign in (1.0, -1.0)}

    def at(x: float, z: float) -> float:
        y = underside(P, T, x, z)
        _req(y is not None, f'no underside at ({x}, {z})')
        return y  # type: ignore[return-value]

    def placed(leg: Part, x: float, z: float, skin: float) -> tuple[float, Vec3]:
        """(scale, offset) putting `leg`'s strut top over (x, z), its skin line at `skin`, its wheel on the ground."""
        s = (skin - ground) / (leg.skin - leg.low)
        return s, (x - leg.top[0] * s, skin - leg.skin * s, z - leg.top[2] * s)

    out: list[Placed] = []
    # the nose: both donor legs side by side, their skin line nose_inset over the underside at the centre line, one hinge
    skin = at(0.0, spec.nose_z) + spec.nose_inset
    parts = []
    s = 1.0
    for sign, leg in legs.items():
        s, off = placed(leg, sign * spec.nose_track, spec.nose_z, skin)
        parts.append((leg.bone, off))
    top_y = legs[1.0].top[1] * s + parts[0][1][1]
    out.append(Placed('gear_nose', s, tuple(parts), frame((0.0, top_y, spec.nose_z), (0.0, 0.0, -NOSE_FOLD))))
    for side, sign in (('l', 1.0), ('r', -1.0)):
        leg = legs[sign]
        x = sign * spec.main_x
        s, off = placed(leg, x, spec.main_z, at(x, spec.main_z))
        top = (x, leg.top[1] * s + off[1], spec.main_z)
        out.append(Placed(f'gear_main_{side}', s, ((leg.bone, off),), frame(top, (sign, 0.0, 0.0))))
    _req(tuple(p.name for p in out) == GEAR_BONES, 'gear order')
    return out


def add_gear(md: Mdb, spec: GearSpec, d: Donors) -> tuple[Mdb, list[str]]:
    """`md` (one skinned object, its body bone the root's skin child) with the gear (see the module doc); and the
    donor texture files its new material uses (to copy into the archive: graft_pure.copy_texture_members)."""
    _req(len(md.objects) == 1 and all(me.flags[1] for me in md.objects[0].meshes), 'not one skinned object')
    _req(all(md.bone_index(n) < 0 for n in GEAR_BONES), 'the model has gear already')
    places = plan(md, spec, d)
    md, idx = insert_bones(md, body_bone(md), [(p.name, p.bind) for p in places])
    meshes: list[tuple[int, Mesh]] = []
    for p, i in zip(places, idx):
        for part, off in p.parts:
            got = g.extract_meshes(d.model, lambda _o, _m, _me: True, {part: i}, p.scale, off)
            _req(bool(got), f'{p.name}: nothing taken from the donor')
            meshes += got
    used = sorted({m for m, _me in meshes})
    md, mat_map = g.merge_materials(md, d.model, used)
    first = len(md.objects[0].meshes)
    md = g.append_meshes(md, meshes, mat_map, obj=0)
    o = md.objects[0]
    md = replace(md, objects=[replace(o, meshes=o.meshes[:first] + [replace(me, mesh_index=first + k)
                                                                   for k, me in enumerate(o.meshes[first:])])])
    textures = sorted({d.model.textures[x.texture].filename for m in used for x in d.model.materials[m].textures},
                      key=str.lower)
    return g.recompute_bounds(md, set(idx)), textures


# ------------------------------------------------------------------------------------------ posing / checks

def rot_x(theta: float) -> Mat:
    c, s = math.cos(theta), math.sin(theta)
    return [1.0, 0.0, 0.0, 0.0, 0.0, c, s, 0.0, 0.0, -s, c, 0.0, 0.0, 0.0, 0.0, 1.0]


def posed_points(md: Mdb, angles: dict[str, float]) -> dict[int, list[Vec3]]:
    """Skinned vertices per first-influence bone with the bones named in `angles` posed local' = Rx(theta) x local
    (src/gear.cpp's pose), the rest at bind."""
    world: list[Mat] = []
    for b in md.bones:
        local = b.local
        th = angles.get(md.name_of(b.name))
        if th is not None:
            local = mmul(rot_x(th), local)
        world.append(local if b.parent < 0 else mmul(local, world[b.parent]))
    out: dict[int, list[Vec3]] = {}
    for bone, pts in g.skinned_points(md).items():
        m = mmul(md.bones[bone].inv_bind, world[bone])
        out[bone] = [g.xform(p, m) for p in pts]
    return out


def outside(Q: list[Vec3], P: list[Vec3], T: list[tuple[int, int, int]]) -> tuple[float, float]:
    """How far the points `Q` reach under the body's underside (P, T) and over its top, each over or under it (0: none
    does; a point with no body over or under it counts from the body's lowest / highest point)."""
    floor, roof = min(p[1] for p in P), max(p[1] for p in P)
    below = above = 0.0
    for q in Q:
        u, t = underside(P, T, q[0], q[2]), topside(P, T, q[0], q[2])
        below = max(below, (floor if u is None else u) - q[1])
        above = max(above, q[1] - (roof if t is None else t))
    return below, above


def check_gear(md: Mdb, scale: float = 1.0) -> dict[str, object]:
    """The gear of a finished model (after jet_models' scale and grounding, `scale` that scale): every bone's bind x
    inverse bind is identity; the gear bones are skin bones of the body bone, in GEAR_BONES order right after its other
    children; the three legs' wheels are the model's lowest points, all at the same height (one ground plane), and
    nothing else reaches within 0.1 m x scale of it; LEG_UP folds each leg level (its axis within 5 deg) the way it
    should (the nose along NOSE_FOLD, the mains inward), into the body (no folded vertex more than 5% of the leg's
    height under the underside or over the top over it). Returns what it measured (model metres)."""
    w = bind_world(md)
    for b in md.bones:
        err = max(abs(x - y) for x, y in zip(mmul(w[b.index], b.inv_bind), ident()))
        _req(err < 1e-4, f'{md.name_of(b.name)}: bind x inverse bind off by {err}')
    body = body_bone(md)
    idx = [md.bone_index(n) for n in GEAR_BONES]
    _req(all(i > 0 for i in idx), f'gear bones missing: {idx}')
    _req(idx == list(range(idx[0], idx[0] + len(idx))) and idx[0] - 1 in g.subtree(md, body), 'gear bones not in place')
    _req(all(md.bones[i].parent == body and md.bones[i].kind == 3 and md.bones[i].bounded == 1 for i in idx),
         'gear bone kind')
    pts = g.skinned_points(md)
    low_all = min(p[1] for v in pts.values() for p in v)
    body_low = min(p[1] for b, v in pts.items() if b not in idx for p in v)
    rep: dict[str, object] = {'ground_y': round(low_all, 4), 'body_clearance': round(body_low - low_all, 4)}
    _req(body_low > low_all + 0.1 * scale, f'the body reaches the ground plane ({body_low} vs {low_all})')
    up = posed_points(md, LEG_UP)
    BP, BT = model_tris(md, set(range(len(md.bones))) - set(idx))
    for n in LEGS:
        i = md.bone_index(n)
        P, Q = pts[i], up[i]
        lo = min(p[1] for p in P)
        size = max(p[1] for p in P) - lo
        _req(abs(lo - low_all) < 2e-3 * max(scale, 1.0), f'{n}: wheels at {lo}, the ground plane at {low_all}')
        contact = [p for p in P if p[1] < lo + 0.03 * scale]   # the tyres' contact patches
        hinge = w[i][12:15]
        below, above = outside(Q[::3], BP, BT)
        _req(below < 0.05 * size and above < 0.05 * size, f'{n}: folded, {below:.2f} m under the skin, {above:.2f} m over it')
        mid_dn = [sum(p[c] for p in P) / len(P) for c in range(3)]
        mid_up = [sum(p[c] for p in Q) / len(Q) for c in range(3)]
        wheels = [k for k, p in enumerate(P) if p[1] < lo + 0.4 * size]
        axis = [sum(Q[k][c] for k in wheels) / len(wheels) - hinge[c] for c in range(3)]
        tilt = math.degrees(math.atan2(axis[1], math.hypot(axis[0], axis[2])))
        _req(abs(tilt) < 5.0, f'{n}: folded {tilt:.1f} deg off level')
        if n == 'gear_nose':
            _req((mid_up[2] - mid_dn[2]) * NOSE_FOLD > 0.3 * size, f'{n}: does not fold along NOSE_FOLD')
        else:
            _req(abs(mid_up[0]) < abs(mid_dn[0]) - 0.3 * size, f'{n}: does not fold inward')
        rep[n] = {'contact_x': [round(min(p[0] for p in contact), 3), round(max(p[0] for p in contact), 3)],
                  'contact_z': [round(min(p[2] for p in contact), 3), round(max(p[2] for p in contact), 3)],
                  'contact_y': round(lo, 4), 'hinge': [round(c, 3) for c in hinge], 'height': round(size, 3),
                  'folded_under_skin': round(below, 3), 'folded_over_top': round(above, 3),
                  'folded_tilt_deg': round(tilt, 2)}
    return rep
