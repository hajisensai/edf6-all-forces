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
import time

PROCESS = 'EDF6.exe'
STEAM_APP = 2291060


def game_running(process: str = PROCESS) -> bool:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {process}', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return process.lower() in r.stdout.lower()


def steam_running_app() -> int:
    """Steam's own idea of the running game (RunningAppID under HKCU Software/Valve/Steam, 0 when none or no Steam).
    It lags EDF6.exe's exit by a few seconds (cloud sync and the like): a steam://rungameid inside that window only
    gets Steam's 「游戏已在运行」 dialog, so every launcher waits for it (wait_steam_released) after the process is gone."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            return int(winreg.QueryValueEx(k, 'RunningAppID')[0])
    except (ImportError, OSError, ValueError):
        return 0


def wait_steam_released(timeout: float = 60.0, app: int = STEAM_APP) -> bool:
    """Wait until Steam no longer counts APP as running; False when it still does after TIMEOUT seconds."""
    end = time.monotonic() + timeout
    while steam_running_app() == app:
        if time.monotonic() >= end:
            return False
        time.sleep(0.5)
    return True


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
