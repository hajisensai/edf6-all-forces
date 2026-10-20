"""Havok 2020.1 (hknp) physics shapes of an EDF6 tagfile (pylib/hkfile.py blocks): read, computed and written in one
place, for every generator that writes an SHKT or a map collision.

Ported from EDF6 SHKT Editor's EdfHavok.Physics (QuickHull.cs, ConvexHull.cs, ConvexHullValidator.cs,
MassProperties.cs, SimdTree.cs, Compound.cs, ConvexMesh.cs, PrimitiveShapes.cs, Mat3.cs, Hknp/HknpShapeWriter.cs;
https://codeberg.org/Ktaro/EDF6_SHKT_Editor v1, 8ade8fd), Copyright (c) 2026 Ktaro, MIT License
(pylib/LICENSE.edf6-shkt-editor); the rules were found on the game's own files (its research/05, 06 and 15) and are
checked here again on the player's (tests/test_hkshape.py). Pure Python except the two tree builders, which are
pylib/hkcms_build.py's (numpy, natively backed by native/aabb_tree.cpp): one implementation of each.

  reading      convex_hull, instances, instance_slot, compound_bounds, simd_lanes, properties, mass_properties,
               read_mass, read_hull
  hulls        build_hull(points) -> ConvexHull (QuickHull, coplanar faces merged, Havok's 252-vertex limit),
               hull_vertices(points), hull_problems(hull)
  mass         MassProperties, from_hull (Steiner volume, polyhedron inertia x factor), box_mass (the compressed
               meshes' solid-box rule), combine (compound children), pack_vec3 / unpack_vec3 (hkPackedVector3: EDF.dll
               0x1305DE0, bit exact on all 22,562 stock vectors), pack_quat, mass_bytes
  codecs       f32_out, encode_box / decode_box (the 4-byte AABB codec with a safety margin: map trees),
               aabb4_encode / aabb4_decode (the same codec at single precision without margin: the stock bytes)
  boxes        instance_box (placed vertices +- convex radius), compound_radius (the stock bounding radius rule)
  writing      write_convex(tf, data) / ConvexData, convex_data(points, radius), affine_convex(tf, shape, M, o),
               refresh_compound(compound) (aabb, bounding radius, simd tree from the instances),
               write_convex_mesh(tf, externs, mass) (a compressed mesh of convex shapes, research/15 section 3)
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field

import hkfile
from hkfile import Block

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]      # x, y, z, w (Havok's order)
Plane = tuple[float, float, float, float]     # n . p + d = 0, n out of the hull
Box = tuple[Vec3, Vec3]
MASS_PROPERTIES_KEY = 0xF100
MAX_VERTICES = 252             # Havok's hull builder keeps no more (indices are 8-bit; format spec 4.1)
MAX_MESH_SHAPES = 127          # convex shapes of one compressed mesh section (8-bit keys and node offsets)
DENSITY = 1000.0               # Havok's default, every game file's
EMPTY = struct.unpack('<f', struct.pack('<I', 0x7F7FFFEE))[0]     # an empty simd lane: lo = +EMPTY, hi = -EMPTY
PENETRATION_FACTOR = 0.165     # maxAllowedPenetration = this x the shape's smallest width (radius included)
CONVEX_FLAGS = 67              # hknpShape.flags of most stock hknpConvexShape (7,471 of 7,889)
CMS_AXES: Quat = (0.0, 0.0, 0.0, 29999 / 30000)   # majorAxisSpace of all 82 stock convex meshes (F52F)
CMS_INERTIA_FACTOR = 1.5       # the box rule's factor in all 82 stock convex meshes


class HullError(ValueError):
    """The points cannot form a hull Havok can store (too few, flat, too many vertices)."""


# ------------------------------------------------------------------------------------------ vectors

def _sub(a, b):  # noqa: ANN001, ANN202
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):  # noqa: ANN001, ANN202
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _mul(a, s):  # noqa: ANN001, ANN202
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b) -> float:  # noqa: ANN001
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):  # noqa: ANN001, ANN202
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _len(a) -> float:  # noqa: ANN001
    return math.sqrt(_dot(a, a))


def _unit(a):  # noqa: ANN001, ANN202
    n = _len(a)
    return _mul(a, 1.0 / n) if n > 0 else (0.0, 0.0, 0.0)


def f32(x: float) -> float:
    return struct.unpack('<f', struct.pack('<f', x))[0]


def rotate(q: Quat, v: Vec3) -> Vec3:
    u = (q[0], q[1], q[2])
    t = _mul(_cross(u, v), 2.0)
    return _add(_add(v, _mul(t, q[3])), _cross(u, t))


def quat_matrix(q: Quat) -> list[list[float]]:
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def matrix_quat(m: list[list[float]]) -> Quat:
    """A rotation matrix (columns = the turned axes) as a quaternion with w >= 0 (Mat3.ToQuaternion)."""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    return q if q[3] >= 0 else (-q[0], -q[1], -q[2], -q[3])


def _mm(a, b):  # noqa: ANN001, ANN202
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _tr(a):  # noqa: ANN001, ANN202
    return [[a[j][i] for j in range(3)] for i in range(3)]


def eigen_symmetric(m: list[list[float]]) -> tuple[Vec3, list[list[float]]]:
    """(eigenvalues, a right-handed rotation whose columns are the eigenvectors) of a symmetric 3x3 (cyclic Jacobi:
    Mat3.EigenSymmetric)."""
    a = [m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]]
    v = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
    for _ in range(64):
        p, q = 0, 1
        if abs(a[2]) > abs(a[p * 3 + q]):
            p, q = 0, 2
        if abs(a[5]) > abs(a[p * 3 + q]):
            p, q = 1, 2
        apq = a[p * 3 + q]
        scale = abs(a[0]) + abs(a[4]) + abs(a[8])
        if abs(apq) <= 1e-15 * max(scale, 1e-300):
            break
        th = 0.5 * math.atan2(2 * apq, a[q * 3 + q] - a[p * 3 + p])
        c, s = math.cos(th), math.sin(th)
        for k in range(3):
            akp, akq = a[k * 3 + p], a[k * 3 + q]
            a[k * 3 + p], a[k * 3 + q] = c * akp - s * akq, s * akp + c * akq
        for k in range(3):
            apk, aqk = a[p * 3 + k], a[q * 3 + k]
            a[p * 3 + k], a[q * 3 + k] = c * apk - s * aqk, s * apk + c * aqk
        for k in range(3):
            vkp, vkq = v[k * 3 + p], v[k * 3 + q]
            v[k * 3 + p], v[k * 3 + q] = c * vkp - s * vkq, s * vkp + c * vkq
    vec = [[v[0], v[1], v[2]], [v[3], v[4], v[5]], [v[6], v[7], v[8]]]
    cols = [tuple(vec[r][c] for r in range(3)) for c in range(3)]
    if _dot(_cross(cols[0], cols[1]), cols[2]) < 0:
        for r in range(3):
            vec[r][2] = -vec[r][2]
    return (a[0], a[4], a[8]), vec


# ------------------------------------------------------------------------------------------ QuickHull

class _Face:
    __slots__ = ('v', 'nb', 'n', 'd', 'alive', 'outside')

    def __init__(self) -> None:
        self.v = [0, 0, 0]
        self.nb = [0, 0, 0]          # neighbour across edge v[k] -> v[k + 1]
        self.n: Vec3 = (0.0, 0.0, 0.0)
        self.d = 0.0
        self.alive = True
        self.outside: list[int] | None = None

    def dist(self, p: Vec3) -> float:
        return _dot(self.n, p) - self.d


def quickhull(pts: list[Vec3], eps: float) -> list[tuple[int, int, int]]:
    """Outward triangles of the hull of `pts` (incremental Quickhull with conflict lists: QuickHull.cs). A point
    closer than eps to the hull is inside; the visible region grows through face adjacency; a point whose horizon
    is not one simple loop (numerical noise) is skipped."""
    n = len(pts)
    if n < 4:
        raise HullError('at least 4 distinct points are needed for a convex hull')
    ext = [0] * 6
    for i in range(1, n):
        for a in range(3):
            if pts[i][a] < pts[ext[a * 2]][a]:
                ext[a * 2] = i
            if pts[i][a] > pts[ext[a * 2 + 1]][a]:
                ext[a * 2 + 1] = i
    i0 = i1 = 0
    best = -1.0
    for a in ext:
        for b in ext:
            d = _dot(_sub(pts[a], pts[b]), _sub(pts[a], pts[b]))
            if d > best:
                best, i0, i1 = d, a, b
    if math.sqrt(best) <= eps:
        raise HullError('all points are (nearly) the same')
    direction = _unit(_sub(pts[i1], pts[i0]))
    i2, best = -1, eps
    for i in range(n):
        d = _len(_cross(direction, _sub(pts[i], pts[i0])))
        if d > best:
            best, i2 = d, i
    if i2 < 0:
        raise HullError('the points are collinear')
    nrm = _unit(_cross(_sub(pts[i1], pts[i0]), _sub(pts[i2], pts[i0])))
    i3, best = -1, eps
    for i in range(n):
        d = abs(_dot(nrm, _sub(pts[i], pts[i0])))
        if d > best:
            best, i3 = d, i
    if i3 < 0:
        raise HullError('the points are coplanar (a flat shape cannot be a convex hull)')
    faces: list[_Face] = []

    def make(a: int, b: int, c: int) -> _Face:
        f = _Face()
        f.v = [a, b, c]
        cr = _cross(_sub(pts[b], pts[a]), _sub(pts[c], pts[a]))
        ln = _len(cr)
        f.n = _mul(cr, 1.0 / ln) if ln > 0 else (0.0, 0.0, 0.0)
        f.d = _dot(f.n, pts[a])
        return f
    centroid = _mul(_add(_add(pts[i0], pts[i1]), _add(pts[i2], pts[i3])), 0.25)
    for a, b, c in ((i0, i1, i2), (i0, i3, i1), (i1, i3, i2), (i0, i2, i3)):
        f = make(a, b, c)
        if f.dist(centroid) > 0:
            f = make(a, c, b)
        faces.append(f)
    at = {}
    for i, f in enumerate(faces):
        for k in range(3):
            at[(f.v[k], f.v[(k + 1) % 3])] = i
    for f in faces:
        for k in range(3):
            f.nb[k] = at[(f.v[(k + 1) % 3], f.v[k])]

    def assign(p: int, candidates) -> None:  # noqa: ANN001
        best_face, best_d = -1, eps
        for fi in candidates:
            d = faces[fi].dist(pts[p])
            if d > best_d:
                best_d, best_face = d, fi
        if best_face >= 0:
            if faces[best_face].outside is None:
                faces[best_face].outside = []
            faces[best_face].outside.append(p)
    tet = {i0, i1, i2, i3}
    for p in range(n):
        if p not in tet:
            assign(p, range(len(faces)))
    pending = list(range(len(faces)))
    while pending:
        fi = pending.pop()
        f = faces[fi]
        if not f.alive or not f.outside:
            continue
        eye = f.outside[0]
        far = f.dist(pts[eye])
        for p in f.outside:
            d = f.dist(pts[p])
            if d > far:
                far, eye = d, p
        E = pts[eye]
        visible = []
        is_visible = {fi}
        stack = [fi]
        while stack:
            g = stack.pop()
            visible.append(g)
            for h in faces[g].nb:
                if h not in is_visible and faces[h].dist(E) > eps:
                    is_visible.add(h)
                    stack.append(h)
        while True:
            nxt: dict[int, tuple[int, int]] = {}
            simple = True
            for g in visible:
                for k in range(3):
                    h = faces[g].nb[k]
                    if h in is_visible:
                        continue
                    a, b = faces[g].v[k], faces[g].v[(k + 1) % 3]
                    if a in nxt:
                        simple = False
                    else:
                        nxt[a] = (b, h)
            grow = -1
            for a, (b, h) in nxt.items():
                cr = _cross(_sub(pts[b], pts[a]), _sub(E, pts[a]))
                ln = _len(cr)
                hv = faces[h].v
                opp = hv[0] if hv[0] not in (a, b) else hv[1] if hv[1] not in (a, b) else hv[2]
                if ln <= eps * _len(_sub(pts[b], pts[a])) or _dot(_mul(cr, 1.0 / ln), _sub(pts[opp], pts[a])) > eps:
                    grow = h
                    break
            if grow < 0:
                break
            is_visible.add(grow)
            visible.append(grow)
        loop: list[int] = []
        if simple and len(nxt) >= 3:
            start = v = next(iter(nxt))
            while True:
                loop.append(v)
                if v not in nxt:
                    simple = False
                    break
                v = nxt[v][0]
                if v == start or len(loop) > len(nxt):
                    break
            if len(loop) != len(nxt) or v != start:
                simple = False
        else:
            simple = False
        if not simple:
            f.outside.remove(eye)
            pending.append(fi)
            continue
        first = len(faces)
        m = len(loop)
        for k in range(m):
            a, b = loop[k], loop[(k + 1) % m]
            nf = make(a, b, eye)
            hidden = nxt[a][1]
            nf.nb = [hidden, first + (k + 1) % m, first + (k + m - 1) % m]
            hf = faces[hidden]
            for j in range(3):
                if hf.v[j] == b and hf.v[(j + 1) % 3] == a:
                    hf.nb[j] = first + k
            faces.append(nf)
        orphans = []
        for g in visible:
            faces[g].alive = False
            for p in faces[g].outside or []:
                if p != eye:
                    orphans.append(p)
            faces[g].outside = None
        new = list(range(first, first + m))
        for p in orphans:
            assign(p, new)
        pending.extend(new)
    return [(f.v[0], f.v[1], f.v[2]) for f in faces if f.alive]


# ------------------------------------------------------------------------------------------ hulls

ANGLE_TOLERANCE = 1e-4        # coplanar triangles merge when their normals differ by less than this (1 - cos)
DISTANCE_TOLERANCE = 1e-5     # ... and their vertices are this close (x the point cloud's size) to the plane


@dataclass
class ConvexHull:
    """An hknpConvexHull's arrays with the game files' conventions: face vertices counter-clockwise round the
    outward normal; planes (n, w) with n . v + w = 0 through the face centroid; face_links[first + k] the face and
    edge position across edge k; vertex_edges[v] one edge leaving v; min_half_angle half the smallest angle to an
    edge neighbour, 0..90 degrees -> 0..255."""
    vertices: list[Vec3]
    planes: list[Plane]
    faces: list[tuple[int, int, int]]        # (firstIndex, numIndices, minHalfAngle)
    indices: list[int]
    face_links: list[tuple[int, int]] = field(default_factory=list)
    vertex_edges: list[tuple[int, int]] = field(default_factory=list)

    def face(self, f: int) -> list[int]:
        first, num, _ = self.faces[f]
        return self.indices[first:first + num]

    def min_width(self) -> float:
        """The smallest extent over the face normals."""
        w = math.inf
        for p in self.planes:
            d = [_dot(p, v) for v in self.vertices]
            w = min(w, max(d) - min(d))
        return w


def _distinct(points) -> list[Vec3]:  # noqa: ANN001
    """Float32-rounded, de-duplicated points: the stored vertices are exactly the ones used."""
    out, seen = [], set()
    for p in points:
        q = (f32(p[0]), f32(p[1]), f32(p[2]))
        if q not in seen:
            seen.add(q)
            out.append(q)
    return out


def _merge_coplanar(pts: list[Vec3], tris: list[tuple[int, int, int]], tol: float) -> list[tuple[Vec3, list[int]]]:
    """Edge-adjacent coplanar triangles merged into polygons (normal, CCW loop); every boundary vertex kept; a region
    whose boundary is not one simple loop stays triangles."""
    tn = [_unit(_cross(_sub(pts[b], pts[a]), _sub(pts[c], pts[a]))) for a, b, c in tris]
    by_edge = {}
    for i, (a, b, c) in enumerate(tris):
        by_edge[(a, b)] = by_edge[(b, c)] = by_edge[(c, a)] = i
    group = [-1] * len(tris)
    polys: list[tuple[Vec3, list[int]]] = []
    for seed in range(len(tris)):
        if group[seed] >= 0:
            continue
        n0 = tn[seed]
        d0 = _dot(n0, pts[tris[seed][0]])
        members = []
        stack = [seed]
        group[seed] = seed
        while stack:
            i = stack.pop()
            members.append(i)
            a, b, c = tris[i]
            for e in ((b, a), (c, b), (a, c)):
                j = by_edge.get(e)
                if j is None or group[j] >= 0:
                    continue
                t = tris[j]
                if _dot(tn[j], n0) > 1 - ANGLE_TOLERANCE and all(abs(_dot(n0, pts[x]) - d0) < tol for x in t):
                    group[j] = seed
                    stack.append(j)
        edges: dict[tuple[int, int], None] = {}
        for i in members:
            a, b, c = tris[i]
            for e in ((a, b), (b, c), (c, a)):
                if (e[1], e[0]) in edges:
                    del edges[(e[1], e[0])]
                else:
                    edges[e] = None
        nxt: dict[int, int] = {}
        simple = True
        for a, b in edges:
            if a in nxt:
                simple = False
            else:
                nxt[a] = b
        loop: list[int] | None = None
        if simple:
            start = v = min(nxt)
            loop = []
            while True:
                loop.append(v)
                v = nxt[v]
                if v == start or len(loop) > len(nxt):
                    break
            if len(loop) != len(nxt):
                loop = None
        if loop is None:
            polys.extend((tn[i], list(tris[i])) for i in members)
            continue
        nn = (0.0, 0.0, 0.0)          # Newell normal of the loop
        for k in range(len(loop)):
            p, q = pts[loop[k]], pts[loop[(k + 1) % len(loop)]]
            nn = _add(nn, ((p[1] - q[1]) * (p[2] + q[2]), (p[2] - q[2]) * (p[0] + q[0]), (p[0] - q[0]) * (p[1] + q[1])))
        polys.append((_unit(nn), loop))
    return polys


def _polys(points) -> tuple[list[Vec3], list[tuple[Vec3, list[int]]]]:  # noqa: ANN001
    pts = _distinct(points)
    scale = max(max((max(abs(c) for c in p) for p in pts), default=0.0), 1e-6)
    tris = quickhull(pts, max(1e-9, 1e-7 * scale))
    return pts, _merge_coplanar(pts, tris, DISTANCE_TOLERANCE * scale)


def hull_vertices(points) -> list[Vec3]:  # noqa: ANN001
    """The hull's vertices without Havok's limit on their number (to reduce a large cloud first)."""
    pts, polys = _polys(points)
    return [pts[i] for i in sorted({v for _, loop in polys for v in loop})]


def build_hull(points) -> ConvexHull:  # noqa: ANN001
    """The hull of `points` as Havok stores it (ConvexHullBuilder.Build); HullError beyond MAX_VERTICES."""
    pts, polys = _polys(points)
    used = sorted({v for _, loop in polys for v in loop})
    if len(used) > MAX_VERTICES:
        raise HullError(f'the hull has {len(used)} vertices; Havok allows at most {MAX_VERTICES}')
    remap = {u: i for i, u in enumerate(used)}
    faces: list[list[int]] = []
    indices: list[int] = []
    planes: list[Plane] = []
    for normal, loop in polys:
        if len(loop) > 255:
            raise HullError('a face has more than 255 vertices')
        c = (0.0, 0.0, 0.0)
        for v in loop:
            c = _add(c, pts[v])
        c = _mul(c, 1.0 / len(loop))
        planes.append((f32(normal[0]), f32(normal[1]), f32(normal[2]), f32(-_dot(normal, c))))
        faces.append([len(indices), len(loop), 0])
        indices.extend(remap[v] for v in loop)
    if len(indices) > 0xFFFF:
        raise HullError('the hull has too many face indices')
    edge_at = {}
    for f, (first, num, _) in enumerate(faces):
        for k in range(num):
            edge_at[(indices[first + k], indices[first + (k + 1) % num])] = (f, k)
    links = [(0, 0)] * len(indices)
    for f, (first, num, _) in enumerate(faces):
        for k in range(num):
            links[first + k] = edge_at[(indices[first + (k + 1) % num], indices[first + k])]
    vedges = [(0, 0)] * len(used)
    for f, (first, num, _) in enumerate(faces):
        for k in range(num):
            vedges[indices[first + k]] = (f, k)     # Havok keeps the last edge it visits
    for f, (first, num, _) in enumerate(faces):
        c = -1.0
        for k in range(num):
            c = max(c, _dot(planes[f], planes[links[first + k][0]]))
        half = math.acos(max(-1.0, min(1.0, c))) / 2
        faces[f][2] = max(0, min(255, int(half * 255 / (math.pi / 2))))
    return ConvexHull([pts[u] for u in used], planes, [tuple(f) for f in faces], indices, links, vedges)  # type: ignore


def hull_problems(h: ConvexHull, tolerance: float = 1e-4) -> list[str]:
    """What is inconsistent in a hull's arrays (ConvexHullValidator.Check); `tolerance` relative to its size."""
    errors = []
    nv = len(h.vertices)
    if nv > MAX_VERTICES:
        errors.append(f'{nv} vertices (max {MAX_VERTICES})')
    if not h.faces:
        return errors
    if len(h.planes) != len(h.faces):
        errors.append('planes and faces differ in count')
    if len(h.face_links) != len(h.indices):
        errors.append('faceLinks and indices differ in count')
    if len(h.vertex_edges) != nv:
        errors.append('vertexEdges and vertices differ in count')
    if errors:
        return errors
    tol = tolerance * max(max(max(abs(c) for c in v) for v in h.vertices), 1e-6)
    nxt = 0
    for f, (first, num, _) in enumerate(h.faces):
        if first != nxt:
            errors.append(f'face {f}: firstIndex {first}, expected {nxt}')
        nxt = first + num
        if num < 3:
            errors.append(f'face {f}: {num} vertices')
            continue
        idx = h.indices[first:first + num]
        n, w = h.planes[f][:3], h.planes[f][3]
        if abs(_len(n) - 1) > 1e-3:
            errors.append(f'face {f}: plane normal is not unit length')
        for v in idx:
            if v >= nv:
                errors.append(f'face {f}: vertex {v} out of range')
            elif abs(_dot(n, h.vertices[v]) + w) > tol:
                errors.append(f'face {f}: vertex {v} is off the plane')
        newell = (0.0, 0.0, 0.0)
        for k in range(num):
            newell = _add(newell, _cross(h.vertices[idx[k]], h.vertices[idx[(k + 1) % num]]))
        if _dot(newell, n) <= 0:
            errors.append(f'face {f}: vertices are not counter-clockwise around the outward normal')
        if any(_dot(n, v) + w > tol for v in h.vertices):
            errors.append(f'face {f}: a vertex is outside (not convex)')
        for k in range(num):
            g, e = h.face_links[first + k]
            if g >= len(h.faces) or e >= h.faces[g][1]:
                errors.append(f'face {f} edge {k}: link out of range')
                continue
            gf = h.faces[g]
            a, b = idx[k], idx[(k + 1) % num]
            if (h.indices[gf[0] + e], h.indices[gf[0] + (e + 1) % gf[1]]) != (b, a):
                errors.append(f'face {f} edge {k}: linked edge is not the reversed edge')
            if h.face_links[gf[0] + e] != (f, k):
                errors.append(f'face {f} edge {k}: link is not symmetric')
    for v, (f, e) in enumerate(h.vertex_edges):
        if f >= len(h.faces) or e >= h.faces[f][1] or h.indices[h.faces[f][0] + e] != v:
            errors.append(f'vertex {v}: vertexEdge does not start at the vertex')
    if nv - len(h.indices) // 2 + len(h.faces) != 2:
        errors.append(f'Euler characteristic {nv - len(h.indices) // 2 + len(h.faces)} (expected 2)')
    return errors


# ------------------------------------------------------------------------------------------ mass

@dataclass
class MassProperties:
    """hknpShapeMassProperties: volume, mass, centre of mass, principal moments, their axes (columns of the
    quaternion's matrix)."""
    volume: float
    mass: float
    com: Vec3
    moments: Vec3
    axes: Quat = (0.0, 0.0, 0.0, 1.0)

    def tensor(self) -> list[list[float]]:
        n = math.sqrt(sum(c * c for c in self.axes)) or 1.0
        r = quat_matrix(tuple(c / n for c in self.axes))       # type: ignore[arg-type]
        d = [[self.moments[0], 0, 0], [0, self.moments[1], 0], [0, 0, self.moments[2]]]
        return _mm(_mm(r, d), _tr(r))

    @staticmethod
    def from_tensor(volume: float, mass: float, com: Vec3, inertia: list[list[float]]) -> 'MassProperties':
        values, vectors = eigen_symmetric(inertia)
        return MassProperties(volume, mass, com, values, matrix_quat(vectors))


def links_of(h: ConvexHull) -> list[tuple[int, int]]:
    """faceLinks from the faces' indices (a hull that does not store them: some stock boxes and cylinders)."""
    if len(h.face_links) == len(h.indices):
        return h.face_links
    at = {}
    for f, (first, num, _) in enumerate(h.faces):
        for k in range(num):
            at[(h.indices[first + k], h.indices[first + (k + 1) % num])] = (f, k)
    return [at[(h.indices[first + (k + 1) % num], h.indices[first + k])]
            for first, num, _ in h.faces for k in range(num)]


def _polyhedron(h: ConvexHull) -> tuple[float, Vec3, list[list[float]]]:
    """Volume, centre of mass and inertia tensor (density 1, about the centre) of the closed polyhedron."""
    vol = 0.0
    c = (0.0, 0.0, 0.0)
    C = [[0.0] * 3 for _ in range(3)]
    for f in range(len(h.faces)):
        idx = h.face(f)
        a = h.vertices[idx[0]]
        for k in range(1, len(idx) - 1):
            b, d = h.vertices[idx[k]], h.vertices[idx[k + 1]]
            v6 = _dot(a, _cross(b, d))
            vol += v6 / 6
            c = _add(c, _mul(_add(_add(a, b), d), v6 / 24))
            s = _add(_add(a, b), d)
            for i in range(3):
                for j in range(3):
                    C[i][j] += v6 / 120 * (a[i] * a[j] + b[i] * b[j] + d[i] * d[j] + s[i] * s[j])
    if vol <= 0:
        raise HullError('the hull has no volume')
    com = _mul(c, 1.0 / vol)
    Cc = [[C[i][j] - vol * com[i] * com[j] for j in range(3)] for i in range(3)]
    tr = Cc[0][0] + Cc[1][1] + Cc[2][2]
    inertia = [[(tr if i == j else 0.0) - Cc[i][j] for j in range(3)] for i in range(3)]
    return vol, com, inertia


def _surface(h: ConvexHull) -> tuple[float, float]:
    """Surface area and the Steiner edge term (sum of edge length x exterior angle / 2)."""
    area = edge = 0.0
    links = links_of(h)
    for f in range(len(h.faces)):
        idx = h.face(f)
        s = (0.0, 0.0, 0.0)
        for k in range(len(idx)):
            s = _add(s, _cross(h.vertices[idx[k]], h.vertices[idx[(k + 1) % len(idx)]]))
        area += _len(s) / 2
        n = h.planes[f][:3]
        for k in range(len(idx)):
            g = h.planes[links[h.faces[f][0] + k][0]][:3]
            ext = math.acos(max(-1.0, min(1.0, _dot(n, g))))
            p, q = h.vertices[idx[k]], h.vertices[idx[(k + 1) % len(idx)]]
            edge += _len(_sub(q, p)) * ext / 4                # each edge is visited from both its faces
    return area, edge


def from_hull(h: ConvexHull, radius: float, inertia_factor: float = 1.0, density: float = DENSITY) -> MassProperties:
    """The hull rounded by `radius`: volume by Steiner's formula V + A r + M r^2 + 4/3 pi r^3, centre and inertia
    from the polyhedron scaled to that mass, x `inertia_factor` (1.0 most E6xx files, 1.5 the EDF5-derived ones).
    Havok's own values come from data the file does not keep: they agree to about 5e-4 (median)."""
    vol, com, inertia = _polyhedron(h)
    area, edge = _surface(h)
    r = radius
    vol_r = vol + area * r + edge * r * r + 4.0 / 3.0 * math.pi * r ** 3
    mass = vol_r * density
    k = inertia_factor * mass / vol
    return MassProperties.from_tensor(vol_r, mass, com, [[x * k for x in row] for row in inertia])


def box_mass(lo: Vec3, hi: Vec3, mass: float, inertia_factor: float = CMS_INERTIA_FACTOR,
             axes: Quat = CMS_AXES) -> MassProperties:
    """A solid box over lo..hi of `mass`: the rule of all 82 stock compressed meshes of convex shapes (their domain,
    moments 1.5 x m (b^2 + c^2) / 12, axes the shape's stored as F52F)."""
    e = _sub(hi, lo)
    k = inertia_factor * mass / 12
    return MassProperties(e[0] * e[1] * e[2], mass, _mul(_add(lo, hi), 0.5),
                          (k * (e[1] ** 2 + e[2] ** 2), k * (e[0] ** 2 + e[2] ** 2), k * (e[0] ** 2 + e[1] ** 2)), axes)


def combine(parts: list[tuple[MassProperties, Quat, Vec3]], inertia_factor: float = 1.0) -> MassProperties:
    """Compound children placed by (rotation, translation); the parallel-axis term x the factor too (research/06)."""
    if not parts:
        raise ValueError('no parts')
    rows = []
    for mp, q, t in parts:
        r = quat_matrix(q)
        rows.append((mp.mass, mp.volume, _add(rotate(q, mp.com), t), _mm(_mm(r, mp.tensor()), _tr(r))))
    m = sum(x[0] for x in rows)
    v = sum(x[1] for x in rows)
    com = (0.0, 0.0, 0.0)
    for x in rows:
        com = _add(com, _mul(x[2], x[0]))
    com = _mul(com, 1.0 / m)
    total = [[0.0] * 3 for _ in range(3)]
    for mass, _, c, inertia in rows:
        d = _sub(c, com)
        dd = _dot(d, d)
        for i in range(3):
            for j in range(3):
                shift = (dd if i == j else 0.0) - d[i] * d[j]
                total[i][j] += inertia[i][j] + shift * inertia_factor * mass
    return MassProperties.from_tensor(v, m, com, total)


def _bits(x: float) -> int:
    return struct.unpack('<I', struct.pack('<f', x))[0]


def _from_bits(b: int) -> float:
    return struct.unpack('<f', struct.pack('<I', b & 0xFFFFFFFF))[0]


def pack_vec3(v: Vec3) -> tuple[int, int, int, int]:
    """hkPackedVector3 (3 int16 mantissas + the upper 16 bits of a power-of-two float32 scale) as EDF.dll 0x1305DE0
    packs it: bit exact on every one of the 22,562 vectors of the stock SHKTs (tests/test_hkshape.py); unsigned
    words."""
    x = [f32(c) for c in v]
    if not all(math.isfinite(c) for c in x):
        raise ValueError('a finite vector is needed')
    guard = _from_bits(0x3F800089)
    exponent = max(_bits(f32(c * guard)) & 0x7F800000 for c in (*x, 2.0 ** -46))
    words = []
    for c in x:
        scaled = _from_bits(_bits(c) + 0x4E800000 - exponent)
        n = 2147483647 if scaled >= 2147483648.0 else int(scaled)
        words.append(((n + 0x8000) >> 16) & 0xFFFF)
    words.append(((exponent - 0x4E800000 + 0x3F800000) >> 16) & 0xFFFF)
    return tuple(words)  # type: ignore[return-value]


def unpack_vec3(w) -> Vec3:  # noqa: ANN001 - 4 words
    scale = _from_bits((w[3] & 0xFFFF) << 16) * 65536.0
    s = [x - 0x10000 if x & 0x8000 else x for x in (w[0] & 0xFFFF, w[1] & 0xFFFF, w[2] & 0xFFFF)]
    return (s[0] * scale, s[1] * scale, s[2] * scale)


def pack_quat(q: Quat) -> tuple[int, int, int, int]:
    """majorAxisSpace: 4 x uint16 = 0x8000 + component x 30000 (round half to even)."""
    return tuple(max(0, min(0xFFFF, 32768 + round(c * 30000))) for c in q)  # type: ignore[return-value]


def unpack_quat(w) -> Quat:  # noqa: ANN001
    return tuple(((x & 0xFFFF) - 32768) / 30000.0 for x in w)  # type: ignore[return-value]


def mass_bytes(mp: MassProperties) -> bytes:
    """hkCompressedMassProperties (32 bytes): centre of mass, inertia, majorAxisSpace, mass, volume."""
    return struct.pack('<4H4H4H2f', *pack_vec3(mp.com), *pack_vec3(mp.moments), *pack_quat(mp.axes), mp.mass, mp.volume)


def read_mass(block: Block) -> MassProperties:
    d = block.data
    return MassProperties(struct.unpack_from('<f', d, 28)[0], struct.unpack_from('<f', d, 24)[0],
                          unpack_vec3(struct.unpack_from('<4H', d, 0)), unpack_vec3(struct.unpack_from('<4H', d, 8)),
                          unpack_quat(struct.unpack_from('<4H', d, 16)))


# ------------------------------------------------------------------------------------------ box codecs

def f32_out(lo: Vec3, hi: Vec3) -> Box:
    """lo..hi rounded outward to float32 (what the game reads still holds it)."""
    def down(x: float) -> float:
        y = f32(x)
        return y if y <= x else _from_bits(_bits(y) - 1) if y > 0 else _from_bits(_bits(y) + 1) if y < 0 else -_from_bits(1)

    def up(x: float) -> float:
        y = f32(x)
        return y if y >= x else _from_bits(_bits(y) + 1) if y > 0 else _from_bits(_bits(y) - 1) if y < 0 else _from_bits(1)
    return (tuple(down(c) for c in lo), tuple(up(c) for c in hi))      # type: ignore[return-value]


def decode_box(parent, xyz: bytes):  # noqa: ANN001, ANN201 - numpy (2, 3)
    """The 4-byte AABB codec (hkcdStaticTree: Aabb4BytesCodec / Aabb5BytesCodec, per axis high nibble a, low b):
    min = parent.min + a^2 / 226 x extent, max = parent.max - b^2 / 226 x extent (double precision: pylib/hkcms.py's
    map trees and the native aabb_tree.cpp agree with it)."""
    import numpy as np
    ext = parent[1] - parent[0]
    a = np.array([c >> 4 for c in xyz], dtype=np.float64)
    c = np.array([c & 15 for c in xyz], dtype=np.float64)
    return np.array([parent[0] + a * a / 226.0 * ext, parent[1] - c * c / 226.0 * ext])


def encode_box(parent, box, margin: float = 1e-3) -> bytes:  # noqa: ANN001
    """The tightest code whose decoded box still holds `box` grown by `margin` (never smaller)."""
    lo_b = [box[0][k] - margin for k in range(3)]
    hi_b = [box[1][k] + margin for k in range(3)]
    out = []
    for k in range(3):
        p0, p1 = float(parent[0][k]), float(parent[1][k])
        ext = p1 - p0
        if ext <= 0:
            out.append(0)
            continue
        lo = max(0.0, (lo_b[k] - p0) / ext * 226.0)
        hi = max(0.0, (p1 - hi_b[k]) / ext * 226.0)
        a = min(15, int(math.floor(math.sqrt(lo))))
        c = min(15, int(math.floor(math.sqrt(hi))))
        while a and p0 + a * a / 226.0 * ext > lo_b[k]:
            a -= 1
        while c and p1 - c * c / 226.0 * ext < hi_b[k]:
            c -= 1
        out.append(a << 4 | c)
    return bytes(out)


def aabb4_decode(lo: float, hi: float, b: int) -> tuple[float, float]:
    """One axis of the codec at single precision (as the game decodes it)."""
    e = f32(hi - lo)
    h, l = b >> 4, b & 15
    return f32(lo + f32(f32(e * (h * h)) / 226.0)), f32(hi - f32(f32(e * (l * l)) / 226.0))


def aabb4_encode(lo: float, hi: float, mn: float, mx: float) -> int:
    """The largest nibbles whose decoded axis still holds mn..mx: the stock files' bytes exactly (Aabb4Codec)."""
    e = f32(hi - lo)
    hn = next((q for q in range(15, -1, -1) if f32(lo + f32(f32(e * (q * q)) / 226.0)) <= mn), 0)
    ln = next((q for q in range(15, -1, -1) if f32(hi - f32(f32(e * (q * q)) / 226.0)) >= mx), 0)
    return hn << 4 | ln


# ------------------------------------------------------------------------------------------ reading

def convex_hull(shape: Block) -> tuple[float, list[Vec3], list[Plane]] | None:
    """(convex radius, vertices, planes) of a convex shape (hknpConvexShape and its kinds); None for any other."""
    v = shape.value
    if not v.has('hull'):
        return None
    h = v['hull']
    verts = h['vertices'].target
    planes = h['planes'].target
    return (v['convexRadius'].f32(),
            [struct.unpack_from('<3f', verts.data, 12 * k) for k in range(verts.count)] if verts is not None else [],
            [struct.unpack_from('<4f', planes.data, 16 * k) for k in range(planes.count)] if planes is not None else [])


def read_hull(shape: Block) -> ConvexHull:
    """Every array of a convex shape's hull."""
    h = shape.value['hull']

    def arr(name: str) -> Block | None:
        return h[name].target
    v, p, f, i, fl, ve = (arr(n) for n in ('vertices', 'planes', 'faces', 'indices', 'faceLinks', 'vertexEdges'))
    return ConvexHull(
        [struct.unpack_from('<3f', v.data, 12 * k) for k in range(v.count)] if v else [],
        [struct.unpack_from('<4f', p.data, 16 * k) for k in range(p.count)] if p else [],
        [(e['firstIndex'].int(), e['numIndices'].int(), e['minHalfAngle'].int()) for e in f] if f else [],
        list(i.data) if i else [],
        [(e['faceIndex'].int(), e['edgeIndex'].int()) for e in fl] if fl else [],
        [(e['faceIndex'].int(), e['edgeIndex'].int()) for e in ve] if ve else [])


def instance_slot(compound: Block) -> hkfile.Value:
    """The reference slot of a compound's instance array (hkFreeListRelArray's elements) or of a compressed mesh's
    extern shapes."""
    v = compound.value
    return v['instances']['elements'] if v.has('instances') else v['externShapes']


def instance_array(compound: Block) -> Block | None:
    return instance_slot(compound).target


def instances(compound: Block) -> list[tuple[Quat, Vec3, Vec3, Block | None]]:
    """A compound's instances (or a compressed mesh's extern shapes): (rotation, translation, scale, shape)."""
    arr = instance_array(compound)
    if arr is None:
        return []
    return [(e['rotation'].vec(4), e['translation'].vec(3), e['scale'].vec(3), e['shape'].target)  # type: ignore[misc]
            for e in arr]


def compound_bounds(compound: Block) -> tuple[Box, Block | None]:
    """(the compound's stored aabb, its simd tree's nodes: None when it has none)."""
    v = compound.value
    a = v['aabb']['min'].vec(3), v['aabb']['max'].vec(3)
    bvd = v['boundingVolumeData']
    nodes = bvd['simdTree']['nodes'].target if bvd['type'].int() == 2 else None
    return a, nodes      # type: ignore[return-value]


def simd_lanes(nodes: Block) -> list[tuple[bool, list[tuple[Vec3, Vec3, int]]]]:
    """Each node of a simd tree: (is leaf, its non-empty lanes (lo, hi, data))."""
    out = []
    o_data = nodes.type.member('data').offset          # type: ignore[union-attr]
    o_leaf = nodes.type.member('isLeaf').offset        # type: ignore[union-attr]
    for k in range(nodes.count):
        base = k * nodes.size
        f = struct.unpack_from('<24f', nodes.data, base)
        data = struct.unpack_from('<4I', nodes.data, base + o_data)
        lanes = []
        for lane in range(4):
            lo = (f[lane], f[8 + lane], f[16 + lane])
            hi = (f[4 + lane], f[12 + lane], f[20 + lane])
            if lo[0] <= hi[0]:
                lanes.append((lo, hi, data[lane]))
        out.append((bool(nodes.data[base + o_leaf]), lanes))
    return out


def properties(shape: Block) -> Block | None:
    """The shape's hknpShapeProperties::Property array."""
    return shape.value['properties']['properties'].target


def mass_properties(shape: Block) -> Block | None:
    """The hknpShapeMassProperties a shape stores under key 0xF100, None when it stores none."""
    props = properties(shape)
    if props is None:
        return None
    for p in props:
        if p['key'].int() == MASS_PROPERTIES_KEY and p['object'].target is not None:
            return p['object'].target
    return None


# ------------------------------------------------------------------------------------------ boxes

def instance_box(inst: tuple[Quat, Vec3, Vec3, Block | None]) -> Box | None:
    """An instance's box in its compound's frame: placed vertices +- the convex radius (the radius is not scaled:
    every stock instance has scale 1, and Havok keeps the radius of a scaled convex shape)."""
    q, tr, sc, shape = inst
    h = convex_hull(shape) if shape is not None else None
    if h is None or not h[1]:
        return None
    radius, verts, _ = h
    pts = [_add(rotate(q, (v[0] * sc[0], v[1] * sc[1], v[2] * sc[2])), tr) for v in verts]
    return (tuple(min(p[k] for p in pts) - radius for k in range(3)),   # type: ignore[return-value]
            tuple(max(p[k] for p in pts) + radius for k in range(3)))


def union(boxes: list[Box]) -> Box:
    return (tuple(min(b[0][k] for b in boxes) for k in range(3)),      # type: ignore[return-value]
            tuple(max(b[1][k] for b in boxes) for k in range(3)))


def compound_radius(insts: list[tuple[Quat, Vec3, Vec3, Block | None]], aabb: Box) -> float:
    """The stock bounding radius rule (CompoundCalculator): the largest (distance from the aabb's centre to a child's
    vertex box centre + the child's bounding sphere about that centre + its convex radius)."""
    centre = _mul(_add(aabb[0], aabb[1]), 0.5)
    best = 0.0
    for q, tr, sc, shape in insts:
        h = convex_hull(shape) if shape is not None else None
        if h is None or not h[1]:
            continue
        radius, verts, _ = h
        pts = [_add(rotate(q, (v[0] * sc[0], v[1] * sc[1], v[2] * sc[2])), tr) for v in verts]
        cc = _mul(_add(tuple(min(p[k] for p in pts) for k in range(3)), tuple(max(p[k] for p in pts) for k in range(3))), 0.5)
        sphere = max(_len(_sub(p, cc)) for p in pts) + radius
        best = max(best, _len(_sub(cc, centre)) + sphere)
    return best


# ------------------------------------------------------------------------------------------ simd trees

def simd_tree(boxes: list[Box], data: list[int]) -> bytes:
    """hkcdSimdTree nodes (128 bytes each) over `boxes` (pylib/hkcms_build.py's builder, natively when the DLL is
    there): node 0 an empty sentinel, node 1 the root, leaves of four boxes with lane data `data[i]`, lanes rounded
    outward to float32. The one tree builder of every shape the generators write."""
    import numpy as np
    import hkcms_build
    return hkcms_build.simd_tree(np.asarray(boxes, dtype=np.float64).reshape(-1, 2, 3), list(data))


def set_simd_tree(tf: hkfile.Tagfile, tree: hkfile.Value, boxes: list[Box], data: list[int]) -> None:
    """A new node array for the hkcdSimdTree value `tree` (its element type from the file)."""
    nodes = simd_tree(boxes, data)
    old = tree['nodes'].target
    node_type = old.type if old is not None else tree['nodes'].type.eff_pointee
    tree['nodes'].set_target(tf.new(node_type, len(nodes) // 128, nodes))
    tree['isCompact'].set_int(1)


# ------------------------------------------------------------------------------------------ writing

def _new_array(tf: hkfile.Tagfile, slot: hkfile.Value, count: int) -> Block | None:
    """A new array for the array member `slot` (its element type from the member), None for none."""
    if count == 0:
        slot.set_target(None)
        return None
    element = slot.type.eff_pointee
    if element is None:
        raise ValueError(f'{slot.type.full_name} has no element type')
    b = tf.new(element, count)
    slot.set_target(b)
    return b


@dataclass
class ConvexData:
    """What write_convex stores."""
    hull: ConvexHull
    radius: float
    penetration: float
    mass: MassProperties


def convex_data(points, radius: float = 0.0, inertia_factor: float = 1.0) -> ConvexData:  # noqa: ANN001
    """The hull of the points (not shrunk), rounded by `radius`, with its own mass properties (PrimitiveShapes.Convex)."""
    h = build_hull(points)
    return ConvexData(h, radius, PENETRATION_FACTOR * (h.min_width() + 2 * radius), from_hull(h, radius, inertia_factor))


def write_mass(tf: hkfile.Tagfile, shape: Block, mp: MassProperties | None, keep: Block | None = None) -> None:
    """The shape's property array: the 0xF100 mass properties (None: none), then the other entries of `keep` (the
    shape it replaces)."""
    entries: list[tuple[int, Block | None]] = []
    if mp is not None:
        entries.append((MASS_PROPERTIES_KEY, tf.new('hknpShapeMassProperties', 1, mass_bytes(mp))))
    old = properties(keep) if keep is not None else None
    if old is not None:
        entries += [(p['key'].int(), p['object'].target) for p in old if p['key'].int() != MASS_PROPERTIES_KEY]
    arr = _new_array(tf, shape.value['properties']['properties'], len(entries))
    for k, (key, obj) in enumerate(entries):
        arr[k]['object'].set_target(obj)      # type: ignore[index]
        arr[k]['key'].set_int(key)            # type: ignore[index]


def write_convex(tf: hkfile.Tagfile, data: ConvexData, keep: Block | None = None, flags: int = CONVEX_FLAGS) -> Block:
    """A new hknpConvexShape (HknpShapeWriter.Convex: type 0, dispatch 2, the stock flags) of `tf`'s own types."""
    shape = tf.new('hknpConvexShape')
    v = shape.value
    v['type'].set_int(0)
    v['dispatchType'].set_int(2)
    v['flags'].set_int(flags)
    v['numShapeKeyBits'].set_int(0)
    v['convexRadius'].set_f32(data.radius)
    v['userData'].set_int(0)
    write_mass(tf, shape, data.mass, keep)
    h, hv = data.hull, v['hull']
    a = _new_array(tf, hv['vertices'], len(h.vertices))
    if a is not None:
        a.data[:] = struct.pack(f'<{3 * len(h.vertices)}f', *(c for p in h.vertices for c in p))
    a = _new_array(tf, hv['planes'], len(h.planes))
    if a is not None:
        a.data[:] = struct.pack(f'<{4 * len(h.planes)}f', *(c for p in h.planes for c in p))
    a = _new_array(tf, hv['faces'], len(h.faces))
    for k, (first, num, angle) in enumerate(h.faces):
        a[k]['firstIndex'].set_int(first)          # type: ignore[index]
        a[k]['numIndices'].set_int(num)            # type: ignore[index]
        a[k]['minHalfAngle'].set_int(angle)        # type: ignore[index]
    a = _new_array(tf, hv['indices'], len(h.indices))
    if a is not None:
        a.data[:] = bytes(h.indices)
    for name, edges in (('faceLinks', h.face_links), ('vertexEdges', h.vertex_edges)):
        a = _new_array(tf, hv[name], len(edges))
        for k, (f, e) in enumerate(edges):
            a[k]['faceIndex'].set_int(f)           # type: ignore[index]
            a[k]['edgeIndex'].set_int(e)           # type: ignore[index]
    v['maxAllowedPenetration'].set_f32(data.penetration)
    return shape


def refresh_compound(tf: hkfile.Tagfile, compound: Block) -> Box:
    """A compound's derived data from its instances (CompoundCalculator, the rules of all 1,842 stock compounds):
    aabb = union of the instance boxes (rounded out to float32), bounding radius, numShapeKeyBits / estimated keys,
    a new simd tree. Its mass properties are left as they are. Returns the aabb."""
    insts = instances(compound)
    boxes = [instance_box(i) for i in insts]
    if not boxes or any(b is None for b in boxes):
        raise ValueError('a compound of convex shapes is needed')
    lo, hi = f32_out(*union(boxes))            # type: ignore[arg-type]
    v = compound.value
    v['aabb']['min'].set_vec((*lo, 0.0))
    v['aabb']['max'].set_vec((*hi, 0.0))
    v['boundingRadius'].set_f32(compound_radius(insts, (lo, hi)) * (1 + 1e-5))
    v['numShapeKeyBits'].set_int(len(insts).bit_length())
    v['estimatedNumShapeKeys'].set_int(len(insts))
    bvd = v['boundingVolumeData']
    set_simd_tree(tf, bvd['simdTree'], boxes, list(range(len(insts))))     # type: ignore[arg-type]
    bvd['type'].set_int(2)
    return lo, hi


def _section_tree(boxes: list[Box], domain: Box) -> bytes:
    import numpy as np
    import hkcms_build
    return hkcms_build._section_tree(np.asarray(boxes, dtype=np.float64).reshape(-1, 2, 3),
                                     np.asarray(domain, dtype=np.float64))


def write_convex_mesh(tf: hkfile.Tagfile, externs: list[Block], mass: float, keep: Block | None = None,
                      layout: dict | None = None, placement: list[tuple[Quat, Vec3]] | None = None) -> Block:
    """A new hknpCompressedMeshShape holding only the convex shapes `externs` (identity placement; HknpShapeWriter.
    ConvexMesh with the member values of the 82 stock ones, research/15 section 3): one section, primitive k -> the
    4-word block k of sharedVerticesIndex (146, 0, k, 0) -> extern k, shape key 2k; the section and simd trees over
    the externs' boxes; the domain their union; the mass properties box_mass over it. `layout` (convex_mesh_layout
    of a stock mesh) writes what that one stores instead of what is computed (tests/test_hkshape.py: all 82 again)."""
    n = len(externs)
    if not 0 < n <= MAX_MESH_SHAPES:
        raise ValueError(f'a convex mesh holds 1 to {MAX_MESH_SHAPES} shapes, not {n}')
    place = layout['placement'] if layout else placement or [((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0))] * n
    boxes = [instance_box((q, t, (1.0, 1.0, 1.0), s)) for (q, t), s in zip(place, externs)]
    if any(b is None for b in boxes):
        raise ValueError('every extern must be a convex shape')
    lo, hi = f32_out(*union(boxes))            # type: ignore[arg-type]
    dom_lo, dom_hi = (layout['domain'] if layout else ((*lo, 0.0), (*hi, 0.0)))
    blocks = layout['blocks'] if layout else list(range(n))
    shapes = layout['shapes'] if layout else list(range(n))
    max_key = 2 * (len(blocks) - 1)
    bits = max(1, max_key.bit_length())
    shape = tf.new('hknpCompressedMeshShape')
    v = shape.value
    v['type'].set_int(8)
    v['dispatchType'].set_int(3)
    v['flags'].set_int(4)
    v['numShapeKeyBits'].set_int(bits)
    v['convexRadius'].set_f32(0.0)
    v['userData'].set_int(0)
    write_mass(tf, shape, box_mass(lo, hi, mass), keep)
    if layout:
        mass_properties(shape).data[:] = layout['mass']      # type: ignore[union-attr]
    v['shapeTagCodecInfo'].set_int(0xFFFFFFFF)
    data = tf.new('hknpCompressedMeshShapeData')
    v['data'].set_target(data)
    tree = data.value['meshTree']
    top = _new_array(tf, tree['nodes'], 1)
    assert top is not None
    tree['domain']['min'].set_vec(dom_lo)
    tree['domain']['max'].set_vec(dom_hi)
    tree['numPrimitiveKeys'].set_int(len(blocks))
    tree['bitsPerKey'].set_int(bits)
    tree['maxKeyValue'].set_int(max_key)
    tree['primitiveStoresIsFlatConvex'].set_int(255)
    sections = _new_array(tf, tree['sections'], 1)
    sec = sections[0]                          # type: ignore[index]
    code = layout['section'] if layout else _section_tree([boxes[shapes[j]] for j in blocks], (lo, hi))  # type: ignore
    nodes = _new_array(tf, sec['nodes'], len(code) // 4)
    nodes.data[:] = code                       # type: ignore[union-attr]
    sec['domain']['min'].set_vec(dom_lo)
    sec['domain']['max'].set_vec(dom_hi)
    struct.pack_into('<3f3f', sections.data, sec['codecParms'].offset, EMPTY, EMPTY, EMPTY,   # type: ignore[union-attr]
                     -math.inf, -math.inf, -math.inf)
    sec['numPrimitives'].set_int(len(blocks))
    sec['numDataRuns'].set_int(1)
    sec['flags'].set_int(1)
    prims = _new_array(tf, tree['primitives'], len(blocks))
    prims.data[:] = b''.join(bytes((4 * j, 4 * j, 4, 4 * j)) for j in blocks)              # type: ignore[union-attr]
    words = _new_array(tf, tree['sharedVerticesIndex'], 4 * len(shapes))
    words.data[:] = b''.join(struct.pack('<4H', 146, 0, k, 0) for k in shapes)             # type: ignore[union-attr]
    run = _new_array(tf, tree['primitiveDataRuns'], 1)
    run[0]['value'].set_int(0xFFFF)            # type: ignore[index]
    run[0]['index'].set_int(0)                 # type: ignore[index]
    run[0]['count'].set_int(len(blocks))       # type: ignore[index]
    if layout:
        simd = data.value['simdTree']
        nb = _new_array(tf, simd['nodes'], len(layout['simd']) // 128)
        nb.data[:] = layout['simd']            # type: ignore[union-attr]
        simd['isCompact'].set_int(1)
    else:
        set_simd_tree(tf, data.value['simdTree'], [boxes[shapes[j]] for j in blocks],   # type: ignore[misc]
                      [2 * k for k in range(len(blocks))])
    data.value['hasSimdTree'].set_int(1)
    interior = v['triangleIsInterior']
    _new_array(tf, interior['words'], (max_key + 1 + 31) // 32)
    interior['numBits'].set_int(max_key + 1)
    v['numTriangles'].set_int(0)
    v['numConvexShapes'].set_int(n)
    arr = _new_array(tf, v['externShapes'], n)
    for k, s in enumerate(externs):
        e = arr[k]                             # type: ignore[index]
        q, t = place[k]
        e['rotation'].set_vec(q)
        e['translation'].set_vec(t)
        e['leafIndex'].set_int(layout['leaf'][k] if layout else 0xFFFF)
        moved, turned = sum(c * c for c in t) > 1e-12, abs(abs(q[3]) - 1) > 1e-7
        e['flags'].set_int(layout['flags'][k] if layout else 0x40 | (0x02 if moved else 0) | (0x04 if turned else 0))
        e['scale'].set_vec((1.0, 1.0, 1.0))
        e['shapeTag'].set_int(0xFFFF)
        e['destructionTag'].set_int(0xFFFF)
        e['shape'].set_target(s)
    return shape


def write_compound(tf: hkfile.Tagfile, children: list[tuple[Quat, Vec3, Block]], mass: MassProperties | None = None,
                   keep: Block | None = None, user_data: int = 0) -> Block:
    """A new hknpCompoundShape over written child shapes (HknpShapeWriter.Compound: type 11, dispatch 3, flags 4),
    its derived data by refresh_compound; `mass` its 0xF100 mass properties (None: the children's combined)."""
    comp = tf.new('hknpCompoundShape')
    v = comp.value
    v['type'].set_int(11)
    v['dispatchType'].set_int(3)
    v['flags'].set_int(4)
    v['convexRadius'].set_f32(0.0)
    v['userData'].set_int(user_data)
    v['shapeTagCodecInfo'].set_int(0xFFFFFFFF)
    arr = _new_array(tf, v['instances']['elements'], len(children))
    for k, (q, t, shape) in enumerate(children):
        e = arr[k]                             # type: ignore[index]
        moved = sum(c * c for c in t) > 1e-12
        turned = abs(abs(q[3]) - 1) > 1e-7
        e['rotation'].set_vec(q)
        e['translation'].set_vec(t)
        e['leafIndex'].set_int(0xFFFF)
        e['flags'].set_int(0x40 | (0x02 if moved else 0) | (0x04 if turned else 0))
        e['scale'].set_vec((1.0, 1.0, 1.0))
        e['shapeTag'].set_int(0xFFFF)
        e['destructionTag'].set_int(0xFFFF)
        e['shape'].set_target(shape)
    v['instances']['firstFree'].set_int(-1)
    v['instances']['numAllocated'].set_int(len(children))
    if mass is None:
        parts = []
        for q, t, shape in children:
            m = mass_properties(shape)
            if m is None:
                raise ValueError('a child without mass properties: give the compound its own')
            parts.append((read_mass(m), q, t))
        mass = combine(parts)
    write_mass(tf, comp, mass, keep)
    refresh_compound(tf, comp)
    return comp


def affine_convex(tf: hkfile.Tagfile, shape: Block, matrix, offset, inertia_factor: float | None = None) -> Block:  # noqa: ANN001
    """A new convex shape: `shape`'s hull under p -> matrix p + offset (a positive determinant; rows of `matrix`),
    built again (planes, faces, links, angles), its convex radius x the smallest singular value, its mass properties
    its own (from_hull; the inertia factor the old shape's, 1.0 or 1.5, unless given) and the other properties kept."""
    radius, verts, _ = convex_hull(shape)      # type: ignore[misc]
    M = [list(r) for r in matrix]
    det = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
           + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))
    if not det > 1e-12:
        raise HullError('an affine map that mirrors or collapses the hull')
    pts = [tuple(sum(M[r][c] * v[c] for c in range(3)) + offset[r] for r in range(3)) for v in verts]
    values, _ = eigen_symmetric(_mm(_tr(M), M))
    smallest = math.sqrt(max(min(values), 0.0))
    if inertia_factor is None:
        inertia_factor = 1.0
        old = mass_properties(shape)
        if old is not None:
            try:
                plain = from_hull(read_hull(shape), radius, 1.0)
                stored = read_mass(old)
                ratio = sum(stored.moments) / stored.mass / (sum(plain.moments) / plain.mass)
                inertia_factor = 1.5 if abs(ratio - 1.5) < abs(ratio - 1.0) else 1.0
            except (HullError, ZeroDivisionError):
                pass
    data = convex_data(pts, max(1e-5, radius * smallest), inertia_factor)
    return write_convex(tf, data, keep=shape)


def convex_mesh_layout(cms: Block) -> dict:
    """What a stock compressed mesh of convex shapes stores beyond its externs (to write it again as it is):
    domain, routing, both trees, mass properties, the externs' flags and leaf indices."""
    v = cms.value
    tree = v['data'].target.value['meshTree']
    sec = tree['sections'].target[0]
    prims = tree['primitives'].target
    words = tree['sharedVerticesIndex'].target
    ext = v['externShapes'].target
    return {'domain': (tree['domain']['min'].vec(4), tree['domain']['max'].vec(4)),
            'blocks': [prims.data[4 * k] // 4 for k in range(prims.count)],
            'shapes': [struct.unpack_from('<H', words.data, 2 * (4 * j + 2))[0] for j in range(words.count // 4)],
            'section': bytes(sec['nodes'].target.data),
            'simd': bytes(v['data'].target.value['simdTree']['nodes'].target.data),
            'mass': bytes(mass_properties(cms).data),
            'flags': [e['flags'].int() for e in ext], 'leaf': [e['leafIndex'].int() for e in ext],
            'placement': [(e['rotation'].vec(4), e['translation'].vec(3)) for e in ext]}
