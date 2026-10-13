"""The cockpits' own textures (pylib/cockpit_kits.py), drawn with PIL into <model dir>/cockpits/textures/ (a developer
step, like tools/prep_sazabi.py: the installer reads the PNGs, it does not need PIL). Everything here is drawn by
this script (no third-party image), so it carries no licence of anyone else's.

    python tools/make_cockpit_textures.py [--out <model dir>/cockpits/textures]   (default: the developer's model folder)

Textures (512 px unless noted):
    paint_interior   the off-white enamel of a tank / truck interior, worn at the edges
    paint_olive      olive drab (frames, consoles), with chips
    metal_dark       dark gunmetal (breech, grips, mounts), brushed
    rubber           black rubber / brow pads
    panel_switches   a row of toggle switches, guarded switches and labels
    canopy_frame     a canopy / window frame's grey, with rivets
    screen           a dark glass screen in a thin bezel (the plugin draws its live read-out over it: cockpits.Screen)
"""
from __future__ import annotations

import argparse
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import obj_model as om  # noqa: E402

SIZE = 512


def noise(base: tuple[int, int, int], spread: int, seed: int, blur: float = 1.2) -> Image.Image:
    rnd = random.Random(seed)
    img = Image.new('RGB', (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            d = rnd.randint(-spread, spread)
            px[x, y] = tuple(max(0, min(255, c + d)) for c in base)
    return img.filter(ImageFilter.GaussianBlur(blur))


def worn(img: Image.Image, edge: tuple[int, int, int], seed: int, chips: int = 60) -> Image.Image:
    """Paint chips (`edge` showing through) and a darker band at the borders."""
    rnd = random.Random(seed)
    d = ImageDraw.Draw(img)
    for _ in range(chips):
        x, y, r = rnd.randrange(SIZE), rnd.randrange(SIZE), rnd.randint(2, 7)
        d.ellipse((x - r, y - r // 2, x + r, y + r // 2), fill=edge)
    shade = Image.new('L', (SIZE, SIZE), 0)
    sd = ImageDraw.Draw(shade)
    for k in range(24):
        sd.rectangle((k, k, SIZE - 1 - k, SIZE - 1 - k), outline=int(60 * (1 - k / 24)))
    dark = Image.new('RGB', (SIZE, SIZE), (0, 0, 0))
    return Image.composite(dark, img, shade)


def brushed(base: tuple[int, int, int], seed: int) -> Image.Image:
    """Brushed metal: the noise smoothed along x (streaks across)."""
    return noise(base, 18, seed, 0.0).resize((24, SIZE), Image.BILINEAR).resize((SIZE, SIZE), Image.BILINEAR)


def toggle(d: ImageDraw.ImageDraw, x: int, y: int, up: bool, guard: bool) -> None:
    d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(150, 150, 145))
    d.rectangle((x - 3, y - 22 if up else y, x + 3, y if up else y + 22), fill=(215, 215, 210))
    if guard:
        d.rectangle((x - 14, y - 26, x + 14, y + 26), outline=(200, 40, 30), width=3)


def lamp(d: ImageDraw.ImageDraw, x: int, y: int, colour: tuple[int, int, int], on: bool) -> None:
    c = colour if on else tuple(v // 4 for v in colour)
    d.rectangle((x - 14, y - 9, x + 14, y + 9), fill=(30, 30, 30))
    d.rectangle((x - 11, y - 6, x + 11, y + 6), fill=c)


def panel_switches() -> Image.Image:
    img = noise((46, 50, 46), 6, 31)
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    for row in range(4):
        y = 70 + row * 115
        d.rectangle((20, y - 50, SIZE - 20, y + 50), outline=(200, 200, 190), width=2)
        d.text((28, y - 46), ['MASTER', 'WEAPON', 'TURRET', 'COMMS'][row], fill=(230, 230, 220))
        for k in range(7):
            x = 70 + k * 62
            if rnd.random() < 0.3:
                lamp(d, x, y, (240, 180, 30), rnd.random() < 0.5)
            else:
                toggle(d, x, y + 8, rnd.random() < 0.5, row == 1 and k == 0)
    return img


def canopy_frame() -> Image.Image:
    img = worn(noise((92, 96, 98), 8, 51), (60, 62, 64), 52, 30)
    d = ImageDraw.Draw(img)
    for y in range(16, SIZE, 48):
        for x in (16, SIZE - 16):
            d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(140, 142, 145))
    return img


def screen() -> Image.Image:
    img = noise((14, 18, 16), 3, 61, 2.0)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, SIZE - 1, SIZE - 1), outline=(70, 72, 74), width=14)
    d.rectangle((14, 14, SIZE - 15, SIZE - 15), outline=(30, 32, 34), width=4)
    for y in range(18, SIZE - 18, 4):   # faint scan lines
        d.line((18, y, SIZE - 19, y), fill=(18, 23, 20))
    return img


TEXTURES = {
    'paint_interior': lambda: worn(noise((214, 212, 198), 5, 1), (176, 174, 160), 2, 18),   # cream enamel, light wear
    'paint_olive': lambda: worn(noise((84, 92, 58), 8, 3), (60, 58, 50), 4),
    'metal_dark': lambda: brushed((54, 56, 58), 5),
    'rubber': lambda: noise((24, 24, 24), 5, 6, 1.5),
    'panel_switches': panel_switches,
    'canopy_frame': canopy_frame,
    'screen': screen,
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
