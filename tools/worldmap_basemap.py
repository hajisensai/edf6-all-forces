"""Developer tool: the world map's base images, NASA Blue Marble Next Generation (topography + bathymetry, December
2004, equirectangular), kept OUTSIDE the repository like the model pack (docs/plan-2026-11-10-worldmap.md §2).

  python -B tools/worldmap_basemap.py fetch [DIR]     download both sources into DIR (default D:\\APP\\edf6-models\\worldmap),
                                                      check their SHA-256, write SOURCE.txt (where from, the licence)
  python -B tools/worldmap_basemap.py derive [DIR]    the two textures' images, still equirectangular:
                                                        bluemarble_4096.png    the world, 4096 x 2048
                                                        bluemarble_japan.png   pylib/worldmap.py DETAIL_* (20 x 20 deg,
                                                                               every place inside) cut from tile D1 at
                                                                               240 px / deg, 4096 x 4096
  python -B tools/worldmap_basemap.py textures [DIR] [OUT]
                                                      the game textures from the two derived PNGs into the model pack
                                                      folder OUT (default DIR/../worldmap_tex, pylib/worldmap.py
                                                      TEXTURE_DIR): only what the release ships, never the sources
                                                        EDF6VC_WORLDMAP_WORLD.DDS / _JAPAN.DDS   DXT1, full mip chain
                                                        worldmap_tex.json   TEXTURE_VERSION, each file's SHA-256 / size
                                                        SOURCE.txt          where from, the credit

Sources: the whole world at 5400 x 2700 (15 px / degree: Japan would be ~300 px wide) and the 500 m tile D1
(90-180 E, 0-90 N, 21600 x 21600) for the zoomed-in view.
Licence: NASA imagery is not copyrighted (NASA Media Usage Guidelines; Visible Earth: free to use, credit NASA Earth
Observatory). It could be committed; it is not, by the maintainer's rule for large assets (2026-11-10).
The game textures (`textures`): loose DDS files under Mods/UI, as the loadout menu's EDF6VC_WHITE.DDS (tools/room_layout.py)
that the plugin already binds through Skin_Texture; DXT1 (BC1) with the legacy 124-byte header and a full mip chain to
1 x 1, the layout of Root.cpk's own UI/LOCALENV_TRAIN_FINE_MENUMODEL.DDS (512 x 512, DXT1, 10 levels). The stock
loading pictures are BC7 (DX10 header); no BC7 encoder is in this project, and DXT1 at 4096 is the plan's choice (§2).
They are built once by the developer and shipped whole: the installer only checks their hashes and copies them
(tools/worldmap_pack.py), it encodes nothing.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import worldmap  # noqa: E402

BASE_URL = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/'


@dataclass(frozen=True)
class Source:
    name: str          # in DIR
    remote: str        # under BASE_URL
    sha256: str
    size: tuple[int, int]
    north: float       # the image's bounds, degrees
    west: float
    degrees: tuple[float, float]   # (east-west, north-south) it spans


WORLD = Source('bluemarble.jpg', 'world.topo.bathy.200412.3x5400x2700.jpg',
               'a9f0088972dee0254610af851c4d6838ca3f2cf79176987e0a5713e2c15ec042', (5400, 2700), 90.0, -180.0,
               (360.0, 180.0))
TILE_D1 = Source('bluemarble_d1.jpg', 'world.topo.bathy.200412.3x21600x21600.D1.jpg',
                 'aff73b464c202319799d29a5f592392618291dabc3ada501d3f0e72852c3a431', (21600, 21600), 90.0, 90.0,
                 (90.0, 90.0))
SOURCES = (WORLD, TILE_D1)
WORLD_OUT, WORLD_SIZE = 'bluemarble_4096.png', (4096, 2048)
JAPAN_OUT, JAPAN_SIZE = 'bluemarble_japan.png', (4096, 4096)
DEFAULT_DIR = r'D:\APP\edf6-models\worldmap'
CREDIT = ('NASA Earth Observatory, Blue Marble Next Generation (Reto Stockli, NASA GSFC), world.topo.bathy.200412 '
          '(December 2004), equirectangular. Public domain (NASA imagery is not copyrighted; credit NASA Earth '
          'Observatory).')


def check(src: Source, data: bytes) -> None:
    got = hashlib.sha256(data).hexdigest()
    if got != src.sha256:
        raise SystemExit(f'{src.name}: SHA-256 {got}, expected {src.sha256} (not the file this tool was written for)')


def fetch(folder: str) -> list[str]:
    os.makedirs(folder, exist_ok=True)
    out = []
    for src in SOURCES:
        req = urllib.request.Request(BASE_URL + src.remote, headers={'User-Agent': 'edf6-all-forces worldmap_basemap'})
        with urllib.request.urlopen(req, timeout=900) as r:
            data = r.read()
        check(src, data)
        path = os.path.join(folder, src.name)
        with open(path, 'wb') as f:
            f.write(data)
        out.append(path)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for src in SOURCES:
            f.write(f'{src.name}\n  {BASE_URL}{src.remote}\n  sha256 {src.sha256}\n')
        f.write(f'{CREDIT}\n')
    return out


def crop_box(src: Source, north: float, south: float, west: float, east: float) -> tuple[int, int, int, int]:
    """The pixel box (left, top, right, bottom) of a lat / lon box inside an equirectangular `src` (ValueError when it
    is not inside)."""
    w, h = src.size
    sx, sy = w / src.degrees[0], h / src.degrees[1]
    box = (round((west - src.west) * sx), round((src.north - north) * sy), round((east - src.west) * sx),
           round((src.north - south) * sy))
    if box[0] < 0 or box[1] < 0 or box[2] > w or box[3] > h or box[0] >= box[2] or box[1] >= box[3]:
        raise ValueError(f'{(north, south, west, east)} is not inside {src.name}')
    return box


def resample(image: 'object', size: tuple[int, int]) -> 'object':
    """The image at `size` (Lanczos); an equirectangular box stays one (the bounds are unchanged)."""
    from PIL import Image
    return image.convert('RGB').resize(size, Image.Resampling.LANCZOS)


def _open(folder: str, src: Source) -> 'object':
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None   # tile D1 is 466 megapixels: ours, its hash checked
    path = os.path.join(folder, src.name)
    with open(path, 'rb') as f:
        check(src, f.read())
    im = Image.open(path)
    if im.size != src.size:
        raise SystemExit(f'{path}: {im.size}, expected {src.size}')
    return im


def derive(folder: str) -> list[str]:
    out = []
    with _open(folder, WORLD) as im:
        path = os.path.join(folder, WORLD_OUT)
        resample(im, WORLD_SIZE).save(path, optimize=True)
        out.append(path)
    with _open(folder, TILE_D1) as im:
        box = crop_box(TILE_D1, worldmap.DETAIL_NORTH, worldmap.DETAIL_SOUTH, worldmap.DETAIL_WEST, worldmap.DETAIL_EAST)
        path = os.path.join(folder, JAPAN_OUT)
        resample(im.crop(box), JAPAN_SIZE).save(path, optimize=True)
        out.append(path)
    return out


# ------------------------------------------------------------------------------------------ game textures

TEXTURE_VERSION = 1    # raise when the textures' bytes change for the same sources (tools/worldmap_pack.py checks it)
TEXTURE_SOURCES = {'world': WORLD_OUT, 'japan': JAPAN_OUT}   # pylib/worldmap.py TEXTURES kind -> derived PNG
TEXTURE_SIZES = {'world': WORLD_SIZE, 'japan': JAPAN_SIZE}
PAGE_URL = 'https://visibleearth.nasa.gov/collection/1484/blue-marble'


def texture_dds(image: 'object') -> bytes:
    """A PIL image as the DXT1 DDS the game reads (texfile_accel.dxt1_dds: range-fit blocks, a 2x2 box-filtered mip
    chain to 1 x 1, the legacy header); alpha dropped."""
    import texfile
    import texfile_accel
    rgba = image.convert('RGBA')
    return texfile_accel.dxt1_dds(texfile.Image(rgba.width, rgba.height, bytearray(rgba.tobytes())))


def texture_source_text() -> str:
    lines = ['World map base textures for EDF6 All Forces (tools/worldmap_basemap.py textures).', '',
             f'Credit: {CREDIT}', f'Collection page: {PAGE_URL}', 'Author: Reto Stockli, NASA Goddard Space Flight Center',
             '(NASA Earth Observatory). No Sketchfab uid: not a Sketchfab model.', '', 'Sources:']
    for src in SOURCES:
        lines.append(f'  {BASE_URL}{src.remote}')
        lines.append(f'    sha256 {src.sha256}')
    lines += ['', 'Files:']
    for kind, rel in worldmap.TEXTURES.items():
        w, h = TEXTURE_SIZES[kind]
        lines.append(f'  {rel.rsplit("/", 1)[1]}  {w} x {h} DXT1 + mips, from {TEXTURE_SOURCES[kind]} '
                     f'(equirectangular; installed as Mods/{rel})')
    return '\n'.join(lines) + '\n'


def textures(folder: str, out: str | None = None) -> list[str]:
    """The release folder (pylib/worldmap.py TEXTURE_DIR) from `folder`'s derived PNGs (run `derive` first)."""
    import json
    from PIL import Image
    out = out or os.path.join(os.path.dirname(os.path.abspath(folder)), worldmap.TEXTURE_DIR)
    os.makedirs(out, exist_ok=True)
    files = {}
    written = []
    for kind, rel in worldmap.TEXTURES.items():
        path = os.path.join(folder, TEXTURE_SOURCES[kind])
        with Image.open(path) as im:
            if im.size != TEXTURE_SIZES[kind]:
                raise SystemExit(f'{path}: {im.size}, expected {TEXTURE_SIZES[kind]} (run derive)')
            data = texture_dds(im)
        name = rel.rsplit('/', 1)[1]
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
        files[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
        written.append(os.path.join(out, name))
    with open(os.path.join(out, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(texture_source_text())
    written.append(os.path.join(out, 'SOURCE.txt'))
    with open(os.path.join(out, worldmap.TEXTURE_INDEX), 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'version': TEXTURE_VERSION, 'files': files}, f, indent=1, sort_keys=True)
        f.write('\n')
    written.append(os.path.join(out, worldmap.TEXTURE_INDEX))
    return written


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] not in ('fetch', 'derive', 'textures'):
        print(__doc__)
        return 2
    folder = argv[2] if len(argv) > 2 else DEFAULT_DIR
    if argv[1] == 'textures':
        paths = textures(folder, argv[3] if len(argv) > 3 else None)
    else:
        paths = fetch(folder) if argv[1] == 'fetch' else derive(folder)
    for path in paths:
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
