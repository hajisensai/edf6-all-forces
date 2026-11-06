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

    # launch, with the game, Steam, the clock and the save folder simulated.
    with open(os.path.join(g, vl.INI), 'wb') as f:
        f.write(raw)
    local = os.path.join(root, 'local', 'EarthDefenceForce6')
    MOD = {'76561198000000000/saveslot00/MAIN.GST': b'mod save', 'SYSTEM/SYSTEM.CFG': b'cfg'}

    def tree(path: str) -> dict[str, bytes] | None:
        if not os.path.isdir(path):
            return None
        out = {}
        for d, _dirs, files in os.walk(path):
            for n in files:
                full = os.path.join(d, n)
                out[os.path.relpath(full, path).replace(os.sep, '/')] = open(full, 'rb').read()
        return out

    def plant(path: str, files: dict[str, bytes]) -> None:
        for rel, data in files.items():
            os.makedirs(os.path.dirname(os.path.join(path, rel)), exist_ok=True)
            with open(os.path.join(path, rel), 'wb') as f:
                f.write(data)
    plant(os.path.join(local, vl.SAVES), MOD)
    live, mod_stash, van_stash = (os.path.join(local, n) for n in (vl.SAVES, vl.MOD_STASH, vl.VANILLA_STASH))
    swapped = os.path.join(local, vl.SWAPPED)

    real = (vl.os.startfile if hasattr(vl.os, 'startfile') else None, vl.game_pid, vl.modfiles.game_running, vl.time.sleep,
            vl._on_close, vl.save_root, vl.first_vanilla_saves)
    seen: dict[str, object] = {}
    try:
        vl._on_close = lambda _g: None
        vl.time.sleep = lambda _s: None
        vl.save_root = lambda: local
        vl.modfiles.game_running = lambda *_a: True
        with quiet():
            check(vl.launch(g) == 1 and ini_of(g) == raw and tree(live) == MOD and not os.path.exists(swapped),
                  'game running: refused, nothing touched')

        def run_game(answers: list[bool], writes: dict[str, bytes] | None = None, starts: bool = True) -> int:
            """One launch: EDF6.exe shows up on the third poll, runs two polls, exits (or never shows up)."""
            asked = iter(answers)
            vl.first_vanilla_saves = lambda: next(asked)
            polls = iter([None, None, 4242])
            running = iter([False, True, True, False, False])   # before, the wait for exit, the save swap after
            vl.modfiles.game_running = (lambda *_a: next(running)) if starts else (lambda *_a: False)
            vl.game_pid = (lambda: next(polls)) if starts else (lambda: None)

            def start(url: str) -> None:
                seen.update(url=url, ini=ini_of(g), live=tree(live), mod=tree(mod_stash))
                if writes:
                    plant(live, writes)   # the vanilla game saving
            vl.os.startfile = start
            with quiet():
                return vl.launch(g)

        vl.prepare(g)   # a left-over ini from a run that never put it back: put back first, then the run goes on
        check(run_game([False], {'76561198000000000/saveslot00/MAIN.GST': b'vanilla save'}) == 0, 'first run: 0')
        check(seen['url'] == vl.STEAM_URL and b'Redirect=False' in seen['ini'], 'Steam started with the vanilla ini')
        check(seen['live'] == {} and seen['mod'] == MOD, 'first run, a new save: SAVE_DATA empty, the mod saves aside')
        check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'after the game: the ini back')
        check(tree(live) == MOD and not os.path.exists(mod_stash) and not os.path.exists(swapped), 'the mod saves back')
        VAN = {'76561198000000000/saveslot00/MAIN.GST': b'vanilla save'}
        check(tree(van_stash) == VAN, 'the vanilla saves kept aside')
        backups = os.path.join(local, vl.BACKUPS)
        check(len(os.listdir(backups)) == 1 and tree(os.path.join(backups, os.listdir(backups)[0])) == MOD,
              'the mod saves backed up before the run')

        check(run_game([]) == 0, 'second run: not asked again')
        check(seen['live'] == VAN and seen['mod'] == MOD, 'second run: the vanilla saves in, the mod saves aside')
        check(tree(live) == MOD and tree(van_stash) == VAN, 'and back after')

        check(run_game([], starts=False) == 1, 'the game never started: 1')
        check(ini_of(g) == raw and tree(live) == MOD and tree(van_stash) == VAN and not os.path.exists(swapped),
              'and everything back')

        # the window closed while the game runs: the ini back, the saves left until the game is gone
        vl.modfiles.game_running = lambda *_a: False
        vl.swap_in_saves()
        vl.prepare(g)
        vl.modfiles.game_running = lambda *_a: True
        said = vl.restore_all(g)
        check(ini_of(g) == raw and tree(live) == VAN and os.path.exists(swapped) and any('还在运行' in s for s in said),
              'game running: the ini back, the saves not swapped under it')
        vl.modfiles.game_running = lambda *_a: False
        vl.restore_all(g)
        check(tree(live) == MOD and tree(van_stash) == VAN and not os.path.exists(swapped), 'after it exits: swapped back')

        # stopped between the marker and the renames: SAVE_DATA is still the mod's, left alone
        vl.modfiles.save_json(swapped, {'mod_had': True})
        check(vl.swap_out_saves() is None and tree(live) == MOD and not os.path.exists(swapped), 'a marker alone: dropped')

        # first run copying the mod saves; a mod with no saves at all
        import shutil
        shutil.rmtree(van_stash)
        vl.modfiles.game_running = lambda *_a: False
        vl.first_vanilla_saves = lambda: True
        vl.swap_in_saves()
        check(tree(live) == MOD and tree(mod_stash) == MOD, 'copy chosen: the vanilla saves start as a copy')
        vl.restore_all(g)
        check(tree(live) == MOD and tree(van_stash) == MOD, 'and back')
        shutil.rmtree(live)
        vl.swap_in_saves()
        check(tree(live) == MOD and not os.path.exists(mod_stash), 'no mod saves: the vanilla ones in, nothing aside')
        vl.restore_all(g)
        check(not os.path.exists(live) and tree(van_stash) == MOD, 'and the vanilla ones aside again, no mod saves made up')
        plant(live, MOD)

        for _ in range(vl.KEEP_BACKUPS + 2):
            vl.backup_mod_saves()
        check(len(os.listdir(backups)) == vl.KEEP_BACKUPS, 'backups pruned to KEEP_BACKUPS')
    finally:
        if real[0] is not None:
            vl.os.startfile = real[0]
        (vl.game_pid, vl.modfiles.game_running, vl.time.sleep, vl._on_close, vl.save_root,
         vl.first_vanilla_saves) = real[1:]
    vl.save_root = lambda: local   # for the installer checks below

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
    real_lock, real_running = installer.wait_generate_lock, vl.modfiles.game_running
    vl.modfiles.game_running = lambda *_a: False   # this machine's own game, if it runs, is not the test's
    vl.swap_in_saves()
    vl.prepare(g)
    installer.wait_generate_lock = lambda _g: contextlib.nullcontext(False)   # refused right after the restore
    try:
        with quiet():
            installer.write_game('install', g)
    finally:
        installer.wait_generate_lock, vl.modfiles.game_running = real_lock, real_running
    check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'write_game puts a left-over ini back first')
    check(tree(live) == MOD and not os.path.exists(swapped), 'and the mod saves')

inst = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
check('write_launcher(game)' in inst and 'remove_launcher(game)' in inst, 'install / uninstall call them')
print(f'vanilla_launch: {checks} checks OK')
