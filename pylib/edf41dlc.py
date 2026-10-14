"""EDF4.1's two DLC mission packs, read from the player's (or developer's) EDF4.1 install, and where their files go in
EDF6's Mods.

4.1 installs each DLC as <EDF4.1>/DLC/<hash>/dlc.cpk, its files under '<app id>/...' and mounted at 'mp01:/' (410780,
Mission Pack 1) or 'mp02:/' (411380, Mission Pack 2) by its Config2.sgo. The packs ship objects whose names collide
with EDF6's own (GiantAnt04.sgo, GiantBeeQueen02.sgo, DragonSmall401_HS.sgo ... differ from EDF6's files of the same
name) and with each other (AntHill301Large.sgo, GiantAnt04SB.sgo, ari_half.rab differ between the packs), so each
pack's files get a folder of their own in Mods:

    mp01:/Object/GiantAnt04.sgo  ->  OBJECT/EDF41MP01/GIANTANT04.SGO   (a Mods path, as make_edf41_objects.rel_of)
                                 ->  app:/Object/EDF41MP01/GiantAnt04.sgo   (what scripts and SGOs then name)

This module is the one place that says so: the object converter (tools/make_edf41_objects.py) and the mission
converter (tools/make_edf41_missions.py) both map through it.
"""
from __future__ import annotations

import glob
import os
import re

import cpk
import crilayla

# mount -> (Steam app id, the folder name its files get in Mods)
PACKS: dict[str, tuple[int, str]] = {'mp01': (410780, 'EDF41MP01'), 'mp02': (411380, 'EDF41MP02')}
MOUNT = re.compile(r'(?i)^(mp0\d):/(.*)$')


def is_mounted(path: str) -> bool:
    """A path a DLC names through its own mount (mp01:/..., mp02:/...)."""
    m = MOUNT.match(path.strip())
    return bool(m) and m.group(1).lower() in PACKS


def _split(path: str) -> tuple[str, str, str]:
    """(mount, the folder under it, the rest) of a mounted path: mp01:/Object/a/b.sgo -> ('mp01', 'Object', 'a/b.sgo')."""
    m = MOUNT.match(path.strip())
    if not m or m.group(1).lower() not in PACKS:
        raise ValueError(f'not a DLC path: {path!r}')
    rest = m.group(2).replace('\\', '/').strip('/')
    folder, _, tail = rest.partition('/')
    return m.group(1).lower(), folder, tail


def app_path(path: str) -> str:
    """mp01:/Object/x.sgo -> app:/Object/EDF41MP01/x.sgo (the case of the rest kept, as scripts compare it as written)."""
    mount, folder, tail = _split(path)
    sub = PACKS[mount][1]
    return f'app:/{folder}/{sub}/{tail}' if tail else f'app:/{folder}/{sub}'


def mods_rel(path: str) -> str:
    """mp01:/Object/x.sgo -> OBJECT/EDF41MP01/X.SGO (upper case, as make_edf41_objects.rel_of gives Mods paths)."""
    return app_path(path).split(':/', 1)[1].upper()


def rewrite(text: str) -> str:
    """Every mounted path in a string (a ';'-separated list too) as the app:/ path it is installed at."""
    return ';'.join(app_path(p) if is_mounted(p) else p for p in text.split(';'))


class Dlc:
    """One installed DLC pack's archive: its files by their mounted path."""

    def __init__(self, mount: str, path: str) -> None:
        self.mount, self.path = mount, path
        self.app_id, self.folder = PACKS[mount]
        self.cpk = cpk.Cpk(path)
        prefix = f'{self.app_id}/'
        # 'OBJECT/X.SGO' (under the mount, upper case) -> the archive's key
        self.files: dict[str, tuple[str, str]] = {}
        for d, n in self.cpk.index:
            if d.upper() == str(self.app_id) or d.upper().startswith(prefix):
                inner = d[len(prefix):] if d.upper().startswith(prefix) else ''
                self.files[(f'{inner}/{n}' if inner else n).upper()] = (d, n)

    def read(self, inner: str) -> bytes:
        """The file at mp0N:/<inner> (case-insensitive), decompressed."""
        d, n = self.files[inner.replace('\\', '/').strip('/').upper()]
        e = self.cpk.index[(d, n)]
        with open(self.cpk.path, 'rb') as h:
            h.seek(self.cpk.base + int(e['FileOffset']))
            data = h.read(int(e['FileSize']))
        return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data

    def mods_rels(self) -> dict[str, str]:
        """{Mods path of every file the pack ships under OBJECT/: its inner path}."""
        return {mods_rel(f'{self.mount}:/{inner}'): inner for inner in self.files if inner.startswith('OBJECT/')}


def find(edf41: str) -> dict[str, Dlc]:
    """The DLC packs installed in the EDF4.1 at `edf41` ({mount: Dlc}; none installed: {}), told apart by the app id
    their Config2.sgo-carrying folder names (the <hash> folder names are not meaningful)."""
    out: dict[str, Dlc] = {}
    for path in sorted(glob.glob(os.path.join(edf41, 'DLC', '*', 'dlc.cpk'))):
        ids = {d.split('/', 1)[0] for d, _ in cpk.Cpk(path).index}
        for mount, (app_id, _) in PACKS.items():
            if str(app_id) in ids and mount not in out:
                out[mount] = Dlc(mount, path)
    return out
