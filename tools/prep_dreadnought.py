"""Developer-only: the Dreadnought's ships (Yager / Six Foot's "Dreadnought", UE 4.14, offline since 2023) out of its
game files into the model folder the installer builds them from (pylib/dreadnought_model.py):

    <models>/dreadnought/<model>.obj / .mtl   one model in game space and metres: +Y up, +Z the bow, +X its left;
                                              one OBJ object per bone (MODELS' bones), every triangle rigid on it
    <models>/dreadnought/<material>_df.png    its colour: the source's grey diffuse x its two paint masks' colours
                                              (its material's own paint) x its ambient occlusion
    <models>/dreadnought/<material>_nm.png    its normal map (the source's, DirectX green as the game's)
    <models>/dreadnought/<material>_rml.png   R roughness, G metal, B light mask (the source's lights: AO's alpha)
    <models>/dreadnought/dreadnought.json     every model's bones (pivot, parent), size and materials
    <models>/dreadnought/SOURCE.txt           where it came from

The source is the folder CUE4Parse wrote the game's meshes and textures into (glb + png, every path as in the game's
paks; D:\\APP\\edf6-assets\\tools\\dnexport, EGame UE4_13 parses its 4.14 packages): each ship a skeletal mesh of one
real bone (root), +Y up, +X the bow, metres / 100 of the game's world.

Size: the Dreadnought's world is scaled down (a dreadnought 19.4 of its metres, its fighters 0.68): the user wants
the original size (2026-10-21「原作多大他就多大」), and the original gives none in metres; so its proportions are
kept with the fighters a fighter's size (17.6 m) and the dreadnought SHIP_LENGTH long.

The hangar module (Abilities/Dreadnought/Hangar, the dreadnoughts' "Fighters" primary) has no socket in the files
(the game places modules by a runtime tag): it is set into the dreadnought's flat foredeck, its rail bays up, so the
fighters stand on the deck (HANGAR_AT). Its six bones (two lifts, two lower and two upper doors) are kept.

    python tools/prep_dreadnought.py [EXPORT_DIR] [--out <models>/dreadnought]
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import obj_model  # noqa: E402

DEFAULT_EXPORT = r'D:\APP\edf6-assets\export'
GENERIC = 'DreadGame/Content/Generic'
SHIP_LENGTH = 500.0        # metres, the dreadnought's bow to stern
TEX = 1024                 # every texture's size
# The source's colour is its paint x its diffuse as a detail overlay (0.5 neutral: x2). The paint is the material's own
# parameters (its colour mask MC's R / G / B pick CustomColor01 / 02 / 03, BaseColor where none), each instance's
# overriding its faction master's (MASTERS: the instance's Parent, read with dnexport -info, as the export's JSON
# names none). Metal and roughness likewise per mask channel.
MASTERS = {'VH_DreadH_': 'VH_YPawn_JupA_MASTERINST', 'AB_DN_Pri_Hangar_': 'VH_YPawn_JupA_MASTERINST'}
MASTER_DIR = 'Ships/_SHARED/Materials'
DEFAULT_BASE = 'BDB8B2'     # a master without BaseColor: its DetailSurfaceColor01 (Jupiter Arms')
AO_DEFAULT = 0.8           # AoInfluence when the material has none


@dataclass(frozen=True)
class Part:
    """One source mesh in a model: its glb, offset in the source's space (its units) before the common scale."""
    glb: str
    at: tuple[float, float, float] = (0.0, 0.0, 0.0)
    bones: tuple[tuple[str, str], ...] = (('root', ''),)   # source bone -> our bone ('' : the model's body bone)


HANGAR_AT = (3.05, 0.94, 0.0)
HANGAR_BONES = (('root', 'hangar'), ('b_elevator_01', 'hangar_lift_f'), ('b_elevator_02', 'hangar_lift_a'),
                ('b_bottom_door01', 'hangar_door_lf'), ('b_bottom_door02', 'hangar_door_la'),
                ('b_upper_door01', 'hangar_door_uf'), ('b_upper_door02', 'hangar_door_ua'))
MODELS: dict[str, tuple[Part, ...]] = {
    'dreadnought': (Part('Ships/Dreadnought/Heavy/VH_DreadH_SK.glb'),
                    Part('Abilities/Dreadnought/Hangar/AB_DN_Pri_Hangar_SK.glb', HANGAR_AT, HANGAR_BONES)),
    'fighter': (Part('Ships/RespawnJet/Fighter/VH_Fighter_SK.glb'),),
    'defender': (Part('Ships/RespawnJet/Defender/DN_Fighter_SK.glb'),),
    'bomber': (Part('Ships/RespawnJet/Bomber/VH_Bomber_SK.glb'),),
    'interceptor': (Part('Ships/RespawnJet/Booster/VH_Interceptor_SK.glb'),),
}
SCALE_FROM = 'dreadnought'     # SHIP_LENGTH is this model's length


# ------------------------------------------------------------------------------------------ the source's glb

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


@dataclass
class Mesh:
    pos: np.ndarray      # (n, 3) source space
    nrm: np.ndarray      # (n, 3)
    uv: np.ndarray       # (n, 2) top-left origin
    bone: np.ndarray     # (n,) each vertex's heaviest joint, as a source bone name index into `names`
    tris: np.ndarray     # (m, 3)
    mat: np.ndarray      # (m,) into `materials`
    materials: list[str]
    names: list[str]     # source bone names
    pivots: dict[str, np.ndarray]


def read_glb(path: str) -> Mesh:
    """CUE4Parse's skinned glb: every primitive with its heaviest joint per vertex (the ships are rigid)."""
    with open(path, 'rb') as h:
        b = h.read()
    jl = struct.unpack_from('<I', b, 12)[0]
    g = json.loads(b[20:20 + jl])
    blob = b[20 + jl + 8:]

    def acc(i: int) -> np.ndarray:
        a = g['accessors'][i]
        v = g['bufferViews'][a['bufferView']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        n = NC[a['type']]
        dt = np.dtype(CT[a['componentType']])
        stride = v.get('byteStride', dt.itemsize * n)
        raw = np.frombuffer(blob, np.uint8, stride * (a['count'] - 1) + dt.itemsize * n, off)
        out = np.lib.stride_tricks.as_strided(raw, (a['count'], dt.itemsize * n), (stride, 1)).copy().view(dt)
        return out.reshape(a['count'], n)

    skin = g['skins'][0] if g.get('skins') else None
    joints = skin['joints'] if skin else []
    names = [g['nodes'][j].get('name', f'joint{j}') for j in joints] or ['root']
    pivots = {g['nodes'][j].get('name', ''): np.array(g['nodes'][j].get('translation', (0.0, 0.0, 0.0)), float)
              for j in joints}
    P, N, U, B, T, M = [], [], [], [], [], []
    base = 0
    for mesh in g['meshes']:
        for pr in mesh['primitives']:
            at = pr['attributes']
            p = acc(at['POSITION']).astype(np.float64)
            P.append(p)
            N.append(acc(at['NORMAL']).astype(np.float64))
            U.append(acc(at['TEXCOORD_0']).astype(np.float64))
            if 'JOINTS_0' in at:
                j, w = acc(at['JOINTS_0']).astype(np.int64), acc(at['WEIGHTS_0']).astype(np.float64)
                B.append(j[np.arange(len(j)), w.argmax(1)])
            else:
                B.append(np.zeros(len(p), np.int64))
            f = acc(pr['indices']).astype(np.int64).reshape(-1, 3)
            T.append(f + base)
            M.append(np.full(len(f), pr.get('material', 0)))
            base += len(p)
    root = g['nodes'][joints[0]] if joints else {}
    if root.get('rotation') is not None:   # the root's rotation: identity up to its sign (w = +-1) in every ship
        x, y, z, w = root['rotation']
        assert abs(abs(w) - 1.0) < 1e-5, f'{path}: root rotation {root["rotation"]}'
    return Mesh(np.vstack(P), np.vstack(N), np.vstack(U), np.concatenate(B), np.vstack(T), np.concatenate(M),
                [m.get('name', f'm{i}') for i, m in enumerate(g.get('materials', []))], names, pivots)


def to_game(p: np.ndarray, scale: float) -> np.ndarray:
    """Source (+X bow, +Y up, +Z port) -> game (+Z bow, +Y up, +X port): a turn about Y, no mirror."""
    return np.stack([-p[:, 2], p[:, 1], p[:, 0]], axis=1) * scale


# ------------------------------------------------------------------------------------------ textures

def material_json(export: str, material: str) -> dict:
    hits = [os.path.join(r, f) for r, _, fs in os.walk(export) for f in fs if f.lower() == material.lower() + '.json']
    if not hits:
        return {}
    with open(hits[0], encoding='utf-8') as h:
        return json.load(h)


def params(export: str, material: str) -> tuple[dict[str, tuple[float, float, float]], dict[str, float]]:
    """The material's colours (sRGB 0..1, from their Hex) and scalars, over its faction master's."""
    colours: dict[str, tuple[float, float, float]] = {}
    scalars: dict[str, float] = {}
    master = next((m for p, m in MASTERS.items() if material.startswith(p)), None)
    chain = ([os.path.join(export, *GENERIC.split('/'), *MASTER_DIR.split('/'), master + '.json')] if master else [])
    docs = []
    for path in chain:
        with open(path, encoding='utf-8') as h:
            docs.append(json.load(h))
    docs.append(material_json(export, material))
    for d in docs:
        for k, v in d.get('Colors', {}).items():
            colours[k] = tuple(int(v['Hex'][i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        scalars.update(d.get('Scalars', {}))
    return colours, scalars


def find_texture(export: str, material: str, slot: tuple[str, ...]) -> str | None:
    """The texture file the source material's JSON names for the first of `slot`'s parameters it has."""
    tex = material_json(export, material).get('Textures', {})
    for key in slot:
        if key in tex:
            rel = tex[key]['ObjectPath'].rsplit('.', 1)[0] + '.png'
            path = os.path.join(export, *rel.split('/'))
            if os.path.isfile(path):
                return path
    return None


def _img(path: str | None, fill: tuple[int, ...], mode: str = 'RGBA') -> np.ndarray:
    if path is None:
        return np.broadcast_to(np.array(fill, np.float64) / 255.0, (TEX, TEX, len(fill))).copy()
    im = Image.open(path).convert(mode)
    # each channel alone: PIL resizes RGBA premultiplied, which zeroes the colour where alpha (here a mask) is 0
    chans = [c.resize((TEX, TEX), Image.LANCZOS) for c in im.split()]
    return np.stack([np.asarray(c, np.float64) for c in chans], axis=-1) / 255.0


def bake(export: str, material: str) -> dict[str, Image.Image]:
    """The material's three textures (see the module doc)."""
    d = _img(find_texture(export, material, ('Albedo', 'PM_Diffuse', 'Diffuse')), (150, 150, 150, 255))
    mc = _img(find_texture(export, material, ('colorMask', 'PM_SpecularMasks')), (0, 0, 0, 255))
    ao = _img(find_texture(export, material, ('AO',)), (255, 255, 255, 0))
    nm = _img(find_texture(export, material, ('Normal', 'PM_Normals')), (128, 128, 255, 255))
    colours, scalars = params(export, material)
    hexrgb = tuple(int(DEFAULT_BASE[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    base = colours.get('BaseColor', colours.get('DetailSurfaceColor01', hexrgb))
    if d[..., :3].mean() < 0.05:   # an "empty" diffuse (CH_DN_Empty_D): no detail
        d = np.full_like(d, 0.5)
    rgb = np.broadcast_to(np.array(base), d[..., :3].shape)
    metal = np.full(d.shape[:2], scalars.get('BaseMetalic', 0.8))
    rough = np.full(d.shape[:2], scalars.get('BaseRoughness01', 0.5))
    for k, ch in enumerate(('Red', 'Green', 'Blue')):
        w = mc[..., k]
        if f'CustomColor0{k + 1}' in colours:
            rgb = rgb + w[..., None] * (np.array(colours[f'CustomColor0{k + 1}']) - rgb)
        metal = metal + w * (scalars.get(f'{ch}Metalic', 0.5) - metal)
        rough = rough + w * (scalars.get(f'{ch}Roughness01', 0.5) - rough)
    ao_w = scalars.get('AoInfluence', AO_DEFAULT)
    rgb = rgb * np.clip(d[..., :3] * 2.0, 0.0, 1.6) * (1.0 - ao_w * (1.0 - ao[..., 1:2]))
    light = ao[..., 3] if ao[..., 3].mean() < 0.5 else np.zeros(d.shape[:2])   # the fighters' AO alpha is all 1: no lights
    rml = np.stack([np.clip(rough, 0, 1), np.clip(metal, 0, 1), light], axis=-1)

    def png(a: np.ndarray) -> Image.Image:
        return Image.fromarray(np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8), 'RGB')
    return {'df': png(rgb), 'nm': png(nm[..., :3]), 'rml': png(rml)}


# ------------------------------------------------------------------------------------------ write

def write_model(name: str, parts: tuple[Part, ...], export: str, scale: float, out: str, done: set[str]) -> dict:
    """<name>.obj / .mtl and its materials' textures; its manifest entry."""
    obj = [f'# {name}: tools/prep_dreadnought.py\n', f'mtllib {name}.mtl\n']
    mtl: list[str] = []
    bones: dict[str, dict] = {name: {'parent': '', 'pivot': [0.0, 0.0, 0.0]}}
    vi = 1
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    materials: list[str] = []
    for part in parts:
        m = read_glb(os.path.join(export, *GENERIC.split('/'), *part.glb.split('/')))
        at = np.array(part.at)
        rename = dict(part.bones)
        for src in sorted(set(m.bone.tolist())):
            sname = m.names[src] if src < len(m.names) else 'root'
            ours = rename.get(sname, rename.get('root', ''))
            bone = ours or name
            if bone not in bones:
                pivot = to_game(((m.pivots.get(sname, np.zeros(3)) + at)[None]), scale)[0]
                bones[bone] = {'parent': name if bone == rename.get('root', bone) else (rename.get('root') or name),
                               'pivot': [round(float(x), 4) for x in pivot]}
            for mi, mat in enumerate(m.materials):
                sel = (m.bone[m.tris[:, 0]] == src) & (m.mat == mi)
                if not sel.any():
                    continue
                tris = m.tris[sel]
                used = np.unique(tris)
                remap = np.zeros(len(m.pos), np.int64)
                remap[used] = np.arange(len(used)) + vi
                p = to_game(m.pos[used] + at, scale)
                n = to_game(m.nrm[used], 1.0)
                lo, hi = np.minimum(lo, p.min(0)), np.maximum(hi, p.max(0))
                obj.append(f'o {bone}\nusemtl {mat}\n')
                obj += [f'v {a:.4f} {b:.4f} {c:.4f}\n' for a, b, c in p]
                obj += [f'vn {a:.4f} {b:.4f} {c:.4f}\n' for a, b, c in n]
                obj += [f'vt {u:.6f} {1.0 - v:.6f}\n' for u, v in m.uv[used]]   # OBJ: bottom-left origin
                obj += [f'f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n' for a, b, c in remap[tris]]
                vi += len(used)
                if mat not in materials:
                    materials.append(mat)
    for mat in materials:
        mtl.append(f'newmtl {mat}\nKd 1 1 1\nmap_Kd {mat}_df.png\nnorm {mat}_nm.png\nmap_Pr {mat}_rml.png\n')
        if mat not in done:
            for k, im in bake(export, mat).items():
                im.save(os.path.join(out, f'{mat}_{k}.png'))
            done.add(mat)
    with open(os.path.join(out, f'{name}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.writelines(obj)
    with open(os.path.join(out, f'{name}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.writelines(mtl)
    return {'bones': bones, 'min': [round(float(x), 3) for x in lo], 'max': [round(float(x), 3) for x in hi],
            'materials': materials}


SOURCE = """Ships from "Dreadnought" (Yager Development / Six Foot, 2017-2023), extracted from its game files
(DreadGame/Content/Generic/Ships, .../Abilities/Dreadnought/Hangar) for the free EDF6VehicleCrew mod.
Not part of the mod's repository; the installer builds the vehicles from this folder on the player's machine.
Made by tools/prep_dreadnought.py (scale: the dreadnought {length:.0f} m bow to stern).
"""


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('export', nargs='?', default=DEFAULT_EXPORT)
    ap.add_argument('--out', default=os.path.join(obj_model.DEV_MODELS, 'dreadnought'))
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    ship = read_glb(os.path.join(a.export, *GENERIC.split('/'), *MODELS[SCALE_FROM][0].glb.split('/')))
    scale = SHIP_LENGTH / float(np.ptp(ship.pos[:, 0]))
    manifest = {'scale': scale, 'models': {}}
    done: set[str] = set()
    for name, parts in MODELS.items():
        manifest['models'][name] = write_model(name, parts, a.export, scale, a.out, done)
        e = manifest['models'][name]
        print(name, 'size', [round(h - l, 1) for l, h in zip(e['min'], e['max'])], 'bones', list(e['bones']))
    with open(os.path.join(a.out, 'dreadnought.json'), 'w', encoding='utf-8', newline='\n') as h:
        json.dump(manifest, h, indent=1)
    with open(os.path.join(a.out, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(SOURCE.format(length=SHIP_LENGTH))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
