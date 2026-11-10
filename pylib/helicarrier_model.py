"""The support air carrier's model (docs/feature-support-jet-models.md §6): the Sketchfab "Helicarrier" (CC BY 4.0; never in
the repository), an archive EDF6VC_HELICARRIER.MRAB built at install from the model folder tools/prep_helicarrier.py
makes and the player's own Root.cpk.

The folder holds helicarrier.obj / .mtl in game space and metres (+Y up, +Z the bow, +X its left, standing on its lowest
point, centred; one OBJ object per bone, every triangle rigid on it, the bones' pivots, the fans' and their exhausts in
helicarrier.json) and its colour (helicarrier_df.png), normal map (_nm) and R roughness / G metal / B 0 map (_rml).

The skeleton (preorder; the frame the C-130's: c130_model.py):
  mdl (root: the V506 MAB's locators hang on it, vcobjects.JET_MAB_ROOT) -> helicarrier (the skinned object's bone),
  globalSRT -> body (the V506's anchor, at c130_model.BODY_AT; no geometry) -> hc_hull (the hull, at the origin) ->
  boosterF_l, boosterF_r, boosterB_l, boosterB_r (THRUSTERS: the four lift fans, each at its hub, bound level: the
  names src/jet_flight.cpp Thrusters tilts, kThrusterNames; F the bow pair, _l the left, +x) -> nozzle_<i> (one under
  each fan: jet_models.with_nozzles, its flames and arrival smoke, src/booster.cpp).
Each fan is written turned upright (tools/prep_helicarrier.py: its disc across the bow, its downwash along -z), the way
the plugin's "level" is: hovering it tilts the fan -90 deg about its x, flat in its ring as the source has it; its
flame then points down. Materials: the V505's hull material with the folder's maps, as the C-130's.

    python pylib/helicarrier_model.py [OUT_DIR]     build the archive from the model folder and the game, check, write
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import replace

import c130_model
import obj_model as om
import texfile
import texfile_accel
from mdb import Mdb, Object, Rab, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, mmul, rab_read, \
    rab_write

MODEL_SUBDIR = 'helicarrier'
MANIFEST = 'helicarrier.json'
MODEL = 'helicarrier'
ARCHIVE = 'EDF6VC_HELICARRIER.MRAB'
MDB = 'edf6vc_helicarrier.mdb'
HULL = 'hc_hull'
THRUSTERS = ('boosterF_l', 'boosterF_r', 'boosterB_l', 'boosterB_r')   # src/jet_flight.cpp kThrusterNames, in its order
SKELETON_BONES = 5 + 2 * len(THRUSTERS)   # mdl, the model's, globalSRT, body, the hull, the fans and their nozzles
MIN_TRIANGLES = 12000
# The size tools/prep_helicarrier.py gives it (m: span, height, length), within SIZE_SLACK.
SIZE = (45.96, 16.39, 80.0)
SIZE_SLACK = 0.3


class HelicarrierModelError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise HelicarrierModelError(msg)


def model_dir() -> str | None:
    return om.model_dir(MODEL_SUBDIR)


def read_manifest(folder: str) -> dict:
    with open(os.path.join(folder, MANIFEST), encoding='utf-8') as h:
        m = json.load(h)
    entry = m.get('models', {}).get(MODEL)
    _req(entry is not None, f'{MANIFEST}: no model {MODEL}')
    _req(set(entry['bones']) == {MODEL, *THRUSTERS} and set(entry.get('nozzles', {})) == set(THRUSTERS),
         f'{MANIFEST}: bones {sorted(entry["bones"])}, nozzles {sorted(entry.get("nozzles", {}))}')
    return m


def model_files(folder: str) -> list[str]:
    """Every file of the folder the archive is made from (the installer's cache hashes them)."""
    names = [MANIFEST, f'{MODEL}.obj', f'{MODEL}.mtl'] + [f'{MODEL}_{x}.png' for x in ('df', 'nm', 'rml')]
    return [os.path.join(folder, n) for n in names]


def _ours(bone: str) -> str:
    return HULL if bone == MODEL else bone


def skeleton(manifest: dict) -> list[tuple[str, str | None]]:
    """(bone, parent) in preorder: the V506's frame, then the hull and its fans (nozzles: with_nozzles adds them)."""
    out: list[tuple[str, str | None]] = [('mdl', None), (MODEL, 'mdl'), ('globalSRT', 'mdl'), ('body', 'globalSRT'),
                                         (HULL, 'body')]
    bones = manifest['models'][MODEL]['bones']
    for b in THRUSTERS:
        _req(bones[b]['parent'] == MODEL, f'{b}: parent {bones[b]["parent"]}, not the hull')
        out.append((b, HULL))
    return out


def _textures(folder: str, made: bool = True) -> tuple[dict[str, bytes], dict[str, str]]:
    """({archive file: DDS}, {slot: file}): its colour, normal map, roughness / metal (B 255: no occlusion). Not `made`:
    the files named, empty (the model only measured: jet_models._model_of)."""
    stem = f'edf6vc_{MODEL}'
    names = [f'{stem}_df.dds', f'{stem}_nm.dds', f'{stem}_rm.dds']
    if not made:
        return dict.fromkeys(names, b''), dict(zip(c130_model.BODY_SLOTS, names))
    rml = texfile.load_image(os.path.join(folder, f'{MODEL}_rml.png'))
    px = bytearray(rml.rgba)
    px[2::4] = b'\xff' * (len(px) // 4)
    rml = texfile.Image(rml.width, rml.height, px)
    files = dict(zip(names, (texfile_accel.dxt1_dds(texfile.load_image(os.path.join(folder, f'{MODEL}_df.png'))),
                             texfile_accel.bc5_normal_file(os.path.join(folder, f'{MODEL}_nm.png')),
                             texfile_accel.dxt1_dds(rml))))
    return files, dict(zip(c130_model.BODY_SLOTS, names))


def nozzles(manifest: dict, shift: tuple[float, float, float] = (0.0, 0.0, 0.0)) -> list:
    """The fans' nozzles (jet_models.NozzleBone, THRUSTERS' order): each under its fan at its exhaust point (the hub's
    back, tools/prep_helicarrier.py), the flame along -z of the bind (out of the upright fan's back: down when the plugin
    lays the fan flat), as wide as the fan's disc and FLAME_PER_DIAMETER of it long."""
    import jet_models
    rows = manifest['models'][MODEL]['nozzles']
    return [jet_models.NozzleBone(b, jet_models.turned_at(tuple(rows[b]['at'][k] + shift[k] for k in range(3))),
                                  float(rows[b]['length']), float(rows[b]['width'])) for b in THRUSTERS]


def build_model(game, folder: str, textures: bool = True) -> tuple[Mdb, dict[str, bytes], dict]:  # noqa: ANN001
    """(model, its textures {file: DDS}, info): `game` a rootcpk.Game. Without `textures` the textures are named, not made
    (empty): the model alone, as jet_models measures it. Its nozzle bones on (jet_models.with_nozzles)."""
    import numpy as np
    import jet_models
    import procmesh
    manifest = read_manifest(folder)
    entry = manifest['models'][MODEL]
    bones = skeleton(manifest)
    names = [n for n, _ in bones]
    obj = om.read_obj(os.path.join(folder, f'{MODEL}.obj'))
    parts = om.obj_parts(obj, om.Conversion(flip_v=True))
    lo = min(v.pos[1] for p in parts for v in p.verts)
    zs = [v.pos[2] for p in parts for v in p.verts]
    # stand it on its lowest point, its z centred; its x as written (centred on its fans: the hull's symmetry plane)
    shift = (0.0, -lo, -(min(zs) + max(zs)) / 2.0)

    def moved(p):  # noqa: ANN001, ANN202 - om.Part
        return replace(p, verts=[replace(v, pos=tuple(v.pos[k] + shift[k] for k in range(3))) for v in p.verts])
    pieces = []
    for p in parts:
        b = _ours(p.name)
        _req(b in names, f'{MODEL}.obj: object {p.name} is no bone of the skeleton')
        pieces.append((moved(p), names.index(b)))
    pivots = {'mdl': (0.0, 0.0, 0.0), MODEL: (0.0, 0.0, 0.0), 'globalSRT': (0.0, 0.0, 0.0), 'body': c130_model.BODY_AT,
              HULL: (0.0, 0.0, 0.0)}
    for b in THRUSTERS:
        pivots[b] = tuple(entry['bones'][b]['pivot'][k] + shift[k] for k in range(3))
    joints = [(n, names.index(p) if p else -1, pivots[n]) for n, p in bones]
    host_md, host_mesh = c130_model._host(game)
    md = Mdb(host_md.version, names, [], [], [], [])
    files, slots = _textures(folder, textures)
    md, mi = om.add_material(md, host_md, c130_model.BODY_HOST[2], f'hc_{MODEL}', slots)
    # the whole model's reach (one material, one call): 80 m long, its positions float (obj_model.position_layout)
    meshes = om.build_meshes(host_mesh, [(p, om.rigid(p, b)) for p, b in pieces], mi,
                             extent=om.reach(p for p, _b in pieces))
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    posed = [(np.array(p.positions()), [list(s) for s in om.rigid(p, b)]) for p, b in pieces]
    skel = procmesh.bones(joints, MODEL, posed, 1.0)
    all_names = list(md.names) + [MODEL] if MODEL not in md.names else list(md.names)
    md = replace(md, names=all_names, bones=skel, objects=[Object(all_names.index(MODEL), names.index(MODEL), meshes)])
    md = jet_models.with_nozzles(md, nozzles(manifest, shift))
    tris = sum(len(me.indices) // 6 for me in meshes)
    _req(tris >= MIN_TRIANGLES, f'{tris} triangles: the model folder is not tools/prep_helicarrier.py\'s')
    size = np.ptp(np.vstack([q for q, _ in posed]), axis=0)
    _req(all(abs(size[k] - SIZE[k]) < SIZE_SLACK for k in range(3)), f'{[round(float(x), 2) for x in size]} m, not {SIZE}')
    info = {'triangles': tris, 'meshes': len(meshes), 'materials': len(md.materials), 'bones': len(md.bones),
            'size': [round(float(x), 2) for x in size]}
    return md, files, info


def build_archive(game) -> tuple[bytes, dict, Mdb]:  # noqa: ANN001 - rootcpk.Game
    """EDF6VC_HELICARRIER.MRAB (the model and its textures, the stock archives' folder layout), info, the model."""
    folder = model_dir()
    _req(folder is not None, f'no {MODEL_SUBDIR} model folder')
    md, tex, info = build_model(game, folder)
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, 'the model does not round-trip')
    template = rab_read(game.read('OBJECT', c130_model.BODY_HOST[0]))
    rab = Rab(template.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    insert_member(rab, RabFile(MDB, rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    for name, dds in tex.items():
        om.add_texture(rab, name, dds)
    out = rab_write(rab)
    check_archive(out, nozzles(read_manifest(folder)))
    return out, info, md


def check_archive(arc: bytes, want_nozzles: list) -> None:
    """Re-read `arc`: archive and model round-trip; the V506 frame's bones first, the hull, the four fans on it, each with
    its nozzle (jet_models.check_nozzle_bones: `want_nozzles`, as measured); one object on the kind-2 bone `helicarrier`;
    every texture member in place; bind x inverse bind the identity; every vertex 100 % on the hull or a fan; the model
    standing on its origin; every fan's bind level (identity turn: the plugin's Rx(theta) x bind is about the hull's x).
    The seat's cockpit tools/make_helicarrier.py grafts in (pylib/cockpits.py: its marker bones, its kit's materials) is
    left out of the bones' and the meshes' checks."""
    import cockpits
    import graft_pure as g
    import jet_models
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = c130_model._member(rab, MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    names = [md.name_of(b.name) for b in md.bones]
    _req(names[:5] == ['mdl', MODEL, 'globalSRT', 'body', HULL], f'bones {names[:6]}')
    _req(len([n for n in names if not cockpits.grafted_bone(n)]) == SKELETON_BONES, f'{len(names)} bones {names}')
    w = bind_world(md)
    for b in THRUSTERS:
        k = names.index(b)
        _req(md.bones[k].parent == names.index(HULL) and md.bones[k].kind == 3, f'{b}: parent / kind')
        turn = [w[k][r * 4 + c] for r in range(3) for c in range(3)]
        _req(max(abs(t - (1.0 if i in (0, 4, 8) else 0.0)) for i, t in enumerate(turn)) < 1e-5, f'{b}: bound turned {turn}')
    jet_models.check_nozzle_bones(md, want_nozzles)
    bad = om.texture_problems(rab, md, MDB)
    _req(not bad, 'textures: ' + '; '.join(bad))
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'not one object on a kind-2 bone')
    for b in md.bones:
        if cockpits.grafted_bone(md.name_of(b.name)):
            continue
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    skin = {HULL, *THRUSTERS}
    lo = 1e9
    on = dict.fromkeys(skin, 0)
    for j, me in enumerate(md.objects[0].meshes):
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(bool(idx) and len(idx) % 3 == 0 and max(idx) < me.nverts < 0x10000, f'mesh {j}: index buffer')
        if cockpits.grafted_material(md.name_of(md.materials[me.material].name)):
            continue
        bi, bw = g.skin_columns(me)
        for p, r, wt in zip(g.mesh_positions(me), bi, bw):
            own = cockpits.own_bone(md, int(r[0]))   # a cockpit shell region's child stands for its bone
            _req(own in skin and md.bones[names.index(own)].kind == 3 and abs(wt[0] - 1.0) < 1e-3, f'mesh {j}: skin {r} {wt}')
            on[own] += 1
            lo = min(lo, p[1])
    _req(all(on.values()), f'a skin bone without vertices: {on}')
    _req(-0.05 < lo < 0.05, f'its lowest point at y {lo:.3f}, not the origin')


def main(argv: list[str]) -> int:
    import mdb
    if model_dir() is None:
        print(f'no {MODEL_SUBDIR} model folder (obj_model.model_roots: {om.model_roots()})')
        return 1
    out = argv[0] if argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
    os.makedirs(out, exist_ok=True)
    arc, info, _md = build_archive(mdb._game())
    with open(os.path.join(out, ARCHIVE), 'wb') as h:
        h.write(arc)
    print(MODEL, json.dumps(info), f'{len(arc) / 1e6:.1f} MB')
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
