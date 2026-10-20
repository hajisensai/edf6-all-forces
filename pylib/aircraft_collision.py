"""Aircraft contact shapes measured from the actual rendered mesh, including its wings.

The 506 has TWO collision shapes: its flight body (normally a box) and its animation
ragdoll (normally a helicopter). We generate a tagged ragdoll body compound; body506.cpp
also uses that exact compound for the flight body. NPC, parked and requested aircraft
share these files. No game directory is written by this module.

The compound is a height-field of small solid convex cells (pylib/hkshape.py: each a hull with its own mass
properties, the compound their combination; the body proxy's massDistribution is removed so that the game derives
it from them). Triangles are clipped to
each X/Z cell before measuring its Y range, so empty corners around swept wings stay
empty. Unlike a vertex-only sampler this includes long triangles spanning many cells.
This is a conservative collision approximation, not a triangle-perfect mesh: 24 by 32
cells, at most one cell of silhouette error and a 3 cm skin for zero-thickness surfaces.
Landing gear is included in the grounded bind pose (its movement is cosmetic).
"""
from __future__ import annotations

import math
import struct
from functools import lru_cache

import hkfile
import hkshape
import jet_models
import model_pieces
from mdb import bind_world, ident, read_elem

MAGIC = 0x4544463641495231  # EDF6AIR1: native hknpShape::userData, src/body506.cpp
STOCK = 'RAGDOLL_V506_HELI.SHKT'
NX, NZ = 24, 32
SKIN = 0.03
FILES = {None: 'EDF6VC_AIRFRAME_BOMBER.SHKT',
         **{k: k.replace('.MRAB', '_AIRFRAME.SHKT') for k in jet_models.MODELS},
         **{k: f'EDF6VC_AIRFRAME_{k.upper()}.SHKT' for k in jet_models.STOCK_BOMBERS}}


def model_key(jet, model):
    """Supported rendered model, or False for aircraft built elsewhere (Primer/sub)."""
    if jet.file in jet_models.MODELS:
        return jet.file
    if jet.box_model in jet_models.STOCK_BOMBERS:
        return jet.box_model
    if jet.model is not None:
        return False
    if model == ['app:/object/edf6vc_jet.mrab', 'bomber501.mdb']:
        return None
    if model is None:
        return False  # a caller using the legacy, ungrounded stock bomber
    file = model[1].removesuffix('.mdb')
    return file if file in jet_models.STOCK_BOMBERS else False


def triangles(md):
    """Triangle vertices in the same model-space bind frame as jet_models.model_box."""
    world = bind_world(md)
    for obj in md.objects:
        for mesh in obj.meshes:
            elem = next(e for e in mesh.elems if e.name.lower() == 'position')
            raw = read_elem(mesh, elem.name, elem.channel)
            matrix = ident() if mesh.flags[1] else world[obj.bone]
            points = [tuple(sum(p[k] * matrix[4*k+c] for k in range(3)) + matrix[12+c]
                            for c in range(3)) for p in raw]
            indices = struct.unpack('<' + 'H'*(len(mesh.indices)//2), mesh.indices)
            for i in range(0, len(indices), 3):
                yield tuple(points[k] for k in indices[i:i+3])


def _clip(poly, axis, edge, sign):
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        da, db = sign*(a[axis]-edge), sign*(b[axis]-edge)
        if da >= -1e-9:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da/(da-db)
            out.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(3)))
    return out


def mesh_cells(tris, nx=NX, nz=NZ):
    """Model-space (min,max) boxes of occupied cells; the actual clipped geometry bounds."""
    tris = list(tris)
    lo, hi = jet_models.bbox([p for tri in tris for p in tri])
    dx, dz = (hi[0]-lo[0])/nx, (hi[2]-lo[2])/nz
    if not dx > 0 or not dz > 0:
        raise ValueError('aircraft has no X/Z extent')
    cells = {}
    for tri in tris:
        x0 = max(0, min(nx-1, int((min(p[0] for p in tri)-lo[0])/dx)))
        x1 = max(0, min(nx-1, int((max(p[0] for p in tri)-lo[0])/dx)))
        z0 = max(0, min(nz-1, int((min(p[2] for p in tri)-lo[2])/dz)))
        z1 = max(0, min(nz-1, int((max(p[2] for p in tri)-lo[2])/dz)))
        for x in range(x0, x1+1):
            for z in range(z0, z1+1):
                poly = list(tri)
                for axis, edge, sign in ((0, lo[0]+x*dx, 1), (0, lo[0]+(x+1)*dx, -1),
                                         (2, lo[2]+z*dz, 1), (2, lo[2]+(z+1)*dz, -1)):
                    poly = _clip(poly, axis, edge, sign)
                    if not poly:
                        break
                if not poly:
                    continue
                a, b = jet_models.bbox(poly)
                if (x, z) in cells:
                    olda, oldb = cells[x, z]
                    a, b = [min(a[k], olda[k]) for k in range(3)], [max(b[k], oldb[k]) for k in range(3)]
                cells[x, z] = a, b
    # A wing may be a plane, so give it a small thickness on both sides. Keep the
    # wheel contact (minimum Y) exactly on the model's existing grounded origin.
    return [(tuple(a[k] if b[k]-a[k] >= 2*SKIN else max(lo[k], (a[k]+b[k])/2-SKIN) for k in range(3)),
             tuple(b[k] if b[k]-a[k] >= 2*SKIN else (a[k]+b[k])/2+SKIN for k in range(3)))
            for _, (a, b) in sorted(cells.items())]


def _compound(tf, boxes, magic=0):
    """A new compound of axis-aligned solid boxes (pylib/hkshape.py: each a convex hull with its own mass properties,
    the compound their combination, its aabb / radius / simd tree from them)."""
    children = []
    for lo, hi in boxes:
        corners = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        children.append(((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0), hkshape.write_convex(tf, hkshape.convex_data(corners))))
    return hkshape.write_compound(tf, children, user_data=magic)


# 'cells' (the default): the 24 x 32 height field of solid boxes above. 'pieces': the model's own connected pieces,
# each cut until nearly convex and wrapped in a hull (pylib/model_pieces.py): tighter round the wings' sweep and the
# fuselage's curves, but untested in the game (docs/model-pipeline-2026-10-20.md has the offline comparison).
MODE = 'cells'
PIECE_TOLERANCE = 0.05     # m: how deep a model vertex may stand inside its piece's hull
PIECE_LIMIT = 512          # compound children at most (the cells reach 466)


def _pieces(tf, tris, magic=0):
    """A compound of the model's convex pieces (MODE 'pieces')."""
    sets = model_pieces.fit(tris, tolerance=PIECE_TOLERANCE, max_vertices=32, max_pieces=PIECE_LIMIT,
                            min_size=0.05, thickness=2*SKIN)
    children = [((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0), hkshape.write_convex(tf, hkshape.convex_data(s)))
                for s in sets]
    return hkshape.write_compound(tf, children, user_data=magic)


@lru_cache(maxsize=32)
def build(game, key, mode=None):
    """Return one complete .SHKT; all points relative to the unchanged full-model centre."""
    md = jet_models._model_of(game, key)
    centre = jet_models.model_box(game, key)[0]
    model_tris = list(triangles(md))
    boxes = [(tuple(lo[k]-centre[k] for k in range(3)), tuple(hi[k]-centre[k] for k in range(3)))
             for lo, hi in mesh_cells(model_tris)]
    tris = [tuple(tuple(p[k]-centre[k] for k in range(3)) for p in tri) for tri in model_tris]
    physics = hkfile.Physics.read(game.read('OBJECT', STOCK))
    body = _pieces(physics.tf, tris, MAGIC) if (mode or MODE) == 'pieces' else _compound(physics.tf, boxes, MAGIC)
    # Retain all stock proxy names and constraints for the 506's animation/death
    # code, but its helicopter blades must no longer collide outside the aircraft.
    lo, hi = max(boxes, key=lambda b: math.prod(b[1][k]-b[0][k] for k in range(3)))
    point = [(lo[k]+hi[k])/2 for k in range(3)]
    tiny = _compound(physics.tf, [(tuple(p-0.005 for p in point), tuple(p+0.005 for p in point))])
    for b in physics.bodies():
        if b.name == 'RagDollProxys.body':
            b.set_shape(body)
            # The helicopter's centre of mass and inertia do not belong to the airframe: without a massDistribution
            # the game derives them from the new shape's (SHKT Editor manual 2.7, confirmed in game there).
            b.value['massDistribution'].set_target(None)
        else:
            # The other proxies stand in for the 506's parts (its death animation and joints name them): their
            # massDistribution stays the 506 part's, the tiny box only keeps them out of every collision.
            b.set_shape(tiny)
    out = physics.write()
    import shkt_check
    kept = {p.where for p in shkt_check.stale_mass_distributions(out, game.read('OBJECT', STOCK))}
    want = {f'body {b.name!r}' for b in physics.bodies() if b.name != 'RagDollProxys.body'}
    if kept != want:
        raise ValueError(f'airframe mass distributions: kept {sorted(kept)}, expected the stand-ins {sorted(want)}')
    return out


def assets(game):
    return {name: build(game, key) for key, name in FILES.items()}


# The carrier's nacelles (src/nacelle_reach.h, jet_flight.cpp Thrusters): the collision is the grounded bind pose, its
# bottom the model origin's plane, but the plugin tilts the four nacelles about their X (HingePose: local Y' = co Y + si Z,
# Z' = -si Y + co Z) from 0 to -NACELLE_BACK; the front pair then reaches 5.2 m under that plane. Their lowest model y
# every NACELLE_STEP degrees, so the plugin keeps them over the ground under it.
NACELLE_MODEL = 'EDF6VC_CARRIER.MRAB'
NACELLE_STEP, NACELLE_BACK = 5, 110
NACELLES = {'front': 'boosterF_l', 'back': 'boosterB_l'}


def nacelle_lowest(game, key=NACELLE_MODEL):
    """{'front'|'back': [lowest model y at tilt 0, -5, ... -NACELLE_BACK degrees]} of the nacelle's skinned vertices."""
    md = jet_models._model_of(game, key)
    world = bind_world(md)
    out = {}
    for side, bone in NACELLES.items():
        i = next(k for k, b in enumerate(md.bones) if md.name_of(b.name) == bone)
        piv = world[i][12:15]
        rows = [world[i][4*r:4*r+3] for r in range(3)]
        pts = jet_models.bind_positions(md, {i})
        local = [[sum((p[c]-piv[c])*rows[r][c] for c in range(3)) for r in range(3)] for p in pts]
        lows = []
        for deg in range(0, -NACELLE_BACK-1, -NACELLE_STEP):
            a = math.radians(deg)
            co, si = math.cos(a), math.sin(a)
            y = [co*rows[1][k]+si*rows[2][k] for k in range(3)], [-si*rows[1][k]+co*rows[2][k] for k in range(3)]
            lows.append(min(piv[1]+d[0]*rows[0][1]+d[1]*y[0][1]+d[2]*y[1][1] for d in local))
        out[side] = lows
    return out
