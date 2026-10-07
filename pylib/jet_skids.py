"""Fixed landing skids for a hover craft jet model (pylib/jet_models.py Recipe.skids): the drone. Pure Python (pylib:
mdb, mdb_jet, graft_pure, jet_gear; no numpy).

    add_skids(md, spec)     -> Mdb      the model (unscaled, ungrounded) with its two skids
    check_skids(md, spec, scale, base) -> dict  raises SkidCheckError on any failure; the numbers it measured

Why: the stock PD607 airstrike drone never lands, so it has nothing to stand on. Grounded (jet_models.grounded: its
lowest point onto the origin, where the game puts a vehicle on the ground) its lowest point was the gun pod under its
belly (bone gun_tilt, 0.66 m under the body at x 3), so a parked or landed drone sat on its gun and its collision
(pylib/aircraft_collision.py, measured off the model) touched the ground there and nowhere else: it rocked on one
point on uneven ground. The skids are the helicopter kind (two rails along z either side of the gun pod, each on two
struts up into the body's underside), their rails the model's lowest points, level, `drop` under the gun pod: the
grounded drone stands on them, its gun clear of the ground, and the collision measured off it has its contact on
both rails (two 2.8 m lines, 2.4 m apart at x 3: it sits on slightly uneven ground instead of balancing).

They are not gear: no bone of their own (src/gear.cpp poses only the gear_* bones), skinned with one influence to the
model's mesh bone (the bone the V506 body drives: jet_models.mesh_bone), so they move as the body and nothing in the
plugin sees them. One mesh, in the model's own material and vertex layout; every vertex takes the UVs of the body
vertex nearest to the first strut's top (one even colour: the body's paint where the strut goes in). Tubes: rings of
SEGS vertices (smooth radial normals, tangent along the tube), flat caps; every triangle wound as its normals.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, replace

import graft_pure as g
from graft_pure import Vec3
from jet_gear import model_tris, underside
from mdb import Mdb, Mesh
from mdb_jet import pack_vertex, vertex_table

SEGS = 12       # vertices round a tube (a multiple of 4: one points straight down on a level rail)
ARC = 4         # steps of each rail end's upturn


class SkidCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O."""
    if not ok:
        raise SkidCheckError(msg)


@dataclass(frozen=True)
class SkidSpec:
    """Where a model's skids go, in its source metres (before jet_models' scale)."""
    rail_x: float                    # each rail's axis at x = +-rail_x ...
    rail_z: tuple[float, float]      # ... level from z0 to z1 ...
    upturn: float                    # ... its ends curving up this radius (a 60 deg arc) past them
    strut_x: float                   # each strut's top at x = +-strut_x ...
    strut_z: tuple[float, ...]       # ... at these z, its foot on the rail's axis at the same z ...
    embed: float                     # ... its top this far over the body's underside there (into the body)
    drop: float                      # every rail's bottom this far under the model's lowest vertex
    rail_r: float
    strut_r: float


# The drone (PD607, x 3.0 in jet_models.MODELS): its body 3.5 m wide and long, its gun pod (x within +-0.29 m, z -0.32
# .. 3.91 m) hanging 0.66 m under the body's lowest point; the body's underside at x +-1.0 m is 1.0 .. 1.1 m over the
# pod's bottom. The rails at x +-1.2 m (0.9 m clear of the pod, inside the body's 1.75 m half width), 2.7 m level (z
# +-1.35, inside the body's +-1.76 m) and turned up 0.15 m at each end, 0.25 m under the pod; their struts 1.8 m apart
# into the underside at x +-1.0 m. Rails 0.12 m thick, struts 0.09 m. (Source metres: those / 3.)
SPECS: dict[str, SkidSpec] = {
    'pd607': SkidSpec(rail_x=0.4, rail_z=(-0.45, 0.45), upturn=0.1, strut_x=1.0 / 3, strut_z=(-0.3, 0.3),
                      embed=0.02, drop=0.25 / 3, rail_r=0.02, strut_r=0.015),
}


# ------------------------------------------------------------------------------------------ geometry

def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _unit(a: Vec3) -> Vec3:
    n = math.sqrt(_dot(a, a))
    _req(n > 1e-9, 'a tube with no length')
    return (a[0] / n, a[1] / n, a[2] / n)


Vertex = tuple[Vec3, Vec3, Vec3]   # position, normal, tangent


@dataclass
class Shape:
    verts: list[Vertex]
    tris: list[tuple[int, int, int]]

    def add(self, p: Vec3, n: Vec3, t: Vec3) -> int:
        self.verts.append((p, n, t))
        return len(self.verts) - 1

    def tri(self, a: int, b: int, c: int) -> None:
        """Triangle a b c, wound so that its face normal points along its corners' normals."""
        pa, pb, pc = (self.verts[i][0] for i in (a, b, c))
        n = tuple(sum(self.verts[i][1][k] for i in (a, b, c)) for k in range(3))
        self.tris.append((a, b, c) if _dot(_cross(_sub(pb, pa), _sub(pc, pa)), n) > 0 else (a, c, b))  # type: ignore[arg-type]


def tube(shape: Shape, path: list[Vec3], r: float) -> None:
    """A round tube of radius `r` through the points `path` (each ring across the path's direction there), capped."""
    rings = []
    for k, c in enumerate(path):
        d = _unit(_sub(path[min(k + 1, len(path) - 1)], path[max(k - 1, 0)]))
        ref = (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)
        a = _unit(_cross(ref, d))
        b = _cross(d, a)
        ring = []
        for s in range(SEGS):
            ph = 2 * math.pi * s / SEGS
            n = tuple(a[i] * math.cos(ph) + b[i] * math.sin(ph) for i in range(3))
            ring.append(shape.add(tuple(c[i] + r * n[i] for i in range(3)), n, d))  # type: ignore[arg-type]
        rings.append((ring, d))
    for (a, _da), (b, _db) in zip(rings, rings[1:]):
        for s in range(SEGS):
            j = (s + 1) % SEGS
            shape.tri(a[s], b[s], b[j])
            shape.tri(a[s], b[j], a[j])
    for (ring, d), c, sign in ((rings[0], path[0], -1.0), (rings[-1], path[-1], 1.0)):
        n = (d[0] * sign, d[1] * sign, d[2] * sign)
        t = shape.verts[ring[0]][1]       # any direction across the tube
        cap = [shape.add(shape.verts[i][0], n, t) for i in ring]
        m = shape.add(c, n, t)
        for s in range(SEGS):
            shape.tri(m, cap[s], cap[(s + 1) % SEGS])


def rail_path(spec: SkidSpec, x: float, y: float) -> list[Vec3]:
    """A rail's axis at (x, y): level from rail_z[0] to rail_z[1], each end turned up along a 60 deg arc."""
    z0, z1 = spec.rail_z
    back = [(x, y + spec.upturn * (1 - math.cos(a)), z0 - spec.upturn * math.sin(a))
            for a in (math.radians(60) * k / ARC for k in range(ARC, 0, -1))]
    front = [(x, y + spec.upturn * (1 - math.cos(a)), z1 + spec.upturn * math.sin(a))
             for a in (math.radians(60) * k / ARC for k in range(1, ARC + 1))]
    return back + [(x, y, z0), (x, y, z1)] + front   # type: ignore[return-value]


def skid_shape(md: Mdb, spec: SkidSpec) -> tuple[Shape, list[Vec3], float]:
    """The skids' geometry in `md`'s model space (bind pose), the struts' tops and the ground plane (the rails' bottom)."""
    P, T = model_tris(md)
    ground = min(p[1] for p in P) - spec.drop
    axis_y = ground + spec.rail_r
    shape = Shape([], [])
    tops: list[Vec3] = []
    for sign in (1.0, -1.0):
        tube(shape, rail_path(spec, sign * spec.rail_x, axis_y), spec.rail_r)
        for z in spec.strut_z:
            skin = underside(P, T, sign * spec.strut_x, z)
            _req(skin is not None, f'no underside over the strut at ({sign * spec.strut_x}, {z})')
            top = (sign * spec.strut_x, skin + spec.embed, z)  # type: ignore[operator]
            tube(shape, [(sign * spec.rail_x, axis_y, z), top], spec.strut_r)
            tops.append(top)
    return shape, tops, ground


# ------------------------------------------------------------------------------------------ mesh

def add_skids(md: Mdb, spec: SkidSpec) -> Mdb:
    """`md` (one object, its meshes skinned) with the skids (see the module doc) as one more mesh of its first mesh's
    material and layout, skinned to its mesh bone; that bone's and the object bone's bounds taken over them too."""
    import jet_models   # its mesh bone (the bone the V506 body drives)
    _req(len(md.objects) == 1 and all(me.flags[1] for me in md.objects[0].meshes), 'not one skinned object')
    body = jet_models.mesh_bone(md)
    tmpl = md.objects[0].meshes[0]
    shape, tops, _ground = skid_shape(md, spec)
    keys, rows = vertex_table(tmpl)
    pos = g.mesh_positions(tmpl)
    bi, _bw = g.skin_columns(tmpl)
    near = min((v for v in range(tmpl.nverts) if int(bi[v][0]) == body),
               key=lambda v: sum((pos[v][c] - tops[0][c]) ** 2 for c in range(3)))
    paint = rows[near]
    out = []
    for p, n, t in shape.verts:
        b = _cross(n, t)
        row = []
        for key, x in zip(keys, paint):
            kind = key.split(':')[0].lower()
            vec = {'position': p, 'normal': n, 'tangent': t, 'binormal': b}.get(kind)
            if vec is not None:
                row.append(tuple(vec) + tuple(x[3:]))
            elif kind == 'blendweight':
                row.append((1.0,) + (0.0,) * (len(x) - 1))
            elif kind == 'blendindices':
                row.append((body,) + (0,) * (len(x) - 1))
            elif kind == 'texcoord':
                row.append(x)
            else:
                raise SkidCheckError(f'vertex element {key} of the model is not handled')
        out.append(pack_vertex(tmpl.elems, tmpl.vsize, row))
    _req(len(out) < 0x10000, f'{len(out)} vertices')
    o = md.objects[0]
    mesh = Mesh(tmpl.flags, tmpl.material, tmpl.unk08, tmpl.vsize, tmpl.elems, len(o.meshes), b''.join(out),
                struct.pack(f'<{3 * len(shape.tris)}H', *(i for t in shape.tris for i in t)))
    md = replace(md, objects=[replace(o, meshes=o.meshes + [mesh])], buffer_order=None)
    return g.recompute_bounds(md, {body})


def skid_mesh(md: Mdb) -> Mesh:
    """The skids' mesh of a model add_skids made (its object's last mesh)."""
    return md.objects[0].meshes[-1]


# ------------------------------------------------------------------------------------------ checks

def check_skids(md: Mdb, spec: SkidSpec, scale: float = 1.0) -> dict[str, object]:
    """The skids of a finished model (after jet_models' scale and grounding, `scale` that scale; its last mesh): its
    lowest points are both rails' bottoms, level (one ground plane, within a half float's step), and nothing else
    of the model comes within drop x scale of it (less a half float's step): the gun pod and the body clear the
    ground; every strut's top is inside the body (over its underside there); every triangle wound as its vertices'
    normals. Returns what it measured (model metres)."""
    me = skid_mesh(md)
    P = g.mesh_positions(me)
    lo = min(p[1] for p in P)
    others = [p for o in md.objects for m in o.meshes if m is not me for p in g.mesh_positions(m)]
    rest = min(p[1] for p in others)
    step = 2e-3 * max(scale, 1.0)            # half floats a few metres out
    _req(abs(lo) < step, f'the skids are not the model\'s bottom: {lo}')
    rails = {}
    for sign in (1.0, -1.0):
        side = [p for p in P if p[0] * sign > 0 and abs(p[1] - lo) < step]
        _req(len(side) >= 2, f'rail at x {sign}: {len(side)} contact vertices')
        rails[sign] = (min(p[2] for p in side), max(p[2] for p in side))
    _req(rest - lo > spec.drop * scale - step, f'the model reaches {rest - lo:.3f} m over the skids\' bottom, under drop')
    keys, rows = vertex_table(me)
    ni = next(k for k, key in enumerate(keys) if key.split(':')[0].lower() == 'normal')
    wrong = 0
    for a, b, c in g.triangles(me):
        pa, pb, pc = P[a], P[b], P[c]
        n = tuple(rows[a][ni][k] + rows[b][ni][k] + rows[c][ni][k] for k in range(3))
        if _dot(_cross(_sub(pb, pa), _sub(pc, pa)), n) <= 0:   # type: ignore[arg-type]
            wrong += 1
    _req(wrong == 0, f'{wrong} skid triangles wound against their normals')
    BP, BT = model_tris(replace(md, objects=[replace(md.objects[0], meshes=md.objects[0].meshes[:-1])]))
    tops = 0
    for sign in (1.0, -1.0):
        for z in spec.strut_z:
            x, zz = sign * spec.strut_x * scale, z * scale
            skin = underside(BP, BT, x, zz)
            _req(skin is not None, f'no body over the strut at ({x:.2f}, {zz:.2f})')
            high = max((p for p in P if abs(p[0] - x) < spec.strut_r * scale * 1.5 and abs(p[2] - zz) < spec.strut_r * scale * 1.5),
                       key=lambda p: p[1])
            _req(high[1] > skin, f'the strut at ({x:.2f}, {zz:.2f}) ends {skin - high[1]:.3f} m under the body')  # type: ignore[operator]
            tops += 1
    return {'ground_y': round(lo, 4), 'clearance': round(rest - lo, 4), 'rails_z': {s: [round(v, 3) for v in r] for s, r in rails.items()},
            'struts': tops, 'triangles': len(g.triangles(me)), 'vertices': me.nverts}
