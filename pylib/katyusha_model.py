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
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import replace

import graft_pure as g
from graft_pure import Vec3
from mdb import Mdb, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, mmul, rab_read, rab_write

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

    # 6. bounds of what carries new geometry (body, used wheels, object bones); relink (hierarchy unchanged)
    md = g.recompute_bounds(md, {hull_root} | {hb[h] for h in centre})
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
    identity; the bone list (names, parents, links, kinds) is the stock one, every bone outside the rack / wheel
    bones bit-identical to stock, the rack bones moved by one common translation with their stock rotations, the
    wheel bones at stock rotation, each used one at the centre of its tire's vertices; the model sits on y = 0."""
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
    # skeleton vs stock
    _req(len(md.bones) == len(STOCK_BONES), f'{len(md.bones)} bones, stock {len(STOCK_BONES)}')
    deltas = []
    pts = g.skinned_points(md)
    for b, s in zip(md.bones, STOCK_BONES):
        n = md.name_of(b.name)
        _req((n, b.parent, b.sibling, b.child, b.child_count, b.kind, b.depth_delta, b.bounded) == tuple(s[:8]),
             f'bone {b.index} {n}: name / links / kind differ from stock {s[:8]}')
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
    for n in ALL_WHEELS:
        _req(any(md.bone_index(n) == k for k in pts) == (n in WHEEL_MAP.values()), f'wheel {n}: geometry mismatch')


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
