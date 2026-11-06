"""原版启动 (tools/vanilla_launch.py), offline, in a temporary game folder; no game, no Steam:
  - vanilla_ini sets LoadPlugins / LoadASI / Redirect False in [ModLoader], keeps every other line, adds what is missing;
  - prepare + restore give back the ini byte for byte (and no ini when there was none); an ini changed in between is kept;
  - launch: puts a left-over run back first, refuses while the game runs, starts Steam, waits for EDF6.exe to come and
    go, restores; restores too when the game never starts;
  - the generator dispatches `vanilla-launch <game>`; install writes 原版启动.cmd, uninstall removes it, write_game
    restores a left-over ini.
Exit code 1 when one fails.
"""
from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib')]
import vanilla_launch as vl  # noqa: E402
import loadout_generator as lg  # noqa: E402
import installer  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why, file=sys.stderr)
        sys.exit(1)


STOCK = ('[ModLoader]\r\n; Load plugins from Mods\\Plugins\r\nLoadPlugins=True\r\n; Load asi plugins\r\nLoadASI=True\r\n'
         '; Redirect file access from Root.cpk to Mods folder\r\nRedirect=True\r\n; Write internal game logs\r\n'
         'GameLog=False\r\n')
got = vl.vanilla_ini(STOCK)
check(got == STOCK.replace('LoadPlugins=True', 'LoadPlugins=False').replace('LoadASI=True', 'LoadASI=False')
      .replace('Redirect=True', 'Redirect=False'), f'the stock ini: three keys off, the rest as it was: {got!r}')
got = vl.vanilla_ini('[Other]\nA=1\n[ModLoader]\nGameLog=True\nRedirect=1\n[Tail]\nB=2')
check(got == '[Other]\nA=1\n[ModLoader]\nGameLog=True\nRedirect=False\nLoadPlugins=False\nLoadASI=False\n[Tail]\nB=2',
      f'missing keys added in the section, LF kept, other sections untouched: {got!r}')
check(vl.vanilla_ini('') == '[ModLoader]\r\nLoadPlugins=False\r\nLoadASI=False\r\nRedirect=False\r\n', 'no ini: the section')
check(vl.vanilla_ini('[X]\nA=1') == '[X]\nA=1\n[ModLoader]\nLoadPlugins=False\nLoadASI=False\nRedirect=False\n',
      'no section: added after the last line')
check(vl.vanilla_ini('[ModLoader]') == '[ModLoader]\nLoadPlugins=False\nLoadASI=False\nRedirect=False\n',
      'a bare header without a newline')
launcher = vl.launcher_text(lg.GENERATOR).decode('ascii')
check(f'"%~dp0Mods\\Plugins\\{lg.GENERATOR}" vanilla-launch "%~dp0."' in launcher and launcher.endswith('\r\n'),
      'the .cmd runs the generator beside the plugin with the game folder')


def game(root: str, ini: bytes | None) -> str:
    g = os.path.join(root, 'game')
    os.makedirs(os.path.join(g, 'Mods', 'Plugins'))
    for name in ('EDF6.exe', 'Root.cpk'):
        open(os.path.join(g, name), 'wb').close()
    if ini is not None:
        with open(os.path.join(g, vl.INI), 'wb') as f:
            f.write(ini)
    return g


def ini_of(g: str) -> bytes | None:
    p = os.path.join(g, vl.INI)
    return open(p, 'rb').read() if os.path.isfile(p) else None


state_of = lambda g: os.path.join(g, 'Mods', 'Plugins', vl.STATE)  # noqa: E731
quiet = lambda: contextlib.redirect_stdout(io.StringIO())  # noqa: E731

with tempfile.TemporaryDirectory(prefix='edf6vc-vanilla-') as root:
    raw = b'\xef\xbb\xbf' + STOCK.encode('utf-8')   # with a BOM: given back as it was
    g = game(os.path.join(root, 'a'), raw)
    vl.prepare(g)
    check(b'LoadPlugins=False' in ini_of(g) and os.path.isfile(state_of(g)), 'prepare: vanilla ini and the state')
    check(vl.restore(g) and ini_of(g) == raw and not os.path.isfile(state_of(g)), 'restore: byte for byte, state gone')
    check(vl.restore(g) is None, 'nothing left: nothing done')

    vl.prepare(g)
    with open(os.path.join(g, vl.INI), 'wb') as f:
        f.write(b'[ModLoader]\r\nLoadPlugins=True\r\n')   # the player changed it meanwhile
    check('保留' in vl.restore(g) and ini_of(g) == b'[ModLoader]\r\nLoadPlugins=True\r\n' and not os.path.isfile(state_of(g)),
          'a changed ini is kept, the state dropped')

    g2 = game(os.path.join(root, 'b'), None)
    vl.prepare(g2)
    check(ini_of(g2) is not None, 'no ini: a vanilla one written')
    vl.restore(g2)
    check(ini_of(g2) is None, 'and removed again')

    # launch, with the game, Steam and the clock simulated.
    with open(os.path.join(g, vl.INI), 'wb') as f:
        f.write(raw)
    real = (vl.os.startfile if hasattr(vl.os, 'startfile') else None, vl.game_pid, vl.modfiles.game_running, vl.time.sleep,
            vl._on_close)
    seen: dict[str, object] = {}
    try:
        vl._on_close = lambda _g: None
        vl.time.sleep = lambda _s: None
        vl.modfiles.game_running = lambda *_a: True
        with quiet():
            vl.prepare(g)   # a left-over run: put back first, then refused (the game runs)
            check(vl.launch(g) == 1 and ini_of(g) == raw and not os.path.isfile(state_of(g)),
                  'game running: refused, the left-over put back')

        polls = iter([None, None, 4242])
        running = iter([False, True, True, False])   # the check before start, then the wait for exit
        vl.modfiles.game_running = lambda *_a: next(running)
        vl.game_pid = lambda: next(polls)

        def start(url: str) -> None:
            seen['url'] = url
            seen['ini'] = ini_of(g)
        vl.os.startfile = start
        with quiet():
            check(vl.launch(g) == 0, 'started and exited: 0')
        check(seen['url'] == vl.STEAM_URL and b'Redirect=False' in seen['ini'], 'Steam started with the vanilla ini')
        check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'after the game: the ini back')

        vl.modfiles.game_running = lambda *_a: False
        vl.game_pid = lambda: None
        with quiet():
            check(vl.launch(g) == 1, 'the game never started: 1')
        check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'and the ini back')
    finally:
        if real[0] is not None:
            vl.os.startfile = real[0]
        vl.game_pid, vl.modfiles.game_running, vl.time.sleep, vl._on_close = real[1:]

    # the generator's dispatch
    called: list[str] = []
    real_launch = vl.launch
    vl.launch = lambda path: called.append(path) or 0
    try:
        check(lg.run(['vanilla-launch', f'"{g}"']) == 0 and called == [g], 'the generator runs vanilla-launch <game>')
    finally:
        vl.launch = real_launch

    # the installer: the .cmd written beside EDF6.exe, removed by uninstall's helper; write_game restores first.
    with quiet():
        installer.write_launcher(g)
    cmd = os.path.join(g, vl.LAUNCHER)
    check(open(cmd, 'rb').read() == vl.launcher_text(installer.GENERATOR), 'install writes 原版启动.cmd')
    with quiet():
        installer.remove_launcher(g)
    check(not os.path.isfile(cmd), 'uninstall removes it')
    vl.prepare(g)
    real_lock = installer.wait_generate_lock
    installer.wait_generate_lock = lambda _g: contextlib.nullcontext(False)   # refused right after the restore
    try:
        with quiet():
            installer.write_game('install', g)
    finally:
        installer.wait_generate_lock = real_lock
    check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'write_game puts a left-over ini back first')

inst = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
check('write_launcher(game)' in inst and 'remove_launcher(game)' in inst, 'install / uninstall call them')
print(f'vanilla_launch: {checks} checks OK')
