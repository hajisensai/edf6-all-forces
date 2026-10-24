"""The air family's cockpit textures (pylib/cockpit_kits_air.py), drawn with PIL into <model dir>/cockpits/textures/
as air_*.png (a developer step like tools/make_cockpit_textures.py, whose drawing helpers it reuses). Everything is
drawn here (no third-party image), so it carries no licence of anyone else's.

    python tools/make_cockpit_textures_air.py [--out <model dir>/cockpits/textures]

Textures (512 px; the cockpits themselves are real models: tools/prep_cockpit_air.py):
    air_tub        a helicopter cockpit tub's dark grey: panel seams, rivet rows, scuffs
"""
from __future__ import annotations

import argparse
import os
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


TEXTURES = {
    'air_tub': air_tub,
}


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
