"""Renders the Primer swarm drone as the plugin poses it, without the game: the bone locals come from
tools/swarm_pose_sim (built by build.cmd from the plugin's own src/swarm_pose.h), the model and the bone composition
from pylib/model_view.py (world = local x parent's world, linear skinning: what the engine does with the records).

    python tools/swarm_pose_view.py [--times ...] [--out build/swarm_pose.png] [--views side,front,iso]
                                    [--color @mat | wing,abd]   (@mat: in the materials' colours)

The model is the dragonfly pylib/dragonfly_model.py makes (built into build/ when missing). Each row is one moment
of the simulator's scripted sortie (cruise, armed: the abdomen curls, shot down); the label says its state, the
curl and whether the guns may fire.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import model_view  # noqa: E402

SIM = os.path.join(ROOT, 'build', 'swarm_pose_sim.exe')
MODEL = (os.path.join(ROOT, 'build', 'EDF6VC_SWARM_UNIT.MRAB'), 'e507_goldufo.mdb')   # pylib/dragonfly_model.py
BONES = ('wing_fl', 'wing_fr', 'wing_bl', 'wing_br', 'abd1', 'abd2', 'abd3', 'abd4')   # src/swarm_pose.h kDroneBones


def run(binds: dict[str, list[float]], times: list[float]) -> list[tuple[str, dict[str, list[float]]]]:
    """(label, bone locals) per time, from the simulator."""
    if not os.path.isfile(SIM):
        raise SystemExit(f'{SIM} 不存在：先运行 build.cmd')
    text = ''.join(f'{n} ' + ' '.join(f'{x:.9g}' for x in m) + '\n' for n, m in binds.items())
    out = subprocess.run([SIM, *(f'{t:g}' for t in times)], input=text, capture_output=True, text=True, check=True).stdout
    rows: list[tuple[str, dict[str, list[float]]]] = []
    for line in out.splitlines():
        part = line.split()
        if part[0] == 'frame':
            rows.append((f't={part[1]} {part[2]} {part[3]} {part[4]}', {}))
        elif part[0] == 'bone':
            rows[-1][1][part[1]] = [float(x) for x in part[2:18]]
    return rows


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--times', default='0,0.04,0.08,2.3,2.8,3.4,4.9')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'swarm_pose.png'))
    ap.add_argument('--model', default=MODEL[0])
    ap.add_argument('--mdb', default=MODEL[1])
    ap.add_argument('--color', default='@mat')
    ap.add_argument('--views', default='side,front,iso')
    ap.add_argument('--size', type=int, default=300)
    a = ap.parse_args(argv)
    if a.model == MODEL[0] and not os.path.isfile(MODEL[0]):
        import dragonfly_model
        import rootcpk
        os.makedirs(os.path.dirname(MODEL[0]), exist_ok=True)
        with open(MODEL[0], 'wb') as h:
            h.write(dragonfly_model.build(rootcpk.default()))
    md = model_view.load(a.model, a.mdb)
    binds = {md.name_of(b.name): b.local for b in md.bones if md.name_of(b.name) in BONES}
    missing = sorted(set(BONES) - set(binds))
    if missing:
        raise SystemExit(f'模型里没有这些骨骼：{missing}')
    rows = run(binds, [float(x) for x in a.times.split(',')])
    model_view.sheet(md, rows, a.views.split(','), a.size, [x for x in a.color.split(',') if x], a.out)
    print(f'wrote {a.out}: {len(rows)} moments')
    for label, _ in rows:
        print(' ', label)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
