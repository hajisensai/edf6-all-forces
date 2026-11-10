"""Developer-only: the support air carrier's model (docs/feature-support-jet-models.md §6) out of the Sketchfab
"Helicarrier" (uid 97cf6cccb2fa48c18fbef5c5600c434d, CC BY 4.0: SOURCE.txt) into the model folder the installer builds
it from (pylib/helicarrier_model.py):

    <models>/helicarrier/helicarrier.obj / .mtl   the carrier in game space and metres (+Y up, +Z the bow, +X its
                                                  left), standing on its lowest point, centred on x and z, LENGTH m
                                                  long; one OBJ object per bone (helicarrier.json), every triangle rigid
                                                  on it: `helicarrier` the hull, `boosterF_l` / `boosterF_r` /
                                                  `boosterB_l` / `boosterB_r` its four lift fans
    <models>/helicarrier/helicarrier_df.png       its colour (the source's texture, as it is)
    <models>/helicarrier/helicarrier_nm.png       a flat normal map (the source has none)
    <models>/helicarrier/helicarrier_rml.png      R roughness, G metal, B 0 (no lights)
    <models>/helicarrier/helicarrier.json         its bones (pivot, parent), its fans (hub, diameter, exhaust point),
                                                  material and size
    <models>/helicarrier/SOURCE.txt               where it came from

    python tools/prep_helicarrier.py <the downloaded .glb> [<models folder>]

The .glb is the model's Sketchfab download (the glTF binary the download API gives, read as data by pylib/gltf.py:
nothing in it runs). Its frame is the game's (glTF: +Y up, right-handed; the bow, the twin catapult prongs, at +z, the
island on the starboard side, -x), so only a scale and a shift: no turn, windings kept.

The four lift fans (each a hub with four spokes and its rotor rim, inside a ducted ring of the hull) are the bones the
plugin tilts (src/jet_flight.cpp Thrusters: the V508's tilting pods' names, boosterF_l .. boosterB_r; local =
Rx(theta) x bind, theta 0 .. -110 deg, -90 hovering: thrust along the body's up). Their bind is "level, thrust along
+z": so each fan is written turned +90 deg about x round its hub (its disc standing across the bow, its downwash out
of its back, -z), and hovering the plugin lays it back flat in its ring, where the source has it. A fan is cut out of
the source's one welded mesh round its hub (FANS): the hub (every vertex within HUB_R of the hub's axis, over HUB_Y),
and what joins it within the fan's radius in the disc's height (DISC_Y), not the ring's inner wall (a face standing
upright and facing the hub's axis): the spokes and the rotor rim.
"""
from __future__ import annotations

import collections
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))

DEV_MODELS = r'D:\APP\edf6-models'
OUT_SUBDIR = 'helicarrier'
MODEL = 'helicarrier'
MANIFEST = 'helicarrier.json'
SOURCE_URL = 'https://sketchfab.com/3d-models/helicarrier-97cf6cccb2fa48c18fbef5c5600c434d'
UID = '97cf6cccb2fa48c18fbef5c5600c434d'
LENGTH = 80.0           # m bow to stern (the V508 carrier it replaces: 76.8 m)
TRIANGLES = 12841       # the source's
# The fans in the source's frame: bone -> (hub guess x, z; the fan's radius to cut within). The hub is then the middle
# of the vertices within HUB_FIND of the guess, twice over.
FANS = {'boosterF_l': (76.5, 26.0, 24.0), 'boosterF_r': (-76.5, 26.0, 24.0),
        'boosterB_l': (68.5, -91.3, 23.5), 'boosterB_r': (-67.5, -91.3, 23.5)}
HUB_FIND = 8.0
HUB_R, HUB_Y = 12.0, 41.5         # the hub: within HUB_R of the axis, over HUB_Y
DISC_Y = (46.0, 54.2)             # the spokes' and the rim's height
WALL_R, WALL_UP, WALL_RADIAL = 17.0, 0.5, 0.6   # the ring's inner wall: past WALL_R, |n.y| < WALL_UP, |n.radial| > WALL_RADIAL
FAN_TRIANGLES = (450, 700)        # each fan's (the source: 513 - 615)
ROUGH, METAL = 150, 60
FLAME_PER_DIAMETER = 4.0          # a fan's flame: as wide as its disc, this many discs long (jet_models.NozzleBone)


class PrepError(Exception):
    pass


def _req(ok: bool, msg: str) -> None:
    if not ok:
        raise PrepError(msg)


def fan_triangles(P, T, hx: float, hz: float, radius: float):  # noqa: ANN001, ANN201 - numpy arrays
    """(hub x, z, the triangles of the fan round (hx, hz)): see the module's docstring."""
    import numpy as np
    for _ in range(2):
        near = np.hypot(P[:, 0] - hx, P[:, 2] - hz) < HUB_FIND
        hx = float(P[near, 0].min() + P[near, 0].max()) / 2.0
        hz = float(P[near, 2].min() + P[near, 2].max()) / 2.0
    r = np.hypot(P[:, 0] - hx, P[:, 2] - hz)[T]
    y = P[T][:, :, 1]
    tri = P[T]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    c = tri.mean(1)
    radial = np.stack([c[:, 0] - hx, np.zeros(len(c)), c[:, 2] - hz], 1)
    rc = np.linalg.norm(radial, axis=1)
    radial /= rc[:, None] + 1e-9
    wall = (rc > WALL_R) & (np.abs(n[:, 1]) < WALL_UP) & (np.abs((n * radial).sum(1)) > WALL_RADIAL)
    ok = (((r < HUB_R).all(1) & (y >= HUB_Y).all(1)) |
          ((r < radius).all(1) & (y >= DISC_Y[0]).all(1) & (y <= DISC_Y[1]).all(1))) & ~wall
    _, weld = np.unique(np.round(P, 3), axis=0, return_inverse=True)
    weld = weld.ravel()
    edges: dict[tuple[int, int], list[int]] = collections.defaultdict(list)
    for i in np.where(ok)[0]:
        a, b, cc = weld[T[i]]
        for e in ((a, b), (b, cc), (cc, a)):
            edges[(min(e), max(e))].append(int(i))
    seed = [int(i) for i in np.where(ok)[0] if (r[i] < 7.0).all()]
    _req(bool(seed), f'no hub at ({hx:.1f}, {hz:.1f})')
    picked = set(seed)
    todo = list(seed)
    while todo:
        a, b, cc = weld[T[todo.pop()]]
        for e in ((a, b), (b, cc), (cc, a)):
            for j in edges[(min(e), max(e))]:
                if j not in picked:
                    picked.add(j)
                    todo.append(j)
    return hx, hz, sorted(picked)


def build(glb: str) -> dict:
    """{'positions', 'normals', 'uvs' per triangle corner (game space), 'bone' per triangle, 'fans', 'texture'}."""
    import numpy as np
    import gltf
    s = gltf.read(glb)
    _req(len(s.triangles) == TRIANGLES and len(s.materials) == 1 and s.materials[0].texture is not None and s.uvs is not None,
         f'{glb}: {len(s.triangles)} triangles, {len(s.materials)} materials: not the Helicarrier this was made for')
    P, T = s.positions, s.triangles
    N = s.normals
    if N is None:
        f = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        N = np.zeros_like(P)
        for k in range(3):
            np.add.at(N, T[:, k], f)
        N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
    lo, hi = P.min(0), P.max(0)
    scale = LENGTH / float(hi[2] - lo[2])
    cut = {name: fan_triangles(P, T, gx, gz, radius) for name, (gx, gz, radius) in FANS.items()}
    # centred on x on the fans' middle (the hull's plane of symmetry: its box is 1.3 source units off it, a lopsided
    # antenna), on z on its box; standing on its lowest point
    shift = np.array([-sum(c[0] for c in cut.values()) / len(cut), -lo[1], -(lo[2] + hi[2]) / 2.0])
    bone = np.array([MODEL] * len(T), dtype=object)
    corners = P[T].copy()                 # (m, 3, 3) source frame
    normals = N[T].copy()
    fans = {}
    for name, (hx, hz, tris) in cut.items():
        _req(FAN_TRIANGLES[0] <= len(tris) <= FAN_TRIANGLES[1], f'{name}: {len(tris)} triangles cut out')
        _req(not any(bone[t] != MODEL for t in tris), f'{name}: shares triangles with another fan')
        bone[tris] = name
        pts = corners[tris].reshape(-1, 3)
        ylo, yhi = float(pts[:, 1].min()), float(pts[:, 1].max())
        hub = np.array([hx, (ylo + yhi) / 2.0, hz])
        reach = float(np.hypot(pts[:, 0] - hx, pts[:, 2] - hz).max())
        # turned +90 deg about x round the hub: (x, y, z) -> (x, -z, y) of the offset (up becomes +z)
        d = corners[tris] - hub
        corners[tris] = hub + np.stack([d[..., 0], -d[..., 2], d[..., 1]], -1)
        nn = normals[tris]
        normals[tris] = np.stack([nn[..., 0], -nn[..., 2], nn[..., 1]], -1)
        # the downwash leaves from under the hub (its lowest point, now its back): -z in the bind
        exhaust = hub + np.array([0.0, 0.0, ylo - hub[1]])
        fans[name] = {'pivot': [round(float(v), 5) for v in (hub + shift) * scale],
                      'exhaust': [round(float(v), 5) for v in (exhaust + shift) * scale],
                      'diameter': round(2.0 * reach * scale, 4), 'triangles': len(tris)}
    _req(abs(fans['boosterF_l']['pivot'][0] + fans['boosterF_r']['pivot'][0]) < 0.3 and
         abs(fans['boosterB_l']['pivot'][0] + fans['boosterB_r']['pivot'][0]) < 0.3, f'fans not in mirrored pairs: {fans}')
    corners = (corners + shift) * scale
    low = float(corners[..., 1].min())
    _req(abs(low) < 1e-6, f'turning the fans moved the lowest point to {low:.3f} m: they hang under the hull')
    uv = s.uvs[T]
    return {'positions': corners, 'normals': normals, 'uvs': uv, 'bone': bone, 'fans': fans, 'scale': scale,
            'texture': s.materials[0].texture, 'extras': s.extras}


def write(folder: str, made: dict) -> dict:
    """helicarrier.obj / .mtl, the textures, the manifest entry."""
    from PIL import Image
    import numpy as np
    lines = [f'# {MODEL}: tools/prep_helicarrier.py from the Sketchfab "Helicarrier" ({SOURCE_URL}, CC BY 4.0)',
             f'mtllib {MODEL}.mtl']
    n = 0
    order = [MODEL, *FANS]
    for b in order:
        lines += [f'o {b}', f'usemtl {MODEL}']
        for t in np.where(made['bone'] == b)[0]:
            f = []
            for k in range(3):
                p, q, uv = made['positions'][t, k], made['normals'][t, k], made['uvs'][t, k]
                n += 1
                lines += [f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}', f'vt {uv[0]:.6f} {1.0 - uv[1]:.6f}',
                          f'vn {q[0]:.5f} {q[1]:.5f} {q[2]:.5f}']
                f.append(f'{n}/{n}/{n}')
            lines.append('f ' + ' '.join(f))
    with open(os.path.join(folder, f'{MODEL}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('\n'.join(lines) + '\n')
    with open(os.path.join(folder, f'{MODEL}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(f'newmtl {MODEL}\nmap_Kd {MODEL}_df.png\n')
    im = Image.open(io.BytesIO(made['texture'])).convert('RGB')
    im.save(os.path.join(folder, f'{MODEL}_df.png'))
    Image.new('RGB', im.size, (128, 128, 255)).save(os.path.join(folder, f'{MODEL}_nm.png'))
    Image.new('RGB', im.size, (ROUGH, METAL, 0)).save(os.path.join(folder, f'{MODEL}_rml.png'))
    pts = made['positions'].reshape(-1, 3)
    bones = {MODEL: {'parent': None, 'pivot': [0.0, 0.0, 0.0]}}
    nozzles = {}
    for b, fan in made['fans'].items():
        bones[b] = {'parent': MODEL, 'pivot': fan['pivot']}
        nozzles[b] = {'at': fan['exhaust'], 'width': fan['diameter'], 'length': round(FLAME_PER_DIAMETER * fan['diameter'], 4)}
    return {'bones': bones, 'materials': [MODEL], 'fans': made['fans'], 'nozzles': nozzles,
            'size': [round(float(v), 2) for v in np.ptp(pts, axis=0)], 'polygons': int(len(made['bone'])),
            'scale': round(made['scale'], 6)}


def source_text(extras: dict) -> str:
    return f"""Helicarrier model used by EDF6VehicleCrew (free mod): the support air carrier / drone carriers
(helicarrier.obj).

Source: "Helicarrier" on Sketchfab, {SOURCE_URL}
  uid {UID}; uploaded by ramonaflowers (display name "RamonaFlowers"), published 2023-11-29; the download's own
  metadata names its author "{extras.get('author', '?')}".
  Downloaded 2026-11-10 through the Sketchfab download API (glTF binary).
Licence: "CC Attribution" (Sketchfab's name; its full name "Creative Commons Attribution", the page's link
  http://creativecommons.org/licenses/by/4.0/; the download's metadata: "{extras.get('license', '?')}").
Changes: scaled to {LENGTH:.0f} m long, stood on its lowest point and centred; its four lift fans (hub, spokes and rotor
rim) cut out of the hull as parts of their own and turned upright about their hubs (the mod tilts them back as it
flies); flat normal map and roughness map added; converted to the game's model format by the mod's installer, which
adds its bones and collision.
Made by tools/prep_helicarrier.py.
"""


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    out_root = argv[1] if len(argv) > 1 else DEV_MODELS
    folder = os.path.join(out_root, OUT_SUBDIR)
    os.makedirs(folder, exist_ok=True)
    made = build(argv[0])
    entry = write(folder, made)
    print(MODEL, json.dumps({k: v for k, v in entry.items() if k not in ('bones', 'nozzles')}))
    with open(os.path.join(folder, MANIFEST), 'w', encoding='utf-8', newline='\n') as h:
        json.dump({'source': SOURCE_URL, 'uid': UID, 'models': {MODEL: entry}}, h, indent=1)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(source_text(made['extras']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
