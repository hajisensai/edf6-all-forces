"""The mech cockpits' own textures (pylib/cockpit_kits_mech.py), drawn with PIL into <model dir>/cockpits/textures/
as mech_*.png (a developer step, like tools/make_cockpit_textures.py, whose helpers it uses). Drawn here, no
third-party image.

    python tools/make_cockpit_textures_mech.py [--out <model dir>/cockpits/textures]

Textures (512 px):
    mech_plate   a pod's interior plating: blue-grey panels with seams, bolts and a few vents
    mech_frame   the visor's and the screens' frame: dark gunmetal with yellow and black hazard stripes on one edge
"""
from __future__ import annotations

import argparse
import os
import random
import sys

from PIL import ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import obj_model as om  # noqa: E402
from make_cockpit_textures import SIZE, brushed, noise, worn  # noqa: E402


def mech_plate():  # noqa: ANN201 - PIL.Image.Image
    img = worn(noise((62, 70, 80), 6, 71), (44, 48, 54), 72, 25)
    d = ImageDraw.Draw(img)
    rnd = random.Random(73)
    for x0, y0, x1, y1 in ((0, 0, 255, 255), (256, 0, 511, 170), (256, 171, 511, 255), (0, 256, 170, 511),
                           (171, 256, 511, 511)):
        d.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2), outline=(30, 33, 38), width=3)
        d.line((x0 + 5, y0 + 5, x1 - 5, y0 + 5), fill=(96, 104, 114), width=1)
        for x, y in ((x0 + 12, y0 + 12), (x1 - 12, y0 + 12), (x0 + 12, y1 - 12), (x1 - 12, y1 - 12)):
            d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(110, 116, 124), outline=(28, 30, 34))
    for k in range(6):   # vents
        x, y = 300 + rnd.randrange(150), 300 + k * 28
        d.rectangle((x, y, x + 140, y + 10), fill=(20, 22, 26))
    return img


def mech_frame():  # noqa: ANN201 - PIL.Image.Image
    img = brushed((48, 50, 54), 81)
    d = ImageDraw.Draw(img)
    for k in range(-SIZE, SIZE, 64):   # hazard stripes along the top edge
        d.polygon([(k, 0), (k + 32, 0), (k + 32 + 48, 48), (k + 48, 48)], fill=(220, 170, 20))
    d.rectangle((0, 48, SIZE, 54), fill=(24, 24, 26))
    for x in range(24, SIZE, 64):
        d.ellipse((x - 5, 400 - 5, x + 5, 400 + 5), fill=(120, 122, 126))
    return img


TEXTURES = {'mech_plate': mech_plate, 'mech_frame': mech_frame}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(om.REPO_MODELS, 'cockpits', 'textures'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, make in TEXTURES.items():
        path = os.path.join(args.out, name + '.png')
        make().save(path)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
