"""Developer tool: the world map's base map, NASA Blue Marble Next Generation (topography + bathymetry, December 2004,
equirectangular) at its full 500 m resolution over the whole Earth, kept OUTSIDE the repository like the model pack
(docs/plan-2026-11-10-worldmap.md §2).

  python -B tools/worldmap_basemap.py fetch [DIR]     download the eight 500 m tiles (A1..D2) into DIR (default
                                                      D:\\APP\\edf6-models\\worldmap), check their SHA-256, write SOURCE.txt
  python -B tools/worldmap_basemap.py tiles [DIR] [OUT]
                                                      the JPEG tile pyramid (pylib/worldmap.py TILE, TILE_LEVELS: the
                                                      layout and the file format are written there) into the model pack
                                                      folder OUT (default DIR/../worldmap_tex, worldmap.TEXTURE_DIR): only
                                                      what the release ships, never the sources
                                                        EDF6VehicleCrew.worldmap.bin   every tile in one file (~344 MB)
                                                        worldmap_tex.json   worldmap.TEXTURE_VERSION, the file's SHA-256 / size
                                                        SOURCE.txt          where from, the credit

Sources: eight 21600 x 21600 tiles, each 90 x 90 degrees (A..D west to east from 180 W, 1 north / 2 south of the
equator), 240 px / degree: together 86400 x 43200 px. The deepest pyramid level is that resolution (nothing upsampled),
each level above it half as wide, down to one 1350 x 675 tile of the whole Earth.
Licence: NASA imagery is not copyrighted (NASA Media Usage Guidelines; Visible Earth: free to use, credit NASA Earth
Observatory). It could be committed; it is not, by the maintainer's rule for large assets (2026-11-10).
Why JPEG (maintainer 2026-11-11, "换成 JPEG 全球240"): the same pyramid as DXT1 game textures would be ~2.5 GB, as JPEG
quality 85 344 MB at PSNR 35-40 dB against the sources (2026-11-11 trial: Tokyo Bay 38.2, the Alps 35.3, the Sahara
40.5). The plugin decodes the tiles it shows itself (WIC) and draws them (D3D11); the game's texture loader never sees them.
Built once by the developer (about a minute) and shipped whole: the installer only checks the hash and copies the file
(tools/worldmap_pack.py), it encodes nothing.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
import json
import os
import struct
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import worldmap  # noqa: E402

BASE_URL = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/'
PAGE_URL = 'https://visibleearth.nasa.gov/collection/1484/blue-marble'
DEFAULT_DIR = r'D:\APP\edf6-models\worldmap'
CREDIT = ('NASA Earth Observatory, Blue Marble Next Generation (Reto Stockli, NASA GSFC), world.topo.bathy.200412 '
          '(December 2004), equirectangular. Public domain (NASA imagery is not copyrighted; credit NASA Earth '
          'Observatory).')
COLUMNS, ROWS = 4, 2    # the sources: A..D west to east, 1..2 north to south
SOURCE_PX = 21600


@dataclass(frozen=True)
class Source:
    tile: str          # A1 .. D2
    sha256: str

    @property
    def name(self) -> str:          # in DIR
        return f'bluemarble_{self.tile.lower()}.jpg'

    @property
    def remote(self) -> str:        # under BASE_URL
        return f'world.topo.bathy.200412.3x21600x21600.{self.tile}.jpg'

    @property
    def cell(self) -> tuple[int, int]:      # (column, row) among the sources
        return 'ABCD'.index(self.tile[0]), int(self.tile[1]) - 1


SOURCES = (Source('A1', 'b9bf1203de698f78bc588d3222e3ac97dc9f56d681fdcdc9a3c5025e47259cc0'),
           Source('B1', '819ca802a3f7a891a64c54c453c66f25a31dc768eedb1670ee774de5d1829ae2'),
           Source('C1', 'e1864a8a30f630185830293a2fda5b22f156ba5c03a3956490bea647a66bc950'),
           Source('D1', 'aff73b464c202319799d29a5f592392618291dabc3ada501d3f0e72852c3a431'),
           Source('A2', 'bc8a13d73d55908114d31e89bb893593122a77412befb6d2cc560de5c1b43690'),
           Source('B2', 'c6b3cd26536de394586433a6cd9ea40bab9fac93fd44cb7fa3ab230737f6877c'),
           Source('C2', '4e71626be3b7b18d28d0f2ba32ca4d6f1c153edddd7c22943a473dc989d237b6'),
           Source('D2', '9e65f151aa0054336102f803e231c962e897c8d5ecec8079c175aa7a24451707'))


def check(src: Source, data: bytes) -> None:
    got = hashlib.sha256(data).hexdigest()
    if got != src.sha256:
        raise SystemExit(f'{src.name}: SHA-256 {got}, expected {src.sha256} (not the file this tool was written for)')


def fetch(folder: str) -> list[str]:
    os.makedirs(folder, exist_ok=True)
    out = []
    for src in SOURCES:
        path = os.path.join(folder, src.name)
        if os.path.isfile(path) and hashlib.sha256(open(path, 'rb').read()).hexdigest() == src.sha256:
            out.append(path)
            continue
        req = urllib.request.Request(BASE_URL + src.remote, headers={'User-Agent': 'edf6-all-forces worldmap_basemap'})
        with urllib.request.urlopen(req, timeout=1800) as r:
            data = r.read()
        check(src, data)
        with open(path, 'wb') as f:
            f.write(data)
        out.append(path)
    with open(os.path.join(folder, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for src in SOURCES:
            f.write(f'{src.name}\n  {BASE_URL}{src.remote}\n  sha256 {src.sha256}\n')
        f.write(f'{CREDIT}\n')
    return out


# ------------------------------------------------------------------------------------------ the tile pyramid

def per_source(level: int) -> int:
    """Tiles across one source at `level` (0: the level is coarser than one tile per source)."""
    cols = worldmap.tile_grid(level)[0]
    return cols // COLUMNS if cols >= COLUMNS else 0


def jpeg(image: 'object') -> bytes:
    buf = io.BytesIO()
    image.convert('RGB').save(buf, 'JPEG', quality=worldmap.TILE_QUALITY, subsampling=2, optimize=True)
    return buf.getvalue()


def pyramid(images: dict[str, 'object']) -> dict[tuple[int, int, int], bytes]:
    """{(level, column, row): JPEG} from the eight source images ({tile: PIL image}, each TILE * per_source(deepest) px
    square; taken out of `images` one at a time, so only one is ever decoded). Levels with at least one tile per source are cut from each source alone; the coarser ones from a mosaic of
    the eight at half a tile each."""
    from PIL import Image
    t, deepest = worldmap.TILE, worldmap.TILE_LEVELS - 1
    side = t * per_source(deepest)
    if per_source(deepest) < 1 or sorted(images) != sorted(s.tile for s in SOURCES):
        raise SystemExit(f'need the eight sources {[s.tile for s in SOURCES]} and a pyramid of at least one tile each')
    out: dict[tuple[int, int, int], bytes] = {}
    mosaic = Image.new('RGB', (COLUMNS * t // 2, ROWS * t // 2))
    for src in SOURCES:
        im = images.pop(src.tile)      # dropped once cut: a decoded source is 1.4 GB
        if im.mode != 'RGB':
            im = im.convert('RGB')
        if im.size != (side, side):
            raise SystemExit(f'{src.name}: {im.size}, expected {(side, side)}')
        sx, sy = src.cell
        level = deepest
        while per_source(level):
            n = per_source(level)
            for ty in range(n):
                for tx in range(n):
                    out[(level, sx * n + tx, sy * n + ty)] = jpeg(im.crop((tx * t, ty * t, tx * t + t, ty * t + t)))
            if n == 1:
                break
            im = im.resize((im.width // 2, im.height // 2), Image.Resampling.LANCZOS)
            level -= 1
        mosaic.paste(im.resize((t // 2, t // 2), Image.Resampling.LANCZOS), (sx * t // 2, sy * t // 2))
    level = min(lv for lv, _x, _y in out) - 1
    while level >= 0:
        cols, rows = worldmap.tile_grid(level)
        if mosaic.size != (cols * t, rows * t if level else t // 2):
            mosaic = mosaic.resize((cols * t, rows * t if level else t // 2), Image.Resampling.LANCZOS)
        for ty in range(rows):
            for tx in range(cols):
                out[(level, tx, ty)] = jpeg(mosaic.crop((tx * t, ty * t, tx * t + t, min(mosaic.height, ty * t + t))))
        level -= 1
    return out


def pack(tiles: dict[tuple[int, int, int], bytes]) -> bytes:
    """The file pylib/worldmap.py describes (TILES_FILE) from every tile of the pyramid."""
    order = [(z, x, y) for z in range(worldmap.TILE_LEVELS)
             for y in range(worldmap.tile_grid(z)[1]) for x in range(worldmap.tile_grid(z)[0])]
    if sorted(order) != sorted(tiles):
        raise SystemExit('the pyramid is not complete')
    head = struct.pack('<4sIII', worldmap.TILES_MAGIC, worldmap.TILES_FORMAT, worldmap.TILE, worldmap.TILE_LEVELS)
    offset = len(head) + 12 * len(order)
    table, body = [], []
    for k in order:
        table.append(struct.pack('<QI', offset, len(tiles[k])))
        body.append(tiles[k])
        offset += len(tiles[k])
    return b''.join([head, *table, *body])


def source_text(name: str) -> str:
    lines = ['World map base map for EDF6 All Forces (tools/worldmap_basemap.py tiles).', '',
             f'Credit: {CREDIT}', f'Collection page: {PAGE_URL}', 'Author: Reto Stockli, NASA Goddard Space Flight Center',
             '(NASA Earth Observatory). No Sketchfab uid: not a Sketchfab model.', '', 'Sources:']
    for src in SOURCES:
        lines.append(f'  {BASE_URL}{src.remote}')
        lines.append(f'    sha256 {src.sha256}')
    cols, rows = worldmap.tile_grid(worldmap.TILE_LEVELS - 1)
    lines += ['', 'Files:',
              f'  {name}  {worldmap.tile_count()} JPEG tiles (quality {worldmap.TILE_QUALITY}) of {worldmap.TILE} px, '
              f'{worldmap.TILE_LEVELS} levels, the deepest {cols * worldmap.TILE} x {rows * worldmap.TILE} '
              f'(equirectangular; installed as Mods/{worldmap.TILES_FILE})']
    return '\n'.join(lines) + '\n'


def write_release(data: bytes, out: str) -> list[str]:
    os.makedirs(out, exist_ok=True)
    name = worldmap.TILES_FILE.rsplit('/', 1)[1]
    paths = [os.path.join(out, name), os.path.join(out, 'SOURCE.txt'), os.path.join(out, worldmap.TEXTURE_INDEX)]
    with open(paths[0], 'wb') as f:
        f.write(data)
    with open(paths[1], 'w', encoding='utf-8', newline='\n') as f:
        f.write(source_text(name))
    with open(paths[2], 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'version': worldmap.TEXTURE_VERSION,
                   'files': {name: {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}}},
                  f, indent=1, sort_keys=True)
        f.write('\n')
    return paths


def tiles(folder: str, out: str | None = None) -> list[str]:
    """The release folder (worldmap.TEXTURE_DIR) from the eight sources in `folder` (run `fetch` first)."""
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None   # 466 megapixels each: ours, their hashes checked
    out = out or os.path.join(os.path.dirname(os.path.abspath(folder)), worldmap.TEXTURE_DIR)
    images = {}
    for src in SOURCES:
        path = os.path.join(folder, src.name)
        with open(path, 'rb') as f:
            check(src, f.read())
        images[src.tile] = Image.open(path)     # decoded lazily, one at a time, by pyramid()
    return write_release(pack(pyramid(images)), out)


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] not in ('fetch', 'tiles'):
        print(__doc__)
        return 2
    folder = argv[2] if len(argv) > 2 else DEFAULT_DIR
    paths = fetch(folder) if argv[1] == 'fetch' else tiles(folder, argv[3] if len(argv) > 3 else None)
    for path in paths:
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
