"""Checks of an object SHKT (Havok physics tagfile) and of the SGOs that bind to it, before the game sees them.

The checks and their thresholds follow EDF6 SHKT Editor's ShktChecker / SgoChecker / OverlapChecker, its format
report and its manual (Copyright (c) 2026 Ktaro, MIT License, pylib/LICENSE.edf6-shkt-editor;
https://codeberg.org/Ktaro/EDF6_SHKT_Editor: EDF6_SHKT_Format_Spec §5, Usage Analysis §1, the Manual), re-implemented
on pylib/hkfile.py / pylib/hkshape.py.

What the game does with a SHKT (from that report, the parts these checks rely on):
  * bodyCinfo.name is the only link to the SGO. A name an SGO mapping row (animation_from_ragdoll /
    ragdoll_from_animation of the embedded SGO after the SHKT path) or an accessory part uses that the SHKT does
    not have crashes the game before the mission starts (in-game).
  * materialId / motionPropertiesId index the system's arrays (0xFFFF = none, static bodies only).
  * A joint's pivot in body A (transformA) and in body B (transformB) coincide in model space: at most 0.8 mm over
    all 3,051 stock joints; the editor warns from 1 cm.
  * Shapes carry derived data Havok computed when the file was written: a convex hull's planes bound its vertices,
    a compound's aabb and simd tree bound its instances. Stale derived data mis-collides without any error.
  * A body whose massDistribution is null gets it from its shape's mass properties (hknpShapeProperties key
    0xF100). Every stock shape (11,281 in OBJECT) stores them; such a body over a shape without them is unlike any
    file the game ships (what the game then does is not measured: warning). A body with its own massDistribution
    (all of pylib/aircraft_collision.py's keyframed airframe bodies) does not read them.
  * collisionFilterInfo: bits 0-4 layer (overwritten by the game), 5-9 subsystem, 10-14 "don't collide with",
    15-30 system group; all bodies of one file share one system group.

    problems(shkt) -> list[Problem]                 the SHKT on its own
    sgo_problems(sgo_bytes, shkt_path, body_names)  body names an SGO binds to `shkt_path` that are missing
    python -B pylib/shkt_check.py FILE.SHKT ...     print the problems of each file (tools/check_shkt.py does a game)

Legacy Havok 2014 binary tagfiles (magic 1E0DB0CA CEFA11D0: the EDF4.1 port ships its ragdolls as they are, EDF.dll
keeps the old readers) are read with pylib/hk2014.py: their hkpRigidBody names are what an SGO binds to (body_names),
and problems() checks what that format has of the rules above (duplicate / empty body names, joints between bodies of
the file). Their shapes are not checked (hkp shapes, another layout).
"""
from __future__ import annotations

import math
import struct
import sys
from dataclasses import dataclass

import dsgo
import hk2014
import hkfile
import hkshape
import sgo

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]      # x, y, z, w (Havok's order)

PACKFILE_MAGIC = hk2014.MAGIC                 # a legacy Havok 2014 binary tagfile (the EDF4.1 port's ragdolls)
MASS_PROPERTIES_KEY = 0xF100
NO_ID = 0xFFFF
STATIC, DYNAMIC = 0, 1                        # hknpMotionType
PIVOT_TOLERANCE = 0.01                        # m: the editor's limit; the stock joints agree to 0.8 mm
PLANE_TOLERANCE = 1e-3                        # m a hull vertex may stand outside one of its planes, at least
PLANE_RELATIVE = 0.02                         # ... or this share of the hull's extent: Havok's planes are simplified
                                              # (stock: up to 0.118 m on a 6.7 m hull, 1.75 % of the extent at most)
UNIT_TOLERANCE = 1e-3                         # |plane normal| - 1
BOX_TOLERANCE = 1e-3                          # m a box may miss what it should hold (float32 storage)
B = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
TRANSFORM_ATOMS = ('transforms', 'suspensionBase')   # the hkpSetLocalTransformsConstraintAtom of each joint type


@dataclass(frozen=True)
class Problem:
    severity: str     # 'error': the game crashes or the data is invalid; 'warning': unlike every stock file
    where: str
    what: str

    def __str__(self) -> str:
        return f'{self.severity}: {self.where}: {self.what}'


@dataclass
class BodyInfo:
    index: int
    name: str
    shape: hkfile.Block | None
    mass: float
    filter_info: int
    material_id: int
    motion_properties_id: int
    motion_type: int
    position: Vec3
    orientation: Quat
    mass_distribution: hkfile.Block | None     # None: the game derives it from the shape
    body: hkfile.Body | None = None


@dataclass
class JointInfo:
    index: int
    kind: str
    body_a: int
    body_b: int
    pivot_a: Vec3 | None       # in body A's frame (None: the joint type has no local transforms)
    pivot_b: Vec3 | None


@dataclass
class SystemInfo:
    index: int
    materials: int
    motion_properties: int
    bodies: list[BodyInfo]
    joints: list[JointInfo]


# ------------------------------------------------------------------------------------------ math

def rot(q: Quat, v: Vec3) -> Vec3:
    """v turned by the unit quaternion q."""
    x, y, z, w = q
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + y * tz - z * ty, v[1] + w * ty + z * tx - x * tz, v[2] + w * tz + x * ty - y * tx)


def place(t: Vec3, q: Quat, v: Vec3) -> Vec3:
    r = rot(q, v)
    return (t[0] + r[0], t[1] + r[1], t[2] + r[2])


def dist(a: Vec3, b: Vec3) -> float:
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))


def hull_outside(vertices: list[Vec3], planes: list[tuple[float, float, float, float]]) -> float:
    """How far (m) the worst vertex stands outside the worst plane (n . v + d <= 0 inside); <= 0: all inside."""
    worst = -math.inf
    for nx, ny, nz, d in planes:
        for v in vertices:
            worst = max(worst, nx * v[0] + ny * v[1] + nz * v[2] + d)
    return worst


def box_of(points: list[Vec3], grow: float = 0.0) -> tuple[Vec3, Vec3]:
    return (tuple(min(p[k] for p in points) - grow for k in range(3)),     # type: ignore[return-value]
            tuple(max(p[k] for p in points) + grow for k in range(3)))


def box_misses(outer: tuple[Vec3, Vec3], inner: tuple[Vec3, Vec3], tol: float = BOX_TOLERANCE) -> float:
    """How far (m) `inner` sticks out of `outer`; <= 0: inside."""
    return max(max(outer[0][k] - inner[0][k], inner[1][k] - outer[1][k]) for k in range(3)) - tol


# ------------------------------------------------------------------------------------------ reading

class Shkt:
    """The parts of an object SHKT these checks read (pylib/hkfile.py's graph: `physics` its systems, shapes are
    blocks)."""

    def __init__(self, data: bytes) -> None:
        self.physics = hkfile.Physics.read(bytes(data))
        self.tf = self.physics.tf
        self.systems: list[SystemInfo] = []
        for k, system in enumerate(self.physics.systems):
            bodies = [BodyInfo(b.index, b.name, b.shape, b.mass, b.filter_info, b.material_id, b.motion_properties_id,
                               b.motion_type, b.position, b.orientation, b.mass_distribution, b)
                      for b in system.bodies]
            joints = [JointInfo(j.index, j.kind, j.body_a, j.body_b, j.pivot('A'), j.pivot('B')) for j in system.joints]
            self.systems.append(SystemInfo(k, system.materials, system.motion_properties, bodies, joints))

    @staticmethod
    def hull(shape: hkfile.Block) -> tuple[float, list[Vec3], list[tuple[float, float, float, float]]] | None:
        return hkshape.convex_hull(shape)

    @staticmethod
    def instance_box(inst: tuple[Quat, Vec3, Vec3, hkfile.Block | None]) -> tuple[Vec3, Vec3] | None:
        """The box of an instance's convex shape in the compound's frame (its convex radius included)."""
        q, tr, sc, shape = inst
        h = hkshape.convex_hull(shape) if shape is not None else None
        if h is None or not h[1]:
            return None
        radius, verts, _ = h
        pts = [place(tr, q, (v[0] * sc[0], v[1] * sc[1], v[2] * sc[2])) for v in verts]
        return box_of(pts, radius)


# ------------------------------------------------------------------------------------------ checks

def _body_problems(sys_: SystemInfo, out: list[Problem]) -> None:
    seen: dict[str, int] = {}
    for b in sys_.bodies:
        w = f'system {sys_.index} body {b.index} {b.name!r}'
        if b.name in seen:
            out.append(Problem('error', w, f'the same name as body {seen[b.name]}: the SGO cannot tell them apart'))
        seen.setdefault(b.name, b.index)
        if b.shape is None:
            out.append(Problem('error', w, 'no shape'))
        if b.material_id >= sys_.materials:
            out.append(Problem('error', w, f'materialId {b.material_id} of {sys_.materials} materials'))
        if b.motion_properties_id >= sys_.motion_properties and \
                not (b.motion_properties_id == NO_ID and b.motion_type == STATIC):
            out.append(Problem('error', w, f'motionPropertiesId {b.motion_properties_id} of '
                                           f'{sys_.motion_properties} motion properties'))
        if b.motion_type == DYNAMIC and not b.mass > 0:
            out.append(Problem('error', w, f'a dynamic body with mass {b.mass}'))
        if b.filter_info & 0x1F:
            out.append(Problem('warning', w, f'collision layer {b.filter_info & 0x1F} (0 in every stock file; '
                                             'the game overwrites it)'))
    groups = {b.filter_info >> 15 for b in sys_.bodies}
    if len(groups) > 1:
        out.append(Problem('warning', f'system {sys_.index}', f'bodies in system groups {sorted(groups)} '
                                                             '(one group per file in every stock file)'))


def _joint_problems(sys_: SystemInfo, out: list[Problem]) -> None:
    n = len(sys_.bodies)
    for j in sys_.joints:
        w = f'system {sys_.index} joint {j.index} ({j.kind})'
        if not (j.body_a < n and j.body_b < n):
            out.append(Problem('error', w, f'bodies {j.body_a}, {j.body_b} of {n}'))
            continue
        if j.body_a == j.body_b:
            out.append(Problem('error', w, f'joins body {j.body_a} to itself'))
        if j.pivot_a is None or j.pivot_b is None:
            continue
        A, Bd = sys_.bodies[j.body_a], sys_.bodies[j.body_b]
        gap = dist(place(A.position, A.orientation, j.pivot_a), place(Bd.position, Bd.orientation, j.pivot_b))
        if gap > PIVOT_TOLERANCE:
            out.append(Problem('error', w, f'{A.name!r} and {Bd.name!r} put the pivot {gap:.3f} m apart '
                                           f'(stock joints: at most 0.0008 m)'))


def _hull_problems(shape: hkfile.Block, w: str, out: list[Problem]) -> None:
    h = hkshape.convex_hull(shape)
    if h is None:
        return
    _, verts, planes = h
    if not verts:
        out.append(Problem('error', w, 'a convex shape without vertices'))
        return
    for k, p in enumerate(planes):
        if abs(math.sqrt(p[0] ** 2 + p[1] ** 2 + p[2] ** 2) - 1.0) > UNIT_TOLERANCE:
            out.append(Problem('error', w, f'plane {k} normal is not unit length'))
            break
    if planes:
        off = hull_outside(verts, planes)
        extent = max(max(v[k] for v in verts) - min(v[k] for v in verts) for k in range(3))
        if off > max(PLANE_TOLERANCE, PLANE_RELATIVE * extent):
            out.append(Problem('error', w, f'a hull vertex {off:.4f} m outside its planes on a {extent:.2f} m hull '
                                           '(stale planes)'))


def _compound_problems(shape: hkfile.Block, w: str, out: list[Problem]) -> list[hkfile.Block | None]:
    """Bound checks of a compound of convex shapes; returns its instances' shapes for the walk."""
    insts = hkshape.instances(shape)
    boxes = [Shkt.instance_box(i) for i in insts]
    aabb, simd = hkshape.compound_bounds(shape)
    for k, box in enumerate(boxes):
        if box is not None and box_misses(aabb, box) > 0:
            out.append(Problem('error', w, f'instance {k} sticks {box_misses(aabb, box):.4f} m out of the aabb'))
    if simd is not None:
        nodes = hkshape.simd_lanes(simd)

        def union(i: int) -> tuple[Vec3, Vec3] | None:
            lanes = nodes[i][1] if 0 <= i < len(nodes) else []
            if not lanes:
                return None
            return (tuple(min(l[0][k] for l in lanes) for k in range(3)),     # type: ignore[return-value]
                    tuple(max(l[1][k] for l in lanes) for k in range(3)))
        for i, (leaf, lanes) in enumerate(nodes):
            for lo, hi, data in lanes:
                if leaf:
                    box = boxes[data] if data < len(boxes) else None
                    if data >= len(boxes):
                        out.append(Problem('error', w, f'simd node {i} names instance {data} of {len(boxes)}'))
                    elif box is not None and box_misses((lo, hi), box) > 0:
                        out.append(Problem('error', w, f'simd node {i} misses instance {data} by '
                                                       f'{box_misses((lo, hi), box):.4f} m (stale tree)'))
                else:
                    child = union(data)
                    if child is not None and box_misses((lo, hi), child) > 0:
                        out.append(Problem('error', w, f'simd node {i} misses its child node {data}'))
    return [i[3] for i in insts]


def legacy_bodies(data: bytes) -> list[str]:
    """The hkpRigidBody names of a legacy Havok 2014 tagfile, in file order (static bodies included: an SGO may bind
    to any of them)."""
    return [str(o.get('name') or '') for o in hk2014.read(data).objects if o.type.name == 'hkpRigidBody']


def _legacy_problems(data: bytes) -> list[Problem]:
    tf = hk2014.read(data)
    out: list[Problem] = []
    number = {id(o): k + 1 for k, o in enumerate(tf.objects)}
    seen: dict[str, int] = {}
    bodies = set()
    for k, o in enumerate(tf.objects):
        if o.type.name != 'hkpRigidBody':
            continue
        bodies.add(number[id(o)])
        name = str(o.get('name') or '')
        w = f'object {k + 1} body {name!r}'
        if not name:
            out.append(Problem('error', w, 'a body without a name: the SGO cannot bind it'))
        elif name in seen:
            out.append(Problem('error', w, f'the same name as object {seen[name]}: the SGO cannot tell them apart'))
        seen.setdefault(name, k + 1)
    for k, o in enumerate(tf.objects):
        if o.type.name != 'hkpConstraintInstance':
            continue
        ents = list(o.get('entities') or [])        # a tuple of two object numbers (0: the world)
        if any(e and e not in bodies for e in ents):
            out.append(Problem('error', f'object {k + 1} joint', f'joins objects {ents}, not all rigid bodies'))
        elif len(ents) == 2 and ents[0] and ents[0] == ents[1]:
            out.append(Problem('error', f'object {k + 1} joint', f'joins body object {ents[0]} to itself'))
    return out


def problems(data: bytes) -> list[Problem]:
    """Every problem of one SHKT on its own (no SGO)."""
    if data[:8] == PACKFILE_MAGIC:
        return _legacy_problems(data)
    s = Shkt(data)
    out: list[Problem] = []
    seen: set[int] = set()
    for sys_ in s.systems:
        _body_problems(sys_, out)
        _joint_problems(sys_, out)
        for b in sys_.bodies:
            if b.shape is not None and b.mass_distribution is None and hkshape.mass_properties(b.shape) is None:
                out.append(Problem('warning', f'body {b.name!r}', 'no massDistribution and its shape stores no mass '
                                                                  'properties (every stock shape has them)'))
            stack = [(b.shape, f'body {b.name!r}')]
            while stack:
                shape, w = stack.pop()
                if shape is None or id(shape) in seen:
                    continue
                seen.add(id(shape))
                kind = shape.type.full_name
                if kind == 'hknpCompoundShape':
                    for k, sub in enumerate(_compound_problems(shape, f'{w} compound', out)):
                        stack.append((sub, f'{w} instance {k}'))
                else:
                    _hull_problems(shape, f'{w} {kind}', out)
    return out


def body_names(data: bytes) -> set[str]:
    """Every body name of a SHKT (a Havok 2020 tagfile or a legacy 2014 one)."""
    if data[:8] == PACKFILE_MAGIC:
        return set(legacy_bodies(data))
    return {b.name for sys_ in Shkt(data).systems for b in sys_.bodies}


# ------------------------------------------------------------------------------------------ SGO side

def shkt_ref(path: str) -> str:
    """'app:/object/Ragdoll_v603_flak.shkt' -> 'OBJECT/RAGDOLL_V603_FLAK.SHKT' (folder OBJECT when none is given)."""
    parts = path.replace('\\', '/').split(':', 1)[-1].strip('/').split('/')
    folder = parts[-2] if len(parts) > 1 else 'OBJECT'
    return f'{folder.upper()}/{parts[-1].upper()}'


SGO_MAGICS = (b'SGO\0', b'\0OGS', b'DSGO')


def _plain_dsgo(v: object) -> object:
    """dsgo's tree as plain Python, blobs kept as bytes (dsgo.to_py drops them to '<blob N>')."""
    if isinstance(v, dsgo.Node):
        if v.names and len(v.names) == len(v.items):
            return {v.names[i]: _plain_dsgo(c) for i, c in enumerate(v.items)}
        return [_plain_dsgo(c) for c in v.items]
    if isinstance(v, dsgo.Blob):
        return v.data
    return v


def load_plain(data: bytes) -> dict | None:
    """An SGO or DSGO file as a dict of its top-level values (lists, numbers, strings, raw blocks as bytes); None if
    `data` is neither or does not parse."""
    try:
        if data[:4] == b'DSGO':
            doc = _plain_dsgo(dsgo.parse(data).root)
        elif data[:4] in SGO_MAGICS:
            doc = sgo.read(data)[1]
        else:
            return None
    except (ValueError, struct.error, IndexError, KeyError, UnicodeDecodeError):
        return None
    return doc if isinstance(doc, dict) else None


def _embedded(v: object) -> dict | None:
    """An embedded SGO / DSGO (a raw block) as load_plain's dict; None if `v` is not one."""
    return load_plain(v) if isinstance(v, bytes) else None


def bound_names(doc: dict) -> list[tuple[str, str, str]]:
    """(SHKT ref, where, body name) for every body name an SGO (load_plain's form, SGO or DSGO) binds to a
    SHKT: the mapping rows of the embedded SGO after each SHKT path ([shkt, {mapping}]: animation_from_ragdoll rows
    are [[body, bone], ...], ragdoll_from_animation rows [[bone, body], ...]) and the parts of each accessory list
    ([n, parts, shkt, mapping], parts = [[body, values...], ...])."""
    out: list[tuple[str, str, str]] = []

    def mapping(shkt: str, inner: dict, where: str) -> None:
        for key, body_at in (('animation_from_ragdoll', 0), ('ragdoll_from_animation', 1)):
            for r, row in enumerate(inner.get(key) or []):
                names = row[0] if isinstance(row, list) and row else None
                if isinstance(names, list) and len(names) == 2 and isinstance(names[body_at], str):
                    out.append((shkt, f'{where}.{key}[{r}]', names[body_at]))

    def is_parts(v: object) -> bool:
        return isinstance(v, list) and bool(v) and all(isinstance(p, list) and p and isinstance(p[0], str) for p in v)

    def walk(v: object, where: str) -> None:
        if isinstance(v, dict):
            for key, value in v.items():
                walk(value, f'{where}.{key}' if where else key)
            return
        if not isinstance(v, list):
            return
        for i, item in enumerate(v):
            if isinstance(item, str) and item.lower().endswith('.shkt'):
                shkt = shkt_ref(item)
                inner = _embedded(v[i + 1]) if i + 1 < len(v) else None
                if inner is not None:
                    mapping(shkt, inner, f'{where}[{i + 1}]')
                if i == 2 and is_parts(v[1]):
                    out.extend((shkt, f'{where}[1][{p}]', part[0]) for p, part in enumerate(v[1]))
            walk(item, f'{where}[{i}]')
    walk(doc, '')
    return out


def sgo_problems(sgo_bytes: bytes, shkt_path: str, names: set[str], sgo_name: str = 'SGO') -> list[Problem]:
    """The body names `sgo_bytes` (SGO or DSGO) binds to the SHKT `shkt_path` (as an SGO writes it, or shkt_ref's
    form) that it does not have (the game crashes before the mission starts on any of them)."""
    members = load_plain(sgo_bytes)
    if members is None:
        raise ValueError(f'{sgo_name} is not an SGO / DSGO')
    want = shkt_ref(shkt_path)
    return [Problem('error', f'{sgo_name} {where}', f'body {body!r} is not in {want}')
            for shkt, where, body in bound_names(members) if shkt == want and body not in names]


def main(argv: list[str]) -> int:
    bad = 0
    for path in argv:
        with open(path, 'rb') as h:
            data = h.read()
        found = problems(data)
        for p in found:
            print(f'{path}: {p}')
        bad += sum(p.severity == 'error' for p in found)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
