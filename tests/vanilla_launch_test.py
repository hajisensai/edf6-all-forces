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
    user = os.path.join(local, vl.SAVES, '76561198000000000')
    slot0, slot1 = os.path.join(user, 'saveslot00'), os.path.join(user, 'saveslot01')
    MOD = {'MAIN.GST': b'mod save', 'MAIN.GST.edf6vc': b'mod rows', 'COMMON.CFG': b'mod cfg'}
    VAN = {'MAIN.GST': b'vanilla save', 'COMMON.CFG': b'vanilla cfg'}
    system = os.path.join(user, 'SYSTEM', 'SYSTEM.CFG')

    def plant(d: str, files: dict[str, bytes]) -> None:
        os.makedirs(d, exist_ok=True)
        for n, data in files.items():
            with open(os.path.join(d, n), 'wb') as f:
                f.write(data)

    def own(slot: str) -> dict[str, bytes]:
        return {n: open(os.path.join(slot, n), 'rb').read() for n in vl._own(slot)}

    def store(slot: str, mode: str) -> dict[str, bytes]:
        return {n: open(os.path.join(slot, n + vl.SUFFIX[mode]), 'rb').read() for n in vl._stored(slot, mode)}

    def settled() -> bool:   # every slot back to the mod's set, nothing pending
        return all(vl._mode(s) == 'mod' for s in vl.slots()) and not os.path.exists(os.path.join(local, vl.ON))
    plant(slot0, MOD)
    plant(os.path.dirname(system), {'SYSTEM.CFG': b'system'})

    real = (vl.os.startfile if hasattr(vl.os, 'startfile') else None, vl.game_pid, vl.modfiles.game_running, vl.time.sleep,
            vl._on_close, vl.save_root, vl.first_vanilla_saves)
    seen: dict[str, object] = {}
    try:
        vl._on_close = lambda _g: None
        vl.time.sleep = lambda _s: None
        vl.save_root = lambda: local
        vl.modfiles.game_running = lambda *_a: True
        with quiet():
            check(vl.launch(g) == 1 and ini_of(g) == raw and own(slot0) == MOD and os.listdir(slot0) == sorted(MOD),
                  'game running: refused, nothing touched')

        def run_game(answers: list[bool], play=None, starts: bool = True) -> int:
            """One launch: EDF6.exe shows up on the third poll, runs two polls, exits (or never shows up)."""
            asked = iter(answers)
            vl.first_vanilla_saves = lambda: next(asked)
            polls = iter([None, None, 4242])
            running = iter([False, True, True, False, False])   # before, the wait for exit, the save swap after
            vl.modfiles.game_running = (lambda *_a: next(running)) if starts else (lambda *_a: False)
            vl.game_pid = (lambda: next(polls)) if starts else (lambda: None)

            def start(url: str) -> None:
                seen.update(url=url, ini=ini_of(g), own=own(slot0), mod=store(slot0, 'mod'))
                if play:
                    play()   # the vanilla game saving
            vl.os.startfile = start
            with quiet():
                return vl.launch(g)

        vl.prepare(g)   # a left-over ini from a run that never put it back: put back first, then the run goes on
        check(run_game([False], lambda: plant(slot0, VAN)) == 0, 'first run: 0')
        check(seen['url'] == vl.STEAM_URL and b'Redirect=False' in seen['ini'], 'Steam started with the vanilla ini')
        check(seen['own'] == {} and seen['mod'] == MOD, 'first run, a new save: the slot empty, the mod set stored in it')
        check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'after the game: the ini back')
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN and store(slot0, 'mod') == MOD and settled(),
              'after the game: the mod set back, both sets in the slot (both synced)')
        check(open(system, 'rb').read() == b'system', 'SYSTEM untouched')
        backups = os.path.join(local, vl.save_backup.BACKUPS)
        first = os.path.join(backups, os.listdir(backups)[0])
        check(len(os.listdir(backups)) == 1 and own(os.path.join(first, '76561198000000000', 'saveslot00')) == MOD,
              'the saves backed up before the run')

        VAN2 = {**VAN, 'MAIN.GST': b'vanilla save 2'}
        check(run_game([], lambda: plant(slot0, VAN2)) == 0, 'second run: not asked again')
        check(seen['own'] == VAN and seen['mod'] == MOD, 'second run: the vanilla set loaded')
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'and its progress stored after')

        check(run_game([], lambda: plant(slot1, {'MAIN.GST': b'new vanilla slot'})) == 0, 'a run making a new slot')
        check(own(slot1) == {} and store(slot1, 'vanilla') == {'MAIN.GST': b'new vanilla slot'} and settled(),
              'the new slot is the vanilla set: stored, empty for the mod')

        check(run_game([], starts=False) == 1, 'the game never started: 1')
        check(ini_of(g) == raw and own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'everything back')

        # the window closed while the game runs: the ini back, the saves left until the game is gone
        vl.modfiles.game_running = lambda *_a: False
        vl.swap_in_saves()
        vl.prepare(g)
        vl.modfiles.game_running = lambda *_a: True
        said = vl.restore_all(g)
        check(ini_of(g) == raw and own(slot0) == VAN2 and any('还在运行' in s for s in said),
              'game running: the ini back, the saves not switched under it')
        vl.modfiles.game_running = lambda *_a: False
        vl.restore_all(g)
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'after it exits: switched back')

        # cut off midway (each step marked): resumed without mixing the sets
        vl.swap_in_saves()                       # vanilla set playing
        vl._store(slot0, 'vanilla')
        vl._set_mode(slot0, 'vanilla-saved')
        plant(slot0, {'MAIN.GST': MOD['MAIN.GST']})   # half the mod set copied back, then the power went
        os.remove(os.path.join(local, vl.ON))    # the next start: a new run, ON gone with nothing else
        vl.restore_all(g)
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'a switch back cut off: resumed')
        vl._store(slot0, 'mod')
        vl._set_mode(slot0, 'mod-saved')         # cut off on the way in, after storing the mod set
        vl.restore_all(g)
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'a switch in cut off: the mod set kept')

        # a slot that came down from Steam Cloud in the vanilla set (a run on another machine, no ON here)
        vl._store(slot0, 'mod')
        vl._load(slot0, 'vanilla')
        vl._set_mode(slot0, 'vanilla')
        check(vl.restore_all(g) and own(slot0) == MOD and settled(), 'synced in the vanilla set: switched back')

        # first run copying the mod set
        for s in vl.slots():
            for n in vl._stored(s, 'vanilla'):
                os.remove(os.path.join(s, n + vl.SUFFIX['vanilla']))
        vl.first_vanilla_saves = lambda: True
        vl.swap_in_saves()
        check(own(slot0) == MOD and vl._mode(slot0) == 'vanilla', 'copy chosen: the vanilla set starts as the mod set')
        vl.restore_all(g)
        check(own(slot0) == MOD and store(slot0, 'vanilla') == MOD and settled(), 'and is stored after')

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
    check(own(slot0) == MOD and settled(), 'and the mod saves')

inst = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
check('write_launcher(game)' in inst and 'remove_launcher(game)' in inst, 'install / uninstall call them')
print(f'vanilla_launch: {checks} checks OK')
