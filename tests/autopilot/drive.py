"""Drives EDF6 in the background through the test-only EDF6Autopilot plugin (tests/autopilot/autopilot.cpp): no key goes
to the desktop and the game window stays off screen, below the others (the user, 2026-10-07: "let it run in the
background"). For measurements, e.g. the big map's memory against the stock test range.

  python tests/autopilot/drive.py install            copy build/tools/EDF6Autopilot.dll into <game>/Mods/Plugins
  python tests/autopilot/drive.py launch             start the game (Steam), then put its window off screen, at the back
  python tests/autopilot/drive.py key VK[+VK] [ms]   hold the keys (hex virtual-key codes, or names: enter esc up ...)
  python tests/autopilot/drive.py shot FILE.png      the game window's picture (PrintWindow, never the screen)
  python tests/autopilot/drive.py mem [N]            the plugin's last N MEM rows
  python tests/autopilot/drive.py cmd TEXT           one command to the plugin: mem, quit, mission ROW|RM015|M001|range [0-4]
  python tests/autopilot/drive.py run [MISSION] [DIFFICULTY] [SECONDS] [LOG]
        one go: install, ask the mission (default range: the test range's own mission pack, by the content id in the
        installed EDF6VehicleCrew.ini; RM015, M001 or a row: the stock offline list), launch in the background, stay
        SECONDS (default 45) in the mission, quit the game's own way, print the memory (at the start, peak, end), keep
        the plugin's log at LOG, uninstall
        options (after the positional ones):
          --cmd TEXT       one more plugin command handed with the mission's (repeatable), e.g. --cmd "probe airdrop"
                           (the stock air delivery probe: tests/autopilot/airdrop_probe.cpp)
          --loadout FILE   the run's forced loadout: FILE copied as <plugins>/EDF6TestRange.loadout.ini (read by the
                           installed EDF6VehicleCrew at the player's creation; the file must not exist already, and
                           is removed again afterwards), e.g. tests/autopilot/loadouts/airraider_grape.ini
          --shots DIR      the game window's picture into DIR every 15 s of the mission (PrintWindow)
        Every run also watches the desktop (DesktopWatch): the foreground window's process, the cursor clip and the
        cursor every 20 ms, and once in the mission the real cursor moved 40 px to see that nothing pulls it back;
        the counts are printed at the end (all must be 0).
  python tests/autopilot/drive.py hide               the window off screen and at the back again
  python tests/autopilot/drive.py uninstall          remove the plugin, its keys file and its log (game closed)

The game must not be running for install / launch / uninstall (it is the user's: their running game is never touched).
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402

NAME = 'EDF6Autopilot'
DEFAULT_MISSION = 'range'   # the test range's mission pack (no longer laid over RM015)
APP_ID = 2291060
KEYS = {'enter': 0x0D, 'esc': 0x1B, 'space': 0x20, 'left': 0x25, 'up': 0x26, 'right': 0x27, 'down': 0x28,
        'alt': 0x12, 'f4': 0x73, 'shift': 0x10, 'ctrl': 0x11, 'tab': 0x09}
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
            self.clipped += (clip.left, clip.top, clip.right, clip.bottom) != (left, top, right, bottom)
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
        moved = []
        for _ in range(100):   # 2 s
            time.sleep(0.02)
            now = wt.POINT()
            user32.GetCursorPos(ctypes.byref(now))
            if (now.x, now.y) != target:
                moved.append((now.x, now.y))
        user32.SetCursorPos(start.x, start.y)
        verdict = (f'cursor set to {target}, stayed there for 2 s' if not moved else
                   f'cursor set to {target}, moved away in {len(moved)}/100 samples (first {moved[0]})')
        self.nudges.append(verdict)
        return verdict

    def report(self) -> str:
        return (f'desktop over {self.samples} samples: game in the foreground {self.game_foreground}, cursor clipped '
                f'{self.clipped}, cursor on the right edge {self.at_right_edge}; nudges: {self.nudges or "none"}')


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
    for ext in SESSION_EXTS:
        if os.path.lexists(os.path.join(directory, NAME + ext)):
            raise RuntimeError('Existing autopilot files must be preserved: ' + NAME + ext)
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


def place_loadout(game: str, source: str) -> tuple[str, bytes]:
    """The run's forced loadout written as EDF6VehicleCrew's ini; never over an existing one (the test range's)."""
    with open(source, 'rb') as f:
        payload = f.read()
    path = os.path.join(plugins(game), LOADOUT)
    with open(path, 'xb') as f:
        f.write(payload)
    print('loadout', source, '->', path)
    return path, payload


def remove_loadout(path: str, payload: bytes) -> None:
    """The run's loadout removed, only while it is still the one this run wrote."""
    try:
        with open(path, 'rb') as f:
            if f.read() != payload:
                print('loadout changed by someone else: left at', path)
                return
    except FileNotFoundError:
        return
    os.remove(path)
    print('removed', path)


def launch() -> None:
    require_closed()
    before = user32.GetForegroundWindow()
    os.startfile(f'steam://rungameid/{APP_ID}')
    # Steam dialogs can authorize cloud-save conflicts or unrelated actions. Shape,
    # color and window title are not semantic identity: leave all confirmations to
    # the user and only wait for the game this command requested.
    deadline = time.time() + 150
    while time.time() < deadline:
        hwnd = game_window()
        if hwnd:
            hide(hwnd)
            user32.SetForegroundWindow(before)
            print('window', hex(hwnd), 'off screen')
            return
        time.sleep(0.5)
    raise SystemExit('no game window in 150 s')


def key(game: str, spec: str, ms: int) -> None:
    codes = [KEYS[k.lower()] if k.lower() in KEYS else int(k, 16) for k in spec.split('+')]
    path = os.path.join(plugins(game), NAME + '.keys')
    def write(text: str) -> None:
        with open(path + '.tmp', 'w') as f:
            f.write(text)
        os.replace(path + '.tmp', path)
    try:
        write(' '.join(f'{c:02x}' for c in codes))
        time.sleep(ms / 1000)
    finally:
        write('')
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


def command(game: str, text: str) -> None:
    """Hands the plugin one command (<dll>.cmd, cleared by it once run)."""
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
    end = time.time() + seconds
    while time.time() < end:
        if any(text in r for r in rows(game)):
            return True
        if not game_pids():
            return False
        time.sleep(1)
    return False


def commit_of(row: str) -> float:
    return float(row.split('commit ')[1].split(' MB')[0])


def run(game: str, mission: str, difficulty: int, seconds: int, keep: str | None, *,
        extra: tuple[str, ...] = (), loadout: str | None = None, shots: str | None = None) -> int:
    install(game)
    placed: tuple[str, bytes] | None = None
    watch = DesktopWatch().start()
    try:
        if loadout:
            placed = place_loadout(game, loadout)
        command(game, '\n'.join([f'mission {mission} {difficulty}', *extra]))
        launch()
        if not wait_for(game, 'PlayMission_Offline', 240):
            print('the mission never started (see the log)')
            return 1
        print('mission started; staying', seconds, 's')
        start = time.time()
        end = start + seconds
        next_shot = start + 15
        nudge_at = start + min(20, seconds / 2)
        while (now := time.time()) < end:
            if not game_pids():
                print('the game exited before the observation period ended')
                return 1
            if nudge_at and now >= nudge_at:
                print('real mouse check:', watch.nudge())
                nudge_at = 0
            if shots and now >= next_shot:
                os.makedirs(shots, exist_ok=True)
                try:
                    shot(os.path.join(shots, f'{int(now - start):04d}s.png'))
                except (RuntimeError, AssertionError) as e:
                    print('no picture:', e)
                next_shot += 15
            time.sleep(1)
        if not game_pids():
            print('the game exited before quit was requested')
            return 1
        command(game, 'quit')
        for _ in range(90):
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
            print('the game did not exit in 90 s: left running, the plugin stays installed')
            return 1
        return 0
    finally:
        watch.stop()
        print(watch.report())
        try:
            if keep and os.path.exists(os.path.join(plugins(game), NAME + '.log')):
                shutil.copyfile(os.path.join(plugins(game), NAME + '.log'), keep)
                print('log kept at', keep)
        finally:
            if not game_pids():
                if placed:
                    remove_loadout(*placed)
                uninstall(game)


def run_options(argv: list[str]) -> tuple[list[str], dict]:
    """`run`'s positional arguments and its options (--cmd TEXT repeatable, --loadout FILE, --shots DIR)."""
    args: list[str] = []
    extra: list[str] = []
    options: dict = {'extra': (), 'loadout': None, 'shots': None}
    i = 0
    while i < len(argv):
        if argv[i] in ('--cmd', '--loadout', '--shots'):
            if i + 1 >= len(argv):
                raise SystemExit(argv[i] + ' needs a value')
            if argv[i] == '--cmd':
                extra.append(argv[i + 1])
            else:
                options[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    options['extra'] = tuple(extra)
    return args, options


def main(argv: list[str]) -> int:
    game = gamedir.find_or_dev()
    cmd = argv[0] if argv else ''
    if cmd == 'install':
        install(game)
    elif cmd == 'uninstall':
        uninstall(game)
    elif cmd == 'launch':
        launch()
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
