"""X-Morph: Defense's walkers (pylib/xm_units.py families 'mech' and 'infantry') on an EDF6 host's own skeleton: the host
keeps its bones, CAS, ragdoll and class (its walk, its aim, its crew and its AI are the game's own), and its geometry is
the X-Morph unit's, each rigid piece on the host bone that moves like the X-Morph bone it hung on (docs/xmorph-port.md
「步行单位」).

    RIGS[key] -> Rig            which host, and which host bone carries each X-Morph bone (Seg)
    load(key) -> Spec           the unit's model folder (tools/prep_xmorph.py prepare_walker: the model pack's)
    place(spec, rig, host)      the X-Morph bones' maps into the host's bind pose (Placement)
    build(game, key)            (archive, model, info): the host's skeleton, the unit's pieces and textures
    gaps(game, key)             how far the pieces' joints come apart over the host's walking clips (the check of a Seg)

How a piece is placed (Seg): the whole model is first scaled by the rig's `scale` (X-Morph's walkers are not the
hosts' size: the host's legs walk its clips, so the X-Morph legs are made the host's length); then each X-Morph bone's
pieces are moved so that the segment's near joint (`at`, an X-Morph bone or attachment) lands on the host's (a host
bone's bind origin) and, where the segment names a far joint too (`to`), turned so the segment lies along the host's
and stretched along it to the host's length (across it the pieces keep the rig's scale): limbs then meet at the joints
the host bends them at, in every pose its clips put them in (gaps() measures it). A piece with no far joint (a foot, a
torso) is only moved. Every piece is rigid on its one host bone (weight 1), as X-Morph draws it.

The pieces are drawn in the V603 body material (tracked_combat_model BODY_MATERIAL: albedo, normal, parameter map), the
material every X-Morph unit is drawn in (pylib/wt_tracked.py), not the host's: the walkers' own (snd_BRDF_Common_Light,
the Ranger's colour-change shader) glow or recolour through maps the X-Morph textures do not have.
"""
from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass, field, replace

import numpy as np

import graft_pure as g
import obj_model as om
import tracked_combat_model as t
import wt_tracked as wt
import xm_units
from mdb import Mdb, Mesh, Rab, RabFile, bind_world, cmpl_compress, cmpl_decompress, insert_member, mdb_read, mdb_write, \
    rab_read, rab_write

FORMAT = 1           # tools/prep_xmorph.py WALKER_FORMAT
PLAIN_RMO = wt.PLAIN_RMO


class XmRigError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise XmRigError(message)


@dataclass(frozen=True)
class Seg:
    """Where an X-Morph bone's pieces go: on host bone `host`, X-Morph point `at[0]` onto host bone `at[1]`'s bind
    origin; with `to`, the segment at[0] -> to[0] laid along at[1] -> to[1] and stretched to its length. With `far`,
    the pieces are skinned along the segment from `host` (at its near end) to `far` (its far end), linearly: both
    joints then stay where the host's bones put them however the two turn apart (the Proteus' knee and heel, three
    bones between them). A point named '@ground:<name>' is <name>'s point on the ground under it (y 0): a foot whose
    ankle stands lower than the host's is stretched down to the ground from the host's ankle."""
    host: str
    at: tuple[str, str]
    to: tuple[str, str] | None = None
    far: str | None = None


@dataclass(frozen=True)
class Rig:
    """A walker's host: its SGO (Root.cpk OBJECT, the class, CAS, ragdoll and seats the unit keeps), the archive and
    model member its skeleton is read from, the X-Morph bones' Segs, and the scale (an X-Morph height onto a host
    bone's: scale = the host bone's bind height / the X-Morph point's height)."""
    sgo: str
    archive: str
    member: str
    scale: tuple[str, str]           # (X-Morph point, host bone): the heights that set the scale
    segs: dict[str, Seg]
    drop: tuple[str, ...] = ()       # X-Morph bones whose pieces are left out (what the host draws itself)
    clips: tuple[str, ...] = ()      # the host's clips gaps() poses
    # X-Morph bone -> radius (m, the model folder's frame): of its pieces only the loose bits (connected triangles)
    # whose middle is that near its joint are kept (trimmed(): the infantry's gun, one piece with its fist, is left
    # out for the weapon the EDF soldier holds)
    trim: dict[str, float] = field(default_factory=dict)


def _leg(xm: tuple[str, str, str], host: tuple[str, str, str], knee: str, sole: str,
         boot: bool = False) -> dict[str, Seg]:
    """A leg: the thigh rigid on the host's thigh, the shin from the host's knee bone `knee` to its ankle, the foot on
    the ankle reaching down to the ground (`sole`: the host bone the ground point is under). `boot`: the X-Morph foot
    joint is on the ground itself (the infantry's, 2 cm up), with nothing below it to stretch: the shin reaches down
    to the ground under the host's ankle (its lower end on the ankle bone) and the foot stands there unstretched.""" 
    thigh, shin, foot = xm
    hthigh, hknee, hankle = host
    return {thigh: Seg(hthigh, (thigh, hthigh), (shin, hknee)),
            shin: Seg(knee, (shin, hknee), (foot, f'@ground:{sole}' if boot else hankle), far=hankle),
            foot: (Seg(hankle, (f'@ground:{foot}', f'@ground:{sole}')) if boot else
                   Seg(hankle, (foot, hankle), (f'@ground:{foot}', f'@ground:{sole}')))}


# The X-Morph walkers on their hosts (measured 2026-10-30, docs/xmorph-port.md 「步行单位」):
#  - the light mech (4.5 m: legs, a torso, two gun arms held out sideways in the bind pose) on the Begaruta the Air Raider
#    calls (V504_BEGARUTA_BLUE: Vehicle504_begaruta, the EWEAPON356 request family): hip 2.25 m to its 5.28, x 2.35.
#    Its shins from the Begaruta's knee (sune0) to its ankle (ashi), over the short second joint (sune1) between them; its
#    arms' two segments (the shoulder held sideways, the gun arm) on the Begaruta's upper arm (hanging) and forearm (forward,
#    the hand's weapon locator at its end): the guns point where the Begaruta holds its guns.
#  - the heavy mech (11.4 m: bird legs, a turret with a gun pod either side) on the Proteus Mk2 (V614_PROTEUS_MK2_CALL,
#    VehicleBigBegaruta, the EWEAPON387 family): hip 5.73 m to its 8.35, x 1.46. Its shins from the Proteus' knee (sune)
#    to its heel, over the strut (leg) between them; its turret and gun on the neck, the pods on the gun mounts.
#  - the infantry (3.2 m powered suit, arms out in the bind pose as the Ranger's) on the Ranger (N601_COMMON_RANGER_AF:
#    AssultSoldier, EDF6ArmySoldier.cas): hip 1.91 m to its 0.89, x 0.47.
RIGS: dict[str, Rig] = {
    'xm_small_mech': Rig(
        'V504_BEGARUTA_BLUE', 'V504_BEGARUTA_BLUE.MRAB', 'v504_begaruta_blue.mdb', ('be_thigh_l', 'momo_l'),
        {'body': Seg('koshi', ('body', 'koshi')),
         'turret_1': Seg('body', ('turret_1', 'body')),
         **_leg(('be_thigh_l', 'be_shin_l', 'be_ankle_l'), ('momo_l', 'sune0_l', 'ashi_l'), 'sune0_l', 'ashi_l'),
         **_leg(('be_thigh_r', 'be_shin_r', 'be_ankle_r'), ('momo_r', 'sune0_r', 'ashi_r'), 'sune0_r', 'ashi_r'),
         'be_arm_l': Seg('jowan_l', ('be_arm_l', 'jowan_l'), ('be_elbow_l', 'kawan_l')),
         'be_elbow_l': Seg('kawan_l', ('be_elbow_l', 'kawan_l'), ('att_muzzle_1', 'weapon_hand_l')),
         'be_arm_r': Seg('jowan_r', ('be_arm_r', 'jowan_r'), ('be_elbow_r', 'kawan_r')),
         'be_elbow_r': Seg('kawan_r', ('be_elbow_r', 'kawan_r'), ('att_muzzle_2', 'weapon_hand_r'))},
        clips=('stand', 'walk_f', 'walk_b', 'run_f', 'turn_l', 'jump_start')),
    'xm_large_mech': Rig(
        'V614_PROTEUS_MK2_CALL', 'V614_PROTEUS_MK2.MRAB', 'v614_proteus_mk2.mdb', ('Be_L_Thigh', 'momo_l'),
        {'body': Seg('koshi', ('body', 'koshi')),
         'turret_1': Seg('neck', ('turret_1', 'neck')),
         'barrel_1': Seg('neck', ('barrel_1', 'neck')),
         'recoil_left': Seg('gun_mount_barrel_l', ('recoil_left', 'gun_mount_barrel_l')),
         'recoil_right': Seg('gun_mount_barrel_r', ('recoil_right', 'gun_mount_barrel_r')),
         **_leg(('Be_L_Thigh', 'Be_L_Shin', 'Be_L_Foot'), ('momo_l', 'sune_l', 'heel_l'), 'sune_l', 'heel_l'),
         **_leg(('Be_R_Thigh', 'Be_R_Shin', 'Be_R_Foot'), ('momo_r', 'sune_r', 'heel_r'), 'sune_r', 'heel_r')},
        clips=('stand', 'walk_f', 'walk_b', 'turn_l', 'jump_start')),
    'xm_small_infantry': Rig(
        'N601_COMMON_RANGER_AF', 'N601_COMMON_RANGER.MRAB', 'n601_common_ranger.mdb', ('Be_L_Thigh', 'momo_l'),
        {'body': Seg('koshi', ('body', 'koshi')),
         'turret_1': Seg('mune', ('turret_1', 'hara0')),
         **_leg(('Be_L_Thigh', 'Be_L_Shin', 'Be_L_Foot'), ('momo_l', 'sune_l', 'ashi_l'), 'sune_l', 'ashi_l',
              boot=True),
         **_leg(('Be_R_Thigh', 'Be_R_Shin', 'Be_R_Foot'), ('momo_r', 'sune_r', 'ashi_r'), 'sune_r', 'ashi_r',
              boot=True),
         'Be_L_Arm': Seg('jowan0_l', ('Be_L_Arm', 'jowan0_l'), ('Be_L_Elbow', 'kawan0_l')),
         'Be_L_Elbow': Seg('kawan0_l', ('Be_L_Elbow', 'kawan0_l'), ('Be_L_Hand', 'te_l')),
         'Be_L_Hand': Seg('te_l', ('Be_L_Hand', 'te_l')),
         'Be_R_Arm': Seg('jowan0_r', ('Be_R_Arm', 'jowan0_r'), ('Be_R_Elbow', 'kawan0_r')),
         'Be_R_Elbow': Seg('kawan0_r', ('Be_R_Elbow', 'kawan0_r'), ('Be_R_Hand', 'te_r')),
         'Be_R_Hand': Seg('te_r', ('Be_R_Hand', 'te_r'))},
        clips=('stand_assault', 'walk_f_assault', 'run_f_assault'), trim={'Be_L_Hand': 0.3}),
}


# ------------------------------------------------------------------------------------------ the model folder

@dataclass
class Spec:
    folder: str
    info: dict
    arrays: dict[str, np.ndarray]

    def part(self, k: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        a = self.arrays
        return a[f'p{k}_pos'], a[f'p{k}_nrm'], a[f'p{k}_uv'], a[f'p{k}_tri']

    def point(self, name: str) -> np.ndarray:
        """An X-Morph bone's bind point or an attachment's point (EDF frame, metres); '@ground:<name>' that point's
        foot on the ground (y 0)."""
        if name.startswith('@ground:'):
            p = self.point(name[len('@ground:'):])
            return np.array([p[0], 0.0, p[2]])
        for b in self.info['bones']:
            if b['name'] == name:
                return np.asarray(b['point'], dtype=np.float64)
        a = self.info['attachments'].get(name)
        _req(a is not None, f'{self.info["kind"]}: no bone or attachment {name}')
        return np.asarray(a['point'], dtype=np.float64)

    def bone_of(self, name: str) -> str:
        """The bone a bone or attachment rides: itself, or the attachment's parent ('scene_node': the root)."""
        if any(b['name'] == name for b in self.info['bones']):
            return name
        parent = self.info['attachments'][name]['bone']
        return self.info['bones'][0]['name'] if parent == 'scene_node' else parent


def folder(key: str) -> str | None:
    d = om.model_dir(xm_units.folder(key))
    ok = d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz'))
    return d if ok else None


_CACHE: dict[tuple, Spec] = {}


def load(key: str, d: str | None = None) -> Spec:
    """The unit's model folder (`d`, else the model pack's: obj_model.model_dir)."""
    d = d or folder(key)
    _req(d is not None, f'{key}: model folder {xm_units.folder(key)} not found ({", ".join(om.model_roots())})')
    stamp = tuple((f, os.stat(os.path.join(d, f)).st_mtime_ns) for f in ('model.json', 'model.npz'))  # type: ignore[arg-type]
    if (d, stamp) not in _CACHE:
        with open(os.path.join(d, 'model.json'), encoding='utf-8') as f:     # type: ignore[arg-type]
            info = json.load(f)
        _req(info.get('format') == FORMAT and info.get('kind') == key, f'{d}: not a format-{FORMAT} {key} walker')
        with np.load(os.path.join(d, 'model.npz'), allow_pickle=False) as z:  # type: ignore[arg-type]
            arrays = {k: z[k] for k in z.files}
        _CACHE.clear()
        _CACHE[(d, stamp)] = Spec(d, info, arrays)  # type: ignore[arg-type]
    return _CACHE[(d, stamp)]


def trimmed(spec: Spec, rig: Rig, k: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Part k's arrays (Spec.part), trimmed to Rig.trim's radius round its bone's joint: its triangles grouped into
    loose bits (sharing a vertex position), the bits whose vertices' middle is farther left out, the vertices no kept
    triangle uses dropped."""
    pos, nrm, uv, tris = spec.part(k)
    bone = spec.info['parts'][k]['bone']
    radius = rig.trim.get(bone)
    if radius is None:
        return pos, nrm, uv, tris
    weld = {}
    vid = np.array([weld.setdefault(tuple(np.round(p, 4)), len(weld)) for p in pos])
    parent = list(range(len(weld)))

    def root(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in tris:
        a, b, c = vid[t]
        parent[root(a)] = root(b)
        parent[root(b)] = root(c)
    bits: dict[int, list[int]] = {}
    for i, t in enumerate(tris):
        bits.setdefault(root(vid[t[0]]), []).append(i)
    joint = spec.point(bone)
    keep = sorted(i for ts in bits.values()
                  if np.linalg.norm(pos[np.unique(tris[ts])].mean(0) - joint) <= radius for i in ts)
    _req(bool(keep), f'{bone}: nothing within {radius} m of its joint')
    used = np.unique(tris[keep])
    remap = np.full(len(pos), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return pos[used], nrm[used], uv[used], remap[tris[keep]].astype(tris.dtype)


# ------------------------------------------------------------------------------------------ placing

def host(game, rig: Rig) -> tuple[Mdb, Rab]:  # noqa: ANN001 - rootcpk.Game
    rab = rab_read(game.read('OBJECT', rig.archive))
    return mdb_read(t.member(rab, rig.member).data), rab


def host_point(md: Mdb, bone: str) -> np.ndarray:
    """A host bone's bind origin (model space); '@ground:<bone>' its foot on the ground (y 0)."""
    if bone.startswith('@ground:'):
        p = host_point(md, bone[len('@ground:'):])
        return np.array([p[0], 0.0, p[2]])
    i = md.bone_index(bone)
    _req(i >= 0, f'the host has no bone {bone}')
    return np.asarray(bind_world(md)[i][12:15], dtype=np.float64)


def _turn(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """The least rotation taking direction a onto direction b (column vectors: R @ a ~ b)."""
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    v, c = np.cross(a, b), float(np.dot(a, b))
    if c < -1 + 1e-9:     # opposite: half a turn about any axis across a
        axis = np.cross(a, [1.0, 0.0, 0.0] if abs(a[0]) < 0.9 else [0.0, 1.0, 0.0])
        axis /= np.linalg.norm(axis)
        return 2 * np.outer(axis, axis) - np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1 + c)


@dataclass
class Placement:
    """Each X-Morph bone's map into the host's bind pose (p' = p @ A.T + b), its host bone and, for a blended Seg,
    its far bone and the segment's ends in the host's bind pose (the weights run along it)."""
    scale: float
    maps: dict[str, tuple[np.ndarray, np.ndarray]]
    bones: dict[str, str]
    blends: dict[str, tuple[str, np.ndarray, np.ndarray]]

    def apply(self, bone: str, p: np.ndarray) -> np.ndarray:
        A, b = self.maps[bone]
        return np.asarray(p, dtype=np.float64) @ A.T + b

    def normals(self, bone: str, n: np.ndarray) -> np.ndarray:
        A, _b = self.maps[bone]
        m = np.asarray(n, dtype=np.float64) @ np.linalg.inv(A)
        return m / np.maximum(np.linalg.norm(m, axis=1, keepdims=True), 1e-12)

    def far_weight(self, bone: str, placed: np.ndarray) -> np.ndarray:
        """Each placed point's weight on the far bone (0 at the segment's near end, 1 at its far end, clamped); zeros
        for a rigid Seg."""
        placed = np.atleast_2d(placed)
        if bone not in self.blends:
            return np.zeros(len(placed))
        _far, h0, h1 = self.blends[bone]
        d = h1 - h0
        return np.clip((placed - h0) @ d / float(d @ d), 0.0, 1.0)


def place(spec: Spec, rig: Rig, md: Mdb) -> Placement:
    s = host_point(md, rig.scale[1])[1] / spec.point(rig.scale[0])[1]
    _req(0.1 < s < 10, f'scale {s}')
    maps, bones, blends = {}, {}, {}
    for b in (x['name'] for x in spec.info['bones']):
        if b in rig.drop:
            continue
        seg = rig.segs.get(b)
        _req(seg is not None, f'{spec.info["kind"]}: X-Morph bone {b} has no host bone (xm_rig.RIGS)')
        x0, h0 = spec.point(seg.at[0]) * s, host_point(md, seg.at[1])
        A = s * np.eye(3)
        if seg.to is not None:
            x1, h1 = spec.point(seg.to[0]) * s, host_point(md, seg.to[1])
            ax, hx = x1 - x0, h1 - h0
            R = _turn(ax, hx)
            u = hx / np.linalg.norm(hx)
            k = float(np.linalg.norm(hx) / np.linalg.norm(ax))
            A = (np.eye(3) + (k - 1) * np.outer(u, u)) @ R @ A      # turn, then stretch along the host's segment
            if seg.far is not None:
                blends[b] = (seg.far, h0, h1)
        else:
            _req(seg.far is None, f'{b}: a blended Seg needs its far joint (to)')
        maps[b] = (A, h0 - A @ (x0 / s))
        bones[b] = seg.host
        for hb in (seg.host, seg.far):
            _req(hb is None or (md.bone_index(hb) >= 0 and md.bones[md.bone_index(hb)].kind == 3),
                 f'{hb}: not a skin bone of the host')
    return Placement(float(s), maps, bones, blends)


def points(spec: Spec, pl: Placement) -> dict[str, tuple[str, list[float]]]:
    """Every attachment (the muzzles among them) where the placed model has it: (host bone, bind-pose point); one on
    a blended piece rides whichever of its two bones carries more of it."""
    out = {}
    for name in spec.info['attachments']:
        b = spec.bone_of(name)
        if b in pl.maps:
            p = pl.apply(b, spec.point(name))
            host_bone = pl.blends[b][0] if pl.far_weight(b, p)[0] > 0.5 else pl.bones[b]
            out[name] = (host_bone, [float(c) for c in p])
    return out


# ------------------------------------------------------------------------------------------ the archive

def _material_textures(spec: Spec, m: dict) -> dict[str, tuple[str, bytes]]:
    import texfile
    stem = m['name'].lower()
    out = {'albedo': (f'{stem}_col.dds', wt._dds_of(os.path.join(spec.folder, m['albedo'])))}
    out['normal'] = ((f'{stem}_nor.dds', wt._normal_dds(os.path.join(spec.folder, m['normal']))) if m.get('normal')
                     else ('edf6vc_flat_nor.dds', texfile.solid_bc5()))
    out['param_r_m_occ_hr'] = (f'{stem}_rmo.dds', wt._param_dds(None, PLAIN_RMO))
    return out


def skeleton(md: Mdb) -> Mdb:
    """The host's bones as they are, its skinned object empty (no meshes, materials or textures); its other objects
    (the Begaruta's shields on their own bones) gone."""
    skinned = [o for o in md.objects if md.bones[o.bone].kind == 2]
    _req(len(skinned) == 1, 'the host has not exactly one skinned object')
    return replace(md, objects=[replace(skinned[0], meshes=[])], materials=[], textures=[], buffer_order=None)


def _rows(template: Mesh, pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray, near: int, far: int,
          w: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """wt_tracked.part_rows with each vertex skinned to `near` and `far` (weights 1 - w, w)."""
    rows, kept = wt.part_rows(template, pos, nrm, uv, tris, near)
    rows = rows.copy()
    n = len(pos)
    for e in template.elems:
        name = e.name.lower()
        if name == 'blendweight':
            v = np.stack((1.0 - w, w, np.zeros(n), np.zeros(n)), axis=1).astype('<f4')
        elif name == 'blendindices':
            v = np.stack((np.full(n, near), np.full(n, far), np.zeros(n), np.zeros(n)), axis=1).astype('u1')
        else:
            continue
        raw = np.ascontiguousarray(v).view(np.uint8).reshape(n, -1)
        rows[:, e.offset:e.offset + raw.shape[1]] = raw
    return rows, kept


def _pack(parts: list[tuple[Mesh, int, np.ndarray, np.ndarray, bool]]) -> list[Mesh]:
    """wt_tracked.pack, the blended parts apart (flags: up to four influences a vertex, as the stock characters')."""
    import model_lod
    by: dict[tuple[int, bool], list] = {}
    for template, material, rows, tris, blended in parts:
        flags = bytes((0, 1, 4 if blended else 1, 0))
        entry = by.setdefault((material, blended), [replace(template, flags=flags, material=material), [], [], 0])
        entry[1].append(rows)
        entry[2].append(tris + entry[3])
        entry[3] += len(rows)
    meshes: list[Mesh] = []
    for template, rows, tris, _n in by.values():
        meshes += model_lod.pack_rows(template, np.concatenate(rows), np.concatenate(tris))
    return meshes


def build(game, key: str, spec: Spec | None = None, model: str | None = None) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001
    """(archive, model, info): the archive holds the model as `model` (default: edf6vc_<key>.mdb) and its textures."""
    rig = RIGS[key]
    spec = spec or load(key)
    hmd, hrab = host(game, rig)
    pl = place(spec, rig, hmd)
    md = skeleton(hmd)
    v603, _v603_rab = t.stock(game)
    template = next(me for me in v603.objects[0].meshes
                    if v603.name_of(v603.materials[me.material].name) == t.BODY_MATERIAL)
    model = model or f'edf6vc_{key}.mdb'
    rab = Rab(hrab.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    mat_index, added = [], set()
    for m in spec.info['materials']:
        tex = _material_textures(spec, m)
        md, mi = om.add_material(md, v603, t.BODY_MATERIAL, m['name'], {k: fn for k, (fn, _d) in tex.items()})
        mat_index.append(mi)
        for fn, dds in tex.values():
            if fn not in added:
                om.add_texture(rab, fn, dds)
                added.add(fn)
    parts, all_pos = [], []
    for k, p in enumerate(spec.info['parts']):
        if p['bone'] in rig.drop:
            continue
        pos, nrm, uv, tris = trimmed(spec, rig, k)
        P = pl.apply(p['bone'], pos)
        N = pl.normals(p['bone'], nrm)
        near = md.bone_index(pl.bones[p['bone']])
        if p['bone'] in pl.blends:
            far = md.bone_index(pl.blends[p['bone']][0])
            rows, kept = _rows(template, P, N, uv, tris.astype(np.int64), near, far, pl.far_weight(p['bone'], P))
        else:
            rows, kept = wt.part_rows(template, P, N, uv, tris.astype(np.int64), near)
        parts.append((template, mat_index[p['material']], rows, kept, p['bone'] in pl.blends))
        all_pos.append(P)
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(_pack(parts))]
    md = g.recompute_bounds(replace(md, objects=[replace(md.objects[0], meshes=meshes)]))
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, 'MDB write/read/write differs')
    insert_member(rab, RabFile(model, rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    arc = rab_write(rab)
    P = np.vstack(all_pos)
    info = {'host': rig.sgo, 'scale': pl.scale, 'model': model, 'bounds': [P.min(0).tolist(), P.max(0).tolist()],
            'triangles': int(sum(len(x[3]) for x in parts)), 'meshes': len(meshes),
            'bones': {b: pl.bones[b] + (f'~{pl.blends[b][0]}' if b in pl.blends else '') for b in pl.bones},
            'points': points(spec, pl)}
    check(arc, model, hmd)
    return arc, md, info


def check(arc: bytes, model: str, hmd: Mdb) -> dict:
    """The archive reads back as written; the model is the host's skeleton unchanged, every vertex on one or two of its
    skin bones (weights summing to 1); every texture there in the stock layout (obj_model.texture_problems)."""
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'RAB write/read/write differs')
    md = mdb_read(t.member(rab, model).data)
    _req([(md.name_of(b.name), b.parent, b.kind, tuple(b.local), tuple(b.inv_bind)) for b in md.bones]
         == [(hmd.name_of(b.name), b.parent, b.kind, tuple(b.local), tuple(b.inv_bind)) for b in hmd.bones],
         'the skeleton is not the host\'s')
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'one skinned object')
    drawn = 0
    for me in md.objects[0].meshes:
        bi, bw = g.skin_columns(me)
        _req(all(abs(sum(w) - 1) < 1e-5 and all(abs(x) < 1e-6 for x in w[2:]) for w in bw), 'a vertex is not on '
             'one or two bones')
        _req(me.flags[2] == 4 or all(abs(w[1]) < 1e-6 for w in bw), 'a two-bone vertex in a one-influence mesh')
        _req(all(md.bones[int(i[k])].kind == 3 for i, w in zip(bi, bw) for k in range(2) if w[k] > 1e-6),
             'a vertex off the skin bones')
        _req(bool(np.isfinite(np.asarray(g.mesh_positions(me))).all()), 'nonfinite vertex')
        drawn += len(me.indices) // 6
    _req(drawn > 0, 'nothing drawn')
    bad = om.texture_problems(rab, md, model)
    _req(not bad, 'textures: ' + '; '.join(bad))
    return {'bones': len(md.bones), 'meshes': len(md.objects[0].meshes), 'triangles': drawn}


# ------------------------------------------------------------------------------------------ the pose check

def host_cas(game, rig: Rig) -> str:  # noqa: ANN001
    """The host SGO's CAS (animation_model[1]), the file name under OBJECT."""
    import dsgo
    import sgo as sgo_mod
    data = game.read('OBJECT', rig.sgo + '.SGO')
    if data[:4] == b'DSGO':
        am = dsgo.to_py(dsgo.parse(data).root)['animation_model']
    else:
        am = sgo_mod.plain(sgo_mod.read(data)[1]['animation_model'])
    return am[1].split('/')[-1].upper()


def gaps(game, key: str, spec: Spec | None = None, clips: tuple[str, ...] | None = None) -> dict:  # noqa: ANN001
    """Over the host's clips (every key): at each X-Morph joint (a bone's near point, Seg.at), how far apart the bone's
    pieces and its parent's carry it (m: 0 where the two meet however the host bends), and how low the placed model
    reaches (its lowest point's height at each key: the soles on the ground at 0).
    {'joints': {X-Morph bone: worst m}, 'floor': (min, max) m}."""
    import cas_pose
    rig = RIGS[key]
    spec = spec or load(key)
    hmd, _rab = host(game, rig)
    pl = place(spec, rig, hmd)
    pose = cas_pose.CasPose(game.read('OBJECT', host_cas(game, rig)))
    inv = [np.linalg.inv(np.asarray(m).reshape(4, 4)) for m in bind_world(hmd)]
    parent = {b['name']: b['parent'] for b in spec.info['bones']}
    joints = [(b, parent[b], host_point(hmd, rig.segs[b].at[1])) for b in pl.maps if parent[b] in pl.maps]
    pieces = [(b, pl.apply(b, trimmed(spec, rig, k)[0])) for k, p in enumerate(spec.info['parts'])
              if (b := p['bone']) in pl.maps]

    def carry(W: list, b: str, P: np.ndarray) -> np.ndarray:
        """Points of X-Morph bone b's pieces (bind pose) where the pose puts them (linear blend skinning)."""
        P = np.atleast_2d(P)
        q = np.hstack((P, np.ones((len(P), 1))))
        near = hmd.bone_index(pl.bones[b])
        out = (q @ inv[near] @ W[near])[:, :3]
        if b in pl.blends:
            far = hmd.bone_index(pl.blends[b][0])
            w = pl.far_weight(b, P)[:, None]
            out = (1 - w) * out + w * (q @ inv[far] @ W[far])[:, :3]
        return out
    worst: dict[str, float] = {}
    floor: list[float] = []
    by_name = {c.name: c for c in pose.clips}
    for name in (clips or rig.clips):
        clip = by_name[name]
        for f in range(cas_pose.clip_info(pose, clip)[2]):
            W = [np.asarray(m).reshape(4, 4) for m in cas_pose.pose_world(pose, clip, hmd, f)]
            for b, pb, p in joints:
                worst[b] = max(worst.get(b, 0.0), float(np.linalg.norm(carry(W, b, p) - carry(W, pb, p))))
            floor.append(min(float(carry(W, b, P)[:, 1].min()) for b, P in pieces))
    return {'joints': worst, 'floor': (min(floor), max(floor)) if floor else None}


def turn_degrees(A: np.ndarray) -> float:
    """How far a map turns (its rotation's angle, the scale taken out): for the reports."""
    u, _s, vt = np.linalg.svd(A)
    R = u @ vt
    return math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(R) - 1) / 2))))
