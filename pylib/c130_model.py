"""The C-130s (docs/feature-support-jet-models.md §6): the support gunship (an AC-130) and the paratroop plane, each an
archive EDF6VC_C130_<MODEL>.MRAB built at install from the model folder tools/prep_c130.py makes out of FlightGear's
C-130 (GPL v2; never in the repository) and the player's own Root.cpk.

The folder holds, per model (MODELS), <model>.obj / .mtl in game space and metres (+Y up, +Z the nose, +X its left; one
OBJ object per bone, every triangle rigid on it, the bones and their pivots in c130.json), and per material its colour
(<material>_df.png), normal map (_nm) and R roughness / G metal / B 0 map (_rml).

The skeleton (preorder; every bone bound level, as the Dreadnought's: dreadnought_model.py):
  mdl (root: the V506 MAB's locators hang on it, vcobjects.JET_MAB_ROOT) -> <model> (the skinned object's bone),
  globalSRT -> body (the V506's anchor, at BODY_AT; no geometry) -> c130_hull (the airframe, at the origin) ->
  c130_prop_<i> (each propeller, at its hub; the plugin turns none yet).
The model stands on its lowest point (origin y = 0), centred on x and z. Its materials are the V505's hull material
(Retro-Balam's lit one is not needed: no lights) with the folder's maps.

    python pylib/c130_model.py [OUT_DIR]     build every archive from the model folder and the game, check, write
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import replace

import obj_model as om
import texfile
import texfile_accel
from mdb import Mdb, Object, Rab, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, mmul, rab_read, \
    rab_write

MODEL_SUBDIR = 'c130'
MANIFEST = 'c130.json'
MODELS = ('gunship', 'transport')
PREFIX = 'c130_'
HULL = PREFIX + 'hull'
PROPS = 4


def archive_name(model: str) -> str:
    return f'EDF6VC_C130_{model.upper()}.MRAB'


def mdb_name(model: str) -> str:
    return f'edf6vc_c130_{model}.mdb'


FILES = {archive_name(m): m for m in MODELS}   # archive -> model (pylib/jet_models.py C130_FILES, which must agree)
BODY_AT = (0.0, 1.637, 0.0)      # the V506 CAS's `default` clip: body's position (dreadnought_model.BODY_AT)
BODY_HOST = ('V505_TANK.MRAB', 'v505_tank.mdb', 'v505_tank')   # (archive, model, material): the hull material
BODY_SLOTS = ('albedo', 'normal', 'param_r_m_occ_hr')     # R roughness, G metal, B occlusion (255: none)
MIN_TRIANGLES = {'gunship': 30000, 'transport': 20000}
# The real C-130's span and length (m: 40.41 x 29.79, the source's Read-Me) the model keeps, within SIZE_SLACK.
SIZE = {'span': 40.41, 'length': 29.79}
SIZE_SLACK = 0.5


class C130ModelError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise C130ModelError(msg)


def model_dir() -> str | None:
    return om.model_dir(MODEL_SUBDIR)


def read_manifest(folder: str) -> dict:
    with open(os.path.join(folder, MANIFEST), encoding='utf-8') as h:
        m = json.load(h)
    _req(set(MODELS) <= set(m.get('models', {})), f'{MANIFEST}: models {sorted(set(MODELS) - set(m.get("models", {})))} missing')
    return m


def model_files(folder: str) -> list[str]:
    """Every file of the folder the archives are made from (the installer's cache hashes them)."""
    names = [MANIFEST] + [f'{m}.{x}' for m in MODELS for x in ('obj', 'mtl')]
    names += sorted(f for f in os.listdir(folder) if f.endswith(('_df.png', '_nm.png', '_rml.png')))
    return [os.path.join(folder, n) for n in names]


def _ours(model: str, bone: str) -> str:
    return HULL if bone == model else PREFIX + bone


def skeleton(model: str, manifest: dict) -> list[tuple[str, str | None]]:
    """(bone, parent) in preorder: the V506's frame, then the hull and every bone of the model under its parent."""
    out: list[tuple[str, str | None]] = [('mdl', None), (model, 'mdl'), ('globalSRT', 'mdl'), ('body', 'globalSRT')]
    bones = manifest['models'][model]['bones']
    kids: dict[str, list[str]] = {}
    for b, e in bones.items():
        if e['parent']:
            kids.setdefault(e['parent'], []).append(b)

    def walk(b: str, parent: str) -> None:
        out.append((_ours(model, b), parent))
        for c in kids.get(b, []):
            walk(c, _ours(model, b))
    walk(model, 'body')
    return out


def _member(rab: Rab, name: str) -> RabFile:
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def _host(game):  # noqa: ANN001, ANN202 - rootcpk.Game -> (Mdb, mdb.Mesh)
    md = mdb_read(_member(rab_read(game.read('OBJECT', BODY_HOST[0])), BODY_HOST[1]).data)
    mesh = next((me for o in md.objects for me in o.meshes if md.name_of(md.materials[me.material].name) == BODY_HOST[2]), None)
    _req(mesh is not None, f'{BODY_HOST[0]}: no mesh in material {BODY_HOST[2]}')
    return md, mesh


def _textures(folder: str, model: str, material: str, made: bool = True) -> tuple[dict[str, bytes], dict[str, str]]:
    """({archive file: DDS}, {slot: file}) of one material: its colour, normal map, roughness / metal (B 255: no
    occlusion). Not `made`: the files named, empty (a model only measured: jet_models._model_of)."""
    stem = f'edf6vc_c130_{model}_{material.lower()}'
    if not made:
        names = [f'{stem}_df.dds', f'{stem}_nm.dds', f'{stem}_rm.dds']
        return dict.fromkeys(names, b''), dict(zip(BODY_SLOTS, names))
    rml = texfile.load_image(os.path.join(folder, f'{material}_rml.png'))
    px = bytearray(rml.rgba)
    px[2::4] = b'\xff' * (len(px) // 4)
    rml = texfile.Image(rml.width, rml.height, px)
    files = {f'{stem}_df.dds': texfile_accel.dxt1_dds(texfile.load_image(os.path.join(folder, f'{material}_df.png'))),
             f'{stem}_nm.dds': texfile_accel.bc5_normal_file(os.path.join(folder, f'{material}_nm.png')),
             f'{stem}_rm.dds': texfile_accel.dxt1_dds(rml)}
    return files, dict(zip(BODY_SLOTS, files))


def build_model(game, folder: str, model: str, textures: bool = True) -> tuple[Mdb, dict[str, bytes], dict]:  # noqa: ANN001
    """(model, its textures {file: DDS}, info): `game` a rootcpk.Game. Without `textures` the textures are named, not made
    (empty): the model alone, as jet_models measures it."""
    import numpy as np
    import procmesh
    manifest = read_manifest(folder)
    entry = manifest['models'][model]
    bones = skeleton(model, manifest)
    names = [n for n, _ in bones]
    obj = om.read_obj(os.path.join(folder, f'{model}.obj'))
    parts = om.obj_parts(obj, om.Conversion(flip_v=True))
    lo = min(v.pos[1] for p in parts for v in p.verts)
    xs = [v.pos[0] for p in parts for v in p.verts]
    zs = [v.pos[2] for p in parts for v in p.verts]
    shift = (-(min(xs) + max(xs)) / 2.0, -lo, -(min(zs) + max(zs)) / 2.0)   # stand it on its lowest point, centred

    def moved(p):  # noqa: ANN001, ANN202 - om.Part
        return replace(p, verts=[replace(v, pos=tuple(v.pos[k] + shift[k] for k in range(3))) for v in p.verts])
    pieces = []
    for p in parts:
        b = _ours(model, p.name)
        _req(b in names, f'{model}.obj: object {p.name} is no bone of the skeleton')
        pieces.append((moved(p), names.index(b)))
    pivots = {'mdl': (0.0, 0.0, 0.0), model: (0.0, 0.0, 0.0), 'globalSRT': (0.0, 0.0, 0.0), 'body': BODY_AT}
    for b, e in entry['bones'].items():
        pivots[_ours(model, b)] = tuple(e['pivot'][k] + shift[k] for k in range(3))
    pivots[HULL] = (0.0, 0.0, 0.0)
    joints = [(n, names.index(p) if p else -1, pivots[n]) for n, p in bones]
    host_md, host_mesh = _host(game)
    md = Mdb(host_md.version, names, [], [], [], [])
    tex: dict[str, bytes] = {}
    meshes = []
    for material in entry['materials']:
        files, slots = _textures(folder, model, material, textures)
        tex.update(files)
        md, mi = om.add_material(md, host_md, BODY_HOST[2], f'c130_{material.lower()}', slots)
        group = [(p, b) for p, b in pieces if p.material == material]
        meshes += om.build_meshes(host_mesh, [(p, om.rigid(p, b)) for p, b in group], mi)
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    posed = [(np.array(p.positions()), [list(s) for s in om.rigid(p, b)]) for p, b in pieces]
    skel = procmesh.bones(joints, model, posed, 1.0)
    all_names = list(md.names) + [model] if model not in md.names else list(md.names)
    md = replace(md, names=all_names, bones=skel, objects=[Object(all_names.index(model), names.index(model), meshes)])
    tris = sum(len(me.indices) // 6 for me in meshes)
    _req(tris >= MIN_TRIANGLES[model], f'{model}: {tris} triangles: the model folder is not tools/prep_c130.py\'s')
    size = np.ptp(np.vstack([q for q, _ in posed]), axis=0)
    _req(abs(size[0] - SIZE['span']) < SIZE_SLACK and abs(size[2] - SIZE['length']) < SIZE_SLACK,
         f'{model}: {size[0]:.2f} m across, {size[2]:.2f} m long, not a C-130\'s {SIZE}')
    info = {'triangles': tris, 'meshes': len(meshes), 'materials': len(md.materials), 'bones': len(skel),
            'size': [round(float(x), 2) for x in size]}
    return md, tex, info


def build_archive(game, folder: str, model: str) -> tuple[bytes, dict, Mdb]:  # noqa: ANN001 - rootcpk.Game
    """EDF6VC_C130_<MODEL>.MRAB (the model and its textures, the stock archives' folder layout), info, the model."""
    md, tex, info = build_model(game, folder, model)
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, f'{model}: the model does not round-trip')
    template = rab_read(game.read('OBJECT', BODY_HOST[0]))
    rab = Rab(template.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    insert_member(rab, RabFile(mdb_name(model), rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    for name, dds in tex.items():
        om.add_texture(rab, name, dds)
    out = rab_write(rab)
    check_archive(out, model)
    return out, info, md


def check_archive(arc: bytes, model: str) -> None:
    """Re-read `arc`: archive and model round-trip; the V506 frame's bones first, the hull and PROPS propellers; one
    object on the kind-2 bone `model`; every texture member in place; bind x inverse bind the identity; every vertex
    100 % on one c130_ skin bone; the model standing on its origin. The seats' cockpits tools/make_c130.py grafts in
    (pylib/cockpits.py: their marker bones, their kits' materials) are left out of the bones' and the meshes' checks."""
    import cockpits
    import graft_pure as g
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = _member(rab, mdb_name(model)).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    names = [md.name_of(b.name) for b in md.bones]
    _req(names[:5] == ['mdl', model, 'globalSRT', 'body', HULL], f'{model}: bones {names[:6]}')
    _req(sorted(n for n in names if n.startswith(PREFIX + 'prop_')) == [f'{PREFIX}prop_{i}' for i in range(1, PROPS + 1)],
         f'{model}: propellers {names}')
    bad = om.texture_problems(rab, md, mdb_name(model))
    _req(not bad, 'textures: ' + '; '.join(bad))
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'not one object on a kind-2 bone')
    w = bind_world(md)
    for b in md.bones:
        if cockpits.grafted_bone(md.name_of(b.name)):
            continue
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    lo = 1e9
    for j, me in enumerate(md.objects[0].meshes):
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(bool(idx) and len(idx) % 3 == 0 and max(idx) < me.nverts < 0x10000, f'mesh {j}: index buffer')
        if cockpits.grafted_material(md.name_of(md.materials[me.material].name)):
            continue
        bi, bw = g.skin_columns(me)
        for p, r, wt in zip(g.mesh_positions(me), bi, bw):
            bone = int(r[0])
            _req(md.name_of(md.bones[bone].name).startswith(PREFIX) and md.bones[bone].kind == 3
                 and abs(wt[0] - 1.0) < 1e-3, f'mesh {j}: skin {r} {wt}')
            lo = min(lo, p[1])
    _req(-0.05 < lo < 0.05, f'{model}: its lowest point at y {lo:.3f}, not the origin')


def main(argv: list[str]) -> int:
    import mdb
    folder = model_dir()
    if folder is None:
        print(f'no {MODEL_SUBDIR} model folder (obj_model.model_roots: {om.model_roots()})')
        return 1
    out = argv[0] if argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
    os.makedirs(out, exist_ok=True)
    game = mdb._game()
    for model in MODELS:
        arc, info, _md = build_archive(game, folder, model)
        with open(os.path.join(out, archive_name(model)), 'wb') as h:
            h.write(arc)
        print(model, json.dumps(info), f'{len(arc) / 1e6:.1f} MB')
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
