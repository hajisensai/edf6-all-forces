"""Renders a Primer centipede linked into one long creature, without the game: N segments (one creature each) one link
spacing apart (src/primer.cpp kLinkSpacing, read from the source) along a curve, each facing the one ahead of it (as
primer.cpp flies a linked one), posed by tools/primer_pose_sim (src/primer_pose.h: the front one's tail hidden, the
last one's head, both on the ones between; each a little later in its step than the one ahead, as primer.cpp steps
a chain: kChainLag), in the model's textures.

    python tools/primer_chain_view.py [--links 16] [--flying] [--out build/centipede_chain.png]

On the ground the curve is a lazy S on the plain; in the air it also climbs and dips (the long form flying).
"""
from __future__ import annotations

import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import model_view  # noqa: E402
import primer_pose_view  # noqa: E402


def link_spacing() -> float:
    with open(os.path.join(ROOT, 'src', 'primer.cpp'), encoding='utf-8') as h:
        return float(re.search(r'kLinkSpacing=([\d.]+)f', h.read()).group(1))


def curve(n: int, spacing: float, flying: bool) -> list[tuple[np.ndarray, np.ndarray]]:
    """(position, forward) of each link, the first at the front, along an S (and a swell in the air)."""
    def at(s: float) -> np.ndarray:   # s: m behind the head along the curve
        return np.array([18.0 * math.sin(s / 22.0), (6.0 * math.sin(s / 15.0) + 8.0) if flying else 0.0, -s])
    # One link spacing apart along the curve (by arc length, as primer.cpp's TrailPoint walks its trail).
    fine = [at(s * 0.05) for s in range(int(n * spacing * 3 / 0.05) + 2)]
    pts, run = [fine[0]], 0.0
    for a_, b_ in zip(fine, fine[1:]):
        run += float(np.linalg.norm(b_ - a_))
        if run >= spacing * len(pts) and len(pts) < n:
            pts.append(b_)
    out = []
    for i, p in enumerate(pts):
        f = (pts[i - 1] - p) if i else (at(-0.5) - p)   # a linked one faces the one ahead (primer.cpp Face)
        out.append((p, f / np.linalg.norm(f)))
    return out


def frame(p: np.ndarray, f: np.ndarray) -> np.ndarray:
    """The 4x4 world (row vectors) of a body at `p` facing `f`, its up as level as that allows."""
    up = np.array([0.0, 1.0, 0.0]) - f * f[1]
    up /= np.linalg.norm(up)
    right = np.cross(up, f)
    m = np.identity(4)
    m[0, :3], m[1, :3], m[2, :3], m[3, :3] = right, up, f, p
    return m


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--links', type=int, default=16)
    ap.add_argument('--flying', action='store_true')
    ap.add_argument('--out')
    ap.add_argument('--px', type=int, default=560)
    a = ap.parse_args(argv)
    md, binds = primer_pose_view.model('centipede')
    spacing = link_spacing()
    vs, ts, cs, base = [], [], [], 0
    links = curve(a.links, spacing, a.flying)
    for i, (p, f) in enumerate(links):
        m = frame(p, f)
        head, tail = ('0' if i > 0 else '1'), ('0' if i + 1 < a.links else '1')   # how much shows: hidden inside it
        # each a little later in its step: run its legs a step's share less far (the chain's wave)
        until = max(0.02, 0.4 - i * 0.18 / 1.3)
        rows = primer_pose_view.run(['centipede-state', '15', '1' if a.flying else '0', head, tail, '0', '-1',
                                     f'{until:.3f}'], binds)
        v, t, c = model_view.geometry(md, {}, ['@tex'], rows[0][1])
        v4 = np.hstack([v, np.ones((len(v), 1))]) @ m
        vs.append(v4[:, :3])
        ts.append(t + base)
        cs.append(c)
        base += len(v)
    v, t, c = np.vstack(vs), np.vstack(ts), np.vstack(cs)
    centre = (v.min(axis=0) + v.max(axis=0)) / 2
    v = v - centre
    extent = float(np.abs(v).max())
    title = f'{a.links} linked, {spacing:g} m apart, {"flying" if a.flying else "on the ground"}'
    views = [model_view.render(v, t, c, view, a.px, extent, title) for view in ('top', 'iso')]
    img = Image.new('RGB', (a.px * 2, a.px))
    for k, im in enumerate(views):
        img.paste(im, (k * a.px, 0))
    out = a.out or os.path.join(ROOT, 'build', f'centipede_chain{"_air" if a.flying else ""}.png')
    img.save(out)
    print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
