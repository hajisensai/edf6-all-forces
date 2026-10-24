"""Gold coats (金色涂装) in EDF6's own metal-gold material: one pipeline for every gold-painted vehicle model.

Why (2026-10-25 user: 「金色一定要有金色」, BM03 维加泰金色涂装 flat plastic yellow, 深渊爬行战车金色涂装 off-white):
both are authored on the EDF5-era reflect shaders (snd_Mech / snd_Mech_Light), whose gold came from the old
reflection. EDF6's Deferred pixel shaders of that family (DX11.cpk, read with D3DDisassemble) write

    o0 (albedo)    = b + f * (1 - b)          b = albedo texture * diffuse, f = reflect.R * fall-off
    o3.x (metal)   = sqrt(f)
    o3.y (rough)   = max(clamp((1 - spec * specular_pow / 150) ** (2f + 3), 0.1, 1), roughness)

The Depth Crawler gold (EDF6's own Vehicle502_groundrobogold.mdb, material groundrobo: reflect.R about 0.63, fall-off
50 / 2 / 0.5, so f is reflect.R nearly everywhere) gets its gold base pushed most of the way to white: (0.25, 0.13, 0)
becomes about (0.72, 0.68, 0.63). The BM03 Vegalta gold (EDF4.1's Vehicle406_begarutagold.mdb, Material1) has the
whitening removed by the EDF41_ shader alias (pylib/edf41materials.py), but its specular_pow 10 maps to roughness
about 0.75: a blurred, plastic surface, its base the texture times (1, 0.698, 0), a saturated yellow.

EDF6 paints its own gold coat differently: V504_BEGARUTA_GOLD.MRAB / v504_begaruta_gold.mdb, material v504_begaruta,
snd_BRDF_Common_Basic, a tan coat albedo (panels 123, 93, 57), metallic 0.9, roughness 0.1, specular_color 0.4,
specular_diffuse_blend 0.5 (REFERENCE below; tests/test_goldcoat.py checks the constants against the game). Its
deferred combine (EDF.dll, docs/edf41-material-shader-contract.md) reflects the environment tinted by the albedo
with weight metallic squared: that tinted, glossy reflection is what reads as gold.

So a registered gold coat material (COATS) becomes EDF6's own SeparateOcc BRDF shader of the same inputs
(snd_BRDF_Common_SeparateOcc, or ..._Light_SeparateOcc when the old one had a light mask: the same vertex inputs and
techniques, the occlusion still from the second UV set as the old shaders read it):

  albedo   the coat's pixels (gold hue, by gold_weight) re-toned to the reference coat colour, each pixel scaled by
           its luminance over the coat's own median luminance: panel lines, rivets and wear keep their light and dark,
           nothing is flat. Other pixels (grey parts, decals, lights) keep their texture colour.
  param    R roughness, G metallic (params 1.0: the values are in the texture): the reference 0.1 / 0.9 on the coat,
           elsewhere what EDF6 computed for that pixel before (legacy_metal_rough); B the old light mask.
  occ      the old occlusion texture, unchanged.

The two new textures get new names (<old>_goldcoat.dds); the old ones leave the archive when nothing else uses them.
A source texture other than the audited one (another mod's) is left alone: the material stays as it was.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib

import mdb
from mdb import MatParam, MatTex, Mdb, Rab, RabFile, Texture, cmpl_compress, cmpl_decompress, insert_member
import texfile

SUFFIX = '_goldcoat'

# EDF6's gold coat, the reference (Root.cpk OBJECT/V504_BEGARUTA_GOLD.MRAB, v504_begaruta_gold.mdb, material
# v504_begaruta; albedo v504_begaruta_coatingdf.DDS: the coat panels' colour).
REFERENCE = {
    'archive': 'V504_BEGARUTA_GOLD.MRAB', 'model': 'v504_begaruta_gold.mdb', 'material': 'v504_begaruta',
    'shader': 'snd_BRDF_Common_Basic', 'albedo': 'v504_begaruta_coatingdf.DDS',
}
GOLD_ALBEDO = (123, 93, 57)          # sRGB texel of the reference coat's panels
GOLD_METALLIC = 0.9
GOLD_ROUGHNESS = 0.1
SPECULAR_COLOR = (0.4, 0.4, 0.4)
SPECULAR_DIFFUSE_BLEND = 0.5

# gold_weight: the coat's hue band (degrees; the reference 32.7, the sources' panels 40-42) and saturation ramp
HUE_FULL = (25.0, 55.0)
HUE_SOFT = 10.0
SAT_RAMP = (0.25, 0.45)
DARK = 8                             # max channel below this: too dark for a hue (DXT1 noise); not coat

SHADER_BASIC = 'snd_BRDF_Common_SeparateOcc'
SHADER_LIGHT = 'snd_BRDF_Common_Light_SeparateOcc'
# legacy shader -> (has a light mask, reflect-texture semantics: spec = G * A for snd_Mech's hspec, G for _Light)
LEGACY = {'SND_MECH': False, 'SND_MECH_LIGHT': True}


@dataclass(frozen=True)
class Coat:
    model: str            # the MDB member
    material: str         # its gold material
    shader: str           # the legacy shader it is authored on (an EDF41_ alias of it is the same)
    albedo: str           # texture slot files and the sha-256 of their decompressed DDS (the audited source)
    albedo_sha256: str
    reflect: str
    reflect_sha256: str
    where: str            # how the plugin installs the model (for the reader)


COATS = (
    # EDF4.1's BM03 Vegalta gold (edf41port VEHICLE406_BEGARUTAGOLD.RAB, its call 「BM03维加泰金色涂装」): converted by
    # pylib/edf41materials.py transform_models (tools/edf41_objects.py, Edf41MaterialCompatibility).
    Coat('Vehicle406_begarutagold.mdb', 'Material1', 'snd_Mech_Light',
         'begaruta_g.dds', 'ee1dcadfe556417b05e266c75e08ea4ba678dd88b0b425064cc10967b048def9',
         'begaruta_rgb_g.dds', '5c1275753e211686275df830d0bf454da09f0fcc0b3511b984efa1b62bce0b9e',
         'edf41 objects'),
    # EDF6's own Depth Crawler gold (VEHICLE502_GROUNDROBOGOLD.MRAB, the 4.1 call 「深渊爬行战车金色涂装」 and EDF6's):
    # converted when pylib/vehicle_optics.py builds EDF6VC_OPTIC_VEHICLE502_GROUNDROBOGOLD.MRAB.
    Coat('Vehicle502_groundrobogold.mdb', 'groundrobo', 'snd_Mech',
         'Vehicle502_groundrobogold_df.dds', '2cd17d4f07b7ffe5c73d42574a980e83e1b1b467610fc2eec22b7939023078a5',
         'Vehicle502_groundrobogold_rgb.dds', 'f38e37c6310fced09f6446b29d6cc6ae5bbfd27d5b26c581052cb4fd77523f25',
         'stock model, optics'),
)


# Every gold-painted vehicle the plugin's calls bring (a call named 金色 / ゴールド / Gold: edf41port/calls.json,
# edf5port/weapons.json; tests/test_goldcoat.py keeps this whole) -> its model: one of COATS, or None for a model
# EDF6 already paints in its metal-gold BRDF material (REFERENCE itself).
GOLD_VEHICLES = {
    'vehicle406_begaruta_gold.sgo': 'Vehicle406_begarutagold.mdb',
    'vehicle502_groundrobogold.sgo': 'Vehicle502_groundrobogold.mdb',
    'v504_begaruta_gold.sgo': None,
}


def coats_of(model: str) -> list[Coat]:
    return [c for c in COATS if c.model.lower() == model.lower()]


def covers(stem: str) -> bool:
    """Whether the stock model `stem` (an archive stem, its model `stem`.mdb) has a gold coat to convert."""
    return bool(coats_of(stem + '.mdb'))


def _legacy_name(shader: str) -> str:
    name = shader.upper()
    return name[len('EDF41_'):] if name.startswith('EDF41_') else name


# ------------------------------------------------------------------------------------------------- pixels

def decode_dxt1(dds: bytes):  # noqa: ANN201 - numpy (h, w, 4) uint8
    """The top mip level of a DXT1 DDS as RGBA8 (the 4-colour / 3-colour + transparent block rule)."""
    import numpy as np
    info = texfile.dds_info(dds)
    if info.fourcc != b'DXT1':
        raise texfile.TextureError(f'gold coat source must be DXT1, not {info.fourcc!r}')
    bw, bh = max(1, (info.width + 3) // 4), max(1, (info.height + 3) // 4)
    blocks = np.frombuffer(dds, dtype=[('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')], count=bw * bh, offset=128)

    def rgb(c):  # noqa: ANN001, ANN202
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)

    c0, c1 = blocks['c0'].astype(np.int32), blocks['c1'].astype(np.int32)
    a, b = rgb(c0), rgb(c1)
    four = (c0 > c1)[:, None]
    p2 = np.where(four, (2 * a + b) // 3, (a + b) // 2)
    p3 = np.where(four, (a + 2 * b) // 3, 0)
    palette = np.stack([a, b, p2, p3], 1)                                         # (n, 4, 3)
    alpha = np.stack([np.full(len(c0), 255), np.full(len(c0), 255), np.full(len(c0), 255),
                      np.where(four[:, 0], 255, 0)], 1)                           # (n, 4)
    idx = (blocks['i'][:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3   # (n, 16)
    rows = np.arange(len(c0))[:, None]
    px = np.concatenate([palette[rows, idx], alpha[rows, idx][..., None]], -1)    # (n, 16, 4)
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:info.height, :info.width].astype(np.uint8)


def gold_weight(rgb):  # noqa: ANN001, ANN201 - numpy (..., 3) uint8 -> float (...)
    """How much each texel is the gold coat: 1 in the coat's hue band at full saturation, 0 for grey, other hues and
    near-black, ramps between (no hard edge round panel lines)."""
    import numpy as np
    c = rgb.astype(np.float64)
    mx, mn = c.max(-1), c.min(-1)
    span = np.maximum(mx - mn, 1e-6)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    hue = np.where(mx == r, 60.0 * ((g - b) / span % 6.0), np.where(mx == g, 60.0 * ((b - r) / span + 2.0),
                                                                    60.0 * ((r - g) / span + 4.0)))
    lo, hi = HUE_FULL
    w_hue = np.clip(np.minimum((hue - (lo - HUE_SOFT)) / HUE_SOFT, ((hi + HUE_SOFT) - hue) / HUE_SOFT), 0.0, 1.0)
    sat = (mx - mn) / np.maximum(mx, 1e-6)
    w_sat = np.clip((sat - SAT_RAMP[0]) / (SAT_RAMP[1] - SAT_RAMP[0]), 0.0, 1.0)
    return np.where(mx < DARK, 0.0, w_hue * w_sat)


def luminance(rgb):  # noqa: ANN001, ANN201
    import numpy as np
    c = rgb.astype(np.float64)
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def tone(rgb, weight):  # noqa: ANN001, ANN201 - numpy (h, w, 3) uint8, (h, w) -> (h, w, 3) uint8, coat luminance
    """The coat re-toned to GOLD_ALBEDO, each texel by its luminance over the coat's median: detail kept."""
    import numpy as np
    y = luminance(rgb)
    coat = weight > 0.9
    if not coat.any():
        raise ValueError('gold coat: no texel in the gold hue band')
    y_coat = float(np.median(y[coat]))
    gold = np.asarray(GOLD_ALBEDO, dtype=np.float64) * (y / max(y_coat, 1.0))[..., None]
    out = rgb.astype(np.float64) * (1.0 - weight[..., None]) + np.clip(gold, 0.0, 255.0) * weight[..., None]
    return np.clip(np.rint(out), 0, 255).astype(np.uint8), y_coat


def legacy_metal_rough(reflect, params: dict[str, list[float]], spec_alpha: bool):  # noqa: ANN001, ANN201
    """What EDF6's snd_Mech / snd_Mech_Light Deferred shader gave each texel as (metallic, roughness): f = reflect.R
    times the fall-off at a grazing view (saturate(scale + offset): fall-off 50 / 2 / 0.5 reaches it at all but a
    head-on view), metallic sqrt(f), roughness from the old specular power (spec = G, times A for snd_Mech's hspec)."""
    import numpy as np
    c = reflect.astype(np.float64) / 255.0
    fall = float(np.clip(_p(params, 'fall_off_scale', 0.0) + _p(params, 'fall_off_offset', 0.0), 0.0, 1.0))
    f = c[..., 0] * fall
    spec = c[..., 1] * (c[..., 3] if spec_alpha else 1.0)
    base = np.clip(1.0 - spec * _p(params, 'specular_pow', 0.0) / 150.0, 0.0, 1.0)
    rough = np.maximum(np.clip(base ** (2.0 * f + 3.0), 0.1, 1.0), _p(params, 'roughness', 0.0))
    return np.sqrt(f), rough


def _p(params: dict[str, list[float]], name: str, default: float) -> float:
    return params[name][0] if name in params else default


def _resize_nearest(img, height: int, width: int):  # noqa: ANN001, ANN201
    import numpy as np
    if img.shape[:2] == (height, width):
        return img
    ys = np.arange(height) * img.shape[0] // height
    xs = np.arange(width) * img.shape[1] // width
    return img[ys][:, xs]


@dataclass(frozen=True)
class Maps:
    albedo: bytes         # DXT1 DDS, full mip chain
    param: bytes
    coat_luminance: float


_CACHE: dict[str, Maps] = {}


def build_maps(coat: Coat, albedo_dds: bytes, reflect_dds: bytes, params: dict[str, list[float]]) -> Maps:
    """The coat's two textures (deterministic; cached by their sources)."""
    key = hashlib.sha256(albedo_dds + reflect_dds + repr(sorted(params.items())).encode() + coat.shader.encode()).hexdigest()
    if key in _CACHE:
        return _CACHE[key]
    import numpy as np
    import texfile_accel
    light = LEGACY[_legacy_name(coat.shader)]
    src = decode_dxt1(albedo_dds)
    h, w = src.shape[:2]
    reflect = _resize_nearest(decode_dxt1(reflect_dds), h, w)
    weight = gold_weight(src[..., :3])
    albedo, y_coat = tone(src[..., :3], weight)
    metal, rough = legacy_metal_rough(reflect, params, spec_alpha=not light)
    metal = metal * (1.0 - weight) + GOLD_METALLIC * weight
    rough = rough * (1.0 - weight) + GOLD_ROUGHNESS * weight
    lamp = np.zeros((h, w))
    if light:
        # the old emissive: lightmask (B) x hlightmask (A) x light_color; the new one is albedo x light_color x B,
        # so a lit texel's albedo goes to white (its emission the old colour, its unlit share the coat's)
        lamp = reflect[..., 2].astype(np.float64) / 255.0 * reflect[..., 3].astype(np.float64) / 255.0
        albedo = np.clip(np.rint(albedo * (1.0 - lamp[..., None]) + 255.0 * lamp[..., None]), 0, 255).astype(np.uint8)
    param = np.stack([rough, metal, lamp], -1)
    param = np.clip(np.rint(param * 255.0), 0, 255).astype(np.uint8)

    def dds(rgb) -> bytes:  # noqa: ANN001
        rgba = np.concatenate([rgb, np.full(rgb.shape[:2] + (1,), 255, np.uint8)], -1)
        return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(np.ascontiguousarray(rgba).tobytes())))

    maps = Maps(dds(albedo), dds(param), y_coat)
    _CACHE[key] = maps
    return maps


# --------------------------------------------------------------------------------------------- the model

def _member(rab: Rab, name: str) -> RabFile | None:
    return next((f for f in rab.files if f.name.lower() == name.lower()), None)


def _replace_texture(rab: Rab, md: Mdb, index: int, filename: str, dds: bytes, used_elsewhere: bool) -> int:
    """Put texture `filename` (both members) in `rab`; in `md` it takes texture slot `index`'s entry when nothing
    else uses that one (its old members leave the archive), else a new entry. Returns its texture index."""
    old = md.textures[index]
    hd, lod = texfile.texture_pair(dds)
    stem, ext = filename.rsplit('.', 1)
    if not used_elsewhere:
        ostem, oext = old.filename.rsplit('.', 1)
        gone = {old.filename.lower(), f'{ostem}.lod.{oext}'.lower()}
        rab.files = [f for f in rab.files if f.name.lower() not in gone]
    for name, folder, flag, data in ((filename, 'HD-TEXTURE', 1, hd), (f'{stem}.lod.{ext}', 'TEXTURE', 0, lod)):
        if folder not in rab.folders:
            rab.folders.append(folder)
        rab.files = [f for f in rab.files if f.name.lower() != name.lower()]
        insert_member(rab, RabFile(name, rab.folders.index(folder), flag, cmpl_compress(data)))
    entry = Texture(index, stem + '_dds', filename, old.unk)
    if not used_elsewhere:
        md.textures[index] = entry
        return index
    md.textures.append(replace(entry, index=len(md.textures)))
    return len(md.textures) - 1


def _param(name: str, value: tuple[float, ...], kind: int) -> MatParam:
    return MatParam(list(value) + [0.0] * (4 - len(value)), (0, 0), name, kind)


def apply(rab: Rab, md: Mdb, model: str) -> tuple[Mdb, list[str]]:
    """Convert the registered gold coats of model member `model` (`md`, read from `rab`) in place: `rab` gets the
    new texture members, the returned model the new material. Returns (model, notes): a note for each coat left as
    it was (its source texture is not the audited one). A model with none, or already converted, comes back as is."""
    notes: list[str] = []
    for coat in coats_of(model):
        k = next((i for i, m in enumerate(md.materials) if md.name_of(m.name) == coat.material), None)
        if k is None:
            raise ValueError(f'{model}: gold coat material {coat.material} missing')
        mat = md.materials[k]
        if _legacy_name(mat.shader) != _legacy_name(coat.shader):
            if mat.shader in (SHADER_BASIC, SHADER_LIGHT):
                continue                      # converted already
            raise ValueError(f'{model}/{coat.material}: shader {mat.shader}, the gold coat is on {coat.shader}')
        slots = {x.kind: x for x in mat.textures}
        albedo_slot = slots['albedo']
        reflect_slot = next(x for x in mat.textures if x.kind.startswith('param_reflect'))
        occ_slot = next(x for x in mat.textures if x.kind.startswith('param_occ'))
        sources = {}
        for slot, name, digest in ((albedo_slot, coat.albedo, coat.albedo_sha256),
                                   (reflect_slot, coat.reflect, coat.reflect_sha256)):
            filename = md.textures[slot.texture].filename
            member = _member(rab, filename)
            data = cmpl_decompress(member.stored) if member is not None else b''
            if filename.lower() != name.lower() or hashlib.sha256(data).hexdigest() != digest:
                notes.append(f'{model}/{coat.material}: {filename} is not the audited gold coat source '
                             '(another mod\'s texture?); left as it was')
                break
            sources[name] = data
        else:
            params = {p.name: p.value for p in mat.params}
            maps = build_maps(coat, sources[coat.albedo], sources[coat.reflect], params)
            light = LEGACY[_legacy_name(coat.shader)]
            md = replace(md, textures=list(md.textures), materials=list(md.materials))
            users = [x.texture for i, m in enumerate(md.materials) if i != k for x in m.textures]
            a_stem = coat.albedo.rsplit('.', 1)[0]
            r_stem = coat.reflect.rsplit('.', 1)[0]
            a_idx = _replace_texture(rab, md, albedo_slot.texture, a_stem + SUFFIX + '.dds', maps.albedo,
                                     albedo_slot.texture in users)
            r_idx = _replace_texture(rab, md, reflect_slot.texture, r_stem + SUFFIX + '.dds', maps.param,
                                     reflect_slot.texture in users)
            normal_amount = params.get('normal_amount', [1.0])[0]
            new_params = [_param('diffuse', (1.0, 1.0, 1.0), 0x402),
                          _param('specular_color', SPECULAR_COLOR, 0x402),
                          _param('specular_diffuse_blend', (SPECULAR_DIFFUSE_BLEND,), 0x100)]
            if light:
                new_params.append(_param('light_color', tuple(params.get('light_color', [0.0, 0.0, 0.0])[:3]), 0x402))
            new_params += [_param('normal_amount', (normal_amount,), 0x100),
                           _param('metallic', (1.0,), 0x100), _param('roughness', (1.0,), 0x100)]
            new_slots = [MatTex(a_idx, 'albedo', albedo_slot.unk), slots['normal'],
                         MatTex(r_idx, 'param_r_m_light' if light else 'param_r_m_xxx', reflect_slot.unk),
                         MatTex(occ_slot.texture, 'param_occ', occ_slot.unk)]
            md.materials[k] = replace(mat, shader=SHADER_LIGHT if light else SHADER_BASIC, params=new_params,
                                      textures=new_slots)
    return md, notes


def convert_archive(data: bytes) -> tuple[bytes, list[str]]:
    """A model archive (RAB / MRAB bytes) with its gold coats converted; the same bytes when it has none."""
    rab = mdb.rab_read(data)
    changed = False
    notes: list[str] = []
    for member in list(rab.files):
        if not member.name.lower().endswith('.mdb') or not coats_of(member.name):
            continue
        raw = member.data
        md, why = apply(rab, mdb.mdb_read(raw), member.name)
        notes += why
        out = mdb.mdb_write(md)
        if out != raw:
            changed = True
            target = _member(rab, member.name)
            target.stored = cmpl_compress(out) if member.stored[:4] == b'CMPL' else out
    return (mdb.rab_write(rab) if changed else data), notes
