"""Starting EDF6 for a test run (tests/autopilot/drive.py, tools/perfrun.py through it, testrange/run_test.py):
straight from the game folder, never through Steam's launch flow.

Why not steam://rungameid: Steam's launch is a chain of steps (SynchronizingCloud, SynchronizingStats,
ShowInterstitials, SynchronizingControllerConfig, ...) any of which can stop and wait for the user. 2026-11-26 14:07
(D:/steam/logs/console_log.txt): `LaunchApp waiting for user response to SynchronizingControllerConfig "syncfailed"`,
Steam's 「无法同步」 inside its main window; three runs gave up after 150 s, and when the user answered the dialog
minutes later Steam went on with that long-abandoned launch and started a game with no test DLL in it. A later
steam://rungameid while one waits is swallowed (only ExecuteSteamURL is logged). Nothing on our side can cancel it.

What Steam actually starts is LaunchGame.exe (the EOS bootstrapper: LaunchGame.ini ApplicationPath=EDF6.exe,
installscript.vdf's "EOS Bootstrapper - 2291060"), with SteamAppId / SteamGameId set; LaunchGame.exe starts EDF6.exe
and exits. Started the same way from here (cwd the game folder, SteamAppId=SteamGameId=2291060, a Steam client
running) the game runs as from Steam: SteamAPI_Init (EDF.dll imports no SteamAPI_RestartAppIfNecessary) connects to the
running client, which logs `Game process added : AppID 2291060 ...EDF6.exe`, injects its overlay and syncs the cloud
saves at exit as usual; EDFModLoader (winmm.dll beside EDF6.exe) loads exactly as before. Measured 2026-11-26. Started
as EDF6.exe itself instead (the bootstrapper skipped) the game stops at 「错误： 验证游戏文件的完整性」 / "ErrorCode:
1_0": that, not Steam, was the integrity error an earlier attempt (2026-10-15) took for "only runs launched by Steam".

So no Steam dialog can hold a test launch, and a launch given up leaves nothing behind: the game process is ours (the
EDF6.exe LaunchGame.exe we started created, with no other EDF6.exe running before: require it), and giving up closes
exactly that process (abandon). Not ours is never touched: the user's own game makes every launch refuse up front.

Failing fast: Steam not running, a Steam launch of EDF6 pending (a dialog nobody answered: answering it later would
start a second game in the middle of the run, or a stray one after it), LaunchGame.exe exiting without starting the
game, a message box of either process (its text is the error), or the game exiting before its window: each is said at
once instead of waiting for the window's deadline.
"""
from __future__ import annotations

import os
import re
import subprocess
import time
from dataclasses import dataclass
from typing import Callable

APP_ID = 2291060
LAUNCHER = 'LaunchGame.exe'
PROCESS = 'EDF6.exe'
WINDOW_CLASS = 'xgs::Framework'   # the game's main window ("EarthDefenceForce 6 for PC")
DIALOG_CLASS = '#32770'           # a message box
# The game's own quit (ExitApp 0x1183190, tests/autopilot/autopilot.cpp Quit): the may-close flag, then WM_CLOSE to
# its window; valid for the supported EDF.dll only (TimeDateStamp below, the plugins' own check).
EDF_DLL_STAMP = 0x678CCB46
MAY_CLOSE, GAME_HWND = 0x21360B8, 0x21360A8
# A Steam client session's first rows (console_log.txt spans several sessions): actions before it are dead.
STEAM_SESSION = 'CStartupModeManager::ComputeStartupMode'
ACTION = re.compile(r'GameAction \[AppID (\d+), ActionID (\d+)\] : (.*)')
ACTION_DONE = ('changed task to Completed', 'changed task to Failed', 'LaunchApp failed')


class LaunchFailed(RuntimeError):
    """The game did not come up; said why. Whatever this launch started is closed again."""


@dataclass
class Game:
    pid: int        # EDF6.exe
    hwnd: int       # its main window
    launcher: int   # the LaunchGame.exe that started it (exited by now)


# ---------- Steam ----------
def steam_path() -> str | None:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            return winreg.QueryValueEx(k, 'SteamPath')[0]
    except (ImportError, OSError):
        return None


def pending_steam_launch(log: str, app: int = APP_ID) -> str | None:
    """A Steam launch of APP that is not over in the client session `log` (console_log.txt) ends with, said as one
    line, or None. Over: its task went Completed / Failed. An action's "waiting for user response to X" that Steam
    answers itself is followed by "continues with user response" at once; one left last is a dialog nobody answered."""
    at = log.rfind(STEAM_SESSION)
    if at >= 0:
        log = log[log.rfind('\n', 0, at) + 1:]
    actions: dict[str, list[str]] = {}
    for line in log.splitlines():
        m = ACTION.search(line)
        if m and int(m[1]) == app:
            actions.setdefault(m[2], []).append(line.strip())
    for action, lines in actions.items():
        if any(done in line for line in lines for done in ACTION_DONE):
            continue
        last = lines[-1]
        waiting = re.search(r'waiting for user response to (\S+)(?: "([^"]*)")?\s*$', last)
        if waiting:
            what = waiting[1] + (f' "{waiting[2]}"' if waiting[2] else '')
            return (f'Steam launch {action} of {app} waits for an answer to {what} in the Steam window ({last}): '
                    'answer or cancel it there (or restart Steam) first; answered later it starts the game by itself')
        return f'Steam is launching {app} right now (action {action}: {last}): not starting a second one'
    return None


def steam_log_pending(app: int = APP_ID) -> str | None:
    """pending_steam_launch of the client's console_log.txt (None when there is none)."""
    path = steam_path()
    if not path:
        return None
    try:
        with open(os.path.join(path, 'logs', 'console_log.txt'), 'rb') as f:
            text = f.read().decode('utf-8', 'replace')
    except OSError:
        return None
    return pending_steam_launch(text, app)


# ---------- processes and windows (Windows) ----------
def processes() -> list[tuple[int, int, str]]:
    """Every process as (pid, parent pid, exe file name): a Toolhelp snapshot."""
    import ctypes
    import ctypes.wintypes as wt

    class Entry(ctypes.Structure):
        _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD),
                    ('th32DefaultHeapID', ctypes.c_void_p), ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
                    ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
                    ('szExeFile', ctypes.c_wchar * 260)]
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
    k32.Process32FirstW.argtypes = k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(Entry)]
    k32.CloseHandle.argtypes = [wt.HANDLE]
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)   # TH32CS_SNAPPROCESS
    if snap in (None, wt.HANDLE(-1).value):
        raise OSError(ctypes.get_last_error(), 'CreateToolhelp32Snapshot')
    out = []
    try:
        e = Entry()
        e.dwSize = ctypes.sizeof(Entry)
        ok = k32.Process32FirstW(snap, ctypes.byref(e))
        while ok:
            out.append((e.th32ProcessID, e.th32ParentProcessID, e.szExeFile))
            ok = k32.Process32NextW(snap, ctypes.byref(e))
    finally:
        k32.CloseHandle(snap)
    return out


def pids_of(exe: str, procs: list[tuple[int, int, str]]) -> list[int]:
    return [pid for pid, _, name in procs if name.lower() == exe.lower()]


def windows(pids: set[int]) -> list[tuple[int, int, str, str]]:
    """The visible top-level windows of `pids` as (hwnd, pid, class, title)."""
    import ctypes
    import ctypes.wintypes as wt
    user32 = ctypes.WinDLL('user32')
    user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    user32.IsWindowVisible.argtypes = [wt.HWND]
    user32.GetClassNameW.argtypes = user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    found: list[tuple[int, int, str, str]] = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd: int, _: int) -> bool:
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and user32.IsWindowVisible(hwnd):
            cls = ctypes.create_unicode_buffer(256)
            title = ctypes.create_unicode_buffer(512)
            user32.GetClassNameW(hwnd, cls, 256)
            user32.GetWindowTextW(hwnd, title, 512)
            found.append((hwnd, pid.value, cls.value, title.value))
        return True
    user32.EnumWindows(each, 0)
    return found


def dialog_text(hwnd: int) -> str:
    """A message box's title and its static texts, one line."""
    import ctypes
    import ctypes.wintypes as wt
    user32 = ctypes.WinDLL('user32')
    user32.GetClassNameW.argtypes = user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    parts: list[str] = []
    title = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, title, 512)
    parts.append(title.value)

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(child: int, _: int) -> bool:
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(child, cls, 64)
        if cls.value == 'Static':
            text = ctypes.create_unicode_buffer(2048)
            user32.GetWindowTextW(child, text, 2048)
            if text.value.strip():
                parts.append(text.value.strip())
        return True
    user32.EnumChildWindows(hwnd, each, 0)
    return ' / '.join(p.replace('\r', ' ').replace('\n', ' ') for p in parts if p)


def game_dll_stamp(game: str) -> int | None:
    """EDF.dll's PE TimeDateStamp on disk."""
    try:
        with open(os.path.join(game, 'EDF.dll'), 'rb') as f:
            head = f.read(4096)
        at = int.from_bytes(head[0x3C:0x40], 'little')
        if head[at:at + 4] != b'PE\0\0':
            return None
        return int.from_bytes(head[at + 8:at + 12], 'little')
    except (OSError, ValueError):
        return None


def request_exit(pid: int, game: str) -> bool:
    """The game's own quit from outside (ExitApp: may-close flag, WM_CLOSE posted to its window), as the autopilot's
    `quit` does from inside. False when it cannot be asked (another EDF.dll, no access, no window yet)."""
    if game_dll_stamp(game) != EDF_DLL_STAMP:
        return False
    import ctypes
    import ctypes.wintypes as wt
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi')
    user32 = ctypes.WinDLL('user32')
    k32.OpenProcess.restype = wt.HANDLE
    k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    k32.CloseHandle.argtypes = [wt.HANDLE]
    rw = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    k32.ReadProcessMemory.argtypes = k32.WriteProcessMemory.argtypes = rw
    psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_void_p), wt.DWORD,
                                           ctypes.POINTER(wt.DWORD), wt.DWORD]
    psapi.GetModuleBaseNameW.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.LPWSTR, wt.DWORD]
    user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
    h = k32.OpenProcess(0x0400 | 0x0010 | 0x0020 | 0x0008, False, pid)
    if not h:
        return False
    try:
        mods = (ctypes.c_void_p * 1024)()
        need = wt.DWORD()
        if not psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3):
            return False
        base = None
        for m in mods[:min(need.value // ctypes.sizeof(ctypes.c_void_p), 1024)]:
            name = ctypes.create_unicode_buffer(260)
            psapi.GetModuleBaseNameW(h, m, name, 260)
            if name.value.lower() == 'edf.dll':
                base = m
                break
        if not base:
            return False
        hwnd = ctypes.c_uint64()
        n = ctypes.c_size_t()
        if not k32.ReadProcessMemory(h, base + GAME_HWND, ctypes.byref(hwnd), 8, ctypes.byref(n)) or not hwnd.value:
            return False
        one = ctypes.c_uint8(1)
        if not k32.WriteProcessMemory(h, base + MAY_CLOSE, ctypes.byref(one), 1, ctypes.byref(n)):
            return False
        return bool(user32.PostMessageW(hwnd.value, 0x0010, 0, 0))   # WM_CLOSE
    finally:
        k32.CloseHandle(h)


def terminate(pid: int) -> bool:
    import ctypes
    import ctypes.wintypes as wt
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.OpenProcess.restype = wt.HANDLE
    k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
    k32.CloseHandle.argtypes = [wt.HANDLE]
    h = k32.OpenProcess(0x0001, False, pid)   # PROCESS_TERMINATE
    if not h:
        return False
    try:
        return bool(k32.TerminateProcess(h, 1))
    finally:
        k32.CloseHandle(h)


# ---------- the launch ----------
def environment(base: dict[str, str] | None = None) -> dict[str, str]:
    """What Steam gives LaunchGame.exe that the game needs: its app id (SteamAPI_Init reads SteamAppId)."""
    env = dict(os.environ if base is None else base)
    env['SteamAppId'] = env['SteamGameId'] = str(APP_ID)
    return env


def refusal(procs: list[tuple[int, int, str]], pending: str | None) -> str | None:
    """Why a launch must not start now, or None."""
    if pids_of(PROCESS, procs):
        return f'{PROCESS} is running (the user\'s game or another run\'s): never touched, not launching'
    if pids_of(LAUNCHER, procs):
        return f'{LAUNCHER} is running (a game starting): not launching a second one'
    if not pids_of('steam.exe', procs):
        return 'Steam is not running: start Steam (logged in) first; the game needs its client'
    return pending


def abandon(game: str, pid: int | None, launcher: int | None, say: Callable[[str], None] = print,
            wait: float = 30.0) -> None:
    """Close what this launch started, and only that: the game's own quit first, TerminateProcess when it does not go
    in WAIT seconds (no window yet, a message box up) — the process is this launch's own (its EDF6.exe, created by
    its LaunchGame.exe), never one that was running before."""
    procs = processes()
    alive = {p for p, _, _ in procs}
    if launcher and launcher in alive and launcher in pids_of(LAUNCHER, procs):
        terminate(launcher)
    if not pid or pid not in pids_of(PROCESS, procs):
        return
    if request_exit(pid, game):
        end = time.monotonic() + wait
        while time.monotonic() < end:
            if pid not in pids_of(PROCESS, processes()):
                say(f'launch given up: its {PROCESS} ({pid}) quit the game\'s own way')
                return
            time.sleep(0.5)
    say(f'launch given up: its {PROCESS} ({pid}) did not quit the game\'s own way: ended')
    terminate(pid)


def launch(game: str, timeout: float = 90.0, *, on_window: Callable[[int], None] | None = None,
           say: Callable[[str], None] = print) -> Game:
    """Start the game from GAME (LaunchGame.exe, as Steam does) and return once its main window is there (`on_window`
    called with it at once, e.g. to move it off screen). LaunchFailed when it cannot, with what this launch started
    closed again (abandon)."""
    import modfiles   # the same folder
    why = refusal(processes(), steam_log_pending())
    if why:
        raise LaunchFailed(why)
    # The game just quit: Steam still counts it running for a few seconds (its exit's cloud sync): start after it.
    if not modfiles.wait_steam_released(60, APP_ID):
        raise LaunchFailed('Steam still counts EDF6 as running 60 s after EDF6.exe is gone: not launching')
    why = refusal(processes(), None)
    if why:
        raise LaunchFailed(why)
    exe = os.path.join(game, LAUNCHER)
    if not os.path.isfile(exe):
        raise LaunchFailed(f'no {exe}: the install is broken')
    started = subprocess.Popen([exe], cwd=game, env=environment(), stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True)
    launcher = started.pid
    pid: int | None = None
    try:
        end = time.monotonic() + timeout
        launcher_gone_at: float | None = None
        while True:
            procs = processes()
            if pid is None:
                pid = next((p for p, parent, name in procs if parent == launcher and name.lower() == PROCESS.lower()),
                           None)
            if pid is not None and pid not in pids_of(PROCESS, procs):
                raise LaunchFailed(f'{PROCESS} ({pid}) exited before its window showed')
            if pid is None and started.poll() is not None:
                launcher_gone_at = launcher_gone_at or time.monotonic()
                if time.monotonic() - launcher_gone_at > 5:
                    raise LaunchFailed(f'{LAUNCHER} exited (code {started.returncode}) without starting {PROCESS}')
            shown = windows({launcher} | ({pid} if pid else set()))
            boxes = [w for w in shown if w[2] == DIALOG_CLASS]
            if boxes:
                raise LaunchFailed('message box of the starting game: ' + ' | '.join(dialog_text(w[0]) for w in boxes))
            main = next((w for w in shown if pid and w[1] == pid and w[2] == WINDOW_CLASS), None)
            if main:
                if on_window:
                    on_window(main[0])
                return Game(pid, main[0], launcher)
            if time.monotonic() >= end:
                raise LaunchFailed(f'no game window in {timeout:.0f} s')
            time.sleep(0.25)
    except BaseException:
        abandon(game, pid, launcher, say)
        raise
