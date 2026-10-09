"""Read-only access to files inside the game's Root.cpk (cpk.py + crilayla.py), and the game directory the
tools default to.

DEFAULT_GAME is what gamedir.find() finds ($EDF6_DIR, next to the running program, the Steam libraries), else
the developer's path. Nothing here writes to the game directory.
"""
from __future__ import annotations

import os
from functools import lru_cache

import cpk
import crilayla
import gamedir

DEFAULT_GAME = gamedir.find_or_dev()


class Game:
    """Read-only view of the game's Root.cpk (or another of its archives: Chunk01.cpk / Chunk02.cpk hold more of the
    objects)."""

    def __init__(self, root: str, archive: str = 'Root.cpk') -> None:
        self.root = root
        self.cpk = cpk.Cpk(os.path.join(root, archive))

    def _key(self, folder: str, name: str) -> tuple[str, str]:
        for k in self.cpk.index:
            if k[0].upper() == folder.upper() and k[1].upper() == name.upper():
                return k
        raise KeyError(f'{folder}/{name}')

    def names(self, folder: str) -> list[str]:
        """The file names in `folder` (case-insensitive), sorted."""
        return sorted(n for d, n in self.cpk.index if d.upper() == folder.upper())

    def read(self, folder: str, name: str) -> bytes:
        """The file folder/name (case-insensitive), decompressed."""
        e = self.cpk.index[self._key(folder, name)]
        with open(self.cpk.path, 'rb') as h:
            h.seek(self.cpk.base + int(e['FileOffset']))
            data = h.read(int(e['FileSize']))
        return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data


@lru_cache(maxsize=None)
def default() -> Game:
    """DEFAULT_GAME's Root.cpk, opened once per process."""
    return Game(DEFAULT_GAME)


def use(root: str) -> None:
    """Makes `root` DEFAULT_GAME for the rest of the process: the installer's game (found, or pasted by the player)
    for the tools that read through default() (autoturret/tools/build.py, describe.py)."""
    global DEFAULT_GAME
    DEFAULT_GAME = os.path.normpath(root)
    default.cache_clear()
