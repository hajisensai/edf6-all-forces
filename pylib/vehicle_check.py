"""Checks of a vehicle SGO's car_base_* settings against its SHKT and model, before the game sees them: the masses as
the game sets them, each wheel's suspension compression at rest, each road wheel's size against its body's shape.

The rules follow EDF6 SHKT Editor's VehicleMath / VehicleSettings / SgoChecker (Copyright (c) 2026 Ktaro, MIT License,
pylib/LICENSE.edf6-shkt-editor; https://codeberg.org/Ktaro/EDF6_SHKT_Editor: research/14, spec 3.7 F66), ported to
Python on pylib/shkt_check.py's reader; the thresholds are this module's, set so that every stock vehicle passes
(tests/test_vehicle_check.py).

What the game does with them (that project's analysis of EDF.dll):
  * car_base_all_rigid_body: [0] the vehicle's total mass (the SHKT masses are scaled to it), [1] / [2] linear /
    angular damping, [3] an offset of the chassis's centre of mass (its body space);
  * car_base_rigid_body rows [body, factor, ...]: the body's scaled mass x factor (-1: unchanged);
  * car_base_wheel rows [node, direction, offset, length, length add, spring, damping compression, damping relaxation,
    radius, half width]: a raycast wheel; its spring force is spring x the vehicle's mass x the compression, so the
    compressions at rest carry the weight at the centre of mass whatever the masses;
  * car_base_cast_mode: one entry per wheel or none (the game then casts mode 1).

    vehicle_problems(sgo_doc, shkt_bytes, bone_world) -> list[shkt_check.Problem]
    mass_balance(...), centre_of_mass(...), rest_compression(...), wheel_size(body)
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from struct import error as struct_error
from typing import Callable

import hkshape
import shkt_check as sc
from shkt_check import Problem

Vec3 = tuple[float, float, float]
GRAVITY = 9.8
WHEEL_RADIUS_RATIO = (0.85, 1.75)  # car_base_wheel radius / its body's shape: the stock wheels are 0.94 .. 1.62 (a
                                   # tank's track wheels cast wider than their hulls): only a gross misfit is flagged


def _num(v: object) -> float:
    try:
        return float(v.value if hasattr(v, 'value') else v)   # type: ignore[union-attr, arg-type]
    except (TypeError, ValueError):
        return math.nan


def _vec(v: object) -> Vec3:
    return tuple(_num(v[k]) for k in range(3)) if isinstance(v, list) and len(v) >= 3 else (0.0, 0.0, 0.0)  # type: ignore


@dataclass
class Wheel:
    index: int
    node: str
    direction: Vec3
    offset: float
    length: float
    length_add: float
    spring: float
    radius: float
    half_width: float
    cast_mode: int | None


@dataclass
class Settings:
    total_mass: float               # nan: none
    com_offset: Vec3
    factors: list[tuple[str, float]]
    wheels: list[Wheel]
    names: list[tuple[str, str, bool]]      # (where, name, is a node: a body or a bone)
    has_cast_modes: bool


def settings(doc: dict) -> Settings | None:
    """The car_base_* settings of an SGO (shkt_check.load_plain's dict); None when it has none."""
    keys = [k for k in doc if str(k).startswith('car_base_')]
    if not keys:
        return None
    names: list[tuple[str, str, bool]] = []

    def name(where: str, v: object, node: bool = False) -> None:
        if isinstance(v, str) and v:
            names.append((where, v, node))
    allrb = doc.get('car_base_all_rigid_body')
    total = _num(allrb[0]) if isinstance(allrb, list) and allrb else math.nan
    com = _vec(allrb[3]) if isinstance(allrb, list) and len(allrb) > 3 else (0.0, 0.0, 0.0)
    factors = []
    for i, row in enumerate(doc.get('car_base_rigid_body') or []):
        if isinstance(row, list) and row and isinstance(row[0], str):
            factors.append((row[0], _num(row[1]) if len(row) > 1 else -1.0))
            name(f'car_base_rigid_body[{i}]', row[0])
    casts = doc.get('car_base_cast_mode')
    wheels = []
    for i, w in enumerate(doc.get('car_base_wheel') or []):
        if not (isinstance(w, list) and w and isinstance(w[0], str)):
            continue
        cast = casts[i] if isinstance(casts, list) and i < len(casts) else None
        wheels.append(Wheel(i, w[0], _vec(w[1]) if len(w) > 1 else (0.0, -1.0, 0.0), *(_num(w[k]) if len(w) > k else math.nan
                                                                                     for k in (2, 3, 4, 5, 8, 9)),
                            int(_num(cast)) if cast is not None and not math.isnan(_num(cast)) else None))
        name(f'car_base_wheel[{i}]', w[0], True)
    name('car_base_body_name', doc.get('car_base_body_name'))
    for i, row in enumerate(doc.get('car_base_constraint_data') or []):
        if isinstance(row, list):
            name(f'car_base_constraint_data[{i}][0]', row[0] if row else None)
            name(f'car_base_constraint_data[{i}][1]', row[1] if len(row) > 1 else None)
    parts = doc.get('car_base_breaking_parts')
    if isinstance(parts, list) and len(parts) > 1 and isinstance(parts[1], list):
        for i, row in enumerate(parts[1]):
            name(f'car_base_breaking_parts[1][{i}]', row[0] if isinstance(row, list) and row else None)
    for i, row in enumerate(doc.get('car_base_disable_contact_lisner') or []):
        name(f'car_base_disable_contact_lisner[{i}]', row if isinstance(row, str) else row[0] if isinstance(row, list) and row else None)
    for i, row in enumerate(doc.get('car_base_simulation_node') or []):
        name(f'car_base_simulation_node[{i}]', row[0] if isinstance(row, list) and row else None, True)
    for i, row in enumerate(doc.get('car_base_draw_wheel_info') or []):
        if isinstance(row, list) and row and isinstance(row[0], list) and len(row[0]) > 1:
            name(f'car_base_draw_wheel_info[{i}]', row[0][1], True)
    return Settings(total, com, factors, wheels, names, isinstance(casts, list) and bool(casts))


def bone_proxies(doc: dict) -> dict[str, str]:
    """bone -> body of the SGO's ragdoll binding (the animation_from_ragdoll rows [[body, bone], ...] of the embedded
    SGO after the SHKT path): the game names car_base_* bodies by these bones too (EDF.dll 0x6E7E40)."""
    rd = doc.get('ragdoll')
    inner = sc.load_plain(rd[1]) if isinstance(rd, list) and len(rd) > 1 and isinstance(rd[1], bytes) else None
    out: dict[str, str] = {}
    for row in (inner or {}).get('animation_from_ragdoll') or []:
        names = row[0] if isinstance(row, list) and row else None
        if isinstance(names, list) and len(names) == 2 and all(isinstance(x, str) for x in names):
            out.setdefault(names[1], names[0])
    return out


@dataclass
class Body:
    """What these checks need of a body: its frame, mass, centre of mass (its own space) and, for a road wheel, its
    shape's (radius, half width)."""
    name: str
    position: Vec3
    orientation: tuple[float, float, float, float]
    mass: float
    com: Vec3
    size: tuple[float, float] | None


def bodies(shkt: bytes) -> list[Body]:
    """The bodies of a Havok 2020 tagfile SHKT or of a legacy 2014 one (the EDF4.1 port's, pylib/hk2014.py)."""
    if shkt[:8] == sc.PACKFILE_MAGIC:
        return _legacy_bodies(shkt)
    return [Body(b.name, b.position, b.orientation, b.mass, body_com(b), wheel_size(b))
            for sys_ in sc.Shkt(shkt).systems for b in sys_.bodies]


def _legacy_bodies(data: bytes) -> list[Body]:
    import hk2014
    tf = hk2014.read(data)
    out = []
    for o in tf.objects:
        if o.type.name != 'hkpRigidBody':
            continue
        motion = o.get('motion') or {}
        state = motion.get('motionState') or {}
        inv = motion.get('inertiaAndMassInv') or (0.0, 0.0, 0.0, 0.0)
        t = state.get('transform') or (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0)
        swept = state.get('sweptTransform') or [(0.0, 0.0, 0.0, 0.0)] * 2 + [(0.0, 0.0, 0.0, 1.0)] * 2 + [(0.0,) * 4]
        size = None
        shape = tf.objects[o['collidable']['shape'] - 1] if o.get('collidable', {}).get('shape') else None
        while shape is not None and shape.type.name in ('hkpConvexTranslateShape', 'hkpConvexTransformShape'):
            child = shape.get('childShape') or {}
            shape = tf.objects[child['childShape'] - 1] if isinstance(child, dict) and child.get('childShape') else None
        if shape is not None and shape.type.name == 'hkpCylinderShape':
            a, b = shape.get('vertexA', (0.0,) * 4), shape.get('vertexB', (0.0,) * 4)
            r, cr = shape.get('cylRadius', 0.0), shape.get('radius', 0.0)
            size = (r + cr, math.dist(a[:3], b[:3]) / 2 + cr)
        out.append(Body(str(o.get('name')), (t[12], t[13], t[14]), tuple(swept[2]), 1.0 / inv[3] if inv[3] > 0 else 0.0,
                        tuple(swept[4][:3]), size))
    return out


def find_body(bs: list[Body], name: str, bones: dict[str, str] | None = None) -> Body | None:
    """The body an SGO name means: the same name, the name after a prefix ending with '.' (VehicleMath.FindBody), or
    the body the ragdoll binding draws that bone from (`bones`: bone_proxies)."""
    hit = next((b for b in bs if b.name == name), None) or \
        next((b for b in bs if len(b.name) > len(name) and b.name.endswith('.' + name)), None)
    if hit is None and bones and name in bones:
        hit = next((b for b in bs if b.name == bones[name]), None)
    return hit


def body_com(b: sc.BodyInfo) -> Vec3:
    """A tagfile body's centre of mass in its own space: its massDistribution's, else its shape's mass properties'."""
    if b.mass_distribution is not None:
        return b.mass_distribution.value['massDistribution']['centerOfMassAndVolume'].vec(3)   # type: ignore[return-value]
    m = hkshape.mass_properties(b.shape) if b.shape is not None else None
    return hkshape.read_mass(m).com if m is not None else (0.0, 0.0, 0.0)


def mass_balance(bs: list[Body], v: Settings, bones: dict[str, str] | None = None) -> list[tuple[Body, float]]:
    """(body, its mass in the game) of every body with a mass: the SHKT masses scaled to car_base_all_rigid_body[0],
    then x their car_base_rigid_body factors."""
    massive = [b for b in bs if b.mass > 0]
    total = sum(b.mass for b in massive)
    scale = v.total_mass / total if total > 0 and v.total_mass > 0 else 1.0
    factors = {}
    for n, f in v.factors:
        b = find_body(bs, n, bones)
        if b is not None and f > 0:
            factors[id(b)] = f
    return [(b, b.mass * scale * factors.get(id(b), 1.0)) for b in massive]


def centre_of_mass(bs: list[Body], v: Settings, chassis: str | None, bones: dict[str, str] | None = None) -> Vec3:
    """The vehicle's centre of mass (model space): its bodies' weighted by their masses in the game, the chassis's moved
    by car_base_all_rigid_body[3] (in its body space)."""
    chassis_body = find_body(bs, chassis, bones) if chassis else None
    total, acc = 0.0, [0.0, 0.0, 0.0]
    for b, m in mass_balance(bs, v, bones):
        local = b.com
        if b is chassis_body:
            local = tuple(local[k] + v.com_offset[k] for k in range(3))
        p = sc.place(b.position, b.orientation, local)
        acc = [acc[k] + p[k] * m for k in range(3)]
        total += m
    return tuple(c / total for c in acc) if total > 0 else (0.0, 0.0, 0.0)   # type: ignore[return-value]


def rest_compression(wheels: list[Wheel], position: Callable[[str], Vec3 | None], com: Vec3) -> list[float]:
    """Each wheel's suspension compression at rest (m; negative: it does not touch the ground). With all wheels level,
    c = a + b x + d z (a rigid chassis on vertical springs) carrying the weight at the centre of mass: the normal
    equations of sum k c (1, x, z) = g (1, x0, z0). Wheels whose position is unknown get g / sum k."""
    pos = [position(w.node) for w in wheels]
    k_sum = sum(w.spring for w in wheels if w.spring > 0)
    uniform = GRAVITY / k_sum if k_sum > 0 else math.nan
    known = [(w, p) for w, p in zip(wheels, pos) if p is not None and w.spring > 0]
    coef = None
    if len(known) >= 3:
        m = [[0.0] * 3 for _ in range(3)]
        for w, p in known:
            r = (1.0, p[0], p[2])
            for i in range(3):
                for j in range(3):
                    m[i][j] += w.spring * r[i] * r[j]
        coef = _cramer(m, [GRAVITY, GRAVITY * com[0], GRAVITY * com[2]])
    return [coef[0] + coef[1] * p[0] + coef[2] * p[2] if coef is not None and p is not None else uniform
            for p in pos]


def _cramer(m: list[list[float]], b: list[float]) -> list[float] | None:
    def det(a: list[list[float]]) -> float:
        return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    d = det(m)
    if abs(d) <= 1e-9 * max(abs(m[0][0] * m[1][1] * m[2][2]), 1e-12):
        return None
    out = []
    for c in range(3):
        a = [row[:] for row in m]
        for r in range(3):
            a[r][c] = b[r]
        out.append(det(a) / d)
    return out


def wheel_size(b: sc.BodyInfo) -> tuple[float, float] | None:
    """(radius, half width) of a road wheel body's shape: the largest distance from the axle (model x through the
    shape's centre) and half its extent along it, both with the convex radius; None without a convex shape."""
    pts = sc.pieces(b)
    if not pts:
        return None
    flat = [(p, r) for ps, r in pts for p in ps]
    xs_lo = min(p[0] - r for p, r in flat)
    xs_hi = max(p[0] + r for p, r in flat)
    cy = (min(p[1] for p, _ in flat) + max(p[1] for p, _ in flat)) / 2
    cz = (min(p[2] for p, _ in flat) + max(p[2] for p, _ in flat)) / 2
    return max(math.hypot(p[1] - cy, p[2] - cz) + r for p, r in flat), (xs_hi - xs_lo) / 2


def vehicle_problems(doc: dict, shkt: bytes, bone_world: Callable[[str], Vec3 | None] | None = None,
                     sgo_name: str = 'SGO') -> list[Problem]:
    """Every problem of a vehicle SGO's car_base_* settings against its SHKT (`bone_world`: a model bone's bind
    position, None when unknown: node names are then checked against the SHKT only). The SHKT may be a legacy 2014
    tagfile (the EDF4.1 port's vehicles keep theirs)."""
    v = settings(doc)
    if v is None:
        return []
    bs = bodies(shkt)
    bones = bone_proxies(doc)
    out: list[Problem] = []
    for where, name, node in v.names:
        if find_body(bs, name, bones) is not None:
            continue
        if not node:
            out.append(Problem('error', f'{sgo_name} {where}', f'body {name!r} is not in the SHKT'))
        elif bone_world is not None and bone_world(name) is None:
            out.append(Problem('error', f'{sgo_name} {where}', f'node {name!r} is neither a body of the SHKT nor a bone'))
    casts = sum(w.cast_mode is not None for w in v.wheels)
    if v.has_cast_modes and casts != len(v.wheels):
        out.append(Problem('warning', f'{sgo_name} car_base_cast_mode', f'{casts} entries for {len(v.wheels)} wheels'))
    balance = mass_balance(bs, v, bones)
    if v.wheels and not sum(m for _, m in balance) > 0:
        out.append(Problem('error', f'{sgo_name} car_base_all_rigid_body', 'a vehicle without mass'))
    if not v.wheels:
        return out

    def position(node: str) -> Vec3 | None:
        b = find_body(bs, node, bones)
        if b is not None:
            return b.position
        return bone_world(node) if bone_world is not None else None
    chassis = doc.get('car_base_body_name') if isinstance(doc.get('car_base_body_name'), str) else None
    com = centre_of_mass(bs, v, chassis or 'body', bones)
    for w, c in zip(v.wheels, rest_compression(v.wheels, position, com)):
        where = f'{sgo_name} car_base_wheel[{w.index}] {w.node!r}'
        travel = w.length + w.length_add
        if not math.isfinite(c):
            continue
        if c < 0:
            out.append(Problem('warning', where, f'lifts off at rest (compression {c:.3f} m): the vehicle tips away'))
        elif c >= travel:
            out.append(Problem('warning', where, f'bottoms out at rest (compression {c:.3f} m of {travel:.3f} m travel)'))
        b = find_body(bs, w.node, bones)
        size = b.size if b is not None else None
        if size is not None and w.radius > 0 and not WHEEL_RADIUS_RATIO[0] <= w.radius / size[0] <= WHEEL_RADIUS_RATIO[1]:
            out.append(Problem('warning', where, f'radius {w.radius:.3f} m, its body\'s shape {size[0]:.3f} m'))
    return out


def model_bones(doc: dict, read: Callable[[str], bytes | None]) -> Callable[[str], Vec3 | None] | None:
    """bone -> its bind position (model space) of the SGO's animation_model, `read(ref)` giving 'OBJECT/X.MRAB'
    (shkt_check.shkt_ref's form) or None; None when the model cannot be read."""
    from mdb import bind_world, mdb_read, rab_read
    am = doc.get('animation_model')
    first = am[0] if isinstance(am, list) and am else None
    while isinstance(first, list) and first and isinstance(first[0], list):
        first = first[0]
    if not (isinstance(first, list) and len(first) >= 2 and isinstance(first[0], str) and isinstance(first[1], str)):
        return None
    arc = read(sc.shkt_ref(first[0]))
    if arc is None:
        return None
    try:
        rab = rab_read(arc)
        md = mdb_read(next(f for f in rab.files if f.name.lower() == first[1].lower()).data)
    except (StopIteration, ValueError, KeyError, IndexError, struct_error):
        return None
    w = bind_world(md)
    pos = {md.name_of(b.name): (w[b.index][12], w[b.index][13], w[b.index][14]) for b in md.bones}
    return pos.get


def shkt_of(doc: dict) -> str | None:
    """The SHKT ref ('OBJECT/X.SHKT') of an SGO's ragdoll, None without one."""
    rd = doc.get('ragdoll')
    return sc.shkt_ref(rd[0]) if isinstance(rd, list) and rd and isinstance(rd[0], str) else None
