"""A vehicle's physics skeleton (its ragdoll .shkt and the SGO's binding blob) refitted to a model whose bones moved.

A stock vehicle's moving parts are Havok bodies: the ragdoll tagfile holds one body ("proxy") per part at the part's
bone (bodyCinfo position / orientation), the joints between them (each constraint's transformA / transformB: the joint
in the child's and in the parent's frame) and the ragdoll skeleton's reference pose (each proxy relative to its
parent). The SGO's `ragdoll` entry binds them to the model by name (the blob after its path):
  ragdoll_from_animation  bone -> proxy, an offset (t, q): proxy = bone o offset
  animation_from_ragdoll  proxy -> bone, an offset:        bone = proxy o offset
(o: compose; (t, q) applied as p -> t + q p). In every stock vehicle these agree with the model's bind pose (checked:
problems() of the stock model and ragdoll is empty). The game keeps them in step every frame (EDF.dll, RVAs of
TimeDateStamp 0x678CCB46; docs/artillery-re.md): 0x6EDCA0 writes each dynamic proxy's world o offset into its bones'
world rows; 0x6ED800 moves each animation-driven proxy to its bone o offset; the vehicle's constructor poses the
proxies from the bind pose (0x6EB760 -> slot 45 -> 0x6ED800) and CarBase's car_base_constraint_data constraints keep
the relative pose they find then (0x6676C1..0x667746). A model whose bones moved while the ragdoll stayed stock has
two incompatible rests for one joint (the shkt's hinge at the stock point, the snapshot at the moved bone): a turret
held at two pivots cannot yaw.

    problems(md, shkt, blob) -> list[str]   what disagrees (empty: the physics skeleton is the model's)
    fit(md, shkt, blob) -> (shkt, blob)      each proxy moved with its bone (orientation kept), the joints with their
                                             child, the reference pose and the binding offsets recomputed; refuses a
                                             stock ragdoll that does not follow the rules above, or a moved bone
                                             whose rotation is not its proxy's
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

import sgo
from hktag import Tag
from mdb import Mdb, bind_world

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]     # x, y, z, w (Havok's order)
TOL = 1e-3                                   # m / rotation-matrix entries: the stock data agrees to ~1e-5
KEEP = 1e-5                                  # m: a value this close to the stock one is left as stored (float noise)


class RagdollFitError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise RagdollFitError(msg)


# ------------------------------------------------------------------------------------------ rotations

def qmul(a: Quat, b: Quat) -> Quat:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def conj(q: Quat) -> Quat:
    return (-q[0], -q[1], -q[2], q[3])


def rot(q: Quat, v: Vec3) -> Vec3:
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), conj(q))
    return (p[0], p[1], p[2])


def axes(q: Quat) -> list[Vec3]:
    """The frame's x, y, z axes in its parent: the rows of the row-vector 3x3 (mdb bind rows)."""
    return [rot(q, e) for e in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))]


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dist(a: Vec3, b: Vec3) -> float:
    return math.dist(a, b)


@dataclass
class Frame:
    t: Vec3
    q: Quat

    def __mul__(self, o: 'Frame') -> 'Frame':      # self o o: o's local applied inside self
        return Frame(add(self.t, rot(self.q, o.t)), qmul(self.q, o.q))


def rows_off(a: list[Vec3], b: list[Vec3]) -> float:
    return max(abs(x - y) for ra, rb in zip(a, b) for x, y in zip(ra, rb))


# ------------------------------------------------------------------------------------------ the tagfile

@dataclass
class Body:
    name: str
    at: int          # bodyCinfo offset
    frame: Frame


@dataclass
class Joint:
    child: int       # bodyA
    parent: int      # bodyB
    a: int           # transformA's offset (rotation columns, then translation at +48)
    b: int


class Shkt:
    """The ragdoll tagfile's bodies, joints and reference pose (offsets from pylib/hktag.py's type table)."""

    def __init__(self, data: bytes) -> None:
        self.buf = bytearray(data)
        self.tag = t = Tag(bytes(data))
        scene = t.root_variant('hknpPhysicsSceneData')
        _, sat, ns = t.item(t.u32(scene + t.offset('hknpPhysicsSceneData', 'systemDatas')))
        _req(ns == 1, f'{ns} physics systems, a vehicle ragdoll has one')
        typ, rag, _ = t.item(t.u32(sat))
        _req(typ in ('hknpRagdollData', 'hknpPhysicsSystemData'), f'the physics system is a {typ}')
        B = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
        _, bat, nb = t.item(t.u32(rag + t.offset('hknpPhysicsSystemData', 'bodyCinfos')))
        o_name, o_pos, o_q = t.offset(B, 'name'), t.offset(B, 'position'), t.offset(B, 'orientation')
        self.bodies = []
        for j in range(nb):
            at = bat + j * t.size(B)
            self.bodies.append(Body(t.cstr(t.u32(at + o_name)), at,
                                    Frame(self._v3(at + o_pos), struct.unpack_from('<4f', self.buf, at + o_q))))
        self.index = {b.name: k for k, b in enumerate(self.bodies)}
        _, cat, nc = t.item(t.u32(rag + t.offset('hknpPhysicsSystemData', 'constraintCinfos')))
        C = 'hknpConstraintCinfo'
        self.joints = []
        for j in range(nc):
            at = cat + j * t.size(C)
            typ, dat, _ = t.item(t.u32(at + t.offset(C, 'constraintData')))
            tr = dat + t.offset(typ, 'atoms') + t.offset(f'{typ}::Atoms', 'transforms')
            A = 'hkpSetLocalTransformsConstraintAtom'
            self.joints.append(Joint(t.u32(at + t.offset(C, 'bodyA')), t.u32(at + t.offset(C, 'bodyB')),
                                     tr + t.offset(A, 'transformA'), tr + t.offset(A, 'transformB')))
        self.parents, self.bone_body, self.ref = [], [], []
        if typ != 'hknpRagdollData':     # a plain physics system (the Blacker's): no ragdoll skeleton
            return
        _, sat, _ = t.item(t.u32(rag + t.offset('hknpRagdollData', 'skeleton')))
        S = 'hkaSkeleton'
        _, pa, pn = t.item(t.u32(sat + t.offset(S, 'parentIndices')))
        _, ba, bn = t.item(t.u32(sat + t.offset(S, 'bones')))
        _, ra, rn = t.item(t.u32(sat + t.offset(S, 'referencePose')))
        _, ma, mn = t.item(t.u32(rag + t.offset('hknpRagdollData', 'boneToBodyMap')))
        _req(pn == bn == rn == mn, f'ragdoll skeleton: {pn} parents, {bn} bones, {rn} poses, {mn} body links')
        self.parents = list(struct.unpack_from(f'<{pn}h', self.buf, pa))
        self.bone_body = list(struct.unpack_from(f'<{mn}i', self.buf, ma))
        qs = t.offset('hkQsTransform', 'scale') + 16     # translation, rotation, scale: one hkVector4 each (no size entry)
        self.ref = [ra + k * qs for k in range(rn)]

    def _v3(self, at: int) -> Vec3:
        return struct.unpack_from('<3f', self.buf, at)  # type: ignore[return-value]

    def _put(self, at: int, v: Vec3) -> None:
        struct.pack_into('<3f', self.buf, at, *v)

    def pivot(self, at: int) -> Vec3:
        """A joint transform's translation (after its three rotation columns)."""
        return self._v3(at + 48)

    def joint_world(self, j: Joint) -> tuple[Vec3, Vec3]:
        """The joint's point as the child and as the parent place it."""
        A, B = self.bodies[j.child].frame, self.bodies[j.parent].frame
        return add(A.t, rot(A.q, self.pivot(j.a))), add(B.t, rot(B.q, self.pivot(j.b)))

    def ref_t(self, k: int) -> Vec3:
        return self._v3(self.ref[k])

    def bytes(self) -> bytes:
        return bytes(self.buf)


# ------------------------------------------------------------------------------------------ the binding blob

@dataclass
class Link:
    src: str         # the frame it starts from (a bone for ragdoll_from_animation, a proxy for animation_from_ragdoll)
    dst: str
    off: Frame
    entry: list      # the blob's row (written back in place)


def _f(x: object) -> float:
    return x.value if isinstance(x, sgo.Float) else float(x)  # type: ignore[arg-type,union-attr]


def links(inner: dict, key: str) -> list[Link]:
    out = []
    for e in inner[key]:
        t, q = e[1], e[2]
        out.append(Link(str(e[0][0]), str(e[0][1]), Frame((_f(t[0]), _f(t[1]), _f(t[2])),
                                                        (_f(q[0]), _f(q[1]), _f(q[2]), _f(q[3]))), e))
    return out


def bone_frames(md: Mdb) -> dict[str, tuple[Vec3, list[Vec3]]]:
    """Every bone's bind (translation, rotation rows), model space."""
    w = bind_world(md)
    return {md.name_of(b.name): ((w[b.index][12], w[b.index][13], w[b.index][14]),
                                 [tuple(w[b.index][4 * r:4 * r + 3]) for r in range(3)]) for b in md.bones}  # type: ignore[misc]


# ------------------------------------------------------------------------------------------ check / fit

def problems(md: Mdb, shkt: bytes, blob: bytes) -> list[str]:
    """Every way the physics skeleton disagrees with `md`'s bind pose: a bone animation_from_ragdoll draws from a proxy
    not where (or not turned as) the bone is; a proxy ragdoll_from_animation poses from a bone not where that puts it;
    a joint the child and the parent place apart; a reference pose that is not the proxies' relative placement."""
    s = Shkt(shkt)
    _v, inner = sgo.read(blob)
    bones = bone_frames(md)
    out: list[str] = []
    for ln in links(inner, 'animation_from_ragdoll'):
        if ln.src not in s.index or ln.dst not in bones:
            out.append(f'{ln.src} -> {ln.dst}: unknown proxy or bone')
            continue
        f = s.bodies[s.index[ln.src]].frame * ln.off
        t, r = bones[ln.dst]
        if dist(f.t, t) > TOL or rows_off(axes(f.q), r) > TOL:
            out.append(f'bone {ln.dst} is at {fmt(t)}, its proxy {ln.src} draws it at {fmt(f.t)}')
    for ln in links(inner, 'ragdoll_from_animation'):
        if ln.dst not in s.index or ln.src not in bones:
            out.append(f'{ln.src} -> {ln.dst}: unknown bone or proxy')
            continue
        t, _r = bones[ln.src]
        want = add(t, rot(_bone_q(bones[ln.src][1]), ln.off.t))
        have = s.bodies[s.index[ln.dst]].frame.t
        if dist(want, have) > TOL:
            out.append(f'proxy {ln.dst} is at {fmt(have)}, its bone {ln.src} poses it at {fmt(want)}')
    for j in s.joints:
        a, b = s.joint_world(j)
        if dist(a, b) > TOL:
            out.append(f'joint {s.bodies[j.child].name} / {s.bodies[j.parent].name}: the child holds it at {fmt(a)}, '
                       f'the parent at {fmt(b)}')
    for k, p in enumerate(s.parents):
        me = s.bodies[s.bone_body[k]].frame
        rel = me.t if p < 0 else rot(conj(s.bodies[s.bone_body[p]].frame.q), sub(me.t, s.bodies[s.bone_body[p]].frame.t))
        if dist(rel, s.ref_t(k)) > TOL:
            out.append(f'reference pose of {s.bodies[s.bone_body[k]].name}: {fmt(s.ref_t(k))}, its body {fmt(rel)}')
    return out


def _bone_q(r: list[Vec3]) -> Quat:
    """The quaternion of rotation rows (x, y, z axes in the parent)."""
    m = [[r[c][k] for c in range(3)] for k in range(3)]     # columns = axes
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    i = max(range(3), key=lambda k: m[k][k])
    j, k = (i + 1) % 3, (i + 2) % 3
    s = math.sqrt(1.0 + m[i][i] - m[j][j] - m[k][k]) * 2
    q = [0.0, 0.0, 0.0, (m[k][j] - m[j][k]) / s]
    q[i], q[j], q[k] = 0.25 * s, (m[j][i] + m[i][j]) / s, (m[k][i] + m[i][k]) / s
    return (q[0], q[1], q[2], q[3])


def fmt(v: Vec3) -> str:
    return '(' + ', '.join(f'{x:.3f}' for x in v) + ')'


def fit(md: Mdb, shkt: bytes, blob: bytes, stock: Mdb) -> tuple[bytes, bytes]:
    """The ragdoll and the binding blob refitted to `md` (`stock`: the model the ragdoll was made for, whose bind pose
    it must agree with: problems(stock, ...) empty). Each proxy goes where ragdoll_from_animation's bone now poses it,
    turned as it was (a moved bone must keep its rotation); each joint stays on its child (transformA kept, transformB
    moved); the reference pose follows the bodies; animation_from_ragdoll's offsets are recomputed from the proxies
    and bones (translation; their rotations must still hold). Raises RagdollFitError when the result still has
    problems."""
    bad = problems(stock, shkt, blob)
    _req(not bad, 'the stock ragdoll does not match its own model: ' + '; '.join(bad))
    s = Shkt(shkt)
    version, inner = sgo.read(blob)
    bones = bone_frames(md)
    old = {b.name: b.frame for b in s.bodies}
    moved: dict[str, Frame] = {}
    for ln in links(inner, 'ragdoll_from_animation'):
        if ln.dst in moved:
            continue
        t, r = bones[ln.src]
        f = Frame(t, _bone_q(r)) * ln.off
        _req(rows_off(axes(f.q), axes(old[ln.dst].q)) < TOL, f'bone {ln.src} turned: its proxy {ln.dst} would turn')
        moved[ln.dst] = Frame(f.t, old[ln.dst].q)
    o_pos = s.tag.offset('hknpPhysicsSystemData::bodyCinfoWithAttachment', 'position')
    for b in s.bodies:
        if b.name in moved and dist(moved[b.name].t, b.frame.t) > KEEP:
            b.frame = moved[b.name]
            s._put(b.at + o_pos, b.frame.t)
    for j in s.joints:      # the joint stays on its child: where transformA puts it, the parent's transformB follows
        A, B = s.bodies[j.child].frame, s.bodies[j.parent].frame
        want = rot(conj(B.q), sub(add(A.t, rot(A.q, s.pivot(j.a))), B.t))
        if dist(want, s.pivot(j.b)) > KEEP:
            s._put(j.b + 48, want)
    for k, p in enumerate(s.parents):
        me = s.bodies[s.bone_body[k]].frame
        par = s.bodies[s.bone_body[p]].frame if p >= 0 else Frame((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))
        want = rot(conj(par.q), sub(me.t, par.t))
        if dist(want, s.ref_t(k)) > KEEP:
            s._put(s.ref[k], want)
    for ln in links(inner, 'animation_from_ragdoll'):
        P = s.bodies[s.index[ln.src]].frame
        t, _r = bones[ln.dst]
        want = rot(conj(P.q), sub(t, P.t))
        if dist(want, ln.off.t) > KEEP:
            ln.entry[1] = [want[0], want[1], want[2]]
    out = (s.bytes(), sgo.write_depth_first(version, inner))   # the stock blobs' layout (round-trips byte for byte)
    bad = problems(md, *out)
    _req(not bad, 'refitted ragdoll: ' + '; '.join(bad))
    return out
