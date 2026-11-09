"""「原版启动」：game/原版启动.cmd (written by tools/installer.py install) runs the slim generator in Mods/Plugins as
`vanilla-launch <game>`, which starts the game once with EDFModLoader loading nothing and puts everything back after.

EDFModLoader (the game folder's winmm.dll) reads the game folder's ModLoader.ini when the game starts: with
LoadPlugins / LoadASI / Redirect all False no plugin loads and Root.cpk is read as shipped (no Mods/ redirect), i.e. the
stock game. So: back the ini up (STATE, in Mods/Plugins), write those three keys False, swap the saves (below: the
vanilla game gets saves of its own), start the game through Steam, wait for EDF6.exe to appear and to exit, then write
the ini back and swap the saves back. The ini is written back only while it is still the one written here (anything
that changed it since keeps its change); a run that never got to put things back (the window closed, the machine off)
is put back by the next launch and by the installer's install / uninstall (restore_all), and the saves also by the
plugin when the game next starts with the mod (restore_saves, before the game reads them: src/loadout_store.cpp).

One at a time, across processes (exclusive: a named mutex per save folder, released by Windows when its holder dies): a
second 原版启动 while one runs, the installer's restore and the plugin's restore_saves are refused, never interleaved.

Stdlib and pylib/modfiles only: frozen into the slim generator (tests/slim_generator_test.py).
"""
from __future__ import annotations

import base64
import contextlib
import ctypes
import hashlib
import os
import re
import shutil
import subprocess
import sys
import threading
import time

import modfiles
import save_backup

LAUNCHER = '原版启动.cmd'                                # in the game folder, beside EDF6.exe
STATE = 'EDF6VehicleCrew.vanilla_launch.json'          # in Mods/Plugins: the ini as it was, while a vanilla run is on
INI = 'ModLoader.ini'
SECTION = 'ModLoader'
OFF = ('LoadPlugins', 'LoadASI', 'Redirect')
STEAM_URL = 'steam://rungameid/2291060'
START_TIMEOUT = 300.0    # seconds for EDF6.exe to appear (Steam may have to start, or update the game, first)
POLL = 2.0
_lock = threading.Lock()   # restore runs on the main thread and in the console close handler
_closing = False           # set (under _lock) by the close handler: the main thread takes no further step
BUSY = 3                   # restore_saves' exit code when another process holds `exclusive`


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
    restore finds unchanged (no harm), never a vanilla ini with nothing to put back. A STATE already there is refused,
    never overwritten: the ini now may be a vanilla one, which would be kept as 'the original' for good."""
    ini, state = _paths(game)
    if os.path.exists(state):
        raise RuntimeError(f'{state} 已存在（上一次原版启动的 ini 还没恢复）：不覆盖')
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
# off anywhere resumes without mixing the sets: MODE is the slot's journal. '<X>-saved' says X's store is whole, not
# which files the slot holds (a load cut off leaves both sets' files mixed), so whatever set a switch is headed for, it
# loads that set's store whole (_load is idempotent: it only reads the store). Every write is an atomic rename; nothing
# is overwritten or removed before the whole SAVE_DATA is backed up (save_backup, deduplicated by content). Before each vanilla run the whole SAVE_DATA is also copied, locally,
# into SAVE_DATA.backups\<time> (tools/save_backup.py: the mod's starts are backed up the same way).
SAVES = 'SAVE_DATA'
SUFFIX = {'mod': '.edf6mod', 'vanilla': '.edf6vanilla'}
MODE = 'EDF6AF.mode'                     # in each slot folder: mod | vanilla | mod-saved | vanilla-saved
ON = SAVES + '.vanilla-on'               # beside SAVE_DATA, local: a vanilla run is on (slots it made are vanilla's)


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


def _write(path: str, data: bytes) -> None:   # every write and removal of a switch goes through these two (the tests
    modfiles.atomic_write(path, data)        # cut a switch off after each one)


def _unlink(path: str) -> None:
    os.remove(path)


def _set_mode(slot: str, mode: str) -> None:
    _write(os.path.join(slot, MODE), mode.encode('ascii'))


def _own(slot: str) -> list[str]:
    """The slot's own files: the playing set (not a store, not MODE, not a half-written temporary)."""
    return sorted(n for n in os.listdir(slot) if os.path.isfile(os.path.join(slot, n)) and n != MODE
                  and not n.endswith((*SUFFIX.values(), '.tmp-edf6')))


def _stored(slot: str, mode: str) -> list[str]:
    s = SUFFIX[mode]
    return sorted(n[:-len(s)] for n in os.listdir(slot) if n.endswith(s) and os.path.isfile(os.path.join(slot, n)))


def _copy(src: str, dst: str) -> None:
    with open(src, 'rb') as f:
        _write(dst, f.read())


def _store(slot: str, mode: str) -> None:
    """The slot's own files copied into `mode`'s store (whose files the set no longer has removed)."""
    own = _own(slot)
    for n in own:
        _copy(os.path.join(slot, n), os.path.join(slot, n + SUFFIX[mode]))
    for n in set(_stored(slot, mode)) - set(own):
        _unlink(os.path.join(slot, n + SUFFIX[mode]))


def _load(slot: str, mode: str) -> None:
    """`mode`'s store copied over the slot's own files (an empty store: an empty slot, the game starts it new)."""
    stored = _stored(slot, mode)
    for n in set(_own(slot)) - set(stored):
        _unlink(os.path.join(slot, n))   # held in the other store already (_store ran first)
    for n in stored:
        _copy(os.path.join(slot, n + SUFFIX[mode]), os.path.join(slot, n))


def switch(slot: str, to: str, keep: bool = False) -> bool:
    """The slot to `to`'s set; True when it changed. `keep` (first vanilla run, copy chosen): the vanilla set starts as
    a copy of the mod's, i.e. the slot's files stay. Each step is marked in MODE before the next."""
    other = 'mod' if to == 'vanilla' else 'vanilla'
    mode = _mode(slot)
    if mode == to:
        return False
    if mode == to + '-saved':   # `to`'s store is whole; the files may be anything up to the other set (a load cut off)
        _load(slot, to)
        _set_mode(slot, to)
        return True
    if mode != other + '-saved':
        _store(slot, other)
        _set_mode(slot, other + '-saved')
    if not keep:
        _load(slot, to)
    _set_mode(slot, to)
    return True


def first_vanilla_saves() -> bool:
    """No vanilla saves yet: True copies the mod's (asked once), False starts the vanilla game with none."""
    print('原版第一次启动，还没有原版专用存档：')
    pick = input('  回车 / 1 = 原版用全新的存档（推荐）；2 = 复制一份现在的 MOD 存档给原版用'
                 '（存档里正装备着 MOD 新增的武器时原版会闪退）：').strip()
    return pick == '2'


def first_time() -> bool:
    """No vanilla set in any slot yet while there are mod saves: swap_in_saves asks first_vanilla_saves."""
    all_slots = slots()
    return not any(_stored(s, 'vanilla') for s in all_slots) and any(_own(s) for s in all_slots)


class GameRunning(RuntimeError):
    """EDF6.exe found running where 原版启动 must not go on (swap_in_saves, _start): a game the player started meanwhile
    (Steam, with the mod: the ini and the saves were still the mod's) is reading and writing the very files."""


def swap_in_saves(keep: bool | None = None) -> None:
    """Every slot to the vanilla set. ON first: a slot the vanilla game makes during the run is the vanilla set's.
    `keep` None: asked here (first_time); launch asks before taking the lock (a prompt must not hold it).
    Never while the game runs (GameRunning, nothing changed): a mod game started during launch's first-run prompt would
    go on playing on the vanilla set, and its progress be stored as the vanilla set's after it exits."""
    if modfiles.game_running():
        raise GameRunning(f'{modfiles.PROCESS} 已经在运行（启动原版之前被正常启动了）：不切换存档。先退出游戏再原版启动。')
    if keep is None:
        keep = first_time() and first_vanilla_saves()
    all_slots = slots()
    for s in all_slots:   # before ON: a slot without MODE reads as vanilla while ON is there
        if not os.path.isfile(os.path.join(s, MODE)):
            _set_mode(s, 'mod')
    os.makedirs(save_root(), exist_ok=True)
    _write(_p(ON), b'')
    for s in all_slots:
        switch(s, 'vanilla', keep)


def swap_out_saves(in_game: bool = False) -> str | None:
    """Every slot back to the mod's set, when any is not (else None): after a vanilla run, or a slot that came down
    from Steam Cloud in the vanilla set (a run on another machine). Never while the game runs (it would be loading or
    writing the very files), except `in_game`: the game itself loading the plugin, before it reads the saves
    (restore_saves). The saves are backed up first: the switch overwrites the vanilla store and the slot's files."""
    pending = [s for s in slots() if _mode(s) != 'mod']
    if not pending and not os.path.exists(_p(ON)):
        return None
    if not in_game and modfiles.game_running():
        return '游戏还在运行：存档等游戏退出后（下次 MOD 启动、原版启动或运行安装器时）再换回 MOD 存档'
    if pending:
        save_backup.backup(save_root())
    for s in pending:
        switch(s, 'mod')
    if os.path.exists(_p(ON)):
        _unlink(_p(ON))
    return '已换回 MOD 存档（原版存档以 .edf6vanilla 留在各存档槽里，和 MOD 存档一起云同步）' if pending else None


def _restore_all(game: str) -> list[str]:
    return [s for s in (_restore(game), swap_out_saves()) if s]


def restore_all(game: str) -> list[str]:
    """The ini and the saves a vanilla run left (what _restore and swap_out_saves said). Callers in another process
    than a running launch hold `exclusive` around it (installer.write_game)."""
    with _lock:
        return _restore_all(game)


def _mutex_name() -> str:
    """One per save folder (the saves and, through the launch, the ini are what it guards); Local\\: this session's."""
    where = os.path.normcase(os.path.abspath(save_root())).encode('utf-8')
    return 'Local\\EDF6AF.vanilla_launch.' + hashlib.sha256(where).hexdigest()[:32]


@contextlib.contextmanager
def exclusive():
    """Yields True holding the cross-process lock over the ini and the saves, False when another process holds it
    (nothing waited for). A named mutex: a holder that dies (killed, the machine off) leaves it abandoned, which the
    next one takes (WAIT_ABANDONED), so a dead holder never locks anyone out."""
    if sys.platform != 'win32':
        raise OSError('原版启动只支持 Windows')
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateMutexW.restype = ctypes.c_void_p
    k32.CreateMutexW.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p)
    k32.WaitForSingleObject.restype = ctypes.c_uint32
    k32.WaitForSingleObject.argtypes = (ctypes.c_void_p, ctypes.c_uint32)
    k32.ReleaseMutex.argtypes = (ctypes.c_void_p,)
    k32.CloseHandle.argtypes = (ctypes.c_void_p,)
    h = k32.CreateMutexW(None, 0, _mutex_name())
    if not h:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        got = k32.WaitForSingleObject(h, 0)
        if got == 0xFFFFFFFF:   # WAIT_FAILED
            raise ctypes.WinError(ctypes.get_last_error())
        if got not in (0, 0x80):   # WAIT_OBJECT_0, WAIT_ABANDONED: ours; WAIT_TIMEOUT: another process's
            yield False
            return
        try:
            yield True
        finally:
            k32.ReleaseMutex(h)
    finally:
        k32.CloseHandle(h)


def restore_saves() -> int:
    """`restore-saves`: the plugin runs it and waits, when the game loads it with the saves not in the mod's set (a
    原版启动 window closed while the vanilla game ran, a slot synced from a vanilla run elsewhere): the game has not read
    them yet, so they are switched back under it. BUSY when a 原版启动 or the installer holds the saves."""
    with exclusive() as held:
        if not held:
            print('EDF6VC_SAVE_RESTORE busy (原版启动 or the installer is switching the saves)')
            return BUSY
        with _lock:
            said = swap_out_saves(in_game=True)
    print(f'EDF6VC_SAVE_RESTORE {"done" if said else "nothing"}')
    return 0


def game_pids() -> set[int]:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {modfiles.PROCESS}', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True, errors='replace')
    pids = set()
    for line in r.stdout.splitlines():
        cells = [c.strip('"') for c in line.split('","')]
        if len(cells) > 1 and cells[0].lower() == modfiles.PROCESS.lower() and cells[1].isdigit():
            pids.add(int(cells[1]))
    return pids


def _start() -> set[int]:
    """Steam asked to start the game, the vanilla ini and saves in place (a step: the close handler never comes between
    the check and the start). Refused (GameRunning) when EDF6.exe already runs: one started since swap_in_saves would
    be taken for the vanilla game and waited on. The EDF6.exe PIDs before the start: launch waits for a new one."""
    before = game_pids()
    if before or modfiles.game_running():
        raise GameRunning(f'{modfiles.PROCESS} 在原版启动之前已经运行：不启动，存档和 ini 换回 MOD 的。')
    os.startfile(STEAM_URL)
    return before


_handler = None   # the console close handler, kept alive while it is installed


class Closed(Exception):
    """The console window is closing: the close handler has put back what it could; the main thread stops."""


def _step(fn, *args):
    """One step of launch, under _lock and only while the window is not closing: the close handler never runs in the
    middle of a step, and no step runs after it (a prepare after its restore would leave the vanilla ini behind)."""
    with _lock:
        if _closing:
            raise Closed
        return fn(*args)


def _close(game: str) -> None:
    """The close handler's work: no further step, then the ini and the saves put back. The game running, the saves stay
    (the game is using them): the ini back means its next start loads the plugin, which switches them back before the
    game reads them (restore_saves), so the ini and the saves never stay apart."""
    global _closing
    with _lock:
        _closing = True
        _restore_all(game)


def _on_close(game: str) -> None:
    """Installed before anything is changed: closing the window at any point puts back what the run changed."""
    global _handler
    if sys.platform != 'win32':
        return

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_uint)
    def handler(_event: int) -> int:
        _close(game)
        return 0
    _handler = handler
    ctypes.windll.kernel32.SetConsoleCtrlHandler(_handler, 1)


def launch(game: str) -> int:
    game = os.path.normpath(game)
    with exclusive() as held:
        if not held:
            print('原版启动已经在运行（或安装器正在恢复存档）：不重复启动。')
            return 1
        try:
            return _launch(game)
        except Closed:
            return 1


def _launch(game: str) -> int:
    if modfiles.game_running():
        print(f'{modfiles.PROCESS} 已经在运行，先退出游戏再原版启动。')
        return 1
    _on_close(game)
    try:
        for said in _step(_restore_all, game):   # a run that did not get to put things back
            print(said)
        print('原版启动：这一次不加载任何 MOD（插件、Mods 文件夹都不读），游戏退出后自动恢复。')
        print('存档不共用：原版用自己的存档（和 MOD 存档都在存档槽里，一起云同步），启动前先备份，游戏退出后换回。')
        if said := _step(save_backup.backup, save_root()):
            print('已备份存档到', said)
        keep = _step(first_time) and first_vanilla_saves()   # asked outside the lock
        _step(swap_in_saves, keep)
        _step(prepare, game)
        before = _step(_start)
        print('游戏运行期间请不要关闭这个窗口。')
        waited = 0.0
        while not (new := game_pids() - before):
            if waited >= START_TIMEOUT:
                print(f'{int(START_TIMEOUT)} 秒内没有看到 {modfiles.PROCESS} 启动（Steam 没打开或启动被取消）。')
                return 1
            time.sleep(POLL)
            waited += POLL
        print(f'游戏已启动（PID {min(new)}），等它退出……')
        while modfiles.game_running():
            time.sleep(POLL)
        print('游戏已退出。')
        return 0
    except GameRunning as e:
        print(e)
        return 1
    finally:
        with _lock:
            if not _closing:   # else the close handler has put things back
                for said in _restore_all(game):
                    print(said)


if __name__ == '__main__':
    sys.exit(launch(sys.argv[1] if len(sys.argv) > 1 else '.'))
