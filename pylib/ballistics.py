"""The game's ballistic round, per frame, and the aim solve EDF6AutoTurret runs for it (pure Python, no numpy).

The game's bullet core moves a round once a frame (BulletControl 0x233CB0 / 0x2349D0, EDF.dll 0x678CCB46): its velocity
(m/s, C+0xB90) first takes the frame's gravity (C+0xBA0 = AmmoGravityFactor x the Havok world gravity, set at spawn
0x231E7B), then its position (C+0xB80) moves by the new velocity: v += g/60, p += v/60 (0x233DC4..0x233E4D, the
constant 1/60 at 0x176B040). The muzzle speed is AmmoSpeed x 60 (spawn 0x231E59 divides the per-frame speed by 1/60).
So after n frames a round fired at `speed` metres a frame and `elev` has gone n*speed*cos(elev) across and risen
n*speed*sin(elev) - drop*n*(n+1)/2 (drop = gravity/3600 metres a frame^2): drop*n/2 lower than the parabola.

  fly(...)         the per-frame positions, as the game steps them
  arc(...)         elevation and frames to a point, the high root or the low (autoturret/src/plugin.cpp Ballistic)
  impact(...)      frames until a round fired at `elev` is back down at `dy` (and how far it went)
  envelope(...)    what a launcher reaches: the most range, the high arc's least range at its elevation stop, times
"""
from __future__ import annotations

import math

FPS = 60.0
# The world gravity, m/s^2: measured in game (autoturret/docs/re-notes.md "Gravity and the ballistic solve"); the
# plugins read it live (common/edf/weapon.h WorldGravity), this is the offline model's.
GRAVITY = 14.7


def drop_per_frame(gravity: float = GRAVITY, factor: float = 1.0) -> float:
    """Metres a frame^2 a round of AmmoGravityFactor `factor` falls by (the autoturret's Shot.drop)."""
    return gravity * factor / (FPS * FPS)


def fly(speed: float, elev: float, drop: float, frames: int) -> list[tuple[float, float]]:
    """The round's (across, up) after each of `frames` frames, stepped as the game steps it (v += drop, p += v)."""
    vx, vy = speed * math.cos(elev), speed * math.sin(elev)
    x = y = 0.0
    out = []
    for _ in range(frames):
        vy -= drop
        x += vx
        y += vy
        out.append((x, y))
    return out


def _root(x: float, y: float, v: float, a: float, high: bool) -> float | None:
    """The parabola's launch elevation through (x, y): v m/frame, a m/frame^2; None out of reach."""
    disc = v ** 4 - a * (a * x * x + 2.0 * y * v * v)
    if disc < 0.0:
        return None
    s = math.sqrt(disc)
    return math.atan((v * v + s) / (a * x)) if high else math.atan((v * v - s) / (a * x))


def arc(x: float, y: float, speed: float, drop: float, high: bool, passes: int = 3) -> tuple[float, float] | None:
    """(elevation rad, frames) to hit (x across, y up) on the high or the low arc, None out of reach. The parabola's
    root aimed drop*n/2 over the point (the per-frame step falls that much more by frame n), `passes` times: what
    autoturret/src/plugin.cpp Ballistic does."""
    if speed <= 0.01:
        return None
    if drop <= 0.0 or x < 0.01:
        return math.atan2(y, x), math.hypot(x, y) / speed
    n = 0.0
    e = 0.0
    for _ in range(passes):
        r = _root(x, y + drop * n * 0.5, speed, drop, high)
        if r is None:
            return None
        e = r
        n = x / (speed * math.cos(e))
    return e, n


def impact(speed: float, elev: float, drop: float, dy: float = 0.0) -> tuple[float, float]:
    """(frames, across) until a round fired at `elev` comes back down to `dy` (up positive), as the game steps it:
    the larger n with n*vy - drop*n*(n+1)/2 = dy (fractional: where in that frame it crosses)."""
    vy, vx = speed * math.sin(elev), speed * math.cos(elev)
    a, b, c = drop * 0.5, drop * 0.5 - vy, dy
    n = (-b + math.sqrt(b * b - 4.0 * a * c)) / (2.0 * a)
    return n, n * vx


def envelope(speed: float, drop: float, max_elev: float) -> dict[str, float]:
    """What a launcher on flat ground reaches (metres, seconds): the most range and its elevation (the per-frame step
    peaks a hair under 45 deg), the high arc's least range (at the elevation stop `max_elev`), and the flight times of
    both, the longest flight being the one at the stop."""
    best_e, best_x = 0.0, 0.0
    for k in range(1, 900):
        e = math.radians(k * 0.1)
        _, x = impact(speed, e, drop)
        if x > best_x:
            best_e, best_x = e, x
    t_best, _ = impact(speed, best_e, drop)
    t_stop, x_stop = impact(speed, max_elev, drop)
    return {'max_range': best_x, 'max_range_elev': math.degrees(best_e), 'max_range_time': t_best / FPS,
            'high_min_range': x_stop, 'high_min_time': t_stop / FPS}
