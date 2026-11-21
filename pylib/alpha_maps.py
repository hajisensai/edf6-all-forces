"""See-through materials of the external models (docs/model-quality-1121-alpha.md): what a source material's opacity
is, as model.json's 'alpha' (pylib/wt_tracked.py builds it on the stock Clip / Alpha host materials).

    model.json material 'alpha' (left out: opaque, as before):
        'mode'     'mask'   cut out: the stock Clip shaders discard where albedo A < cutoff (wt_tracked.CLIP_HOST;
                            a track keeps the native track material, whose alpha_clip does the same)
                   'blend'  translucent: the stock snd_BRDF_Common_Alpha (alpha = albedo A x diffuse.w; its material
                            header b3 = 2 files it in the translucent draw list, out of the opaque / shadow layers)
                   'opaque' the same as leaving 'alpha' out
        'cutoff'   mask: the alpha_clip, 0..1 (0.5 unless the source says: glTF alphaCutoff)
        'factor'   blend: diffuse.w, 0..1 (the OBJ's `d`, glTF baseColorFactor's A)
        'map'      a grey map (folder-relative) whose value is the albedo's A (the OBJ's map_d); left out: the albedo
                   file's own alpha channel (a glTF base colour PNG)

    classify(alpha, factor=1.0, declared=None, cutoff=None) -> dict | None   the 'alpha' entry (no 'map'), None: opaque
    entry(spec) -> dict                                                       a model.json 'alpha' checked, defaults in

War Thunder's exports (OBJ + MTL) give `map_d` (a grey map) on every see-through material and `d 0.15` on the glass
only; Sketchfab's glTF of the same models says BLEND for all of them. Which is a cut-out is read off the map itself:
a map that is (nearly) only black and white is a cut-out (nets, tracks' gaps, ammunition belts: 0-14 % of their
texels between), one with a range of greys a translucent surface (glass: 66-100 %).
"""
from __future__ import annotations

import numpy as np

OPAQUE, MASK, BLEND = 'opaque', 'mask', 'blend'
MODES = (OPAQUE, MASK, BLEND)
CUTOFF = 0.5            # glTF's default alphaCutoff, and the threshold of a cut-out read off its map
SOLID = 240             # A at or over this: a texel that is not see-through
CLEAR = 16              # A under this: a texel that is not there
SOLID_SHARE = 0.999     # a map with at least this share of SOLID texels changes nothing: opaque
GREY_SHARE = 0.25       # a map with more than this share of texels between CLEAR and SOLID: blended, else a cut-out


class AlphaError(ValueError):
    pass


def classify(alpha: np.ndarray | None, factor: float = 1.0, declared: str | None = None,
             cutoff: float | None = None) -> dict | None:
    """The 'alpha' entry of a material whose albedo's A is `alpha` (uint8, any shape; None: no alpha channel), whose
    constant opacity is `factor` (OBJ `d`, glTF baseColorFactor A) and whose source declares `declared` (glTF
    alphaMode: 'OPAQUE', 'MASK', 'BLEND'; None: an OBJ, which declares nothing). None: opaque."""
    if declared is not None and declared.upper() not in ('OPAQUE', 'MASK', 'BLEND'):
        raise AlphaError(f'unknown alphaMode {declared!r}')
    declared = declared.upper() if declared else None
    if declared == 'OPAQUE':
        return None             # glTF: the alpha is ignored
    if not 0.0 <= factor <= 1.0:
        raise AlphaError(f'opacity {factor} outside 0..1')
    if declared == 'MASK':
        c = CUTOFF if cutoff is None else float(cutoff)
        if not 0.0 <= c <= 1.0:
            raise AlphaError(f'alphaCutoff {c} outside 0..1')
        return {'mode': MASK, 'cutoff': c}
    a = None if alpha is None else np.asarray(alpha, dtype=np.uint8).ravel()
    if factor < 1.0:
        return {'mode': BLEND, 'factor': float(factor)}
    if a is None or a.size == 0 or (a >= SOLID).mean() >= SOLID_SHARE:
        return None
    grey = ((a >= CLEAR) & (a < SOLID)).mean()
    if grey > GREY_SHARE:
        return {'mode': BLEND, 'factor': 1.0}
    return {'mode': MASK, 'cutoff': CUTOFF}


def texture_alpha(image: bytes | None) -> np.ndarray | None:
    """The A channel of an image file's bytes (uint8), None when it has none (a JPEG, an RGB PNG) or there is none."""
    if image is None:
        return None
    import io
    from PIL import Image
    with Image.open(io.BytesIO(image)) as im:
        if im.mode not in ('RGBA', 'LA', 'PA') and not (im.mode == 'P' and 'transparency' in im.info):
            return None
        return np.asarray(im.convert('RGBA'))[..., 3].copy()


def of_gltf(material) -> dict | None:  # noqa: ANN001 - gltf.Material
    """A glTF material's 'alpha' entry: its alphaMode, alphaCutoff, base colour factor's A and base colour texture's A
    (classify: Sketchfab's BLEND on a black-and-white map is a cut-out)."""
    return classify(texture_alpha(material.texture), float(material.base[3]), material.alpha_mode,
                    material.alpha_cutoff)


def entry(spec: dict | None) -> dict:
    """A material's model.json 'alpha' checked, with its defaults: {'mode', 'cutoff', 'factor', 'map'} (map None:
    the albedo's own A). None / left out: opaque."""
    if spec is None:
        return {'mode': OPAQUE, 'cutoff': 0.0, 'factor': 1.0, 'map': None}
    if not isinstance(spec, dict) or spec.get('mode') not in MODES:
        raise AlphaError(f"alpha {spec!r}: 'mode' must be one of {', '.join(MODES)}")
    unknown = set(spec) - {'mode', 'cutoff', 'factor', 'map'}
    if unknown:
        raise AlphaError(f'alpha: unknown keys {sorted(unknown)}')
    cutoff = float(spec.get('cutoff', CUTOFF if spec['mode'] == MASK else 0.0))
    factor = float(spec.get('factor', 1.0))
    if not (0.0 <= cutoff <= 1.0 and 0.0 <= factor <= 1.0):
        raise AlphaError(f'alpha {spec!r}: cutoff and factor are 0..1')
    if spec['mode'] != MASK and 'cutoff' in spec:
        raise AlphaError(f"alpha {spec!r}: a cutoff only means something for 'mask'")
    if spec['mode'] != BLEND and 'factor' in spec:
        raise AlphaError(f"alpha {spec!r}: a factor only means something for 'blend'")
    if spec['mode'] == OPAQUE and 'map' in spec:
        raise AlphaError(f'alpha {spec!r}: an opaque material has no alpha map')
    return {'mode': spec['mode'], 'cutoff': cutoff, 'factor': factor, 'map': spec.get('map')}
