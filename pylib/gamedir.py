"""Finds the EARTH DEFENSE FORCE 6 install directory on this machine.

Order: $EDF6_DIR; the directory of the running program (or script) and its parents, so an installer
unzipped into the game folder finds it; every Steam library listed by the Steam install the registry
names (steamapps/libraryfolders.vdf); the developer's own path. A directory counts only when it holds
both EDF6.exe and Root.cpk.
"""
from __future__ import annotations

import os
import re
import sys

APP_DIR = os.path.join('steamapps', 'common', 'EARTH DEFENSE FORCE 6')
DEV_DIR = r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'


def is_game(path: str | None) -> bool:
    return bool(path) and all(os.path.isfile(os.path.join(path, n)) for n in ('EDF6.exe', 'Root.cpk'))


def _self_dirs() -> list[str]:
    start = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else sys.argv[0] or '.'))
    out: list[str] = []
    for d in (start, os.getcwd()):
        while d and d not in out:
            out.append(d)
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent
    return out


def _steam_roots() -> list[str]:
    roots: list[str] = []
    try:
        import winreg
    except ImportError:
        return roots
    keys = ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
            (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath'),
            (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Valve\Steam', 'InstallPath'))
    for hive, sub, name in keys:
        try:
            with winreg.OpenKey(hive, sub) as k:
                roots.append(os.path.normpath(winreg.QueryValueEx(k, name)[0]))
        except OSError:
            pass
    return roots


def _libraries(steam: str) -> list[str]:
    libs = [steam]
    vdf = os.path.join(steam, 'steamapps', 'libraryfolders.vdf')
    try:
        with open(vdf, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return libs
    for m in re.finditer(r'"path"\s+"([^"]+)"', text):
        libs.append(os.path.normpath(m.group(1).replace('\\\\', '\\')))
    return libs


def candidates() -> list[str]:
    out: list[str] = []
    env = os.environ.get('EDF6_DIR')
    if env:
        out.append(env)
    out += _self_dirs()
    for steam in _steam_roots():
        out += [os.path.join(lib, APP_DIR) for lib in _libraries(steam)]
    out.append(DEV_DIR)
    return out


def find() -> str | None:
    for path in candidates():
        if is_game(path):
            return os.path.normpath(path)
    return None


def find_or_dev() -> str:
    """The game dir, or the developer's path when none is found (the tools' old default)."""
    return find() or DEV_DIR


if __name__ == '__main__':
    print(find())
