"""A source material's per-texel roughness, metalness and occlusion as the game's parameter map wants them (the prep
tools: tools/prep_glb_model.py, tools/glb_to_wt_obj.py, tools/prep_wt_tracked.py; docs/model-quality-1110.md 1).

    roughness_metal(mat) -> (h, w, 3) uint8     R roughness, G metalness (0..255 for 0..1), B 0
    occlusion(mat, floor) -> (h, w) uint8 | None    the occlusion map, its black lifted to OCC_FLOOR
    occlusion_floor(grey) -> uint8              that lift of a grey map (the War Thunder maps')

The game's parameter map (param_r_m_occ_hr) is R roughness, G metalness, B occlusion, all linear 0..1 as bytes: the
deferred combine takes m = t3.x, F0 = S (0.04 + 0.96 m), the diffuse times (1 - m) (docs/edf41-material-shader-
contract.md), and EDF6's own gold coat writes glTF-style roughness / metallic values straight into R / G
(pylib/goldcoat.py). So a glTF material's maps go over as they are:
    metallic-roughness   roughness = G x roughnessFactor, metalness = B x metallicFactor (textures linear)
    specular-glossiness  roughness = 1 - A x glossinessFactor, metalness 0: its diffuse texture is the albedo, and the
                         combine's (1 - m) would darken that diffuse a second time (the Khronos converter's metalness
                         comes with a base colour of its own, recomputed from both colours; no source here has a
                         specular brighter than 0.2: War Thunder's uploads 0.05, the Flak 88's 0.06 / 0.2)
    no texture           the factors, a FLAT x FLAT map
    occlusion            1 + strength (R - 1), then lifted to OCC_FLOOR as the War Thunder maps are
"""
from __future__ import annotations

import io

import numpy as np

OCC_FLOOR = 110     # the occlusion map's black is this (stock body maps: occlusion mean ~198 of 255)
FLAT = 4            # side of a map made from factors only


def _rgba(data: bytes) -> np.ndarray:
    from PIL import Image
    with Image.open(io.BytesIO(data)) as im:
        return np.asarray(im.convert('RGBA'), dtype=np.float64) / 255.0


def _bytes(x: np.ndarray) -> np.ndarray:
    return np.clip(np.rint(x * 255.0), 0, 255).astype(np.uint8)


def roughness_metal(mat) -> np.ndarray:  # noqa: ANN001 - gltf.Material
    if mat.workflow == 'specular':
        gloss = _rgba(mat.spec_gloss)[..., 3] if mat.spec_gloss is not None else np.ones((FLAT, FLAT))
        rough = 1.0 - gloss * mat.glossiness
        metal = np.zeros_like(rough)
    elif mat.metal_rough is not None:
        mr = _rgba(mat.metal_rough)
        rough, metal = mr[..., 1] * mat.roughness, mr[..., 2] * mat.metallic
    else:
        rough, metal = np.full((FLAT, FLAT), mat.roughness), np.full((FLAT, FLAT), mat.metallic)
    out = np.zeros(rough.shape + (3,), dtype=np.uint8)
    out[..., 0], out[..., 1] = _bytes(rough), _bytes(metal)
    return out


def occlusion_floor(grey: np.ndarray) -> np.ndarray:
    """A grey occlusion map (0..255) with its black lifted to OCC_FLOOR (it multiplies the stock shader's lighting)."""
    g = np.asarray(grey, dtype=np.float64)
    return (OCC_FLOOR + g * (255 - OCC_FLOOR) / 255).round().astype(np.uint8)


def occlusion(mat, floor: bool = True) -> np.ndarray | None:  # noqa: ANN001 - gltf.Material
    """The material's occlusion (R, at its strength) as a grey map, lifted to OCC_FLOOR unless `floor` is False (a map
    tools/prep_wt_tracked.py lifts itself); None: it has none."""
    if mat.occlusion is None:
        return None
    grey = 255.0 * (1.0 + mat.occlusion_strength * (_rgba(mat.occlusion)[..., 0] - 1.0))
    return occlusion_floor(grey) if floor else _bytes(grey / 255.0)
