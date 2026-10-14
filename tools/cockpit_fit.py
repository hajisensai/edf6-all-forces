"""Help fit a seat's cockpit (pylib/cockpit_fits*.py): measure a stock model the way pylib/cockpits.py lays a kit out.
Read only (Root.cpk, or a loose Mods copy of the model as the build reads it). A vector starting with a minus
goes after an equals sign: --probe=-0.45,1.8,0.

    python tools/cockpit_fit.py STEM --digest              the sha-256 a fit is measured on (cockpit_fits DIGESTS)
    python tools/cockpit_fit.py STEM --bones               every bone: index, parent, rest position (model space)
    python tools/cockpit_fit.py STEM --probe x,y,z [--yaw D]   the room round that eye (metres to the hull: left,
                                                           right, up, down, ahead, back) and whether the eye itself
                                                           is enclosed (hidden from outside)
    python tools/cockpit_fit.py STEM --search x,y,z [--radius 0.6] [--step 0.1] [--yaw D]
        the enclosed points near x,y,z whose room is widest (the smallest of up+down, left+right, ahead+back
        biggest), best first: a start for an eye inside a hull
"""
from __future__ import annotations

import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cockpits  # noqa: E402
import rootcpk  # noqa: E402
from mdb import bind_world, mdb_read, rab_read  # noqa: E402


def vec(text: str) -> tuple[float, float, float]:
    x, y, z = (float(v) for v in text.split(','))
    return (x, y, z)


def load(game, stem: str):  # noqa: ANN001, ANN201 - rootcpk.Game, mdb.Mdb
    from pathlib import Path
    name = stem + '.MRAB'
    loose = Path(game.root, 'Mods', 'OBJECT', name)
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', name))
    return mdb_read(next(f for f in arc.files if f.name.lower() == stem.lower() + '.mdb').data)


def room(tris, eye, yaw: float) -> dict[str, float]:  # noqa: ANN001
    return cockpits.clearance(tris, eye, cockpits.frame(cockpits.Fit(0, '', eye, '', yaw)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('stem')
    ap.add_argument('--digest', action='store_true')
    ap.add_argument('--bones', action='store_true')
    ap.add_argument('--probe', type=vec)
    ap.add_argument('--search', type=vec)
    ap.add_argument('--radius', type=float, default=0.6)
    ap.add_argument('--step', type=float, default=0.1)
    ap.add_argument('--yaw', type=float, default=0.0)
    ap.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    a = ap.parse_args()
    game = rootcpk.Game(a.game)
    if a.digest:
        print(cockpits.model_digest(game, a.stem))
    md = load(game, a.stem)
    if a.bones:
        for b, w in zip(md.bones, bind_world(md)):
            print(f'{b.index:3d} {md.name_of(b.name):32s} parent {b.parent:3d}  rest ({w[12]:.3f}, {w[13]:.3f}, {w[14]:.3f})')
    if a.probe or a.search:
        tris = cockpits.hull_triangles(md)
    if a.probe:
        r = room(tris, a.probe, a.yaw)
        hidden = not cockpits.exposed(tris, [a.probe])[0]
        print('room', {k: round(v, 3) for k, v in r.items()}, 'enclosed' if hidden else 'EXPOSED (seen from outside)')
    if a.search:
        n = int(round(a.radius / a.step))
        found = []
        for i in range(-n, n + 1):
            for j in range(-n, n + 1):
                for k in range(-n, n + 1):
                    p = (a.search[0] + i * a.step, a.search[1] + j * a.step, a.search[2] + k * a.step)
                    r = room(tris, p, a.yaw)
                    if not all(math.isfinite(v) for v in r.values()):
                        continue
                    score = min(r['up'] + r['down'], r['left'] + r['right'], r['ahead'] + r['back'])
                    found.append((score, p, r))
        found.sort(key=lambda t: -t[0])
        shown = 0
        for score, p, r in found:
            if cockpits.exposed(tris, [p])[0]:
                continue
            print(f'{score:.2f}  eye ({p[0]:.2f}, {p[1]:.2f}, {p[2]:.2f})  ' + ' '.join(f'{k} {v:.2f}' for k, v in r.items()))
            shown += 1
            if shown >= 12:
                break
        if not shown:
            print('no enclosed point with a closed room near there')
    return 0


if __name__ == '__main__':
    sys.exit(main())
