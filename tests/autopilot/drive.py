"""Drives EDF6 in the background through the test-only EDF6Autopilot plugin (tests/autopilot/autopilot.cpp): no key goes
to the desktop and the game window stays off screen, below the others (the user, 2026-10-07: "let it run in the
background"). For measurements, e.g. the big map's memory against the stock test range.

  python tests/autopilot/drive.py install            copy build/tools/EDF6Autopilot.dll into <game>/Mods/Plugins
  python tests/autopilot/drive.py launch             start the game (Steam), then put its window off screen, at the back
  python tests/autopilot/drive.py key VK[+VK] [ms]   hold the keys (hex virtual-key codes, or names: enter esc up ...)
  python tests/autopilot/drive.py shot FILE.png      the game window's picture (PrintWindow, never the screen)
  python tests/autopilot/drive.py mem [N]            the plugin's last N MEM rows
  python tests/autopilot/drive.py cmd TEXT           one command to the plugin: mem, quit, mission ROW|RM015 [0-4]
  python tests/autopilot/drive.py run [MISSION] [DIFFICULTY] [SECONDS] [LOG]
        one go: install, ask the mission (default RM015, the test range's slot), launch in the background, stay SECONDS
        (default 45) in the mission, quit the game's own way, print the memory (at the start, peak, end), keep the
        plugin's log at LOG, uninstall
  python tests/autopilot/drive.py hide               the window off screen and at the back again
  python tests/autopilot/drive.py uninstall          remove the plugin, its keys file and its log (game closed)

The game must not be running for install / launch / uninstall (it is the user's: their running game is never touched).
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402

NAME = 'EDF6Autopilot'
APP_ID = 2291060
KEYS = {'enter': 0x0D, 'esc': 0x1B, 'space': 0x20, 'left': 0x25, 'up': 0x26, 'right': 0x27, 'down': 0x28,
        'alt': 0x12, 'f4': 0x73, 'shift': 0x10, 'ctrl': 0x11, 'tab': 0x09}
user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32')
user32.SetProcessDPIAware()


def plugins(game: str) -> str:
    return os.path.join(game, 'Mods', 'Plugins')


def game_pids() -> list[int]:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq EDF6.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                         text=True).stdout
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


def install(game: str) -> None:
    assert not game_pids(), 'EDF6 is running: never touched'
    src = os.path.join(ROOT, 'build', 'tools', NAME + '.dll')
    shutil.copyfile(src, os.path.join(plugins(game), NAME + '.dll'))
    print('installed', os.path.join(plugins(game), NAME + '.dll'))


def uninstall(game: str) -> None:
    assert not game_pids(), 'EDF6 is running: never touched'
    for ext in ('.dll', '.keys', '.log', '.cmd'):
        path = os.path.join(plugins(game), NAME + ext)
        if os.path.exists(path):
            os.remove(path)
            print('removed', path)


def steam_windows() -> list[tuple[int, str]]:
    """Steam's own windows (SDL_app): the main "Steam" one and its launching dialog."""
    # The dialogs are steamwebhelper.exe's windows (its CEF), not steam.exe's.
    pids: set[int] = set()
    for image in ('steam.exe', 'steamwebhelper.exe'):
        out = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {image}', '/FO', 'CSV', '/NH'], capture_output=True,
                             text=True).stdout
        pids |= {int(line.split('","')[1]) for line in out.splitlines() if line.startswith(f'"{image}"')}
    found: list[tuple[int, str]] = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd: int, _: int) -> bool:
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        title = ctypes.create_unicode_buffer(128)
        user32.GetWindowTextW(hwnd, title, 128)
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, name, 64)
        if pid.value in pids and user32.IsWindowVisible(hwnd) and name.value == 'SDL_app':
            found.append((hwnd, title.value))
        return True
    user32.EnumWindows(each, 0)
    return found


def steam_dialogs() -> list[tuple[int, str]]:
    return [(hwnd, title) for hwnd, title in steam_windows() if title != 'Steam']


def steam_main() -> int | None:
    return next((hwnd for hwnd, title in steam_windows() if title == 'Steam'), None)


def sync_warning_button(img) -> tuple[int, int] | None:  # noqa: ANN001 - PIL.Image
    """Where the cloud-sync warning's 仍然进行游戏 is in Steam's main window (the user's screenshot, 2026-10-08: 「无法同步」
    is a modal inside it, not a window; Steam offline fails the sync every launch): a blue button 20-80 px high and
    100-320 px wide with the grey 取消 right beside it. None when the warning is not up."""
    w, h = img.size
    px = img.load()

    def blue(p: tuple[int, int, int]) -> bool:
        return p[2] > 190 and p[0] < 120 and 80 < p[1] < 170

    def grey(p: tuple[int, int, int]) -> bool:
        return abs(p[0] - 61) <= 10 and abs(p[1] - 68) <= 10 and abs(p[2] - 80) <= 10
    # (y, first x, last x) of a row's longest blue run, gaps up to 30 px bridged (the button's white label cuts it)
    rows: list[tuple[int, int, int]] = []
    for y in range(0, h, 2):
        best, start, last = (0, 0, 0), -1, -100
        for x in range(0, w + 2, 2):
            if x < w and blue(px[x, y]):
                if start < 0 or x - last > 30:
                    if start >= 0:
                        best = max(best, (last + 1 - start, start, last))
                    start = x
                last = x
        if start >= 0:
            best = max(best, (last + 1 - start, start, last))
        if 100 <= best[0] <= 320:
            rows.append((y, best[1], best[2]))
    blobs: list[list[tuple[int, int, int]]] = []
    for r in rows:
        if blobs and r[0] - blobs[-1][-1][0] <= 4 and abs(r[1] - blobs[-1][-1][1]) <= 6:
            blobs[-1].append(r)
        else:
            blobs.append([r])
    for blob in blobs:
        if not 20 <= blob[-1][0] - blob[0][0] <= 80:
            continue
        y = (blob[0][0] + blob[-1][0]) // 2
        left, right = min(r[1] for r in blob), max(r[2] for r in blob)
        edge = blob[0][0] + 4   # near the top edge, above the 取消 label's glyphs
        beside = [px[x, edge] for x in range(right + 30, min(right + 140, w), 5)]
        if beside and sum(grey(p) for p in beside) >= 0.7 * len(beside):
            return (left + right) // 2, y
    return None


def post_click(hwnd: int, x: int, y: int) -> None:
    """A left click posted at client (x, y) of a Steam (CEF) window, to its Chrome_WidgetWin_1 input child: no focus
    taken, the mouse not moved."""
    target: list[int] = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(child: int, _: int) -> bool:
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(child, name, 64)
        # The visible one: Steam's main window holds a hidden, zero-sized browser too.
        rect = wt.RECT()
        user32.GetClientRect(child, ctypes.byref(rect))
        if name.value == 'Chrome_WidgetWin_1' and user32.IsWindowVisible(child) and rect.right > 0:
            target.append(child)
        return True
    user32.EnumChildWindows(hwnd, each, 0)
    into = target[0] if target else hwnd
    lp = (y << 16) | x
    for msg, wp in ((0x0200, 0), (0x0201, 1), (0x0202, 0)):   # move, left down, left up
        user32.PostMessageW(into, msg, wp, lp)
        time.sleep(0.08)


def agree_sync_warning() -> bool:
    """仍然进行游戏 on Steam's cloud-sync warning when it is up, a picture of it kept first."""
    main = steam_main()
    if not main:
        return False
    img = capture(main)
    at = sync_warning_button(img)
    if not at:
        return False
    evidence = os.path.join(tempfile.gettempdir(), f'steam_sync_warning_{int(time.time())}.png')
    img.save(evidence)
    post_click(main, *at)
    print(f'steam: clicked 仍然进行游戏 at {at} (picture {evidence})')
    return True


def dialog_button(hwnd: int, title: str) -> tuple[str, float, float] | None:
    """The button to agree on, as a fraction of the client: the launching dialog's 打开游戏, the cloud-sync warning's
    仍然进行游戏 (the user's screenshot, 2026-10-08: 「无法同步」, buttons 仍然进行游戏 / 取消, 868 x 323). Anything
    else: none (never clicked)."""
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    if title.startswith(('启动中', 'Launching')):
        return '打开游戏', 0.64, 0.78
    aspect = rect.right / rect.bottom if rect.bottom else 0.0
    if 2.4 <= aspect <= 3.0:
        return '仍然进行游戏', 0.63, 0.86
    return None


def agree(hwnd: int, title: str) -> bool:
    """A click posted (no focus taken, the mouse not moved) on the dialog's agreeing button, to its CEF input window; a
    picture of the dialog kept first. False when the dialog is not one this knows."""
    button = dialog_button(hwnd, title)
    evidence = os.path.join(tempfile.gettempdir(), f'steam_dialog_{int(time.time())}.png')
    try:
        shot(evidence, hwnd)
    except Exception as e:   # the picture is evidence only
        print('steam: no picture of the dialog:', e)
    if not button:
        print(f'steam: unknown dialog {title!r} left alone (picture {evidence})')
        return False
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    post_click(hwnd, int(rect.right * button[1]), int(rect.bottom * button[2]))
    print(f'steam: clicked {button[0]} on {title!r} (picture {evidence})')
    return True


def launch() -> None:
    assert not game_pids(), 'EDF6 is already running'
    before = user32.GetForegroundWindow()
    os.startfile(f'steam://rungameid/{APP_ID}')
    seen: dict[int, float] = {}
    clicked: dict[int, float] = {}
    checked = 0.0
    deadline = time.time() + 150
    while time.time() < deadline:
        for dialog, title in ([] if game_pids() else steam_dialogs()):
            seen.setdefault(dialog, time.time())
            if time.time() - seen[dialog] > 5 and time.time() - clicked.get(dialog, 0.0) > 15:
                agree(dialog, title)
                clicked[dialog] = time.time()
        if not game_pids() and time.time() - checked > 5:   # a picture and a scan: not every turn
            checked = time.time()
            if time.time() - clicked.get(0, 0.0) > 10 and agree_sync_warning():
                clicked[0] = time.time()
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
    for text in (' '.join(f'{c:02x}' for c in codes), ''):
        with open(path + '.tmp', 'w') as f:
            f.write(text)
        os.replace(path + '.tmp', path)
        if text:
            time.sleep(ms / 1000)
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
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    buf = ctypes.create_string_buffer(w * h * 4)

    class Header(ctypes.Structure):
        _fields_ = [('size', wt.DWORD), ('w', wt.LONG), ('h', wt.LONG), ('planes', wt.WORD), ('bits', wt.WORD),
                    ('comp', wt.DWORD), ('img', wt.DWORD), ('x', wt.LONG), ('y', wt.LONG), ('used', wt.DWORD),
                    ('imp', wt.DWORD)]
    hdr = Header(ctypes.sizeof(Header), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(hdr), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return Image.frombuffer('RGBA', (w, h), buf, 'raw', 'BGRA', 0, 1).convert('RGB')


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


def run(game: str, mission: str, difficulty: int, seconds: int, keep: str | None) -> int:
    install(game)
    command(game, f'mission {mission} {difficulty}')
    try:
        launch()
        if not wait_for(game, 'PlayMission_Offline', 240):
            print('the mission never started (see the log)')
            return 1
        print('mission started; staying', seconds, 's')
        end = time.time() + seconds
        while time.time() < end and game_pids():
            time.sleep(1)
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
        if keep and os.path.exists(os.path.join(plugins(game), NAME + '.log')):
            shutil.copyfile(os.path.join(plugins(game), NAME + '.log'), keep)
            print('log kept at', keep)
        if not game_pids():
            uninstall(game)


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
        return run(game, argv[1] if len(argv) > 1 else 'RM015', int(argv[2]) if len(argv) > 2 else 1,
                   int(argv[3]) if len(argv) > 3 else 45, argv[4] if len(argv) > 4 else None)
    elif cmd == 'mem':
        mem(game, int(argv[1]) if len(argv) > 1 else 10)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
