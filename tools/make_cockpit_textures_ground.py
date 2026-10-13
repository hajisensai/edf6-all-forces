"""The ground vehicles' cab textures (pylib/cockpit_kits_ground.py), drawn with PIL into <model dir>/cockpits/textures/
as ground_*.png (a developer step, like tools/make_cockpit_textures.py; everything drawn here, no third-party image).

    python tools/make_cockpit_textures_ground.py [--out <model dir>/cockpits/textures]

Textures (256 px):
    ground_dash       a dashboard's dark grey grained plastic with a moulded seam
    ground_door       a door's grey trim: a padded upper band, a stitched seam, a map pocket's lip
    ground_headliner  a cab roof's light grey fabric
    ground_floor      a ribbed black rubber floor mat
"""
from __future__ import annotations

import argparse
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import obj_model as om  # noqa: E402

SIZE = 256


def grain(base: tuple[int, int, int], spread: int, seed: int, blur: float = 0.8) -> Image.Image:
    rnd = random.Random(seed)
    img = Image.new('RGB', (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            d = rnd.randint(-spread, spread)
            px[x, y] = tuple(max(0, min(255, c + d)) for c in base)
    return img.filter(ImageFilter.GaussianBlur(blur))


def dash() -> Image.Image:
    img = grain((46, 47, 49), 6, 11)
    d = ImageDraw.Draw(img)
    d.line((0, 40, SIZE, 40), fill=(30, 31, 33), width=3)
    d.line((0, 43, SIZE, 43), fill=(64, 65, 68), width=1)
    return img


def door() -> Image.Image:
    img = grain((92, 94, 98), 5, 12)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, SIZE, 70), fill=(74, 75, 80))
    for x in range(4, SIZE, 10):   # stitches under the padded band
        d.line((x, 76, x + 5, 76), fill=(140, 140, 136), width=1)
    d.rectangle((0, 180, SIZE, 186), fill=(52, 53, 56))   # the pocket's lip
    return img.filter(ImageFilter.GaussianBlur(0.5))


def headliner() -> Image.Image:
    return grain((176, 176, 170), 10, 13, 1.1)


def floor() -> Image.Image:
    img = grain((30, 30, 31), 4, 14)
    d = ImageDraw.Draw(img)
    for y in range(0, SIZE, 16):
        d.rectangle((0, y, SIZE, y + 5), fill=(44, 44, 46))
    return img.filter(ImageFilter.GaussianBlur(0.6))


TEXTURES = {
    'ground_dash': dash,
    'ground_door': door,
    'ground_headliner': headliner,
    'ground_floor': floor,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits', 'textures'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, make in TEXTURES.items():
        path = os.path.join(args.out, name + '.png')
        make().save(path)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
