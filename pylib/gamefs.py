"""Read-only access to files inside the game's Root.cpk.

The game directory is $EDF6_DIR, else what gamedir.find() finds (next to the running program, the Steam
libraries), else the developer's path. Nothing here writes to the game directory.
"""
from __future__ import annotations

import os
from functools import lru_cache

import cpk
import crilayla
import gamedir


def game_dir() -> str:
    return os.environ.get('EDF6_DIR') or gamedir.find_or_dev()


GAME = game_dir()   # the directory read from (kept for the callers that name it)


@lru_cache(maxsize=None)
def _root() -> 'cpk.Cpk':
    return cpk.Cpk(os.path.join(GAME, 'Root.cpk'))


def _key(d: str, n: str) -> tuple[str, str]:
    for k in _root().index:
        if k[0].upper() == d.upper() and k[1].upper() == n.upper():
            return k
    raise KeyError(f'{d}/{n}')


def names(d: str) -> list[str]:
    return sorted(n for dd, n in _root().index if dd.upper() == d.upper())


def read(d: str, n: str) -> bytes:
    """The file d/n (folder, name; case-insensitive), decompressed."""
    c = _root()
    e = c.index[_key(d, n)]
    with open(c.path, 'rb') as h:
        h.seek(c.base + int(e['FileOffset']))
        data = h.read(int(e['FileSize']))
    return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data
