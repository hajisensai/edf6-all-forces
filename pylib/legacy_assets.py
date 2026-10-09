"""Earlier games' asset files in EDF6's form: a model archive (RAB / MRAB), a model (MDB) or an animation (CAS) of
EDF5 or EDF4.1 -> the bytes EDF6 loads. Read only (the caller writes the result into Mods).

  convert(rel, data, map_part=False) -> bytes     rel: the file's path in its game ('OBJECT/V505_TANKEDF4.MRAB')

An archive keeps every member, folder and order (pylib/mdb.py insert_member: the order is the loader's contract); its
models go through pylib/mdb_legacy.py (version 0x14 -> 0x20, object names, binormal handedness, skin elements last),
its animations through pylib/cas_legacy.py, and every other member (the DDS textures: the same format in all three
games) stays as it is. A member that was CMPL-compressed is compressed again. A file of another kind, or one a
converter refuses, raises ValueError: nothing half-converted reaches the game.
"""
from __future__ import annotations

import os

import mdb
import mdb_legacy

ARCHIVES = ('.RAB', '.MRAB')


def _cas(data: bytes) -> bytes:
    import cas_legacy
    return cas_legacy.cas_from_legacy(data)


def _member(name: str, stored: bytes, map_part: bool) -> bytes:
    ext = os.path.splitext(name)[1].upper()
    if ext not in ('.MDB', '.CAS'):
        return stored
    raw = mdb.cmpl_decompress(stored)
    out = mdb_legacy.mdb_from_legacy(raw, map_part=map_part) if ext == '.MDB' else _cas(raw)
    return mdb.cmpl_compress(out) if stored[:4] == b'CMPL' else out


def convert_archive(data: bytes, map_part: bool = False) -> bytes:
    """A RAB / MRAB with its models and animations converted, everything else as it was."""
    rab = mdb.rab_read(data)
    for f in rab.files:
        f.stored = _member(f.name, f.stored, map_part)
    if not mdb.folder_order_ok(rab):
        raise ValueError('archive members out of folder order')
    return mdb.rab_write(rab)


def convert(rel: str, data: bytes, map_part: bool = False) -> bytes:
    """`rel`'s bytes (an EDF5 / EDF4.1 file) as EDF6 loads them. ValueError: not a kind converted here, or refused."""
    ext = os.path.splitext(rel)[1].upper()
    if ext in ARCHIVES:
        return convert_archive(data, map_part)
    if ext == '.MDB':
        return mdb_legacy.mdb_from_legacy(data, map_part=map_part)
    if ext == '.CAS':
        return _cas(data)
    raise ValueError(f'{rel}: no converter for {ext} files')
