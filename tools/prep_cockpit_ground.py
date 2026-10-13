"""The ground vehicles' cockpit assets made from real sources (a developer step, like tools/prep_sazabi.py; the installer
reads the results, it needs neither PIL nor the sources): photo textures cropped from public-domain / CC0 / CC-BY
photographs (Wikimedia Commons, downloaded with their licence metadata into <assets>/ground-photos/<name>.jpg and
<name>.jpg.json), resized to at most 512 px into <model dir>/cockpits/textures/ground_*.png, and the CC0 steering
wheel into <model dir>/cockpits/parts/oga-steering-wheel-cc0/ with its LICENSE.txt. Writes
<model dir>/cockpits/CREDITS_ground.txt: every used source with its page, author and licence. Reads the sources as
data only (images and an OBJ text file); nothing in them is run.

    python tools/prep_cockpit_ground.py [--assets D:/APP/edf6-cockpit-assets] [--out <model dir>/cockpits]
"""
from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import sys
from dataclasses import dataclass

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import obj_model as om  # noqa: E402

LIMIT = 512


@dataclass(frozen=True)
class Crop:
    """A texture `name` (textures/<name>.png) cut from the photo `photo` (ground-photos/<photo>.jpg) at `box` (left,
    top, right, bottom in the downloaded image's pixels): what it shows, for the reader."""
    name: str
    photo: str
    box: tuple[int, int, int, int]
    shows: str


CROPS = (

    Crop('ground_dash_console', 'f150_dash', (660, 260, 1920, 650), "a pickup's dashboard: radio, vents, glovebox"),
)

WHEEL = ('oga-steering-wheel-cc0', 'low_6.obj', 'steering wheel by Savino (OpenGameArt, '
         'https://opengameart.org/content/steering-wheel), CC0')


def plain(text: str | None) -> str:
    return re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', '', text or ''))).strip()


def crop(src: str, c: Crop, out: str) -> dict:
    meta = json.load(open(os.path.join(src, c.photo + '.jpg.json'), encoding='utf-8'))
    if meta['license'] not in ('Public domain', 'CC0', 'CC BY 4.0', 'CC BY 3.0', 'CC BY 2.0'):
        raise SystemExit(f'{c.photo}: licence {meta["license"]} is not PD / CC0 / CC-BY')
    im = Image.open(os.path.join(src, c.photo + '.jpg')).convert('RGB').crop(c.box)
    im.thumbnail((LIMIT, LIMIT), Image.LANCZOS)
    im.save(os.path.join(out, 'textures', c.name + '.png'))
    return meta


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=r'D:\APP\edf6-cockpit-assets')
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits'))
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, 'textures'), exist_ok=True)
    credits, seen = [], set()
    for c in CROPS:
        meta = crop(os.path.join(a.assets, 'ground-photos'), c, a.out)
        print(os.path.join(a.out, 'textures', c.name + '.png'))
        if c.photo not in seen:
            seen.add(c.photo)
            credits.append(f'{meta["title"][5:]}: {plain(meta["artist"])}, {meta["license"]}, {meta["page"]}')
        credits.append(f'    textures/{c.name}.png: {c.shows}')
    asset, name, line = WHEEL
    dst = os.path.join(a.out, 'parts', asset)
    os.makedirs(dst, exist_ok=True)
    shutil.copyfile(os.path.join(a.assets, asset, name), os.path.join(dst, name))
    with open(os.path.join(dst, 'LICENSE.txt'), 'w', encoding='utf-8') as h:
        h.write(f'{line}.\nCC0 1.0 Universal: https://creativecommons.org/publicdomain/zero/1.0/\n')
    credits.append(f'{line}: parts/{asset}/{name}')
    credits.append('Also used, credited in CREDITS_photos.txt: textures/photo_mrap_dashboard.png (the military cabs\' '
                   'dashboards) and the photo_m1a1* textures of cockpit_kits.tank_gunner (the turrets).')
    with open(os.path.join(a.out, 'CREDITS_ground.txt'), 'w', encoding='utf-8') as h:
        h.write('The ground vehicles\' cockpits (pylib/cockpit_kits_ground.py) use these sources:\n\n')
        h.write('\n'.join(credits) + '\n')
    print(os.path.join(a.out, 'CREDITS_ground.txt'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
