"""A persistent, content-addressed store of deterministic game-derived builds, shared by the test processes.

Every test process (ctest runs each test in a process of its own, tools/selftest.py each test in a fresh worker) built
the same tracked vehicles from Root.cpk again: the War Thunder models' collision fitting, textures and compression,
10-30 s a kind, rebuilt by weapon_mount_native, the eight wt_tracked shards, footprint, emplacement, the range's
tracked test and the spawn catalog's shards (2026-11-27: about a third of tools/check.py --full's CPU time).

Only when $EDF6_DERIVED_CACHE names a folder (CMakeLists.txt gives every test build/derived-cache; tools/selftest.py
the same folder): the installer and the release never set it and always build. A build dir made anew (CI's) starts
empty, so it builds everything for real once.

An entry's name is the sha256 of its key: what the build reads (the archive's identity, the model folders' content;
buildcache.inputs, the installer's own incremental-update key), the code it runs (buildcache.source_recipe: the
generator's local import closure), the native helper DLL, Python's and numpy's versions. Its file holds that key text
and the sha256 of its pickled value: an entry whose key or digest does not match (a torn or foreign file) is built
again and replaced. Entries are written to a temporary file and renamed into place; the first writer of an entry wins
(the same key is the same value), a stale file is never read.
"""
from __future__ import annotations

import hashlib
import json
import os
import pickle
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable, TypeVar

ENV = 'EDF6_DERIVED_CACHE'
VERSION = 1
MAGIC = b'EDF6DC1\n'
T = TypeVar('T')


def folder() -> Path | None:
    value = os.environ.get(ENV)
    return Path(value) if value else None


def environment() -> dict[str, Any]:
    """What every entry's value also depends on beyond its own key: the interpreter, numpy (the fitting's numerics)
    and the native helpers (pylib/edf6native.py) the build calls."""
    import edf6native
    import numpy
    from modfiles import sha256_file
    dll = edf6native.path()
    return {'python': sys.version, 'numpy': numpy.__version__,
            'native': sha256_file(dll) if os.path.isfile(dll) else None}


def _encode(key_text: bytes, payload: bytes) -> bytes:
    return MAGIC + hashlib.sha256(key_text).digest() + hashlib.sha256(payload).digest() + payload


def _decode(data: bytes, key_text: bytes) -> bytes | None:
    """The pickled value in `data`, None when `data` is not a whole entry of `key_text`."""
    head = len(MAGIC) + 64
    if len(data) < head or not data.startswith(MAGIC):
        return None
    if data[len(MAGIC):len(MAGIC) + 32] != hashlib.sha256(key_text).digest():
        return None
    payload = data[head:]
    if data[len(MAGIC) + 32:head] != hashlib.sha256(payload).digest():
        return None
    return payload


def entry_path(namespace: str, key: dict[str, Any]) -> tuple[Path, bytes]:
    root = folder()
    if root is None:
        raise RuntimeError(f'${ENV} is not set')
    key_text = json.dumps({'version': VERSION, 'namespace': namespace, 'key': key, 'environment': environment()},
                          sort_keys=True, ensure_ascii=True).encode('ascii')
    return root / namespace / (hashlib.sha256(key_text).hexdigest() + '.pickle'), key_text


def fetch(namespace: str, key: Callable[[], dict[str, Any]], build: Callable[[], T]) -> T:
    """`build()`'s value for `key()`: from the store when it holds a whole entry of it, else built and stored. Without
    $EDF6_DERIVED_CACHE, build() every time (and key() is never made: the frozen installer has no source to hash)."""
    if folder() is None:
        return build()
    path, key_text = entry_path(namespace, key())
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        data = None
    if data is not None:
        payload = _decode(data, key_text)
        if payload is not None:
            return pickle.loads(payload)
        path.unlink(missing_ok=True)   # torn or foreign: built again below
    value = build()
    _store(path, _encode(key_text, pickle.dumps(value, protocol=pickle.HIGHEST_PROTOCOL)))
    return value


def _store(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
        try:
            os.rename(tmp, path)   # never over an existing entry: another process stored the same key's value first
        except FileExistsError:
            os.unlink(tmp)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
