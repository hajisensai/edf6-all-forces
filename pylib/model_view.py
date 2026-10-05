"""Offline model viewer: renders an MDB from Root.cpk (or a built archive) posed, as a PNG, to check how the plugin's
bone poses (src/jet_swarm.cpp SwarmPose) will look before trying them in the game. Read only; writes only --out.

    python pylib/model_view.py E606_SHELLFISH.MRAB e606_shellfish.mdb --out build/view.png
        [--pose "fin_rollA_l=z:30,fin_rollA_r=z:-30" --pose "..."]   one row of views per --pose (none: the bind pose)
        [--color fin_,tail,upper_lid,gun] [--bones] [--views top,front,side,iso] [--size 360]

A pose turns a bone in its own frame before its bind local: local' = R(axis, deg) x local (row vectors), the
same as the plugin writes a bone record (+0x70) and the engine makes world = local x parent's world each frame.
Skinning: v' = sum of w x (v x inv_bind x world') over the vertex's bones (skinned meshes are stored in model
space); a rigid mesh follows its object's bone. The model's +x is its right, +y up, +z its nose.
--color tints the vertices whose strongest bone's name starts with one of the prefixes (one colour each).
--bones prints every bone's local axes (the rows of its local 3x3) and bind position, to pick the hinge axes.
"""
from __future__ import annotations

import argparse
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mdb import Mdb, bind_world, mdb_read, mmul, rab_read, read_elem  # noqa: E402

PALETTE = [(230, 90, 60), (70, 160, 230), (90, 200, 110), (230, 190, 60), (190, 100, 220), (60, 210, 210)]
BASE = (175, 175, 168)
# --color @mat: each vertex in its material's colour (by the material's name; others BASE), roughly as it shows.
MATERIAL_COLOURS = {'gold': (214, 172, 72), 'copper': (186, 112, 70), 'metal': (162, 168, 178), 'matblack': (44, 44, 50),
                    'translucent': (90, 165, 255)}


def rot(axis: str, deg: float) -> list[float]:
    """A rotation of `deg` about the local `axis` (x, y, z), row-vector convention."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    if axis == 'x':
        r = [1, 0, 0, 0, c, s, 0, -s, c]
    elif axis == 'y':
        r = [c, 0, -s, 0, 1, 0, s, 0, c]
    else:
        r = [c, s, 0, -s, c, 0, 0, 0, 1]
    return [r[0], r[1], r[2], 0.0, r[3], r[4], r[5], 0.0, r[6], r[7], r[8], 0.0, 0.0, 0.0, 0.0, 1.0]


def parse_pose(text: str) -> dict[str, list[tuple[str, float]]]:
    """'bone=axis:deg[,bone=axis:deg...]' (a bone may come twice: the turns apply in order)."""
    out: dict[str, list[tuple[str, float]]] = {}
    for part in filter(None, (p.strip() for p in text.split(','))):
        name, spec = part.split('=')
        axis, deg = spec.split(':')
        out.setdefault(name.strip(), []).append((axis.strip().lower(), float(deg)))
    return out


def posed_world(md: Mdb, pose: dict[str, list[tuple[str, float]]],
                locals_: dict[str, list[float]] | None = None) -> list[list[float]]:
    """Every bone's world: its local (or the one `locals_` gives it: what a program wrote into its record), turned
    by `pose`, times its parent's world."""
    w: list[list[float]] = []
    for b in md.bones:
        local = (locals_ or {}).get(md.name_of(b.name), b.local)
        for axis, deg in pose.get(md.name_of(b.name), []):
            local = mmul(rot(axis, deg), local)
        w.append(local if b.parent < 0 else mmul(local, w[b.parent]))
    return w


def geometry(md: Mdb, pose: dict[str, list[tuple[str, float]]], colour_prefixes: list[str],
             locals_: dict[str, list[float]] | None = None):
    """(vertices N x 3, triangles M x 3, per-vertex colour N x 3) of the posed model."""
    world = [np.array(m, dtype=np.float64).reshape(4, 4) for m in posed_world(md, pose, locals_)]
    inv = [np.array(b.inv_bind, dtype=np.float64).reshape(4, 4) for b in md.bones]
    skin = [ib @ wd for ib, wd in zip(inv, world)]
    verts, tris, cols = [], [], []
    base = 0
    for ob in md.objects:
        for me in ob.meshes:
            e = next(x for x in me.elems if x.name.lower() == 'position')
            p = np.array(read_elem(me, e.name, e.channel), dtype=np.float64)[:, :3]
            p4 = np.hstack([p, np.ones((len(p), 1))])
            bi = read_elem(me, next((x.name for x in me.elems if x.name.upper() == 'BLENDINDICES'), ''), 0)
            bw = read_elem(me, next((x.name for x in me.elems if x.name.upper() == 'BLENDWEIGHT'), ''), 0)
            if me.flags[1] and bi and bw:
                bi_a, bw_a = np.array(bi), np.array(bw, dtype=np.float64)
                out = np.zeros_like(p4)
                for k in range(4):
                    m = np.stack([skin[i] for i in bi_a[:, k]])
                    out += bw_a[:, k:k + 1] * np.einsum('ni,nij->nj', p4, m)
                owner = bi_a[np.arange(len(bi_a)), bw_a.argmax(axis=1)]
            else:   # a rigid mesh: bone-local positions through the object bone's world
                out = p4 @ world[ob.bone]
                owner = np.full(len(p4), ob.bone)
            names = [md.name_of(md.bones[i].name) for i in range(len(md.bones))]
            c = np.tile(np.array(BASE, dtype=np.float64), (len(p4), 1))
            by_material = colour_prefixes == ['@mat']
            if by_material:
                mat = md.name_of(md.materials[me.material].name).lower() if me.material < len(md.materials) else ''
                c[:] = MATERIAL_COLOURS.get(mat, BASE)
            for n, prefix in enumerate([] if by_material else colour_prefixes):
                hit = np.array([names[o].startswith(prefix) for o in owner])
                c[hit] = PALETTE[n % len(PALETTE)]
            idx = np.frombuffer(me.indices, dtype='<u2').reshape(-1, 3).astype(np.int64)
            verts.append(out[:, :3])
            tris.append(idx + base)
            cols.append(c)
            base += len(p4)
    return np.vstack(verts), np.vstack(tris), np.vstack(cols)


VIEWS = {   # (right, up, toward the viewer) in model axes: +x right, +y up, +z nose
    'top': ((1, 0, 0), (0, 0, 1), (0, 1, 0)),
    'front': ((-1, 0, 0), (0, 1, 0), (0, 0, 1)),
    'side': ((0, 0, 1), (0, 1, 0), (1, 0, 0)),
    'iso': None,
}


def view_axes(name: str):
    if VIEWS[name] is not None:
        return [np.array(a, dtype=np.float64) for a in VIEWS[name]]
    eye = np.array([0.8, 0.55, 1.0])   # front-right, above
    eye /= np.linalg.norm(eye)
    right = np.cross(np.array([0.0, 1.0, 0.0]), eye)
    right /= np.linalg.norm(right)
    up = np.cross(eye, right)
    return [right, up, eye]


def render(v: np.ndarray, t: np.ndarray, c: np.ndarray, view: str, size: int, extent: float, label: str) -> Image.Image:
    right, up, toward = view_axes(view)
    x, y, z = v @ right, v @ up, v @ toward
    img = Image.new('RGB', (size, size), (32, 34, 40))
    d = ImageDraw.Draw(img)
    scale = size * 0.45 / extent
    px = size / 2 + x * scale
    py = size / 2 - y * scale
    a, b, cc = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    n = np.cross(b - a, cc - a)
    ln = np.linalg.norm(n, axis=1)
    ln[ln == 0] = 1
    n /= ln[:, None]
    light = np.array([0.3, 0.8, 0.5])
    light /= np.linalg.norm(light)
    shade = 0.35 + 0.65 * np.abs(n @ light)
    depth = z[t].mean(axis=1)
    col = (c[t].mean(axis=1) * shade[:, None]).clip(0, 255).astype(int)
    for i in np.argsort(depth):   # far first
        tri = t[i]
        d.polygon([(px[tri[0]], py[tri[0]]), (px[tri[1]], py[tri[1]]), (px[tri[2]], py[tri[2]])], fill=tuple(col[i]))
    d.text((6, 4), f'{view}  {label}', fill=(230, 230, 230))
    return img


def load(archive: str, model: str) -> Mdb:
    """The model `model` of `archive`: a file on disk, else OBJECT/<archive> in Root.cpk (read only)."""
    if os.path.isfile(archive):
        with open(archive, 'rb') as h:
            raw = h.read()
    else:
        import rootcpk
        raw = rootcpk.default().read('OBJECT', archive)
    return mdb_read(next(f for f in rab_read(raw).files if f.name.lower() == model.lower()).data)


def sheet(md: Mdb, rows: list[tuple[str, dict[str, list[float]]]], views: list[str], size: int, colour: list[str],
          out: str) -> None:
    """One row of `views` per (label, bone locals) in `rows`, the scale of the first, saved to `out`."""
    images, extent = [], None
    for label, locals_ in rows:
        v, t, c = geometry(md, {}, colour, locals_)
        if extent is None:
            extent = float(np.abs(v).max())
        images.append([render(v, t, c, view, size, extent, label) for view in views])
    img = Image.new('RGB', (size * len(views), size * len(images)))
    for r, row in enumerate(images):
        for k, im in enumerate(row):
            img.paste(im, (k * size, r * size))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('archive')
    ap.add_argument('model')
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'build', 'model_view.png'))
    ap.add_argument('--pose', action='append', default=[])
    ap.add_argument('--color', default='')
    ap.add_argument('--views', default='top,front,side,iso')
    ap.add_argument('--size', type=int, default=360)
    ap.add_argument('--bones', action='store_true')
    a = ap.parse_args(argv)
    md = load(a.archive, a.model)
    if a.bones:
        w = bind_world(md)
        for bn in md.bones:
            loc = bn.local
            axes = ' '.join(f'{"xyz"[r]}=({loc[r * 4]:+.2f},{loc[r * 4 + 1]:+.2f},{loc[r * 4 + 2]:+.2f})' for r in range(3))
            print(f'[{bn.index:2}] {md.name_of(bn.name):<22} parent {bn.parent:2}  local {axes}  at '
                  f'({w[bn.index][12]:+.2f},{w[bn.index][13]:+.2f},{w[bn.index][14]:+.2f})')
    poses = a.pose or ['']
    views = [s for s in a.views.split(',') if s]
    prefixes = [s for s in a.color.split(',') if s]
    rows = []
    extent = None
    for text in poses:
        v, t, c = geometry(md, parse_pose(text), prefixes)
        if extent is None:
            extent = float(np.abs(v).max())
        rows.append([render(v, t, c, view, a.size, extent, text[:60] or 'bind pose') for view in views])
    sheet = Image.new('RGB', (a.size * len(views), a.size * len(rows)))
    for r, row in enumerate(rows):
        for k, im in enumerate(row):
            sheet.paste(im, (k * a.size, r * a.size))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sheet.save(a.out)
    print(f'wrote {a.out} ({len(rows)} pose(s) x {len(views)} view(s))')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
