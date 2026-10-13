"""Offline first-person preview: what an eye inside (or on) a vehicle sees, drawn in perspective from the model in
Root.cpk or a built archive, so cockpit models and eye positions can be checked without the game. Read only; writes
only --out.

    python tools/cabin_view.py ARCHIVE MODEL.mdb --eye x,y,z --look x,y,z --out out.png
        [--bone NAME --eye-local x,y,z]   eye = eye_local x the bone's bind world; no --look: along the bone's +z row
        [--up x,y,z | --bone-up]          the camera's up (world +y by default; --bone-up: the bone's +y row, so it rolls)
        [--fov 70] [--size 1280x720] [--near 0.02] [--cull | --mark-back] [--no-ground]
        [--pose "bone=axis:deg,..."]      the main model's pose, as pylib/model_view.py --pose
        [--color @tex|flat|@mat|PREFIX,..] (default @tex: each pixel in its material's albedo texture)
        [--extra ARCHIVE2 MODEL2.mdb [--extra-offset x,y,z] [--extra-bone HOSTBONE]] ...
    python tools/cabin_view.py ARCHIVE MODEL.mdb --list-bones

ARCHIVE: a file on disk, else OBJECT/<ARCHIVE> in Root.cpk (pylib/model_view.py load). Game axes: +y up, +z the
model's nose, +x its left, metres; matrices are row vectors (p' = p x M, rows right, up, forward, position).

Each --extra model is drawn in the same frame; the n-th --extra-offset / --extra-bone belong to the n-th --extra:
p' = (p + offset) x the host (main model's) bone's bind world (no --extra-bone: p + offset in the model frame).

Rendering: triangles are clipped against the near plane (never dropped for being close), rasterised with a per-pixel
z-buffer (1/z, perspective correct), then shaded per pixel: the albedo texture at the perspective-correct UV (or the
interpolated vertex colour), times a two-sided Lambert term from a fixed light. --cull drops the faces turned away
from the eye as the game does (its hull is invisible from inside); without it both sides draw, so interiors show
(--mark-back tints those back faces magenta). Vectors may be negative: --eye -8,5,12 works.
--fov is the vertical field of view in degrees. A thin crosshair marks the screen centre.
"""
from __future__ import annotations

import argparse
import functools
import io
import math
import os
import sys
import time
from dataclasses import dataclass, field

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import model_view as mv  # noqa: E402
from mdb import Mdb, bind_world, mdb_read, rab_read, read_elem  # noqa: E402

LIGHT = np.array([0.35, 0.8, -0.45]) / np.linalg.norm([0.35, 0.8, -0.45])
# Front faces: cross(b - a, c - a) x WINDING points out of the hull. Measured on the stock models: the signed volume
# sum(a . (b x c)) / 6 is positive for every one tried (403 tank 37.6, 505 tank 28.7, 506 heli 16.4, 409 heli 20.7,
# 503 bike 1.1 m^3), so the corners run counter-clockwise seen from outside in these (x, y, z) coordinates.
WINDING = 1.0
GROUND_TEX = -2    # the ground plane's texture id: a 1 m / 10 m checker by world position
NO_TEX = -1
CHUNK = 2_000_000  # candidate pixels a rasterising step tests at once (memory bound)


@dataclass
class Scene:
    """World-space triangles with what shading needs: per vertex colour and UV, per triangle texture id."""
    pos: np.ndarray = field(default_factory=lambda: np.zeros((0, 3)))
    tris: np.ndarray = field(default_factory=lambda: np.zeros((0, 3), dtype=np.int64))
    vcol: np.ndarray = field(default_factory=lambda: np.zeros((0, 3)))
    uv: np.ndarray = field(default_factory=lambda: np.zeros((0, 2)))
    tex: np.ndarray = field(default_factory=lambda: np.zeros(0, dtype=np.int64))
    textures: list[np.ndarray] = field(default_factory=list)

    def add(self, pos: np.ndarray, tris: np.ndarray, vcol: np.ndarray, uv: np.ndarray, tex: np.ndarray,
            textures: list[np.ndarray]) -> None:
        base, tbase = len(self.pos), len(self.textures)
        self.pos = np.vstack([self.pos, pos])
        self.tris = np.vstack([self.tris, tris + base])
        self.vcol = np.vstack([self.vcol, vcol])
        self.uv = np.vstack([self.uv, uv])
        self.tex = np.concatenate([self.tex, np.where(tex >= 0, tex + tbase, tex)])
        self.textures.extend(textures)


@functools.lru_cache(maxsize=None)
def load(archive: str, model: str) -> Mdb:
    """model_view.load, decoding only the albedo textures (the normal/roughness maps cost seconds a model and are
    never drawn) and once per (archive, model) however many --extra copies name it."""
    if os.path.isfile(archive):
        with open(archive, 'rb') as h:
            raw = h.read()
    else:
        import rootcpk
        raw = rootcpk.default().read('OBJECT', archive)
    rab = rab_read(raw)
    hit = next((f for f in rab.files if f.name.lower() == model.lower()), None)
    if hit is None:
        raise SystemExit(f'{archive} has no {model}: ' + ', '.join(f.name for f in rab.files if f.name.lower().endswith('.mdb')))
    md = mdb_read(hit.data)
    wanted = {md.textures[t.texture].filename.lower() for m in md.materials for t in m.textures
              if t.kind.lower() == 'albedo' and 0 <= t.texture < len(md.textures)}
    textures = {}
    for f in rab.files:
        if f.name.lower() in wanted:
            try:
                im = Image.open(io.BytesIO(f.data))
                textures[f.name.lower()] = im.convert('RGB').resize((512, 512))
            except Exception:   # a format PIL cannot read: that material keeps its plain colour
                pass
    mv.TEXTURES[id(md)] = textures
    return md


def vec3(text: str) -> np.ndarray:
    v = [float(x) for x in text.split(',')]
    if len(v) != 3:
        raise argparse.ArgumentTypeError(f'want x,y,z: {text!r}')
    return np.array(v)


def bone_world(md: Mdb, name: str) -> np.ndarray:
    """The 4x4 bind world of the bone called `name` (case-insensitive)."""
    names = [md.name_of(b.name).lower() for b in md.bones]
    if name.lower() not in names:
        raise SystemExit(f'no bone {name!r} (try --list-bones)')
    return np.array(bind_world(md)[names.index(name.lower())], dtype=np.float64).reshape(4, 4)


def mesh_uvs(md: Mdb, use_textures: bool) -> tuple[np.ndarray, np.ndarray, list[np.ndarray]]:
    """(per-vertex UV (NaN without one), per-triangle texture id, the textures), walking the meshes in
    model_view.geometry's order so the rows line up with its vertices and triangles."""
    uvs, tex, images, ids = [], [], [], {}
    for ob in md.objects:
        for me in ob.meshes:
            e = next(x for x in me.elems if x.name.lower() == 'position')
            n = len(read_elem(me, e.name, e.channel))
            ntri = len(me.indices) // 6
            raw = read_elem(me, next((x.name for x in me.elems if x.name.lower() == 'texcoord' and x.channel == 0),
                                     ''), 0)
            image = mv.albedo(md, me.material) if use_textures else None
            if raw and image is not None:
                uvs.append(np.array(raw, dtype=np.float64)[:, :2])
                if id(image) not in ids:
                    ids[id(image)] = len(images)
                    images.append(np.asarray(image, dtype=np.float32))
                tex.append(np.full(ntri, ids[id(image)]))
            else:
                uvs.append(np.full((n, 2), np.nan))
                tex.append(np.full(ntri, NO_TEX))
    return np.vstack(uvs), np.concatenate(tex).astype(np.int64), images


def model_scene(scene: Scene, md: Mdb, colour: str, pose: dict, place: np.ndarray | None) -> int:
    """Adds `md` (posed, placed by the 4x4 `place` if given) to `scene`; returns its triangle count."""
    use_tex = colour == '@tex'
    prefixes = [] if colour in ('flat', '') else [s for s in colour.split(',') if s]
    v, t, c = mv.geometry(md, pose, prefixes)
    uv, tex, images = mesh_uvs(md, use_tex)
    assert len(uv) == len(v) and len(tex) == len(t), 'mesh walk out of step with model_view.geometry'
    if place is not None:
        v = np.hstack([v, np.ones((len(v), 1))]) @ place
        v = v[:, :3]
    scene.add(v, t, c, uv, tex, images)
    return len(t)


def ground_scene(scene: Scene, y: float = -0.005, half: float = 400.0) -> None:
    p = np.array([[-half, y, -half], [half, y, -half], [half, y, half], [-half, y, half]])
    t = np.array([[0, 2, 1], [0, 3, 2]])   # facing up (WINDING)
    scene.add(p, t, np.full((4, 3), 90.0), np.full((4, 2), np.nan), np.array([GROUND_TEX, GROUND_TEX]), [])


def camera(eye: np.ndarray, look: np.ndarray, up: np.ndarray) -> np.ndarray:
    """3x3 rows (right, up, forward) of the camera looking from `eye` at `look`; screen right = forward x up."""
    f = look - eye
    if np.linalg.norm(f) < 1e-9:
        raise SystemExit('--look equals --eye')
    f = f / np.linalg.norm(f)
    r = np.cross(f, up)
    if np.linalg.norm(r) < 1e-6:   # looking straight along `up`: any right will do
        r = np.cross(f, np.array([0.0, 0.0, 1.0]) if abs(f[2]) < 0.9 else np.array([1.0, 0.0, 0.0]))
    r /= np.linalg.norm(r)
    return np.stack([r, np.cross(r, f), f])


def clip_near(v: np.ndarray, near: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Clips camera-space triangles v (M, 3 corners, xyz; z forward) against z = near, keeping the winding.
    Returns (corners (K, 3, 3), each corner's barycentric weights in its source triangle (K, 3, 3), source index)."""
    m = len(v)
    bary = np.broadcast_to(np.eye(3), (m, 3, 3))
    inside = v[:, :, 2] >= near
    cnt = inside.sum(axis=1)
    src = np.arange(m)
    out_v, out_b, out_s = [v[cnt == 3]], [bary[cnt == 3]], [src[cnt == 3]]

    def rolled(sel: np.ndarray, first: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        order = (first[:, None] + np.arange(3)[None, :]) % 3
        return (np.take_along_axis(v[sel], order[:, :, None], axis=1),
                np.take_along_axis(bary[sel], order[:, :, None], axis=1))

    def cut(pa: np.ndarray, ba: np.ndarray, pb: np.ndarray, bb: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        t = ((near - pa[:, 2]) / (pb[:, 2] - pa[:, 2]))[:, None]
        return pa + t * (pb - pa), ba + t * (bb - ba)

    sel = np.nonzero(cnt == 1)[0]   # one corner in front: one smaller triangle
    if len(sel):
        rv, rb = rolled(sel, inside[sel].argmax(axis=1))
        a, ab = cut(rv[:, 0], rb[:, 0], rv[:, 1], rb[:, 1])
        b, bb = cut(rv[:, 0], rb[:, 0], rv[:, 2], rb[:, 2])
        out_v.append(np.stack([rv[:, 0], a, b], 1))
        out_b.append(np.stack([rb[:, 0], ab, bb], 1))
        out_s.append(sel)
    sel = np.nonzero(cnt == 2)[0]   # one corner behind: a quad, two triangles
    if len(sel):
        rv, rb = rolled(sel, (~inside[sel]).argmax(axis=1))
        a, ab = cut(rv[:, 1], rb[:, 1], rv[:, 0], rb[:, 0])   # on the edge behind -> first in front
        b, bb = cut(rv[:, 2], rb[:, 2], rv[:, 0], rb[:, 0])   # on the edge second in front -> behind
        out_v += [np.stack([a, rv[:, 1], rv[:, 2]], 1), np.stack([a, rv[:, 2], b], 1)]
        out_b += [np.stack([ab, rb[:, 1], rb[:, 2]], 1), np.stack([ab, rb[:, 2], bb], 1)]
        out_s += [sel, sel]
    return np.concatenate(out_v), np.concatenate(out_b), np.concatenate(out_s)


def rasterise(q: np.ndarray, iz: np.ndarray, w: int, h: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Z-buffers screen triangles q (K, 3, 2 pixels) with 1/z per corner iz (K, 3). Returns per pixel (h*w):
    the nearest 1/z (0 = empty), that triangle's index (-1 = empty) and its screen-space barycentrics (h*w, 3).
    Triangles are batched by their clamped bounding box size (powers of two), so each batch is one numpy step."""
    zbuf = np.zeros(w * h)
    kbuf = np.full(w * h, -1, dtype=np.int64)
    lbuf = np.zeros((w * h, 3), dtype=np.float32)
    area = (q[:, 1, 0] - q[:, 0, 0]) * (q[:, 2, 1] - q[:, 0, 1]) - (q[:, 2, 0] - q[:, 0, 0]) * (q[:, 1, 1] - q[:, 0, 1])
    x0 = np.clip(np.ceil(q[:, :, 0].min(1) - 0.5), 0, w).astype(np.int64)
    x1 = np.clip(np.floor(q[:, :, 0].max(1) - 0.5), -1, w - 1).astype(np.int64)
    y0 = np.clip(np.ceil(q[:, :, 1].min(1) - 0.5), 0, h).astype(np.int64)
    y1 = np.clip(np.floor(q[:, :, 1].max(1) - 0.5), -1, h - 1).astype(np.int64)
    live = (np.abs(area) > 1e-12) & (x1 >= x0) & (y1 >= y0)
    bw = np.where(live, x1 - x0 + 1, 1)
    bh = np.where(live, y1 - y0 + 1, 1)
    cw = np.ceil(np.log2(bw)).astype(np.int64)
    ch = np.ceil(np.log2(bh)).astype(np.int64)
    key = cw * 64 + ch
    for kk in np.unique(key[live]):
        sw, sh = 1 << int(kk // 64), 1 << int(kk % 64)
        dx = np.tile(np.arange(sw), sh)
        dy = np.repeat(np.arange(sh), sw)
        group = np.nonzero(live & (key == kk))[0]
        step = max(1, CHUNK // (sw * sh))
        for s in range(0, len(group), step):
            g = group[s:s + step]
            xs = x0[g, None] + dx[None, :]
            ys = y0[g, None] + dy[None, :]
            ok = (xs <= x1[g, None]) & (ys <= y1[g, None])
            px, py = xs + 0.5, ys + 0.5
            a, b, c = q[g, 0], q[g, 1], q[g, 2]
            inv = 1.0 / area[g, None]
            l0 = ((b[:, 0, None] - px) * (c[:, 1, None] - py) - (c[:, 0, None] - px) * (b[:, 1, None] - py)) * inv
            l1 = ((c[:, 0, None] - px) * (a[:, 1, None] - py) - (a[:, 0, None] - px) * (c[:, 1, None] - py)) * inv
            l2 = 1.0 - l0 - l1
            ok &= (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            ti, ci = np.nonzero(ok)
            if not len(ti):
                continue
            pix = ys[ti, ci] * w + xs[ti, ci]
            L = np.stack([l0[ti, ci], l1[ti, ci], l2[ti, ci]], 1)
            z = (L * iz[g[ti]]).sum(1)   # 1/z is linear in screen space
            near = z > zbuf[pix]
            pix, L, z, ti = pix[near], L[near], z[near], ti[near]
            np.maximum.at(zbuf, pix, z)
            win = z >= zbuf[pix]
            kbuf[pix[win]] = g[ti[win]]
            lbuf[pix[win]] = L[win]
    return zbuf, kbuf, lbuf


def sample(img: np.ndarray, uv: np.ndarray) -> np.ndarray:
    """Bilinear, wrapping texture lookup (uv in [0, 1) repeating, v down as model_view reads it)."""
    th, tw = img.shape[:2]
    x = np.mod(uv[:, 0], 1.0) * tw - 0.5
    y = np.mod(uv[:, 1], 1.0) * th - 0.5
    xi, yi = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - xi)[:, None], (y - yi)[:, None]
    x0, x1, y0, y1 = xi % tw, (xi + 1) % tw, yi % th, (yi + 1) % th
    return ((img[y0, x0] * (1 - fx) + img[y0, x1] * fx) * (1 - fy) + (img[y1, x0] * (1 - fx) + img[y1, x1] * fx) * fy)


def render(scene: Scene, eye: np.ndarray, look: np.ndarray, up: np.ndarray, fov: float, w: int, h: int, near: float,
           cull: bool, mark_back: bool = False) -> tuple[Image.Image, dict]:
    """The frame from `eye` towards `look`, and counts/timings for the label."""
    t0 = time.perf_counter()
    cam = camera(eye, look, up)
    tri_w = scene.pos[scene.tris]                                   # (M, 3, 3) world
    n = np.cross(tri_w[:, 1] - tri_w[:, 0], tri_w[:, 2] - tri_w[:, 0]) * WINDING
    ln = np.linalg.norm(n, axis=1)
    n /= np.where(ln > 0, ln, 1.0)[:, None]
    front = ((eye - tri_w[:, 0]) * n).sum(1) > 0
    keep = front if cull else np.ones(len(tri_w), dtype=bool)
    src_all = np.nonzero(keep)[0]
    vc = (tri_w[src_all] - eye) @ cam.T                             # camera space: x right, y up, z forward
    cv, cb, cs = clip_near(vc, near)
    cs = src_all[cs]
    k = (h / 2) / math.tan(math.radians(fov) / 2)
    q = np.stack([w / 2 + k * cv[:, :, 0] / cv[:, :, 2], h / 2 - k * cv[:, :, 1] / cv[:, :, 2]], -1)
    iz = 1.0 / cv[:, :, 2]
    t1 = time.perf_counter()
    zbuf, kbuf, lbuf = rasterise(q, iz, w, h)
    t2 = time.perf_counter()

    # shading: the sky, then every covered pixel from its source triangle's attributes
    rows = np.linspace(0.0, 1.0, h)[:, None]
    sky = (np.array([120.0, 150.0, 190.0]) * (1 - rows) + np.array([190.0, 205.0, 220.0]) * rows)
    img = np.broadcast_to(sky[:, None, :], (h, w, 3)).reshape(-1, 3).copy()
    pix = np.nonzero(kbuf >= 0)[0]
    kk = kbuf[pix]
    wgt = lbuf[pix].astype(np.float64) * iz[kk]                    # perspective correct
    wgt /= wgt.sum(1, keepdims=True)
    ob = np.einsum('ni,nij->nj', wgt, cb[kk])                       # weights in the source triangle
    src = cs[kk]
    corners = scene.tris[src]
    col = (ob[:, :, None] * scene.vcol[corners]).sum(1)
    tex = scene.tex[src]
    for t in np.unique(tex[tex >= 0]):
        sel = tex == t
        uv = (ob[sel, :, None] * scene.uv[corners[sel]]).sum(1)
        col[sel] = sample(scene.textures[t], uv)
    sel = tex == GROUND_TEX
    if sel.any():
        p = (ob[sel, :, None] * scene.pos[corners[sel]]).sum(1)
        fine = (np.floor(p[:, 0]) + np.floor(p[:, 2])) % 2
        coarse = (np.floor(p[:, 0] / 10) + np.floor(p[:, 2] / 10)) % 2
        col[sel] = np.array([96.0, 118.0, 84.0])[None] * (0.85 + 0.1 * fine[:, None] + 0.1 * coarse[:, None])
    lam = 0.35 + 0.65 * np.abs(n[src] @ LIGHT)
    lam[sel] = 1.0
    col = col * lam[:, None]
    if mark_back:   # the faces the game would not draw from here, tinted magenta
        back = ~front[src] & ~sel
        col[back] = col[back] * 0.5 + np.array([255.0, 0.0, 200.0]) * 0.5
    img[pix] = col
    out = Image.fromarray(img.reshape(h, w, 3).clip(0, 255).astype(np.uint8))
    t3 = time.perf_counter()
    stats = {'tris': len(tri_w), 'kept': len(src_all), 'clipped': len(cv), 'covered': len(pix) / (w * h),
             'setup': t1 - t0, 'raster': t2 - t1, 'shade': t3 - t2}
    return out, stats


def crosshair(img: Image.Image) -> None:
    d = ImageDraw.Draw(img)
    cx, cy = img.width // 2, img.height // 2
    for (ax, ay, bx, by) in ((cx - 14, cy, cx - 4, cy), (cx + 4, cy, cx + 14, cy), (cx, cy - 14, cx, cy - 4),
                             (cx, cy + 4, cx, cy + 14)):
        d.line([(ax + 1, ay + 1), (bx + 1, by + 1)], fill=(0, 0, 0))
        d.line([(ax, ay), (bx, by)], fill=(255, 255, 255))
    d.point((cx, cy), fill=(255, 60, 60))


def list_bones(md: Mdb) -> None:
    w = bind_world(md)
    for b in md.bones:
        m = w[b.index]
        print(f'[{b.index:3}] {md.name_of(b.name):<28} parent {b.parent:3}  at ({m[12]:+7.3f},{m[13]:+7.3f},{m[14]:+7.3f})'
              f'  fwd ({m[8]:+.2f},{m[9]:+.2f},{m[10]:+.2f})')


VEC_OPTIONS = ('--eye', '--look', '--eye-local', '--up', '--extra-offset')


def join_negative_vectors(argv: list[str]) -> list[str]:
    """'--eye -8,5,12' -> '--eye=-8,5,12': argparse takes a value starting with '-' for an option otherwise."""
    out: list[str] = []
    i = 0
    while i < len(argv):
        if argv[i] in VEC_OPTIONS and i + 1 < len(argv) and argv[i + 1].startswith('-') and ',' in argv[i + 1]:
            out.append(f'{argv[i]}={argv[i + 1]}')
            i += 2
        else:
            out.append(argv[i])
            i += 1
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('archive')
    ap.add_argument('model')
    ap.add_argument('--list-bones', action='store_true')
    ap.add_argument('--eye', type=vec3)
    ap.add_argument('--look', type=vec3)
    ap.add_argument('--bone')
    ap.add_argument('--eye-local', type=vec3, default=np.zeros(3))
    ap.add_argument('--up', type=vec3, default=np.array([0.0, 1.0, 0.0]))
    ap.add_argument('--bone-up', action='store_true', help="the camera's up is the bone's +y row")
    ap.add_argument('--fov', type=float, default=70.0, help='vertical, degrees')
    ap.add_argument('--size', default='1280x720')
    ap.add_argument('--near', type=float, default=0.02)
    ap.add_argument('--pose', default='')
    ap.add_argument('--color', default='@tex')
    ap.add_argument('--cull', action='store_true')
    ap.add_argument('--no-ground', action='store_true')
    ap.add_argument('--mark-back', action='store_true', help='tint back faces magenta (without --cull)')
    ap.add_argument('--extra', nargs=2, action='append', default=[], metavar=('ARCHIVE', 'MODEL'))
    ap.add_argument('--extra-offset', type=vec3, action='append', default=[])
    ap.add_argument('--extra-bone', action='append', default=[])
    ap.add_argument('--out')
    a = ap.parse_args(join_negative_vectors(argv))
    md = load(a.archive, a.model)
    if a.list_bones:
        list_bones(md)
        return 0
    if not a.out:
        ap.error('--out is required')
    if a.bone:
        m = bone_world(md, a.bone)
        eye = np.append(a.eye_local, 1.0) @ m
        eye = eye[:3]
        look = a.look if a.look is not None else eye + m[2, :3] / np.linalg.norm(m[2, :3])
        up = m[1, :3] / np.linalg.norm(m[1, :3]) if a.bone_up else a.up
    else:
        if a.eye is None or a.look is None:
            ap.error('give --eye and --look, or --bone [--eye-local] [--look]')
        eye, look, up = a.eye, a.look, a.up
    if len(a.extra_offset) > len(a.extra) or len(a.extra_bone) > len(a.extra):
        ap.error('more --extra-offset/--extra-bone than --extra')
    w, h = (int(x) for x in a.size.lower().split('x'))
    t0 = time.perf_counter()
    scene = Scene()
    model_scene(scene, md, a.color, mv.parse_pose(a.pose), None)
    for i, (arch, name) in enumerate(a.extra):
        place = np.eye(4)
        if i < len(a.extra_offset):
            place[3, :3] = a.extra_offset[i]
        if i < len(a.extra_bone) and a.extra_bone[i] not in ('', '-'):
            place = place @ bone_world(md, a.extra_bone[i])
        model_scene(scene, load(arch, name), a.color, {}, place)
    if not a.no_ground:
        ground_scene(scene)
    t_load = time.perf_counter() - t0
    img, st = render(scene, eye, look, up, a.fov, w, h, a.near, a.cull, a.mark_back)
    crosshair(img)
    d = ImageDraw.Draw(img)
    label = (f'{a.model}{" +" + str(len(a.extra)) if a.extra else ""}  eye ({eye[0]:+.2f},{eye[1]:+.2f},{eye[2]:+.2f})'
             f'  look ({look[0]:+.2f},{look[1]:+.2f},{look[2]:+.2f})  fov {a.fov:g}  {"cull" if a.cull else "no cull"}')
    d.text((7, 5), label, fill=(0, 0, 0))
    d.text((6, 4), label, fill=(255, 255, 255))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    img.save(a.out)
    print(f'wrote {a.out}: {st["tris"]} tris, {st["kept"]} after cull, {st["clipped"]} after clip, '
          f'{st["covered"] * 100:.0f}% covered; load {t_load:.2f}s setup {st["setup"]:.2f}s '
          f'raster {st["raster"]:.2f}s shade {st["shade"]:.2f}s')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
