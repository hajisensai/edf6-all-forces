"""X-Morph: Defense's maps read as a scene of static models (docs/xmorph-maps.md): what pylib/map_build.py writes as an
EDF6 map. Read-only over the packs (pylib/xm_pack.py); nothing of the game runs.

    MAPS: {EDF map name: XmMap}            the five ported maps (their X-Morph folder by map_dir(): the newest revision)
    scene(packs, edf_name) -> Scene        models (EDF frame, model-local), their placements, the textures they draw with

What a map is in X-Morph (maps_N/<map>/game/):
  <map>_chunk_<k>a.mesh   the ground of one chunk: `terrain/...` submeshes (up to 3 tiled layers blended per vertex: the
                          BLEND_WEIGHTS element; the layers are the material's texture_units in the map's .material, every
                          third a diffuse) and `cliffs/...` submeshes (a 3-variant cliff atlas on uv0, its blend mask
                          letting the terrain layer of uv1 show at the top, the variant weights in uv2)
  <map>_chunk_<k>b.mesh   what lies on the ground: roads, static decals, the water plane and its foam
  <map>.entities          every placed entity: a blueprint (props/..., units/..., logic/..., effects/...) with its
                          PositionComp, OrientComp (a quaternion) and ScaleComp
  <map>.game              SceneParams: bounds, fog, water colour, sky

How it becomes models:
  ground      one model per chunk `a`: the terrain baked into one top-down colour texture (BAKE_RES m a texel: EDF's map
              shaders have no per-vertex layer blend) and the cliffs drawn with their variant composited over their
              terrain layer; collision = every triangle of it
  overlay     one model per chunk `b` without collision (the ground's carries it): roads and decals alpha-tested, the water
              a translucent plane of the map's water colour
  props       one model per (blueprint, scale, tilt): the blueprint's mesh (RenderableDescComp, or a BuildingDef's base mesh
              and every chunk of its .building topology: the intact building), the skin's materials; collision = the
              game's own collision meshes (`<mesh>_c<n>.mesh`) when it has them (vegetation and small clutter have none:
              walked through, as EDF's own trees)
  units       placed `units/...` entities (a fort's barracks, a parked bus) are static models like props and are listed by
              their pylib/xm_units.py row (Scene.units)
  skipped     logic markers, volumes, effects (fire, steam: no EDF counterpart), the `decal_cube` road paint (projected
              decals: EDF's map shaders cannot project) -- counted in Scene.skipped

Frames: X-Morph is +X forward, +Y left, +Z up (xm_model.to_edf: EDF (x, y, z) = X-Morph (y, z, x), a proper rotation).
A record's rotation in EDF's mapb is degrees about +Y applied as the right-handed R_y (measured on NW_DANTI01's fence
chains: docs/xmorph-maps.md), and X-Morph's yaw about +Z is the same angle; any tilt and every scale is baked into the
model's vertices. The map is moved so the centre of its terrain_limits is EDF's origin.
"""
from __future__ import annotations

import io
import math
import re
from collections import Counter
from dataclasses import dataclass, field

import numpy as np

import ogre_mesh as om
import xm_model
import xm_pack as xp

BAKE_RES = 0.125           # metres a texel of the baked ground
BAKE_MAX = 2048            # texels a side at most
PROP_TEX_MAX = 1024        # a prop texture's longest side after conversion
WATER_ALPHA = 0.55
WATER_CELL = 2.0           # metres: the water plane redrawn as runs of cells this size
# A placement's tilt under SNAP_TILT degrees is dropped and its scales rounded to SNAP_SCALE steps (1 within one
# step): X-Morph sets nearly every prop a hair off level and off scale, which would make each one its own model.
SNAP_TILT = 2.5
MISSING = ''               # prop_meshes' override key for a submesh whose material does not exist
SNAP_SCALE = 0.03


@dataclass(frozen=True)
class XmMap:
    folder: str                       # X-Morph map name (Packs.map_dir)
    prefix: str                       # the EDF names' prefix (models, textures): unique per map
    donor: str                        # the EDF6 map whose weather (MAE / RAB) and navmesh the map borrows
    weathers: tuple[str, ...]         # the donor's weathers it ships
    label: str


MAPS: dict[str, XmMap] = {
    'XM_STORY_JAPAN': XmMap('story_japan', 'xmjp', 'NW_RINKAI01', ('FINE', 'CLOUDY', 'EVENING'), '日本（战役）'),
    'XM_STORY_HAWAII': XmMap('story_hawaii', 'xmhw', 'IG_KAIGAN502', ('FINE', 'CLOUDY', 'SUNSET'), '夏威夷（战役）'),
    'XM_DLC_ICELAND': XmMap('dlc_3_iceland', 'xmic', 'NW_HENDEN_WINTER', ('FINE', 'CLOUDY'), '冰岛（DLC 战役）'),
    'XM_DLC_FRANCE': XmMap('dlc_1_france', 'xmfr', 'NW_EUROPE01', ('FINE', 'CLOUDY', 'EVENING'), '法国（生存）'),
    'XM_DLC_FINLAND': XmMap('dlc_1_finland', 'xmfi', 'NW_HENDEN_WINTER', ('FINE', 'CLOUDY'), '芬兰（生存）'),
}


class XmMapError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise XmMapError(message)


# ------------------------------------------------------------------------------------------ scene

@dataclass
class Tex:
    """A texture of the scene: an RGBA image and how its alpha is used ('opaque', 'clip' alpha-tested, 'blend')."""
    name: str
    image: object                      # PIL RGBA
    mode: str


@dataclass
class Part:
    tex: str                           # Scene.textures key
    pos: np.ndarray                    # (n, 3) EDF frame, model-local
    nrm: np.ndarray                    # (n, 3)
    uv: np.ndarray                     # (n, 2)
    tris: np.ndarray                   # (m, 3) int


@dataclass
class Model:
    name: str                          # EDF model name (lower case): <name>.mdb, its bodies <name>.<name>
    parts: list[Part]
    collision: np.ndarray              # (k, 3, 3) EDF frame, model-local (k = 0: none)
    ground: bool                       # the map's ground (def type 3) rather than an object
    source: str                        # where it came from (for the build log)


@dataclass
class Placement:
    model: str
    pos: tuple[float, float, float]    # EDF world
    yaw: float                         # degrees about +Y


@dataclass
class Scene:
    edf_name: str
    xm: XmMap
    folder: str                        # maps_N/<map>
    offset: np.ndarray                 # EDF = to_edf(X-Morph) - offset
    models: dict[str, Model] = field(default_factory=dict)
    placements: list[Placement] = field(default_factory=list)
    textures: dict[str, Tex] = field(default_factory=dict)
    units: list[tuple[str, Placement]] = field(default_factory=list)    # (xm_units key, where)
    skipped: Counter = field(default_factory=Counter)                   # what was not converted, by reason
    bounds: tuple[np.ndarray, np.ndarray] | None = None                 # EDF world box of the ground


def to_edf(p: np.ndarray) -> np.ndarray:
    return xm_model.to_edf(p)


# ------------------------------------------------------------------------------------------ materials

class Materials:
    """Every material block of the packs' scripts and of the map's own .material, by name, with Ogre's inheritance
    (`material a : b`): a texture alias a block does not set is its parent's."""

    def __init__(self, packs: xp.Packs, map_material: str | None) -> None:
        self.packs = packs
        self.blocks: dict[str, xp.Node] = {}
        self.parent: dict[str, str] = {}
        sources = [p for p in packs.paths('materials/scripts/') if p.endswith('.material')]
        for path in sources + ([map_material] if map_material else []):
            for c in xp.parse(packs.text(path), stray_close=True).all('material'):
                head = (c.get('type') or '').split(':')
                name = head[0].strip()
                if name:
                    self.blocks[name] = c
                    if len(head) > 1:
                        self.parent[name] = head[1].strip()

    def has(self, name: str) -> bool:
        return name in self.blocks

    def chain(self, name: str) -> list[str]:
        out = []
        while name in self.blocks and name not in out:
            out.append(name)
            name = self.parent.get(name, '')
        return out

    def alias(self, name: str, kind: str) -> str | None:
        for n in self.chain(name):
            for v in self.blocks[n].values.get('set_texture_alias', []):
                words = v.split()
                if len(words) >= 2 and words[0] == kind:
                    return words[1]
        return None

    def _pass_value(self, name: str, keys: tuple[str, ...]) -> tuple[str, str] | None:
        """(key, value) of the first of `keys` the material's pass sets (Ogre: a child block that does not set a pass
        attribute keeps its parent's; within one pass the last line wins), None when no block of the chain sets any."""
        for n in self.chain(name):
            block = self.blocks[n]
            for node in [block, *block.find('pass')]:
                found = [(k, node.values[k][-1].strip()) for k in keys if node.values.get(k)]
                if found:
                    return found[-1]
        return None

    def alpha(self, name: str) -> tuple[str, int]:
        """(how the material uses its diffuse's alpha, the alpha-test threshold 0..255) from its pass's
        alpha_rejection / scene_blend / separate_scene_blend along the inheritance chain:
            'clip'    an alpha test (base/default_alpha_test, base/deferred_vegetation: `alpha_rejection
                      greater_equal 128`)
            'blend'   an alpha blend of the colour (`scene_blend alpha_blend`: base/default_alpha_blend,
                      base/roads_alpha_blend; `separate_scene_blend src_alpha one_minus_src_alpha ...`: the static
                      decals)
            'opaque'  neither (base/default and every material inheriting it untouched: electric_box_01,
                      billboard_large_01, whatever alpha their diffuse carries); an additive / modulating blend
                      (`add`, `modulate`, `one one`: glows, effects) does not read the alpha either"""
        rej = self._pass_value(name, ('alpha_rejection',))
        words = rej[1].split() if rej else []
        if words and words[0] != 'always_pass':
            # the texels it rejects are those below the threshold; another compare function (none in the game's
            # scripts) keeps the test whatever the texture holds
            v = int(float(words[1])) if len(words) > 1 else 0
            return 'clip', {'greater_equal': v, 'greater': v + 1}.get(words[0], 256)
        blend = self._pass_value(name, ('scene_blend', 'separate_scene_blend'))
        if blend:
            words = blend[1].split()
            if blend[0] == 'separate_scene_blend':        # colour then alpha: 2 simple words or 4 factors
                words = words[:1] if len(words) == 2 else words[:2]
            if words in (['alpha_blend'], ['src_alpha', 'one_minus_src_alpha']):
                return 'blend', 0
        return 'opaque', 0

    def units(self, name: str) -> list[str]:
        """The `texture_unit { texture <path> }` paths of the block's first pass, in order (terrain / cliff layers)."""
        out = []
        for unit in self.blocks[name].find('texture_unit'):
            t = unit.get('texture')
            if t:
                out.append(t.split()[0])
        return out


class Images:
    """The packs' textures as RGBA arrays, read once."""

    def __init__(self, packs: xp.Packs) -> None:
        self.packs = packs
        self.cache: dict[str, np.ndarray] = {}

    def get(self, path: str) -> np.ndarray:
        if path not in self.cache:
            from PIL import Image
            data = self.packs.read(path)
            im = Image.open(io.BytesIO(data))
            im.load()
            self.cache[path] = np.asarray(im.convert('RGBA'))
        return self.cache[path]

    def exists(self, path: str) -> bool:
        return self.packs.exists(path)


BLEND_CLIP = 128    # the alpha test an alpha-blended map surface (roads, decals) is drawn with (alpha_clip 0.5)


def alpha_mode(material_mode: str, threshold: int, rgba: np.ndarray) -> str:
    """The scene texture's mode from its material's (Materials.alpha): an opaque material is 'opaque' whatever its
    diffuse's alpha holds (electric_box_01's, billboard_large_01's carry one that base/default never reads); an
    alpha test, and an alpha blend (EDF's map shaders draw roads and decals alpha-tested; 'blend' is the water's), is
    'clip'. The pixels only pick the storage: a test that rejects no texel of the texture draws as the opaque
    material does and is stored as one (DXT1, no alpha clip)."""
    if material_mode == 'opaque':
        return 'opaque'
    if material_mode == 'blend':
        threshold = BLEND_CLIP
    return 'clip' if np.any(rgba[..., 3] < threshold) else 'opaque'


def _fit(img, limit: int):  # noqa: ANN001, ANN202 - PIL image
    from PIL import Image
    w, h = img.size
    k = max(w, h) / limit
    if k > 1:
        img = img.resize((max(4, round(w / k)), max(4, round(h / k))), Image.LANCZOS)
    return img


# ------------------------------------------------------------------------------------------ meshes

@dataclass
class Sub:
    """One Ogre submesh in X-Morph's frame."""
    material: str
    pos: np.ndarray
    nrm: np.ndarray
    uv: list[np.ndarray]
    weights: np.ndarray | None
    tris: np.ndarray


def subs(packs: xp.Packs, path: str) -> list[Sub]:
    mesh = om.read_mesh(packs.read(path))
    out = []
    for s in mesh.submeshes:
        g = mesh.geometry(s)
        p = mesh.positions(s)
        n = mesh.normals(s)
        uvs = []
        for k in range(4):
            a = g.attribute(om.SEM_TEXCOORD, k)
            if a is None:
                break
            uvs.append(a.astype(np.float64))
        w = g.attribute(om.SEM_BLEND_WEIGHTS)
        out.append(Sub(s.material, p, n if n is not None else np.tile([0.0, 0.0, 1.0], (len(p), 1)),
                       uvs, None if w is None else w.astype(np.float64), s.indices.astype(np.int64)))
    return out


def _transform(sub: Sub, m: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """(positions, normals, triangles) of `sub` under the 3x3 X-Morph-frame matrix m (scale and tilt), in EDF's frame;
    a mirroring matrix flips the winding so the front faces stay front."""
    p = sub.pos @ m.T
    n = sub.nrm @ np.linalg.inv(m)          # normals by the inverse transpose
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    t = sub.tris if np.linalg.det(m) > 0 else sub.tris[:, ::-1]
    return to_edf(p), to_edf(n), t


# ------------------------------------------------------------------------------------------ the reader

class Reader:
    def __init__(self, packs: xp.Packs, edf_name: str) -> None:
        _req(edf_name in MAPS, f'{edf_name}: not an X-Morph map of MAPS')
        self.packs = packs
        self.xm = MAPS[edf_name]
        self.dir = packs.map_dir(self.xm.folder)
        game = f'{self.dir}/game/{self.xm.folder}'
        self.game = game
        self.mats = Materials(packs, f'{game}.material' if packs.exists(f'{game}.material') else None)
        self.images = Images(packs)
        params = xp.parse(packs.text(f'{game}.game')).child('SceneParams')
        _req(params is not None, f'{game}.game: no SceneParams')
        self.params = params
        lim = params.child('terrain_limits')
        lo, hi = np.array(lim.child('min').vector()), np.array(lim.child('max').vector())
        centre = (lo + hi) / 2
        centre[2] = 0.0
        self.scene = Scene(edf_name, self.xm, self.dir, to_edf(centre))
        self.tex_ids: dict[tuple, str] = {}
        self.model_ids: dict[tuple, str] = {}

    # ---------------------------------------------------------------------------------- textures

    def _tex_name(self) -> str:
        return f'{self.xm.prefix}_t{len(self.scene.textures):04d}'

    def texture(self, key: tuple, make) -> str:  # noqa: ANN001 - () -> (PIL image, mode)
        if key not in self.tex_ids:
            img, mode = make()
            name = self._tex_name()
            self.scene.textures[name] = Tex(name, img, mode)
            self.tex_ids[key] = name
        return self.tex_ids[key]

    def material_tex(self, material: str) -> str | None:
        """The scene texture of an ordinary material (its diffuse), None when it has none."""
        if not self.mats.has(material):
            self.scene.skipped[f'material not found: {material}'] += 1
            return None
        path = self.mats.alias(material, 'diffuse')
        if path is None or not self.images.exists(path):
            self.scene.skipped[f'material without a diffuse: {material}'] += 1
            return None

        material_mode, threshold = self.mats.alpha(material)

        def make():  # noqa: ANN202
            from PIL import Image
            rgba = self.images.get(path)
            return _fit(Image.fromarray(rgba, 'RGBA'), PROP_TEX_MAX), alpha_mode(material_mode, threshold, rgba)
        # one image drawn by an opaque and by an alpha-tested material is two scene textures (their modes differ)
        return self.texture(('mat', path, material_mode, threshold), make)

    def water_tex(self) -> str:
        def make():  # noqa: ANN202
            from PIL import Image
            c = self.params.child('water_color')
            rgb = np.clip(np.array(c.vector() if c is not None else (0.03, 0.075, 0.075)) * 4.0, 0, 1)
            px = np.array([*(rgb * 255), WATER_ALPHA * 255], dtype=np.uint8)
            return Image.fromarray(np.tile(px, (16, 16, 1)), 'RGBA'), 'blend'
        return self.texture(('water',), make)

    # ---------------------------------------------------------------------------------- ground

    def terrain_layers(self, material: str) -> list[str]:
        units = self.mats.units(material)
        return [u for u in units[0::3]]

    def _bake(self, chunk: str, terrain: list[Sub]) -> tuple[str, np.ndarray, np.ndarray]:
        """The chunk's terrain colour texture: every texel the blend of the layers at its ground point (the triangle
        that covers it from above, barycentric weights and tile coordinates). Returns (texture, box min, box max) of
        the X-Morph xy box the texture spans."""
        from PIL import Image
        pts = np.vstack([s.pos[:, :2] for s in terrain])
        lo, hi = pts.min(0), pts.max(0)
        size = []
        for e in hi - lo:
            n = 64
            while n < e / BAKE_RES and n < BAKE_MAX:
                n *= 2
            size.append(n)
        nx, ny = size
        acc = np.zeros((ny, nx, 3))
        wsum = np.zeros((ny, nx))
        for s in terrain:
            layers = self.terrain_layers(s.material)
            _req(bool(layers), f'{s.material}: no terrain layer')
            w = s.weights if s.weights is not None else np.ones((len(s.pos), 1))
            w = w[:, :len(layers)]
            uv = s.uv[0]
            texs = [self.images.get(p)[..., :3].astype(np.float64) for p in layers]
            for t in s.tris:
                a, b, c = s.pos[t, :2]
                x0 = int(max(0, math.floor((min(a[0], b[0], c[0]) - lo[0]) / (hi[0] - lo[0]) * nx)))
                x1 = int(min(nx - 1, math.ceil((max(a[0], b[0], c[0]) - lo[0]) / (hi[0] - lo[0]) * nx)))
                y0 = int(max(0, math.floor((min(a[1], b[1], c[1]) - lo[1]) / (hi[1] - lo[1]) * ny)))
                y1 = int(min(ny - 1, math.ceil((max(a[1], b[1], c[1]) - lo[1]) / (hi[1] - lo[1]) * ny)))
                if x1 < x0 or y1 < y0:
                    continue
                gx = lo[0] + (np.arange(x0, x1 + 1) + 0.5) / nx * (hi[0] - lo[0])
                gy = lo[1] + (np.arange(y0, y1 + 1) + 0.5) / ny * (hi[1] - lo[1])
                X, Y = np.meshgrid(gx, gy)
                d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(d) < 1e-12:
                    continue
                l1 = ((b[1] - c[1]) * (X - c[0]) + (c[0] - b[0]) * (Y - c[1])) / d
                l2 = ((c[1] - a[1]) * (X - c[0]) + (a[0] - c[0]) * (Y - c[1])) / d
                l3 = 1 - l1 - l2
                inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
                if not inside.any():
                    continue
                L = np.stack([l1[inside], l2[inside], l3[inside]], 1)
                tuv = L @ uv[t]
                tw = L @ w[t]
                col = np.zeros((len(L), 3))
                for k, tex in enumerate(texs):
                    if not tw[:, k].any():
                        continue
                    col += tw[:, k:k + 1] * _sample(tex, tuv)
                iy, ix = np.nonzero(inside)
                acc[iy + y0, ix + x0] = col / np.maximum(tw.sum(1, keepdims=True), 1e-6)
                wsum[iy + y0, ix + x0] = 1
        if not wsum.all():      # texels no triangle covers (outside the ground's outline): the nearest covered colour
            from scipy import ndimage
            _d, (iy, ix) = ndimage.distance_transform_edt(wsum == 0, return_indices=True)
            acc = acc[iy, ix]
        img = Image.fromarray(np.clip(acc[::-1] + 0.5, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
        name = self.texture(('bake', chunk), lambda: (img, 'opaque'))
        return name, lo, hi

    def _cliff_tex(self, material: str, variant: int, layer: str | None) -> str:
        """A cliff material's variant (a third of its atlas) with the terrain layer showing where its blend mask is
        dark (the top edge), as one tiling texture."""
        diffuse = self.mats.alias(material, 'cliff_diffuse')
        blend = self.mats.alias(material, 'cliff_blend')
        _req(diffuse is not None, f'{material}: no cliff_diffuse')

        def make():  # noqa: ANN202
            from PIL import Image
            atlas = self.images.get(diffuse)[..., :3].astype(np.float64)
            w = atlas.shape[1] // 3
            cliff = atlas[:, variant * w:(variant + 1) * w]
            if blend is not None and layer is not None and self.images.exists(blend):
                mask = self.images.get(blend)[..., 0].astype(np.float64)[:, variant * w:(variant + 1) * w, None] / 255
                ground = np.asarray(Image.fromarray(self.images.get(layer)[..., :3]).resize((w, cliff.shape[0]),
                                                                                          Image.BILINEAR), dtype=np.float64)
                cliff = cliff * mask + ground * (1 - mask)
            img = Image.fromarray(np.clip(cliff + 0.5, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
            return _fit(img, PROP_TEX_MAX), 'opaque'
        return self.texture(('cliff', diffuse, variant, layer), make)

    def ground(self, chunk_path: str) -> None:
        """Chunk `a`: terrain + cliffs, one ground model placed at the origin."""
        chunk = chunk_path.rsplit('_chunk_', 1)[1][:-len('.mesh')]
        ss = subs(self.packs, chunk_path)
        terrain = [s for s in ss if s.material.startswith('terrain/')]
        parts: list[Part] = []
        coll = []
        if terrain:
            tex, lo, hi = self._bake(chunk, terrain)
            for s in terrain:
                uv = np.stack([(s.pos[:, 0] - lo[0]) / (hi[0] - lo[0]), (hi[1] - s.pos[:, 1]) / (hi[1] - lo[1])], 1)
                parts.append(self._part(tex, s.pos, s.nrm, uv, s.tris))
        for s in ss:
            if s.material.startswith('terrain/'):
                continue
            if s.material.startswith('cliffs/'):
                layers = self.terrain_layers(s.material)
                var = s.uv[2] if len(s.uv) > 2 else np.tile([1.0, 0.0, 0.0], (len(s.pos), 1))
                lw = s.weights if s.weights is not None else np.ones((len(s.pos), 1))
                tv = var[s.tris].sum(1).argmax(1)
                tl = lw[s.tris].sum(1)[:, :max(1, len(layers))].argmax(1)
                for v, l in sorted(set(zip(tv.tolist(), tl.tolist()))):
                    keep = s.tris[(tv == v) & (tl == l)]
                    tex = self._cliff_tex(s.material, v, layers[l] if layers else None)
                    parts.append(self._part(tex, s.pos, s.nrm, s.uv[0], keep))
            else:
                tex = self.material_tex(s.material)
                if tex is None:
                    continue
                parts.append(self._part(tex, s.pos, s.nrm, s.uv[0], s.tris))
        for s in ss:
            coll.append(to_edf(s.pos)[s.tris] - self.scene.offset)
        name = f'{self.xm.prefix}_g{chunk}'
        self.scene.models[name] = Model(name, [p for p in parts if len(p.tris)], np.concatenate(coll) if coll else
                                        np.zeros((0, 3, 3)), True, chunk_path)
        self.scene.placements.append(Placement(name, (0.0, 0.0, 0.0), 0.0))

    def overlay(self, chunk_path: str) -> None:
        """Chunk `b`: roads, decals, water; drawn only."""
        chunk = chunk_path.rsplit('_chunk_', 1)[1][:-len('.mesh')]
        parts = []
        for s in subs(self.packs, chunk_path):
            if s.material == 'base/water':
                pos, tris = water_grid(s.pos, s.tris)
                parts.append(self._part(self.water_tex(), pos, np.tile([0.0, 0.0, 1.0], (len(pos), 1)),
                                        np.zeros((len(pos), 2)), tris))
                continue
            if s.material.startswith('water/'):    # a river (animated water shaders): as is, in the water's colour
                parts.append(self._part(self.water_tex(), s.pos, s.nrm, np.zeros((len(s.pos), 2)), s.tris))
                continue
            if s.material.startswith('base/water'):
                self.scene.skipped['water foam'] += 1
                continue
            tex = self.material_tex(s.material)
            if tex is not None and s.uv:
                parts.append(self._part(tex, s.pos, s.nrm, s.uv[0], s.tris))
        if parts:
            name = f'{self.xm.prefix}_o{chunk}'
            self.scene.models[name] = Model(name, parts, np.zeros((0, 3, 3)), True, chunk_path)
            self.scene.placements.append(Placement(name, (0.0, 0.0, 0.0), 0.0))

    def _part(self, tex: str, pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray) -> Part:
        """A part of a ground / overlay model: X-Morph world points moved into EDF's world (placed at the origin)."""
        used = np.unique(tris)
        remap = np.full(len(pos), -1)
        remap[used] = np.arange(len(used))
        return Part(tex, to_edf(pos[used]) - self.scene.offset, to_edf(nrm[used]), uv[used], remap[tris])

    # ---------------------------------------------------------------------------------- props

    def prop_meshes(self, bp: xp.Node) -> list[tuple[str, dict[str, str]]]:
        """[(mesh path, {submesh material: replacement})] of a composed blueprint, [] when it has no mesh."""
        rend = bp.child('RenderableDescComp')
        if rend is not None and rend.get('mesh'):
            over = {}
            mesh = rend.get('mesh')
            sm = rend.child('SubMeshes')
            if sm is not None:
                names = [s.material for s in om.read_mesh(self.packs.read(mesh)).submeshes]
                for s in sm.all('SubMesh'):
                    i = int(s.get('id', '0'))
                    if i < len(names) and s.get('material'):
                        over[names[i]] = s.get('material')
            return [(mesh, over)]
        beh = bp.child('BehaviourComp')
        defs = beh.child('behaviour_defs') if beh is not None else None
        b = defs.child('BuildingDef') if defs is not None else None
        if b is None or not b.get('mesh_prefix'):
            return []
        skin = b.get('chunk_material') or ''
        plain = re.sub(r'_skin\d+$', '', skin)
        over = {plain: skin} if skin and skin != plain else {}
        if skin:
            # a submesh whose own material is no material (an exporter's leftover such as oil_shaft_01's 'Standard_5')
            # is drawn with the building's chunk_material
            over[MISSING] = skin
        meshes = [b.get('mesh_prefix') + '.mesh']
        topo = b.get('chunk_topology_file_path')
        if topo and self.packs.exists(topo):
            for c in xp.parse(self.packs.text(topo)).find('ChunkDesc'):
                if c.get('graphics_mesh'):
                    meshes.append(c.get('graphics_mesh'))
        return [(m, over) for m in meshes if self.packs.exists(m)]

    def collision_meshes(self, mesh: str) -> list[str]:
        out = []
        k = 1
        while self.packs.exists(f'{mesh[:-len(".mesh")]}_c{k}.mesh'):
            out.append(f'{mesh[:-len(".mesh")]}_c{k}.mesh')
            k += 1
        return out

    def prop_model(self, blueprint: str, bp: xp.Node, m: np.ndarray) -> str | None:
        key = (blueprint, tuple(np.round(m, 3).ravel()))
        if key in self.model_ids:
            return self.model_ids[key]
        meshes = self.prop_meshes(bp)
        if not meshes:
            self.scene.skipped[f'no mesh: {blueprint}'] += 1
            self.model_ids[key] = None
            return None
        parts, coll = [], []
        for mesh, over in meshes:
            for s in subs(self.packs, mesh):
                if not s.uv:
                    continue
                material = over.get(s.material, s.material)
                if not self.mats.has(material) and MISSING in over:
                    material = over[MISSING]
                tex = self.material_tex(material)
                if tex is None:
                    continue
                p, n, t = _transform(s, m)
                parts.append(Part(tex, p, n, s.uv[0], t))
            for c in self.collision_meshes(mesh):
                for s in subs(self.packs, c):
                    p, _n, t = _transform(s, m)
                    coll.append(p[t])
        if not parts:
            self.scene.skipped[f'no drawable submesh: {blueprint}'] += 1
            self.model_ids[key] = None
            return None
        name = f'{self.xm.prefix}_p{len([k for k in self.model_ids.values() if k]):04d}'
        self.scene.models[name] = Model(name, parts, np.concatenate(coll) if coll else np.zeros((0, 3, 3)), False,
                                        blueprint)
        self.model_ids[key] = name
        return name

    def entities(self) -> None:
        import xm_units
        unit_rows = {u.entity: u.key for u in xm_units.UNITS.values()}
        bps = xm_model.blueprints(self.packs)
        for e in xp.parse(self.packs.text(f'{self.game}.entities')).all('Entity'):
            bc = e.child('BlueprintComp')
            if bc is None:
                rend = e.child('RenderableDescComp')
                self.scene.skipped['decal cube (projected decal)' if rend is not None and 'decal_cube' in
                                   (rend.get('mesh') or '') else 'entity without a blueprint'] += 1
                continue
            name = bc.get('blueprint') or ''
            kind = name.split('/')[0]
            if kind not in ('props', 'units'):
                self.scene.skipped[f'{kind} entity'] += 1
                continue
            raw = bps.get(name)
            if raw is None:
                self.scene.skipped[f'blueprint not found: {name}'] += 1
                continue
            bp = xm_model.compose(self.packs, raw)
            pc = e.child('PositionComp')
            pos = np.array(pc.child('position').vector()) if pc is not None and pc.child('position') else np.zeros(3)
            oc = e.child('OrientComp')
            q = (0.0, 0.0, 0.0, 1.0)
            if oc is not None and oc.child('q') is not None:
                qn = oc.child('q')
                q = tuple(float(qn.get(a, '1' if a == 'w' else '0')) for a in 'xyzw')
            sc = e.child('ScaleComp')
            s = np.array(sc.child('scale').vector()) if sc is not None and sc.child('scale') else np.ones(3)
            if sc is not None and sc.child('scale') is not None:
                sn = sc.child('scale')
                s = np.array([float(sn.get(a, '1')) for a in 'xyz'])
            r = om.quat_matrix((q[3], q[0], q[1], q[2]))[:3, :3]
            yaw = math.atan2(r[1, 0], r[0, 0])
            cz, sz = math.cos(-yaw), math.sin(-yaw)
            rest = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]) @ r
            if np.arccos(np.clip((np.trace(rest) - 1) / 2, -1.0, 1.0)) < math.radians(SNAP_TILT):
                rest = np.eye(3)
            s = np.where(np.abs(s - 1) < SNAP_SCALE, 1.0, np.round(s / SNAP_SCALE) * SNAP_SCALE)
            m = rest @ np.diag(s)
            model = self.prop_model(name, bp, m)
            if model is None:
                continue
            at = to_edf(pos) - self.scene.offset
            place = Placement(model, (float(at[0]), float(at[1]), float(at[2])), math.degrees(yaw))
            self.scene.placements.append(place)
            entity = name[len('units/'):] if kind == 'units' else None
            if entity is not None:
                key = unit_rows.get(entity)
                if key is None:
                    self.scene.skipped[f'unit without an xm_units row: {entity}'] += 1
                else:
                    self.scene.units.append((key, place))

    def read(self) -> Scene:
        chunks = sorted(p for p in self.packs.paths(f'{self.dir}/game/') if re.search(r'_chunk_\d+[ab]\.mesh$', p))
        _req(bool(chunks), f'{self.dir}: no chunk meshes')
        for c in chunks:
            if c.endswith('a.mesh'):
                self.ground(c)
            else:
                self.overlay(c)
        self.entities()
        pts = np.vstack([p.pos for m in self.scene.models.values() if m.ground for p in m.parts])
        self.scene.bounds = (pts.min(0), pts.max(0))
        return self.scene


def water_grid(pos: np.ndarray, tris: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """The water plane (X-Morph world, z up) as a few quads: the WATER_CELL cells its vertices and triangle centres
    fall in, merged into runs along x, at its mean height. X-Morph's plane is a dense mesh (about 100k triangles a
    chunk) for its wave shader; a flat translucent plane needs none of that."""
    pts = np.vstack((pos[:, :2], pos[tris].mean(1)[:, :2]))
    lo = np.floor(pts.min(0) / WATER_CELL) * WATER_CELL
    ij = np.floor((pts - lo) / WATER_CELL).astype(np.int64)
    w, h = ij.max(0) + 1
    grid = np.zeros((h, w), bool)
    grid[ij[:, 1], ij[:, 0]] = True
    z = float(pos[:, 2].mean())
    verts, quads = [], []
    for j in range(h):
        row = np.concatenate(([False], grid[j], [False]))
        d = np.diff(row.astype(np.int8))
        for a, b in zip(np.flatnonzero(d == 1), np.flatnonzero(d == -1)):
            x0, x1 = lo[0] + a * WATER_CELL, lo[0] + b * WATER_CELL
            y0, y1 = lo[1] + j * WATER_CELL, lo[1] + (j + 1) * WATER_CELL
            k = len(verts)
            verts += [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
            quads += [(k, k + 1, k + 2), (k, k + 2, k + 3)]
    return np.array(verts, float).reshape(-1, 3), np.array(quads, np.int64).reshape(-1, 3)


def _sample(tex: np.ndarray, uv: np.ndarray) -> np.ndarray:
    """Bilinear, wrapping: (n, 3) colours of tex (h, w, 3) at uv (n, 2) (Ogre's: u right, v down)."""
    h, w = tex.shape[:2]
    x = uv[:, 0] * w - 0.5
    y = uv[:, 1] * h - 0.5
    x0, y0 = np.floor(x), np.floor(y)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    x0 = x0.astype(np.int64) % w
    y0 = y0.astype(np.int64) % h
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    return ((tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx) * (1 - fy) + (tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx) * fy)


def scene(packs: xp.Packs, edf_name: str) -> Scene:
    return Reader(packs, edf_name).read()
