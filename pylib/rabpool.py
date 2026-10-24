"""Model archives (.RAB / .MRAB) shipped as a manifest plus de-duplicated members, rebuilt byte for byte at install.

The 20 Hector variants each carry the same body textures: 330 MB of archives, 26 MB of distinct members (2026-10-13).
Committed and bundled whole, every variant would ship its own copy. Stored here instead:

  <rel>.members.json    {'version', 'folders', 'files': [[name, folder, flag, unk0, unk1, sha256], ...]} (archive order)
                        a row of a member the player's EDF6 already ships has a 7th column, its source [archive, RAB
                        path, member index, member name] there (as tools/edf5_dlc.py stores one): not shipped, read
                        from the player's game at install and checked against the row's SHA-256
  _members/<sha256>     a member's stored bytes (raw or CMPL, as in the archive), once however many archives hold it

    pack(files, sources) -> {stored path: bytes}   every archive in `files` as above, every other file as it is;
                                                   sources {archive rel: {sha256: source}}: the members not shipped
    unpack(rel, read, source) -> bytes       the archive `rel`, rebuilt from what read(stored path) returns and, for a
                                             sourced member, source(row's source) (Unsourced when there is none)
    stored(rel) -> [stored path]             what `rel` is shipped as (an archive: its manifest; members by unpack)
    sources(rel, read) -> [source]           the sources of `rel`'s members that are not shipped (an archive's)

pack refuses an archive mdb.rab_write would not lay out again byte for byte (ValueError): rebuilt, it would not be
the file the registry's SHA-256 names.
"""
from __future__ import annotations

import hashlib
import json
from collections.abc import Callable, Mapping

import mdb

ARCHIVES = ('.RAB', '.MRAB')
MANIFEST = '.members.json'
MEMBERS = '_members'


class Unsourced(LookupError):
    """An archive with a member read from the player's EDF6, unpacked with no way to read it there."""


def is_archive(rel: str) -> bool:
    return rel.upper().endswith(ARCHIVES)


def stored(rel: str) -> list[str]:
    return [rel + MANIFEST] if is_archive(rel) else [rel]


def pack(files: dict[str, bytes], sources: Mapping[str, Mapping[str, list]] | None = None) -> dict[str, bytes]:
    out: dict[str, bytes] = {}
    for rel, data in files.items():
        if not is_archive(rel):
            out[rel] = data
            continue
        rab = mdb.rab_read(data)
        if mdb.rab_write(rab) != data:
            raise ValueError(f'{rel}: mdb.rab_write does not rebuild it byte for byte')
        mine = (sources or {}).get(rel, {})
        rows = []
        for f in rab.files:
            sha = hashlib.sha256(f.stored).hexdigest()
            row = [f.name, f.folder, f.flag, f.unk[0], f.unk[1], sha]
            if sha in mine:
                row.append(list(mine[sha]))
            else:
                out[f'{MEMBERS}/{sha}'] = f.stored
            rows.append(row)
        unused = set(mine) - {row[5] for row in rows}
        if unused:
            raise ValueError(f'{rel}: sources for members it does not have: {sorted(unused)}')
        manifest = {'version': rab.version, 'folders': rab.folders, 'files': rows}
        out[rel + MANIFEST] = (json.dumps(manifest, ensure_ascii=False, indent=0) + '\n').encode('utf-8')
    return out


def sources(rel: str, read: Callable[[str], bytes]) -> list[list]:
    if not is_archive(rel):
        return []
    return [row[6] for row in json.loads(read(rel + MANIFEST).decode('utf-8'))['files'] if len(row) > 6]


def unpack(rel: str, read: Callable[[str], bytes], source: Callable[[list], bytes] | None = None) -> bytes:
    if not is_archive(rel):
        return read(rel)
    m = json.loads(read(rel + MANIFEST).decode('utf-8'))
    files = []
    for row in m['files']:
        name, folder, flag, u0, u1, sha = row[:6]
        if len(row) > 6:
            if source is None:
                raise Unsourced(f"{rel}: {name} is read from the player's EDF6 ({row[6][1]}), and no game was given")
            data = source(row[6])
            if hashlib.sha256(data).hexdigest() != sha:
                raise ValueError(f'{rel}: {name} from EDF6 {row[6][1]} is not the member it was generated from')
        else:
            data = read(f'{MEMBERS}/{sha}')
        files.append(mdb.RabFile(name, folder, flag, data, (u0, u1)))
    return mdb.rab_write(mdb.Rab(m['version'], m['folders'], files))
