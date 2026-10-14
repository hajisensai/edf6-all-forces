"""glTF 2.0 (.gltf with external or embedded buffers, or .glb) static meshes into world-space triangles, numpy only
(tools/prep_sazabi.py reads the user's Sketchfab export with it; tools/prep_cockpit.py the CC0 cockpits).

    scene = read(path)        every mesh primitive of the default scene, its node transforms applied: positions,
                              normals (when given), texture coordinates (TEXCOORD_0, when given), triangle indices
                              into them, and each triangle's material
    scene.materials           name, base colour (linear RGBA), emissive colour (linear RGB), the base colour
                              texture's image (its bytes and MIME type: from a buffer view, a file or a data URI)
                              and the alpha mode
    scene.mesh                each triangle's mesh node (tools/prep_sazabi_rifle.py picks the rifle out by it)

Only what a static export uses: no skins, no animations, no sparse accessors, no Draco or meshopt compression, only
the base colour texture (a normal or metallic map is ignored with its factors kept; a material in the older
KHR_materials_pbrSpecularGlossiness workflow gives its diffuse texture and factor as the base colour), triangle
primitives (mode 4) only. Anything else is an error, not a guess.
"""
from __future__ import annotations

import base64
import json
import os
import struct
from dataclasses import dataclass

import numpy as np

COMPONENT = {5126: '<f4', 5125: '<u4', 5123: '<u2', 5121: 'u1', 5122: '<i2', 5120: 'i1'}
NORMALIZED = {5121: 255.0, 5123: 65535.0, 5120: 127.0, 5122: 32767.0}   # a normalized integer's 1.0
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
TRIANGLES = 4
GLB_MAGIC, GLB_JSON, GLB_BIN = 0x46546C67, 0x4E4F534A, 0x004E4942
SPEC_GLOSS = 'KHR_materials_pbrSpecularGlossiness'   # Sketchfab's exports of older models: diffuse instead of base colour
COMPRESSED = ('KHR_draco_mesh_compression', 'EXT_meshopt_compression')


class GltfError(Exception):
    pass


@dataclass
class Material:
    name: str
    base: tuple[float, float, float, float]
    emissive: tuple[float, float, float]
    metallic: float = 1.0       # glTF's defaults
    roughness: float = 1.0
    texture: bytes | None = None    # the base colour texture's image file (PNG / JPEG bytes), None: none
    texture_mime: str = ''          # 'image/png', 'image/jpeg'
    alpha_mode: str = 'OPAQUE'      # OPAQUE, MASK, BLEND


@dataclass
class Scene:
    positions: np.ndarray        # (n, 3) float64, world space
    normals: np.ndarray | None   # (n, 3) float64 world space, None when a primitive had none
    triangles: np.ndarray        # (m, 3) int64 into positions
    material: np.ndarray         # (m,) int64 into materials
    materials: list[Material]
    extras: dict                 # asset.extras (author, licence, source)
    mesh: np.ndarray | None = None   # (m,) int64: each triangle's mesh node, in the scene's walk order
    uvs: np.ndarray | None = None    # (n, 2) float64 TEXCOORD_0 (origin top left), None when no primitive had them


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


def _data_uri(uri: str) -> bytes:
    head, _, data = uri.partition(',')
    if ';base64' not in head:
        raise GltfError('a data URI that is not base64')
    return base64.b64decode(data)


def _container(path: str) -> tuple[dict, bytes | None]:
    """The JSON and, for a .glb, its binary chunk (buffer 0 with no uri)."""
    with open(path, 'rb') as h:
        raw = h.read()
    if len(raw) < 12 or struct.unpack_from('<I', raw, 0)[0] != GLB_MAGIC:
        return json.loads(raw.decode('utf-8')), None
    version, length = struct.unpack_from('<II', raw, 4)
    if version != 2 or length > len(raw):
        raise GltfError(f'a .glb of version {version} or cut short')
    g, binary, at = None, None, 12
    while at + 8 <= length:
        size, kind = struct.unpack_from('<II', raw, at)
        chunk = raw[at + 8:at + 8 + size]
        if kind == GLB_JSON:
            g = json.loads(chunk.decode('utf-8'))
        elif kind == GLB_BIN and binary is None:
            binary = chunk
        at += 8 + size
    if g is None:
        raise GltfError('a .glb without its JSON chunk')
    return g, binary


def _image(g: dict, i: int, buffers: list[bytes], base_dir: str) -> tuple[bytes, str]:
    img = g['images'][i]
    if 'bufferView' in img:
        bv = g['bufferViews'][img['bufferView']]
        off = bv.get('byteOffset', 0)
        return buffers[bv['buffer']][off:off + bv['byteLength']], img.get('mimeType', '')
    uri = img.get('uri', '')
    if uri.startswith('data:'):
        return _data_uri(uri), uri[5:].split(';')[0]
    with open(os.path.join(base_dir, uri), 'rb') as h:
        data = h.read()
    ext = os.path.splitext(uri)[1].lower()
    return data, {'.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg'}.get(ext, '')


def _materials(g: dict, buffers: list[bytes] | None = None, base_dir: str = '') -> list[Material]:
    out = []
    for i, m in enumerate(g.get('materials', [])):
        pbr = m.get('pbrMetallicRoughness', {})
        base = tuple(float(x) for x in pbr.get('baseColorFactor', (1.0, 1.0, 1.0, 1.0)))
        em = tuple(float(x) for x in m.get('emissiveFactor', (0.0, 0.0, 0.0)))
        texture, mime = None, ''
        tex = pbr.get('baseColorTexture')
        sg = m.get('extensions', {}).get(SPEC_GLOSS)
        if sg is not None:   # the older specular-glossiness workflow: its diffuse is the base colour
            base = tuple(float(x) for x in sg.get('diffuseFactor', (1.0, 1.0, 1.0, 1.0)))
            tex = sg.get('diffuseTexture', tex)
        if tex is not None and buffers is not None:
            source = g['textures'][tex['index']].get('source')
            if source is not None:
                texture, mime = _image(g, source, buffers, base_dir)
        out.append(Material(m.get('name', f'material{i}'), base, em, float(pbr.get('metallicFactor', 1.0)),
                            float(pbr.get('roughnessFactor', 1.0)), texture, mime, m.get('alphaMode', 'OPAQUE')))
    return out


def read(path: str) -> Scene:
    g, binary = _container(path)
    base_dir = os.path.dirname(path)
    for ext in COMPRESSED:
        if ext in g.get('extensionsRequired', []) or ext in g.get('extensionsUsed', []):
            raise GltfError(f'{ext}: compressed meshes are not read (decompress it first)')
    buffers = []
    for b in g.get('buffers', []):
        uri = b.get('uri', '')
        if not uri:
            if binary is None:
                raise GltfError('a buffer with no uri outside a .glb')
            buffers.append(binary)
        elif uri.startswith('data:'):
            buffers.append(_data_uri(uri))
        else:
            with open(os.path.join(base_dir, uri), 'rb') as h:
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
            out = np.frombuffer(data, dt, a['count'] * n, off).reshape(a['count'], n)
        else:
            raw = np.frombuffer(data, np.uint8, stride * (a['count'] - 1) + dt.itemsize * n, off)
            rows = np.lib.stride_tricks.as_strided(raw, (a['count'], dt.itemsize * n), (stride, 1))
            out = rows.copy().view(dt).reshape(a['count'], n)
        if a.get('normalized') and a['componentType'] in NORMALIZED:
            out = np.maximum(out.astype(np.float64) / NORMALIZED[a['componentType']], -1.0)
        return out

    pos, nrm, uv, tri, mat, node_of = [], [], [], [], [], []
    has_normals, has_uvs = True, False
    base = 0
    meshes = 0

    def walk(i: int, parent: np.ndarray) -> None:
        nonlocal base, has_normals, has_uvs, meshes
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
                if 'TEXCOORD_0' in p['attributes']:
                    has_uvs = True
                    uv.append(accessor(p['attributes']['TEXCOORD_0']).astype(np.float64)[:, :2])
                else:
                    uv.append(np.zeros((len(v), 2)))
                f = accessor(p['indices']).reshape(-1, 3).astype(np.int64) if 'indices' in p \
                    else np.arange(len(v), dtype=np.int64).reshape(-1, 3)
                if np.linalg.det(world[:3, :3]) < 0:
                    f = f[:, ::-1]   # a mirroring transform flips the winding
                tri.append(f + base)
                mat.append(np.full(len(f), p.get('material', 0), dtype=np.int64))
                node_of.append(np.full(len(f), meshes, dtype=np.int64))
                base += len(v)
            meshes += 1
        for c in node.get('children', []):
            walk(c, world)

    for r in g['scenes'][g.get('scene', 0)]['nodes']:
        walk(r, np.eye(4))
    if not pos:
        raise GltfError('no meshes')
    return Scene(np.vstack(pos), np.vstack(nrm) if has_normals else None, np.vstack(tri), np.concatenate(mat),
                 _materials(g, buffers, base_dir), g.get('asset', {}).get('extras', {}), np.concatenate(node_of),
                 np.vstack(uv) if has_uvs else None)
