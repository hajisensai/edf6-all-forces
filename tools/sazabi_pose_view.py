"""Draws the Sazabi's poses (src/sazabi_pose.h through tools/sazabi_pose_check.exe --dump) on its built model, so a
gait or a swing is looked at before it is flown: a contact sheet per scenario, front and side, every few frames.

    python tools/sazabi_pose_view.py MRAB OUT_DIR [scenario ...]     (needs numpy and PIL; build sazabi_pose_check first)

MRAB: EDF6VC_SAZABI.MRAB as pylib/sazabi_model.py builds it (python pylib/sazabi_model.py DIR). Each vertex moves with
the bone it is skinned to: v' = (v - joint) R + p (the bone's model rotation and joint after the pose, sz_root's frame).
"""
from __future__ import annotations

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
from mdb import bind_world, mdb_read, rab_read  # noqa: E402

CHECK = os.path.join(ROOT, 'build', 'sazabi_pose_check.exe')
FRAMES = tuple(int(x) for x in os.environ.get("SAZABI_FRAMES", "0,4,8,12,16,20").split(","))
SIZE = int(os.environ.get("SAZABI_VIEW_SIZE", "360"))


def load_model(path: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str], dict[str, np.ndarray]]:
    """(triangles' corners (n, 3, 3), each triangle's bone, its colour, bone names, each bone's bind joint)."""
    rab = rab_read(open(path, 'rb').read())
    md = mdb_read(next(f for f in rab.files if f.name.endswith('.mdb')).data)
    import io
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
                  (uv[:, 0] * pal.shape[1]).astype(int).clip(0, pal.shape[1] - 1)]
        tris.append(pos[idx])
        bones.append(bone[idx[:, 0]])
        cols.append(col[idx[:, 0]])
    return np.vstack(tris), np.concatenate(bones), np.vstack(cols).astype(float), names, joint


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


def draw(tris: np.ndarray, cols: np.ndarray, view: str, label: str) -> Image.Image:
    a, b, d, sx = {'front': (0, 1, 2, -1), 'side': (2, 1, 0, 1)}[view]
    p = tris[:, :, [a, b]].copy()
    p[:, :, 0] *= sx
    depth = tris[:, :, d].mean(1) * (1 if view == 'front' else -1)
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    light = np.abs(n @ (np.array([0.3, 0.5, 0.8]) / np.linalg.norm([0.3, 0.5, 0.8])))
    scale = (SIZE - 20) / 34.0                           # 34 m across the frame, the floor at its bottom
    q = np.empty_like(p)
    q[:, :, 0] = p[:, :, 0] * scale + SIZE / 2
    q[:, :, 1] = SIZE - 10 - p[:, :, 1] * scale
    img = Image.new('RGB', (SIZE, SIZE), (36, 38, 46))
    dr = ImageDraw.Draw(img)
    dr.line([(0, SIZE - 10), (SIZE, SIZE - 10)], fill=(90, 90, 90))
    c = np.clip(cols * (0.35 + 0.65 * light[:, None]), 0, 255).astype(int)
    for i in np.argsort(depth):
        dr.polygon([tuple(x) for x in q[i]], fill=tuple(c[i]))
    dr.text((6, 4), label, fill=(255, 255, 255))
    return img


def main(argv: list[str]) -> int:
    mrab, out = argv[0], argv[1]
    want = argv[2:] or None
    os.makedirs(out, exist_ok=True)
    tris, bones, cols, names, joint = load_model(mrab)
    root = joint['sz_root']
    jfile = os.path.join(out, 'joints.txt')
    with open(jfile, 'w', encoding='utf-8') as h:
        for n, at in joint.items():
            if n.startswith('sz_'):
                h.write(f'{n} {at[0] - root[0]:.4f} {at[1] - root[1]:.4f} {at[2] - root[2]:.4f}\n')
    dump = os.path.join(out, 'poses.txt')
    r = subprocess.run([CHECK, '--joints', jfile, '--dump', dump], capture_output=True, text=True)
    print(r.stdout.strip())
    frames = read_dump(dump)
    joint = {n: v - root for n, v in joint.items()}
    tris = tris - root
    for scenario in sorted({s for s, _ in frames}):
        if want and scenario not in want:
            continue
        sheet = Image.new('RGB', (SIZE * len(FRAMES), SIZE * 2))
        for k, f in enumerate(FRAMES):
            t = posed(tris, bones, names, joint, frames[(scenario, f)])
            sheet.paste(draw(t, cols, 'front', f'{scenario} {f}'), (k * SIZE, 0))
            sheet.paste(draw(t, cols, 'side', ''), (k * SIZE, SIZE))
        sheet.save(os.path.join(out, f'{scenario}.png'))
        print('wrote', os.path.join(out, f'{scenario}.png'))
    return r.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
