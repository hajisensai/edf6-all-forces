"""hknpCompressedMeshShape (the terrain collision in <map>.MAD) decoded and its vertex heights edited in place.
Format notes: docs/map-collision.md. Every rule below was checked against all 995 compressed meshes of
IG_HEIGEN601.MAD (selftest in tools/make_bigmap.py --check).

Compact layouts used (pylib/hktag.py reads them from TBDY; the offsets are looked up, not hard coded):
  hknpCompressedMeshShape   +28 data -> hknpCompressedMeshShapeData
  hknpCompressedMeshShapeData: meshTree (hknpCompressedMeshShapeTree) at 0, simdTree (hkcdSimdTree) after it
  meshTree: nodes (Aabb5BytesCodec), domain (hkAabb), sections, primitives, sharedVerticesIndex, packedVertices,
            sharedVertices, primitiveDataRuns
  Section: nodes (Aabb4BytesCodec), domain, codecParms[6], firstPackedVertexIndex, firstSharedVertexIndex,
           firstPrimitiveIndex, ..., numPackedVertices, numPrimitives

Vertices: a primitive is 4 u8 indices (a triangle when the last two are equal). An index below the section's
numPackedVertices is a packed vertex (u32: x 11 bits, y 11, z 10; pos = parms[0:3] + q * parms[3:6]); above it,
sharedVerticesIndex[firstShared + i - numPacked] picks a shared vertex (u64: x 21 bits, y 21, z 22, quantized over
the meshTree domain: pos = lo + q * (hi - lo) / (2^bits - 1)).

AABB trees (hkcdStaticTree): nodes in depth-first order, the root's box is coded against the tree's domain and each
child's against its parent's decoded box: per axis one byte, high nibble a, low nibble b,
min = parent.min + a^2 / 226 * extent, max = parent.max - b^2 / 226 * extent. Aabb4BytesCodec's 4th byte: even =
leaf of primitive (byte >> 1), odd = inner node, children at i + 1 and i + 2 * (byte >> 1).
hkcdSimdTree (a mesh's primitives, a compound's instances): 4-wide float boxes; node 0 is an empty sentinel, an
inner lane's box is the union of its child node's lanes (data = child node), a leaf lane's box is the object's
(data = primitive key = primitive index << 1 in a one-section mesh / instance index); empty lanes are inverted
(+FLT_MAX / -FLT_MAX).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from typing import Callable

import numpy as np

from hktag import Tag

Q21 = (1 << 21) - 1
Q22 = (1 << 22) - 1
FLT_MAX = 3.4028234663852886e38


def _aabb(b: bytes | bytearray, at: int) -> np.ndarray:
    v = struct.unpack_from('<8f', b, at)
    return np.array([v[0:3], v[4:7]], dtype=np.float64)


def _f32_out(box: np.ndarray) -> np.ndarray:
    """`box` rounded outward to float32 (what the game reads must still contain it)."""
    lo = np.asarray(box[0], dtype=np.float32)
    hi = np.asarray(box[1], dtype=np.float32)
    lo = np.where(lo > box[0], np.nextafter(lo, np.float32(-np.inf)), lo)
    hi = np.where(hi < box[1], np.nextafter(hi, np.float32(np.inf)), hi)
    return np.array([lo, hi], dtype=np.float64)


def _put_aabb(b: bytearray, at: int, box: np.ndarray) -> None:
    lo, hi = _f32_out(box)
    w_lo = struct.unpack_from('<f', b, at + 12)[0]
    w_hi = struct.unpack_from('<f', b, at + 28)[0]
    struct.pack_into('<8f', b, at, lo[0], lo[1], lo[2], w_lo, hi[0], hi[1], hi[2], w_hi)


def decode_box(parent: np.ndarray, xyz: bytes) -> np.ndarray:
    ext = parent[1] - parent[0]
    a = np.array([c >> 4 for c in xyz], dtype=np.float64)
    c = np.array([c & 15 for c in xyz], dtype=np.float64)
    return np.array([parent[0] + a * a / 226.0 * ext, parent[1] - c * c / 226.0 * ext])


def encode_box(parent: np.ndarray, box: np.ndarray, margin: float = 1e-3) -> bytes:
    """The tightest code whose decoded box still contains `box` grown by `margin` (never smaller)."""
    box = np.array([box[0] - margin, box[1] + margin])
    out = []
    ext = parent[1] - parent[0]
    for k in range(3):
        if ext[k] <= 0:
            out.append(0)
            continue
        lo = max(0.0, (box[0][k] - parent[0][k]) / ext[k] * 226.0)
        hi = max(0.0, (parent[1][k] - box[1][k]) / ext[k] * 226.0)
        a = min(15, int(math.floor(math.sqrt(lo))))
        c = min(15, int(math.floor(math.sqrt(hi))))
        while a and parent[0][k] + a * a / 226.0 * ext[k] > box[0][k]:
            a -= 1
        while c and parent[1][k] - c * c / 226.0 * ext[k] < box[1][k]:
            c -= 1
        out.append(a << 4 | c)
    return bytes(out)


@dataclass
class Section:
    at: int              # absolute offset of the Section record
    nodes: int           # item index of its Aabb4BytesCodec array
    domain: np.ndarray
    parms: np.ndarray
    first_packed: int
    first_shared: int
    first_prim: int
    num_packed: int
    num_prims: int


class Cms:
    """One hknpCompressedMeshShape inside `buf` (the tagfile bytes, edited in place)."""

    def __init__(self, tag: Tag, buf: bytearray, shape_item: int) -> None:
        self.tag, self.buf = tag, buf
        _, shape_at, _ = tag.item(shape_item)
        self.data_item = tag.u32(shape_at + tag.offset('hknpCompressedMeshShape', 'data'))
        _, self.data_at, _ = tag.item(self.data_item)
        T = 'hknpCompressedMeshShapeTree'
        d = self.data_at
        off = {m: tag.offset(T, m) for m in ('nodes', 'domain', 'sections', 'primitives', 'sharedVerticesIndex',
                                              'packedVertices', 'sharedVertices')}
        self.tree_nodes = tag.u32(d + off['nodes'])
        self.domain_at = d + off['domain']
        self.domain = _aabb(buf, self.domain_at)
        self.simd_item = tag.u32(d + tag.offset('hknpCompressedMeshShapeData', 'simdTree')
                                 + tag.offset('hkcdSimdTree', 'nodes'))
        self.prims = self._arr(tag.u32(d + off['primitives']), 'u1').reshape(-1, 4)
        self.svi = self._arr(tag.u32(d + off['sharedVerticesIndex']), '<u2')
        self.pv_item = tag.u32(d + off['packedVertices'])
        self.sv_item = tag.u32(d + off['sharedVertices'])
        self.pv = self._arr(self.pv_item, '<u4')
        self.sv = self._arr(self.sv_item, '<u8')
        S = 'hkcdStaticMeshTree::Section'
        so = {m: tag.offset(S, m) for m in ('nodes', 'domain', 'codecParms', 'firstPackedVertexIndex',
                                            'firstSharedVertexIndex', 'firstPrimitiveIndex', 'numPackedVertices',
                                            'numPrimitives')}
        ssize = tag.size(S)
        sec_item = tag.u32(d + off['sections'])
        self.sections: list[Section] = []
        if sec_item:
            _, sat, n = tag.item(sec_item)
            for k in range(n):
                at = sat + k * ssize
                self.sections.append(Section(
                    at, tag.u32(at + so['nodes']), _aabb(buf, at + so['domain']),
                    np.array(struct.unpack_from('<6f', buf, at + so['codecParms']), dtype=np.float64),
                    tag.u32(at + so['firstPackedVertexIndex']), tag.u32(at + so['firstSharedVertexIndex']) & 0xFFFFFF,
                    tag.u32(at + so['firstPrimitiveIndex']), buf[at + so['numPackedVertices']],
                    buf[at + so['numPrimitives']]))
        self._sec_domain_off = so['domain']

    def _arr(self, idx: int, dt: str) -> np.ndarray:
        if not idx:
            return np.zeros(0, dtype=dt)
        _, at, n = self.tag.item(idx)
        size = np.dtype(dt).itemsize
        k = 4 if dt == 'u1' else 1
        return np.frombuffer(bytes(self.buf[at:at + n * size * k]), dtype=dt).copy()

    # ------------------------------------------------------------------ decode

    def shared_positions(self) -> np.ndarray:
        v = self.sv.astype(np.uint64)
        q = np.stack([v & Q21, (v >> np.uint64(21)) & Q21, v >> np.uint64(42)], 1).astype(np.float64)
        lo, hi = self.domain
        return lo + q * (hi - lo) / np.array([Q21, Q21, Q22], dtype=np.float64)

    def packed_positions(self, s: Section) -> np.ndarray:
        v = self.pv[s.first_packed:s.first_packed + s.num_packed].astype(np.int64)
        q = np.stack([v & 0x7FF, (v >> 11) & 0x7FF, v >> 22], 1).astype(np.float64)
        return s.parms[:3] + q * s.parms[3:]

    def section_vertices(self, s: Section) -> list[tuple[str, int]]:
        """('p', packed index) / ('s', shared index) of every vertex the section's primitives use."""
        out = []
        for pr in self.prims[s.first_prim:s.first_prim + s.num_prims]:
            for i in {int(x) for x in pr}:
                out.append(('p', s.first_packed + i) if i < s.num_packed
                           else ('s', int(self.svi[s.first_shared + i - s.num_packed])))
        return sorted(set(out))

    def triangles(self) -> np.ndarray:
        sp = self.shared_positions()
        tris = []
        for s in self.sections:
            pp = self.packed_positions(s)

            def vtx(i: int) -> np.ndarray:
                return pp[i] if i < s.num_packed else sp[self.svi[s.first_shared + i - s.num_packed]]
            for a, b, c, d in self.prims[s.first_prim:s.first_prim + s.num_prims].astype(int):
                tris.append((vtx(a), vtx(b), vtx(c)))
                if c != d:
                    tris.append((vtx(a), vtx(c), vtx(d)))
        return np.array(tris).reshape(-1, 3, 3)

    # -------------------------------------------------------------------- edit

    def set_heights(self, fn: Callable[[np.ndarray], np.ndarray]) -> bool:
        """y := fn(positions (n, 3)) for every vertex; returns whether anything changed. Refreshes the mesh's
        domains, its section trees and its simd tree; the caller refreshes the compound holding it."""
        changed = False
        sp = self.shared_positions()
        new_sy = fn(sp) if len(sp) else np.zeros(0)
        packed_new: dict[int, np.ndarray] = {}
        for k, s in enumerate(self.sections):
            pp = self.packed_positions(s)
            if len(pp):
                packed_new[k] = fn(pp)
        if len(sp) and np.abs(new_sy - sp[:, 1]).max() > 1e-4:
            changed = True
        if any(np.abs(packed_new[k] - self.packed_positions(self.sections[k])[:, 1]).max() > 1e-4 for k in packed_new):
            changed = True
        if not changed:
            return False
        if len(sp):
            self._requantize_shared(sp, new_sy)
        for k, ny in packed_new.items():
            self._requantize_packed(self.sections[k], ny)
        self._refresh_bounds()
        return True

    def _requantize_shared(self, sp: np.ndarray, ny: np.ndarray) -> None:
        lo, hi = self.domain.copy()
        lo[1], hi[1] = float(ny.min()), float(ny.max()) + 1e-3
        lo, hi = _f32_out(np.array([lo, hi]))
        ylo, yhi = lo[1], hi[1]
        v = self.sv.astype(np.uint64)
        qy = np.rint((ny - ylo) / (yhi - ylo) * Q21).astype(np.uint64)
        v = (v & ~(np.uint64(Q21) << np.uint64(21))) | (qy << np.uint64(21))
        self.sv = v.astype('<u8')
        _, at, n = self.tag.item(self.sv_item)
        self.buf[at:at + 8 * n] = self.sv.tobytes()
        self.domain = np.array([lo, hi])
        _put_aabb(self.buf, self.domain_at, self.domain)

    def _requantize_packed(self, s: Section, ny: np.ndarray) -> None:
        ylo, yhi = float(ny.min()), float(ny.max())
        scale = max((yhi - ylo) / 0x7FF, 1e-7)
        parms = s.parms.copy()
        parms[1], parms[4] = ylo, scale
        v = self.pv[s.first_packed:s.first_packed + s.num_packed].astype(np.int64)
        qy = np.clip(np.rint((ny - ylo) / scale), 0, 0x7FF).astype(np.int64)
        v = (v & ~(0x7FF << 11)) | (qy << 11)
        self.pv[s.first_packed:s.first_packed + s.num_packed] = v.astype('<u4')
        _, at, _ = self.tag.item(self.pv_item)
        o = at + 4 * s.first_packed
        self.buf[o:o + 4 * s.num_packed] = self.pv[s.first_packed:s.first_packed + s.num_packed].tobytes()
        s.parms = parms
        struct.pack_into('<6f', self.buf, s.at + self.tag.offset('hkcdStaticMeshTree::Section', 'codecParms'),
                         *parms)

    def _prim_box(self, s: Section, sp: np.ndarray, pp: np.ndarray, p: int) -> np.ndarray:
        idx = {int(x) for x in self.prims[s.first_prim + p]}
        pts = np.array([pp[i] if i < s.num_packed else sp[self.svi[s.first_shared + i - s.num_packed]] for i in idx])
        return np.array([pts.min(0), pts.max(0)])

    def _refresh_bounds(self) -> None:
        sp = self.shared_positions()
        boxes = []
        prim_boxes: list[np.ndarray] = []
        for s in self.sections:
            pp = self.packed_positions(s)
            pb = [self._prim_box(s, sp, pp, p) for p in range(s.num_prims)]
            prim_boxes += pb
            box = np.array([np.min([b[0] for b in pb], 0), np.max([b[1] for b in pb], 0)])
            # x / z of the section domain stay (the packed codec and the stock data use them); y covers the new ones
            dom = s.domain.copy()
            dom[0][1], dom[1][1] = box[0][1], box[1][1]
            dom[0] = np.minimum(dom[0], box[0])
            dom[1] = np.maximum(dom[1], box[1])
            s.domain = dom
            _put_aabb(self.buf, s.at + self._sec_domain_off, dom)
            self._encode_section_tree(s, pb)
            boxes.append(dom)
        self.domain[0] = np.minimum(self.domain[0], np.min([b[0] for b in boxes], 0))
        self.domain[1] = np.maximum(self.domain[1], np.max([b[1] for b in boxes], 0))
        _put_aabb(self.buf, self.domain_at, self.domain)
        self._encode_mesh_tree()
        refresh_simd(self.tag, self.buf, self.simd_item, lambda key: prim_boxes[key >> 1])

    def _encode_section_tree(self, s: Section, prim_boxes: list[np.ndarray]) -> None:
        _, at, n = self.tag.item(s.nodes)

        def subtree(i: int) -> np.ndarray:
            d = self.buf[at + 4 * i + 3]
            if d & 1:
                a, b = subtree(i + 1), subtree(i + 2 * (d >> 1))
                return np.array([np.minimum(a[0], b[0]), np.maximum(a[1], b[1])])
            return prim_boxes[d >> 1]

        def code(i: int, parent: np.ndarray) -> None:
            box = subtree(i)
            xyz = encode_box(parent, box)
            self.buf[at + 4 * i:at + 4 * i + 3] = xyz
            dec = decode_box(parent, xyz)
            d = self.buf[at + 4 * i + 3]
            if d & 1:
                code(i + 1, dec)
                code(i + 2 * (d >> 1), dec)
        if n:
            code(0, s.domain)

    def _encode_mesh_tree(self) -> None:
        if not self.tree_nodes:
            return
        _, at, n = self.tag.item(self.tree_nodes)
        if n != 1 or len(self.sections) != 1:
            raise NotImplementedError('mesh tree with %d nodes / %d sections' % (n, len(self.sections)))
        self.buf[at:at + 3] = encode_box(self.domain, self.sections[0].domain)


def refresh_simd(tag: Tag, buf: bytearray, simd_item: int,
                 leaf_box: Callable[[int], np.ndarray]) -> np.ndarray | None:
    """Leaf lanes := their object's box, inner lanes := union of their child; returns the root union."""
    if not simd_item:
        return None
    _, at, n = tag.item(simd_item)
    N = 'hkcdSimdTree::Node'
    o_data = tag.offset(N, 'data')
    o_leaf = tag.offset(N, 'isLeaf')
    size = tag.size(N)

    def lanes(k: int) -> tuple[np.ndarray, np.ndarray]:
        f = np.frombuffer(bytes(buf[at + size * k:at + size * k + 96]), '<f4').reshape(6, 4).astype(np.float64)
        return np.stack([f[0], f[2], f[4]], 1), np.stack([f[1], f[3], f[5]], 1)

    def write(k: int, lo: np.ndarray, hi: np.ndarray) -> None:
        f = np.stack([lo[:, 0], hi[:, 0], lo[:, 1], hi[:, 1], lo[:, 2], hi[:, 2]]).astype('<f4')
        buf[at + size * k:at + size * k + 96] = f.tobytes()

    def node(k: int) -> np.ndarray:
        lo, hi = lanes(k)
        data = struct.unpack_from('<4I', buf, at + size * k + o_data)
        leaf = buf[at + size * k + o_leaf]
        for l in range(4):
            if lo[l, 0] > hi[l, 0]:
                continue
            box = leaf_box(data[l]) if leaf else node(data[l])
            lo[l], hi[l] = box[0], box[1]
        write(k, lo, hi)
        used = lo[:, 0] <= hi[:, 0]
        return np.array([lo[used].min(0), hi[used].max(0)])
    if n < 2:
        return None
    return node(1)


class Compound:
    """A body's hknpCompoundShape of compressed meshes (the terrain pieces)."""

    def __init__(self, tag: Tag, buf: bytearray, shape_item: int) -> None:
        self.tag, self.buf = tag, buf
        C = 'hknpCompoundShape'
        _, self.at, _ = tag.item(shape_item)
        inst_item = tag.u32(self.at + tag.offset(C, 'instances'))
        _, iat, n = tag.item(inst_item)
        isz = tag.size('hknpShapeInstance')
        o_shape = tag.offset('hknpShapeInstance', 'shape')
        self.instances: list[Cms] = []
        for k in range(n):
            ia = iat + k * isz
            rot_tr = struct.unpack_from('<7f', buf, ia)
            scale = struct.unpack_from('<3f', buf, ia + tag.offset('hknpShapeInstance', 'scale'))
            if rot_tr != (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0) or scale != (1.0, 1.0, 1.0):
                raise NotImplementedError('compound instance %d has a transform' % k)
            sh = tag.u32(ia + o_shape)
            if tag.item(sh)[0] != 'hknpCompressedMeshShape':
                raise NotImplementedError('compound instance %d is a %s' % (k, tag.item(sh)[0]))
            self.instances.append(Cms(tag, buf, sh))
        bvd = self.at + tag.offset(C, 'boundingVolumeData')
        self.simd_item = tag.u32(bvd + tag.offset('hknpCompoundShapeData', 'simdTree')
                                 + tag.offset('hkcdSimdTree', 'nodes'))
        self.aabb_at = self.at + tag.offset(C, 'aabb')

    def set_heights(self, fn: Callable[[np.ndarray], np.ndarray]) -> int:
        n = sum(1 for c in self.instances if c.set_heights(fn))
        if n:
            root = refresh_simd(self.tag, self.buf, self.simd_item, lambda k: self.instances[k].domain)
            box = _aabb(self.buf, self.aabb_at)
            if root is not None:
                box = np.array([np.minimum(box[0], root[0]), np.maximum(box[1], root[1])])
                box[0][1], box[1][1] = root[0][1], root[1][1]
            _put_aabb(self.buf, self.aabb_at, box)
        return n

    def triangles(self) -> np.ndarray:
        return np.concatenate([c.triangles() for c in self.instances])


def bodies(tag: Tag) -> dict[str, int]:
    """Body name -> its shape item, for every body of every system in the scene."""
    scene = tag.root_variant('hknpPhysicsSceneData')
    out: dict[str, int] = {}
    _, sat, ns = tag.item(tag.u32(scene + tag.offset('hknpPhysicsSceneData', 'systemDatas')))
    B = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
    bsize = tag.size(B)
    o_shape, o_name = tag.offset(B, 'shape'), tag.offset(B, 'name')
    for k in range(ns):
        _, sys_at, _ = tag.item(tag.u32(sat + 4 * k))
        bi = tag.u32(sys_at + tag.offset('hknpPhysicsSystemData', 'bodyCinfos'))
        if not bi:
            continue
        _, bat, nb = tag.item(bi)
        for j in range(nb):
            at = bat + j * bsize
            out[tag.cstr(tag.u32(at + o_name))] = tag.u32(at + o_shape)
    return out


def bound_problems(tag: Tag, buf: bytes | bytearray, compound_item: int) -> list[str]:
    """Every box of a compound of compressed meshes that does not contain what it should (empty = sound): the
    compound's aabb and simd tree, each mesh's domain, section domains, section trees and simd tree."""
    out: list[str] = []
    eps = 2e-3
    comp = Compound(tag, bytearray(buf), compound_item)
    cbox = _aabb(buf, comp.aabb_at)

    def inside(p: np.ndarray, box: np.ndarray) -> bool:
        return bool((p >= box[0] - eps).all() and (p <= box[1] + eps).all())

    def simd(item_idx: int, leaf_pts: Callable[[int], np.ndarray], what: str) -> None:
        _, at, n = tag.item(item_idx)
        for k in range(1, n):
            o = at + 128 * k
            f = np.frombuffer(bytes(buf[o:o + 96]), '<f4').reshape(6, 4).astype(np.float64)
            lo, hi = np.stack([f[0], f[2], f[4]], 1), np.stack([f[1], f[3], f[5]], 1)
            if not buf[o + 112]:
                continue
            data = struct.unpack_from('<4I', buf, o + 96)
            for l in range(4):
                if lo[l, 0] <= hi[l, 0] and not inside(leaf_pts(data[l]), np.array([lo[l], hi[l]])):
                    out.append('%s simd node %d lane %d' % (what, k, l))
    for ci, c in enumerate(comp.instances):
        tris = c.triangles().reshape(-1, 3)
        if not inside(tris, c.domain) or not inside(tris, cbox):
            out.append('mesh %d outside its domain / the compound aabb' % ci)
        sp = c.shared_positions()
        prim_pts: list[np.ndarray] = []
        for s in c.sections:
            pp = c.packed_positions(s)

            def pts(p: int, s: Section = s, pp: np.ndarray = pp) -> np.ndarray:
                idx = {int(x) for x in c.prims[s.first_prim + p]}
                return np.array([pp[i] if i < s.num_packed else sp[c.svi[s.first_shared + i - s.num_packed]]
                                 for i in idx])
            allp = np.concatenate([pts(p) for p in range(s.num_prims)])
            if not inside(allp, s.domain):
                out.append('mesh %d section outside its domain' % ci)
            _, at, n = tag.item(s.nodes)

            def walk(i: int, parent: np.ndarray) -> None:
                box = decode_box(parent, bytes(buf[at + 4 * i:at + 4 * i + 3]))
                d = buf[at + 4 * i + 3]
                if d & 1:
                    walk(i + 1, box)
                    walk(i + 2 * (d >> 1), box)
                elif not inside(pts(d >> 1), box):
                    out.append('mesh %d section tree node %d' % (ci, i))
            if n:
                walk(0, s.domain)
            prim_pts += [pts(p) for p in range(s.num_prims)]
        simd(c.simd_item, lambda key: prim_pts[key >> 1], 'mesh %d' % ci)
    simd(comp.simd_item, lambda k: comp.instances[k].triangles().reshape(-1, 3), 'compound')
    return out
