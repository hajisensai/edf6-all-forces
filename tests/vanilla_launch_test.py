"""原版启动 (tools/vanilla_launch.py), offline, in a temporary game folder; no game, no Steam:
  - vanilla_ini sets LoadPlugins / LoadASI / Redirect False in [ModLoader], keeps every other line, adds what is missing;
  - prepare + restore give back the ini byte for byte (and no ini when there was none); an ini changed in between is kept;
  - launch: puts a left-over run back first, refuses while the game runs, starts Steam, waits for EDF6.exe to come and
    go, restores; restores too when the game never starts;
  - the window closed while the game runs: the ini back, the saves switched back by the plugin's restore-saves at the
    next mod start (also a slot synced in the vanilla set); closed at the prompt or while waiting: nothing left; no
    launch step after the close handler;
  - a switch in / out cut off after every single write or removal: the next restore gives every slot the whole mod
    set, every store whole, and a full round after it works (a vanilla load cut half way: the mod store untouched);
  - prepare refuses over an existing STATE; another process holding the lock refuses a second launch, the installer
    and restore-saves with nothing touched, and a killed holder frees it;
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

    def set_store(slot: str, mode: str, files: dict[str, bytes]) -> None:
        for n in vl._stored(slot, mode):
            os.remove(os.path.join(slot, n + vl.SUFFIX[mode]))
        plant(slot, {n + vl.SUFFIX[mode]: data for n, data in files.items()})

    def settled() -> bool:   # every slot back to the mod's set, nothing pending
        return all(vl._mode(s) == 'mod' for s in vl.slots()) and not os.path.exists(os.path.join(local, vl.ON))
    plant(slot0, MOD)
    plant(os.path.dirname(system), {'SYSTEM.CFG': b'system'})

    real = (vl.os.startfile if hasattr(vl.os, 'startfile') else None, vl.game_pids, vl.modfiles.game_running, vl.time.sleep,
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
            polls = iter([set(), set(), set(), {4242}])   # before the start (none), then shows up on the third poll
            # launch's check, swap_in_saves', _start's, the wait for exit (two polls), the save swap after
            running = iter([False, False, False, True, True, False, False])
            vl.modfiles.game_running = (lambda *_a: next(running)) if starts else (lambda *_a: False)
            vl.game_pids = (lambda: next(polls)) if starts else (lambda: set())

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
        made = sorted(os.listdir(backups))   # same second: <stamp>, <stamp>-1
        check(len(made) == 2 and own(os.path.join(backups, made[0], '76561198000000000', 'saveslot00')) == MOD
              and own(os.path.join(backups, made[1], '76561198000000000', 'saveslot00')) == VAN,
              'the saves backed up before the run (the mod set) and before switching back (the vanilla set played)')

        VAN2 = {**VAN, 'MAIN.GST': b'vanilla save 2'}
        check(run_game([], lambda: plant(slot0, VAN2)) == 0, 'second run: not asked again')
        check(seen['own'] == VAN and seen['mod'] == MOD, 'second run: the vanilla set loaded')
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'and its progress stored after')

        check(run_game([], lambda: plant(slot1, {'MAIN.GST': b'new vanilla slot'})) == 0, 'a run making a new slot')
        check(own(slot1) == {} and store(slot1, 'vanilla') == {'MAIN.GST': b'new vanilla slot'} and settled(),
              'the new slot is the vanilla set: stored, empty for the mod')

        check(run_game([], starts=False) == 1, 'the game never started: 1')
        check(ini_of(g) == raw and own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(), 'everything back')

        # the window closed while the vanilla game runs: the ini back, the saves left under the running game; the
        # game's next start with the mod (Steam, the ini back) runs restore-saves from the plugin before it reads them:
        # never "the mod game on the vanilla saves", and the mod set never stored as the vanilla one.
        vl.modfiles.game_running = lambda *_a: False
        vl.swap_in_saves()
        vl.prepare(g)
        vl.modfiles.game_running = lambda *_a: True
        vl._close(g)
        check(ini_of(g) == raw and own(slot0) == VAN2 and vl._mode(slot0) == 'vanilla',
              'closed while the game runs: the ini back, the saves left under the running game')
        try:
            vl._step(vl.prepare, g)
            check(False, 'no step after the close handler')
        except vl.Closed:
            check(ini_of(g) == raw and not os.path.isfile(state_of(g)), 'no step after the close handler: ini kept')
        vl._closing = False
        with quiet():   # the mod start: the plugin's restore-saves, the game (itself) running
            check(lg.run(['restore-saves']) == 0, 'restore-saves: 0')
        check(own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and store(slot0, 'mod') == MOD and settled(),
              'the mod start switches the saves back before the game reads them, the vanilla progress stored')
        with quiet():
            check(lg.run(['restore-saves']) == 0 and own(slot0) == MOD and settled(), 'restore-saves again: nothing')

        # a slot synced from a vanilla run on another machine (MODE=vanilla, no ON here): the mod start switches it
        vl.modfiles.game_running = lambda *_a: False
        vl.swap_in_saves()
        os.remove(os.path.join(local, vl.ON))
        vl.modfiles.game_running = lambda *_a: True
        with quiet():
            check(lg.run(['restore-saves']) == 0 and own(slot0) == MOD and store(slot0, 'vanilla') == VAN2 and settled(),
                  'synced in the vanilla set: the mod start switches it back')
        vl.modfiles.game_running = lambda *_a: False

        # the window closed during a launch, before the game: whatever step it came at, nothing is left changed
        def close_at(prompt: bool) -> None:
            """A launch whose window is closed at the first-run prompt (prompt) or while waiting for the game."""
            vl.os.startfile = lambda _url: None
            vl.modfiles.game_running = lambda *_a: False
            for s in vl.slots():   # first run: asked
                set_store(s, 'vanilla', {})

            def ask() -> bool:
                vl._close(g)
                return False
            vl.first_vanilla_saves = ask if prompt else (lambda: False)
            polls: list[int] = []

            def waiting() -> set[int]:   # the first call is _start's (under the lock): closed at the first poll after
                polls.append(1)
                if len(polls) == 2:
                    vl._close(g)
                return set()
            vl.game_pids = (lambda: set()) if prompt else waiting
            with quiet():
                check(vl.launch(g) == 1, 'closed: 1')
            check(ini_of(g) == raw and not os.path.isfile(state_of(g)) and own(slot0) == MOD and settled()
                  and store(slot0, 'vanilla') == {}, f'closed {"at the prompt" if prompt else "waiting"}: nothing left')
            vl._closing = False
        close_at(True)
        close_at(False)

        # the mod game started from Steam while the first-run prompt waits (the ini and the saves still the mod's, so
        # the plugin loads as usual): the saves are never switched under it, Steam never asked, nothing left changed.
        for s in vl.slots():
            set_store(s, 'vanilla', {})
        game_on: list[bool] = []
        vl.modfiles.game_running = lambda *_a: bool(game_on)
        vl.game_pids = lambda: {777} if game_on else set()
        started: list[str] = []
        vl.os.startfile = lambda url: started.append(url)
        vl.first_vanilla_saves = lambda: game_on.append(True) or False
        with quiet():
            check(vl.launch(g) == 1, 'the game started at the prompt: 1')
        check(not started and ini_of(g) == raw and not os.path.isfile(state_of(g)) and own(slot0) == MOD
              and store(slot0, 'vanilla') == {} and settled(), 'the game started at the prompt: the saves never switched')
        game_on.clear()
        try:
            game_on.append(True)
            vl.swap_in_saves(False)
            check(False, 'swap_in_saves with the game running: refused')
        except vl.GameRunning:
            check(own(slot0) == MOD and settled(), 'swap_in_saves with the game running: nothing changed')
        game_on.clear()

        # ... or right after the saves were switched (before Steam is asked): not started, never taken for the vanilla
        # game; the ini back at once, the saves left under that game for its plugin's restore-saves.
        real_prepare = vl.prepare
        vl.prepare = lambda path: real_prepare(path) or game_on.append(True)
        vl.first_vanilla_saves = lambda: False
        try:
            with quiet():
                check(vl.launch(g) == 1, 'the game started before Steam was asked: 1')
        finally:
            vl.prepare = real_prepare
        check(not started and ini_of(g) == raw and not os.path.isfile(state_of(g)) and vl._mode(slot0) == 'vanilla',
              'the game started before Steam was asked: not started, the ini back, the saves left for restore-saves')
        game_on.clear()
        vl.restore_all(g)
        check(own(slot0) == MOD and settled(), 'and switched back once it is gone')
        vl.modfiles.game_running = lambda *_a: False
        set_store(slot0, 'vanilla', VAN2)   # the vanilla set again, as the sections below expect

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
        (vl.game_pids, vl.modfiles.game_running, vl.time.sleep, vl._on_close, vl.save_root,
         vl.first_vanilla_saves) = real[1:]
    vl.save_root = lambda: local   # for the installer checks below

    # A switch cut off after every single write / removal (the power off, the process killed), then the next start's
    # restore: every slot back to the whole mod set, every store whole, and a full round after it still intact. Three
    # slots: one with both sets, one whose sets differ in file names, one never switched (no MODE yet).
    class Cut(BaseException):
        pass

    real_write, real_unlink, real_running = vl._write, vl._unlink, vl.modfiles.game_running
    slot2 = os.path.join(user, 'saveslot02')
    MOD1, V1 = {'MAIN.GST': b'mod 1', 'MAIN.GST.edf6vc': b'rows 1'}, {'MAIN.GST': b'van 1', 'VONLY': b'only vanilla'}
    MOD2 = {'MAIN.GST': b'mod 2'}

    def base() -> None:
        import shutil
        shutil.rmtree(os.path.join(local, vl.SAVES))
        if os.path.exists(os.path.join(local, vl.ON)):
            os.remove(os.path.join(local, vl.ON))
        plant(slot0, MOD)
        set_store(slot0, 'mod', MOD)
        set_store(slot0, 'vanilla', VAN)
        plant(slot1, MOD1)
        set_store(slot1, 'vanilla', V1)
        plant(slot2, MOD2)
        for s in (slot0, slot1):
            vl._set_mode(s, 'mod')

    def cut_after(n: int, fn) -> bool:
        """fn with its (n+1)th write / removal never happening; True when it was cut."""
        done = [0]

        def gate(real):
            def op(*a):
                if done[0] == n:
                    raise Cut
                done[0] += 1
                return real(*a)
            return op
        vl._write, vl._unlink = gate(real_write), gate(real_unlink)
        try:
            fn()
            return False
        except Cut:
            return True
        finally:
            vl._write, vl._unlink = real_write, real_unlink

    def whole_mod(why: str, van0: dict, van1: dict) -> None:
        check(own(slot0) == MOD and own(slot1) == MOD1 and own(slot2) == MOD2 and settled(), f'{why}: the mod sets back')
        check(store(slot0, 'mod') == MOD and store(slot0, 'vanilla') == van0 and store(slot1, 'vanilla') == van1,
              f'{why}: the stores whole')
        vl.swap_in_saves(False)
        check(own(slot0) == van0 and own(slot1) == van1 and own(slot2) == {}, f'{why}: a full switch in after it')
        vl.swap_out_saves()
        check(own(slot0) == MOD and own(slot1) == MOD1 and own(slot2) == MOD2 and settled(), f'{why}: and out again')

    real_fsync, real_backup = os.fsync, vl.save_backup.backup
    try:   # durability and the backup are not what these cuts test (the launch above checks the backups): kept fast
        os.fsync, vl.save_backup.backup = (lambda _fd: None), (lambda *_a: None)
        vl.modfiles.game_running = lambda *_a: False
        cuts = 0
        while True:   # switching in (to the vanilla set), cut after `cuts` operations
            base()
            if not cut_after(cuts, lambda: vl.swap_in_saves(False)):
                break
            with quiet():
                vl.restore_all(g)   # the next launch / install / the plugin's restore-saves: the same swap_out_saves
            whole_mod(f'switch in cut after {cuts}', VAN, V1)
            cuts += 1
        check(cuts > 20, f'switching in: cut at each of its {cuts} operations')
        out = 0
        PLAYED0, PLAYED1 = {**VAN, 'MAIN.GST': b'played'}, {'MAIN.GST': b'played 1'}
        while True:   # switching back after a vanilla run that saved, cut after `out` operations
            base()
            vl.swap_in_saves(False)
            plant(slot0, PLAYED0)
            os.remove(os.path.join(slot1, 'VONLY'))
            plant(slot1, PLAYED1)
            if not cut_after(out, vl.swap_out_saves):
                break
            with quiet():
                vl.restore_all(g)
            whole_mod(f'switch out cut after {out}', PLAYED0, PLAYED1)
            out += 1
        check(out > 10, f'switching out: cut at each of its {out} operations')

        # the reviewed case by hand: the mod set stored, the vanilla load half done (some mod files removed, some
        # vanilla ones copied in), MODE 'mod-saved': the restore loads the mod store, which stays as it was
        base()
        vl._store(slot0, 'mod')
        vl._set_mode(slot0, 'mod-saved')
        os.remove(os.path.join(slot0, 'MAIN.GST.edf6vc'))
        plant(slot0, {'MAIN.GST': VAN['MAIN.GST']})
        vl.swap_out_saves()
        check(own(slot0) == MOD and store(slot0, 'mod') == MOD and vl._mode(slot0) == 'mod',
              'a vanilla load cut off half way: the mod set loaded back, its store untouched')
        vl.swap_in_saves(False)
        vl.swap_out_saves()
        check(own(slot0) == MOD and store(slot0, 'mod') == MOD, 'and a full round after it keeps the mod set')
    finally:
        vl._write, vl._unlink, vl.modfiles.game_running = real_write, real_unlink, real_running
        os.fsync, vl.save_backup.backup = real_fsync, real_backup

    # A STATE already there (an unrestored run): prepare refuses, the ini and STATE kept (never the vanilla ini recorded
    # as the original).
    vl.prepare(g)
    before = ini_of(g), open(state_of(g), 'rb').read()
    try:
        vl.prepare(g)
        check(False, 'prepare over an existing STATE refused')
    except RuntimeError:
        check((ini_of(g), open(state_of(g), 'rb').read()) == before, 'prepare over an existing STATE: nothing changed')
    vl.restore(g)
    check(ini_of(g) == raw, 'and the first STATE still restores the mod ini')

    # Two at once, across processes: another process holding the lock (a 原版启动 running, the installer restoring)
    # refuses a second launch, the installer and the plugin's restore-saves with nothing touched; a holder killed (no
    # release) frees it.
    import subprocess
    env = {**os.environ, 'LOCALAPPDATA': os.path.join(root, 'local'), 'PYTHONIOENCODING': 'utf-8',
           'PYTHONPATH': os.pathsep.join([os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib')])}
    hold = ('import sys, vanilla_launch as vl\n'
            'with vl.exclusive() as held:\n'
            '    print(held, flush=True)\n'
            '    sys.stdin.readline()\n')

    def holder() -> subprocess.Popen:
        p = subprocess.Popen([sys.executable, '-c', hold], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             text=True)
        check(p.stdout.readline().strip() == 'True', 'the other process holds the lock')
        return p

    real_running = vl.modfiles.game_running
    vl.modfiles.game_running = lambda *_a: False
    try:
        vl.swap_in_saves(False)   # a vanilla run on: the saves in the vanilla set, the ini vanilla
        vl.prepare(g)
        snap = lambda: (ini_of(g), os.path.isfile(state_of(g)), own(slot0), own(slot1), vl._mode(slot0),  # noqa: E731
                        os.path.exists(os.path.join(local, vl.ON)))
        was = snap()
        p = holder()
        with vl.exclusive() as held:
            check(not held, 'held by the other process: not taken here')
        started = []
        vl.os.startfile = lambda url: started.append(url)
        with quiet():
            check(vl.launch(g) == 1 and not started and snap() == was, 'a second launch: refused, nothing touched')
            check(lg.run(['restore-saves']) == vl.BUSY and snap() == was, 'restore-saves: busy, nothing touched')
            check(installer.write_game('install', g) == 1 and snap() == was, 'the installer: refused, nothing touched')
        p.stdin.write('\n')
        p.stdin.flush()
        check(p.wait(30) == 0, 'the holder released it')
        p = holder()
        p.kill()   # never released: the mutex abandoned
        p.wait(30)
        with vl.exclusive() as held:
            check(held, 'a killed holder does not lock anyone out')
        with quiet():
            check(lg.run(['restore-saves']) == 0 and own(slot0) == MOD and settled(), 'then restore-saves runs')
        vl.restore(g)
    finally:
        vl.modfiles.game_running = real_running
        if real[0] is not None:
            vl.os.startfile = real[0]

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
# the plugin, the saves not switched back: the game itself ended (a box saying why) before it reads them, not just this
# plugin unloaded: ModLoader.ini still loads the other plugins and redirects Mods, the mod game on the vanilla saves.
plugin = open(os.path.join(ROOT, 'src', 'plugin.cpp'), encoding='utf-8').read()
at = plugin.find('if(!RestoreModSaves())')
branch = plugin[at:plugin.find('return false;', at)]
check(at >= 0 and 'MessageBoxW(' in branch and 'TerminateProcess(GetCurrentProcess(),' in branch
      and branch.find('FlushLog();') < branch.find('TerminateProcess('),
      'RestoreModSaves failing ends the game (log flushed, a box) before it reads the saves')
print(f'vanilla_launch: {checks} checks OK')
