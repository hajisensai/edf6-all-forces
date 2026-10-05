"""Renders a whole Primer swarm as it flies combined, without the game: the core (its installer model) and its
drones (the dragonfly, posed by tools/swarm_pose_sim at one moment) at the formation slots src/jet_swarm.cpp flies
them to (kSlots, read from the source, times its size's slot scale), all facing the core's heading.

    python tools/swarm_formation_view.py [--size large|huge] [--units 12] [--time 0.03] [--out build/swarm_formation.png]
"""
from __future__ import annotations

import argparse
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
import swarm_pose_view  # noqa: E402

CORES = {'large': ('EDF6VC_SWARM_CORE.MRAB', 1.0), 'huge': ('EDF6VC_SWARM_CORE_XL.MRAB', 2.0)}   # kSizes' slot scale


def slots() -> list[tuple[float, float, float]]:
    """kSlots from src/jet_swarm.cpp, in its order."""
    with open(os.path.join(ROOT, 'src', 'jet_swarm.cpp'), encoding='utf-8') as h:
        src = h.read()
    body = src.split('constexpr float kSlots[kMaxUnits][3]={', 1)[1].split('};', 1)[0]
    return [tuple(float(x) for x in m) for m in re.findall(r'\{(-?[\d.]+)f,(-?[\d.]+)f,(-?[\d.]+)f\}', body)]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--size', choices=sorted(CORES), default='large')
    ap.add_argument('--units', type=int, default=12)
    ap.add_argument('--time', type=float, default=0.03)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'swarm_formation.png'))
    ap.add_argument('--px', type=int, default=640)
    a = ap.parse_args(argv)
    import jet_models
    import rootcpk
    game = rootcpk.default()
    core_file, scale = CORES[a.size]
    core_arc = jet_models.build(game, {core_file: jet_models.MODELS[core_file]})[core_file]
    core = model_view.mdb_read(next(f for f in model_view.rab_read(core_arc).files
                                    if f.name.lower() == 'e515_imperialufo.mdb').data)
    if not os.path.isfile(swarm_pose_view.MODEL[0]):
        with open(swarm_pose_view.MODEL[0], 'wb') as h:
            h.write(jet_models.generated(game, 'EDF6VC_SWARM_UNIT.MRAB'))
    drone = model_view.load(*swarm_pose_view.MODEL)
    binds = {drone.name_of(b.name): b.local for b in drone.bones if drone.name_of(b.name) in swarm_pose_view.BONES}
    label, locals_ = swarm_pose_view.run(binds, [a.time])[0]
    cv, ct, cc = model_view.geometry(core, {}, ['@mat'])
    dv, dt, dc = model_view.geometry(drone, {}, ['@mat'], locals_)
    vs, ts, cs = [cv], [ct], [cc]
    base = len(cv)
    for s in slots()[:a.units]:
        vs.append(dv + np.array(s) * scale)
        ts.append(dt + base)
        cs.append(dc)
        base += len(dv)
    v, t, c = np.vstack(vs), np.vstack(ts), np.vstack(cs)
    extent = float(np.abs(v).max())
    title = f'{a.size}: {a.units} drones, {label}'
    views = [model_view.render(v, t, c, view, a.px, extent, title) for view in ('top', 'iso')]
    img = Image.new('RGB', (a.px * 2, a.px))
    for k, im in enumerate(views):
        img.paste(im, (k * a.px, 0))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    img.save(a.out)
    print(f'wrote {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
