"""Writing into the game's Mods folder safely: shared by the repo's data installers.

Everything here is about files the installers own or share with other mods: refuse while the game runs
(it holds the tables open and would overwrite or crash on half-written files), write each file whole or
not at all, and fingerprint what was written so a later run can tell our file from another mod's.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess

PROCESS = 'EDF6.exe'


def game_running(process: str = PROCESS) -> bool:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {process}', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return process.lower() in r.stdout.lower()


def refuse_while_running(process: str = PROCESS) -> None:
    if game_running(process):
        raise SystemExit(f'{process} is running: close the game first')


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: str) -> str | None:
    """The file's SHA-256, or None when it does not exist."""
    if not os.path.isfile(path):
        return None
    with open(path, 'rb') as f:
        return sha256(f.read())


def read(path: str) -> bytes | None:
    if not os.path.isfile(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


def atomic_write(path: str, data: bytes) -> None:
    """Writes `data` to a temporary file next to `path`, then renames it over `path`: a crash or a full disk
    leaves either the old file or the new one, never a truncated one."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.tmp-edf6'
    with open(tmp, 'wb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def load_json(path: str, default: dict) -> dict:
    if not os.path.isfile(path):
        return default
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def save_json(path: str, value: dict) -> None:
    atomic_write(path, json.dumps(value, indent=1, ensure_ascii=False).encode('utf-8'))
