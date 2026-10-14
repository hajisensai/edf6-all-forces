"""A closed cockpit tub round the eye (pylib/cockpit_kits_plugin.py's fighters and flight deck,
pylib/cockpit_kits_air.py's helicopter stations): a floor and four walls up to the canopy's sill, laid on the box a kit's model is cut to, so that
looking down or aside under the sill the eye meets the cockpit, never the world through the hull the game does not
draw from inside (the user, 2026-10-14: "固定翼的底部是空白，完全能看过去", and for the helicopters too).

A real window under the sill (a helicopter's chin window at the pilot's feet, its doors' lower panes) stays open: a
kit gives its model's glass (`windows`, eye-frame triangles) and the tub, laid in CELL-sized cells there, leaves out
every cell the eye sees wholly through that glass; cockpits.leaks lets the lines through the glass through the same
way.

    tub(lo, hi, sill, windows)      the tub as one Part (material 'tub'), its faces toward the eye
    through(dirs, windows)          which lines of sight from the eye cross the glass
    face(name, material, pts, facing)  a flat convex polygon facing `facing` (UVs one repeat per 0.8 m)
"""
from __future__ import annotations

import math

import numpy as np

import obj_model as om
from cockpit_shapes import merge

Vec3 = tuple[float, float, float]
Tri = tuple[Vec3, Vec3, Vec3]
TUB_TEXTURE, TUB_STRIPS, TUB_INSET = 'air_tub', 12, 0.002   # textures/<name>.png; strips a wall's top is drawn in
CELL = 0.05   # m: with windows, the floor and walls are laid in cells this size, so a window's opening is left round


def through(dirs: np.ndarray, windows: np.ndarray | tuple[Tri, ...]) -> np.ndarray:
    """For each line of sight from the eye (the origin) along `dirs` (n, 3), whether it crosses one of the window
    triangles (m, 3, 3: either side, anywhere ahead of the eye)."""
    w = np.asarray(windows, dtype=np.float64).reshape(-1, 3, 3)
    out = np.zeros(len(dirs), dtype=bool)
    if not len(w) or not len(dirs):
        return out
    v0, e1, e2 = w[:, 0], w[:, 1] - w[:, 0], w[:, 2] - w[:, 0]
    s = -v0
    q = np.cross(s, e1)
    chunk = max(1, 2_000_000 // len(w))
    for a in range(0, len(dirs), chunk):
        d = dirs[a:a + chunk]
        p = np.cross(d[:, None, :], e2[None, :, :])
        det = np.einsum('mj,nmj->nm', e1, p)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        u = np.einsum('mj,nmj->nm', s, p) * inv
        v = (d @ q.T) * inv
        t = (e2 * q).sum(1)[None, :] * inv
        out[a:a + chunk] = (ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4)).any(1)
    return out


def face(name: str, material: str, pts: list[Vec3], facing: Vec3) -> list[om.Part]:
    """The flat convex polygon `pts` (its corners in order; repeated ones count once) with its front toward
    `facing`, UVs laid flat on it one repeat per 0.8 m; none when it has no area."""
    ring = [p for k, p in enumerate(pts) if math.dist(p, pts[k - 1]) > 1e-6]
    if len(ring) < 3:
        return []
    n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if len(ring) == 4 and np.linalg.norm(n) < 1e-9:
        ring = ring[1:] + ring[:1]
        n = np.cross(np.subtract(ring[1], ring[0]), np.subtract(ring[2], ring[0]))
    if np.linalg.norm(n) < 1e-9:
        return []
    if float(np.dot(n, facing)) < 0:
        ring = ring[::-1]
    axes = [i for i in range(3) if abs(facing[i]) < 0.5]
    f = tuple(float(c) for c in np.array(facing) / np.linalg.norm(facing))
    verts = [om.Vertex(p, f, (p[axes[0]] / 0.8, -p[axes[1]] / 0.8)) for p in ring]
    return [om.Part(name, material, verts, [(0, k, k + 1) for k in range(1, len(ring) - 1)])]


def _open(pts: list[Vec3], windows: np.ndarray) -> bool:
    """Whether the eye sees out through the glass across the whole polygon `pts` (its corners and centre): only such a
    cell is left out, so the opening never reaches past the glass (a cell on the window's edge stays, over a sliver of
    it)."""
    p = np.array(pts + [tuple(np.mean(np.array(pts, dtype=np.float64), axis=0))], dtype=np.float64)
    n = np.linalg.norm(p, axis=1)
    return bool((n > 1e-9).all() and through(p / n[:, None], windows).all())


def _cells(span: tuple[float, float], windows: np.ndarray) -> np.ndarray:
    """A side's cut points: the whole side at once without windows, else CELL-sized steps."""
    if not len(windows):
        return np.array(span)
    return np.linspace(span[0], span[1], max(1, math.ceil(abs(span[1] - span[0]) / CELL)) + 1)


def _wall(name: str, axis: int, at: float, along: int, span: tuple[float, float], floor: float, roof: float,
          sill: float, windows: np.ndarray) -> list[om.Part]:
    """The wall in the plane `axis` = `at` (eye frame), from `span[0]` to `span[1]` along the axis `along`, from the
    floor up to where the sill's cone (pitch `sill` degrees from the eye) meets it (no higher than the roof), in
    TUB_STRIPS upright strips (on a flat wall that cone is a curve), its front toward the eye; with windows, each strip
    in CELL-high rows, a row the eye sees the glass across left out."""
    slope = math.tan(math.radians(sill))
    ts = np.linspace(span[0], span[1], TUB_STRIPS + 1)
    tops = [min(roof, max(floor, slope * math.hypot(at, t))) for t in ts]
    facing = [0.0, 0.0, 0.0]
    facing[axis] = -math.copysign(1.0, at)

    def pt(t: float, y: float) -> Vec3:
        p = [0.0, y, 0.0]
        p[axis], p[along] = at, t
        return (p[0], p[1], p[2])
    out = []
    for k in range(TUB_STRIPS):
        if max(tops[k], tops[k + 1]) - floor <= 1e-4:
            continue
        high = max(tops[k], tops[k + 1]) - floor
        rows = np.linspace(0.0, 1.0, (max(1, math.ceil(high / CELL)) if len(windows) else 1) + 1)
        for r in range(len(rows) - 1):
            lo_a, lo_b = (floor + rows[r] * (tops[k] - floor), floor + rows[r] * (tops[k + 1] - floor))
            hi_a, hi_b = (floor + rows[r + 1] * (tops[k] - floor), floor + rows[r + 1] * (tops[k + 1] - floor))
            pts = [pt(ts[k], lo_a), pt(ts[k + 1], lo_b), pt(ts[k + 1], hi_b), pt(ts[k], hi_a)]
            if len(windows) and _open(pts, windows):
                continue
            out += face(f'{name}{k}_{r}', 'tub', pts, tuple(facing))
    return out


def tub(lo: np.ndarray, hi: np.ndarray, sill: float, windows: np.ndarray | tuple[Tri, ...] = ()) -> om.Part:
    """A closed cockpit tub round the eye in the box lo..hi (the room the model is cut to; the user, 2026-10-14:
    "照着人家完全照搬": a real cockpit's tub, as DCS and War Thunder draw theirs): the floor, and the four walls up to
    the canopy's sill (pitch `sill` from the eye), TUB_INSET inside the box, all facing the eye, less what the eye sees
    the model's real glass across (`windows`). The model cut to the box stands in it; where the cut took the model's own
    floor or side walls away (a room smaller than the real cockpit), or the model never had them (it was modelled to be
    seen through glass from above), the tub is what the eye meets instead of the world: the hull itself is not drawn
    from inside."""
    w = np.asarray(windows, dtype=np.float64).reshape(-1, 3, 3)
    (x0, y0, z0), (x1, y1, z1) = lo + TUB_INSET, hi - TUB_INSET
    parts: list[om.Part] = []
    xs, zs = _cells((x0, x1), w), _cells((z0, z1), w)
    for i in range(len(xs) - 1):
        for j in range(len(zs) - 1):
            pts = [(xs[i], y0, zs[j]), (xs[i + 1], y0, zs[j]), (xs[i + 1], y0, zs[j + 1]), (xs[i], y0, zs[j + 1])]
            if len(w) and _open(pts, w):
                continue
            parts += face(f'tub_floor{i}_{j}', 'tub', pts, (0.0, 1.0, 0.0))
    for name, axis, at, along, span in (('tub_left', 0, x1, 2, (z0, z1)), ('tub_right', 0, x0, 2, (z0, z1)),
                                        ('tub_back', 2, z0, 0, (x0, x1)), ('tub_front', 2, z1, 0, (x0, x1))):
        parts += _wall(name, axis, at, along, span, y0, y1, sill, w)
    return merge(parts, 'tub')
