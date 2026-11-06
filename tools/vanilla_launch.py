"""「原版启动」：game/原版启动.cmd (written by tools/installer.py install) runs the slim generator in Mods/Plugins as
`vanilla-launch <game>`, which starts the game once with EDFModLoader loading nothing and puts everything back after.

EDFModLoader (the game folder's winmm.dll) reads the game folder's ModLoader.ini when the game starts: with
LoadPlugins / LoadASI / Redirect all False no plugin loads and Root.cpk is read as shipped (no Mods/ redirect), i.e. the
stock game. So: back the ini up (STATE, in Mods/Plugins), write those three keys False, start the game through Steam,
wait for EDF6.exe to appear and to exit, then write the ini back. The ini is written back only while it is still the
one written here (anything that changed it since keeps its change); a run that never got to put it back (the window
closed, the machine off) is put back by the next launch and by the installer's install / uninstall (restore).

Stdlib and pylib/modfiles only: frozen into the slim generator (tests/slim_generator_test.py).
"""
from __future__ import annotations

import base64
import ctypes
import os
import re
import subprocess
import sys
import threading
import time

import modfiles

LAUNCHER = '原版启动.cmd'                                # in the game folder, beside EDF6.exe
STATE = 'EDF6VehicleCrew.vanilla_launch.json'          # in Mods/Plugins: the ini as it was, while a vanilla run is on
INI = 'ModLoader.ini'
SECTION = 'ModLoader'
OFF = ('LoadPlugins', 'LoadASI', 'Redirect')
STEAM_URL = 'steam://rungameid/2291060'
START_TIMEOUT = 300.0    # seconds for EDF6.exe to appear (Steam may have to start, or update the game, first)
POLL = 2.0
_lock = threading.Lock()   # restore runs on the main thread and in the console close handler


def launcher_text(generator: str) -> bytes:
    """The .cmd beside EDF6.exe: ASCII only (cmd reads it in the OEM code page); keeps its window open on a failure."""
    return ('@echo off\r\n'
            f'"%~dp0Mods\\Plugins\\{generator}" vanilla-launch "%~dp0."\r\n'
            'if errorlevel 1 pause\r\n').encode('ascii')


def _paths(game: str) -> tuple[str, str]:
    return os.path.join(game, INI), os.path.join(game, 'Mods', 'Plugins', STATE)


def vanilla_ini(text: str) -> str:
    """`text` (the ini, '' when there is none) with OFF set False in [ModLoader]; the other lines, their order and line
    endings kept. Keys the section lacks are added at its end; a missing section is added."""
    nl = '\r\n' if '\r\n' in text or not text else '\n'
    lines = text.splitlines(keepends=True)
    section = None
    start = end = None   # [ModLoader]'s header line and the index after its last line
    seen: set[str] = set()
    for i, line in enumerate(lines):
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            section = m.group(1).strip()
            if section.lower() == SECTION.lower() and start is None:
                start = i
            continue
        if start is not None and section is not None and section.lower() == SECTION.lower():
            end = i + 1
            k = re.match(r'\s*([A-Za-z0-9_]+)\s*=', line)
            if k and k.group(1) in OFF:
                seen.add(k.group(1))
                eol = line[len(line.rstrip('\r\n')):] or nl
                lines[i] = f'{k.group(1)}=False{eol}'
    missing = [f'{k}=False{nl}' for k in OFF if k not in seen]
    if start is None:
        if lines and not lines[-1].endswith(('\n', '\r')):
            lines[-1] += nl
        return ''.join(lines) + f'[{SECTION}]{nl}' + ''.join(missing)
    at = end if end is not None else start + 1
    if missing and not lines[at - 1].endswith(('\n', '\r')):
        lines[at - 1] += nl
    lines[at:at] = missing
    return ''.join(lines)


def prepare(game: str) -> None:
    """Backs the ini up into STATE and writes the vanilla one. STATE first: a crash between the two leaves an ini the
    restore finds unchanged (no harm), never a vanilla ini with nothing to put back."""
    ini, state = _paths(game)
    before = modfiles.read(ini)
    after = vanilla_ini((before or b'').decode('utf-8-sig', errors='replace')).encode('utf-8')
    modfiles.save_json(state, {'had': before is not None,
                               'original': base64.b64encode(before or b'').decode('ascii'),
                               'vanilla': modfiles.sha256(after)})
    modfiles.atomic_write(ini, after)


def restore(game: str) -> str | None:
    """Puts the ini back when a vanilla run left STATE (none: None, nothing done). Returns what it did, for the caller to
    print. An ini changed since the vanilla one was written (the player, another tool) is kept as it is."""
    with _lock:
        return _restore(game)


def _restore(game: str) -> str | None:
    ini, state = _paths(game)
    if not os.path.isfile(state):
        return None
    saved = modfiles.load_json(state, {})
    if modfiles.sha256_file(ini) == saved.get('vanilla'):
        if saved.get('had'):
            modfiles.atomic_write(ini, base64.b64decode(saved.get('original', '')))
        else:
            os.remove(ini)
        said = f'已恢复 {ini}（MOD 照常加载）'
    else:
        said = f'{ini} 在原版启动后被改过，保留现在的内容'
    os.remove(state)
    return said


def game_pid() -> int | None:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {modfiles.PROCESS}', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True, errors='replace')
    for line in r.stdout.splitlines():
        cells = [c.strip('"') for c in line.split('","')]
        if len(cells) > 1 and cells[0].lower() == modfiles.PROCESS.lower() and cells[1].isdigit():
            return int(cells[1])
    return None


_handler = None   # the console close handler, kept alive while it is installed


def _on_close(game: str) -> None:
    """Closing the window while the game runs puts the ini back too (the loader read it when the game started)."""
    global _handler
    if sys.platform != 'win32':
        return

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_uint)
    def handler(_event: int) -> int:
        restore(game)
        return 0
    _handler = handler
    ctypes.windll.kernel32.SetConsoleCtrlHandler(_handler, 1)


def launch(game: str) -> int:
    game = os.path.normpath(game)
    if said := restore(game):   # a run that did not get to put it back
        print(said)
    if modfiles.game_running():
        print(f'{modfiles.PROCESS} 已经在运行，先退出游戏再原版启动。')
        return 1
    print('原版启动：这一次不加载任何 MOD（插件、Mods 文件夹都不读），游戏退出后自动恢复。')
    print('注意：存档里正装备着 MOD 新增的武器时，原版会在主菜单闪退；先在 MOD 下换回原版武器再原版启动。')
    print('游戏运行期间请不要关闭这个窗口。')
    prepare(game)
    _on_close(game)
    try:
        os.startfile(STEAM_URL)
        waited = 0.0
        while (pid := game_pid()) is None:
            if waited >= START_TIMEOUT:
                print(f'{int(START_TIMEOUT)} 秒内没有看到 {modfiles.PROCESS} 启动（Steam 没打开或启动被取消）。')
                return 1
            time.sleep(POLL)
            waited += POLL
        print(f'游戏已启动（PID {pid}），等它退出……')
        while modfiles.game_running():
            time.sleep(POLL)
        print('游戏已退出。')
        return 0
    finally:
        print(restore(game) or '')


if __name__ == '__main__':
    sys.exit(launch(sys.argv[1] if len(sys.argv) > 1 else '.'))
