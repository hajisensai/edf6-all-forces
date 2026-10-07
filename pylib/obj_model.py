"""Wavefront OBJ models into EDF6 MDB meshes, pure Python (pylib/mdb.py data classes; textures via pylib/texfile.py).

The pipeline (each step a plain function, the caller decides what goes where):

    obj = read_obj(path)                              OBJ + its MTL: objects, usemtl groups, v / vt / vn (negative
                                                      indices too), every face kept as a polygon
    parts = obj_parts(obj, Conversion(...))           one Part per (object, material) in game space: n-gons
                                                      ear-clipped, corners deduplicated into vertices (position,
                                                      normal, uv), UV v flipped to the game's top-left origin
    components(part) / split_part(part, key)          a part cut by connectivity (welded positions) or by a function
                                                      of each triangle's three positions (e.g. z > station)
    boundary_loops(part) / mirror_unmatched(loops)    open holes, and the ones the mirror side (x -> -x) does not have
    fill_loop(part, loop) / restore_from_reference()  close a hole by ear clipping, or with the triangles a reference
                                                      mesh has there (exact repair of an accidentally deleted face)
    split_by_reference(part, refs)                    a part cut by which reference mesh has each triangle
    skins_by_reference(part, ref, bone_map)           per-vertex bone influences copied from a reference mesh whose
                                                      triangles coincide (a kitbash of stock parts), bones mapped
    rigid(part, bone)                                 every vertex 100 % on one bone
    build_meshes(template, [(part, skins)], material) skinned MDB meshes in the template mesh's vertex layout,
                                                      tangents / binormals from the UVs, split below 65536 vertices
    model_dir(name)                                   where a user-supplied model folder is ($EDF6VC_MODELS, the release's
                                                      `models`, the developer's folder)
    add_material(md, template, ...) / add_texture(rab, filename, dds)
                                                      a material copied from a stock one (shader, parameters) with
                                                      new textures, and the texture's HD + .lod archive members

Conventions (measured on stock models, see docs/mdb-format.md): game space is +Y up, +Z forward, +X the vehicle's left
(its `_l` bones are at +x); a triangle's front face has normal cross(p1 - p0, p2 - p0) (counter-clockwise like OBJ);
UV (0, 0) is the texture's top-left (OBJ's is bottom-left: v' = 1 - v); tangent = dP/du, binormal = dP/dv, every w = 1.
An OBJ exported by Blender from a ripped EDF model is already in game space (Conversion() = identity): the twin tank's
E551 hull matches V505_TANK.MRAB vertex for vertex (pylib/artillery_model.py checks it).
"""
from __future__ import annotations

import math
import os
import struct
import sys
from dataclasses import dataclass, field, replace
from typing import Callable, Hashable, Iterable

import texfile
from mdb import MatTex, Mdb, Mesh, Rab, RabFile, Texture, cmpl_compress, folder_order_ok, insert_member
from mdb_jet import pack_vertex

Vec3 = tuple[float, float, float]
Vec2 = tuple[float, float]
Skin = tuple[tuple[int, float], ...]       # (bone, weight) pairs, weights summing to 1
MAX_VERTS = 0xFFFF                          # u16 index buffers: fewer than 65536 vertices per mesh
WELD = 1e-4                                 # m: positions closer than this are one point (stock vertices, half
                                            # floats, are >= 1e-3 apart; OBJ exports round to 1e-6)


class ObjError(Exception):
    pass


# ------------------------------------------------------------------------------------------ vectors

def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a: Vec3, k: float) -> Vec3:
    return (a[0] * k, a[1] * k, a[2] * k)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a: Vec3) -> Vec3:
    n = math.sqrt(dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 0.0, 0.0)


class _Welder:
    """Positions within WELD of a point seen earlier get that point's key (a grid of WELD cells, the 27 around a
    position searched): plain rounding would split two copies of one point that straddle a cell edge (an OBJ's
    6-decimal 0.995605 and the stock half float 0.99560546875)."""

    def __init__(self) -> None:
        self.cells: dict[tuple[int, int, int], list[tuple[Vec3, tuple[int, int, int]]]] = {}

    def key(self, p: Vec3) -> tuple[int, int, int]:
        c = (math.floor(p[0] / WELD), math.floor(p[1] / WELD), math.floor(p[2] / WELD))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q, k in self.cells.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                        if abs(q[0] - p[0]) <= WELD and abs(q[1] - p[1]) <= WELD and abs(q[2] - p[2]) <= WELD:
                            return k
        self.cells.setdefault(c, []).append((p, c))
        return c


_WELDER = _Welder()


def wkey(p: Vec3) -> tuple[int, int, int]:
    """The weld key of a position: equal for positions within WELD of each other (one process-wide welder, so keys
    compare across parts and reference meshes)."""
    return _WELDER.key(p)


# ------------------------------------------------------------------------------------------ model folders

DEV_MODELS = r'D:\APP\edf6-models'        # the developer's model folder


def model_roots() -> list[str]:
    """Where user-supplied models are looked for, in order: $EDF6VC_MODELS; `models` next to the installer (the
    frozen exe's folder in a release, the repository root otherwise: tools/build_release.py bundles it there);
    the developer's folder."""
    here = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    roots = [os.environ.get('EDF6VC_MODELS', ''), os.path.join(here, 'models'), DEV_MODELS]
    return [os.path.normpath(r) for r in roots if r]


def model_dir(name: str) -> str | None:
    """The first `<root>/<name>` folder of model_roots() that exists, None when none does (the model is not installed:
    callers skip what needs it, with a message)."""
    return next((os.path.join(r, name) for r in model_roots() if os.path.isdir(os.path.join(r, name))), None)


# ------------------------------------------------------------------------------------------ OBJ / MTL

@dataclass
class ObjMaterial:
    name: str
    diffuse_map: str | None         # map_Kd as written in the MTL (may be another machine's absolute path)
    diffuse: Vec3 = (1.0, 1.0, 1.0)


@dataclass
class ObjFace:
    material: str
    corners: tuple[tuple[int, int, int], ...]     # 0-based (v, vt, vn); -1 = not given


@dataclass
class ObjObject:
    name: str
    faces: list[ObjFace] = field(default_factory=list)


@dataclass
class ObjFile:
    path: str
    positions: list[Vec3]
    uvs: list[Vec2]
    normals: list[Vec3]
    objects: list[ObjObject]
    materials: dict[str, ObjMaterial]

    def object(self, name: str) -> ObjObject:
        hits = [o for o in self.objects if o.name == name]
        if len(hits) != 1:
            raise ObjError(f'{self.path}: {len(hits)} objects named {name}')
        return hits[0]


def read_mtl(path: str) -> dict[str, ObjMaterial]:
    out: dict[str, ObjMaterial] = {}
    cur: ObjMaterial | None = None
    with open(path, encoding='utf-8', errors='replace') as h:
        for line in h:
            t = line.strip().split(None, 1)
            if not t:
                continue
            if t[0] == 'newmtl':
                cur = out.setdefault(t[1].strip(), ObjMaterial(t[1].strip(), None))
            elif cur is not None and t[0] == 'map_Kd' and len(t) > 1:
                cur.diffuse_map = t[1].strip().split()[-1] if t[1].strip().startswith('-') else t[1].strip()
            elif cur is not None and t[0] == 'Kd' and len(t) > 1:
                cur.diffuse = tuple(float(x) for x in t[1].split()[:3])  # type: ignore[assignment]
    return out


def _index(s: str, count: int) -> int:
    """A 1-based (or negative, relative) OBJ index as 0-based; '' -> -1."""
    if not s:
        return -1
    i = int(s)
    return i - 1 if i > 0 else count + i


def read_obj(path: str) -> ObjFile:
    """The OBJ file and the materials of its mtllib(s). Faces before any `o` / `g` go to an object named after the
    file; `g` starts an object like `o` does when no `o` name is given."""
    pos: list[Vec3] = []
    uvs: list[Vec2] = []
    nrm: list[Vec3] = []
    objects: list[ObjObject] = []
    mats: dict[str, ObjMaterial] = {}
    cur: ObjObject | None = None
    mat = ''
    with open(path, encoding='utf-8', errors='replace') as h:
        for line in h:
            t = line.split()
            if not t or t[0].startswith('#'):
                continue
            if t[0] == 'v':
                pos.append((float(t[1]), float(t[2]), float(t[3])))
            elif t[0] == 'vt':
                uvs.append((float(t[1]), float(t[2]) if len(t) > 2 else 0.0))
            elif t[0] == 'vn':
                nrm.append((float(t[1]), float(t[2]), float(t[3])))
            elif t[0] in ('o', 'g') and len(t) > 1:
                if t[0] == 'o' or cur is None or cur.faces:
                    cur = ObjObject(' '.join(t[1:]))
                    objects.append(cur)
            elif t[0] == 'usemtl':
                mat = ' '.join(t[1:])
            elif t[0] == 'mtllib':
                lib = os.path.join(os.path.dirname(path), ' '.join(t[1:]))
                if os.path.exists(lib):
                    mats.update(read_mtl(lib))
            elif t[0] == 'f':
                if cur is None:
                    cur = ObjObject(os.path.splitext(os.path.basename(path))[0])
                    objects.append(cur)
                corners = []
                for c in t[1:]:
                    s = (c.split('/') + ['', ''])[:3]
                    corners.append((_index(s[0], len(pos)), _index(s[1], len(uvs)), _index(s[2], len(nrm))))
                if len(corners) >= 3:
                    cur.faces.append(ObjFace(mat, tuple(corners)))
    return ObjFile(path, pos, uvs, nrm, [o for o in objects if o.faces], mats)


def texture_path(obj: ObjFile, material: str, overrides: dict[str, str] | None = None) -> str:
    """The diffuse texture file of `material`: `overrides[material]` (a file name in the OBJ's folder) when given,
    else the MTL's map_Kd when it exists, else a file of the same base name in the OBJ's folder (case-insensitive;
    the MTL usually holds the exporting machine's absolute path)."""
    folder = os.path.dirname(obj.path)
    if overrides and material in overrides:
        p = os.path.join(folder, overrides[material])
        if not os.path.exists(p):
            raise ObjError(f'{material}: texture {p} not found')
        return p
    m = obj.materials.get(material)
    if m is None or not m.diffuse_map:
        raise ObjError(f'{obj.path}: material {material} has no map_Kd')
    if os.path.exists(m.diffuse_map):
        return m.diffuse_map
    base = os.path.basename(m.diffuse_map.replace('\\', '/')).lower()
    for f in os.listdir(folder):
        if f.lower() == base:
            return os.path.join(folder, f)
    raise ObjError(f'{material}: texture {m.diffuse_map} not found next to {obj.path}')


# ------------------------------------------------------------------------------------------ parts

@dataclass(frozen=True)
class Conversion:
    """OBJ space -> game space: p' = (p * axes) * scale + offset (row vector times the 3x3 `axes`); normals through
    `axes`; a mirroring `axes` (determinant < 0) also reverses every face's winding. flip_v: v' = 1 - v."""
    axes: tuple[Vec3, Vec3, Vec3] = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    scale: float = 1.0
    offset: Vec3 = (0.0, 0.0, 0.0)
    flip_v: bool = True

    def point(self, p: Vec3) -> Vec3:
        return add(scale(self.direction(p), self.scale), self.offset)

    def direction(self, d: Vec3) -> Vec3:
        a = self.axes
        return tuple(d[0] * a[0][c] + d[1] * a[1][c] + d[2] * a[2][c] for c in range(3))  # type: ignore[return-value]

    def mirrors(self) -> bool:
        return dot(self.axes[0], cross(self.axes[1], self.axes[2])) < 0


@dataclass(frozen=True)
class Vertex:
    pos: Vec3
    normal: Vec3
    uv: Vec2


@dataclass
class Part:
    """Triangles of one material in game space: `tris` index `verts`."""
    name: str
    material: str
    verts: list[Vertex]
    tris: list[tuple[int, int, int]]

    def positions(self) -> list[Vec3]:
        return [v.pos for v in self.verts]

    def box(self) -> tuple[Vec3, Vec3]:
        P = self.positions()
        return (tuple(min(p[c] for p in P) for c in range(3)),  # type: ignore[return-value]
                tuple(max(p[c] for p in P) for c in range(3)))


def ear_clip(points: list[Vec3]) -> list[tuple[int, int, int]]:
    """Triangles (indices into `points`) of a simple polygon in 3D, same winding as the polygon (ear clipping in the
    plane of its Newell normal; a degenerate polygon falls back to a fan)."""
    n = len(points)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]
    nrm = (0.0, 0.0, 0.0)
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        nrm = add(nrm, ((a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])))
    nrm = norm(nrm)
    if nrm == (0.0, 0.0, 0.0):
        return [(0, k, k + 1) for k in range(1, n - 1)]
    u = norm(cross(nrm, (1.0, 0.0, 0.0) if abs(nrm[0]) < 0.9 else (0.0, 1.0, 0.0)))
    v = cross(nrm, u)
    q = [(dot(p, u), dot(p, v)) for p in points]

    def area2(a: int, b: int, c: int) -> float:
        return (q[b][0] - q[a][0]) * (q[c][1] - q[a][1]) - (q[c][0] - q[a][0]) * (q[b][1] - q[a][1])

    def inside(p: int, a: int, b: int, c: int) -> bool:
        return area2(a, b, p) >= 0 and area2(b, c, p) >= 0 and area2(c, a, p) >= 0

    idx = list(range(n))
    out: list[tuple[int, int, int]] = []
    while len(idx) > 3:
        for k in range(len(idx)):
            a, b, c = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            if area2(a, b, c) <= 1e-14:
                continue
            if any(inside(p, a, b, c) for p in idx if p not in (a, b, c)):
                continue
            out.append((a, b, c))
            idx.pop(k)
            break
        else:                                   # no ear (self-intersecting / degenerate): fan the rest
            out += [(idx[0], idx[k], idx[k + 1]) for k in range(1, len(idx) - 1)]
            return out
    out.append((idx[0], idx[1], idx[2]))
    return out


def obj_parts(obj: ObjFile, conv: Conversion = Conversion(), objects: Iterable[str] | None = None) -> list[Part]:
    """One Part per (object, material) of `obj` (or of the named objects), converted to game space. A corner without
    a normal gets its face's; without a uv, (0, 0)."""
    want = set(objects) if objects is not None else None
    out: list[Part] = []
    for o in obj.objects:
        if want is not None and o.name not in want:
            continue
        groups: dict[str, Part] = {}
        index: dict[str, dict[tuple[int, int, int, Vec3], int]] = {}
        for f in o.faces:
            p = groups.setdefault(f.material, Part(o.name, f.material, [], []))
            ix = index.setdefault(f.material, {})
            pts = [conv.point(obj.positions[c[0]]) for c in f.corners]
            corners = list(f.corners)
            if conv.mirrors():
                corners.reverse()
                pts.reverse()
            fn = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
            vids = []
            for c, pt in zip(corners, pts):
                nv = norm(conv.direction(obj.normals[c[2]])) if c[2] >= 0 else fn
                key = (c[0], c[1], c[2], nv if c[2] < 0 else (0.0, 0.0, 0.0))
                if key not in ix:
                    uv = obj.uvs[c[1]] if c[1] >= 0 else (0.0, 0.0)
                    ix[key] = len(p.verts)
                    p.verts.append(Vertex(pt, nv, (uv[0], 1.0 - uv[1]) if conv.flip_v else uv))
                vids.append(ix[key])
            p.tris += [(vids[a], vids[b], vids[c]) for a, b, c in ear_clip(pts)]
        out += list(groups.values())
    return out


def subpart(part: Part, tris: list[tuple[int, int, int]], name: str | None = None) -> Part:
    """A part of only `tris` (vertices compacted)."""
    used = sorted({i for t in tris for i in t})
    remap = {v: k for k, v in enumerate(used)}
    return Part(name or part.name, part.material, [part.verts[v] for v in used],
                [(remap[a], remap[b], remap[c]) for a, b, c in tris])


def components(part: Part) -> list[Part]:
    """The connected pieces of `part` (triangles sharing a welded position), largest first."""
    parent: dict[tuple[int, int, int], tuple[int, int, int]] = {}

    def find(a: tuple[int, int, int]) -> tuple[int, int, int]:
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    keys = [wkey(v.pos) for v in part.verts]
    for t in part.tris:
        r = find(keys[t[0]])
        for i in t[1:]:
            parent[find(keys[i])] = r
    groups: dict[tuple[int, int, int], list[tuple[int, int, int]]] = {}
    for t in part.tris:
        groups.setdefault(find(keys[t[0]]), []).append(t)
    return sorted((subpart(part, ts, f'{part.name}#{k}') for k, ts in enumerate(groups.values())),
                  key=lambda p: -len(p.tris))


def split_part(part: Part, key: Callable[[tuple[Vec3, Vec3, Vec3]], Hashable]) -> dict[Hashable, Part]:
    """`part` cut by `key(the triangle's three positions)`: one part per key value."""
    groups: dict[Hashable, list[tuple[int, int, int]]] = {}
    for t in part.tris:
        groups.setdefault(key((part.verts[t[0]].pos, part.verts[t[1]].pos, part.verts[t[2]].pos)), []).append(t)
    return {k: subpart(part, ts, f'{part.name}:{k}') for k, ts in groups.items()}


def merge(parts: list[Part], name: str | None = None) -> Part:
    """One part of several (same material assumed by the caller)."""
    out = Part(name or parts[0].name, parts[0].material, [], [])
    for p in parts:
        base = len(out.verts)
        out.verts += p.verts
        out.tris += [(a + base, b + base, c + base) for a, b, c in p.tris]
    return out


# ------------------------------------------------------------------------------------------ reference meshes

@dataclass(frozen=True)
class RefCorner:
    """A corner of a reference mesh's triangle: what a coinciding OBJ corner can take from it."""
    pos: Vec3
    normal: Vec3
    skin: Skin


RefTri = tuple[RefCorner, RefCorner, RefCorner]


# ------------------------------------------------------------------------------------------ holes

@dataclass
class Fill:
    """A repair: the triangles added to `part` to close one hole."""
    part: str
    loop: list[Vec3]                 # the hole's boundary, in order
    tris: list[tuple[Vec3, Vec3, Vec3]]
    how: str                         # 'reference' / 'ear-clip'

    def describe(self) -> str:
        lo = [min(p[c] for p in self.loop) for c in range(3)]
        hi = [max(p[c] for p in self.loop) for c in range(3)]
        return (f'{self.part}: {len(self.loop)}-edge hole x {lo[0]:.3f}..{hi[0]:.3f} y {lo[1]:.3f}..{hi[1]:.3f} '
                f'z {lo[2]:.3f}..{hi[2]:.3f}, {len(self.tris)} triangles ({self.how})')


def _directed_edges(part: Part) -> dict[tuple[tuple[int, int, int], tuple[int, int, int]], tuple[int, int]]:
    """Welded directed edge -> (start vertex, end vertex) of the face that has it."""
    out: dict[tuple[tuple[int, int, int], tuple[int, int, int]], tuple[int, int]] = {}
    for t in part.tris:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            out[(wkey(part.verts[a].pos), wkey(part.verts[b].pos))] = (a, b)
    return out


def boundary_loops(part: Part) -> list[list[int]]:
    """Open boundaries of `part` as loops of vertex indices in face order (a hole's edges run a -> b in the faces
    around it; the triangles that close it run the loop backwards). Edges only one face has, with welded positions;
    a boundary vertex shared by two loops (a pinch) splits them at that vertex."""
    edges = _directed_edges(part)
    nxt: dict[tuple[int, int, int], list[tuple[int, int, int]]] = {}
    for (a, b) in edges:
        if (b, a) not in edges:
            nxt.setdefault(a, []).append(b)
    used: set[tuple[tuple[int, int, int], tuple[int, int, int]]] = set()
    loops: list[list[int]] = []
    for a0 in list(nxt):
        for b0 in nxt[a0]:
            if (a0, b0) in used:
                continue
            loop, a, b = [], a0, b0
            while (a, b) not in used:
                used.add((a, b))
                loop.append(edges[(a, b)][0])
                cand = [c for c in nxt.get(b, []) if (b, c) not in used]
                if not cand:
                    break
                a, b = b, cand[0]
            loops.append(loop)
    return loops


def mirror_unmatched(part: Part, loops: list[list[int]], axis: int = 0) -> list[list[int]]:
    """The loops whose mirror image (`axis` negated) is not also a boundary loop of `part`: on a mirror-symmetric
    model, the holes only one side has."""
    def mirrored(p: Vec3) -> tuple[int, int, int]:
        q = list(p)
        q[axis] = -q[axis]
        return wkey((q[0], q[1], q[2]))
    shapes = {frozenset(wkey(part.verts[i].pos) for i in L) for L in loops}
    return [L for L in loops if frozenset(mirrored(part.verts[i].pos) for i in L) not in shapes]


def fill_loop(part: Part, loop: list[int]) -> Fill:
    """Close the hole `loop` (from boundary_loops) by ear clipping, in place: new vertices on the loop's positions
    with the uv of the face beside each boundary edge and the fill's flat normal."""
    ring = list(reversed(loop))                  # the filling faces run the boundary backwards
    pts = [part.verts[i].pos for i in ring]
    tris = ear_clip(pts)
    nrm = norm(cross(sub(pts[tris[0][1]], pts[tris[0][0]]), sub(pts[tris[0][2]], pts[tris[0][0]]))) if tris else (0.0, 1.0, 0.0)
    base = len(part.verts)
    part.verts += [Vertex(part.verts[i].pos, nrm, part.verts[i].uv) for i in ring]
    part.tris += [(base + a, base + b, base + c) for a, b, c in tris]
    return Fill(part.name, pts, [(pts[a], pts[b], pts[c]) for a, b, c in tris], 'ear-clip')


def restore_from_reference(part: Part, ref: list[RefTri], tol: float = 1e-3) -> list[Fill]:
    """Close holes of `part` with the triangles of a reference mesh it was copied from: every reference triangle
    (RefTri, game-space winding) whose corners all coincide (within `tol`) with points of `part`, that
    `part` lacks, and that has an edge on `part`'s open boundary, is added (repeated until none is left, so a hole
    several triangles deep closes from its rim inward). New vertices take the part's position and the uv of the face
    beside the hole, the reference's normal. One Fill per closed hole (triangles grouped by shared edges)."""
    grid: dict[tuple[int, int, int], list[int]] = {}
    for i, v in enumerate(part.verts):
        grid.setdefault(tuple(int(math.floor(c / tol)) for c in v.pos), []).append(i)  # type: ignore[arg-type]

    def match(p: Vec3) -> int | None:
        c = [int(math.floor(x / tol)) for x in p]
        best, bi = tol, None
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for i in grid.get((c[0] + dx, c[1] + dy, c[2] + dz), []):
                        e = max(abs(part.verts[i].pos[k] - p[k]) for k in range(3))
                        if e <= best:
                            best, bi = e, i
        return bi

    added: list[tuple[Vec3, Vec3, Vec3]] = []
    loops_before = {wkey(part.verts[i].pos): i for L in boundary_loops(part) for i in L}
    while True:
        edges = _directed_edges(part)
        have = {frozenset(wkey(part.verts[i].pos) for i in t) for t in part.tris}
        new = 0
        for tri in ref:
            ids = [match(c.pos) for c in tri]
            if any(i is None for i in ids):
                continue
            ks = [wkey(part.verts[i].pos) for i in ids]  # type: ignore[index]
            if frozenset(ks) in have or len(set(ks)) < 3:
                continue
            # an edge of the new triangle a -> b must be open: the part has b -> a without a -> b's partner
            rim = [(ks[k], ks[(k + 1) % 3]) for k in range(3) if (ks[(k + 1) % 3], ks[k]) in edges]
            if not rim or any(e in edges for e in rim):
                continue
            uvs = [part.verts[i].uv for i in ids]  # type: ignore[index]
            for k in range(3):                       # corners on the rim take the uv of the face beside the hole
                face = edges.get((ks[(k + 1) % 3], ks[k]))
                if face is not None:
                    uvs[k], uvs[(k + 1) % 3] = part.verts[face[1]].uv, part.verts[face[0]].uv
            base = len(part.verts)
            for i, c, uv in zip(ids, tri, uvs):
                part.verts.append(Vertex(part.verts[i].pos, norm(c.normal), uv))  # type: ignore[index]
            part.tris.append((base, base + 1, base + 2))
            have.add(frozenset(ks))
            added.append(tuple(part.verts[i].pos for i in ids))  # type: ignore[arg-type, index]
            new += 1
            edges = _directed_edges(part)
        if not new:
            break
    # group the added triangles into holes (shared welded edges)
    fills: list[Fill] = []
    pending = list(added)
    while pending:
        group = [pending.pop()]
        grew = True
        while grew:
            grew = False
            pts = {wkey(p) for t in group for p in t}
            for t in list(pending):
                if sum(wkey(p) in pts for p in t) >= 2:
                    group.append(t)
                    pending.remove(t)
                    grew = True
        rim = [part.verts[loops_before[wkey(p)]].pos for t in group for p in t if wkey(p) in loops_before]
        loop = list(dict.fromkeys(rim))
        fills.append(Fill(part.name, loop, group, 'reference'))
    return fills


# ------------------------------------------------------------------------------------------ skinning

def rigid(part: Part, bone: int) -> list[Skin]:
    return [((bone, 1.0),)] * len(part.verts)


def split_by_reference(part: Part, refs: dict[str, list[RefTri]]) -> dict[str, Part]:
    """`part` cut by which reference mesh (name -> its triangles) has each triangle (same three welded positions):
    an OBJ object may mix pieces the source model draws with different materials. A triangle no reference has
    goes under the key ''."""
    owner: dict[frozenset, str] = {}
    for name, tris in refs.items():
        for t in tris:
            owner.setdefault(frozenset(wkey(c.pos) for c in t), name)
    return {str(k): v for k, v in split_part(part, lambda t: owner.get(frozenset(wkey(p) for p in t), '')).items()}


def skins_by_reference(part: Part, ref: list[RefTri], bone_map: Callable[[int], int]) -> list[Skin]:
    """Each vertex's influences from the reference mesh `part` was copied from: from the corner at its position of the
    reference triangle with the same three (welded) positions as a triangle using the vertex (a point where several
    bones' pieces meet carries one skin per piece; the face says which), bones mapped through `bone_map` (influences
    landing on the same bone summed). A vertex in no matching triangle takes the skin of another vertex at its point
    that has one, or the reference's skin there when all reference corners at that point agree; else it raises."""
    faces: dict[frozenset, RefTri] = {}
    for t in ref:
        faces.setdefault(frozenset(wkey(c.pos) for c in t), t)
    found: list[Skin | None] = [None] * len(part.verts)
    for t in part.tris:
        hit = faces.get(frozenset(wkey(part.verts[i].pos) for i in t))
        if hit is None:
            continue
        at = {wkey(c.pos): c.skin for c in hit}
        for i in t:
            if found[i] is None:
                found[i] = at[wkey(part.verts[i].pos)]
    at_point: dict[tuple[int, int, int], set[Skin]] = {}
    for t in ref:
        for c in t:
            at_point.setdefault(wkey(c.pos), set()).add(c.skin)
    for i, f in enumerate(found):     # a vertex whose faces the reference triangulates differently: its point's skin
        if f is None:
            k = wkey(part.verts[i].pos)
            same = [found[j] for j, v in enumerate(part.verts) if found[j] is not None and wkey(v.pos) == k]
            options = at_point.get(k, set())
            found[i] = same[0] if same else (next(iter(options)) if len(options) == 1 else None)
    missing = sum(f is None for f in found)
    if missing:
        raise ObjError(f'{part.name}: {missing} of {len(part.verts)} vertices match no reference triangle, and their '
                       f'point no single reference skin')
    out: list[Skin] = []
    for f in found:
        acc: dict[int, float] = {}
        for b, w in f:  # type: ignore[union-attr]
            acc[bone_map(b)] = acc.get(bone_map(b), 0.0) + w
        out.append(tuple(sorted(acc.items(), key=lambda bw: -bw[1])))
    return out


# ------------------------------------------------------------------------------------------ meshes

def tangent_frames(part: Part) -> list[tuple[Vec3, Vec3]]:
    """Per vertex (tangent, binormal): the UV gradients dP/du, dP/dv summed over its triangles (area weighted), made
    perpendicular to the normal; a vertex without usable UVs gets any perpendicular pair."""
    T = [(0.0, 0.0, 0.0)] * len(part.verts)
    B = [(0.0, 0.0, 0.0)] * len(part.verts)
    for t in part.tris:
        v0, v1, v2 = (part.verts[i] for i in t)
        e1, e2 = sub(v1.pos, v0.pos), sub(v2.pos, v0.pos)
        du1, dv1 = v1.uv[0] - v0.uv[0], v1.uv[1] - v0.uv[1]
        du2, dv2 = v2.uv[0] - v0.uv[0], v2.uv[1] - v0.uv[1]
        det = du1 * dv2 - du2 * dv1
        if abs(det) < 1e-12:
            continue
        s = 1.0 if det > 0 else -1.0                 # area weighting without the UV-area scale
        tu = scale(sub(scale(e1, dv2), scale(e2, dv1)), s)
        tv = scale(sub(scale(e2, du1), scale(e1, du2)), s)
        for i in t:
            T[i] = add(T[i], tu)
            B[i] = add(B[i], tv)
    out = []
    for v, t, b in zip(part.verts, T, B):
        n = v.normal
        t = norm(sub(t, scale(n, dot(n, t))))
        if t == (0.0, 0.0, 0.0):
            t = norm(cross(n, (0.0, 1.0, 0.0) if abs(n[1]) < 0.9 else (1.0, 0.0, 0.0)))
        b = norm(sub(b, scale(n, dot(n, b))))
        if b == (0.0, 0.0, 0.0) or abs(dot(b, t)) > 0.999:
            b = norm(cross(n, t))
        out.append((t, b))
    return out


def _row(elems: list, v: Vertex, tb: tuple[Vec3, Vec3], skin: Skin) -> list[tuple[float, ...]]:
    """The values of every layout element for one vertex."""
    bones = [b for b, _w in skin] + [0] * (4 - len(skin))
    weights = [w for _b, w in skin] + [0.0] * (4 - len(skin))
    row: list[tuple[float, ...]] = []
    for e in elems:
        n = e.name.lower()
        if n == 'position':
            row.append(v.pos + (1.0,))
        elif n == 'normal':
            row.append(v.normal + (1.0,))
        elif n == 'tangent':
            row.append(tb[0] + (1.0,))
        elif n == 'binormal':
            row.append(tb[1] + (1.0,))
        elif n == 'texcoord':
            row.append(v.uv)
        elif n == 'blendweight':
            row.append(tuple(weights))
        elif n == 'blendindices':
            row.append(tuple(bones))
        else:
            raise ObjError(f'vertex element {e.name} of the template layout is not handled')
        if len(row[-1]) < {1: 4, 4: 3, 7: 4, 12: 2, 21: 4}[e.fmt]:
            raise ObjError(f'vertex element {e.name}: format {e.fmt} wants more components')
        row[-1] = row[-1][:{1: 4, 4: 3, 7: 4, 12: 2, 21: 4}[e.fmt]]
    return row


def build_meshes(template: Mesh, pieces: list[tuple[Part, list[Skin]]], material: int) -> list[Mesh]:
    """Skinned meshes (material index `material`, mesh_index 0: the caller numbers them) of the pieces' triangles in
    `template`'s vertex layout, each with fewer than 65536 vertices (a piece is never split across meshes unless it
    alone is too big). flags = skinned, max influences actually used (1..4).
    Two kinds of triangle draw nothing and are not written (drawn_triangles): one with two corners at the same point as
    written (no area; the Sazabi's OBJ has 1,604), and one whose three vertices, packed, are those of one written before in
    the same turn (the same surface drawn twice). Every vertex's tangent frame is still taken over all of its piece's
    triangles, so what is written is byte for byte what was, less those."""
    if not any(e.name.lower() == 'blendindices' for e in template.elems):
        raise ObjError('the template mesh is not skinned')
    out: list[Mesh] = []
    rows: list[bytes] = []
    idx: list[int] = []
    infl = 1

    def flush() -> None:
        nonlocal rows, idx, infl
        if idx:
            out.append(replace(template, flags=bytes((0, 1, infl, 0)), material=material, mesh_index=0,
                               vdata=b''.join(rows), indices=struct.pack(f'<{len(idx)}H', *idx)))
        rows, idx, infl = [], [], 1

    seen: set[tuple[bytes, bytes, bytes]] = set()
    e = next(x for x in template.elems if x.name.lower() == 'position')
    at = ({1: '<4f', 4: '<3f', 7: '<4e'}[e.fmt], e.offset)
    for part, skins in pieces:
        if len(skins) != len(part.verts) or any(not s or len(s) > 4 for s in skins):
            raise ObjError(f'{part.name}: one skin of 1..4 influences per vertex needed')
        frames = tangent_frames(part)
        packed: dict[int, bytes] = {}

        def vertex(i: int) -> bytes:
            if i not in packed:
                packed[i] = pack_vertex(template.elems, template.vsize, _row(template.elems, part.verts[i], frames[i], skins[i]))
            return packed[i]
        remap: dict[int, int] = {}
        for t in drawn_triangles(part.tris, vertex, at, seen):
            if len(rows) + sum(i not in remap for i in t) > MAX_VERTS:
                flush()
                remap = {}
            for i in t:
                if i not in remap:
                    remap[i] = len(rows)
                    rows.append(vertex(i))
                    infl = max(infl, len(skins[i]))
                idx.append(remap[i])
    flush()
    return out


def drawn_triangles(tris: list[tuple[int, int, int]], vertex: Callable[[int], bytes], at: tuple[str, int],
                    seen: set[tuple[bytes, bytes, bytes]]) -> list[tuple[int, int, int]]:
    """`tris` less the ones that draw nothing: two corners at one point (their packed vertices' (`vertex`) positions,
    `at` = (struct format, offset), equal as numbers, -0 = 0: no area as drawn), or the three packed vertices of one
    already in `seen` (any of its three rotations: the same winding), which collects the kept ones' (build_meshes)."""
    out = []
    for t in tris:
        r = tuple(vertex(i) for i in t)
        a, b, c = (struct.unpack_from(at[0], x, at[1])[:3] for x in r)
        if a == b or b == c or a == c:
            continue
        key = min(r, r[1:] + r[:1], r[2:] + r[:2])
        if key in seen:
            continue
        seen.add(key)   # type: ignore[arg-type]
        out.append(t)
    return out


# ------------------------------------------------------------------------------------------ materials / archives

def add_material(md: Mdb, template: Mdb, template_material: str, name: str,
                 textures: dict[str, str]) -> tuple[Mdb, int]:
    """`md` with a new material `name`: `template`'s material `template_material` (shader, parameters, texture
    slot kinds and order) with each slot's texture replaced by `textures[kind]` (a file name such as 'x.dds'; a kind
    left out keeps the template's file, which the archive must then hold). Texture entries are added (by file
    name, deduplicated). Returns (model, material index)."""
    src = next((m for m in template.materials if template.name_of(m.name) == template_material), None)
    if src is None:
        raise ObjError(f'template material {template_material} not found')
    names = list(md.names)
    texs = list(md.textures)
    slots = []
    for x in src.textures:
        fn = textures.get(x.kind, template.textures[x.texture].filename)
        hit = next((t.index for t in texs if t.filename.lower() == fn.lower()), None)
        if hit is None:
            hit = len(texs)
            texs.append(Texture(hit, fn.rsplit('.', 1)[0] + '_DDS', fn, 0))
        slots.append(MatTex(hit, x.kind, x.unk))
    unknown = set(textures) - {x.kind for x in src.textures}
    if unknown:
        raise ObjError(f'template material {template_material} has no texture slot {sorted(unknown)}')
    if name in names:
        ni = names.index(name)
    else:
        ni = len(names)
        names.append(name)
    mat = replace(src, index=len(md.materials), name=ni, textures=slots, params=[replace(p) for p in src.params])
    return replace(md, names=names, textures=texs, materials=list(md.materials) + [mat]), mat.index


def add_texture(rab: Rab, filename: str, dds: bytes) -> list[str]:
    """Add texture `filename` ('x.dds') to `rab` as the game stores it: `x.dds` (HD-TEXTURE, flag 1) and
    `x.lod.dds` (TEXTURE, flag 0, the mip tail texfile.texture_pair cuts), CMPL-compressed, each placed by
    mdb.insert_member (folder-table order: the .lod before the model, which binds its textures when it loads). An
    existing member of the same name is replaced. Returns the member names."""
    hd, lod = texfile.texture_pair(dds)
    stem, ext = filename.rsplit('.', 1)
    added = []
    for name, folder, flag, data in ((filename, 'HD-TEXTURE', 1, hd), (f'{stem}.lod.{ext}', 'TEXTURE', 0, lod)):
        if folder not in rab.folders:
            rab.folders.append(folder)
        rab.files = [f for f in rab.files if f.name.lower() != name.lower()]
        insert_member(rab, RabFile(name, rab.folders.index(folder), flag, cmpl_compress(data)))
        added.append(name)
    return added


def texture_problems(rab: Rab, md: Mdb, model_member: str) -> list[str]:
    """What keeps the model `md` (archive member `model_member` of `rab`) from getting its textures in game: members
    not in folder-table order; a material texture whose HD member (HD-TEXTURE, flag 1) or .lod member (TEXTURE, flag
    0) is missing, or whose .lod comes after the model (mdb.insert_member: the model binds its textures when it
    loads, a later one is never bound and the model renders black); an HD / .lod pair that breaks the stock layout
    (texfile.pair_problem)."""
    out: list[str] = []
    if not folder_order_ok(rab):
        out.append('archive members not in folder-table order (' +
                   ', '.join(dict.fromkeys(rab.folders[f.folder] for f in rab.files)) + ')')
    at = {f.name.lower(): k for k, f in enumerate(rab.files)}
    model_at = at.get(model_member.lower())
    if model_at is None:
        return out + [f'no member {model_member}']
    for fn in sorted({t.filename for t in md.textures}):
        stem, ext = fn.rsplit('.', 1)
        hd, lod = (rab.files[at[n.lower()]] if n.lower() in at else None for n in (fn, f'{stem}.lod.{ext}'))
        if hd is None or lod is None:
            out.append(f'{fn}: HD or .lod member missing')
            continue
        if (rab.folders[hd.folder], hd.flag, rab.folders[lod.folder], lod.flag) != ('HD-TEXTURE', 1, 'TEXTURE', 0):
            out.append(f'{fn}: members not in HD-TEXTURE (flag 1) / TEXTURE (flag 0)')
        if at[lod.name.lower()] > model_at:
            out.append(f'{lod.name} after the model {model_member}: the model loads before it and never binds it')
        bad = texfile.pair_problem(hd.data, lod.data)
        if bad:
            out.append(f'{fn}: {bad}')
    return out


def texture_dds(path: str) -> bytes:
    """A texture file as a DDS the game reads: a .dds as it is (validated), a PNG / JPEG converted to DXT1."""
    with open(path, 'rb') as h:
        head = h.read(4)
    if head == texfile.DDS_MAGIC:
        with open(path, 'rb') as h:
            data = h.read()
        texfile.texture_pair(data)
        return data
    return texfile.dxt1_dds(texfile.load_image(path))
