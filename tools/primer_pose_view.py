"""Renders the Primer creatures as the plugin poses them, without the game: the bone locals come from
tools/primer_pose_sim (built by build.cmd from the plugin's own src/primer_pose.h), the model and the bone
composition from pylib/model_view.py (world = local x parent's world, linear skinning: what the engine does with the
records), in the models' own textures.

    python tools/primer_pose_view.py dragonfly [--times 0,0.04,0.08,2.3,2.9] [--out build/dragonfly_pose.png]
    python tools/primer_pose_view.py centipede [--times 0,0.15,0.3,3.2,4.5] [--out build/centipede_pose.png]
        [--views side,top,iso] [--color @tex | @mat | wing,abd]

Each row is one moment of the simulator's scripted sortie (see tools/primer_pose_sim.cpp); its label says the state.
The models are built into build/ (pylib/dragonfly_model.py, pylib/centipede_model.py) when missing.
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

SIM = os.path.join(ROOT, 'build', 'primer_pose_sim.exe')
# creature -> (model archive in build/, member, builder module, the bones its pose table moves, default times)
CREATURES = {
    'dragonfly': ('EDF6VC_DRAGONFLY.MRAB', 'e507_goldufo.mdb', 'dragonfly_model',
                  ('wing_fl', 'wing_fr', 'wing_bl', 'wing_br', 'abd1', 'abd2', 'abd3', 'abd4'), '0,0.04,0.08,2.3,2.9'),
    'centipede': ('EDF6VC_CENTIPEDE.MRAB', 'e508_carrier.mdb', 'centipede_model',
                  ('segF1', 'segF2', 'segB1', 'segB2', 'head', 'tail', 'leg_segF2_l', 'leg_segF2_r', 'leg_segF1_l',
                   'leg_segF1_r', 'leg_body_l', 'leg_body_r', 'leg_segB1_l', 'leg_segB1_r', 'leg_segB2_l', 'leg_segB2_r'),
                  '0,0.15,0.3,3.2,4.5'),
}


def model(creature: str):
    """The creature's model (built into build/ when missing) and its bind locals of the posed bones."""
    file, member, builder, bones, _ = CREATURES[creature]
    path = os.path.join(ROOT, 'build', file)
    if not os.path.isfile(path):
        import importlib
        import rootcpk
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as h:
            h.write(importlib.import_module(builder).build(rootcpk.default()))
    md = model_view.load(path, member)
    binds = {md.name_of(b.name): b.local for b in md.bones if md.name_of(b.name) in bones}
    missing = sorted(set(bones) - set(binds))
    if missing:
        raise SystemExit(f'模型里没有这些骨骼：{missing}')
    return md, binds


def run(args: list[str], binds: dict[str, list[float]]) -> list[tuple[str, dict[str, list[float]]]]:
    """(label, bone locals) per frame the simulator prints."""
    if not os.path.isfile(SIM):
        raise SystemExit(f'{SIM} 不存在：先运行 build.cmd')
    text = ''.join(f'{n} ' + ' '.join(f'{x:.9g}' for x in m) + '\n' for n, m in binds.items())
    out = subprocess.run([SIM, *args], input=text, capture_output=True, text=True, check=True).stdout
    rows: list[tuple[str, dict[str, list[float]]]] = []
    for line in out.splitlines():
        part = line.split()
        if part[0] == 'frame':
            rows.append(('t=' + ' '.join(part[1:]), {}))
        elif part[0] == 'bone':
            rows[-1][1][part[1]] = [float(x) for x in part[2:18]]
    return rows


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('creature', choices=sorted(CREATURES))
    ap.add_argument('--times')
    ap.add_argument('--out')
    ap.add_argument('--color', default='@tex')
    ap.add_argument('--views', default='side,top,iso')
    ap.add_argument('--size', type=int, default=300)
    a = ap.parse_args(argv)
    md, binds = model(a.creature)
    times = (a.times or CREATURES[a.creature][4]).split(',')
    rows = run([a.creature, *times], binds)
    out = a.out or os.path.join(ROOT, 'build', f'{a.creature}_pose.png')
    model_view.sheet(md, rows, a.views.split(','), a.size, [x for x in a.color.split(',') if x], out)
    print(f'wrote {out}: {len(rows)} moments')
    for label, _ in rows:
        print(' ', label)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
