"""Developer-only: the plugin vehicles' drawn cockpit textures (pylib/cockpit_kits_plugin.py) into
<model dir>/cockpits/textures/. Only what no photograph gives cleanly; the panels and the hold are photographs
(tools/prep_cockpit_plugin.py).

    python tools/make_cockpit_textures_plugin.py [--out <model dir>/cockpits]

    plugin_webbing    a troop seat's red nylon webbing (a C-130's side-wall seats): straps woven over and under,
                      one repeat 0.5 m
"""
from __future__ import annotations

import argparse
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import obj_model as om  # noqa: E402

SIZE = 256
STRAP, GAP = 22, 10            # px: a strap 4.3 cm wide, the gaps between them 2 cm (one repeat 0.5 m)
RED, DARK = (150, 38, 30), (40, 14, 12)


def webbing() -> Image.Image:
    """Straps across and down on a dark backing; where they cross, the one on top lighter, alternating."""
    rnd = random.Random(6)
    img = Image.new('RGB', (SIZE, SIZE), DARK)
    d = ImageDraw.Draw(img)
    step = STRAP + GAP
    for k in range(SIZE // step):
        a = k * step + GAP // 2
        d.rectangle([a, 0, a + STRAP - 1, SIZE - 1], fill=RED)
        d.rectangle([0, a, SIZE - 1, a + STRAP - 1], fill=tuple(c - 18 for c in RED))
    for i in range(SIZE // step):
        for j in range(SIZE // step):
            x, y = i * step + GAP // 2, j * step + GAP // 2
            top = RED if (i + j) % 2 else tuple(c - 18 for c in RED)
            d.rectangle([x, y, x + STRAP - 1, y + STRAP - 1], fill=tuple(min(255, c + 22) for c in top))
    px = img.load()
    for y in range(SIZE):              # the weave's fine ribs and a little dirt
        for x in range(SIZE):
            r, g, b = px[x, y]
            n = rnd.randint(-10, 10) + (6 if (x + y) % 4 == 0 else 0)
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n // 2)), max(0, min(255, b + n // 2)))
    return img.filter(ImageFilter.SMOOTH)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(om.DEV_MODELS, 'cockpits'))
    args = ap.parse_args()
    tex = os.path.join(args.out, 'textures')
    os.makedirs(tex, exist_ok=True)
    path = os.path.join(tex, 'plugin_webbing.png')
    webbing().save(path)
    print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
