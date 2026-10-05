"""KATYUSHA.MRAB, built from the player's own Root.cpk (pure Python: pylib + graft_pure, no numpy / PIL).

    build(game) -> bytes     the finished archive (game: rootcpk.Game, read only)
    check(arc)  -> None      raises KatyushaCheckError on any self-check failure

The model: the Naegling (OBJECT/VEHICLE402_ROCKET.MRAB, Vehicle402_Rocket.mdb) with its hull and track geometry
removed and the V607 robo-truck's truck (OBJECT/V607_ROBOTRUCK.MRAB, object 0 mesh 0: body + 6 tires; the robot
and its cradle on the bed dropped) grafted in; the rocket rack (Rocketcannon_base subtree) translated onto the bed.

Kept for the stock 402_Rocket class / VEHICLE402_ROCKET.SGO: every bone (names, parents, order, kinds, links), the
rack geometry and every stock material / texture. Changed binds (local + inverse bind consistently): the 3 rack
bones (translated together) and the 12 car wheel bones tire_moveA..F_l/r (front axle -> A, middle -> C, rear -> E,
each tire skinned to its bone at the tire centre; B / D / F put on A / C / E with no geometry). The truck body is
skinned to `body`, the Naegling's hull bone. The catapi (track) object is dropped.

The elevation ram (the Naegling's Rocketcannon_prop: a hydraulic cylinder on the turntable, its rod's eye in the
launcher's belly by the launcher's pivot) is made telescopic: the rod, its end collar and its eye go onto a bone of
their own, RAM_ROD (inserted after the prop under Rocketcannon_base, preorder kept, later bones and blend indices
renumbered), pivoting at the eye, and the rod is lengthened into the cylinder by the stroke + RAM_OVERLAP. The stock
class turns the whole prop by the launcher's elevation about the cylinder's pivot (0x5FDDA0: asin of the launcher's
forward on the turntable's up, plus its bind angle): the eye stays inside the launcher only up to ~52 deg
(ram_report), so past the Naegling's 50 deg the rod hung in the air. EDF6VehicleCrew (src/katyusha.cpp) aims both
bones at each other's pivot every frame instead (ram_pose: the cylinder from its pivot P towards the eye E, the rod
from E back along the same line), over the whole 0..PITCH_STOP_DEG.
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import replace

import graft_pure as g
from graft_pure import Vec3
from mdb import Bone, Mdb, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import link, vertex_table

HOST_ARC, HOST_MDB = 'VEHICLE402_ROCKET.MRAB', 'Vehicle402_Rocket.mdb'
DONOR_ARC, DONOR_MDB = 'V607_ROBOTRUCK.MRAB', 'v607_robotruck.mdb'

TRUCK_SCALE = 1.0               # the donor truck is 8.14 m long: already the ~8 m target
DONOR_TRUCK_MESHES = {(0, 0)}   # donor object 0 mesh 0 = the truck; meshes 1/4 = robot cradle, 2/3 = robot
CAB_CLEARANCE = 0.15            # m between the rack's front (0 elevation) and the cab / headboard
RAIL_CLEARANCE = 0.03           # m between the rack main box's underside and the bed side rails' top
# donor tire bone -> host wheel bone (A front .. F rear; _l = +x on both models)
WHEEL_MAP = {'tireF_l': 'tire_moveA_l', 'tireF_r': 'tire_moveA_r',
             'tireB0_l': 'tire_moveC_l', 'tireB0_r': 'tire_moveC_r',
             'tireB1_l': 'tire_moveE_l', 'tireB1_r': 'tire_moveE_r'}
ALL_WHEELS = [f'tire_move{a}_{s}' for a in 'ABCDEF' for s in 'lr']
RACK = ['Rocketcannon_base', 'Rocketcannon_main', 'Rocketcannon_prop']
# The launcher's elevation stop, degrees up (tools/make_katyusha.py writes it into the hinge limit; the ram is built for
# it: its stroke is the eye's travel from 0 up to here).
PITCH_STOP_DEG = 80.0
# The ram (module docstring): the rod's bone; the distance along the ram from the cylinder's pivot (m) past which a
# piece's centre puts it on the rod (the cylinder and its gland are centred within ~0.75 m, the rod's pieces from
# ~1.0 m); how much rod stays inside the cylinder at full stroke.
RAM_ROD = 'edf6vc_ram_rod'
RAM_SPLIT = 0.85
RAM_OVERLAP = 0.08
# The ram's pieces per side: the cylinder and its gland (the prop's), the rod, its end collar and its eye (the rod's).
RAM_PIECES, ROD_PIECES = 5, 3
OBJECT_BONES = ['Vehicle402_Rocket', 'Vehicle_Rocketcannon']   # their bounds are recomputed (object boxes)


class KatyushaCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O (assert would vanish)."""
    if not ok:
        raise KatyushaCheckError(msg)


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def object_positions(md: Mdb, obj: int) -> list[Vec3]:
    return [p for me in md.objects[obj].meshes for p in g.mesh_positions(me)]


# ------------------------------------------------------------------------------------------ measurements

def truck_geometry(md: Mdb, obj: int, body: int) -> dict[str, float]:
    """Bed / cab landmarks measured off the grafted truck (host space): the z where the cab / headboard starts
    (first point above 2.6 m), the bed floor height (median over 0.2 m slabs of the highest centre point), the
    side rail top, the truck's rear / front z."""
    Q: list[Vec3] = []
    for me in md.objects[obj].meshes:
        bi, _bw = g.skin_columns(me)
        Q += [p for p, i in zip(g.mesh_positions(me), bi) if int(i[0]) == body]
    cab_z = min(p[2] for p in Q if p[1] > 2.6)
    zmin = min(p[2] for p in Q)
    bed = [p for p in Q if p[2] < cab_z - 0.2 and p[2] > zmin + 0.3]
    centre = [p for p in bed if abs(p[0]) < 0.9]
    start, stop, step = min(p[2] for p in bed), cab_z - 0.2, 0.2
    n = -int(-((stop - start) / step) // 1)                # numpy.arange length: ceil((stop - start) / step)
    slabs = []
    for k in range(n):
        z = start + k * step
        ys = [p[1] for p in centre if z <= p[2] < z + 0.2]
        if ys:
            slabs.append(max(ys))
    slabs.sort()
    m = len(slabs)
    floor = slabs[m // 2] if m % 2 else (slabs[m // 2 - 1] + slabs[m // 2]) / 2
    rails = max(p[1] for p in bed if abs(p[0]) > 1.2)
    return {'cab_z': cab_z, 'bed_floor_y': floor, 'rail_top_y': rails, 'rear_z': zmin, 'front_z': max(p[2] for p in Q)}


# ------------------------------------------------------------------------------------------ the elevation ram

def _unit(v: Vec3) -> Vec3:
    n = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5
    _req(n > 1e-9, 'zero vector')
    return (v[0] / n, v[1] / n, v[2] / n)


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def launcher_point(p: Vec3, M: Vec3, theta: float) -> Vec3:
    """Point `p` of the launcher (bind, model space) with the launcher raised `theta` rad about its pivot `M` (nose up:
    its forward (0, 0, 1) turns to (0, sin, cos), as the stock hinge and src/katyusha.cpp LoftRows turn it)."""
    dy, dz = p[1] - M[1], p[2] - M[2]
    c, s = math.cos(theta), math.sin(theta)
    return (p[0], M[1] + dy * c + dz * s, M[2] - dy * s + dz * c)


def ram_angle(P: Vec3, E: Vec3) -> float:
    """The ram's angle from P to E, rad up from the turntable's backward (-z): the eye is behind the cylinder."""
    return math.atan2(E[1] - P[1], -(E[2] - P[2]))


def ram_pose(P: Vec3, E: Vec3, M: Vec3, theta: float) -> tuple[float, Vec3, float]:
    """The ram with the launcher raised `theta` rad (P the cylinder's pivot, E the rod's eye at bind, M the launcher's
    pivot; model space, the turntable's frame): (its turn from bind, rad, far end rising; where the eye is now; its
    length P..eye). Both bones turn by the same angle (src/katyusha.cpp RamTurn), so the rod slides along the
    cylinder's axis."""
    e = launcher_point(E, M, theta)
    return ram_angle(P, e) - ram_angle(P, E), e, math.dist((P[1], P[2]), (e[1], e[2]))


def ram_turn(p: Vec3, pivot: Vec3, d: float) -> Vec3:
    """`p` turned `d` rad about the x axis through `pivot`, the far (backward) end rising (src/katyusha.cpp RamTurn)."""
    dy, dz = p[1] - pivot[1], p[2] - pivot[2]
    c, s = math.cos(d), math.sin(d)
    return (p[0], pivot[1] + dy * c - dz * s, pivot[2] + dy * s + dz * c)


def _pieces(me, bone: int) -> list[list[int]]:  # noqa: ANN001 - mdb.Mesh
    """The connected pieces (vertex lists, by shared triangles) of `me` whose vertices are skinned to `bone`."""
    bi, _bw = g.skin_columns(me)
    par = list(range(me.nverts))

    def root(a: int) -> int:
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for t in g.triangles(me):
        par[root(t[0])] = root(t[1])
        par[root(t[1])] = root(t[2])
    out: dict[int, list[int]] = {}
    for v in range(me.nverts):
        if int(bi[v][0]) == bone:
            out.setdefault(root(v), []).append(v)
    return list(out.values())


def stroke(P: Vec3, E: Vec3, M: Vec3, stop_deg: float = PITCH_STOP_DEG) -> float:
    """How much longer the ram gets from 0 to `stop_deg` (m; it only lengthens on the way: checked)."""
    lengths = [ram_pose(P, E, M, math.radians(k))[2] for k in range(0, int(stop_deg) + 1)]
    _req(all(b >= a - 1e-6 for a, b in zip(lengths, lengths[1:])), 'the ram does not only lengthen')
    return lengths[-1] - lengths[0]


def split_ram(md: Mdb, info: dict) -> Mdb:
    """The prop's rod, end collar and eye onto RAM_ROD, a new bone at the eye (on the prop's axis) under
    Rocketcannon_base right after the prop; the rod lengthened into the cylinder by the stroke + RAM_OVERLAP. Every
    bone after the insertion is renumbered (blend indices too). info['ram'] gets the pivots and the stroke."""
    prop, base, main = (md.bone_index(n) for n in ('Rocketcannon_prop', 'Rocketcannon_base', 'Rocketcannon_main'))
    w = bind_world(md)
    P: Vec3 = (w[prop][12], w[prop][13], w[prop][14])
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    u = _unit((w[prop][0], w[prop][1], w[prop][2]))   # the prop's local x: along the ram, towards the eye
    hits = [(k, j) for k, o in enumerate(md.objects) for j, me in enumerate(o.meshes) if me.flags[1] and _pieces(me, prop)]
    _req(len(hits) == 1, f'the prop\'s geometry is in {len(hits)} meshes, expected 1')
    k, j = hits[0]
    me = md.objects[k].meshes[j]
    pos = g.mesh_positions(me)
    axial = [_dot((p[0] - P[0], p[1] - P[1], p[2] - P[2]), u) for p in pos]
    pieces = _pieces(me, prop)
    rod = [pc for pc in pieces if sum(axial[v] for v in pc) / len(pc) > RAM_SPLIT]
    _req(len(pieces) == 2 * RAM_PIECES and len(rod) == 2 * ROD_PIECES,
         f'the ram has {len(pieces)} pieces, {len(rod)} past {RAM_SPLIT} m (expected {2 * RAM_PIECES}, {2 * ROD_PIECES})')
    sides = [[pc for pc in rod if (pos[pc[0]][0] > P[0]) == left] for left in (True, False)]
    _req(all(len(sd) == ROD_PIECES for sd in sides), 'the rod\'s pieces are not three a side')
    near = lambda pc: min(axial[v] for v in pc)  # noqa: E731
    # The eye: each side's farthest piece; its centre (both sides' mean, on the prop's axis) is the rod's pivot.
    eyes = [max(sd, key=near) for sd in sides]
    eye_at = sum(sum(axial[v] for v in pc) / len(pc) for pc in eyes) / len(eyes)
    E: Vec3 = (P[0] + u[0] * eye_at, P[1] + u[1] * eye_at, P[2] + u[2] * eye_at)
    rise = stroke(P, E, M)
    reach = rise + RAM_OVERLAP
    # The rod proper is each side's piece nearest the cylinder: its front ring goes `reach` into the cylinder.
    rods = [min(sd, key=near) for sd in sides]
    stretch = {v for pc in rods for v in pc if axial[v] < near(pc) + 0.01}
    on_rod = {v for pc in rod for v in pc}

    at = max(g.subtree(md, base)) + 1
    _req(at == prop + 1, 'the prop is not the last bone under the launcher\'s base')
    names = list(md.names)
    name = g._name_index(names, RAM_ROD)
    bind = g.translation(E)
    shift = lambda i: i + 1 if i >= at else i  # noqa: E731
    bones = [replace(b, index=shift(b.index), parent=shift(b.parent) if b.parent >= 0 else -1) for b in md.bones]
    bones.insert(at, Bone(at, base, -1, -1, name, 0, 3, 0, 1, 0, 0, mmul(bind, g.inverse_affine_general(w[base])),
                          g.translation((-E[0], -E[1], -E[2])), [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    link(bones)
    objects = []
    for ko, o in enumerate(md.objects):
        meshes = []
        for jo, m in enumerate(o.meshes):
            if not m.flags[1]:
                meshes.append(m)
                continue
            keys, rows = vertex_table(m)
            pk, bk = g._pos_key(keys), g._bi_key(keys)
            bi, bw = g.skin_columns(m)
            ours = (ko, jo) == (k, j)
            for v, r in enumerate(rows):
                idx = [shift(int(x)) for x in r[bk]]
                if ours and v in on_rod:
                    _req(g.influences(bi[v], bw[v]) == {prop}, f'ram vertex {v} not on the prop alone')
                    idx[0] = at
                if ours and v in stretch:
                    p = r[pk]
                    r[pk] = (p[0] - u[0] * reach, p[1] - u[1] * reach, p[2] - u[2] * reach) + tuple(p[3:])
                r[bk] = tuple(idx)
            meshes.append(g.rebuild_mesh(m, rows, g.triangles(m)) or m)
        objects.append(replace(o, bone=shift(o.bone), meshes=meshes))
    info['ram'] = {'P': P, 'E': E, 'M': M, 'stroke': rise, 'rod_into_cylinder': reach}
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None)


def ram_report(md: Mdb, stop_deg: float = PITCH_STOP_DEG, step: int = 5) -> list[dict[str, float]]:
    """Per elevation 0..stop_deg (every `step` deg) the ram of `md` (with RAM_ROD) as src/katyusha.cpp poses it: its
    length P..eye, where the rod's front is along it (m from the cylinder's pivot) against the cylinder's span, and
    how far the eye's vertices stray outside the launcher's own box (in the launcher's frame; 0: inside); and, to
    compare, how far the stock pose (the whole prop turned by the elevation about P, rod and all) puts the eye out."""
    names = {md.name_of(b.name): b.index for b in md.bones}
    w = bind_world(md)
    pts = g.skinned_points(md)
    prop, rod, main = names['Rocketcannon_prop'], names[RAM_ROD], names['Rocketcannon_main']
    P: Vec3 = (w[prop][12], w[prop][13], w[prop][14])
    E: Vec3 = (w[rod][12], w[rod][13], w[rod][14])
    M: Vec3 = (w[main][12], w[main][13], w[main][14])
    u = _unit((E[0] - P[0], E[1] - P[1], E[2] - P[2]))
    cyl = [_dot((p[0] - P[0], p[1] - P[1], p[2] - P[2]), u) for p in pts[prop]]
    rod_rel = [_dot((p[0] - E[0], p[1] - E[1], p[2] - E[2]), u) for p in pts[rod]]
    eye = [p for p, a in zip(pts[rod], rod_rel) if a > -0.2]
    box = [(min(p[c] for p in pts[main]), max(p[c] for p in pts[main])) for c in range(3)]

    def outside(q: Vec3, theta: float) -> float:
        b = launcher_point(q, M, -theta)   # back into the launcher's frame
        return max(max(lo - b[c], b[c] - hi, 0.0) for c, (lo, hi) in enumerate(box))
    out = []
    for deg in range(0, int(stop_deg) + 1, step):
        t = math.radians(deg)
        d, e, length = ram_pose(P, E, M, t)
        posed = [ram_turn((q[0] + e[0] - E[0], q[1] + e[1] - E[1], q[2] + e[2] - E[2]), e, d) for q in eye]
        stock = [ram_turn(q, P, t) for q in eye]
        out.append({'deg': deg, 'length': length, 'rod_front': length + min(rod_rel), 'cyl_front': min(cyl),
                    'cyl_back': max(cyl), 'eye_out': max(outside(q, t) for q in posed),
                    'stock_eye_out': max(outside(q, t) for q in stock)})
    return out


# ------------------------------------------------------------------------------------------ build

def build_model(game) -> tuple[Mdb, Mdb, object, object, dict]:  # noqa: ANN001 - rootcpk.Game
    """(new model, stock host model, host Rab, donor Rab, info) - the archive is not assembled yet."""
    host_raw, donor_raw = game.read('OBJECT', HOST_ARC), game.read('OBJECT', DONOR_ARC)
    host_rab, donor_rab = rab_read(host_raw), rab_read(donor_raw)
    host0 = mdb_read(member(host_rab, HOST_MDB).data)
    donor = mdb_read(member(donor_rab, DONOR_MDB).data)
    info: dict = {}

    hb = {host0.name_of(b.name): b.index for b in host0.bones}
    db = {donor.name_of(b.name): b.index for b in donor.bones}
    for n in ['body'] + RACK + ALL_WHEELS + OBJECT_BONES:
        _req(n in hb, f'host bone {n} missing')
    for n in ['body'] + list(WHEEL_MAP):
        _req(n in db, f'donor bone {n} missing')
    rack = g.subtree(host0, hb['Rocketcannon_base'])
    hull_root = hb['body']        # the hull's skin bone (stock: body x4630 + mudguard / tire bones)

    # 1. strip: keep only the rack; object 0 (hull) kept empty for the truck, object 2 (catapi tracks) dropped
    md = g.strip_geometry(host0, rack, drop_empty_objects=False)
    _req([len(o.meshes) for o in md.objects] == [0, 1, 0], 'unexpected stock object / mesh layout')
    md = replace(md, objects=md.objects[:2])

    # 2. donor truck in host space: body -> host body, tires -> wheel bones; lowest point onto y = 0, x centred
    bone_map = {db['body']: hull_root} | {db[d]: hb[h] for d, h in WHEEL_MAP.items()}
    sel = lambda k, j, me: (k, j) in DONOR_TRUCK_MESHES  # noqa: E731
    probe = g.extract_meshes(donor, sel, bone_map, TRUCK_SCALE)
    P = [p for _m, me in probe for p in g.mesh_positions(me)]
    xs, ys = [p[0] for p in P], [p[1] for p in P]
    offset: Vec3 = (-(min(xs) + max(xs)) / 2, -min(ys), 0.0)
    truck = g.extract_meshes(donor, sel, bone_map, TRUCK_SCALE, offset)
    info['truck_offset'] = offset

    # 3. wheel bones onto the truck axles: used ones on their tire's centre, the rest on the nearest used axle (same side)
    dw = bind_world(donor)
    centre = {h: tuple(dw[db[d]][12 + c] * TRUCK_SCALE + offset[c] for c in range(3)) for d, h in WHEEL_MAP.items()}
    hw = bind_world(md)
    wheel_pos: dict[str, Vec3] = {}
    for name in ALL_WHEELS:
        if name in centre:
            wheel_pos[name] = centre[name]  # type: ignore[assignment]
        else:
            z0 = hw[hb[name]][14]
            near = min((h for h in centre if h.endswith(name[-1])), key=lambda h: abs(centre[h][2] - z0))
            wheel_pos[name] = centre[near]  # type: ignore[assignment]
    for name, p in wheel_pos.items():
        md = g.set_bone_origin(md, hb[name], p)
    info['wheels'] = wheel_pos

    # 4. donor materials + truck meshes into object 0 (Vehicle402_Rocket, kind-2 bone)
    md, mat_map = g.merge_materials(md, donor, {m for m, _me in truck})
    md = g.append_meshes(md, truck, mat_map, obj=0)

    # 5. rack onto the bed: front (0 elevation) CAB_CLEARANCE behind the cab, main box underside above the rails
    tg = truck_geometry(md, 0, hull_root)
    w = bind_world(md)
    base_o = w[hb['Rocketcannon_base']][12:15]
    ahead = max(p[2] for p in object_positions(md, 1)) - base_o[2]
    main_under = min(p[1] for p in g.skinned_points(md)[hb['Rocketcannon_main']]) - base_o[1]
    new_base = (0.0, max(tg['bed_floor_y'], tg['rail_top_y'] + RAIL_CLEARANCE - main_under),
                tg['cab_z'] - CAB_CLEARANCE - ahead)
    delta: Vec3 = (new_base[0] - base_o[0], new_base[1] - base_o[1], new_base[2] - base_o[2])
    md = g.move_bones(md, rack, delta)
    info['truck'] = tg
    info['rack_delta'] = delta

    # 6. the elevation ram made telescopic: the rod on a bone of its own (the bones from the prop's next renumbered)
    md = split_ram(md, info)

    # 7. bounds of what carries new geometry (body, used wheels, the ram, object bones); relink
    md = g.recompute_bounds(md, {md.bone_index(n) for n in ['body', 'Rocketcannon_prop', RAM_ROD] + list(centre)})
    md = g.relink(md)
    info['mat_map'] = mat_map
    return md, host0, host_rab, donor_rab, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished KATYUSHA.MRAB (the stock host archive, its model replaced, the donor textures added)."""
    md, _host0, host_rab, donor_rab, info = build_model(game)
    data = mdb_write(md)
    tex_files = sorted({md.textures[x.texture].filename for m in info['mat_map'].values() for x in md.materials[m].textures})
    g.copy_texture_members(host_rab, donor_rab, tex_files)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(host_rab, HOST_MDB).stored = stored
    return rab_write(host_rab)


# ------------------------------------------------------------------------------------------ check

# Fingerprint of the stock Vehicle402_Rocket.mdb skeleton (Root.cpk OBJECT/VEHICLE402_ROCKET.MRAB):
# per bone (name, parent, sibling, child, child_count, kind, depth_delta, bounded, sha256[:16] of the float32
# local + inv_bind (+ half + centre for bones whose bounds are kept)). Bones whose bind is moved carry their stock
# model-space bind instead (rotation rows 0-2 and translation, 5 decimals). Generated by make_fingerprint().
STOCK_BONES: list[tuple] = []          # filled below
STOCK_MOVED: dict[str, list[float]] = {}


def _bone_hash(b, with_bounds: bool) -> str:  # noqa: ANN001 - mdb.Bone
    raw = struct.pack('<32f', *b.local, *b.inv_bind)
    if with_bounds:
        raw += struct.pack('<8f', *b.half, *b.centre)
    return hashlib.sha256(raw).hexdigest()[:16]


def _bounds_kept(name: str) -> bool:
    return name not in ['body'] + OBJECT_BONES + [WHEEL_MAP[d] for d in WHEEL_MAP]


def make_fingerprint(host0: Mdb) -> tuple[list[tuple], dict[str, list[float]]]:
    """(STOCK_BONES, STOCK_MOVED) of a stock host model (used once to generate the constants above)."""
    w = bind_world(host0)
    bones, moved = [], {}
    for b in host0.bones:
        n = host0.name_of(b.name)
        mv = n in RACK or n in ALL_WHEELS
        bones.append((n, b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded,
                      None if mv else _bone_hash(b, _bounds_kept(n))))
        if mv:
            moved[n] = [round(x, 5) for x in w[b.index]]
    return bones, moved


def check(arc: bytes) -> None:
    """Re-read `arc` and raise KatyushaCheckError unless: the archive and model round-trip; < 256 bones; every mesh's
    vertex buffer, index count / range, blend indices (< bone count, skin bones only), weights and material index
    are valid; every material texture (HD and .lod) is an archive member; every bone's bind x inverse bind is the
    identity; the bone list (names, parents, kinds) is the stock one with RAM_ROD after the prop, the links follow the
    parents, every bone outside the rack / wheel bones bit-identical to stock, the rack bones moved by one common
    translation with their stock rotations, the wheel bones at stock rotation, each used one at the centre of its
    tire's vertices; the model sits on y = 0; the ram holds together over 0..PITCH_STOP_DEG (check_ram)."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = member(rab, HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    nb = len(md.bones)
    _req(nb < 256, f'{nb} bones')
    files = {f.name.lower() for f in rab.files}
    lowest = None
    for o in md.objects:
        _req(0 <= o.bone < nb and md.bones[o.bone].kind == 2, f'object {md.name_of(o.name)} not on a kind-2 bone')
        _req(len(o.meshes) > 0, f'object {md.name_of(o.name)} has no mesh')
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
            low = min(p[1] for p in g.mesh_positions(me))
            lowest = low if lowest is None else min(lowest, low)
    _req(lowest is not None and abs(lowest) < 2e-3, f'lowest vertex y = {lowest}, not on the ground')
    for m in md.materials:
        for x in m.textures:
            _req(0 <= x.texture < len(md.textures), f'material {md.name_of(m.name)}: texture {x.texture}')
            fn = md.textures[x.texture].filename
            stem, ext = fn.rsplit('.', 1)
            for want in (fn, f'{stem}.lod.{ext}'):
                _req(want.lower() in files, f'material {md.name_of(m.name)}: texture member {want} missing')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    # skeleton vs stock: the stock bones in their order with RAM_ROD after the prop; each stock bone's name, parent,
    # kind and bounds flag the stock one's; the links what the parents make of them (relink)
    stock_names = [s[0] for s in STOCK_BONES]
    at = stock_names.index('Rocketcannon_prop') + 1
    names = [md.name_of(b.name) for b in md.bones]
    _req(names == stock_names[:at] + [RAM_ROD] + stock_names[at:], f'bones {names}: not the stock ones with {RAM_ROD}')
    back = lambda i: i - 1 if i > at else i  # noqa: E731 - model index -> stock index (the rod excluded)
    linked = g.relink(md).bones
    deltas = []
    pts = g.skinned_points(md)
    for b in md.bones:
        n = md.name_of(b.name)
        lk = linked[b.index]
        _req((b.sibling, b.child, b.child_count, b.depth_delta) == (lk.sibling, lk.child, lk.child_count, lk.depth_delta),
             f'bone {b.index} {n}: links do not follow its parents')
        if b.index == at:
            continue
        s = STOCK_BONES[back(b.index)]
        _req((n, back(b.parent) if b.parent >= 0 else -1, b.kind, b.bounded) == (s[0], s[1], s[5], s[7]),
             f'bone {b.index} {n}: name / parent / kind differ from stock {s[:8]}')
        if s[8] is not None:
            _req(_bone_hash(b, _bounds_kept(n)) == s[8], f'bone {n}: matrices / bounds differ from stock')
            continue
        ref = STOCK_MOVED[n]
        _req(max(abs(w[b.index][k] - ref[k]) for k in range(12)) < 1e-4, f'bone {n}: rotation differs from stock')
        if n in RACK:
            deltas.append([w[b.index][12 + c] - ref[12 + c] for c in range(3)])
        elif b.index in pts:
            q = pts[b.index]
            mid = [(min(p[c] for p in q) + max(p[c] for p in q)) / 2 for c in range(3)]
            _req(all(abs(mid[c] - w[b.index][12 + c]) < 0.01 for c in range(3)), f'wheel {n}: not at its tire centre')
        else:
            _req(not any(b.index in (int(r[0]) for r in g.skin_columns(me)[0]) for o in md.objects for me in o.meshes),
                 f'wheel {n}: unexpected geometry')
    _req(len(deltas) == len(RACK), 'rack bones missing')
    _req(all(max(abs(d[c] - deltas[0][c]) for c in range(3)) < 1e-4 for d in deltas), 'rack bones moved apart')
    check_ram(md)
    for n in ALL_WHEELS:
        _req(any(md.bone_index(n) == k for k in pts) == (n in WHEEL_MAP.values()), f'wheel {n}: geometry mismatch')


def check_ram(md: Mdb) -> None:
    """RAM_ROD is a skin bone under Rocketcannon_base with the model's axes, on the prop's axis (the line the ram
    slides along), carrying the rod; and at every 5 deg from 0 to PITCH_STOP_DEG, posed as src/katyusha.cpp poses it
    (ram_report), the rod's front stays inside the cylinder (RAM_OVERLAP from its back end at the most, never past
    its front) and the eye inside the launcher's box: the ram neither parts nor comes off the launcher."""
    names = {md.name_of(b.name): b.index for b in md.bones}
    rod, prop, base = names.get(RAM_ROD, -1), names['Rocketcannon_prop'], names['Rocketcannon_base']
    _req(rod >= 0 and md.bones[rod].parent == base and md.bones[rod].kind == 3, f'{RAM_ROD} not a skin bone under the base')
    w = bind_world(md)
    _req(max(abs(a - b) for a, b in zip(w[rod][:12], (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))) < 1e-5, f"{RAM_ROD}: axes not the model's")
    pts = g.skinned_points(md)
    _req(len(pts.get(rod, [])) > 0 and len(pts.get(prop, [])) > 0, "the ram's cylinder or rod has no geometry")
    P, E = w[prop][12:15], w[rod][12:15]
    axis = _unit((w[prop][0], w[prop][1], w[prop][2]))
    off = [E[c] - P[c] - axis[c] * _dot((E[0] - P[0], E[1] - P[1], E[2] - P[2]), axis) for c in range(3)]
    _req(max(abs(x) for x in off) < 1e-4, f"{RAM_ROD} is off the prop's axis by {off}")
    for r in ram_report(md):
        _req(r['cyl_front'] < r['rod_front'] <= r['cyl_back'] - RAM_OVERLAP + 1e-4,
             f"{r['deg']} deg: the rod's front {r['rod_front']:.3f} m out of the cylinder ({r['cyl_front']:.3f}..{r['cyl_back']:.3f})")
        _req(r['eye_out'] < 1e-3, f"{r['deg']} deg: the ram's eye {r['eye_out']:.3f} m outside the launcher")


STOCK_BONES = [
    ('mdl', -1, -1, 1, 2, 0, 1, 0, '7fe77e43245cd060'),
    ('Vehicle402_Rocket', 0, 4, 2, 2, 2, 1, 0, '1084ffb78a77cdde'),
    ('Vehicle_Rocketcannon', 1, 3, -1, 0, 2, 0, 0, '1084ffb78a77cdde'),
    ('catapi', 1, -1, -1, 0, 2, -1, 0, '70c440320f06c33f'),
    ('body', 0, -1, 5, 30, 3, 1, 1, '1084ffb78a77cdde'),
    ('Rocketcannon_base', 4, 8, 6, 2, 3, 1, 1, None),
    ('Rocketcannon_main', 5, 7, -1, 0, 3, 0, 1, None),
    ('Rocketcannon_prop', 5, -1, -1, 0, 3, -1, 1, None),
    ('catapi_body', 4, 21, 9, 12, 3, 1, 1, '005edd2a14f7d634'),
    ('catapiA_l', 8, 10, -1, 0, 3, 0, 1, 'd1c8c079024a9e6f'),
    ('catapiA_r', 8, 11, -1, 0, 3, 0, 1, 'ada2f94ad95c05b4'),
    ('catapiB_l', 8, 12, -1, 0, 3, 0, 1, 'e94140c41dc03d47'),
    ('catapiB_r', 8, 13, -1, 0, 3, 0, 1, '3d41cb7452be29b0'),
    ('catapiC_l', 8, 14, -1, 0, 3, 0, 1, '002498a76e549e35'),
    ('catapiC_r', 8, 15, -1, 0, 3, 0, 1, 'af569597678aa3cf'),
    ('catapiD_l', 8, 16, -1, 0, 3, 0, 1, '0e04b281d64f5126'),
    ('catapiD_r', 8, 17, -1, 0, 3, 0, 1, 'b6dc00118f50599d'),
    ('catapiE_l', 8, 18, -1, 0, 3, 0, 1, '9ebff6dff33f23cf'),
    ('catapiE_r', 8, 19, -1, 0, 3, 0, 1, '9cb62a2867b829cc'),
    ('catapiF_l', 8, 20, -1, 0, 3, 0, 1, '42e164875f8ab644'),
    ('catapiF_r', 8, -1, -1, 0, 3, -1, 1, 'ef585c4445ac47c1'),
    ('mudGuard_l', 4, 22, -1, 0, 3, 0, 1, 'c556310364ede72a'),
    ('mudGuard_r', 4, 23, -1, 0, 3, 0, 1, '3fe4b4935c4a9a26'),
    ('tire_lockA_l', 4, 24, -1, 0, 3, 0, 1, 'ed8e0d8552b3a3fc'),
    ('tire_lockA_r', 4, 25, -1, 0, 3, 0, 1, '55d4433b37fe9cab'),
    ('tire_lockB_l', 4, 26, -1, 0, 3, 0, 1, 'e72836b6f5ecc4c8'),
    ('tire_lockB_r', 4, 27, -1, 0, 3, 0, 1, '9d8c8d178c8b16af'),
    ('tire_lockC_l', 4, 28, -1, 0, 3, 0, 1, 'b0cacf1b65ff6399'),
    ('tire_lockC_r', 4, 29, -1, 0, 3, 0, 1, 'b88661344c6c8200'),
    ('tire_lockD_l', 4, 30, -1, 0, 3, 0, 1, '681219cd8dcd4687'),
    ('tire_lockD_r', 4, 31, -1, 0, 3, 0, 1, 'c27ba5a1f769f7b3'),
    ('tire_lockE_l', 4, 32, -1, 0, 3, 0, 1, '42ec3e89b2410317'),
    ('tire_lockE_r', 4, 33, -1, 0, 3, 0, 1, '8573caa6042ff499'),
    ('tire_lockF_l', 4, 34, -1, 0, 3, 0, 1, '05f176d78ed98b49'),
    ('tire_lockF_r', 4, 35, -1, 0, 3, 0, 1, '3fda4c6c2a4f3252'),
    ('tire_lockG_l', 4, 36, -1, 0, 3, 0, 1, 'b78707e33dfcd8ec'),
    ('tire_lockG_r', 4, 37, -1, 0, 3, 0, 1, '7933f9ef27292b27'),
    ('tire_moveA_l', 4, 38, -1, 0, 3, 0, 1, None),
    ('tire_moveA_r', 4, 39, -1, 0, 3, 0, 1, None),
    ('tire_moveB_l', 4, 40, -1, 0, 3, 0, 1, None),
    ('tire_moveB_r', 4, 41, -1, 0, 3, 0, 1, None),
    ('tire_moveC_l', 4, 42, -1, 0, 3, 0, 1, None),
    ('tire_moveC_r', 4, 43, -1, 0, 3, 0, 1, None),
    ('tire_moveD_l', 4, 44, -1, 0, 3, 0, 1, None),
    ('tire_moveD_r', 4, 45, -1, 0, 3, 0, 1, None),
    ('tire_moveE_l', 4, 46, -1, 0, 3, 0, 1, None),
    ('tire_moveE_r', 4, 47, -1, 0, 3, 0, 1, None),
    ('tire_moveF_l', 4, 48, -1, 0, 3, 0, 1, None),
    ('tire_moveF_r', 4, -1, -1, 0, 3, 2, 1, None),
]
STOCK_MOVED = {
    'Rocketcannon_base': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.00845, -2.43447, 1.0],
    'Rocketcannon_main': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.36551, -3.33157, 1.0],
    'Rocketcannon_prop': [-0.0, 0.2041, -0.97895, 0.0, 0.0, 0.97895, 0.2041, 0.0, 1.0, 0.0, -0.0, 0.0, -0.00929, 2.34301, -1.8332, 1.0],
    'tire_moveA_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.5285, 2.37727, 1.0],
    'tire_moveA_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.5285, 2.37727, 1.0],
    'tire_moveB_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, 1.38061, 1.0],
    'tire_moveB_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, 1.38061, 1.0],
    'tire_moveC_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, 0.44228, 1.0],
    'tire_moveC_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, 0.44228, 1.0],
    'tire_moveD_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, -0.52622, 1.0],
    'tire_moveD_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, -0.52622, 1.0],
    'tire_moveE_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.45227, -1.46454, 1.0],
    'tire_moveE_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.45227, -1.46454, 1.0],
    'tire_moveF_l': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.9778, 0.47801, -2.34379, 1.0],
    'tire_moveF_r': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.9778, 0.47801, -2.34379, 1.0],
}
