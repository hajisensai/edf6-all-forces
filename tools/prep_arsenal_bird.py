"""Developer-only: the Arsenal Bird (武库鸟, docs/feature-support-jet-models.md §6.6) and its M101 drone out of the ACE COMBAT 7
mod "Arsenal Bird (AIAS)" (its unpacked OBJ export: SOURCE.txt) into the model folder the installer builds them from
(pylib/salvo_model.py):

    <models>/arsenal_bird/arsenal_bird.obj / .mtl   the airframe in game space and metres (+Y up, +Z the nose, +X its
                                                    left), at its real size (SPAN m across), standing on its lowest
                                                    point, centred on x and z; one OBJ object (`arsenal_bird`, the hull:
                                                    one rigid bone), one material (`arsb`)
    <models>/arsenal_bird/m101.obj / .mtl           the M101 carrier drone the same way, scaled to DRONE_LENGTH m long;
                                                    one object (`m101`), one material (`m101`)
    <models>/arsenal_bird/<material>_df.png         its colour (paint PAINT, at most TEX / DRONE_TEX px)
    <models>/arsenal_bird/<material>_nm.png         its normal map (DirectX green as measured, Z rebuilt: the source's
                                                    is BC5, R and G only)
    <models>/arsenal_bird/<material>_rml.png        R roughness, G metal, B 0 (the source's MREC: R metal, G roughness,
                                                    B emissive)
    <models>/arsenal_bird/arsenal_bird.json         both models' bone, material, size
    <models>/arsenal_bird/SOURCE.txt                where they came from and what was changed

    python tools/prep_arsenal_bird.py <the unpacked mod's folder> [<models folder>]

The folder (Arsenal-Bird.obj, 贴图/, 武器-M101/) is read as data: nothing in it runs. Keep it out of the model folder:
build_release copies the model folder whole.

The source's frame is Unreal's: centimetres, +X the nose, +Y right, +Z up (a left-handed frame). Game space is
(x, y, z)_game = (-Y, Z, X)_source / 100. That mapping is a mirror (determinant -1), and the source's left-handed
winding turns into the game's: measured, the triangles kept as they are enclose a positive volume (outward, as every
other prepared model), so no winding is flipped.

What is changed:
- vertex normals made (mesh_normals.auto_smooth, 45 degrees: the source has none; its own viewer computes them);
- the 5 521 airframe triangles whose V lies in the tile below (-1 .. 0: Unreal samples with wrap) moved up one tile, so
  that nothing depends on the game's sampler wrapping;
- the airframe's 4096 x 2048 colour map halved to TEX wide (one support aircraft: 4096 would be 22 MB of video memory
  for its colour alone); the M101's maps to DRONE_TEX (a 5.7 m drone, many in the air at once, sharing them);
- MREC repacked into the plugin's R roughness / G metal (its B, the M101's lamps' emission, has no slot: dropped);
- the M101 scaled up DRONE_SCALE times: the source's 1.33 m drone is a missile-sized toy next to the 5.7 m drone every
  other carrier launches (EDF6VC_DRONE.MRAB, pd607 x 3: jet_models.RECIPES); the plugin's drone AI, collision and
  weapons are sized for that one, so the M101 takes its length.
The airframe keeps every triangle (111 267: one material, no mesh_simplify; the TU-1600 it replaces had 72 000 after
collapsing): its four-blade propellers and thin control surfaces are what a collapse eats first. It has no jet engine:
no nozzle (the plugin draws no exhaust for a model without nozzle bones: src/booster.cpp). The salvo's missiles leave from
the store hardpoint (HP_STORE) make_jets puts on its belly.
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))

DEV_MODELS = r'D:\APP\edf6-models'
OUT_SUBDIR = 'arsenal_bird'
MANIFEST = 'arsenal_bird.json'
TITLE = 'Arsenal Bird (AIAS) - ACE COMBAT 7 mod'
ARCHIVE_NAME = 'Arsenal Bird-3317-1-1-1757441192.7z'
PAINT = '14'
TEX = 2048
DRONE_TEX = 1024
DRONE_LENGTH = 5.7          # m: EDF6VC_DRONE.MRAB's length (pd607_Drone_airstrike x 3, jet_models.RECIPES)
NORMAL_GREEN_MIN = 0.1      # texfile_accel.green_down_score: DirectX (+G down); measured 0.54 (airframe), 0.78 (M101)


class PrepError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise PrepError(msg)


# name: (source OBJ, its group and material, triangles, vertices, size in game axes (m, source scale), the textures'
# folder and stems, the maps' cap)
SOURCES = {
    'arsenal_bird': {'obj': 'Arsenal-Bird.obj', 'group': 'body', 'material': 'arsb', 'triangles': 111267,
                     'vertices': 93568, 'size': (106.56, 14.88, 22.83), 'tex': '贴图', 'stem': 'arsb', 'cap': TEX},
    'm101': {'obj': os.path.join('武器-M101', 'M101.obj'), 'group': 'm101', 'material': 'm101', 'triangles': 4384,
             'vertices': 4991, 'size': (0.755, 0.357, 1.329), 'tex': os.path.join('武器-M101', '贴图'), 'stem': 'm101',
             'cap': DRONE_TEX},
}


def read_source(path: str, group: str, material: str) -> tuple:
    """(positions (n, 3) cm Unreal axes, uvs (n, 2) OBJ convention, triangles (t, 3)): `v`/`vt` paired by index as the
    export writes them (every face `f a/a b/b c/c`), one group and one material."""
    import numpy as np
    V: list = []
    VT: list = []
    T: list = []
    groups, mats = set(), set()
    with open(path, encoding='utf-8') as h:
        for line in h:
            if line.startswith('v '):
                V.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('vt '):
                VT.append([float(x) for x in line.split()[1:3]])
            elif line.startswith('f '):
                c = [x.split('/') for x in line.split()[1:]]
                _req(len(c) == 3 and all(len(k) >= 2 and k[0] == k[1] for k in c), f'{path}: face {line.strip()}')
                T.append([int(k[0]) - 1 for k in c])
            elif line.startswith('g '):
                groups.add(line.split(None, 1)[1].strip())
            elif line.startswith('usemtl '):
                mats.add(line.split(None, 1)[1].strip())
    _req(groups == {group} and mats == {material}, f'{path}: groups {groups}, materials {mats}')
    _req(len(V) == len(VT), f'{path}: {len(V)} positions, {len(VT)} uvs')
    return np.array(V), np.array(VT), np.array(T, dtype=np.int64)


def to_game(p):  # noqa: ANN001, ANN201 - numpy (n, 3)
    """Unreal cm (X nose, Y right, Z up) -> game m (+Y up, +Z nose, +X left)."""
    import numpy as np
    return np.stack([-p[:, 1], p[:, 2], p[:, 0]], 1) / 100.0


def signed_volume(P, T) -> float:  # noqa: ANN001
    import numpy as np
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    return float(np.einsum('ij,ij->i', a, np.cross(b, c)).sum() / 6.0)


def build(root: str, name: str) -> dict:
    """{'positions', 'normals', 'uvs' (per corner row), 'tris', 'scale', 'retiled'}: one source in game space."""
    import numpy as np
    import mesh_normals
    s = SOURCES[name]
    P, UV, T = read_source(os.path.join(root, s['obj']), s['group'], s['material'])
    _req(len(T) == s['triangles'] and len(P) == s['vertices'],
         f'{s["obj"]}: {len(T)} triangles, {len(P)} vertices: not the {name} this was made for')
    G = to_game(P)
    size = np.ptp(G, axis=0)
    _req(all(abs(size[k] - s['size'][k]) < 0.01 * s['size'][k] for k in range(3)), f'{name}: {size} m, not {s["size"]}')
    scale = 1.0 if name == 'arsenal_bird' else DRONE_LENGTH / float(size[2])
    lo, hi = G.min(0), G.max(0)
    shift = np.array([-(lo[0] + hi[0]) / 2.0, -lo[1], -(lo[2] + hi[2]) / 2.0])
    G = (G + shift) * scale
    _req(signed_volume(G, T) > 0.0, f'{name}: its triangles enclose a negative volume: the winding is inward')
    # corners: one row per triangle corner, then shared again by (position, uv)
    tile = np.floor(UV[T][..., 1].min(1) + 1e-6)
    _req(bool((UV[T][..., 1].max(1) - tile <= 1.0 + 1e-4).all()), f'{name}: a triangle straddles a V tile')
    cuv = UV[T].copy()
    cuv[..., 1] -= tile[:, None]
    key = np.concatenate([T.reshape(-1, 1).astype(np.float64), np.round(cuv.reshape(-1, 2) * 1e6)], 1)
    _u, first, rows = np.unique(key, axis=0, return_index=True, return_inverse=True)
    rows = rows.reshape(-1, 3)
    pos = G[T.reshape(-1)[first]]
    uv = cuv.reshape(-1, 2)[first]
    normals, normal_of = mesh_normals.auto_smooth(pos, rows)
    return {'positions': pos, 'uvs': uv, 'normals': normals, 'normal_of': normal_of, 'tris': rows, 'scale': scale,
            'retiled': int((tile != 0).sum()), 'size': [round(float(v), 3) for v in np.ptp(pos, axis=0)]}


def _capped(im, cap: int):  # noqa: ANN001, ANN202 - PIL image
    from PIL import Image
    if im.width > cap:
        f = cap / im.width
        im = im.resize((cap, max(1, round(im.height * f))), Image.LANCZOS)
    return im


def write_textures(root: str, folder: str, name: str) -> dict:
    """The material's three maps; {'size', 'normal_green'}."""
    from PIL import Image
    import numpy as np
    import texfile_accel
    s = SOURCES[name]
    src = os.path.join(root, s['tex'])
    mat = s['material']
    df = _capped(Image.open(os.path.join(src, f'{s["stem"]}_{PAINT}_D.png')).convert('RGB'), s['cap'])
    df.save(os.path.join(folder, f'{mat}_df.png'))
    nm = _capped(Image.open(os.path.join(src, f'{s["stem"]}_N.png')).convert('RGB'), s['cap'])
    rgb = np.asarray(nm)
    score = texfile_accel.green_down_score(rgb)
    _req(score >= NORMAL_GREEN_MIN, f'{name}: normal map green score {score:+.3f}: not DirectX')
    xy = rgb[..., :2].astype(np.float32) / 127.5 - 1.0
    z = np.sqrt(np.clip(1.0 - (xy * xy).sum(-1), 0.0, 1.0))
    out = np.array(rgb, dtype=np.uint8, copy=True)
    out[..., 2] = np.clip(np.round((z + 1.0) * 127.5), 0, 255).astype(np.uint8)
    Image.fromarray(out, 'RGB').save(os.path.join(folder, f'{mat}_nm.png'))
    mrec = np.asarray(_capped(Image.open(os.path.join(src, f'{s["stem"]}_{PAINT}_MREC.png')).convert('RGB'), s['cap']))
    rml = np.zeros_like(mrec)
    rml[..., 0] = mrec[..., 1]          # roughness
    rml[..., 1] = mrec[..., 0]          # metal
    Image.fromarray(rml, 'RGB').save(os.path.join(folder, f'{mat}_rml.png'))
    return {'size': list(df.size), 'normal_green': round(score, 3)}


def write(root: str, folder: str, name: str) -> dict:
    """<name>.obj / .mtl, the textures, the manifest entry."""
    import numpy as np
    made = build(root, name)
    mat = SOURCES[name]['material']
    P, UV, N = made['positions'], made['uvs'], made['normals']
    lines = [f'# {name}: tools/prep_arsenal_bird.py from "{TITLE}"', f'mtllib {name}.mtl', f'o {name}']
    lines += [f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}' for p in P]
    lines += [f'vt {u[0]:.6f} {u[1]:.6f}' for u in UV]
    lines += [f'vn {q[0]:.5f} {q[1]:.5f} {q[2]:.5f}' for q in N]
    lines.append(f'usemtl {mat}')
    for t, nt in zip(made['tris'] + 1, made['normal_of'] + 1):
        lines.append('f ' + ' '.join(f'{t[k]}/{t[k]}/{nt[k]}' for k in range(3)))
    with open(os.path.join(folder, f'{name}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    with open(os.path.join(folder, f'{name}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(f'newmtl {mat}\nmap_Kd {mat}_df.png\n')
    maps = write_textures(root, folder, name)
    return {'bones': {name: {'parent': None, 'pivot': [0.0, 0.0, 0.0]}}, 'materials': [mat], 'maps': {mat: maps},
            'size': made['size'], 'polygons': int(len(made['tris'])), 'scale': round(made['scale'], 6),
            'retiled': made['retiled']}


def source_text(entries: dict) -> str:
    a, m = entries['arsenal_bird'], entries['m101']
    return f"""Arsenal Bird and M101 models used by EDF6VehicleCrew (free mod): the 武库鸟 support calls (missile mode: the
salvo sortie; drone mode: the carrier launching M101 drones).

Source: "{TITLE}", the community mod archive {ARCHIVE_NAME} (its ~AIAS_Base_P.pak: Unreal 4.18 meshes and textures,
  the Arsenal Bird airframe and the M101 drone w_wuav_m101_arsb), unpacked to OBJ + PNG; the design is Bandai Namco's
  (ACE COMBAT 7: SKIES UNKNOWN). Paint {PAINT}.
Changes: turned into the game's axes (+Y up, +Z the nose) and metres; the airframe at its real size
({a['size'][0]:.1f} m across, {a['size'][2]:.1f} m long, {a['polygons']} triangles), the M101 scaled x{m['scale']:.3f} to
{m['size'][2]:.1f} m long ({m['polygons']} triangles); both stood on their lowest point and centred; vertex normals made
(45 degree auto-smooth); the airframe's colour map halved to {TEX} px, the M101's maps to {DRONE_TEX} px; the MREC maps
repacked to roughness / metal; converted to the game's model format by the mod's installer, which adds its bones,
weapon hardpoints and collision.
Made by tools/prep_arsenal_bird.py.
"""


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    out_root = argv[1] if len(argv) > 1 else DEV_MODELS
    folder = os.path.join(out_root, OUT_SUBDIR)
    os.makedirs(folder, exist_ok=True)
    entries = {}
    for name in SOURCES:
        entries[name] = write(argv[0], folder, name)
        print(name, json.dumps({k: v for k, v in entries[name].items() if k != 'bones'}, ensure_ascii=False))
    with open(os.path.join(folder, MANIFEST), 'w', encoding='utf-8', newline='\n') as h:
        json.dump({'source': TITLE, 'paint': PAINT, 'models': entries}, h, indent=1, ensure_ascii=False)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(source_text(entries))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
