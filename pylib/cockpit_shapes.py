"""Primitive shapes for the cockpits (pylib/cockpit_kits.py), as obj_model Parts (game axes: +x left, +y up, +z
forward, metres; counter-clockwise front faces), each face with its own normal and planar UVs, so a texture lands on
it the way it is drawn (a panel's gauges upright, read from the eye). Pure Python, like obj_model.

    box(name, material, centre, size)                       six faces, outward (an interior wall: inward=True; `faces`
                                                            picks which: 'xXyYzZ' = +x -x +y -y +z -z)
    quad(name, material, a, b, c, d, uv)                    one face a-b-c-d (counter-clockwise seen from its front)
    panel(name, material, centre, width, height, facing)    a quad facing `facing` (its texture upright)
    frame(name, material, centre, width, height, bar, facing, depth)  a window's border: four bars round an opening
    cylinder(name, material, a, b, radius, segs)            a capped tube from a to b
    merge(parts)                                            the parts of one material as one Part
    two_sided(part)                                         its faces seen from both sides (a part outside the hull)
"""
from __future__ import annotations

import math

import obj_model as om
from obj_model import Vertex

Vec3 = tuple[float, float, float]


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _mul(a: Vec3, k: float) -> Vec3:
    return (a[0] * k, a[1] * k, a[2] * k)


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a: Vec3) -> Vec3:
    n = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    if n < 1e-12:
        raise ValueError('a zero direction')
    return (a[0] / n, a[1] / n, a[2] / n)


def quad(name: str, material: str, a: Vec3, b: Vec3, c: Vec3, d: Vec3,
         uv: tuple[tuple[float, float], ...] = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))) -> om.Part:
    """One face a-b-c-d, counter-clockwise seen from its front; `uv` at a, b, c, d (default: a bottom left of the
    texture, c top right; UV origin top left as the game's)."""
    n = _unit(_cross(_sub(b, a), _sub(c, a)))
    verts = [Vertex(p, n, t) for p, t in zip((a, b, c, d), uv)]
    return om.Part(name, material, verts, [(0, 1, 2), (0, 2, 3)])


def _basis(facing: Vec3, up: Vec3 = (0.0, 1.0, 0.0)) -> tuple[Vec3, Vec3, Vec3]:
    """(right as seen from the front, up, facing) for a face whose front looks along `facing`."""
    f = _unit(facing)
    hint = up if abs(f[0] * up[0] + f[1] * up[1] + f[2] * up[2]) < 0.99 else (0.0, 0.0, 1.0)
    # Seen from its front (looking along -f), the viewer's right is the face's... right = up x f (game: +x is left).
    right = _unit(_cross(hint, f))
    return right, _unit(_cross(f, right)), f


def panel(name: str, material: str, centre: Vec3, width: float, height: float, facing: Vec3,
          up: Vec3 = (0.0, 1.0, 0.0), uv: tuple[float, float, float, float] = (0.0, 0.0, 1.0, 1.0)) -> om.Part:
    """A width x height face at `centre` whose front looks along `facing`, its texture upright to one in front of it;
    `uv` the texture's (u0, v0, u1, v1) box on it (v0 the top)."""
    r, u, _ = _basis(facing, up)
    hw, hh = _mul(r, width / 2), _mul(u, height / 2)
    u0, v0, u1, v1 = uv
    a = _sub(_sub(centre, hw), hh)   # bottom left seen from the front
    b = _sub(_add(centre, hw), hh)
    c = _add(_add(centre, hw), hh)
    d = _add(_sub(centre, hw), hh)
    return quad(name, material, a, b, c, d, ((u0, v1), (u1, v1), (u1, v0), (u0, v0)))


def box(name: str, material: str, centre: Vec3, size: Vec3, inward: bool = False, tile: float = 0.0,
        faces: str = 'xXyYzZ') -> om.Part:
    """An axis-aligned box (`size` full lengths x, y, z), its faces outward (inward: the inside of a room), only the
    faces named in `faces` ('x' +x, 'X' -x, 'y' +y, 'Y' -y, 'z' +z, 'Z' -z: an opening where one is left out).
    `tile`: metres a texture repeat covers (0: each face takes the whole texture)."""
    x, y, z = (s / 2 for s in size)
    sides = [((1, 0, 0), (0, 1, 0), x), ((-1, 0, 0), (0, 1, 0), x), ((0, 1, 0), (0, 0, 1), y),
             ((0, -1, 0), (0, 0, 1), y), ((0, 0, 1), (0, 1, 0), z), ((0, 0, -1), (0, 1, 0), z)]
    parts = []
    for k, (n, up, dist) in enumerate(sides):
        if 'xXyYzZ'[k] not in faces:
            continue
        facing = _mul(n, -1.0) if inward else n
        w = size[2] if n[0] else size[0]
        h = size[2] if n[1] else size[1]
        if n[2]:
            w, h = size[0], size[1]
        uv = (0.0, 0.0, w / tile, h / tile) if tile else (0.0, 0.0, 1.0, 1.0)
        parts.append(panel(f'{name}{k}', material, _add(centre, _mul(n, dist)), w, h, facing, up, uv))
    return merge(parts, name)


def frame(name: str, material: str, centre: Vec3, width: float, height: float, bar: float, facing: Vec3,
          depth: float = 0.04, up: Vec3 = (0.0, 1.0, 0.0)) -> om.Part:
    """The border of a width x height window round `centre`: four bars `bar` wide, `depth` deep, the opening left
    clear (a vision slot, a windshield's frame)."""
    r, u, f = _basis(facing, up)
    side = height - 2 * bar
    bars = ((_mul(u, (height - bar) / 2), width, bar), (_mul(u, -(height - bar) / 2), width, bar),
            (_mul(r, (width - bar) / 2), bar, side), (_mul(r, -(width - bar) / 2), bar, side))
    return merge([_slab(f'{name}{k}', material, _add(centre, d), r, u, f, sx, sy, depth)
                  for k, (d, sx, sy) in enumerate(bars)], name)


def _slab(name: str, material: str, centre: Vec3, r: Vec3, u: Vec3, f: Vec3, sx: float, sy: float, sz: float) -> om.Part:
    """A box sx x sy x sz along the frame r, u, f round `centre`, its faces outward."""
    faces = []
    for axis, half, w, h, up in ((f, sz / 2, sx, sy, u), (u, sy / 2, sx, sz, f), (r, sx / 2, sz, sy, u)):
        for sign in (1.0, -1.0):
            n = _mul(axis, sign)
            faces.append(panel(f'{name}f{len(faces)}', material, _add(centre, _mul(n, half)), w, h, n, up))
    return merge(faces, name)


def cylinder(name: str, material: str, a: Vec3, b: Vec3, radius: float, segs: int = 12, cap: bool = True) -> om.Part:
    """A tube of `radius` from a to b, its sides outward, capped at both ends (UV u round it, v along it)."""
    axis = _sub(b, a)
    f = _unit(axis)
    r, u, _ = _basis(f, (0.0, 1.0, 0.0) if abs(f[1]) < 0.9 else (1.0, 0.0, 0.0))
    verts, tris = [], []
    for i in range(segs + 1):
        t = 2 * math.pi * i / segs
        n = _add(_mul(r, math.cos(t)), _mul(u, math.sin(t)))
        for end, v in ((a, 1.0), (b, 0.0)):
            verts.append(Vertex(_add(end, _mul(n, radius)), n, (i / segs, v)))
    for i in range(segs):
        p, q = 2 * i, 2 * i + 2
        tris += [(p, q, q + 1), (p, q + 1, p + 1)]
    part = om.Part(name, material, verts, tris)
    if not cap:
        return part
    caps = []
    for end, n in ((a, _mul(f, -1.0)), (b, f)):
        base = len(verts) + sum(len(c.verts) for c in caps)
        ring = [Vertex(end, n, (0.5, 0.5))]
        for i in range(segs):
            t = 2 * math.pi * i / segs
            d = _add(_mul(r, math.cos(t)), _mul(u, math.sin(t)))
            ring.append(Vertex(_add(end, _mul(d, radius)), n, (0.5 + 0.5 * math.cos(t), 0.5 - 0.5 * math.sin(t))))
        fan = []
        for i in range(segs):
            j, k = 1 + i, 1 + (i + 1) % segs
            fan.append((0, j, k) if n == f else (0, k, j))
        caps.append(om.Part(name + 'cap', material, ring, fan))
    return merge([part] + caps, name)


def merge(parts: list[om.Part], name: str | None = None) -> om.Part:
    """Parts of one material as one (their vertices kept apart: each face keeps its own normals)."""
    if not parts:
        raise ValueError('nothing to merge')
    verts, tris = [], []
    for p in parts:
        if p.material != parts[0].material:
            raise ValueError(f'{p.name}: material {p.material}, not {parts[0].material}')
        base = len(verts)
        verts += p.verts
        tris += [(a + base, b + base, c + base) for a, b, c in p.tris]
    return om.Part(name or parts[0].name, parts[0].material, verts, tris)


def two_sided(part: om.Part) -> om.Part:
    """`part` with every face also turned the other way (the game draws only front faces: a shield or a frame seen
    from outside the vehicle as well as from the seat)."""
    back = [Vertex(v.pos, (-v.normal[0], -v.normal[1], -v.normal[2]), v.uv) for v in part.verts]
    n = len(part.verts)
    return om.Part(part.name, part.material, part.verts + back,
                   list(part.tris) + [(a + n, c + n, b + n) for a, b, c in part.tris])
