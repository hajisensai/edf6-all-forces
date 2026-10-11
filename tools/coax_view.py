"""Offline view of the tanks' coaxial machine guns (pylib/vcobjects.py COAX_MOUNTS, docs/coax-feedback-2026-10-11.md):
each stock tank's model from Root.cpk (read only) with its coax model hung on the holder bone as the game hangs a
weapon (the bone's bind world), the main gun's muzzle and the coax's muzzle locator marked, in the game's right-handed
frame (+x the vehicle's LEFT: nothing drawn mirrored). And the face map the coax ports were measured with: for each
point right of the bore, the first surface a ray from straight ahead meets and whose bone it is (C the holder,
elevating; S the barrel's recoil part; T the turret, which does not elevate; H the hull).

    python tools/coax_view.py OUT_DIR [--tanks 505,601,401] [--size 420] [--red] [--faces]

Writes OUT_DIR/coax_<tank>.png: a row of the whole tank (right side, top, front, front-right) and rows closer in on
the mantlet; --red tints the coax red to find it; --faces prints the face maps.
"""
from __future__ import annotations

import argparse
import io
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..', 'pylib')]
import dsgo  # noqa: E402
import make_stock_stores as stores  # noqa: E402
import mdb  # noqa: E402
import model_view as mv  # noqa: E402
import vcobjects as vc  # noqa: E402

TANKS = {'505': ('V505_TANK', 'cannon', 'V_505TANK_CANNON01.SGO'),
         '601': ('V601_TANK', 'cannon', 'V601_TANK_CANNON01.SGO'),
         '401': ('VEHICLE401_STRIKER', 'Barrel', 'V_401STRIKER_CANNON.SGO')}
VIEWS = {'right': ((0, 0, 1), (0, 1, 0), (-1, 0, 0)), 'top': ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
         'front': ((1, 0, 0), (0, 1, 0), (0, 0, 1))}


def axes(view: str) -> list[np.ndarray]:
    if view == 'iso':   # from ahead, on the right (the coax's side), above
        eye = np.array([-0.9, 0.5, 1.0])
        eye /= np.linalg.norm(eye)
        right = np.cross(np.array([0.0, 1.0, 0.0]), eye)
        right /= np.linalg.norm(right)
        return [right, np.cross(eye, right), eye]
    return [np.array(a, dtype=np.float64) for a in VIEWS[view]]


def load(raw: bytes) -> mdb.Mdb:
    rab = mdb.rab_read(raw)
    md = mdb.mdb_read(next(f for f in rab.files if f.name.lower().endswith('.mdb')).data)
    wanted, textures = {t.filename.lower() for t in md.textures}, {}
    for f in rab.files:
        if f.name.lower() in wanted:
            try:
                im = Image.open(io.BytesIO(f.data))
                im.draft('RGB', (512, 512))
                textures[f.name.lower()] = im.convert('RGB').resize((512, 512))
            except Exception:   # a format PIL cannot read: that material keeps its plain colour
                pass
    mv.TEXTURES[id(md)] = textures
    return md


def muzzle(sgo: bytes) -> tuple[float, float, float]:
    data = dsgo.parse(sgo).root.get('animation_model').items[2].data
    return struct.unpack_from('<3f', data, vc.mab_muzzles(data)[0][2])


def tank_model(game: vc.Game, tank: str) -> tuple[mdb.Mdb, list[str], list]:
    stem = TANKS[tank][0]
    d = stores._read_any(game.read('OBJECT', stem + '.SGO'))
    md = load(game.read('OBJECT', str(d['animation_model'][0][0]).split('/')[-1]))
    return md, [md.name_of(b.name) for b in md.bones], d['vehicle_weapon_setting']


def scene(game: vc.Game, tank: str, red: bool):
    stem, bone, main = TANKS[tank]
    md, names, rows = tank_model(game, tank)
    world = mdb.bind_world(md)
    holder = np.array(world[names.index(bone)]).reshape(4, 4)
    slide = np.array(world[names.index(rows[0][0])]).reshape(4, 4)
    v, t, c = mv.geometry(md, {}, ['@tex'])
    kind = f'MG{tank}'
    coax = load(vc.coax_model(game, vc.STORES[kind].weapon.model))
    cv, ct, cc = mv.geometry(coax, {}, ['@tex'])
    cv = (np.hstack([cv, np.ones((len(cv), 1))]) @ holder)[:, :3]
    if red:
        cc[:] = (255, 60, 60)
    marks = {'main': (np.array([*muzzle(game.read('WEAPON', main)), 1.0]) @ slide)[:3],
             'coax': (np.array([*muzzle(vc.store_sgo(game, vc.store_file(kind, vc.COAX_MOUNTS[tank].belt))), 1.0])
                      @ holder)[:3]}
    return np.vstack([v, cv]), np.vstack([t, ct + len(v)]), np.vstack([c, cc]), marks


def draw(v, t, c, marks, centre, extent, view, size, label) -> Image.Image:
    img = mv.render(v - centre, t, c, view, size, extent, label)
    right, up, _ = axes(view)
    d = ImageDraw.Draw(img)
    s = size * 0.45 / extent
    for name, p in marks.items():
        q = p - centre
        x, y = size / 2 + q @ right * s, size / 2 - q @ up * s
        colour = (60, 255, 60) if name == 'coax' else (60, 200, 255)
        d.ellipse((x - 4, y - 4, x + 4, y + 4), outline=colour, width=2)
        d.text((x + 6, y - 6), name, fill=colour)
    bar = 0.5 if extent > 0.6 else 0.1
    d.line((10, size - 12, 10 + bar * s, size - 12), fill=(255, 255, 0), width=3)
    d.text((12, size - 28), f'{bar} m', fill=(255, 255, 0))
    return img


def face_map(game: vc.Game, tank: str, xs, ys) -> dict:
    """{(x, y): (bone letter, z)} of the first surface a ray from straight ahead meets, in the holder bone's frame."""
    _, bone, _ = TANKS[tank]
    md, names, _ = tank_model(game, tank)
    holder = np.array(mdb.bind_world(md)[names.index(bone)]).reshape(4, 4)
    v, t, _ = mv.geometry(md, {}, [])
    v = v - holder[3, :3]
    owners = []
    for ob in md.objects:
        for me in ob.meshes:
            e = next(x for x in me.elems if x.name.lower() == 'position')
            n = len(mdb.read_elem(me, e.name, e.channel))
            bi = mdb.read_elem(me, next((x.name for x in me.elems if x.name.upper() == 'BLENDINDICES'), ''), 0)
            bw = mdb.read_elem(me, next((x.name for x in me.elems if x.name.upper() == 'BLENDWEIGHT'), ''), 0)
            if me.flags[1] and bi and bw:
                a, w = np.array(bi), np.array(bw)
                owners += list(a[np.arange(len(a)), w.argmax(axis=1)])
            else:
                owners += [ob.bone] * n
    own = np.array(owners)[t[:, 0]]
    a, b, cc = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    out = {}
    for y in ys:
        for x in xs:
            e0, e1, e2 = b[:, :2] - a[:, :2], cc[:, :2] - a[:, :2], np.array([x, y]) - a[:, :2]
            den = e0[:, 0] * e1[:, 1] - e1[:, 0] * e0[:, 1]
            ok = np.abs(den) > 1e-12
            safe = np.where(ok, den, 1.0)
            u = np.where(ok, (e2[:, 0] * e1[:, 1] - e1[:, 0] * e2[:, 1]) / safe, -1.0)
            w = np.where(ok, (e0[:, 0] * e2[:, 1] - e2[:, 0] * e0[:, 1]) / safe, -1.0)
            hit = ok & (u >= 0) & (w >= 0) & (u + w <= 1)
            if not hit.any():
                out[(x, y)] = ('.', None)
                continue
            z = a[hit, 2] + u[hit] * (b[hit, 2] - a[hit, 2]) + w[hit] * (cc[hit, 2] - a[hit, 2])
            k = int(np.argmax(z))
            o = names[own[hit][k]]
            out[(x, y)] = ('C' if o == bone else 'S' if o == 'cannon_slide' else
                           'T' if o in ('cannon_main', 'Battery') else 'H', float(z[k]))
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('--tanks', default=','.join(TANKS))
    ap.add_argument('--size', type=int, default=420)
    ap.add_argument('--red', action='store_true')
    ap.add_argument('--faces', action='store_true')
    a = ap.parse_args(argv)
    mv.view_axes = axes   # model_view.render draws in these (right-handed) views
    game = vc.Game(vc.DEFAULT_GAME)
    os.makedirs(a.out, exist_ok=True)
    for tank in a.tanks.split(','):
        v, t, c, marks = scene(game, tank, a.red)
        port = np.array(marks['coax'])
        rows = [(np.array([0.0, 1.2, 0.0]), 5.0), (port - np.array([0.0, 0.0, 0.4]), 1.2),
                (port - np.array([0.0, 0.0, 0.15]), 0.35)]
        views = ('right', 'top', 'front', 'iso')
        img = Image.new('RGB', (a.size * len(views), a.size * len(rows)))
        for r, (centre, extent) in enumerate(rows):
            for k, view in enumerate(views):
                img.paste(draw(v, t, c, marks, centre, extent, view, a.size, f'{tank} {extent} m'), (k * a.size, r * a.size))
        path = os.path.join(a.out, f'coax_{tank}.png')
        img.save(path)
        print(path, {k: tuple(round(float(x), 3) for x in p) for k, p in marks.items()})
        if a.faces:
            xs = np.round(np.arange(-0.40, -0.099, 0.02), 3)
            ys = np.round(np.arange(0.20, -0.201, -0.02), 3)
            got = face_map(game, tank, xs, ys)
            print(f'  face map (holder frame), columns x {xs[0]} .. {xs[-1]}, rows y; letter and z of the first surface')
            for y in ys:
                print(f'  {y:+.2f} ' + ' '.join(f'{got[(x, y)][0]}{got[(x, y)][1]:5.2f}' if got[(x, y)][1] is not None
                                                else '   .  ' for x in xs))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
