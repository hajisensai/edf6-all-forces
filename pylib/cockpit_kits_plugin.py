"""Cockpit kits for the plugin's own vehicles (jets, gunship, transports; pylib/cockpit_kits.py merges KITS; the
conventions are its docstring's: a kit is a function of the room round its eye, returning parts in the eye's frame
laid out inside that room). The plugin's other vehicles take the families' kits (pylib/cockpit_fits_plugin.py: the
Katyusha's truck cab, the sidecar's bike, the artillery's turret gunner, a camera's monitor for the drones, the
drill, the Sazabi and the gunship's side gunner).

The plugin's aircraft are the stock bombers' and transports' hulls with no interior: seen from a seat the hull is
not drawn (the game culls its backs), so a kit is the whole cockpit, and the view ahead stays open for flying and
aiming (the user: "这是为游戏服务的"): every panel's top 11-12 degrees under the line of sight, no canopy bow or post
across the view ahead, the information on the cockpit's screens (cockpits.Screen).

    jet_f16           a fighter's cockpit after the F-16A's: its front panel photographed from the seat (threat
                      scope: 'status', stores control panel: 'weapons'), the HUD on its control panel (a thin
                      combiner frame round the 'hud' glass), the left auxiliary panel and the side-stick console
                      (an F-16's, photographed), the tub under the canopy sills, the ejection seat
    jet_f35           after the F-35's: the panoramic display in its housing (four portals: map, flight, weapons,
                      status), its throttle and side stick (photographed on the mock-up), the helmet's display as
                      the 'hud' screen (no combiner glass), the tub, the seat
    plane_deck        a transport's flight deck after an HC-130H's, from the left (pilot's) seat: the photographed
                      panel in two halves (the pilot's tablet: 'map', his attitude indicator: 'flight', the engine
                      display: 'status'), the glare shield, the throttle quadrant, the overhead panel, the
                      windscreen's posts only at the panel's ends and between its halves, the copilot's seat
    plane_deck_gun    the same deck for a gunship's pilot: the engine display's lower half is his 'weapons'
    plane_hold        a C-130's cargo hold round a passenger on the side-wall bench, looking across: the roller
                      floor, the frames and stringers of the walls and roof (photographed), the troop seats' red
                      webbing along both walls
    passenger         another passenger in the same hold: an eye only (the hold is the first passenger's kit)

The panels, consoles, floors and walls are photographs of real cockpits on faces turned to the eye (the eye never moves
inside the cockpit, so they read as the real thing): textures/plugin_*, cut by tools/prep_cockpit_plugin.py from public
domain / CC BY photographs (credits: <model dir>/cockpits/CREDITS_plugin.txt); the rest are the shared textures
(tools/make_cockpit_textures.py) and the helicopter cockpits' seat and overhead panel (cockpit_kits_air).
"""
from __future__ import annotations

import math

import obj_model as om
from cockpit_kits import TEX, Cabin, _room
from cockpit_kits_air import _overhead, _screen, _seat, _stretched, beam, wall
from cockpit_shapes import basis, box, frame, panel
from cockpits import Kit, KitFn, KitMaterial, Screen

Vec3 = tuple[float, float, float]
Box = tuple[int, int, int, int]
UV = tuple[float, float]
UVBox = tuple[float, float, float, float]


def _px(box: Box, pts: list[tuple[float, float]]) -> list[UV]:
    """Photograph pixels as (u, v) fractions of the texture cut from `box` (tools/prep_cockpit_plugin.py CROPS)."""
    x0, y0, x1, y1 = box
    return [((x - x0) / (x1 - x0), (y - y0) / (y1 - y0)) for x, y in pts]


def _pxbox(box: Box, b: Box) -> UVBox:
    (u0, v0), (u1, v1) = _px(box, [(b[0], b[1]), (b[2], b[3])])
    return (u0, v0, u1, v1)


# The panels' boxes in their photographs (tools/prep_cockpit_plugin.py CROPS: change both together), the textures'
# aspect (height / width) and, measured in the photographs, their face outlines and displays.
_F16, _C130L, _C130R = (80, 128, 1106, 582), (230, 640, 1920, 1760), (1920, 640, 3610, 1760)
ASPECT = {'plugin_f16a_panel': 454 / 1026, 'plugin_f35_panel': 0.5, 'plugin_c130_panel_l': 1120 / 1690,
          'plugin_c130_panel_r': 1120 / 1690, 'plugin_f16_console_l': 740 / 432, 'plugin_f16_console_r': 954 / 782,
          'plugin_f35_throttle': 620 / 410, 'plugin_f35_stick': 439 / 307}
F16_OUTLINE = _px(_F16, [(84, 550), (84, 345), (228, 143), (402, 138), (470, 160), (730, 160), (810, 136), (968, 138),
                         (1102, 340), (1102, 492), (862, 492), (780, 578), (482, 578), (470, 550)])
F16_COAMING = _px(_F16, [(228, 140), (968, 140)])           # the coaming's top corners
F16_RWR = _pxbox(_F16, (357, 232, 458, 334))                # the threat warning scope
F16_SCP = _pxbox(_F16, (292, 398, 443, 512))                # the stores control panel's display
F16_HUD_BASE = _pxbox(_F16, (470, 160, 730, 175))           # the HUD control panel's top: the HUD stands on it
F35_GLASS = (0.07 / 1.14, 0.16 / 1.78, 1.07 / 1.14, 1.16 / 1.78)   # the display glass in its housing
C130_EFB = _pxbox(_C130L, (466, 980, 727, 1375))            # the pilot's tablet
C130_ADI = _pxbox(_C130L, (905, 772, 1125, 986))            # his attitude indicator
C130_ENG_HI = _pxbox(_C130R, (1920, 807, 2235, 1205))       # the engine display (its right half's part)
C130_ENG_LO = _pxbox(_C130R, (1920, 1232, 2235, 1617))
C130_PILOT_U = (1010 - 230) / 1690                         # the pilot's yoke column on the left half


def _materials(parts: list[om.Part], solid: dict[str, tuple[int, int, int]] | None = None) -> dict[str, KitMaterial]:
    solid = solid or {}
    return {m: KitMaterial(solid.get(m, TEX.format(m))) for m in sorted({p.material for p in parts})}


def _toward(c: Vec3) -> Vec3:
    n = math.sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2])
    return (-c[0] / n, -c[1] / n, -c[2] / n)


def _on(centre: Vec3, facing: Vec3, width: float, height: float, uv: UV) -> Vec3:
    """The point at (u, v) on a width x height face at `centre` facing `facing` (its texture upright)."""
    r, up, _ = basis(facing)
    du, dv = (uv[0] - 0.5) * width, (0.5 - uv[1]) * height
    return tuple(centre[k] + r[k] * du + up[k] * dv for k in range(3))  # type: ignore[return-value]


def _photo_face(name: str, texture: str, centre: Vec3, width: float, height: float,
                outline: list[UV] | None = None) -> om.Part:
    """A photographed panel on a face turned to the eye, `width` x `height` at `centre`, cut to its `outline` (u, v
    round the face; None: the whole rectangle)."""
    facing = _toward(centre)
    if outline is None:
        return panel(name, texture, centre, width, height, facing)
    if sum((a[0] - b[0]) * (-a[1] - b[1]) for a, b in zip(outline, outline[1:] + outline[:1])) < 0:
        outline = list(reversed(outline))   # counter-clockwise seen from the front
    mid = (sum(u for u, _ in outline) / len(outline), sum(v for _, v in outline) / len(outline))
    verts = [om.Vertex(_on(centre, facing, width, height, uv), facing, uv) for uv in [mid] + outline]
    n = len(outline)
    return om.Part(name, texture, verts, [(0, 1 + k, 1 + (k + 1) % n) for k in range(n)])


def _display(parts: list[om.Part], screens: list[Screen], kind: str, centre: Vec3, width: float, height: float,
             box: UVBox, inset: float = 0.0) -> None:
    """A live read-out on a photographed display `box` (u0, v0, u1, v1) of the face at `centre`."""
    u0, v0, u1, v1 = box
    facing = _toward(centre)
    c = _on(centre, facing, width, height, ((u0 + u1) / 2, (v0 + v1) / 2))
    _screen(parts, screens, kind, c, facing, (u1 - u0) * width - inset, (v1 - v0) * height - inset)


def _tub(parts: list[om.Part], left: float, right: float, sill: float, floor: float, back: float, front: float,
         well_top: float, material: str = 'metal_dark') -> None:
    """A cockpit's tub round the seat: the side walls from the floor up to the sills (x `left` and -`right`), the
    floor, the foot well's front wall at `front` up to `well_top` (behind the panel), the bulkhead behind at
    -`back`; its sills' rails (the canopy's frame, low at the view's sides)."""
    def side(x: float, name: str) -> None:
        parts.append(wall(name, material, [(x, floor, -back), (x, floor, front), (x, sill, front), (x, sill, -back)],
                          tile=0.6))
    side(left, 'tub_l')
    side(-right, 'tub_r')
    parts.append(wall('tub_floor', material, [(left, floor, -back), (-right, floor, -back), (-right, floor, front),
                                              (left, floor, front)], tile=0.6))
    parts.append(wall('foot_well', material, [(left, floor, front), (-right, floor, front), (-right, well_top, front),
                                              (left, well_top, front)], tile=0.6))
    parts.append(wall('tub_back', material, [(left, floor, -back), (-right, floor, -back), (-right, sill, -back),
                                             (left, sill, -back)], tile=0.6))
    for x, s in ((left - 0.015, 'l'), (-right + 0.015, 'r')):
        parts.append(beam(f'sill_{s}', 'canopy_frame', (x, sill, -back + 0.02), (x, sill, front - 0.08), 0.03))


# ---------------------------------------------------------------------------------------------------- fighters
def _fighter(cabin: Cabin, style: str) -> Kit:
    """A single-seat fighter's cockpit after the F-16A's (style 'f16') or the F-35's ('f35'), centred on the eye."""
    name = f'jet_{style}'
    r = _room(cabin, {'left': 0.55, 'right': 0.55, 'up': 0.35, 'down': 0.85, 'ahead': 0.95, 'back': 0.6},
              {'left': 0.3, 'right': 0.3, 'up': 0.06, 'down': 0.45, 'ahead': 0.5, 'back': 0.2}, name)
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts: list[om.Part] = []
    screens: list[Screen] = []
    half = min(L, R)
    z = min(0.72 if style == 'f16' else 0.66, A - 0.06)
    top = -z * math.tan(math.radians(11.0))           # the panel's top 11 degrees under the line of sight
    floor, sill = -D, max(-D + 0.3, -0.33)
    texture = 'plugin_f16a_panel' if style == 'f16' else 'plugin_f35_panel'
    width = min(1.06 * z if style == 'f16' else 0.98 * z, 2 * half - 0.04)
    height = width * ASPECT[texture]
    if top - height < floor + 0.08:                   # a shallow tub: the panel smaller, its top where it was
        height = top - floor - 0.08
        width = height / ASPECT[texture]
    centre = (0.0, top - height / 2, z)
    lip = (top + 0.004, z - 0.02)                      # the glare shield's back edge (y, z)
    if style == 'f16':
        parts.append(_photo_face('panel', texture, centre, width, height, F16_OUTLINE))
        _display(parts, screens, 'status', centre, width, height, F16_RWR, 0.004)
        _display(parts, screens, 'weapons', centre, width, height, F16_SCP, 0.004)
        _hud(parts, screens, centre, width, height)
        coaming = [(0.5 - F16_COAMING[0][0]) * width, (0.5 - F16_COAMING[1][0]) * width]   # x of its left / right ends
    else:
        parts.append(_photo_face('panel', texture, centre, width, height))
        u0, v0, u1, v1 = F35_GLASS
        portal = (u1 - u0) / 4
        for k, kind in enumerate(('map', 'flight', 'weapons', 'status')):
            _display(parts, screens, kind, centre, width, height,
                     (u0 + k * portal + 0.006, v0 + 0.012, u0 + (k + 1) * portal - 0.006, v1 - 0.012))
        screens.append(Screen('hud', (0.0, -0.02, 0.9), (0.0, 0.0, -1.0), 0.36, 0.27))   # the helmet's display
        coaming = [width / 2 - 0.01, -width / 2 + 0.01]
    # The glare shield: from the coaming forward, narrowing toward the windscreen, low over the panel.
    depth = min(0.16, A - z - 0.02)
    if depth > 0.03:
        xl, xr = coaming
        corners = [(xl, lip[0], lip[1]), (xr, lip[0], lip[1]), (xr * 0.8, lip[0] - 0.012, lip[1] + depth),
                   (xl * 0.8, lip[0] - 0.012, lip[1] + depth)]
        parts.append(wall('glare_shield', 'rubber', corners, toward=(0.0, 1.0, lip[1])))
    # The side consoles along the tub's walls (switch panels on top), the throttle / side stick photographed on a
    # face turned to the eye standing on each.
    sides = (('plugin_f16_console_l', 'plugin_f16_console_r') if style == 'f16' else
             ('plugin_f35_throttle', 'plugin_f35_stick'))
    deck = max(floor + 0.15, sill - 0.2)               # the consoles' tops
    cw = max(0.08, min(0.15, half - 0.3))
    c0, c1 = -min(B, 0.5) + 0.05, z - 0.14
    for sign, texture_s, w in ((1, sides[0], 0.15), (-1, sides[1], 0.19)):
        wall_x = L if sign > 0 else R
        x = sign * (wall_x - cw / 2)
        parts.append(box(f'console_{sign}', 'air_tub', (x, (deck + floor) / 2, (c0 + c1) / 2),
                         (cw, deck - floor, c1 - c0), faces=('X' if sign > 0 else 'x') + 'zZ'))
        parts.append(panel(f'console_top_{sign}', 'air_ah64_console', (x, deck + 0.002, (c0 + c1) / 2), c1 - c0, cw,
                           (-0.2 * sign, 1.0, 0.0), up=(-sign, 0, 0), uv=(0, 0, max(1.0, round((c1 - c0) / 0.3)), 1)))
        h = min(w * ASPECT[texture_s], sill - deck + 0.06, 0.3)
        w = h / ASPECT[texture_s]
        c = (sign * (wall_x - cw - w / 2 + 0.05), deck + h / 2, min(0.28, z - 0.28))
        parts.append(_photo_face(f'grip_{texture_s[7:]}', texture_s, c, w, h))
    _tub(parts, L, R, sill, floor, min(B, 0.55), z + 0.04, top - 0.55 * height, 'air_tub')
    # The coaming's cheeks: from the panel's sides out to the sills, so the panel stands in the cockpit's width.
    edge = top - (F16_OUTLINE[1][1] if style == 'f16' else 0.0) * height   # the panel's side edge's top
    for sign in (1, -1):
        px, wx = sign * width / 2, sign * ((L if sign > 0 else R) - 0.005)
        low = max(top - height, sill - 0.12)
        parts.append(wall(f'cheek_{sign}', 'air_tub', [(px, max(edge, low + 0.02), z + 0.005), (px, low, z + 0.005),
                                                      (wx, low, z - 0.18), (wx, sill, z - 0.18)]))
    seat_half = max(0.17, min(0.25, half - 0.06))
    pan = max(floor + 0.05, -0.78)
    if pan + 0.94 < U - 0.02:                          # the ejection seat's head box clears the canopy
        _seat(parts, seat_half, pan, -min(B, 0.4) + 0.06)
    return Kit(parts, _materials(parts), screens=tuple(screens))


def _hud(parts: list[om.Part], screens: list[Screen], centre: Vec3, width: float, height: float) -> None:
    """The F-16's HUD on its control panel: the stand, two thin posts and a thin frame round the combiner glass (no
    glass drawn: the 'hud' screen's symbols are drawn on the world through it)."""
    u0, v0, u1, _ = F16_HUD_BASE
    base = _on(centre, _toward(centre), width, height, ((u0 + u1) / 2, v0))
    bw = (u1 - u0) * width
    parts.append(box('hud_stand', 'metal_dark', (base[0], base[1] + 0.02, base[2] - 0.05), (bw * 0.8, 0.04, 0.1)))
    gw, gh = 0.75 * bw, 0.13
    gc = (base[0], base[1] + 0.05 + gh / 2, base[2] - 0.1)
    facing = (0.0, 0.12, -1.0)
    parts.append(frame('hud_frame', 'metal_dark', gc, gw, gh, 0.006, facing, 0.008))
    for s in (1, -1):
        x = base[0] + s * (gw / 2 - 0.003)
        parts.append(beam(f'hud_post_{s}', 'metal_dark', (x, base[1] + 0.035, base[2] - 0.08),
                          (x, gc[1] - gh / 2 + 0.003, gc[2]), 0.008))
    screens.append(Screen('hud', (gc[0], gc[1], gc[2] - 0.002), facing, gw - 0.014, gh - 0.014))


def jet_f16(cabin: Cabin) -> Kit:
    return _fighter(cabin, 'f16')


def jet_f35(cabin: Cabin) -> Kit:
    return _fighter(cabin, 'f35')


# ----------------------------------------------------------------------------------------------- flight decks
def _deck(cabin: Cabin, gunship: bool) -> Kit:
    """A transport's flight deck from the left seat, after an HC-130H's: the pilot's eye over his yoke column, the
    panel's two halves (the right half the copilot's, angled to the eye like the left), the cabin to his right."""
    name = 'plane_deck_gun' if gunship else 'plane_deck'
    r = _room(cabin, {'left': 0.65, 'right': 1.75, 'up': 0.75, 'down': 1.1, 'ahead': 1.3, 'back': 0.9},
              {'left': 0.3, 'right': 0.55, 'up': 0.2, 'down': 0.6, 'ahead': 0.65, 'back': 0.3}, name)
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts: list[om.Part] = []
    screens: list[Screen] = []
    z = min(0.85, A - 0.1)
    top = -z * math.tan(math.radians(12.0))
    # Each half's width: a real one about 0.95 m, the pilot's column at C130_PILOT_U of the left one.
    half = min(0.95, (L - 0.03) / C130_PILOT_U, (R - 0.05) / (2 - C130_PILOT_U))
    h = half * ASPECT['plugin_c130_panel_l']
    if top - h < -D + 0.3:
        h = top + D - 0.3
        half = h / ASPECT['plugin_c130_panel_l']
    xl = (C130_PILOT_U - 0.5) * half                  # the left half's centre x (+x is left)
    xr = xl - half                                     # the right half's
    yc = top - h / 2
    left_c, right_c = (xl, yc, z), (xr, yc, z - 0.08)
    parts.append(_photo_face('panel_l', 'plugin_c130_panel_l', left_c, half, h))
    parts.append(_photo_face('panel_r', 'plugin_c130_panel_r', right_c, half, h))
    _display(parts, screens, 'map', left_c, half, h, C130_EFB, 0.006)
    _display(parts, screens, 'flight', left_c, half, h, C130_ADI, 0.004)
    _display(parts, screens, 'status', right_c, half, h, C130_ENG_HI, 0.004)
    if gunship:
        _display(parts, screens, 'weapons', right_c, half, h, C130_ENG_LO, 0.004)
    # The glare shield over both halves, the fabric cover stretched once over it.
    lip, depth = z - 0.06, min(0.3, A - z - 0.05)
    edge_l, edge_r = xl + half / 2, xr - half / 2
    if depth > 0.05:
        corners = [(edge_l, top + 0.02, lip), (edge_r, top + 0.02, lip - 0.08), (edge_r * 0.9, top + 0.0, lip + depth),
                   (edge_l * 0.9, top + 0.0, lip + depth)]
        parts.append(_stretched(wall('glare_shield', 'plugin_c130_glare', corners, toward=(0.0, 1.0, lip))))
    # The throttle quadrant on the pedestal between the seats, sloping down toward them.
    pc = (xl + xr) / 2
    pw = min(0.5, half * 0.55)
    ya, za, yb, zb = top - h - 0.01, z - 0.04, top - h - 0.5, z - 0.45
    if yb > -D + 0.05:
        corners = [(pc + pw / 2, ya, za), (pc - pw / 2, ya, za), (pc - pw / 2, yb, zb), (pc + pw / 2, yb, zb)]
        parts.append(_stretched(wall('pedestal', 'plugin_c130_pedestal', corners, toward=(pc, 0.4, 0.0))))
        parts.append(box('pedestal_box', 'metal_dark', (pc, (yb - D) / 2, zb + 0.15), (pw, yb + D, 0.3), faces='xXZ'))
    # The windscreen: posts at the panel's two ends and between its halves (not before the pilot), the roof.
    roof, roof_front = U, max(-B + 0.1, z - 0.22)
    for k, (x, zp) in enumerate(((edge_l, lip), ((xl + xr) / 2, lip - 0.04), (edge_r, lip - 0.08))):
        x = min(max(x, -R + 0.04), L - 0.04)
        parts.append(beam(f'post_{k}', 'canopy_frame', (x, top + 0.02, zp), (x, roof - 0.03, roof_front), 0.03))
    parts.append(wall('roof', 'air_tub', [(L, roof, -B), (-R, roof, -B), (-R, roof, roof_front),
                                          (L, roof, roof_front)]))
    parts.append(beam('bow', 'canopy_frame', (L - 0.02, roof - 0.03, roof_front), (-R + 0.02, roof - 0.03, roof_front),
                      0.04))
    if U > 0.35:
        _overhead(parts, pc, roof, -min(B, 0.5) + 0.1, roof_front - 0.03, min(0.55, 0.6 * (L + R)))
    # The walls under the side windows, the floor, the bulkhead behind the seats, the side window's post.
    sill = max(-D + 0.3, -0.3)
    _tub(parts, L, R, sill, -D, min(B, 0.85), z + 0.05, top - h, 'air_tub')
    parts.append(wall('bulkhead', 'air_tub', [(L, sill, -min(B, 0.85)), (-R, sill, -min(B, 0.85)),
                                              (-R, roof, -min(B, 0.85)), (L, roof, -min(B, 0.85))]))
    parts.append(beam('post_side', 'canopy_frame', (L - 0.03, sill, -0.1), (L - 0.03, roof - 0.03, -0.1), 0.05))
    # The seats: the pilot's behind him, the copilot's across the pedestal.
    pan = max(-D + 0.05, -0.8)
    _seat(parts, 0.24, pan, -min(B, 0.45) + 0.06)
    cx = 2 * pc
    if -cx < R - 0.25:
        parts.append(box('copilot_pan', 'air_seat', (cx, pan + 0.04, -0.1), (0.44, 0.08, 0.42)))
        parts.append(box('copilot_back', 'air_seat', (cx, pan + 0.4, -0.33), (0.42, 0.66, 0.08)))
    return Kit(parts, _materials(parts), screens=tuple(screens))


def plane_deck(cabin: Cabin) -> Kit:
    return _deck(cabin, gunship=False)


def plane_deck_gun(cabin: Cabin) -> Kit:
    return _deck(cabin, gunship=True)


# ---------------------------------------------------------------------------------------------------- the hold
def plane_hold(cabin: Cabin) -> Kit:
    """A C-130's cargo hold round a passenger seated on the troop seats along one side wall, looking across the
    hold (the fit's yaw turns him to it): in the eye's frame ahead is the far wall, back his own wall, left and
    right the hold's two ends (the cockpit's bulkhead, the closed ramp)."""
    r = _room(cabin, {'left': 2.2, 'right': 2.2, 'up': 1.4, 'down': 1.05, 'ahead': 3.2, 'back': 0.5},
              {'left': 0.5, 'right': 0.5, 'up': 0.4, 'down': 0.5, 'ahead': 0.8, 'back': 0.15}, 'plane_hold')
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    parts: list[om.Part] = []
    # The hold's section across (z, y): the side walls upright, the roof's corners chamfered (a fuselage is round:
    # a box's corners would stand out of it), the floor's a little.
    top = min(0.35 * (U + D), 0.3 * (A + B) + 0.2)
    cn, cf, low = min(top, 0.9 * B + 0.2), min(top, 0.5 * A), 0.12
    ring = [(-B, -D + low), (-B, U - cn), (-B + min(cn, 0.9 * B), U), (A - cf, U), (A, U - cf), (A, -D + low),
            (A - low, -D), (-B + min(low, 0.5 * B), -D)]
    names = ('wall_near', 'roof_near', 'roof', 'roof_far', 'wall_far', 'chamfer_far', 'floor', 'chamfer_near')
    textures = {'roof': ('plugin_c130_roof', 2.2), 'floor': ('plugin_c130_floor', 1.4)}
    for k, name in enumerate(names):
        (z0, y0), (z1, y1) = ring[k], ring[(k + 1) % len(ring)]
        tex, tile = textures.get(name, ('plugin_c130_wall', 0.9))
        parts.append(wall(name, tex, [(L, y0, z0), (L, y1, z1), (-R, y1, z1), (-R, y0, z0)], tile=tile))
    for name, x in (('end_l', L), ('end_r', -R)):
        parts.append(wall(name, 'air_tub', [(x, y, z) for z, y in ring]))
    # The troop seats along both walls: the webbing seat on its rail, the webbing back against the wall.
    seat_y = -D + min(0.42, 0.45 * (D + U))
    for tag, z, s in (('near', -B, 1.0), ('far', A, -1.0)):
        depth = min(0.4, 0.4 * (A + B) - 0.05)
        parts.append(box(f'seat_{tag}', 'plugin_webbing', (L / 2 - R / 2, seat_y, z + s * depth / 2),
                         (L + R - 0.1, 0.03, depth)))
        parts.append(box(f'rail_{tag}', 'metal_dark', (L / 2 - R / 2, seat_y - 0.03, z + s * (depth - 0.02)),
                         (L + R - 0.1, 0.04, 0.04)))
        back_h = min(0.6, U - seat_y - 0.1)
        parts.append(panel(f'back_{tag}', 'plugin_webbing', (L / 2 - R / 2, seat_y + 0.05 + back_h / 2, z + s * 0.03),
                           L + R - 0.1, back_h, (0.0, 0.0, s), uv=(0, 0, (L + R) / 0.5, back_h / 0.5)))
    return Kit(parts, _materials(parts))


def passenger(cabin: Cabin) -> Kit:
    """Another passenger in a hold another seat's kit (plane_hold) draws: only his eye."""
    del cabin
    return Kit([], {})


KITS: dict[str, KitFn] = {
    'jet_f16': jet_f16,
    'jet_f35': jet_f35,
    'plane_deck': plane_deck,
    'plane_deck_gun': plane_deck_gun,
    'plane_hold': plane_hold,
    'passenger': passenger,
}
