"""Developer-only: an X-Morph: Defense unit drawn offline, the game's own mesh beside the model folder
tools/prep_xmorph.py made of it (docs/xmorph-port.md), so the conversion is checked by eye before anything goes into the
game: the textures and their mapping, the normals and the normal map's sense, the running gear, the scale.

    python tools/xmorph_view.py OUT_DIR [unit ...] [--views side,top,front,back,quarter,rear3q] [--size PX]
                                [--mode lit|albedo|normal|bones] [--models DIR] [--game DIR] [--ranger]

Per unit OUT_DIR/<key>.png: a row per view, the source left (its packs' mesh, normals and textures), the folder right
(model.npz's parts in their materials' textures, the way pylib/wt_tracked.py builds them). Modes: lit (albedo x a sun
and the normal map: the tangent frame dP/du, dP/dv as wt_tracked.tangent_frames, the map's DirectX green), albedo (the
texture only), normal (the shading normal as colour), bones (the folder's parts coloured by bone). --ranger stands a
1.8 m figure beside it (an EDF soldier's height). A small numpy rasteriser (z-buffer, per-pixel texture): needs numpy
and Pillow; the packs are data, nothing in them runs.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'pylib')]

import obj_model  # noqa: E402
import xm_model as xm  # noqa: E402
import xm_pack as xp  # noqa: E402
import xm_units  # noqa: E402

SUN = np.array([0.45, 0.8, 0.4]) / np.linalg.norm([0.45, 0.8, 0.4])
AMBIENT = 0.35
# view -> (camera direction it looks along, its up), orthographic; EDF frame (+x left, +y up, +z front)
VIEWS = {'side': ((1.0, 0.0, 0.0), (0, 1, 0)),          # from its right side
         'top': ((0.0, -1.0, 0.0), (0, 0, 1)),
         'front': ((0.0, 0.0, -1.0), (0, 1, 0)),
         'back': ((0.0, 0.0, 1.0), (0, 1, 0)),
         'quarter': ((0.6, -0.45, -0.65), (0, 1, 0)),   # from ahead on its right, above
         'rear3q': ((-0.6, -0.45, 0.65), (0, 1, 0))}


class Mesh:
    """Triangles to draw: positions (n, 3), normals (n, 3), uvs (n, 2, top-left origin), triangles (t, 3), per
    triangle a texture index and a colour (bones mode)."""

    def __init__(self) -> None:
        self.pos, self.nrm, self.uv, self.tri, self.tex, self.col = [], [], [], [], [], []
        self.textures: list[tuple[np.ndarray, np.ndarray | None]] = []   # (albedo RGB float, normal RGB float or None)

    def add(self, pos, nrm, uv, tri, tex: int, col=(200, 200, 200)) -> None:  # noqa: ANN001
        base = sum(len(p) for p in self.pos)
        self.pos.append(np.asarray(pos, float))
        self.nrm.append(np.asarray(nrm, float))
        self.uv.append(np.asarray(uv, float))
        self.tri.append(np.asarray(tri, np.int64) + base)
        self.tex.append(np.full(len(tri), tex))
        self.col.append(np.tile(np.asarray(col, float), (len(tri), 1)))

    def done(self) -> 'Mesh':
        for k in ('pos', 'nrm', 'uv', 'tri', 'tex', 'col'):
            setattr(self, k, np.concatenate(getattr(self, k)))
        return self


def _rgb(im) -> np.ndarray:  # noqa: ANN001
    return np.asarray(im.convert('RGB'), dtype=np.float64) / 255.0


def source_mesh(packs: xp.Packs, unit: xm_units.Unit) -> Mesh:
    um = xm.load(packs, unit.entity)
    m = Mesh()
    for mat in um.materials:
        imgs = xm.texture_images(packs, mat)
        m.textures.append((_rgb(imgs['diffuse']), _rgb(imgs['normal']) if 'normal' in imgs else None))
    m.add(um.positions, um.normals, um.uvs, um.triangles, 0)
    m.tex = [um.tri_material.astype(np.int64)]
    return m.done()


BONE_COLOURS = {'body': (170, 170, 170), 'cannon_main': (220, 120, 60), 'cannon_l': (60, 160, 220),
                'cannon_r': (60, 220, 160), 'cannon_slide_l': (230, 220, 60), 'cannon_slide_r': (230, 60, 200),
                'catapi_body': (90, 60, 40)}


def folder_mesh(folder: str) -> Mesh:
    with open(os.path.join(folder, 'model.json'), encoding='utf-8') as f:
        info = json.load(f)
    arrays = np.load(os.path.join(folder, 'model.npz'))
    m = Mesh()
    keys = {}
    for mat in info['materials']:
        albedo = mat.get('albedo')
        k = (albedo, mat.get('normal'))
        if k not in keys:
            keys[k] = len(m.textures)
            nrm = _rgb(Image.open(os.path.join(folder, mat['normal']))) if mat.get('normal') else None
            m.textures.append((_rgb(Image.open(os.path.join(folder, albedo))), nrm))
    mats = [keys[(mat.get('albedo'), mat.get('normal'))] for mat in info['materials']]
    for k, p in enumerate(info['parts']):
        bone = p.get('bone') or p.get('role')
        col = BONE_COLOURS.get(bone, (80, 200, 80) if str(bone).startswith('tire') else (150, 150, 230))
        m.add(arrays[f'p{k}_pos'], arrays[f'p{k}_nrm'], arrays[f'p{k}_uv'], arrays[f'p{k}_tri'], mats[p['material']], col)
    return m.done()


def ranger(m: Mesh, at: tuple[float, float, float]) -> None:
    """A 1.8 m box figure (0.5 x 1.8 x 0.3) at `at` (its feet), untextured (texture -1)."""
    x, y, z = at
    lo, hi = np.array([x - 0.25, y, z - 0.15]), np.array([x + 0.25, y + 1.8, z + 0.15])
    c = np.array([[lo[0], lo[1], lo[2]], [hi[0], lo[1], lo[2]], [hi[0], hi[1], lo[2]], [lo[0], hi[1], lo[2]],
                  [lo[0], lo[1], hi[2]], [hi[0], lo[1], hi[2]], [hi[0], hi[1], hi[2]], [lo[0], hi[1], hi[2]]])
    t = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [3, 6, 2], [3, 7, 6],
                  [0, 4, 7], [0, 7, 3], [1, 2, 6], [1, 6, 5]])
    m.pos = np.vstack((m.pos, c))
    m.nrm = np.vstack((m.nrm, (c - c.mean(0)) / np.linalg.norm(c - c.mean(0), axis=1, keepdims=True)))
    m.uv = np.vstack((m.uv, np.zeros((8, 2))))
    m.tri = np.vstack((m.tri, t + len(m.pos) - 8))
    m.tex = np.concatenate((m.tex, np.full(12, -1)))
    m.col = np.vstack((m.col, np.tile([40.0, 90.0, 200.0], (12, 1))))


def _sample(img: np.ndarray, uv: np.ndarray) -> np.ndarray:
    """Bilinear, wrapping; uv top-left origin."""
    h, w = img.shape[:2]
    x = (uv[:, 0] % 1.0) * w - 0.5
    y = (uv[:, 1] % 1.0) * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    x0, y0, x1, y1 = x0 % w, y0 % h, (x0 + 1) % w, (y0 + 1) % h
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy) + img[y1, x0] * (1 - fx) * fy
            + img[y1, x1] * fx * fy)


def render(m: Mesh, view: str, size: int, frame: tuple[np.ndarray, float], mode: str) -> Image.Image:
    d, up = (np.asarray(v, float) for v in VIEWS[view])
    d /= np.linalg.norm(d)
    right = np.cross(d, up)
    right /= np.linalg.norm(right)
    upv = np.cross(right, d)
    centre, half = frame
    rel = m.pos - centre
    sx = (rel @ right / half * 0.5 + 0.5) * size
    sy = (0.5 - rel @ upv / half * 0.5) * size
    depth = rel @ d
    zbuf = np.full((size, size), np.inf)
    out = np.full((size, size, 3), 0.92)
    # per-triangle tangent frames (dP/du, dP/dv)
    P, U, T = m.pos, m.uv, m.tri
    e1, e2 = P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]]
    d1, d2 = U[T[:, 1]] - U[T[:, 0]], U[T[:, 2]] - U[T[:, 0]]
    det = d1[:, 0] * d2[:, 1] - d1[:, 1] * d2[:, 0]
    det = np.where(np.abs(det) < 1e-12, 1e-12, det)
    tan = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / det[:, None]
    bit = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / det[:, None]
    for i, (a, b, c) in enumerate(T):
        xs, ys = sx[[a, b, c]], sy[[a, b, c]]
        x0, x1 = max(int(np.floor(xs.min())), 0), min(int(np.ceil(xs.max())), size - 1)
        y0, y1 = max(int(np.floor(ys.min())), 0), min(int(np.ceil(ys.max())), size - 1)
        if x1 < x0 or y1 < y0:
            continue
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not inside.any():
            continue
        z = w0 * depth[a] + w1 * depth[b] + w2 * depth[c]
        py, px = np.nonzero(inside)
        z = z[inside]
        win = z < zbuf[py + y0, px + x0]
        if not win.any():
            continue
        py, px, z = py[win] + y0, px[win] + x0, z[win]
        wa, wb, wc = (w[inside][win][:, None] for w in (w0, w1, w2))
        zbuf[py, px] = z
        n = wa * m.nrm[a] + wb * m.nrm[b] + wc * m.nrm[c]
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        tex = int(m.tex[i])
        if tex < 0 or mode == 'bones':
            col = np.tile(m.col[i] / 255.0, (len(py), 1))
        else:
            uv = wa * U[a] + wb * U[b] + wc * U[c]
            albedo, nmap = m.textures[tex]
            col = _sample(albedo, uv)
            if nmap is not None and mode in ('lit', 'normal'):
                tn = _sample(nmap, uv) * 2 - 1
                t_ = tan[i] - n * (n @ tan[i])[:, None]
                t_ /= np.maximum(np.linalg.norm(t_, axis=1, keepdims=True), 1e-12)
                b_ = bit[i] - n * (n @ bit[i])[:, None]
                b_ /= np.maximum(np.linalg.norm(b_, axis=1, keepdims=True), 1e-12)
                n = tn[:, 0:1] * t_ + tn[:, 1:2] * b_ + tn[:, 2:3] * n
                n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        if mode == 'normal':
            out[py, px] = n * 0.5 + 0.5
        elif mode == 'albedo':
            out[py, px] = col
        else:
            out[py, px] = col * (AMBIENT + (1 - AMBIENT) * np.clip(n @ SUN, 0, 1))[:, None]
    # the ground line (y = 0) in the side / front / back views
    img = Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8))
    if view in ('side', 'front', 'back'):
        gy = (0.5 - (0 - centre[1]) / half * 0.5) * size
        ImageDraw.Draw(img).line([(0, gy), (size, gy)], fill=(255, 0, 0))
    return img


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('units', nargs='*')
    ap.add_argument('--views', default='side,top,front,quarter')
    ap.add_argument('--size', type=int, default=640)
    ap.add_argument('--mode', default='lit', choices=('lit', 'albedo', 'normal', 'bones'))
    ap.add_argument('--models', default=obj_model.DEV_MODELS)
    ap.add_argument('--game', default=xp.STEAM_DIR)
    ap.add_argument('--ranger', action='store_true')
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    units = [u for u in xm_units.ported() if not a.units or u.key in a.units or u.key[3:] in a.units]
    packs = xp.Packs(a.game)
    try:
        for u in units:
            folder = os.path.join(a.models, xm_units.folder(u.key))
            if not os.path.isfile(os.path.join(folder, 'model.json')):
                print(f'{u.key}: no folder in {a.models}', flush=True)
                continue
            src = source_mesh(packs, u)
            dst = folder_mesh(folder)
            lo, hi = np.minimum(src.pos.min(0), dst.pos.min(0)), np.maximum(src.pos.max(0), dst.pos.max(0))
            if a.ranger:
                for m in (src, dst):
                    ranger(m, (float(lo[0]) - 1.0, 0.0, float(hi[2]) - 1.0))
                lo[0] -= 1.5
            frame = ((lo + hi) / 2, float((hi - lo).max()) * 0.55)
            views = a.views.split(',')
            sheet = Image.new('RGB', (2 * a.size, len(views) * a.size), (255, 255, 255))
            for r, v in enumerate(views):
                sheet.paste(render(src, v, a.size, frame, a.mode), (0, r * a.size))
                sheet.paste(render(dst, v, a.size, frame, a.mode), (a.size, r * a.size))
                ImageDraw.Draw(sheet).text((4, r * a.size + 4), f'{u.key} {v}: source | folder', fill=(0, 0, 0))
            path = os.path.join(a.out, f'{u.key}_{a.mode}.png')
            sheet.save(path)
            print(path, flush=True)
    finally:
        packs.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
