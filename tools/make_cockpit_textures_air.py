"""The air family's cockpit textures (pylib/cockpit_kits_air.py), drawn with PIL into <model dir>/cockpits/textures/
as air_*.png (a developer step like tools/make_cockpit_textures.py, whose drawing helpers it reuses). Everything is
drawn here (no third-party image), so it carries no licence of anyone else's.

    python tools/make_cockpit_textures_air.py [--out <model dir>/cockpits/textures]

Textures (512 px; the panels, consoles and seats are photographs: tools/prep_cockpit_air.py):
    air_tub        a helicopter cockpit tub's dark grey: panel seams, rivet rows, scuffs
    air_overhead   an overhead panel: rows of circuit breakers, a few toggles and engine control levers' slots
"""
from __future__ import annotations

import argparse
import os
import random
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import make_cockpit_textures as base  # noqa: E402
import obj_model as om  # noqa: E402

SIZE = base.SIZE
LABEL = (205, 208, 200)


def fastener(d: ImageDraw.ImageDraw, x: int, y: int) -> None:
    """A quarter-turn panel fastener: a small grey disc with a slot."""
    d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(98, 100, 102))
    d.line((x - 3, y + 3, x + 3, y - 3), fill=(40, 40, 42), width=2)


def subpanel(d: ImageDraw.ImageDraw, box: tuple[int, int, int, int], fill: tuple[int, int, int]) -> None:
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=fill, outline=(22, 23, 24), width=3)
    for x in (x0 + 9, x1 - 9):
        for y in (y0 + 9, y1 - 9):
            fastener(d, x, y)


def air_tub() -> Image.Image:
    img = base.worn(base.noise((56, 60, 62), 6, 101), (42, 44, 46), 102, 25)
    d = ImageDraw.Draw(img)
    for x in (0, 256):
        d.line((x, 0, x, SIZE), fill=(34, 36, 38), width=3)
    for y in (0, 256):
        d.line((0, y, SIZE, y), fill=(34, 36, 38), width=3)
    for k in range(0, SIZE, 32):
        for line in (12, 268):
            d.ellipse((k + 12 - 3, line - 3, k + 12 + 3, line + 3), fill=(84, 88, 90))
            d.ellipse((line - 3, k + 12 - 3, line + 3, k + 12 + 3), fill=(84, 88, 90))
    return img


def air_overhead() -> Image.Image:
    img = base.noise((42, 44, 46), 4, 131)
    d = ImageDraw.Draw(img)
    rnd = random.Random(133)
    for row in range(5):
        y0 = 10 + row * 100
        subpanel(d, (8, y0, SIZE - 8, y0 + 92), (32, 34, 36))
        d.text((22, y0 + 10), ['DC BUS', 'AC BUS', 'FUEL', 'ENG', 'LIGHTS'][row], fill=LABEL)
        for k in range(9):
            x, y = 50 + k * 50, y0 + 58
            if row == 3 and k in (3, 4, 5):
                d.rectangle((x - 8, y - 30, x + 8, y + 26), fill=(10, 10, 10))   # an engine lever's slot
                d.rectangle((x - 12, y - 6, x + 12, y + 6), fill=(200, 200, 195) if k != 4 else (200, 40, 30))
            elif rnd.random() < 0.75:
                d.ellipse((x - 10, y - 10, x + 10, y + 10), fill=(225, 225, 220))   # a breaker's collar
                d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=(14, 14, 14))
            else:
                base.toggle(d, x, y, rnd.random() < 0.5, False)
    return img


TEXTURES = {
    'air_tub': air_tub,
    'air_overhead': air_overhead,
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
