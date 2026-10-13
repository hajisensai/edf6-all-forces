"""Model archives (.RAB / .MRAB) shipped as a manifest plus de-duplicated members, rebuilt byte for byte at install.

The 20 Hector variants each carry the same body textures: 330 MB of archives, 26 MB of distinct members (2026-10-13).
Committed and bundled whole, every variant would ship its own copy. Stored here instead:

  <rel>.members.json    {'version', 'folders', 'files': [[name, folder, flag, unk0, unk1, sha256], ...]} (archive order)
  _members/<sha256>     a member's stored bytes (raw or CMPL, as in the archive), once however many archives hold it

    pack(files) -> {stored path: bytes}      every archive in `files` as above, every other file as it is
    unpack(rel, read) -> bytes               the archive `rel`, rebuilt from what read(stored path) returns
    stored(rel) -> [stored path]             what `rel` is shipped as (an archive: its manifest; members by unpack)

pack refuses an archive mdb.rab_write would not lay out again byte for byte (ValueError): rebuilt, it would not be
the file the registry's SHA-256 names.
"""
from __future__ import annotations

import hashlib
import json
from typing import Callable

import mdb

ARCHIVES = ('.RAB', '.MRAB')
MANIFEST = '.members.json'
MEMBERS = '_members'


def is_archive(rel: str) -> bool:
    return rel.upper().endswith(ARCHIVES)


def stored(rel: str) -> list[str]:
    return [rel + MANIFEST] if is_archive(rel) else [rel]


def pack(files: dict[str, bytes]) -> dict[str, bytes]:
    out: dict[str, bytes] = {}
    for rel, data in files.items():
        if not is_archive(rel):
            out[rel] = data
            continue
        rab = mdb.rab_read(data)
        if mdb.rab_write(rab) != data:
            raise ValueError(f'{rel}: mdb.rab_write does not rebuild it byte for byte')
        rows = []
        for f in rab.files:
            sha = hashlib.sha256(f.stored).hexdigest()
            out[f'{MEMBERS}/{sha}'] = f.stored
            rows.append([f.name, f.folder, f.flag, f.unk[0], f.unk[1], sha])
        manifest = {'version': rab.version, 'folders': rab.folders, 'files': rows}
        out[rel + MANIFEST] = (json.dumps(manifest, ensure_ascii=False, indent=0) + '\n').encode('utf-8')
    return out


def unpack(rel: str, read: Callable[[str], bytes]) -> bytes:
    if not is_archive(rel):
        return read(rel)
    m = json.loads(read(rel + MANIFEST).decode('utf-8'))
    files = [mdb.RabFile(name, folder, flag, read(f'{MEMBERS}/{sha}'), (u0, u1))
             for name, folder, flag, u0, u1, sha in m['files']]
    return mdb.rab_write(mdb.Rab(m['version'], m['folders'], files))
