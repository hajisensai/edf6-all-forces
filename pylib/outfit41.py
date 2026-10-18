"""EDF4.1's player soldiers as extra EDF6 outfits: one retargeting pipeline for every class (docs/edf41-outfits.md).

An EDF6 player model is played by its class's EDF6 CAS (EDF6ArmySoldier.cas, EDF6Engineer.cas, ...), whose clips store
each bone's local transform (pylib/cas_pose.py), so a 4.1 mesh cannot keep 4.1's skeleton: its bones point another way
(90-150 degrees at the same joints). Every class goes through the same steps, given only its Spec (below):

 1. Pose. The 4.1 mesh is skinned by 4.1's own bones (linear blend) into EDF6's bind pose: walking 4.1's hierarchy
    parent first, a bone keeps its parent's turn, and a paired bone turns further when one of its paired children sits
    more than POSE_REACH from its EDF6 joint, by the turn that points the children's offsets the way EDF6's do (Kabsch;
    one child, or children in a line: the shortest turn of their direction; under MIN_TURN: none). Where the two bind
    poses agree (Ranger / Air Raider: every shared joint within 2.6 cm but the outer fingers) the mesh stays where it
    is, and the Wing Diver's arms (EDF6's hang 10-18 cm lower) are turned down by the same code.
    Positions follow forward kinematics (4.1's bone lengths): the mesh is never stretched to EDF6's joints.
 2. Rebind. Each 4.1 bone that weights a vertex is paired with an EDF6 bone by name (case-insensitive), or by the
    Spec's exceptions (COMMON for every class: 4.1's otherFinger* -> middle*, 4.1's kubi, which carries the head, ->
    head); its vertices' BLENDINDICES then name that EDF6 bone and are skinned through EDF6's inverse bind.
 3. A 4.1 bone with no EDF6 counterpart (armour plates, skirts, boosters, the Air Raider's radar ...) is added as a
    transform bone where the pose put it, under its 4.1 parent's counterpart (mdb.insert_bone's place in file order):
    no EDF6 clip names it, so it follows that parent rigidly (docs: known losses). A skeleton that would grow past
    MAX_BONES (the Wing Diver's 34 armour / booster / skirt bones on EDF6's 99) folds them into that parent instead:
    the same picture, since nothing moves them.
 4. Every model of the 4.1 SGO's animation_model[0] (the body and its LOD1 / LOD2) through the same steps against the
    same skeleton; their materials, textures and vertex layout are 4.1's (pylib/mdb_legacy.py's conversion: the
    snd_Chara_* shaders EDF6's DX11.cpk still has with the same parameters, so change_color0/1 recolour them).

Output (build): under new names only, nothing stock replaced:
    OBJECT/<out>.SGO       the class's stock battle SGO with animation_model[0] = [our MRAB, body, LODs]
    MENUOBJECT/<out>.SGO   the class's stock menu SGO with animation_model[0] = [our MRAB, body]
    OBJECT/<out>.MRAB      4.1's textures (LOD and HD, as stored), the retargeted models
CAS, ragdoll (SHKT), MAB locators and every other key are the stock SGO's: they name EDF6's bones, all kept.

    build(spec, read41, read6) -> Result      read41 / read6: Mods-style path ('OBJECT/X.SGO') -> bytes
"""
from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from typing import Callable

import numpy as np

import dsgo
import mdb
import mdb_legacy
import sgo

MIN_TURN = 3.0          # degrees: a smaller bind-pose difference at a joint is not worth moving the mesh for
POSE_REACH = 0.03       # m: a bone turns only when one of its paired children sits farther than this from its EDF6
                        # joint (the 4.1 and EDF6 hands, finger roots and heads differ by 1-2.6 cm: left as they are)
PAIR_REACH = 0.25       # m: a same-named pair farther apart than this is not the same joint (refused, said)
MAX_BONES = 126         # the largest skeleton of any stock EDF6 model (e601_martian, e661_martian_leader; 738 models of
                        # Root.cpk's OBJECT/ archives, 2026-10-17): no model is known to load with more
LOD_DISTANCES = (15.0, 30.0)
# Exceptions every class shares: (4.1 bone name regex, the EDF6 bone it pairs with; re.sub's replacement)
COMMON: tuple[tuple[str, str], ...] = (
    (r'otherFinger(\d)_([lr])', r'middle\1_\2'),   # 4.1 has one chain for the three outer fingers; EDF6's middle one
    (r'kubi', r'head'),                             # 4.1's neck bone carries the head (EDF6 splits kubi / head)
)


@dataclass(frozen=True)
class Spec:
    key: str                 # 'ranger'
    soldier_class: int       # CONFIG.SGO SoldierInit class number (0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer)
    edf41_sgo: str           # 4.1's player SGO (its animation_model[0] names the models)
    base_sgo: str            # EDF6's battle SGO it is built on (class, CAS, ragdoll, MAB)
    base_menu: str           # EDF6's menu SGO
    out: str                 # the new files' base name
    exceptions: tuple[tuple[str, str], ...] = ()   # class-only pairs, tried before COMMON


SPECS: dict[str, Spec] = {s.key: s for s in (
    Spec('ranger', 0, 'OBJECT/ARMYSOLDIER.SGO', 'OBJECT/P505_RANGER.SGO', 'MENUOBJECT/P505_RANGER.SGO', 'EDF41_P_RANGER'),
    Spec('wingdiver', 1, 'OBJECT/PALEWING.SGO', 'OBJECT/P506_WINGDIVER.SGO', 'MENUOBJECT/P506_WINGDIVER.SGO',
         'EDF41_P_WINGDIVER'),
    Spec('airraider', 2, 'OBJECT/ENGINEER.SGO', 'OBJECT/P508_AIRRADER.SGO', 'MENUOBJECT/P508_AIRRADER.SGO',
         'EDF41_P_AIRRAIDER'),
    # backWeapon_l / _r are mirrored between the two rigs (4.1's _l is EDF6's _r); no vertex weights them, so the
    # pairing below never sees them: listed for whoever adds weights there
    Spec('fencer', 3, 'OBJECT/HEAVYARMOR.SGO', 'OBJECT/P507_FENCER.SGO', 'MENUOBJECT/P507_FENCER.SGO', 'EDF41_P_FENCER',
         ((r'backWeapon(Joint)?_l', r'backWeapon\1_r'), (r'backWeapon(Joint)?_r', r'backWeapon\1_l'))),
)}


def rel_of(app: str) -> str:
    """'app:/Object/x.mrab' -> 'OBJECT/X.MRAB'."""
    return app.split(':/', 1)[-1].replace('\\', '/').upper()


def app_path(rel: str) -> str:
    folder, name = rel.split('/', 1)
    return f'app:/{folder.capitalize() if folder != "MENUOBJECT" else "MenuObject"}/{name}'


def model_entry(am0: object) -> tuple[str, str, list[tuple[str, float]]]:
    """animation_model[0] as (archive, body, [(lod, distance)]): 4.1 writes a list of entries ([[RAB, MDB, LODs],
    [light RAB, ...]], the first is the one the game draws), EDF6 one entry."""
    e = am0[0] if isinstance(am0, list) and am0 and isinstance(am0[0], list) else am0
    if not (isinstance(e, list) and len(e) >= 2 and isinstance(e[0], str) and isinstance(e[1], str)):
        raise ValueError(f'animation_model[0] is not [archive, model, ...]: {am0!r}')
    lods = [(str(x[0]), float(sgo.plain(x[1]))) for x in e[2]] if len(e) > 2 and isinstance(e[2], list) else []
    return e[0], e[1], lods


def _member(rab: mdb.Rab, name: str) -> mdb.RabFile:
    for f in rab.files:
        if f.name.lower() == name.lower():
            return f
    raise ValueError(f'the archive has no member {name}')


# ------------------------------------------------------------------------------------------------- math

def m4(m: list[float]) -> np.ndarray:
    return np.array(m, dtype=np.float64).reshape(4, 4)


def turn_angle(q: np.ndarray) -> float:
    return math.degrees(math.acos(max(-1.0, min(1.0, (float(np.trace(q)) - 1.0) / 2.0))))


def shortest_turn(u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """The rotation (row vectors: u @ R ~ v) turning direction u onto v by the least angle."""
    a, b = u / np.linalg.norm(u), v / np.linalg.norm(v)
    c = float(a @ b)
    axis = np.cross(a, b)
    s = float(np.linalg.norm(axis))
    if s < 1e-9:
        if c > 0:
            return np.eye(3)
        p = np.cross(a, [1.0, 0.0, 0.0] if abs(a[0]) < 0.9 else [0.0, 1.0, 0.0])
        axis, s, c = p / np.linalg.norm(p), 0.0, -1.0
        k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
        return (np.eye(3) + 2 * k @ k).T
    k = axis / s
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    col = np.eye(3) + s * kx + (1 - c) * kx @ kx        # column-vector form: R v
    return col.T


def align(us: list[np.ndarray], vs: list[np.ndarray]) -> np.ndarray:
    """The rotation Q (row vectors) best turning every u onto its v: Kabsch; one vector, or vectors in a line (the
    forearm's twist bones and hand), the shortest turn of their summed direction."""
    u, v = np.array(us), np.array(vs)
    sv = np.linalg.svd(u, compute_uv=False)
    if len(us) == 1 or sv[1] < 0.08 * sv[0]:
        return shortest_turn(u.sum(axis=0), v.sum(axis=0))
    a, _s, bt = np.linalg.svd(u.T @ v)
    d = np.sign(np.linalg.det(a @ bt))
    return a @ np.diag([1.0, 1.0, d]) @ bt


# ---------------------------------------------------------------------------------------------- skeletons

def names(md: mdb.Mdb) -> list[str]:
    return [md.name_of(b.name) for b in md.bones]


def weighted(md: mdb.Mdb) -> set[int]:
    """Bones some vertex of `md` weights (weight > 0)."""
    out: set[int] = set()
    for ob in md.objects:
        for me in ob.meshes:
            bi, bw = mdb.read_elem(me, 'BLENDINDICES'), mdb.read_elem(me, 'BLENDWEIGHT')
            if not bi or not bw:
                raise ValueError('a mesh without blend weights (every 4.1 player mesh is skinned)')
            for i4, w4 in zip(bi, bw):
                out.update(int(i) for i, w in zip(i4, w4) if w > 0)
    return out


@dataclass
class Pairing:
    to6: dict[str, str]          # 4.1 bone -> EDF6 bone, for every weighted 4.1 bone that has one
    added: list[str]             # weighted 4.1 bones (and their unpaired 4.1 ancestors below a paired one), parent first
    parent_of: dict[str, str]    # added bone -> its parent in the new skeleton (an EDF6 bone or an added one)
    distance: dict[str, float]   # paired 4.1 bone -> bind distance to its EDF6 bone (m)
    folded: dict[str, str] = field(default_factory=dict)   # 4.1-only bone -> the EDF6 bone it folded into (MAX_BONES)


def pair(md41: mdb.Mdb, base: mdb.Mdb, spec: Spec, used: set[str]) -> Pairing:
    """Pairs every 4.1 bone in `used` (the names weighted by any of the class's models). ValueError when a weighted
    bone has no paired ancestor, or a pair is beyond PAIR_REACH."""
    n41 = names(md41)
    n6 = {n.lower(): n for n in names(base)}
    w41, w6 = mdb.bind_world(md41), mdb.bind_world(base)
    pos6 = {names(base)[i]: np.array(w6[i][12:15]) for i in range(len(base.bones))}
    to6: dict[str, str] = {}
    distance: dict[str, float] = {}
    for i, n in enumerate(n41):
        target = None
        for pat, rep in spec.exceptions + COMMON:
            if re.fullmatch(pat, n):
                target = re.sub(pat, rep, n)
                break
        if target is None and n.lower() in n6 and md41.bones[i].kind == 3:
            target = n6[n.lower()]
        if target is None:
            continue
        if target.lower() not in n6:
            raise ValueError(f'{spec.key}: {n} pairs with {target}, which {spec.base_sgo} lacks')
        target = n6[target.lower()]
        d = float(np.linalg.norm(np.array(w41[i][12:15]) - pos6[target]))
        if d > PAIR_REACH and n in used:
            raise ValueError(f'{spec.key}: {n} and {target} are {d:.2f} m apart in their bind poses: not one joint')
        to6[n] = target
        distance[n] = d
    added: list[str] = []
    parent_of: dict[str, str] = {}
    need: set[int] = set()
    for i, n in enumerate(n41):
        if n in used and n not in to6:
            j = i
            while j >= 0 and n41[j] not in to6:
                need.add(j)
                j = md41.bones[j].parent
            if j < 0:
                raise ValueError(f'{spec.key}: weighted 4.1 bone {n} has no paired ancestor')
    taken = {k.lower() for k in n6}
    for i in sorted(need):                   # 4.1 stores bones parent first
        n = n41[i]
        p = n41[md41.bones[i].parent]
        name = n if n.lower() not in taken else f'e41_{n}'
        taken.add(name.lower())
        added.append(name)
        parent_of[name] = to6.get(p) or next(a for a in added if a in (p, f'e41_{p}'))
    folded: dict[str, str] = {}
    if len(base.bones) + len(added) > MAX_BONES:
        # over the largest stock skeleton: every added bone folds into the EDF6 bone it hangs under (no clip moves an
        # added bone, so a vertex on it moves exactly as one on that bone would: the same picture, fewer bones)
        for name in added:
            up = parent_of[name]
            while up in parent_of:
                up = parent_of[up]
            src = name[4:] if name.startswith('e41_') and name[4:] in n41 else name
            to6[src] = folded[src] = up
        added, parent_of = [], {}
    return Pairing(to6, added, parent_of, distance, folded)


@dataclass
class Pose:
    rot: dict[str, np.ndarray]      # 4.1 bone -> the turn applied to it (3x3, row vectors)
    pos: dict[str, np.ndarray]      # 4.1 bone -> its joint after the pose
    joint_error: dict[str, float]   # paired skin bone -> distance from its EDF6 joint after the pose (m)
    turned: dict[str, float]        # paired bone -> the turn it took on top of its parent's (degrees)


def solve_pose(md41: mdb.Mdb, base: mdb.Mdb, pairing: Pairing) -> Pose:
    """Step 1 (module doc)."""
    n41 = names(md41)
    w41 = [m4(w) for w in mdb.bind_world(md41)]
    w6 = mdb.bind_world(base)
    pos6 = {names(base)[i]: np.array(w6[i][12:15]) for i in range(len(base.bones))}
    kids: dict[int, list[int]] = {}
    for b in md41.bones:
        kids.setdefault(b.parent, []).append(b.index)
    rot: dict[str, np.ndarray] = {}
    pos: dict[str, np.ndarray] = {}
    turned: dict[str, float] = {}
    for b in md41.bones:
        p41 = w41[b.index][3, :3]
        if b.parent < 0:
            r, at = np.eye(3), p41.copy()
        else:
            pn = n41[b.parent]
            r = rot[pn]
            at = pos[pn] + (p41 - w41[b.parent][3, :3]) @ r
        n = n41[b.index]
        if n in pairing.to6 and n not in pairing.folded:
            us, vs, off = [], [], 0.0
            for c in kids.get(b.index, []):
                cn = n41[c]
                if md41.bones[c].kind == 3 and cn in pairing.to6 and cn not in pairing.folded:
                    u = (w41[c][3, :3] - p41) @ r
                    us.append(u)
                    vs.append(pos6[pairing.to6[cn]] - pos6[pairing.to6[n]])
                    off = max(off, float(np.linalg.norm(at + u - pos6[pairing.to6[cn]])))
            if us and off >= POSE_REACH:
                q = align(us, vs)
                if turn_angle(q) >= MIN_TURN:
                    r = r @ q
                    turned[n] = round(turn_angle(q), 1)
        rot[n], pos[n] = r, at
    err = {n: float(np.linalg.norm(pos[n] - pos6[t])) for n, t in pairing.to6.items()
           if md41.bones[n41.index(n)].kind == 3 and n not in pairing.folded}
    return Pose(rot, pos, err, turned)


def skeleton(base: mdb.Mdb, md41: mdb.Mdb, pairing: Pairing, pose: Pose) -> mdb.Mdb:
    """EDF6's bones with Pairing.added inserted (step 3): a model with no objects, materials or textures yet."""
    md = mdb.Mdb(base.version, [base.name_of(b.name) for b in base.bones],
                 [mdb.Bone(b.index, b.parent, b.sibling, b.child, b.index, b.child_count, b.kind, b.depth_delta,
                           b.bounded, b.pad, b.unk1c, list(b.local), list(b.inv_bind), list(b.half), list(b.centre))
                  for b in base.bones], [], [], [])
    n41 = names(md41)
    w41 = [m4(w) for w in mdb.bind_world(md41)]
    for name in pairing.added:
        src = name[4:] if name.startswith('e41_') and name[4:] in n41 else name
        i = n41.index(src)
        at = mdb.insert_bone(md, name, pairing.parent_of[name])
        world = np.eye(4)
        world[:3, :3] = _orthonormal(w41[i][:3, :3]) @ pose.rot[src]
        world[3, :3] = pose.pos[src]
        parent_world = m4(mdb.bind_world(md)[md.bones[at].parent])
        bone = md.bones[at]
        bone.local = [float(x) for x in (world @ np.linalg.inv(parent_world)).reshape(16)]
        bone.inv_bind = mdb.inverse_affine([float(x) for x in world.reshape(16)])
        bone.kind = 3
    # names: one entry per bone, in bone order (as stock 0x20 files lay them out)
    md.names = [md.name_of(b.name) for b in md.bones]
    for i, b in enumerate(md.bones):
        b.name = i
    return md


def _orthonormal(r: np.ndarray) -> np.ndarray:
    u, _s, vt = np.linalg.svd(r)
    return u @ vt


# --------------------------------------------------------------------------------------------------- meshes

_FMT = {1: '<4f', 4: '<3f', 7: '<4e', 12: '<2f', 21: '<4B'}
_DIRECTIONS = ('normal', 'binormal', 'tangent')


def _elems(me: mdb.Mesh) -> dict[str, mdb.VElem]:
    return {e.name.lower(): e for e in me.elems if e.channel == 0}


def retarget_mesh(me: mdb.Mesh, md41: mdb.Mdb, n41: list[str], deform: list[tuple[np.ndarray, np.ndarray]],
                  index6: dict[str, int]) -> mdb.Mesh:
    """`me` (a 4.1 mesh after mdb_legacy's conversion) posed (deform[4.1 bone] = (turn, offset): v' = v @ turn +
    offset) and rebound to the new skeleton's indices (index6: 4.1 bone name -> new index); weights onto one bone
    merged, heaviest first, unused slots index 0 weight 0 (as every stock file)."""
    el = _elems(me)
    v = bytearray(me.vdata)
    bi_e, bw_e = el['blendindices'], el['blendweight']
    if bi_e.fmt != 21 or bw_e.fmt != 1:
        raise ValueError('blend elements not ubyte4 / float4')
    n = me.nverts
    raw = np.frombuffer(me.vdata, dtype=np.uint8).reshape(n, me.vsize)
    bi = raw[:, bi_e.offset:bi_e.offset + 4].astype(np.int64)
    bw = np.frombuffer(raw[:, bw_e.offset:bw_e.offset + 16].tobytes(), dtype='<f4').reshape(n, 4).astype(np.float64)
    turn = np.stack([d[0] for d in deform])          # (nb, 3, 3)
    off = np.stack([d[1] for d in deform])           # (nb, 3)

    def read(e: mdb.VElem) -> np.ndarray:
        dt = {1: '<f4', 4: '<f4', 7: '<f2', 12: '<f4'}[e.fmt]
        size = {1: 16, 4: 12, 7: 8, 12: 8}[e.fmt]
        k = size // np.dtype(dt).itemsize
        return np.frombuffer(raw[:, e.offset:e.offset + size].tobytes(), dtype=dt).reshape(n, k).astype(np.float64)

    def write(e: mdb.VElem, val: np.ndarray) -> None:
        dt = {1: '<f4', 4: '<f4', 7: '<f2', 12: '<f4'}[e.fmt]
        b = val.astype(dt).tobytes()
        size = len(b) // n
        for k in range(n):
            o = k * me.vsize + e.offset
            v[o:o + size] = b[k * size:(k + 1) * size]

    w = bw / np.where(bw.sum(axis=1, keepdims=True) > 0, bw.sum(axis=1, keepdims=True), 1.0)
    rot_v = np.einsum('nk,nkij->nij', w, turn[bi])                  # blended turn per vertex
    off_v = np.einsum('nk,nkj->nj', w, off[bi])
    pe = el['position']
    p = read(pe)
    p[:, :3] = np.einsum('ni,nij->nj', p[:, :3], rot_v) + off_v
    write(pe, p)
    for name in _DIRECTIONS:
        e = el.get(name)
        if e is None:
            continue
        d = read(e)
        t = np.einsum('ni,nij->nj', d[:, :3], rot_v)
        ln = np.linalg.norm(t, axis=1, keepdims=True)
        d[:, :3] = t / np.where(ln > 0, ln, 1.0)
        write(e, d)
    out_i = np.zeros((n, 4), dtype=np.uint8)
    out_w = np.zeros((n, 4), dtype=np.float32)
    most = 1
    for k in range(n):
        acc: dict[int, float] = {}
        for s in range(4):
            if bw[k, s] > 0:
                j = index6[n41[bi[k, s]]]
                acc[j] = acc.get(j, 0.0) + float(bw[k, s])
        items = sorted(acc.items(), key=lambda x: (-x[1], x[0]))
        most = max(most, len(items))
        for s, (j, x) in enumerate(items):
            out_i[k, s], out_w[k, s] = j, x
    for k in range(n):
        o = k * me.vsize
        v[o + bi_e.offset:o + bi_e.offset + 4] = out_i[k].tobytes()
        v[o + bw_e.offset:o + bw_e.offset + 16] = out_w[k].tobytes()
    flags = bytes((me.flags[0], 1, most, me.flags[3]))
    return mdb.Mesh(flags, me.material, me.unk08, me.vsize, [mdb.VElem(e.fmt, e.offset, e.channel, e.name) for e in me.elems],
                    me.mesh_index, bytes(v), me.indices)


def bounds(md: mdb.Mdb) -> None:
    """Every bone's bounds as stock files have them (checked: every skin bone of p501_proto_ranger, p505_ranger (74) and p506_wingDiver (90) within 0.5 mm, the bounded flag alike): the box of
    the vertices it weights, in its bind frame (half extents, centre); an unweighted bone is unbounded."""
    pts: dict[int, list[np.ndarray]] = {}
    for ob in md.objects:
        for me in ob.meshes:
            p = np.array(mdb.read_elem(me, _elems(me)['position'].name), dtype=np.float64)[:, :3]
            bi = np.array(mdb.read_elem(me, _elems(me)['blendindices'].name))
            bw = np.array(mdb.read_elem(me, _elems(me)['blendweight'].name))
            for s in range(4):
                for j in np.unique(bi[bw[:, s] > 0, s]):
                    pts.setdefault(int(j), []).append(p[(bi[:, s] == j) & (bw[:, s] > 0)])
    for b in md.bones:
        if b.kind != 3:
            continue
        if b.index not in pts:
            b.bounded, b.half, b.centre = 0, [0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0, 1.0]
            continue
        q = np.vstack(pts[b.index])
        loc = np.hstack([q, np.ones((len(q), 1))]) @ m4(b.inv_bind)
        lo, hi = loc[:, :3].min(axis=0), loc[:, :3].max(axis=0)
        b.bounded = 1
        b.half = [float(x) for x in (hi - lo) / 2] + [1.0]
        b.centre = [float(x) for x in (hi + lo) / 2] + [1.0]


def build_model(skel: mdb.Mdb, md41: mdb.Mdb, base: mdb.Mdb, pairing: Pairing, pose: Pose) -> mdb.Mdb:
    """One 4.1 model (body or LOD) on the new skeleton: steps 1, 2 and the bounds."""
    n41 = names(md41)
    w41 = [m4(w) for w in mdb.bind_world(md41)]
    deform = []
    for i, n in enumerate(n41):
        r = pose.rot.get(n)
        if r is None:
            raise ValueError(f'4.1 model bone {n} is not in the body\'s skeleton')
        p41 = w41[i][3, :3]
        deform.append((r, pose.pos[n] - p41 @ r))
    sk = names(skel)
    index6 = {n: sk.index(t) for n, t in pairing.to6.items()}
    index6.update({(a[4:] if a.startswith('e41_') else a): sk.index(a) for a in pairing.added})
    bones = [mdb.Bone(b.index, b.parent, b.sibling, b.child, b.name, b.child_count, b.kind, b.depth_delta, b.bounded,
                      b.pad, b.unk1c, list(b.local), list(b.inv_bind), list(b.half), list(b.centre)) for b in skel.bones]
    obj_bone = base.objects[0].bone
    nb = len(bones)
    mats = [mdb.Material(k, m.b2, m.b3, nb + k, m.shader, [mdb.MatParam(list(p.value), p.unk, p.name, p.type) for p in m.params],
                         [mdb.MatTex(t.texture, t.kind, t.unk) for t in m.textures], m.unk) for k, m in enumerate(md41.materials)]
    nm = [md41.name_of(m.name) for m in md41.materials]
    meshes = [retarget_mesh(me, md41, n41, deform, index6) for ob in md41.objects for me in ob.meshes]
    out = mdb.Mdb(mdb_legacy.EDF6_VERSION, sk + nm + [sk[obj_bone]], bones,
                  [mdb.Object(nb + len(nm), obj_bone, meshes)], mats,
                  [mdb.Texture(t.index, t.name, t.filename, t.unk) for t in md41.textures])
    bounds(out)
    return out


# --------------------------------------------------------------------------------------------------- files

@dataclass
class Result:
    files: dict[str, bytes]                   # Mods path -> bytes
    report: dict = field(default_factory=dict)


def _texture_members(rab41: mdb.Rab, models: list[mdb.Mdb]) -> tuple[list[mdb.RabFile], list[mdb.RabFile]]:
    """The TEXTURE (stem.lod.ext or the file name) and HD-TEXTURE members the models' textures name, in 4.1's order."""
    want = {t.filename.lower() for md in models for t in md.textures}
    stems = {w.rsplit('.', 1)[0] + '.lod.' + w.rsplit('.', 1)[1] for w in want} | want
    tex = rab41.folders.index('TEXTURE')
    hd = rab41.folders.index('HD-TEXTURE') if 'HD-TEXTURE' in rab41.folders else -1
    lo = [f for f in rab41.files if f.folder == tex and f.name.lower() in stems]
    high = [f for f in rab41.files if f.folder == hd and f.name.lower() in want]
    have = {f.name.lower() for f in lo}
    missing = sorted(w for w in want if w not in have and w.rsplit('.', 1)[0] + '.lod.' + w.rsplit('.', 1)[1] not in have)
    if missing:
        raise ValueError(f'4.1 archive lacks the textures {missing}')
    return lo, high


def _dsgo_entry(archive: str, body: str, lods: list[str]) -> dsgo.Node:
    items: list[dsgo.Value] = [archive, body]
    if lods:
        items.append(dsgo.Node([dsgo.Node([n, d]) for n, d in zip(lods, LOD_DISTANCES)]))
    return dsgo.Node(items)


def battle_sgo(stock: bytes, archive: str, body: str, lods: list[str]) -> bytes:
    """The stock battle SGO (a DSGO) with only animation_model[0] replaced."""
    doc = dsgo.parse(stock)
    am = doc.root.get('animation_model')
    if not isinstance(am, dsgo.Node) or not am.items:
        raise ValueError('battle SGO without animation_model')
    am.items[0] = _dsgo_entry(archive, body, lods)
    return dsgo.compact(doc)


def menu_sgo(stock: bytes, archive: str, body: str) -> bytes:
    """The stock menu SGO (classic SGO) with only animation_model[0] = [archive, body] (menu SGOs list no LODs)."""
    version, members = sgo.read(stock)
    members['animation_model'][0] = [archive, body]
    return sgo.write_depth_first(version, members)


def build(spec: Spec, read41: Callable[[str], bytes], read6: Callable[[str], bytes]) -> Result:
    """The class's three files (module doc) and a report of the pairing and the pose."""
    sgo41 = sgo.load(data=read41(spec.edf41_sgo))
    a41, body41, lods41 = model_entry(sgo41['animation_model'][0])
    rab41 = mdb.rab_read(read41(rel_of(a41)))
    raw41 = [_member(rab41, body41).data] + [_member(rab41, n).data for n, _d in lods41]
    models41 = [mdb.mdb_read(mdb_legacy.mdb_from_legacy(b)) for b in raw41]
    stock = read6(spec.base_sgo)
    a6, body6, _l6 = model_entry(dsgo.to_py(dsgo.parse(stock).root.get('animation_model'))[0])
    base = mdb.mdb_read(_member(mdb.rab_read(read6(rel_of(a6))), body6).data)
    if len(base.objects) != 1:
        raise ValueError(f'{spec.base_sgo}: {len(base.objects)} objects in the body (one expected)')
    main = models41[0]
    n_main = names(main)
    used = set()
    for md in models41:
        nm = names(md)
        if [x for x in nm if x not in n_main]:
            raise ValueError(f'{spec.key}: a LOD has bones the body lacks')
        used |= {nm[i] for i in weighted(md)}
    pairing = pair(main, base, spec, used)
    pose = solve_pose(main, base, pairing)
    skel = skeleton(base, main, pairing, pose)
    out_models = [build_model(skel, md, base, pairing, pose) for md in models41]
    stem = spec.out.lower()
    names_out = [f'{stem}.mdb'] + [f'{stem}-lod{k + 1}.mdb' for k in range(len(models41) - 1)]
    lo, high = _texture_members(rab41, models41)
    folders = ['TEXTURE', 'MODEL', 'HD-TEXTURE']
    members = [mdb.RabFile(f.name, 0, 0, f.stored, f.unk) for f in lo]
    members += [mdb.RabFile(n, 1, 0, mdb.cmpl_compress(mdb.mdb_write(md))) for n, md in zip(names_out, out_models)]
    members += [mdb.RabFile(f.name, 2, 1, f.stored, f.unk) for f in high]
    archive_rel = f'OBJECT/{spec.out}.MRAB'
    archive = mdb.rab_write(mdb.Rab(rab41.version, folders, members))
    app = f'app:/Object/{spec.out}.mrab'
    files = {
        f'OBJECT/{spec.out}.SGO': battle_sgo(stock, app, names_out[0], names_out[1:]),
        f'MENUOBJECT/{spec.out}.SGO': menu_sgo(read6(spec.base_menu), app, names_out[0]),
        archive_rel: archive,
    }
    report = {
        'class': spec.key, 'edf41': [rel_of(a41), body41] + [n for n, _ in lods41], 'base': spec.base_sgo,
        'pairs': {k: v for k, v in sorted(pairing.to6.items()) if k not in pairing.folded},
        'added': {a: pairing.parent_of[a] for a in pairing.added}, 'folded': dict(sorted(pairing.folded.items())),
        'bones': len(skel.bones), 'turned': pose.turned,
        'joint_error_max': max(pose.joint_error.values()),
        'joint_error_worst': sorted(((round(v, 3), k) for k, v in pose.joint_error.items()), reverse=True)[:6],
        'vertices': [sum(me.nverts for ob in md.objects for me in ob.meshes) for md in out_models],
    }
    return Result(files, report)


# ----------------------------------------------------------------------------------------------- checking

def check_archive(archive: bytes, models: list[str]) -> list[str]:
    """Faults of a built archive: a model that does not re-serialise byte for byte, an index past the skeleton,
    weights not summing to 1, a texture the loader would not bind (missing or after the model), a non-orthonormal
    inverse bind, a depth_delta out of rule. [] when sound."""
    faults: list[str] = []
    rab = mdb.rab_read(archive)
    if mdb.rab_write(rab) != archive:
        faults.append('archive does not re-serialise')
    if not mdb.folder_order_ok(rab):
        faults.append('members not grouped in folder order')
    for name in models:
        at = next((i for i, f in enumerate(rab.files) if f.name.lower() == name.lower()), None)
        if at is None:
            faults.append(f'no model {name}')
            continue
        raw = rab.files[at].data
        md = mdb.mdb_read(raw)
        if mdb.mdb_write(md) != raw:
            faults.append(f'{name}: does not re-serialise')
        before = {f.name.lower() for f in rab.files[:at] if f.folder == 0}
        for t in md.textures:
            stem, ext = t.filename.lower().rsplit('.', 1)
            if t.filename.lower() not in before and f'{stem}.lod.{ext}' not in before:
                faults.append(f'{name}: texture {t.filename} not before the model')
        d = mdb.depth_of(md)
        for i, b in enumerate(md.bones):
            if b.depth_delta != ((d[i + 1] - d[i]) if i + 1 < len(md.bones) else d[i]):
                faults.append(f'{name}: bone {md.name_of(b.name)} depth_delta')
        w = mdb.bind_world(md)
        for b in md.bones:
            if b.kind == 3 and max(abs(x - y) for x, y in zip(mdb.inverse_affine(w[b.index]), b.inv_bind)) > 2e-3:
                faults.append(f'{name}: bone {md.name_of(b.name)} inv_bind is not its bind inverse')
        for ob in md.objects:
            for me in ob.meshes:
                bi, bw = mdb.read_elem(me, _elems(me)['blendindices'].name), mdb.read_elem(me, _elems(me)['blendweight'].name)
                if any(i >= len(md.bones) for i4 in bi for i in i4):
                    faults.append(f'{name}: blend index past the skeleton')
                if any(abs(sum(w4) - 1.0) > 2e-3 for w4 in bw):
                    faults.append(f'{name}: weights not summing to 1')
    return faults


def skinned_positions(md: mdb.Mdb, locals_: dict[str, list[float]] | None = None) -> np.ndarray:
    """Every vertex skinned by the bones' worlds (the bind pose, or with `locals_` replacing bones' locals)."""
    world: list[np.ndarray] = []
    for b in md.bones:
        loc = m4((locals_ or {}).get(md.name_of(b.name), b.local))
        world.append(loc if b.parent < 0 else loc @ world[b.parent])
    skin = [m4(b.inv_bind) @ wd for b, wd in zip(md.bones, world)]
    out = []
    for ob in md.objects:
        for me in ob.meshes:
            el = _elems(me)
            p = np.array(mdb.read_elem(me, el['position'].name), dtype=np.float64)[:, :3]
            p4 = np.hstack([p, np.ones((len(p), 1))])
            bi = np.array(mdb.read_elem(me, el['blendindices'].name))
            bw = np.array(mdb.read_elem(me, el['blendweight'].name), dtype=np.float64)
            acc = np.zeros((len(p), 4))
            for s in range(4):
                acc += bw[:, s:s + 1] * np.einsum('ni,nij->nj', p4, np.stack([skin[i] for i in bi[:, s]]))
            out.append(acc[:, :3])
    return np.vstack(out)


def quat_matrix(q: tuple[float, float, float, float], t: tuple[float, float, float]) -> list[float]:
    """A CANM key (xyzw, translation) as a bone local (row vectors, row 3 the translation)."""
    x, y, z, w = q
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    return [r[0], r[1], r[2], 0.0, r[3], r[4], r[5], 0.0, r[6], r[7], r[8], 0.0, t[0], t[1], t[2], 1.0]


def clip_locals(cas: bytes, clip: str, frame: int, md: mdb.Mdb) -> dict[str, list[float]]:
    """The bone locals an EDF6 CAS clip sets at `frame` (a track's missing channel keeps the bind's)."""
    import cas_pose
    pose = cas_pose.CasPose(cas)
    c = next((x for x in pose.clips if x.name == clip), None)
    if c is None:
        raise ValueError(f'no clip {clip}')
    bind = {md.name_of(b.name): b.local for b in md.bones}
    out: dict[str, list[float]] = {}
    for t in c.tracks:
        if t.name not in bind:
            continue
        b = bind[t.name]
        q = cas_pose.rotation(pose, t.rotation, frame) if t.rotation >= 0 else None
        tr = pose.translation(t.translation, frame) if t.translation >= 0 else tuple(b[12:15])
        if q is None:
            m = list(b)
            m[12:15] = tr
            out[t.name] = m
        else:
            out[t.name] = quat_matrix(q, tr)
    return out


def stretch(md: mdb.Mdb, locals_: dict[str, list[float]]) -> float:
    """How far the posed mesh's edges stretch against the bind pose: the 99.9th percentile of |posed edge| / |bind
    edge| - 1 over every triangle edge (a seam torn or a part flung away shows as a large value)."""
    a = skinned_positions(md)
    b = skinned_positions(md, locals_)
    ratios = []
    base = 0
    for ob in md.objects:
        for me in ob.meshes:
            idx = np.frombuffer(me.indices, dtype='<u2').reshape(-1, 3).astype(np.int64) + base
            for i, j in ((0, 1), (1, 2), (2, 0)):
                la = np.linalg.norm(a[idx[:, i]] - a[idx[:, j]], axis=1)
                lb = np.linalg.norm(b[idx[:, i]] - b[idx[:, j]], axis=1)
                ok = la > 1e-4
                ratios.append(np.abs(lb[ok] / la[ok] - 1.0))
            base += me.nverts
    return float(np.percentile(np.concatenate(ratios), 99.9))
