"""Developer-only: the helicopter cockpits' photographic textures (pylib/cockpit_kits_air.py), cut from real cockpit
photographs on Wikimedia Commons (public domain US Army photos and one CC BY 2.0 USAF photo), perspective-corrected
and resized with PIL into <model dir>/cockpits/textures/air_*.png, and their credits into
<model dir>/cockpits/CREDITS_air.txt. The installer reads only the PNGs.

    python tools/prep_cockpit_air.py [--src D:/APP/edf6-cockpit-assets/wikimedia-heli-cockpits] [--out <model dir>/cockpits]

The source folder holds the photographs as downloaded (srcNN.jpg) and meta.json (each one's Commons page, author and
licence, read from the Commons API). The photographs are data: only decoded, cropped and resized here.

Textures (each from one region of one photograph; QUADS: the region's corners in the photograph's pixels):
    air_uh60_panel_l / _r   a UH-60M's instrument panel, its left and right halves (four MFDs, the standby
                            instrument between them), seen straight on
    air_uh60_pedestal       its centre console (two CDUs, the flight control switches)
    air_uh60_hood           its glare shield's fabric
    air_seat                a crew seat's black mesh
    air_ah64_panel          an AH-64D pilot's panel face (two MFDs, the up-front display over them)
    air_ah64_standby        its standby instruments beside the panel
    air_ah64_lower          the lower centre panel under it (wiper, generator switches)
    air_ah64_console        a side console's switches

SCREENS (cockpit_kits_air.py reads them): where each panel texture's displays are, as (u0, v0, u1, v1) fractions of
the texture, so the kits put the live read-outs (cockpits.Screen) on the photographed displays.
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

SRC = r'D:\APP\edf6-cockpit-assets\wikimedia-heli-cockpits'
Quad = tuple[tuple[float, float], tuple[float, float], tuple[float, float], tuple[float, float]]   # TL, TR, BR, BL

# src00: "16th CAB upgrades Black Hawk helicopters" (UH-60M, 1920 x 1280, straight on).
# src07: "Paris Air Show 2015 150618-F-RN211-226" (AH-64D pilot station, 1920 x 1278, from the left; the regions
# measured on a crop whose origin is (768, 329) in the photograph).
_A = (768, 329)


def _o(x: float, y: float) -> tuple[float, float]:
    return (x + _A[0], y + _A[1])


QUADS: dict[str, tuple[str, Quad, tuple[int, int]]] = {
    'air_uh60_panel_l': ('src00.jpg', ((70, 235), (900, 235), (900, 640), (70, 640)), (512, 256)),
    'air_uh60_panel_r': ('src00.jpg', ((900, 235), (1730, 235), (1730, 640), (900, 640)), (512, 256)),
    'air_uh60_pedestal': ('src00.jpg', ((480, 650), (1250, 650), (1250, 1180), (480, 1180)), (512, 512)),
    'air_uh60_hood': ('src00.jpg', ((420, 40), (1480, 40), (1480, 200), (420, 200)), (512, 128)),
    'air_seat': ('src00.jpg', ((1560, 960), (1860, 960), (1860, 1120), (1560, 1120)), (256, 256)),
    # the panel is composed (ah64_panel): its strip over the MFDs, and the right MFD's unit twice (in the photograph
    # the cyclic's grip hides the left one)
    'ah64_strip': ('src07.jpg', (_o(80, 52), _o(345, 50), _o(345, 136), _o(82, 136)), (432, 120)),
    'ah64_mfd': ('src07.jpg', (_o(222, 140), _o(395, 140), _o(398, 345), _o(225, 345)), (240, 368)),
    'air_ah64_standby': ('src07.jpg', (_o(425, 330), _o(570, 335), _o(570, 430), _o(425, 425)), (512, 256)),
    'air_ah64_lower': ('src07.jpg', (_o(175, 375), _o(300, 375), _o(305, 700), _o(170, 700)), (256, 512)),
    'air_ah64_console': ('src07.jpg', (_o(455, 615), _o(610, 640), _o(590, 760), _o(395, 720)), (512, 256)),
}

# The displays on the panel textures (u0, v0, u1, v1), measured on the photographs.
SCREENS: dict[str, dict[str, tuple[float, float, float, float]]] = {
    'air_uh60_panel_l': {'mfd1': (45 / 830, 55 / 405, 310 / 830, 250 / 405),
                         'mfd2': (385 / 830, 40 / 405, 650 / 830, 235 / 405)},
    'air_uh60_panel_r': {'mfd3': (215 / 830, 50 / 405, 475 / 830, 250 / 405),
                         'mfd4': (550 / 830, 85 / 405, 790 / 830, 275 / 405)},
    # composed by ah64_panel(): the strip at (40, 4), the MFD units at (10, 136) and (262, 136); in the units the
    # display is (0.16, 0.146, 0.91, 0.78), in the strip the up-front display (0.46, 0.09, 0.86, 0.88)
    'air_ah64_panel': {'mfd_l': ((10 + 0.16 * 240) / 512, (136 + 0.146 * 368) / 512, (10 + 0.91 * 240) / 512,
                                 (136 + 0.78 * 368) / 512),
                       'mfd_r': ((262 + 0.16 * 240) / 512, (136 + 0.146 * 368) / 512, (262 + 0.91 * 240) / 512,
                                 (136 + 0.78 * 368) / 512),
                       'eufd': ((40 + 0.46 * 432) / 512, (4 + 0.09 * 120) / 512, (40 + 0.86 * 432) / 512,
                                (4 + 0.88 * 120) / 512)},
}

CREDIT_USE = {
    'src00.jpg': 'air_uh60_panel_l.png, air_uh60_panel_r.png, air_uh60_pedestal.png, air_uh60_hood.png, air_seat.png',
    'src07.jpg': 'air_ah64_panel.png, air_ah64_standby.png, air_ah64_lower.png, air_ah64_console.png',
}


def rectify(img: Image.Image, quad: Quad, size: tuple[int, int]) -> Image.Image:
    """The photograph's region `quad` (TL, TR, BR, BL) as an upright `size` image (perspective corrected)."""
    (tl, tr, br, bl) = quad
    out = img.transform(size, Image.Transform.QUAD, data=(*tl, *bl, *br, *tr), resample=Image.Resampling.BICUBIC)
    return out.filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))


def ah64_panel(strip: Image.Image, mfd: Image.Image) -> Image.Image:
    """The AH-64D pilot's panel face: the photograph's panel plate colour, its strip (engine fire panel, up-front
    display) on top, two of its MFD units side by side (SCREENS['air_ah64_panel'])."""
    out = Image.new('RGB', (512, 512), (34, 36, 37))
    out.paste(strip, (40, 4))
    out.paste(mfd, (10, 136))
    out.paste(mfd, (262, 136))
    return out


def credits(meta: dict) -> str:
    lines = ['Helicopter cockpits (pylib/cockpit_kits_air.py): third-party material', '']
    for d in sorted(meta.values(), key=lambda d: d['file']):
        if d['file'] not in CREDIT_USE:
            continue
        lic = d['license']
        note = ' (work of the US federal government)' if lic.lower().startswith('public') else ''
        url = {'CC BY 2.0': ' (https://creativecommons.org/licenses/by/2.0/)'}.get(lic, '')
        lines.append(f"Photograph by {d['artist']}, {lic}{url}{note}")
        lines.append(f"    {d['page']} (cropped, perspective-corrected and resized: cockpits/textures/{CREDIT_USE[d['file']]})")
    lines.append('')
    lines.append('The other air_*.png (air_tub, air_overhead) are drawn by '
                 'tools/make_cockpit_textures_air.py.')
    return '\n'.join(lines) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=SRC)
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits'))
    args = ap.parse_args()
    meta = json.load(open(os.path.join(args.src, 'meta.json'), encoding='utf-8'))
    for d in meta.values():
        if d['file'] not in CREDIT_USE:
            continue
        lic = d['license']
        if not (lic.lower().startswith('public') or lic.startswith('CC BY ') or lic.startswith('CC0')) or 'SA' in lic:
            raise SystemExit(f"{d['file']}: licence {lic} is not public domain, CC0 or CC BY")
    tex = os.path.join(args.out, 'textures')
    os.makedirs(tex, exist_ok=True)
    photos: dict[str, Image.Image] = {}
    for name, (src, quad, size) in QUADS.items():
        if src not in photos:
            photos[src] = Image.open(os.path.join(args.src, src)).convert('RGB')
        part = rectify(photos[src], quad, size)
        if not name.startswith('air_'):
            photos[name] = part
            continue
        path = os.path.join(tex, name + '.png')
        part.save(path)
        print(path)
    path = os.path.join(tex, 'air_ah64_panel.png')
    ah64_panel(photos['ah64_strip'], photos['ah64_mfd']).save(path)
    print(path)
    with open(os.path.join(args.out, 'CREDITS_air.txt'), 'w', encoding='utf-8') as h:
        h.write(credits(meta))
    return 0


if __name__ == '__main__':
    sys.exit(main())
