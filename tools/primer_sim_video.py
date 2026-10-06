"""A video of the Primer creatures flying and fighting, without the game: tools/primer_flight_sim runs the plugin's
own primer.cpp (and the flight code) in a stand-in world, its bodies holding bone records the plugin poses as in the
game; every few frames it writes each body's matrix and those records. Here each body is skinned with its model
(pylib/model_view.py, in the models' own textures) in that pose, put where the simulation had it and drawn through
a perspective camera over a ground grid, the player a red marker; ffmpeg makes an MP4 of the frames.

    python tools/primer_sim_video.py [--camera chase|overview|follow] [--fps 10] [--out build/primer_sim.mp4]
                                     [--size 960x540] -- [primer_flight_sim arguments]

e.g. python tools/primer_sim_video.py -- --centipedes 6 --dragonflies 2 --seconds 40 --player stand --shoot 60

Not the game's renderer: no lights, effects, buildings or bullets. What it shows is what the plugin does: where each
creature goes, how it faces, what its legs, wings, head and tail do, as the plugin's code computed them.
"""
from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import model_view  # noqa: E402
import primer_pose_view  # noqa: E402

SIM = os.path.join(ROOT, 'build', 'primer_flight_sim.exe')
SKY_TOP, SKY_LOW, GROUND = (70, 98, 140), (176, 192, 205), (96, 104, 82)


def write_binds(path: str) -> dict:
    """The posed bones' bind locals of both models into `path` (the simulator's --binds); the models."""
    models = {}
    with open(path, 'w') as h:
        for kind in ('centipede', 'dragonfly'):
            md, binds = primer_pose_view.model(kind)
            models[kind] = md
            for name, m in binds.items():
                h.write(f'{kind} {name} ' + ' '.join(f'{x:.9g}' for x in m) + '\n')
    return models


def read_frames(path: str) -> list[dict]:
    frames: list[dict] = []
    with open(path) as h:
        for line in h:
            p = line.split()
            if not p:
                continue
            if p[0] == 'frame':
                frames.append({'t': float(p[1]), 'player': None, 'bodies': {}})
            elif p[0] == 'player':
                frames[-1]['player'] = np.array([float(x) for x in p[1:4]])
            elif p[0] == 'body':
                frames[-1]['bodies'][int(p[1])] = {'kind': p[2], 'm': np.array([float(x) for x in p[3:19]]).reshape(4, 4), 'bones': {}}
            elif p[0] == 'bone':
                frames[-1]['bodies'][int(p[1])]['bones'][p[2]] = [float(x) for x in p[3:19]]
    return frames


class Camera:
    def __init__(self, eye, target, w: int, h: int, fov_deg: float = 60.0):
        self.eye = np.array(eye, dtype=np.float64)
        f = np.array(target, dtype=np.float64) - self.eye
        self.f = f / np.linalg.norm(f)
        r = np.cross(self.f, np.array([0.0, 1.0, 0.0]))
        if np.linalg.norm(r) < 1e-6:
            r = np.array([1.0, 0.0, 0.0])
        self.r = r / np.linalg.norm(r)
        self.u = np.cross(self.r, self.f)
        self.w, self.h = w, h
        self.focal = (w / 2) / math.tan(math.radians(fov_deg) / 2)

    def project(self, v: np.ndarray):
        d = v - self.eye
        x, y, z = d @ self.r, d @ self.u, d @ self.f
        zs = np.maximum(z, 1e-3)
        return self.w / 2 + self.focal * x / zs, self.h / 2 - self.focal * y / zs, z


def background(cam: Camera) -> Image.Image:
    img = Image.new('RGB', (cam.w, cam.h))
    d = ImageDraw.Draw(img)
    # The horizon: where the camera's view crosses level.
    pitch = math.asin(max(-1.0, min(1.0, cam.f[1])))
    horizon = cam.h / 2 + cam.focal * math.tan(pitch)
    for y in range(cam.h):
        if y < horizon:
            k = max(0.0, min(1.0, y / max(horizon, 1)))
            c = tuple(int(SKY_TOP[i] + (SKY_LOW[i] - SKY_TOP[i]) * k) for i in range(3))
        else:
            c = GROUND
        d.line([(0, y), (cam.w, y)], fill=c)
    # A grid on the ground, 50 m a square, round where the camera looks.
    c0 = np.round((cam.eye + cam.f * 300.0) / 50.0) * 50.0
    for k in range(-14, 15):
        for a, b in (((c0[0] + k * 50, 0, c0[2] - 700), (c0[0] + k * 50, 0, c0[2] + 700)),
                     ((c0[0] - 700, 0, c0[2] + k * 50), (c0[0] + 700, 0, c0[2] + k * 50))):
            pts = np.linspace(np.array(a), np.array(b), 60)
            sx, sy, z = cam.project(pts)
            seg = [(float(x), float(y)) for x, y, zz in zip(sx, sy, z) if zz > 2.0]
            if len(seg) > 1:
                d.line(seg, fill=(118, 126, 100), width=1)
    return img


def draw(img: Image.Image, cam: Camera, v: np.ndarray, t: np.ndarray, c: np.ndarray) -> None:
    sx, sy, z = cam.project(v)
    tz = z[t]
    keep = (tz > 1.0).all(axis=1)
    t, tz = t[keep], tz[keep]
    a, b, cc = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    n = np.cross(b - a, cc - a)
    ln = np.linalg.norm(n, axis=1)
    ln[ln == 0] = 1
    n /= ln[:, None]
    light = np.array([0.35, 0.85, 0.4])
    light /= np.linalg.norm(light)
    shade = 0.4 + 0.6 * np.abs(n @ light)
    col = (c[t].mean(axis=1) * shade[:, None]).clip(0, 255).astype(int)
    d = ImageDraw.Draw(img)
    for i in np.argsort(-tz.mean(axis=1)):   # far first
        tri = t[i]
        d.polygon([(sx[tri[0]], sy[tri[0]]), (sx[tri[1]], sy[tri[1]]), (sx[tri[2]], sy[tri[2]])], fill=tuple(col[i]))


def marker(img: Image.Image, cam: Camera, at: np.ndarray, colour=(220, 40, 40)) -> None:
    """The player: a 2 m post with a ring round its foot."""
    d = ImageDraw.Draw(img)
    post = np.array([at, at + [0.0, 2.0, 0.0]])
    sx, sy, z = cam.project(post)
    if (z > 1.0).all():
        d.line([(sx[0], sy[0]), (sx[1], sy[1])], fill=colour, width=4)
    ring = np.array([at + [3.0 * math.cos(k * 0.4), 0.05, 3.0 * math.sin(k * 0.4)] for k in range(17)])
    rx, ry, rz = cam.project(ring)
    seg = [(float(x), float(y)) for x, y, zz in zip(rx, ry, rz) if zz > 1.0]
    if len(seg) > 1:
        d.line(seg, fill=colour, width=2)


def camera_for(frame: dict, how: str, w: int, h: int, prev: list) -> Camera:
    player = frame['player'] if frame['player'] is not None else np.zeros(3)
    pts = [b['m'][3, :3] for b in frame['bodies'].values() if b['kind'] != 'jet']
    centre = np.mean(pts, axis=0) if pts else player
    if how == 'follow':   # close on the centipedes (else everything), going slowly round them
        cps = [b['m'][3, :3] for b in frame['bodies'].values() if b['kind'] == 'centipede']
        target = np.mean(cps, axis=0) if cps else centre
        a = frame['t'] * 0.12
        eye = target + np.array([90.0 * math.cos(a), 35.0, 90.0 * math.sin(a)])
    elif how == 'overview':
        eye = player + np.array([-260.0, 220.0, -260.0])
        target = player + np.array([0.0, 20.0, 0.0])
    else:   # behind the player, looking past them at the creatures
        away = player - centre
        away[1] = 0.0
        if np.linalg.norm(away) < 1.0:
            away = np.array([0.0, 0.0, -1.0])
        away /= np.linalg.norm(away)
        eye = player + away * 60.0 + np.array([0.0, 28.0, 0.0])
        target = (player + centre) / 2 + np.array([0.0, 15.0, 0.0])
    if prev:   # smoothed: a camera that does not jump
        eye = prev[0] * 0.85 + eye * 0.15
        target = prev[1] * 0.85 + target * 0.15
    prev[:] = [eye, target]
    return Camera(eye, target, w, h)


def main(argv: list[str]) -> int:
    if '--' in argv:
        k = argv.index('--')
        mine, sim_args = argv[:k], argv[k + 1:]
    else:
        mine, sim_args = argv, []
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--camera', choices=('chase', 'overview', 'follow'), default='chase')
    ap.add_argument('--fps', type=int, default=10)
    ap.add_argument('--size', default='960x540')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'primer_sim.mp4'))
    a = ap.parse_args(mine)
    w, h = (int(x) for x in a.size.split('x'))
    if not os.path.isfile(SIM):
        raise SystemExit(f'{SIM} 不存在：先运行 build.cmd')
    work = tempfile.mkdtemp(prefix='primer_video_', dir=os.path.join(ROOT, 'build'))
    try:
        models = write_binds(os.path.join(work, 'binds.txt'))
        frames_path = os.path.join(work, 'frames.txt')
        every = max(1, round(60 / a.fps))
        run = subprocess.run([SIM, *sim_args, '--binds', os.path.join(work, 'binds.txt'), '--frames', frames_path,
                              '--every', str(every)], cwd=os.path.join(ROOT, 'build'), capture_output=True, text=True)
        if run.returncode:
            raise SystemExit(run.stderr or run.stdout)
        print(run.stdout.strip())
        frames = read_frames(frames_path)
        prev: list = []
        for n, fr in enumerate(frames):
            cam = camera_for(fr, a.camera, w, h, prev)
            img = background(cam)
            vs, ts, cs, base = [], [], [], 0
            for body in fr['bodies'].values():
                if body['kind'] not in models:
                    continue
                v, t, c = model_view.geometry(models[body['kind']], {}, ['@tex'], body['bones'])
                v4 = np.hstack([v, np.ones((len(v), 1))]) @ body['m']
                vs.append(v4[:, :3])
                ts.append(t + base)
                cs.append(c)
                base += len(v)
            if vs:
                draw(img, cam, np.vstack(vs), np.vstack(ts), np.vstack(cs))
            if fr['player'] is not None:
                marker(img, cam, fr['player'])
            ImageDraw.Draw(img).text((10, 8), f't = {fr["t"]:5.1f} s   {len(fr["bodies"])} bodies   (stand-in render)', fill=(255, 255, 255))
            img.save(os.path.join(work, f'f{n:05d}.png'))
            if n % 25 == 0:
                print(f'  frame {n + 1}/{len(frames)}')
        ffmpeg = shutil.which('ffmpeg') or r'D:\APP\ffmpeg\bin\ffmpeg.exe'
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-framerate', str(a.fps), '-i', os.path.join(work, 'f%05d.png'),
                        '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', a.out], check=True)
        print(f'wrote {a.out}: {len(frames)} frames at {a.fps} fps')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
