"""Build a stock vehicle's model with its cockpits (pylib/vehicle_optics.py build_model, the installer's own build) into
a temp folder and render, with tools/cabin_view.py, each seat's view from its `vc_eye_<seat>` marker (culled as the
game culls: the hull invisible from inside) and the vehicle from outside (front-left and rear-right, culled: no part of
a cockpit may show through the hull). Read only on the game; writes only under --out.

    python tools/cockpit_preview.py VEHICLE403_TANK [--out tmp/cockpit_preview] [--size 1280x720] [--fov 70]
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import rootcpk  # noqa: E402
import vehicle_optics as optics  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402


def render(archive: str, model: str, out: str, extra: list[str]) -> None:
    cmd = [sys.executable, os.path.join(HERE, 'cabin_view.py'), archive, model, '--out', out, *extra]
    subprocess.run(cmd, check=True)
    print(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('stem')
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'tmp', 'cockpit_preview'))
    ap.add_argument('--size', default='1280x720')
    ap.add_argument('--fov', default='70')
    ap.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    args = ap.parse_args()
    spec = next((m for m in optics.MODELS if m.stem.upper() == args.stem.upper()), None)
    if spec is None or not spec.cockpits:
        print(f'{args.stem}: no cockpit fitted (pylib/cockpit_fits.py)')
        return 1
    game = rootcpk.Game(args.game)
    data = optics.build_model(game, spec)
    os.makedirs(args.out, exist_ok=True)
    archive = os.path.join(args.out, optics.output(spec))
    with open(archive, 'wb') as h:
        h.write(data)
    model = spec.stem.lower() + '.mdb'
    member = next(f for f in rab_read(data).files if f.name.lower() == model)
    md = mdb_read(member.data)
    common = ['--size', args.size, '--fov', args.fov]
    for fit in spec.cockpits:
        marker = f'vc_eye_{fit.seat}'
        if md.bone_index(marker) < 0:
            raise SystemExit(f'{marker} missing from the built model')
        render(archive, model, os.path.join(args.out, f'{spec.stem}_seat{fit.seat}.png'),
               ['--bone', marker, '--eye-local', '0,0,0', '--cull', *common])
        render(archive, model, os.path.join(args.out, f'{spec.stem}_seat{fit.seat}_inside.png'),
               ['--bone', marker, '--eye-local', '0,0,0', '--mark-back', *common])
    size = 12.0
    for name, eye in (('front_left', f'{size*0.6},{size*0.35},{size*0.8}'), ('rear_right', f'{-size*0.6},{size*0.35},{-size*0.8}')):
        render(archive, model, os.path.join(args.out, f'{spec.stem}_{name}.png'),
               ['--eye', eye, '--look', '0,1.5,0', '--cull', *common])
    return 0


if __name__ == '__main__':
    sys.exit(main())
