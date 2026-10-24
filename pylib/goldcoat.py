"""Gold coats (金色涂装) in EDF6's own metal-gold material: one pipeline for every gold-painted model.

Why (2026-10-25 user: 「金色一定要有金色」, BM03 维加泰金色涂装 flat plastic yellow, 深渊爬行战车金色涂装 off-white):
gold models authored on the EDF5-era reflect shaders (snd_Mech / snd_Mech_Light / snd_Common_Basic) had their gold
from the old reflection. EDF6's Deferred pixel shaders of that family (DX11.cpk, read with D3DDisassemble) write

    o0 (albedo)    = b + f * (1 - b)          b = albedo texture * diffuse, f = reflect.R * fall-off
    o3.x (metal)   = sqrt(f)
    o3.y (rough)   = max(clamp((1 - spec * specular_pow / 150) ** (2f + 3), 0.1, 1), roughness)

The Depth Crawler gold (EDF6's own Vehicle502_groundrobogold.mdb, material groundrobo: reflect.R about 0.63, fall-off
50 / 2 / 0.5, so f is reflect.R nearly everywhere) gets its gold base pushed most of the way to white: (0.25, 0.13, 0)
becomes about (0.72, 0.68, 0.63); the Balam gold (V515_RETROBALAM_GOLD, reflect.R 0.97) nearly all the way. The BM03
Vegalta gold (EDF4.1's Vehicle406_begarutagold.mdb, Material1) has the whitening removed by the EDF41_ shader alias
(pylib/edf41materials.py), but its specular_pow 10 maps to roughness about 0.75: a blurred, plastic surface, its base
the texture times (1, 0.698, 0), a saturated yellow.

EDF6 paints its own gold coat differently: V504_BEGARUTA_GOLD.MRAB / v504_begaruta_gold.mdb, material v504_begaruta,
snd_BRDF_Common_Basic, a tan coat albedo (panels 123, 93, 57), metallic 0.9, roughness 0.1, specular_color 0.4,
specular_diffuse_blend 0.5 (REFERENCE below; tests/test_goldcoat.py checks the constants against the game). Its
deferred combine (EDF.dll, docs/edf41-material-shader-contract.md) reflects the environment tinted by the albedo
with weight metallic squared: that tinted, glossy reflection is what reads as gold.

So a registered gold coat material (COATS) becomes the EDF6 BRDF shader of the same inputs (LEGACY: the same vertex
inputs and techniques; the occlusion where the old shader read it: the second UV set's own texture for snd_Mech*,
the param map's B for snd_Common_Basic):

  albedo   the coat's texels (gold hue, by gold_weight) re-toned to the reference coat colour, each scaled by its
           luminance over the coat's (its median, but at least the EDF5-era gold paint's PAINT_LUMINANCE: a coat
           painted darker on purpose, the gatling's barrels, stays as much darker): panel lines, rivets and wear keep
           their light and dark, nothing is flat. Other texels (grey parts, decals, lights) keep their colour.
  param    R roughness, G metallic (params 1.0: the values are in the texture): the reference 0.1 / 0.9 on the coat,
           elsewhere what EDF6 computed for that texel before (legacy_metal_rough); B the old light mask (the
           _Light shader) or the old occlusion (snd_Common_Basic's).

The new textures get new names (<old>_goldcoat.dds); old ones leave the archive when no material uses them any more.
A source texture other than the audited one (another mod's) is left alone: the material stays as it was.

Where the models come from (all generated files of their own; no stock file is overwritten):
  - EDF4.1's BM03 gold: pylib/edf41materials.py transform_models (the EDF4.1 objects' install).
  - EDF6's stock gold vehicles (crawler, Balam): pylib/vehicle_optics.py build_model, OBJECT/EDF6VC_OPTIC_<stem>.MRAB.
  - EDF6's stock gold weapons (STOCK_WEAPONS): the same builder, WEAPON/EDF6VC_GOLD_<stem>.RAB, their stock SGOs
    redirected by tools/make_optics.py like the vehicles'.
discover() finds every gold model on a whitening shader in the game; tests/test_goldcoat.py and tools/goldcoat_scan.py
hold COATS to it, so a new one cannot be missed.
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
# The EDF5-era gold paint, (66, 48, 0): the Depth Crawler gold's body and arm cannons' coat median (the BM03's 68, 46,
# 0); tests/test_goldcoat.py measures it. Luminance as luminance() computes it.
PAINT_LUMINANCE = 0.2126 * 66 + 0.7152 * 48

# gold_weight: the coat's hue band (degrees; the reference 32.7, the sources' panels 40-49) and saturation ramp
HUE_FULL = (25.0, 55.0)
HUE_SOFT = 10.0
SAT_RAMP = (0.25, 0.45)
DARK = 8                             # max channel below this: too dark for a hue (DXT1 noise); not coat
COAT_SHARE = 0.15                    # discover(): a material whose albedo is at least this much coat is a gold coat


@dataclass(frozen=True)
class Legacy:
    """What a legacy reflect shader reads, and the EDF6 BRDF shader that takes the same inputs."""
    light: bool           # a light mask (B x A of the reflect texture, times light_color): emissive
    spec_alpha: bool      # spec = reflect G x A (snd_Mech's hspec) rather than G
    occ_in_param: bool    # occlusion in the reflect texture's B (snd_Common_Basic), not its own texture on UV1
    target: str
    param_kind: str


LEGACY = {
    'SND_MECH': Legacy(False, True, False, 'snd_BRDF_Common_SeparateOcc', 'param_r_m_xxx'),
    'SND_MECH_LIGHT': Legacy(True, False, False, 'snd_BRDF_Common_Light_SeparateOcc', 'param_r_m_light'),
    'SND_COMMON_BASIC': Legacy(False, False, True, 'snd_BRDF_Common_Basic', 'param_r_m_occ_hr'),
}
TARGETS = frozenset(x.target for x in LEGACY.values())


@dataclass(frozen=True)
class Coat:
    model: str            # the MDB member
    material: str         # its gold material
    shader: str           # the legacy shader it is authored on (an EDF41_ alias of it is the same)
    albedo: str           # texture slot files and the sha-256 of their decompressed DDS (the audited source)
    albedo_sha256: str
    reflect: str
    reflect_sha256: str
    where: str            # where the model is (discover()'s archive key: 'OBJECT/X.MRAB'; edf41 for EDF4.1's)


_CRAWLER_WEAPON_DF = ('Vehicle502_groundrobogold_weapon_df.dds',
                      'b4e36398639648e5b539e6073cb2199a7319c96430e08a0e4d9042de8ebcb53c')
_CRAWLER_WEAPON_RGB = ('Vehicle502_groundrobogold_weapon_rgb.dds',
                       '43f5dd30ba326f572aa22acd7c0056286e031b1d74d9714411b2904c25677786')
_BALAM_DF = ('retrobalam4.dds', 'd95d1dd77a463b8213dc65610f764e153cc07b5737c9c667e40f3cd00fc2809b')
_BALAM_RGB = ('v515_retrobalam_nm_gold.dds', 'a027daff33a9b1d937f357963e64b106f143476a9f77750ed992ed6ebee3c604')

COATS = (
    # EDF4.1's BM03 Vegalta gold (edf41port VEHICLE406_BEGARUTAGOLD.RAB, its call 「BM03维加泰金色涂装」).
    Coat('Vehicle406_begarutagold.mdb', 'Material1', 'snd_Mech_Light',
         'begaruta_g.dds', 'ee1dcadfe556417b05e266c75e08ea4ba678dd88b0b425064cc10967b048def9',
         'begaruta_rgb_g.dds', '5c1275753e211686275df830d0bf454da09f0fcc0b3511b984efa1b62bce0b9e',
         'edf41:OBJECT/VEHICLE406_BEGARUTAGOLD.RAB'),
    # EDF6's own Depth Crawler gold (the 4.1 call 「深渊爬行战车金色涂装」 and EDF6's DLC one).
    Coat('Vehicle502_groundrobogold.mdb', 'groundrobo', 'snd_Mech',
         'Vehicle502_groundrobogold_df.dds', '2cd17d4f07b7ffe5c73d42574a980e83e1b1b467610fc2eec22b7939023078a5',
         'Vehicle502_groundrobogold_rgb.dds', 'f38e37c6310fced09f6446b29d6cc6ae5bbfd27d5b26c581052cb4fd77523f25',
         'OBJECT/VEHICLE502_GROUNDROBOGOLD.MRAB'),
    # its gold weapons (V_502_GROUNDROBOGOLD_CANNON01_L / _R, _GATLING)
    Coat('Vehicle502_groundrobogold_Cannon_l.mdb', 'groundrobo_weapon', 'snd_Common_Basic', *_CRAWLER_WEAPON_DF,
         *_CRAWLER_WEAPON_RGB, 'WEAPON/VEHICLE502_GROUNDROBOGOLD_CANNON_L.RAB'),
    Coat('Vehicle502_groundrobogold_Cannon_r.mdb', 'groundrobo_weapon', 'snd_Common_Basic', *_CRAWLER_WEAPON_DF,
         *_CRAWLER_WEAPON_RGB, 'WEAPON/VEHICLE502_GROUNDROBOGOLD_CANNON_R.RAB'),
    Coat('Vehicle502_groundrobogold_gatling.mdb', 'groundrobo', 'snd_Common_Basic',
         'Vehicle502_groundrobogold_gatling_df.dds', 'c1f04897cd1f9ee997bbed8e17ab797ca4c9746f6cce5378317d7e26b41e4f7b',
         'Vehicle502_groundrobogold_gatling_rgb.dds', '3330f30d16e0e9a5fc36376ec5a70524fdd39e564771eb166cf5917ffe7b5bda',
         'WEAPON/VEHICLE502_GROUNDROBOGOLD_GATLING.RAB'),
    # EDF6's Balam gold (V515_RETROBALAM_GOLD): two materials on one coat texture (their diffuse tints differ; the
    # coat is the texture's olive gold, re-toned to the reference: both come out EDF6's gold)
    Coat('v515_retrobalam_gold.mdb', 'metal1', 'snd_Mech', *_BALAM_DF, *_BALAM_RGB, 'OBJECT/V515_RETROBALAM_GOLD.MRAB'),
    Coat('v515_retrobalam_gold.mdb', 'metal2', 'snd_Mech', *_BALAM_DF, *_BALAM_RGB, 'OBJECT/V515_RETROBALAM_GOLD.MRAB'),
)

# The stock gold weapon archives built into private copies (pylib/vehicle_optics.py MODELS): (folder, stem, ext) ->
# the sha-256 of their model (the audited geometry, as vehicle_optics checks every stock model it builds).
STOCK_WEAPONS = {
    ('WEAPON', 'VEHICLE502_GROUNDROBOGOLD_CANNON_L', '.RAB'):
        '00f2a4f981c2078d3dd880bd859d665c24d46df4a6b9bd1c2c036c1b6fa7100a',
    ('WEAPON', 'VEHICLE502_GROUNDROBOGOLD_CANNON_R', '.RAB'):
        '8a2451775878adfc9f26ce1fe033423518c034e179c1d88299301c1fa93b7e96',
    ('WEAPON', 'VEHICLE502_GROUNDROBOGOLD_GATLING', '.RAB'):
        'e40a62f395726677eb4d2cdb551c71ed180d252de465487a068fd12079ac0d2a',
}

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

def _colour_blocks(raw, n: int):  # noqa: ANN001, ANN201 - numpy
    """(n, 16, 4) RGBA texels of n BC1 colour blocks (8 bytes each); four=True forces the 4-colour rule (DXT5)."""
    import numpy as np
    blocks = np.frombuffer(raw, dtype=[('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')], count=n)

    def rgb(c):  # noqa: ANN001, ANN202
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)

    c0, c1 = blocks['c0'].astype(np.int32), blocks['c1'].astype(np.int32)
    return c0, c1, rgb(c0), rgb(c1), blocks['i']


def decode_dds(dds: bytes):  # noqa: ANN201 - numpy (h, w, 4) uint8
    """The top mip level of a DXT1 or DXT5 DDS as RGBA8."""
    import numpy as np
    info = texfile.dds_info(dds)
    if info.fourcc not in (b'DXT1', b'DXT5'):
        raise texfile.TextureError(f'gold coat source must be DXT1 / DXT5, not {info.fourcc!r}')
    bw, bh = max(1, (info.width + 3) // 4), max(1, (info.height + 3) // 4)
    n = bw * bh
    rows = np.arange(n)[:, None]
    if info.fourcc == b'DXT1':
        colour = dds[128:128 + 8 * n]
        c0, c1, a, b, idx_bits = _colour_blocks(colour, n)
        four = (c0 > c1)[:, None]
        alpha = np.stack([np.full(n, 255), np.full(n, 255), np.full(n, 255), np.where(four[:, 0], 255, 0)], 1)
        texel_alpha = None
    else:
        raw = np.frombuffer(dds, dtype=np.uint8, count=16 * n, offset=128).reshape(n, 16)
        colour = raw[:, 8:].copy().tobytes()
        c0, c1, a, b, idx_bits = _colour_blocks(colour, n)
        four = np.ones((n, 1), bool)
        a0, a1 = raw[:, 0].astype(np.int32), raw[:, 1].astype(np.int32)
        eight = (a0 > a1)[:, None]
        k = np.arange(1, 7)[None, :]
        inter8 = ((7 - k) * a0[:, None] + k * a1[:, None]) // 7
        k6 = np.arange(1, 5)[None, :]
        inter6 = ((5 - k6) * a0[:, None] + k6 * a1[:, None]) // 5
        table = np.where(eight, np.concatenate([a0[:, None], a1[:, None], inter8], 1),
                         np.concatenate([a0[:, None], a1[:, None], inter6, np.zeros((n, 1), np.int32),
                                         np.full((n, 1), 255)], 1))
        bits = np.zeros(n, np.uint64)
        for j in range(6):
            bits |= raw[:, 2 + j].astype(np.uint64) << np.uint64(8 * j)
        aidx = ((bits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))[None, :]) & np.uint64(7)).astype(np.int64)
        texel_alpha = table[rows, aidx]
        alpha = None
    p2 = np.where(four, (2 * a + b) // 3, (a + b) // 2)
    p3 = np.where(four, (a + 2 * b) // 3, 0)
    palette = np.stack([a, b, p2, p3], 1)
    idx = (idx_bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))[None, :]) & 3
    texel_alpha = alpha[rows, idx] if texel_alpha is None else texel_alpha
    px = np.concatenate([palette[rows, idx], texel_alpha[..., None]], -1)
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:info.height, :info.width].astype(np.uint8)


decode_dxt1 = decode_dds


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
    c = np.asarray(rgb).astype(np.float64)
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def tone(rgb, weight):  # noqa: ANN001, ANN201 - numpy (h, w, 3) uint8, (h, w) -> (h, w, 3) uint8, coat luminance
    """The coat re-toned to GOLD_ALBEDO, each texel by its luminance over the coat's (its median, at least
    PAINT_LUMINANCE): detail kept, a darker paint kept darker."""
    import numpy as np
    y = luminance(rgb)
    coat = weight > 0.9
    if not coat.any():
        raise ValueError('gold coat: no texel in the gold hue band')
    y_coat = max(float(np.median(y[coat])), PAINT_LUMINANCE)
    gold = np.asarray(GOLD_ALBEDO, dtype=np.float64) * (y / y_coat)[..., None]
    out = rgb.astype(np.float64) * (1.0 - weight[..., None]) + np.clip(gold, 0.0, 255.0) * weight[..., None]
    return np.clip(np.rint(out), 0, 255).astype(np.uint8), y_coat


def legacy_metal_rough(reflect, params: dict[str, list[float]], spec_alpha: bool):  # noqa: ANN001, ANN201
    """What EDF6's legacy reflect shaders gave each texel as (metallic, roughness): f = reflect.R times the fall-off at
    a grazing view (saturate(scale + offset): fall-off 50 / 2 / 0.5 reaches it at all but a head-on view), metallic
    sqrt(f), roughness from the old specular power (spec = G, times A for snd_Mech's hspec)."""
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
_MAP_PARAMS = ('specular_pow', 'fall_off_scale', 'fall_off_offset', 'roughness')   # what the maps depend on


def build_maps(coat: Coat, albedo_dds: bytes, reflect_dds: bytes, params: dict[str, list[float]]) -> Maps:
    """The coat's two textures (deterministic; cached by their sources)."""
    legacy = LEGACY[_legacy_name(coat.shader)]
    used = [(k, params[k][0]) for k in _MAP_PARAMS if k in params]
    key = hashlib.sha256(albedo_dds + reflect_dds + repr((used, legacy)).encode()).hexdigest()
    if key in _CACHE:
        return _CACHE[key]
    import numpy as np
    import texfile_accel
    src = decode_dds(albedo_dds)
    h, w = src.shape[:2]
    reflect = _resize_nearest(decode_dds(reflect_dds), h, w)
    weight = gold_weight(src[..., :3])
    albedo, y_coat = tone(src[..., :3], weight)
    metal, rough = legacy_metal_rough(reflect, params, spec_alpha=legacy.spec_alpha)
    metal = metal * (1.0 - weight) + GOLD_METALLIC * weight
    rough = rough * (1.0 - weight) + GOLD_ROUGHNESS * weight
    third = np.zeros((h, w))
    if legacy.light:
        # the old emissive: lightmask (B) x hlightmask (A) x light_color; the new one is albedo x light_color x B,
        # so a lit texel's albedo goes to white (its emission the old colour, its unlit share the coat's)
        third = reflect[..., 2].astype(np.float64) / 255.0 * reflect[..., 3].astype(np.float64) / 255.0
        albedo = np.clip(np.rint(albedo * (1.0 - third[..., None]) + 255.0 * third[..., None]), 0, 255).astype(np.uint8)
    elif legacy.occ_in_param:
        third = reflect[..., 2].astype(np.float64) / 255.0       # the old occlusion, where the new shader reads it
    param = np.clip(np.rint(np.stack([rough, metal, third], -1) * 255.0), 0, 255).astype(np.uint8)

    def dds(rgb) -> bytes:  # noqa: ANN001
        rgba = np.concatenate([rgb, np.full(rgb.shape[:2] + (1,), 255, np.uint8)], -1)   # A 1: hr (rough scale) 1
        return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(np.ascontiguousarray(rgba).tobytes())))

    maps = Maps(dds(albedo), dds(param), y_coat)
    _CACHE[key] = maps
    return maps


# --------------------------------------------------------------------------------------------- the model

def _member(rab: Rab, name: str) -> RabFile | None:
    return next((f for f in rab.files if f.name.lower() == name.lower()), None)


def _lod_name(filename: str) -> str:
    stem, ext = filename.rsplit('.', 1)
    return f'{stem}.lod.{ext}'


def _put_texture(rab: Rab, md: Mdb, filename: str, dds: bytes, unk: int) -> tuple[Mdb, int, str]:
    """Texture `filename` in `rab` (both members) and `md` (an entry): the existing one when it holds these very
    bytes (two coats sharing a texture), else under `filename`, or a name of its own when that is taken by others.
    Returns (model, its texture index, the file name used)."""
    hd, lod = texfile.texture_pair(dds)
    stem, ext = filename.rsplit('.', 1)
    k = 0
    while True:
        name = filename if k == 0 else f'{stem}_{k}.{ext}'
        held = _member(rab, name)
        if held is None:
            break
        if cmpl_decompress(held.stored) == hd:
            index = next((t.index for t in md.textures if t.filename.lower() == name.lower()), None)
            if index is not None:
                return md, index, name
            break
        k += 1
    for member, folder, flag, data in ((name, 'HD-TEXTURE', 1, hd), (_lod_name(name), 'TEXTURE', 0, lod)):
        if folder not in rab.folders:
            rab.folders.append(folder)
        rab.files = [f for f in rab.files if f.name.lower() != member.lower()]
        insert_member(rab, RabFile(member, rab.folders.index(folder), flag, cmpl_compress(data)))
    index = len(md.textures)
    md = replace(md, textures=list(md.textures) + [Texture(index, name.rsplit('.', 1)[0] + '_dds', name, unk)])
    return md, index, name


def _drop_unused(rab: Rab, md: Mdb, candidates: set[str]) -> Mdb:
    """Texture entries of `candidates` (file names) no material uses any more leave the model and the archive; the
    others keep their order, the slots their indices remapped."""
    used = {x.texture for m in md.materials for x in m.textures}
    gone = {t.index for t in md.textures if t.index not in used and t.filename.lower() in candidates}
    if not gone:
        return md
    keep = [t for t in md.textures if t.index not in gone]
    remap = {t.index: i for i, t in enumerate(keep)}
    textures = [replace(t, index=i) for i, t in enumerate(keep)]
    materials = [replace(m, textures=[replace(x, texture=remap[x.texture]) for x in m.textures]) for m in md.materials]
    left = {t.filename.lower() for t in textures}
    dropped = {t.filename.lower() for t in md.textures if t.index in gone} - left
    names = dropped | {_lod_name(n) for n in dropped}
    rab.files = [f for f in rab.files if f.name.lower() not in names]
    return replace(md, textures=textures, materials=materials)


def _param(name: str, value: tuple[float, ...], kind: int) -> MatParam:
    return MatParam(list(value) + [0.0] * (4 - len(value)), (0, 0), name, kind)


def apply(rab: Rab, md: Mdb, model: str) -> tuple[Mdb, list[str]]:
    """Convert the registered gold coats of model member `model` (`md`, read from `rab`) in place: `rab` gets the
    new texture members, the returned model the new materials. Returns (model, notes): a note for each coat left as
    it was (its source texture is not the audited one). A model with none, or already converted, comes back as is."""
    notes: list[str] = []
    replaced: set[str] = set()
    for coat in coats_of(model):
        k = next((i for i, m in enumerate(md.materials) if md.name_of(m.name) == coat.material), None)
        if k is None:
            raise ValueError(f'{model}: gold coat material {coat.material} missing')
        mat = md.materials[k]
        if mat.shader in TARGETS:
            continue                          # converted already
        if _legacy_name(mat.shader) != _legacy_name(coat.shader):
            raise ValueError(f'{model}/{coat.material}: shader {mat.shader}, the gold coat is on {coat.shader}')
        legacy = LEGACY[_legacy_name(coat.shader)]
        slots = {x.kind: x for x in mat.textures}
        albedo_slot = slots['albedo']
        reflect_slot = next(x for x in mat.textures if x.kind.startswith('param_reflect'))
        occ_slot = None if legacy.occ_in_param else next(x for x in mat.textures if x.kind.startswith('param_occ'))
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
            old_a, old_r = md.textures[albedo_slot.texture], md.textures[reflect_slot.texture]
            md, a_idx, _ = _put_texture(rab, md, old_a.filename.rsplit('.', 1)[0] + SUFFIX + '.dds', maps.albedo,
                                        old_a.unk)
            md, r_idx, _ = _put_texture(rab, md, old_r.filename.rsplit('.', 1)[0] + SUFFIX + '.dds', maps.param,
                                        old_r.unk)
            replaced |= {old_a.filename.lower(), old_r.filename.lower()}
            new_params = [_param('diffuse', (1.0, 1.0, 1.0), 0x402),
                          _param('specular_color', SPECULAR_COLOR, 0x402),
                          _param('specular_diffuse_blend', (SPECULAR_DIFFUSE_BLEND,), 0x100)]
            if legacy.light:
                new_params.append(_param('light_color', tuple(params.get('light_color', [0.0, 0.0, 0.0])[:3]), 0x402))
            new_params += [_param('normal_amount', (params.get('normal_amount', [1.0])[0],), 0x100),
                           _param('metallic', (1.0,), 0x100), _param('roughness', (1.0,), 0x100)]
            new_slots = [MatTex(a_idx, 'albedo', albedo_slot.unk), slots['normal'],
                         MatTex(r_idx, legacy.param_kind, reflect_slot.unk)]
            if occ_slot is not None:
                new_slots.append(MatTex(occ_slot.texture, 'param_occ', occ_slot.unk))
            materials = list(md.materials)
            materials[k] = replace(mat, shader=legacy.target, params=new_params, textures=new_slots)
            md = replace(md, materials=materials)
    return _drop_unused(rab, md, replaced), notes


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


# ------------------------------------------------------------------------------------------- discovery

def whitening(shader: str) -> bool:
    """A shader whose EDF6 Deferred pass whitens the base by its reflection (b + f(1-b)): the reviewed families of
    pylib/edf41materials.py (SUPPORTED, by their D3DDisassemble'd instructions) and LEGACY's."""
    import edf41materials
    return _legacy_name(shader) in LEGACY or edf41materials.source_name(shader) is not None


def discover_archive(where: str, data: bytes) -> list[tuple[str, str, str, str, float]]:
    """The gold coats of one model archive: every material on a whitening() shader whose albedo is at least
    COAT_SHARE gold-hued (gold_weight > 0.9). [(where, model, material, shader, coat share)]; a converted one is not
    one. A shader outside LEGACY found here has no converter yet: tests/test_goldcoat.py fails on it."""
    rab = mdb.rab_read(data)
    members = {f.name.lower(): f for f in rab.files}
    out = []
    shares: dict[str, float] = {}
    for f in rab.files:
        if not f.name.lower().endswith('.mdb'):
            continue
        md = mdb.mdb_read(f.data)
        for mat in md.materials:
            if not whitening(mat.shader):
                continue
            slot = next((x for x in mat.textures if x.kind == 'albedo'), None)
            name = md.textures[slot.texture].filename.lower() if slot is not None else ''
            if name not in members:
                continue
            if name not in shares:
                px = decode_dds(members[name].data)[..., :3]
                shares[name] = float((gold_weight(px) > 0.9).mean())
            if shares[name] >= COAT_SHARE:
                out.append((where, f.name, md.name_of(mat.name), mat.shader, shares[name]))
    return out


def discover(game) -> list[tuple[str, str, str, str, float]]:  # noqa: ANN001 - rootcpk.Game
    """Every stock gold model archive (OBJECT / WEAPON, GOLD in its name) with a gold coat on a whitening shader."""
    out = []
    for folder, name in sorted(game.cpk.index):
        if folder.upper() in ('OBJECT', 'WEAPON') and 'GOLD' in name.upper() and name.upper().endswith(('.RAB', '.MRAB')):
            out += discover_archive(f'{folder.upper()}/{name.upper()}', game.read(folder, name))
    return out
