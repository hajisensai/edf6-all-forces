"""Checks every SHKT the mods install and every body name the mods' SGOs bind to a SHKT (pylib/shkt_check.py).
Reads the game directory only; writes nothing.

python tools/check_shkt.py [--game DIR]            Mods/OBJECT and Mods/WEAPON: each tagfile SHKT on its own, then
                                                   each SGO's ragdoll mappings / accessory parts against the SHKT the
                                                   game would load (Mods first, else Root.cpk / Chunk01 / Chunk02)
python tools/check_shkt.py --stock [--game DIR]    the same over the game's own files (calibration: must be clean)

Each vehicle SGO's car_base_* settings are checked against its SHKT and model too (pylib/vehicle_check.py: names,
cast modes, mass, each wheel's suspension compression at rest and its size).

Exit 1 on any error. A name an SGO binds that its SHKT lacks crashes the game before the mission starts.
Legacy Havok 2014 tagfiles (the EDF4.1 port's ragdolls) are read with pylib/hk2014.py: their body names are checked
like the others', of their own rules only the ones that format has (pylib/shkt_check.py).
"""
from __future__ import annotations

import argparse
import os
import sys
from typing import Iterator

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import gamedir  # noqa: E402
import rootcpk  # noqa: E402
import shkt_check as sc  # noqa: E402
import vehicle_check  # noqa: E402

FOLDERS = ('OBJECT', 'WEAPON')
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk')


class Files:
    """The game's files as it loads them: Mods/<folder> first (unless stock_only), else the archives."""

    def __init__(self, game: str, stock_only: bool) -> None:
        self.mods = {} if stock_only else {
            f'{folder}/{n.upper()}': os.path.join(game, 'Mods', folder, n)
            for folder in FOLDERS if os.path.isdir(os.path.join(game, 'Mods', folder))
            for n in os.listdir(os.path.join(game, 'Mods', folder))}
        self.archives = [rootcpk.Game(game, a) for a in ARCHIVES if os.path.isfile(os.path.join(game, a))]
        self.stock = {f'{folder}/{n.upper()}': g for g in reversed(self.archives) for folder in FOLDERS
                      for n in g.names(folder)}

    def read(self, ref: str) -> bytes | None:
        if ref in self.mods:
            with open(self.mods[ref], 'rb') as h:
                return h.read()
        g = self.stock.get(ref)
        return g.read(*ref.split('/', 1)) if g is not None else None

    def own(self, stock_only: bool, ext: str) -> list[str]:
        """The refs to check: every Mods file with `ext`, or (stock_only) every archive file with it."""
        pool = self.stock if stock_only else self.mods
        return sorted(r for r in pool if r.endswith(ext))


def check(files: Files, stock_only: bool) -> Iterator[tuple[str, str]]:
    """(file, line) for everything found; the line starts with 'error', 'warning', 'info' or 'skipped'."""
    names: dict[str, set[str] | None] = {}

    def body_names(ref: str) -> set[str] | None:
        if ref not in names:
            data = files.read(ref)
            names[ref] = None if data is None else sc.body_names(data)
        return names[ref]

    for ref in files.own(stock_only, '.SHKT'):
        data = files.read(ref) or b''
        for p in sc.problems(data):
            yield ref, str(p)
    for ref in files.own(stock_only, '.SGO'):
        data = files.read(ref) or b''
        if data[:4] not in sc.SGO_MAGICS:
            continue      # .SGO names that hold other formats (CAS animation sets and the like)
        doc = sc.load_plain(data)
        if doc is None:
            yield ref, 'skipped: an SGO that does not parse'
            continue
        for shkt, where, body in sc.bound_names(doc):
            have = body_names(shkt)
            if have is None and files.read(shkt) is None:
                yield ref, f'error: {where}: binds to {shkt}, which the game does not have'
            elif have is not None and body not in have:
                yield ref, f'error: {where}: body {body!r} is not in {shkt}'
        shkt = vehicle_check.shkt_of(doc) if doc.get('car_base_wheel') else None
        data = files.read(shkt) if shkt else None
        if data is not None and (data[4:8] == b'TAG0' or data[:8] == sc.PACKFILE_MAGIC):
            for p in vehicle_check.vehicle_problems(doc, data, vehicle_check.model_bones(doc, lambda r: files.read(r)),
                                                    ref.split('/', 1)[1]):
                yield ref, str(p)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', default=None, help='the game directory (default: found)')
    ap.add_argument('--stock', action='store_true', help="check the game's own files instead of the mods'")
    a = ap.parse_args(argv)
    game = a.game or gamedir.find_or_dev()
    if not gamedir.is_game(game):
        print(f'not an EDF6 directory: {game}')
        return 2
    files = Files(game, a.stock)
    counts = {'error': 0, 'warning': 0, 'info': 0, 'skipped': 0}
    for ref, line in check(files, a.stock):
        counts[line.split(':', 1)[0]] = counts.get(line.split(':', 1)[0], 0) + 1
        print(f'{ref}: {line}')
    print(f"{counts['error']} error(s), {counts['warning']} warning(s), {counts['info']} info, {counts['skipped']} skipped "
          f"({'stock files' if a.stock else 'Mods'} of {game})")
    return 1 if counts['error'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
