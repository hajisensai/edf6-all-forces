"""Drives EDF6 in the background through the test-only EDF6Autopilot plugin (tests/autopilot/autopilot.cpp): no key goes
to the desktop and the game window stays off screen, below the others (the user, 2026-10-07: "let it run in the
background"). For measurements, e.g. the big map's memory against the stock test range.

  python tests/autopilot/drive.py install            copy build/tools/EDF6Autopilot.dll into <game>/Mods/Plugins
  python tests/autopilot/drive.py launch             start the game (Steam), then put its window off screen, at the back
  python tests/autopilot/drive.py key VK[+VK] [ms]   hold the keys (hex virtual-key codes, or names: enter esc up ...)
  python tests/autopilot/drive.py shot FILE.png      the game window's picture (PrintWindow, never the screen)
  python tests/autopilot/drive.py mem [N]            the plugin's last N MEM rows
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
    for ext in ('.dll', '.keys', '.log'):
        path = os.path.join(plugins(game), NAME + ext)
        if os.path.exists(path):
            os.remove(path)
            print('removed', path)


def launch() -> None:
    assert not game_pids(), 'EDF6 is already running'
    before = user32.GetForegroundWindow()
    os.startfile(f'steam://rungameid/{APP_ID}')
    for _ in range(240):
        hwnd = game_window()
        if hwnd:
            hide(hwnd)
            user32.SetForegroundWindow(before)
            print('window', hex(hwnd), 'off screen')
            return
        time.sleep(0.5)
    raise SystemExit('no game window in 120 s')


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


def shot(out: str) -> None:
    from PIL import Image
    hwnd = game_window()
    assert hwnd, 'no game window'
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
    img = Image.frombuffer('RGBA', (w, h), buf, 'raw', 'BGRA', 0, 1).convert('RGB')
    img.thumbnail((1280, 720))
    img.save(out)
    print('saved', out, f'{w}x{h}')


def mem(game: str, n: int) -> None:
    with open(os.path.join(plugins(game), NAME + '.log'), encoding='utf-8', errors='replace') as f:
        rows = f.read().splitlines()
    for row in rows[-n:]:
        print(row)


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
    elif cmd == 'mem':
        mem(game, int(argv[1]) if len(argv) > 1 else 10)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
