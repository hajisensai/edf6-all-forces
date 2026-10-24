"""EDF6 map collision (`collision.hkt` in <map>.MAD) written from scratch geometry: a Havok 2020.1 tagfile writer plus a
builder of static terrain / object bodies (hknpCompoundShape of hknpCompressedMeshShape tiles), in the layout the
stock EDF6 files have (docs/map-collision.md; the tree rules are pylib/hkcms.py's). EDF6 has no heightfield shape the
map loader can use, so the terrain converted from EDF4.1 (pylib/sk_terrain.py) goes in as a triangle mesh like EDF6's
own ground (ig_heigen507_2).

  The tagfile itself is pylib/hkfile.py's graph (read, written back by Havok's layout rules).
  build_collision(donor, terrain_name, heights, x0, z0, step, bodies, tile_cells) -> bytes   the collision.hkt
  write_mad(hkt) -> bytes           the MAD (a MARC holding that file)

The donor is a stock EDF6 `collision.hkt` of the player's own game (IG_HEIGEN601.MAD's): its type tables, material /
motion libraries and physics system are kept, its bodies replaced. Nothing of it is stored in the repo.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

import hkfile
from hkfile import Block, Tagfile


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
    """The Aabb4BytesCodec tree (depth first, hkcms.py's rules) over the primitives' boxes: _section_tree_py's bytes,
    natively when native/aabb_tree.cpp's DLL is there (pylib/edf6native.py)."""
    import edf6native
    lib = edf6native.lib()
    if lib is not None and len(boxes):
        import ctypes
        b = np.ascontiguousarray(boxes, dtype=np.float64)
        d = np.ascontiguousarray(domain, dtype=np.float64)
        if b.shape[1:] == (2, 3) and d.shape == (2, 3):
            out = ctypes.create_string_buffer(4 * (2 * len(b) - 1))
            if lib.edf6_section_tree(b.ctypes.data, len(b), d.ctypes.data, out):
                return out.raw
    return _section_tree_py(boxes, domain)


def _section_tree_py(boxes: np.ndarray, domain: np.ndarray) -> bytes:
    """_section_tree's reference."""
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
    """simd_tree_py's bytes, natively when native/aabb_tree.cpp's DLL is there (pylib/edf6native.py)."""
    import edf6native
    lib = edf6native.lib()
    if lib is not None and len(boxes) and len(data) == len(boxes) and all(0 <= v <= 0xFFFFFFFF for v in data):
        import ctypes
        b = np.ascontiguousarray(boxes, dtype=np.float64)
        if b.shape[1:] == (2, 3):
            d = np.ascontiguousarray(data, dtype=np.uint32)
            out = ctypes.create_string_buffer(128 * lib.edf6_simd_tree_nodes(len(b)))
            if lib.edf6_simd_tree(b.ctypes.data, len(b), d.ctypes.data, out):
                return out.raw
    return simd_tree_py(boxes, data)


def simd_tree_py(boxes: np.ndarray, data: list[int]) -> bytes:
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
    """Adds the stock layout's blocks for meshes, compounds and bodies to a donor tagfile (its own types)."""

    def __init__(self, tf: Tagfile) -> None:
        self.tf = tf

    # ---- strings and properties

    def string_object(self, text: str) -> Block:
        so = self.tf.new('hkStringObject')
        so.value['string'].set_target(self.tf.string(text))
        return so

    def prop(self, string_obj: Block) -> Block:
        p = self.tf.new('hknpShapeProperties::Property')
        p[0]['object'].set_target(string_obj)
        p[0]['key'].set_int(0xF201)
        return p

    # ---- a mesh

    def mesh(self, m: Mesh, string_obj: Block) -> Block:
        """The blocks of one compressed mesh; its hknpCompressedMeshShape."""
        tf = self.tf
        nm = len(m.prims)
        keys = m.max_key + 1
        bpk = keys.bit_length()
        sec_nodes = tf.new('hkcdCompressedAabbCodecs::Aabb4BytesCodec', 2 * nm - 1, _section_tree(m.boxes, m.domain))
        nodes = tf.new('hkcdCompressedAabbCodecs::Aabb5BytesCodec', 1, hkcms.encode_box(m.domain, m.domain) + b'\0\0')
        runs = _runs(m.values)
        sections = tf.new(SECTION)
        sec = sections[0]
        sec['nodes'].set_target(sec_nodes)
        sec['domain']['min'].set_vec((*m.domain[0], 1.0))
        sec['domain']['max'].set_vec((*m.domain[1], 1.0))
        struct.pack_into('<6I', sections.data, sec['codecParms'].offset, *([0x7F7FFFEE] * 3 + [0xFF800000] * 3))
        sec['numPackedVertices'].set_int(0)
        sec['numPrimitives'].set_int(nm)
        sec['numDataRuns'].set_int(len(runs))
        data = tf.new(CMS_DATA)
        d = data.value
        tree = d['meshTree']
        tree['nodes'].set_target(nodes)
        tree['domain']['min'].set_vec((*m.domain[0], 1.0))
        tree['domain']['max'].set_vec((*m.domain[1], 1.0))
        tree['numPrimitiveKeys'].set_int(m.num_triangles)
        tree['bitsPerKey'].set_int(bpk)
        tree['maxKeyValue'].set_int(m.max_key)
        tree['primitiveStoresIsFlatConvex'].set_int(255)
        tree['sections'].set_target(sections)
        tree['primitives'].set_target(tf.new('hkcdStaticMeshTree::Primitive', nm, m.prims.tobytes()))
        tree['sharedVerticesIndex'].set_target(tf.new('hkUint16', len(m.sv),
                                                      np.arange(len(m.sv), dtype='<u2').tobytes()))
        tree['sharedVertices'].set_target(tf.new('unsigned long long', len(m.sv), m.sv.astype('<u8').tobytes()))
        tree['primitiveDataRuns'].set_target(tf.new('hkcdDefaultStaticMeshTree::PrimitiveDataRun', len(runs),
                                                    b''.join(runs)))
        simd_bytes = simd_tree(m.boxes, [p << 1 for p in range(nm)])
        d['simdTree']['nodes'].set_target(tf.new('hkcdSimdTree::Node', len(simd_bytes) // 128, simd_bytes))
        d['simdTree']['isCompact'].set_int(1)
        d['hasSimdTree'].set_int(1)
        shape = tf.new(CMS)
        v = shape.value
        shape.data[0], shape.data[1] = 8, 3                  # type, dispatchType
        v['flags'].set_int(4)
        v['numShapeKeyBits'].set_int(bpk)
        v['properties']['properties'].set_target(self.prop(string_obj))
        v['shapeTagCodecInfo'].set_int(0xFFFFFFFF)
        v['data'].set_target(data)
        v['triangleIsInterior']['words'].set_target(tf.new('hkUint32', (keys + 31) // 32))   # no triangle interior
        v['triangleIsInterior']['numBits'].set_int(keys)
        v['numTriangles'].set_int(m.num_triangles)
        return shape

    # ---- a compound

    def compound(self, shapes: list[Block], domains: list[np.ndarray], bits: list[int], string_obj: Block) -> Block:
        tf = self.tf
        n = len(shapes)
        inst = tf.new('hkFreeListArrayElement<hknpShapeInstance>', n)
        for e, sh in zip(inst, shapes):
            e['rotation'].set_vec((0.0, 0.0, 0.0, 1.0))
            e['leafIndex'].set_int(0xFFFF)
            e['flags'].set_int(0x40)
            e['scale'].set_vec((1.0, 1.0, 1.0))
            e['shapeTag'].set_int(0xFFFF)
            e['destructionTag'].set_int(0xFFFF)
            e['shape'].set_target(sh)
        doms = np.array(domains)
        simd_bytes = simd_tree(doms, list(range(n)))
        lo, hi = doms[:, 0].min(0), doms[:, 1].max(0)
        c = tf.new('hknpCompoundShape')
        v = c.value
        c.data[0], c.data[1] = 0x0B, 3
        v['flags'].set_int(4)
        v['numShapeKeyBits'].set_int(n.bit_length())
        v['properties']['properties'].set_target(self.prop(string_obj))
        v['shapeTagCodecInfo'].set_int(0xFFFFFFFF)
        v['instances']['elements'].set_target(inst)
        v['instances']['firstFree'].set_int(-1)
        v['instances']['numAllocated'].set_int(n)
        v['aabb']['min'].set_vec((*lo, 0.0))
        v['aabb']['max'].set_vec((*hi, 0.0))
        v['boundingRadius'].set_f32(float(np.float32(np.linalg.norm(hi - lo) * 0.5)))
        v['estimatedNumShapeKeys'].set_int(sum(1 << b for b in bits))
        bvd = v['boundingVolumeData']
        bvd['simdTree']['nodes'].set_target(tf.new('hkcdSimdTree::Node', len(simd_bytes) // 128, simd_bytes))
        bvd['simdTree']['isCompact'].set_int(1)
        bvd['type'].set_int(2)
        return c


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


# ------------------------------------------------------------------------------------------------ the donor


@dataclass
class Body:
    """A static collision body: `triangles` (n, 3, 3) in the body's own space (the map places it with the object's
    matrix, like every EDF6 object collision); its name is '<model>.<node>' (the map loader looks it up)."""
    name: str
    triangles: np.ndarray


@dataclass
class Donor:
    """What the builder rewrites and copies in a donor tagfile."""
    variants: Block          # the root's NamedVariant array
    system: Block            # the physics system data
    cinfos: Block            # its body cinfo array
    palette_terrain: Block | None
    palette_object: Block | None
    scene_variant: int       # the scene's own NamedVariant entry


def find_donor(tf: Tagfile) -> Donor:
    variants = tf.root.value['namedVariants'].target
    scene_k = next(k for k, e in enumerate(variants) if e['className'].string() == 'hknpPhysicsSceneData')
    system = variants[scene_k]['variant'].target.value['systemDatas'].target[0].target
    cinfos = system.value['bodyCinfos'].target
    name0 = cinfos[0]['name'].string()
    pal_t = pal_o = None
    for e in variants:
        if e['className'].string() != 'hknpMaterialPalette':
            continue
        palette = e['variant'].target
        if e['name'].string() == name0:
            pal_t = palette
        elif palette.value[palette.type.all_members()[0].name].count == 1 and pal_o is None:
            pal_o = palette
    return Donor(variants, system, cinfos, pal_t, pal_o, scene_k)


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
    tf = hkfile.read(donor)
    don = find_donor(tf)
    b = Builder(tf)
    made: list[tuple[str, Block]] = []
    entries: list[tuple[str, Block]] = []

    def add_body(name: str, shape: Block, palette_src: Block | None) -> None:
        made.append((name, shape))
        if palette_src is not None:
            entries.append((name, hkfile.clone(palette_src)))

    def shapes_of(parts: list[tuple[np.ndarray, np.ndarray, np.ndarray]], name: str) -> Block:
        so = b.string_object('{"name": "%s",id=%d }' % (name, len(made)))
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
    cinfos = tf.new(don.cinfos.type, len(made))
    for k, (name, shape) in enumerate(made):
        don.cinfos.copy_element(0, cinfos, k)          # the donor's first body (the ground): the template of ours
        cinfos[k]['shape'].set_target(shape)
        cinfos[k]['name'].set_target(tf.string(name))
        cinfos[k]['materialId'].set_int(0)
    don.system.value['bodyCinfos'].set_target(cinfos)
    variants = tf.new(don.variants.type, 1 + len(entries))
    don.variants.copy_element(don.scene_variant, variants, 0)
    for k, (name, palette) in enumerate(entries, 1):
        variants[k]['name'].set_target(tf.string(name))
        variants[k]['className'].set_target(tf.string('hknpMaterialPalette'))
        variants[k]['variant'].set_target(palette)
    tf.root.value['namedVariants'].set_target(variants)
    return tf.write()


def write_mad(hkt: bytes) -> bytes:
    """The MAD: a MARC (version 5) with the single file collision.hkt."""
    import bigmap
    return bigmap.Marc(5, [('collision.hkt', hkt)]).build()


# ------------------------------------------------------------------------------------------------ checks


def walk(block: Block, path: str = '', out: list | None = None) -> list[tuple[str, str, int, Block]]:
    """(member path, type, count, block) of everything reachable from `block` through named reference members."""
    out = [] if out is None else out
    out.append((path, block.type.full_name, block.count, block))

    def rec(v: hkfile.Value, pre: str) -> None:
        for m in v.type.all_members():
            mv = v[m.name]
            if mv.type.kind in (hkfile.POINTER, hkfile.STRING) or (mv.type.kind == hkfile.ARRAY and not mv.type.is_tuple):
                t = mv.target
                if t is not None:
                    walk(t, path + '/' + pre + m.name, out)
            elif mv.type.kind == hkfile.RECORD and mv.type.all_members():
                rec(mv, pre + m.name + '.')
    rec(block.value, '')
    return out


def _first_instance(compound: Block) -> Block:
    return compound.value['instances']['elements'].target[0]['shape'].target


def selftest(donor: bytes) -> list[str]:
    """Raises AssertionError on failure; the lines say what was checked. `donor` is a stock collision.hkt with a ground
    compound of compressed meshes (IG_HEIGEN601's)."""
    import hktag as H
    log: list[str] = []
    stock = hkfile.read(donor)
    assert stock.write() == donor, 'identity round trip differs'
    log.append(f'identity: {len(stock.items) - 1} items re-emitted byte for byte ({len(donor)} bytes)')
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
    mine = hkfile.Physics.read(out)
    ground = mine.body('sk_test01_fld2mgrid.sk_test01_fld2mgrid').shape
    stock_ground = hkfile.Physics(stock).bodies()[0].shape
    wm = walk(_first_instance(ground))
    ws = walk(_first_instance(stock_ground))
    assert [x[:2] for x in wm] == [x[:2] for x in ws], 'item structure of a tile differs from the stock ground tile'
    consts = ('hknpCompressedMeshShape', 'hknpCompressedMeshShapeData', 'hkcdStaticMeshTree::Section',
              'hkcdCompressedAabbCodecs::Aabb5BytesCodec', 'hknpShapeProperties::Property')
    free = {'hknpCompressedMeshShape': {4, 20, 21, 28, 29, 32, 33, 36, 40, 41},
            'hknpCompressedMeshShapeData': set(range(0, 2)) | set(range(16, 48)) | {48, 52, 56, 64, 65, 68, 69, 72, 73,
                                                                                     80, 81, 84, 85, 96, 97},
            'hkcdStaticMeshTree::Section': {0, 1, 89, 90} | set(range(16, 48)),
            'hkcdCompressedAabbCodecs::Aabb5BytesCodec': set(), 'hknpShapeProperties::Property': {0, 1}}
    for (_, tn, _, x), (_, _, _, y) in zip(wm, ws):
        if tn in consts:
            slots = {e * x.size + off + k for e in range(x.count) for off, _ in x.slots for k in range(4)}
            diff = {k for k in range(len(x.data)) if x.data[k] != y.data[k]} - slots
            assert diff <= free[tn], (tn, sorted(diff - free[tn]))
    log.append(f'structure: a tile mesh has the stock ground tile\'s {len(wm)} items (type, member path); its '
               'shape / data / section / node / property bytes differ from the stock mesh only in data fields')
    cm, cs = ground.data, stock_ground.data
    slots = {off + k for off, _ in ground.slots for k in range(4)}
    var = {4, 36, 37, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 64, 65, 66, 67, 68, 69, 70, 71,
           72, 73, 74, 75, 80, 81, 82, 83, 88, 89}
    assert {k for k in range(128) if cm[k] != cs[k]} - slots <= var
    log.append('compound: the 128 bytes differ from the stock ground compound only where stock compounds differ among '
               'themselves (key bits, counts, aabb, radius, key estimate)')
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
