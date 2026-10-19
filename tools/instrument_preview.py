"""A physical cockpit instrument (pylib/cockpit_instruments.py) drawn flat, posed by live readings, without the game.

The instrument's parts are laid out on its screen and every live control is posed the way src/cockpit_instruments.h
Pose poses its bone (shown / collapsed, a digit's segments, a bar's fill, a needle's turn, a contact's move), for the
seat that owns it. Each part is then drawn face-on, nearest last, in its material's colour: the layout, the sizes and
what each reading shows, read as the player reads the screen (no perspective, no occlusion by the cabin).

    python tools/instrument_preview.py [--out DIR] [--pylib DIR] [--lang 0..3] [--scale PX_PER_M]
        DIR/<scene>.png for every scene in SCENES (a tank's gun ready / loading, a heli's stores, an empty launcher, ...)
        --pylib: another checkout's pylib/ (to draw the instruments as that version builds them)
"""
from __future__ import annotations

import argparse
import importlib
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Fields (src/cockpit_instruments.h Field). Older pylibs know only the first twelve: their scenes read those.
(HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN, CLIMB, PITCH, POWER, ARM, SHELL, MAG, LOAD, EMPTY, PICK,
 ROW0, ROW1, ROW2, ROW3, ROWLOAD0, ROWLOAD1, ROWLOAD2, ROWLOAD3) = range(26)
COUNT = 26
# ARM's icon groups (src/cockpit_instruments.h ArmGroup) and SHELL's tags (reticle.h Shell).
GUN, CANNON, ROCKET, AAM, AGM, BOMB, DRONE, ENERGY, CHARGE = range(9)
AP, HE, BEAM, HEAT, CANISTER = 1, 2, 3, 4, 5


def arms(rows: list[tuple[int, int, float, float]], picked: int, shell: int = 0, empty: bool = False) -> dict:
    """Readings of a seat's weapons as PublishInstruments makes them: rows of (group, rounds, magazine share (<0
    unknown), load share (<0 not loading)), `picked` the row of the weapon the seat fires. A weapon loading shows its
    loading in place of its magazine's fill."""
    group, rounds, mag, load = rows[picked]
    r = {SLOT: (picked + 1, 0), AMMO: (rounds, 0), ARM: (group, 0), SHELL: (shell if shell else -1, 0),
         MAG: (rounds if mag >= 0 else -1, max(mag, 0) if load < 0 else 0), LOAD: (load * 100 if load >= 0 else -1, max(load, 0)),
         EMPTY: (1 if empty else -1, 0), PICK: (picked if len(rows) > 1 else -1, 0)}
    for i in range(4):
        shown = len(rows) > 1 and i < len(rows)
        g, n, m, ld = rows[i] if shown else (0, 0, -1, -1)
        fill = 0 if not shown or ld >= 0 else m if m >= 0 else 1 if n > 0 else 0   # as ArmReadings
        r[ROW0 + i] = (max(n, 0) if shown else -1, min(fill, 1))
        r[ROWLOAD0 + i] = (ld * 100 if shown and ld >= 0 else -1, max(ld, 0))
    return r


SCENES: dict[str, tuple[str, float, float, dict]] = {
    # name: (screen kind, width m, height m, readings)
    'tank_gun_ready': ('weapons', .11, .075, arms([(CANNON, 1, 1, -1), (GUN, 286, .57, -1)], 0, AP)),
    'tank_gun_loading': ('weapons', .11, .075, arms([(CANNON, 1, 1, .42), (GUN, 286, .57, -1)], 0, HE)),
    'tank_coax_picked': ('weapons', .11, .075, arms([(CANNON, 1, 1, -1), (GUN, 286, .57, -1)], 1)),
    'heli_stores': ('weapons', .14, .07, arms([(GUN, 742, .74, -1), (ROCKET, 14, .73, -1), (AGM, 0, 0, .65),
                                                 (AAM, 2, 1, -1)], 1)),
    'mech_reload': ('weapons', .13, .07, arms([(GUN, 0, 0, .3), (ROCKET, 8, 1, -1)], 0)),
    'launcher_empty': ('weapons', .16, .08, arms([(ROCKET, 0, 0, -1)], 0, empty=True)),
    'jet_stores': ('weapons', .13, .10, arms([(AAM, 4, -1, -1), (AGM, 2, -1, -1), (BOMB, 6, -1, -1), (GUN, 510, -1, -1)], 0)),
    'transport_tall': ('weapons', .19, .26, arms([(CANNON, 3, .6, -1), (GUN, 1200, .8, -1), (ROCKET, 0, 0, .8)], 0, HE)),
    'status': ('status', .11, .075, {HP: (3400, .62), FUEL: (40, .4), WARN: (-1, 0)}),
}


def segment(value: float, place: int, seg: int) -> bool:
    masks = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)
    if not math.isfinite(value) or value < 0 or value > 99999:
        return seg == 6
    number = int(value + .5)
    if place and number < 10 ** place:
        return False
    return bool(masks[(number // 10 ** place) % 10] >> seg & 1)


def available(r: dict, field: int, flight: bool) -> bool:
    if field not in r:
        return False
    value, _share = r[field]
    return math.isfinite(value) and (flight if field in (CLIMB, PITCH) else value >= 0)


def pose(control: str, r: dict, language: int, flight: bool = False) -> tuple[str, float]:
    """('hide' | 'show' | 'scale' | 'turn' | 'move', amount) for a control of the owning seat, as Pose."""
    bits = control.split('_')
    mode = bits[0].lower()
    nums = [int(b) for b in bits[1:]]
    field = nums[0] if nums else -1
    place = nums[1] if len(nums) > 1 else 0
    if mode == 'l':
        ok = place == 0 or available(r, place - 1, flight)
        return ('show' if field == language and ok else 'hide', 0)
    if field < 0 or field >= COUNT:
        return 'show', 0
    value, share = r.get(field, (-1, 0))
    if mode == 'v':
        return ('show' if available(r, field, flight) else 'hide', 0)
    if mode == 'u':
        return ('hide' if available(r, field, flight) else 'show', 0)
    if mode == 'e':
        return ('show' if available(r, field, flight) and int(round(value)) == place else 'hide', 0)
    if mode == 'c':
        level = 0 if share > .55 else 1 if share > .25 else 2
        return ('show' if value >= 0 and place == level else 'hide', 0)
    if mode in 'dp':
        number = max(0, min(1, share)) * 100 if mode == 'p' and value >= 0 else value
        return ('show' if segment(number, place, nums[2]) else 'hide', 0)
    if mode == 'b':
        return 'scale', max(0., min(1., share))
    if mode in 'nrqa':
        if not available(r, field, flight):
            return 'hide', 0
        most = place if place > 0 else 2000 if field == SPEED else 3000 if field == ALT else 360
        s = share if mode == 'q' else (value + most) / (2 * most) if mode == 'a' else value / most
        return 'turn', -135 + 270 * max(0., min(1., s))
    if mode == 't':
        return 'hide', 0
    return 'show', 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'tmp', 'instrument_preview'))
    ap.add_argument('--pylib', default=os.path.join(HERE, '..', 'pylib'))
    ap.add_argument('--lang', type=int, default=1, help='0 English, 1 zh-CN, 2 zh-TW, 3 ja (hudtext::Lang)')
    ap.add_argument('--scale', type=float, default=6000.0, help='pixels a metre of screen')
    ap.add_argument('--scene', action='append')
    args = ap.parse_args()
    sys.path.insert(0, os.path.abspath(args.pylib))
    ci = importlib.import_module('cockpit_instruments')
    from cockpit_shapes import basis
    from cockpits import Screen
    from PIL import Image, ImageDraw
    os.makedirs(args.out, exist_ok=True)
    for name, (kind, width, height, readings) in SCENES.items():
        if args.scene and name not in args.scene:
            continue
        s = Screen(kind, (0., 0., 0.), (0., 0., -1.), width, height)
        parts, moving = ci.instrument(s, 0)
        control = {m.part: m for m in moving}
        right, up, facing = basis(s.facing, s.up)
        dot = lambda a, b: a[0] * b[0] + a[1] * b[1] + a[2] * b[2]   # noqa: E731
        pad = 24
        w, h = int(width * args.scale), int(height * args.scale)
        img = Image.new('RGB', (w + 2 * pad, h + 2 * pad), (58, 60, 62))
        draw = ImageDraw.Draw(img)
        drawn = []
        for p in parts:
            pts = [(dot(v.pos, right), dot(v.pos, up), dot(v.pos, facing)) for v in p.verts]
            m = control.get(p.name)
            if m is not None:
                how, amount = pose(m.control, readings, args.lang)
                cx, cy = dot(m.centre, right), dot(m.centre, up)
                if how == 'hide':
                    continue
                if how == 'scale' and amount <= 0:
                    continue   # a bar with nothing to fill: no area, nothing drawn
                if how == 'scale':
                    pts = [(cx + (x - cx) * amount, y, z) for x, y, z in pts]
                elif how == 'turn':
                    a = math.radians(amount)
                    pts = [(cx + (x - cx) * math.cos(a) + (y - cy) * math.sin(a),
                            cy - (x - cx) * math.sin(a) + (y - cy) * math.cos(a), z) for x, y, z in pts]
            colour = ci.MATERIALS[p.material].albedo
            for a, b, c in p.tris:
                tri = [pts[a], pts[b], pts[c]]
                drawn.append((sum(t[2] for t in tri) / 3, colour, [(pad + (x / width + .5) * w, pad + (.5 - y / height) * h)
                                                                   for x, y, _z in tri]))
        for _depth, colour, poly in sorted(drawn, key=lambda d: d[0]):
            draw.polygon(poly, fill=tuple(colour))
        path = os.path.join(args.out, f'{name}.png')
        img.save(path)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
