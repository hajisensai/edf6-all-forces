"""Ogre 1.x binary meshes and skeletons as X-Morph: Defense ships them (docs/xmorph-port.md).

The game's meshes carry the stock '[MeshSerializer_v1.41]' header (map chunks, props) or EXOR's own
'[ExorSerializer_v1.41]' (units): the same chunk layout, so one reader takes both; skeletons are
'[Serializer_v1.10]'. Every chunk is (u16 id, u32 length counting its 6-byte header); what is not read
here is skipped by its length, so an unknown chunk never shifts the rest.

    read_mesh(data) -> Mesh            submeshes (material, positions, normals, uvs, triangles, bone weights)
    read_skeleton(data) -> Skeleton    bones (name, parent, local position / rotation / scale), animation names

Coordinates are the game's own (Z up, metres); conversion to EDF's frame is the caller's (pylib/xm_model.py).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

M_HEADER = 0x1000
M_MESH = 0x3000
M_SUBMESH = 0x4000
M_SUBMESH_OPERATION = 0x4010
M_SUBMESH_BONE_ASSIGNMENT = 0x4100
M_SUBMESH_TEXTURE_ALIAS = 0x4200
M_GEOMETRY = 0x5000
M_GEOMETRY_VERTEX_DECLARATION = 0x5100
M_GEOMETRY_VERTEX_ELEMENT = 0x5110
M_GEOMETRY_VERTEX_BUFFER = 0x5200
M_GEOMETRY_VERTEX_BUFFER_DATA = 0x5210
M_MESH_SKELETON_LINK = 0x6000
M_MESH_BONE_ASSIGNMENT = 0x7000
M_MESH_BOUNDS = 0x9000
M_SUBMESH_NAME_TABLE = 0xA000
M_SUBMESH_NAME_TABLE_ELEMENT = 0xA100

SKELETON_BONE = 0x2000
SKELETON_BONE_PARENT = 0x3000
SKELETON_ANIMATION = 0x4000

# VertexElementSemantic / VertexElementType (OgreHardwareVertexBuffer.h)
SEM_POSITION, SEM_BLEND_WEIGHTS, SEM_BLEND_INDICES, SEM_NORMAL, SEM_DIFFUSE, SEM_SPECULAR, SEM_TEXCOORD = 1, 2, 3, 4, 5, 6, 7
SEM_BINORMAL, SEM_TANGENT = 8, 9
_TYPES = {0: ('<f4', 1), 1: ('<f4', 2), 2: ('<f4', 3), 3: ('<f4', 4), 4: ('u1', 4), 5: ('<i2', 1), 6: ('<i2', 2),
          7: ('<i2', 3), 8: ('<i2', 4), 9: ('u1', 4), 10: ('u1', 4), 11: ('u1', 4)}


class OgreError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise OgreError(message)


class _Reader:
    def __init__(self, data: bytes, pos: int = 0) -> None:
        self.d, self.p = data, pos

    def take(self, fmt: str):
        v = struct.unpack_from('<' + fmt, self.d, self.p)
        self.p += struct.calcsize('<' + fmt)
        return v if len(v) > 1 else v[0]

    def string(self) -> str:
        end = self.d.index(b'\n', self.p)
        s = self.d[self.p:end].decode('utf-8')
        self.p = end + 1
        return s

    def chunks(self, allowed: set[int], containers: frozenset[int] = frozenset()):
        """(id, chunk start, length) of each following chunk while its id is in allowed: Ogre's own rule. A
        container's stored length does not count the chunks Ogre writes after it (a submesh's bone assignments), so
        a container is left where its reader stops; any other chunk is skipped to its stored end."""
        while self.p + 6 <= len(self.d):
            start = self.p
            cid, length = struct.unpack_from('<HI', self.d, start)
            if cid not in allowed:
                return
            _req(cid in containers or (length >= 6 and start + length <= len(self.d)),
                 f'chunk {cid:#06x} at {start}: length {length}')
            self.p = start + 6
            yield cid, start, length
            if cid not in containers:
                self.p = start + length


@dataclass
class Element:
    source: int
    type: int
    semantic: int
    offset: int
    index: int


@dataclass
class Geometry:
    count: int
    elements: list[Element]
    buffers: dict[int, bytes]           # bind index -> raw vertex data

    def attribute(self, semantic: int, index: int = 0) -> np.ndarray | None:
        el = next((e for e in self.elements if e.semantic == semantic and e.index == index), None)
        if el is None:
            return None
        _req(el.type in _TYPES, f'vertex element type {el.type} (semantic {semantic})')
        raw = self.buffers[el.source]
        stride = len(raw) // self.count if self.count else 0
        dtype, n = _TYPES[el.type]
        view = np.ndarray((self.count, n), dtype=dtype, buffer=raw, offset=el.offset, strides=(stride, np.dtype(dtype).itemsize))
        return np.array(view)


@dataclass
class SubMesh:
    material: str
    indices: np.ndarray                  # (n, 3) triangles
    geometry: Geometry | None            # None: the mesh's shared geometry
    bone_assignments: list[tuple[int, int, float]] = field(default_factory=list)   # (vertex, bone, weight)
    name: str = ''


@dataclass
class Mesh:
    version: str
    submeshes: list[SubMesh]
    shared: Geometry | None
    skeleton: str
    shared_bones: list[tuple[int, int, float]]
    bounds: tuple[tuple[float, float, float], tuple[float, float, float], float] | None

    def geometry(self, sub: SubMesh) -> Geometry:
        g = sub.geometry or self.shared
        _req(g is not None, f'{sub.material}: no geometry')
        return g

    def positions(self, sub: SubMesh) -> np.ndarray:
        p = self.geometry(sub).attribute(SEM_POSITION)
        _req(p is not None, f'{sub.material}: no positions')
        return p[:, :3].astype(np.float64)

    def normals(self, sub: SubMesh) -> np.ndarray | None:
        n = self.geometry(sub).attribute(SEM_NORMAL)
        return None if n is None else n[:, :3].astype(np.float64)

    def uvs(self, sub: SubMesh, index: int = 0) -> np.ndarray | None:
        uv = self.geometry(sub).attribute(SEM_TEXCOORD, index)
        return None if uv is None else uv[:, :2].astype(np.float64)

    def rigid_bones(self, sub: SubMesh) -> np.ndarray:
        """Each vertex's heaviest bone (-1 with none): X-Morph's units are rigid pieces, one bone a vertex."""
        g = self.geometry(sub)
        out = np.full(g.count, -1, dtype=np.int32)
        best = np.zeros(g.count)
        for v, b, w in (sub.bone_assignments if sub.geometry is not None else self.shared_bones):
            if w > best[v]:
                out[v], best[v] = b, w
        return out


_GEOMETRY_CHILDREN = {M_GEOMETRY_VERTEX_DECLARATION, M_GEOMETRY_VERTEX_BUFFER}
_SUBMESH_CHILDREN = {M_GEOMETRY, M_SUBMESH_OPERATION, M_SUBMESH_BONE_ASSIGNMENT, M_SUBMESH_TEXTURE_ALIAS}
_MESH_CHILDREN = {M_GEOMETRY, M_SUBMESH, M_MESH_SKELETON_LINK, M_MESH_BONE_ASSIGNMENT, M_MESH_BOUNDS,
                  M_SUBMESH_NAME_TABLE, 0x8000, 0xB000, 0xC000, 0xD000, 0xE000}   # LOD, edges, poses, animations, table


def _geometry(r: _Reader) -> Geometry:
    count = r.take('I')
    elements: list[Element] = []
    buffers: dict[int, bytes] = {}
    for cid, _start, _len in r.chunks(_GEOMETRY_CHILDREN):
        if cid == M_GEOMETRY_VERTEX_DECLARATION:
            for _eid, _s, _l in r.chunks({M_GEOMETRY_VERTEX_ELEMENT}):
                elements.append(Element(*r.take('HHHHH')))
        else:
            bind, size = r.take('HH')
            for _did, dstart, dlen in r.chunks({M_GEOMETRY_VERTEX_BUFFER_DATA}):
                _req(dlen - 6 == size * count, f'vertex buffer {bind}: {dlen - 6} bytes, {size} x {count}')
                buffers[bind] = r.d[dstart + 6:dstart + dlen]
    _req(all(e.source in buffers for e in elements), 'vertex element without its buffer')
    return Geometry(count, elements, buffers)


def _bone_assignment(r: _Reader) -> tuple[int, int, float]:
    return r.take('IHf')


def _submesh(r: _Reader) -> SubMesh:
    material = r.string()
    shared = r.take('?')
    count = r.take('I')
    wide = r.take('?')
    dtype = '<u4' if wide else '<u2'
    idx = np.frombuffer(r.d, dtype=dtype, count=count, offset=r.p).astype(np.int64)
    r.p += count * (4 if wide else 2)
    sub = SubMesh(material, idx, None)
    operation = 4                        # OT_TRIANGLE_LIST
    for cid, _start, _len in r.chunks(_SUBMESH_CHILDREN, frozenset({M_GEOMETRY})):
        if cid == M_GEOMETRY:
            _req(not shared, f'{material}: own geometry on a shared-vertex submesh')
            sub.geometry = _geometry(r)
        elif cid == M_SUBMESH_OPERATION:
            operation = r.take('H')
        elif cid == M_SUBMESH_BONE_ASSIGNMENT:
            sub.bone_assignments.append(_bone_assignment(r))
    _req(operation == 4, f'{material}: operation {operation} (only triangle lists)')
    _req(count % 3 == 0, f'{material}: {count} indices')
    sub.indices = idx.reshape(-1, 3)
    return sub


def read_mesh(data: bytes) -> Mesh:
    r = _Reader(data)
    _req(r.take('H') == M_HEADER, 'not an Ogre mesh')
    version = r.string()
    _req(version.endswith('_v1.41]'), f'mesh version {version}')
    subs: list[SubMesh] = []
    shared = None
    skeleton = ''
    shared_bones: list[tuple[int, int, float]] = []
    bounds = None
    for _cid, _start, _len in r.chunks({M_MESH}, frozenset({M_MESH})):
        r.take('?')                      # skeletally animated
        for sid, _s, _l in r.chunks(_MESH_CHILDREN, frozenset({M_GEOMETRY, M_SUBMESH, 0xD000})):
            if sid == 0xD000:            # animations: their stored lengths are not trusted, and they are the last
                break
            if sid == M_GEOMETRY:
                shared = _geometry(r)
            elif sid == M_SUBMESH:
                subs.append(_submesh(r))
            elif sid == M_MESH_SKELETON_LINK:
                skeleton = r.string()
            elif sid == M_MESH_BONE_ASSIGNMENT:
                shared_bones.append(_bone_assignment(r))
            elif sid == M_MESH_BOUNDS:
                v = r.take('7f')
                bounds = (tuple(v[0:3]), tuple(v[3:6]), v[6])
            elif sid == M_SUBMESH_NAME_TABLE:
                for _nid, _ns, _nl in r.chunks({M_SUBMESH_NAME_TABLE_ELEMENT}):
                    i = r.take('H')
                    if i < len(subs):
                        subs[i].name = r.string()
    _req(bool(subs), 'mesh without submeshes')
    for s in subs:
        g = s.geometry or shared
        _req(g is not None, f'{s.material}: no geometry')
        _req(int(s.indices.max(initial=-1)) < g.count, f'{s.material}: index past {g.count} vertices')
    return Mesh(version, subs, shared, skeleton, shared_bones, bounds)


@dataclass
class Bone:
    name: str
    handle: int
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]      # (w, x, y, z)
    scale: tuple[float, float, float]
    parent: int = -1


@dataclass
class Skeleton:
    bones: list[Bone]
    animations: list[str]

    def index(self, name: str) -> int:
        for i, b in enumerate(self.bones):
            if b.name == name:
                return i
        raise OgreError(f'no bone {name}')

    def world(self) -> list[np.ndarray]:
        """Each bone's 4x4 bind-pose transform in model space."""
        out: list[np.ndarray | None] = [None] * len(self.bones)
        by_handle = {b.handle: i for i, b in enumerate(self.bones)}

        def get(i: int) -> np.ndarray:
            if out[i] is None:
                b = self.bones[i]
                m = np.eye(4)
                m[:3, :3] = quat_matrix(b.rotation) * np.asarray(b.scale)
                m[:3, 3] = b.position
                out[i] = m if b.parent < 0 else get(by_handle[b.parent]) @ m
            return out[i]
        return [get(i) for i in range(len(self.bones))]


def quat_matrix(q: tuple[float, float, float, float]) -> np.ndarray:
    w, x, y, z = q
    n = (w * w + x * x + y * y + z * z) ** .5 or 1.
    w, x, y, z = w / n, x / n, y / n, z / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


def read_skeleton(data: bytes) -> Skeleton:
    r = _Reader(data)
    _req(r.take('H') == M_HEADER, 'not an Ogre skeleton')
    version = r.string()
    _req(version == '[Serializer_v1.10]', f'skeleton version {version}')
    bones: list[Bone] = []
    anims: list[str] = []
    # The stored lengths are not the bones' (a bone's counts neither its name nor its scale), so every chunk is read
    # to its end by its content and a bone has a scale when what follows it is not the next chunk.
    ids = {SKELETON_BONE, SKELETON_BONE_PARENT, SKELETON_ANIMATION}
    for cid, _start, _length in r.chunks(ids, frozenset(ids)):
        if cid == SKELETON_ANIMATION:
            anims.append(r.string())
            break                                # tracks follow; nothing past them is read
        if cid == SKELETON_BONE:
            name = r.string()
            handle = r.take('H')
            pos = r.take('3f')
            rot = r.take('4f')                   # stored x, y, z, w
            nxt = struct.unpack_from('<H', r.d, r.p)[0] if r.p + 2 <= len(r.d) else None
            scale = (1., 1., 1.) if nxt is None or nxt in ids else r.take('3f')
            bones.append(Bone(name, handle, pos, (rot[3], rot[0], rot[1], rot[2]), scale))
        elif cid == SKELETON_BONE_PARENT:
            child, parent = r.take('HH')
            by_handle = {b.handle: b for b in bones}
            _req(child in by_handle, f'parent link for unknown bone {child}')
            by_handle[child].parent = parent
        elif cid == SKELETON_ANIMATION:
            anims.append(r.string())
    return Skeleton(bones, anims)
