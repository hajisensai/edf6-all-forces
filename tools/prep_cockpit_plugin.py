"""Prepare the plugin's own vehicles' cockpits (pylib/cockpit_kits_plugin.py) in <model dir>/cockpits/ (a developer
step, like tools/prep_cockpit_mech.py), and write CREDITS_plugin.txt.

Each cockpit is cut out of a downloaded Sketchfab model of the real aircraft with its cockpit modelled (CC BY 4.0,
the licence checked in the model's API record, license.json beside the download): only read, pylib/gltf.py parses the
GLB; nothing in it is run.

What is kept of a model (ASSETS), in its seat's eye frame (+x left, +y up, +z ahead, metres, the eye at the origin):
the triangles of its cockpit materials (`keep`; not `drop`) that reach into the box `box` round the eye, less the
ones in any of `cut` (by the centre or two corners) and the ones reaching into `view` (the canopy's bows, a HUD's
glass, a windscreen's posts: what would stand in the view ahead), and of those only the ones facing the eye (the
game culls the others; the eye never moves inside the cockpit; a double-sided material's back faces are turned to
the eye instead). Written to parts/<slug>/cockpit.npz
(float32 positions, normals, texture coordinates, int32 triangles, int16 each triangle's material, the materials'
texture files and sRGB base colours) with the base colour textures (at most 1024 px, PNG, the material's colour
factor multiplied in) and LICENSE.txt. The kit cuts it to each vehicle's room and scales it down where that is small.

    python tools/prep_cockpit_plugin.py [--src D:/APP/edf6-cockpit-assets] [--out <model dir>/cockpits] [--only slug]
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import sys
from dataclasses import dataclass

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import gltf  # noqa: E402
import obj_model as om  # noqa: E402

Vec3 = tuple[float, float, float]
Box = tuple[Vec3, Vec3]   # eye frame: (lowest x, y, z), (highest x, y, z): x left, y up, z ahead
MAX_TEX = 1024
AXES = {'+x': (1, 0, 0), '-x': (-1, 0, 0), '+y': (0, 1, 0), '-y': (0, -1, 0), '+z': (0, 0, 1), '-z': (0, 0, -1)}


@dataclass(frozen=True)
class Asset:
    """A cockpit cut from a Sketchfab model `uid` (sketchfab/<uid>/model.glb under --src): the eye at `eye` (the
    model's own coordinates), the aircraft's nose along `ahead` and its top along `up` there (axis names), `scale`
    metres per model unit; what is kept (see the module's docstring). `what`: the credit's note of what was cut."""
    uid: str
    title: str
    eye: Vec3
    ahead: str
    up: str
    scale: float
    box: Box
    keep: tuple[str, ...] = ()          # material name parts kept (none: every material)
    drop: tuple[str, ...] = ()          # material name parts left out
    drop_nodes: tuple[str, ...] = ()    # mesh node name parts left out
    cut: tuple[Box, ...] = ()           # eye-frame boxes whose triangles are left out
    view: tuple[float, float, float, float] | None = None   # the view ahead kept open: yaw left, yaw right, lowest
                                                            # pitch, highest pitch (degrees); what reaches into it
                                                            # (farther than 0.2 m) is left out
    what: str = ''
    drop_blended: bool = False          # every blended material left out (a model whose glass is only told apart by
                                        # its alpha mode: material names that are ids)


# The War Thunder F-16A Block 15's cockpit (the game's own model, as shared on Sketchfab; 1:1, metres, nose +z, +x the
# pilot's left; the user, 2026-10-14: "座舱和模型干脆直接也搬战雷的吧"): every opaque material round the pilot's eye
# 0.75 m over the seat's cushion, just before the ejection seat's headbox (the plugin's fighters: the interceptor, the
# fighter / strike jet, the multirole and the Primer fighter). Its glass (the canopy, the HUD's combiner: blended
# materials, named by ids only) is left out: the plugin draws the HUD's symbols on the world (the 'hud' screen).
WT_F16A = Asset('a6fa145facf045fca90ac499340db5b0', 'F-16A Block 15 Fighting Falcon (War Thunder)', (0.0, 0.95, 4.12),
                '+z', '+y', 1.0, ((-0.62, -1.15, -0.42), (0.62, 0.55, 1.75)), drop_blended=True,
                what="the cockpit round the pilot's seat only; the canopy's and the HUD's glass left out")
# The F-16D Block 60's front seat (the model 1:1, its nose -z): its cockpit materials (cock*, ins*) round the pilot's
# eye 0.13 m before the headrest, less the canopy's glass and the HUD's combiner glass (its symbols are drawn by the
# plugin on the world, the 'hud' screen), and nothing of the rear seat's cockpit behind the front seat's bulkhead.
F16D = Asset('ea8edb08d79e4eafa9c794c581f88cf3', 'General Dynamics F-16D Block 60', (0.0, 2.6, -3.4), '-z', '+y', 1.0,
             ((-0.62, -1.15, -0.42), (0.62, 0.55, 1.75)), keep=('cock', 'ins'), drop=('glass',),
             what="the front seat's cockpit only; the canopy and the HUD's glass left out")
# The Chengdu J-10S's front seat (the model 1:1, nose -z): a modern fighter's three-display panel (the plugin's other
# fighters: the F-35's stand-ins), its cockpit materials (cock*) less the canopy, the HUD glass and the windscreen's
# arch (it crossed the view ahead 3 degrees over the line of sight; the HUD's posts stay).
J10S = Asset('aa7c517fbded47439a526cafb78b4618', 'Chengdu J-10S', (0.0, 1.08, -3.17), '-z', '+y', 1.0,
             ((-0.62, -1.15, -0.42), (0.62, 0.55, 1.75)), keep=('cock',), drop=('glass', 'HUD'),
             cut=(((0.15, -0.13, 0.35), (1.0, 0.6, 1.9)), ((-1.0, -0.13, 0.35), (-0.15, 0.6, 1.9)),
                  ((0.3, -0.32, 0.35), (1.0, 0.6, 1.9)), ((-1.0, -0.32, 0.35), (-0.3, 0.6, 1.9)),
                  ((-0.15, 0.0, 0.35), (0.15, 0.6, 1.9))),
             what="the front seat's cockpit only; the canopy, the windscreen's arch and the HUD's glass left out")
# The C-130J's flight deck from the left (pilot's) seat (model units 0.0505 m: 40.4 m span over 800 units; nose +z):
# the eye 0.77 m over the seat's cushion, so the glare shield is 10-12 degrees under the line of sight; the view ahead
# kept open over it (40 degrees left, 18 right, 9 down to 32 up: the overhead console stays): the pilot's HUD (an
# opaque slab in this model, right on the line of sight 0.4 m ahead), the windscreen's posts and the bar under the
# eyebrow windows left out.
C130_SCALE = 40.41 / 800.0
C130_DECK = Asset('223b4ff4abb945038fe35726bedd0db1', 'Lockheed Martin C130J "Super Hercules"',
                  (11.0, 78.5, 158.0), '+z', '+y', C130_SCALE, ((-3.2, -1.3, -1.4), (1.4, 1.2, 2.2)),
                  view=(40.0, 18.0, -9.0, 32.0),
                  what="the flight deck only; the pilot's HUD and the windscreen's frame ahead left out")
# The C-130J's cargo hold round a passenger in the middle of the right-hand troop seats' aft run (it stands against
# the wall; forward of the side door the hold widens and the seats stand off it), looking across (the eye turned to
# +x): his back 0.3 m off the wall, 0.8 m over the seat; the far wall 2.6 m off, the run 1.4 m either way.
C130_HOLD = Asset('223b4ff4abb945038fe35726bedd0db1', 'Lockheed Martin C130J "Super Hercules"',
                  (-23.5, 50.0, -32.0), '+x', '+y', C130_SCALE, ((-2.2, -1.4, -0.45), (2.2, 1.9, 2.8)),
                  what='the cargo hold only')
# The UH-60M's troop cabin from the right-hand middle seat of the forward-facing row (model units 8.09 m: the cabin's
# 2.2 m width over 0.27 units; nose +z): the crew figures and the windows' glass left out.
UH60_SCALE = 8.09
UH60_CABIN = Asset('12b4e525676a49678ac3006b360c8750', 'Sikorsky UH-60M Blackhawk', (-0.029, -0.075, 0.31), '+z', '+y',
                   UH60_SCALE, ((-1.2, -1.3, -2.4), (1.6, 0.95, 3.4)), drop=('Plt', 'mat_6-', 'mat_7-'),
                   what="the troop cabin and the cockpit ahead of it; the crew figures and the windows' glass left out")

# F16D and J10S no longer back a kit (the War Thunder F-16A took their seats); they stay while their parts are in the
# model folder, so its credits still name them.
ASSETS: dict[str, Asset] = {
    'plugin_wt_f16a': WT_F16A,
    'plugin_f16d': F16D,
    'plugin_j10s': J10S,
    'plugin_c130_deck': C130_DECK,
    'plugin_c130_hold': C130_HOLD,
    'plugin_uh60_cabin': UH60_CABIN,
}

LICENSE = """{title} by {author}, CC BY 4.0
Source: {page} (via Sketchfab)
Licence: Creative Commons Attribution 4.0 (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/
Changes: {what}; turned into the seat's eye frame and scaled to metres, triangles facing away from the eye left out
(a double-sided material's turned to it); textures resized and re-saved as PNG. The EDF6 plugin's cockpit builder
(pylib/cockpit_kits_plugin.py) cuts it to each vehicle's room.
"""


def default_out() -> str:
    return os.path.join(om.DEV_MODELS, 'cockpits')


def basis(a: Asset) -> np.ndarray:
    """Rows: the eye frame's left, up and ahead in the model's axes (left = up x ahead: a rotation, winding kept)."""
    ahead, up = np.array(AXES[a.ahead], dtype=np.float64), np.array(AXES[a.up], dtype=np.float64)
    return np.stack([np.cross(up, ahead), up, ahead])


def node_names(path: str) -> list[str]:
    """Each mesh node's name in gltf.read's walk order (Scene.mesh indexes it)."""
    g, _ = gltf._container(path)  # noqa: SLF001 - the same JSON gltf.read walks
    names: list[str] = []

    def walk(i: int) -> None:
        n = g['nodes'][i]
        if 'mesh' in n:
            names.append(n.get('name') or g['meshes'][n['mesh']].get('name', ''))
        for c in n.get('children', []):
            walk(c)
    for r in g['scenes'][g.get('scene', 0)]['nodes']:
        walk(r)
    return names


def double_sided(path: str) -> list[bool]:
    g, _ = gltf._container(path)  # noqa: SLF001
    return [bool(m.get('doubleSided')) for m in g.get('materials', [])]


def inside(c: np.ndarray, box: Box) -> np.ndarray:
    return ((c >= np.array(box[0])) & (c <= np.array(box[1]))).all(1)


def in_view(p: np.ndarray, view: tuple[float, float, float, float]) -> np.ndarray:
    left, right, low, high = view
    flat = np.hypot(p[:, 0], p[:, 2])
    pitch = np.degrees(np.arctan2(p[:, 1], flat))
    yaw = np.degrees(np.arctan2(p[:, 0], np.maximum(p[:, 2], 1e-9)))   # + left
    return (p[:, 2] > 0.2) & (yaw <= left) & (yaw >= -right) & (pitch >= low) & (pitch <= high)


def view_cone(centre: np.ndarray, corners: np.ndarray, view: tuple[float, float, float, float]) -> np.ndarray:
    return in_view(centre, view) | np.logical_or.reduce([in_view(corners[:, k], view) for k in range(3)])


def cut_out(a: Asset, path: str, scene: gltf.Scene) -> dict[str, np.ndarray]:
    """The asset's kept triangles in the eye frame: positions, normals, UVs, triangles, materials."""
    m = basis(a)
    pos = (scene.positions - np.array(a.eye)) @ m.T * a.scale
    if scene.normals is None:
        raise SystemExit(f'{a.uid}: the model has no normals')
    nrm = scene.normals @ m.T
    tri = scene.triangles.copy()
    corners = pos[tri]
    centre = corners.mean(1)
    names = [mt.name.lower() for mt in scene.materials]
    lo_box, hi_box = np.array(a.box[0]), np.array(a.box[1])
    keep = ((corners.max(1) >= lo_box) & (corners.min(1) <= hi_box)).all(1)   # reaching into the box (the kit clips)
    if a.keep:
        keep &= np.isin(scene.material, [k for k, n in enumerate(names) if any(s.lower() in n for s in a.keep)])
    if a.drop:
        keep &= ~np.isin(scene.material, [k for k, n in enumerate(names) if any(s.lower() in n for s in a.drop)])
    if a.drop_blended:
        keep &= ~np.isin(scene.material, [k for k, mt in enumerate(scene.materials) if mt.alpha_mode == 'BLEND'])
    if a.drop_nodes:
        nodes = node_names(path)
        dropped = [k for k, n in enumerate(nodes) if any(s.lower() in n.lower() for s in a.drop_nodes)]
        keep &= ~np.isin(scene.mesh, dropped)
    for box in a.cut:   # by its centre or two of its corners (a long sliver's centre can lie outside)
        corners_in = sum(inside(corners[:, k], box).astype(int) for k in range(3))
        keep &= ~(inside(centre, box) | (corners_in >= 2))
    if a.view is not None:
        keep &= ~view_cone(centre, corners, a.view)
    face = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    toward = (face * -corners[:, 0]).sum(1) > 0          # the eye on its front side (counter-clockwise front)
    sided = np.array(double_sided(path) + [False])[np.minimum(scene.material, len(scene.materials))]
    flip = keep & ~toward & sided
    tri[flip] = tri[flip][:, ::-1]                       # a double-sided face seen from its back: turned to the eye
    keep &= toward | sided
    tris, mats = tri[keep], scene.material[keep]
    if flip.any():   # the turned faces' corners need their normals turned too: give them corners of their own
        turned = flip[keep]
        extra = tris[turned].reshape(-1)
        base = len(pos)
        pos = np.vstack([pos, pos[extra]])
        nrm = np.vstack([nrm, -nrm[extra]])
        uvs = scene.uvs if scene.uvs is not None else np.full((base, 2), 0.5)
        uvs = np.vstack([uvs, uvs[extra]])
        tris[turned] = base + np.arange(len(extra)).reshape(-1, 3)
    else:
        uvs = scene.uvs if scene.uvs is not None else np.full((len(pos), 2), 0.5)
    used, inv = np.unique(tris.reshape(-1), return_inverse=True)
    return {'pos': pos[used].astype(np.float32), 'nrm': nrm[used].astype(np.float32),
            'uv': uvs[used].astype(np.float32),
            'tris': inv.reshape(-1, 3).astype(np.int32), 'mat': mats.astype(np.int16)}


def textures(scene: gltf.Scene, used: set[int], dest: str) -> tuple[list[str], np.ndarray]:
    """Each material's base colour texture as a PNG in `dest` (its colour factor multiplied in; '' none; identical
    images saved once) and the materials' sRGB base colours."""
    names, saved = [], {}
    for k, mat in enumerate(scene.materials):
        if k not in used or not mat.texture:
            names.append('')
            continue
        key = hashlib.sha256(mat.texture + repr(mat.base[:3]).encode()).hexdigest()
        if key not in saved:
            img = Image.open(io.BytesIO(mat.texture)).convert('RGB')
            img.thumbnail((MAX_TEX, MAX_TEX), Image.LANCZOS)
            if any(abs(c - 1.0) > 1e-3 for c in mat.base[:3]):
                f = np.array([min(1.0, max(0.0, c)) ** (1 / 2.2) for c in mat.base[:3]])
                img = Image.fromarray((np.asarray(img, dtype=np.float32) * f).clip(0, 255).astype(np.uint8))
            saved[key] = f'tex{len(saved)}.png'
            img.save(os.path.join(dest, saved[key]))
        names.append(saved[key])
    colours = np.array([[round(255 * min(1.0, max(0.0, c)) ** (1 / 2.2)) for c in m.base[:3]] for m in scene.materials],
                       dtype=np.uint8)
    return names, colours


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=r'D:\APP\edf6-cockpit-assets')
    ap.add_argument('--out', default=None)
    ap.add_argument('--only', action='append', help='only this asset slug (the credits still name all)')
    a = ap.parse_args()
    out = a.out or default_out()
    credits = ["The plugin's own vehicles' cockpits (pylib/cockpit_kits_plugin.py): third-party material", '']
    scenes: dict[str, gltf.Scene] = {}
    for slug, asset in ASSETS.items():
        src = os.path.join(a.src, 'sketchfab', asset.uid, 'model.glb')
        with open(os.path.join(os.path.dirname(src), 'license.json'), encoding='utf-8') as h:
            meta = json.load(h)
        if meta['license']['slug'] not in ('by', 'cc0'):
            raise SystemExit(f'{asset.uid}: licence {meta["license"]["slug"]}: not used')
        author = meta['user']['displayName']
        page = meta.get('viewerUrl') or f'https://sketchfab.com/3d-models/{asset.uid}'
        dest = os.path.join(out, 'parts', slug)
        title = meta.get('name') or asset.title
        credits.append(f'{title} by {author}, CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/), via '
                       f'Sketchfab: {page} ({asset.what}; cropped/scaled)  ->  parts/{slug}/cockpit.npz, '
                       f'parts/{slug}/tex*.png')
        if a.only and slug not in a.only:
            continue
        if asset.uid not in scenes:
            scenes[asset.uid] = gltf.read(src)
        scene = scenes[asset.uid]
        data = cut_out(asset, src, scene)
        os.makedirs(dest, exist_ok=True)
        for old in os.listdir(dest):
            if old.startswith('tex') and old.endswith('.png'):
                os.remove(os.path.join(dest, old))
        names, colours = textures(scene, set(int(k) for k in np.unique(data['mat'])), dest)
        np.savez_compressed(os.path.join(dest, 'cockpit.npz'), textures=np.array(names), colours=colours,
                            material_names=np.array([m.name for m in scene.materials]), **data)
        with open(os.path.join(dest, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
            h.write(LICENSE.format(title=title, author=author, page=page, what=asset.what))
        lo, hi = data['pos'].min(0), data['pos'].max(0)
        print(f'{slug}: {len(data["tris"])} triangles of {len(scene.triangles)}, {sum(1 for n in names if n)} textured '
              f'materials, extent {lo.round(2)} .. {hi.round(2)} -> {dest}')
    with open(os.path.join(out, 'CREDITS_plugin.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(credits) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
