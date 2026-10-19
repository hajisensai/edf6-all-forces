"""Checks of an object SHKT (Havok physics tagfile) and of the SGOs that bind to it, before the game sees them.

The checks and their thresholds follow EDF6 SHKT Editor's ShktChecker / SgoChecker and its format report
(ktaro, MIT License, Copyright (c) 2026 ktaro; https://github.com/ktaro: EDF6_SHKT_Format_Spec §5, Usage
Analysis §1): the rules are re-implemented here on pylib/hktag.py, no code is copied.

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
"""
from __future__ import annotations

import math
import struct
import sys
from dataclasses import dataclass

import dsgo
import sgo
from hktag import Tag

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]      # x, y, z, w (Havok's order)

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
    shape: int                 # item index (0: none)
    mass: float
    filter_info: int
    material_id: int
    motion_properties_id: int
    motion_type: int
    position: Vec3
    orientation: Quat
    mass_distribution: int     # item index (0: the game derives it from the shape)


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
    """The parts of an object SHKT these checks read (offsets from pylib/hktag.py's type table)."""

    def __init__(self, data: bytes) -> None:
        self.tag = t = Tag(bytes(data))
        self.b = t.b
        scene = t.root_variant('hknpPhysicsSceneData')
        si = t.u32(scene + t.offset('hknpPhysicsSceneData', 'systemDatas'))
        self.systems: list[SystemInfo] = []
        if not si:
            return
        _, sat, ns = t.item(si)
        for k in range(ns):
            _, at, _ = t.item(t.u32(sat + 4 * k))
            self.systems.append(self._system(k, at))

    def _count(self, at: int, type_name: str, member: str) -> int:
        idx = self.tag.u32(at + self.tag.offset(type_name, member))
        return self.tag.item(idx)[2] if idx else 0

    def _system(self, k: int, at: int) -> SystemInfo:
        t = self.tag
        S = 'hknpPhysicsSystemData'
        bodies: list[BodyInfo] = []
        bi = t.u32(at + t.offset(S, 'bodyCinfos'))
        if bi:
            _, bat, nb = t.item(bi)
            size = t.size(B)
            o = {m: t.offset(B, m) for m in ('shape', 'collisionFilterInfo', 'materialId', 'name', 'motionType',
                                              'position', 'orientation', 'mass', 'massDistribution',
                                              'motionPropertiesId')}
            for j in range(nb):
                a = bat + j * size
                bodies.append(BodyInfo(
                    j, t.cstr(t.u32(a + o['name'])), t.u32(a + o['shape']),
                    struct.unpack_from('<f', self.b, a + o['mass'])[0], t.u32(a + o['collisionFilterInfo']),
                    struct.unpack_from('<H', self.b, a + o['materialId'])[0],
                    struct.unpack_from('<H', self.b, a + o['motionPropertiesId'])[0], self.b[a + o['motionType']],
                    struct.unpack_from('<3f', self.b, a + o['position']),
                    struct.unpack_from('<4f', self.b, a + o['orientation']), t.u32(a + o['massDistribution'])))
        joints: list[JointInfo] = []
        ci = t.u32(at + t.offset(S, 'constraintCinfos'))
        if ci:
            C = 'hknpConstraintCinfo'
            _, cat, nc = t.item(ci)
            for j in range(nc):
                a = cat + j * t.size(C)
                kind, dat, _ = t.item(t.u32(a + t.offset(C, 'constraintData')))
                pa, pb = self._pivots(kind, dat)
                joints.append(JointInfo(j, kind, t.u32(a + t.offset(C, 'bodyA')), t.u32(a + t.offset(C, 'bodyB')),
                                        pa, pb))
        return SystemInfo(k, self._count(at, S, 'materials'), self._count(at, S, 'motionProperties'), bodies, joints)

    def _pivots(self, kind: str, at: int) -> tuple[Vec3 | None, Vec3 | None]:
        t = self.tag
        members = t.members(t.type_index(kind)) if kind in t._by_name else {}
        if 'atoms' not in members:
            return None, None
        atoms = t.tname(members['atoms'].type)
        am = t.members(members['atoms'].type)
        name = next((n for n in TRANSFORM_ATOMS if n in am), None)
        if name is None or t.tname(am[name].type) != 'hkpSetLocalTransformsConstraintAtom':
            return None, None
        A = 'hkpSetLocalTransformsConstraintAtom'
        tr = at + members['atoms'].offset + t.offset(atoms, name) if atoms in t._by_name else \
            at + members['atoms'].offset + am[name].offset
        tl = t.offset('hkTransform', 'translation')
        return (struct.unpack_from('<3f', self.b, tr + t.offset(A, 'transformA') + tl),
                struct.unpack_from('<3f', self.b, tr + t.offset(A, 'transformB') + tl))

    # ---------------------------------------------------------------------- shapes

    def shape_members(self, kind: str) -> dict:
        return self.tag.members(self.tag.type_index(kind))

    def has_mass_properties(self, shape: int) -> bool:
        t = self.tag
        kind, at, _ = t.item(shape)
        pi = t.u32(at + t.offset('hknpShape', 'properties') + t.offset('hknpShapeProperties', 'properties'))
        if not pi:
            return False
        P = 'hknpShapeProperties::Property'
        _, pat, n = t.item(pi)
        return any(struct.unpack_from('<H', self.b, pat + k * t.size(P) + t.offset(P, 'key'))[0] == MASS_PROPERTIES_KEY
                   and t.u32(pat + k * t.size(P) + t.offset(P, 'object')) for k in range(n))

    def hull(self, shape: int) -> tuple[float, list[Vec3], list[tuple[float, float, float, float]]] | None:
        """(convex radius, vertices, planes) of a convex shape (box, capsule, sphere, cylinder too); None otherwise."""
        t = self.tag
        kind, at, _ = t.item(shape)
        m = self.shape_members(kind)
        if 'hull' not in m:
            return None
        H = at + m['hull'].offset
        vi = t.u32(H + t.offset('hknpConvexHull', 'vertices'))
        pi = t.u32(H + t.offset('hknpConvexHull', 'planes'))
        verts: list[Vec3] = []
        planes: list[tuple[float, float, float, float]] = []
        if vi:
            _, va, vn = t.item(vi)
            verts = [struct.unpack_from('<3f', self.b, va + 12 * k) for k in range(vn)]
        if pi:
            _, pa, pn = t.item(pi)
            planes = [struct.unpack_from('<4f', self.b, pa + 16 * k) for k in range(pn)]
        return struct.unpack_from('<f', self.b, at + m['convexRadius'].offset)[0], verts, planes

    def instances(self, compound: int) -> list[tuple[Quat, Vec3, Vec3, int]]:
        """A compound's instances: (rotation, translation, scale, shape item)."""
        t = self.tag
        _, at, _ = t.item(compound)
        ii = t.u32(at + t.offset('hknpCompoundShape', 'instances'))
        if not ii:
            return []
        _, iat, n = t.item(ii)
        I = 'hknpShapeInstance'
        size = t.size(I)
        o_r, o_t, o_s, o_sh = (t.offset(I, m) for m in ('rotation', 'translation', 'scale', 'shape'))
        return [(struct.unpack_from('<4f', self.b, iat + k * size + o_r),
                 struct.unpack_from('<3f', self.b, iat + k * size + o_t),
                 struct.unpack_from('<3f', self.b, iat + k * size + o_s),
                 t.u32(iat + k * size + o_sh)) for k in range(n)]

    def instance_box(self, inst: tuple[Quat, Vec3, Vec3, int]) -> tuple[Vec3, Vec3] | None:
        """The box of an instance's convex shape in the compound's frame (its convex radius included)."""
        q, tr, sc, shape = inst
        h = self.hull(shape)
        if h is None or not h[1]:
            return None
        radius, verts, _ = h
        pts = [place(tr, q, (v[0] * sc[0], v[1] * sc[1], v[2] * sc[2])) for v in verts]
        return box_of(pts, radius)

    def compound_bounds(self, compound: int) -> tuple[tuple[Vec3, Vec3], int]:
        """(the compound's stored aabb, its simd tree's node item: 0 when it has none)."""
        t = self.tag
        _, at, _ = t.item(compound)
        C = 'hknpCompoundShape'
        a = struct.unpack_from('<8f', self.b, at + t.offset(C, 'aabb'))
        bvd = at + t.offset(C, 'boundingVolumeData')
        D = 'hknpCompoundShapeData'
        simd = 0
        if self.b[bvd + t.offset(D, 'type')] == 2:
            simd = t.u32(bvd + t.offset(D, 'simdTree') + t.offset('hkcdSimdTree', 'nodes'))
        return ((a[0], a[1], a[2]), (a[4], a[5], a[6])), simd

    def simd_lanes(self, nodes: int) -> list[tuple[bool, list[tuple[Vec3, Vec3, int]]]]:
        """Each node of a simd tree: (is leaf, its non-empty lanes (lo, hi, data))."""
        t = self.tag
        N = 'hkcdSimdTree::Node'
        _, at, n = t.item(nodes)
        size, o_data, o_leaf = t.size(N), t.offset(N, 'data'), t.offset(N, 'isLeaf')
        out = []
        for k in range(n):
            base = at + k * size
            f = struct.unpack_from('<24f', self.b, base)
            data = struct.unpack_from('<4I', self.b, base + o_data)
            lanes = []
            for lane in range(4):
                lo = (f[lane], f[8 + lane], f[16 + lane])
                hi = (f[4 + lane], f[12 + lane], f[20 + lane])
                if lo[0] <= hi[0]:
                    lanes.append((lo, hi, data[lane]))
            out.append((bool(self.b[base + o_leaf]), lanes))
        return out


# ------------------------------------------------------------------------------------------ checks

def _body_problems(sys_: SystemInfo, out: list[Problem]) -> None:
    seen: dict[str, int] = {}
    for b in sys_.bodies:
        w = f'system {sys_.index} body {b.index} {b.name!r}'
        if b.name in seen:
            out.append(Problem('error', w, f'the same name as body {seen[b.name]}: the SGO cannot tell them apart'))
        seen.setdefault(b.name, b.index)
        if not b.shape:
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


def _hull_problems(s: Shkt, shape: int, w: str, out: list[Problem]) -> None:
    h = s.hull(shape)
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


def _compound_problems(s: Shkt, shape: int, w: str, out: list[Problem]) -> list[int]:
    """Bound checks of a compound of convex shapes; returns its instances' shapes for the walk."""
    insts = s.instances(shape)
    boxes = [s.instance_box(i) for i in insts]
    aabb, simd = s.compound_bounds(shape)
    for k, box in enumerate(boxes):
        if box is not None and box_misses(aabb, box) > 0:
            out.append(Problem('error', w, f'instance {k} sticks {box_misses(aabb, box):.4f} m out of the aabb'))
    if simd:
        nodes = s.simd_lanes(simd)

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


def problems(data: bytes) -> list[Problem]:
    """Every problem of one SHKT on its own (no SGO)."""
    s = Shkt(data)
    out: list[Problem] = []
    seen: set[int] = set()
    for sys_ in s.systems:
        _body_problems(sys_, out)
        _joint_problems(sys_, out)
        for b in sys_.bodies:
            if b.shape and not b.mass_distribution and not s.has_mass_properties(b.shape):
                out.append(Problem('warning', f'body {b.name!r}', 'no massDistribution and its shape stores no mass '
                                                                  'properties (every stock shape has them)'))
            stack = [(b.shape, f'body {b.name!r}')]
            while stack:
                shape, w = stack.pop()
                if not shape or shape in seen:
                    continue
                seen.add(shape)
                kind = s.tag.item(shape)[0]
                if kind == 'hknpCompoundShape':
                    for k, sub in enumerate(_compound_problems(s, shape, f'{w} compound', out)):
                        stack.append((sub, f'{w} instance {k}'))
                else:
                    _hull_problems(s, shape, f'{w} {kind}', out)
    return out


def body_names(data: bytes) -> set[str]:
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


PACKFILE_MAGIC = bytes.fromhex('1e0db0cacefa11d0')     # a legacy Havok packfile (the EDF4.1 port's ragdolls)


def main(argv: list[str]) -> int:
    bad = 0
    for path in argv:
        with open(path, 'rb') as h:
            data = h.read()
        if data[:8] == PACKFILE_MAGIC:
            print(f'{path}: skipped: a legacy Havok packfile, not a tagfile')
            continue
        found = problems(data)
        for p in found:
            print(f'{path}: {p}')
        bad += sum(p.severity == 'error' for p in found)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
