"""The panoramic cockpits (pylib/cockpit_kits_mech.py BOOT_STEPS, docs/feature-panoramic-cockpit.md), no game needed:
every mech pilot's pod and the Kepler's gunner pod are panoramic, Nix's a half dome; a full pod's boot tiles cover its
walls whole (dark until they light: nothing of the world before its step), a half pod's only the walls ahead of the eye
and its back stays plated; each glow is nearer the eye than its tile; the steps are the plugin's
(src/cockpit_instruments.h kBootSteps) and spread from ahead outward; graft names the bones the plugin reads.
Exit code 1 on a failure."""
from __future__ import annotations

import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cockpit_fits  # noqa: E402
import cockpit_kits_mech as mech  # noqa: E402
import cockpit_shell  # noqa: E402
import cockpits  # noqa: E402

failures = 0


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what)
    failures += not ok


def area(parts) -> float:
    total = 0.0
    for p in parts:
        v = np.array([x.pos for x in p.verts])
        for a, b, c in p.tris:
            total += 0.5 * np.linalg.norm(np.cross(v[b] - v[a], v[c] - v[a]))
    return total


def centroid(p) -> np.ndarray:
    return np.array([x.pos for x in p.verts]).mean(0)


header = open(os.path.join(HERE, '..', 'src', 'cockpit_instruments.h'), encoding='utf-8').read()
plugin_steps = int(re.search(r'constexpr int kBootSteps=(\d+);', header).group(1))
expect(plugin_steps == mech.BOOT_STEPS, f'BOOT_STEPS {mech.BOOT_STEPS} is the plugin\'s kBootSteps {plugin_steps}')

expect(all(mode != 'closed' for *_, mode in mech.PODS.values()), 'every mech pod is panoramic')
expect(mech.PODS['mech_nix'][3] == 'half', "Nix's pod is a half dome")
expect('pano_gunner' in cockpit_shell.REPLACEMENTS, 'the panoramic gunner pod replaces the native cabin')
expect(cockpit_fits.FITS['V603_FLAK'][0].kit == 'pano_gunner', "the Kepler's (KG6) gunner sits in a panoramic pod")
balga = [f.kit for f in cockpit_fits.FITS['V605_BARGA_CANNON']]
expect(balga == ['mech_large'] and mech.PODS['mech_large'][3] == 'full', 'the Balga cannon pilot: a full panoramic pod')

cabin = {'left': 1.2, 'right': 1.2, 'up': 0.8, 'down': 1.0, 'ahead': 1.4, 'back': 0.9}
for name, (want, _screens, _doc, mode) in mech.PODS.items():
    kit = mech.KITS[name](cabin)
    r = {side: min(w, cabin[side] - mech.MARGIN) for side, w in want.items()}
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    W, H = L + R, U + D
    dark = [p for p in kit.parts if p.material == 'boot_dark']
    glow = [p for p in kit.parts if p.material == 'boot_glow']
    expect(kit.panoramic, f'{name}: a panoramic kit')
    names = {p.name for p in kit.parts if p.name.startswith('boot_')}
    expect(all(cockpits.BOOT_PART.match(n) and int(n.split('_')[1]) < mech.BOOT_STEPS for n in names),
           f'{name}: boot parts named boot_<step>_<kind>, step under {mech.BOOT_STEPS}')
    expect(len(names) <= 2 * mech.BOOT_STEPS, f'{name}: at most {2 * mech.BOOT_STEPS} boot bones ({len(names)})')
    expect({n.split('_')[1] for n in names if n.endswith('_0')} == {n.split('_')[1] for n in names if n.endswith('_1')},
           f'{name}: every step has its dark tiles and their glow')
    if mode == 'full':
        whole = 2 * (W * H + W * (A + B) + H * (A + B))
    else:
        whole = W * H + 2 * (W * A + H * A)
    expect(abs(area(dark) - whole) < 1e-6 * max(1.0, whole), f'{name} ({mode}): the dark tiles cover the walls whole '
           f'({area(dark):.4f} of {whole:.4f} m2)')
    if mode == 'half':
        expect(all(centroid(p)[2] >= -1e-9 for p in dark), f'{name}: no boot tile behind the eye')
        expect(any(p.material == 'mech_plate' for p in kit.parts), f'{name}: the walls behind the eye stay plated')
    # Each step's glow is nearer the eye than its dark tiles (it flashes in front of them).
    for p in glow:
        twin = next(q for q in dark if q.name == p.name[:-1] + '0')
        gv = np.array([x.pos for x in p.verts])
        dv = np.array([x.pos for x in twin.verts])
        expect(np.linalg.norm(gv, axis=1).mean() < np.linalg.norm(dv, axis=1).mean() + 1e-9,
               f'{name}: {p.name} nearer the eye than its tiles')
        break
    # The steps spread outward from ahead: the earliest step's tiles nearer the line of sight than the last's.
    by_step = {}
    for p in dark:
        c = centroid(p)
        by_step.setdefault(int(p.name.split('_')[1]), []).append(np.degrees(np.arccos(c[2] / np.linalg.norm(c))))
    first, last = min(by_step), max(by_step)
    expect(np.mean(by_step[first]) < np.mean(by_step[last]), f'{name}: the boot spreads outward from ahead '
           f'(step {first} {np.mean(by_step[first]):.0f} deg, step {last} {np.mean(by_step[last]):.0f} deg)')
    expect(len(kit.screens) == 3, f'{name}: its three live screens kept')
    # Its two control sticks, each one part on its own moving control at its foot (StickPose's j_<hand>_<throw>_0).
    sticks = {c.part: c for c in kit.controls if c.control.startswith('j_')}
    expect(set(sticks) == {'stick_l', 'stick_r'} and sticks['stick_l'].control.startswith('j_0_')
           and sticks['stick_r'].control.startswith('j_1_') and sticks['stick_l'].centre[0] > 0 > sticks['stick_r'].centre[0],
           f'{name}: a left stick (the left, +x, the move) and a right one (the aim)')
    stick_parts = [p for p in kit.parts if p.name.startswith('stick_')]
    inside = all(-R <= v.pos[0] <= L and -D <= v.pos[1] <= U and -B <= v.pos[2] <= A for p in stick_parts for v in p.verts)
    expect(inside, f'{name}: the sticks inside the pod')
    # The seat keeps no piece the pod's box cut (its faces left hanging: the user, 2026-11-26 「这里有多余的东西」)
    # and nothing floating free of the rest.
    seat = [p for p in kit.parts if p.name.startswith('seat')]
    expect(all(-R - 1e-6 <= v.pos[0] <= L + 1e-6 and -D - 1e-6 <= v.pos[1] <= U + 1e-6 for p in seat for v in p.verts),
           f'{name}: the seat inside the pod')

# The seat's cut (a narrow pod's box): no triangle of a piece the box cuts is kept.
lo, hi = (-0.37, -0.42, -0.47), (0.37, 0.18, 0.42)
scale = mech.SEAT_SCALE * 0.6
parts, _ = mech._seat(scale, lo, hi)
part_tris = {p.name: len(p.tris) for p in parts}
cut_any = False
for name, _rgb, pos, _n, tris, piece in mech._seat_mesh():
    q = pos * scale
    ins = ((q >= np.array(lo)) & (q <= np.array(hi))).all(1)[tris].all(1)
    cut = np.unique(piece[~ins])
    partial = [p for p in cut if ins[piece == p].any()]
    cut_any = cut_any or bool(partial)
    expect(part_tris.get(name, 0) <= int((ins & ~np.isin(piece, cut)).sum()),
           f'seat {name}: no triangle of a piece the box cut ({len(partial)} cut pieces)')
expect(cut_any, 'the narrow box cuts some of the seat (the check above checks something)')

print('FAILED' if failures else 'all ok')
sys.exit(1 if failures else 0)
