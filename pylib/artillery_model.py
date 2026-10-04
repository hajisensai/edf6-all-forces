"""EDF6VC_ARTILLERY.MRAB, built from the player's own Root.cpk (pure Python: pylib + graft_pure, no numpy / PIL).

    build(game) -> bytes     the finished archive (game: rootcpk.Game, read only)
    check(arc)  -> None      raises ArtilleryCheckError on any self-check failure

The model: the KG6 Kepler (OBJECT/V603_FLAK.MRAB, v603_flak.mdb, kept as the model file name) with its whole turret
removed (the cannon_main subtree's geometry: turret, both AA guns, both radars) and in its place
  - the E551's turret housing (OBJECT/V505_TANK.MRAB: its cannon_main geometry without the gun and the antennas),
    skinned to the Kepler's cannon_main, centred on the footprint of the Kepler's own turret;
  - the Armed Barga's two back cannons (OBJECT/V605_BARGA_CANNON.MRAB: the cannon2_l / cannon2_r subtrees) turned from
    their stowed pose (muzzles +Y) to point forward (+Z), x GUN_SCALE, side by side (x = +-GUN_X) resting on the
    housing roof (GUN_CLEARANCE above the highest housing vertex under their footprint), muzzles MUZZLE_OUT ahead of
    the housing's nose. Each gun's body (cannon2 + cannonSlideB) is skinned to cannon_l / cannon_r (elevation), its
    barrel (cannonSlideF + cannon3) to cannon_slide_l / cannon_slide_r (recoil).

Kept for the stock Vehicle603_Flak class / V603_FLAK.SGO: every bone (names, parents, order, kinds, links), the hull and
track geometry, every stock material / texture. Changed binds (local + inverse bind consistently, stock rotations):
cannon_l / cannon_r at the breech end of each gun, on the gun body's axis (z its rearmost vertex: no gun vertex behind
the pivot, so elevating 0..90 deg never takes a vertex below the gun's 0 deg underside), held in a cradle,
cannon_slide_l / _r on the barrel axis at the Barga's own slide station (its cannonSlideF pivot), 0.3 m behind the
muzzle.
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import replace

import graft_pure as g
from graft_pure import Vec3
from mdb import Mdb, Mesh, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import vertex_table

HOST_ARC, HOST_MDB = 'V603_FLAK.MRAB', 'v603_flak.mdb'
TURRET_ARC, TURRET_MDB = 'V505_TANK.MRAB', 'v505_tank.mdb'
GUN_ARC, GUN_MDB = 'V605_BARGA_CANNON.MRAB', 'v605_barga_cannon.mdb'
OUT_ARC = 'EDF6VC_ARTILLERY.MRAB'

TURRET_SCALE = 1.0          # the E551 housing at its own size (M2); the Kepler's hull is 0.94x the E551's
GUN_SCALE = 0.25            # the Barga cannons (M2)
GUN_X = 0.68                # each gun's x centre, right gun mirrored (room for the cradle cheeks between the guns)
GUN_CLEARANCE = 0.03        # m between a gun's lowest vertex and the highest housing vertex under its footprint
MUZZLE_OUT = 1.6            # m from the housing's nose (max z) to the muzzles (cannon3 front end) at 0 deg
CRADLE_GAP = 0.01           # m between the cradle base's top and its gun's underside (never entered, 0..90 deg)
CRADLE_WIDTH = 0.85         # cradle base width / gun body width
CHEEK_T, CHEEK_GAP = 0.1, 0.02     # m: cradle cheek thickness, and its gap to the gun body's side
CHEEK_BACK, CHEEK_FWD, CHEEK_UP = 0.3, 0.5, 0.2   # m: cheek extent behind / ahead of / above the pivot
CRADLE_FRONT = 0.8          # m the cradle reaches past the housing nose, under the gun body
HULL_GAP = 0.02             # m the cradle's underside keeps above the hull's highest point (it turns over the hull)
ELEVATIONS = (0.0, 20.0, 40.0, 60.0)    # deg, checked: no gun vertex below the roof under it / the hull top

HOST_TURRET_ROOT = 'cannon_main'
GUN_BONES = ['cannon_l', 'cannon_r', 'cannon_slide_l', 'cannon_slide_r']     # binds moved
BOUNDS_BONES = ['cannon_main'] + GUN_BONES + ['polymesh']                     # bounds recomputed
TURRET_DROP = ['cannon', 'antena0_l', 'antena0_r']                            # V505 subtrees left out of the housing
# donor gun bone -> host bone, per side
GUN_MAP = {s: {f'cannon2_{s}': f'cannon_{s}', f'cannonSlideB_{s}': f'cannon_{s}',
               f'cannonSlideF_{s}': f'cannon_slide_{s}', f'cannon3_{s}': f'cannon_slide_{s}'} for s in 'lr'}
# stowed (+Y muzzle) -> forward (+Z muzzle): mock.py's pitch of +90 deg about x, row vector p' = p * R
GUN_ROT = ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0))
IDENT = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
DIRECTIONS = ('normal', 'binormal', 'tangent')


class ArtilleryCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise ArtilleryCheckError(msg)


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


# ------------------------------------------------------------------------------------------ geometry helpers

Rot = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]
Affine = tuple[Rot, float | Vec3, Vec3]  # p' = (p * R) * s + t  (row vector; s uniform or per axis, positive)


def scales(s: float | Vec3) -> Vec3:
    """A scale as per-axis factors (applied after the rotation)."""
    return (s, s, s) if isinstance(s, (int, float)) else s  # type: ignore[return-value]


def apply(a: Affine, p: Vec3) -> Vec3:
    r, s, t = a
    k = scales(s)
    return tuple(k[c] * (p[0] * r[0][c] + p[1] * r[1][c] + p[2] * r[2][c]) + t[c] for c in range(3))  # type: ignore[return-value]


def direction(a: Affine, v: tuple[float, ...], normal: bool) -> tuple[float, ...]:
    """A direction through `a`'s rotation and scale (a normal by the inverse scale), renormalised; any further
    components (w: handedness sign, unchanged by positive scales) kept."""
    k = scales(a[1])
    d = rotate(a[0], v)
    d3 = [d[c] / k[c] if normal else d[c] * k[c] for c in range(3)]
    n = (d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]) ** 0.5 or 1.0
    return tuple(x / n for x in d3) + tuple(v[3:])


def rotate(r: Rot, v: tuple[float, ...]) -> tuple[float, ...]:
    """xyz of `v` times R, any further components (w: handedness sign) kept."""
    return tuple(v[0] * r[0][c] + v[1] * r[1][c] + v[2] * r[2][c] for c in range(3)) + tuple(v[3:])


def placed(r: Rot, s: float, base: Vec3, at: Vec3) -> Affine:
    """The affine that scales by `s` about `base`, rotates by R, and puts `base` at `at` (mock.py part())."""
    rb = apply((r, s, (0.0, 0.0, 0.0)), base)
    return r, s, (at[0] - rb[0], at[1] - rb[1], at[2] - rb[2])


def bone_points(md: Mdb, bones: set[int]) -> list[Vec3]:
    """Model-space bind positions of the skinned vertices whose influences all lie in `bones`, over the triangles
    whose 3 vertices do (what a subtree's geometry is)."""
    out: list[Vec3] = []
    for o in md.objects:
        for me in o.meshes:
            if not me.flags[1]:
                continue
            P = g.mesh_positions(me)
            bi, bw = g.skin_columns(me)
            ok = [g.influences(i, w) <= bones for i, w in zip(bi, bw)]
            used = sorted({v for t in g.triangles(me) if all(ok[x] for x in t) for v in t})
            out += [P[v] for v in used]
    return out


def bottom_centre(P: list[Vec3]) -> Vec3:
    return ((min(p[0] for p in P) + max(p[0] for p in P)) / 2, min(p[1] for p in P),
            (min(p[2] for p in P) + max(p[2] for p in P)) / 2)


def box(P: list[Vec3]) -> tuple[Vec3, Vec3]:
    return (tuple(min(p[c] for p in P) for c in range(3)),  # type: ignore[return-value]
            tuple(max(p[c] for p in P) for c in range(3)))


CELL = 0.1      # m: roof grid cell; a vertex's roof is the highest housing vertex in its cell and the 8 around it


def roof_grid(housing: list[Vec3]) -> dict[tuple[int, int], float]:
    grid: dict[tuple[int, int], float] = {}
    for p in housing:
        k = (int(p[0] // CELL), int(p[2] // CELL))
        grid[k] = max(grid.get(k, -1e9), p[1])
    return grid


def roof_at(grid: dict[tuple[int, int], float], x: float, z: float) -> float | None:
    i, k = int(x // CELL), int(z // CELL)
    hs = [grid[(i + a, k + b)] for a in (-1, 0, 1) for b in (-1, 0, 1) if (i + a, k + b) in grid]
    return max(hs) if hs else None


def pitched(P: list[Vec3], o: Vec3, deg: float) -> list[Vec3]:
    """`P` pitched muzzle-up (+z towards +y) by `deg` about the x axis through `o` (a gun bone, identity rotation)."""
    import math
    c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(p[0], o[1] + (p[1] - o[1]) * c + (p[2] - o[2]) * sn, o[2] - (p[1] - o[1]) * sn + (p[2] - o[2]) * c)
            for p in P]


def sweep(P: list[Vec3], o: Vec3, grid: dict[tuple[int, int], float], deg: float) -> tuple[float, float, float]:
    """(lowest y, min over vertices above the housing of y - roof there, the roof under the tightest vertex) of `P`
    pitched by `deg` about `o`."""
    Q = pitched(P, o, deg)
    margin, under = 1e9, 0.0
    for q in Q:
        r = roof_at(grid, q[0], q[2])
        if r is not None and q[1] - r < margin:
            margin, under = q[1] - r, r
    return min(q[1] for q in Q), margin, under


def extract(donor: Mdb, bone_map: dict[int, int], a: Affine) -> list[tuple[int, Mesh]]:
    """graft_pure.extract_meshes with a rotation: every donor skinned mesh's triangles whose vertices are all
    influenced only by `bone_map` bones, blend indices rewritten through it, positions through `a`, normal / binormal /
    tangent directions through its rotation and scale (proper rotation, positive scales: handedness unchanged)."""
    out: list[tuple[int, Mesh]] = []
    for o in donor.objects:
        for me in o.meshes:
            if not me.flags[1]:
                continue
            keys, rows = vertex_table(me)
            bi, bw = g.skin_columns(me)
            pk, bk = g._pos_key(keys), g._bi_key(keys)
            dk = [(k, s.split(':')[0].lower() == 'normal') for k, s in enumerate(keys)
                  if s.split(':')[0].lower() in DIRECTIONS]
            ok = [g.influences(i, w) <= bone_map.keys() for i, w in zip(bi, bw)]
            tris = [t for t in g.triangles(me) if all(ok[i] for i in t)]
            if not tris:
                continue
            for v, row in enumerate(rows):
                if not ok[v]:
                    continue
                p = row[pk]
                row[pk] = apply(a, (p[0], p[1], p[2])) + tuple(p[3:])
                for k, nrm in dk:
                    row[k] = direction(a, row[k], nrm)
                row[bk] = tuple(bone_map[int(x)] if wt > 0 or n == 0 else 0 for n, (x, wt) in enumerate(zip(row[bk], bw[v])))
            new = g.rebuild_mesh(me, rows, tris)
            if new is not None:
                out.append((me.material, new))
    return out


def renumber(md: Mdb, obj: int, first: int) -> Mdb:
    """Meshes `first`.. of object `obj` get mesh_index first, first + 1, ... (stock objects number their meshes as a
    permutation of 0..n-1; appended donor meshes would carry their donor numbers)."""
    o = md.objects[obj]
    meshes = o.meshes[:first] + [replace(me, mesh_index=first + k) for k, me in enumerate(o.meshes[first:])]
    objects = list(md.objects)
    objects[obj] = replace(o, meshes=meshes)
    return replace(md, objects=objects)


# ------------------------------------------------------------------------------------------ build

def build_model(game) -> tuple[Mdb, Mdb, object, list[tuple[object, set[int]]], dict]:  # noqa: ANN001 - rootcpk.Game
    """(new model, stock host model, host Rab, [(donor Rab, donor materials used)], info)."""
    host_rab = rab_read(game.read('OBJECT', HOST_ARC))
    tur_rab = rab_read(game.read('OBJECT', TURRET_ARC))
    gun_rab = rab_read(game.read('OBJECT', GUN_ARC))
    host0 = mdb_read(member(host_rab, HOST_MDB).data)
    tur = mdb_read(member(tur_rab, TURRET_MDB).data)
    gun = mdb_read(member(gun_rab, GUN_MDB).data)
    info: dict = {}

    hb = {host0.name_of(b.name): b.index for b in host0.bones}
    tb = {tur.name_of(b.name): b.index for b in tur.bones}
    gb = {gun.name_of(b.name): b.index for b in gun.bones}
    for n in [HOST_TURRET_ROOT] + BOUNDS_BONES + ['body']:
        _req(n in hb, f'host bone {n} missing')
    for n in ['cannon_main'] + TURRET_DROP:
        _req(n in tb, f'turret donor bone {n} missing')
    for m in GUN_MAP.values():
        for n in list(m) + ['cannon1_l', 'cannon1_r']:
            _req(n in gb, f'gun donor bone {n} missing')
    turret_sub = g.subtree(host0, hb[HOST_TURRET_ROOT])

    # 1. strip the Kepler's turret (everything skinned in the cannon_main subtree); the hull stays as it is
    md = g.strip_geometry(host0, set(range(len(host0.bones))) - turret_sub, drop_empty_objects=False)
    _req(len(md.objects) == 1 and len(md.objects[0].meshes) == 4, 'unexpected stock object / mesh layout')
    _req(not bone_points(md, turret_sub), 'turret geometry left after the strip')
    kepler_housing = bone_points(host0, {hb[HOST_TURRET_ROOT]})     # the Kepler's turret body (no guns / radars)

    # 2. the housing: V505 cannon_main geometry without the gun / antennas, its bottom-centre on the Kepler turret
    #    body's (x / z mid, y min of the cannon_main-skinned vertices: the footprint on the hull's turret ring)
    tur_main = g.subtree(tur, tb['cannon_main'])
    housing = tur_main.copy()
    for n in TURRET_DROP:
        housing -= g.subtree(tur, tb[n])
    _req(housing == {tb['cannon_main']}, f'housing bones {sorted(housing)}: expected cannon_main alone')
    hp = bone_points(tur, housing)
    kb = bottom_centre(kepler_housing)
    a_tur = placed(IDENT, TURRET_SCALE, bottom_centre(hp), kb)
    housing_meshes = extract(tur, {tb['cannon_main']: hb[HOST_TURRET_ROOT]}, a_tur)
    info['housing_affine'] = a_tur

    # 3. the guns: turned forward, x at +-GUN_X, muzzles MUZZLE_OUT past the housing nose, resting on the roof
    housing_host = [apply(a_tur, p) for p in hp]
    nose_z = max(p[2] for p in housing_host)
    gw = bind_world(gun)
    sides: dict[str, dict] = {}
    for s, sign in (('l', 1.0), ('r', -1.0)):
        sub = g.subtree(gun, gb[f'cannon2_{s}'])
        _req(sub == {gb[n] for n in GUN_MAP[s]}, f'cannon2_{s} subtree is not {sorted(GUN_MAP[s])}')
        gp = bone_points(gun, sub)
        blo, bhi = box(bone_points(gun, {gb[f'cannon3_{s}']}))
        axis_x, axis_z = (blo[0] + bhi[0]) / 2, (blo[2] + bhi[2]) / 2      # the barrel tube's axis (stowed: along y)
        a0 = placed(GUN_ROT, GUN_SCALE, bottom_centre(gp), (0.0, 0.0, 0.0))
        q = [apply(a0, p) for p in gp]
        lo, hi = box(q)
        muz = apply(a0, (axis_x, bhi[1], axis_z))
        dx, dz = sign * GUN_X - (lo[0] + hi[0]) / 2, nose_z + MUZZLE_OUT - muz[2]
        under = [p[1] for p in housing_host if lo[0] + dx <= p[0] <= hi[0] + dx and p[2] >= lo[2] + dz]
        sides[s] = {'gp': gp, 'a0': a0, 'dx': dx, 'dz': dz, 'roof': max(under) if under else 0.0, 'lo_y': lo[1],
                    'axis': (axis_x, axis_z), 'bhi_y': bhi[1], 'body_lo_z': lo[2] + dz}
    roof = max(v['roof'] for v in sides.values())      # one height for both guns: they stay mirrored
    # resting on the roof under their 0 deg footprint, then raised by whatever the 0..60 deg sweep still lacks (the top
    # of a gun swings back over the housing behind the pivot as it elevates)
    grid = roof_grid(housing_host)
    lift = roof + GUN_CLEARANCE
    lack = 0.0
    for s in 'lr':
        v = sides[s]
        r0, s0, t0 = v['a0']
        q = [apply((r0, s0, (t0[0] + v['dx'], t0[1] + lift - v['lo_y'], t0[2] + v['dz'])), p) for p in v['gp']]
        c2 = apply((r0, s0, (t0[0] + v['dx'], t0[1] + lift - v['lo_y'], t0[2] + v['dz'])),
                   tuple(gw[gb[f'cannon2_{s}']][12:15]))  # type: ignore[arg-type]
        o = (0.0, c2[1], min(p[2] for p in q))
        lack = max([lack] + [GUN_CLEARANCE - sweep(q, o, grid, e)[1] for e in range(0, 61, 5)])
    lift += lack
    info['sweep_lift'] = lack
    gun_meshes: list[tuple[int, Mesh]] = []
    gun_info: dict[str, dict[str, Vec3]] = {}
    for s in 'lr':
        v = sides[s]
        r0, s0, t0 = v['a0']
        a_gun: Affine = (r0, s0, (t0[0] + v['dx'], t0[1] + lift - v['lo_y'], t0[2] + v['dz']))
        bone_map = {gb[d]: hb[h] for d, h in GUN_MAP[s].items()}
        gun_meshes += extract(gun, bone_map, a_gun)
        axis_x, axis_z = v['axis']
        body_axis = apply(a_gun, tuple(gw[gb[f'cannon2_{s}']][12:15]))  # type: ignore[arg-type]  # on the body tube
        q = [apply(a_gun, p) for p in v['gp']]
        pivot = (body_axis[0], body_axis[1], min(p[2] for p in q))    # the breech end, on the gun body's axis
        slide = apply(a_gun, (axis_x, gw[gb[f'cannonSlideF_{s}']][13], axis_z))
        muzzle = apply(a_gun, (axis_x, v['bhi_y'], axis_z))
        gun_info[s] = {'pivot': pivot, 'slide': slide, 'muzzle': muzzle, 'affine_t': a_gun[2], 'box': box(q)}
    info['guns'] = gun_info
    # 3b. a cradle under each gun, three pieces of the Barga's own gun yoke (cannon1_<s>), turned like the gun and
    #     scaled per axis into boxes: a base under the gun (CRADLE_WIDTH of its width, from the breech end to
    #     CRADLE_FRONT past the housing nose, up to CRADLE_GAP under the gun's underside) and two cheeks flanking the
    #     gun body at the pivot (CHEEK_GAP off its sides, CHEEK_BACK / CHEEK_FWD around the pivot, up to CHEEK_UP above
    #     it). Every piece starts at the lowest roof point under it, but HULL_GAP above the hull's highest point (they
    #     turn over the hull). Skinned to cannon_main: they turn with the turret, do not elevate. No piece meets the
    #     gun at 0..90 deg: the gun has no vertex behind the pivot, so pitching it never takes a vertex below its 0 deg
    #     underside (the base), and pitching about x keeps every vertex's x (the cheeks lie beside the gun body).
    cradle_info: dict[str, list[tuple[Vec3, Vec3]]] = {}
    hull_top = max(p[1] for p in bone_points(md, set(range(len(md.bones))) - turret_sub))

    def floor_under(x0: float, x1: float, z0: float, z1: float) -> float:
        foot = [h for k, h in grid.items() if x0 <= (k[0] + 0.5) * CELL <= x1 and z0 <= (k[1] + 0.5) * CELL <= z1]
        return max(min(foot) if foot else hull_top, hull_top + HULL_GAP)

    for s in 'lr':
        pv, (glo, ghi) = gun_info[s]['pivot'], gun_info[s]['box']
        cp = bone_points(gun, {gb[f'cannon1_{s}']})
        r0 = [apply((GUN_ROT, 1.0, (0.0, 0.0, 0.0)), p) for p in cp]
        lo, hi = box(r0)
        half_w = (ghi[0] - glo[0]) / 2 * CRADLE_WIDTH
        zb = (pv[2], nose_z + CRADLE_FRONT)
        zc = (pv[2] - CHEEK_BACK, pv[2] + CHEEK_FWD)
        pieces = [((pv[0] - half_w, pv[0] + half_w), zb, glo[1] - CRADLE_GAP),
                  ((ghi[0] + CHEEK_GAP, ghi[0] + CHEEK_GAP + CHEEK_T), zc, pv[1] + CHEEK_UP),
                  ((glo[0] - CHEEK_GAP - CHEEK_T, glo[0] - CHEEK_GAP), zc, pv[1] + CHEEK_UP)]
        cradle_info[s] = []
        for (x0, x1), (z0, z1), y1 in pieces:
            y0 = floor_under(x0, x1, z0, z1)
            _req(y1 - y0 > 0.05, f'cradle piece {x0:.2f}..{x1:.2f}: no room ({y0:.3f}..{y1:.3f})')
            k = ((x1 - x0) / (hi[0] - lo[0]), (y1 - y0) / (hi[1] - lo[1]), (z1 - z0) / (hi[2] - lo[2]))
            a_cr: Affine = (GUN_ROT, k, (x0 - k[0] * lo[0], y0 - k[1] * lo[1], z0 - k[2] * lo[2]))
            gun_meshes += extract(gun, {gb[f'cannon1_{s}']: hb[HOST_TURRET_ROOT]}, a_cr)
            cradle_info[s].append(box([apply(a_cr, p) for p in cp]))
    info['cradles'] = cradle_info
    info['roof_under_guns'] = roof
    info['housing_nose_z'] = nose_z

    # 4. gun bones onto the guns: elevation pivot at the breech end, slide near the muzzle (stock rotations kept)
    for s in 'lr':
        md = g.set_bone_origin(md, hb[f'cannon_{s}'], gun_info[s]['pivot'])
        md = g.set_bone_origin(md, hb[f'cannon_slide_{s}'], gun_info[s]['slide'])

    # 5. donor materials + meshes into object 0 (polymesh, the kind-2 bone)
    n0 = len(md.objects[0].meshes)
    md, tur_map = g.merge_materials(md, tur, {m for m, _me in housing_meshes})
    md = g.append_meshes(md, housing_meshes, tur_map, obj=0)
    md, gun_map = g.merge_materials(md, gun, {m for m, _me in gun_meshes})
    md = g.append_meshes(md, gun_meshes, gun_map, obj=0)
    md = renumber(md, 0, n0)

    # 6. bounds of what carries new geometry (+ the object bone); relink (hierarchy unchanged)
    md = g.recompute_bounds(md, {hb[n] for n in BOUNDS_BONES})
    md = g.relink(md)
    info['tur_map'], info['gun_map'] = tur_map, gun_map
    return md, host0, host_rab, [(tur_rab, set(tur_map.values())), (gun_rab, set(gun_map.values()))], info


def build_with_info(game) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    """(archive bytes, model, info): build() plus what the report prints."""
    md, _host0, host_rab, donors, info = build_model(game)
    data = mdb_write(md)
    for rab, mats in donors:
        tex = sorted({md.textures[x.texture].filename for m in mats for x in md.materials[m].textures})
        g.copy_texture_members(host_rab, rab, tex)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(host_rab, HOST_MDB).stored = stored
    return rab_write(host_rab), md, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_ARTILLERY.MRAB (the stock Kepler archive, its model replaced, the donor textures added)."""
    return build_with_info(game)[0]


# ------------------------------------------------------------------------------------------ check

# Fingerprint of the stock v603_flak.mdb skeleton (Root.cpk OBJECT/V603_FLAK.MRAB): per bone (name, parent, sibling,
# child, child_count, kind, depth_delta, bounded, sha256[:16] of the float32 local + inv_bind (+ half + centre for
# bones whose bounds are kept)). Moved bones carry their stock model-space bind instead (5 decimals). STOCK_HULL: the
# hull / track geometry kept (triangle count, vertex box). Generated by make_fingerprint().
STOCK_BONES: list[tuple] = []          # filled below
STOCK_MOVED: dict[str, list[float]] = {}
STOCK_HULL: tuple = ()


def _bone_hash(b, with_bounds: bool) -> str:  # noqa: ANN001 - mdb.Bone
    raw = struct.pack('<32f', *b.local, *b.inv_bind)
    if with_bounds:
        raw += struct.pack('<8f', *b.half, *b.centre)
    return hashlib.sha256(raw).hexdigest()[:16]


def _hull(md: Mdb, turret_root: int) -> tuple:
    """(triangles, vertex box rounded to 4 decimals) of the geometry outside the turret subtree."""
    out = set(range(len(md.bones))) - g.subtree(md, turret_root)
    ntri = 0
    for o in md.objects:
        for me in o.meshes:
            bi, bw = g.skin_columns(me)
            ok = [g.influences(i, w) <= out for i, w in zip(bi, bw)]
            ntri += sum(all(ok[x] for x in t) for t in g.triangles(me))
    lo, hi = box(bone_points(md, out))
    return ntri, [round(x, 4) for x in lo + hi]


def make_fingerprint(host0: Mdb) -> tuple[list[tuple], dict[str, list[float]], tuple]:
    """(STOCK_BONES, STOCK_MOVED, STOCK_HULL) of the stock host model (used once to generate the constants below)."""
    w = bind_world(host0)
    bones, moved = [], {}
    for b in host0.bones:
        n = host0.name_of(b.name)
        mv = n in GUN_BONES
        bones.append((n, b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded,
                      None if mv else _bone_hash(b, n not in BOUNDS_BONES)))
        if mv:
            moved[n] = [round(x, 5) for x in w[b.index]]
    return bones, moved, _hull(host0, host0.bone_index(HOST_TURRET_ROOT))


def _mesh_ok(md: Mdb, o, files: set[str]) -> None:  # noqa: ANN001 - mdb.Object
    nb = len(md.bones)
    _req(0 <= o.bone < nb and md.bones[o.bone].kind == 2, f'object {md.name_of(o.name)} not on a kind-2 bone')
    _req(len(o.meshes) > 0, f'object {md.name_of(o.name)} has no mesh')
    _req(sorted(me.mesh_index for me in o.meshes) == list(range(len(o.meshes))), 'mesh_index not a permutation')
    for j, me in enumerate(o.meshes):
        nv = me.nverts
        tag = f'{md.name_of(o.name)} mesh {j}'
        _req(me.vsize > 0 and len(me.vdata) == nv * me.vsize, f'{tag}: vertex buffer size')
        _req(0 < nv < 0x10000, f'{tag}: {nv} vertices')
        _req(len(me.indices) % 6 == 0 and me.indices, f'{tag}: index count')
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(max(idx) < nv, f'{tag}: index {max(idx)} >= {nv} vertices')
        _req(0 <= me.material < len(md.materials), f'{tag}: material {me.material}')
        bi, bw = g.skin_columns(me)
        _req(len(bi) == nv and len(bw) == nv, f'{tag}: skin columns')
        for r, wt in zip(bi, bw):
            _req(all(int(i) < nb for i in r), f'{tag}: blend index >= {nb}')
            _req(md.bones[int(r[0])].kind == 3, f'{tag}: vertex skinned to non-skin bone {int(r[0])}')
            _req(abs(sum(wt) - 1.0) < 1e-3, f'{tag}: weights sum {sum(wt)}')
        for p in g.mesh_positions(me):
            _req(all(abs(x) < 50.0 for x in p), f'{tag}: vertex {p} out of range')
    for m in md.materials:
        for x in m.textures:
            _req(0 <= x.texture < len(md.textures), f'material {md.name_of(m.name)}: texture {x.texture}')
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'material {md.name_of(m.name)}: texture member {want} missing')


def check(arc: bytes) -> None:
    """Re-read `arc` and raise ArtilleryCheckError unless: the archive and model round-trip; < 256 bones; every mesh's
    vertex buffer, index count / range, blend indices (< bone count, skin bones only), weights, material index and
    mesh numbering are valid; every material texture (HD and .lod) is an archive member; every bone's bind x inverse
    bind is the identity; the bone list (names, parents, links, kinds) is the stock one, every bone but the 4 gun bones
    bit-identical to stock (bounds aside where recomputed), the gun bones at their stock rotations, mirrored left /
    right, each cannon bone inside its gun body's vertex box, each slide bone inside its barrel's cross-section with
    the muzzle ahead of it; the hull geometry is the stock one; the radars carry no geometry; the turret sits on the
    hull (no turret vertex below the Kepler's turret bottom); each cannon bone at its gun's breech end, each slide
    0.15..0.5 m behind its muzzle, the muzzles >= 1.5 m past the housing nose, and at every ELEVATIONS angle no gun
    vertex below the housing roof under it or the hull top, nor inside a cradle piece; each gun's cradle has a base
    under it and a cheek on each side at its pivot reaching above the pivot."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    nb = len(md.bones)
    _req(nb < 256, f'{nb} bones')
    files = {f.name.lower() for f in rab.files}
    for o in md.objects:
        _mesh_ok(md, o, files)
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    # skeleton vs stock
    _req(len(md.bones) == len(STOCK_BONES), f'{len(md.bones)} bones, stock {len(STOCK_BONES)}')
    for b, s in zip(md.bones, STOCK_BONES):
        n = md.name_of(b.name)
        _req((n, b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded) == tuple(s[:8]),
             f'bone {b.index} {n}: name / links / kind differ from stock {s[:8]}')
        if s[8] is not None:
            _req(_bone_hash(b, n not in BOUNDS_BONES) == s[8], f'bone {n}: matrices / bounds differ from stock')
        else:
            ref = STOCK_MOVED[n]
            _req(max(abs(w[b.index][k] - ref[k]) for k in range(12)) < 1e-4, f'bone {n}: rotation differs from stock')
    # geometry per bone
    bi_of = {n: md.bone_index(n) for n in [HOST_TURRET_ROOT, 'doppler_radar', 'tracking_radar'] + GUN_BONES}
    pts = g.skinned_points(md)
    for n in ('doppler_radar', 'tracking_radar'):
        _req(bi_of[n] not in pts, f'{n} carries geometry')
    _req(_hull(md, bi_of[HOST_TURRET_ROOT]) == (STOCK_HULL[0], list(STOCK_HULL[1])), 'hull geometry differs from stock')
    for n in [HOST_TURRET_ROOT] + GUN_BONES:
        _req(len(pts.get(bi_of[n], [])) > 100, f'{n}: no grafted geometry')
    turret_lo = min(p[1] for n in [HOST_TURRET_ROOT] + GUN_BONES for p in pts[bi_of[n]])
    _req(turret_lo >= STOCK_TURRET_BOTTOM - 0.01, f'turret geometry down to y {turret_lo:.3f}, into the hull')
    pos = {n: w[bi_of[n]][12:15] for n in GUN_BONES}
    for c, k in ((0, -1.0), (1, 1.0), (2, 1.0)):
        _req(abs(pos['cannon_l'][c] - k * pos['cannon_r'][c]) < 2e-3, 'cannon_l / _r not mirrored')
        _req(abs(pos['cannon_slide_l'][c] - k * pos['cannon_slide_r'][c]) < 2e-3, 'cannon_slide_l / _r not mirrored')
    housing, _cradle_pts = turret_parts(md)
    cradles = cradle_boxes(md)
    nose = max(p[2] for p in housing)
    for s in 'lr':
        blo, bhi = box(pts[bi_of[f'cannon_{s}']])
        t = pos[f'cannon_{s}']
        _req(all(blo[c] - 1e-3 <= t[c] <= bhi[c] + 1e-3 for c in range(3)), f'cannon_{s} outside its gun body')
        _req(t[2] - blo[2] < 0.05, f'cannon_{s}: pivot {t[2] - blo[2]:.3f} m ahead of the breech end')
        sl = pos[f'cannon_slide_{s}']
        slo, shi = box(pts[bi_of[f'cannon_slide_{s}']])
        _req(all(slo[c] <= sl[c] <= shi[c] for c in range(2)), f'cannon_slide_{s} outside its barrel section')
        _req(0.15 < shi[2] - sl[2] < 0.5 and sl[2] > t[2], f'cannon_slide_{s}: {shi[2] - sl[2]:.3f} m behind the muzzle')
        _req(shi[2] >= nose + 1.5, f'cannon_{s}: muzzle {shi[2]:.3f} not 1.5 m past the housing nose {nose:.3f}')
        # the cradle: a piece under the gun and one on each side of it at the pivot, reaching above the pivot
        near = [c for c in cradles if c[0][2] - 1e-3 <= t[2] <= c[1][2] + 1e-3]
        _req(any(c[1][0] < blo[0] and c[1][1] > t[1] for c in near), f'cannon_{s}: no inner cradle cheek at its pivot')
        _req(any(c[0][0] > bhi[0] and c[1][1] > t[1] for c in near), f'cannon_{s}: no outer cradle cheek at its pivot')
        _req(any(c[0][0] >= blo[0] and c[1][0] <= bhi[0] and c[1][1] < blo[1] for c in cradles),
             f'cannon_{s}: no cradle base under it')
        for e, low, margin, roof, hit in gun_clearance(md, s):
            _req(margin >= 0.0 and low > STOCK_HULL[1][4],
                 f'cannon_{s} at {e:g} deg: a vertex {margin:.3f} m from the roof ({roof:.3f}); lowest {low:.3f}')
            _req(hit == 0, f'cannon_{s} at {e:g} deg: {hit} gun vertices inside a cradle piece')


def turret_parts(md: Mdb) -> tuple[list[Vec3], list[Vec3]]:
    """(housing, cradles): the cannon_main-skinned vertices, split by material: those of meshes whose material is
    also a gun mesh's (the Barga's) are the cradles."""
    main, guns = md.bone_index(HOST_TURRET_ROOT), {md.bone_index('cannon_l'), md.bone_index('cannon_r')}
    gun_mats = {me.material for o in md.objects for me in o.meshes
                if any(int(i[0]) in guns for i in g.skin_columns(me)[0])}
    housing: list[Vec3] = []
    cradles: list[Vec3] = []
    for o in md.objects:
        for me in o.meshes:
            P, bi = g.mesh_positions(me), g.skin_columns(me)[0]
            (cradles if me.material in gun_mats else housing).extend(p for p, i in zip(P, bi) if int(i[0]) == main)
    return housing, cradles


def cradle_boxes(md: Mdb) -> list[tuple[Vec3, Vec3]]:
    """The vertex box of every cradle piece: each mesh skinned only to cannon_main in a gun (Barga) material."""
    main, guns = md.bone_index(HOST_TURRET_ROOT), {md.bone_index('cannon_l'), md.bone_index('cannon_r')}
    gun_mats = {me.material for o in md.objects for me in o.meshes
                if any(int(i[0]) in guns for i in g.skin_columns(me)[0])}
    return [box(g.mesh_positions(me)) for o in md.objects for me in o.meshes
            if me.material in gun_mats and all(int(i[0]) == main for i in g.skin_columns(me)[0])]


def gun_clearance(md: Mdb, side: str) -> list[tuple[float, float, float, float, int]]:
    """Per ELEVATIONS angle, cannon_<side>'s subtree pitched muzzle-up about the bone: (deg, the gun's lowest vertex y,
    min over its vertices of y - the housing roof under that vertex (CELL grid), that roof's y, how many of its
    vertices lie inside a cradle piece's box)."""
    pts = g.skinned_points(md)
    gi = md.bone_index(f'cannon_{side}')
    o = bind_world(md)[gi][12:15]
    P = [p for b in g.subtree(md, gi) for p in pts.get(b, [])]
    grid = roof_grid(turret_parts(md)[0])
    boxes = cradle_boxes(md)
    out = []
    for e in ELEVATIONS:
        Q = pitched(P, (o[0], o[1], o[2]), e)
        hit = sum(1 for q in Q for lo, hi in boxes if all(lo[c] - 1e-3 < q[c] < hi[c] + 1e-3 for c in range(3)))
        out.append((e,) + sweep(P, (o[0], o[1], o[2]), grid, e) + (hit,))
    return out


STOCK_BONES = [
    ('v603_flak', -1, -1, 1, 2, 0, 1, 0, '05a28f4f41951dab'),
    ('globalSRT', 0, 39, 2, 1, 0, 1, 0, '05a28f4f41951dab'),
    ('body', 1, -1, 3, 18, 3, 1, 1, 'a627191540591683'),
    ('cannon_main', 2, 10, 4, 4, 3, 1, 1, 'f2c985d4add89456'),
    ('cannon_l', 3, 6, 5, 1, 3, 1, 1, None),
    ('cannon_slide_l', 4, -1, -1, 0, 3, -1, 1, None),
    ('cannon_r', 3, 8, 7, 1, 3, 1, 1, None),
    ('cannon_slide_r', 6, -1, -1, 0, 3, -1, 1, None),
    ('doppler_radar', 3, 9, -1, 0, 3, 0, 1, '0c6095d9f55e527f'),
    ('tracking_radar', 3, -1, -1, 0, 3, -1, 1, '1cf2f4566695d02f'),
    ('catapi_body', 2, 23, 11, 12, 3, 1, 1, 'bd121f27f2589a63'),
    ('catapiB_l', 10, 12, -1, 0, 3, 0, 1, '089b77d7670c1f0b'),
    ('catapiB_r', 10, 13, -1, 0, 3, 0, 1, '0c5277fc2ee64e85'),
    ('catapiC_l', 10, 14, -1, 0, 3, 0, 1, 'e9b382ad2d2cacd5'),
    ('catapiC_r', 10, 15, -1, 0, 3, 0, 1, '6aae865628374085'),
    ('catapiD_l', 10, 16, -1, 0, 3, 0, 1, '1079dd19727b8a87'),
    ('catapiD_r', 10, 17, -1, 0, 3, 0, 1, 'eeb335e59fe8c0a0'),
    ('catapiE_l', 10, 18, -1, 0, 3, 0, 1, '56c77ea49d03fe8c'),
    ('catapiE_r', 10, 19, -1, 0, 3, 0, 1, '591e4eca21979858'),
    ('catapiF_l', 10, 20, -1, 0, 3, 0, 1, '80fea8f43545a782'),
    ('catapiF_r', 10, 21, -1, 0, 3, 0, 1, '96b8d3f57f30fb1b'),
    ('catapiG_l', 10, 22, -1, 0, 3, 0, 1, 'e3a9ad168ca269bf'),
    ('catapiG_r', 10, -1, -1, 0, 3, -1, 1, '28c00d6506cc0f46'),
    ('tire_moveA_l', 2, 24, -1, 0, 3, 0, 1, 'd71952629ba2b8bb'),
    ('tire_moveA_r', 2, 25, -1, 0, 3, 0, 1, '66d088090b8b7325'),
    ('tire_moveB_l', 2, 26, -1, 0, 3, 0, 1, 'a003acad23f5fa32'),
    ('tire_moveB_r', 2, 27, -1, 0, 3, 0, 1, '00feab91b0271111'),
    ('tire_moveC_l', 2, 28, -1, 0, 3, 0, 1, '0e9fb0b7917a46b4'),
    ('tire_moveC_r', 2, 29, -1, 0, 3, 0, 1, '09a866cbe5c1be0f'),
    ('tire_moveD_l', 2, 30, -1, 0, 3, 0, 1, 'c2b9c278d217bbd4'),
    ('tire_moveD_r', 2, 31, -1, 0, 3, 0, 1, '22a2cf64865b44bf'),
    ('tire_moveE_l', 2, 32, -1, 0, 3, 0, 1, 'b4131ba32e03bd45'),
    ('tire_moveE_r', 2, 33, -1, 0, 3, 0, 1, '4f547e8c657f5289'),
    ('tire_moveF_l', 2, 34, -1, 0, 3, 0, 1, '8209be8c87ef26ed'),
    ('tire_moveF_r', 2, 35, -1, 0, 3, 0, 1, 'b240e3bc85c1f8fe'),
    ('tire_moveG_l', 2, 36, -1, 0, 3, 0, 1, '987d51c04a2e57ed'),
    ('tire_moveG_r', 2, 37, -1, 0, 3, 0, 1, '570ad50bf08cb00c'),
    ('tire_moveH_l', 2, 38, -1, 0, 3, 0, 1, '7341850bd876bb13'),
    ('tire_moveH_r', 2, -1, -1, 0, 3, -2, 1, '9d384d719d6a8b40'),
    ('polymesh', 0, -1, -1, 0, 2, 1, 0, '1f39301f6bb92913'),
]
STOCK_MOVED = {
    'cannon_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.74721, 2.218, -0.43787, 1.0],
    'cannon_slide_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.943, 2.218, 1.00693, 1.0],
    'cannon_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.74721, 2.218, -0.43787, 1.0],
    'cannon_slide_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.943, 2.218, 1.00693, 1.0],
}
STOCK_HULL = (4712, [-1.542, 0.0221, -3.3242, 1.542, 1.6709, 3.0625])
STOCK_TURRET_BOTTOM = 1.4541   # lowest vertex of the stock turret (cannon_main subtree)
