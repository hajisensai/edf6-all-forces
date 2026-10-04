"""EDF6 MDB0 model + RAB/MRAB archive reader/writer (experimental).

Layout and semantics: docs/mdb-format.md. Everything here is checked against the stock files:
`python pylib/mdb.py verify` parses every .mdb inside every OBJECT/*.RAB|*.MRAB of the game's Root.cpk,
re-serializes it and compares byte for byte (also re-encodes CMPL and rebuilds whole archives).

Commands:
  python pylib/mdb.py dump BOMBER501.MRAB [bomber501.mdb]   bones / materials / meshes / layouts
  python pylib/mdb.py verify [--cmpl] [NAME ...]            round-trip every stock model + its CMPL stream + the archive
                                                      (--cmpl: re-encode the textures too; --fast: skip CMPL
                                                       re-encoding, which is pure Python and slow; default: all archives)
  python pylib/mdb.py jet [OUTDIR]                          split bomber501 into control-surface bones
Archive names without a path are read from OBJECT/ in Root.cpk (read only; nothing is written there).
"""
from __future__ import annotations

import json
import math
import os
import struct
import sys
from dataclasses import dataclass, field

import gamedir  # noqa: E402  (pylib)
GAME = gamedir.find_or_dev()
HERE = os.path.dirname(os.path.abspath(__file__))

# --------------------------------------------------------------------------------------------- CMPL


def cmpl_decompress(data: bytes) -> bytes:
    """'CMPL' + u32 BE size + Okumura LZSS (N=4096, F=18, THRESHOLD=2, r starts at N-F, zero window)."""
    if data[:4] != b'CMPL':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    win = bytearray(4096)
    r = 0xFEE
    out = bytearray()
    i = 8
    flags = 0
    n = len(data)
    while len(out) < size and i < n:
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            c = data[i]
            i += 1
            out.append(c)
            win[r] = c
            r = (r + 1) & 0xFFF
            continue
        b0, b1 = data[i], data[i + 1]
        i += 2
        off = (b0 << 4) | (b1 >> 4)
        for k in range((b1 & 0xF) + 3):
            c = win[(off + k) & 0xFFF]
            out.append(c)
            win[r] = c
            r = (r + 1) & 0xFFF
    return bytes(out[:size])


def cmpl_compress(data: bytes) -> bytes:
    """Okumura's LZSS.C encoder (binary search trees), the classic 1989 public-domain algorithm.
    `verify --cmpl` shows it reproduces the game's CMPL streams byte for byte (EDF.dll encoder 0x3F350)."""
    N, F, TH, NIL = 4096, 18, 2, 4096
    text = bytearray(N + F - 1)
    lson = [NIL] * (N + 1)
    rson = [NIL] * (N + 257)
    dad = [NIL] * (N + 1)
    mpos = 0
    mlen = 0

    def insert(r: int) -> None:
        nonlocal mpos, mlen
        cmp = 1
        key = r
        p = N + 1 + text[key]
        rson[r] = lson[r] = NIL
        mlen = 0
        while True:
            if cmp >= 0:
                if rson[p] != NIL:
                    p = rson[p]
                else:
                    rson[p] = r
                    dad[r] = p
                    return
            else:
                if lson[p] != NIL:
                    p = lson[p]
                else:
                    lson[p] = r
                    dad[r] = p
                    return
            i = 1
            while i < F:
                cmp = text[key + i] - text[p + i]
                if cmp != 0:
                    break
                i += 1
            if i > mlen:
                mpos = p
                mlen = i
                if mlen >= F:
                    break
        dad[r] = dad[p]
        lson[r] = lson[p]
        rson[r] = rson[p]
        dad[lson[p]] = r
        dad[rson[p]] = r
        if rson[dad[p]] == p:
            rson[dad[p]] = r
        else:
            lson[dad[p]] = r
        dad[p] = NIL

    def delete(p: int) -> None:
        if dad[p] == NIL:
            return
        if rson[p] == NIL:
            q = lson[p]
        elif lson[p] == NIL:
            q = rson[p]
        else:
            q = lson[p]
            if rson[q] != NIL:
                while rson[q] != NIL:
                    q = rson[q]
                rson[dad[q]] = lson[q]
                dad[lson[q]] = dad[q]
                lson[q] = lson[p]
                dad[lson[p]] = q
            rson[q] = rson[p]
            dad[rson[p]] = q
        dad[q] = dad[p]
        if rson[dad[p]] == p:
            rson[dad[p]] = q
        else:
            lson[dad[p]] = q
        dad[p] = NIL

    out = bytearray(b'CMPL' + struct.pack('>I', len(data)))
    if not data:
        return bytes(out)
    code = bytearray(17)
    code[0] = 0
    cptr = 1
    mask = 1
    s = 0
    r = N - F
    src = 0
    length = 0
    while length < F and src < len(data):
        text[r + length] = data[src]
        src += 1
        length += 1
    for i in range(1, F + 1):
        insert(r - i)
    insert(r)
    while True:
        if mlen > length:
            mlen = length
        if mlen <= TH:
            mlen = 1
            code[0] |= mask
            code[cptr] = text[r]
            cptr += 1
        else:
            code[cptr] = (mpos >> 4) & 0xFF     # the game stores the position high byte first
            code[cptr + 1] = ((mpos & 0xF) << 4) | (mlen - (TH + 1))
            cptr += 2
        mask = (mask << 1) & 0xFF
        if mask == 0:
            out += code[:cptr]
            code[0] = 0
            cptr = 1
            mask = 1
        last = mlen
        i = 0
        while i < last and src < len(data):
            c = data[src]
            src += 1
            delete(s)
            text[s] = c
            if s < F - 1:
                text[s + N] = c
            s = (s + 1) & (N - 1)
            r = (r + 1) & (N - 1)
            insert(r)
            i += 1
        while i < last:
            i += 1
            delete(s)
            s = (s + 1) & (N - 1)
            r = (r + 1) & (N - 1)
            length -= 1
            if length:
                insert(r)
        if length <= 0:
            break
    if cptr > 1:
        out += code[:cptr]
    return bytes(out)


# ---------------------------------------------------------------------------------------------- RAB


@dataclass
class RabFile:
    name: str
    folder: int
    flag: int            # entry +0xC: 1 for the HD-TEXTURE folder in every stock archive
    stored: bytes        # as in the archive (raw or CMPL)
    unk: tuple[int, int] = (0, 0)  # entry +0x10/+0x14, zero in every stock archive

    @property
    def data(self) -> bytes:
        return cmpl_decompress(self.stored)


@dataclass
class Rab:
    version: int
    folders: list[str]
    files: list[RabFile]


def _wstr(b: bytes, p: int) -> str:
    e = p
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[p:e].decode('utf-16le')


def _astr(b: bytes, p: int) -> str:
    return b[p:b.index(b'\0', p)].decode('ascii')


def rab_read(b: bytes) -> Rab:
    assert b[:4] == b'SSA\0', 'not a RAB archive'
    ver, _data0, _maxst, _maxraw, cnt, tab, _srt, nfold, ftab = struct.unpack_from('<9I', b, 4)
    folders = [_wstr(b, ftab + i * 4 + struct.unpack_from('<i', b, ftab + i * 4)[0]) for i in range(nfold)]
    files = []
    for i in range(cnt):
        e = tab + i * 0x20
        no, size, fo, fl, u1, u2, off = struct.unpack_from('<iIIIIIQ', b, e)
        files.append(RabFile(_wstr(b, e + no), fo, fl, b[off:off + size], (u1, u2)))
    return Rab(ver, folders, files)


def _raw_size(stored: bytes) -> int:
    return struct.unpack_from('>I', stored, 4)[0] if stored[:4] == b'CMPL' else len(stored)


def rab_write(rab: Rab) -> bytes:
    """Header 0x28, file table (0x20 each, archive order), name-sorted index (8 each), folder table (4 each),
    one sorted de-duplicated UTF-16 string pool, then the stored data back to back (no alignment)."""
    n = len(rab.files)
    tab = 0x28
    srt = tab + n * 0x20
    ftab = srt + n * 8
    pool_at = ftab + len(rab.folders) * 4
    strings = sorted(set(rab.folders) | {f.name for f in rab.files})
    pos: dict[str, int] = {}
    pool = bytearray()
    for s in strings:
        pos[s] = pool_at + len(pool)
        pool += s.encode('utf-16le') + b'\0\0'
    data0 = pool_at + len(pool)
    out = bytearray(data0)
    struct.pack_into('<4s9I', out, 0, b'SSA\0', rab.version, data0,
                     max((len(f.stored) for f in rab.files), default=0),
                     max((_raw_size(f.stored) for f in rab.files), default=0), n, tab, srt, len(rab.folders), ftab)
    off = data0
    for i, f in enumerate(rab.files):
        e = tab + i * 0x20
        struct.pack_into('<iIIIIIQ', out, e, pos[f.name] - e, len(f.stored), f.folder, f.flag, f.unk[0], f.unk[1], off)
        off += len(f.stored)
    order = sorted(range(n), key=lambda i: rab.files[i].name)
    for k, i in enumerate(order):
        p = srt + k * 8
        struct.pack_into('<iI', out, p, pos[rab.files[i].name] - p, i)
    for i, name in enumerate(rab.folders):
        p = ftab + i * 4
        struct.pack_into('<i', out, p, pos[name] - p)
    out[pool_at:data0] = pool
    for f in rab.files:
        out += f.stored
    return bytes(out)


# ---------------------------------------------------------------------------------------------- MDB

# Vertex element formats (layout entry +0). Sizes are bytes; all seen in stock EDF6 models.
VFMT: dict[int, tuple[str, int]] = {
    1: ('float4', 16),
    4: ('float3', 12),
    7: ('half4', 8),
    12: ('float2', 8),
    21: ('ubyte4', 4),
}


@dataclass
class Bone:
    index: int
    parent: int
    sibling: int          # next sibling, -1 = none
    child: int            # first child, -1 = none
    name: int             # name-table index
    child_count: int
    kind: int             # byte +0x18: 0 transform only, 1 rigid-mesh bone, 2 skinned object's bone, 3 skin bone
    depth_delta: int      # byte +0x19 (signed): depth(next bone in file order) - depth(this); last = depth
    bounded: int          # byte +0x1A: 1 = half/centre are meaningful, the instance bounds include this bone
    pad: int              # byte +0x1B (0)
    unk1c: int            # +0x1C (0)
    local: list[float]    # +0x20 4x4 row-major, row-vector convention (row 3 = translation)
    inv_bind: list[float]  # +0x60 4x4: inverse of the bind-pose model-space matrix
    half: list[float]     # +0xA0 vec4 (w = 1)
    centre: list[float]   # +0xB0 vec4, in the bone's bind frame (w = 1)


@dataclass
class Texture:
    index: int
    name: str             # e.g. 'bomber501_df_dds'
    filename: str         # e.g. 'bomber501_df.dds' (the RAB file of the same name)
    unk: int


@dataclass
class MatParam:
    value: list[float]    # vec4
    unk: tuple[int, int]
    name: str             # ascii, e.g. 'diffuse'
    type: int             # u32, e.g. 0x0402 / 0x0201 / 0x0100


@dataclass
class MatTex:
    texture: int          # index into textures
    kind: str             # ascii, e.g. 'albedo', 'normal'
    unk: tuple[int, ...]  # 5 x i32


@dataclass
class Material:
    index: int            # u16
    b2: int
    b3: int
    name: int             # name-table index
    shader: str           # UTF-16, e.g. 'snd_Mech'
    params: list[MatParam]
    textures: list[MatTex]
    unk: int              # +0x1C (3 in every stock file)


@dataclass
class VElem:
    fmt: int
    offset: int
    channel: int
    name: str             # position normal binormal tangent texcoord BLENDWEIGHT BLENDINDICES ...


@dataclass
class Mesh:
    flags: bytes          # 4 bytes: [0] 0, [1] skinned, [2] max influences per vertex, [3] 0
    material: int
    unk08: int
    vsize: int
    elems: list[VElem]
    mesh_index: int       # +0x18
    vdata: bytes          # nverts * vsize
    indices: bytes        # u16 triangle list

    @property
    def nverts(self) -> int:
        return len(self.vdata) // self.vsize


@dataclass
class Object:
    name: int             # name-table index
    bone: int             # bone the object is attached to
    meshes: list[Mesh]


@dataclass
class Mdb:
    version: int
    names: list[str | None]
    bones: list[Bone]
    objects: list[Object]
    materials: list[Material]
    textures: list[Texture]
    tail: bytes = b''     # anything after the string pools (empty in every stock file)
    # File order of the index / vertex buffers as (object, mesh) pairs. Stock files do not always store them in
    # object order (e.g. e503_frog: objects 0, 1, 10, 11, ..., 19, 2, 20, ...: an exporter artefact with no
    # meaning to the loader, which only follows the offsets). Kept so a rewrite is byte-identical; None = object order.
    buffer_order: tuple[list[tuple[int, int]], list[tuple[int, int]]] | None = None

    def name_of(self, i: int) -> str:
        n = self.names[i] if 0 <= i < len(self.names) else None
        return n if n is not None else f'#{i}'

    def bone_index(self, name: str) -> int:
        for b in self.bones:
            if self.names[b.name] == name:
                return b.index
        return -1


def mdb_read(m: bytes) -> Mdb:
    m = cmpl_decompress(m)
    assert m[:4] == b'MDB0', 'not an MDB model'
    (ver, nn, nt, nb, bt, no, ot, nm, mt, ntx, tt) = struct.unpack_from('<11I', m, 4)
    names: list[str | None] = []
    for i in range(nn):
        o = struct.unpack_from('<i', m, nt + i * 4)[0]
        names.append(_wstr(m, nt + i * 4 + o) if o else None)
    bones = []
    for k in range(nb):
        p = bt + k * 0xC0
        idx, par, sib, ch, ni, cc = struct.unpack_from('<6i', m, p)
        kind, dd, bd, pad = struct.unpack_from('<BbBB', m, p + 0x18)
        u1c = struct.unpack_from('<i', m, p + 0x1C)[0]
        bones.append(Bone(idx, par, sib, ch, ni, cc, kind, dd, bd, pad, u1c,
                          list(struct.unpack_from('<16f', m, p + 0x20)), list(struct.unpack_from('<16f', m, p + 0x60)),
                          list(struct.unpack_from('<4f', m, p + 0xA0)), list(struct.unpack_from('<4f', m, p + 0xB0))))
    textures = []
    for k in range(ntx):
        p = tt + k * 0x10
        idx, a, b, u = struct.unpack_from('<4i', m, p)
        textures.append(Texture(idx, _wstr(m, p + a), _wstr(m, p + b), u))
    materials = []
    for k in range(nm):
        p = mt + k * 0x20
        idx, b2, b3, ni, sh, po, pc, xo, xc, u = struct.unpack_from('<HBBiiiiiii', m, p)
        params = []
        for j in range(pc):
            q = p + po + j * 0x20
            v = list(struct.unpack_from('<4f', m, q))
            u0, u1, so, ty = struct.unpack_from('<iiiI', m, q + 0x10)
            params.append(MatParam(v, (u0, u1), _astr(m, q + so), ty))
        texs = []
        for j in range(xc):
            q = p + xo + j * 0x1C
            ti, so, *rest = struct.unpack_from('<7i', m, q)
            texs.append(MatTex(ti, _astr(m, q + so), tuple(rest)))
        materials.append(Material(idx, b2, b3, ni, _wstr(m, p + sh), params, texs, u))
    objects = []
    ib_at: list[tuple[int, int, int]] = []
    vb_at: list[tuple[int, int, int]] = []
    for k in range(no):
        p = ot + k * 0x10
        ni, bone, mc, mo = struct.unpack_from('<4i', m, p)
        meshes = []
        for j in range(mc):
            q = p + mo + j * 0x28
            flags = m[q:q + 4]
            mat, u8, lo = struct.unpack_from('<3i', m, q + 4)
            vs, lc = struct.unpack_from('<HH', m, q + 16)
            nv, mi, vo, ni2, io = struct.unpack_from('<5i', m, q + 20)
            elems = []
            for L in range(lc):
                r = q + lo + L * 16
                f, o, c, so = struct.unpack_from('<4i', m, r)
                elems.append(VElem(f, o, c, _astr(m, r + so)))
            ib_at.append((q + io, k, j))
            vb_at.append((q + vo, k, j))
            meshes.append(Mesh(bytes(flags), mat, u8, vs, elems, mi, m[q + vo:q + vo + nv * vs], m[q + io:q + io + ni2 * 2]))
        objects.append(Object(ni, bone, meshes))
    order = ([(k, j) for _p, k, j in sorted(ib_at)], [(k, j) for _p, k, j in sorted(vb_at)])
    natural = [(k, j) for _p, k, j in ib_at]
    return Mdb(ver, names, bones, objects, materials, textures,
               buffer_order=None if order == (natural, natural) else order)


def _align(n: int, a: int) -> int:
    return (n + a - 1) // a * a


def mdb_write(md: Mdb) -> bytes:
    """Re-serialize in the stock order: header 0x30, name table, bones, textures, material headers, then per
    material its params and texture slots; objects, then per object its mesh headers followed by each mesh's
    layout table; index buffers (mesh order, each 16-aligned), vertex buffers (16-aligned); ASCII string pool
    (sorted, unique), 2-aligned; UTF-16 string pool (sorted, unique)."""
    nn, nb, ntx, nm, no = len(md.names), len(md.bones), len(md.textures), len(md.materials), len(md.objects)
    nt = 0x30
    bt = nt + nn * 4
    tt = bt + nb * 0xC0
    mt = tt + ntx * 0x10
    p = mt + nm * 0x20
    mat_sub = []
    for mat in md.materials:
        po = p
        p += len(mat.params) * 0x20
        xo = p
        p += len(mat.textures) * 0x1C
        mat_sub.append((po, xo))
    ot = p
    p = ot + no * 0x10
    obj_mesh = []
    mesh_hdr = []    # (obj k, mesh j, header pos, layout pos)
    for k, ob in enumerate(md.objects):
        obj_mesh.append(p)
        hdr = p
        p += len(ob.meshes) * 0x28
        for j, me in enumerate(ob.meshes):
            mesh_hdr.append((k, j, hdr + j * 0x28, p))
            p += len(me.elems) * 0x10
    natural = [(k, j) for k, j, _h, _l in mesh_hdr]
    i_order, v_order = md.buffer_order or (natural, natural)
    assert sorted(i_order) == sorted(natural) and sorted(v_order) == sorted(natural), 'buffer_order does not match meshes'
    i_pos: dict[tuple[int, int], int] = {}
    for k, j in i_order:
        p = _align(p, 16)
        i_pos[(k, j)] = p
        p += len(md.objects[k].meshes[j].indices)
    v_pos: dict[tuple[int, int], int] = {}
    for k, j in v_order:
        p = _align(p, 16)
        v_pos[(k, j)] = p
        p += len(md.objects[k].meshes[j].vdata)
    idx_at = [i_pos[kj] for kj in natural]
    v_at = [v_pos[kj] for kj in natural]
    ascii_set = {e.name for ob in md.objects for me in ob.meshes for e in me.elems}
    ascii_set |= {x.name for mat in md.materials for x in mat.params}
    ascii_set |= {x.kind for mat in md.materials for x in mat.textures}
    apos: dict[str, int] = {}
    ascii_at = p
    for s in sorted(ascii_set):
        apos[s] = p
        p += len(s) + 1
    p = _align(p, 2)
    wide_set = {n for n in md.names if n is not None}
    wide_set |= {t.name for t in md.textures} | {t.filename for t in md.textures} | {m.shader for m in md.materials}
    wpos: dict[str, int] = {}
    wide_at = p
    for s in sorted(wide_set):
        wpos[s] = p
        p += len(s.encode('utf-16le')) + 2
    out = bytearray(p)
    struct.pack_into('<4s11I', out, 0, b'MDB0', md.version, nn, nt, nb, bt, no, ot, nm, mt, ntx, tt)
    for i, n in enumerate(md.names):
        q = nt + i * 4
        struct.pack_into('<i', out, q, (wpos[n] - q) if n is not None else 0)
    for k, b in enumerate(md.bones):
        q = bt + k * 0xC0
        struct.pack_into('<6i', out, q, b.index, b.parent, b.sibling, b.child, b.name, b.child_count)
        struct.pack_into('<BbBBi', out, q + 0x18, b.kind, b.depth_delta, b.bounded, b.pad, b.unk1c)
        struct.pack_into('<16f', out, q + 0x20, *b.local)
        struct.pack_into('<16f', out, q + 0x60, *b.inv_bind)
        struct.pack_into('<4f', out, q + 0xA0, *b.half)
        struct.pack_into('<4f', out, q + 0xB0, *b.centre)
    for k, t in enumerate(md.textures):
        q = tt + k * 0x10
        struct.pack_into('<4i', out, q, t.index, wpos[t.name] - q, wpos[t.filename] - q, t.unk)
    for k, mat in enumerate(md.materials):
        q = mt + k * 0x20
        po, xo = mat_sub[k]
        struct.pack_into('<HBBiiiiiii', out, q, mat.index, mat.b2, mat.b3, mat.name, wpos[mat.shader] - q,
                         po - q, len(mat.params), xo - q, len(mat.textures), mat.unk)
        for j, x in enumerate(mat.params):
            r = po + j * 0x20
            struct.pack_into('<4f', out, r, *x.value)
            struct.pack_into('<iiiI', out, r + 0x10, x.unk[0], x.unk[1], apos[x.name] - r, x.type)
        for j, x in enumerate(mat.textures):
            r = xo + j * 0x1C
            struct.pack_into('<7i', out, r, x.texture, apos[x.kind] - r, *x.unk)
    for k, ob in enumerate(md.objects):
        q = ot + k * 0x10
        struct.pack_into('<4i', out, q, ob.name, ob.bone, len(ob.meshes), obj_mesh[k] - q)
    for n, (k, j, h, lo) in enumerate(mesh_hdr):
        me = md.objects[k].meshes[j]
        out[h:h + 4] = me.flags
        struct.pack_into('<3i', out, h + 4, me.material, me.unk08, lo - h)
        struct.pack_into('<HH', out, h + 16, me.vsize, len(me.elems))
        struct.pack_into('<5i', out, h + 20, me.nverts, me.mesh_index, v_at[n] - h, len(me.indices) // 2, idx_at[n] - h)
        for L, e in enumerate(me.elems):
            r = lo + L * 16
            struct.pack_into('<4i', out, r, e.fmt, e.offset, e.channel, apos[e.name] - r)
        out[idx_at[n]:idx_at[n] + len(me.indices)] = me.indices
        out[v_at[n]:v_at[n] + len(me.vdata)] = me.vdata
    for s, q in apos.items():
        out[q:q + len(s) + 1] = s.encode('ascii') + b'\0'
    for s, q in wpos.items():
        e = s.encode('utf-16le') + b'\0\0'
        out[q:q + len(e)] = e
    del ascii_at, wide_at
    return bytes(out) + md.tail


# ------------------------------------------------------------------------------------- matrix helpers

Mat = list[float]   # 16 floats, row-major, row-vector convention: p' = p * M, row 3 = translation


def mmul(a: Mat, b: Mat) -> Mat:
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


def ident() -> Mat:
    return [1.0 if r == c else 0.0 for r in range(4) for c in range(4)]


def inverse_affine(m: Mat) -> Mat:
    """Inverse of an affine row-vector matrix whose 3x3 part is a rotation (orthonormal)."""
    r = [[m[i * 4 + j] for j in range(3)] for i in range(3)]
    t = m[12:15]
    rt = [[r[j][i] for j in range(3)] for i in range(3)]
    nt = [-sum(t[k] * rt[k][j] for k in range(3)) for j in range(3)]
    return [rt[0][0], rt[0][1], rt[0][2], 0.0, rt[1][0], rt[1][1], rt[1][2], 0.0,
            rt[2][0], rt[2][1], rt[2][2], 0.0, nt[0], nt[1], nt[2], 1.0]


def bind_world(md: Mdb) -> list[Mat]:
    """Model-space bind matrix of every bone: local * parent's (bones are stored parent-first)."""
    w: list[Mat] = []
    for b in md.bones:
        w.append(b.local if b.parent < 0 else mmul(b.local, w[b.parent]))
    return w


def depth_of(md: Mdb) -> list[int]:
    d: list[int] = []
    for b in md.bones:
        d.append(0 if b.parent < 0 else d[b.parent] + 1)
    return d


# ---------------------------------------------------------------------------------------- vertices

def half_to_float(h: int) -> float:
    return struct.unpack('<e', struct.pack('<H', h))[0]


def read_elem(me: Mesh, name: str, channel: int = 0) -> list[tuple[float, ...]] | None:
    e = next((x for x in me.elems if x.name == name and x.channel == channel), None)
    if e is None:
        return None
    fmt = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}[e.fmt]
    return [struct.unpack_from(fmt, me.vdata, v * me.vsize + e.offset) for v in range(me.nverts)]


# ------------------------------------------------------------------------------------------- loading

_GAME = None


def _game() -> object:
    """The game's Root.cpk reader (pylib/rootcpk.py), opened once per process."""
    global _GAME
    if _GAME is None:
        from rootcpk import Game
        _GAME = Game(GAME)
    return _GAME


def _load(arg: str) -> bytes:
    if os.path.exists(arg):
        with open(arg, 'rb') as h:
            return h.read()
    return _game().read('OBJECT', arg)


def _stock_archives() -> list[str]:
    return sorted(n for (d, n) in _game().cpk.index if d.upper() == 'OBJECT' and n.upper().endswith(('.RAB', '.MRAB')))


# ---------------------------------------------------------------------------------------------- dump

def dump(md: Mdb, out=sys.stdout) -> None:
    w = bind_world(md)
    print(f'MDB0 v{md.version:#x}  names {len(md.names)} ({sum(n is not None for n in md.names)} used)  '
          f'bones {len(md.bones)}  objects {len(md.objects)}  materials {len(md.materials)}  textures {len(md.textures)}', file=out)
    for b in md.bones:
        t = w[b.index][12:15]
        print(f'  bone [{b.index:2}] {md.name_of(b.name):<24} parent {b.parent:2} sib {b.sibling:2} child {b.child:2} '
              f'n{b.child_count} kind {b.kind} dd {b.depth_delta:2} bnd {b.bounded}  bind ({t[0]:7.3f} {t[1]:7.3f} {t[2]:7.3f})  '
              f'half ({b.half[0]:.2f} {b.half[1]:.2f} {b.half[2]:.2f}) centre ({b.centre[0]:.2f} {b.centre[1]:.2f} {b.centre[2]:.2f})', file=out)
    for t in md.textures:
        print(f'  texture [{t.index}] {t.name}  ({t.filename})', file=out)
    for mt in md.materials:
        print(f'  material [{mt.index}] {md.name_of(mt.name)}  shader {mt.shader}  unk {mt.b2},{mt.b3},{mt.unk}', file=out)
        for x in mt.params:
            print(f'      param {x.name:<34} type {x.type:#06x} = {[round(v, 4) for v in x.value]}', file=out)
        for x in mt.textures:
            print(f'      tex   {x.kind:<16} -> [{x.texture}] {md.textures[x.texture].name if 0 <= x.texture < len(md.textures) else "?"}  {x.unk}', file=out)
    for k, ob in enumerate(md.objects):
        print(f'  object [{k}] {md.name_of(ob.name)}  bone [{ob.bone}] {md.name_of(md.bones[ob.bone].name)}', file=out)
        for j, me in enumerate(ob.meshes):
            print(f'    mesh [{j}] flags {me.flags.hex()} material {me.material} verts {me.nverts} tris {len(me.indices) // 6} '
                  f'vsize {me.vsize} mesh_index {me.mesh_index}', file=out)
            print('        ' + '  '.join(f'{e.name}{e.channel}:{VFMT.get(e.fmt, ("fmt" + str(e.fmt), 0))[0]}@{e.offset}' for e in me.elems), file=out)
            bi = read_elem(me, 'BLENDINDICES')
            bw = read_elem(me, 'BLENDWEIGHT')
            if bi and bw:
                used: dict[int, int] = {}
                for i4, w4 in zip(bi, bw):
                    for i, x in zip(i4, w4):
                        if x > 0:
                            used[i] = used.get(i, 0) + 1
                print('        skin bones: ' + ', '.join(f'{md.name_of(md.bones[i].name)}[{i}]x{n}' for i, n in sorted(used.items())), file=out)


# -------------------------------------------------------------------------------------------- verify

def verify(names: list[str], cmpl: bool, fast: bool = False) -> int:
    """Round-trip every model (and, with cmpl, re-encode every CMPL stream and rebuild the archive)."""
    bad = 0
    total = 0
    stats: dict[str, int] = {}
    for n_an, an in enumerate(names):
        print(f'.. [{n_an + 1}/{len(names)}] {an}', file=sys.stderr, flush=True)
        raw = _load(an)
        rab = rab_read(raw)
        if rab_write(rab) != raw:
            print(f'RAB MISMATCH {an}')
            bad += 1
        for f in rab.files:
            if f.stored[:4] == b'CMPL' and (cmpl or (not fast and f.name.lower().endswith('.mdb'))):
                if cmpl_compress(f.data) != f.stored:
                    print(f'CMPL MISMATCH {an}/{f.name}', flush=True)
                    bad += 1
            if not f.name.lower().endswith('.mdb'):
                continue
            total += 1
            data = f.data
            try:
                md = mdb_read(data)
                again = mdb_write(md)
            except Exception as ex:  # noqa: BLE001 - report and continue
                print(f'PARSE FAIL {an}/{f.name}: {ex!r}')
                bad += 1
                continue
            ok = again == data
            if not ok:
                first = next((i for i in range(min(len(again), len(data))) if again[i] != data[i]), min(len(again), len(data)))
                print(f'MDB MISMATCH {an}/{f.name}: len {len(again)} vs {len(data)}, first diff at {first:#x}')
                bad += 1
            # semantic checks
            for e in (e for ob in md.objects for me in ob.meshes for e in me.elems):
                stats[f'fmt{e.fmt}:{e.name}'] = stats.get(f'fmt{e.fmt}:{e.name}', 0) + 1
            w = bind_world(md)
            for b in md.bones:
                inv = inverse_affine(w[b.index])
                if max(abs(x - y) for x, y in zip(inv, b.inv_bind)) > 2e-3:
                    stats['inv_bind != inverse(bind)'] = stats.get('inv_bind != inverse(bind)', 0) + 1
            d = depth_of(md)
            for i, b in enumerate(md.bones):
                exp = (d[i + 1] - d[i]) if i + 1 < len(md.bones) else d[i]
                if b.depth_delta != exp:
                    stats['depth_delta rule broken'] = stats.get('depth_delta rule broken', 0) + 1
            for ob in md.objects:
                for me in ob.meshes:
                    k = f'flags {me.flags.hex()} bonekind {md.bones[ob.bone].kind}'
                    stats[k] = stats.get(k, 0) + 1
    print(f'{total} models in {len(names)} archives, {bad} problems')
    for k, v in sorted(stats.items()):
        print(f'  {k}: {v}')
    return bad


# ------------------------------------------------------------------------------- jet control surfaces

def main(argv: list[str]) -> int:
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == 'dump':
        rab = rab_read(_load(rest[0]))
        for f in rab.files:
            if f.name.lower().endswith('.mdb') and (len(rest) < 2 or f.name.lower() == rest[1].lower()):
                print(f'== {rest[0]} / {f.name}')
                dump(mdb_read(f.data))
        return 0
    if cmd == 'verify':
        cmpl = '--cmpl' in rest
        names = [a for a in rest if not a.startswith('--')] or _stock_archives()
        return 1 if verify(names, cmpl, '--fast' in rest) else 0
    if cmd == 'jet':
        import mdb_jet  # noqa: E402  (pylib/mdb_jet.py)
        return mdb_jet.main(rest)
    print(f'unknown command {cmd}')
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
