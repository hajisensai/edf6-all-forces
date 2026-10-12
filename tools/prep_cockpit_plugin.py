"""Developer-only: the plugin vehicles' cockpit photographs (pylib/cockpit_kits_plugin.py), cut from real cockpit
photographs on Wikimedia Commons (public domain US Air Force / DoD photos, a public domain F-35 mock-up photo and
one CC BY 2.0 photo of the F-16A at the National Museum of the US Air Force), perspective-corrected and resized
with PIL into <model dir>/cockpits/textures/plugin_*.png, and their credits into
<model dir>/cockpits/CREDITS_plugin.txt.
The installer reads only the PNGs.

    python tools/prep_cockpit_plugin.py [--src D:/APP/edf6-cockpit-assets/plugin-photos] [--out <model dir>/cockpits]

The source folder holds each photograph as downloaded (<slug>/original.jpg, or a 2400 px rendition of it) and
meta.json (each one's Commons page, author and licence, read from the Commons API). The photographs are data: only
decoded, cropped and resized here.

Textures (CROPS: a region of a photograph seen straight on; RECTS: a region of a photograph seen at an angle, mapped
through the perspective that makes one of its flat faces upright):
    plugin_f16a_panel        an F-16A's front panel seen from the pilot's eye (the HUD controls, the threat
                             warning scope, the stores control panel, the flight and engine instruments)
    plugin_f16_console_l/_r  an F-16's left auxiliary panel (landing gear, brakes, stores jettison) and its right
                             console with the side stick
    plugin_f35_panel         an F-35's panoramic cockpit display in its housing, the keypad display under it
    plugin_f35_throttle / _stick   the F-35's throttle on its left console, its side stick on the right one
    plugin_c130_panel_l / _r an HC-130H's instrument panel (glass engine displays, the pilots' tablets), its two halves
    plugin_c130_pedestal     its throttle quadrant
    plugin_c130_glare        its glare shield's fabric cover
    plugin_c130_floor        a C-130's cargo floor (roller tracks, tie-down plates) seen from above
    plugin_c130_wall         a C-130 cargo hold's side wall (frames, stringers, the troop seats' rails)
    plugin_c130_roof         the hold's roof (frames, cable runs, lights)

PANELS (cockpit_kits_plugin.py reads them): the face outline of a panel texture, and where its displays are, as
(u, v) fractions of the texture, so the kits shape the panel like the photographed one and put the live read-outs
(cockpits.Screen) on the photographed displays.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import obj_model as om  # noqa: E402

SRC = r'D:\APP\edf6-cockpit-assets\plugin-photos'
Box = tuple[int, int, int, int]                                          # x0, y0, x1, y1 in the photograph's pixels
Quad = tuple[tuple[float, float], tuple[float, float], tuple[float, float], tuple[float, float]]   # TL, TR, BR, BL

# Straight-on regions: texture -> (slug, box, width of the texture). cockpit_kits_plugin.PANELS measures the
# panels' outlines and displays in these boxes (the photographs' pixels): change both together.
CROPS: dict[str, tuple[str, Box, int]] = {
    # The F-16A at the NMUSAF, from the seat (1300 x 870): the panel from the coaming to the knees.
    'plugin_f16a_panel': ('f16a_nmusaf', (80, 128, 1106, 582), 512),
    # The F-16 at Asian Aerospace 2006 (3072 x 2304, from over the left sill).
    'plugin_f16_console_l': ('f16_asian', (330, 1560, 762, 2300), 256),
    'plugin_f16_console_r': ('f16_asian', (2290, 1350, 3072, 2304), 384),
    # The F-35 mock-up (2048 x 1536, from the right, above).
    'plugin_f35_throttle': ('f35_mockup', (590, 880, 1000, 1500), 256),
    'plugin_f35_stick': ('f35_mockup', (1317, 488, 1624, 927), 256),
    # The HC-130H (a 3840 x 2560 rendition, straight on from between the seats): the panel under the glare shield.
    'plugin_c130_panel_l': ('hc130h_glass', (230, 640, 1920, 1760), 512),
    'plugin_c130_panel_r': ('hc130h_glass', (1920, 640, 3610, 1760), 512),
    'plugin_c130_pedestal': ('hc130h_glass', (1430, 1750, 2560, 2560), 512),
    'plugin_c130_glare': ('hc130h_glass', (900, 470, 2900, 600), 512),
    # A C-130's hold: its floor from above beside a loadmaster (a 3840 x 2555 rendition), its right wall and roof
    # through a fisheye lens looking aft (4000 x 2600; the wall is the least bent part of the frame).
    'plugin_c130_floor': ('c130_floor', (2400, 100, 3780, 1700), 448),
    'plugin_c130_wall': ('c130_cabin', (3060, 150, 3960, 2450), 200),
    'plugin_c130_roof': ('c130_cabin', (950, 0, 3050, 560), 512),
}

# Angled regions: texture -> (slug, the face's corners TL TR BR BL in the photograph, the texture's (u, v) box of that
# face's rectangle (u0, v0, u1, v1; beyond 0..1: the region round it), the texture's size).
RECTS: dict[str, tuple[str, Quad, tuple[float, float, float, float], tuple[int, int]]] = {
    # The F-35's display glass (20 x 8 in): the housing round it and the keypad display below.
    'plugin_f35_panel': ('f35_mockup', ((448, 327), (1023, 174), (1066, 436), (508, 679)), (-0.07, -0.16, 1.07, 1.62),
                         (512, 256)),
}

CREDIT_USE: dict[str, list[str]] = {}
for _t, (_slug, *_r) in {**CROPS, **RECTS}.items():
    CREDIT_USE.setdefault(_slug, []).append(_t)


def homography(src: list[tuple[float, float]], dst: list[tuple[float, float]]) -> list[float]:
    """The 8 coefficients of the projective map taking each `src` point to its `dst` point (PIL's PERSPECTIVE data
    when src are output pixels and dst photograph pixels)."""
    import numpy as np
    a, b = [], []
    for (x, y), (u, v) in zip(src, dst):
        a.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        b.append(u)
        a.append([0, 0, 0, x, y, 1, -v * x, -v * y])
        b.append(v)
    return [float(c) for c in np.linalg.solve(np.array(a, dtype=float), np.array(b, dtype=float))]


def rect(img: Image.Image, quad: Quad, box: tuple[float, float, float, float], size: tuple[int, int]) -> Image.Image:
    """The photograph round the flat face `quad` (TL, TR, BR, BL), the face upright: the texture spans the face's
    (u, v) `box` (0..1 the face itself)."""
    u0, v0, u1, v1 = box
    w, h = size

    def at(u: float, v: float) -> tuple[float, float]:
        return ((u - u0) / (u1 - u0) * w, (v - v0) / (v1 - v0) * h)
    face = [at(0, 0), at(1, 0), at(1, 1), at(0, 1)]
    out = img.transform(size, Image.Transform.PERSPECTIVE, homography(face, list(quad)), Image.Resampling.BICUBIC)
    return out.filter(ImageFilter.UnsharpMask(radius=1.0, percent=50, threshold=2))


def crop(img: Image.Image, box: Box, width: int) -> Image.Image:
    part = img.crop(box)
    w = min(width, 512)
    h = max(4, min(512, 4 * round(part.height * w / part.width / 4)))
    return part.resize((w, h), Image.Resampling.LANCZOS).filter(
        ImageFilter.UnsharpMask(radius=1.0, percent=40, threshold=2))


def credits(meta: dict) -> str:
    lines = ["The plugin's own vehicles' cockpits (pylib/cockpit_kits_plugin.py): third-party material", '']
    for slug, d in sorted(meta.items()):
        if slug not in CREDIT_USE:
            continue
        lic = d['license']
        federal = 'U.S.' in d['artist'] + d['credit']
        note = ' (work of the US federal government)' if lic.lower().startswith('public') and federal else ''
        url = {'CC BY 2.0': ' (https://creativecommons.org/licenses/by/2.0/)'}.get(lic, '')
        lines.append(f"Photograph by {d['artist']}, {lic}{url}{note}")
        uses = ', '.join(t + '.png' for t in CREDIT_USE[slug])
        lines.append(f"    {d['page']} (cropped, perspective-corrected and resized: cockpits/textures/{uses})")
    lines += ['', 'plugin_webbing.png is drawn by tools/make_cockpit_textures_plugin.py.']
    return '\n'.join(lines) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=SRC)
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits'))
    args = ap.parse_args()
    meta = json.load(open(os.path.join(args.src, 'meta.json'), encoding='utf-8'))
    for slug in CREDIT_USE:
        lic = meta[slug]['license']
        if not (lic.lower().startswith('public') or lic.startswith('CC BY ') or lic.startswith('CC0')) or 'SA' in lic:
            raise SystemExit(f'{slug}: licence {lic} is not public domain, CC0 or CC BY')
    tex = os.path.join(args.out, 'textures')
    os.makedirs(tex, exist_ok=True)
    photos: dict[str, Image.Image] = {}

    def photo(slug: str) -> Image.Image:
        if slug not in photos:
            photos[slug] = Image.open(os.path.join(args.src, meta[slug]['file'])).convert('RGB')
        return photos[slug]
    made = {name: crop(photo(slug), box, width) for name, (slug, box, width) in CROPS.items()}
    made.update({name: rect(photo(slug), quad, box, size) for name, (slug, quad, box, size) in RECTS.items()})
    for name, img in sorted(made.items()):
        path = os.path.join(tex, name + '.png')
        img.save(path)
        print(path, img.size)
    with open(os.path.join(args.out, 'CREDITS_plugin.txt'), 'w', encoding='utf-8') as h:
        h.write(credits(meta))
    return 0


if __name__ == '__main__':
    sys.exit(main())
