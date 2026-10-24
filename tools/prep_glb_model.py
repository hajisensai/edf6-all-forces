"""Developer-only: Sketchfab glTF (.glb) downloads into the model folders the installer builds the emplacements from
(pylib/emplacement_model.py: a turret, its guns and its own base on the fortified base it adds):

    <models>/<folder>/
        model.npz      per part (one role, one material): positions (game space, metres), normals, uvs (top-left
                       origin), triangles
        model.json     the parts (role, material), the materials (texture files), the pivots (the turret's turning
                       axis, the guns' trunnion, their mouths, a rotor's axis), the source and its licence
        textures/      each material's base colour, at most MAX_TEXTURE pixels a side
        LICENSE.txt    the attribution the licence asks for

    python tools/prep_glb_model.py <kind> <model.glb> [--out <models>/<folder>] [--license <license.json>]

Each kind is a Model below: which source nodes (their path of node names from the scene's root, matched by regex) are
the turret (it turns), the guns (they lift; a rotor's barrels spin as well), the base (it stays), what the model's
front is (yaw: its forward axis turned onto +z), its size, and the points the source does not mark. Game space: +y up,
+z forward, +x the left (glTF's own axes for a model that faces +z), metres, the base's foot at y = 0.
The source is treated as data: nothing in it runs. It needs numpy and Pillow (the installer only reads the result).
"""
from __future__ import annotations

import argparse
import io
import json
import os
import re
import shutil
import struct
import sys
from dataclasses import dataclass, field

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

import gltf  # noqa: E402

FORMAT = 1
MAX_TEXTURE = 1024
ROLES = ('base', 'turret', 'gun', 'rotor')


@dataclass
class Model:
    kind: str
    folder: str
    title: str
    rules: list[tuple[str, str]]          # (node path regex, role), first match wins; no match: base
    yaw: float = 0.0                      # deg about +y taking the model's front onto +z
    height: float | None = None           # m: the whole model's height (its scale), or
    length: float | None = None           # m: its length along z, or
    width: float | None = None            # m: its width along x
    # The points (game space, after the scale; None: measured): the turret's turning axis (x, z) and the guns'
    # trunnion (y, z) default to the turret's / the guns' box centres; the mouths to the guns' front, at `muzzle_x`
    # (one barrel: both sides the same) or the two barrels' tips.
    ring: tuple[float, float] | None = None
    trunnion: tuple[float, float] | None = None
    muzzle_x: tuple[float, float] | None = None
    rotor_axis: tuple[float, float] | None = None   # (x, y) of the spinning barrels' axis (default: their box centre)
    # The source nodes whose own origin is the turret's turning axis / the guns' trunnion (a rig the source keeps):
    # taken over `ring` / `trunnion` when given.
    ring_node: str | None = None
    trunnion_node: str | None = None
    skinned: bool = False                 # one skinned mesh: roles by the joint each triangle leans on (read_skinned)
    drop: list[str] = field(default_factory=list)   # node path regexes left out
    # For a source whose nodes are merged (one mesh a material): its connected pieces (welded at 1 mm) that a path rule
    # left on the base go to the role of the first box (game space, after the scale) holding their box's centre;
    # 'drop' leaves them out. --pieces lists every piece's box to write these from.
    boxes: list[tuple[str, tuple[float, float, float], tuple[float, float, float]]] = field(default_factory=list)
    # A launcher modelled raised: its guns brought down by this many degrees about the trunnion (which must be given)
    # before anything is measured, so the rest pose is level and lifting it is the gunner's.
    lower: float = 0.0
    # A launcher's tube mouths: True puts the 2 x 2 grid of mouths at the guns' front in pivots['tubes'] (the
    # emplacement's missile holder fires from them).
    tubes: bool = False


MODELS: dict[str, Model] = {
    # Lemper_Studio's Phalanx (its nodes: Stand, Rotate_Z (the mount's turn), Rotate_x (its elevation: the cradle,
    # the magazine drum, the search radar), Gun (the six barrels), Gun_Part (the barrels' clamps)). The real mount is
    # 4.7 m tall to the radome's top.
    # bear17's Flak 18/36 (merged meshes: its pieces sorted by box): the cruciform carriage and the pedestal stay, the
    # top carriage with its shield turns, the barrel and its cradle lift. 7.6 m long over its outriggers.
    'flak88': Model(kind='flak88', folder='emp_flak88', title='8.8 cm Flak 18/36', rules=[], yaw=-90.0, width=7.6,
                    boxes=[('gun', (-3.0, 2.12, -9.0), (3.0, 9.0, 9.0)), ('turret', (-3.0, 1.05, -9.0), (3.0, 2.12, 9.0))],
                    trunnion=(2.35, -1.3)),
    # KojfDiscord's GDF-005 (WARNO; one merged object, an OBJ): the four-wheeled carriage stays, the turret with its
    # radar turns, the twin 35 mm barrels lift. Its export is about twice the real gun's size: 8.5 m long over its
    # barrels (the real towed gun 7.8 m).
    'gdf005': Model(kind='gdf005', folder='emp_gdf005', title='Oerlikon GDF-005 35 mm', rules=[], length=8.5,
                    boxes=[('gun', (-0.27, 1.20, 0.55), (0.27, 1.55, 9.0)), ('turret', (-1.0, 1.0, -9.0), (1.0, 9.0, 0.9))],
                    trunnion=(1.38, 0.30)),
    # Planetrix23's M11-23A1 anti-drone turret (its nodes: Base, Turret (the yaw), Gun (the projector)): the laser
    # emplacement, 3.2 m tall (no real one is modelled: Iron Beam / LaWS have no usable model).
    'laser': Model(kind='laser', folder='emp_laser', title='Laser air-defence turret',
                   rules=[(r'/Gun/', 'gun'), (r'/Turret/', 'turret'), (r'/Base/', 'base')], height=3.2, yaw=90.0),
    # Obi-Wanya's railgun turret (one skinned mesh; its joints: Base, UGN (the turn), UVN (the elevation) and Cannon,
    # the radiators and the damper on the turret): 4.5 m tall, 15.6 m over its barrel.
    'railgun': Model(kind='railgun', folder='emp_railgun', title='Railgun turret',
                     rules=[(r'/(UVN|Cannon)_', 'gun'), (r'/Base_', 'base'), (r'/GLTF_created_0_rootJoint/', 'base'), (r'/', 'turret')],
                     skinned=True, height=4.5, ring_node='UGN_17', trunnion_node='UVN_1', muzzle_x=(0.0, 0.0)),
    'patriot': Model(kind='patriot', folder='emp_patriot', title='MIM-104 Patriot launcher', rules=[], yaw=-90.0, length=10.5,
                     boxes=[('base', (-9.0, -9.0, 2.3), (9.0, 9.0, 9.0)), ('gun', (-9.0, 1.9, -9.0), (9.0, 9.0, 2.3)),
                            ('turret', (-9.0, 1.25, -5.3), (9.0, 9.0, -1.2))],
                     trunnion=(1.9, -4.3), lower=45.4, tubes=True),
    'ciws_phalanx': Model(
        kind='ciws_phalanx', folder='emp_phalanx', title='Phalanx CIWS Mk 15',
        rules=[(r'/Gun/', 'rotor'), (r'/Gun_Part/', 'rotor'), (r'/Rotate_x/', 'gun'), (r'/Rotate_Z/', 'turret'),
               (r'/Stand/', 'base')],
        height=4.7, ring_node='Rotate_Z', trunnion_node='Rotate_x'),
}


def node_paths(path: str, origins: dict | None = None) -> list[str]:
    """Each mesh node's path of node names, in gltf.read's walk order (its Scene.mesh numbers); `origins` gets each
    node's world origin by name."""
    data = open(path, 'rb').read()
    if struct.unpack_from('<I', data, 0)[0] != gltf.GLB_MAGIC:
        raise ValueError(f'{path}: not a .glb')
    n = struct.unpack_from('<I', data, 12)[0]
    g = json.loads(data[20:20 + n])
    out: list[str] = []

    def walk(i: int, at: str, parent: np.ndarray) -> None:
        node = g['nodes'][i]
        here = f'{at}/{node.get("name", str(i))}'
        world = parent @ gltf.node_matrix(node)
        if origins is not None:
            origins.setdefault(node.get('name', str(i)), world[:3, 3].copy())
        if 'mesh' in node:
            out.append(here + '/')
        for c in node.get('children', []):
            walk(c, here, world)
    for r in g['scenes'][g.get('scene', 0)]['nodes']:
        walk(r, '', np.eye(4))
    return out


LIST_PIECES = False      # --pieces: print every connected piece's box (to write a Model's boxes)


def pieces(tris: np.ndarray, P: np.ndarray) -> np.ndarray:
    """Each triangle's connected piece (its corners welded at 1 mm), numbered from 0."""
    used, inv = np.unique(tris, return_inverse=True)
    inv = inv.reshape(-1, 3)
    _k, weld = np.unique(np.round(P[used] / 1e-3).astype(np.int64), axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    parent = np.arange(int(weld.max()) + 1)

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in weld[inv]:
        for u, v in ((a, b), (b, c)):
            ru, rv = find(u), find(v)
            if ru != rv:
                parent[ru] = rv
    roots = np.array([find(x) for x in weld[inv[:, 0]]])
    return np.unique(roots, return_inverse=True)[1].reshape(-1)


def by_boxes(m: Model, scene, P: np.ndarray, roles: np.ndarray, keep: np.ndarray):  # noqa: ANN001, ANN201
    """(roles, keep) with the base's pieces sorted into the Model's boxes (see Model.boxes)."""
    roles, keep = roles.copy(), keep.copy()
    sel = np.flatnonzero(keep & (roles == 'base'))
    if not len(sel):
        return roles, keep
    piece = pieces(scene.triangles[sel], P)
    for k in range(int(piece.max()) + 1):
        tri = sel[piece == k]
        Q = P[np.unique(scene.triangles[tri])]
        lo, hi = Q.min(0), Q.max(0)
        c = (lo + hi) / 2
        role = next((r for r, a, b in m.boxes if (np.asarray(a) <= c).all() and (c <= np.asarray(b)).all()), 'base')
        if LIST_PIECES and len(tri) >= 12:
            print(f'piece {k:4d} {len(tri):6d} tris  lo {np.round(lo, 2)} hi {np.round(hi, 2)} -> {role}')
        if role == 'drop':
            keep[tri] = False
        else:
            roles[tri] = role
    return roles, keep


def role_of(m: Model, path: str) -> str | None:
    if any(re.search(p, path) for p in m.drop):
        return None
    for pattern, role in m.rules:
        if re.search(pattern, path):
            return role
    return 'base'


def _texture(mat: gltf.Material, out_dir: str, name: str) -> str | None:
    """The material's base colour as a file in textures/ (PNG, at most MAX_TEXTURE a side), None: it has none."""
    from PIL import Image
    if mat.texture is None:
        return None
    with Image.open(io.BytesIO(mat.texture)) as im:
        im = im.convert('RGBA')
        if max(im.size) > MAX_TEXTURE:
            k = MAX_TEXTURE / max(im.size)
            im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
        rel = f'textures/{name}.png'
        im.convert('RGB').save(os.path.join(out_dir, rel), optimize=True)
    return rel


def _plain(mat: gltf.Material, out_dir: str, name: str) -> str:
    """A material with no texture: its base colour as a 4 x 4 image."""
    from PIL import Image
    c = tuple(int(round(255 * min(1.0, max(0.0, v)) ** (1 / 2.2))) for v in mat.base[:3])
    rel = f'textures/{name}.png'
    Image.new('RGB', (4, 4), c).save(os.path.join(out_dir, rel))
    return rel


def plating(out_dir: str) -> None:
    """textures/plating.png: the fortified base's concrete (pylib/emplacement_model.py puts it under every source's
    model), a fixed grey noise with panel seams every 64 pixels."""
    from PIL import Image
    rng = np.random.default_rng(6)
    n = 256
    g = 118 + 10 * rng.standard_normal((n, n))
    g += 6 * np.repeat(np.repeat(rng.standard_normal((n // 16, n // 16)), 16, 0), 16, 1)
    g[::64, :] -= 40
    g[:, ::64] -= 40
    img = np.clip(np.stack([g, g * 0.99, g * 0.96], -1), 0, 255).astype(np.uint8)
    Image.fromarray(img, 'RGB').save(os.path.join(out_dir, 'textures', 'plating.png'), optimize=True)


def read_obj(path: str) -> tuple[gltf.Scene, list[str]]:
    """An OBJ (War Thunder / WARNO exports: triangles, texture coordinates, an MTL with map_Kd) as gltf.read gives a
    .glb: one mesh node a material, its path /<material>/; uv v negated to the top-left origin."""
    V, T, tri_v, tri_t, mat = [], [], [], [], []
    mats: dict[str, int] = {}
    cur, mtllib = 0, None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.startswith('v '):
                V.append(line.split()[1:4])
            elif line.startswith('vt '):
                T.append(line.split()[1:3])
            elif line.startswith('usemtl '):
                cur = mats.setdefault(line.split(None, 1)[1].strip(), len(mats))
            elif line.startswith('mtllib '):
                mtllib = line.split(None, 1)[1].strip()
            elif line.startswith('f '):
                c = [x.split('/') for x in line.split()[1:]]
                for k in range(1, len(c) - 1):
                    tri_v.append([int(c[0][0]) - 1, int(c[k][0]) - 1, int(c[k + 1][0]) - 1])
                    tri_t.append([int(c[0][1]) - 1, int(c[k][1]) - 1, int(c[k + 1][1]) - 1])
                    mat.append(cur)
    maps: dict[str, str] = {}
    if mtllib:
        name = None
        with open(os.path.join(os.path.dirname(path), mtllib), encoding='utf-8', errors='replace') as f:
            for line in f:
                t = line.strip().split(None, 1)
                if t and t[0] == 'newmtl':
                    name = t[1]
                elif t and t[0] == 'map_Kd' and name:
                    maps[name] = t[1]
    P, UV = np.asarray(V, dtype=np.float64), np.asarray(T, dtype=np.float64)
    tv, tt = np.asarray(tri_v, dtype=np.int64), np.asarray(tri_t, dtype=np.int64)
    # unweld per (position, uv) corner, as glTF stores them
    corner = np.stack((tv, tt), axis=-1).reshape(-1, 2)
    uniq, inv = np.unique(corner, axis=0, return_inverse=True)
    materials = []
    for name in mats:
        tex = maps.get(name)
        data = open(os.path.join(os.path.dirname(path), tex), 'rb').read() if tex else None
        materials.append(gltf.Material(name, (1.0, 1.0, 1.0, 1.0), (0.0, 0.0, 0.0), texture=data,
                                       texture_mime='image/jpeg' if data else ''))
    uv = UV[uniq[:, 1]] * (1.0, -1.0) + (0.0, 1.0)     # bottom-left OBJ v -> glTF's top-left
    scene = gltf.Scene(P[uniq[:, 0]], None, inv.reshape(-1, 3), np.asarray(mat, dtype=np.int64), materials, {},
                       np.asarray(mat, dtype=np.int64), uv)
    return scene, [f'/{name}/' for name in mats]


def read_skinned(path: str, origins: dict) -> tuple[gltf.Scene, list[str]]:
    """A .glb with one skinned mesh (the source's rig: its turret's and guns' joints) in its bind pose, each triangle's
    'mesh node' the joint its first corner leans on most (paths /<joint>/: the Model's rules name roles by joint);
    `origins` gets every joint's bind position (its rig's pivots)."""
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 12)[0]
    g = json.loads(data[20:20 + n])
    binary = data[20 + n + 8:]

    def acc(i: int) -> np.ndarray:
        a = g['accessors'][i]
        bv = g['bufferViews'][a['bufferView']]
        dt = np.dtype(gltf.COMPONENT[a['componentType']])
        w = gltf.WIDTH[a['type']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = bv.get('byteStride', dt.itemsize * w)
        rows = np.lib.stride_tricks.as_strided(np.frombuffer(binary, np.uint8, stride * (a['count'] - 1) + dt.itemsize * w, off),
                                               (a['count'], dt.itemsize * w), (stride, 1))
        return rows.copy().view(dt).reshape(a['count'], w)
    (node,) = [nd for nd in g['nodes'] if 'mesh' in nd and 'skin' in nd]
    skin = g['skins'][node['skin']]
    joints = [g['nodes'][j].get('name', str(j)) for j in skin['joints']]
    ibm = acc(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)   # column-major
    for name, m_ in zip(joints, ibm):
        origins[name] = np.linalg.inv(m_)[:3, 3]
    pos, uv, tri, mat, joint_of = [], [], [], [], []
    base = 0
    for p in g['meshes'][node['mesh']]['primitives']:
        v = acc(p['attributes']['POSITION']).astype(np.float64)
        f = acc(p['indices']).reshape(-1, 3).astype(np.int64)
        J = acc(p['attributes']['JOINTS_0'])
        W = acc(p['attributes']['WEIGHTS_0']).astype(np.float64)
        top = J[np.arange(len(J)), np.argmax(W, axis=1)].astype(np.int64)
        pos.append(v)
        uv.append(acc(p['attributes']['TEXCOORD_0']).astype(np.float64)[:, :2] if 'TEXCOORD_0' in p['attributes']
                  else np.zeros((len(v), 2)))
        tri.append(f + base)
        mat.append(np.full(len(f), p.get('material', 0)))
        joint_of.append(top[f[:, 0]])
        base += len(v)
    T = np.vstack(tri)
    jo = np.concatenate(joint_of)
    scene = gltf.Scene(np.vstack(pos), None, T, np.concatenate(mat).astype(np.int64), gltf._materials(g, [binary]),
                       {}, jo.astype(np.int64), np.vstack(uv))
    return scene, [f'/{name}/' for name in joints]


def prepare(m: Model, src: str, out: str) -> dict:
    origins: dict[str, np.ndarray] = {}
    if src.lower().endswith('.obj'):
        scene, paths = read_obj(src)
    elif m.skinned:
        scene, paths = read_skinned(src, origins)
    else:
        scene = gltf.read(src)
        paths = node_paths(src, origins)
    roles = np.asarray([role_of(m, paths[k]) for k in scene.mesh], dtype=object)
    keep = roles != None   # noqa: E711 - an object array
    P = scene.positions.copy()
    a = np.radians(m.yaw)
    rot = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    P = P @ rot.T
    N = scene.normals @ rot.T if scene.normals is not None else None
    used = np.unique(scene.triangles[keep])
    lo, hi = P[used].min(0), P[used].max(0)
    scale = (m.height / (hi[1] - lo[1]) if m.height else m.length / (hi[2] - lo[2]) if m.length else
             m.width / (hi[0] - lo[0]) if m.width else 1.0)
    centre = (lo + hi) / 2
    P = (P - (centre[0], lo[1], centre[2])) * scale
    if m.boxes or LIST_PIECES:
        roles, keep = by_boxes(m, scene, P, roles, keep)
    if m.lower:
        if not m.trunnion:
            raise ValueError(f'{m.kind}: lowering the guns needs their trunnion')
        moved = np.unique(scene.triangles[keep & ((roles == 'gun') | (roles == 'rotor'))])
        b = np.radians(m.lower)      # about +x, +z towards -y: the muzzle down onto the front
        tilt = np.array([[1, 0, 0], [0, np.cos(b), -np.sin(b)], [0, np.sin(b), np.cos(b)]])
        pivot = np.array((0.0, m.trunnion[0], m.trunnion[1]))
        P[moved] = (P[moved] - pivot) @ tilt.T + pivot
        if N is not None:
            N = N.copy()
            N[moved] = N[moved] @ tilt.T
    os.makedirs(os.path.join(out, 'textures'), exist_ok=True)
    plating(out)
    materials, mat_of = [], {}
    arrays: dict[str, np.ndarray] = {}
    parts = []
    tri_n = None if N is None else N
    for role in ROLES:
        for mi in sorted(set(scene.material[keep & (roles == role)].tolist())):
            sel = np.flatnonzero(keep & (roles == role) & (scene.material == mi))
            if mi not in mat_of:
                name = f'{m.kind}_{mi}'
                mat = scene.materials[mi]
                albedo = _texture(mat, out, name) or _plain(mat, out, name)
                mat_of[mi] = len(materials)
                materials.append({'name': f'edf6vc_{name}', 'template': 'v603_body', 'albedo': albedo,
                                  'source': mat.name})
            tri = scene.triangles[sel]
            idx, inv = np.unique(tri, return_inverse=True)
            k = len(parts)
            pos = P[idx]
            if tri_n is not None and np.isfinite(tri_n[idx]).all() and np.any(tri_n[idx]):
                nrm = tri_n[idx]
            else:                       # no normals: the faces' own, per vertex (flat enough for hard surfaces)
                nrm = np.zeros_like(pos)
                t = inv.reshape(-1, 3)
                fn = np.cross(pos[t[:, 1]] - pos[t[:, 0]], pos[t[:, 2]] - pos[t[:, 0]])
                for c in range(3):
                    np.add.at(nrm, t[:, c], fn)
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
            uv = scene.uvs[idx] if scene.uvs is not None else np.zeros((len(idx), 2))
            arrays[f'p{k}_pos'] = pos.astype(np.float32)
            arrays[f'p{k}_nrm'] = nrm.astype(np.float32)
            arrays[f'p{k}_uv'] = uv.astype(np.float32)   # glTF's top-left origin: the engine's, as wt_tracked keeps them
            arrays[f'p{k}_tri'] = inv.reshape(-1, 3).astype(np.uint32)
            parts.append({'name': f'{role}.{materials[mat_of[mi]]["name"]}', 'role': role, 'material': mat_of[mi],
                          'triangles': int(len(sel)), 'vertices': int(len(idx))})
    def pts(*rs: str) -> np.ndarray:
        got = [arrays[f'p{k}_pos'] for k, p in enumerate(parts) if p['role'] in rs]
        if not got:
            raise ValueError(f'{m.kind}: no part is {rs}')
        return np.vstack(got).astype(np.float64)
    turret, guns = pts('turret'), pts('gun', 'rotor')
    tlo, thi = turret.min(0), turret.max(0)
    glo, ghi = guns.min(0), guns.max(0)
    placed = lambda name: (rot @ origins[name] - (centre[0], lo[1], centre[2])) * scale   # noqa: E731
    ring = m.ring or ((tlo[0] + thi[0]) / 2, (tlo[2] + thi[2]) / 2)
    trunnion = m.trunnion or ((glo[1] + ghi[1]) / 2, (glo[2] + ghi[2]) / 2)
    if m.ring_node:
        p = placed(m.ring_node)
        ring = (p[0], p[2])
    if m.trunnion_node:
        p = placed(m.trunnion_node)
        trunnion = (p[1], p[2])
    front = guns[guns[:, 2] > ghi[2] - 0.04 * (ghi[2] - glo[2])]
    if m.muzzle_x:
        xs = m.muzzle_x
    else:
        mid = (front[:, 0].min() + front[:, 0].max()) / 2
        left, right = front[front[:, 0] > mid], front[front[:, 0] <= mid]
        twin = front[:, 0].max() - front[:, 0].min() > 0.25 * (ghi[0] - glo[0]) and len(left) and len(right)
        xs = (left[:, 0].mean(), right[:, 0].mean()) if twin else ((glo[0] + ghi[0]) / 2,) * 2
    my = float(front[:, 1].mean())
    pivots = {'ring': [float(ring[0]), float(ring[1])], 'trunnion': [float(trunnion[0]), float(trunnion[1])],
              'muzzles': {'left': [float(xs[0]), my, float(ghi[2])], 'right': [float(xs[1]), my, float(ghi[2])]}}
    if m.tubes:
        mid_x, mid_y = (front[:, 0].min() + front[:, 0].max()) / 2, (front[:, 1].min() + front[:, 1].max()) / 2
        pivots['tubes'] = [[float(front[sel][:, 0].mean()), float(front[sel][:, 1].mean()), float(ghi[2])]
                           for sel in ((front[:, 0] > mid_x) & (front[:, 1] > mid_y), (front[:, 0] <= mid_x) & (front[:, 1] > mid_y),
                                       (front[:, 0] > mid_x) & (front[:, 1] <= mid_y), (front[:, 0] <= mid_x) & (front[:, 1] <= mid_y))]
    if any(p['role'] == 'rotor' for p in parts):
        r = pts('rotor')
        ax = m.rotor_axis or ((r[:, 0].min() + r[:, 0].max()) / 2, (r[:, 1].min() + r[:, 1].max()) / 2)
        pivots['rotor'] = [float(ax[0]), float(ax[1]), float(r[:, 2].min())]
        pivots['muzzles'] = {s: [float(ax[0]), float(ax[1]), float(r[:, 2].max())] for s in ('left', 'right')}
    np.savez_compressed(os.path.join(out, 'model.npz'), **arrays)
    info = {'format': FORMAT, 'kind': m.kind, 'title': m.title, 'prepared_from': os.path.basename(src),
            'scale': float(scale), 'yaw': m.yaw, 'materials': materials, 'parts': parts, 'pivots': pivots,
            'triangles': int(sum(p['triangles'] for p in parts)),
            'size': (P[used].max(0) - P[used].min(0)).tolist()}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1)
        f.write('\n')
    return info


def license_text(record: dict, title: str) -> str:
    """Sketchfab's credit for its API record (the download tool's license.json)."""
    user, url, lic = record['user'], record['viewerUrl'], record['license']
    short = {'by': 'CC-BY-4.0', 'cc0': 'CC0-1.0'}[lic['slug']]
    return (f'Model Information:\n* title:\t{title}\n* source:\t{url}\n* author:\t{user} (https://sketchfab.com/{user})\n\n'
            f'Model License:\n* license type:\t{short} ({lic["url"]})\n* requirements:\t{lic["requirements"]}\n\n'
            f'If you use this 3D model in your project be sure to copy paste this credit wherever you share it:\n'
            f'This work is based on "{title}" ({url}) by {user} (https://sketchfab.com/{user}) licensed under {short} '
            f'({lic["url"]})\n')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('kind', choices=sorted(MODELS))
    ap.add_argument('source', help='the downloaded .glb')
    ap.add_argument('--out', help='model folder to write (default: models/<folder> in the repository)')
    ap.add_argument('--license', help="the download's license.json (its Sketchfab API record)")
    ap.add_argument('--pieces', action='store_true', help="print every merged piece's box (Model.boxes)")
    a = ap.parse_args(argv)
    global LIST_PIECES
    LIST_PIECES = a.pieces
    m = MODELS[a.kind]
    out = a.out or os.path.join(ROOT, 'models', m.folder)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    info = prepare(m, a.source, out)
    if a.license:
        with open(a.license, encoding='utf-8') as f:
            record = json.load(f)
        with open(os.path.join(out, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(license_text(record, record['name']))
        info['source'] = record['viewerUrl']
        with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(info, f, indent=1)
            f.write('\n')
    print(json.dumps({k: info[k] for k in ('kind', 'triangles', 'scale', 'size', 'pivots')}, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
