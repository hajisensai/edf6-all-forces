"""The ground vehicles' cockpit interiors cut out of real 3D models (a developer step, like tools/prep_sazabi.py; the
installer reads the results, it needs neither PIL nor the sources).

Each SOURCES entry is a vehicle model downloaded from Sketchfab under CC BY (its API page saved beside it as
license.json; <assets>/sketchfab/<uid>/model.glb): the cab round the driver's eye is turned into the eye's frame
(+x left, +y up, +z ahead, metres, the eye at the origin) and cut down to what that eye can see:

  1. glass (blended materials, *glass* / *window* names) and the parts named in `drop` are left out: nothing in the
     line of sight but the open windscreen;
  2. only the triangles whose centre lies in `box` (eye frame) are kept: the cab, not the chassis or the cargo bed;
  3. only the triangles the eye sees: drawn with tools/cabin_view.py's rasteriser into the six faces of a cube round
     the eye (and round eyes `JITTER` m off it: the head moves a little), every triangle that wins a pixel is kept
     (the outer shell, the engine, the wheels and the undersides go);
  4. every triangle is turned to face the eye: the sources are double-sided (Sketchfab draws both sides), the game
     draws only fronts;
  5. every edge longer than EDGE is split: the kit draws the cab in toward the eye where the vehicle is smaller,
     vertex by vertex (pylib/cockpit_kits_ground.py squeeze), and a big face needs vertices to follow.

What is written to <model dir>/cockpits/parts/<asset>/: cab.npz (positions, normals, UVs, triangles, each triangle's
material; per material its sRGB colour and texture file) and each textured material's base colour texture, times its
colour factor, as a PNG of at most 1024 px (m<k>.png), and LICENSE.txt.
CREDITS_ground.txt lists every source with its author, licence and page. The sources are read as data only (pylib/
gltf.py parses them); nothing in them is run.

    python tools/prep_cockpit_ground.py [--assets D:/APP/edf6-cockpit-assets] [--out <model dir>/cockpits]
        [--only ASSET] [--view DIR]
--view: also renders each cut cab from its eye (ahead, left, right, behind) into DIR, to check the eye and the cut.
"""
from __future__ import annotations

import argparse
import io
import json
import math
import os
import re
import shutil
import sys
from dataclasses import dataclass, field

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import cabin_view as cv  # noqa: E402
import gltf  # noqa: E402
import obj_model as om  # noqa: E402

Vec3 = tuple[float, float, float]
TEX_LIMIT = 1024
JITTER = 0.08            # m: the other eyes of the visibility pass, this far off the eye along each axis
CUBE_PX = 512            # each cube face's pixels a side in the visibility pass
GLASS = re.compile(r'glass|window|transp|windshield|windscreen', re.I)


@dataclass(frozen=True)
class Source:
    """A cab cut from Sketchfab model `uid` (<assets>/sketchfab/<uid>/model.glb) into parts/<asset>/: the eye at `eye`
    (the source's coordinates) looking along `ahead` with `up` up, the source's unit `scale` metres; `mirror`: the
    source's left and right swapped (a left-hand-drive cab for a vehicle driven from the right); `box`: (lo, hi) in
    the eye frame, the triangles kept; `drop`: regular expressions of node or material names left out."""
    asset: str
    uid: str
    title: str
    author: str
    eye: Vec3
    ahead: Vec3
    up: Vec3
    box: tuple[Vec3, Vec3]
    scale: float = 1.0
    mirror: bool = False
    drop: tuple[str, ...] = ()
    shows: str = ''


SOURCES = (
    Source('sf-sisu-gtp-10seat', '7100033510ba4ccebfaac7877b63638e', 'SISU GTP 4x4 Armored Vehicle 10 Seater version',
           'tamminen', (-0.45, 2.3, 0.59), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), ((-1.9, -1.2, -0.9), (0.9, 0.6, 2.0)),
           drop=('^floor',), shows="an armoured car's driver's station: dashboard, wheel, pillars and roof"),
    Source('sf-arhan-p51', '6f92e0acc0d44989a6ae3b74fc67cc62', 'ARHAN P51 "VALTRA"', 'Mayess', (27.0, 33.5, -178.0),
           (0.0, 0.0, 1.0), (0.0, 1.0, 0.0), ((-1.9, -1.3, -1.0), (0.9, 0.7, 1.6)), scale=0.018,
           shows="an armoured car's driver's station: dashboard with its gauges, wheel, radio console, doors"),
    Source('sf-arhan-p880', 'a85231fbbfac482ea0bb3fc7e2758cd8', 'ARHAN P880 6x6', 'Mayess', (0.2, 0.45, -0.42),
           (0.0, 0.0, 1.0), (0.0, 1.0, 0.0), ((-0.9, -1.4, -0.45), (2.0, 1.0, 1.5)), scale=2.4, mirror=True,
           shows="a cab-over truck's cab (mirrored: driven from the right): dashboard, wheel, the big windscreen's "
                 "pillars, roof console, seats"),
    Source('sf-lightbody-85', '67beae18c3d24be68f9c8f0ec382d8e3', "Lightbody Lifted '85 - Low poly model",
           'DanielZhabotinsky', (0.0, 1.42, -0.38), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0),
           ((-0.6, -1.0, -0.7), (1.3, 0.5, 1.3)), mirror=True,
           shows="a pickup's cab (mirrored: driven from the right): dashboard with its gauges, wheel, doors, roof"),
    # Not a cab: a moped's digital speedometer, seen from 3 units over its LCD (the bikes' screens' cases).
    Source('sf-stage6-speedo', '93432cfe04004b7c854e8eff680afccd', 'DIGITAL SPEEDOMETER STAGE6 R/T', 'emilisbrz',
           (0.0, 3.0, -0.2), (0.0, -1.0, 0.0), (0.0, 0.0, -1.0), ((-1.0, -1.0, 0.0), (1.0, 1.0, 1.0)), scale=0.055,
           shows="a moped's digital speedometer: the case round each bike screen"),
)


# ------------------------------------------------------------------------------------------ reading a source

@dataclass
class Cut:
    """A source in the eye frame: positions (n, 3), normals (n, 3), uvs (n, 2), triangles (m, 3), each triangle's
    material, the materials (name, sRGB colour, texture PNG bytes or None)."""
    pos: np.ndarray
    nrm: np.ndarray
    uv: np.ndarray
    tris: np.ndarray
    mat: np.ndarray
    materials: list[tuple[str, tuple[int, int, int], bytes | None]] = field(default_factory=list)


def srgb(linear: tuple[float, ...]) -> tuple[int, int, int]:
    """A linear colour factor in sRGB, none darker than 30 (near-black reads as holes under the game's light)."""
    return tuple(max(30, int(round(255 * min(1.0, max(0.0, c)) ** (1 / 2.2)))) for c in linear[:3])  # type: ignore[return-value]


def node_names(g: dict) -> list[str]:
    """The mesh nodes' names in pylib/gltf.py's walk order (Scene.mesh indexes them)."""
    names: list[str] = []

    def walk(i: int) -> None:
        node = g['nodes'][i]
        if 'mesh' in node:
            names.append(node.get('name') or g['meshes'][node['mesh']].get('name', ''))
        for c in node.get('children', []):
            walk(c)
    for r in g['scenes'][g.get('scene', 0)]['nodes']:
        walk(r)
    return names


def materials(path: str, scene: gltf.Scene) -> list[tuple[str, tuple[float, ...], bytes | None, bool]]:
    """Each material's name, linear base colour (RGBA), base colour texture and whether it is glass. pylib/gltf.py
    reads the metal-rough model; a KHR_materials_pbrSpecularGlossiness material's diffuse factor and texture are
    taken here."""
    g, binary = gltf._container(path)
    buffers = [binary if not b.get('uri') else open(os.path.join(os.path.dirname(path), b['uri']), 'rb').read()
               for b in g.get('buffers', [])]
    out = []
    for k, m in enumerate(scene.materials):
        base, tex = m.base, m.texture
        sg = g['materials'][k].get('extensions', {}).get('KHR_materials_pbrSpecularGlossiness')
        if sg is not None:
            base = tuple(sg.get('diffuseFactor', (1.0, 1.0, 1.0, 1.0)))
            if 'diffuseTexture' in sg:
                src = g['textures'][sg['diffuseTexture']['index']].get('source')
                tex = gltf._image(g, src, buffers, os.path.dirname(path))[0] if src is not None else None
        glass = (m.alpha_mode == 'BLEND' and base[3] < 0.95) or bool(GLASS.search(m.name))
        out.append((m.name, base, tex, glass))
    return out


def texture_png(data: bytes, factor: tuple[float, ...]) -> bytes:
    """A base colour texture times its colour factor, at most TEX_LIMIT px, as PNG."""
    img = Image.open(io.BytesIO(data)).convert('RGB')
    img.thumbnail((TEX_LIMIT, TEX_LIMIT), Image.LANCZOS)
    if any(abs(c - 1.0) > 1e-3 for c in factor[:3]):
        a = np.asarray(img, dtype=np.float64) / 255.0
        a = (a ** 2.2 * np.array(factor[:3])) ** (1 / 2.2)
        img = Image.fromarray((a * 255).clip(0, 255).astype(np.uint8))
    out = io.BytesIO()
    img.save(out, 'PNG', optimize=True)
    return out.getvalue()


def eye_frame(src: Source) -> np.ndarray:
    """Rows: the eye's left, up and ahead in the source's coordinates (mirrored: left the other way)."""
    a = np.array(src.ahead, dtype=np.float64)
    a /= np.linalg.norm(a)
    u = np.array(src.up, dtype=np.float64)
    u -= a * (u @ a)
    u /= np.linalg.norm(u)
    left = np.cross(u, a)
    return np.stack([-left if src.mirror else left, u, a])


def cut(src: Source, path: str) -> Cut:
    s = gltf.read(path)
    mats = materials(path, s)
    names = node_names(gltf._container(path)[0])
    drop = [re.compile(d, re.I) for d in src.drop]
    gone_mat = np.array([glass or any(d.search(n) for d in drop) for n, _, _, glass in mats])
    gone_node = np.array([any(d.search(n) for d in drop) for n in names] + [False])
    mesh = s.mesh if s.mesh is not None else np.zeros(len(s.triangles), dtype=np.int64)
    keep = ~gone_mat[s.material] & ~gone_node[np.minimum(mesh, len(names))]
    rot = eye_frame(src)
    pos = (s.positions - np.array(src.eye)) @ rot.T * src.scale
    nrm = s.normals @ rot.T if s.normals is not None else np.zeros_like(pos)
    tris = s.triangles.copy()
    if src.mirror:   # a mirroring frame turns every face inside out
        tris = tris[:, ::-1]
    lo, hi = np.array(src.box[0]), np.array(src.box[1])
    c = pos[tris].mean(1)
    keep &= ((c >= lo) & (c <= hi)).all(1)
    uv = s.uvs if s.uvs is not None else np.zeros((len(pos), 2))
    return Cut(pos, nrm, uv, tris[keep], s.material[keep],
               [(n, srgb(b), tex and texture_png(tex, b)) for n, b, tex, _ in mats])


# ------------------------------------------------------------------------------------------ what the eye sees

CUBE = [((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0)), ((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)),
        ((0, 1, 0), (0, 0, 1)), ((0, -1, 0), (0, 0, 1))]


def seen(pos: np.ndarray, tris: np.ndarray) -> np.ndarray:
    """Which triangles win a pixel of a cube round the eye (or round the eyes JITTER off it)."""
    hit = np.zeros(len(tris), dtype=bool)
    eyes = [np.zeros(3)] + [s * JITTER * np.eye(3)[k] for k in range(3) for s in (1.0, -1.0)]
    corners = pos[tris]
    for eye in eyes:
        for look, up in CUBE:
            cam = cv.camera(eye, eye + np.array(look, dtype=np.float64), np.array(up, dtype=np.float64))
            vc = (corners - eye) @ cam.T
            v, _, s = cv.clip_near(vc, 0.01)
            k = (CUBE_PX / 2) / math.tan(math.radians(45.0))
            q = np.stack([CUBE_PX / 2 + k * v[:, :, 0] / v[:, :, 2], CUBE_PX / 2 - k * v[:, :, 1] / v[:, :, 2]], -1)
            _, kbuf, _ = cv.rasterise(q, 1.0 / v[:, :, 2], CUBE_PX, CUBE_PX)
            hit[s[np.unique(kbuf[kbuf >= 0])]] = True
    return hit


def facing_eye(c: Cut) -> Cut:
    """Every triangle turned to face the eye (the origin), its corners' normals with it; the visible ones only."""
    vis = seen(c.pos, c.tris)
    tris = c.tris[vis]
    p = c.pos[tris]
    n = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    away = (n * (-p.mean(1))).sum(1) < 0
    tris = np.where(away[:, None], tris[:, ::-1], tris)
    # Corners of flipped faces get their own vertices with the normal turned.
    key = tris * 2 + away[:, None]
    used, inv = np.unique(key.reshape(-1), return_inverse=True)
    vid, flip = used // 2, (used % 2).astype(bool)
    nrm = np.where(flip[:, None], -c.nrm[vid], c.nrm[vid])
    return Cut(c.pos[vid], nrm, c.uv[vid], inv.reshape(-1, 3), c.mat[vis], c.materials)


EDGE = 0.22   # m: no edge longer (the kit draws a cab in toward the eye per vertex: long edges would bend wrongly)


def subdivide(c: Cut, limit: float = EDGE) -> Cut:
    """Every edge longer than `limit` split at its middle (the triangles round it into two, three or four), until none
    is: the kit moves each vertex along its own sight line (cockpit_kits_ground.squeeze), so a big face must have
    vertices to follow. An edge is split by where its ends are (not by their indices): faces that meet at a seam of
    the UVs split it alike, no crack opens."""
    pos, nrm, uv = list(map(tuple, c.pos)), list(map(tuple, c.nrm)), list(map(tuple, c.uv))
    tris, mats = [tuple(t) for t in c.tris], list(c.mat)
    while True:
        def key(a: int, b: int) -> tuple:
            pa, pb = tuple(round(x, 5) for x in pos[a]), tuple(round(x, 5) for x in pos[b])
            return (pa, pb) if pa < pb else (pb, pa)
        long_edges = set()
        for t in tris:
            for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                if math.dist(pos[a], pos[b]) > limit:
                    long_edges.add(key(a, b))
        if not long_edges:
            break
        mids: dict[tuple, int] = {}

        def mid(a: int, b: int) -> int:
            k = (a, b) if a < b else (b, a)
            if k not in mids:
                mids[k] = len(pos)
                pos.append(tuple((x + y) / 2 for x, y in zip(pos[a], pos[b])))
                nrm.append(tuple((x + y) / 2 for x, y in zip(nrm[a], nrm[b])))
                uv.append(tuple((x + y) / 2 for x, y in zip(uv[a], uv[b])))
            return mids[k]
        out_t, out_m = [], []
        for t, m in zip(tris, mats):
            a, b, cc = t
            split = [key(a, b) in long_edges, key(b, cc) in long_edges, key(cc, a) in long_edges]
            n = sum(split)
            if n == 0:
                out_t.append(t)
            elif n == 3:
                ab, bc, ca = mid(a, b), mid(b, cc), mid(cc, a)
                out_t += [(a, ab, ca), (ab, b, bc), (ca, bc, cc), (ab, bc, ca)]
            else:
                # Rotate so the first split edge is (v0, v1).
                k = split.index(True)
                v = (t[k], t[(k + 1) % 3], t[(k + 2) % 3])
                s2 = [split[(k + i) % 3] for i in range(3)]
                m01 = mid(v[0], v[1])
                if n == 1:
                    out_t += [(v[0], m01, v[2]), (m01, v[1], v[2])]
                elif s2[1]:   # (v0, v1) and (v1, v2)
                    m12 = mid(v[1], v[2])
                    out_t += [(v[0], m01, v[2]), (m01, v[1], m12), (m01, m12, v[2])]
                else:         # (v0, v1) and (v2, v0)
                    m20 = mid(v[2], v[0])
                    out_t += [(v[0], m01, m20), (m01, v[1], v[2]), (m01, v[2], m20)]
            out_m += [m] * (len(out_t) - len(out_m))
        tris, mats = out_t, out_m
    return Cut(np.array(pos), np.array(nrm), np.array(uv), np.array(tris, dtype=np.int64), np.array(mats), c.materials)


# ------------------------------------------------------------------------------------------ writing

def write_npz(c: Cut, dest: str) -> None:
    """cab.npz in `dest` (as tools/prep_cockpit_tank.py's station.npz: pos, nrm, uv, tris, mat, textures, colours;
    the triangles' materials renumbered to the used ones) and one PNG per textured material."""
    os.makedirs(dest, exist_ok=True)
    used = sorted(set(int(m) for m in c.mat))
    textures, colours = [], []
    for k, src in enumerate(used):
        _, rgb, png = c.materials[src]
        colours.append(rgb)
        textures.append(f'm{k}.png' if png else '')
        if png:
            with open(os.path.join(dest, f'm{k}.png'), 'wb') as h:
                h.write(png)
    remap = np.zeros(max(used) + 1, dtype=np.int32)
    remap[used] = np.arange(len(used))
    nrm = c.nrm / np.maximum(np.linalg.norm(c.nrm, axis=1, keepdims=True), 1e-9)
    np.savez_compressed(os.path.join(dest, 'cab.npz'), pos=c.pos.astype(np.float32), nrm=nrm.astype(np.float32),
                        uv=c.uv.astype(np.float32), tris=c.tris.astype(np.int32), mat=remap[c.mat],
                        textures=np.array(textures), colours=np.array(colours, dtype=np.uint8),
                        names=np.array([c.materials[k][0] for k in used]))


LICENSE = """{title} by {author}, CC BY 4.0
Source: {page} (downloaded through the Sketchfab API; its API page saved as license.json in the developer's asset
folder)
Licence: Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/
Changes: only the cab round the driver's seat is kept (cropped), turned into the seat's eye frame and scaled to metres;
glass and every part the seated driver cannot see are removed; faces are turned toward the driver; textures are resized
to at most 1024 px. The EDF6 plugin's cockpit builder (pylib/cockpit_kits_ground.py) places it in the vehicle.
"""


def view(c: Cut, dest_png: str) -> None:
    """The cut cab from its eye: ahead, left, right and behind, side by side (2 x 2)."""
    sc = cv.Scene()
    vcol = np.zeros((len(c.pos), 3))
    for k in set(int(m) for m in c.mat):
        vcol[c.tris[c.mat == k].reshape(-1)] = c.materials[k][1]
    textures, tex_of = [], {}
    for k, (_, _, png) in enumerate(c.materials):
        if png:
            tex_of[k] = len(textures)
            textures.append(np.asarray(Image.open(io.BytesIO(png)).convert('RGB'), dtype=np.float64))
    uv = c.uv.copy()
    sc.add(c.pos, c.tris, vcol, uv, np.array([tex_of.get(int(m), -1) for m in c.mat]), textures)
    tiles = []
    for look in ((0, 0, 1), (1, 0, 0.3), (-1, 0, 0.3), (0, -0.2, -1)):
        img, _ = cv.render(sc, np.zeros(3), np.array(look, dtype=np.float64), np.array([0.0, 1.0, 0.0]), 75, 640, 360,
                           0.01, True)
        cv.crosshair(img)
        tiles.append(img)
    out = Image.new('RGB', (1280, 720))
    for k, t in enumerate(tiles):
        out.paste(t, ((k % 2) * 640, (k // 2) * 360))
    out.save(dest_png)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=r'D:\APP\edf6-cockpit-assets')
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits'))
    ap.add_argument('--only')
    ap.add_argument('--view')
    a = ap.parse_args()
    credits = []
    for src in SOURCES:
        meta = json.load(open(os.path.join(a.assets, 'sketchfab', src.uid, 'license.json'), encoding='utf-8'))
        if meta['license']['slug'] not in ('by', 'cc0'):
            raise SystemExit(f'{src.uid}: licence {meta["license"]["slug"]} is not CC BY / CC0')
        page = meta.get('viewerUrl') or f'https://sketchfab.com/3d-models/{src.uid}'
        author = meta['user'].get('profileUrl', '')
        credits.append(f'{src.title} by {src.author} ({author}), CC BY 4.0 '
                       f'(https://creativecommons.org/licenses/by/4.0/), via Sketchfab: {page} (cropped/scaled)  ->  '
                       f'parts/{src.asset}/: {src.shows}')
        if a.only and a.only != src.asset:
            continue
        c = subdivide(facing_eye(cut(src, os.path.join(a.assets, 'sketchfab', src.uid, 'model.glb'))))
        dest = os.path.join(a.out, 'parts', src.asset)
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        write_npz(c, dest)
        with open(os.path.join(dest, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
            h.write(LICENSE.format(title=src.title, author=src.author, page=page))
        print(f'{src.asset}: {len(c.tris)} triangles -> {dest}')
        if a.view:
            os.makedirs(a.view, exist_ok=True)
            view(c, os.path.join(a.view, f'{src.asset}.png'))
    with open(os.path.join(a.out, 'CREDITS_ground.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write("The ground vehicles' cockpits (pylib/cockpit_kits_ground.py) use these sources:\n\n")
        h.write('\n'.join(credits) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
