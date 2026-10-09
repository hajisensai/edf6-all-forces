"""EDF4.1 / EDF5 MDB models (MDB0 version 0x14) turned into EDF6's (version 0x20).

    mdb_from_legacy(b, map_part=False) -> bytes    the 0x20 model (uncompressed); ValueError on anything not as below

The file layout (header, name table, bones, textures, materials, objects, meshes, buffers, string pools:
docs/mdb-format.md) is the same in all three games: pylib/mdb.py reads and rewrites every MDB in the Root / Chunk cpks
of EDF4.1 (4385), EDF5 (6477) and EDF6 (18826) byte for byte. EDF.dll only takes {'MDB0', 0x20}: RVA 0x11126C0 passes
that pair to the generic header check 0x4AFC0, which compares magic and version for equality. What a 0x14 file has to
change, from every distinct EDF5 / EDF6 pair of the same model (3203) and EDF4.1 / EDF5 pair (1134), 2026-10-10:

 1. version 0x14 -> 0x20.
 2. Object names. 0x14: object +0 is the object's own index (0, 1, 2 ...: 10760 of 10760 4.1 / EDF5 files with
    objects) and the name table has no object names (every slot after the bone and material names is empty). 0x20:
    object k's +0 is the name-table index nb + nm + k (nb bones, nm materials; 18669 of 18669 EDF6 files with
    objects) and that slot holds the object's name: the name of the bone it hangs on (all objects of 18756 of 18826
    files); in the other 70 a second / third object on the same bone is named bone + '.B' / bone + '.c' (91 / 95;
    '.b' 4). No 4.1 / EDF5 file hangs two objects on one bone, so that suffix rule is EDF6 evidence only. The table
    keeps its length (there is always room: 10862 of 10862).
 3. Binormals negated (x, y, z; w stays 1). 4.1 / EDF5 frames are right-handed: dot(cross(N, T), B) > 0 for 91% / 85%
    of sampled vertices; EDF6's are left-handed (85% < 0), its own meshes on the EDF5-era shaders too (12038 of 13030
    meshes). EDF5 / EDF6 pairs: 1957 meshes carry exactly the EDF5 binormal negated, 10 keep the binormal and negate the
    tangent (the same handedness flip; e516_biggreyBoss_ring_large etc.: this converter differs from EDF6 there).
 4. Skinned meshes put BLENDWEIGHT, BLENDINDICES last; the other elements keep their order and pack from offset 0.
    EDF6: 18256 of 18256 sampled skinned meshes; EDF5: 17 of 18448.
 5. A skinned object's bone that no vertex weights (weight > 0) gets bounded (+0x1A) = 0: EDF6 1120 of 1121 such
    bones (every 2nd file sampled), EDF5 0 of 524 (always 1).
 6. Material parameter components past the parameter's count (type >> 8) holding uninitialised memory (0 < |x| <
    1e-20, |x| > 1e7, NaN / inf) are zeroed: EDF5 has 6072 such components (4.1 2568), EDF6 none; in the EDF5 / EDF6
    pairs 2092 of them became 0 (every snd_Map_Build_Window material). Sane values there (the 1.0s in specular_pow
    (100, 1, 1, 0)) stay, as EDF6 keeps them. Params of kind 4 (int) and 0xff are left alone.
Nothing else changes. Materials on a shader EDF6 kept are identical in 5310 of 5640 EDF5 / EDF6 pairs (shader name,
param names / order / types / values, texture slots with their +8 / +0x18 sampler fields, header); the other 330 are
value edits. EDF6's DX11.cpk still ships all 89 EDF5 shaders, and 87 of them declare the same xgl_user_param variables,
textures and vertex inputs as EDF5's (TEST_NORMAL drops parallax, TEST_SIMPLE gains skinning techniques). EDF6's own
materials on those shaders (24170, 17890 outside the EDF5 pairs) use the same param-name sets as EDF5's, list texture
slots in any order, mix 0x302 / 0x402 for colours, and omit metallic / roughness just as often (4461), so no material
needs rewriting. EDF6 moved ~900 EDF5 materials to snd_BRDF_* shaders with new param textures (param_r_m_occ_hr ...),
re-rigged or re-exported most models (maps lost the 'mdl' root bone): new content, not a format change.

EDF4.1 files (also 0x14) differ from EDF5's in two ways, then take the steps above:
 a. Param type 0xNK (N = component count; K = kind: 0 float, 1 float2, 2 float3 / colour, 3 float4 colour, 4 int,
    0xff other) is stored with N - 1: every 4.1 / EDF5 pair has the 4.1 type + 0x100 (0x0 -> 0x100, 0x101 -> 0x201,
    0x302 -> 0x402, 0x303 -> 0x403, 0x3ff -> 0x4ff; no exception). A file is 4.1-encoded when a type is 0x0 / 0x101 /
    0x303 / 0x3ff: 4322 of 4322 4.1 files with params, no EDF6 file, and only EDF5's 25 IG_CAVE501_TEST leftovers.
    A file mixing the encodings, or whose only type is the ambiguous 0x302, raises.
 b. map_part=True (a map part, from MAP/): each rigid mesh (flags 00000000) on a snd_Map_* shader becomes a one-bone
    skinned mesh, as EDF5's port of 4.1's maps did (928 of 928 in MAP pairs; ROCK.MRAB's rigid snd_Map_Build_NoOcc
    rocks in OBJECT/ stay rigid in EDF5 and EDF6, so it follows use, not the shader): flags 00010100, BLENDWEIGHT
    float4 (1, 0, 0, 0) and BLENDINDICES ubyte4 (object bone, 0, 0, 0) appended, the object bone's kind 1 -> 2;
    vertices and inv_bind unchanged (every 4.1 map's rigid object bone has inv_bind = identity, 48004 meshes, so the
    skinned v * inv_bind * world is the rigid v * world). EDF5 / EDF6 map-shader meshes in MAP/ are skinned 16867 /
    52477 times, rigid 30 / 3 (EDF6's 3 are the rocks); EDF5-era map shaders take BLENDINDICES in their instancing
    techniques where 4.1's took INSTANCING_MATRIX0..2. Missing params (4.1 has no metallic / roughness) stay missing:
    EDF5 added them to only 380 of the 1914 4.1 materials it kept on the same shader, and EDF6 itself omits them 4461
    times. 4.1's vertex format 24 (a 4-byte texcoord, half2 by its values; 657 elements in 8 station models of
    NW_HILLYCITY_LIGHT / NW_KASENJIKI01) appears in no EDF5 / EDF6 model and raises.

Checked (jobs/4bf89026/tmp/mdb/final_stats.py, cross41.py, convert_all.py): every 4.1 MDB but those 8 and every
EDF5 MDB converts and re-parses with EDF6's invariants. EDF5 -> EDF6, 3203 pairs: 147 byte for byte; 320 more where
EDF6 only dropped the 'mdl' root have identical vertex data (BLENDINDICES - 1); 204 differ only by EDF6 content edits;
3 are the tangent case of step 3; the rest were remodelled. 4.1 -> EDF6, 1193 pairs: 29 byte for byte, 112 more
identical vertex data under the dropped root. 4.1 / EDF5 pairs converted both ways: 247 identical outputs, 353 more
identical but for -0.0 vs 0.0 in bone matrices; the others are EDF5's own edits.
"""
from __future__ import annotations

import math
import struct

from mdb import Mdb, Mesh, VElem, VFMT, cmpl_decompress, mdb_read, mdb_write

LEGACY_VERSION = 0x14
EDF6_VERSION = 0x20
_FMT: dict[int, str] = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}
_BLEND: tuple[str, str] = ('blendweight', 'blendindices')
_TYPES_41: frozenset[int] = frozenset({0x000, 0x101, 0x302, 0x303, 0x3FF})
_ONLY_41: frozenset[int] = frozenset({0x000, 0x101, 0x303, 0x3FF})
_TYPES_5: frozenset[int] = frozenset({0x100, 0x201, 0x302, 0x402, 0x403, 0x104, 0x1FF, 0x2FF, 0x4FF})
_DUP_SUFFIX: tuple[str, ...] = ('', '.B', '.c')
_SKIN_FLAGS = bytes((0, 1, 1, 0))


def _param_encoding(md: Mdb) -> str:
    """'41', '5' or 'none' (no params). ValueError on a mix, on the ambiguous all-0x302 case, on unknown codes."""
    types = {p.type for m in md.materials for p in m.params}
    if not types:
        return 'none'
    if types & _ONLY_41:
        if not types <= _TYPES_41:
            raise ValueError(f'param types mix the EDF4.1 and EDF5 encodings: {sorted(map(hex, types))}')
        return '41'
    if types == {0x302}:
        raise ValueError('every param type is 0x302: EDF4.1 (4 floats) or EDF5 (3 floats) cannot be told apart')
    if not types <= _TYPES_5:
        raise ValueError(f'unknown param types {sorted(map(hex, types - _TYPES_5))}')
    return '5'


def _is_identity(m: list[float]) -> bool:
    return all(abs(m[i] - (1.0 if i % 5 == 0 else 0.0)) < 1e-5 for i in range(16))


def _skin_rigid_map_meshes(md: Mdb) -> None:
    """EDF5's port of EDF4.1 map parts: rigid snd_Map_* meshes -> skinned to their object bone with weight 1."""
    for ob in md.objects:
        for me in ob.meshes:
            if me.flags[1] or not 0 <= me.material < len(md.materials):
                continue
            if not md.materials[me.material].shader.lower().startswith('snd_map'):
                continue
            if me.flags != bytes(4):
                raise ValueError(f'rigid mesh with flags {me.flags.hex()}')
            if any(e.name.lower() in _BLEND for e in me.elems):
                raise ValueError('rigid mesh already has blend elements')
            bone = md.bones[ob.bone]
            if not _is_identity(bone.inv_bind):
                raise ValueError('rigid map mesh on a bone whose inv_bind is not identity')
            if ob.bone > 255:
                raise ValueError('object bone index does not fit BLENDINDICES')
            add = struct.pack('<4f', 1.0, 0.0, 0.0, 0.0) + bytes((ob.bone, 0, 0, 0))
            out = bytearray()
            for v in range(me.nverts):
                out += me.vdata[v * me.vsize:(v + 1) * me.vsize] + add
            me.elems = me.elems + [VElem(1, me.vsize, 0, 'BLENDWEIGHT'), VElem(21, me.vsize + 16, 0, 'BLENDINDICES')]
            me.vsize += 20
            me.vdata = bytes(out)
            me.flags = _SKIN_FLAGS
            if bone.kind == 1:
                bone.kind = 2


def _name_objects(md: Mdb) -> None:
    nb, nm = len(md.bones), len(md.materials)
    if any(n is not None for n in md.names[nb + nm:]):
        raise ValueError('name table has names after the materials (not a 0x14 layout)')
    if len(md.names) < nb + nm + len(md.objects):
        raise ValueError('name table has no room for the object names')
    if any(o.name != k for k, o in enumerate(md.objects)):
        raise ValueError('object +0 is not the object index')
    seen: dict[int, int] = {}
    for k, ob in enumerate(md.objects):
        if not 0 <= ob.bone < nb:
            raise ValueError('object bone out of range')
        n = seen.get(ob.bone, 0)
        seen[ob.bone] = n + 1
        if n >= len(_DUP_SUFFIX):
            raise ValueError('more than 3 objects on one bone (EDF6 naming not known)')
        md.names[nb + nm + k] = md.name_of(md.bones[ob.bone].name) + _DUP_SUFFIX[n]
        ob.name = nb + nm + k


def _weighted_bones(md: Mdb) -> set[int]:
    used: set[int] = set()
    for ob in md.objects:
        for me in ob.meshes:
            el = {e.name.lower(): e for e in me.elems if e.channel == 0}
            bi, bw = el.get('blendindices'), el.get('blendweight')
            if bi is None or bw is None:
                continue
            fi, fw = _FMT[bi.fmt], _FMT[bw.fmt]
            for v in range(me.nverts):
                o = v * me.vsize
                for i, w in zip(struct.unpack_from(fi, me.vdata, o + bi.offset),
                                struct.unpack_from(fw, me.vdata, o + bw.offset)):
                    if w > 0:
                        used.add(int(i))
    return used


def _unbound_unweighted_object_bones(md: Mdb) -> None:
    used = _weighted_bones(md)
    for ob in md.objects:
        if any(me.flags[1] for me in ob.meshes) and ob.bone not in used:
            md.bones[ob.bone].bounded = 0


def _junk(x: float) -> bool:
    return not math.isfinite(x) or (x != 0.0 and abs(x) < 1e-20) or abs(x) > 1e7


def _zero_junk_padding(md: Mdb) -> None:
    for m in md.materials:
        for p in m.params:
            if p.type & 0xFF in (4, 0xFF):
                continue
            n = p.type >> 8
            p.value = [0.0 if i >= n and _junk(x) else x for i, x in enumerate(p.value)]


def _negate_binormal(me: Mesh) -> None:
    v = bytearray(me.vdata)
    for e in me.elems:
        if e.name.lower() != 'binormal':
            continue
        if e.fmt not in (1, 4, 7):
            raise ValueError(f'binormal in format {e.fmt}')
        f = _FMT[e.fmt]
        for i in range(me.nverts):
            o = i * me.vsize + e.offset
            x = list(struct.unpack_from(f, v, o))
            x[0], x[1], x[2] = -x[0], -x[1], -x[2]
            struct.pack_into(f, v, o, *x)
    me.vdata = bytes(v)


def _blend_last(me: Mesh) -> None:
    blend = sorted((e for e in me.elems if e.name.lower() in _BLEND), key=lambda e: _BLEND.index(e.name.lower()))
    order = [e for e in me.elems if e.name.lower() not in _BLEND] + blend
    if order == me.elems:
        return
    out = bytearray(len(me.vdata))
    elems: list[VElem] = []
    off = 0
    for e in order:
        size = VFMT[e.fmt][1]
        for i in range(me.nverts):
            b = i * me.vsize
            out[b + off:b + off + size] = me.vdata[b + e.offset:b + e.offset + size]
        elems.append(VElem(e.fmt, off, e.channel, e.name))
        off += size
    if off != me.vsize:
        raise ValueError('vertex layout does not fill the vertex (gaps or overlaps)')
    me.vdata = bytes(out)
    me.elems = elems


def _check_mesh(me: Mesh) -> None:
    ends = 0
    for e in me.elems:
        if e.fmt not in _FMT:
            raise ValueError(f'unknown vertex format {e.fmt}')
        ends += VFMT[e.fmt][1]
    if ends != me.vsize:
        raise ValueError('vertex layout does not fill the vertex (gaps or overlaps)')
    if me.flags[0] or me.flags[3] or me.flags[1] not in (0, 1):
        raise ValueError(f'unknown mesh flags {me.flags.hex()}')
    if me.flags[1] != any(e.name.lower() == 'blendindices' for e in me.elems):
        raise ValueError('skinned flag does not match the blend elements')


def mdb_from_legacy(b: bytes, map_part: bool = False) -> bytes:
    """An EDF4.1 / EDF5 MDB (version 0x14, raw or CMPL) as EDF6's version 0x20 (raw, uncompressed). `map_part`: the
    model is a map part (MAP/): rigid snd_Map_* meshes become single-bone skinned ones (EDF4.1 maps; a no-op for
    EDF5's, already skinned). Raises ValueError on anything this conversion was not derived from (module docstring)."""
    data = cmpl_decompress(b)
    if data[:4] != b'MDB0':
        raise ValueError('not an MDB0 model')
    ver = struct.unpack_from('<I', data, 4)[0]
    if ver != LEGACY_VERSION:
        raise ValueError(f'MDB version {ver:#x}, expected {LEGACY_VERSION:#x}')
    try:
        md = mdb_read(data)
        same = mdb_write(md) == data
    except (AssertionError, struct.error, IndexError, UnicodeDecodeError, KeyError) as ex:
        raise ValueError(f'unreadable MDB: {ex!r}') from ex
    if not same:
        raise ValueError('unrecognised MDB layout (does not re-serialise byte for byte)')
    for ob in md.objects:
        for me in ob.meshes:
            _check_mesh(me)
    if _param_encoding(md) == '41':
        for m in md.materials:
            for p in m.params:
                p.type += 0x100
    if map_part:
        _skin_rigid_map_meshes(md)
    md.version = EDF6_VERSION
    _name_objects(md)
    _unbound_unweighted_object_bones(md)
    _zero_junk_padding(md)
    for ob in md.objects:
        for me in ob.meshes:
            _negate_binormal(me)
            _blend_last(me)
    return mdb_write(md)
