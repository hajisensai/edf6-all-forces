"""Builds a small tiles file (tools/worldmap_basemap.py with an 8 px tile, eight flat-coloured sources as
tests/test_worldmap_pack.py) and a truncated copy, then runs the C++ reader test on them (src/worldmap_tiles.cpp).

    python tests/worldmap_tiles_test.py <worldmap_tiles_test.exe>
"""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'pylib'), str(ROOT / 'tests')]

import test_worldmap_pack as twp  # noqa: E402
import worldmap  # noqa: E402
import worldmap_basemap as wb  # noqa: E402


def main(argv: list[str]) -> int:
    exe = str(Path(argv[1]).resolve())
    with tempfile.TemporaryDirectory() as work, mock.patch.object(worldmap, 'TILE', twp.SMALL_TILE):
        data = wb.pack(wb.pyramid(twp.sources()))
        full, cut = Path(work, 'tiles.bin'), Path(work, 'cut.bin')
        full.write_bytes(data)
        cut.write_bytes(data[:len(data) - 100])
        return subprocess.run([exe, str(full), str(cut), str(twp.SMALL_TILE), str(worldmap.TILE_LEVELS),
                               str(worldmap.TILES_FORMAT)]).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv))
