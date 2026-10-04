"""Read-only view of the game's Root.cpk (cpk.py + crilayla.py), and the game directory the tools default to."""
from __future__ import annotations

import os

import cpk
import crilayla
import gamedir

DEFAULT_GAME = gamedir.find_or_dev()


class Game:
    """Read-only view of the game's Root.cpk."""

    def __init__(self, root: str) -> None:
        self.root = root
        self.cpk = cpk.Cpk(os.path.join(root, 'Root.cpk'))

    def read(self, folder: str, name: str) -> bytes:
        for (d, n), e in self.cpk.index.items():
            if d.upper() == folder.upper() and n.upper() == name.upper():
                with open(self.cpk.path, 'rb') as h:
                    h.seek(self.cpk.base + int(e['FileOffset']))
                    data = h.read(int(e['FileSize']))
                return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data
        raise KeyError(f'{folder}/{name}')
