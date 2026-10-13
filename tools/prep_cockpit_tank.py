"""Prepare the tank gunner's cockpit material (pylib/cockpit_kits.py tank_gunner) in <model dir>/cockpits/ (a developer
step, like tools/prep_cockpit_mech.py), and write CREDITS_tank.txt.

The station is cut out of a downloaded Sketchfab model of a real tank with its interior modelled (T-62A Main Battle
Tank by Muhamad Mirza Arrafi, CC BY 4.0: the gunner's station on the left of the gun, his two sight eyepieces with
their brow pads, the gun's control handles, the traverse handwheel, the turret's azimuth dial, the breech guard, the
radio and fire-control boxes on the turret wall). Only read: pylib/gltf.py parses the GLB; nothing in it is run.

What is kept, in the gunner's eye frame (+x left, +y up, +z ahead, metres, the eye at the origin: EYE in the asset is
where a gunner's head is when he lifts it from the sight, so the eyepieces sit low in the view): every triangle of the
interior in the box BOX round the eye that faces the eye (the game culls the others; a triangle seen from its back at
full size is seen from its back at any scale about the eye). Written to parts/<slug>/station.npz (float32 positions,
normals, texture coordinates, int32 triangles, int16 each triangle's material) with its materials' base colour
textures (at most 1024 px, PNG) and LICENSE.txt. The kit scales it about the eye and cuts it to each turret's room.

    python tools/prep_cockpit_tank.py [--src D:/APP/edf6-cockpit-assets] [--out <model dir>/cockpits]
"""
from __future__ import annotations

import argparse
import io
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import gltf  # noqa: E402
import obj_model as om  # noqa: E402

SLUG = 'sf-t62a-arrafi'
UID = 'c61279bad4ca4502bcc8e584b558d10e'
SOURCE = f'sketchfab/{UID}/model.glb'
TITLE = 'T-62A Main Battle Tank'
PAGE = f'https://sketchfab.com/3d-models/t-62a-main-battle-tank-{UID}'
# The asset's frame: +x ahead (the gun), +y up, +z the tank's right; metres. The gunner's eye with his head lifted from
# the sight: 0.42 m behind and 0.18 m above the main sight's brow pad (about 23 degrees down from the eye), so the
# eyepieces and their pads stand at the bottom of the view (the user's "前方视野必须开阔").
EYE = (-0.30, 1.45, -0.55)
BOX = {'left': 1.1, 'right': 1.1, 'up': 0.35, 'down': 1.4, 'ahead': 1.7, 'back': 0.7}
MAX_TEX = 1024
LICENSE = """{title} by {author}, CC BY 4.0
Source: {page} (via Sketchfab)
Licence: Creative Commons Attribution 4.0 (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/
Changes: the gunner's station cut out of the model (only the interior round the gunner's eye), turned into the eye's
frame, triangles facing away from the eye left out; textures re-saved as PNG. The EDF6 plugin's cockpit builder
(pylib/cockpit_kits.py tank_gunner) scales it and cuts it to each vehicle's turret.
"""


def default_out() -> str:
    return os.path.join(om.DEV_MODELS, 'cockpits')


def to_eye(p: np.ndarray, origin: tuple[float, float, float] = (0.0, 0.0, 0.0)) -> np.ndarray:
    """Asset (x ahead, y up, z right) into the eye frame (x left, y up, z ahead): a rotation (the winding is kept)."""
    q = p - np.array(origin)
    return np.stack([-q[:, 2], q[:, 1], q[:, 0]], 1)


def station(scene: gltf.Scene) -> dict[str, np.ndarray]:
    pos = to_eye(scene.positions, EYE)
    nrm = to_eye(scene.normals) if scene.normals is not None else None
    corners = pos[scene.triangles]
    lo = np.array([-BOX['right'], -BOX['down'], -BOX['back']])
    hi = np.array([BOX['left'], BOX['up'], BOX['ahead']])
    inside = ((corners >= lo) & (corners <= hi)).all((1, 2))
    face = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    toward = (face * -corners[:, 0]).sum(1) > 0          # the eye on its front side (counter-clockwise front)
    keep = inside & toward
    tris = scene.triangles[keep]
    used, inv = np.unique(tris.reshape(-1), return_inverse=True)
    uv = scene.uvs[used] if scene.uvs is not None else np.full((len(used), 2), 0.5)
    if nrm is None:
        raise SystemExit('the asset has no normals')
    return {'pos': pos[used].astype(np.float32), 'nrm': nrm[used].astype(np.float32), 'uv': uv.astype(np.float32),
            'tris': inv.reshape(-1, 3).astype(np.int32), 'mat': scene.material[keep].astype(np.int16)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=r'D:\APP\edf6-cockpit-assets')
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    out = a.out or default_out()
    src = os.path.join(a.src, SOURCE)
    with open(os.path.join(os.path.dirname(src), 'license.json'), encoding='utf-8') as h:
        meta = json.load(h)
    if meta['license']['slug'] not in ('by', 'cc0'):
        raise SystemExit(f'{UID}: licence {meta["license"]["slug"]}: not used')
    author = meta['user']['displayName']
    scene = gltf.read(src)
    data = station(scene)
    dest = os.path.join(out, 'parts', SLUG)
    os.makedirs(dest, exist_ok=True)
    names = []
    for k, mat in enumerate(scene.materials):
        name = f'tex{k}.png'
        if (data['mat'] == k).any() and mat.texture:
            img = Image.open(io.BytesIO(mat.texture)).convert('RGB')
            img.thumbnail((MAX_TEX, MAX_TEX), Image.LANCZOS)
            img.save(os.path.join(dest, name))
        names.append(name if (data['mat'] == k).any() and mat.texture else '')
    colours = np.array([[255 * min(1.0, max(0.0, c)) ** (1 / 2.2) for c in m.base[:3]] for m in scene.materials])
    np.savez_compressed(os.path.join(dest, 'station.npz'), textures=np.array(names), colours=colours, **data)
    with open(os.path.join(dest, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(LICENSE.format(title=TITLE, author=author, page=meta.get('viewerUrl') or PAGE))
    page = meta.get('viewerUrl') or PAGE
    with open(os.path.join(out, 'CREDITS_tank.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('Tank gunner cockpits (pylib/cockpit_kits.py tank_gunner): third-party material\n\n'
                f'"{TITLE}" by {author}, CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/), via Sketchfab: '
                f'{page} (cropped: the gunner\'s station only; scaled)  ->  parts/{SLUG}/station.npz, '
                f'parts/{SLUG}/tex*.png\n')
    print(f'{SLUG}: {len(data["tris"])} triangles of {len(scene.triangles)} -> {dest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
