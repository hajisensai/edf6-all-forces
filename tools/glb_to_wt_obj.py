"""Developer-only: a Sketchfab .glb of a tracked vehicle into the OBJ form tools/prep_wt_tracked.py reads (War Thunder's
own exports: one OBJ object a node, `vehicle#<node>_<n>`, its MTL's albedo maps), its pieces named by what they are, so
the War Thunder pipeline (bones, wheels, collision, muzzles, missiles) builds it as it builds the 2S6 or the BMPT.

    python tools/glb_to_wt_obj.py <kind> <model.glb> <out folder>      -> <out>/<kind>.obj, .mtl, textures/

A model's pieces (its triangles' connected pieces, welded at 1 mm, in game space: +y up, +z its front, +x its left,
metres, the tracks' ground at y 0, its running gear's middle at z 0) are named by its Source's classify function: the
node names its prep_wt_tracked.Vehicle's rules read (wheel_<side>_<station>, track_<side>, gun_barrel_<side>,
gun_<side>, turret, rocket (the launching tubes), x_root: the hull). The source is treated as data: nothing in it runs.
"""
from __future__ import annotations

import io
import os
import sys
from collections.abc import Callable
from dataclasses import dataclass

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)

import gltf  # noqa: E402
import texfile_accel  # noqa: E402
import prep_glb_model as pg  # noqa: E402



@dataclass(frozen=True)
class Source:
    yaw: float                                  # deg about +y taking the model's front onto +z
    classify: Callable[[str, np.ndarray, np.ndarray, int], str]   # (glb node, piece box lo, hi, triangles) -> node
    gear: str                                   # the glb node of its tracks: their box is centred on x 0, z 0
    normal_green: str = ''                      # its normal maps' green, measured ('down' DirectX, 'up' OpenGL;
                                                # texfile_accel.directx_normals): required when it has them


def _apocalypse(node: str, lo: np.ndarray, hi: np.ndarray, tris: int) -> str:
    """ska-ara's RA2 Apocalypse: two track units a side (each its two end wheels and three road wheels), the turret on
    the hull's middle with a missile pod either side and twin guns ahead of it."""
    c, s = (lo + hi) / 2, hi - lo
    side = 'l' if c[0] > 0 else 'r'
    if node == 'tracks':
        return f'track_{side}'
    if node == 'whzeels':
        if abs(s[1] - s[2]) < 0.1 and 0.5 < s[1] < 0.7:
            kind = 'end' if c[1] > 0.7 else 'road'      # the end wheels ride higher than the road wheels
            return f'wheel_{side}_{kind}_{c[2]:+.3f}'   # given their stations by stations()
        return f'track_{side}'
    if node in ('Object001', 'chains'):
        return 'x_root'                                 # the dozer blades and their chains, on the hull
    if abs(c[0]) < 1.0 and 2.35 < c[1] < 3.2 and c[2] > TURRET_Z + 1.0:
        gun = 'l' if c[0] > -0.1 else 'r'
        return f'gun_barrel_{gun}' if c[2] > TURRET_Z + 1.8 else f'gun_{gun}'
    if node == 'major' and tris == 144 and 1.2 < abs(c[0]) < 2.0 and 2.95 < c[1] < 3.5 and abs(c[2] - TURRET_Z + 0.45) < 0.4:
        return f'rocket@{c[0]:+.2f}{c[1]:+.2f}'         # every tube its own object (prep: missile instances)
    if np.hypot(c[0], c[2] - TURRET_Z) < 2.3 and c[1] > 2.0:
        if s[1] > 2.0 and s[0] < 0.2:
            return f'antenna_{side}'                    # the two whip antennas (prep: the radar bones' parts)
        return 'turret'
    return 'x_root'


def _tos1(node: str, lo: np.ndarray, hi: np.ndarray, tris: int) -> str:
    """lm9241221's low-poly TOS-1 (a T-72 hull: six road wheels a side, the small idler in front, the sprocket behind;
    the launcher pack on its turntable over the hull's back, its tubes' mouths ahead; a ground plane under it, left
    out)."""
    c, s = (lo + hi) / 2, hi - lo
    side = 'l' if c[0] > 0 else 'r'
    round_ = abs(s[1] - s[2]) < 0.05
    if node == 'Object_2':
        return 'drop'                                   # the ground plane the model stands on
    if node == 'Object_3':
        return f'track_{side}'
    if round_ and 0.6 < s[1] < 0.8 and c[1] < 0.5:
        return f'wheel_{side}_road_{c[2]:+.3f}'
    if round_ and (0.4 < s[1] < 0.55 or (s[1] > 0.6 and c[1] > 0.6 and c[2] < -2.5)):
        return f'wheel_{side}_end_{c[2]:+.3f}'          # the idler in front, the sprocket behind
    if tris > 1000 and s[1] > 1.2:
        return 'launcher'                               # the 30-tube pack: it lifts
    if 2.0 < s[0] < 2.6 and s[1] < 0.5 and c[1] > 1.4:
        return 'turret'                                 # its turntable
    if tris == 58 and c[1] > 1.6:
        return f'antenna_{side}'                        # two fittings on the turntable (prep: the radar bones' parts)
    return 'x_root'


TURRET_SOURCE_Z = -2.4    # the Apocalypse's turret's middle along z before its running gear is centred
TURRET_Z = TURRET_SOURCE_Z   # after: convert() adds the centring's shift
SOURCES = {'apocalypse': Source(90.0, _apocalypse, 'tracks', 'up'), 'tos1': Source(0.0, _tos1, 'Object_3')}


def stations(names: set[str]) -> dict[str, str]:
    """wheel_<side>_<end|road>_<z> -> wheel_<side>_<station>: the frontmost end wheel A, the rearmost H, the road
    wheels B.. from the front (six; of two units of four, the two at the gap between them the hull's), the inner end
    wheels the hull's (the V603 has eight stations a side)."""
    out = {}
    for side in 'lr':
        z = lambda n: float(n.rsplit('_', 1)[1])   # noqa: E731
        ends = sorted((n for n in names if n.startswith(f'wheel_{side}_end_')), key=z, reverse=True)
        road = sorted((n for n in names if n.startswith(f'wheel_{side}_road_')), key=z, reverse=True)
        if len(road) == 8:      # two units of four: the two either side of the gap between them stay on the hull
            out.update({n: 'x_root' for n in road[3:5]})
            road = road[:3] + road[5:]
        if len(ends) < 2 or len(road) != 6:
            raise ValueError(f'side {side}: {len(ends)} end wheels, {len(road)} road wheels (6 wanted)')
        out[ends[0]], out[ends[-1]] = f'wheel_{side}_A', f'wheel_{side}_H'
        out.update({n: 'x_root' for n in ends[1:-1]})
        out.update({n: f'wheel_{side}_{"BCDEFG"[i]}' for i, n in enumerate(road)})
    return out


def convert(kind: str, src: str, out: str) -> dict:
    from PIL import Image
    s = SOURCES[kind]
    scene = gltf.read(src)
    paths = pg.node_paths(src)
    a = np.radians(s.yaw)
    rot = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    P = scene.positions @ rot.T
    P -= (0.0, P[:, 1].min(), 0.0)
    # a piece's node: its mesh node's name, or the parent's where the mesh node is only its material's (Collada exports)
    segs = [paths[k].strip('/').split('/') for k in range(len(paths))]
    node_name = [g[-2] if g[-1] == 'defaultMaterial' else g[-1] for g in segs]
    node_of_tri = [node_name[k] for k in scene.mesh]
    # name the pieces of each node
    names = np.empty(len(scene.triangles), dtype=object)
    by_node: dict[str, list[int]] = {}
    for t, n in enumerate(node_of_tri):
        by_node.setdefault(n, []).append(t)
    # centre the running gear (its tracks' box) on z 0, x 0
    tracks = np.asarray(by_node.get(s.gear, []), dtype=np.int64)
    gear = P[np.unique(scene.triangles[tracks])] if len(tracks) else P
    shift = np.array(((gear[:, 0].min() + gear[:, 0].max()) / 2, 0.0, (gear[:, 2].min() + gear[:, 2].max()) / 2))
    P -= shift
    global TURRET_Z
    TURRET_Z = TURRET_SOURCE_Z - shift[2]
    for n, tris in by_node.items():
        tris = np.asarray(tris, dtype=np.int64)
        piece = pg.pieces(scene.triangles[tris], P)
        for k in range(int(piece.max()) + 1):
            sel = tris[piece == k]
            Q = P[np.unique(scene.triangles[sel])]
            names[sel] = s.classify(n, Q.min(0), Q.max(0), len(sel))
    rename = stations({n for n in names if n.startswith('wheel_')})
    names = np.asarray([rename.get(n, n) for n in names], dtype=object)
    os.makedirs(os.path.join(out, 'textures'), exist_ok=True)
    mtl = []
    for mi, m in enumerate(scene.materials):
        name = f'm{mi}'
        # the colour at its own size; a JPEG as it is (saving it again would lose more), anything else as a JPEG
        if m.texture is not None and m.texture[:2] == b'\xff\xd8':
            with open(os.path.join(out, 'textures', f'{name}_c.jpg'), 'wb') as f:
                f.write(m.texture)
        elif m.texture is not None:
            with Image.open(io.BytesIO(m.texture)) as im:
                im.convert('RGB').save(os.path.join(out, 'textures', f'{name}_c.jpg'), quality=90)
        else:
            c = tuple(int(round(255 * min(1.0, max(0.0, v)) ** (1 / 2.2))) for v in m.base[:3])
            Image.new('RGB', (4, 4), c).save(os.path.join(out, 'textures', f'{name}_c.jpg'))
        mtl.append(f'newmtl {name}\nmap_Kd textures/{name}_c.jpg\n')
        if m.normal is not None:     # the normal map in the game's green, lossless (decoded to be flipped)
            if not s.normal_green:
                raise SystemExit(f'{kind}: material {m.name} has a normal map; measure its green (Source.normal_green)')
            with Image.open(io.BytesIO(m.normal)) as im:
                rgb = np.asarray(im.convert('RGB'))
            Image.fromarray(texfile_accel.directx_normals(rgb, s.normal_green, f'{kind} {m.name}'), 'RGB').save(
                os.path.join(out, 'textures', f'{name}_n.png'), optimize=True)
            mtl[-1] += f'map_normal textures/{name}_n.png\n'
    with open(os.path.join(out, f'{kind}.mtl'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(mtl))
    uv = scene.uvs if scene.uvs is not None else np.zeros((len(P), 2))
    order = sorted((t for t in range(len(scene.triangles)) if names[t] != 'drop'),   # 'drop': left out
                   key=lambda t: (names[t], scene.material[t]))
    with open(os.path.join(out, f'{kind}.obj'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'mtllib {kind}.mtl\n')
        f.writelines(f'v {x:.5f} {y:.5f} {z:.5f}\n' for x, y, z in P)
        # prep_wt_tracked reads the WT exports' (u, -v): the glb's top-left v is negated back to theirs
        f.writelines(f'vt {u:.6f} {-v:.6f}\n' for u, v in uv)
        cur, count = None, 0
        for t in order:
            key = (names[t], int(scene.material[t]))
            if key != cur:
                count += 1
                # War Thunder's `vehicle#<node>_<object>_<material>` (name@piece: an object of its own)
                f.write(f'o vehicle#{key[0].split("@")[0]}_{count}_0\nusemtl m{key[1]}\n')
                cur = key
            a_, b_, c_ = scene.triangles[t] + 1
            f.write(f'f {a_}/{a_} {b_}/{b_} {c_}/{c_}\n')
    counts: dict[str, int] = {}
    for n in names:
        counts[n.split('@')[0]] = counts.get(n.split('@')[0], 0) + 1
    return counts


def main(argv: list[str]) -> int:
    kind, src, out = argv
    counts = convert(kind, src, out)
    for n, k in sorted(counts.items()):
        print(f'{n:20s} {k}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
