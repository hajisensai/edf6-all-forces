"""The jets' hardpoints: where their guns' and stores' rounds leave the airframe. Pure Python (pylib: mdb, mdb_jet,
graft_pure, jet_gear; no numpy, no game).

    hardpoints(md, muzzles, skids) -> list[Hardpoint]   the bones' places, measured on the model
    with_hardpoints(md, hps) -> Mdb                      the model with those bones
    strip_hardpoints(md) -> Mdb                          with_hardpoints undone
    check_hardpoints(md, want, skids)                    raises HardpointError on any failure

Why (the user, 2026-10-19: 「在地面停着的飞机射导弹，会在地底生成」): every jet hung its weapons on its root bone `mdl`
(pylib/vcobjects.py jet_sgo: vehicle_weapon_setting), the model's origin. A grounded jet model (pylib/jet_models.py
grounded) has that origin at its wheels' contact, on the ground when it is parked (the native chain that puts a
vehicle's model world on its origin: docs/reviews/2026-10-08-carrier-walkup.md). The weapons are the stock 506
helicopter's: their muzzles (each weapon SGO's animation_model[2], vcobjects.mab_muzzles) are offsets from the holder
bone measured for the 506's body, all under it: the gatlings' (+-2.5, -0.24, 2.53), the missile pods' (+-1.05 / +-1.76,
-0.67, 0.2), the bomb racks' (+-1.37 / +-1.96, -0.48, 1.1..1.5). Under the root bone that is 0.24-0.67 m under the
ground: a parked jet's missiles, rockets, bombs and gun rounds were made in the ground (and in the air 1-2 m under the
belly, nowhere near the airframe).

Now each jet model carries two bones of its own, HP_GUN and HP_STORE, children of the mesh bone (so they move with the
airframe as it is drawn), transform only (kind 0, unbounded: no mesh), turned as the model (identity rotation: the
muzzles' +z the nose, as on the root). vcobjects.jet_sgo hangs the guns on HP_GUN and every missile / rocket / bomb on
HP_STORE. Each bone is placed on the model (`place`):
  - its z: the nearest to its target (the stores: the main gear's z, under the wing roots, where real fighters' inner
    pylons are; the guns: the nose gear's z; without gear the airframe's middle) where the belly's centre line meets the
    airframe;
  - its y: as high as it can go with every muzzle of the weapons hung on it at or under the airframe's skin over it (a
    muzzle whose vertical misses the airframe, beside it, is held no higher than the belly's centre line there): the
    rounds leave from the airframe's underside, where its stores hang, not from inside it nor far under it.
  - every muzzle then stands at least GROUND_CLEAR over the ground plane the parked jet stands on (its origin's: the
    wheels' or skids' contact), else HardpointError: no layout leaves a muzzle in the ground.
The airframe is the model without its landing gear (jet_gear.GEAR_BONES: retracted in flight, beside the stores on the
ground) and without its skids (jet_skids: the drone's last mesh), so a muzzle is never put under a wheel or a rail.
"""
from __future__ import annotations

from dataclasses import dataclass, replace

import graft_pure as g
import jet_gear
import mdb_jet
from graft_pure import Vec3
from mdb import Mdb, bind_world, mdb_write

HP_GUN, HP_STORE = 'hp_gun', 'hp_store'
HARDPOINT_BONES = (HP_GUN, HP_STORE)
GROUND_CLEAR = 0.2      # m: every muzzle at least this over the ground plane the parked jet stands on
STEP = 0.05             # m: the z search step
SKIN_SLACK = 1e-3       # m: the check's tolerance on "at or under the skin"
Muzzle = tuple[float, float, float]   # an offset from its holder bone (x left, y up, z ahead)


class HardpointError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise HardpointError(msg)


@dataclass(frozen=True)
class Hardpoint:
    name: str                 # HP_GUN / HP_STORE
    at: Vec3                  # its bind origin in the model's frame
    muzzles: tuple[Muzzle, ...]   # the muzzles of every weapon hung on it (the ones placed against the airframe)


def airframe(md: Mdb, skids: bool) -> tuple[list[Vec3], list[tuple[int, int, int]]]:
    """The airframe's triangles (model space, bind pose): `md` without its gear's (jet_gear.GEAR_BONES) and, `skids`,
    without its skids' mesh (jet_skids: object 0's last)."""
    if skids:
        md = replace(md, objects=[replace(md.objects[0], meshes=md.objects[0].meshes[:-1]), *md.objects[1:]])
    gear = {md.bone_index(n) for n in jet_gear.GEAR_BONES} - {-1}
    keep = set(range(len(md.bones))) - gear - {md.bone_index(n) for n in HARDPOINT_BONES}
    return jet_gear.model_tris(md, keep)


class _Lines:
    """Vertical lines through the airframe: the triangles each x meets, kept per x (the search asks the same x often)."""

    def __init__(self, P: list[Vec3], T: list[tuple[int, int, int]]) -> None:
        self.P, self.T = P, T
        self.by_x: dict[float, list[tuple[int, int, int]]] = {}

    def underside(self, x: float, z: float) -> float | None:
        tris = self.by_x.get(x)
        if tris is None:
            P = self.P
            tris = self.by_x[x] = [t for t in self.T if min(P[i][0] for i in t) - 1e-6 <= x <= max(P[i][0] for i in t) + 1e-6]
        return jet_gear.underside(self.P, tris, x, z)


def _height(lines: _Lines, muzzles: tuple[Muzzle, ...], z: float) -> float | None:
    """The bone's y at z (see the module): every muzzle at or under the skin over it, the lowest no lower than the belly's
    centre line; None where the centre line misses the airframe."""
    centre = lines.underside(0.0, z)
    if centre is None:
        return None
    caps = [centre - min(m[1] for m in muzzles)]
    for m in muzzles:
        u = lines.underside(m[0], z + m[2])
        if u is not None:
            caps.append(u - m[1])
    return min(caps)


def place(lines: _Lines, muzzles: tuple[Muzzle, ...], target: float, lo: float, hi: float) -> Vec3:
    """The bone of `muzzles`: the z nearest `target` within lo..hi (STEP apart) where it fits (its muzzles GROUND_CLEAR
    over the ground at least), and its y there."""
    n = int((hi - lo) / STEP) + 1
    zs = sorted((lo + i * STEP for i in range(n)), key=lambda z: (abs(z - target), z))
    for z in zs:
        y = _height(lines, muzzles, z)
        if y is not None and y + min(m[1] for m in muzzles) >= GROUND_CLEAR:
            return (0.0, y, z)
    raise HardpointError(f'no place along the belly ({lo:.2f}..{hi:.2f}) leaves the muzzles {muzzles} '
                         f'{GROUND_CLEAR} m over the ground')


def _gear_z(md: Mdb, names: tuple[str, ...]) -> float | None:
    w = bind_world(md)
    ks = [md.bone_index(n) for n in names]
    return sum(w[k][14] for k in ks) / len(ks) if all(k >= 0 for k in ks) else None


def targets(md: Mdb, P: list[Vec3]) -> dict[str, float]:
    """Each bone's wanted z: the stores under the main gear's legs, the guns over the nose gear's; without gear the
    airframe's middle."""
    mid = (min(p[2] for p in P) + max(p[2] for p in P)) / 2.0
    main = _gear_z(md, ('gear_main_l', 'gear_main_r'))
    nose = _gear_z(md, ('gear_nose',))
    return {HP_STORE: mid if main is None else main, HP_GUN: mid if nose is None else nose}


def hardpoints(md: Mdb, muzzles: dict[str, tuple[Muzzle, ...]], skids: bool = False) -> list[Hardpoint]:
    """HARDPOINT_BONES (those `muzzles` names, in that order) placed on `md` (see the module)."""
    P, T = airframe(md, skids)
    _req(bool(T), 'no airframe triangles')
    lines = _Lines(P, T)
    lo, hi = min(p[2] for p in P), max(p[2] for p in P)
    want = targets(md, [P[i] for t in T for i in t])
    out = []
    for name in HARDPOINT_BONES:
        ms = muzzles.get(name)
        if ms:
            out.append(Hardpoint(name, place(lines, tuple(ms), want[name], lo, hi), tuple(ms)))
    return out


def with_hardpoints(md: Mdb, hps: list[Hardpoint]) -> Mdb:
    """`md` with a bone for each of `hps` under its mesh bone (see the module), placed after that bone's subtree."""
    _req(not any(md.name_of(b.name) in HARDPOINT_BONES for b in md.bones), 'the model has hardpoint bones already')
    parent = next(b.index for b in md.bones if b.kind in (1, 3))   # jet_models.mesh_bone
    for hp in hps:
        bind = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, hp.at[0], hp.at[1], hp.at[2], 1.0]
        md, _at = jet_gear.insert_bones(md, parent, [(hp.name, bind)], kind=0,
                                        bounded=0, renumber_skin=True)
    return md


def strip_hardpoints(md: Mdb) -> Mdb:
    """`md` without its hardpoint bones (with_hardpoints undone: later bones, objects, parents and skinned vertices
    renumbered back; their names dropped from the name table's end, where with_hardpoints put them, when unused)."""
    gone = sorted(b.index for b in md.bones if md.name_of(b.name) in HARDPOINT_BONES)
    if not gone:
        return md
    _req(all(not any(c.parent == k for c in md.bones) for k in gone), 'a bone hangs on a hardpoint bone')

    def moved(i: int) -> int:
        return i - sum(1 for k in gone if k < i)
    bones = [replace(b, index=moved(b.index), parent=moved(b.parent) if b.parent >= 0 else -1)
             for b in md.bones if b.index not in gone]
    mdb_jet.link(bones)
    objects = [replace(o, bone=moved(o.bone), meshes=[jet_gear._renumber_skin(me, moved) if me.flags[1] else me
                                                      for me in o.meshes]) for o in md.objects]
    used = {b.name for b in bones} | {o.name for o in md.objects} | {m.name for m in md.materials}
    names = list(md.names)
    while names and (names[-1] or '') in HARDPOINT_BONES and len(names) - 1 not in used:
        names.pop()
    return replace(md, names=names, bones=bones, objects=objects, buffer_order=None)


def muzzle_points(md: Mdb, name: str, muzzles: tuple[Muzzle, ...]) -> list[Vec3]:
    """Where `muzzles` are on bone `name` of `md` (bind pose, model space)."""
    w = bind_world(md)[md.bone_index(name)]
    return [tuple(m[0] * w[c] + m[1] * w[4 + c] + m[2] * w[8 + c] + w[12 + c] for c in range(3)) for m in muzzles]  # type: ignore[misc]


def check_hardpoints(md: Mdb, want: list[Hardpoint], skids: bool = False) -> None:
    """`md` carries exactly the bones `want` (each transform only, unbounded, under the mesh bone, turned as the model,
    at its place to 1e-4) and they are where `hardpoints` puts them on the model without them; every muzzle at least
    GROUND_CLEAR over the ground plane (y 0) and at or under the airframe's skin over it; without them (strip_hardpoints)
    every skinned vertex and object on the same bones by name, and strip_hardpoints undoes with_hardpoints byte for byte."""
    named = [md.name_of(b.name) for b in md.bones if md.name_of(b.name) in HARDPOINT_BONES]
    _req(named == [h.name for h in want], f'hardpoint bones {named}, want {[h.name for h in want]}')
    w = bind_world(md)
    mesh = next(b.index for b in md.bones if b.kind in (1, 3))
    bare = strip_hardpoints(md)
    again = hardpoints(bare, {h.name: h.muzzles for h in want}, skids)
    _req([(h.name, h.muzzles) for h in again] == [(h.name, h.muzzles) for h in want], 'the hardpoints measure otherwise')
    P, T = airframe(bare, skids)
    lines = _Lines(P, T)
    for h, a in zip(want, again):
        k = md.bone_index(h.name)
        b = md.bones[k]
        _req((b.kind, b.bounded) == (0, 0), f'{h.name}: kind {b.kind}, bounded {b.bounded}')
        _req(b.parent == mesh, f'{h.name} hangs on bone {b.parent}, not the mesh bone {mesh}')
        _req(max(abs(x - y) for x, y in zip(w[k][:12], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0])) < 1e-5, f'{h.name} is turned')
        _req(max(abs(w[k][12 + c] - h.at[c]) for c in range(3)) < 1e-4, f'{h.name} at {w[k][12:15]}, want {h.at}')
        _req(max(abs(x - y) for x, y in zip(a.at, h.at)) < 1e-4, f'{h.name} at {h.at}, measured {a.at}')
        for m, p in zip(h.muzzles, muzzle_points(md, h.name, h.muzzles)):
            _req(p[1] >= GROUND_CLEAR - 1e-6, f'{h.name} muzzle {m} at {p}: under {GROUND_CLEAR} m over the ground')
            u = lines.underside(p[0], p[2])
            _req(u is None or p[1] <= u + SKIN_SLACK, f'{h.name} muzzle {m} at {p}: over the skin ({u})')
    names = lambda m: [tuple(m.name_of(m.bones[int(x)].name) for x in i4)  # noqa: E731
                       for o in m.objects for me in o.meshes if me.flags[1] for i4 in g.skin_columns(me)[0]]
    _req(names(bare) == names(md), 'a skinned vertex changed bones with the hardpoint bones')
    _req([(o.name, md.name_of(md.bones[o.bone].name)) for o in md.objects] ==
         [(o.name, bare.name_of(bare.bones[o.bone].name)) for o in bare.objects], 'an object changed bones')
    _req(mdb_write(strip_hardpoints(with_hardpoints(bare, want))) == mdb_write(bare),
         'strip_hardpoints does not undo with_hardpoints')
