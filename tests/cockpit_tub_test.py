"""pylib/cockpit_tub.py with cockpits.leaks, no game needed: a tub closes every line of sight under its sill; a real
window under the sill (a chin window's glass below the eye) is left open in the tub, and leaks() counts the lines out
through it as a view, not a leak, while the lines the tub closes stay closed. Exit code 1 on a failure."""
from __future__ import annotations

import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import cockpit_tub  # noqa: E402
import cockpits  # noqa: E402

failures = 0


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what)
    failures += 0 if ok else 1


def main() -> int:
    lo, hi = np.array([-0.5, -1.0, -0.4]), np.array([0.5, 0.4, 1.5])
    sill = -30.0
    closed = cockpits.Kit([cockpit_tub.tub(lo, hi, sill)], {}, sill=sill)
    expect(not cockpits.leaks(closed), 'a tub without windows closes every line under its sill')

    # A chin window: glass 0.6-0.9 m ahead, 0.15-0.45 m left, a little over the floor.
    a, b, c, d = (0.15, -0.9, 0.6), (0.45, -0.9, 0.6), (0.45, -0.9, 0.9), (0.15, -0.9, 0.9)
    glass = ((a, b, c), (a, c, d))
    kit = cockpits.Kit([cockpit_tub.tub(lo, hi, sill, glass)], {}, sill=sill, windows=glass)
    expect(not cockpits.leaks(kit), 'with a chin window: no line leaks (the window counts as a view)')
    lines = cockpits.sight_lines(sill)
    yaw, pitch = (np.radians([ln[k] for ln in lines]) for k in (0, 1))
    dirs = np.stack([np.sin(yaw) * np.cos(pitch), np.sin(pitch), np.cos(yaw) * np.cos(pitch)], 1)
    out = cockpit_tub.through(dirs, glass)
    bare = cockpits.Kit(kit.parts, {}, sill=sill)
    open_lines = set(cockpits.leaks(bare))
    window_lines = {ln for ln, o in zip(lines, out) if o}
    expect(bool(window_lines), f'some lines go out through the window ({len(window_lines)})')
    expect(len(window_lines & open_lines) >= len(window_lines) // 2,
           f'...and the tub leaves them open, not sealed over ({len(window_lines & open_lines)} of them; a cell on the '
           "glass's edge stays)")
    expect(not open_lines - window_lines, 'the opening reaches no line past the glass')
    print(f'{failures} FAILED' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
