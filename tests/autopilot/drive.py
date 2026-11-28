"""Drives EDF6 in the background through the test-only EDF6Autopilot plugin (tests/autopilot/autopilot.cpp): no key goes
to the desktop and the game window stays off screen, below the others (the user, 2026-10-07: "let it run in the
background"). For measurements, e.g. the big map's memory against the stock test range.

  python tests/autopilot/drive.py install            copy build/tools/EDF6Autopilot.dll into <game>/Mods/Plugins
  python tests/autopilot/drive.py launch             start the game (LaunchGame.exe from its folder, never through Steam's
                                                     launch: pylib/gamelaunch.py), its window off screen, at the back
  python tests/autopilot/drive.py key VK[+VK] [ms]   hold the keys (hex virtual-key codes, or names: enter esc up ...;
                                                     pad_a ... pad_rt: the autopilot's virtual pad; stick_*: its
                                                     left stick, look_*: its right stick)
  python tests/autopilot/drive.py shot FILE.png      the game window's picture (PrintWindow, never the screen)
  python tests/autopilot/drive.py mem [N]            the plugin's last N MEM rows
  python tests/autopilot/drive.py cmd TEXT           one command to the plugin: mem, quit, mission ROW|RM015|M001|range|edf41:ROW|edf41mp01:ROW|edf41mp02:ROW|edf5:ROW [0-4],
                                                     cycle N [STAY] [reboot],
                                                     next MISSION [0-4] [reboot] (leave the mission now and enter
                                                     MISSION in the same launch: tools/perfrun.py's legs)
  python tests/autopilot/drive.py run [MISSION] [DIFFICULTY] [SECONDS] [LOG]
        one go: install, ask the mission (default range: the test range's own mission pack, by the content id in the
        installed EDF6VehicleCrew.ini; RM015, M001 or a row: the stock offline list), launch in the background, stay
        SECONDS (default 45) in the mission, quit the game's own way, print the memory (at the start, peak, end), keep
        the plugin's log at LOG, uninstall
        options (after the positional ones):
          --cmd TEXT       one more plugin command handed with the mission's (repeatable), e.g. --cmd "probe airdrop"
                           (the stock air delivery probe: tests/autopilot/airdrop_probe.cpp)
                           or --cmd "probe wdenergy" (the Wing Diver's energy and her weapons' charge every 250 ms:
                           tests/autopilot/wdenergy_probe.cpp)
                           or --cmd "probe hector" (each EDF4.1 Hector's position, facing, walk vector, velocity
                           and main CAS state from its own frame: tests/autopilot/hector_probe.cpp)
                           or --cmd "probe players" (each local soldier's pad +0x338 and class key record +0xD40
                           every second: tests/autopilot/players_probe.cpp)
                           or --cmd "probe umbra" (every box EDF.dll hands Umbra checked for values out of reach, with
                           its owner, and every Vehicle601_Tank's aim and gun pose: tests/autopilot/umbra_probe.cpp)
                           or --cmd "probe physics [TICKS]" (every live non-static hknp body's position and
                           velocity by scene tick from the mission's first, default 6000 ticks; a bit-exact hash every
                           10 ticks: tests/autopilot/physics_probe.cpp, compared by tools/physics_probe_compare.py)
                           or --cmd "probe heap N" with --cycle (what was allocated from the Nth cycle's way out on
                           and is still alive at each later one, by call path: tests/autopilot/heap_probe.cpp; every
                           cycle also logs MEMMAP / HEAPS, where the commit sits)
          --loadout FILE   the run's forced loadout: FILE copied as <plugins>/EDF6TestRange.loadout.ini (read by the
                           installed EDF6VehicleCrew at the player's creation; the file must not exist already, and
                           is removed again afterwards), e.g. tests/autopilot/loadouts/airraider_grape.ini
          --shots DIR      the game window's picture into DIR every 15 s of the mission (PrintWindow)
          --every SECONDS  ...every SECONDS instead (e.g. 1.5 to follow an effect)
          --key SPEC@S[:MS] hold SPEC (as `key` takes it) S seconds into the mission for MS ms (default 1000),
                           with a picture just before and after when --shots is given (repeatable)
          --place SRC=DEST  SRC put at <game>/Mods/DEST for the run (e.g. a build's EDF6VehicleCrew.dll over the
                           installed one, or an SGO the build needs); the original bytes are kept and put back
                           byte for byte afterwards (repeatable)
          --ini KEY=VALUE  KEY set in EDF6VehicleCrew.ini [VehicleCrew] for the run, the ini put back afterwards
          --hq             the mission's pack and row made current, then the HQ (sortie) menu itself instead of the
                           mission: SECONDS there (its pictures with --shots), quit the same way; the HQ menu as stock
                           (--ini HqAutoMissionSelect=0 added: the plugin would open the mission select over it), unless
                           --ini HqAutoMissionSelect=... is given (=1: to see that mission select open by itself)
          --cycle N        leave the mission and enter the same one again N times in this one launch (the plugin's
                           `cycle` command: the pause menu's withdraw on the game thread, the result screen skipped,
                           nothing saved; docs/mission-cycle-re.md), then quit as soon as the plugin logs
                           "CYCLE done"; SECONDS is then the deadline for all of it (failed when it passes first,
                           or on "CYCLE failed" / "CYCLE refused")
          --cycle-stay S   seconds in each mission before it is left (default 20)
          --cycle-reboot   back in through the script's ReBoot (SystemReset) instead of straight into the mission
        Every run also watches the desktop (DesktopWatch): the foreground window's process, the cursor clip and the
        cursor every 20 ms, and once in the mission the real cursor moved 40 px to see that nothing pulls it back;
        the counts are printed at the end (all must be 0).
  python tests/autopilot/drive.py hide               the window off screen and at the back again
  python tests/autopilot/drive.py uninstall          remove the plugin, its keys file and its log (game closed)
  python tests/autopilot/drive.py restore            what a run left when the game outlived it (game closed): every
                                                     file it placed (--place, --ini, --loadout) put back byte for byte
                                                     from <plugins>/EDF6Autopilot.placed/manifest.json, then uninstall

The game must not be running for install / launch / uninstall (it is the user's: their running game is never touched).
Every command but mem / shot first waits its turn for the machine's game (pylib/gamelease.py, tools/gamequeue.py):
another session's run is never installed over or driven. Steps meant as one run (install, launch, key ..., uninstall)
go under one `python tools/gamequeue.py hold`, with its EDF6_GAME_LEASE set for each step.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import math
import ctypes.wintypes as wt
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from collections.abc import Callable

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402
import gamelaunch  # noqa: E402
import gamelease  # noqa: E402
TOOLS = os.path.join(ROOT, 'tools')   # tools/support_config.py (the ini's own editor)

NAME = 'EDF6Autopilot'
DEFAULT_MISSION = 'range'   # the test range's mission pack (no longer laid over RM015)
KEYS = {'enter': 0x0D, 'esc': 0x1B, 'space': 0x20, 'left': 0x25, 'up': 0x26, 'right': 0x27, 'down': 0x28,
        'alt': 0x12, 'f4': 0x73, 'shift': 0x10, 'ctrl': 0x11, 'tab': 0x09,
        # the autopilot's virtual pad (0x100 + XInput button bit; 0x110 / 0x111 the triggers)
        'pad_up': 0x100, 'pad_down': 0x101, 'pad_left': 0x102, 'pad_right': 0x103, 'pad_ls': 0x106, 'pad_rs': 0x107,
        'pad_lb': 0x108, 'pad_rb': 0x109, 'pad_a': 0x10C, 'pad_b': 0x10D, 'pad_x': 0x10E, 'pad_y': 0x10F,
        'pad_lt': 0x110, 'pad_rt': 0x111,
        # its left stick pushed all the way (the autopilot's PadState: up, down, right, left)
        'stick_up': 0x112, 'stick_down': 0x113, 'stick_right': 0x114, 'stick_left': 0x115,
        # its right stick (the look) pushed half way
        'look_up': 0x116, 'look_down': 0x117, 'look_right': 0x118, 'look_left': 0x119}
user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32')
# ctypes otherwise defaults to C int, truncating Win64 HWND/HDC/HGDIOBJ values.
ENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
for lib, name, result, args in (
    (user32, 'GetForegroundWindow', wt.HWND, []),
    (user32, 'GetWindow', wt.HWND, [wt.HWND, wt.UINT]),
    (user32, 'GetDC', wt.HDC, [wt.HWND]),
    (user32, 'ReleaseDC', ctypes.c_int, [wt.HWND, wt.HDC]),
    (user32, 'GetWindowThreadProcessId', wt.DWORD, [wt.HWND, ctypes.POINTER(wt.DWORD)]),
    (user32, 'GetClientRect', wt.BOOL, [wt.HWND, ctypes.POINTER(wt.RECT)]),
    (user32, 'IsWindowVisible', wt.BOOL, [wt.HWND]),
    (user32, 'EnumWindows', wt.BOOL, [ENUMPROC, wt.LPARAM]),
    (user32, 'SetWindowPos', wt.BOOL, [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, ctypes.c_int, wt.UINT]),
    (user32, 'SetForegroundWindow', wt.BOOL, [wt.HWND]),
    (user32, 'PrintWindow', wt.BOOL, [wt.HWND, wt.HDC, wt.UINT]),
    (user32, 'GetCursorPos', wt.BOOL, [ctypes.POINTER(wt.POINT)]),
    (user32, 'SetCursorPos', wt.BOOL, [ctypes.c_int, ctypes.c_int]),
    (user32, 'GetClipCursor', wt.BOOL, [ctypes.POINTER(wt.RECT)]),
    (gdi32, 'CreateCompatibleDC', wt.HDC, [wt.HDC]),
    (gdi32, 'CreateCompatibleBitmap', wt.HBITMAP, [wt.HDC, ctypes.c_int, ctypes.c_int]),
    (gdi32, 'SelectObject', wt.HGDIOBJ, [wt.HDC, wt.HGDIOBJ]),
    (gdi32, 'GetDIBits', ctypes.c_int, [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT,
                                     ctypes.c_void_p, ctypes.c_void_p, wt.UINT]),
    (gdi32, 'DeleteObject', wt.BOOL, [wt.HGDIOBJ]),
    (gdi32, 'DeleteDC', wt.BOOL, [wt.HDC]),
):
    fn = getattr(lib, name)
    fn.restype, fn.argtypes = result, args
user32.SetProcessDPIAware()


def plugins(game: str) -> str:
    return os.path.join(game, 'Mods', 'Plugins')


def game_pids() -> list[int]:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                         text=True, check=True).stdout
    return [int(line.split('","')[1]) for line in out.splitlines() if line.startswith('"EDF6.exe"')]


def game_window() -> int | None:
    pids = set(game_pids())
    found: list[int] = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd: int, _: int) -> bool:
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        rect = wt.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(rect))
        if pid.value in pids and user32.IsWindowVisible(hwnd) and not user32.GetWindow(hwnd, 4) and rect.right >= 320:
            found.append(hwnd)
        return True
    user32.EnumWindows(each, 0)
    return found[0] if found else None


def hide(hwnd: int) -> None:
    """Off screen past the right of the desktop, at the bottom of the z-order, without activating it."""
    left = user32.GetSystemMetrics(76) + user32.GetSystemMetrics(78) + 200   # SM_XVIRTUALSCREEN + SM_CXVIRTUALSCREEN
    user32.SetWindowPos(hwnd, 1, left, 0, 0, 0, 0x0001 | 0x0010)   # HWND_BOTTOM, SWP_NOSIZE | SWP_NOACTIVATE


class DesktopWatch:
    """What a background run does to the user's desktop, sampled every 20 ms from outside the game: the foreground
    window's process, the cursor clip and the cursor. `nudge()` moves the real cursor 40 px and back and tells whether
    something pulled it meanwhile (2026-10-10: the game recentred the user's mouse every frame)."""

    def __init__(self) -> None:
        self.samples = 0
        self.game_foreground = 0      # samples with an EDF6.exe window in the foreground
        self.clipped = 0              # samples with the cursor clipped to less than the whole desktop
        self.at_right_edge = 0        # samples with the cursor on the desktop's right edge (where an off-screen centre clamps)
        self.nudges: list[str] = []
        self.clips: list[str] = []    # each distinct clip rectangle seen: when, the rectangle, the foreground process
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def start(self) -> 'DesktopWatch':
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(5)

    def _loop(self) -> None:
        left, top = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
        right, bottom = left + user32.GetSystemMetrics(78), top + user32.GetSystemMetrics(79)
        while not self._stop.is_set():
            pids = set(game_pids()) if self.samples % 50 == 0 else getattr(self, '_pids', set())
            self._pids = pids
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
            clip, pos = wt.RECT(), wt.POINT()
            user32.GetClipCursor(ctypes.byref(clip))
            user32.GetCursorPos(ctypes.byref(pos))
            self.samples += 1
            self.game_foreground += pid.value in pids
            rect = (clip.left, clip.top, clip.right, clip.bottom)
            if rect != (left, top, right, bottom):
                self.clipped += 1
                if len(self.clips) < 8 and not any(str(rect) in c for c in self.clips):
                    self.clips.append(f'{time.strftime("%H:%M:%S")} clip {rect} with {process_name(pid.value)} '
                                      'in the foreground')
            self.at_right_edge += pos.x >= right - 1
            time.sleep(0.02)

    def nudge(self) -> str:
        start = wt.POINT()
        user32.GetCursorPos(ctypes.byref(start))
        # 40 px towards the desktop's middle (a target past the edge would be clamped by Windows, not by a pull)
        middle = (user32.GetSystemMetrics(76) + user32.GetSystemMetrics(78) // 2,
                  user32.GetSystemMetrics(77) + user32.GetSystemMetrics(79) // 2)
        target = (start.x + (40 if start.x < middle[0] else -40), start.y + (40 if start.y < middle[1] else -40))
        user32.SetCursorPos(*target)
        # The game's window is off screen past the desktop's right edge: a pull to its centre lands on that edge.
        edge = user32.GetSystemMetrics(76) + user32.GetSystemMetrics(78) - 1
        moved, pulled = [], []
        for _ in range(100):   # 2 s
            time.sleep(0.02)
            now = wt.POINT()
            user32.GetCursorPos(ctypes.byref(now))
            if (now.x, now.y) != target:
                (pulled if now.x >= edge else moved).append((now.x, now.y))
        user32.SetCursorPos(start.x, start.y)
        if pulled:
            verdict = f'cursor set to {target}, PULLED to the edge in {len(pulled)}/100 samples (first {pulled[0]})'
        elif moved:
            verdict = (f'cursor set to {target}, moved elsewhere in {len(moved)}/100 samples (first {moved[0]}): '
                       'the user moving it, not pulled to the game')
        else:
            verdict = f'cursor set to {target}, stayed there for 2 s'
        self.nudges.append(verdict)
        return verdict

    def report(self) -> str:
        return (f'desktop over {self.samples} samples: game in the foreground {self.game_foreground}, cursor clipped '
                f'{self.clipped}, cursor on the right edge {self.at_right_edge}; nudges: {self.nudges or "none"}'
                + (f'; clips: {self.clips}' if self.clips else ''))


def process_name(pid: int) -> str:
    """The executable's file name of a process (or its id when it cannot be read)."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    handle = kernel.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return f'pid {pid}'
    try:
        size = wt.DWORD(512)
        buf = ctypes.create_unicode_buffer(512)
        if kernel.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
            return os.path.basename(buf.value)
        return f'pid {pid}'
    finally:
        kernel.CloseHandle(handle)


SESSION_EXTS = ('.dll', '.keys', '.log', '.cmd', '.keys.tmp', '.cmd.tmp')
LOADOUT = 'EDF6TestRange.loadout.ini'   # EDF6VehicleCrew's forced loadout (src/loadout.cpp), next to the plugins


def require_closed() -> None:
    if game_pids():
        raise RuntimeError('EDF6 is running: never touched')


def install(game: str) -> None:
    require_closed()
    directory = plugins(game)
    src = os.path.join(ROOT, 'build', 'tools', NAME + '.dll')
    with open(src, 'rb') as f:
        payload = f.read()
    marker = os.path.join(directory, NAME + '.session.json')
    # No files from a previous/manual installation may be adopted or overwritten.
    if os.path.lexists(placed_dir(game)):
        raise RuntimeError(f'an earlier run\'s placed files are not put back ({placed_dir(game)}): '
                           'python tests/autopilot/drive.py restore (or python tools/perfrun.py --restore)')
    for ext in SESSION_EXTS:
        if os.path.lexists(os.path.join(directory, NAME + ext)):
            raise RuntimeError('Existing autopilot files must be preserved: ' + NAME + ext +
                               ' (a run that left it: python tests/autopilot/drive.py restore)')
    with open(marker, 'x', encoding='utf-8') as f:
        json.dump({'sha256': hashlib.sha256(payload).hexdigest()}, f)
    created = False
    dll = os.path.join(directory, NAME + '.dll')
    try:
        with open(dll, 'xb') as f:
            created = True
            f.write(payload)
    except BaseException:
        if created:
            os.remove(dll)
        os.remove(marker)
        raise
    print('installed', os.path.join(directory, NAME + '.dll'))


def uninstall(game: str) -> None:
    require_closed()
    directory = plugins(game)
    marker = os.path.join(directory, NAME + '.session.json')
    if not os.path.isfile(marker):
        raise RuntimeError('No owned autopilot session: existing files left untouched')
    with open(marker, encoding='utf-8') as f:
        expected = json.load(f)['sha256']
    dll = os.path.join(directory, NAME + '.dll')
    if os.path.exists(dll):
        with open(dll, 'rb') as f:
            if hashlib.sha256(f.read()).hexdigest() != expected:
                raise RuntimeError('Autopilot DLL changed: files left untouched')
    for ext in SESSION_EXTS:
        path = os.path.join(directory, NAME + ext)
        if os.path.exists(path):
            os.remove(path)
            print('removed', path)
    os.remove(marker)


def placed_dir(game: str) -> str:
    """Where a run's placements are recorded until put back: the originals and manifest.json (place)."""
    return os.path.join(plugins(game), NAME + '.placed')


def _manifest(game: str) -> list[dict]:
    try:
        with open(os.path.join(placed_dir(game), 'manifest.json'), encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        return []


def _write_manifest(game: str, entries: list[dict]) -> None:
    path = os.path.join(placed_dir(game), 'manifest.json')
    if not entries:
        if os.path.exists(path):
            os.remove(path)
        return
    os.makedirs(placed_dir(game), exist_ok=True)
    with open(path + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(entries, f, indent=1)
    os.replace(path + '.tmp', path)


def _sha(path: str) -> str | None:
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except FileNotFoundError:
        return None


def place_file(game: str, dest: str, payload: bytes, *, new: bool = False) -> None:
    """`payload` put at <game>/Mods/`dest` for one run (`--place`, `--ini`, `--loadout`; `new`: only where no file is,
    FileExistsError otherwise). A transaction on disk: the original (when there is one) is copied into placed_dir and
    the placement recorded in its manifest.json (dest, the original's and the payload's sha256) before the file is
    written, so whoever puts things back (restore_placed: the run's own end, or `drive.py restore` / `perfrun
    --restore` after a game that outlived the run) works from the disk, not from a driver that may be gone."""
    path = os.path.join(game, 'Mods', dest)
    entries = _manifest(game)
    if any(os.path.normcase(e['dest']) == os.path.normcase(dest) for e in entries):
        raise RuntimeError(f'{dest} is already placed for this run')
    if new and os.path.lexists(path):
        raise FileExistsError(path)
    os.makedirs(placed_dir(game), exist_ok=True)
    original = _sha(path)
    kept = None
    if original is not None:
        kept = dest.replace('/', '__').replace('\\', '__')
        if os.path.lexists(os.path.join(placed_dir(game), kept)):
            raise FileExistsError(os.path.join(placed_dir(game), kept))
    # The folders this placement makes (none there yet), outermost first, under Mods: taken back with it (_unmake).
    made, up = [], os.path.dirname(dest.replace('\\', '/'))
    while up and not os.path.isdir(os.path.join(game, 'Mods', up)):
        made.insert(0, up)
        up = os.path.dirname(up)
    # The entry (with its kept name) is on disk before the copy: no file in placed_dir is ever unrecorded. An entry
    # whose copy or payload never came is harmless to restore_placed (the file still holds its original).
    entries.append({'dest': dest, 'kept': kept, 'original_sha256': original,
                    'payload_sha256': hashlib.sha256(payload).hexdigest(), 'made_dirs': made})
    _write_manifest(game, entries)
    try:
        if kept:
            shutil.copy2(path, os.path.join(placed_dir(game), kept))   # bytes and mtime
        os.makedirs(os.path.dirname(path), exist_ok=True)
        f = open(path, 'xb' if new else 'wb')
    except BaseException:
        # nothing of the payload written: this placement never happened (its copy and its entry taken back)
        if kept and os.path.lexists(os.path.join(placed_dir(game), kept)):
            os.remove(os.path.join(placed_dir(game), kept))
        _unmake(game, entries.pop())
        _write_manifest(game, entries)
        if not entries and not os.listdir(placed_dir(game)):
            os.rmdir(placed_dir(game))
        raise
    with f:   # a write failing from here on leaves the entry: restore_placed puts the original back
        f.write(payload)
    print('placed', dest, 'over the original' if original is not None else '(new)')


def replace_placed(game: str, dest: str, payload: bytes) -> None:
    """A file this run already placed (place_file) rewritten in place mid-run, e.g. the ini for a hot-reloaded switch
    (tools/perfrun.py's stay legs): its manifest entry takes the new payload's sha256 first, so restore_placed still
    puts the original back (it keeps only a file still holding the payload it recorded)."""
    entries = _manifest(game)
    entry = next((e for e in entries if os.path.normcase(e['dest']) == os.path.normcase(dest)), None)
    if entry is None:
        raise RuntimeError(f'{dest} was not placed for this run')
    entry['payload_sha256'] = hashlib.sha256(payload).hexdigest()
    _write_manifest(game, entries)
    path = os.path.join(game, 'Mods', dest)
    with open(path + '.tmp', 'wb') as f:
        f.write(payload)
    os.replace(path + '.tmp', path)


def _unmake(game: str, entry: dict) -> None:
    """The folders `entry`'s placement made (place_file's made_dirs) removed, innermost first, each only while empty
    (something else put in it since: it and those over it stay)."""
    for rel in reversed(entry.get('made_dirs', [])):
        path = os.path.join(game, 'Mods', rel)
        if not os.path.isdir(path) or os.listdir(path):
            return
        os.rmdir(path)
        print('removed the folder', path)


def place_loadout(game: str, source: str) -> None:
    """The run's forced loadout written as EDF6VehicleCrew's ini; never over an existing one (the test range's)."""
    with open(source, 'rb') as f:
        payload = f.read()
    place_file(game, os.path.join('Plugins', LOADOUT), payload, new=True)
    print('loadout', source, '->', os.path.join(plugins(game), LOADOUT))


def restore_placed(game: str) -> bool:
    """Everything a run placed put back, from the manifest on disk (place_file), last placed first, then the autopilot
    uninstalled (when its session is there): a file still holding the payload gets its original back byte for byte
    (mtime too) or is removed when there was none; one already holding the original needs nothing; one changed by
    someone else is left as it is, with its original and its manifest entry kept (said so). The game must be closed.
    True when nothing is left."""
    require_closed()
    left = []
    for e in reversed(_manifest(game)):
        path = os.path.join(game, 'Mods', e['dest'])
        kept = os.path.join(placed_dir(game), e['kept']) if e['kept'] else None
        now = _sha(path)
        if now == e['payload_sha256'] and now != e['original_sha256']:
            if kept is None:
                os.remove(path)
                print('removed', path)
            elif _sha(kept) == e['original_sha256']:
                shutil.copy2(kept, path)
                print('restored', path, 'byte for byte')
            else:
                print('the kept original is missing or changed: left as it is:', path)
                left.append(e)
                continue
        elif now != e['original_sha256']:
            print('changed by someone else during the run: left as it is:', path,
                  f'(its original kept at {kept})' if kept else '')
            left.append(e)
            continue
        if kept and os.path.exists(kept):
            os.remove(kept)
        _unmake(game, e)
    left.reverse()
    _write_manifest(game, left)
    if os.path.exists(os.path.join(placed_dir(game), 'manifest.json.tmp')):   # a manifest write that never finished
        os.remove(os.path.join(placed_dir(game), 'manifest.json.tmp'))
    stray = unrecorded_placed(game, left)
    for name in stray:
        print('!!! not in the manifest, left as it is:', os.path.join(placed_dir(game), name),
              '(an original kept by an earlier run: an older drive.py that recorded none, or a run killed while '
              f'placing; it was Mods/{name.replace("__", "/")}): put it back or delete it by hand, then restore again')
    if not left and not stray and os.path.isdir(placed_dir(game)):
        os.rmdir(placed_dir(game))
    if os.path.isfile(os.path.join(plugins(game), NAME + '.session.json')):
        uninstall(game)
    return not left and not stray


def anything_left(game: str) -> bool:
    """A run's placements (placed_dir) or its autopilot (its session) still in the game folder: restore_placed's work."""
    return os.path.lexists(placed_dir(game)) or os.path.isfile(os.path.join(plugins(game), NAME + '.session.json'))


def unrecorded_placed(game: str, entries: list[dict]) -> list[str]:
    """The files in placed_dir that `entries` (its manifest) do not account for: manifest.json and the kept originals
    are; anything else (an older drive.py's backups had no manifest) is an original nobody knows where to put back."""
    try:
        names = os.listdir(placed_dir(game))
    except FileNotFoundError:
        return []
    known = {'manifest.json'} | {e['kept'] for e in entries if e['kept']}
    return sorted(n for n in names if n not in known)


def ini_with(game: str, settings: tuple[str, ...]) -> bytes:
    """EDF6VehicleCrew.ini with `KEY=VALUE` settings set in [VehicleCrew] (tools/support_config.put), its encoding kept."""
    with open(os.path.join(plugins(game), 'EDF6VehicleCrew.ini'), 'rb') as f:
        return ini_text_with(f.read(), settings)


def ini_text_with(raw: bytes, settings: tuple[str, ...]) -> bytes:
    """ini_with on an ini's bytes `raw`."""
    if TOOLS not in sys.path:
        sys.path.insert(0, TOOLS)
    import support_config
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = raw[3 if bom else 0:].decode('utf-8')
    for setting in settings:
        key, _, value = setting.partition('=')
        text = support_config.put(text, key.strip(), value.strip())
    return (b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8')


# The game below normal priority (the user, 2026-11-28: 「测试 ... 还会把我的电脑弄得很卡」): a bench of thousands of
# enemies keeps every core busy, the desktop and the editor take them back whenever they want them. On an idle machine
# the game's throughput is the same; a run's A/B halves alternate in one launch (perfrun's stay legs), so both see the
# same machine. perfrun.py --normal-priority keeps the game normal.
BELOW_NORMAL = True


def below_normal(pid: int) -> None:
    """EDF6.exe `pid` in BELOW_NORMAL_PRIORITY_CLASS (its threads' own relative priorities kept)."""
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x0200, False, pid)   # PROCESS_SET_INFORMATION
    if not h:
        print(f'below normal priority: EDF6.exe {pid} not opened (error {k.GetLastError()}), left normal')
        return
    try:
        if k.SetPriorityClass(h, 0x4000):
            print(f'EDF6.exe {pid} below normal priority')
        else:
            print(f'below normal priority: refused (error {k.GetLastError()}), left normal')
    finally:
        k.CloseHandle(h)


def launch(game: str) -> gamelaunch.Game:
    """The game started straight from its folder (pylib/gamelaunch.py: LaunchGame.exe as Steam starts it, never
    steam://, so no Steam dialog can hold it), its window off screen as soon as it shows and the foreground given
    back. gamelaunch.LaunchFailed, said at once, when it cannot start; what it started is closed again."""
    require_closed()
    before = user32.GetForegroundWindow()

    def off_screen(hwnd: int) -> None:
        hide(hwnd)
        user32.SetForegroundWindow(before)
    started = gamelaunch.launch(game, on_window=off_screen)
    if BELOW_NORMAL:
        below_normal(started.pid)
    print('window', hex(started.hwnd), f'off screen (EDF6.exe {started.pid}, started by {gamelaunch.LAUNCHER})')
    return started


class RunEnded(RuntimeError):
    """A write into the game folder refused: the run's end began (Gate)."""


class Gate:
    """The game folder's writes by run's `during` thread (tools/perfrun.py's legs: the `next` commands, the keys, the
    range's files) against the run's own end, one lock for both. The run's end goes OPEN -> QUITTING (its `quit`
    written under the lock: no command, key press or file after it, so nothing overwrites the quit) -> CLOSED (before
    anything is put back: no write at all, a key's release included, so nothing is left behind once restore_placed /
    uninstall ran). A writer checks and writes under the lock (writing), so a write is either wholly before the end
    began or never happens."""
    OPEN, QUITTING, CLOSED = 0, 1, 2

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.state = Gate.OPEN

    def end(self, state: int, then: Callable[[], None] | None = None) -> None:
        """At least `state` from now on, and `then` (the quit command) done under the same lock."""
        with self.lock:
            self.state = max(self.state, state)
            if then:
                then()

    def writing(self, release: bool = False):  # noqa: ANN201 - a context manager holding the lock
        """The lock held for one write: RunEnded once the end began (a key's release: once CLOSED only)."""
        gate = self

        class _Held:
            def __enter__(self) -> None:
                gate.lock.acquire()
                if gate.state >= (Gate.CLOSED if release else Gate.QUITTING):
                    gate.lock.release()
                    raise RunEnded('the run ended first: nothing written into the game folder')

            def __exit__(self, *exc: object) -> None:
                gate.lock.release()
        return _Held()


def replace_open(src: str, dst: str) -> None:
    """`src` renamed over `dst` atomically while the autopilot may hold `dst` open: its keys poll reads it with
    FILE_SHARE_DELETE, which os.replace (MoveFileEx) still refuses (WinError 5, 2026-11-26), but a POSIX-semantics
    rename (FileRenameInfoEx) takes: the reader keeps the old file until it closes it."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = wt.HANDLE
    kernel.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
    kernel.SetFileInformationByHandle.argtypes = [wt.HANDLE, ctypes.c_int, wt.LPVOID, wt.DWORD]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    target = os.path.normpath(os.path.abspath(dst))
    # DELETE access, every share mode, OPEN_EXISTING
    handle = kernel.CreateFileW(os.path.abspath(src), 0x00010000, 7, None, 3, 0, None)
    if handle in (None, wt.HANDLE(-1).value):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        # FILE_RENAME_INFO with the name as WCHARs (a c_char array would stop at the UTF-16's first zero byte)
        class RenameInfo(ctypes.Structure):
            _fields_ = [('flags', wt.DWORD), ('root', wt.HANDLE), ('length', wt.DWORD),
                        ('name', ctypes.c_wchar * (len(target) + 1))]
        info = RenameInfo(0x1 | 0x2, None, 2 * len(target), target)   # REPLACE_IF_EXISTS | POSIX_SEMANTICS
        if not kernel.SetFileInformationByHandle(handle, 22, ctypes.byref(info), ctypes.sizeof(info)):   # FileRenameInfoEx
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def key(game: str, spec: str, ms: int, gate: Gate | None = None) -> None:
    """Holds `spec` `ms` ms (the autopilot's keys file). With `gate` (a run's `during`): pressed only before the run's
    end began, released unless everything is already being put back (then uninstall removes the keys file)."""
    codes = [KEYS[k.lower()] if k.lower() in KEYS else int(k, 16) for k in spec.split('+')]
    if not os.path.isfile(os.path.join(plugins(game), NAME + '.session.json')) or not game_pids():
        # A keys file written with no session would be left behind and block the next install.
        raise RuntimeError('no autopilot session with the game running: no keys written')
    path = os.path.join(plugins(game), NAME + '.keys')
    def write(text: str) -> None:
        with open(path + '.tmp', 'w') as f:
            f.write(text)
        replace_open(path + '.tmp', path)
    def release() -> None:
        if gate is None:
            write('')
            return
        try:
            with gate.writing(release=True):
                write('')
        except RunEnded:
            return   # CLOSED: the files are being put back (uninstall removes the keys file); nothing written now
    if gate is None:
        write(' '.join(f'{c:02x}' for c in codes))
    else:
        with gate.writing():
            write(' '.join(f'{c:02x}' for c in codes))
    try:
        time.sleep(ms / 1000)
    finally:
        release()
    time.sleep(0.15)


def shot(out: str, hwnd: int | None = None) -> None:
    hwnd = hwnd or game_window()
    assert hwnd, 'no game window'
    img = capture(hwnd)
    size = img.size
    img.thumbnail((1280, 720))
    img.save(out)
    print('saved', out, f'{size[0]}x{size[1]}')


def capture(hwnd: int):  # noqa: ANN201 - PIL.Image
    """The window's client as PrintWindow renders it (never the screen: covered or off screen is fine)."""
    from PIL import Image
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom
    if not w or not h:
        raise RuntimeError('window has no client area')
    hdc = user32.GetDC(hwnd)
    mem = bmp = previous = None
    try:
        if not hdc:
            raise ctypes.WinError(ctypes.get_last_error())
        mem = gdi32.CreateCompatibleDC(hdc)
        bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
        if not mem or not bmp:
            raise RuntimeError('could not allocate window capture bitmap')
        previous = gdi32.SelectObject(mem, bmp)
        if not previous or previous == ctypes.c_void_p(-1).value:
            previous = None
            raise RuntimeError('could not select window capture bitmap')
        if not user32.PrintWindow(hwnd, mem, 3):
            raise RuntimeError('PrintWindow failed')
        # GetDIBits requires a bitmap that is not currently selected into a DC.
        gdi32.SelectObject(mem, previous)
        previous = None
        buf = ctypes.create_string_buffer(w * h * 4)

        class Header(ctypes.Structure):
            _fields_ = [('size', wt.DWORD), ('w', wt.LONG), ('h', wt.LONG), ('planes', wt.WORD), ('bits', wt.WORD),
                        ('comp', wt.DWORD), ('img', wt.DWORD), ('x', wt.LONG), ('y', wt.LONG), ('used', wt.DWORD),
                        ('imp', wt.DWORD)]
        hdr = Header(ctypes.sizeof(Header), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
        if gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(hdr), 0) != h:
            raise RuntimeError('GetDIBits did not return the complete image')
        return Image.frombuffer('RGBA', (w, h), buf, 'raw', 'BGRA', 0, 1).convert('RGB')
    finally:
        if previous:
            gdi32.SelectObject(mem, previous)
        if bmp:
            gdi32.DeleteObject(bmp)
        if mem:
            gdi32.DeleteDC(mem)
        if hdc:
            user32.ReleaseDC(hwnd, hdc)


def command(game: str, text: str, gate: Gate | None = None) -> None:
    """Hands the plugin one command (<dll>.cmd, cleared by it once run). With `gate` (a run's `during`): only before
    the run's end began (RunEnded otherwise), checked and written under its lock."""
    if gate is not None:
        with gate.writing():
            command(game, text)
        return
    path = os.path.join(plugins(game), NAME + '.cmd')
    with open(path + '.tmp', 'w') as f:
        f.write(text)
    os.replace(path + '.tmp', path)


def mem(game: str, n: int) -> None:
    with open(os.path.join(plugins(game), NAME + '.log'), encoding='utf-8', errors='replace') as f:
        rows = f.read().splitlines()
    for row in rows[-n:]:
        print(row)


def rows(game: str) -> list[str]:
    try:
        with open(os.path.join(plugins(game), NAME + '.log'), encoding='utf-8', errors='replace') as f:
            return f.read().splitlines()
    except OSError:
        return []


def wait_for(game: str, text: str, seconds: float) -> bool:
    """TEXT in the autopilot's log within SECONDS; False at once when the game exits or shows a message box (its
    text printed: an error the run would otherwise only time out on)."""
    end = time.time() + seconds
    while time.time() < end:
        if any(text in r for r in rows(game)):
            return True
        pids = game_pids()
        if not pids:
            return False
        boxes = [w for w in gamelaunch.windows(set(pids)) if w[2] == gamelaunch.DIALOG_CLASS]
        if boxes:
            print('the game shows a message box:', ' | '.join(gamelaunch.dialog_text(w[0]) for w in boxes))
            return False
        time.sleep(1)
    return False


def commit_of(row: str) -> float:
    return float(row.split('commit ')[1].split(' MB')[0])


def run(game: str, mission: str, difficulty: int, seconds: int, keep: str | None, *,
        extra: tuple[str, ...] = (), loadout: str | None = None, shots: str | None = None,
        keys: tuple[tuple[str, float, int], ...] = (), place: tuple[str, ...] = (), ini: tuple[str, ...] = (),
        hq: bool = False, every: float = 15.0, during: Callable[[], None] | None = None,
        quit_wait: int = 90, cycle: tuple[int, float, bool] | None = None, gate: Gate | None = None) -> int:
    """`during`: called on a thread of its own once the mission has started (tools/perfrun.py samples the game there);
    the run quits as soon as it returns, SECONDS then only the longest stay. `gate`: what `during` writes into the game
    folder goes through it (command / key with gate=, the range's files under gate.writing()): the run's `quit` ends it
    (QUITTING), and before anything is put back it is CLOSED, so `during` (a daemon thread, never joined: it may be
    waiting on a sampler) cannot overwrite the quit nor leave a file behind after the restore.
    `cycle` (N, STAY, REBOOT): the plugin's `cycle` command handed with the mission; the run quits once it logs
    CYCLE done, and fails when SECONDS pass first or the cycle failed.
    Everything placed is put back at the end (restore_placed); non-zero too when anything is left (the game still
    running, or restore_placed left a file), which placed_dir on disk then records for `drive.py restore`."""
    put_back = [False]
    rc = _session(game, mission, difficulty, seconds, keep, put_back, extra=extra, loadout=loadout, shots=shots,
                  keys=keys, place=place, ini=ini, hq=hq, every=every, during=during, quit_wait=quit_wait, cycle=cycle,
                  gate=gate or Gate())
    return rc if put_back[0] else rc or 1


def _session(game: str, mission: str, difficulty: int, seconds: int, keep: str | None, put_back: list[bool], *,
             extra: tuple[str, ...], loadout: str | None, shots: str | None, keys: tuple[tuple[str, float, int], ...],
             place: tuple[str, ...], ini: tuple[str, ...], hq: bool, every: float,
             during: Callable[[], None] | None, quit_wait: int, cycle: tuple[int, float, bool] | None,
             gate: Gate) -> int:
    """run's launch and stay; put_back[0] set once everything placed is put back (its finally)."""
    install(game)
    watch = DesktopWatch().start()
    try:
        if loadout:
            place_loadout(game, loadout)
        for item in place:
            source, _, dest = item.partition('=')
            with open(source, 'rb') as f:
                place_file(game, dest, f.read())
        if ini:
            place_file(game, 'Plugins/EDF6VehicleCrew.ini', ini_with(game, ini))
        command(game, '\n'.join([f'mission {mission} {difficulty}' + (' hq' if hq else ''), *extra,
                                 *([cycle_command(*cycle)] if cycle else [])]))
        try:
            started = launch(game)
        except gamelaunch.LaunchFailed as e:
            print('the game did not start:', e)
            return 1
        if not wait_for(game, 'SCRIPT HQMain stays' if hq else 'PlayMission_Offline', 240):
            print('the HQ menu never came (see the log)' if hq else 'the mission never started (see the log)')
            # The game this run started (a message box up, e.g. 7_0 "a file is missing", or stuck loading): closed
            # again, so what was placed is put back below instead of staying behind a game nobody plays.
            if isinstance(started, gamelaunch.Game):
                gamelaunch.abandon(game, started.pid, None)
            return 1
        print('the HQ menu shown' if hq else 'mission started', '; staying', seconds, 's')
        start = time.time()
        end = start + seconds
        next_shot = start + every
        nudge_at = start + min(20, seconds / 2)
        pending = sorted(keys, key=lambda k: k[1])
        worker = threading.Thread(target=during, daemon=True) if during else None
        if worker:
            worker.start()
        cycled: str | None = None   # with `cycle`: how it ended (done / failed), None while it runs
        next_look = start
        while (now := time.time()) < end and not (worker and not worker.is_alive()):
            if cycle and now >= next_look:
                cycled = cycle_outcome(rows(game))
                if cycled:
                    break
                next_look = now + 2
            if not game_pids():
                print('the game exited before the observation period ended')
                return 1
            if nudge_at and now >= nudge_at:
                print('real mouse check:', watch.nudge())
                nudge_at = 0
            if shots and now >= next_shot:
                os.makedirs(shots, exist_ok=True)
                try:
                    shot(os.path.join(shots, f'{now - start:06.1f}s.png'))
                except (RuntimeError, AssertionError) as e:
                    print('no picture:', e)
                next_shot += every
            while pending and now >= start + pending[0][1]:
                spec, at, ms = pending.pop(0)
                hold_keys(game, spec, ms, shots, f'{int(at):04d}s-{spec.replace("+", "_")}')
            time.sleep(min(1.0, every / 2))
        if not game_pids():
            print('the game exited before quit was requested')
            return 1
        if cycle:
            log = rows(game)
            cycled = cycled or cycle_outcome(log)
            print(cycle_report(log, cycle[0], cycled))
        # under the gate's lock: no `next` / key / range file of `during` after it (nothing overwrites the quit)
        gate.end(Gate.QUITTING, lambda: command(game, 'quit'))
        for _ in range(quit_wait):   # (a game at a few fps takes minutes to leave a mission)
            if not game_pids():
                break
            time.sleep(1)
        log = rows(game)
        mem_rows = [r for r in log if ' MEM ' in r]
        start = next((r for r in log if 'MEM mission start' in r), None)
        peak = max((commit_of(r) for r in mem_rows), default=0.0)
        peak_rows = [r for r in mem_rows if '(peak ' in r]
        reported_peak = max((float(r.split('(peak ')[1].split(')')[0]) for r in peak_rows), default=0.0)
        print('at the mission start:', start)
        print(f'commit peak sampled {peak:.0f} MB, process peak commit {reported_peak:.0f} MB')
        print('last:', mem_rows[-1] if mem_rows else None)
        if game_pids():
            print(f'the game did not exit in {quit_wait} s: left running, the plugin stays installed')
            return 1
        return 0 if not cycle or cycled == 'done' else 1
    finally:
        # before anything is copied or put back: `during` writes nothing more (a write under way finishes first)
        gate.end(Gate.CLOSED)
        watch.stop()
        print(watch.report())
        try:
            if keep and os.path.exists(os.path.join(plugins(game), NAME + '.log')):
                shutil.copyfile(os.path.join(plugins(game), NAME + '.log'), keep)
                print('log kept at', keep)
        finally:
            if game_pids():
                print('!!! the game is still running: the autopilot and the placed files stay '
                      f'({placed_dir(game)}); python tests/autopilot/drive.py restore once the game is closed')
            elif restore_placed(game):
                put_back[0] = True
            else:
                print('!!! not everything placed is put back (said above): it stays recorded in '
                      f'{placed_dir(game)}; python tests/autopilot/drive.py restore once it is dealt with')


def cycle_command(count: int, stay: float, reboot: bool) -> str:
    """The plugin's `cycle` command (tests/autopilot/autopilot.cpp AskCycle)."""
    return f'cycle {count} {stay:g}' + (' reboot' if reboot else '')


def next_command(mission: str, difficulty: int, reboot: bool = False) -> str:
    """The plugin's `next` command (tests/autopilot/autopilot.cpp AskNext): leave the mission running now and enter
    MISSION (a `mission` command's name: ROW, RM015, M001, range, ...) in the same launch."""
    return f'next {mission} {difficulty}' + (' reboot' if reboot else '')


def next_outcome(log: list[str], k: int) -> str | None:
    """How the launch's k-th `next` ended, from the autopilot log rows since it was handed: 'entered' (the plugin's
    "NEXT k entered" row: the new mission runs), 'failed' (refused, the pack's mode not entered, or the script went
    back to the title), None while it is on its way."""
    how = entry_outcome(log, k)
    return how and how[0]


# The plugin's rows once a mission runs (MissionMgr says so): "NEXT k entered" for the k-th `next`, "RUNNING" for the
# launch's first (the `running` command) -- what the game state holds then.
ENTERED = re.compile(r' (?:NEXT (\d+) entered|RUNNING): the mission runs '
                     r'\(row (-?\d+), difficulty (-?\d+), mode (-?\d+), content id (-?\d+)\)')
# The pack's mode made current (or already) on the way in (autopilot.cpp EnterRangeMode).
RANGE_MODE = re.compile(r' RANGE mode (-?\d+) \(content id (-?\d+)\) (?:made|already) current')
# The way in failed: refused, the pack's mode not entered (RANGE ... rows, then this; the menu stays, no mission
# comes), no game state, the script back at the title.
ENTRY_FAILED = (' NEXT refused', ' RUNNING refused', ' CYCLE failed', " SCRIPT HQMain: the pack's mode not entered",
                ' SCRIPT HQMain: no game state')


def entry_outcome(log: list[str], k: int) -> tuple[str, dict | str] | None:
    """How the launch's k-th mission entry went (k 0: the first, its RUNNING row; k 1...: the k-th `next`), from the
    autopilot log rows since it was asked: ('entered', what the mission runs with: row, difficulty, mode, content and
    the mode the RANGE row before it made current, None without one), ('failed', the row saying so), None while it is
    on its way."""
    mode = None
    for row in log:
        m = RANGE_MODE.search(row)
        if m:
            mode = int(m[1])
            continue
        m = ENTERED.search(row)
        if m and (m[1] is None if k == 0 else m[1] == str(k)):
            return 'entered', {'row': int(m[2]), 'difficulty': int(m[3]), 'mode': int(m[4]), 'content': int(m[5]),
                               'range_mode': mode}
        if any(f in row for f in ENTRY_FAILED):
            return 'failed', row.strip()
    return None


def pack_content(game: str, key: str) -> int:
    """[VehicleCrew] `key` of the EDF6VehicleCrew.ini beside the plugins (the installer's TestRangeContent,
    EDF41Content, EDF5CampaignContent), as the autopilot reads it (ReadPackContent: GetPrivateProfileInt, 0 when absent
    or past 65535)."""
    try:
        with open(os.path.join(plugins(game), 'EDF6VehicleCrew.ini'), encoding='utf-8-sig', errors='replace') as f:
            lines = f.read().splitlines()
    except OSError:
        return 0
    section = ''
    for line in lines:
        s = line.strip()
        if s.startswith('[') and s.endswith(']'):
            section = s[1:-1].strip().lower()
        elif section == 'vehiclecrew' and '=' in s:
            name, _, value = s.partition('=')
            if name.strip().lower() == key.lower():
                m = re.match(r'\s*(\d+)', value)
                n = int(m[1]) if m else 0
                return n if n <= 65535 else 0
    return 0


def mission_target(game: str, mission: str) -> tuple[int, int]:
    """The row and the mode's content id a `mission` / `next` name enters (autopilot.cpp AskMission / AskNext):
    range row 0 (range:ROW row ROW: the PvP presets) of TestRangeContent's mode, edf41:ROW / edf41mp01:ROW / edf41mp02:ROW of EDF41Content (+1, +2),
    edf5:ROW of EDF5CampaignContent, a stock row (RM015 13, M001 1, a number) of the stock list (content id 0)."""
    low = mission.lower()
    if low == 'range' or low.startswith('range:'):
        return (int(mission[6:]) if low.startswith('range:') else 0), pack_content(game, 'TestRangeContent')
    for prefix, extra in (('edf41:', 0), ('edf41mp01:', 1), ('edf41mp02:', 2)):
        if low.startswith(prefix):
            first = pack_content(game, 'EDF41Content')
            return int(mission[len(prefix):]), first + extra if first > 0 else first
    if low.startswith('edf5:'):
        return int(mission[5:]), pack_content(game, 'EDF5CampaignContent')
    return (13 if low == 'rm015' else 1 if low == 'm001' else int(mission)), 0


def entry_mismatch(got: dict, row: int, difficulty: int, content: int) -> str | None:
    """Why the mission running is not the one asked (row, difficulty, the mode's content id, the mode the way in made
    current), None when it is."""
    bad = []
    if got['row'] != row:
        bad.append(f'row {got["row"]}, not {row}')
    if got['difficulty'] != difficulty:
        bad.append(f'difficulty {got["difficulty"]}, not {difficulty}')
    if got['content'] != content:
        bad.append(f'content id {got["content"]}, not {content}')
    if got['range_mode'] is not None and got['mode'] != got['range_mode']:
        bad.append(f'mode {got["mode"]}, not the {got["range_mode"]} made current on the way in')
    return '; '.join(bad) or None


def cycle_outcome(log: list[str]) -> str | None:
    """How the plugin's cycle ended, from its log: 'done', 'failed' (the script went back to the title, or the command
    was refused), None while it runs."""
    for row in log:
        if ' CYCLE done' in row:
            return 'done'
        if ' CYCLE failed' in row or ' CYCLE refused' in row:
            return 'failed'
    return None


def cycle_report(log: list[str], count: int, outcome: str | None) -> str:
    """One line on the cycle: how many times the mission was left and entered, and how it ended."""
    left = sum(1 for r in log if re.search(r' CYCLE \d+/\d+ left', r))
    entered = sum(1 for r in log if re.search(r' CYCLE \d+/\d+ entered', r))
    how = {'done': 'done', 'failed': 'FAILED (see the CYCLE rows)'}.get(outcome or '', 'NOT finished in time')
    return f'cycle {how}: left {left} of {count}, entered {entered} (the first mission included)'


def hold_keys(game: str, spec: str, ms: int, shots: str | None, name: str) -> None:
    """A scheduled key hold of `run`, with the window's picture just before, halfway through the hold (what the held
    input moves, while it is still held) and just after when pictures are kept."""
    def picture(when: str) -> None:
        if not shots:
            return
        os.makedirs(shots, exist_ok=True)
        try:
            shot(os.path.join(shots, f'{name}-{when}.png'))
        except (RuntimeError, AssertionError) as e:
            print('no picture:', e)
    picture('before')
    print('holding', spec, 'for', ms, 'ms')
    during = threading.Timer(ms / 2000, picture, ('during',)) if shots else None
    if during:
        during.start()
    try:
        key(game, spec, ms)
    finally:
        if during:
            during.join()
    picture('after')


def key_option(text: str) -> tuple[str, float, int]:
    """--key SPEC@SECONDS[:MS]: hold SPEC (as `key` takes it) SECONDS into the mission, for MS (default 1000)."""
    try:
        spec, when = text.split('@', 1)
        at, _, ms = when.partition(':')
        seconds, hold = float(at), int(ms) if ms else 1000
        # float() takes nan, inf and -5 too: a hold at no time (or never) is refused, not slept on
        if not spec or not math.isfinite(seconds) or seconds < 0 or hold <= 0:
            raise ValueError(text)
        return spec, seconds, hold
    except ValueError:
        raise SystemExit('--key wants SPEC@SECONDS[:MS], e.g. 57@25:5000') from None


def run_options(argv: list[str]) -> tuple[list[str], dict]:
    """`run`'s positional arguments and its options (--cmd TEXT and --key SPEC@SECONDS[:MS] repeatable,
    --loadout FILE, --shots DIR)."""
    args: list[str] = []
    extra: list[str] = []
    keys: list[tuple[str, float, int]] = []
    place: list[str] = []
    ini: list[str] = []
    options: dict = {'extra': (), 'loadout': None, 'shots': None, 'keys': (), 'place': (), 'ini': (), 'hq': False, 'every': 15.0}
    cycle_count, cycle_stay, cycle_reboot = 0, 20.0, False
    i = 0
    while i < len(argv):
        if argv[i] in ('--hq', '--cycle-reboot'):
            if argv[i] == '--hq':
                options['hq'] = True
            else:
                cycle_reboot = True
            i += 1
            continue
        if argv[i] in ('--cmd', '--loadout', '--shots', '--key', '--place', '--ini', '--every', '--cycle',
                       '--cycle-stay'):
            if i + 1 >= len(argv):
                raise SystemExit(argv[i] + ' needs a value')
            if argv[i] == '--cmd':
                extra.append(argv[i + 1])
            elif argv[i] == '--key':
                keys.append(key_option(argv[i + 1]))
            elif argv[i] == '--place':
                if '=' not in argv[i + 1]:
                    raise SystemExit('--place wants SOURCE=DEST (DEST under Mods, e.g. Plugins/EDF6VehicleCrew.dll)')
                place.append(argv[i + 1])
            elif argv[i] == '--every':
                options['every'] = float(argv[i + 1])
                if not options['every'] > 0:
                    raise SystemExit('--every wants a number of seconds over 0')
            elif argv[i] == '--ini':
                if '=' not in argv[i + 1]:
                    raise SystemExit('--ini wants KEY=VALUE')
                ini.append(argv[i + 1])
            elif argv[i] == '--cycle':
                try:
                    cycle_count = int(argv[i + 1])
                except ValueError:
                    cycle_count = 0
                if not 1 <= cycle_count <= 10000:
                    raise SystemExit('--cycle wants a count from 1 to 10000')
            elif argv[i] == '--cycle-stay':
                try:
                    cycle_stay = float(argv[i + 1])
                except ValueError:
                    cycle_stay = 0.0
                if not 1 <= cycle_stay <= 3600:
                    raise SystemExit('--cycle-stay wants seconds from 1 to 3600')
            else:
                options[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    options['extra'] = tuple(extra)
    options['keys'] = tuple(keys)
    options['place'] = tuple(place)
    if options['hq'] and not any(s.partition('=')[0].strip().lower() == 'hqautomissionselect' for s in ini):
        ini.append('HqAutoMissionSelect=0')   # the HQ menu itself, not the mission select the plugin opens over it
    options['ini'] = tuple(ini)
    if (cycle_stay != 20.0 or cycle_reboot) and not cycle_count:
        raise SystemExit('--cycle-stay / --cycle-reboot go with --cycle N')
    if cycle_count and options['hq']:
        raise SystemExit('--cycle leaves a mission: not with --hq')
    if cycle_count:
        options['cycle'] = (cycle_count, cycle_stay, cycle_reboot)
    return args, options


# What touches the game folder or the running game: done holding the machine's game lease (pylib/gamelease.py), so
# two sessions never install over / drive each other's run. `mem` and `shot` only read.
LEASED = ('install', 'uninstall', 'restore', 'launch', 'hide', 'key', 'cmd', 'run')


def wants_help(argv: list[str]) -> bool:
    """-h / --help anywhere (`run --help` too): the usage only, nothing installed or launched."""
    return any(a in ('-h', '--help', 'help') for a in argv[:1]) or any(a in ('-h', '--help') for a in argv[1:])


def main(argv: list[str]) -> int:
    if wants_help(argv):
        print(__doc__)
        return 0
    cmd = argv[0] if argv else ''
    if cmd not in LEASED:
        return command_main(argv)
    with gamelease.lease(gamedir.find_or_dev(), 'drive.py ' + ' '.join(argv)):
        return command_main(argv)


def command_main(argv: list[str]) -> int:
    game = gamedir.find_or_dev()
    cmd = argv[0] if argv else ''
    if cmd == 'install':
        install(game)
    elif cmd == 'uninstall':
        uninstall(game)
    elif cmd == 'restore':
        return 0 if restore_placed(game) else 1
    elif cmd == 'launch':
        launch(game)
    elif cmd == 'hide':
        hwnd = game_window()
        assert hwnd, 'no game window'
        hide(hwnd)
    elif cmd == 'key':
        key(game, argv[1], int(argv[2]) if len(argv) > 2 else 150)
    elif cmd == 'shot':
        shot(argv[1])
    elif cmd == 'cmd':
        command(game, ' '.join(argv[1:]))
    elif cmd == 'run':
        args, options = run_options(argv[1:])
        return run(game, args[0] if args else DEFAULT_MISSION, int(args[1]) if len(args) > 1 else 1,
                   int(args[2]) if len(args) > 2 else 45, args[3] if len(args) > 3 else None, **options)
    elif cmd == 'mem':
        mem(game, int(argv[1]) if len(argv) > 1 else 10)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
