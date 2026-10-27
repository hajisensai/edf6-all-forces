"""The M163 and the BMPT from their War Thunder models (models/wt_m163, models/wt_bmpt: tools/prep_wt_tracked.py made
them from the Sketchfab uploads, docs/wt-tracked-models.md), on the native V603 skeleton the stock Vehicle603_Flak
drives: the same bones, links, flags and rotations, each bone's origin moved onto the model (the turret ring, the
guns' trunnions and mouths, the wheels' axles: model.json 'origins'), plus the M163's visual rotor leaf. Everything
that follows a moved bone is refitted from these binds, never from the stock positions: the ragdoll
(ragdoll_fit.fit), the CAS's absolute clip (cas_pose.retarget), the muzzle and case-port locators (bone-local).

    available(kind) -> bool             the model folder is there (and ENABLED)
    build_with_info(game, kind)         (archive, model, info) like the procedural modules
    parts(a, kind)                      the Assembly's parts (one bone, one material each), muzzles and wheels
    muzzle_local / ejection_local       the weapons' locators in their bones' frames
    animation(game, kind)               the private CAS
    collision_layout(kind)              what tracked_collision fits each physics proxy to
    check(arc, kind)                    raises WtModelError

The pieces are rigid (one bone each) and keep the model's own UVs and textures: the source's albedo and normal maps,
its occlusion map in the stock body material's occlusion channel (param_r_m_occ_hr: roughness and metalness are the
stock maps' mean, the procedural vehicles' PLAIN_RMO), the tracks under the native track materials' names so the
SGO's tank_caterpillar_animation still scrolls them.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, replace

import numpy as np

import cockpits
import graft_pure as g
import obj_model as om
import tracked_combat_model as t
import xm_units
from mdb import Mdb, Mesh, bind_world, cmpl_compress, cmpl_decompress, mdb_read, mdb_write, rab_read, rab_write

FOLDERS = {'vulcan': 'wt_m163', 'bmpt': 'wt_bmpt', 'tunguska': 'wt_tunguska', 'pzh2000': 'wt_pzh2000',
           'sturmtiger': 'wt_sturmtiger', 'apocalypse': 'wt_apocalypse', 'tos1': 'wt_tos1',
           # the X-Morph: Defense units (pylib/xm_units.py; tools/prep_xmorph.py writes these folders, the model pack's)
           **{u.key: xm_units.folder(u.key) for u in xm_units.ported('tracked')}}
# A kind made from another kind's folder: its module's derive(spec) turns that kind's spec into its own (the quad M61:
# the M163's model with its one gun made four, pylib/quadvulcan_model.py); with no base kind, its module's spec(kind)
# makes it from its own folder (folder(): the Dreadnought's turrets on their bases, pylib/emplacement_model.py).
DERIVED = {'quadvulcan': ('vulcan', 'quadvulcan_model')}


def _derived(kind: str) -> tuple[str | None, str] | None:
    """(base kind or None, module) of a derived kind: DERIVED's, or an emplacement's (pylib/emplacement_model.py)."""
    if kind in DERIVED:
        return DERIVED[kind]
    import emplacement_model
    return (None, 'emplacement_model') if kind in emplacement_model.KINDS else None
ENABLED = True            # False: the procedural models (their own tests switch it off)
FORMAT = 1                # tools/prep_wt_tracked.py FORMAT
PLAIN_RMO = (140, 90)     # roughness, metalness (~ the V603 / E551 body maps' mean); occlusion from the model
TRACK_RMX = (189, 0, 0)   # the V603 track parameter map's mean
NO_OCCLUSION = 200
HALF_TOL = 4e-3           # m: stored positions are half floats (2^-9 m steps at 2..4 m)


class WtModelError(t.TrackedCombatCheckError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise WtModelError(message)


@dataclass
class Spec:
    folder: str
    info: dict
    arrays: dict[str, np.ndarray]

    def part(self, k: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        a = self.arrays
        return a[f'p{k}_pos'], a[f'p{k}_nrm'], a[f'p{k}_uv'], a[f'p{k}_tri']


@dataclass
class Piece:
    """A part as tracked_collision and the checks read it: positions, triangles, its one bone."""
    name: str
    pos: np.ndarray
    tris: np.ndarray
    _rigid_bone: int
    material: int = -1


def folder(kind: str) -> str | None:
    """The model folder (obj_model.model_dir; a derived kind's: its base's), None when it is not there or the WT models
    are switched off."""
    derived = _derived(kind)
    if derived and derived[0] is None:
        import emplacement_model
        return emplacement_model.source_folder(kind) if ENABLED else None
    kind = derived[0] if derived else kind
    if not ENABLED or kind not in FOLDERS:
        return None
    d = om.model_dir(FOLDERS[kind])
    ok = d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz'))
    return d if ok else None


def available(kind: str) -> bool:
    return folder(kind) is not None


_CACHE: dict[tuple, Spec] = {}


def load(kind: str) -> Spec:
    derived = _derived(kind)
    if derived:
        base, _module = derived
        if base is None:
            import emplacement_model
            _req(ENABLED, f'{kind}: the model folders are switched off')
            return emplacement_model.spec(kind)
        import quadvulcan_model
        return quadvulcan_model.derive(load(base))
    d = folder(kind)
    _req(d is not None, f'{kind}: model folder {FOLDERS.get(kind)} not found ({", ".join(om.model_roots())})')
    stamp = tuple((f, os.stat(os.path.join(d, f)).st_mtime_ns) for f in ('model.json', 'model.npz'))  # type: ignore[arg-type]
    key = (d, stamp)
    if key not in _CACHE:
        with open(os.path.join(d, 'model.json'), encoding='utf-8') as f:     # type: ignore[arg-type]
            info = json.load(f)
        _req(info.get('format') == FORMAT and info.get('kind') == kind, f'{d}: not a format-{FORMAT} {kind} model')
        with np.load(os.path.join(d, 'model.npz'), allow_pickle=False) as z:  # type: ignore[arg-type]
            arrays = {k: z[k] for k in z.files}
        _CACHE.clear()
        _CACHE[key] = Spec(d, info, arrays)  # type: ignore[arg-type]
    return _CACHE[key]


# ------------------------------------------------------------------------------------------ skeleton

def visual_bones(spec: Spec) -> tuple[t.VisualBone, ...]:
    return tuple(t.VisualBone(b['name'], b['parent'], tuple(b['origin'])) for b in spec.info['visual_bones'])


def skeleton(host: Mdb, spec: Spec) -> Mdb:
    """The stock skeleton (no meshes, materials or textures) with each listed bone's origin moved (rotation kept,
    the children's world binds kept: graft_pure.set_bone_origin, parents first), then the visual leaves."""
    md = replace(host, objects=[replace(host.objects[0], meshes=[])], materials=[], textures=[], buffer_order=None)
    origins = spec.info['origins']
    for name in sorted(origins, key=lambda n: md.bone_index(n)):
        _req(md.bone_index(name) >= 0, f'model.json moves a bone the V603 has not: {name}')
        md = g.set_bone_origin(md, md.bone_index(name), tuple(origins[name]))
    extra = visual_bones(spec)
    return t.add_visual_bones(md, extra) if extra else md


def check_skeleton(md: Mdb, host: Mdb, spec: Spec) -> None:
    """Names, links, flags and rotations are the native ones (plus the visual leaves); every bone sits where
    model.json puts it, the others where the stock model has them."""
    extra = {b.name for b in visual_bones(spec)}
    native = [b for b in md.bones if md.name_of(b.name) not in extra]
    _req([md.name_of(b.name) for b in native] == [host.name_of(b.name) for b in host.bones],
         'bone names or order differ from the V603')
    w, w0 = bind_world(md), bind_world(host)
    for b in native:
        name = md.name_of(b.name)
        h = host.bones[host.bone_index(name)]
        _req(md.name_of(md.bones[b.parent].name) == host.name_of(host.bones[h.parent].name) if b.parent >= 0
             else h.parent < 0, f'{name}: parent differs')
        _req((b.kind, b.bounded) == (h.kind, h.bounded), f'{name}: kind / bounded flag differs')
        _req(np.allclose(w[b.index][:12], w0[h.index][:12], atol=1e-5), f'{name}: bind rotation differs')
        want = spec.info['origins'].get(name, w0[h.index][12:15])
        _req(np.allclose(w[b.index][12:15], want, atol=1e-5), f'{name}: bind origin differs from model.json')
        ident = np.asarray(w[b.index]).reshape(4, 4) @ np.asarray(b.inv_bind).reshape(4, 4)
        _req(np.max(np.abs(ident - np.eye(4))) < 2e-5, f'{name}: inverse bind differs')
    for b in visual_bones(spec):
        i = md.bone_index(b.name)
        _req(i >= 0 and md.name_of(md.bones[md.bones[i].parent].name) == b.parent and md.bones[i].kind == 3,
             f'{b.name}: not a skin leaf under {b.parent}')
        _req(np.allclose(w[i][12:15], b.origin, atol=1e-5), f'{b.name}: origin moved')


# ------------------------------------------------------------------------------------------ meshes

def tangent_frames(pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray
                   ) -> tuple[np.ndarray, np.ndarray]:
    """obj_model.tangent_frames in numpy: per vertex dP/du, dP/dv summed over its triangles (sign-normalised), made
    perpendicular to the normal; a vertex without usable UVs gets any perpendicular pair."""
    p0, p1, p2 = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    t0, t1, t2 = uv[tris[:, 0]], uv[tris[:, 1]], uv[tris[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    du1, dv1 = t1[:, 0] - t0[:, 0], t1[:, 1] - t0[:, 1]
    du2, dv2 = t2[:, 0] - t0[:, 0], t2[:, 1] - t0[:, 1]
    det = du1 * dv2 - du2 * dv1
    ok = np.abs(det) >= 1e-12
    s = np.where(det > 0, 1.0, -1.0) * ok
    tu = (e1 * dv2[:, None] - e2 * dv1[:, None]) * s[:, None]
    tv = (e2 * du1[:, None] - e1 * du2[:, None]) * s[:, None]
    T = np.zeros_like(pos)
    B = np.zeros_like(pos)
    for k in range(3):
        np.add.at(T, tris[:, k], tu)
        np.add.at(B, tris[:, k], tv)

    def unit(v: np.ndarray) -> np.ndarray:
        n = np.linalg.norm(v, axis=1, keepdims=True)
        return np.where(n > 1e-12, v / np.maximum(n, 1e-12), 0.0)
    T = unit(T - nrm * np.einsum('ij,ij->i', nrm, T)[:, None])
    bad = ~np.any(T, axis=1)
    if bad.any():
        ref = np.where(np.abs(nrm[bad, 1:2]) < 0.9, [[0.0, 1.0, 0.0]], [[1.0, 0.0, 0.0]])
        T[bad] = unit(np.cross(nrm[bad], ref))
    B = unit(B - nrm * np.einsum('ij,ij->i', nrm, B)[:, None])
    bad = ~np.any(B, axis=1) | (np.abs(np.einsum('ij,ij->i', B, T)) > 0.999)
    if bad.any():
        B[bad] = unit(np.cross(nrm[bad], T[bad]))
    return T, B


def _columns(template: Mesh, pos: np.ndarray, nrm: np.ndarray, tan: np.ndarray, bin_: np.ndarray, uv: np.ndarray,
             bone: int) -> bytes:
    """The vertices in `template`'s layout (mdb_jet.pack_vertex's formats), one bone at full weight."""
    n = len(pos)
    out = np.zeros((n, template.vsize), dtype=np.uint8)
    one = np.ones((n, 1))
    for e in template.elems:
        name = e.name.lower()
        if name == 'position':
            v = np.hstack((pos, one))
        elif name == 'normal':
            v = np.hstack((nrm, one))
        elif name == 'tangent':
            v = np.hstack((tan, one))
        elif name == 'binormal':
            v = np.hstack((bin_, one))
        elif name == 'texcoord':
            v = uv
        elif name == 'blendweight':
            v = np.tile((1.0, 0.0, 0.0, 0.0), (n, 1))
        elif name == 'blendindices':
            v = np.tile((bone, 0, 0, 0), (n, 1))
        else:
            raise WtModelError(f'vertex element {e.name} of the template layout is not handled')
        dtype, width = {1: ('<f4', 4), 4: ('<f4', 3), 7: ('<f2', 4), 12: ('<f4', 2), 21: ('u1', 4)}[e.fmt]
        _req(v.shape[1] >= width, f'vertex element {e.name}: format {e.fmt} wants {width} components')
        raw = np.ascontiguousarray(v[:, :width].astype(dtype)).view(np.uint8).reshape(n, -1)
        out[:, e.offset:e.offset + raw.shape[1]] = raw
    return out.tobytes()


def part_rows(template: Mesh, pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray, bone: int
              ) -> tuple[np.ndarray, np.ndarray]:
    """The part's vertices packed in `template`'s layout (one row each, rigid on `bone`) and the triangles that draw:
    one with two corners at one stored (half-float) point draws nothing and is left out (obj_model.drawn_triangles)."""
    pos = pos.astype(np.float64)
    nrm = nrm.astype(np.float64)
    uv = uv.astype(np.float64)
    tan, bin_ = tangent_frames(pos, nrm, uv, tris)
    half = pos.astype(np.float16)
    a, b, c = (half[tris[:, k]] for k in range(3))
    keep = ~(np.all(a == b, axis=1) | np.all(b == c, axis=1) | np.all(a == c, axis=1))
    rows = np.frombuffer(_columns(template, pos, nrm, tan, bin_, uv, bone), dtype=np.uint8).reshape(len(pos), -1)
    return rows, tris[keep]


def pack(parts: list[tuple[Mesh, int, np.ndarray, np.ndarray]]) -> list[Mesh]:
    """The parts (template, material, rows, triangles: part_rows) as skinned meshes, one material's parts together
    across their bones (each vertex carries its own bone: one influence, up to bone 255), split only where the u16
    indices end (model_lod.pack_rows), in the order of each material's first part. A mesh is one draw in every pass
    (depth, G-buffer, each shadow cascade): until 2026-10-27 each part (one bone x one material) was a mesh of its own,
    cut every 21000 triangles (the BMPT: 58 meshes, now one or two per material)."""
    import model_lod
    by: dict[int, list] = {}
    for template, material, rows, tris in parts:
        by.setdefault(material, [replace(template, flags=bytes((0, 1, 1, 0)), material=material), [], [], 0])
        entry = by[material]
        _req(entry[0].vsize == template.vsize and entry[0].elems == template.elems,
             'two parts of one material in different vertex layouts')
        entry[1].append(rows)
        entry[2].append(tris + entry[3])
        entry[3] += len(rows)
    meshes: list[Mesh] = []
    for template, rows, tris, _n in by.values():
        meshes += model_lod.pack_rows(template, np.concatenate(rows), np.concatenate(tris))
    return meshes


# ------------------------------------------------------------------------------------------ textures

def _image(path: str):  # noqa: ANN202 - texfile.Image
    import texfile
    from PIL import Image
    with Image.open(path) as im:
        rgba = im.convert('RGBA')
        return texfile.Image(rgba.width, rgba.height, bytearray(rgba.tobytes()))


def _dds_of(path: str) -> bytes:
    import texfile_accel
    return texfile_accel.dxt1_dds(_image(path))


def _normal_dds(path: str) -> bytes:
    """A tangent-space normal map (R = X, G = Y, DirectX green: +G is the texture's down, +v, the binormal's way;
    docs/texture-quality.md) as the stock vehicles' BC5U."""
    import texfile_accel
    return texfile_accel.bc5_normal_dds(_image(path))


def _param_dds(occlusion: str | None, rg: tuple[int, int]) -> bytes:
    """A parameter map: R, G constant, B the model's occlusion (NO_OCCLUSION without one)."""
    import texfile
    import texfile_accel
    from PIL import Image
    if occlusion is None:
        return texfile.solid_dxt1((rg[0], rg[1], NO_OCCLUSION))
    with Image.open(occlusion) as im:
        occ = np.asarray(im.convert('L'), dtype=np.uint8)
    h, w = occ.shape
    rgba = np.empty((h, w, 4), dtype=np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2], rgba[..., 3] = rg[0], rg[1], occ, 255
    return texfile_accel.dxt1_dds(texfile.Image(w, h, bytearray(rgba.tobytes())))


def material_textures(spec: Spec, m: dict) -> dict[str, tuple[str, bytes]]:
    """slot kind -> (archive file name, DDS) of one model.json material."""
    import texfile
    stem = m['name'].lower()
    path = lambda key: os.path.join(spec.folder, m[key]) if m.get(key) else None   # noqa: E731
    out = {'albedo': (f'{stem}_col.dds', _dds_of(path('albedo')))}
    if m.get('normal'):
        out['normal'] = (f'{stem}_nor.dds', _normal_dds(path('normal')))
    else:
        out['normal'] = ('edf6vc_flat_nor.dds', texfile.solid_bc5())
    if m['template'].startswith('v603_kyata'):
        out['param_r_m_xxx'] = ('edf6vc_wt_track_rmx.dds', texfile.solid_dxt1(TRACK_RMX))
        occ = path('occlusion')
        out['param_occ'] = (f'{stem}_occ.dds', _dds_of(occ) if occ else texfile.solid_dxt1((222, 219, 222)))
    else:
        out['param_r_m_occ_hr'] = (f'{stem}_rmo.dds', _param_dds(path('occlusion'), PLAIN_RMO))
    return out


# ------------------------------------------------------------------------------------------ build

def parts(a: t.Assembly, kind: str) -> None:
    """`a`'s parts (Piece: positions, triangles, bone; as model.json lists them), its muzzles and wheel centres, and the
    collision pieces ('collision:<proxy>', on that proxy's bone)."""
    spec = load(kind)
    for k, p in enumerate(spec.info['parts']):
        pos, _n, _uv, tris = spec.part(k)
        _req(p['name'] not in a.parts, f'duplicate part {p["name"]}')
        a.parts[p['name']] = Piece(p['name'], pos.astype(np.float64), tris.astype(np.int64), a.bone(p['bone']),
                                   p['material'])
    for proxy in spec.info['collision']:
        pos = spec.arrays[f'c_{proxy}_pos'].astype(np.float64)
        tris = spec.arrays[f'c_{proxy}_tri'].astype(np.int64)
        a.parts['collision:' + proxy] = Piece('collision:' + proxy, pos, tris, a.bone(proxy))
    for side, rows in spec.info['muzzles'].items():
        for name, row in zip(muzzle_names(side, len(rows)), rows):
            a.muzzle(name, row['bone'], tuple(row['point']), 0.0)
    for bone, w in spec.info['wheels'].items():
        a.wheels[bone] = tuple(w['centre'])


def muzzle_names(side: str, count: int) -> list[str]:
    """The Assembly's names of a muzzle slot's `count` points: a gun side its own name; the missile tubes missile_<n>
    (bmpt_model's), another store slot (an X-Morph unit's arm, xm_<key>) <slot>_<n>."""
    if side in ('left', 'right'):
        return [side]
    stem = 'missile' if side == 'missiles' else side
    return [f'{stem}_{i + 1}' for i in range(count)]


def _weapon_muzzles(a: t.Assembly, spec: Spec) -> dict:
    out = {}
    for side, rows in spec.info['muzzles'].items():
        out[side] = [(a.muzzles[n]['bone'], a.muzzles[n]['local']) for n in muzzle_names(side, len(rows))] if rows else []
    return out


def posed(game, kind: str) -> tuple[Mdb, Mdb, Spec]:  # noqa: ANN001 - rootcpk.Game
    """(the moved skeleton, the stock V603 model, the spec)."""
    host, _rab = t.stock(game)
    spec = load(kind)
    return skeleton(host, spec), host, spec


def muzzle_local(game, kind: str) -> dict:  # noqa: ANN001
    md, _host, spec = posed(game, kind)
    a = t.Assembly(md)
    parts(a, kind)
    return _weapon_muzzles(a, spec)


def ejection_local(game, kind: str) -> dict:  # noqa: ANN001
    md, _host, spec = posed(game, kind)
    a = t.Assembly(md)
    for side, row in spec.info['ejections'].items():
        a.muzzle(side, row['bone'], tuple(row['point']), 0.0)
    return {side: a.muzzles[side]['local'] for side in spec.info['ejections']}


def build_with_info(game, kind: str) -> tuple[bytes, Mdb, dict]:  # noqa: ANN001 - rootcpk.Game
    host, rab = t.stock(game)
    spec = load(kind)
    md = skeleton(host, spec)
    a = t.Assembly(md)
    parts(a, kind)
    # materials (the stock template's shader and parameters, the model's textures) and their archive members
    keep = t.member(rab, t.HOST_MDB).name.lower()
    rab.files = [f for f in rab.files if f.name.lower() == keep]
    mat_index = []
    added: set[str] = set()
    for m in spec.info['materials']:
        tex = material_textures(spec, m)
        md, mi = om.add_material(md, host, m['template'], m['name'], {kind_: fn for kind_, (fn, _d) in tex.items()})
        mat_index.append(mi)
        for fn, dds in tex.values():
            if fn not in added:
                om.add_texture(rab, fn, dds)
                added.add(fn)
    templates = {host.name_of(host.materials[me.material].name): me for me in host.objects[0].meshes}
    packed_parts = []
    for k, p in enumerate(spec.info['parts']):
        pos, nrm, uv, tris = spec.part(k)
        tmpl = templates[spec.info['materials'][p['material']]['template']]
        rows, kept = part_rows(tmpl, pos, nrm, uv, tris.astype(np.int64), md.bone_index(p['bone']))
        packed_parts.append((tmpl, mat_index[p['material']], rows, kept))
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(pack(packed_parts))]
    md = g.recompute_bounds(replace(md, objects=[replace(md.objects[0], meshes=meshes)]))
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, 'MDB write/read/write differs')
    packed = cmpl_compress(data)
    _req(cmpl_decompress(packed) == data, 'CMPL compression differs')
    t.member(rab, t.HOST_MDB).stored = packed
    arc = rab_write(rab)
    allpos = np.vstack([p.pos for n, p in a.parts.items() if not n.startswith('collision:')])
    info = {'designation': spec.info['title'], 'source': spec.info['source'],
            'model_folder': os.path.basename(spec.folder), 'render_bounds': [allpos.min(0).tolist(), allpos.max(0).tolist()],
            'bone_count': len(md.bones), 'visual_bones': [b['name'] for b in spec.info['visual_bones']],
            'moved_bones': sorted(spec.info['origins']), 'stock_meshes_retained': 0,
            'materials': [m['name'] for m in spec.info['materials']], 'meshes': len(meshes),
            'triangles': int(sum(len(spec.part(k)[3]) for k in range(len(spec.info['parts'])))),
            'muzzles': _weapon_muzzles(a, spec), 'muzzle_details': a.muzzles,
            'wheel_centres': a.wheels,
            'collision_contract': 'make_tracked_combat fits the private SHKT to the model (collision groups in '
                                  'model.json) after refitting the V603 ragdoll to the moved bones (ragdoll_fit)',
            'cas_contract': 'absolute radar loop retargeted to the moved bones (cas_pose.retarget), the radar '
                            'bones\' tracks dropped; the fire loop is additive and kept'}
    check(arc, kind, host)
    return arc, md, info


def animation(game, kind: str, md: Mdb | None = None) -> bytes:  # noqa: ANN001
    """The V603's CAS with its absolute clip (doppler_radar_loop: every bone's translation, the gun slides' too)
    rebased onto the moved bones, then the two radar bones' tracks dropped (they carry the model's optics, still on
    the turret)."""
    import cas_pose
    host, _rab = t.stock(game)
    if md is None:
        md = skeleton(host, load(kind))
    data = cas_pose.retarget(game.read('OBJECT', t.HOST_CAS), host, md, {'doppler_radar_loop'})
    for name in ('doppler_radar', 'tracking_radar'):
        data = cas_pose.strip_bone(data, {'doppler_radar_loop'}, name)
    return data


def collision_layout(kind: str) -> dict:
    """proxy -> {'parts': [Assembly part names], 'target': box}: the collision pieces model.json lists."""
    spec = load(kind)
    return {proxy: {'parts': ['collision:' + proxy], 'target': np.asarray(c['target'], dtype=float)}
            for proxy, c in spec.info['collision'].items()}


def check(arc: bytes, kind: str, host: Mdb | None = None) -> dict:
    spec = load(kind)
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'RAB write/read/write differs')
    data = t.member(rab, t.HOST_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'MDB write/read/write differs')
    if host is not None:
        check_skeleton(md, host, spec)
    _req(len(md.objects) == 1, 'the model must have one object')
    a = t.Assembly(md)
    parts(a, kind)
    # a finished model also carries its seats' cockpits (make_tracked_combat.build: cockpits.graft_files), not ours
    names = {n for n in (md.name_of(m.name) for m in md.materials) if not cockpits.grafted_material(n)}
    _req(names == {m['name'] for m in spec.info['materials']}, 'materials differ from model.json')
    _req({'v603_kyata_L', 'v603_kyata_R'} <= names, 'the tracks lost the native scrolled material names')
    by_material: dict[int, list[Mesh]] = {}
    for me in md.objects[0].meshes:
        by_material.setdefault(me.material, []).append(me)
    drawn = 0
    for k, p in enumerate(spec.info['parts']):
        bone = md.bone_index(p['bone'])
        _req(bone >= 0 and md.bones[bone].kind == 3, f'{p["name"]}: not on a skin bone')
    for me in md.objects[0].meshes:
        if cockpits.grafted_material(md.name_of(md.materials[me.material].name)):
            continue
        bi, bw = g.skin_columns(me)
        _req(all(abs(w[0] - 1) < 1e-6 and all(abs(x) < 1e-6 for x in w[1:]) for w in bw), 'a vertex is not rigid')
        _req(all(0 <= int(i[0]) < len(md.bones) and md.bones[int(i[0])].kind == 3 for i in bi), 'a vertex off the skin bones')
        P = np.asarray(g.mesh_positions(me))
        _req(bool(np.isfinite(P).all()), 'nonfinite vertex')
        _req(len(me.indices) % 6 == 0 and max(np.frombuffer(me.indices, '<u2'), default=0) < me.nverts, 'bad index')
        drawn += len(me.indices) // 6
    total = sum(len(spec.part(k)[3]) for k in range(len(spec.info['parts'])))
    _req(total * 0.99 <= drawn <= total, f'{drawn} of {total} triangles drawn')
    files = {f.name.lower() for f in rab.files}
    for tex in md.textures:
        _req(tex.filename.lower() in files, f'missing texture {tex.filename}')
    bad = om.texture_problems(rab, md, t.HOST_MDB)
    _req(not bad, 'textures: ' + '; '.join(bad))
    w = bind_world(md)
    for bone, centre in a.wheels.items():
        group = [bone]
        if f'wheel_G_outer{bone[-2:]}' in a.parts and bone[:-2] in ('tire_moveG', 'tire_moveH'):
            group = ['tire_moveG' + bone[-2:], 'tire_moveH' + bone[-2:]]     # the split idler: one wheel, two bones
        P = np.vstack([p.pos for n, p in a.parts.items()
                       if not n.startswith('collision:') and md.name_of(md.bones[p._rigid_bone].name) in group])
        mid = (P.min(0) + P.max(0)) / 2
        _req(np.allclose(mid[1:], centre[1:], atol=1e-5), f'{bone}: the wheel is not on its axle')
        _req(np.allclose(w[a.bone(bone)][13:15], centre[1:], atol=1e-5), f'{bone}: the bone is not at its wheel')
    return {'bones': len(md.bones), 'meshes': len(md.objects[0].meshes), 'triangles': drawn,
            'stock_meshes_retained': 0, 'muzzles': a.muzzles}


class Model:
    """A kind built from its model folder alone (no procedural fallback), as make_tracked_combat.model_module hands the
    vehicles' modules (vulcan_model's API): the 2S6, the PzH 2000, the Sturmtiger, the emplacements (CAMERA: the
    vehicle SGO's camera, game_object_camera_setting)."""

    def __init__(self, kind: str, camera: dict) -> None:
        self.kind = kind
        self.__name__ = f'wt_tracked.Model[{kind}]'
        self.OUT_ARC = f'EDF6VC_{kind.upper()}.MRAB'
        self.OUT_CAS = f'EDF6VC_{kind.upper()}.CAS'
        self.HOST_MDB = t.HOST_MDB
        self.CAMERA = camera

    def available(self) -> bool:
        return available(self.kind)

    def parts(self, a: t.Assembly) -> None:
        parts(a, self.kind)

    def muzzle_local(self, game) -> dict:  # noqa: ANN001 - rootcpk.Game
        return muzzle_local(game, self.kind)

    def ejection_local(self, game) -> dict:  # noqa: ANN001
        return ejection_local(game, self.kind)

    def build_with_info(self, game):  # noqa: ANN001, ANN201
        return build_with_info(game, self.kind)

    def build(self, game) -> bytes:  # noqa: ANN001
        return self.build_with_info(game)[0]

    def check(self, arc: bytes, host=None) -> dict:  # noqa: ANN001
        return check(arc, self.kind, host)

    def animation(self, game, md=None) -> bytes:  # noqa: ANN001
        return animation(game, self.kind, md)

    def collision_layout(self) -> dict:
        return collision_layout(self.kind)
