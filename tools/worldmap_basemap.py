"""Developer tool: the world map's base image, NASA Blue Marble Next Generation (topography + bathymetry, December
2004, 5400 x 2700, equirectangular), kept OUTSIDE the repository like the model pack (docs/plan-2026-11-10-worldmap.md
§2).

  python -B tools/worldmap_basemap.py fetch [DIR]     download it into DIR (default D:\\APP\\edf6-models\\worldmap),
                                                      check its SHA-256, write SOURCE.txt (where from, the licence)
  python -B tools/worldmap_basemap.py derive [DIR]    DIR/bluemarble.jpg -> DIR/bluemarble_4096.png (4096 x 2048, the
                                                      power-of-two size a game texture wants; still equirectangular)

Licence: NASA imagery is not copyrighted (NASA Media Usage Guidelines; Visible Earth: "free to use, credit NASA Earth
Observatory"). It could be committed; it is not, by the maintainer's rule for large assets (2026-11-10).
The game texture (DDS inside a RAB the plugin loads) is made from the derived PNG once the drawing path is fixed (plan
§4); that converter is not written yet.
"""
from __future__ import annotations

import hashlib
import os
import sys
import urllib.request

URL = ('https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/'
       'world.topo.bathy.200412.3x5400x2700.jpg')
SHA256 = 'a9f0088972dee0254610af851c4d6838ca3f2cf79176987e0a5713e2c15ec042'
SIZE = (5400, 2700)
DERIVED = (4096, 2048)
SOURCE_NAME = 'bluemarble.jpg'
DERIVED_NAME = 'bluemarble_4096.png'
DEFAULT_DIR = r'D:\APP\edf6-models\worldmap'
CREDIT = ('NASA Earth Observatory, Blue Marble Next Generation (Reto Stockli, NASA GSFC), '
          'world.topo.bathy.200412 (December 2004), 5400 x 2700, equirectangular. Public domain (NASA imagery is not '
          'copyrighted; credit NASA Earth Observatory).')


def check(data: bytes) -> None:
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        raise SystemExit(f'Blue Marble: SHA-256 {got}, expected {SHA256} (not the file this tool was written for)')


def fetch(folder: str) -> str:
    os.makedirs(folder, exist_ok=True)
    req = urllib.request.Request(URL, headers={'User-Agent': 'edf6-all-forces worldmap_basemap'})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    check(data)
    path = os.path.join(folder, SOURCE_NAME)
    with open(path, 'wb') as f:
        f.write(data)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'{SOURCE_NAME}\n  {URL}\n  sha256 {SHA256}\n  {CREDIT}\n')
    return path


def resample(image: 'object', size: tuple[int, int] = DERIVED) -> 'object':
    """The equirectangular image at `size` (Lanczos); still plate carree, so the projection is unchanged."""
    from PIL import Image
    return image.convert('RGB').resize(size, Image.Resampling.LANCZOS)


def derive(folder: str) -> str:
    from PIL import Image
    src = os.path.join(folder, SOURCE_NAME)
    with open(src, 'rb') as f:
        check(f.read())
    with Image.open(src) as im:
        if im.size != SIZE:
            raise SystemExit(f'{src}: {im.size}, expected {SIZE}')
        out = os.path.join(folder, DERIVED_NAME)
        resample(im).save(out, optimize=True)
    return out


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] not in ('fetch', 'derive'):
        print(__doc__)
        return 2
    folder = argv[2] if len(argv) > 2 else DEFAULT_DIR
    print(fetch(folder) if argv[1] == 'fetch' else derive(folder))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
