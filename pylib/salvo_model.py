"""The Arsenal Bird's models (武库鸟, docs/feature-support-jet-models.md §6.6): the airframe and its M101 drone out of the ACE
COMBAT 7 mod "Arsenal Bird (AIAS)" (never in the repository), two archives built at install from the model folder
tools/prep_arsenal_bird.py makes and the player's own Root.cpk:

  EDF6VC_SALVO_INTERCEPTOR.MRAB   the airframe (MODELS['arsenal_bird']): one model, two uses, the missile mode (the
                                  salvo sortie, tools/calls.py sortie_missile_rain: vcobjects.SALVO_JET) and the drone
                                  mode (the carrier launching M101s: vcobjects.ARSENAL_CARRIER_JET)
  EDF6VC_M101.MRAB                the M101 the drone mode launches (MODELS['m101']: vcobjects.M101_JET)

The folder holds <model>.obj / .mtl in game space and metres (+Y up, +Z the nose, +X its left, standing on its lowest
point, centred; one OBJ object, the hull, one material), each material's colour (<material>_df.png), normal map (_nm)
and R roughness / G metal / B 0 map (_rml), and arsenal_bird.json (each model's bone, material and size).

The skeleton (preorder; the frame the C-130's: c130_model.py), for each:
  mdl (root: the V506 MAB's locators hang on it, vcobjects.JET_MAB_ROOT) -> <model> (the skinned object's bone),
  globalSRT -> body (the V506's anchor, at c130_model.BODY_AT; no geometry) -> <hull> (the airframe, at the origin) ->
  hp_gun, hp_store (jet_hardpoints.with_hardpoints: placed on its belly for the stock muzzles, as on every jet model
  vcobjects.HARDPOINT_FILES names).
No nozzle bones: the Arsenal Bird flies on propellers and the M101's exhaust is not modelled, and the plugin draws no
flame for a model without them (src/booster.cpp ExhaustOf).
The salvo's 256 missiles leave from the airframe's hp_store at the stock missile pod's four muzzle offsets
(vcobjects.hardpoint_muzzles: the same for every jet, so not this model's to change): the bone's place on the belly
decides where they appear. There is no launcher mesh: the missiles' own glow is the effect.
Materials: the V505's hull material with the folder's maps, as the C-130's.

    python pylib/salvo_model.py [OUT_DIR]     build both archives from the model folder and the game, check, write
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import dataclass, replace

import c130_model
import obj_model as om
import texfile
import texfile_accel
from mdb import Mdb, Object, Rab, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, mmul, rab_read, \
    rab_write

MODEL_SUBDIR = 'arsenal_bird'
MANIFEST = 'arsenal_bird.json'


@dataclass(frozen=True)
class Spec:
    archive: str                # its file in Mods/OBJECT
    mdb: str                    # its model member
    hull: str                   # its one rigid bone
    min_triangles: int          # fewer: the folder is not tools/prep_arsenal_bird.py's
    size: tuple[float, float, float]   # m: span, height, length as tools/prep_arsenal_bird.py makes it (within SIZE_SLACK)


# The archives' names are the jets' (vcobjects.SALVO_MODEL, M101_MODEL; jet_models.SALVO_FILE, M101_FILE).
MODELS = {
    'arsenal_bird': Spec('EDF6VC_SALVO_INTERCEPTOR.MRAB', 'edf6vc_salvo_interceptor.mdb', 'ab_hull', 110000,
                         (106.56, 14.88, 22.83)),
    'm101': Spec('EDF6VC_M101.MRAB', 'edf6vc_m101.mdb', 'm101_hull', 4300, (3.24, 1.53, 5.70)),
}
ARCHIVE = MODELS['arsenal_bird'].archive
SKELETON_BONES = 5 + 2           # mdl, the model's, globalSRT, body, the hull, hp_gun / hp_store
SIZE_SLACK = 0.05


class SalvoModelError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise SalvoModelError(msg)


def model_dir() -> str | None:
    return om.model_dir(MODEL_SUBDIR)


def read_manifest(folder: str) -> dict:
    with open(os.path.join(folder, MANIFEST), encoding='utf-8') as h:
        m = json.load(h)
    for name in MODELS:
        entry = m.get('models', {}).get(name)
        _req(entry is not None, f'{MANIFEST}: no model {name}')
        _req(set(entry['bones']) == {name} and not entry.get('nozzles') and entry.get('materials'),
             f'{MANIFEST}: {name}: bones {sorted(entry["bones"])}, nozzles {entry.get("nozzles")}')
    return m


def model_files(folder: str) -> list[str]:
    """Every file of the folder the archives are made from (the installer's cache hashes them)."""
    models = read_manifest(folder)['models']
    names = [MANIFEST]
    for name in MODELS:
        names += [f'{name}.obj', f'{name}.mtl'] + [f'{m}_{x}.png' for m in models[name]['materials']
                                                   for x in ('df', 'nm', 'rml')]
    return [os.path.join(folder, n) for n in names]


def skeleton(name: str) -> list[tuple[str, str | None]]:
    """(bone, parent) in preorder: the V506's frame, then the hull (hardpoints: added after)."""
    return [('mdl', None), (name, 'mdl'), ('globalSRT', 'mdl'), ('body', 'globalSRT'), (MODELS[name].hull, 'body')]


def _textures(folder: str, name: str, material: str, made: bool = True) -> tuple[dict[str, bytes], dict[str, str]]:
    """({archive file: DDS}, {slot: file}) of one material: its colour (BC7), normal map (BC5), roughness / metal (DXT1, B
    255: no occlusion). Not `made`: the files named, empty (the model only measured: jet_models._model_of)."""
    stem = f'edf6vc_{name}_{material}'
    names = [f'{stem}_df.dds', f'{stem}_nm.dds', f'{stem}_rm.dds']
    if not made:
        return dict.fromkeys(names, b''), dict(zip(c130_model.BODY_SLOTS, names))
    rml = texfile.load_image(os.path.join(folder, f'{material}_rml.png'))
    px = bytearray(rml.rgba)
    px[2::4] = b'\xff' * (len(px) // 4)
    rml = texfile.Image(rml.width, rml.height, px)
    files = dict(zip(names, (texfile_accel.bc7_dds(texfile.load_image(os.path.join(folder, f'{material}_df.png'))),
                             texfile_accel.bc5_normal_file(os.path.join(folder, f'{material}_nm.png')),
                             texfile_accel.dxt1_dds(rml))))
    return files, dict(zip(c130_model.BODY_SLOTS, names))


def build_model(game, folder: str, textures: bool = True, name: str = 'arsenal_bird') -> tuple[Mdb, dict[str, bytes], dict]:  # noqa: ANN001
    """(model `name`, its textures {file: DDS}, info; info['shift'] what moved the folder's model onto its origin): `game`
    a rootcpk.Game. Without `textures` the textures are named, not made (empty): the model alone, as jet_models measures
    it. Its hardpoints on."""
    import numpy as np
    import jet_hardpoints
    import jet_models
    import procmesh
    spec = MODELS[name]
    entry = read_manifest(folder)['models'][name]
    bones = skeleton(name)
    names = [n for n, _ in bones]
    obj = om.read_obj(os.path.join(folder, f'{name}.obj'))
    parts = om.obj_parts(obj, om.Conversion(flip_v=True))
    lo = min(v.pos[1] for p in parts for v in p.verts)
    xs = [v.pos[0] for p in parts for v in p.verts]
    zs = [v.pos[2] for p in parts for v in p.verts]
    shift = (-(min(xs) + max(xs)) / 2.0, -lo, -(min(zs) + max(zs)) / 2.0)   # stand it on its lowest point, centred

    def moved(p):  # noqa: ANN001, ANN202 - om.Part
        return replace(p, verts=[replace(v, pos=tuple(v.pos[k] + shift[k] for k in range(3))) for v in p.verts])
    _req(all(p.name == name for p in parts), f'{name}.obj: objects {sorted({p.name for p in parts})}, not one {name}')
    hull = names.index(spec.hull)
    pieces = [(moved(p), hull) for p in parts]
    pivots = {'mdl': (0.0, 0.0, 0.0), name: (0.0, 0.0, 0.0), 'globalSRT': (0.0, 0.0, 0.0), 'body': c130_model.BODY_AT,
              spec.hull: (0.0, 0.0, 0.0)}
    joints = [(n, names.index(p) if p else -1, pivots[n]) for n, p in bones]
    host_md, host_mesh = c130_model._host(game)
    md = Mdb(host_md.version, names, [], [], [], [])
    tex: dict[str, bytes] = {}
    meshes = []
    extent = om.reach(p for p, _b in pieces)     # the whole model's: its materials store positions alike
    for material in entry['materials']:
        group = [(p, b) for p, b in pieces if p.material == material]
        _req(bool(group), f'{name}.obj: no faces in material {material}')
        files, slots = _textures(folder, name, material, textures)
        tex.update(files)
        md, mi = om.add_material(md, host_md, c130_model.BODY_HOST[2], f'{name}_{material}', slots)
        meshes += om.build_meshes(host_mesh, [(p, om.rigid(p, b)) for p, b in group], mi, extent=extent)
    _req({p.material for p, _b in pieces} <= set(entry['materials']), f'{name}.obj: a material not in {MANIFEST}')
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    posed = [(np.array(p.positions()), [list(s) for s in om.rigid(p, b)]) for p, b in pieces]
    skel = procmesh.bones(joints, name, posed, 1.0)
    all_names = list(md.names) + [name] if name not in md.names else list(md.names)
    md = replace(md, names=all_names, bones=skel, objects=[Object(all_names.index(name), names.index(name), meshes)])
    md = jet_hardpoints.with_hardpoints(md, jet_models.hardpoints_for(game, md, spec.archive))
    tris = sum(len(me.indices) // 6 for me in meshes)
    _req(tris >= spec.min_triangles, f'{name}: {tris} triangles: the model folder is not tools/prep_arsenal_bird.py\'s')
    size = np.ptp(np.vstack([q for q, _ in posed]), axis=0)
    _req(all(abs(size[k] - spec.size[k]) < SIZE_SLACK * max(1.0, spec.size[k] / 10.0) for k in range(3)),
         f'{name}: {[round(float(x), 2) for x in size]} m, not {spec.size}')
    info = {'triangles': tris, 'meshes': len(meshes), 'materials': len(md.materials), 'bones': len(md.bones),
            'size': [round(float(x), 2) for x in size], 'shift': list(shift)}
    return md, tex, info


def build_archive(game, name: str = 'arsenal_bird') -> tuple[bytes, dict, Mdb]:  # noqa: ANN001 - rootcpk.Game
    """MODELS[name]'s archive (the model and its textures, the stock archives' folder layout), info, the model."""
    folder = model_dir()
    _req(folder is not None, f'no {MODEL_SUBDIR} model folder')
    spec = MODELS[name]
    md, tex, info = build_model(game, folder, name=name)
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, 'the model does not round-trip')
    template = rab_read(game.read('OBJECT', c130_model.BODY_HOST[0]))
    rab = Rab(template.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    insert_member(rab, RabFile(spec.mdb, rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    for file, dds in tex.items():
        om.add_texture(rab, file, dds)
    out = rab_write(rab)
    check_archive(out, name)
    return out, info, md


def check_archive(arc: bytes, name: str = 'arsenal_bird') -> None:
    """Re-read `arc`: archive and model round-trip; the V506 frame's bones first, then the hull; no nozzle bones; its
    hardpoints (hp_gun, hp_store: transform-only bones on the hull); one object on the kind-2 bone `name`; every texture
    member in place; bind x inverse bind the identity; every vertex 100 % on the hull; the model standing on its origin.
    A cockpit grafted in later (pylib/cockpits.py: its marker bones, its kit's materials) is left out of the bones' and the
    meshes' checks."""
    import cockpits
    import graft_pure as g
    import jet_hardpoints
    import jet_models
    spec = MODELS[name]
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = c130_model._member(rab, spec.mdb).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    names = [md.name_of(b.name) for b in md.bones]
    _req(names[:5] == ['mdl', name, 'globalSRT', 'body', spec.hull], f'bones {names[:6]}')
    _req(len([n for n in names if not cockpits.grafted_bone(n)]) == SKELETON_BONES, f'{len(names)} bones {names}')
    _req(not any(n.startswith(jet_models.NOZZLE_BONE) for n in names), f'{name}: nozzle bones {names}')
    hull = names.index(spec.hull)
    for b in jet_hardpoints.HARDPOINT_BONES:
        _req(b in names and md.bones[names.index(b)].parent == hull and md.bones[names.index(b)].kind == 0,
             f'{b}: not a transform-only bone on {spec.hull}')
    bad = om.texture_problems(rab, md, spec.mdb)
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
    count = 0
    for j, me in enumerate(md.objects[0].meshes):
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(bool(idx) and len(idx) % 3 == 0 and max(idx) < me.nverts < 0x10000, f'mesh {j}: index buffer')
        if cockpits.grafted_material(md.name_of(md.materials[me.material].name)):
            continue
        bi, bw = g.skin_columns(me)
        for p, r, wt in zip(g.mesh_positions(me), bi, bw):
            own = cockpits.own_bone(md, int(r[0]))   # a cockpit shell region's child stands for its bone
            _req(own == spec.hull and abs(wt[0] - 1.0) < 1e-3, f'mesh {j}: skin {r} {wt}')
            count += 1
            lo = min(lo, p[1])
    _req(count > 0, 'no vertices on the hull')
    _req(-0.05 < lo < 0.05, f'its lowest point at y {lo:.3f}, not the origin')


def main(argv: list[str]) -> int:
    import mdb
    if model_dir() is None:
        print(f'no {MODEL_SUBDIR} model folder (obj_model.model_roots: {om.model_roots()})')
        return 1
    out = argv[0] if argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
    os.makedirs(out, exist_ok=True)
    game = mdb._game()
    for name, spec in MODELS.items():
        arc, info, md = build_archive(game, name)
        with open(os.path.join(out, spec.archive), 'wb') as h:
            h.write(arc)
        hps = {md.name_of(b.name): [round(x, 2) for x in bind_world(md)[b.index][12:15]] for b in md.bones
               if md.name_of(b.name).startswith('hp_')}
        print(name, json.dumps(info), 'hardpoints', json.dumps(hps), f'{len(arc) / 1e6:.1f} MB')
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
