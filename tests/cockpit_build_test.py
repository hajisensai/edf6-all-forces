"""The cockpits (pylib/cockpits.py) against the real models: every fitted seat of every stock model in pylib/cockpit_fits*.py
lays its kit out inside its hull (cockpits.fitted: the room measured from the model, nothing of an inside part seen from
outside), its screens are of a known kind; one model (the Blacker) is built whole the installer's way
(vehicle_optics.build_model): its archive has its textures where the game binds them (obj_model.texture_problems), and a
vc_eye_<seat> marker and the screens' vc_scr_ markers for every fitted seat, under the bone the fit names.

The plugin's own vehicles (FITS stems their generators make) are left to their generators' builds.
Exit 77 (skipped) without Root.cpk or without the cockpits' assets (the model folder's cockpits/)."""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cockpit_fits  # noqa: E402
import cockpit_kits  # noqa: E402
import cockpits  # noqa: E402
import obj_model as om  # noqa: E402
import rootcpk  # noqa: E402
import vehicle_optics  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

failures = 0


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what, flush=True)
    failures += 0 if ok else 1


def model_of(game, stem: str):  # noqa: ANN001, ANN201
    from pathlib import Path
    loose = Path(game.root, 'Mods', 'OBJECT', stem + '.MRAB')
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', stem + '.MRAB'))
    return mdb_read(next(f for f in arc.files if f.name.lower() == stem.lower() + '.mdb').data)


def main() -> int:
    root = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(root, 'Root.cpk')) or not cockpits.assets_present():
        print('SKIP: Root.cpk and the cockpits\' assets (the model folder\'s cockpits/) are needed')
        return 77
    game = rootcpk.Game(root)
    stock = {m.stem: m for m in vehicle_optics.MODELS if m.cockpits}
    for stem, fits in sorted(cockpit_fits.FITS.items()):
        if stem not in stock:
            print(f'      {stem}: a generated model (its generator builds it)')
            continue
        started = time.perf_counter()
        md = model_of(game, stem)
        hull = cockpits.hull_triangles(md)
        for fit in fits:
            try:
                kit = cockpits.fitted(hull, fit, cockpit_kits.KITS[fit.kit])
                kinds = {s.kind for s in kit.screens}
                expect(kinds <= set(cockpits.SCREEN_KINDS),
                       f'{stem} seat {fit.seat} ({fit.kit} on {fit.bone}): laid out inside the hull, screens {sorted(kinds)}')
            except Exception as e:  # noqa: BLE001 - every seat reported, then the count fails the test
                expect(False, f'{stem} seat {fit.seat} ({fit.kit}): {e}')
        print(f'      {stem}: {time.perf_counter() - started:.1f} s', flush=True)

    spec = stock['VEHICLE403_TANK']
    data = vehicle_optics.build_model(game, spec)
    arc = rab_read(data)
    member = spec.stem.lower() + '.mdb'
    md = mdb_read(next(f for f in arc.files if f.name.lower() == member).data)
    expect(not om.texture_problems(arc, md, member), f'{spec.stem}: built, its textures where the game binds them')
    names = [md.name_of(b.name) or '' for b in md.bones]
    for fit in spec.cockpits:
        marker = cockpits.MARKER.format(fit.seat)
        at = names.index(marker) if marker in names else -1
        expect(at >= 0 and md.bones[at].parent == md.bone_index(fit.bone),
               f'{spec.stem} seat {fit.seat}: {marker} under {fit.bone}')
        screens = [n for n in names if n.startswith(f'vc_scr_{fit.seat}_')]
        expect(bool(screens), f'{spec.stem} seat {fit.seat}: its screens {screens}')
    print(f'{failures} FAILED' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
