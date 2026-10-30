"""EDF6 map files (MAC / MAD / RAB) of a scene of static models (pylib/xm_map.py's Scene).

Every model is a map object (an `.mdx` def): its mesh (`<model>.mdb`) in the RAB, its collision (body
`<model>.<model>`) in the MAD, placed by a mapb record. The ground is such objects too (def type 3, the terrain's
material palette). docs/xmorph-maps.md has the formats this writes and where each layout was measured.

- rab(scene): textures (HD + .lod pairs, CMPL) then models, the stock folder order.
- mad(scene, donor): collision.hkt written by hkcms_build on EDF6's own map collision (IG_HEIGEN601.MAD) as donor.
- mac(scene, template): IG_TEST_BLUEFLD.MAC with its defs / records / objects replaced by the scene's.
"""
from __future__ import annotations

import struct
import uuid
from dataclasses import dataclass

import numpy as np

import bigmap
import graft_pure
import hkcms_build
import mdb
import sgo
import texfile
import texfile_accel

MDB_VERSION = 0x20
MAX_VERTS = 0xFFFF                 # u16 indices
TEMPLATE_MAC = 'IG_TEST_BLUEFLD.MAC'
DONOR_MAD = 'IG_HEIGEN601.MAD'
GROUND_FAR = 1000.0                # def +1C / SGO Disappear: the stock ground's draw distance
OBJECT_FAR = 1000.0                # the props: a city map is seen from far (stock buildings: 500 .. 1500)
FLAT_RMO = (181, 0, 255)           # roughness 0.71, metallic 0, occlusion 1: the stock ig_flat_rmo
WIRE_TEXTURE = 'wire_test.dds'


# ------------------------------------------------------------------------------------------------ textures

def _dxt5_dds(rgba: np.ndarray) -> bytes:
    """A DXT5 DDS of an H x W x 4 uint8 image with its whole mip chain: per block BC4 alpha then the DXT1 colour block
    (texfile_accel's range fit, which never picks the 3-colour mode except on flat blocks, whose indices are 0)."""
    h, w = rgba.shape[:2]
    levels = []
    px = rgba
    while True:
        alpha = np.frombuffer(texfile_accel._bc4_blocks(texfile_accel._blocks16(px[..., 3])), np.uint8).reshape(-1, 8)
        colour = np.frombuffer(texfile_accel._level(px), np.uint8).reshape(-1, 8)
        levels.append(np.hstack((alpha, colour)).tobytes())
        if px.shape[0] == 1 and px.shape[1] == 1:
            break
        px = texfile_accel._half(px)
    top = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    return texfile._header(w, h, len(levels), b'DXT5', 0, (0, 0, 0, 0), top) + b''.join(levels)


def texture_dds(image, mode: str) -> bytes:  # noqa: ANN001 - PIL image
    """The DDS of a scene texture: DXT1 when opaque, DXT5 when its alpha counts (clip / blend)."""
    rgba = np.asarray(image.convert('RGBA'), dtype=np.uint8)
    if mode == 'opaque':
        h, w = rgba.shape[:2]
        return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(rgba.tobytes())))
    return _dxt5_dds(np.ascontiguousarray(rgba))


# ------------------------------------------------------------------------------------------------ models

SHADERS = {'opaque': 'snd_BRDF_Map_Common', 'clip': 'snd_BRDF_Map_Common_Clip', 'blend': 'snd_BRDF_Map_Common_Alpha'}
B3 = {'opaque': 0, 'clip': 0, 'blend': 2}


def _params(mode: str) -> list[mdb.MatParam]:
    def p(name: str, value: list[float], kind: int) -> mdb.MatParam:
        return mdb.MatParam(value + [0.0] * (4 - len(value)), (0, 0), name, kind)
    out = [p('alpha_clip', [0.5], 256)] if mode == 'clip' else []
    out += [p('diffuse', [1.0, 1.0, 1.0, 0.5] if mode == 'blend' else [1.0, 1.0, 1.0, 0.0], 1027 if mode == 'blend' else 770),
            p('metallic', [1.0], 256), p('normal_amount', [1.0], 256), p('roughness', [1.0], 256),
            p('specular_color', [1.0, 1.0, 1.0, 0.0], 770), p('specular_diffuse_blend', [0.5], 256)]
    if mode == 'blend':
        out += [p('translucent_fall_off_amount', [1.0], 256), p('translucent_fall_off_offset', [1.0], 256),
                p('translucent_fall_off_scale', [1.0], 256)]
    return out


ELEMS = [mdb.VElem(7, 0, 0, 'BINORMAL'), mdb.VElem(7, 8, 0, 'TANGENT'), mdb.VElem(7, 16, 0, 'NORMAL'),
         mdb.VElem(4, 24, 0, 'POSITION'), mdb.VElem(12, 36, 0, 'TEXCOORD'), mdb.VElem(1, 44, 0, 'BLENDWEIGHT'),
         mdb.VElem(21, 60, 0, 'BLENDINDICES')]
VERTEX = np.dtype([('binormal', '<f2', 4), ('tangent', '<f2', 4), ('normal', '<f2', 4), ('position', '<f4', 3),
                   ('uv', '<f4', 2), ('weight', '<f4', 4), ('index', 'u1', 4)])
assert VERTEX.itemsize == 64


def _frame(nrm: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """A tangent and binormal for each unit normal (the normal maps are flat, so any orthonormal pair is right)."""
    ref = np.where(np.abs(nrm[:, 1:2]) < 0.9, np.array([[0.0, 1.0, 0.0]]), np.array([[1.0, 0.0, 0.0]]))
    t = np.cross(ref, nrm)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-12)
    return t, np.cross(nrm, t)


def _vertices(pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray) -> bytes:
    n = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    t, b = _frame(n)
    v = np.zeros(len(pos), VERTEX)
    v['binormal'][:, :3], v['binormal'][:, 3] = b, 1.0
    v['tangent'][:, :3], v['tangent'][:, 3] = t, 1.0
    v['normal'][:, :3], v['normal'][:, 3] = n, 1.0
    v['position'] = pos
    v['uv'] = uv
    v['weight'][:, 0] = 1.0
    return v.tobytes()


def _chunks(tris: np.ndarray) -> list[tuple[np.ndarray, np.ndarray]]:
    """(vertex indices into the part, its triangles re-indexed) per run of MAX_VERTS // 3 triangles: a run can use
    no more vertices than u16 indices reach."""
    out = []
    for start in range(0, len(tris), MAX_VERTS // 3):
        run = tris[start:start + MAX_VERTS // 3]
        used, local = np.unique(run, return_inverse=True)
        out.append((used, local.reshape(-1, 3)))
    return out


@dataclass
class TextureFiles:
    """The RAB names of a scene's textures."""
    prefix: str

    def albedo(self, tex: str) -> str:
        return f'{tex}.dds'

    @property
    def normal(self) -> str:
        return f'{self.prefix}_flat_normal.dds'

    @property
    def rmo(self) -> str:
        return f'{self.prefix}_flat_rmo.dds'


def model_mdb(model, scene, files: TextureFiles) -> bytes:  # noqa: ANN001 - xm_map.Model / Scene
    """The mdb of a scene model: one bone (the object's, at the origin), one object, a mesh per part (split at
    65535 vertices) on a material per texture."""
    names: list[str | None] = [model.name]
    textures = [mdb.Texture(0, files.normal[:-4], files.normal, 0), mdb.Texture(1, files.rmo[:-4], files.rmo, 0)]
    materials: list[mdb.Material] = []
    mat_of: dict[str, int] = {}
    meshes: list[mdb.Mesh] = []
    for part in model.parts:
        if part.tex not in mat_of:
            tex = scene.textures[part.tex]
            ti = len(textures)
            fn = files.albedo(tex.name)
            textures.append(mdb.Texture(ti, fn[:-4], fn, 0))
            names.append(f'{tex.name}_mat')
            mat_of[part.tex] = len(materials)
            materials.append(mdb.Material(len(materials), 0, B3[tex.mode], len(names) - 1, SHADERS[tex.mode],
                                          _params(tex.mode),
                                          [mdb.MatTex(ti, 'albedo', (0, 0, 0, 0, 0)), mdb.MatTex(0, 'normal', (0, 0, 0, 0, 0)),
                                           mdb.MatTex(1, 'param_r_m_xxx_hr', (0, 0, 0, 0, 0))], 3))
        tris = np.asarray(part.tris, np.int64)
        for used, local in _chunks(tris):
            meshes.append(mdb.Mesh(bytes((0, 1, 1, 0)), mat_of[part.tex], 0, VERTEX.itemsize, list(ELEMS), len(meshes),
                                   _vertices(part.pos[used], part.nrm[used], part.uv[used]),
                                   local.astype('<u2').tobytes()))
    ident = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    # the lone bone as the stock map objects have it (ig_transparentFld01: kind 2, depth_delta 1, bounded)
    bone = mdb.Bone(0, -1, -1, -1, 0, 0, 2, 1, 1, 0, 0, list(ident), list(ident), [0.0, 0.0, 0.0, 1.0],
                    [0.0, 0.0, 0.0, 1.0])
    md = mdb.Mdb(MDB_VERSION, names, [bone], [mdb.Object(0, 0, meshes)], materials, textures)
    return mdb.mdb_write(graft_pure.recompute_bounds(md))


# ------------------------------------------------------------------------------------------------ archives

def rab(scene, log=print) -> bytes:  # noqa: ANN001
    """The map's RAB: every texture (HD-TEXTURE + .lod in TEXTURE) and every model (MODEL), CMPL-compressed."""
    files = TextureFiles(scene.xm.prefix)
    out = mdb.Rab(272, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])

    def add_texture(name: str, dds: bytes) -> None:
        hd, lod = texfile.texture_pair(dds)
        stem = name[:-4]
        mdb.insert_member(out, mdb.RabFile(f'{stem}.lod.dds', 0, 0, mdb.cmpl_compress(lod)))
        mdb.insert_member(out, mdb.RabFile(name, 2, 1, mdb.cmpl_compress(hd)))

    add_texture(files.normal, texfile.solid_bc5())
    add_texture(files.rmo, texfile.solid_dxt1(FLAT_RMO, 16))
    # the wire class every map's MAC carries (elec_wire_default.erb) names this texture; no record uses a wire
    mdb.insert_member(out, mdb.RabFile(WIRE_TEXTURE, 0, 0, mdb.cmpl_compress(texfile.solid_dxt1((40, 40, 40), 16))))
    for tex in scene.textures.values():
        add_texture(files.albedo(tex.name), texture_dds(tex.image, tex.mode))
    for model in scene.models.values():
        mdb.insert_member(out, mdb.RabFile(f'{model.name}.mdb', 1, 0, mdb.cmpl_compress(model_mdb(model, scene, files))))
    log(f'{scene.edf_name}.RAB: {len(scene.textures)} textures, {len(scene.models)} models')
    return mdb.rab_write(out)


def mad(scene, donor_mad: bytes) -> bytes:  # noqa: ANN001
    """The map's MAD: a body `<model>.<model>` (model-local triangles) per model that has collision."""
    donor = bigmap.Marc.parse(donor_mad).get('collision.hkt')
    bodies = [hkcms_build.Body(f'{m.name}.{m.name}', m.collision, ground=m.ground)
              for m in scene.models.values() if len(m.collision)]
    return hkcms_build.write_mad(hkcms_build.build_collision(donor, scene.edf_name.lower(), None, 0.0, 0.0, 1.0, bodies))


# ------------------------------------------------------------------------------------------------ MAC
#
# The layout of a mapb with no wires and no route points, as IG_CAVE605 / NW_RINKAI01 ship it (docs/map-format.md):
# header (0x90), defs, records, the 0x78 block {0, 0, 0, 0, wire def name}, the 0x7C block {0, 0, 0, 0, -4, 1}, the
# defs' SGOs, two empty SGOs (0x70 points at them, count 0), the UTF-16 name table (the defs', then the wire's).

MAPB_HDR = 0x90
WIRE_NAME = 'elec_wire_default.elec'
EMPTY_SGO = struct.pack('<4s7I', b'SGO\0', 0x102, 0, 0x20, 0, 0x20, 0, 0x20)
GROUND_TYPE, OBJECT_TYPE = 3, 1


def def_sgo(template: dict, filename: str, ground: bool) -> bytes:
    """A def's SGO: the template ground def's (IG_TEST_BLUEFLD's) with this model's file, draw distance and type."""
    m = dict(template)
    m['filename'] = filename
    m['Disappear'] = int(GROUND_FAR if ground else OBJECT_FAR)
    m['Durability'] = 0
    if not ground:
        m['Type'] = 'ビル'
    return sgo.write_depth_first(0x102, m)


def _align4(b: bytearray) -> None:
    b += bytes(-len(b) % 4)


def mapb(scene, template_sgo: dict) -> bytes:  # noqa: ANN001
    models = list(scene.models.values())
    index = {m.name: k for k, m in enumerate(models)}
    n_def, n_rec = len(models), len(scene.placements)
    def_at = MAPB_HDR
    rec_at = def_at + n_def * bigmap.DEF
    b78 = rec_at + n_rec * bigmap.REC
    b7c = b78 + 20
    b = bytearray(b7c + 24)
    struct.pack_into('<2i', b, b7c + 16, -4, 1)
    sgo_at = []
    for m in models:
        _align4(b)
        sgo_at.append(len(b))
        b += def_sgo(template_sgo, f'{m.name}.mdx', m.ground)
    _align4(b)
    empty_at = len(b)
    b += EMPTY_SGO * 2
    str_at = len(b)
    name_at = []
    for m in models:
        name_at.append(len(b))
        b += f'{m.name}.mdx'.encode('utf-16le') + b'\0\0'
    wire_at = len(b)
    b += WIRE_NAME.encode('utf-16le') + b'\0\0'
    struct.pack_into('<i', b, b78 + 16, wire_at - (b78 + 16))
    for k, m in enumerate(models):
        o = def_at + k * bigmap.DEF
        far = GROUND_FAR if m.ground else OBJECT_FAR
        struct.pack_into('<6i7f2i', b, o, GROUND_TYPE if m.ground else OBJECT_TYPE, sgo_at[k] - (o + 4), 0,
                         name_at[k] - (o + 0xC), -1, 0, 100.0, far, 0.0, 0.0, -1.0, 0.0, -1.0, 0, 0)
    for i, p in enumerate(scene.placements):
        o = rec_at + i * bigmap.REC
        k = index[p.model]
        struct.pack_into('<i3f3fi', b, o, def_at + k * bigmap.DEF - o, *p.pos, 0.0, p.yaw, 0.0, 0)
        b[o + 0x34:o + 0x44] = uuid.uuid5(uuid.NAMESPACE_OID, f'edf6-xmmap:{scene.edf_name}:{i}').bytes
    hdr = [0] * 32
    hdr[0:2] = [struct.unpack('<i', b'MAPB')[0], 2]
    hdr[2:4] = [str_at, (len(b) - str_at) // 2]
    hdr[6:10] = [def_at, n_def, rec_at, n_rec]
    hdr[28:32] = [empty_at, 0, b78, b7c]
    struct.pack_into('<32i', b, 0, *hdr)
    return bytes(b)


MAPO_HDR = 0x60
OBJ_STRUCT = 0x3C


def mapo(scene) -> bytes:  # noqa: ANN001
    """The mapo of mdx records only (IG_TEST_BLUEFLD's layout): per record a 0x3C object struct naming the model's
    files (+04 '<m>.hkt', +0C the zero vec3, +14 '<m>_hk.mdb', +20 '<m>.mdb'), an int pointing at it, an entry
    (+04 that int, +08 count 1) and a named entry (the record index as text)."""
    n = len(scene.placements)
    vec_at = MAPO_HDR
    st_at = vec_at + 12
    int_at = st_at + n * OBJ_STRUCT
    ent_at = int_at + n * 4
    obj_at = ent_at + n * bigmap.ENT
    pool_at = obj_at + n * bigmap.OBJ
    texts = {str(i) for i in range(n)}
    for p in scene.placements:
        texts |= {f'{p.model}.hkt', f'{p.model}_hk.mdb', f'{p.model}.mdb'}
    pool = bytearray()
    at: dict[str, int] = {}
    for t in sorted(texts):
        at[t] = pool_at + len(pool)
        pool += t.encode('utf-16le') + b'\0\0'
    o = bytearray(pool_at) + pool
    for i, p in enumerate(scene.placements):
        s = st_at + i * OBJ_STRUCT
        struct.pack_into('<9i', o, s, 4, at[f'{p.model}.hkt'] - s, 0, vec_at - s, 0, at[f'{p.model}_hk.mdb'] - s, 0, 0,
                         at[f'{p.model}.mdb'] - s)
        struct.pack_into('<i', o, int_at + i * 4, s - (int_at + i * 4))
        e = ent_at + i * bigmap.ENT
        struct.pack_into('<3if3i', o, e, 0, int_at + i * 4 - e, 1, 0.0, 0, 0, 0)
        e = obj_at + i * bigmap.OBJ
        struct.pack_into('<3i', o, e, at[str(i)] - e, at[str(i)] - e, 0)
    hdr = [0] * 24
    hdr[0] = struct.unpack('<i', b'MAPO')[0]
    hdr[2:4] = [ent_at, n]
    hdr[8:14] = [st_at, n, int_at, n, vec_at, 1]
    hdr[18:22] = [pool_at, len(pool) // 2, obj_at, n]
    struct.pack_into('<24i', o, 0, *hdr)
    return bytes(o)


def mac(scene, template_mac: bytes) -> bytes:  # noqa: ANN001
    """The map's MAC: the template's (IG_TEST_BLUEFLD) rmpa, wire class and externals with the scene's mapb / mapo."""
    t = bigmap.Marc.parse(template_mac)
    tb = bigmap.MapB(t.get('map.mapb'))
    k = next(k for k in range(tb.def_n) if tb.def_name(k) == 'ig_transparentfld01.mdx')
    o = tb.def_off + k * bigmap.DEF
    rel, size = struct.unpack_from('<ii', tb.raw, o + 4)
    _, template_sgo = sgo.read(tb.raw[o + 4 + rel:])
    out = bigmap.Marc(t.version, [(n, d) for n, d in t.files if n in ('map.rmpa', 'elec_wire_default.erb', 'externals.sgo')])
    out.files.append(('map.mapb', mapb(scene, template_sgo)))
    out.files.append(('map.mapo', mapo(scene)))
    return out.build()
