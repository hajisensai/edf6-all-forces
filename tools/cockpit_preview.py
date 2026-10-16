"""Build a vehicle's model with its cockpits into a temp folder and render, with tools/cabin_view.py, each seat's view
from its `vc_eye_<seat>` marker (culled as the game culls: the hull invisible from inside) and the vehicle from outside
(front-left and rear-right, culled: no part of a cockpit may show through the hull). Read only on the game; writes only
under --out.

A stock vehicle's model is built as the installer builds it (pylib/vehicle_optics.py build_model). A model a generator
makes (--generator make_jets: tools/make_jets.py and the like, whose build() ends in cockpits.graft_files) is built by
running that generator's build(game root) in memory (Root.cpk only read) with the grafting held back, its ungrafted
OBJECT/<STEM>.MRAB kept under --out (reused by later runs: --rebuild makes it again), then grafted through
cockpits.graft_files as the generator does it: kit and fit changes show without rebuilding the generator's output.

    python tools/cockpit_preview.py VEHICLE403_TANK [--out tmp/cockpit_preview] [--size 1280x720] [--fov 70]
    python tools/cockpit_preview.py EDF6VC_INTERCEPTOR --generator make_jets [--rebuild] [--seat 0] [--around]
        --around: each seat's view also turned down and to the sides (AROUND: <stem>_seat<n>_<name>.png), where a
        cockpit without its floor or walls shows the world through the hull
    python tools/cockpit_preview.py EDF6VC_GUNSHIP --stock-model BOMBER401.MRAB
        a stem fitted for a model a generator is to make as a copy of a stock one (Root.cpk's OBJECT/<that>), grafted
        as graft_files would graft the copy
"""
from __future__ import annotations

import argparse
import importlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import cockpits  # noqa: E402
import rootcpk  # noqa: E402
import vehicle_optics as optics  # noqa: E402
from mdb import bind_world, mdb_read, rab_read  # noqa: E402


# The seat's view turned (yaw left, pitch up, degrees: cabin_view.py --turn): straight down, down past each knee,
# out each side under the canopy's sill, back down over each shoulder.
AROUND = (('down', 0, -85), ('down_left', 50, -50), ('down_right', -50, -50), ('left', 90, -30),
          ('right', -90, -30), ('back_left', 150, -40), ('back_right', -150, -40))


def render(archive: str, model: str, out: str, extra: list[str]) -> None:
    cmd = [sys.executable, os.path.join(HERE, 'cabin_view.py'), archive, model, '--out', out, *extra]
    subprocess.run(cmd, check=True)
    print(out)


def generated(game_root: str, generator: str, stem: str, out: str, rebuild: bool) -> bytes:
    """OBJECT/<stem>.MRAB as `generator`'s build() makes it, before its cockpits are grafted (cached under `out`)."""
    cache = os.path.join(out, f'_generated_{stem.upper()}.MRAB')
    if os.path.isfile(cache) and not rebuild:
        with open(cache, 'rb') as h:
            return h.read()
    mod = importlib.import_module(generator)
    real = cockpits.graft_files
    cockpits.graft_files = lambda game, files: dict(files)   # held back: grafted from the cache below
    try:
        files = mod.build(game_root)
    finally:
        cockpits.graft_files = real
    rel = next((k for k in files if k.upper() == f'OBJECT/{stem.upper()}.MRAB'), None)
    if rel is None:
        models = sorted(k for k in files if k.upper().endswith('.MRAB'))
        raise SystemExit(f'{generator}.build() makes no OBJECT/{stem}.MRAB (it makes {models})')
    with open(cache, 'wb') as h:
        h.write(files[rel])
    return files[rel]


def model_member(data: bytes, stem: str) -> str:
    mdbs = [f.name for f in rab_read(data).files if f.name.lower().endswith('.mdb')]
    return next((n for n in mdbs if n.lower() == stem.lower() + '.mdb'), mdbs[0])


def outside_size(md) -> tuple[float, float]:  # noqa: ANN001 - mdb.Mdb
    """The distance scale and look height for the outside views: the model's bone extent (at least 12 m)."""
    pos = [(w[12], w[13], w[14]) for w in bind_world(md)]
    span = max(max(p[k] for p in pos) - min(p[k] for p in pos) for k in range(3)) if pos else 0.0
    return max(12.0, 1.2 * span), sum(p[1] for p in pos) / len(pos) if pos else 1.5


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('stem')
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'tmp', 'cockpit_preview'))
    ap.add_argument('--size', default='1280x720')
    ap.add_argument('--fov', default='70')
    ap.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    ap.add_argument('--generator', help='a tools/ module whose build(game root) makes OBJECT/<STEM>.MRAB')
    ap.add_argument('--rebuild', action='store_true', help='run the generator again (else its cached model)')
    ap.add_argument('--stock-model', help="Root.cpk's OBJECT/<file> as the model the stem's generator makes")
    ap.add_argument('--seat', type=int, action='append', help='only these seats (and no outside views)')
    ap.add_argument('--no-outside', action='store_true')
    ap.add_argument('--around', action='store_true', help='each seat also looking down and to the sides (AROUND)')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    game = rootcpk.Game(args.game)
    if args.generator or args.stock_model:
        import cockpit_fits
        fits = cockpit_fits.FITS.get(args.stem.upper(), ())
        if not fits:
            print(f'{args.stem}: no cockpit fitted (pylib/cockpit_fits.py)')
            return 1
        rel = f'OBJECT/{args.stem.upper()}.MRAB'
        source = (game.read('OBJECT', args.stock_model) if args.stock_model else
                  generated(args.game, args.generator, args.stem, args.out, args.rebuild))
        data = cockpits.graft_files(game, {rel: source})[rel]
        stem, name = args.stem.upper(), f'{args.stem.upper()}.MRAB'
    else:
        spec = next((m for m in optics.MODELS if m.stem.upper() == args.stem.upper()), None)
        if spec is None or not spec.cockpits:
            print(f'{args.stem}: no cockpit fitted (pylib/cockpit_fits.py)')
            return 1
        data, fits, stem, name = optics.build_model(game, spec), spec.cockpits, spec.stem, optics.output(spec)
    archive = os.path.join(args.out, name)
    with open(archive, 'wb') as h:
        h.write(data)
    model = model_member(data, stem)
    md = mdb_read(next(f for f in rab_read(data).files if f.name == model).data)
    common = ['--size', args.size, '--fov', args.fov]
    for fit in fits:
        if args.seat and fit.seat not in args.seat:
            continue
        marker = f'vc_eye_{fit.seat}'
        if md.bone_index(marker) < 0:
            raise SystemExit(f'{marker} missing from the built model')
        render(archive, model, os.path.join(args.out, f'{stem}_seat{fit.seat}.png'),
               ['--bone', marker, '--eye-local', '0,0,0', '--first-person-seat', str(fit.seat), '--cull', *common])
        render(archive, model, os.path.join(args.out, f'{stem}_seat{fit.seat}_inside.png'),
               ['--bone', marker, '--eye-local', '0,0,0', '--first-person-seat', str(fit.seat), '--mark-back', *common])
        for view, yaw, pitch in AROUND if args.around else ():
            render(archive, model, os.path.join(args.out, f'{stem}_seat{fit.seat}_{view}.png'),
                   ['--bone', marker, '--eye-local', '0,0,0', '--first-person-seat', str(fit.seat),
                    f'--turn={yaw},{pitch}', '--cull', *common])
    if args.seat or args.no_outside:
        return 0
    size, look = (outside_size(md) if args.generator or args.stock_model else (12.0, 1.5))
    for side, eye in (('front_left', (size * 0.6, size * 0.35, size * 0.8)),
                      ('rear_right', (-size * 0.6, size * 0.35, -size * 0.8))):
        render(archive, model, os.path.join(args.out, f'{stem}_{side}.png'),
               [f'--eye={eye[0]},{eye[1] + look - 1.5},{eye[2]}', f'--look=0,{look},0', '--cull', *common])
    return 0


if __name__ == '__main__':
    sys.exit(main())
