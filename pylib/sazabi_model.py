"""EDF6VC_SAZABI.MRAB: the Sazabi (MSN-04) the player pilots (src/sazabi.cpp, docs/sazabi-re.md),
built at install from the model folder tools/prep_sazabi.py makes out of the user's glTF (never in the repository:
CC BY-NC-SA 4.0, see that folder's LICENSE.txt) and the player's own Root.cpk.

The skeleton (SKELETON, preorder; every bone bound level, so the plugin's poses are plain rotations):
  mdl (root: the V506 MAB's locators hang on it, vcobjects.JET_MAB_ROOT) -> sazabi (the skinned object's bone),
  globalSRT -> body (the V506's anchor: its seat, weapons and dead effect, and the bone its ragdoll drives when it
  dies; at BODY_AT, where the V506 CAS's `default` clip puts it; no geometry of its own) -> sz_root (at the floor,
  under body so that the crash's tilt and the wreck carry the whole mech) -> the mech. The V506's CAS animates none of
  the sz_ bones (it names them not), so the plugin's local matrices stay; every bone's world is drawn each frame
  (docs/sazabi-re.md §2). The names it does animate (mdl, globalSRT, body) sit where its `default` clip puts them.
Joints come from the model folder's SKELETON_FILE (tools/prep_sazabi.py BONE_SEGMENTS' heads).

The model: every OBJ object is one bone's rigid pieces; the body in the Blacker's hull material (snd_BRDF, one UV set:
what the drill tank is drawn in, tools/make_drill.py), the three glowing colours each in a copy of Retro-Balam's
`light` material (snd_BRDF_Common_Light_NoOcc: mask x albedo x diffuse x light_color into the emission target,
docs/gundam-plan.md §2). No texture of the source (it has none): every material one cell of a palette
(PALETTE x PALETTE, CELL-square cells, each vertex's UV at its material's cell centre), with matching roughness /
metal maps made from the glTF's factors.

    python pylib/sazabi_model.py [OUT_DIR]     build the archive from the model folder and the game, check, write
"""
from __future__ import annotations

import json
import os
import struct
from dataclasses import dataclass, replace

import obj_model as om
import sazabi_arms
import texfile
from mdb import (Mdb, Object, Rab, RabFile, bind_world, cmpl_compress, insert_member, mdb_read, mdb_write, mmul,
                 rab_read, rab_write)

MODEL_SUBDIR = 'sazabi'
OBJ_FILE = 'sazabi.obj'
MTL_FILE = 'sazabi.mtl'
SKELETON_FILE = 'sazabi_skeleton.json'
OUT_ARC = 'EDF6VC_SAZABI.MRAB'
OUT_MDB = 'edf6vc_sazabi.mdb'
# Canonical size (the user, 2026-10-06: 「按原设身高」): head 23.0 m, overall 25.6 m. The source runs from its soles at
# y = -84 to the funnel packs' top at y = 1108 (1192 units): 25.6 m / 1192.
SOURCE_SOLE_Y = -84.0
SOURCE_SCALE = 25.6 / 1192.0
BODY_AT = (0.0, 1.637, 0.0)      # the V506 CAS's `default` clip: body's position (channel 13)

SKELETON: list[tuple[str, str | None]] = [
    ('mdl', None),
    ('sazabi', 'mdl'),
    ('globalSRT', 'mdl'),
    ('body', 'globalSRT'),
    ('sz_root', 'body'),
    ('sz_pelvis', 'sz_root'),
    ('sz_waist', 'sz_pelvis'),
    ('sz_chest', 'sz_waist'),
    ('sz_head', 'sz_chest'),
    ('sz_cannon', 'sz_chest'),    # the chest mega particle cannon's muzzle (no geometry)
    ('sz_backpack', 'sz_chest'),
    ('sz_funnelpack_l', 'sz_backpack'),
    ('sz_funnel_l1', 'sz_funnelpack_l'),
    ('sz_funnel_l2', 'sz_funnelpack_l'),
    ('sz_funnel_l3', 'sz_funnelpack_l'),
    ('sz_tube_l', 'sz_backpack'),
    ('sz_funnelpack_r', 'sz_backpack'),
    ('sz_funnel_r1', 'sz_funnelpack_r'),
    ('sz_funnel_r2', 'sz_funnelpack_r'),
    ('sz_funnel_r3', 'sz_funnelpack_r'),
    ('sz_tube_r', 'sz_backpack'),
    ('sz_shoulder_l', 'sz_chest'),
    ('sz_upperarm_l', 'sz_chest'),
    ('sz_forearm_l', 'sz_upperarm_l'),
    ('sz_hand_l', 'sz_forearm_l'),
    ('sz_shield', 'sz_forearm_l'),
    ('sz_missile', 'sz_shield'),  # the shield's missile ports (no geometry)
    ('sz_shoulder_r', 'sz_chest'),
    ('sz_upperarm_r', 'sz_chest'),
    ('sz_forearm_r', 'sz_upperarm_r'),
    ('sz_hand_r', 'sz_forearm_r'),
    ('sz_rifle', 'sz_hand_r'),
    ('sz_muzzle', 'sz_rifle'),    # the rifle's muzzle (no geometry): its rounds leave from here
    ('sz_thigh_l', 'sz_pelvis'),
    ('sz_shin_l', 'sz_thigh_l'),
    ('sz_foot_l', 'sz_shin_l'),
    ('sz_thigh_r', 'sz_pelvis'),
    ('sz_shin_r', 'sz_thigh_r'),
    ('sz_foot_r', 'sz_shin_r'),
    ('sz_axe', 'sz_root'),     # the plugin places it in world terms: in the shield, or in the right hand
    ('sz_axe_blade', 'sz_axe'),   # its beam: scaled to nothing while stowed
]
# pylib/sazabi_arms.py models these and sets their joints; the muzzles carry no geometry (the weapons hang on them).
ARM_BONES = ('sz_rifle', 'sz_muzzle', 'sz_shield', 'sz_missile', 'sz_axe', 'sz_axe_blade', 'sz_cannon')
BONE_NAMES = [n for n, _ in SKELETON]
FIXED_JOINTS: dict[str, tuple[float, float, float]] = {
    'mdl': (0.0, 0.0, 0.0), 'sazabi': (0.0, 0.0, 0.0), 'globalSRT': (0.0, 0.0, 0.0), 'body': BODY_AT,
    'sz_root': (0.0, 0.0, 0.0),
}

# Templates (the player's Root.cpk, read only): (archive, model, material).
BODY_HOST = ('V505_TANK.MRAB', 'v505_tank.mdb', 'v505_tank')
LIGHT_HOST = ('V515_RETROBALAM.MRAB', 'v515_retrobalam.mdb', 'light')
BODY_SLOTS = ('albedo', 'normal', 'param_r_m_occ_hr')
LIGHT_SLOTS = ('albedo', 'normal', 'param_r_m_light_hr')
LIGHT_PARAM = 'light_color'
# The palette: the mip levels down to a cell's 1 x 1 (four of them) stay one colour.
PALETTE, CELL = 128, 16
FLAT_NORMAL = (128, 128, 255)
TEX_ALBEDO = 'edf6vc_sazabi_df.dds'
TEX_RMO = 'edf6vc_sazabi_rmo.dds'      # hull shader: R roughness, G metal, B occlusion (255: none; tools/make_drill.py)
TEX_RML = 'edf6vc_sazabi_rml.dds'      # light shader: R roughness, G metal, B light mask (255: lit)
TEX_NORMAL = 'edf6vc_sazabi_nm.dds'
# The source's three emissive colours (their MTL Ke), each a material of its own, so the plugin can drive each one
# (the thrusters with the throttle): its name and light_color strength (stock lights run 1..30; about 3 and over bloom).
GLOW_MATERIALS: dict[str, tuple[str, float]] = {
    '14___Default': ('sz_glow_eye', 6.0),        # the mono-eye (cyan)
    '13___Default': ('sz_glow_thruster', 3.0),   # the thruster throats and the funnels' nozzles (yellow)
    '15___Default': ('sz_glow_lamp', 4.0),       # the chest lamps (red)
    sazabi_arms.BEAM: ('sz_glow_blade', 8.0),    # the beam tomahawk's blade (pink)
}
BODY_MATERIAL = 'sz_body'
OBJECT_NAME = 'sazabi'
MIN_TRIANGLES = 40000      # a model folder from tools/prep_sazabi.py has ~62,500


class SazabiModelError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise SazabiModelError(msg)


def check_skeleton() -> None:
    """SKELETON is a preorder tree under one root with unique names (procmesh.bones walks it by depth)."""
    seen: list[str] = []
    chain: list[str] = []
    for name, parent in SKELETON:
        if name in seen:
            raise ValueError(f'{name}: twice')
        while chain and chain[-1] != parent:
            chain.pop()
        if (parent is None) != (not seen) or (parent is not None and not chain):
            raise ValueError(f'{name}: not in preorder under {parent}')
        chain.append(name)
        seen.append(name)


check_skeleton()


# ------------------------------------------------------------------------------------------ model folder

@dataclass
class Colour:
    """One OBJ material: sRGB albedo (Kd), roughness (Pr), metallic (Pm), emission (Ke, linear; zero: none)."""
    kd: tuple[float, float, float]
    roughness: float
    metallic: float
    ke: tuple[float, float, float]


def read_mtl(path: str) -> dict[str, Colour]:
    """tools/prep_sazabi.py's MTL (Kd, Ke, and the PBR extension's Pr / Pm)."""
    out: dict[str, dict] = {}
    cur: dict | None = None
    with open(path, encoding='utf-8') as h:
        for line in h:
            t = line.split()
            if not t:
                continue
            if t[0] == 'newmtl':
                cur = out.setdefault(t[1], {'kd': (1.0, 1.0, 1.0), 'roughness': 0.6, 'metallic': 0.0, 'ke': (0.0, 0.0, 0.0)})
            elif cur is not None and t[0] in ('Kd', 'Ke'):
                cur[t[0].lower()] = tuple(float(x) for x in t[1:4])
            elif cur is not None and t[0] in ('Pr', 'Pm'):
                cur['roughness' if t[0] == 'Pr' else 'metallic'] = float(t[1])
    return {k: Colour(**v) for k, v in out.items()}


def model_dir() -> str | None:
    return om.model_dir(MODEL_SUBDIR)


def model_files(folder: str) -> list[str]:
    return [os.path.join(folder, f) for f in (OBJ_FILE, MTL_FILE, SKELETON_FILE)]


def skeleton(folder: str) -> list[tuple[str, int, tuple[float, float, float]]]:
    """procmesh Joints (name, parent index, model-space joint) from the folder's SKELETON_FILE, the arms' from
    sazabi_arms.joints."""
    with open(os.path.join(folder, SKELETON_FILE), encoding='utf-8') as h:
        at = json.load(h)
    _req(set(at) == set(BONE_NAMES), f'{SKELETON_FILE}: bones {sorted(set(at) ^ set(BONE_NAMES))} differ from SKELETON')
    pts = {n: (float(v[0]), float(v[1]), float(v[2])) for n, v in at.items()}
    pts.update(sazabi_arms.joints(pts, folder))
    return [(n, BONE_NAMES.index(p) if p else -1, pts[n]) for n, p in SKELETON]


def _byte(x: float) -> int:
    return max(0, min(255, int(round(x * 255.0))))


def palette(colours: dict[str, Colour]) -> tuple[dict[str, tuple[float, float]], dict[str, bytes]]:
    """(each material's UV, the textures {file: DDS}): the albedo palette, the hull shader's roughness / metal /
    occlusion map, the light shader's roughness / metal / light mask (only the glow materials sample it), the flat
    normal map."""
    per_row = PALETTE // CELL
    _req(len(colours) <= per_row * per_row, f'{len(colours)} materials, the palette holds {per_row * per_row}')
    uv: dict[str, tuple[float, float]] = {}
    img = {k: bytearray(PALETTE * PALETTE * 4) for k in ('df', 'rmo', 'rml')}
    for n, (name, c) in enumerate(sorted(colours.items())):
        cx, cy = n % per_row, n // per_row
        uv[name] = ((cx + 0.5) * CELL / PALETTE, (cy + 0.5) * CELL / PALETTE)
        px = {'df': bytes((*(_byte(x) for x in c.kd), 255)),
              'rmo': bytes((_byte(c.roughness), _byte(c.metallic), 255, 255)),
              'rml': bytes((_byte(c.roughness), _byte(c.metallic), 255, 255))}
        for y in range(cy * CELL, (cy + 1) * CELL):
            row = (y * PALETTE + cx * CELL) * 4
            for k, rgba in px.items():
                img[k][row:row + CELL * 4] = rgba * CELL
    tex = {name: texfile.dxt1_dds(texfile.Image(PALETTE, PALETTE, img[k]))
           for name, k in ((TEX_ALBEDO, 'df'), (TEX_RMO, 'rmo'), (TEX_RML, 'rml'))}
    tex[TEX_NORMAL] = texfile.solid_dxt1(FLAT_NORMAL)
    return uv, tex


def parts(folder: str, colours: dict[str, Colour], uv: dict[str, tuple[float, float]]) -> list[tuple[om.Part, int]]:
    """Every (object = bone, material) part of the OBJ with its UVs on its material's palette cell, and its bone."""
    obj = om.read_obj(os.path.join(folder, OBJ_FILE))
    out = []
    for p in om.obj_parts(obj, om.Conversion(flip_v=False)):
        _req(p.name in BONE_NAMES, f'{OBJ_FILE}: object {p.name} is no bone of SKELETON')
        _req(p.material in colours, f'{OBJ_FILE}: material {p.material} not in {MTL_FILE}')
        cell = uv[p.material]
        out.append((replace(p, verts=[replace(v, uv=cell) for v in p.verts]), BONE_NAMES.index(p.name)))
    return out


# ------------------------------------------------------------------------------------------ build

def _member(rab: Rab, name: str) -> RabFile:
    hits = [f for f in rab.files if f.name.lower() == name.lower()]
    _req(len(hits) == 1, f'{name}: {len(hits)} archive members')
    return hits[0]


def _host(game, host: tuple[str, str, str]):  # noqa: ANN001, ANN202 - rootcpk.Game -> (Mdb, mdb.Mesh)
    md = mdb_read(_member(rab_read(game.read('OBJECT', host[0])), host[1]).data)
    mesh = next((me for o in md.objects for me in o.meshes if md.name_of(md.materials[me.material].name) == host[2]), None)
    _req(mesh is not None, f'{host[0]}: no mesh in material {host[2]}')
    return md, mesh


def _with_light(md: Mdb, index: int, strength: float) -> Mdb:
    mats = list(md.materials)
    m = mats[index]
    _req(any(p.name == LIGHT_PARAM for p in m.params), f'{LIGHT_HOST[2]} has no {LIGHT_PARAM}')
    mats[index] = replace(m, params=[replace(p, value=[strength, strength, strength, 0.0]) if p.name == LIGHT_PARAM
                                     else p for p in m.params])
    return replace(md, materials=mats)


def build_model(game, folder: str) -> tuple[Mdb, dict[str, bytes], dict]:  # noqa: ANN001 - rootcpk.Game
    """(model, its textures {file: DDS}, info)."""
    import numpy as np
    import procmesh
    colours = read_mtl(os.path.join(folder, MTL_FILE))
    colours[sazabi_arms.BEAM] = Colour(sazabi_arms.BEAM_KD, 0.5, 0.0, sazabi_arms.BEAM_KD)
    _req(set(GLOW_MATERIALS) <= set(colours), f'{MTL_FILE} lacks the glow materials {sorted(set(GLOW_MATERIALS) - set(colours))}')
    uv, tex = palette(colours)
    joints = skeleton(folder)
    body_md, body_mesh = _host(game, BODY_HOST)
    light_md, light_mesh = _host(game, LIGHT_HOST)
    pieces = parts(folder, colours, uv)
    at = {n: j for n, _, j in joints}
    for p in sazabi_arms.parts(at, folder):
        _req(p.material in colours, f'sazabi_arms: material {p.material} not in the palette')
        pieces.append((replace(p, verts=[replace(v, uv=uv[p.material]) for v in p.verts]), BONE_NAMES.index(p.name)))
    md = Mdb(body_md.version, [n for n, _, _ in joints], [], [], [], [])
    md, body = om.add_material(md, body_md, BODY_HOST[2], BODY_MATERIAL, dict(zip(BODY_SLOTS, (TEX_ALBEDO, TEX_NORMAL, TEX_RMO))))
    groups = [(body, body_mesh, [(p, b) for p, b in pieces if p.material not in GLOW_MATERIALS])]
    for src, (name, strength) in GLOW_MATERIALS.items():
        md, mi = om.add_material(md, light_md, LIGHT_HOST[2], name, dict(zip(LIGHT_SLOTS, (TEX_ALBEDO, TEX_NORMAL, TEX_RML))))
        md = _with_light(md, mi, strength)
        groups.append((mi, light_mesh, [(p, b) for p, b in pieces if p.material == src]))
    meshes = []
    for mi, tmpl, group in groups:
        if group:
            meshes += om.build_meshes(tmpl, [(p, om.rigid(p, b)) for p, b in group], mi)
    meshes = [replace(me, mesh_index=k) for k, me in enumerate(meshes)]
    posed = [(np.array(p.positions()), [list(s) for s in om.rigid(p, b)]) for p, b in pieces]
    bones = procmesh.bones(joints, OBJECT_NAME, posed, 1.0)
    names = list(md.names) + [OBJECT_NAME]
    md = replace(md, names=names, bones=bones, objects=[Object(len(names) - 1, BONE_NAMES.index(OBJECT_NAME), meshes)])
    tris = sum(len(me.indices) // 6 for me in meshes)
    _req(tris >= MIN_TRIANGLES, f'{tris} triangles: the model folder is not tools/prep_sazabi.py\'s')
    info = {'triangles': tris, 'meshes': len(meshes), 'materials': len(md.materials), 'bones': len(bones),
            'per material': {md.name_of(md.materials[i].name): sum(len(me.indices) // 6 for me in meshes if me.material == i)
                             for i in range(len(md.materials))}}
    return md, tex, info


def build_archive(game, folder: str) -> tuple[bytes, dict]:  # noqa: ANN001 - rootcpk.Game
    """EDF6VC_SAZABI.MRAB (the model and its textures, the stock archives' folder layout), info."""
    md, tex, info = build_model(game, folder)
    data = mdb_write(md)
    _req(mdb_write(mdb_read(data)) == data, 'the model does not round-trip')
    template = rab_read(game.read('OBJECT', LIGHT_HOST[0]))
    rab = Rab(template.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [])
    insert_member(rab, RabFile(OUT_MDB, rab.folders.index('MODEL'), 0, cmpl_compress(data)))
    for name, dds in tex.items():
        om.add_texture(rab, name, dds)
    out = rab_write(rab)
    check_archive(out)
    return out, info


# ------------------------------------------------------------------------------------------ check

def check_archive(arc: bytes) -> None:
    """Re-read `arc`: archive and model round-trip; the bone names SKELETON's; one object on the kind-2 bone
    OBJECT_NAME; every texture member in place (obj_model.texture_problems); bind x inverse bind the identity; every
    vertex 100 % on one sz_ skin bone; the soles at the floor."""
    import graft_pure as g
    rab = rab_read(arc)
    _req(rab_write(rab) == arc, 'archive does not round-trip')
    data = _member(rab, OUT_MDB).data
    md = mdb_read(data)
    _req(mdb_write(md) == data, 'model does not round-trip')
    _req([md.name_of(b.name) for b in md.bones] == BONE_NAMES, 'bone names are not SKELETON\'s')
    bad = om.texture_problems(rab, md, OUT_MDB)
    _req(not bad, 'textures: ' + '; '.join(bad))
    _req(len(md.objects) == 1 and md.bones[md.objects[0].bone].kind == 2, 'not one object on a kind-2 bone')
    w = bind_world(md)
    for b in md.bones:
        p = mmul(w[b.index], b.inv_bind)
        err = max(abs(p[k] - (1.0 if k in (0, 5, 10, 15) else 0.0)) for k in range(16))
        _req(err < 1e-4, f'bone {md.name_of(b.name)}: bind x inverse bind off identity by {err}')
    lo = 1e9
    for j, me in enumerate(md.objects[0].meshes):
        idx = struct.unpack(f'<{len(me.indices) // 2}H', me.indices)
        _req(bool(idx) and len(idx) % 3 == 0 and max(idx) < me.nverts < 0x10000, f'mesh {j}: index buffer')
        bi, bw = g.skin_columns(me)
        for p, r, wt in zip(g.mesh_positions(me), bi, bw):
            bone = int(r[0])
            _req(md.name_of(md.bones[bone].name).startswith('sz_') and md.bones[bone].kind == 3
                 and abs(wt[0] - 1.0) < 1e-3, f'mesh {j}: skin {r} {wt}')
            lo = min(lo, p[1])
    _req(-0.05 < lo < 0.05, f'the soles at y {lo:.3f}, not the floor')


def main(argv: list[str]) -> int:
    import mdb
    folder = model_dir()
    if folder is None:
        print(f'no {MODEL_SUBDIR} model folder (obj_model.model_roots: {om.model_roots()})')
        return 1
    out = argv[0] if argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
    os.makedirs(out, exist_ok=True)
    game = mdb._game()
    arc, info = build_archive(game, folder)
    with open(os.path.join(out, OUT_ARC), 'wb') as h:
        h.write(arc)
    print(json.dumps(info, indent=1))
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
