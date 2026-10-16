"""Cockpit kits for mechs and walkers: a mech cockpit (pylib/cockpit_kits.py merges KITS; the conventions are its docstring's: a kit is a
function of the room round its eye, returning parts in the eye's frame laid out inside that room).

A mech's cockpit is a closed pod in its torso, laid out for playing through it (the user: "注意这是为游戏服务的"): the
visor is the whole front less a thin frame and nothing crosses it (the mech's own hull is not drawn from inside: the
game culls its backs, so the world shows through), the live screens (cockpits.Screen) sit at the view's lower corners
(status left, weapons right) and on the console under the visor (the map, a glance down), the pilot's seat with its
console and control arms below the sight line, plated walls round the rest.

Real material, not boxes: the seat, console and control arms are Romain Revert's modelled cockpit "The cockpit"
(poly.pizza, CC-BY 3.0: <model dir>/cockpits/parts/pp-scifi-cockpit-revert/) less its screen tower and roof bar (they
stood in the line of sight). The instrument housings, switches and display mounts are model geometry with a shared
matte palette; photographs of unrelated cockpits no longer stand in for these surfaces. The seat is prepared by
tools/prep_cockpit_mech.py; credits in cockpits/CREDITS_mech.txt.

Mech torsos are tight (a Begaruta's chest closes round a box 1.3 m wide and 0.7 m high about the pilot's head) and
uneven (a 49 m Balam's is huge): each mech has its own pod size (PODS, measured), pulled in further where the room
round the eye is smaller, and the seat and screens are scaled down together when it is smaller than full-size
consoles need (from the seat the cockpit then only looks closer). Seat triangles that would still cross a wall are
left out (the wall behind them is drawn). The mechs' remote guns and missile pods are not crewed stations: their
seats get cockpit_kits.remote_display (pylib/cockpit_fits_mech.py).
"""
from __future__ import annotations

import functools
import math
import os

import numpy as np

import cockpits
import obj_model as om
from cockpit_room import MARGIN, Cabin, _materials, _room
from cockpit_shapes import basis, box, cylinder, frame, merge, panel
from cockpits import Kit, KitMaterial, Screen

SEAT_ASSET = 'parts/pp-scifi-cockpit-revert/the_cockpit.glb'
SEAT_EYE = (0.33, 0.25, 0.0)    # asset frame (it faces -x, +y up): a seated pilot's eye over the seat cushion
SEAT_SCALE = 1.4                # asset units to metres at full size (the seat back about 0.75 m, the seat 1.5 m wide)
FULL = {'left': 0.62, 'right': 0.62, 'down': 0.45, 'ahead': 0.6}   # the room full-size consoles and screens need

Vec3 = tuple[float, float, float]


def _srgb(linear: tuple[float, float, float, float]) -> tuple[int, int, int]:
    """A base colour in sRGB, none darker than 40 (the asset's near-black, 0.01 linear, read as holes under the game's
    light)."""
    return tuple(max(40, int(round(255 * min(1.0, max(0.0, c)) ** (1 / 2.2)))) for c in linear[:3])  # type: ignore[return-value]


def _military(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    """One graphite palette for the console, retaining restrained red/amber switch guards."""
    import colorsys
    h, l, sat = colorsys.rgb_to_hls(*(c / 255.0 for c in rgb))
    accent = h < 0.06 or h > 0.94 or 0.07 < h < 0.15          # red, amber
    if sat >= .25 and accent:
        return tuple(round(c*.7) for c in rgb)
    grey = 0.12 + 0.30 * l
    r, g, b = colorsys.hls_to_rgb(0.26, grey, 0.045)
    return (int(r * 255), int(g * 255), int(b * 255))


@functools.lru_cache(maxsize=1)
def _seat_mesh() -> tuple[tuple[str, tuple[int, int, int], np.ndarray, np.ndarray, np.ndarray], ...]:
    """The seat asset in the eye's frame at 1 asset unit = 1 m, eye at the origin, less the screen tower in front of
    the eye and the roof bar over it (whole mesh nodes, by their centres): per material its name, sRGB colour,
    positions (n, 3), normals (n, 3) and triangles (m, 3)."""
    import gltf
    s = gltf.read(os.path.join(cockpits.asset_root(), SEAT_ASSET))
    centre = {}
    for node in np.unique(s.mesh):
        centre[node] = s.positions[s.triangles[s.mesh == node].reshape(-1)].mean(0)
    mc = np.array([centre[n] for n in s.mesh])
    tower = (mc[:, 0] < 0.15) & (mc[:, 1] > 0.12)
    roof = mc[:, 1] > 0.45
    corners = s.positions[s.triangles]
    stalks = (((corners[:, :, 1] > 0.13) & (corners[:, :, 0] < 0.1))      # the tower's feet on the console
              | ((corners[:, :, 1] > 0.05) & (corners[:, :, 0] < 0.1) & (np.abs(corners[:, :, 2]) < 0.13))).any(1)
    keep = ~(tower | roof | stalks)
    # Asset (x, y, z), facing -x, into the eye frame: left = +z, up = +y, ahead = -x.
    p = s.positions - np.array(SEAT_EYE)
    pos = np.stack([p[:, 2], p[:, 1], -p[:, 0]], 1)
    nrm = np.stack([s.normals[:, 2], s.normals[:, 1], -s.normals[:, 0]], 1)
    out = []
    for k, mat in enumerate(s.materials):
        tris = s.triangles[keep & (s.material == k)]
        if not len(tris):
            continue
        used, inv = np.unique(tris.reshape(-1), return_inverse=True)
        out.append((f'seat{k}', _military(_srgb(mat.base)), pos[used], nrm[used], inv.reshape(-1, 3)))
    return tuple(out)


def _seat(scale: float, lo: Vec3, hi: Vec3) -> tuple[list[om.Part], dict[str, KitMaterial]]:
    """The seat at `scale` (metres per asset unit), only its triangles wholly inside the box lo..hi (eye frame)."""
    parts, mats = [], {}
    lo_a, hi_a = np.array(lo), np.array(hi)
    for name, rgb, pos, nrm, tris in _seat_mesh():
        q = pos * scale
        inside = ((q >= lo_a) & (q <= hi_a)).all(1)
        t = tris[inside[tris].all(1)]
        if not len(t):
            continue
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        verts = [om.Vertex(tuple(map(float, q[i])), tuple(map(float, nrm[i])), (0.5, 0.5)) for i in used]
        tri = [tuple(map(int, r)) for r in inv.reshape(-1, 3)]
        parts.append(om.Part(name, name, verts, tri))
        mats[name] = KitMaterial(rgb)
    return parts, mats


def _toward_eye(c: Vec3) -> Vec3:
    n = math.sqrt(sum(v * v for v in c))
    return (-c[0] / n, -c[1] / n, -c[2] / n)


def _screen(kind: str, c: Vec3, w: float, h: float) -> tuple[om.Part, Screen]:
    """A live screen of `kind` at `c` turned to the eye: its dark glass part (textures/screen.png) and its Screen,
    the glass 2 mm behind the read-out."""
    f = _toward_eye(c)
    glass = (c[0] - 0.002 * f[0], c[1] - 0.002 * f[1], c[2] - 0.002 * f[2])
    return panel(f'scr_{kind}', 'screen', glass, w, h, f), Screen(kind, c, f, w, h)


CONSOLE_MATERIALS = {
    'console_shell': KitMaterial((54,59,55)),
    'console_trim': KitMaterial((85,91,82)),
    'console_button': KitMaterial((34,40,37)),
    'console_mark': KitMaterial((154,167,146)),
}


def _bezel(name: str, c: Vec3, w: float, h: float) -> list[om.Part]:
    """A recessed display with a solid rim, rear case, fasteners and keys in its own plane."""
    f = _toward_eye(c)
    right,up,_=basis(f)
    rim=.06*w
    def at(x=0.,y=0.,z=0.):
        return tuple(c[k]+right[k]*x+up[k]*y+f[k]*z for k in range(3))
    parts=[frame(name,'console_shell',at(z=-.005),w+2*rim,h+2*rim,rim,f,.024),
           frame(name+'_lip','console_trim',at(z=.006),w+.004,h+.004,.002,f,.002),
           panel(name+'_back','console_shell',at(z=-.017),w,h,tuple(-v for v in f))]
    for x in (-w/2-rim/2,w/2+rim/2):
        for y in (-h/2-rim/2,h/2+rim/2):
            parts.append(cylinder(name+'_screw','console_trim',at(x,y,.007),at(x,y,.009),rim*.15,8))
    for i in range(5):
        x=(i-2)*w*.16
        parts.append(panel(name+'_key','console_button',at(x,-h/2-rim/2,.008),w*.085,rim*.48,f))
    return parts


def _switch_bank(name: str, c: Vec3, w: float, h: float, facing: Vec3, count: int) -> list[om.Part]:
    """Small physical knobs and index marks on a matte inset, without photographic controls."""
    right,up,f=basis(facing)
    def at(x=0.,y=0.,z=0.):
        return tuple(c[k]+right[k]*x+up[k]*y+f[k]*z for k in range(3))
    parts=[panel(name,'console_shell',c,w,h,f),
           frame(name+'_rim','console_trim',at(z=.001),w,h,min(w,h)*.035,f,.004)]
    radius=min(w/count*.18,h*.18)
    for i in range(count):
        x=(i-(count-1)/2)*w/count*.84
        parts.append(cylinder(name+'_knob','console_button',at(x,0,.002),at(x,0,.009),radius,10))
        parts.append(panel(name+'_index','console_mark',at(x,0,.0095),radius*.22,radius*1.15,f))
        parts.append(panel(name+'_tick','console_mark',at(x,h*.32,.002),radius*.25,h*.06,f))
    return parts


def _at(yaw: float, pitch: float, dist: float) -> Vec3:
    """The eye-frame point `dist` away `yaw` degrees to the left and `pitch` up."""
    y, p = math.radians(yaw), math.radians(pitch)
    return (dist * math.sin(y) * math.cos(p), dist * math.sin(p), dist * math.cos(y) * math.cos(p))


LEAST = {'left': 0.2, 'right': 0.2, 'up': 0.15, 'down': 0.25, 'ahead': 0.35, 'back': 0.15}
SIDE_SCREEN = (34.0, -19.0)   # degrees left (right for the other) and down of the sight line: the view's lower corners


def _pod(cabin: Cabin, kit: str, want: Cabin, middle: str, sides: tuple[str, str]) -> Kit:
    """The pod: walls where `want` puts them (pulled in to the room), the visor the whole front less a thin frame (the
    game is played through it: nothing crosses it), the seat with its console and control arms, screens `sides` (left,
    right) at the view's lower corners and `middle` on the console under the visor."""
    r = _room(cabin, want, LEAST, kit)
    L, R, U, D, A, B = r['left'], r['right'], r['up'], r['down'], r['ahead'], r['back']
    k = min(1.0, *(r[side] / need for side, need in FULL.items()))    # the seat's and screens' scale
    parts: list[om.Part] = []
    mid = (L - R) / 2
    # The shell seen from inside, open ahead (the front wall with the visor is that).
    parts.append(box('shell', 'mech_plate', (mid, (U - D) / 2, (A - B) / 2), (L + R, U + D, A + B), inward=True,
                     tile=0.9, faces='xXyYZ'))
    # The front: the visor from 20 deg down (no lower than the console's top) to 5 cm under the roof, 5 cm in from
    # each side; solid instrument panels below and above it, with physical controls.
    vl, vr, vt = L - 0.05, R - 0.05, U - 0.05
    vb = min(D - 0.05, A * math.tan(math.radians(20)))
    parts.append(panel('wall_front_low', 'console_shell', (mid, (-D - vb) / 2, A), L + R, D - vb, (0, 0, -1)))
    parts += _switch_bank('wall_front_top',(mid,(U+vt)/2,A),L+R,U-vt,(0,0,-1),8)
    for name, x0, x1 in (('front_l', vl, L), ('front_r', -R, -vr)):
        parts.append(panel(f'wall_{name}', 'mech_plate', ((x0 + x1) / 2, (vt - vb) / 2, A), x1 - x0, vt + vb,
                           (0, 0, -1), uv=(0.0, 0.0, (x1 - x0) / 0.9, (vt + vb) / 0.9)))
    parts.append(frame('visor_frame', 'mech_frame', ((vl - vr) / 2, (vt - vb) / 2, A - 0.015), vl + vr + 0.04,
                       vt + vb + 0.04, 0.03, (0, 0, -1), 0.03))
    # The sill's physical controls lean back toward the eye.
    sill_d = min(0.12, A * 0.2)
    parts += _switch_bank('sill',(mid,-vb-.01,A-sill_d/2),L+R-.02,sill_d,(0,.8,-.6),10)
    # The pod's ribs on the side walls and the roof, behind the visor.
    for z in (A * 0.35, -B * 0.5):
        parts.append(box(f'rib{z:.2f}', 'metal_dark', (mid, U - 0.02, z), (L + R - 0.02, 0.04, 0.06)))
        for x, n in ((L - 0.02, 'l'), (-R + 0.02, 'r')):
            parts.append(box(f'rib{n}{z:.2f}', 'metal_dark', (x, (U - D) / 2, z), (0.04, U + D - 0.02, 0.06)))
    # Switch panels on the side walls by the seat.
    for x, n, f in ((L - 0.005, 'l', (-1.0, 0.0, 0.0)), (-R + 0.005, 'r', (1.0, 0.0, 0.0))):
        parts += _switch_bank(f'side_{n}',(x,-.25*k,.05),min(.45,A+B-.1)*k,
                              min(.3,U+D-.1)*k,f,4)
    # The seat, its consoles and control arms, cut to the walls.
    seat, seat_mats = _seat(SEAT_SCALE * k, (-R + MARGIN, -D + MARGIN, -B + MARGIN), (L - MARGIN, U - MARGIN, A - 0.08))
    parts += seat
    # The screens, each in its bezel: `sides` at the view's lower corners (as near as the walls let them be), `middle`
    # on the console.
    screens = []
    w, h = 0.21 * k, 0.155 * k
    for kind, sign, wall in ((sides[0], 1.0, L), (sides[1], -1.0, R)):
        dist = min(0.62 * k, A - 0.06)
        c = _at(sign * SIDE_SCREEN[0], SIDE_SCREEN[1], dist)
        if abs(c[0]) + w / 2 / 0.86 > wall - 0.03:   # pulled toward the middle to clear the side wall
            c = (sign * (wall - 0.03 - w / 2 / 0.86), c[1], c[2])
        glass, scr = _screen(kind, c, w, h)
        parts += [glass]+_bezel(f'bezel_{kind}',c,w,h)
        # The display is bolted to the sill rather than floating in front of it.
        _,up,facing=basis(scr.facing)
        foot=(c[0],-vb-.01,A-sill_d/2)
        neck=tuple(c[i]-up[i]*(h/2+.06*w)-facing[i]*.014 for i in range(3))
        parts.append(cylinder(f'mount_{kind}','console_shell',foot,neck,.009*k,10))
        screens.append(scr)
    cw, ch = 0.2 * k, 0.15 * k
    c = (-0.02 * k, -0.42 * k, 0.58 * k)   # the asset console's centre screen (asset (-0.1, -0.06, -0.015)), 1 cm proud
    glass, scr = _screen(middle, c, cw, ch)
    parts += [glass]+_bezel(f'bezel_{middle}',c,cw,ch)
    screens.append(scr)
    mats = _materials('mech_plate','mech_frame','metal_dark','screen')
    mats.update(CONSOLE_MATERIALS)
    mats.update(seat_mats)
    return Kit(_merged(parts), mats, screens=tuple(screens))


def _merged(parts: list[om.Part]) -> list[om.Part]:
    """One part per material (fewer meshes in the model)."""
    by: dict[str, list[om.Part]] = {}
    for p in parts:
        by.setdefault(p.material, []).append(p)
    return [merge(ps, f'{m}_all') if len(ps) > 1 else ps[0] for m, ps in by.items()]


# Each mech's pod: the walls (metres from the eye) as far as its torso stays closed round them. A mech's armour is
# many separate plates: a ray from a point near a seam escapes between them (cockpits.exposed), so the pod is the
# box round the eye whose every face point is enclosed (measured with cockpits.exposed on a 0.12 m grid of each face,
# grown 0.05 m a side at a time), a little inside it; the room measured straight out (clearance) is wider.
PILOT = ('map', ('status', 'weapons'))      # middle under the visor; left and right of it
PODS: dict[str, tuple[Cabin, tuple[str, tuple[str, str]], str]] = {
    'mech_begaruta': ({'left': 0.62, 'right': 0.62, 'up': 0.32, 'down': 0.33, 'ahead': 0.73, 'back': 0.37}, PILOT,
                      "Begaruta's pilot in its chest (closed box round (0, 7.2, -0.3): 0.65 0.65 0.35 0.35 1.3 0.4)."),
    'mech_nix': ({'left': 0.85, 'right': 0.85, 'up': 0.42, 'down': 0.37, 'ahead': 0.67, 'back': 0.32}, PILOT,
                 "Nix's pilot in its chest (closed box round (0, 7.2, -0.3): 0.9 0.9 0.45 0.4 0.7 0.35)."),
    'mech_oldrobot': ({'left': 0.37, 'right': 0.37, 'up': 0.18, 'down': 0.42, 'ahead': 0.5, 'back': 0.47}, PILOT,
                      "The old robot's (608) pilot in its narrow chest (closed box round (0, 7.2, -0.2): 0.4 0.4 0.2 "
                      "0.45 0.9 0.5)."),
    'mech_groundrobo': ({'left': 0.57, 'right': 0.57, 'up': 0.5, 'down': 0.42, 'ahead': 0.67, 'back': 0.37}, PILOT,
                        "The crawler's (502) driver in its body (closed box round (0, 2.0, -0.3): 0.6 0.6 0.65 0.45 "
                        "1.0 0.4)."),
    'mech_large': ({'left': 1.0, 'right': 1.0, 'up': 0.62, 'down': 0.92, 'ahead': 0.9, 'back': 0.72}, PILOT,
                   "A big mech's pilot with room to spare: a Proteus's in its head (407: closed box round (0, 13.2, 3.0), "
                   "Mk2: round (0, 12.8, 3.0)), Balam's in the front of its chest (round (0, 43, 5.5)); all at the "
                   "search's caps 1.05 1.05 0.65 0.95 1.35 0.75 (Mk2's ahead 1.1)."),
}


def _kit(name: str, want: Cabin, screens: tuple[str, tuple[str, str]], doc: str) -> cockpits.KitFn:
    def kit(cabin: Cabin) -> Kit:
        return _pod(cabin, name, want, screens[0], screens[1])
    kit.__doc__ = doc
    kit.__name__ = name
    return kit


KITS = {name: _kit(name, want, screens, doc) for name, (want, screens, doc) in PODS.items()}
