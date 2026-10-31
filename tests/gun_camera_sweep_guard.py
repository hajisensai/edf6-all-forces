"""Geometry guard (the user, 2026-10-27: 「有些载具第一人称会被挡到，比如温压弹火箭车，抬炮管以后看不见前面了」): every gun
camera seat of a plugin V603 vehicle sees where its gun points at every elevation the gun has.

A seat whose kit lays its sight line with the gun (pylib/cockpits.py Kit.gaze: a remote_display station's camera, its
sensor screen, and an open_rider at an open mount, its vc_gaze_ marker: the M163's and the quad Vulcan's operator, the
user 2026-10-31 「第一人称四管防空车视角不随炮管往上抬」; src/fp_view.h GunCameraSeat) looks from its eye marker along the
gun's bore (GunCameraFrame / vhud.cpp SeatGunLine); the eye moves only with the bone its fit names
(pylib/cockpit_fits_plugin.py).
pylib/cockpit_sweep.py aim_blocked raises the gun through the V603's elevation range on the vehicle's own model
(models/<folder>: wt_tracked.load, the emplacements' too) and casts the aim line from the eye: it must meet nothing of
the vehicle. The TOS-1's and the Patriot's eyes on the turret (cannon_main) met their own launcher packs from 31 / 26
deg up; the check is held to catch that fit.

A model folder that is not there (the Dreadnought's turrets are the user's model, not in the repository) is skipped,
and so is an emplacement without the installed EDF6 Root.cpk (pylib/emplacement_model.py spec reads its stock wheel
origin there; CI has no game). Read only.

    python tests/gun_camera_sweep_guard.py
"""
from __future__ import annotations

import os
import sys
from dataclasses import replace

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import cockpit_fits  # noqa: E402
import cockpit_kits  # noqa: E402
import cockpit_sweep  # noqa: E402
import emplacement_model as em  # noqa: E402
import rootcpk  # noqa: E402
import vcobjects as vc  # noqa: E402
import wt_tracked as wt  # noqa: E402

checks = failed = skipped = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def describe(blocked: list[tuple[float, float]]) -> str:
    return (f'{blocked[0][0]:.0f}..{blocked[-1][0]:.0f} deg ({len(blocked)} steps; at {blocked[0][0]:.0f} deg '
            f'{blocked[0][1]:.2f} m from the eye)')


def main() -> int:
    global skipped
    kinds = {v.sgo: kind for kind, v in vc.GROUND_VEHICLES.items() if v.stock == 'V603_FLAK'}
    room = dict(left=1.5, right=1.5, up=1.0, down=1.0, ahead=2.0, back=2.0)   # a kit's gaze does not hang on its room
    used = {f.kit for fits in cockpit_fits.FITS.values() for f in fits}
    gazing = {name for name in used if cockpit_kits.KITS[name](room).gaze}
    check({'remote_display', 'open_rider'} <= gazing, f'the gun-laid kits include the stations and the open seats: {gazing}')
    swept = set()
    seen = 0
    for stem, fits in sorted(cockpit_fits.FITS.items()):
        kind = kinds.get(stem)
        cameras = [f for f in fits if f.kit in gazing]
        if kind is None or not cameras:
            continue
        if wt.folder(kind) is None:
            skipped += 1
            print(f'SKIP {stem}: its model folder is not there')
            continue
        if kind in em.KINDS and not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
            skipped += 1
            print(f'SKIP {stem}: an emplacement, and the EDF6 Root.cpk is not there')
            continue
        spec = wt.load(kind)
        seen += 1
        swept |= {f.kit for f in cameras}
        for fit in cameras:
            blocked = cockpit_sweep.aim_blocked(spec, fit)
            check(not blocked, f'{stem} seat {fit.seat} (eye {fit.eye} on {fit.bone}): the aim line meets the vehicle '
                               f'at {describe(blocked) if blocked else ""}')
            if stem == 'EDF6VC_TOS1':
                # The fit the user saw (the eye on the turret): the check sees the launcher pack swing through it.
                old = cockpit_sweep.aim_blocked(spec, replace(fit, bone='cannon_main'))
                check(bool(old) and old[0][0] > 20.0 and old[-1][0] == cockpit_sweep.V603_ELEVATION[1],
                      f'{stem}: the turret-mounted eye is blocked from some elevation up to the stop ({old[:2]})')
    check(seen > 0, 'no gun camera seat of a V603 vehicle had its model to sweep')
    if wt.folder('vulcan') is not None:
        check('open_rider' in swept, f'the M163 / quad Vulcan open seats were swept: {sorted(swept)}')
    print(f'{"FAIL" if failed else "PASS"}: {checks - failed}/{checks} gun camera sweeps clear, {skipped} skipped')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
