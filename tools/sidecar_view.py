"""Preview of the sidecar motorcycle (pylib/sidecar_model.py) as built: the model in its own texture (pylib/model_view.py
geometry), the Ranger standing at GUNNER_POINT, and the collision (the ragdoll's body hulls, the floor slab in yellow)
drawn over it. Read only (Root.cpk); writes only --out.

    python tools/sidecar_view.py --out tmp/sidecar_view.png [--built DIR] [--size 420]

--built DIR: the files tools/make_sidecar.py --out DIR wrote (else they are built in memory). Views: the right side
(the sidecar's side), the front, the top, a view from ahead and to the right above, and the right side cut along the
tub's centre line (the near half of the tub, the wheel and its guard left out: the gunner's feet on the floor, the
rim at their hips).
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import model_view as mv  # noqa: E402
import sidecar_model as sm  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

SOLDIER = (120, 200, 120)
HULL, FLOOR = (150, 150, 160), (250, 210, 60)


VIEWS = {   # (screen right, screen up, toward the viewer) in the model frame, +x the vehicle's LEFT (its stock
    # convention, pylib/sidecar_model.py): a right-handed frame, so right x up = toward. Nothing is drawn mirrored.
    'right': ((0, 0, 1), (0, 1, 0), (-1, 0, 0)),    # from the sidecar's side: the nose on the right
    'front': ((1, 0, 0), (0, 1, 0), (0, 0, 1)),     # from ahead: the sidecar on the left
    'top': ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),      # from above, the nose up: the sidecar on the right
}
VIEWS['cut'] = VIEWS['right']


def axes(view: str) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """(right, up, toward the viewer) of a view; 'iso' looks from ahead, on the sidecar's side, above."""
    if view == 'iso':
        eye = np.array([-0.9, 0.6, 1.0])
        eye /= np.linalg.norm(eye)
        right = np.cross(np.array([0.0, 1.0, 0.0]), eye)
        right /= np.linalg.norm(right)
        return right, np.cross(eye, right), eye
    r, u, t = (np.array(x, dtype=np.float64) for x in VIEWS[view])
    assert np.allclose(np.cross(r, u), t), view
    return r, u, t


def project(v: np.ndarray, view: str, size: int, extent: float, centre: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    right, up, toward = axes(view)
    s = size * 0.45 / extent
    c = v - centre
    return size / 2 + (c @ right) * s, size / 2 - (c @ up) * s, c @ toward


def draw(scene: list[tuple[np.ndarray, np.ndarray, np.ndarray]], hulls: list[tuple[np.ndarray, tuple]], view: str,
         size: int, extent: float, centre: np.ndarray, label: str) -> Image.Image:
    v = np.vstack([x[0] for x in scene])
    base, ts = 0, []
    for x in scene:
        ts.append(x[1] + base)
        base += len(x[0])
    t = np.vstack(ts)
    c = np.vstack([x[2] for x in scene])
    px, py, depth = project(v, view, size, extent, centre)
    img = Image.new('RGB', (size, size), (32, 34, 40))
    d = ImageDraw.Draw(img)
    a, b, cc = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    n = np.cross(b - a, cc - a)
    ln = np.linalg.norm(n, axis=1)
    ln[ln == 0] = 1
    n /= ln[:, None]
    light = np.array([-0.3, 0.8, 0.5])
    light /= np.linalg.norm(light)
    shade = 0.35 + 0.65 * np.abs(n @ light)
    col = (c[t].mean(axis=1) * shade[:, None]).clip(0, 255).astype(int)
    for i in np.argsort(depth[t].mean(axis=1)):
        tri = t[i]
        d.polygon([(px[k], py[k]) for k in tri], fill=tuple(col[i]))
    for pts, colour in hulls:
        hx, hy, _ = project(pts, view, size, extent, centre)
        ring = convex(np.stack([hx, hy], axis=1))
        d.line([tuple(p) for p in ring] + [tuple(ring[0])], fill=colour, width=2 if colour == FLOOR else 1)
    # The ground (y 0) and the floor (FLOOR_Y) as marks on the left edge in the side views.
    if view in ('right', 'cut', 'front'):
        for y, colour in ((0.0, (90, 90, 90)), (sm.FLOOR_Y, FLOOR), (sm.RIM_Y, (200, 120, 120))):
            _, gy, _ = project(np.array([[0.0, y, 0.0]]), view, size, extent, centre)
            d.line([(0, gy[0]), (14, gy[0])], fill=colour, width=2)
    d.text((6, 4), f'{view}  {label}', fill=(230, 230, 230))
    return img


def convex(p: np.ndarray) -> np.ndarray:
    """The 2-D convex hull of the points (monotone chain)."""
    q = sorted(map(tuple, p))
    if len(q) < 3:
        return np.array(q)

    def cross(o, a, b) -> float:  # noqa: ANN001
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for x in q:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], x) <= 0:
            lo.pop()
        lo.append(x)
    for x in reversed(q):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], x) <= 0:
            hi.pop()
        hi.append(x)
    return np.array(lo[:-1] + hi[:-1])


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True)
    ap.add_argument('--built', default='')
    ap.add_argument('--size', type=int, default=420)
    a = ap.parse_args(argv)
    import rootcpk
    game = rootcpk.default()
    if a.built:
        with open(os.path.join(a.built, 'OBJECT', sm.OUT_ARC), 'rb') as h:
            arc = h.read()
        with open(os.path.join(a.built, 'OBJECT', sm.OUT_RAGDOLL), 'rb') as h:
            shkt = h.read()
    else:
        arc, shkt = sm.build(game), sm.build_collision(game)[0]
    tmp = os.path.join(os.path.dirname(os.path.abspath(a.out)), '_sidecar_view.mrab')
    with open(tmp, 'wb') as h:
        h.write(arc)
    md = mv.load(tmp, sm.HOST_MDB)
    os.remove(tmp)
    v, t, c = mv.geometry(md, {}, ['@tex'])
    soldier = mdb_read(sm.member(rab_read(game.read('OBJECT', sm.SOLDIER_ARC)), sm.SOLDIER_MDB).data)
    sv, st, _sc = mv.geometry(soldier, {}, [])
    sv = sv + np.array(sm.GUNNER_POINT)
    s = sm._Shkt(shkt)
    proxy = np.array(sm.BODY_PROXY)
    hulls = [(np.array(s.vertices(k)) + proxy, FLOOR if k == sm.FLOOR_HULL else HULL) for k in range(s.inst_n)]
    centre = np.array([-0.55, 0.75, 0.45])
    extent = 1.9
    full = [(v, t, c), (sv, st, np.tile(np.array(SOLDIER, dtype=np.float64), (len(sv), 1)))]
    # The cut: the bike's and the soldier's triangles all, the sidecar's only behind its centre line (x > TUB_X).
    cen_x = v[t].mean(axis=1)[:, 0]
    keep = (cen_x > sm.TUB_X) | (cen_x > -0.18)
    cut = [(v, t[keep], c), full[1]]
    floor_only = [h for h in hulls if h[1] == FLOOR]
    tiles = [draw(full, hulls, 'right', a.size, extent, centre, 'body hulls grey, floor slab yellow'),
             draw(full, floor_only, 'front', a.size, extent, centre, 'floor slab yellow'),
             draw(full, floor_only, 'top', a.size, extent, centre, 'floor slab yellow'),
             draw(full, [], 'iso', a.size, extent, centre, ''),
             draw(cut, floor_only, 'cut', a.size, extent, centre, f'cut at x {sm.TUB_X}: floor {sm.FLOOR_Y}, rim {sm.RIM_Y}')]
    cols = 3
    img = Image.new('RGB', (a.size * cols, a.size * ((len(tiles) + cols - 1) // cols)), (20, 20, 24))
    for k, im in enumerate(tiles):
        img.paste(im, ((k % cols) * a.size, (k // cols) * a.size))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    img.save(a.out)
    print(f'wrote {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
