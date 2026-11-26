"""Developer-only: the salvo interceptor's model (天顶截击机, docs/feature-support-jet-models.md §6.6) out of the Sketchfab
"SPACESHIP PAK DA (K) TU-1600 ZVEZDA" (uid 30e58a90fe874f21afdfaa5c4022b311, CC BY 4.0: SOURCE.txt) into the model
folder the installer builds it from (pylib/salvo_model.py):

    <models>/salvo_tu1600/tu1600.obj / .mtl       the ship in game space and metres (+Y up, +Z the nose, +X its left),
                                                  standing on its lowest point, centred on x and z, SPAN m across;
                                                  one OBJ object (`tu1600`, the hull: one rigid bone), one OBJ material
                                                  per source material (MATERIALS)
    <models>/salvo_tu1600/<material>_df.png       its colour (the source's base colour, at most TEX px)
    <models>/salvo_tu1600/<material>_nm.png       its normal map (the source's, DirectX green as measured, at most TEX)
    <models>/salvo_tu1600/<material>_rml.png      R roughness, G metal, B 0 (the source's metallicRoughness: pbr_maps)
    <models>/salvo_tu1600/tu1600.json             its bone, materials, nozzles (the two engine clusters' exits), size
    <models>/salvo_tu1600/SOURCE.txt              where it came from, its licence and what was changed

    python tools/prep_tu1600.py <the download's scene.gltf> [<models folder>]

The .gltf (+ scene.bin, textures/) is the model's Sketchfab download (glTF), read as data by pylib/gltf.py: nothing in it
runs. Keep the download out of the model folder (D:/APP/edf6-models/downloads/salvo_tu1600): build_release copies the
model folder whole.

The source's frame as gltf.read gives it (its root node's turn applied): +Y up, the nose at -x. Game space is
(x, y, z)_game = (z, y, -x)_source, a turn about y (windings kept); then scaled to SPAN m across the wings (the source's
28.6 units: a 40 m ship, 62 m long, as wide as the C-130 the other support calls fly), centred on its box in x and z,
standing on its lowest point.

What is changed:
- the Decals material (62 alpha-tested quads: insignia) is left out: the hull material the installer gives every
  material (c130_model.BODY_HOST, opaque) would draw their transparent corners;
- the 177 534 triangles are collapsed to about TARGET_TRIANGLES (pylib/mesh_simplify.py: no new vertices, the UV seams
  and outlines kept, never past MAX_ERROR m), most of them in the six engine bells' rings;
- the 4096 px maps are halved to TEX (the stock aircraft's size): seven materials at 4096 would be 7 x 3 x 22 MB of
  video memory for one support aircraft.
The engines: six bells, three per side in a triangle (BELLS, measured on the source: each bell's rim at z -19.68, its
box 1.65 units across). The plugin draws at most four flames (jet_models.MAX_NOZZLES): one per side, at the middle of
its three bells' rims, as wide as the three (NOZZLE_WIDTH_OF_CLUSTER x the cluster's span), FLAME_PER_WIDTH of that long.
There is no launcher on it: the salvo's missiles leave from the store hardpoint (HP_STORE) make_jets puts on its belly.
"""
from __future__ import annotations

import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))

DEV_MODELS = r'D:\APP\edf6-models'
OUT_SUBDIR = 'salvo_tu1600'
MODEL = 'tu1600'
MANIFEST = 'tu1600.json'
SOURCE_URL = 'https://sketchfab.com/3d-models/spaceship-pak-da-k-tu-1600-zvezda-30e58a90fe874f21afdfaa5c4022b311'
UID = '30e58a90fe874f21afdfaa5c4022b311'
TITLE = 'SPACESHIP PAK DA (K) TU-1600 ZVEZDA "ПАК ДА (К)"'
SOURCE_TRIANGLES = 177534
SOURCE_MATERIALS = ('Decals', 'UV_2', 'UV_3', 'UV_4', 'UV_5', 'UV_6', 'UV_7', 'UV_glass')
LEFT_OUT = ('Decals',)
MATERIALS = tuple(m for m in SOURCE_MATERIALS if m not in LEFT_OUT)
SPAN = 40.0                 # m across the wings
TARGET_TRIANGLES = 72000
MAX_ERROR = 0.03            # m (game scale): a collapse never moves a surface further
TEX = 2048
NORMAL_GREEN_MIN = 0.1      # texfile_accel.green_down_score of every map with relief: DirectX (+G down), measured 0.17 .. 0.65
# The engine bells in the source's game-turned frame (before scaling): (x, y) of each bell's box centre, the rims at
# BELL_Z; mirrored pairs (+x the left cluster).
BELLS = {'left': ((4.745, 3.275), (3.305, 4.26), (3.025, 2.555)),
         'right': ((-4.745, 3.275), (-3.305, 4.26), (-3.025, 2.555))}
BELL_Z = -19.68
BELL_FIND = 0.9             # a bell's rim vertices: within this (source units) of its centre in x / y, at BELL_Z
NOZZLE_WIDTH_OF_CLUSTER = 1.0
FLAME_PER_WIDTH = 5.0       # jet_models.FLAME_LENGTH_PER_DIAMETER


class PrepError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise PrepError(msg)


def turned(p):  # noqa: ANN001, ANN201 - numpy (n, 3)
    """Source (as gltf.read gives it) -> game axes: (z, y, -x)."""
    import numpy as np
    return np.stack([p[:, 2], p[:, 1], -p[:, 0]], 1)


def bells(G, T, material, uv6: int) -> dict[str, list[tuple[float, float, float, float]]]:  # noqa: ANN001
    """{cluster: [(x, y, z, diameter) of each bell's rim]} measured on the turned source (before scaling): the UV_6
    vertices at the rim's z within BELL_FIND of each BELLS centre."""
    import numpy as np
    rows = np.unique(T[material == uv6].ravel())
    q = G[rows]
    rim = q[np.abs(q[:, 2] - BELL_Z) < 0.02]
    out: dict[str, list[tuple[float, float, float, float]]] = {}
    for side, centres in BELLS.items():
        got = []
        for cx, cy in centres:
            r = rim[(np.abs(rim[:, 0] - cx) < BELL_FIND) & (np.abs(rim[:, 1] - cy) < BELL_FIND)]
            _req(len(r) >= 16, f'bell at ({cx}, {cy}): {len(r)} rim vertices: not the TU-1600 this was measured on')
            lo, hi = r.min(0), r.max(0)
            c = (lo + hi) / 2.0
            _req(abs(c[0] - cx) < 0.05 and abs(c[1] - cy) < 0.05, f'bell at ({cx}, {cy}): rim centred at {c}')
            got.append((float(c[0]), float(c[1]), float(BELL_Z), float(max(hi[0] - lo[0], hi[1] - lo[1]))))
        out[side] = got
    return out


def build(path: str) -> dict:
    """{'positions', 'normals', 'uvs' (rows, game space), 'tris' {material: (m, 3) rows}, 'nozzles', 'scale', ...}."""
    import numpy as np
    import gltf
    import mesh_simplify
    s = gltf.read(path)
    names = [m.name for m in s.materials]
    _req(len(s.triangles) == SOURCE_TRIANGLES and tuple(names) == SOURCE_MATERIALS and s.uvs is not None and
         s.normals is not None, f'{path}: {len(s.triangles)} triangles, materials {names}: not the TU-1600 this was made for')
    G = turned(s.positions)
    N = turned(s.normals)
    lo, hi = G.min(0), G.max(0)
    scale = SPAN / float(hi[0] - lo[0])
    shift = np.array([-(lo[0] + hi[0]) / 2.0, -lo[1], -(lo[2] + hi[2]) / 2.0])
    measured = bells(G, s.triangles, s.material, names.index('UV_6'))
    P = (G + shift) * scale
    keep_tri = ~np.isin(s.material, [names.index(m) for m in LEFT_OUT])
    T = s.triangles[keep_tri]
    mat = s.material[keep_tri]
    group = np.zeros(len(P), dtype=np.int64)
    for k in range(3):
        group[T[:, k]] = mat
    _u, att = np.unique(np.round(s.uvs * 65536.0).astype(np.int64), axis=0, return_inverse=True)
    stats: dict = {}
    kept = mesh_simplify.simplify(P, T, group, TARGET_TRIANGLES, MAX_ERROR, N, att.reshape(-1), stats)
    kept_mat = group[kept[:, 0]]
    _req(bool((group[kept] == kept_mat[:, None]).all()), 'a collapse joined two materials')
    tris = {m: kept[kept_mat == names.index(m)] for m in MATERIALS}
    _req(all(len(t) for t in tris.values()), f'a material collapsed away: { {m: len(t) for m, t in tris.items()} }')
    nozzles = {}
    for side, got in measured.items():
        g = np.array(got)
        c = (g[:, :3].min(0) + g[:, :3].max(0)) / 2.0
        span = float(max(g[:, 0].max() - g[:, 0].min(), g[:, 1].max() - g[:, 1].min()) + g[:, 3].max())
        width = NOZZLE_WIDTH_OF_CLUSTER * span * scale
        nozzles[f'nozzle_{side}'] = {'at': [round(float(v), 5) for v in (c + shift) * scale], 'width': round(width, 4),
                                     'length': round(FLAME_PER_WIDTH * width, 4),
                                     'bells': [[round(float(v), 4) for v in (np.array(b[:3]) + shift) * scale] +
                                               [round(b[3] * scale, 4)] for b in got]}
    return {'positions': P, 'normals': N, 'uvs': s.uvs, 'tris': tris, 'nozzles': nozzles, 'scale': scale,
            'materials': {m.name: m for m in s.materials}, 'extras': s.extras, 'rounds': stats.get('rounds')}


def _image(data: bytes, mode: str):  # noqa: ANN202 - PIL image at most TEX
    from PIL import Image
    im = Image.open(io.BytesIO(data)).convert(mode)
    if max(im.size) > TEX:
        f = TEX / max(im.size)
        im = im.resize((max(1, round(im.width * f)), max(1, round(im.height * f))), Image.LANCZOS)
    return im


def write_textures(folder: str, made: dict) -> dict[str, dict]:
    """Each kept material's three maps; {material: {map: size, 'normal_green': score}}."""
    from PIL import Image
    import numpy as np
    import pbr_maps
    import texfile_accel
    out = {}
    for name in MATERIALS:
        m = made['materials'][name]
        _req(m.texture is not None and m.normal is not None and m.metal_rough is not None and m.alpha_mode == 'OPAQUE',
             f'{name}: not an opaque material with colour, normal and metallicRoughness maps')
        df = _image(m.texture, 'RGB')
        df.save(os.path.join(folder, f'{name.lower()}_df.png'))
        nm = _image(m.normal, 'RGB')
        rgb = np.asarray(nm)
        score = texfile_accel.green_down_score(rgb)
        relief = float(rgb[..., :2].std()) > 2.0
        _req(not relief or score >= NORMAL_GREEN_MIN, f'{name}: normal map green score {score:+.3f}: not DirectX')
        nm.save(os.path.join(folder, f'{name.lower()}_nm.png'))
        rml = Image.fromarray(pbr_maps.roughness_metal(m), 'RGB')
        if max(rml.size) > TEX:
            rml = rml.resize((TEX, TEX), Image.LANCZOS)
        rml.save(os.path.join(folder, f'{name.lower()}_rml.png'))
        out[name.lower()] = {'size': list(df.size), 'normal_green': round(score, 3)}
    return out


def write(folder: str, made: dict) -> dict:
    """tu1600.obj / .mtl, the textures, the manifest entry."""
    import numpy as np
    P, N, UV = made['positions'], made['normals'], made['uvs']
    used = np.unique(np.concatenate([t.ravel() for t in made['tris'].values()]))
    index = np.full(len(P), -1, dtype=np.int64)
    index[used] = np.arange(1, len(used) + 1)
    lines = [f'# {MODEL}: tools/prep_tu1600.py from the Sketchfab "{TITLE}" ({SOURCE_URL}, CC BY 4.0)',
             f'mtllib {MODEL}.mtl', f'o {MODEL}']
    lines += [f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}' for p in P[used]]
    lines += [f'vt {uv[0]:.6f} {1.0 - uv[1]:.6f}' for uv in UV[used]]
    nn = N[used] / (np.linalg.norm(N[used], axis=1, keepdims=True) + 1e-12)
    lines += [f'vn {q[0]:.5f} {q[1]:.5f} {q[2]:.5f}' for q in nn]
    for name, tris in made['tris'].items():
        lines.append(f'usemtl {name.lower()}')
        for t in index[tris]:
            lines.append('f ' + ' '.join(f'{i}/{i}/{i}' for i in t))
    with open(os.path.join(folder, f'{MODEL}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    with open(os.path.join(folder, f'{MODEL}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(''.join(f'newmtl {m.lower()}\nmap_Kd {m.lower()}_df.png\n' for m in MATERIALS))
    maps = write_textures(folder, made)
    pts = P[used]
    return {'bones': {MODEL: {'parent': None, 'pivot': [0.0, 0.0, 0.0]}},
            'materials': [m.lower() for m in MATERIALS], 'maps': maps,
            'nozzles': {k: {'parent': MODEL, **v} for k, v in made['nozzles'].items()},
            'size': [round(float(v), 2) for v in np.ptp(pts, axis=0)],
            'polygons': int(sum(len(t) for t in made['tris'].values())), 'scale': round(made['scale'], 6)}


def source_text(extras: dict, entry: dict) -> str:
    return f"""TU-1600 model used by EDF6VehicleCrew (free mod): the salvo interceptor support call (天顶截击机; tu1600.obj).

Source: {TITLE} on Sketchfab, {SOURCE_URL}
  uid {UID}; by Ilya Shevchuk (https://sketchfab.com/shilma513); the download's own metadata names its author
  "{extras.get('author', '?')}".
  Downloaded 2026-11-26 through the Sketchfab download API (glTF).
Licence: CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/); the download's metadata: "{extras.get('license', '?')}".
Credit: This work is based on "{TITLE}" ({SOURCE_URL}) by Ilya Shevchuk (https://sketchfab.com/shilma513) licensed under
  CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
Changes: turned to face +z, scaled to {SPAN:.0f} m across ({entry['size'][2]:.1f} m long), stood on its lowest point and
centred; its decal sheet left out; its triangles reduced to {entry['polygons']} (from {SOURCE_TRIANGLES}); its textures
halved to {TEX} px; converted to the game's model format by the mod's installer, which adds its bones, engine flames,
weapon hardpoint and collision.
Made by tools/prep_tu1600.py.
"""


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    out_root = argv[1] if len(argv) > 1 else DEV_MODELS
    folder = os.path.join(out_root, OUT_SUBDIR)
    os.makedirs(folder, exist_ok=True)
    made = build(argv[0])
    entry = write(folder, made)
    print(MODEL, json.dumps({k: v for k, v in entry.items() if k not in ('bones', 'maps')}), 'rounds', made['rounds'])
    with open(os.path.join(folder, MANIFEST), 'w', encoding='utf-8', newline='\n') as h:
        json.dump({'source': SOURCE_URL, 'uid': UID, 'models': {MODEL: entry}}, h, indent=1)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(source_text(made['extras'], entry))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
