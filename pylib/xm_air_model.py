"""An X-Morph: Defense aircraft's model on the V506 (docs/xmorph-port.md 「直升机」): the archive EDF6VC_XM_<KEY>.MRAB and
its animation EDF6VC_XM_<KEY>.CAS, built at install from the unit's model folder (tools/prep_xmorph.py heli: models/
xm_<key>/, the model pack's) and the player's own Root.cpk.

A heli flies as the stock V506 does (the game's heli flight, the player's or an NPC pilot's: src/heli.cpp), so its model
keeps every bone the V506's SGO names (its MAB's locators, its ragdoll, its dead effect: body, rotor, tailRotor,
rearWheelSus, bend_roterA..D) and the V506's own animation drives them: rotor spun about its local y, tailRotor about
its x (V506_HELI.CAS roter_roll_add), at the places its `default` clip puts them. The folder's two spun rotors hang on
those bones through a turned mount each (xm_rotor_mount, xm_tail_mount: the rotor's frame, its axis the bone's y / x;
the clip sets the spun bone's local rotation, so the turn is the mount's), and the CAS is the V506's with `default`'s
translations moved onto this skeleton (cas_pose.retarget). Skeleton (preorder, every bone but the mounts' subtrees
level):

    mdl -> xm_<key> (the skinned object's bone), globalSRT -> body (at BODY_AT, where the CAS's default clip puts it)
        -> rearWheelSus, xm_hull (the airframe, every triangle but the spun rotors'), xm_rotor_mount -> rotor ->
           bend_roterA..D, xm_tail_mount -> tailRotor, and the weapon holders (Holder: no mesh, at their muzzles)

The model stands as the game's: X-Morph's ground (its z 0) at y 0, x and z as the unit's own (its centre of rotation).

    build_archive(game, folder, key, holders) -> (MRAB bytes, CAS bytes, info)
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import replace

import numpy as np

import obj_model as om
import texfile
import texfile_accel
from dreadnought_model import BODY_AT, BODY_HOST, BODY_SLOTS, _host, _member, _with_frames
from mdb import Mdb, Object, Rab, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, mmul, rab_read, \
    rab_write

HULL, ROTOR_MOUNT, TAIL_MOUNT = 'xm_hull', 'xm_rotor_mount', 'xm_tail_mount'
SPUN = {'rotor': ROTOR_MOUNT, 'tailRotor': TAIL_MOUNT}
BENDS = ('bend_roterA', 'bend_roterB', 'bend_roterC', 'bend_roterD')
STOCK = ('V506_HELI.MRAB', 'v506_heli.mdb', 'V506_HELI.CAS')
# The V506's bones its SGO names (MAB locators' parents, ragdoll, dead effect, bone mapping): all kept, by name.
V506_BONES = ('mdl', 'globalSRT', 'body', 'rearWheelSus', 'rotor', *BENDS, 'tailRotor')
# The textures' third map (v505_tank's param_r_m_occ_hr: R roughness, G metal, B occlusion, 255 none): X-Morph's
# materials have a specular map, not these; a painted metal's.
ROUGH_METAL = (150, 70, 255)
MIN_TRIANGLES = 800


class XmAirModelError(ValueError):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise XmAirModelError(msg)


def archive_name(key: str) -> str:
    return f'EDF6VC_{key.upper()}.MRAB'


def animation_name(key: str) -> str:
    return f'EDF6VC_{key.upper()}.CAS'


def mdb_name(key: str) -> str:
    return f'edf6vc_{key}.mdb'


def read_info(folder: str) -> dict:
    with open(os.path.join(folder, 'model.json'), encoding='utf-8') as h:
        info = json.load(h)
    _req(info.get('format') == 1 and info.get('family') in ('heli', 'jet'), f'{folder}: not tools/prep_xmorph.py\'s aircraft')
    return info


def model_files(folder: str) -> list[str]:
    """Every file of the folder the archive is made from (the installer's cache hashes them)."""
    info = read_info(folder)
    names = ['model.json', 'model.npz'] + [m[k] for m in info['materials'] for k in ('albedo', 'normal') if k in m]
    return [os.path.join(folder, n) for n in names]


def _stock(game) -> tuple[Mdb, bytes]:  # noqa: ANN001 - rootcpk.Game
    md = mdb_read(_member(rab_read(game.read('OBJECT', STOCK[0])), STOCK[1]).data)
    return md, game.read('OBJECT', STOCK[2])


def _rel(stock: Mdb, name: str) -> tuple[float, float, float]:
    """A V506 bone's bind translation from its parent (its local's, every V506 bone is level)."""
    b = stock.bones[[stock.name_of(x.name) for x in stock.bones].index(name)]
    return tuple(float(x) for x in b.local[12:15])


def skeleton(info: dict, stock: Mdb, holders: dict[str, tuple[float, float, float]]) -> \
        tuple[list[tuple[str, str | None, tuple[float, float, float]]], dict[str, list[list[float]]]]:
    """([(bone, parent, model-space pivot)] in preorder, {bone: frame rows} of the turned ones)."""
    key = info['kind']
    body = np.array(BODY_AT)
    out = [('mdl', None, (0.0, 0.0, 0.0)), (key, 'mdl', (0.0, 0.0, 0.0)), ('globalSRT', 'mdl', (0.0, 0.0, 0.0)),
           ('body', 'globalSRT', BODY_AT), ('rearWheelSus', 'body', tuple(body + _rel(stock, 'rearWheelSus'))),
           (HULL, 'body', (0.0, 0.0, 0.0))]
    frames: dict[str, list[list[float]]] = {}
    for spun, mount in SPUN.items():
        r = info['rotors'].get(spun)
        if r is None:
            # no rotor of its own spun on this bone (the small heli's one propeller, the medium heli's coaxial pair):
            # the bone where the V506 has it, empty (its MAB and dead effect name it)
            at = tuple(body + _rel(stock, spun))
            rows = np.eye(3)
        else:
            at, rows = tuple(r['centre']), np.array(r['frame'])
        out += [(mount, 'body', at), (spun, mount, at)]
        frames[mount] = frames[spun] = rows.tolist()
        if spun == 'rotor':
            for b in BENDS:
                p = tuple(np.array(at) + np.array(_rel(stock, b)) @ rows)
                out.append((b, 'rotor', p))
                frames[b] = rows.tolist()
    out += [(h, 'body', tuple(float(c) for c in p)) for h, p in holders.items()]
    names = [n for n, _p, _a in out]
    _req(len(set(names)) == len(names), f'{key}: a bone twice {names}')
    _req(set(V506_BONES) <= set(names), f'{key}: V506 bones missing {sorted(set(V506_BONES) - set(names))}')
    return out, frames


def _textures(folder: str, key: str, material: dict) -> tuple[dict[str, bytes], dict[str, str]]:
    """({archive file: DDS}, {slot: file}) of one material: its colour, its normal map (flat where it has none) and the
    painted metal's roughness map (ROUGH_METAL)."""
    stem = f'edf6vc_{key}_{material["name"]}'
    flat = texfile.Image(4, 4, bytearray(bytes((128, 128, 255, 255)) * 16))
    rml = texfile.Image(4, 4, bytearray(bytes((*ROUGH_METAL, 255)) * 16))
    nm = (texfile_accel.bc5_normal_file(os.path.join(folder, material['normal'])) if 'normal' in material
          else texfile_accel.bc5_normal_dds(flat))
    files = {f'{stem}_df.dds': texfile_accel.dxt1_dds(texfile.load_image(os.path.join(folder, material['albedo']))),
             f'{stem}_nm.dds': nm, f'{stem}_rm.dds': texfile_accel.dxt1_dds(rml)}
    return files, dict(zip(BODY_SLOTS, files))


def _parts(folder: str, info: dict) -> list[tuple[om.Part, str]]:
    """(part, its bone) of every part the folder has, in model space as prepared."""
    arr = np.load(os.path.join(folder, 'model.npz'))
    out = []
    for k, p in enumerate(info['parts']):
        pos, nrm, uv, tri = (arr[f'p{k}_{s}'] for s in ('pos', 'nrm', 'uv', 'tri'))
        verts = [om.Vertex(tuple(map(float, a)), tuple(map(float, n)), (float(t[0]), float(t[1])))
                 for a, n, t in zip(pos, nrm, uv)]
        out.append((om.Part(f'{p["bone"]}_{k}', info['materials'][p['material']]['name'], verts,
                            [tuple(int(i) for i in t) for t in tri]), p['bone']))
    return out


def build_model(game, folder: str, key: str,  # noqa: ANN001 - rootcpk.Game
                holders: dict[str, tuple[float, float, float]]) -> tuple[Mdb, dict[str, bytes], bytes, dict]:
    """(model, its textures {file: DDS}, its animation (the V506's CAS retargeted), info)."""
    import procmesh
    from cas_pose import retarget
    info = read_info(folder)
    _req(info['kind'] == key, f'{folder}: the folder of {info["kind"]}, not {key}')
    stock, cas = _stock(game)
    bones, frames = skeleton(info, stock, holders)
    names = [n for n, _p, _a in bones]
    pieces = [(p, names.index(b)) for p, b in _parts(folder, info)]
    joints = [(n, names.index(p) if p else -1, a) for n, p, a in bones]
    body_md, body_mesh = _host(game, BODY_HOST)
    md = Mdb(body_md.version, names, [], [], [], [])
    tex: dict[str, bytes] = {}
    meshes = []
    for m in info['materials']:
        if m.get('dropped'):
            continue
        files, slots = _textures(folder, key, m)
        tex.update(files)
        md, mi = om.add_material(md, body_md, BODY_HOST[2], f'{key}_{m["name"]}', slots)
        group = [(p, b) for p, b in pieces if p.material == m['name']]
        meshes += om.build_meshes(body_mesh, [(p, om.rigid(p, b)) for p, b in group], mi)
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    posed = [(np.array(p.positions()), [list(s) for s in om.rigid(p, b)]) for p, b in pieces]
    skel = procmesh.bones(joints, key, posed, 1.0)
    md = replace(md, bones=skel, objects=[Object(names.index(key), names.index(key), meshes)])
    pivots = {n: a for n, _p, a in bones}
    md = _with_frames(md, frames, pivots, posed, names)
    anim = retarget(cas, stock, md, {'default'})
    tris = sum(len(me.indices) // 6 for me in meshes)
    _req(tris >= MIN_TRIANGLES, f'{key}: {tris} triangles: the model folder is not tools/prep_xmorph.py\'s')
    out = {'triangles': tris, 'meshes': len(meshes), 'materials': len(md.materials), 'bones': len(skel),
           'size': [round(float(x), 1) for x in info['size']]}
    return md, tex, anim, out


def build_archive(game, folder: str, key: str,  # noqa: ANN001 - rootcpk.Game
                  holders: dict[str, tuple[float, float, float]]) -> tuple[bytes, bytes, dict]:
    """EDF6VC_XM_<KEY>.MRAB (the model and its textures, the stock archives' folder layout), its CAS, info."""
    md, tex, anim, info = build_model(game, folder, key, holders)
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, f'{key}: the model does not round-trip')
    template = rab_read(game.read('OBJECT', BODY_HOST[0]))
    rab = Rab(template.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    insert_member(rab, RabFile(mdb_name(key), rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    for name, dds in tex.items():
        om.add_texture(rab, name, dds)
    out = rab_write(rab)
    check_archive(out, key, holders)
    return out, anim, info


def check_archive(arc: bytes, key: str, holders: dict[str, tuple[float, float, float]]) -> None:
    """Re-read `arc`: archive and model round-trip; every V506 bone and holder there; one object on the kind-2 bone
    `key`; every texture member in place; bind x inverse bind the identity; every vertex 100 % on one mesh bone (the hull
    or a spun rotor)."""
    import graft_pure as g
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = _member(rab, mdb_name(key)).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    names = [md.name_of(b.name) for b in md.bones]
    _req(names[:4] == ['mdl', key, 'globalSRT', 'body'] and set(V506_BONES) | set(holders) <= set(names),
         f'{key}: bones {names}')
    bad = om.texture_problems(rab, md, mdb_name(key))
    _req(not bad, 'textures: ' + '; '.join(bad))
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'not one object on a kind-2 bone')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    for name, at in holders.items():
        got = w[names.index(name)][12:15]
        _req(max(abs(got[k] - at[k]) for k in range(3)) < 1e-4, f'holder {name} at {got}, not {at}')
    for j, me in enumerate(md.objects[0].meshes):
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(bool(idx) and len(idx) % 3 == 0 and max(idx) < me.nverts < 0x10000, f'mesh {j}: index buffer')
        bi, bw = g.skin_columns(me)
        for r, wt in zip(bi, bw):
            bone = names[int(r[0])]
            _req(bone in (HULL, 'rotor', 'tailRotor') and abs(wt[0] - 1.0) < 1e-3, f'mesh {j}: skin {bone} {wt}')
