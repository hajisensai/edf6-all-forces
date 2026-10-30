"""Whether a gun camera's aim line stays clear while its gun elevates (the user, 2026-10-27: 「有些载具第一人称会被挡到，比如
温压弹火箭车，抬炮管以后看不见前面了」).

A remote_display seat's first person is the camera on its gun (src/fp_view.h GunCameraFrame): from the eye marker
vc_eye_<seat> (pylib/cockpits.py: its place on the bone the fit names) along the gun's bore (src/vhud.cpp SeatGunLine).
The direction follows the gun's elevation; the place follows only the marker's bone. An eye on the turret's yaw bone
(cannon_main) behind or under an elevating part stays put while that part swings up through its line of sight: the
TOS-1's eye 0.42 m over its launcher pack's back end was inside the pack from 31 deg up (the pack pivots at its foot
behind the eye), the Patriot's over its canister pack's back end from 26 deg. An eye on the pitch hinge moves with
the gun instead, and whatever does not elevate (the turret, the hull) swings round it.

    aim_blocked(spec, fit, elevations)  the elevations (deg) at which the line from the eye along the bore meets the
                                        model, with how far along it

The model is a wt_tracked.Spec (the War Thunder vehicles' and the emplacements' parts in game space at rest, each on
one bone of the native V603 skeleton). The V603's pitch hinges are cannon_l / cannon_r (turning about the vehicle's
x axis through their origins); the slides and any visual bone under them turn with them.
"""
from __future__ import annotations

import math

import numpy as np

# The Vehicle603_Flak's gun elevation: car_base_constraint_data's pitch hinge [1, -60, 5] (degrees, negative up:
# autoturret/docs/re-notes.md, tools/make_katyusha.py), which every plugin V603 vehicle keeps
# (tools/make_tracked_combat.py changes only the turret hinge, TURRET_LIMITS).
V603_ELEVATION = (-5.0, 60.0)
# The V603's pitch hinges, and the bones that turn with them (hinge of each).
V603_HINGE_OF = {'cannon_l': 'cannon_l', 'cannon_r': 'cannon_r', 'cannon_slide_l': 'cannon_l',
                 'cannon_slide_r': 'cannon_r'}


def elevations(lo: float = V603_ELEVATION[0], hi: float = V603_ELEVATION[1], step: float = 1.0) -> list[float]:
    n = int(round((hi - lo) / step))
    return [lo + k * (hi - lo) / n for k in range(n + 1)]


def hinge_of(spec, bone: str) -> str | None:  # noqa: ANN001 - wt_tracked.Spec
    """The pitch hinge `bone` turns with (V603_HINGE_OF, or a visual bone's parent's), None: it does not elevate."""
    parents = {v['name']: v['parent'] for v in spec.info.get('visual_bones', [])}
    seen = set()
    while bone not in V603_HINGE_OF and bone in parents and bone not in seen:
        seen.add(bone)
        bone = parents[bone]
    return V603_HINGE_OF.get(bone)


def _pitch(deg: float) -> np.ndarray:
    """Rotation about +x by `deg` up: +z (ahead) toward +y (up)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return np.array([[1.0, 0.0, 0.0], [0.0, c, s], [0.0, -s, c]])


def _first_hit(o: np.ndarray, d: np.ndarray, tris: np.ndarray) -> float | None:
    """Distance along the ray o + t d (t > 0) to the nearest of `tris` (n, 3, 3), None: it meets none."""
    v0 = tris[:, 0]
    e1, e2 = tris[:, 1] - v0, tris[:, 2] - v0
    p = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    t = o - v0
    u = np.einsum('ij,ij->i', t, p) * inv
    q = np.cross(t, e1)
    v = (q @ d) * inv
    dist = np.einsum('ij,ij->i', e2, q) * inv
    hit = ok & (u >= 0.0) & (v >= 0.0) & (u + v <= 1.0) & (dist > 1e-4)
    return float(dist[hit].min()) if hit.any() else None


def aim_blocked(spec, fit, at: list[float] | None = None) -> list[tuple[float, float]]:  # noqa: ANN001
    """(elevation deg, metres from the eye) wherever the gun camera of `fit` (a cockpits.Fit on `spec`'s model) looks
    along the bore into the model itself, over `at` (default: the V603's whole elevation range, a degree a step).
    The bore is the model's +z at rest (the guns' rest pose), raised by the elevation; the eye turns with it when its
    bone is on a hinge, and every part on a hinge about that hinge's origin."""
    origins = spec.info['origins']
    eye_hinge = hinge_of(spec, fit.bone)
    groups: dict[str | None, list[np.ndarray]] = {}
    for k, part in enumerate(spec.info['parts']):
        pos, _nrm, _uv, tri = spec.part(k)
        groups.setdefault(hinge_of(spec, part['bone']), []).append(pos.astype(np.float64)[tri.astype(np.int64)])
    tris = {h: np.vstack(g) for h, g in groups.items()}
    eye = np.asarray(fit.eye, dtype=np.float64)
    out = []
    for deg in at if at is not None else elevations():
        r = _pitch(deg)

        def turned(p: np.ndarray, hinge: str | None) -> np.ndarray:
            if hinge is None:
                return p
            pivot = np.asarray(origins[hinge], dtype=np.float64)
            return (p - pivot) @ r.T + pivot
        scene = np.vstack([turned(t, h) for h, t in tris.items()])
        hit = _first_hit(turned(eye, eye_hinge), r @ np.array([0.0, 0.0, 1.0]), scene)
        if hit is not None:
            out.append((deg, hit))
    return out
