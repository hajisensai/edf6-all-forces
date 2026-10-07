"""The Sazabi's hand-held arms, which the source model lacks (docs/sazabi-re.md §1): the beam shot rifle, the shield and
the beam tomahawk, made of flat-shaded boxes, tubes and plates in the source's own colours (its MTL's material names, so
pylib/sazabi_model.py's palette paints them), each rigid on its bone (game space, metres; the model's bind pose):

  sz_rifle      on the right hand: forward (+z) from the grip at RIFLE_GRIP; xianxiao's rifle (RIFLE_FILE,
                tools/prep_sazabi_rifle.py) when the model folder has it, its muzzle in RIFLE_INFO; else ~13 m of
                boxes, its muzzle at RIFLE_MUZZLE (both in the bone's frame)
  sz_shield     on the left forearm's outer side, its long axis along the forearm, the point at the hand
  sz_axe        the tomahawk's grip, stowed along the shield's inner face (the plugin moves it to the right hand to
                swing it: it hangs on sz_root and is placed in world terms, src/sazabi.cpp)
  sz_axe_blade  its beam blade, a glowing plate (sazabi_model GLOW_MATERIALS[BEAM]); the plugin scales it to nothing
                while stowed
  sz_muzzle, sz_missile, sz_cannon   no geometry: where the rifle's, the shield missiles' and the chest cannon's
                rounds leave (the weapons hang on them, vcobjects JETS[SAZABI_JET].weapon_bones)

joints(at) gives these bones' joints from the arm joints tools/prep_sazabi.py measured.
"""
from __future__ import annotations

import json
import math
import os
from dataclasses import replace

import obj_model as om

Vec = tuple[float, float, float]
BEAM = 'sz_beam'                       # the blade's palette colour (not in the source's MTL)
BEAM_KD = (1.0, 0.45, 0.75)            # pink, as the anime's beam tomahawk
DARK, METAL, RED, TRIM, DEEP = '17___Default', '09___Default', '11___Default', '02___Default', '04___Default'
RIFLE_FILE = 'sazabi_rifle.obj'        # in the model folder: the rifle, hand-relative (tools/prep_sazabi_rifle.py)
RIFLE_INFO = 'sazabi_rifle.json'       # ...and its muzzle, hand-relative
RIFLE_GRIP = (0.0, -1.3, 0.6)          # sz_rifle frame: the grip, in the fist
RIFLE_MUZZLE = (0.0, 0.25, 10.6)       # sz_rifle frame: where the boxes' rounds leave (the bone sz_muzzle's joint)
SHIELD_OUT = 1.7                       # m from the forearm's axis to the shield's back
SHIELD_SCALE = 0.85                    # 8.4 m point to top, 4.6 m across
AXE_HANDLE = 6.0
CANNON_AT = (0.0, 16.9, 3.3)           # the chest's front, under the cockpit hatch (the mega particle cannon)
BLADE_SPAN = 3.4


def _add(a: Vec, b: Vec) -> Vec:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a: Vec, b: Vec) -> Vec:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _mul(a: Vec, k: float) -> Vec:
    return (a[0] * k, a[1] * k, a[2] * k)


def _norm(a: Vec) -> Vec:
    n = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


def _cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Vec, b: Vec) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Builder:
    """Flat-shaded faces into one om.Part per material (each face its own vertices: hard edges)."""

    def __init__(self, bone: str) -> None:
        self.bone = bone
        self.parts: dict[str, om.Part] = {}

    def face(self, material: str, pts: list[Vec]) -> None:
        """A convex polygon, counter-clockwise seen from its front (the game's winding, obj_model)."""
        p = self.parts.setdefault(material, om.Part(self.bone, material, [], []))
        n = _norm(_cross(_sub(pts[1], pts[0]), _sub(pts[2], pts[0])))
        base = len(p.verts)
        p.verts += [om.Vertex(q, n, (0.0, 0.0)) for q in pts]
        p.tris += [(base, base + k, base + k + 1) for k in range(1, len(pts) - 1)]

    def prism(self, material: str, outline: list[tuple[float, float]], origin: Vec, u: Vec, w: Vec, n: Vec,
              back: float, front: float, side: str | None = None) -> None:
        """`outline` (counter-clockwise in u, w seen from +n) extruded along n from `back` to `front`."""
        at = lambda a, b, c: _add(origin, _add(_mul(u, a), _add(_mul(w, b), _mul(n, c))))  # noqa: E731
        top = [at(a, b, front) for a, b in outline]
        bottom = [at(a, b, back) for a, b in reversed(outline)]
        self.face(material, top)
        self.face(material, bottom)
        k = len(outline)
        for i in range(k):
            a, b = outline[i], outline[(i + 1) % k]
            self.face(side or material, [at(a[0], a[1], back), at(b[0], b[1], back), at(b[0], b[1], front),
                                         at(a[0], a[1], front)])

    def box(self, material: str, centre: Vec, axes: tuple[Vec, Vec, Vec], half: Vec) -> None:
        u, w, n = axes
        self.prism(material, [(-half[0], -half[1]), (half[0], -half[1]), (half[0], half[1]), (-half[0], half[1])],
                   centre, u, w, n, -half[2], half[2])

    def tube(self, material: str, a: Vec, b: Vec, radius: float, segs: int = 12) -> None:
        d = _norm(_sub(b, a))
        u = _norm(_cross(d, (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)))
        w = _cross(d, u)
        ring = [(radius * math.cos(2 * math.pi * k / segs), radius * math.sin(2 * math.pi * k / segs)) for k in range(segs)]
        self.prism(material, ring, a, u, w, d, 0.0, math.sqrt(_dot(_sub(b, a), _sub(b, a))))


def rifle_files(folder: str | None) -> tuple[str, str] | None:
    """(RIFLE_FILE, RIFLE_INFO) in the model folder, None when it has neither (the rifle of boxes); one without the
    other is an error (tools/prep_sazabi_rifle.py writes both)."""
    paths = [os.path.join(folder, f) for f in (RIFLE_FILE, RIFLE_INFO)] if folder else []
    have = [os.path.isfile(p) for p in paths]
    if not any(have):
        return None
    if not all(have):
        raise FileNotFoundError(f'{folder}: {RIFLE_FILE} and {RIFLE_INFO} go together (tools/prep_sazabi_rifle.py)')
    return paths[0], paths[1]


def muzzle(folder: str | None) -> Vec:
    """The rifle's muzzle in the bone's frame: its RIFLE_INFO's, else the boxes' RIFLE_MUZZLE."""
    files = rifle_files(folder)
    if files is None:
        return RIFLE_MUZZLE
    with open(files[1], encoding='utf-8') as h:
        x, y, z = json.load(h)['muzzle']
    return (float(x), float(y), float(z))


def joints(at: dict[str, Vec], folder: str | None = None) -> dict[str, Vec]:
    """The arms' joints (model space) from the arm joints: the rifle's at the right wrist (its muzzle the model
    folder's rifle's), the shield's on the left forearm's middle, the tomahawk's grip and blade along the shield's
    inner face."""
    s = shield_frame(at)
    centre, u, _w, n = s
    grip = _add(centre, _mul(n, -0.9))
    return {'sz_rifle': at['sz_hand_r'], 'sz_muzzle': _add(at['sz_hand_r'], muzzle(folder)),
            'sz_shield': _add(centre, _mul(n, -SHIELD_OUT)),
            'sz_missile': _add(centre, _add(_mul(u, -3.6 * SHIELD_SCALE), _mul(n, 1.1))),
            'sz_axe': _add(grip, _mul(u, AXE_HANDLE / 2)), 'sz_axe_blade': _add(grip, _mul(u, -AXE_HANDLE / 2)),
            'sz_cannon': CANNON_AT}


def shield_frame(at: dict[str, Vec]) -> tuple[Vec, Vec, Vec, Vec]:
    """(centre, u along the forearm to the hand, w across, n out of the arm) of the shield's back plane."""
    elbow, wrist = at['sz_forearm_l'], at['sz_hand_l']
    u = _norm(_sub(wrist, elbow))
    n = _norm(_sub((1.0, 0.0, 0.0), _mul(u, _dot((1.0, 0.0, 0.0), u))))   # out of the left arm: +x off the forearm
    w = _cross(n, u)
    mid = _mul(_add(elbow, wrist), 0.5)
    return _add(mid, _mul(n, SHIELD_OUT)), u, w, n


def rifle(at: dict[str, Vec], folder: str | None = None) -> list[om.Part]:
    """The model folder's RIFLE_FILE on the right hand when it has one, else the rifle of boxes."""
    h = at['sz_hand_r']
    files = rifle_files(folder)
    if files is not None:
        return [replace(p, name='sz_rifle', verts=[replace(v, pos=_add(v.pos, h)) for v in p.verts])
                for p in om.obj_parts(om.read_obj(files[0]), om.Conversion(flip_v=False))]
    b = Builder('sz_rifle')
    ax = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    off = lambda x, y, z: (h[0] + x, h[1] + y, h[2] + z)  # noqa: E731
    b.box(DARK, off(0.0, 0.2, 2.6), ax, (0.55, 0.85, 4.2))          # receiver
    b.box(RED, off(0.0, 1.1, 3.0), ax, (0.35, 0.12, 2.6))           # top rail stripe
    b.box(DARK, off(0.0, 0.2, -2.4), ax, (0.45, 0.7, 1.1))          # stock
    b.box(METAL, off(*RIFLE_GRIP), ax, (0.4, 0.8, 0.5))             # grip
    b.box(METAL, off(0.0, -1.1, 3.6), ax, (0.5, 0.9, 1.1))          # E-cap
    b.box(METAL, off(0.0, 1.45, 1.6), ax, (0.3, 0.3, 1.0))          # sight
    m = RIFLE_MUZZLE
    b.tube(METAL, off(0.0, m[1], 6.6), off(0.0, m[1], m[2]), 0.38)  # barrel
    b.tube(DARK, off(0.0, m[1], m[2] - 0.8), off(0.0, m[1], m[2]), 0.55)   # muzzle
    return list(b.parts.values())


def shield(at: dict[str, Vec]) -> list[om.Part]:
    """The long pointed shield, red with a yellow rim behind it and three missile ports at its top."""
    b = Builder('sz_shield')
    centre, u, w, n = shield_frame(at)
    face = [(SHIELD_SCALE * a, SHIELD_SCALE * c) for a, c in
            ((-4.4, -2.5), (2.6, -2.7), (5.0, 0.0), (2.6, 2.7), (-4.4, 2.5), (-4.9, 0.0))]
    rim = [(a * 1.035, c * 1.05) for a, c in face]
    b.prism(TRIM, rim, centre, u, w, n, 0.0, 0.25)
    b.prism(RED, face, centre, u, w, n, 0.25, 0.85, side=DEEP)
    for k in (-1.2, 0.0, 1.2):     # missile ports near the top (the elbow end)
        port = _add(centre, _add(_mul(u, -3.6 * SHIELD_SCALE), _add(_mul(w, k), _mul(n, 0.85))))
        b.tube(DARK, port, _add(port, _mul(n, 0.25)), 0.38, 10)
    return list(b.parts.values())


def axe(at: dict[str, Vec]) -> tuple[list[om.Part], list[om.Part]]:
    """(the grip on sz_axe, the beam blade on sz_axe_blade), stowed along the shield's inner face."""
    centre, u, w, n = shield_frame(at)
    grip = _add(centre, _mul(n, -0.9))
    top = _add(grip, _mul(u, -AXE_HANDLE / 2))
    handle = Builder('sz_axe')
    handle.tube(DARK, top, _add(grip, _mul(u, AXE_HANDLE / 2)), 0.28, 10)
    handle.box(METAL, top, (u, w, n), (0.5, 0.45, 0.4))           # the emitter
    blade = Builder('sz_axe_blade')
    shape = [(-0.2, 0.0), (0.6, -0.4), (1.4, BLADE_SPAN * 0.5), (0.6, BLADE_SPAN), (-0.2, BLADE_SPAN * 0.7)]
    blade.prism(BEAM, shape, top, u, w, n, -0.08, 0.08)
    return list(handle.parts.values()), list(blade.parts.values())


def parts(at: dict[str, Vec], folder: str | None = None) -> list[om.Part]:
    grip, blade = axe(at)
    return rifle(at, folder) + shield(at) + grip + blade
