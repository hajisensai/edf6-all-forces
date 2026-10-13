"""EDF6 map collision (`collision.hkt` in <map>.MAD) written from scratch geometry: a Havok 2020.1 tagfile writer plus a
builder of static terrain / object bodies (hknpCompoundShape of hknpCompressedMeshShape tiles), in the layout the
stock EDF6 files have (docs/map-collision.md; the tree rules are pylib/hkcms.py's). EDF6 has no heightfield shape the
map loader can use, so the terrain converted from EDF4.1 (pylib/sk_terrain.py) goes in as a triangle mesh like EDF6's
own ground (ig_heigen507_2).

  load(raw) -> Doc                  a tagfile as items (type, flags, bytes, count) + the donor's type tables
  Doc.emit() -> bytes               the tagfile again: with nothing changed, byte for byte the same file
  build_collision(donor, terrain_name, heights, x0, z0, step, bodies, tile_cells) -> bytes   the collision.hkt
  write_mad(hkt) -> bytes           the MAD (a MARC holding that file)

The donor is a stock EDF6 `collision.hkt` of the player's own game (IG_HEIGEN601.MAD's): its type tables, material /
motion libraries and physics system are kept, its bodies replaced. Nothing of it is stored in the repo.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

import hktag

PRIM = {'hkUint32': (4, 4), 'unsigned int': (4, 4), 'hkUint16': (2, 2), 'hkUint8': (1, 1), 'char': (1, 1),
        'unsigned long long': (8, 4), 'hkVector4': (16, 16),
        'hkFreeListArrayElement<hknpShapeInstance>': (64, 16),
        'hknpThreadSafeObjectPoolElement<hknpMaterial>': (60, 4),
        'hknpThreadSafeObjectPoolElement<hknpMotionProperties>': (104, 4)}


@dataclass
class Item:
    tidx: int            # type index (TNA1 order)
    flags: int           # 16 = one object, 32 = an array (the stock files' two values)
    count: int
    data: bytes


@dataclass
class Doc:
    sdkv: bytes                  # payload of SDKV
    tst1: bytes
    tna1: bytes
    fst1: bytes
    tbdy: bytes
    tpad: bytes
    items: list[Item]            # [0] is the null item
    layout: list[int] = field(default_factory=list)    # item indices in DATA order
    tag: hktag.Tag | None = None
    aligns: dict[int, tuple[int | None, int | None]] = field(default_factory=dict)   # type index -> (size, align)

    def tname(self, tidx: int) -> str:
        return self.tag.tname(tidx)

    def size_align(self, tidx: int) -> tuple[int, int]:
        name = self.tname(tidx)
        if name in PRIM:
            return PRIM[name]
        s, a = self.aligns.get(tidx, (None, None))
        if s is None:
            raise ValueError('no size for type ' + name)
        return s, a or 1

    def add(self, type_name: str, count: int, data: bytes, flags: int = 32) -> int:
        """A new item (its index)."""
        tidx = self.tag.type_index(type_name)
        size, _ = self.size_align(tidx)
        if len(data) != size * count:
            raise ValueError(f'{type_name}: {len(data)} bytes for {count} x {size}')
        self.items.append(Item(tidx, flags, count, data))
        self.layout.append(len(self.items) - 1)
        return len(self.items) - 1

    # --------------------------------------------------------------------------------------------- emit

    def emit(self) -> bytes:
        data = bytearray()
        offs = [0] * len(self.items)
        for i in self.layout:
            it = self.items[i]
            _, a = self.size_align(it.tidx)
            data += bytes(-len(data) % a)
            offs[i] = len(data)
            data += it.data
        data += bytes(-len(data) % 16)              # DATA ends on a 16 byte boundary
        index = bytearray()
        for it, o in zip(self.items, offs):
            index += struct.pack('<III', it.tidx | it.flags << 24, o, it.count)
        types = _sec(b'TST1', self.tst1, True) + _sec(b'TNA1', self.tna1, True) + _sec(b'FST1', self.fst1, True) + \
            _sec(b'TBDY', self.tbdy, True) + _sec(b'TPAD', self.tpad, True)
        body = _sec(b'SDKV', self.sdkv, True) + _sec(b'DATA', bytes(data), True) + _sec(b'TYPE', types, False) + \
            _sec(b'INDX', _sec(b'ITEM', bytes(index), True), False)
        return _sec(b'TAG0', body, False)


def _sec(tag: bytes, payload: bytes, leaf: bool) -> bytes:
    return struct.pack('>I', (1 << 30 if leaf else 0) | (len(payload) + 8)) + tag + payload


def _tbdy_sizes(b: bytes, o: int, end: int) -> dict[int, tuple[int | None, int | None]]:
    """type index -> (size, alignment) of every TBDY entry (hktag.Tag drops the alignment)."""
    out: dict[int, tuple[int | None, int | None]] = {}
    while o < end:
        ti, o = hktag.varint(b, o)
        if ti == 0:
            continue
        _, o = hktag.varint(b, o)
        opt, o = hktag.varint(b, o)
        for bit in (1, 2, 4):
            if opt & bit:
                _, o = hktag.varint(b, o)
        size = align = None
        if opt & 8:
            size, o = hktag.varint(b, o)
            align, o = hktag.varint(b, o)
        if opt & 0x10:
            _, o = hktag.varint(b, o)
        if opt & 0x20:
            n, o = hktag.varint(b, o)
            for _ in range((n & 0xFFFF) * 4):
                _, o = hktag.varint(b, o)
        if opt & 0x40:
            n, o = hktag.varint(b, o)
            for _ in range(2 * n):
                _, o = hktag.varint(b, o)
        if opt & 0x80:
            _, o = hktag.varint(b, o)
        out[ti] = (size, align)
    return out


def load(raw: bytes) -> Doc:
    """A tagfile as a Doc (ValueError when its sections are not the stock layout)."""
    tag = hktag.Tag(raw)
    sec = hktag.sections(raw)
    part = {k: raw[a:b] for k, (a, b) in sec.items()}
    doc = Doc(part['/TAG0/SDKV'], part['/TAG0/TYPE/TST1'], part['/TAG0/TYPE/TNA1'], part['/TAG0/TYPE/FST1'],
              part['/TAG0/TYPE/TBDY'], part['/TAG0/TYPE/TPAD'], [], [], tag,
              _tbdy_sizes(raw, *sec['/TAG0/TYPE/TBDY']))
    d0 = sec['/TAG0/DATA'][0]
    spans = []
    for f, o, n in tag.items:
        tidx, fl = f & 0xFFFFFF, f >> 24
        if tidx == 0:
            doc.items.append(Item(0, 0, 0, b''))
            continue
        size, _ = doc.size_align(tidx)
        doc.items.append(Item(tidx, fl, n, raw[d0 + o:d0 + o + size * n]))
        spans.append((o, len(doc.items) - 1))
    doc.layout = [i for _, i in sorted(spans)]
    return doc


# ------------------------------------------------------------------------------------------------ pointers

_PTR_PREFIX = ('hkRelPtr<', 'hkRelArray<', 'hkRelArrayView<', 'hkArray<', 'hkRefPtr<', 'hkFreeListRelArray<')


def pointer_offsets(doc: Doc, tidx: int) -> list[int]:
    """Byte offsets inside one object of `tidx` that hold an item index (compact layout: every pointer and array is
    one u32 item index)."""
    cache = doc.__dict__.setdefault('_ptr_cache', {})
    if tidx in cache:
        return cache[tidx]
    tag = doc.tag
    out: list[int] = []

    def walk(ti: int, base: int) -> None:
        for m in tag.members(ti).values():
            name = tag.tname(m.type)
            if name.startswith(_PTR_PREFIX) or name == 'hkStringPtr' or '*' in name:
                out.append(base + m.offset)
            elif m.type in tag.bodies and tag.members(m.type):
                walk(m.type, base + m.offset)
    walk(tidx, 0)
    cache[tidx] = sorted(set(out))
    return cache[tidx]


def pointers(doc: Doc, i: int) -> list[tuple[int, int]]:
    """(byte offset in the item's data, target item) of every non-null pointer of item i."""
    it = doc.items[i]
    size, _ = doc.size_align(it.tidx)
    out = []
    for off in pointer_offsets(doc, it.tidx):
        for k in range(it.count):
            v = struct.unpack_from('<I', it.data, k * size + off)[0]
            if v:
                out.append((k * size + off, v))
    return out


def reachable(doc: Doc, roots: list[int]) -> set[int]:
    seen: set[int] = set()
    todo = list(roots)
    while todo:
        i = todo.pop()
        if i in seen:
            continue
        seen.add(i)
        todo += [v for _, v in pointers(doc, i)]
    return seen


# ------------------------------------------------------------------------------------------------ meshes

import hkcms  # noqa: E402  (decode_box / encode_box / the quantisation constants are its)

Q21, Q22 = hkcms.Q21, hkcms.Q22
QMAX = np.array([Q21, Q21, Q22], dtype=np.float64)
EMPTY_LO = struct.unpack('<f', struct.pack('<I', 0x7F7FFFEE))[0]    # an empty simd lane: lo = +huge, hi = -huge
EMPTY_HI = struct.unpack('<f', struct.pack('<I', 0xFF7FFFEE))[0]
MAX_PRIMS = 127          # Aabb4BytesCodec byte 3: leaf = prim << 1 (u8) and child offset = leaves of the first child << 1
MAX_VERTS = 256          # a primitive's indices are u8
MIN_EXTENT = 1e-3        # a mesh domain thinner than this on an axis is widened (the shared vertices divide by it)


def _out32(box: np.ndarray) -> np.ndarray:
    """`box` (2, 3) rounded outward to float32, as float64."""
    return hkcms._f32_out(np.asarray(box, dtype=np.float64))


@dataclass
class Mesh:
    """One hknpCompressedMeshShape's arrays (one section, every vertex shared)."""
    domain: np.ndarray           # (2, 3) float64, float32 exact
    sv: np.ndarray               # (n,) uint64 packed shared vertices
    pos: np.ndarray              # (n, 3) float64 their decoded positions
    prims: np.ndarray            # (m, 4) uint8
    values: np.ndarray           # (m,) uint8 per primitive material index
    boxes: np.ndarray            # (m, 2, 3) float64 each primitive's box (decoded vertices)
    num_triangles: int
    max_key: int


def make_mesh(verts: np.ndarray, prims: np.ndarray, values: np.ndarray | None = None) -> Mesh:
    """Quantise `verts` (n, 3) over their float32 domain and describe `prims` (m, 4) (a, b, c, d; a triangle repeats c
    as d; the triangles are (a, b, c) and (a, c, d)). ValueError beyond what one section holds."""
    n, m = len(verts), len(prims)
    if not 0 < m <= MAX_PRIMS or n > MAX_VERTS:
        raise ValueError(f'a section holds at most {MAX_PRIMS} primitives / {MAX_VERTS} vertices, got {m} / {n}')
    dom = _out32(np.array([verts.min(0), verts.max(0)]))
    ext = dom[1] - dom[0]
    for k in range(3):
        if ext[k] < MIN_EXTENT:
            dom[1][k] = dom[0][k] + MIN_EXTENT
    dom = _out32(dom)
    ext = dom[1] - dom[0]
    q = np.rint((verts - dom[0]) / ext * QMAX).astype(np.uint64)
    sv = q[:, 0] | (q[:, 1] << np.uint64(21)) | (q[:, 2] << np.uint64(42))
    pos = dom[0] + q.astype(np.float64) * ext / QMAX
    idx = np.asarray(prims, dtype=np.int64)
    pts = pos[idx]                                   # (m, 4, 3)
    boxes = np.stack([pts.min(1), pts.max(1)], 1)
    quads = int((idx[:, 2] != idx[:, 3]).sum())
    max_key = 2 * (m - 1) + (1 if idx[-1, 2] != idx[-1, 3] else 0)
    vals = np.zeros(m, dtype=np.uint8) if values is None else np.asarray(values, dtype=np.uint8)
    return Mesh(dom, sv, pos, idx.astype(np.uint8), vals, boxes, m + quads, max_key)


def _section_tree(boxes: np.ndarray, domain: np.ndarray) -> bytes:
    """The Aabb4BytesCodec tree (depth first, hkcms.py's rules) over the primitives' boxes."""
    out = bytearray()
    centres = (boxes[:, 0] + boxes[:, 1]) * 0.5

    def rec(ids: np.ndarray, parent: np.ndarray) -> None:
        box = np.array([boxes[ids, 0].min(0), boxes[ids, 1].max(0)])
        xyz = hkcms.encode_box(parent, box)
        me = hkcms.decode_box(parent, xyz)
        at = len(out)
        out.extend(xyz + b'\0')
        if len(ids) == 1:
            out[at + 3] = int(ids[0]) << 1
            return
        axis = int(np.argmax(box[1] - box[0]))
        order = ids[np.argsort(centres[ids, axis], kind='stable')]
        half = len(order) // 2
        rec(order[:half], me)
        first = len(order[:half])
        out[at + 3] = (first << 1) | 1               # the second child is 2 * first nodes on
        rec(order[half:], me)
    rec(np.arange(len(boxes)), domain)
    return bytes(out)


def simd_tree(boxes: np.ndarray, data: list[int]) -> bytes:
    """An hkcdSimdTree's nodes (128 bytes each) over `boxes` (n, 2, 3): node 0 an empty sentinel, node 1 the root (always
    an inner node), leaf nodes of four boxes (the last may hold fewer) whose lane data is `data[i]`, inner nodes of
    four child nodes (lane data = their node index); empty lanes +-huge (leaf data -1, inner data 0), lanes rounded
    outward to float32. The leaves follow a median split order (the stock builder's trees pack leaves the same way:
    48 primitives = 12 leaf nodes), the levels above group four neighbours."""
    n = len(boxes)
    centres = boxes[:, 0] + boxes[:, 1]
    order: list[int] = []

    def kd(ids: np.ndarray) -> None:
        if len(ids) <= 4:
            order.extend(int(i) for i in ids)
            return
        ext = boxes[ids, 1].max(0) - boxes[ids, 0].min(0)
        o = ids[np.argsort(centres[ids, int(np.argmax(ext))], kind='stable')]
        cut = 4 * ((len(o) + 7) // 8)                # a multiple of 4: the left part fills whole leaf nodes
        kd(o[:cut])
        kd(o[cut:])
    kd(np.arange(n))
    # level 0: leaf nodes; each node = (lo (4, 3), hi (4, 3), data (4), is_leaf, children)
    level = []
    for k in range(0, n, 4):
        ids = order[k:k + 4]
        lo, hi = np.full((4, 3), EMPTY_LO), np.full((4, 3), EMPTY_HI)
        d = [0xFFFFFFFF] * 4
        for lane, i in enumerate(ids):
            lo[lane], hi[lane] = boxes[i, 0], boxes[i, 1]
            d[lane] = data[i]
        level.append({'lo': lo, 'hi': hi, 'data': d, 'leaf': 1, 'kids': []})
    while len(level) > 1 or level[0]['leaf']:
        nxt = []
        for k in range(0, len(level), 4):
            kids = level[k:k + 4]
            lo, hi = np.full((4, 3), EMPTY_LO), np.full((4, 3), EMPTY_HI)
            for lane, c in enumerate(kids):
                used = c['lo'][:, 0] <= c['hi'][:, 0]
                lo[lane], hi[lane] = c['lo'][used].min(0), c['hi'][used].max(0)
            nxt.append({'lo': lo, 'hi': hi, 'data': [0] * 4, 'leaf': 0, 'kids': kids})
        level = nxt
    flat = []                                        # breadth first from the root: node numbers 1, 2, 3 ...
    queue = [level[0]]
    while queue:
        node = queue.pop(0)
        node['id'] = len(flat) + 1
        flat.append(node)
        queue += node['kids']
    for node in flat:
        for lane, c in enumerate(node['kids']):
            node['data'][lane] = c['id']
    out = bytearray()
    sentinel = {'lo': np.full((4, 3), EMPTY_LO), 'hi': np.full((4, 3), EMPTY_HI), 'data': [0] * 4, 'leaf': 0}
    for node in [sentinel] + flat:
        lo, hi = node['lo'], node['hi']
        used = lo[:, 0] <= hi[:, 0]
        lo32, hi32 = lo.astype(np.float32), hi.astype(np.float32)
        lo32 = np.where(used[:, None] & (lo32 > lo), np.nextafter(lo32, np.float32(-np.inf)), lo32)
        hi32 = np.where(used[:, None] & (hi32 < hi), np.nextafter(hi32, np.float32(np.inf)), hi32)
        f = np.stack([lo32[:, 0], hi32[:, 0], lo32[:, 1], hi32[:, 1], lo32[:, 2], hi32[:, 2]]).astype('<f4')
        out += f.tobytes() + struct.pack('<4I', *node['data']) + bytes([node['leaf']]) + bytes(15)
    return bytes(out)


# ------------------------------------------------------------------------------------------------ items

CMS = 'hknpCompressedMeshShape'
CMS_DATA = 'hknpCompressedMeshShapeData'
TREE = 'hknpCompressedMeshShapeTree'
SECTION = 'hkcdStaticMeshTree::Section'


class Builder:
    """Adds the stock layout's items for meshes, compounds and bodies to a donor Doc (offsets looked up in its TBDY)."""

    def __init__(self, doc: Doc) -> None:
        self.doc = doc
        self.tag = doc.tag
        t = self.tag
        self.o = {m: t.offset(TREE, m) for m in ('nodes', 'domain', 'numPrimitiveKeys', 'bitsPerKey', 'maxKeyValue',
                                                  'primitiveStoresIsFlatConvex', 'sections', 'primitives',
                                                  'sharedVerticesIndex', 'packedVertices', 'sharedVertices',
                                                  'primitiveDataRuns')}
        self.o_simd = t.offset(CMS_DATA, 'simdTree') + t.offset('hkcdSimdTree', 'nodes')
        self.o_simd_compact = t.offset(CMS_DATA, 'simdTree') + t.offset('hkcdSimdTree', 'isCompact')
        self.o_has_simd = t.offset(CMS_DATA, 'hasSimdTree')
        self.sz_data = t.size(CMS_DATA)
        self.sz_cms = t.size(CMS)
        self.sz_sec = t.size(SECTION)
        self.sz_comp = t.size('hknpCompoundShape')
        self.sz_inst = 64
        self.shape_o = {m: t.offset(CMS, m) for m in ('properties', 'data', 'triangleIsInterior', 'numTriangles',
                                                      'numConvexShapes', 'externShapes', 'shapeTagCodecInfo')}
        self.comp_o = {m: t.offset('hknpCompoundShape', m) for m in ('properties', 'instances', 'instanceVelocities',
                                                                     'aabb', 'boundingRadius', 'isMutable',
                                                                     'estimatedNumShapeKeys', 'boundingVolumeData')}
        self.comp_simd = self.comp_o['boundingVolumeData'] + t.offset('hknpCompoundShapeData', 'simdTree')
        self.comp_type = self.comp_o['boundingVolumeData'] + t.offset('hknpCompoundShapeData', 'type')

    # ---- strings and properties

    def chars(self, text: str) -> int:
        return self.doc.add('char', len(text.encode()) + 1, text.encode() + b'\0')

    def string_object(self, text: str) -> int:
        c = self.chars(text)
        return self.doc.add('hkStringObject', 1, struct.pack('<I', c), 16)

    def prop(self, string_obj: int) -> int:
        return self.doc.add('hknpShapeProperties::Property', 1, struct.pack('<II', string_obj, 0xF201))

    # ---- a mesh

    def mesh(self, m: Mesh, string_obj: int) -> int:
        """The items of one compressed mesh; its hknpCompressedMeshShape item."""
        d, o = self.doc, self.o
        nm = len(m.prims)
        keys = m.max_key + 1
        bpk = keys.bit_length()
        sec_nodes = d.add('hkcdCompressedAabbCodecs::Aabb4BytesCodec', 2 * nm - 1, _section_tree(m.boxes, m.domain))
        mesh_node = hkcms.encode_box(m.domain, m.domain) + b'\0\0'
        nodes = d.add('hkcdCompressedAabbCodecs::Aabb5BytesCodec', 1, mesh_node)
        sec = bytearray(self.sz_sec)
        so = {k: self.tag.offset(SECTION, k) for k in ('nodes', 'domain', 'codecParms', 'firstPackedVertexIndex',
                                                      'firstSharedVertexIndex', 'firstPrimitiveIndex',
                                                      'firstDataRunIndex', 'numPackedVertices', 'numPrimitives',
                                                      'numDataRuns')}
        runs = _runs(m.values)
        struct.pack_into('<I', sec, so['nodes'], sec_nodes)
        struct.pack_into('<8f', sec, so['domain'], *m.domain[0], 1.0, *m.domain[1], 1.0)
        struct.pack_into('<6I', sec, so['codecParms'], *([0x7F7FFFEE] * 3 + [0xFF800000] * 3))
        sec[so['numPackedVertices']] = 0
        sec[so['numPrimitives']] = nm
        sec[so['numDataRuns']] = len(runs)
        sections = d.add(SECTION, 1, bytes(sec))
        prims = d.add('hkcdStaticMeshTree::Primitive', nm, m.prims.tobytes())
        svi = d.add('hkUint16', len(m.sv), np.arange(len(m.sv), dtype='<u2').tobytes())
        sv = d.add('unsigned long long', len(m.sv), m.sv.astype('<u8').tobytes())
        pdr = d.add('hkcdDefaultStaticMeshTree::PrimitiveDataRun', len(runs), b''.join(runs))
        keys_boxes = m.boxes
        simd_bytes = simd_tree(keys_boxes, [p << 1 for p in range(nm)])
        simd = d.add('hkcdSimdTree::Node', len(simd_bytes) // 128, simd_bytes)
        data = bytearray(self.sz_data)
        struct.pack_into('<I', data, o['nodes'], nodes)
        struct.pack_into('<8f', data, o['domain'], *m.domain[0], 1.0, *m.domain[1], 1.0)
        struct.pack_into('<iiI', data, o['numPrimitiveKeys'], m.num_triangles, bpk, m.max_key)
        data[o['primitiveStoresIsFlatConvex']] = 255
        struct.pack_into('<I', data, o['sections'], sections)
        struct.pack_into('<I', data, o['primitives'], prims)
        struct.pack_into('<I', data, o['sharedVerticesIndex'], svi)
        struct.pack_into('<I', data, o['sharedVertices'], sv)
        struct.pack_into('<I', data, o['primitiveDataRuns'], pdr)
        struct.pack_into('<I', data, self.o_simd, simd)
        data[self.o_simd_compact] = 1
        data[self.o_has_simd] = 1
        data_item = d.add(CMS_DATA, 1, bytes(data), 16)
        words = d.add('hkUint32', (keys + 31) // 32, bytes(4 * ((keys + 31) // 32)))   # no triangle marked interior
        shape = bytearray(self.sz_cms)
        shape[0], shape[1] = 8, 3
        struct.pack_into('<H', shape, 2, 4)
        shape[4] = bpk
        struct.pack_into('<I', shape, self.shape_o['properties'], self.prop(string_obj))
        struct.pack_into('<I', shape, self.shape_o['shapeTagCodecInfo'], 0xFFFFFFFF)
        struct.pack_into('<I', shape, self.shape_o['data'], data_item)
        struct.pack_into('<I', shape, self.shape_o['triangleIsInterior'], words)
        struct.pack_into('<i', shape, self.shape_o['triangleIsInterior'] + 4, keys)
        struct.pack_into('<i', shape, self.shape_o['numTriangles'], m.num_triangles)
        return d.add(CMS, 1, bytes(shape), 16)

    # ---- a compound

    def compound(self, shapes: list[int], domains: list[np.ndarray], bits: list[int], string_obj: int) -> int:
        d = self.doc
        n = len(shapes)
        inst = bytearray()
        for sh in shapes:
            e = bytearray(self.sz_inst)
            struct.pack_into('<4f', e, 0, 0.0, 0.0, 0.0, 1.0)
            struct.pack_into('<H', e, 28, 0xFFFF)
            e[30] = 0x40
            struct.pack_into('<3f', e, 32, 1.0, 1.0, 1.0)
            struct.pack_into('<HH', e, 44, 0xFFFF, 0xFFFF)
            struct.pack_into('<I', e, 48, sh)
            inst += e
        instances = d.add('hkFreeListArrayElement<hknpShapeInstance>', n, bytes(inst))
        doms = np.array(domains)
        simd_bytes = simd_tree(doms, list(range(n)))
        simd = d.add('hkcdSimdTree::Node', len(simd_bytes) // 128, simd_bytes)
        lo, hi = doms[:, 0].min(0), doms[:, 1].max(0)
        c = bytearray(self.sz_comp)
        c[0], c[1] = 0x0B, 3
        struct.pack_into('<H', c, 2, 4)
        c[4] = n.bit_length()
        co = self.comp_o
        struct.pack_into('<I', c, co['properties'], self.prop(string_obj))
        struct.pack_into('<I', c, 24, 0xFFFFFFFF)
        struct.pack_into('<IiI', c, co['instances'], instances, -1, n)
        struct.pack_into('<8f', c, co['aabb'], *lo, 0.0, *hi, 0.0)
        struct.pack_into('<f', c, co['boundingRadius'], float(np.float32(np.linalg.norm(hi - lo) * 0.5)))
        struct.pack_into('<i', c, co['estimatedNumShapeKeys'], sum(1 << b for b in bits))
        struct.pack_into('<I', c, self.comp_simd, simd)
        c[self.comp_simd + 4] = 1
        c[self.comp_type] = 2
        return d.add('hknpCompoundShape', 1, bytes(c), 16)


def _runs(values: np.ndarray) -> list[bytes]:
    """The primitiveDataRuns of a section: (value, 0, first primitive, count) for each stretch of equal values."""
    out = []
    k = 0
    while k < len(values):
        j = k
        while j < len(values) and values[j] == values[k] and j - k < 255:
            j += 1
        out.append(bytes((int(values[k]), 0, k, j - k)))
        k = j
    return out


# ------------------------------------------------------------------------------------------------ graph edits


def clone(doc: Doc, root: int) -> int:
    """A deep copy of item `root` and everything it reaches (shared items stay shared); the copy's root item."""
    order = sorted(reachable(doc, [root]))
    new = {}
    for i in order:
        it = doc.items[i]
        doc.items.append(Item(it.tidx, it.flags, it.count, it.data))
        doc.layout.append(len(doc.items) - 1)
        new[i] = len(doc.items) - 1
    for i in order:
        it = doc.items[new[i]]
        data = bytearray(it.data)
        for off, v in pointers(doc, i):
            struct.pack_into('<I', data, off, new[v])
        it.data = bytes(data)
    return new[root]


def compact(doc: Doc, root: int = 1) -> Doc:
    """The items reachable from `root`, renumbered in their old order (pointers rewritten), laid out in the old
    DATA order: the donor's bodies and everything only they used are gone."""
    keep = sorted(reachable(doc, [root]))
    remap = {old: new for new, old in enumerate(keep, 1)}
    items = [doc.items[0]]
    for old in keep:
        it = doc.items[old]
        data = bytearray(it.data)
        for off, v in pointers(doc, old):
            struct.pack_into('<I', data, off, remap[v])
        items.append(Item(it.tidx, it.flags, it.count, bytes(data)))
    return Doc(doc.sdkv, doc.tst1, doc.tna1, doc.fst1, doc.tbdy, doc.tpad, items,
               [remap[i] for i in doc.layout if i in remap], doc.tag, doc.aligns)


def _u32(doc: Doc, item: int, off: int) -> int:
    return struct.unpack_from('<I', doc.items[item].data, off)[0]


def _put32(doc: Doc, item: int, off: int, v: int) -> None:
    it = doc.items[item]
    d = bytearray(it.data)
    struct.pack_into('<I', d, off, v)
    it.data = bytes(d)


@dataclass
class Body:
    """A static collision body: `triangles` (n, 3, 3) in the body's own space (the map places it with the object's
    matrix, like every EDF6 object collision); its name is '<model>.<node>' (the map loader looks it up)."""
    name: str
    triangles: np.ndarray


@dataclass
class Donor:
    """Where a donor Doc keeps what the builder rewrites and copies."""
    variants: int            # the root's NamedVariant array item
    system: int              # the physics system data item
    cinfos: int              # its body cinfo array
    cinfo0: bytes            # the donor's first body cinfo (the ground: the template of ours)
    palette_terrain: int | None
    palette_object: int | None
    scene_variant: tuple[int, int, int]    # the scene's own NamedVariant entry (name, class, item)


def find_donor(doc: Doc) -> Donor:
    tag = doc.tag
    variants = _u32(doc, 1, 0)
    entries = []
    for k in range(doc.items[variants].count):
        entries.append(struct.unpack_from('<III', doc.items[variants].data, 12 * k))
    scene_entry = next(e for e in entries if _cstr(doc, e[1]) == 'hknpPhysicsSceneData')
    scene = scene_entry[2]
    sd_item = _u32(doc, scene, tag.offset('hknpPhysicsSceneData', 'systemDatas'))
    system = _u32(doc, sd_item, 0)
    cinfos = _u32(doc, system, tag.offset('hknpPhysicsSystemData', 'bodyCinfos'))
    bsz = tag.size('hknpPhysicsSystemData::bodyCinfoWithAttachment')
    cinfo0 = doc.items[cinfos].data[:bsz]
    name0 = _cstr(doc, struct.unpack_from('<I', cinfo0, tag.offset('hknpPhysicsSystemData::bodyCinfoWithAttachment', 'name'))[0])
    pal_t = pal_o = None
    for e in entries:
        if _cstr(doc, e[1]) != 'hknpMaterialPalette':
            continue
        descr = _u32(doc, e[2], 0)
        if _cstr(doc, e[0]) == name0:
            pal_t = e[2]
        elif doc.items[descr].count == 1 and pal_o is None:
            pal_o = e[2]
    return Donor(variants, system, cinfos, cinfo0, pal_t, pal_o, scene_entry)


def _cstr(doc: Doc, item: int) -> str:
    return doc.items[item].data.split(b'\0')[0].decode('utf-8', 'replace') if item else ''


# ------------------------------------------------------------------------------------------------ geometry


def grid_tiles(heights: np.ndarray, x0: float, z0: float, step: float, tile_cells: int,
               values: np.ndarray | None) -> list[tuple[np.ndarray, np.ndarray, np.ndarray]]:
    """The lattice cut into tiles: (vertices (n, 3), quads (m, 4) indices, per-quad value) of each. Quad (i, j) has the
    corners (i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1) with x = x0 + j * step, z = z0 + i * step: that winding
    faces up (+y), the stock ground's."""
    rows, cols = heights.shape
    out = []
    for i0 in range(0, rows - 1, tile_cells):
        i1 = min(i0 + tile_cells, rows - 1)
        for j0 in range(0, cols - 1, tile_cells):
            j1 = min(j0 + tile_cells, cols - 1)
            ii, jj = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1), indexing='ij')
            verts = np.stack([x0 + jj * step, heights[ii, jj], z0 + ii * step], -1).reshape(-1, 3)
            w = j1 - j0 + 1
            qi, qj = np.meshgrid(np.arange(i1 - i0), np.arange(j1 - j0), indexing='ij')
            a = (qi * w + qj).ravel()
            quads = np.stack([a, a + w, a + w + 1, a + 1], 1)
            vals = None if values is None else values[i0:i1, j0:j1].reshape(-1)
            out.append((verts, quads, vals if vals is not None else np.zeros(len(quads), dtype=np.uint8)))
    return out


def soup_tiles(tris: np.ndarray) -> list[tuple[np.ndarray, np.ndarray, np.ndarray]]:
    """A triangle soup (n, 3, 3) cut by median splits until each part holds <= MAX_PRIMS triangles and <= MAX_VERTS
    distinct vertices: (vertices, primitives (c repeated as d), values) each."""
    out = []

    def rec(ids: np.ndarray) -> None:
        pts = tris[ids].reshape(-1, 3)
        uniq, inv = np.unique(np.round(pts, 6), axis=0, return_inverse=True)
        if len(ids) <= MAX_PRIMS and len(uniq) <= MAX_VERTS or len(ids) == 1:
            t = inv.reshape(-1, 3)
            prims = np.concatenate([t, t[:, 2:3]], 1)
            out.append((uniq, prims, np.zeros(len(prims), dtype=np.uint8)))
            return
        c = tris[ids].mean(1)
        axis = int(np.argmax(c.max(0) - c.min(0)))
        o = ids[np.argsort(c[:, axis], kind='stable')]
        h = len(o) // 2
        rec(o[:h])
        rec(o[h:])
    if len(tris):
        rec(np.arange(len(tris)))
    return out


def build_collision(donor: bytes, terrain_name: str, heights: np.ndarray, x0: float, z0: float, step: float,
                    bodies: list[Body] | None = None, tile_cells: int = 10,
                    values: np.ndarray | None = None) -> bytes:
    """The collision.hkt of a map whose ground is `heights` (rows, cols): vertex (i, j) at (x0 + j * step, h, z0 + i *
    step), a static body named f'{terrain_name}.{terrain_name}' (a compound of tile_cells x tile_cells tiles of one
    quad per cell, up to 127 quads each), plus `bodies` (static, identity transform). `values` (rows - 1, cols - 1)
    uint8 picks each quad's material of the terrain's palette (0..5, default 0). The donor's libraries and physics
    system are kept, its bodies dropped."""
    if tile_cells * tile_cells > MAX_PRIMS:
        raise ValueError('a tile holds at most %d quads' % MAX_PRIMS)
    doc = load(donor)
    don = find_donor(doc)
    b = Builder(doc)
    tag = doc.tag
    cinfo_sz = len(don.cinfo0)
    co = {k: tag.offset('hknpPhysicsSystemData::bodyCinfoWithAttachment', k) for k in ('shape', 'name', 'materialId')}
    cinfos = bytearray()
    entries: list[tuple[int, int, int]] = []

    def add_body(name: str, shape: int, palette_src: int | None) -> None:
        k = len(cinfos) // cinfo_sz
        c = bytearray(don.cinfo0)
        struct.pack_into('<I', c, co['shape'], shape)
        struct.pack_into('<I', c, co['name'], b.chars(name))
        struct.pack_into('<H', c, co['materialId'], 0)
        cinfos.extend(c)
        if palette_src is not None:
            entries.append((b.chars(name), b.chars('hknpMaterialPalette'), clone(doc, palette_src)))

    def shapes_of(parts: list[tuple[np.ndarray, np.ndarray, np.ndarray]], name: str) -> int:
        so = b.string_object('{"name": "%s",id=%d }' % (name, len(cinfos) // cinfo_sz))
        items, doms, bits = [], [], []
        for verts, prims, vals in parts:
            m = make_mesh(verts, prims, vals)
            items.append(b.mesh(m, so))
            doms.append(m.domain)
            bits.append((m.max_key + 1).bit_length())
        if len(items) == 1:
            return items[0]
        return b.compound(items, doms, bits, so)

    tname = f'{terrain_name}.{terrain_name}'
    add_body(tname, shapes_of(grid_tiles(heights, x0, z0, step, tile_cells, values), tname), don.palette_terrain)
    for body in bodies or []:
        parts = soup_tiles(np.asarray(body.triangles, dtype=np.float64).reshape(-1, 3, 3))
        if parts:
            add_body(body.name, shapes_of(parts, body.name), don.palette_object)
    n = len(cinfos) // cinfo_sz
    new_cinfos = doc.add('hknpPhysicsSystemData::bodyCinfoWithAttachment', n, bytes(cinfos))
    _put32(doc, don.system, tag.offset('hknpPhysicsSystemData', 'bodyCinfos'), new_cinfos)
    ents = [don.scene_variant] + entries
    new_variants = doc.add('hkRootLevelContainer::NamedVariant', len(ents), b''.join(struct.pack('<III', *e) for e in ents))
    _put32(doc, 1, 0, new_variants)
    return compact(doc).emit()


def write_mad(hkt: bytes) -> bytes:
    """The MAD: a MARC (version 5) with the single file collision.hkt."""
    import bigmap
    return bigmap.Marc(5, [('collision.hkt', hkt)]).build()


# ------------------------------------------------------------------------------------------------ checks


def walk(doc: Doc, item: int, path: str = '', out: list | None = None) -> list[tuple[str, str, int, int, int]]:
    """(member path, type, flags, count, item) of everything reachable from `item` through named pointer members."""
    out = [] if out is None else out
    it = doc.items[item]
    out.append((path, doc.tname(it.tidx), it.flags, it.count, item))
    tag = doc.tag

    def rec(ty: int, base: int, pre: str) -> None:
        for name, m in tag.members(ty).items():
            tn = tag.tname(m.type)
            off = base + m.offset
            if tn.startswith(_PTR_PREFIX) or tn == 'hkStringPtr' or '*' in tn:
                v = struct.unpack_from('<I', it.data, off)[0]
                if v:
                    walk(doc, v, path + '/' + pre + name, out)
            elif m.type in tag.bodies and tag.members(m.type):
                rec(m.type, off, pre + name + '.')
    rec(it.tidx, 0, '')
    return out


def _first_instance(doc: Doc, compound: int) -> int:
    inst = _u32(doc, compound, doc.tag.offset('hknpCompoundShape', 'instances'))
    return _u32(doc, inst, doc.tag.offset('hknpShapeInstance', 'shape'))


def selftest(donor: bytes) -> list[str]:
    """Raises AssertionError on failure; the lines say what was checked. `donor` is a stock collision.hkt with a ground
    compound of compressed meshes (IG_HEIGEN601's)."""
    import hktag as H
    log: list[str] = []
    doc = load(donor)
    assert doc.emit() == donor, 'identity round trip differs'
    log.append(f'identity: {len(doc.items) - 1} items re-emitted byte for byte ({len(donor)} bytes)')
    yy, xx = np.mgrid[0:41, 0:41]
    h = 5 * np.sin(xx / 6.0) + 3 * np.cos(yy / 5.0)
    vals = ((xx[:-1, :-1] // 8) % 6).astype(np.uint8)
    rock = np.array([[[0, 0, 0], [1, 0, 0], [0, 1, 0]], [[1, 0, 0], [1, 1, 0], [0, 1, 0]]], dtype=np.float64)
    ang = np.linspace(0, 2 * np.pi, 91)
    ring = np.array([[[np.cos(a) * 3, k, np.sin(a) * 3], [np.cos(b) * 3, k, np.sin(b) * 3],
                      [np.cos(a) * 3, k + 1, np.sin(a) * 3]] for k in range(5) for a, b in zip(ang[:-1], ang[1:])])
    out = build_collision(donor, 'sk_test01_fld2mgrid', h, -80.0, -80.0, 4.0,
                          [Body('sk_rock.node1', rock), Body('sk_ring.node1', ring)], values=vals)
    tag = H.Tag(out)
    buf = bytearray(out)
    bodies = hkcms.bodies(tag)
    assert list(bodies) == ['sk_test01_fld2mgrid.sk_test01_fld2mgrid', 'sk_rock.node1', 'sk_ring.node1'], bodies
    comp = hkcms.Compound(tag, buf, bodies['sk_test01_fld2mgrid.sk_test01_fld2mgrid'])
    assert hkcms.bound_problems(tag, out, bodies['sk_test01_fld2mgrid.sk_test01_fld2mgrid']) == []
    tris = comp.triangles()
    pts = tris.reshape(-1, 3)
    ix = np.rint((pts[:, 0] + 80) / 4).astype(int)
    iz = np.rint((pts[:, 2] + 80) / 4).astype(int)
    err = max(np.abs(pts[:, 1] - h[iz, ix]).max(), np.abs(pts[:, 0] - (-80 + ix * 4.0)).max(),
              np.abs(pts[:, 2] - (-80 + iz * 4.0)).max())
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    assert err < 1e-4 and len(tris) == 2 * 40 * 40 and (np.cross(b - a, c - a)[:, 1] > 0).all()
    log.append(f'terrain: 16 tiles / {len(tris)} triangles decode within {err:.1e} m of the lattice, all face up, '
               'bound_problems == []')
    rr = hkcms.Compound(tag, buf, bodies['sk_ring.node1'])
    canon = lambda tr: sorted(tuple(sorted(map(tuple, np.round(x, 3)))) for x in tr)   # noqa: E731
    assert canon(rr.triangles()) == canon(ring)
    assert hkcms.bound_problems(tag, out, bodies['sk_ring.node1']) == []
    assert canon(hkcms.Cms(tag, buf, bodies['sk_rock.node1']).triangles()) == canon(rock)
    log.append(f'objects: {len(ring)}-triangle soup in {len(rr.instances)} meshes and a 2-triangle body '
               '(a single mesh, no compound) decode to their input, bound_problems == []')
    # the field-by-field comparison with the stock ground
    mine, stock = load(out), doc
    gb = hkcms.bodies(stock.tag)
    sc = gb[next(iter(gb))]
    wm = walk(mine, _first_instance(mine, bodies['sk_test01_fld2mgrid.sk_test01_fld2mgrid']))
    ws = walk(stock, _first_instance(stock, sc))
    assert [x[:3] for x in wm] == [x[:3] for x in ws], 'item structure of a tile differs from the stock ground tile'
    consts = ('hknpCompressedMeshShape', 'hknpCompressedMeshShapeData', 'hkcdStaticMeshTree::Section',
              'hkcdCompressedAabbCodecs::Aabb5BytesCodec', 'hknpShapeProperties::Property')
    free = {'hknpCompressedMeshShape': {4, 20, 21, 28, 29, 32, 33, 36, 40, 41},
            'hknpCompressedMeshShapeData': set(range(0, 2)) | set(range(16, 48)) | {48, 52, 56, 64, 65, 68, 69, 72, 73,
                                                                                     80, 81, 84, 85, 96, 97},
            'hkcdStaticMeshTree::Section': {0, 1, 89, 90} | set(range(16, 48)),
            'hkcdCompressedAabbCodecs::Aabb5BytesCodec': set(), 'hknpShapeProperties::Property': {0, 1}}
    for (_, tn, _, _, i), (_, _, _, _, j) in zip(wm, ws):
        if tn in consts:
            da, db = mine.items[i].data, stock.items[j].data
            diff = {k for k in range(len(da)) if da[k] != db[k]}
            assert diff <= free[tn], (tn, sorted(diff - free[tn]))
    log.append(f'structure: a tile mesh has the stock ground tile\'s {len(wm)} items (type, flags, member path); its '
               'shape / data / section / node / property bytes differ from the stock mesh only in data fields')
    cm = mine.items[[x[4] for x in walk(mine, bodies['sk_test01_fld2mgrid.sk_test01_fld2mgrid'])][0]].data
    cs = stock.items[sc].data
    var = {4, 20, 21, 28, 29, 36, 37, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 64, 65, 66, 67, 68, 69, 70, 71,
           72, 73, 74, 75, 80, 81, 82, 83, 88, 89, 112, 113}
    assert {k for k in range(128) if cm[k] != cs[k]} <= var
    log.append('compound: the 128 bytes differ from the stock ground compound only where stock compounds differ among '
               'themselves (key bits, item indices, counts, aabb, radius, key estimate, simd node)')
    return log


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[0] != 'selftest':
        print(__doc__)
        return 2
    for line in selftest(open(argv[1], 'rb').read()):
        print(line)
    print('SELFTEST OK')
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
