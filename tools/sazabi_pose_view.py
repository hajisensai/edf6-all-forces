"""The Sazabi's poses (src/sazabi_pose.h through tools/sazabi_pose_check.exe --dump) drawn on its built model: the
simulation the gait, the stance, the swing and the camera are worked out in before anything goes into the game.

    python tools/sazabi_pose_view.py MRAB OUT_DIR [--views V,..] [--frames N,..] [--bones] [--size PX] [scenario ...]

MRAB: EDF6VC_SAZABI.MRAB as pylib/sazabi_model.py builds it (python pylib/sazabi_model.py DIR). Each vertex moves with
the bone it is skinned to: v' = (v - joint) R s + p (the bone's model rotation, scale and joint after the pose, sz_root's
frame: the soles' floor is y = 0). Views:
  front, side       orthographic, the whole mech, the floor line
  legs, legside     orthographic, the legs only (the floor to 15 m), the floor line: do the soles stand on it?
  cam               the riding camera (pylib/vcobjects.py SAZABI_SEAT_CAMERA), perspective, the ground grid
  quarter           a three-quarter view from behind and above, perspective, the ground grid
  front3q           a three-quarter view from ahead on its right, perspective, the ground grid
--bones colours each bone apart (with a legend) instead of the model's own colours. Needs numpy and PIL; build
sazabi_pose_check first (cmake --build build --target sazabi_pose_check).
"""
from __future__ import annotations

import argparse
import io
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import graft_pure as g  # noqa: E402
import mdb  # noqa: E402
import vcobjects as vc  # noqa: E402
from mdb import bind_world, mdb_read, rab_read  # noqa: E402

CHECK = os.path.join(ROOT, 'build', 'sazabi_pose_check.exe')
LIGHT = np.array([0.35, 0.8, -0.45]) / np.linalg.norm([0.35, 0.8, -0.45])
QUARTER = ((-24.0, 30.0, -42.0), (0.0, 12.0, 0.0))   # eye, look (model frame)
FRONT3Q = ((-19.0, 17.0, 25.0), (0.0, 13.0, 2.0))   # ahead of it on its right, a little above: the swings and the shield
FOV = 60.0   # degrees, vertical


def load_model(path: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str], dict[str, np.ndarray]]:
    """(triangles' corners (n, 3, 3), each triangle's bone, its colour, bone names, each bone's bind joint)."""
    rab = rab_read(open(path, 'rb').read())
    md = mdb_read(next(f for f in rab.files if f.name.endswith('.mdb')).data)
    pal = np.asarray(Image.open(io.BytesIO(next(f for f in rab.files if f.name.endswith('_df.dds')).data)).convert('RGB'))
    names = [md.name_of(b.name) for b in md.bones]
    joint = {n: np.array(w[12:15]) for n, w in zip(names, bind_world(md))}
    tris, bones, cols = [], [], []
    for me in md.objects[0].meshes:
        pos = np.array(g.mesh_positions(me))
        uv = np.array(mdb.read_elem(me, 'texcoord'))
        bi, _ = g.skin_columns(me)
        bone = np.array([int(r[0]) for r in bi])
        idx = np.frombuffer(me.indices, '<u2').reshape(-1, 3)
        col = pal[(uv[:, 1] * pal.shape[0]).astype(int).clip(0, pal.shape[0] - 1),
                  (uv[:, 0] * pal.shape[1]).astype(int).clip(0, pal.shape[1] - 1)].astype(float)
        if md.name_of(md.materials[me.material].name) != 'sz_body':   # a glowing part: bright
            col = np.minimum(col * 1.5 + 80.0, 255.0)
        tris.append(pos[idx])
        bones.append(bone[idx[:, 0]])
        cols.append(col[idx[:, 0]])
    return np.vstack(tris), np.concatenate(bones), np.vstack(cols), names, joint


def read_dump(path: str) -> dict[tuple[str, int], dict[str, tuple[np.ndarray, np.ndarray, float]]]:
    frames: dict = {}
    cur = None
    for line in open(path, encoding='utf-8'):
        t = line.split()
        if t[0] == 'frame':
            cur = frames.setdefault((t[1], int(t[2])), {})
        else:
            v = [float(x) for x in t[1:]]
            cur[t[0]] = (np.array(v[:9]).reshape(3, 3), np.array(v[9:12]), v[12])
    return frames


def posed(tris: np.ndarray, bones: np.ndarray, names: list[str], joint: dict, frame: dict) -> np.ndarray:
    out = tris.copy()
    for b in np.unique(bones):
        n = names[b]
        if n not in frame:
            continue
        rot, at, scale = frame[n]
        sel = bones == b
        out[sel] = ((tris[sel] - joint[n]) * scale) @ rot + at
    return out


def shade(tris: np.ndarray, cols: np.ndarray) -> np.ndarray:
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    light = 0.35 + 0.65 * np.abs(n @ LIGHT)
    return np.clip(cols * light[:, None], 0, 255).astype(int)


def ortho(tris: np.ndarray, cols: np.ndarray, view: str, size: int, label: str) -> Image.Image:
    """front: looking at the mech's face (its left on the right); side: from its left (its nose to the right)."""
    legs = view in ('legs', 'legside')
    a, d, sx, sd = (0, 2, -1.0, 1.0) if view in ('front', 'legs') else (2, 0, 1.0, -1.0)
    if legs:
        keep = tris[:, :, 1].mean(axis=1) < 15.0
        tris, cols = tris[keep], cols[keep]
    span = 24.0 if legs else 34.0
    scale = (size - 20) / span
    img = Image.new('RGB', (size, size), (36, 38, 46))
    dr = ImageDraw.Draw(img)
    floor = size - 30
    dr.line([(0, floor), (size, floor)], fill=(120, 160, 120), width=2)
    for k in range(1, int(span) + 1):
        y = floor - k * scale
        dr.line([(0, y), (6, y)], fill=(90, 90, 90))
    q = np.empty(tris.shape[:2] + (2,))
    q[:, :, 0] = tris[:, :, a] * sx * scale + size / 2
    q[:, :, 1] = floor - tris[:, :, 1] * scale
    depth = tris[:, :, d].mean(axis=1) * sd
    c = shade(tris, cols)
    for i in np.argsort(depth):
        dr.polygon([tuple(x) for x in q[i]], fill=tuple(c[i]))
    low = tris[:, :, 1].min()
    dr.text((6, 4), f'{label} {view} min {low:+.2f}', fill=(255, 255, 255))
    return img


def perspective(tris: np.ndarray, cols: np.ndarray, eye, look, size: int, label: str) -> Image.Image:
    eye, look = np.array(eye, float), np.array(look, float)
    f = look - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, (0.0, 1.0, 0.0))
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    w, h = size, int(size * 9 / 16)
    k = (h / 2) / math.tan(math.radians(FOV) / 2)

    def project(p: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        rel = p - eye
        z = rel @ f
        return np.stack([w / 2 + k * (rel @ r) / np.maximum(z, 0.1), h / 2 - k * (rel @ u) / np.maximum(z, 0.1)], -1), z

    img = Image.new('RGB', (w, h), (150, 175, 205))
    dr = ImageDraw.Draw(img)
    # the ground: a 10 m grid round the mech
    grid = np.arange(-60.0, 61.0, 10.0)
    for x in grid:
        for line in (((x, 0, -60), (x, 0, 60)), ((-60, 0, x), (60, 0, x))):
            pts = np.linspace(line[0], line[1], 40)
            q, z = project(pts)
            seg = [tuple(p) for p, zz in zip(q, z) if zz > 0.5]
            if len(seg) > 1:
                dr.line(seg, fill=(95, 120, 80), width=1)
    q, z = project(tris.reshape(-1, 3))
    q, z = q.reshape(-1, 3, 2), z.reshape(-1, 3)
    ok = (z > 0.5).all(axis=1)
    c = shade(tris, cols)
    for i in np.argsort(-z.mean(axis=1)):
        if ok[i]:
            dr.polygon([tuple(x) for x in q[i]], fill=tuple(c[i]))
    dr.text((6, 4), label, fill=(0, 0, 0))
    return img


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('mrab')
    ap.add_argument('out')
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--views', default='front,side')
    ap.add_argument('--frames', default='0,4,8,12,16,20')
    ap.add_argument('--bones', action='store_true')
    ap.add_argument('--size', type=int, default=420)
    ap.add_argument('--wrap', type=int, default=0, help='tiles a row (default: one frame a row)')
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    tris, bones, cols, names, joint = load_model(a.mrab)
    if a.bones:
        rng = np.random.default_rng(7)
        pal = rng.uniform(60, 255, (len(names), 3))
        cols = pal[bones]
    root = joint['sz_root']
    jfile = os.path.join(a.out, 'joints.txt')
    with open(jfile, 'w', encoding='utf-8') as h:
        for n, at in joint.items():
            if n.startswith('sz_'):
                h.write(f'{n} {at[0] - root[0]:.4f} {at[1] - root[1]:.4f} {at[2] - root[2]:.4f}\n')
    dump = os.path.join(a.out, 'poses.txt')
    r = subprocess.run([CHECK, '--joints', jfile, '--dump', dump], capture_output=True, text=True)
    print(r.stdout.strip())
    frames = read_dump(dump)
    joint = {n: v - root for n, v in joint.items()}
    tris = tris - root
    views = a.views.split(',')
    picks = [int(x) for x in a.frames.split(',')]
    for scenario in sorted({s for s, _ in frames}):
        if a.scenarios and scenario not in a.scenarios:
            continue
        tiles = []
        for f in picks:
            t = posed(tris, bones, names, joint, frames[(scenario, f)])
            legs = {side: t[np.isin(bones, [names.index(f'sz_{b}_{side}') for b in ('thigh', 'shin', 'foot')])][:, :, 1].min()
                    for side in ('l', 'r')}
            for v in views:
                label = f'{scenario} {f} L{legs["l"]:+.2f} R{legs["r"]:+.2f}'
                if v == 'cam':
                    tiles.append(perspective(t, cols, *vc.SAZABI_SEAT_CAMERA, a.size, label))
                elif v == 'quarter':
                    tiles.append(perspective(t, cols, *QUARTER, a.size, label))
                elif v == 'front3q':
                    tiles.append(perspective(t, cols, *FRONT3Q, a.size, label))
                else:
                    tiles.append(ortho(t, cols, v, a.size, label))
        cols_n = len(views) * max(1, a.wrap // len(views))   # --wrap: that many tiles a row (frames side by side)
        th = max(im.height for im in tiles)
        sheet = Image.new('RGB', (a.size * cols_n, th * ((len(tiles) + cols_n - 1) // cols_n)))
        for k, im in enumerate(tiles):
            sheet.paste(im, ((k % cols_n) * a.size, (k // cols_n) * th))
        path = os.path.join(a.out, f'{scenario}.png')
        sheet.save(path)
        print('wrote', path)
    if a.bones:   # the legend
        leg = Image.new('RGB', (260, 14 * len(names) + 8), (30, 30, 30))
        dr = ImageDraw.Draw(leg)
        for k, n in enumerate(names):
            dr.rectangle([4, 4 + 14 * k, 14, 14 + 14 * k], fill=tuple(int(x) for x in pal[k]))
            dr.text((20, 3 + 14 * k), n, fill=(255, 255, 255))
        leg.save(os.path.join(a.out, 'legend.png'))
    return r.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
