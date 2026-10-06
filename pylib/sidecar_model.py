"""The sidecar motorcycle (边三轮摩托, tools/make_sidecar.py, src/sidecar.cpp): the stock Freed bike (V503_BIKE, class
Vehicle503_Bike) with a sidecar built from stock parts, read from the player's own Root.cpk (read only). Pure Python
(pylib: mdb, mdb_jet, graft_pure; no numpy).

    build_model(game)        -> (Mdb, Rab, info)    the bike's model with the sidecar
    build(game)              -> bytes               the finished EDF6VC_SIDECAR.MRAB
    build_collision(game)    -> (bytes, info)       the bike's ragdoll (its collision) with the sidecar's platform
    check(arc, host_bones)   / check_collision(shkt)  raise SidecarCheckError on any failure

Frame: the bike's model frame is the vehicle's (the model is rooted at veh+0x60 as every vehicle's): +y up, +z
forward, +x the vehicle's LEFT (the stock convention: te_ik_l, the left hand's IK bone, is at +x; the drill notes:
"+X 为车左"). The sidecar is on the right: x < 0.

What is built (docs/sidecar-re.md §2):
  * the platform: the stock transport container (V509_TRANSPORTBOX, the airdrop crate: a closed armoured box) scaled
    onto PLATFORM, a low tub 0.9 m wide, 1.9 m long and 0.4 m deep beside the bike, its floor clear of the ground. The
    gunner stands on it (src/sidecar.cpp holds them at GUNNER_POINT, on its top);
  * the sidecar's wheel: the bike's own front tyre (its triangles on the front_tire bone) copied to WHEEL_CENTRE,
    outboard of the platform. It is drawn only: the bike's physics keeps its two wheels (car_base_wheel names
    front_tire and rear_tire; a third wheel the BikeBase class would steer and drive is not something the SGO can add
    and the class's balance law would fight), and the plugin holds the outfit level instead (src/sidecar.cpp);
  * the marker bone MARKER_BONE (kind 3, no geometry) at GUNNER_POINT: what tells the plugin a sidecar bike from a
    stock Freed bike, and where its gunner stands.
Everything new is skinned to the stock `body` bone (one influence): it moves with the bike's body and needs no pose
of its own (a bone the vehicle's animation does not know is never posed on screen: docs/drill-re.md §5.4).

The collision (build_collision): the bike's chassis is the ragdoll's RagDollProxys.body compound (14 convex hulls, the
proxy frame = the model frame less the body bone's bind, BODY_PROXY). One hull, PLATFORM_HULL (the rear seat's top
cover, overlapped by the hulls round it), is moved and scaled onto the platform's box; the compound's bounding tree
and box follow. The hull is turned half round its long axis on the way (x and y negated: a rotation, not a mirror):
its flat bottom face becomes the platform's top, the floor the gunner stands on. Nothing else in the file changes
(sizes, items, the mass distributions: the bike weighs and balances as before).
"""
from __future__ import annotations

import math
import struct
from dataclasses import replace

import graft_pure as g
from mdb import Mdb, Mesh, VElem, bind_world, cmpl_compress, cmpl_decompress, ident, inverse_affine, mdb_read, mdb_write, mmul, rab_read, rab_write
from mdb_jet import link, pack_vertex, vertex_table

HOST_ARC, HOST_MDB = 'V503_BIKE.MRAB', 'v503_bike.mdb'
HOST_RAGDOLL = 'RAGDOLL_V503_BIKE.SHKT'
OUT_ARC = 'EDF6VC_SIDECAR.MRAB'
OUT_RAGDOLL = 'EDF6VC_SIDECAR_RAGDOLL.SHKT'
DONOR_ARC, DONOR_MDB = 'V509_TRANSPORTBOX.MRAB', 'v509_transportbox.mdb'
BODY_BONE = 'body'
TYRE_BONE = 'front_tire'
MARKER_BONE = 'edf6vc_sidecar'          # src/sidecar.cpp kMarkerBone (tools/selftest.py holds them equal)

Vec3 = tuple[float, float, float]
# The platform's box in the model frame (m): (low corner, high corner). Its inner side 0.12 m clear of the bike's
# widest point (|x| 0.40), its floor 0.26 m over the ground (the bike's wheels stand at y 0), its top at y 0.66.
PLATFORM: tuple[Vec3, Vec3] = ((-1.42, 0.26, -0.55), (-0.52, 0.66, 1.35))
# Where the gunner stands: the middle of the platform's top (src/sidecar.cpp kGunnerX/Y/Z).
GUNNER_POINT: Vec3 = (-0.97, 0.66, 0.40)
# The sidecar wheel's hub: outboard of the platform, level with the gunner, on the ground as the bike's (its tyre's
# radius is the front tyre's, TYRE_RADIUS: the hub at that height puts its bottom on y 0).
TYRE_RADIUS = 0.393
WHEEL_CENTRE: Vec3 = (-1.62, TYRE_RADIUS, 0.40)
# The ragdoll's body proxy sits at the body bone's bind position (RagDollProxys.body, no rotation).
BODY_PROXY: Vec3 = (0.0, 0.72, 0.669)
PLATFORM_HULL = 7          # the body compound's instance turned into the platform (the rear seat cover)


class SidecarCheckError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    """A check that also holds under python -O."""
    if not ok:
        raise SidecarCheckError(msg)


def member(rab, name: str):  # noqa: ANN001, ANN201 - mdb.Rab / RabFile
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{len(hits)} members {name}')
    return hits[0]


def _norm(v: list[float]) -> list[float]:
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return [v[0] / n, v[1] / n, v[2] / n]


# ------------------------------------------------------------------------------------------ the model

def insert_marker(md: Mdb) -> tuple[Mdb, int]:
    """`md` with MARKER_BONE (kind 3, bounded, no geometry) under the body bone at GUNNER_POINT, placed at the end of
    the body's subtree (preorder kept): every later bone, parent link and the object's bone shifted. Every skinned
    vertex is on a bone before it (checked)."""
    pi = md.bone_index(BODY_BONE)
    _req(pi >= 0, f'no bone {BODY_BONE}')
    at = max(g.subtree(md, pi)) + 1
    for o in md.objects:
        for me in o.meshes:
            if me.flags[1]:
                bi, _bw = g.skin_columns(me)
                _req(all(int(x) < at for i4 in bi for x in i4), 'a skinned vertex on a bone the insertion moves')
    shift = lambda i: i + 1 if i >= at else i  # noqa: E731
    names = list(md.names)
    name = g._name_index(names, MARKER_BONE)
    w = bind_world(md)
    world = g.translation(GUNNER_POINT)
    bones = [replace(b, index=shift(b.index), parent=shift(b.parent) if b.parent >= 0 else -1) for b in md.bones]
    from mdb import Bone
    bones.insert(at, Bone(at, pi, -1, -1, name, 0, 3, 0, 1, 0, 0, mmul(world, inverse_affine(w[pi])),
                          g.translation((-GUNNER_POINT[0], -GUNNER_POINT[1], -GUNNER_POINT[2])),
                          [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]))
    link(bones)
    objects = [replace(o, bone=shift(o.bone)) for o in md.objects]
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None), at


def box_map(lo: Vec3, hi: Vec3, to: tuple[Vec3, Vec3]) -> tuple[Vec3, Vec3]:
    """(scale, offset) per axis taking the box lo..hi onto `to`: p -> p * scale + offset."""
    s = tuple((to[1][c] - to[0][c]) / (hi[c] - lo[c]) for c in range(3))
    o = tuple(to[0][c] - lo[c] * s[c] for c in range(3))
    return s, o  # type: ignore[return-value]


def platform_mesh(donor: Mdb, body: int) -> tuple[int, list[Mesh]]:
    """The donor container's meshes scaled onto PLATFORM (per axis: normals by the inverse scale, tangents and
    binormals by the scale, renormalised; their handedness w kept, the scale has no flip), each made a skinned mesh
    of one influence on `body` (BLENDWEIGHT float4 and BLENDINDICES ubyte4 appended to its layout, as the stock bike's
    meshes have them). Returns the donor material and the meshes."""
    _req(len(donor.objects) == 1, f'{DONOR_MDB}: {len(donor.objects)} objects')
    meshes = donor.objects[0].meshes
    _req(meshes and all(not me.flags[1] for me in meshes), f'{DONOR_MDB}: not rigid meshes')
    _req(len({me.material for me in meshes}) == 1, f'{DONOR_MDB}: more than one material')
    P = [p for me in meshes for p in g.mesh_positions(me)]
    lo = tuple(min(p[c] for p in P) for c in range(3))
    hi = tuple(max(p[c] for p in P) for c in range(3))
    s, off = box_map(lo, hi, PLATFORM)  # type: ignore[arg-type]
    out = []
    for me in meshes:
        keys, rows = vertex_table(me)
        names = [k.split(':')[0] for k in keys]
        _req(names[0] == 'position' and 'BLENDWEIGHT' not in names, f'{DONOR_MDB}: layout {names}')
        for r in rows:
            for k, n in enumerate(names):
                v = list(r[k])
                if n == 'position':
                    v[0], v[1], v[2] = v[0] * s[0] + off[0], v[1] * s[1] + off[1], v[2] * s[2] + off[2]
                elif n == 'normal':
                    v[0], v[1], v[2] = _norm([v[0] / s[0], v[1] / s[1], v[2] / s[2]])
                elif n in ('tangent', 'binormal'):
                    v[0], v[1], v[2] = _norm([v[0] * s[0], v[1] * s[1], v[2] * s[2]])
                r[k] = tuple(v)
        elems = list(me.elems) + [VElem(1, me.vsize, 0, 'BLENDWEIGHT'), VElem(21, me.vsize + 16, 0, 'BLENDINDICES')]
        vsize = me.vsize + 20
        vdata = b''.join(pack_vertex(elems, vsize, r + [(1.0, 0.0, 0.0, 0.0), (body, 0, 0, 0)]) for r in rows)
        out.append(replace(me, flags=bytes([0, 1, 1, 0]), vsize=vsize, elems=elems, vdata=vdata))
    return meshes[0].material, out


def wheel_meshes(host: Mdb, body: int) -> list[tuple[int, Mesh]]:
    """The bike's front tyre (its triangles wholly on the front_tire bone) re-skinned to `body` and moved so that its
    hub, the front_tire bone's bind position, is at WHEEL_CENTRE (no turn: its axle stays across the bike)."""
    tyre = host.bone_index(TYRE_BONE)
    hub = bind_world(host)[tyre][12:15]
    _req(abs(hub[1] - TYRE_RADIUS) < 0.01, f'{TYRE_BONE} hub at {hub}: TYRE_RADIUS is {TYRE_RADIUS}')
    off = (WHEEL_CENTRE[0] - hub[0], WHEEL_CENTRE[1] - hub[1], WHEEL_CENTRE[2] - hub[2])
    got = g.extract_meshes(host, lambda _o, _m, _me: True, {tyre: body}, 1.0, off)
    _req(bool(got), f'nothing on {TYRE_BONE}')
    return got


def build_model(game) -> tuple[Mdb, object, dict]:  # noqa: ANN001 - rootcpk.Game
    """(model, host Rab with the donor's textures copied in, info)."""
    rab = rab_read(game.read('OBJECT', HOST_ARC))
    host = mdb_read(member(rab, HOST_MDB).data)
    donor_rab = rab_read(game.read('OBJECT', DONOR_ARC))
    donor = mdb_read(member(donor_rab, DONOR_MDB).data)
    _req(len(host.objects) == 1, f'{HOST_MDB}: {len(host.objects)} objects')
    wheel = wheel_meshes(host, host.bone_index(BODY_BONE))
    md, marker = insert_marker(host)
    body = md.bone_index(BODY_BONE)
    mat, platform = platform_mesh(donor, body)
    md, mat_map = g.merge_materials(md, donor, [mat])
    first = len(md.objects[0].meshes)
    md = g.append_meshes(md, [(mat, me) for me in platform], mat_map, obj=0)
    md = g.append_meshes(md, wheel, {m: m for m, _me in wheel}, obj=0)
    o = md.objects[0]
    md = replace(md, objects=[replace(o, meshes=o.meshes[:first] + [replace(me, mesh_index=first + k)
                                                                   for k, me in enumerate(o.meshes[first:])])])
    md = g.recompute_bounds(md, {body})
    textures = sorted({donor.textures[x.texture].filename for x in donor.materials[mat].textures}, key=str.lower)
    copied = g.copy_texture_members(rab, donor_rab, textures)
    info = {'marker bone': marker, 'meshes': len(md.objects[0].meshes), 'platform meshes': len(platform),
            'wheel meshes': len(wheel), 'textures copied': copied}
    return md, rab, info


def build_with_info(game) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    md, rab, info = build_model(game)
    data = mdb_write(md)
    stored = cmpl_compress(data)
    _req(cmpl_decompress(stored) == data, 'CMPL round trip failed')
    member(rab, HOST_MDB).stored = stored
    return rab_write(rab), md, info


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The finished EDF6VC_SIDECAR.MRAB."""
    return build_with_info(game)[0]


def stock_bones(game) -> list[str]:  # noqa: ANN001 - rootcpk.Game
    md = mdb_read(member(rab_read(game.read('OBJECT', HOST_ARC)), HOST_MDB).data)
    return [md.name_of(b.name) for b in md.bones]


def check(arc: bytes, host_bones: list[str] | None = None) -> dict:
    """The archive opens and its model round-trips; every skin bone's bind x inverse bind is identity; the stock bones keep
    their names and order with MARKER_BONE (no geometry) at GUNNER_POINT right after the body's subtree; every new
    vertex is on the body bone alone; the platform's vertices fill PLATFORM, the wheel's sit round WHEEL_CENTRE within
    the tyre's radius, its lowest on the ground (y 0, as the bike's own wheels); the donor's textures are in the
    archive. Returns what it measured."""
    rab = rab_read(arc)
    md = mdb_read(member(rab, HOST_MDB).data)
    _req(mdb_write(md) == cmpl_decompress(member(rab, HOST_MDB).data), 'the model does not round-trip')
    w = bind_world(md)
    for b in md.bones:
        if b.kind != 3:
            continue   # the stock IK and object bones' inverse binds are not their binds' (te_ik_l: 1.37 off)
        err = max(abs(x - y) for x, y in zip(mmul(w[b.index], b.inv_bind), ident()))
        _req(err < 1e-4, f'{md.name_of(b.name)}: bind x inverse bind off by {err}')
    names = [md.name_of(b.name) for b in md.bones]
    m = md.bone_index(MARKER_BONE)
    body = md.bone_index(BODY_BONE)
    _req(m > 0 and md.bones[m].parent == body and md.bones[m].kind == 3, f'{MARKER_BONE}: {m}')
    _req(m == max(g.subtree(md, body)) and m - 1 in g.subtree(md, body), f'{MARKER_BONE} not at the end of the body subtree')
    if host_bones is not None:
        _req(names[:m] + names[m + 1:] == host_bones, f'stock bones changed: {names}')
    _req(max(abs(a - b) for a, b in zip(w[m][12:15], GUNNER_POINT)) < 1e-4, f'{MARKER_BONE} at {w[m][12:15]}')
    pts = g.skinned_points(md)
    _req(m not in pts, f'vertices on {MARKER_BONE}')
    o = md.objects[0]
    host_meshes = len(o.meshes)
    new = [me for me in o.meshes if md.name_of(md.materials[me.material].name) == DONOR_MDB.split('.')[0]]
    _req(bool(new), 'no platform meshes')
    P = [p for me in new for p in g.mesh_positions(me)]
    lo = [min(p[c] for p in P) for c in range(3)]
    hi = [max(p[c] for p in P) for c in range(3)]
    _req(all(abs(lo[c] - PLATFORM[0][c]) < 2e-3 and abs(hi[c] - PLATFORM[1][c]) < 2e-3 for c in range(3)),
         f'platform at {lo}..{hi}, PLATFORM is {PLATFORM}')
    for me in new:
        bi, bw = g.skin_columns(me)
        _req(all(int(i[0]) == body and abs(x[0] - 1.0) < 1e-6 for i, x in zip(bi, bw)), 'a platform vertex off the body bone')
    # The wheel: the vertices outboard of the platform (x < its outer side) are the sidecar tyre's.
    wheel = [p for p in pts.get(body, []) if p[0] < PLATFORM[0][0] - 1e-3]
    _req(len(wheel) > 50, f'{len(wheel)} wheel vertices')
    r = max(math.hypot(p[1] - WHEEL_CENTRE[1], p[2] - WHEEL_CENTRE[2]) for p in wheel)
    _req(abs(r - TYRE_RADIUS) < 0.02, f'the wheel reaches {r:.3f} m from its hub, the tyre {TYRE_RADIUS}')
    low = min(p[1] for p in wheel)
    _req(abs(low) < 0.02, f'the wheel stands at y {low:.3f}, the ground is 0')
    have = {f.name.lower() for f in rab.files}
    for t in md.textures:
        _req(t.filename.lower() in have, f'texture {t.filename} missing from the archive')
    return {'bones': len(md.bones), 'marker': m, 'meshes': host_meshes, 'platform': [lo, hi],
            'wheel vertices': len(wheel), 'wheel radius': round(r, 4), 'wheel low': round(low, 4)}


# ------------------------------------------------------------------------------------------ the collision

class _Shkt:
    """The bike's ragdoll tagfile, the parts this edit reads and writes (offsets from pylib/hktag.py's type table)."""

    def __init__(self, data: bytes) -> None:
        from hkcms import bodies
        from hktag import Tag
        self.buf = bytearray(data)
        self.tag = t = Tag(bytes(data))
        self.body = bodies(t)['RagDollProxys.body']
        C = 'hknpCompoundShape'
        typ, self.at, _ = t.item(self.body)
        _req(typ == C, f'RagDollProxys.body is a {typ}')
        _, self.inst_at, self.inst_n = t.item(t.u32(self.at + t.offset(C, 'instances')))
        self.isize = t.size('hknpShapeInstance')
        bvd = self.at + t.offset(C, 'boundingVolumeData')
        self.simd = t.u32(bvd + t.offset('hknpCompoundShapeData', 'simdTree') + t.offset('hkcdSimdTree', 'nodes'))
        _req(self.buf[bvd + t.offset('hknpCompoundShapeData', 'type')] == 2, 'the compound has no simd tree')
        self.aabb_at = self.at + t.offset(C, 'aabb')
        self.radius_at = self.at + t.offset(C, 'boundingRadius')

    def instance(self, k: int) -> tuple[int, int]:
        """(instance offset, its convex shape's offset)."""
        t = self.tag
        ia = self.inst_at + k * self.isize
        typ, sat, _ = t.item(t.u32(ia + t.offset('hknpShapeInstance', 'shape')))
        _req(typ == 'hknpConvexShape', f'instance {k} is a {typ}')
        return ia, sat

    def identity(self, k: int) -> bool:
        ia, _ = self.instance(k)
        return struct.unpack_from('<7f', self.buf, ia) == (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0) and \
            struct.unpack_from('<3f', self.buf, ia + 32) == (1.0, 1.0, 1.0)

    def hull(self, k: int) -> tuple[float, list[int], list[int]]:
        """(convex radius, the vertices' and the planes' offsets)."""
        t = self.tag
        _, sat = self.instance(k)
        H = sat + t.offset('hknpConvexShape', 'hull')
        _, va, vn = t.item(t.u32(H + t.offset('hknpConvexHull', 'vertices')))
        _, pa, pn = t.item(t.u32(H + t.offset('hknpConvexHull', 'planes')))
        r = struct.unpack_from('<f', self.buf, sat + t.offset('hknpConvexShape', 'convexRadius'))[0]
        return r, [va + 12 * j for j in range(vn)], [pa + 16 * j for j in range(pn)]

    def vertices(self, k: int) -> list[Vec3]:
        _, vs, _ = self.hull(k)
        return [struct.unpack_from('<3f', self.buf, a) for a in vs]

    def planes(self, k: int) -> list[tuple[float, float, float, float]]:
        _, _, ps = self.hull(k)
        return [struct.unpack_from('<4f', self.buf, a) for a in ps]

    def box(self, k: int) -> tuple[list[float], list[float]]:
        """The instance's box as its tree leaf holds it: its hull's vertices' box grown by its convex radius."""
        r = self.hull(k)[0]
        V = self.vertices(k)
        return [min(v[c] for v in V) - r for c in range(3)], [max(v[c] for v in V) + r for c in range(3)]

    def leaves(self) -> dict[int, tuple[list[float], list[float]]]:
        """Every leaf lane of the simd tree: instance -> its box as stored."""
        t = self.tag
        _, at, n = t.item(self.simd)
        N = 'hkcdSimdTree::Node'
        size, o_data, o_leaf = t.size(N), t.offset(N, 'data'), t.offset(N, 'isLeaf')
        out = {}
        for k in range(n):
            if not self.buf[at + size * k + o_leaf]:
                continue
            f = struct.unpack_from('<24f', self.buf, at + size * k)
            data = struct.unpack_from('<4I', self.buf, at + size * k + o_data)
            for lane in range(4):
                lo = [f[0 + lane], f[8 + lane], f[16 + lane]]
                hi = [f[4 + lane], f[12 + lane], f[20 + lane]]
                if lo[0] <= hi[0]:
                    out[data[lane]] = (lo, hi)
        return out


def platform_box() -> tuple[Vec3, Vec3]:
    """PLATFORM in the body proxy's frame."""
    return (tuple(PLATFORM[0][c] - BODY_PROXY[c] for c in range(3)),   # type: ignore[return-value]
            tuple(PLATFORM[1][c] - BODY_PROXY[c] for c in range(3)))


def hull_map(V: list[Vec3], radius: float) -> tuple[Vec3, Vec3]:
    """(scale, offset) per axis taking the hull's vertex box, turned half round its long axis (x and y negated: the
    flat bottom face up), onto the platform's box less the convex radius (the shell the radius adds stays inside it)."""
    lo, hi = platform_box()
    to = (tuple(lo[c] + radius for c in range(3)), tuple(hi[c] - radius for c in range(3)))
    flip = (-1.0, -1.0, 1.0)
    F = [tuple(v[c] * flip[c] for c in range(3)) for v in V]
    flo = tuple(min(f[c] for f in F) for c in range(3))
    fhi = tuple(max(f[c] for f in F) for c in range(3))
    s, o = box_map(flo, fhi, to)  # type: ignore[arg-type]
    return tuple(s[c] * flip[c] for c in range(3)), o   # type: ignore[return-value]


def build_collision(game) -> tuple[bytes, dict]:  # noqa: ANN001 - rootcpk.Game
    """The bike's ragdoll with its body compound's PLATFORM_HULL moved onto the platform (see the module doc): its
    vertices mapped p -> p * scale + offset (a positive determinant: a turn and a stretch, no mirror), each plane
    (n, d) of n.p + d = 0 rewritten exactly for the map (n' = n / scale, renormalised with d'), its tree leaf, the
    tree's inner boxes, the compound's box and bounding radius grown to hold it."""
    s = _Shkt(game.read('OBJECT', HOST_RAGDOLL))
    _req(s.inst_n == 14, f'RagDollProxys.body has {s.inst_n} hulls')
    _req(all(s.identity(k) for k in range(s.inst_n)), 'a body hull with a transform of its own')
    stored = s.leaves()
    _req(sorted(stored) == list(range(s.inst_n)), f'tree leaves {sorted(stored)}')
    radius, vs, ps = s.hull(PLATFORM_HULL)
    V = s.vertices(PLATFORM_HULL)
    scale, off = hull_map(V, radius)
    _req(scale[0] * scale[1] * scale[2] > 0.0, 'the map mirrors the hull')
    for a, v in zip(vs, V):
        struct.pack_into('<3f', s.buf, a, *(v[c] * scale[c] + off[c] for c in range(3)))
    for a in ps:
        n0, n1, n2, d = struct.unpack_from('<4f', s.buf, a)
        n = [n0 / scale[0], n1 / scale[1], n2 / scale[2]]
        L = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        d2 = d - (n[0] * off[0] + n[1] * off[1] + n[2] * off[2])
        struct.pack_into('<4f', s.buf, a, n[0] / L, n[1] / L, n[2] / L, d2 / L)
    _refresh_tree(s, PLATFORM_HULL)
    lo, hi = s.box(PLATFORM_HULL)
    box = struct.unpack_from('<8f', s.buf, s.aabb_at)
    struct.pack_into('<8f', s.buf, s.aabb_at, min(box[0], lo[0]), min(box[1], lo[1]), min(box[2], lo[2]), box[3],
                     max(box[4], hi[0]), max(box[5], hi[1]), max(box[6], hi[2]), box[7])
    far = max(math.sqrt(sum(c * c for c in v)) for v in s.vertices(PLATFORM_HULL)) + radius
    old = struct.unpack_from('<f', s.buf, s.radius_at)[0]
    struct.pack_into('<f', s.buf, s.radius_at, max(old, far))
    return bytes(s.buf), {'hull': PLATFORM_HULL, 'scale': scale, 'offset': off, 'box': (lo, hi), 'bounding radius': max(old, far)}


def _refresh_tree(s: _Shkt, k: int) -> None:
    """The simd tree's leaf lane of instance `k` := its box (Shkt.box), every inner lane := the union of its child
    node's lanes; the other leaves keep what they hold."""
    t = s.tag
    _, at, n = t.item(s.simd)
    N = 'hkcdSimdTree::Node'
    size, o_data, o_leaf = t.size(N), t.offset(N, 'data'), t.offset(N, 'isLeaf')

    def node(i: int) -> tuple[list[float], list[float]]:
        base = at + size * i
        f = list(struct.unpack_from('<24f', s.buf, base))
        data = struct.unpack_from('<4I', s.buf, base + o_data)
        leaf = s.buf[base + o_leaf]
        lo_all, hi_all = [math.inf] * 3, [-math.inf] * 3
        for lane in range(4):
            if f[lane] > f[4 + lane]:
                continue       # an empty lane (+FLT_MAX / -FLT_MAX)
            if leaf:
                if data[lane] == k:
                    lo, hi = s.box(k)
                else:
                    lo, hi = [f[lane], f[8 + lane], f[16 + lane]], [f[4 + lane], f[12 + lane], f[20 + lane]]
            else:
                lo, hi = node(data[lane])
            f[lane], f[8 + lane], f[16 + lane] = lo
            f[4 + lane], f[12 + lane], f[20 + lane] = hi
            lo_all = [min(a, b) for a, b in zip(lo_all, lo)]
            hi_all = [max(a, b) for a, b in zip(hi_all, hi)]
        struct.pack_into('<24f', s.buf, base, *f)
        return lo_all, hi_all
    _req(n >= 2, 'an empty simd tree')
    node(1)


def check_collision(data: bytes, stock: bytes | None = None) -> dict:
    """The edited ragdoll: the platform hull's vertices fill the platform's box (less its convex radius) with a flat
    top (its old bottom face) at the box's top; every one of its planes holds its face's vertices (each plane has at
    least three vertices on it) and has every vertex on its inner side; every tree leaf holds its instance's box, every
    inner lane the union under it; the compound's box holds every hull. With `stock`: nothing but the platform hull's
    vertices and planes, the tree's boxes, the compound's box and bounding radius differs. Returns what it measured."""
    s = _Shkt(data)
    radius = s.hull(PLATFORM_HULL)[0]
    V = s.vertices(PLATFORM_HULL)
    lo, hi = platform_box()
    vlo = [min(v[c] for v in V) for c in range(3)]
    vhi = [max(v[c] for v in V) for c in range(3)]
    _req(all(abs(vlo[c] - (lo[c] + radius)) < 1e-4 and abs(vhi[c] - (hi[c] - radius)) < 1e-4 for c in range(3)),
         f'platform hull {vlo}..{vhi}, the box {lo}..{hi}')
    top = [v for v in V if abs(v[1] - vhi[1]) < 0.01]   # the old bottom face: within 2 mm of level
    _req(len(top) >= 4, f'{len(top)} vertices on the platform top: not a flat floor')
    floor = (max(v[0] for v in top) - min(v[0] for v in top)) * (max(v[2] for v in top) - min(v[2] for v in top))
    for p in s.planes(PLATFORM_HULL):
        _req(abs(p[0] * p[0] + p[1] * p[1] + p[2] * p[2] - 1.0) < 1e-4, f'plane {p} not unit')
        dist = [p[0] * v[0] + p[1] * v[1] + p[2] * v[2] + p[3] for v in V]
        _req(max(dist) < 1e-4, f'plane {p}: a vertex {max(dist):.5f} m outside it')
        _req(sum(1 for d in dist if abs(d) < 1e-4) >= 3, f'plane {p}: fewer than three vertices on it')
    leaves = s.leaves()
    for k in range(s.inst_n):
        blo, bhi = s.box(k)
        llo, lhi = leaves[k]
        _req(all(llo[c] <= blo[c] + 1e-5 and lhi[c] >= bhi[c] - 1e-5 for c in range(3)), f'tree leaf {k} misses its hull')
    box = struct.unpack_from('<8f', s.buf, s.aabb_at)
    for k in range(s.inst_n):
        blo, bhi = s.box(k)
        _req(all(box[c] <= blo[c] + 1e-5 and box[4 + c] >= bhi[c] - 1e-5 for c in range(3)), f'the compound box misses hull {k}')
    if stock is not None:
        st = _Shkt(stock)
        _req(len(st.buf) == len(s.buf), 'the file changed size')
        _, vs, ps = s.hull(PLATFORM_HULL)
        allowed = set()
        for a in vs:
            allowed |= set(range(a, a + 12))
        for a in ps:
            allowed |= set(range(a, a + 16))
        _, tat, tn = s.tag.item(s.simd)
        size = s.tag.size('hkcdSimdTree::Node')
        for i in range(tn):
            allowed |= set(range(tat + size * i, tat + size * i + 96))
        allowed |= set(range(s.aabb_at, s.aabb_at + 32)) | set(range(s.radius_at, s.radius_at + 4))
        diff = [i for i in range(len(s.buf)) if s.buf[i] != st.buf[i] and i not in allowed]
        _req(not diff, f'{len(diff)} bytes changed outside the platform edit (first at {diff[:4]})')
        for k in range(s.inst_n):
            if k != PLATFORM_HULL:
                _req(s.vertices(k) == st.vertices(k) and s.planes(k) == st.planes(k), f'hull {k} changed')
                _req(leaves[k] == st.leaves()[k], f'tree leaf {k} changed')
    return {'hull box': (vlo, vhi), 'top y (model)': vhi[1] + BODY_PROXY[1] + radius, 'floor m2': round(floor, 3),
            'radius': radius}
