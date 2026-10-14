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
    """One writer of Mods (and its ledger, pylib/ledger.py) at a time per game: the generator and every installer flow
    that writes (wait_generate_lock). A byte lock on GENERATE_LOCK (released by Windows when the process dies, so a killed
    generator never leaves it held). Yields False when another one holds it."""
    import msvcrt
    path = os.path.join(game, 'Mods', 'Plugins', GENERATE_LOCK)
    os.makedirs(os.path.dirname(path), exist_ok=True)
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


LOCK_WAIT_S = 120.0   # how long an installer flow waits for a running generator before it refuses


@contextlib.contextmanager
def wait_generate_lock(game: str, timeout: float | None = None, say=print, poll: float = 0.5) -> Iterator[bool]:
    """generate_lock for an installer flow that writes Mods: a background generator (the game may have started one and
    closed since; it outlives the game) is waited for up to `timeout` seconds, said once; then False (the flow refuses,
    nothing written). The generator itself never waits (exit 3: busy)."""
    import time
    timeout = LOCK_WAIT_S if timeout is None else timeout
    deadline = time.monotonic() + timeout
    told = False
    while True:
        with generate_lock(game) as held:
            if held:
                yield True
                return
        if time.monotonic() >= deadline:
            say(f'后台生成器（{GENERATOR}）仍在运行，已等待 {timeout:.0f} 秒：本次没有写入任何文件，请稍后再试。')
            yield False
            return
        if not told:
            say(f'后台生成器（{GENERATOR}，游戏内战前配置菜单启动的）正在运行：等它结束（最多 {timeout:.0f} 秒）……')
            told = True
        time.sleep(poll)


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
    return support_loadout.install_new(game, files) + generate_menu_preview(game, errors), errors


def generate_menu_preview(game: str, errors: dict[str, str]) -> list[str]:
    """The preview's menu SGOs (tools/menu_preview.py) when one is missing, as new names only (install_new: the game may
    be running). A failure goes into `errors` under the file's name. The paths written."""
    import menu_preview
    import rootcpk
    if not menu_preview.missing(game):
        return []
    try:
        files = menu_preview.build(rootcpk.Game(game))
    except Exception as e:   # reported in the status file like a variant that could not be made
        errors[menu_preview.RANGER] = f'{type(e).__name__}: {e}'
        return []
    return menu_preview.install_new(game, files)


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
    try:   # the menu's screen, when an install before it did not write it: new files only, as the variants
        import room_layout
        for path in room_layout.install_missing(game):
            print('写入', path)
    except Exception as e:   # said in the status, never swallowed
        errors['LYT_HUIONLINEROOM.SGO / LYT_HUIHQMAIN.SGO'] = f'{type(e).__name__}: {e}'
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
