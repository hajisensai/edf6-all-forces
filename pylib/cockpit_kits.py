"""The cockpit kits (pylib/cockpits.py): each a function of the room round the seat's eye (Cabin: how far the
vehicle's own surfaces are left, right, up, down, ahead and back, measured from the model, cockpits.clearance)
returning a Kit: its parts in the eye's frame (+x left, +y up, +z ahead, metres, the eye at the origin) and its
materials (textures under <model dir>/cockpits/, drawn by tools/make_cockpit_textures.py or prepared from CC0 / CC-BY
sources by tools/prep_cockpit.py; credits in <model dir>/cockpits/CREDITS.txt).

A kit lays itself out inside that room, MARGIN short of the hull, so no part of it shows through the vehicle from
outside; a part meant to be seen outside too (a gun shield, a canopy frame) is two-sided. Every kit leaves the view
ahead open (a vision slot, a windshield, a canopy): seen from inside, the vehicle's own hull is not drawn (its faces
point out and the game culls their backs), so the world shows through the opening and the vehicle can still be
driven and fought from the seat; the kit fills the rest of the view.
"""
from __future__ import annotations

import math

from cockpit_shapes import box, cylinder, frame, panel, two_sided
from cockpits import Kit, KitMaterial

TEX = 'textures/{}.png'
MARGIN = 0.02   # m: a kit's walls this far inside the hull

Cabin = dict[str, float]   # 'left', 'right', 'up', 'down', 'ahead', 'back': metres to the hull (inf: open)


class CabinTooSmall(ValueError):
    """The eye of a fit has no room for its kit: the eye is outside the hull or in a wall (fix the fit)."""


def _materials(*names: str) -> dict[str, KitMaterial]:
    return {n: KitMaterial(TEX.format(n)) for n in names}


def _room(cabin: Cabin, want: Cabin, least: Cabin, kit: str) -> Cabin:
    """Each side's wall: where the kit wants it, pulled in to MARGIN short of the hull; a side with less room than
    `least` (or open, inf, where the kit needs a hull) is an error."""
    out = {}
    for side, w in want.items():
        room = cabin[side] - MARGIN
        if not math.isfinite(cabin[side]) or room < least[side]:
            raise CabinTooSmall(f'{kit}: {side} {cabin[side]:.2f} m (needs {least[side] + MARGIN:.2f}): {cabin}')
        out[side] = min(w, room)
    return out


def tank_gunner(cabin: Cabin) -> Kit:
    """A tank turret's gunner station in the turret's room: the gunner's sight's eyepiece just below the line of
    sight, a vision slot ahead in the front wall, the fire-control console to the right, the turret control's two
    grips below, a guard plate against the gun on the left, the walls and the hatch ring round it."""
    r = _room(cabin, {'left': 0.9, 'right': 0.9, 'up': 0.35, 'down': 0.8, 'ahead': 0.6, 'back': 0.8},
              {'left': 0.15, 'right': 0.3, 'up': 0.15, 'down': 0.25, 'ahead': 0.35, 'back': 0.2}, 'tank_gunner')
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts = []
    # The front wall at A with its vision slot round the line of sight.
    sw, sh = min(0.9, 0.85 * (L + R)), min(0.28, 0.6 * (U + 0.14))
    sx = (L - R) / 2 * 0.3   # the slot a little toward the room's middle
    wide = L + R
    mid = (L - R) / 2
    top_h, low_h = U - sh / 2, D - sh / 2
    if top_h > 0.01:
        parts.append(panel('wall_front_top', 'paint_interior', (mid, U - top_h / 2, A), wide, top_h, (0, 0, -1)))
    parts.append(panel('wall_front_low', 'paint_interior', (mid, -sh / 2 - low_h / 2, A), wide, low_h, (0, 0, -1)))
    for side, x0, x1 in (('l', sx + sw / 2, L), ('r', -R, sx - sw / 2)):
        if x1 - x0 > 0.01:
            parts.append(panel(f'wall_front_{side}', 'paint_interior', ((x0 + x1) / 2, 0.0, A), x1 - x0, sh, (0, 0, -1)))
    parts.append(frame('slot_frame', 'paint_olive', (sx, 0.0, A - 0.015), sw + 0.06, sh + 0.06, 0.03, (0, 0, -1), 0.03))
    # The rest of the shell, seen from inside (no front face: the front wall is that).
    parts.append(box('shell', 'paint_interior', (mid, (U - D) / 2, (A - B) / 2), (wide, U + D, A + B), inward=True,
                     tile=0.8, faces='xXyYZ'))
    ring = min(0.28, 0.45 * wide, 0.45 * (A + B))
    parts.append(cylinder('hatch_ring', 'paint_olive', (mid, U - 0.03, -min(B, 0.5) / 2), (mid, U - 0.005, -min(B, 0.5) / 2),
                          ring, 20))
    # The gunner's sight: its eyepiece and brow pad under the line of sight, its body going into the front wall.
    ez = min(0.16, A - 0.12)
    parts.append(box('sight_body', 'paint_olive', (0.0, -0.17, (ez + A) / 2), (0.12, 0.1, A - ez)))
    parts.append(cylinder('eyepiece', 'metal_dark', (0.0, -0.15, ez + 0.03), (0.0, -0.15, ez - 0.03), 0.032, 16))
    parts.append(box('brow_pad', 'rubber', (0.0, -0.1, ez - 0.02), (0.13, 0.03, 0.04)))
    # The guard plate against the gun on the left wall.
    parts.append(box('gun_guard', 'paint_olive', (L - 0.02, -D / 2, (A - B) / 2), (0.03, D, min(0.9, A + B))))
    # The fire-control console on the right, turned to the eye; a switch panel above it.
    cw = min(0.36, 0.8 * R)
    parts.append(panel('console', 'panel_analog', (-R + cw * 0.45, -min(0.28, D * 0.6), min(0.3, A * 0.6)), cw, cw,
                       (0.55, 0.35, -0.75)))
    if U > 0.12:
        parts.append(panel('switches', 'panel_switches', (-R + 0.03, min(0.06, U * 0.4), min(0.15, A * 0.4)), 0.24,
                           min(0.24, U + 0.1), (1.0, -0.1, -0.25)))
    # The turret control's two grips, below and ahead.
    gy = -min(0.38, D - 0.12)
    for x in (0.09, -0.09):
        parts.append(cylinder(f'grip{x}', 'rubber', (x, gy - 0.12, min(0.3, A - 0.1)), (x, gy, min(0.27, A - 0.13)),
                              0.02, 10))
    parts.append(box('grip_base', 'metal_dark', (0.0, gy - 0.15, min(0.31, A - 0.09)), (0.26, 0.05, 0.1)))
    return Kit(parts, _materials('paint_interior', 'paint_olive', 'metal_dark', 'rubber', 'panel_analog',
                                 'panel_switches'))


def gun_station(cabin: Cabin) -> Kit:
    """An open gun mount on a turret or at a door (the gun itself is the vehicle's own model): the shield before it with
    its firing window (two-sided: it is seen from outside too), the two spade grips, a small readout box. Open air
    round it, so the room only bounds how far down the grips reach."""
    down = cabin['down'] - MARGIN if math.isfinite(cabin['down']) else 0.5
    parts = []
    z = 0.75
    for name, x in (('shield_l', 0.29), ('shield_r', -0.29)):
        parts.append(two_sided(panel(name, 'paint_olive', (x, -0.1, z), 0.3, 0.5, (0, 0, -1))))
    parts.append(two_sided(panel('shield_low', 'paint_olive', (0.0, -0.29, z), 0.28, 0.12, (0, 0, -1))))
    parts.append(frame('shield_rim', 'metal_dark', (0.0, -0.1, z - 0.01), 0.9, 0.52, 0.025, (0, 0, -1), 0.03))
    gy = -min(0.3, max(0.12, down - 0.05))
    for x in (0.16, -0.16):
        parts.append(cylinder(f'spade{x}', 'rubber', (x, gy - 0.06, 0.32), (x, gy + 0.07, 0.32), 0.024, 10))
    parts.append(box('spade_bar', 'metal_dark', (0.0, gy, 0.36), (0.36, 0.03, 0.03)))
    parts.append(panel('readout', 'panel_switches', (-0.36, gy, 0.45), 0.14, 0.14, (0.4, 0.5, -0.75)))
    return Kit(parts, _materials('paint_olive', 'metal_dark', 'rubber', 'panel_switches'),
               frozenset(p.name for p in parts))   # open air: all of it is seen from outside


KITS = {
    'tank_gunner': tank_gunner,
    'gun_station': gun_station,
}
