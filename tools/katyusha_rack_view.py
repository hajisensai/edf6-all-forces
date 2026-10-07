"""The Katyusha's rack (src/katyusha_rack.h through tools/katyusha_rack_check.exe --dump) drawn on its built model: one
row per state (full, half a salvo fired, empty, being loaded, spent), each rocket's bone posed as src/katyusha.cpp
Rockets poses it (its bind local; off the rack: its rotation rows times 0; being loaded: LOAD x (1 - on) short of its
stop along the rail), with the launcher at its bind (0 deg) or raised (--elevation).

    python tools/katyusha_rack_view.py OUT_DIR [--mrab EDF6VC_KATYUSHA.MRAB] [--views back,side,iso] [--size PX]
        [--elevation DEG]

Views (pylib/model_view.py's, and `back`: from behind the truck), centred on the launcher's pivot and framed on the
rack (RACK_EXTENT m either way), so the rockets are big enough to count.

Without --mrab the model is built from Root.cpk (pylib/katyusha_model.py build; read only). Writes OUT_DIR/rack.png
and one PNG per state. Needs numpy and PIL; build katyusha_rack_check first (cmake --build build --target
katyusha_rack_check).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import katyusha_model as km  # noqa: E402
import model_view  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
from mdb import Mdb  # noqa: E402

CHECK = os.path.join(ROOT, 'build', 'katyusha_rack_check.exe')
RACK_EXTENT = 1.6   # m round the rockets' middle: the rack across, the rockets and the rails' back half along
model_view.VIEWS.setdefault('back', ((1, 0, 0), (0, 1, 0), (0, 0, -1)))


def states() -> list[tuple[str, list[float]]]:
    """(label, the 16 rockets' on) as the rack's own code works them out."""
    out = subprocess.run([CHECK, '--dump'], check=True, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        label, *vals = line.split()
        rows.append((label, [float(v) for v in vals]))
    return rows


def rack_locals(md: Mdb, on: list[float], elevation: float) -> dict[str, list[float]]:
    """The rocket bones' locals for `on` (src/katyusha.cpp Rockets), and the launcher's raised `elevation` deg."""
    out = {}
    for name, x in zip(km.ROCKET_BONES, on):
        local = list(md.bones[md.bone_index(name)].local)
        if x <= 0.0:
            local[:12] = [0.0] * 12
        else:
            local[14] -= (1.0 - x) * km.LOAD
        out[name] = local
    if elevation:
        main = md.bones[md.bone_index('Rocketcannon_main')].local
        out['Rocketcannon_main'] = model_view.mmul(model_view.rot('x', -elevation), main)
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('--mrab', default='')
    ap.add_argument('--views', default='back,side,iso')
    ap.add_argument('--size', type=int, default=520)
    ap.add_argument('--elevation', type=float, default=0.0)
    a = ap.parse_args(argv)
    if a.mrab:
        md = model_view.load(a.mrab, km.HOST_MDB)
    else:
        import rootcpk
        path = os.path.join(a.out, 'EDF6VC_KATYUSHA.MRAB')
        os.makedirs(a.out, exist_ok=True)
        with open(path, 'wb') as h:
            h.write(km.build(rootcpk.default()))
        md = model_view.load(path, km.HOST_MDB)
    views = [v for v in a.views.split(',') if v]
    w = model_view.bind_world(md)[md.bone_index('Rocketcannon_main')]
    centre = np.array([w[12], w[13] + km.RAIL_Y, w[14] + km.ROCKET_TAIL + km.ROCKET_LEN / 2])
    sheet = []
    for label, on in states():
        v, t, c = model_view.geometry(md, {}, ['@tex'], rack_locals(md, on, a.elevation))
        row = [model_view.render(v - centre, t, c, view, a.size, RACK_EXTENT, label) for view in views]
        img = Image.new('RGB', (a.size * len(views), a.size))
        for k, im in enumerate(row):
            img.paste(im, (k * a.size, 0))
        img.save(os.path.join(a.out, f'rack_{label}.png'))
        sheet.append(img)
    whole = Image.new('RGB', (a.size * len(views), a.size * len(sheet)))
    for r, img in enumerate(sheet):
        whole.paste(img, (0, r * a.size))
    whole.save(os.path.join(a.out, 'rack.png'))
    rows = sheet
    print(f'wrote {a.out}: rack.png and {len(rows)} states x {len(views)} views')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
