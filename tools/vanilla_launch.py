"""「原版启动」：game/原版启动.cmd (written by tools/installer.py install) runs the slim generator in Mods/Plugins as
`vanilla-launch <game>`, which starts the game once with EDFModLoader loading nothing and puts everything back after.

EDFModLoader (the game folder's winmm.dll) reads the game folder's ModLoader.ini when the game starts: with
LoadPlugins / LoadASI / Redirect all False no plugin loads and Root.cpk is read as shipped (no Mods/ redirect), i.e. the
stock game. So: back the ini up (STATE, in Mods/Plugins), write those three keys False, swap the saves (below: the
vanilla game gets saves of its own), start the game through Steam, wait for EDF6.exe to appear and to exit, then write
the ini back and swap the saves back. The ini is written back only while it is still the one written here (anything
that changed it since keeps its change); a run that never got to put things back (the window closed, the machine off)
is put back by the next launch and by the installer's install / uninstall (restore_all).

Stdlib and pylib/modfiles only: frozen into the slim generator (tests/slim_generator_test.py).
"""
from __future__ import annotations

import base64
import ctypes
import os
import re
import shutil
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


# ---------------------------------------------------------------- the saves
# Not shared: a mod save with a weapon the stock table lacks equipped crashes the stock game at the main menu. The game
# keeps each slot in %LOCALAPPDATA%\EarthDefenceForce6\SAVE_DATA\<SteamID>\saveslotNN\, and Steam Cloud syncs every file
# in those folders (any name: the plugin's MAIN.GST.edf6vc is synced too; the game's BACKUP folder is not). So both sets
# live in the slot folders, both synced: the slot's own files are the set its MODE file names (no file: the mod's), the
# other set beside them as <name>.edf6mod / <name>.edf6vanilla. A switch copies (never renames or moves): the playing
# set into its store first, then the other store over the slot's files; MODE says which step is done, so a switch cut
# off anywhere resumes without mixing the sets. Before each vanilla run the whole SAVE_DATA is also copied, locally,
# into SAVE_DATA.mod-backups\<time> (the last KEEP_BACKUPS kept).
SAVES = 'SAVE_DATA'
BACKUPS = SAVES + '.mod-backups'         # beside SAVE_DATA, not synced: a copy of everything before each vanilla run
SUFFIX = {'mod': '.edf6mod', 'vanilla': '.edf6vanilla'}
MODE = 'EDF6AF.mode'                     # in each slot folder: mod | vanilla | mod-saved | vanilla-saved
ON = SAVES + '.vanilla-on'               # beside SAVE_DATA, local: a vanilla run is on (slots it made are vanilla's)
KEEP_BACKUPS = 10


def save_root() -> str:
    return os.path.join(os.environ.get('LOCALAPPDATA') or os.path.expanduser('~'), 'EarthDefenceForce6')


def _p(*names: str) -> str:
    return os.path.join(save_root(), *names)


def slots() -> list[str]:
    """Every saveslotNN folder of every account in SAVE_DATA."""
    out: list[str] = []
    root = _p(SAVES)
    for user in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        d = os.path.join(root, user)
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if name.lower().startswith('saveslot') and os.path.isdir(os.path.join(d, name)):
                out.append(os.path.join(d, name))
    return out


def _mode(slot: str) -> str:
    try:
        with open(os.path.join(slot, MODE), encoding='ascii') as f:
            return f.read().strip()
    except (OSError, UnicodeDecodeError):
        return 'vanilla' if os.path.exists(_p(ON)) else 'mod'   # a slot the vanilla run made has no MODE yet


def _set_mode(slot: str, mode: str) -> None:
    modfiles.atomic_write(os.path.join(slot, MODE), mode.encode('ascii'))


def _own(slot: str) -> list[str]:
    """The slot's own files: the playing set (not a store, not MODE, not a half-written temporary)."""
    return sorted(n for n in os.listdir(slot) if os.path.isfile(os.path.join(slot, n)) and n != MODE
                  and not n.endswith((*SUFFIX.values(), '.tmp-edf6')))


def _stored(slot: str, mode: str) -> list[str]:
    s = SUFFIX[mode]
    return sorted(n[:-len(s)] for n in os.listdir(slot) if n.endswith(s) and os.path.isfile(os.path.join(slot, n)))


def _copy(src: str, dst: str) -> None:
    with open(src, 'rb') as f:
        modfiles.atomic_write(dst, f.read())


def _store(slot: str, mode: str) -> None:
    """The slot's own files copied into `mode`'s store (whose files the set no longer has removed)."""
    own = _own(slot)
    for n in own:
        _copy(os.path.join(slot, n), os.path.join(slot, n + SUFFIX[mode]))
    for n in set(_stored(slot, mode)) - set(own):
        os.remove(os.path.join(slot, n + SUFFIX[mode]))


def _load(slot: str, mode: str) -> None:
    """`mode`'s store copied over the slot's own files (an empty store: an empty slot, the game starts it new)."""
    stored = _stored(slot, mode)
    for n in set(_own(slot)) - set(stored):
        os.remove(os.path.join(slot, n))   # held in the other store already (_store ran first)
    for n in stored:
        _copy(os.path.join(slot, n + SUFFIX[mode]), os.path.join(slot, n))


def switch(slot: str, to: str, keep: bool = False) -> bool:
    """The slot to `to`'s set; True when it changed. `keep` (first vanilla run, copy chosen): the vanilla set starts as
    a copy of the mod's, i.e. the slot's files stay. Each step is marked in MODE before the next."""
    other = 'mod' if to == 'vanilla' else 'vanilla'
    mode = _mode(slot)
    if mode == to:
        return False
    if mode == to + '-saved':   # cut off after storing `to`'s set, before loading the other: the files are still `to`'s
        _set_mode(slot, to)
        return False
    if mode != other + '-saved':
        _store(slot, other)
        _set_mode(slot, other + '-saved')
    if not keep:
        _load(slot, to)
    _set_mode(slot, to)
    return True


def backup_mod_saves() -> str | None:
    """SAVE_DATA copied into BACKUPS/<time> (the oldest beyond KEEP_BACKUPS removed); its path, None with no saves."""
    if not os.path.isdir(_p(SAVES)):
        return None
    stamp = time.strftime('%Y%m%d-%H%M%S')
    dst, n = _p(BACKUPS, stamp), 1
    while os.path.exists(dst):
        dst, n = _p(BACKUPS, f'{stamp}-{n}'), n + 1
    shutil.copytree(_p(SAVES), dst)
    for old in sorted(os.listdir(_p(BACKUPS)))[:-KEEP_BACKUPS]:
        shutil.rmtree(_p(BACKUPS, old), ignore_errors=True)
    return dst


def first_vanilla_saves() -> bool:
    """No vanilla saves yet: True copies the mod's (asked once), False starts the vanilla game with none."""
    print('原版第一次启动，还没有原版专用存档：')
    pick = input('  回车 / 1 = 原版用全新的存档（推荐）；2 = 复制一份现在的 MOD 存档给原版用'
                 '（存档里正装备着 MOD 新增的武器时原版会闪退）：').strip()
    return pick == '2'


def swap_in_saves() -> None:
    """Every slot to the vanilla set. ON first: a slot the vanilla game makes during the run is the vanilla set's."""
    all_slots = slots()
    keep = bool(all_slots) and not any(_stored(s, 'vanilla') for s in all_slots) and any(_own(s) for s in all_slots) \
        and first_vanilla_saves()
    for s in all_slots:   # before ON: a slot without MODE reads as vanilla while ON is there
        if not os.path.isfile(os.path.join(s, MODE)):
            _set_mode(s, 'mod')
    os.makedirs(save_root(), exist_ok=True)
    modfiles.atomic_write(_p(ON), b'')
    for s in all_slots:
        switch(s, 'vanilla', keep)


def swap_out_saves() -> str | None:
    """Every slot back to the mod's set, when any is not (else None): after a vanilla run, or a slot that came down
    from Steam Cloud in the vanilla set (a run on another machine). Never while the game runs: it would be loading or
    writing the very files."""
    pending = [s for s in slots() if _mode(s) != 'mod']
    if not pending and not os.path.exists(_p(ON)):
        return None
    if modfiles.game_running():
        return '游戏还在运行：存档等游戏退出后（下次原版启动或运行安装器时）再换回 MOD 存档'
    for s in pending:
        switch(s, 'mod')
    if os.path.exists(_p(ON)):
        os.remove(_p(ON))
    return '已换回 MOD 存档（原版存档以 .edf6vanilla 留在各存档槽里，和 MOD 存档一起云同步）' if pending else None


def restore_all(game: str) -> list[str]:
    """The ini and the saves a vanilla run left (what _restore and swap_out_saves said)."""
    with _lock:
        return [s for s in (_restore(game), swap_out_saves()) if s]


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
    """Closing the window while the game runs puts the ini back (the loader read it when the game started); the saves
    wait for the game to exit (swap_out_saves)."""
    global _handler
    if sys.platform != 'win32':
        return

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_uint)
    def handler(_event: int) -> int:
        restore_all(game)
        return 0
    _handler = handler
    ctypes.windll.kernel32.SetConsoleCtrlHandler(_handler, 1)


def launch(game: str) -> int:
    game = os.path.normpath(game)
    if modfiles.game_running():
        print(f'{modfiles.PROCESS} 已经在运行，先退出游戏再原版启动。')
        return 1
    for said in restore_all(game):   # a run that did not get to put things back
        print(said)
    print('原版启动：这一次不加载任何 MOD（插件、Mods 文件夹都不读），游戏退出后自动恢复。')
    print('存档不共用：原版用自己的存档（和 MOD 存档都在存档槽里，一起云同步），MOD 存档先备份，游戏退出后换回。')
    if said := backup_mod_saves():
        print('已备份 MOD 存档到', said)
    swap_in_saves()
    prepare(game)
    _on_close(game)
    print('游戏运行期间请不要关闭这个窗口。')
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
        for said in restore_all(game):
            print(said)


if __name__ == '__main__':
    sys.exit(launch(sys.argv[1] if len(sys.argv) > 1 else '.'))
