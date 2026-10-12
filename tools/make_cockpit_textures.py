"""The cockpits' own textures (pylib/cockpit_kits.py), drawn with PIL into <model dir>/cockpits/textures/ (a developer
step, like tools/prep_sazabi.py: the installer reads the PNGs, it does not need PIL). Everything here is drawn by
this script (no third-party image), so it carries no licence of anyone else's.

    python tools/make_cockpit_textures.py [--out <model dir>/cockpits/textures]   (default: the developer's model folder)

Textures (512 px unless noted):
    paint_interior   the off-white enamel of a tank / truck interior, worn at the edges
    paint_olive      olive drab (frames, consoles), with chips
    metal_dark       dark gunmetal (breech, grips, mounts), brushed
    rubber           black rubber / brow pads
    panel_analog     a console of round gauges, switches and lamps (the needles at rest; V4 turns real ones)
    panel_mfd        two green multi-function screens with bezels and push buttons
    panel_switches   a row of toggle switches, guarded switches and labels
    leather          a seat's brown leather
    canopy_frame     a canopy / window frame's grey, with rivets
    screen           a dark glass screen in a thin bezel (the plugin draws its live read-out over it: cockpits.Screen)
"""
from __future__ import annotations

import argparse
import math
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


def gauge(d: ImageDraw.ImageDraw, cx: int, cy: int, r: int, label: str, value: float, seed: int) -> None:
    """A round gauge: bezel, black face, white ticks over 270 degrees, a needle at `value` (0..1)."""
    d.ellipse((cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6), fill=(60, 62, 60))
    d.ellipse((cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2), fill=(25, 25, 25))
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(12, 12, 14))
    for k in range(28):
        a = math.radians(225 - 270 * k / 27)
        long = k % 3 == 0
        r0 = r * (0.72 if long else 0.82)
        d.line((cx + r0 * math.cos(a), cy - r0 * math.sin(a), cx + 0.92 * r * math.cos(a), cy - 0.92 * r * math.sin(a)),
               fill=(230, 230, 220), width=3 if long else 1)
    a = math.radians(225 - 270 * value)
    d.line((cx, cy, cx + 0.78 * r * math.cos(a), cy - 0.78 * r * math.sin(a)), fill=(245, 120, 30), width=4)
    d.ellipse((cx - 6, cy - 6, cx + 6, cy + 6), fill=(80, 80, 80))
    d.text((cx - 3 * len(label), cy + r // 3), label, fill=(220, 220, 210))


def toggle(d: ImageDraw.ImageDraw, x: int, y: int, up: bool, guard: bool) -> None:
    d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(150, 150, 145))
    d.rectangle((x - 3, y - 22 if up else y, x + 3, y if up else y + 22), fill=(215, 215, 210))
    if guard:
        d.rectangle((x - 14, y - 26, x + 14, y + 26), outline=(200, 40, 30), width=3)


def lamp(d: ImageDraw.ImageDraw, x: int, y: int, colour: tuple[int, int, int], on: bool) -> None:
    c = colour if on else tuple(v // 4 for v in colour)
    d.rectangle((x - 14, y - 9, x + 14, y + 9), fill=(30, 30, 30))
    d.rectangle((x - 11, y - 6, x + 11, y + 6), fill=c)


def panel_analog() -> Image.Image:
    img = noise((52, 58, 50), 6, 11)
    d = ImageDraw.Draw(img)
    rnd = random.Random(5)
    labels = ['RPM', 'OIL', 'TEMP', 'FUEL', 'AMP', 'HYD', 'PSI', 'ELEV']
    for k, (cx, cy) in enumerate([(90, 110), (256, 110), (422, 110), (90, 300), (256, 300), (422, 300)]):
        gauge(d, cx, cy, 64, labels[k], rnd.random(), k)
    for k in range(8):
        lamp(d, 48 + k * 60, 430, [(220, 40, 30), (240, 180, 30), (40, 200, 60)][k % 3], rnd.random() < 0.4)
    for k in range(8):
        toggle(d, 48 + k * 60, 480, rnd.random() < 0.5, k in (2, 6))
    for x in (8, SIZE - 8):
        for y in (8, SIZE - 8):
            d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(110, 110, 105))
    return img


def panel_mfd() -> Image.Image:
    img = noise((38, 40, 42), 5, 21)
    d = ImageDraw.Draw(img)
    for k, x0 in enumerate((16, 266)):
        d.rectangle((x0, 40, x0 + 230, 360), fill=(20, 20, 22), outline=(90, 90, 92), width=4)
        d.rectangle((x0 + 22, 62, x0 + 208, 338), fill=(6, 22, 8))
        g = (70, 255, 90)
        cx, cy = x0 + 115, 200
        if k == 0:   # an attitude scale
            for p in range(-3, 4):
                y = cy + p * 30
                d.line((cx - (40 if p else 70), y, cx + (40 if p else 70), y), fill=g, width=2)
            d.polygon([(cx, cy - 8), (cx - 12, cy + 6), (cx + 12, cy + 6)], outline=g)
        else:        # a radar / map scope
            for rr in (40, 80, 110):
                d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), outline=g, width=1)
            d.line((cx, cy, cx + 90, cy - 60), fill=g, width=2)
            for _ in range(5):
                a, rr = random.random() * 6.28, random.random() * 100
                d.rectangle((cx + rr * math.cos(a) - 3, cy + rr * math.sin(a) - 3, cx + rr * math.cos(a) + 3,
                             cy + rr * math.sin(a) + 3), fill=g)
        for b in range(5):   # bezel buttons
            for side in (x0 + 4, x0 + 214):
                d.rectangle((side, 80 + b * 52, side + 12, 104 + b * 52), fill=(150, 150, 150))
    for k in range(10):
        toggle(d, 40 + k * 48, 440, k % 2 == 0, k == 9)
    return img


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


def leather() -> Image.Image:
    img = noise((92, 62, 40), 14, 41, 1.6)
    d = ImageDraw.Draw(img)
    for x in range(0, SIZE, 128):
        d.line((x, 0, x, SIZE), fill=(60, 40, 26), width=4)
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
    'paint_interior': lambda: worn(noise((206, 208, 196), 7, 1), (120, 122, 110), 2),
    'paint_olive': lambda: worn(noise((84, 92, 58), 8, 3), (60, 58, 50), 4),
    'metal_dark': lambda: brushed((54, 56, 58), 5),
    'rubber': lambda: noise((24, 24, 24), 5, 6, 1.5),
    'panel_analog': panel_analog,
    'panel_mfd': panel_mfd,
    'panel_switches': panel_switches,
    'leather': leather,
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
