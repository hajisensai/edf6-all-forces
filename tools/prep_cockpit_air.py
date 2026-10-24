"""Developer-only: the helicopter cockpits' 3D models (pylib/cockpit_kits_air.py), cut from CC BY models downloaded
from Sketchfab into <model dir>/cockpits/parts/<asset>/, and their credits into <model dir>/cockpits/CREDITS_air.txt.
The installer reads only what this writes.

    python tools/prep_cockpit_air.py [--src D:/APP/edf6-cockpit-assets/sketchfab] [--out <model dir>/cockpits]

--src holds one folder per Sketchfab model (its uid): model.glb as downloaded and license.json (the model's API
record: name, author, licence; a licence other than CC BY / CC0 is refused). The downloads are data: only parsed by
pylib/gltf.py and decoded by PIL here, nothing in them is run.

For each asset (ASSETS) the whole aircraft is cut down to the one crew station the kit uses:
  - the crew figures, the canopy glass and other blended surfaces are left out (by material / mesh node: the glass
    would cover the view, a figure would sit where the eye is);
  - the model is turned and scaled into the eye's frame (+x left, +y up, +z ahead, metres, the seated eye at the
    origin: SPEC.eye) and cropped to the station's box (the other crew station, the fuselage beyond it, the rotor);
  - only what the eye can see is kept: the eye never moves inside the cockpit, so every triangle is drawn from that
    one point; each is rasterised from the eye into a cube map (both sides: Sketchfab exports are double-sided) and
    kept if it covers a pixel or lies on the nearest surface where it would (a sliver), then turned to face the eye
    (the game draws one side) -- what is behind panels, under the floor and on the far side of the seat goes;
  - the base colour images are resized to at most 1024 px into PNGs beside it.
The result is parts/<asset>/cockpit.glb (positions, normals, UVs, one primitive per material; its images the PNGs
beside it, by file name) with LICENSE.txt. The kit (cockpit_kits_air.py) scales it to the room the vehicle has round
the eye and draws it; the screens' places are measured on it there (SCREENS).
"""
from __future__ import annotations

import argparse
import io
import json
import os
import struct
import sys
from dataclasses import dataclass

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import gltf  # noqa: E402
import obj_model as om  # noqa: E402

SRC = r'D:\APP\edf6-cockpit-assets\sketchfab'
MAX_TEX = 4096
CUBE = 2048            # pixels a cube face's side has in the visibility pass (0.044 degrees a pixel)
SLIVER = 0.01          # a triangle covering no pixel is kept if its centre is this close (share of depth) to the front


@dataclass(frozen=True)
class Spec:
    """One asset: the download `uid`, `scale` (source units to metres), the seated `eye` and the `ahead` and `up`
    axes in source units (unit vectors), the materials and mesh nodes left out, the box kept round the eye (eye
    frame, metres: left, right, up, down, ahead, back), what it is, the Sketchfab page."""
    uid: str
    scale: float
    eye: tuple[float, float, float]
    ahead: tuple[float, float, float]
    up: tuple[float, float, float]
    drop_materials: tuple[int, ...]
    drop_nodes: tuple[int, ...]
    box: dict[str, float]
    what: str
    glass: tuple[int, ...] = ()   # the materials (of drop_materials) that are its windows' glass: kept as the kit's
                                  # windows (cockpits.Kit.windows), the lines of sight its tub leaves open


# War Thunder's AH-64A (the game's own model as shared on Sketchfab, 1:1, metres, nose +z, +x the crew's left; the
# user, 2026-10-14: "座舱和模型干脆直接也搬战雷的吧"): its two tandem stations. Left out: its blended materials (0, 2, 4, 6,
# 7: the canopy's glass, the glare shields' and the tail's decals), the rotor overhead (the box).
_WT_AH64A = '486f3845d88647abae002e1222788686'
_WT_AH64A_DROP = (0, 2, 4, 6, 7)
WT_ASSETS: dict[str, Spec] = {
    # The front (copilot / gunner) station: the eye 0.75 m over the seat's cushion, the TADS sight's hood 0.4 m ahead.
    'wt-ah64a-cpg': Spec(_WT_AH64A, 1.0, (0.0, 0.65, 2.75), (0, 0, 1), (0, 1, 0), _WT_AH64A_DROP, (),
                         {'left': 0.8, 'right': 0.8, 'up': 0.75, 'down': 1.35, 'ahead': 2.0, 'back': 0.45},
                         "War Thunder's AH-64A front (copilot / gunner) station", glass=(0,)),
    # The rear (pilot's) station, higher, looking over the front station.
    'wt-ah64a-pilot': Spec(_WT_AH64A, 1.0, (0.0, 0.95, 1.45), (0, 0, 1), (0, 1, 0), _WT_AH64A_DROP, (),
                           {'left': 0.8, 'right': 0.8, 'up': 0.75, 'down': 1.35, 'ahead': 2.0, 'back': 0.45},
                           "War Thunder's AH-64A rear (pilot's) station", glass=(0,)),
}

# The earlier gunship station (sf-ah64e-cpg) no longer backs a kit; it stays while its part is in the model folder,
# so its credits still name it.
ASSETS: dict[str, Spec] = {
    **WT_ASSETS,
    # A tandem AH-64E: the front (copilot / gunner) station, its eye over the seat (the rear, pilot's station looks
    # through the front canopy's frame posts, a few degrees either side of the sight line). Left out: the crew
    # (materials 5, 20, 24, 30, 31, 34; their harness, nodes 211 / 220), the canopy glass (22, 33, 35).
    'sf-ah64e-cpg': Spec('9eb641f9179d413e87367ebd9b96347a', 1000.0, (0.004555, 0.03053, -0.001017), (1, 0, 0), (0, 1, 0),
                         (5, 20, 24, 30, 31, 34, 22, 33, 35), (211, 220),
                         {'left': 0.8, 'right': 0.8, 'up': 0.75, 'down': 1.35, 'ahead': 2.0, 'back': 0.45},
                         "an AH-64E's front (copilot / gunner) station"),
    # A UH-60M: the pilot's (right) seat. Left out: the crew (node 0), the windows' glass (materials 6, 7), the
    # rotor head and the cabin behind the cockpit (the box).
    'sf-uh60m-pilot': Spec('12b4e525676a49678ac3006b360c8750', 5.6, (-0.0714, -0.0457, 0.6), (0, 0, 1), (0, 1, 0),
                           (6, 7), (0,),
                           {'left': 1.4, 'right': 0.5, 'up': 0.6, 'down': 1.05, 'ahead': 1.5, 'back': 0.55},
                           "a UH-60M's pilot station (the right seat)", glass=(6, 7)),
}


def _frame(spec: Spec) -> np.ndarray:
    """Rows: the eye frame's left, up, ahead in source axes."""
    f, u = np.array(spec.ahead, dtype=np.float64), np.array(spec.up, dtype=np.float64)
    return np.stack([np.cross(u, f), u, f])


def cut(spec: Spec, scene: gltf.Scene) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """The station in the eye frame: positions, normals, uvs (per vertex) and triangles, their material, cropped."""
    m = _frame(spec)
    pos = (scene.positions - np.array(spec.eye)) @ m.T * spec.scale
    nrm = scene.normals @ m.T if scene.normals is not None else np.zeros_like(pos)
    uv = scene.uvs if scene.uvs is not None else np.zeros((len(pos), 2))
    b = spec.box
    lo = np.array([-b['right'], -b['down'], -b['back']])
    hi = np.array([b['left'], b['up'], b['ahead']])
    corners = pos[scene.triangles]
    keep = ((corners >= lo) & (corners <= hi)).all(2).all(1)
    keep &= ~np.isin(scene.material, spec.drop_materials) & ~np.isin(scene.mesh, spec.drop_nodes)
    return pos, nrm, uv, scene.triangles[keep], scene.material[keep]


def glass_of(spec: Spec, scene: gltf.Scene) -> list[float]:
    """The station's window glass (Spec.glass) in the eye frame, metres, inside its box: flat (x, y, z) corners, three a
    triangle, at 1 mm (the kit's windows, stored in the cockpit file's asset extras)."""
    m = _frame(spec)
    pos = (scene.positions - np.array(spec.eye)) @ m.T * spec.scale
    b = spec.box
    lo = np.array([-b['right'], -b['down'], -b['back']])
    hi = np.array([b['left'], b['up'], b['ahead']])
    corners = pos[scene.triangles[np.isin(scene.material, spec.glass)]]
    corners = corners[((corners >= lo) & (corners <= hi)).all(2).all(1)]
    return [round(float(c), 3) for c in corners.reshape(-1)]


CUBE_FACES = [((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0)), ((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)),
              ((0, 1, 0), (0, 0, -1)), ((0, -1, 0), (0, 0, 1))]


def visible(pos: np.ndarray, tris: np.ndarray) -> np.ndarray:
    """Which triangles the eye (the origin) sees, both sides drawn: rasterised into a cube map round it."""
    import cabin_view as cv
    seen = np.zeros(len(tris), dtype=bool)
    tw = pos[tris]
    for look, up in CUBE_FACES:
        cam = cv.camera(np.zeros(3), np.array(look, dtype=np.float64), np.array(up, dtype=np.float64))
        vc = tw @ cam.T
        ahead = (vc[:, :, 2] > 1e-3).any(1)
        src = np.nonzero(ahead)[0]
        v, _b, s = cv.clip_near(vc[src], 1e-3)
        s = src[s]
        k = CUBE / 2   # 90 degrees across: tan(45) = 1
        q = np.stack([CUBE / 2 + k * v[:, :, 0] / v[:, :, 2], CUBE / 2 - k * v[:, :, 1] / v[:, :, 2]], -1)
        iz = 1.0 / v[:, :, 2]
        zbuf, kbuf, _l = cv.rasterise(q, iz, CUBE, CUBE)
        hit = kbuf[kbuf >= 0]
        seen[s[np.unique(hit)]] = True
        # Slivers: a triangle no pixel centre falls in, whose centre is at the front where it projects.
        c = vc.mean(1)
        cand = np.nonzero(~seen & (c[:, 2] > 1e-3))[0]
        px = CUBE / 2 + k * c[cand, 0] / c[cand, 2]
        py = CUBE / 2 - k * c[cand, 1] / c[cand, 2]
        inside = (px >= 0) & (px < CUBE) & (py >= 0) & (py < CUBE)
        cand, px, py = cand[inside], px[inside].astype(np.int64), py[inside].astype(np.int64)
        front = zbuf[py * CUBE + px]
        seen[cand[(1.0 / c[cand, 2]) >= front * (1.0 - SLIVER)]] = True
    return seen


def facing_eye(pos: np.ndarray, nrm: np.ndarray, uv: np.ndarray, tris: np.ndarray) -> tuple[np.ndarray, ...]:
    """Each triangle turned to face the eye (its winding and normals flipped where its back was toward it), the
    vertices it uses only (a vertex used by both turned and unturned triangles split in two)."""
    t = pos[tris]
    n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    back = (n * -t[:, 0]).sum(1) < 0
    tris = np.where(back[:, None], tris[:, ::-1], tris)
    key = tris * 2 + back[:, None]       # (vertex, flipped)
    used, inv = np.unique(key.reshape(-1), return_inverse=True)
    vid, flip = used // 2, (used % 2).astype(bool)
    vn = nrm[vid] * np.where(flip, -1.0, 1.0)[:, None]
    return pos[vid], vn, uv[vid], inv.reshape(-1, 3)


def write_glb(path: str, prims: list[tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]],
              materials: list[dict], images: list[str], extras: dict) -> None:
    """A glTF binary: one mesh, one primitive per (positions, normals, uvs, triangles), its material; images by
    file name (beside it)."""
    blob = bytearray()
    views, accessors, primitives = [], [], []

    def add(arr: np.ndarray, target: int, kind: str, ctype: int, minmax: bool = False) -> int:
        data = arr.tobytes()
        while len(blob) % 4:
            blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
        blob.extend(data)
        acc = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(arr), 'type': kind}
        if minmax:
            acc['min'] = arr.min(0).tolist()
            acc['max'] = arr.max(0).tolist()
        accessors.append(acc)
        return len(accessors) - 1

    for k, (p, n, uv, t) in enumerate(prims):
        a = {'POSITION': add(p.astype('<f4'), 34962, 'VEC3', 5126, True),
             'NORMAL': add(n.astype('<f4'), 34962, 'VEC3', 5126),
             'TEXCOORD_0': add(uv.astype('<f4'), 34962, 'VEC2', 5126)}
        primitives.append({'attributes': a, 'indices': add(t.astype('<u4').reshape(-1), 34963, 'SCALAR', 5125),
                           'material': k, 'mode': 4})
    g = {'asset': {'version': '2.0', 'generator': 'tools/prep_cockpit_air.py', 'extras': extras},
         'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0, 'name': 'cockpit'}],
         'meshes': [{'primitives': primitives}], 'materials': materials,
         'images': [{'uri': name} for name in images], 'textures': [{'source': i} for i in range(len(images))],
         'buffers': [{'byteLength': len(blob)}], 'bufferViews': views, 'accessors': accessors}
    js = json.dumps(g, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    while len(blob) % 4:
        blob.append(0)
    with open(path, 'wb') as h:
        h.write(struct.pack('<III', gltf.GLB_MAGIC, 2, 12 + 8 + len(js) + 8 + len(blob)))
        h.write(struct.pack('<II', len(js), gltf.GLB_JSON) + js)
        h.write(struct.pack('<II', len(blob), gltf.GLB_BIN) + bytes(blob))


def licence_of(record: dict) -> tuple[str, str, str, str]:
    """(model name, author, licence label, licence url) from the API record; refuses anything but CC BY / CC0."""
    lic = record.get('license') or {}
    if lic.get('slug') not in ('by', 'cc0'):
        raise SystemExit(f"{record.get('uid')}: licence {lic.get('slug')} is not CC BY or CC0")
    label = 'CC BY 4.0' if lic['slug'] == 'by' else 'CC0 1.0'
    return record['name'], record['user'].get('displayName') or record['user']['username'], label, lic.get('url', '')


def prepare(slug: str, spec: Spec, src: str, out: str) -> str:
    folder = os.path.join(src, spec.uid)
    with open(os.path.join(folder, 'license.json'), encoding='utf-8') as h:
        record = json.load(h)
    name, author, label, url = licence_of(record)
    scene = gltf.read(os.path.join(folder, 'model.glb'))
    pos, nrm, uv, tris, mat = cut(spec, scene)
    seen = visible(pos, tris)
    tris, mat = tris[seen], mat[seen]
    dest = os.path.join(out, 'parts', slug)
    os.makedirs(dest, exist_ok=True)
    for old in os.listdir(dest):
        if old.endswith('.png') or old == 'cockpit.glb':
            os.remove(os.path.join(dest, old))
    prims, materials, images = [], [], []
    for k in np.unique(mat):
        m = scene.materials[k]
        p, n, u, t = facing_eye(pos, nrm, uv, tris[mat == k])
        prims.append((p, n, u, t))
        entry = {'name': f'm{k}', 'pbrMetallicRoughness': {'baseColorFactor': list(m.base[:3]) + [1.0]}}
        if m.texture:
            img = Image.open(io.BytesIO(m.texture)).convert('RGB')
            if max(img.size) > MAX_TEX:
                f = MAX_TEX / max(img.size)
                img = img.resize((max(1, round(img.width * f)), max(1, round(img.height * f))), Image.LANCZOS)
            file = f'tex_m{k}.png'
            img.save(os.path.join(dest, file))
            entry['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(images)}
            images.append(file)
        materials.append(entry)
    page = record.get('viewerUrl') or f'https://sketchfab.com/3d-models/{spec.uid}'
    credit = f'{name} by {author}, {label}'
    extras = {'author': author, 'license': label, 'source': page, 'title': name}
    if spec.glass:
        extras['glass'] = glass_of(spec, scene)
    write_glb(os.path.join(dest, 'cockpit.glb'), prims, materials, images, extras)
    with open(os.path.join(dest, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(f'{credit}\nSource: {page} (via Sketchfab)\nLicence: {label}, {url}\n'
                f'Changes: cut down to {spec.what} (crew figures, glass and everything the seated eye cannot see left '
                f'out), turned, scaled to metres and re-exported with its base colour textures resized to at most '
                f'{MAX_TEX} px (tools/prep_cockpit_air.py); the EDF6 plugin\'s cockpit builder '
                f'(pylib/cockpit_kits_air.py) scales it to the vehicle and places it there.\n')
    print(f'{slug}: {len(tris)} triangles, {len(prims)} materials, {len(images)} images -> {dest}')
    return (f'{credit} ({url}), via Sketchfab: {page} (cropped/scaled)  ->  parts/{slug}/cockpit.glb, '
            + ', '.join(images))


# The photographic textures earlier helicopter kits used (tools/prep_cockpit_air.py of 971c9a51 cut them): credited
# for as long as a kit still names them (cockpit_kits_air._seat, kept for the plugin's aircraft kits).
PHOTOS = {
    'air_seat': ('Photograph by Sgt. Adrianna Barnes, Public domain (work of the US federal government)',
                 'https://commons.wikimedia.org/wiki/File:16th_CAB_upgrades_Black_Hawk_helicopters_DVIDS815181.jpg'),
}


def photos_in_use() -> list[str]:
    """PHOTOS' credit lines for the textures a kit (pylib/cockpit_kits*.py) still names."""
    import glob
    text = ''
    for path in glob.glob(os.path.join(HERE, '..', 'pylib', 'cockpit_kits*.py')):
        with open(path, encoding='utf-8') as h:
            text += h.read()
    return [f'{credit}\n    {page} (cropped, perspective-corrected and resized: cockpits/textures/{tex}.png)'
            for tex, (credit, page) in sorted(PHOTOS.items()) if f"'{tex}'" in text]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=SRC)
    ap.add_argument('--out', default=os.path.join(om.REPO_MODELS, 'cockpits'))
    ap.add_argument('--only', default='', help='comma-separated asset slugs (default: all)')
    args = ap.parse_args()
    want = [s for s in args.only.split(',') if s] or list(ASSETS)
    lines = ['Helicopter cockpits (pylib/cockpit_kits_air.py): third-party material', '']
    for slug in ASSETS:
        if slug in want:
            lines.append(prepare(slug, ASSETS[slug], args.src, args.out))
        else:
            lines.append(f'(unchanged) parts/{slug}/')
    lines += photos_in_use()
    lines += ['', 'The other textures the helicopter kits use (air_tub, air_overhead) are drawn by '
              'tools/make_cockpit_textures_air.py.']
    if set(want) == set(ASSETS):
        with open(os.path.join(args.out, 'CREDITS_air.txt'), 'w', encoding='utf-8', newline='\n') as h:
            h.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
