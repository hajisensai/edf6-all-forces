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

    # A control deeper than the tub stays in view: its silhouette closes those lines, the liner the rest; a liner
    # pulled in toward the eye (the radial hull conformance) still leaves it open.
    from cockpit_shapes import basis, panel
    control = panel('console', 'test', (0, -.7, .45), .3, .3, (0, .8, -.6))
    liner = cockpit_tub.gap_liner(cockpit_tub.tub(lo, hi, sill), [control])
    expect(not cockpits.leaks(cockpits.Kit([liner, control], {}, sill=sill)),
           'the gap-only liner and the control together still close every line under the sill')
    expect(bool(cockpits.leaks(cockpits.Kit([liner], {}, sill=sill))), "...and the liner alone leaves the control's lines")
    tri = np.array([[liner.verts[i].pos for i in t] for t in liner.tris])
    right, up, _f = basis((0, .8, -.6))
    rays = np.array([np.array((0, -.7, .45)) + np.asarray(right) * x + np.asarray(up) * y
                     for x in (-.1, 0, .1) for y in (-.1, 0, .1)])
    expect(not cockpit_tub.through(rays, tri).any(), 'no liner face in front of the control')
    expect(not cockpit_tub.through(rays, tri * .5).any(), 'nor after the liner is pulled in toward the eye')
    expect(cockpit_tub.gap_liner(cockpit_tub.tub(lo, hi, sill), []).tris == cockpit_tub.tub(lo, hi, sill).tris,
           'no hardware: the whole tub')

    # The clip to a face's half-space, all triangles at once, matches the one-at-a-time Sutherland-Hodgman pass.
    rng = np.random.default_rng(7)
    tris = rng.uniform(-1, 1, (400, 3, 3))
    for axis, sign in ((0, 1.0), (1, -1.0), (2, 1.0)):
        got = cockpit_tub._clipped(tris, axis, sign)
        want = []
        for triangle in tris:
            poly = []
            for j, cur in enumerate(triangle):
                prev = triangle[j - 1]
                dc, dp = cur[axis] * sign - 1e-6, prev[axis] * sign - 1e-6
                if (dc >= 0) != (dp >= 0):
                    poly.append(prev + (cur - prev) * (dp / (dp - dc)))
                if dc >= 0:
                    poly.append(cur)
            if poly:
                want.append(np.asarray(poly + [poly[0]] * (4 - len(poly))))
        expect(len(got) == len(want) and np.allclose(got, np.asarray(want)),
               f'the batched clip to axis {axis} sign {sign:+} matches the per-triangle one')

    # Hardware whose projection misses every tub face of a plane is not merged there (a fighter's ~84k faces cost GEOS
    # ~20 s a kit when all went in): faces high overhead change nothing and reach no union.
    import shapely
    overhead = panel('overhead', 'test', (0, 5, 1), 2, 2, (0, -1, 0))
    real, merged = shapely.union_all, []

    def counting(shapes, *a, **kw):
        merged.append(len(shapes))
        return real(shapes, *a, **kw)
    shapely.union_all = counting
    try:
        alone = cockpit_tub.gap_liner(cockpit_tub.tub(lo, hi, sill), [control])
        n_alone, merged[:] = sum(merged), []
        both = cockpit_tub.gap_liner(cockpit_tub.tub(lo, hi, sill), [control, overhead])
        n_both = sum(merged)
    finally:
        shapely.union_all = real
    expect(n_both == n_alone, f'faces overhead reach no union ({n_alone} shapes merged, with them {n_both})')
    expect([v.pos for v in alone.verts] == [v.pos for v in both.verts] and alone.tris == both.tris,
           '...and leave the liner as it was')
    print(f'{failures} FAILED' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
