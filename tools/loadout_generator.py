"""The in-game loadout menu's background generator: `loadout-generate <game>` (src/loadout_store.cpp runs it hidden, as
Mods/Plugins/EDF6VehicleCrew.generator.exe, after a save and when the room pre-sync finds files missing: src/room_sync.cpp).
It makes only the variant files the installed ini and the pending list name that are not there yet, as new
content-addressed names (tools/support_loadout.py), the game running or not.

Frozen on its own by tools/build_release.py (GENERATOR_EXE, a few MB: this module, support_loadout and the SGO / CPK
readers it needs, nothing of the installer's network, menus or model builders); tools/installer.py imports the same
functions (its `loadout-generate` argument and install_generator's fallback when no slim exe was built).
"""
from __future__ import annotations

import contextlib
import os
import sys
import traceback
from typing import Iterator

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, '..', 'pylib')):
    if os.path.normpath(_p) not in sys.path:
        sys.path.insert(0, os.path.normpath(_p))

import gamedir  # noqa: E402
import modfiles  # noqa: E402

PLUGIN = 'EDF6VehicleCrew'                           # tools/installer.py PLUGIN
GENERATOR = PLUGIN + '.generator.exe'                # src/loadout_store.cpp kGenerator
GENERATE_STATUS = PLUGIN + '.generate_status.txt'    # src/loadout_store.cpp kStatus
GENERATE_LOCK = PLUGIN + '.generate.lock'


def write_generate_status(game: str, state: str, made: int = 0, failed: int = 0, message: str = '') -> None:
    """GENERATE_STATUS, which the plugin shows (src/loadout_file.cpp ParseGenerateStatus): UTF-8 key=value lines, replaced
    whole (the plugin opens it sharing delete, so the rename never fails under it)."""
    message = ' '.join(message.split())[:180]
    text = f'state={state}\r\nmade={made}\r\nfailed={failed}\r\nmessage={message}\r\n'
    modfiles.atomic_write(os.path.join(game, 'Mods', 'Plugins', GENERATE_STATUS), text.encode('utf-8'))


def generate_line(state: str, made: int = 0, failed: int = 0) -> str:
    """The one machine-readable line `loadout-generate` prints last."""
    return f'EDF6VC_GENERATE state={state} made={made} failed={failed}'


@contextlib.contextmanager
def generate_lock(game: str) -> Iterator[bool]:
    """One generator at a time per game: a byte lock on GENERATE_LOCK (released by Windows when the process dies, so a
    killed generator never leaves it held). Yields False when another one holds it."""
    import msvcrt
    path = os.path.join(game, 'Mods', 'Plugins', GENERATE_LOCK)
    with open(path, 'a+b') as f:
        f.seek(0)
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            yield False
            return
        try:
            yield True
        finally:
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)


def generate_variants(game: str) -> tuple[list[str], dict[str, str]]:
    """The variant files the installed ini and the pending list name that are not there yet, made from Root.cpk (read
    only) and written as new names only (support_loadout.install_new). The pending list is left alone: the plugin appends
    to it while the game runs; the installer's own flows (menu 7 / 8, install) clear it. (paths written, name -> why not)."""
    import support_loadout
    with open(os.path.join(game, 'Mods', 'Plugins', PLUGIN + '.ini'), 'rb') as f:
        raw = f.read()
    text = (raw[3:] if raw.startswith(b'\xef\xbb\xbf') else raw).decode('utf-8', errors='replace')
    names = support_loadout.missing_names(game, text, support_loadout.read_pending(game))
    errors: dict[str, str] = {}
    files = support_loadout.build_named(game, names, errors=errors)
    return support_loadout.install_new(game, files), errors


def generate_summary(written: list[str], errors: dict[str, str]) -> tuple[str, str]:
    """(state, message) for the status file."""
    if errors:
        name, why = next(iter(errors.items()))
        return 'failed', f'{len(errors)} 个没有生成（{name}：{why}）；已生成 {len(written)} 个'
    return 'ok', f'已生成 {len(written)} 个文件' if written else '没有需要生成的文件'


def loadout_generate(game: str) -> int:
    """`loadout-generate <game>`: never asks, never refuses for a running game (it only adds new content-addressed files).
    Exit 0 ok, 1 some file not made (or an error), 2 not a game with the plugin installed, 3 another generator running;
    the last line printed is generate_line, the status file says the same with a message."""
    if not gamedir.is_game(game) or not os.path.isfile(os.path.join(game, 'Mods', 'Plugins', PLUGIN + '.ini')):
        print(generate_line('failed'), 'message=not a game directory with EDF6VehicleCrew installed')
        return 2
    with generate_lock(game) as held:
        if not held:
            print(generate_line('busy'))
            return 3
        return _generate_held(game)


def _generate_held(game: str) -> int:
    write_generate_status(game, 'running', message='正在生成支援预设需要的文件')
    try:
        written, errors = generate_variants(game)
    except Exception as e:   # said in the status file the plugin shows, and the line, never swallowed
        traceback.print_exc()
        write_generate_status(game, 'failed', message=f'{type(e).__name__}: {e}')
        print(generate_line('failed'))
        return 1
    for path in written:
        print('写入', path)
    state, message = generate_summary(written, errors)
    write_generate_status(game, state, len(written), len(errors), message)
    print(generate_line(state, len(written), len(errors)))
    return 0 if state == 'ok' else 1


def generate_main(args: list[str]) -> int:
    if len(args) != 1:
        print(generate_line('failed'), 'message=usage: loadout-generate <game directory>')
        return 2
    return loadout_generate(os.path.normpath(args[0].strip('"')))


def run(argv: list[str]) -> int:
    """The slim exe's entry (also what installer.run does for this argument): `loadout-generate <game>`; never asks,
    never waits for Enter."""
    if argv[:1] != ['loadout-generate']:
        print(generate_line('failed'), 'message=usage: loadout-generate <game directory>')
        return 2
    try:
        return generate_main(argv[1:])
    except Exception:
        traceback.print_exc()
        print(generate_line('failed'))
        return 1


if __name__ == '__main__':
    sys.exit(run(sys.argv[1:]))
