"""Developer-only: the user's Sazabi glTF (Sketchfab "P-Japran color ver", after kunnatee's "SAZABI MSN-04 Gundam",
CC BY-NC-SA 4.0) into the model folder the installer builds the vehicle from (pylib/sazabi_model.py):

    <models>/sazabi/sazabi.obj / .mtl     the mech in game space and metres, one OBJ object per bone of
                                          sazabi_model.SKELETON (every triangle rigid on that bone), decimated to
                                          about TARGET_TRIS, normals smoothed within AUTO_SMOOTH
    <models>/sazabi/sazabi_skeleton.json  each bone's joint (game space, metres): where it turns
    <models>/sazabi/LICENSE.txt           the attribution the licence asks for

The source is read from <models>/sazabi_source/scene.gltf by default: outside the model folder, which the installer's
cache hashes and tools/build_release.py ships whole (the installer needs only the files above).

It needs numpy, scipy and fast-simplification (pip): none of them is in the installer, which only reads the result.

    python tools/prep_sazabi.py [<models>/sazabi_source/scene.gltf] [--out <models>/sazabi]

The source (measured, docs/sazabi-re.md §1): 252,848 triangles in 68 meshes that are not body parts (Sketchfab merged
by material, then cut ~5000 triangles a mesh), 20 plain-colour materials (three emissive), no skin, no texture; +Y up,
+Z the front, +X the mech's left (the game's axes), soles at y = -84, the funnel packs' top at y = 1108.
So the parts are found again from connectivity: welded positions -> 682 rigid pieces (armour plates, frames), each
put on the bone whose segment (BONE_SEGMENTS) its area-weighted centre is nearest, then RULES.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import gltf  # noqa: E402
import obj_model  # noqa: E402
import sazabi_model as sz  # noqa: E402

SOURCE_SUBDIR = 'sazabi_source'   # next to the model folder, not in it
TARGET_TRIS = 54000          # the whole mech (Retro-Balam 54,468, Nix 27,367)
KEEP_SMALL = 48              # pieces this small are kept whole: bolts, vents, the eye
AUTO_SMOOTH = 40.0           # degrees: sharper creases keep hard edges (it is armour)
WELD = 1e-3                  # source units: positions this close are one point
# Each bone's segment in source units (head = its joint, tail), read off front, side and top renders with a 50-unit
# grid (tools/prep_sazabi.py has no way to find joints by itself: the inner frames and the armour overlap, the pieces
# that meet are not the ones that turn). The legs and packs are symmetric (the right side is the left mirrored,
# x -> -x); the arms are not: the right forearm hangs by the thigh, the left one points forward.
BONE_SEGMENTS: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]] = {
    'sz_root': ((0, -84, 0), (0, 300, 0)),
    'sz_pelvis': ((0, 520, -20), (0, 600, -20)),
    'sz_waist': ((0, 600, -20), (0, 700, -20)),
    'sz_chest': ((0, 690, -20), (0, 840, 10)),
    'sz_head': ((0, 870, 30), (0, 950, 60)),
    'sz_backpack': ((0, 820, -100), (0, 600, -250)),
    'sz_funnelpack_l': ((110, 960, -150), (220, 1100, -330)),
    'sz_tube_l': ((120, 820, -140), (300, 640, -700)),
    'sz_shoulder_l': ((170, 880, -20), (330, 930, -40)),
    'sz_upperarm_l': ((170, 870, -20), (220, 770, 0)),
    'sz_forearm_l': ((220, 770, 0), (300, 700, 100)),
    'sz_hand_l': ((300, 700, 100), (320, 690, 140)),
    'sz_shoulder_r': ((-170, 880, -20), (-330, 930, -40)),
    'sz_upperarm_r': ((-170, 870, -20), (-215, 790, 20)),
    'sz_forearm_r': ((-215, 790, 20), (-290, 600, 80)),
    'sz_hand_r': ((-290, 600, 80), (-300, 560, 100)),
    'sz_thigh_l': ((120, 480, -20), (260, 380, 40)),
    'sz_shin_l': ((260, 380, 40), (330, 140, -50)),
    'sz_foot_l': ((330, 120, -60), (460, -60, -40)),
}
WAIST_HALF_WIDTH = 60.0   # a waist piece further out than this is a front skirt plate, the pelvis's
SKIRT_TOP = 490.0        # a thigh piece over this is a side skirt, the pelvis's
ARM_SKIRT = (700.0, 260.0)   # an arm piece under y 700 within |x| 260 is a skirt too (the arms hang over them)
FUNNEL_MATERIAL = '13___Default'   # the funnels' glowing nozzles: a funnel is a pack piece with this colour in it
FUNNEL_MIN_TRIS = 500


def mirrored(segments: dict) -> dict:
    out = dict(segments)
    for name, (h, t) in segments.items():
        if name.endswith('_l') and name[:-2] + '_r' not in segments:
            out[name[:-2] + '_r'] = ((-h[0], h[1], h[2]), (-t[0], t[1], t[2]))
    return out


def to_game(p: np.ndarray) -> np.ndarray:
    """Source units -> game metres, soles on y = 0."""
    q = np.array(p, dtype=np.float64)
    q[..., 1] = q[..., 1] - sz.SOURCE_SOLE_Y
    return q * sz.SOURCE_SCALE


# ------------------------------------------------------------------------------------------ pieces

def weld_keys(p: np.ndarray) -> np.ndarray:
    _, inv = np.unique(np.round(p / WELD).astype(np.int64), axis=0, return_inverse=True)
    return inv.ravel()


def pieces(scene: gltf.Scene) -> np.ndarray:
    """Each triangle's connected piece (welded positions)."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    w = weld_keys(scene.positions)[scene.triangles]
    n = int(w.max()) + 1
    a = coo_matrix((np.ones(2 * len(w)), (np.r_[w[:, 0], w[:, 1]], np.r_[w[:, 1], w[:, 2]])), shape=(n, n))
    _, label = connected_components(a, directed=False)
    return label[w[:, 0]]


def _seg_dist(p: np.ndarray, a: np.ndarray, b: np.ndarray) -> np.ndarray:
    ab = b - a
    t = np.clip(((p - a) @ ab) / (ab @ ab + 1e-9), 0.0, 1.0)
    return np.linalg.norm(p - (a + t[:, None] * ab), axis=1)


def assign(scene: gltf.Scene, piece: np.ndarray) -> np.ndarray:
    """Each piece's bone index in sz.BONE_NAMES."""
    tri = scene.positions[scene.triangles]
    area = np.linalg.norm(np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]), axis=1) / 2 + 1e-12
    centre = tri.mean(axis=1)
    k = int(piece.max()) + 1
    w = np.bincount(piece, area, k)
    c = np.stack([np.bincount(piece, area * centre[:, i], k) for i in range(3)], axis=1) / w[:, None]
    ymin = np.full(k, np.inf)
    np.minimum.at(ymin, piece, tri[:, :, 1].min(axis=1))
    segs = mirrored(BONE_SEGMENTS)
    names = list(segs)
    d = np.stack([_seg_dist(c, np.array(segs[n][0], float), np.array(segs[n][1], float)) for n in names], axis=1)
    bone = np.array([sz.BONE_NAMES.index(names[i]) for i in d.argmin(axis=1)])
    idx = sz.BONE_NAMES.index
    for side in ('l', 'r'):
        leg = np.isin(bone, [idx(f'sz_shin_{side}'), idx(f'sz_foot_{side}')])
        bone[leg] = idx(f'sz_shin_{side}')
        bone[leg & (ymin < 0.0)] = idx(f'sz_foot_{side}')        # the feet are the two pieces under the floor
    bone[(bone == idx('sz_head')) & (c[:, 1] < 850.0)] = idx('sz_chest')   # the head owns what is over the neck ring
    bone[(bone == idx('sz_waist')) & (np.abs(c[:, 0]) > WAIST_HALF_WIDTH)] = idx('sz_pelvis')   # front skirt plates
    for side in ('l', 'r'):
        bone[(bone == idx(f'sz_thigh_{side}')) & (c[:, 1] > SKIRT_TOP)] = idx('sz_pelvis')
        arm = np.isin(bone, [idx(f'sz_{b}_{side}') for b in ('upperarm', 'forearm')])
        bone[arm & (c[:, 1] < ARM_SKIRT[0]) & (np.abs(c[:, 0]) < ARM_SKIRT[1])] = idx('sz_pelvis')
    funnel_mat = next(i for i, m in enumerate(scene.materials) if m.name == FUNNEL_MATERIAL)
    for side in ('l', 'r'):
        pack = np.where(bone == idx(f'sz_funnelpack_{side}'))[0]
        tris = np.bincount(piece, minlength=k)
        glow = np.zeros(k, bool)
        glow[np.unique(piece[scene.material == funnel_mat])] = True
        funnels = [p for p in pack if glow[p] and tris[p] >= FUNNEL_MIN_TRIS]
        if len(funnels) != 3:
            raise SystemExit(f'{side}: {len(funnels)} funnels found in the pack, 3 expected')
        for j, p in enumerate(sorted(funnels, key=lambda q: -c[q, 1])):   # 1 the top one
            bone[p] = idx(f'sz_funnel_{side}{j + 1}')
    return bone[piece]


# ------------------------------------------------------------------------------------------ joints

def joints(scene: gltf.Scene, tri_bone: np.ndarray) -> dict[str, list[float]]:
    """Each bone's joint in game metres: sz.FIXED_JOINTS, else its segment's head (BONE_SEGMENTS), else (a funnel,
    whose joint is its own centre: it leaves its pack whole) the centre of its triangles, else (a weapon bone the
    installer models) its parent's."""
    segs = mirrored(BONE_SEGMENTS)
    out: dict[str, list[float]] = {}
    for i, (name, parent) in enumerate(sz.SKELETON):
        if name in sz.FIXED_JOINTS:
            out[name] = list(sz.FIXED_JOINTS[name])
        elif name in segs:
            out[name] = [float(x) for x in to_game(np.array(segs[name][0], float))]
        elif (tri_bone == i).any():
            out[name] = [float(x) for x in to_game(scene.positions[scene.triangles[tri_bone == i]].reshape(-1, 3).mean(axis=0))]
        else:
            out[name] = list(out[parent])
    return out


# ------------------------------------------------------------------------------------------ decimation, normals

def decimate(p: np.ndarray, f: np.ndarray, keep: float) -> tuple[np.ndarray, np.ndarray]:
    """One single-material piece to about `keep` of its triangles (fast-simplification's quadric collapse)."""
    if len(f) <= KEEP_SMALL or keep >= 0.999:
        return p, f
    import fast_simplification
    key = weld_keys(p)
    uniq, first = np.unique(key, return_index=True)
    pw = p[first]
    remap = np.searchsorted(uniq, key)
    fw = remap[f]
    fw = fw[(fw[:, 0] != fw[:, 1]) & (fw[:, 1] != fw[:, 2]) & (fw[:, 0] != fw[:, 2])]
    target = max(KEEP_SMALL, int(round(len(fw) * keep)))
    if target >= len(fw):
        return pw, fw
    q, g = fast_simplification.simplify(pw.astype(np.float32), fw.astype(np.int32), 1.0 - target / len(fw))
    return np.asarray(q, np.float64), np.asarray(g, np.int64)


def split_normals(p: np.ndarray, f: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Vertices split where faces meet sharper than AUTO_SMOOTH; returns positions, normals, triangles."""
    fn = np.cross(p[f[:, 1]] - p[f[:, 0]], p[f[:, 2]] - p[f[:, 0]])
    area = np.linalg.norm(fn, axis=1)
    ok = area > 1e-12
    f, fn, area = f[ok], fn[ok], area[ok]
    unit = fn / area[:, None]
    cos_lim = math.cos(math.radians(AUTO_SMOOTH))
    pos, nrm, tris = [], [], np.zeros_like(f)
    corners: dict[int, list[tuple[np.ndarray, list[tuple[int, int]]]]] = {}
    for t in range(len(f)):
        for k in range(3):
            v = int(f[t, k])
            groups = corners.setdefault(v, [])
            for g in groups:
                if float(g[0] @ unit[t]) / (np.linalg.norm(g[0]) + 1e-12) >= cos_lim:
                    g[1].append((t, k))
                    g[0][:] = g[0] + fn[t]
                    break
            else:
                groups.append((fn[t].copy(), [(t, k)]))
    for v, groups in corners.items():
        for acc, members in groups:
            idx = len(pos)
            pos.append(p[v])
            nrm.append(acc / (np.linalg.norm(acc) + 1e-12))
            for t, k in members:
                tris[t, k] = idx
    return np.array(pos), np.array(nrm), tris


# ------------------------------------------------------------------------------------------ output

def srgb(c: float) -> float:
    c = min(max(c, 0.0), 1.0)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def write(scene: gltf.Scene, tri_bone: np.ndarray, piece: np.ndarray, out_dir: str) -> dict:
    """The OBJ and MTL. Decimation can drop a sole's lowest corner: the whole mech is lowered or raised by `lift`
    (returned) so its lowest vertex as written is on y = 0, and the joints must move with it (main)."""
    total = len(scene.triangles)
    small = np.bincount(piece)[piece] <= KEEP_SMALL
    keep = min(1.0, (TARGET_TRIS - small.sum()) / max(1, total - small.sum()))
    chunks: list[tuple[str, str, np.ndarray, np.ndarray, np.ndarray]] = []   # bone, material, positions, normals, tris
    for b, name in enumerate(sz.BONE_NAMES):
        sel = np.where(tri_bone == b)[0]
        for pc in np.unique(piece[sel]):
            for mat in np.unique(scene.material[sel][piece[sel] == pc]):
                t = sel[(piece[sel] == pc) & (scene.material[sel] == mat)]
                used, inv = np.unique(scene.triangles[t].ravel(), return_inverse=True)
                p, f = decimate(scene.positions[used], inv.reshape(-1, 3), keep)
                p, n, f = split_normals(to_game(p), f)
                if len(f):
                    chunks.append((name, scene.materials[mat].name, p, n, f))
    lift = -min(float(c[2][:, 1].min()) for c in chunks)
    lines_obj = ['# Sazabi for EDF6VehicleCrew (tools/prep_sazabi.py): game space, metres; one object per bone',
                 'mtllib sazabi.mtl']
    vbase, count, current = 1, 0, None
    per_bone: dict[str, int] = {}
    for name, mat, p, n, f in chunks:
        if name != current:
            lines_obj.append(f'o {name}')
            current = name
        lines_obj.append(f'usemtl {mat}')
        lines_obj += [f'v {x:.5f} {y + lift:.5f} {z:.5f}' for x, y, z in p]
        lines_obj += [f'vn {x:.4f} {y:.4f} {z:.4f}' for x, y, z in n]
        lines_obj += [f'f {a + vbase}//{a + vbase} {c + vbase}//{c + vbase} {d + vbase}//{d + vbase}' for a, c, d in f]
        vbase += len(p)
        count += len(f)
        per_bone[name] = per_bone.get(name, 0) + len(f)
    lines_mtl = []
    for m in scene.materials:
        r, g, b, _ = m.base
        lines_mtl += [f'newmtl {m.name}', f'Kd {srgb(r):.4f} {srgb(g):.4f} {srgb(b):.4f}',
                      f'Pr {m.roughness:.4f}', f'Pm {m.metallic:.4f}']
        if any(m.emissive):
            lines_mtl.append('Ke {:.4f} {:.4f} {:.4f}'.format(*m.emissive))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, sz.OBJ_FILE), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines_obj) + '\n')
    with open(os.path.join(out_dir, 'sazabi.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines_mtl) + '\n')
    return {'triangles': count, 'per_bone': per_bone, 'keep': keep, 'lift': lift}


LICENSE = """Sazabi model used by EDF6VehicleCrew (free, non-commercial mod).

Source: "P-Japran color ver" on Sketchfab (https://sketchfab.com/3d-models/p-japran-color-ver-06dd5654ed4449e7b66e68f98f6cb6b1),
based on "SAZABI MSN-04 Gundam" by kunnatee (https://sketchfab.com/kunnatee,
{source}).
Colour code: https://www.rioxteir.com/mg-sazabi-ver-ka-p-japran-color-ver-by-masjapran-gunpla/
Licence of the original model: {licence}.
Changes: re-rigged into rigid parts per bone, decimated to ~60k triangles, recoloured into one palette texture,
converted to the game's model format; beam rifle, shield and beam tomahawk modelled separately by the mod.
This adaptation is shared under the same licence (CC BY-NC-SA 4.0). Gundam and Sazabi are trademarks of
Sotsu / Sunrise (Bandai Namco); this is an unofficial fan work.
"""


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    default_dir = obj_model.model_dir(sz.MODEL_SUBDIR) or os.path.join(obj_model.DEV_MODELS, sz.MODEL_SUBDIR)
    ap.add_argument('gltf', nargs='?', default=os.path.join(os.path.dirname(default_dir), SOURCE_SUBDIR, 'scene.gltf'))
    ap.add_argument('--out', default=default_dir)
    a = ap.parse_args(argv)
    scene = gltf.read(a.gltf)
    piece = pieces(scene)
    tri_bone = assign(scene, piece)
    joint = joints(scene, tri_bone)
    info = write(scene, tri_bone, piece, a.out)
    joint = {k: v if k in sz.FIXED_JOINTS else [v[0], v[1] + info['lift'], v[2]] for k, v in joint.items()}
    with open(os.path.join(a.out, sz.SKELETON_FILE), 'w', encoding='utf-8', newline='\n') as h:
        json.dump(joint, h, indent=1)
    with open(os.path.join(a.out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(LICENSE.format(source=scene.extras.get('source', ''), licence=scene.extras.get('license', '')))
    print(f'{len(scene.triangles)} -> {info["triangles"]} triangles (pieces kept at {info["keep"]:.3f}, '
          f'lifted {info["lift"]:+.3f} m), '
          f'{int(piece.max()) + 1} pieces')
    for name, n in info['per_bone'].items():
        print(f'  {name:18s} {n:6d}  joint {[round(x, 2) for x in joint[name]]}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
