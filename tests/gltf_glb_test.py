"""pylib/gltf.py reads a .glb and a .gltf with embedded buffers the same way, with their texture coordinates, base colour
texture (from a buffer view and from a data URI), alpha mode and normalized integer coordinates; a compressed mesh is
refused, not guessed. Exit code 1 on a failure."""
from __future__ import annotations

import base64
import json
import os
import struct
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import gltf  # noqa: E402

PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==')
failures = 0


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what)
    failures += 0 if ok else 1


def document(uv_normalized: bool) -> tuple[dict, bytes]:
    pos = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], '<f4').tobytes()
    if uv_normalized:
        uv = np.array([[0, 0], [65535, 0], [0, 65535]], '<u2').tobytes()
    else:
        uv = np.array([[0, 0], [1, 0], [0, 1]], '<f4').tobytes()
    idx = np.array([0, 1, 2, 0], '<u2').tobytes()   # padded to 4 bytes
    blob = pos + uv + idx + PNG
    views = [{'buffer': 0, 'byteOffset': 0, 'byteLength': len(pos)},
             {'buffer': 0, 'byteOffset': len(pos), 'byteLength': len(uv)},
             {'buffer': 0, 'byteOffset': len(pos) + len(uv), 'byteLength': 6},
             {'buffer': 0, 'byteOffset': len(pos) + len(uv) + len(idx), 'byteLength': len(PNG)}]
    g = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
         'nodes': [{'mesh': 0, 'translation': [0, 0, 2]}],
         'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'TEXCOORD_0': 1}, 'indices': 2, 'material': 0}]}],
         'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
                       {'bufferView': 1, 'componentType': 5123 if uv_normalized else 5126, 'count': 3, 'type': 'VEC2',
                        'normalized': uv_normalized},
                       {'bufferView': 2, 'componentType': 5123, 'count': 3, 'type': 'SCALAR'}],
         'bufferViews': views,
         'images': [{'bufferView': 3, 'mimeType': 'image/png'}], 'textures': [{'source': 0}],
         'materials': [{'name': 'panel', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}],
         'buffers': [{'byteLength': len(blob)}]}
    return g, blob


def glb(g: dict, blob: bytes) -> bytes:
    js = json.dumps(g).encode()
    js += b' ' * (-len(js) % 4)
    blob += b'\0' * (-len(blob) % 4)
    body = struct.pack('<II', len(js), gltf.GLB_JSON) + js + struct.pack('<II', len(blob), gltf.GLB_BIN) + blob
    return struct.pack('<III', gltf.GLB_MAGIC, 2, 12 + len(body)) + body


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        g, blob = document(False)
        path = os.path.join(d, 'a.glb')
        with open(path, 'wb') as h:
            h.write(glb(g, blob))
        s = gltf.read(path)
        expect(np.allclose(s.positions[1], [1, 0, 2]), 'a .glb: positions through the node transform')
        expect(s.uvs is not None and np.allclose(s.uvs[2], [0, 1]), '...its texture coordinates')
        m = s.materials[0]
        expect(m.texture == PNG and m.texture_mime == 'image/png' and m.alpha_mode == 'BLEND',
               '...its base colour texture from a buffer view, and the alpha mode')

        g2, blob2 = document(True)
        g2['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(blob2).decode()
        g2['images'] = [{'uri': 'data:image/png;base64,' + base64.b64encode(PNG).decode()}]
        path2 = os.path.join(d, 'b.gltf')
        with open(path2, 'w', encoding='utf-8') as h:
            json.dump(g2, h)
        s2 = gltf.read(path2)
        expect(np.allclose(s2.uvs[1], [1, 0]), 'a .gltf with embedded buffers: normalized coordinates to 0..1')
        expect(s2.materials[0].texture == PNG and s2.materials[0].texture_mime == 'image/png',
               '...its texture from a data URI')

        g4, blob4 = document(False)
        g4['extensionsUsed'] = [gltf.SPEC_GLOSS]
        g4['materials'] = [{'name': 'old', 'extensions': {gltf.SPEC_GLOSS: {
            'diffuseTexture': {'index': 0}, 'diffuseFactor': [0.5, 0.25, 1.0, 1.0]}}}]
        path4 = os.path.join(d, 'd.glb')
        with open(path4, 'wb') as h:
            h.write(glb(g4, blob4))
        m4 = gltf.read(path4).materials[0]
        expect(m4.texture == PNG and m4.texture_mime == 'image/png' and m4.base == (0.5, 0.25, 1.0, 1.0),
               'a specular-glossiness material: its diffuse texture and factor as the base colour')

        g3, blob3 = document(False)
        g3['extensionsUsed'] = ['KHR_draco_mesh_compression']
        path3 = os.path.join(d, 'c.glb')
        with open(path3, 'wb') as h:
            h.write(glb(g3, blob3))
        try:
            gltf.read(path3)
            expect(False, 'a Draco-compressed mesh is refused')
        except gltf.GltfError:
            expect(True, 'a Draco-compressed mesh is refused')
    print(f'{failures} FAILED' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
