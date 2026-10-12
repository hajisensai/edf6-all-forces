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

from cockpit_shapes import box, cylinder, frame, panel, quad, two_sided
from cockpits import Kit, KitMaterial, Screen

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
    """A tank turret's gunner station after the real one (an M1A1's, US Army photographs, public domain: textures
    photo_m1a1*, <model dir>/cockpits/CREDITS_photos.txt; the user, 2026-10-13: "参考现实的座舱或者座舱图片"). A gunner has
    no window: he sees through his primary sight, so the opening ahead (as wide as the room allows, right of the gun's
    cradle) is the sight's field, its rim the eyepiece's black rubber with the brow pad on its lower edge, the reticle the
    HUD's. Round it as the photographs show it, each photograph on a face turned to the eye (the eye does not move
    inside the turret, so a photograph there reads as the station itself): under the sight the eyepiece and the red
    handle of the turret control, the black breech guard on the left, the cream turret wall with its fire-control
    boxes and cables on the right, the wall and hatch ring overhead; the gunner's two screens low right."""
    r = _room(cabin, {'left': 0.9, 'right': 0.9, 'up': 0.35, 'down': 0.8, 'ahead': 0.6, 'back': 0.8},
              {'left': 0.15, 'right': 0.3, 'up': 0.12, 'down': 0.25, 'ahead': 0.35, 'back': 0.2}, 'tank_gunner')
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts = []
    # The sight's field: from just inside the right wall to just inside the left one, at eye height.
    x0, x1 = -R + 0.06, L - 0.03          # its right and left edges (+x is left)
    sw, sh = x1 - x0, min(0.3, 0.55 * (U + D))
    sx, sy = (x0 + x1) / 2, min(0.0, U - sh / 2 - 0.03)
    top, low = sy + sh / 2, sy - sh / 2
    wide, mid = L + R, (L - R) / 2
    if U - top > 0.01:
        parts.append(panel('wall_above', 'photo_m1a1tc_turretwall', (mid, (U + top) / 2, A), wide, U - top, (0, 0, -1),
                           uv=(0.0, 0.0, 1.0, max(0.15, (U - top) / wide))))
    # Under the sight: the eyepiece and the turret control's handle, the face tipped back toward the eye.
    under = low + D
    parts.append(quad('under_sight', 'photo_m1a1gps_gas_controls', (x1, -D, A - 0.18), (x0, -D, A - 0.18),
                      (x0, low, A), (x1, low, A)))
    for side, a0, a1 in (('l', x1, L), ('r', -R, x0)):
        if a1 - a0 > 0.01:
            parts.append(panel(f'wall_side_{side}', 'photo_m1a1gps_turretwall', ((a0 + a1) / 2, sy, A), a1 - a0, sh, (0, 0, -1)))
    parts.append(frame('eyepiece_rim', 'rubber', (sx, sy, A - 0.01), sw + 0.05, sh + 0.05, 0.035, (0, 0, -1), 0.05))
    parts.append(box('brow_pad', 'rubber', (sx, low - 0.03, A - 0.06), (min(0.24, sw * 0.4), 0.035, 0.07)))
    # The left: the breech guard, black, standing from the floor to the roof, turned to the eye.
    depth = min(A - 0.02, U + D)   # a square of it, from the front back: a long strip would stretch the photograph
    parts.append(quad('breech_guard', 'photo_m1a1int_breech', (L, -D, A - 0.02 - depth), (L, -D, A - 0.02),
                      (L, U, A - 0.02), (L, U, A - 0.02 - depth)))
    # The right: the turret wall with the fire-control boxes and cables, and its face toward the front.
    parts.append(quad('wall_right', 'photo_m1a1int_turretwall', (-R, -D, A - 0.02), (-R, -D, -min(B, 0.6)),
                      (-R, U, -min(B, 0.6)), (-R, U, A - 0.02)))
    # Overhead and behind: the cream wall (no front face: the faces above are that), the hatch ring.
    parts.append(box('shell', 'paint_interior', (mid, (U - D) / 2, (A - B) / 2), (wide, U + D, A + B), inward=True,
                     tile=0.8, faces='yYZ'))
    ring = min(0.28, 0.45 * wide, 0.45 * (A + B))
    parts.append(cylinder('hatch_ring', 'paint_olive', (mid, U - 0.03, -min(B, 0.5) / 2),
                          (mid, U - 0.005, -min(B, 0.5) / 2), ring, 20))
    # The gunner's two read-outs low right, on the wall's corner, turned to the eye.
    screens = []
    scr_w, scr_h = min(0.16, R * 0.4), min(0.1, D * 0.35)
    if scr_w > 0.08 and scr_h > 0.05:
        for k, kind in enumerate(('weapons', 'status')):
            c = (-R + 0.03 + scr_w / 2, -D + 0.06 + scr_h / 2 + k * (scr_h + 0.02), A * 0.55)
            facing = (0.55, 0.25, -0.8)
            screens.append(Screen(kind, c, facing, scr_w, scr_h))
            parts.append(panel(f'screen_{kind}', 'screen', c, scr_w, scr_h, facing))
    return Kit(parts, _materials('paint_interior', 'paint_olive', 'rubber', 'screen', 'photo_m1a1tc_turretwall',
                                 'photo_m1a1gps_gas_controls', 'photo_m1a1gps_turretwall', 'photo_m1a1int_breech',
                                 'photo_m1a1int_turretwall'), screens=tuple(screens))


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
    readout = Screen('weapons', (-0.36, gy + 0.06, 0.45), (0.4, 0.3, -0.85), 0.16, 0.11)
    parts.append(two_sided(panel('readout', 'screen', readout.centre, readout.width, readout.height, readout.facing)))
    return Kit(parts, _materials('paint_olive', 'metal_dark', 'rubber', 'screen'),
               frozenset(p.name for p in parts), (readout,))   # open air: all of it is seen from outside


def remote_display(cabin: Cabin) -> Kit:
    """An unmanned weapon station's seat (the user, 2026-10-13: the Blacker's side guns "明明是无人炮塔"; "哪怕弄成显示器那种
    显示都行"): no cockpit round a gun nobody stands at. The eye is the gun's camera (the fit puts it at the gun's sight /
    camera locator, riding its pitch bone) and the whole view is the operator's monitor: the plugin draws the station's
    display over it (a sensor screen: hud.cpp RwsDisplay)."""
    del cabin   # open air round the camera: nothing to fit
    return Kit([], {}, screens=(Screen('sensor', (0.0, 0.0, 1.0), (0.0, 0.0, -1.0), 0.4, 0.3),))


KITS = {
    'tank_gunner': tank_gunner,
    'gun_station': gun_station,
    'remote_display': remote_display,
}


def _family(module: str) -> dict:
    """Another family's kits (pylib/cockpit_kits_<family>.py: each module its own KITS), merged into KITS."""
    return __import__(module).KITS


for _name in ('cockpit_kits_ground', 'cockpit_kits_air', 'cockpit_kits_mech'):
    _more = _family(_name)
    if set(_more) & set(KITS):
        raise ValueError(f'{_name}: kit names taken: {sorted(set(_more) & set(KITS))}')
    KITS.update(_more)
