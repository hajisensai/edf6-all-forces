"""Prepare the mech cockpits' third-party material (pylib/cockpit_kits_mech.py) in <model dir>/cockpits/ (a developer
step, like tools/prep_sazabi.py), and write CREDITS_mech.txt:

  - the seat model: the downloaded glTF binary copied unchanged into parts/<asset>/ with its LICENSE.txt (only read:
    pylib/gltf.py parses it; nothing in it is run; the kit cuts and scales it when the model is built);
  - the panel textures: crops of public-domain photographs of real glass cockpits (US Government works on Wikimedia
    Commons), resized to at most 512 px into textures/mech_*.png.

    python tools/prep_cockpit_mech.py [--src D:/APP/edf6-cockpit-assets] [--photos DIR] [--out <model dir>/cockpits]

--photos: a folder with one sub-folder per photo (PHOTOS' keys), each holding image.jpg, the Commons 1280 px rendition
of that file (tmp/mech_downloads in the worktree).
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import gltf  # noqa: E402
import obj_model as om  # noqa: E402

# asset slug -> (source file under --src, its file name in parts/<slug>/, attribution as in the asset catalog, page)
ASSETS = {
    'pp-scifi-cockpit-revert': ('pp-scifi-cockpit-revert/the_cockpit.glb', 'the_cockpit.glb',
                                'The cockpit by Romain Revert (poly.pizza), CC-BY 3.0',
                                'https://poly.pizza/m/ccQg0nHGSA9'),
}
LICENSE = """{credit}
Source: {page}
Licence: Creative Commons Attribution 3.0 (CC BY 3.0), https://creativecommons.org/licenses/by/3.0/
Changes: none to this file; the EDF6 plugin's cockpit builder (pylib/cockpit_kits_mech.py) leaves out its screen tower
and roof bar, scales it and places it in the mech cockpits it builds.
"""

# photo slug -> (Commons file page, author as Commons gives it, licence)
PHOTOS = {
    'commons-c141c-glass-cockpit': ('https://commons.wikimedia.org/wiki/File:C-141C_Glass_Cockpit_Upgrade.JPEG',
                                    'TSgt Lance Cheung, USAF', 'Public domain (work of the US federal government)'),
    'commons-kc135-block45-cockpit': ('https://commons.wikimedia.org/wiki/File:KC-135_Block_45_glass_cockpit_2017.jpg',
                                      'Master Sgt. Vincent De Groot, USAF',
                                      'Public domain (work of the US federal government)'),
}
# texture -> (photo slug, crop box in the 1280 px rendition, output size): what the kit puts it on
CROPS = {
    'mech_dash': ('commons-c141c-glass-cockpit', (370, 215, 890, 500), (512, 256)),       # the panel under the visor
    'mech_mfd': ('commons-kc135-block45-cockpit', (480, 340, 820, 600), (512, 512)),      # each screen's bezel
    'mech_sill': ('commons-kc135-block45-cockpit', (160, 235, 1120, 345), (512, 64)),     # the switch strip at the sill
    'mech_overhead': ('commons-c141c-glass-cockpit', (420, 60, 780, 170), (512, 128)),    # the strip over the visor
}


def default_out() -> str:
    return os.path.join(om.DEV_MODELS, 'cockpits')   # where make_cockpit_textures.py writes (not a worktree's models/)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=r'D:\APP\edf6-cockpit-assets')
    ap.add_argument('--photos', default=os.path.join(HERE, '..', 'tmp', 'mech_downloads'))
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    out = a.out or default_out()
    credits = []
    for slug, (src, name, credit, page) in sorted(ASSETS.items()):
        path = os.path.join(a.src, src)
        scene = gltf.read(path)   # parses: a file the kit could not read is refused here
        dest = os.path.join(out, 'parts', slug)
        os.makedirs(dest, exist_ok=True)
        shutil.copyfile(path, os.path.join(dest, name))
        with open(os.path.join(dest, 'LICENSE.txt'), 'w', encoding='utf-8', newline='\n') as h:
            h.write(LICENSE.format(credit=credit, page=page))
        credits.append(credit + '\n    ' + f'{page} (cockpits/parts/{slug}/)')
        print(f'{slug}: {len(scene.triangles)} triangles -> {dest}')
    os.makedirs(os.path.join(out, 'textures'), exist_ok=True)
    used: dict[str, list[str]] = {}
    for tex, (slug, crop, size) in sorted(CROPS.items()):
        img = Image.open(os.path.join(a.photos, slug, 'image.jpg')).convert('RGB').crop(crop)
        path = os.path.join(out, 'textures', tex + '.png')
        img.resize(size, Image.LANCZOS).save(path)
        used.setdefault(slug, []).append(tex)
        print(path)
    for slug, texs in sorted(used.items()):
        page, author, licence = PHOTOS[slug]
        credits.append(f'Photograph by {author}, {licence}\n    {page} (cropped and resized: cockpits/textures/'
                       + ', '.join(t + '.png' for t in texs) + ')')
    with open(os.path.join(out, 'CREDITS_mech.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('Mech cockpits (pylib/cockpit_kits_mech.py): third-party material\n\n' + '\n'.join(credits) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
