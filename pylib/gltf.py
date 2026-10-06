"""glTF 2.0 (.gltf + .bin) static meshes into world-space triangles, numpy only (tools/prep_sazabi.py reads the
user's Sketchfab export with it).

    scene = read(path)        every mesh primitive of the default scene, its node transforms applied: positions,
                              normals (when given), triangle indices into them, and each triangle's material
    scene.materials           name, base colour (linear RGBA), emissive colour (linear RGB)

Only what a static, untextured export uses: no skins, no animations, no sparse accessors, no textures (a material's
texture is ignored with its factors kept), triangle primitives (mode 4) only. Anything else is an error, not a guess.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass

import numpy as np

COMPONENT = {5126: '<f4', 5125: '<u4', 5123: '<u2', 5121: 'u1'}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
TRIANGLES = 4


class GltfError(Exception):
    pass


@dataclass
class Material:
    name: str
    base: tuple[float, float, float, float]
    emissive: tuple[float, float, float]
    metallic: float = 1.0       # glTF's defaults
    roughness: float = 1.0


@dataclass
class Scene:
    positions: np.ndarray        # (n, 3) float64, world space
    normals: np.ndarray | None   # (n, 3) float64 world space, None when a primitive had none
    triangles: np.ndarray        # (m, 3) int64 into positions
    material: np.ndarray         # (m,) int64 into materials
    materials: list[Material]
    extras: dict                 # asset.extras (author, licence, source)


def node_matrix(node: dict) -> np.ndarray:
    """A node's local transform (column vectors: p' = M @ p)."""
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if 'scale' in node:
        m = np.diag(list(node['scale']) + [1.0]) @ m
    if 'rotation' in node:
        x, y, z, w = node['rotation']
        r = np.eye(4)
        r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        m = r @ m
    if 'translation' in node:
        t = np.eye(4)
        t[:3, 3] = node['translation']
        m = t @ m
    return m


def _materials(g: dict) -> list[Material]:
    out = []
    for i, m in enumerate(g.get('materials', [])):
        pbr = m.get('pbrMetallicRoughness', {})
        base = tuple(float(x) for x in pbr.get('baseColorFactor', (1.0, 1.0, 1.0, 1.0)))
        em = tuple(float(x) for x in m.get('emissiveFactor', (0.0, 0.0, 0.0)))
        out.append(Material(m.get('name', f'material{i}'), base, em, float(pbr.get('metallicFactor', 1.0)),
                            float(pbr.get('roughnessFactor', 1.0))))
    return out


def read(path: str) -> Scene:
    with open(path, encoding='utf-8') as h:
        g = json.load(h)
    buffers = []
    for b in g.get('buffers', []):
        uri = b.get('uri', '')
        if not uri or uri.startswith('data:'):
            raise GltfError('only external .bin buffers are read')
        with open(os.path.join(os.path.dirname(path), uri), 'rb') as h:
            buffers.append(h.read())

    def accessor(i: int) -> np.ndarray:
        a = g['accessors'][i]
        if 'sparse' in a or 'bufferView' not in a:
            raise GltfError(f'accessor {i}: sparse or bufferless accessors are not read')
        bv = g['bufferViews'][a['bufferView']]
        data = buffers[bv['buffer']]
        n, dt = WIDTH[a['type']], np.dtype(COMPONENT[a['componentType']])
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = bv.get('byteStride', dt.itemsize * n)
        if stride == dt.itemsize * n:
            return np.frombuffer(data, dt, a['count'] * n, off).reshape(a['count'], n)
        raw = np.frombuffer(data, np.uint8, stride * (a['count'] - 1) + dt.itemsize * n, off)
        rows = np.lib.stride_tricks.as_strided(raw, (a['count'], dt.itemsize * n), (stride, 1))
        return rows.copy().view(dt).reshape(a['count'], n)

    pos, nrm, tri, mat = [], [], [], []
    has_normals = True
    base = 0

    def walk(i: int, parent: np.ndarray) -> None:
        nonlocal base, has_normals
        node = g['nodes'][i]
        if 'skin' in node:
            raise GltfError('skinned meshes are not read')
        world = parent @ node_matrix(node)
        if 'mesh' in node:
            for p in g['meshes'][node['mesh']]['primitives']:
                if p.get('mode', TRIANGLES) != TRIANGLES:
                    raise GltfError('only triangle primitives are read')
                v = accessor(p['attributes']['POSITION']).astype(np.float64)
                pos.append((world @ np.c_[v, np.ones(len(v))].T).T[:, :3])
                if 'NORMAL' in p['attributes']:
                    nm = accessor(p['attributes']['NORMAL']).astype(np.float64)
                    inv_t = np.linalg.inv(world[:3, :3]).T
                    nm = nm @ inv_t.T
                    nrm.append(nm / np.maximum(np.linalg.norm(nm, axis=1, keepdims=True), 1e-12))
                else:
                    has_normals = False
                    nrm.append(np.zeros_like(v))
                f = accessor(p['indices']).reshape(-1, 3).astype(np.int64) if 'indices' in p \
                    else np.arange(len(v), dtype=np.int64).reshape(-1, 3)
                if np.linalg.det(world[:3, :3]) < 0:
                    f = f[:, ::-1]   # a mirroring transform flips the winding
                tri.append(f + base)
                mat.append(np.full(len(f), p.get('material', 0), dtype=np.int64))
                base += len(v)
        for c in node.get('children', []):
            walk(c, world)

    for r in g['scenes'][g.get('scene', 0)]['nodes']:
        walk(r, np.eye(4))
    if not pos:
        raise GltfError('no meshes')
    return Scene(np.vstack(pos), np.vstack(nrm) if has_normals else None, np.vstack(tri), np.concatenate(mat),
                 _materials(g), g.get('asset', {}).get('extras', {}))
