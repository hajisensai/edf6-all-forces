"""Physical cockpit instruments (pylib/cockpit_instruments.py, pylib/cockpit_retrofit.py dials) drawn flat, posed by
live readings, without the game.

The instrument's parts are laid out on its screen and every live control is posed the way src/cockpit_instruments.h
Pose poses its bone (shown / collapsed, a digit's segments, a bar's fill, a needle's turn, a compass card's turn, an
attitude card's drop and bank, a contact's move), for the seat that owns it. Each part is then drawn face-on, nearest
last, in its material's colour: the layout, the sizes and what each reading shows, as the player reads the screen (no
perspective, no occlusion by the cabin, no glow).

    python tools/instrument_preview.py [--out DIR] [--pylib DIR] [--lang 0..3] [--scale PX_PER_M] [--scene NAME ...]
        DIR/<scene>.png for every scene in SCENES (weapons screens of a tank, a heli, a jet ..., flight and nav displays
        flying and on the ground, a status screen, the retrofitted standby dials)
        --pylib: another checkout's pylib/ (to draw the instruments as that version builds them)
"""
from __future__ import annotations

import argparse
import importlib
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Fields and round classes: this checkout's pylib/cockpit_fields.py (the plugin's numbers; src/cockpit_fields.inc), loaded
# by path so that --pylib may name an older checkout without it. Fields are appended only: an older pylib reads the first.
_spec = importlib.util.spec_from_file_location('_preview_cockpit_fields', os.path.join(HERE, '..', 'pylib', 'cockpit_fields.py'))
_cf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cf)
HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN, CLIMB, PITCH, POWER = (
    _cf.HP, _cf.HULL, _cf.FUEL, _cf.SPEED, _cf.ALT, _cf.HEADING, _cf.SLOT, _cf.AMMO, _cf.WARN, _cf.CLIMB, _cf.PITCH, _cf.POWER)
ROWS, ARM, MAG, LOAD, EMPTY, PICK, ROW, ROWROUND, ROLL, COUNT = (
    _cf.ROWS, _cf.ARM, _cf.MAG, _cf.LOAD, _cf.EMPTY, _cf.PICK, _cf.ROW, _cf.ROWROUND, _cf.ROLL, _cf.COUNT)
FLIGHT_ONLY = (CLIMB, PITCH, ROLL)
MG, GUN, AP, HE, HEAT, CAN, BEAM, RKT, MSL, AAM, BOMB, SPC = (
    _cf.MG, _cf.GUN, _cf.AP, _cf.HE, _cf.HEAT, _cf.CAN, _cf.BEAM, _cf.RKT, _cf.MSL, _cf.AAM, _cf.BOMB, _cf.SPC)


def arms(rows: list[tuple[int, int, float, float]], picked: int, empty: bool = False) -> dict:
    """Readings of a seat's weapons as ArmReadings makes them: rows of (round class, rounds, magazine share (<0
    unknown), load share (<0 not loading)), `picked` the one the seat fires."""
    cls, rounds, mag, load = rows[picked]
    n = len(rows)
    first = 0 if n <= ROWS or picked < 1 else min(picked - 1, n - ROWS)
    r = {SLOT: (picked + 1, 0), AMMO: (rounds, 0), ARM: (cls, 0),
         MAG: (rounds if mag >= 0 else -1, max(mag, 0) if load < 0 else 0),
         LOAD: (load * 100 if load >= 0 else -1, max(load, 0)), EMPTY: (1 if empty else -1, 0),
         PICK: (picked - first if n > 1 else -1, 0)}
    for i in range(ROWS):
        k = first + i
        shown = n > 1 and k < n
        c, m_rounds, m, ld = rows[k] if shown else (0, 0, -1, -1)
        fill = 0 if not shown else ld if ld >= 0 else m if m >= 0 else 1 if m_rounds > 0 else 0
        r[ROW + i] = (max(m_rounds, 0) if shown else -1, min(fill, 1))
        r[ROWROUND + i] = (c if shown else -1, 0)
    return r


def flying(speed: float, alt: float, heading: float, pitch: float, roll: float, **more) -> dict:
    r = {SPEED: (speed, 0), ALT: (alt, 0), HEADING: (heading, 0), PITCH: (pitch, 0), ROLL: (roll, 0), CLIMB: (8, 0),
         'flight': True, 'contacts': more.get('contacts', ())}
    r.update(more.get('extra', {}))
    return r


def driving(speed: float, heading: float, **more) -> dict:
    return {SPEED: (speed, 0), HEADING: (heading, 0), 'flight': False, 'contacts': more.get('contacts', ())}


THREATS = ((.12, .2), (-.25, .05), (.05, -.3))   # contact offsets (cockpit_screens.inc: range .35 = 600 m)

SCENES: dict[str, tuple[str, float, float, dict]] = {
    # name: (screen kind or 'retrofit:<field>', width m, height m, readings)
    'weapons_tank_ready': ('weapons', .11, .075, arms([(AP, 1, 1, -1), (HE, 1, 1, -1), (MG, 286, .57, -1)], 0)),
    'weapons_tank_loading': ('weapons', .11, .075, arms([(AP, 0, 0, -1), (HEAT, 1, 1, .42), (MG, 286, .57, -1)], 1)),
    'weapons_tank_coax': ('weapons', .11, .075, arms([(AP, 1, 1, -1), (HE, 1, 1, -1), (MG, 286, .57, -1)], 2)),
    'weapons_heli': ('weapons', .14, .07, arms([(MG, 742, .74, -1), (RKT, 14, .73, -1), (MSL, 0, 0, .65), (AAM, 2, 1, -1)], 1)),
    'weapons_spent': ('weapons', .16, .08, arms([(RKT, 0, 0, -1)], 0, empty=True)),
    'weapons_jet': ('weapons', .13, .10, arms([(AAM, 4, -1, -1), (MSL, 2, -1, -1), (BOMB, 6, -1, -1), (MG, 510, -1, -1)], 0)),
    'weapons_tall': ('weapons', .19, .26, arms([(GUN, 3, .6, -1), (MG, 1200, .8, -1), (RKT, 0, 0, .8)], 0)),
    'flight_jet': ('flight', .13, .10, flying(620, 1840, 74, 12, 25)),
    'flight_heli': ('flight', .14, .07, flying(180, 230, 305, -6, -15)),
    'flight_tall': ('flight', .19, .26, flying(410, 3260, 152, 4, 8)),
    # A ground vehicle has no flight display (no ground kit carries one): its speed and heading are the retrofitted
    # cluster's (pylib/cockpit_retrofit.py GROUND: 160 km/h speedometer, compass) and the nav display's.
    'ground_speed': ('retrofit:speed:ground', .14, .14, driving(54, 212)),
    'ground_compass': ('retrofit:heading:ground', .14, .14, driving(54, 212)),
    'nav_jet': ('map', .13, .10, flying(620, 1840, 74, 12, 25, contacts=THREATS)),
    'nav_ground': ('map', .16, .10, driving(30, 212, contacts=THREATS[:2])),
    'status': ('status', .11, .075, {HP: (3400, .62), FUEL: (40, .4), WARN: (-1, 0)}),
    'standby_heading': ('retrofit:heading', .14, .14, flying(410, 3260, 152, 4, 8)),
    'standby_attitude': ('retrofit:pitch', .14, .14, flying(410, 3260, 152, 9, -20)),
    'standby_altitude': ('retrofit:altitude', .14, .14, flying(410, 3260, 152, 4, 8)),
    'standby_speed': ('retrofit:speed', .14, .14, flying(410, 3260, 152, 4, 8)),
}


def segment(value: float, place: int, seg: int, pad: bool = False) -> bool:
    masks = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)
    if not math.isfinite(value) or value < 0 or value > 99999:
        return seg == 6
    number = int(value + .5)
    if place and number < 10 ** place and not pad:
        return False
    return bool(masks[(number // 10 ** place) % 10] >> seg & 1)


def available(r: dict, field: int) -> bool:
    if field not in r:
        return False
    value, _share = r[field]
    return math.isfinite(value) and (r.get('flight', False) if field in FLIGHT_ONLY else value >= 0)


def pose(control: str, r: dict, language: int) -> list[tuple]:
    """The pose of a control of the owning seat, as Pose: [] hidden, else steps applied to its vertices in order:
    ('scale', k) along the bone's x, ('turn', degrees clockwise), ('move', dx, dy) in the bone's units."""
    bits = control.split('_')
    mode = bits[0].lower()
    nums = [int(b) for b in bits[1:]]
    field = nums[0] if nums else -1
    place = nums[1] if len(nums) > 1 else 0
    flight = r.get('flight', False)
    if mode == 'l':
        return [()] if field == language and (place == 0 or available(r, place - 1)) else []
    if mode == 't':
        contacts = r.get('contacts', ())
        return [('move', *contacts[field])] if field < len(contacts) else []
    if field < 0 or field >= COUNT:
        return [()]
    value, share = r.get(field, (-1, 0))
    if mode == 'v':
        return [()] if available(r, field) else []
    if mode == 'u':
        return [] if available(r, field) else [()]
    if mode == 'e':
        return [()] if available(r, field) and int(value + .5) == place else []
    if mode == 'c':
        level = 0 if share > .55 else 1 if share > .25 else 2
        return [()] if value >= 0 and place == level else []
    if mode in 'dpz':
        number = max(0, min(1, share)) * 100 if mode == 'p' and value >= 0 else value
        return [()] if segment(number, place, nums[2], mode == 'z') else []
    if mode == 'b':
        k = max(0., min(1., share))
        return [('scale', k)] if k > 0 else []
    if mode == 'h':
        if not available(r, field) or (place == 1 and flight) or (place == 2 and not flight):
            return []
        return [('turn', -value)]
    if mode == 'w':
        return [('turn', 360 * value / place)] if available(r, field) and place > 0 else []
    if mode == 'g':
        if not (available(r, field) and available(r, ROLL)):
            return []
        most = nums[2] * 1e-4
        drop = max(-most, min(most, -value * place * 1e-4))
        return [('move', 0, drop), ('turn', -r[ROLL][0])]
    if mode in 'nrqa':
        if not available(r, field):
            return []
        most = place if place > 0 else 2000 if field == SPEED else 3000 if field == ALT else 360
        k = share if mode == 'q' else (value + most) / (2 * most) if mode == 'a' else value / most
        return [('turn', -135 + 270 * max(0., min(1., k)))]
    return [()]


def build(kind: str, width: float, height: float, pylib) -> tuple[list, list, object]:  # noqa: ANN001
    """The parts and moving controls of a scene's instrument, and its screen (centre at the origin, facing -z)."""
    ci, cockpits = pylib
    s = cockpits.Screen('status' if kind.startswith('retrofit') else kind, (0., 0., 0.), (0., 0., -1.), width, height)
    if not kind.startswith('retrofit:'):
        return (*ci.instrument(s, 0), s)
    retrofit = importlib.import_module('cockpit_retrofit')
    field = {'heading': ci.HEADING, 'pitch': ci.PITCH, 'altitude': ci.ALT, 'speed': ci.SPEED}[kind.split(':')[1]]
    ground = kind.endswith(':ground')
    labels: list[list] = [[] for _ in range(4)]
    parts, controls = retrofit._dial('dial', (0., 0., 0.), (0., 0., -1.), (0., 1., 0.), min(width, height) * .9, field,
                                     ground, labels, {})
    parts = parts + [p for p in labels[1] if p.verts]
    return parts, controls, s


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
    cockpits = importlib.import_module('cockpits')
    from cockpit_shapes import basis
    from PIL import Image, ImageDraw
    os.makedirs(args.out, exist_ok=True)
    dot = lambda a, b: a[0] * b[0] + a[1] * b[1] + a[2] * b[2]   # noqa: E731
    for name, (kind, width, height, readings) in SCENES.items():
        if args.scene and name not in args.scene:
            continue
        parts, moving, s = build(kind, width, height, (ci, cockpits))
        control = {m.part: m for m in moving}
        right, up, facing = basis(s.facing, s.up)
        pad = 24
        w, h = int(width * args.scale), int(height * args.scale)
        img = Image.new('RGB', (w + 2 * pad, h + 2 * pad), (58, 60, 62))
        draw = ImageDraw.Draw(img)
        drawn = []
        materials = {**ci.MATERIALS, **getattr(importlib.import_module('cockpit_retrofit'), 'MATERIALS', {})}
        for p in parts:
            pts = [(dot(v.pos, right), dot(v.pos, up), dot(v.pos, facing)) for v in p.verts]
            m = control.get(p.name)
            if m is not None:
                steps = pose(m.control, readings, args.lang)
                if not steps:
                    continue
                cx, cy = dot(m.centre, right), dot(m.centre, up)
                ex, ey = m.extent
                for step in steps:
                    if not step:
                        continue
                    if step[0] == 'scale':
                        pts = [(cx + (x - cx) * step[1], y, z) for x, y, z in pts]
                    elif step[0] == 'turn':
                        a = math.radians(step[1])
                        pts = [(cx + (x - cx) * math.cos(a) + (y - cy) * math.sin(a),
                                cy - (x - cx) * math.sin(a) + (y - cy) * math.cos(a), z) for x, y, z in pts]
                    else:   # moved in the bone's units: the control's extent (1: metres)
                        pts = [(x + step[1] * ex, y + step[2] * ey, z) for x, y, z in pts]
            colour = materials[p.material].albedo
            if isinstance(colour, str):
                colour = (90, 90, 90)
            for a, b, c in p.tris:
                tri = [pts[a], pts[b], pts[c]]
                drawn.append((sum(t[2] for t in tri) / 3, tuple(colour),
                              [(pad + (x / width + .5) * w, pad + (.5 - y / height) * h) for x, y, _z in tri]))
        for _depth, colour, poly in sorted(drawn, key=lambda d: d[0]):
            draw.polygon(poly, fill=colour)
        path = os.path.join(args.out, f'{name}.png')
        img.save(path)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
