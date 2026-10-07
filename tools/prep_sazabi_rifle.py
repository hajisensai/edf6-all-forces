"""Developer-only: the beam shot rifle out of xianxiao's Sazabi (Sketchfab ec72dbae49644e228b983643989cdd83, CC BY 4.0)
into the model folder, where pylib/sazabi_arms.py puts it on the right hand in place of its own boxes:

    <models>/sazabi/sazabi_rifle.obj        the rifle, one object sz_rifle, in metres and the model's axes, relative
                                            to the right hand's joint (sz_hand_r): its grip at RIFLE_GRIP (in the
                                            fist), its barrel level along +z
    <models>/sazabi/sazabi_rifle.json       {"muzzle": its bore's mouth, hand-relative}: sz_muzzle's joint, where the
                                            plugin fires from (it reads the bone, nothing in it is fixed to a rifle)
    <models>/sazabi/LICENSE_rifle.txt       the attribution CC BY asks for

The source is read from <models>/sazabi_rifle_source/scene.gltf by default (outside the model folder, as
tools/prep_sazabi.py's). It needs numpy and scipy (pip), as tools/prep_sazabi.py.

    python tools/prep_sazabi_rifle.py [<models>/sazabi_rifle_source/scene.gltf] [--out <models>/sazabi]

The source (measured 2026-10-07): 21 meshes, one grey material, no texture, no skin; +Y up, +Z the front, +X the
mech's left; 37 m tall. The rifle is its own mesh (the one reaching farthest forward: 24,732 triangles, rolled ~24 deg
in the right hand); the hand's fingers lie inside its bounds round the grip, and one piece of the muzzle sits in another
mesh. So:
  - the rifle = that mesh, plus the other meshes' pieces wholly inside its bounds in its front half; the ones in its
    back half are the hand (their centre: the grip);
  - its axes from its own shape (principal axes: the longest its barrel, the next its up), so the roll goes;
  - the muzzle: the round pieces (as wide as tall) at its front, their centre, at its front face;
  - its grip on RIFLE_GRIP, its axes on the model's (level: the source's hand holds it from above, its bore under the
    grip, so no other turn puts it level in the fist), scaled so the muzzle is as far ahead of the grip as
    RIFLE_MUZZLE's (its own rifle is longer than the mech is tall: here it is the length the plugin's rifle always
    had, ~12.6 m);
  - painted from the model folder's palette by piece size (no colour in the source).
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
sys.path.insert(0, HERE)
import gltf  # noqa: E402
import obj_model  # noqa: E402
import prep_sazabi  # noqa: E402
import sazabi_arms  # noqa: E402
import sazabi_model as sz  # noqa: E402

SOURCE_SUBDIR = 'sazabi_rifle_source'
FRONT_DEPTH = 0.06      # of the rifle's length: the pieces reaching this near its front are its muzzle's
ROUND = 0.05            # a round piece: its width and height within this share of each other
# The paint (sazabi.mtl's colours) by piece size in triangles: the shells gunmetal, the frames darker, the small
# parts and the bore light grey.
SHELL, FRAME, SMALL = '07___Default', '17___Default', '09___Default'
SHELL_TRIS, FRAME_TRIS = 1000, 100


def rifle_triangles(scene: gltf.Scene, piece: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(the rifle's triangles, the hand's triangles) as index arrays into scene.triangles."""
    tz = scene.positions[scene.triangles][:, :, 2].max(axis=1)
    mesh = scene.mesh[int(np.argmax(tz))]
    own = scene.mesh == mesh
    pts = scene.positions[np.unique(scene.triangles[own])]
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    mid = 0.5 * (lo[2] + hi[2])
    rifle, hand = [np.nonzero(own)[0]], []
    for pc in np.unique(piece[~own]):
        t = np.nonzero((piece == pc) & ~own)[0]
        v = scene.positions[scene.triangles[t]].reshape(-1, 3)
        if (v >= lo).all() and (v <= hi).all():
            (rifle if v[:, 2].mean() > mid else hand).append(t)
    if not hand:
        raise SystemExit('no hand round the rifle\'s grip: not xianxiao\'s Sazabi?')
    return np.concatenate(rifle), np.concatenate(hand)


def frame(p: np.ndarray, grip: np.ndarray) -> np.ndarray:
    """The rows (side = the mech's left, up, forward to the muzzle) of the rifle's own axes."""
    _, vec = np.linalg.eigh(np.cov((p - p.mean(axis=0)).T))
    fwd, up = vec[:, 2], vec[:, 1]
    proj = (p - grip) @ fwd
    if proj.max() < -proj.min():
        fwd = -fwd               # the muzzle: the end far from the grip
    if up[1] < 0:
        up = -up
    return np.stack([np.cross(up, fwd), up, fwd])


def muzzle(q: np.ndarray, tris: np.ndarray, piece: np.ndarray) -> tuple[np.ndarray, list[int]]:
    """(the bore's mouth (rifle frame), the bore's pieces): the round pieces near the front, their centre, on the
    front face."""
    front = q[:, 2].max()
    depth = FRONT_DEPTH * (front - q[:, 2].min())
    centres, bore = [], []
    for pc in np.unique(piece):
        v = q[np.unique(tris[piece == pc])]
        if v[:, 2].max() < front - depth:
            continue
        w, h = np.ptp(v[:, 0]), np.ptp(v[:, 1])
        if abs(w - h) <= ROUND * max(w, h) and max(w, h) > 0.0:
            centres.append(0.5 * (v.min(axis=0) + v.max(axis=0)))
            bore.append(int(pc))
    if not centres:
        raise SystemExit('no round piece at the rifle\'s front: no bore found')
    c = np.mean(centres, axis=0)
    return np.array([c[0], c[1], front]), bore


def paint(sizes: np.ndarray, bore: list[int]) -> np.ndarray:
    """Each piece's material by its size; the bore's pieces light."""
    out = np.where(sizes >= SHELL_TRIS, SHELL, np.where(sizes >= FRAME_TRIS, FRAME, SMALL)).astype(object)
    out[bore] = SMALL
    return out


def write(out_dir: str, groups: list[tuple[str, np.ndarray, np.ndarray, np.ndarray]]) -> int:
    lines = ['# The Sazabi\'s beam shot rifle for EDF6VehicleCrew (tools/prep_sazabi_rifle.py): metres, the model\'s',
             '# axes, relative to the right hand\'s joint (sz_hand_r)', 'mtllib sazabi.mtl', 'o sz_rifle']
    base, count = 1, 0
    for mat, p, n, f in groups:
        lines.append(f'usemtl {mat}')
        lines += [f'v {x:.5f} {y:.5f} {z:.5f}' for x, y, z in p]
        lines += [f'vn {x:.4f} {y:.4f} {z:.4f}' for x, y, z in n]
        lines += [f'f {a + base}//{a + base} {b + base}//{b + base} {c + base}//{c + base}' for a, b, c in f]
        base += len(p)
        count += len(f)
    with open(os.path.join(out_dir, sazabi_arms.RIFLE_FILE), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    return count


LICENSE = """Beam shot rifle of the Sazabi used by EDF6VehicleCrew (free mod).

This work is based on "Sazabi" (https://sketchfab.com/3d-models/sazabi-ec72dbae49644e228b983643989cdd83)
by xianxiao (https://sketchfab.com/xian_xiao) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
Changes: only the rifle kept, scaled and turned onto the mod's Sazabi's right hand, painted from its palette,
converted to the game's model format. Gundam and Sazabi are trademarks of Sotsu / Sunrise (Bandai Namco); this is an
unofficial fan work.
"""


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    default_dir = obj_model.model_dir(sz.MODEL_SUBDIR) or os.path.join(obj_model.DEV_MODELS, sz.MODEL_SUBDIR)
    ap.add_argument('gltf', nargs='?', default=os.path.join(os.path.dirname(default_dir), SOURCE_SUBDIR, 'scene.gltf'))
    ap.add_argument('--out', default=default_dir)
    a = ap.parse_args(argv)
    scene = gltf.read(a.gltf)
    piece = prep_sazabi.pieces(scene)
    rifle, hand = rifle_triangles(scene, piece)
    grip = scene.positions[np.unique(scene.triangles[hand])].mean(axis=0)
    used, inv = np.unique(scene.triangles[rifle], return_inverse=True)
    tris = inv.reshape(-1, 3)
    axes = frame(scene.positions[used], grip)
    q = (scene.positions[used] - grip) @ axes.T
    _, rp = np.unique(piece[rifle], return_inverse=True)
    m, bore = muzzle(q, tris, rp)
    grip_at = np.array(sazabi_arms.RIFLE_GRIP)
    k = float((sazabi_arms.RIFLE_MUZZLE[2] - grip_at[2]) / m[2])
    g = q * k + grip_at
    mouth = m * k + grip_at
    mats = paint(np.bincount(rp), bore)
    groups = []
    for mat in (SHELL, FRAME, SMALL):
        t = tris[mats[rp] == mat]
        if len(t):
            u2, inv2 = np.unique(t.ravel(), return_inverse=True)
            p, n, f = prep_sazabi.split_normals(g[u2], inv2.reshape(-1, 3))
            groups.append((mat, p, n, f))
    os.makedirs(a.out, exist_ok=True)
    count = write(a.out, groups)
    with open(os.path.join(a.out, sazabi_arms.RIFLE_INFO), 'w', encoding='utf-8', newline='\n') as h:
        json.dump({'muzzle': [round(float(x), 4) for x in mouth]}, h, indent=1)
    with open(os.path.join(a.out, 'LICENSE_rifle.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(LICENSE)
    lo, hi = g.min(axis=0), g.max(axis=0)
    roll = math.degrees(math.atan2(-axes[1][0], axes[1][1]))
    print(f'{len(rifle)} source triangles -> {count}, scale {k:.4f}, rolled back {roll:+.1f} deg, '
          f'{np.ptp(g[:, 2]):.2f} m long; bounds {np.round(lo, 2).tolist()} .. {np.round(hi, 2).tolist()} '
          f'(hand-relative); muzzle at {np.round(mouth, 3).tolist()}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
