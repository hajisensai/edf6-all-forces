"""The installer's headless `loadout-generate` (tools/installer.py, run hidden by the in-game loadout menu,
src/loadout_store.cpp): it never asks, runs with the game running, makes only the variant files not there yet, each into a
temporary file renamed onto its free name (never a file overwritten or deleted, the pending list left to the plugin), one
generator at a time, and leaves a status file the plugin parses (src/loadout_file.cpp) and one machine-readable last line.
A fake game directory and a stand-in builder; with the game installed, the real builder once more in memory (Root.cpk
only read, nothing written to the game)."""
from __future__ import annotations

import builtins
import contextlib
import io
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import installer  # noqa: E402
import ledger  # noqa: E402
import menu_preview  # noqa: E402
import modfiles  # noqa: E402
import room_layout  # noqa: E402
import support_loadout as sl  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def no_prompt(*_args: object) -> str:
    raise AssertionError('loadout-generate asked for input')


INI = ('﻿; test\r\n[VehicleCrew]\r\nEnabled=1\r\nSupportPreset_SQUAD=lance@1E3A8A*2\r\n'
       'SupportVehicle_TANK_CREWED=HE,AP:25\r\n')
PENDING_NAME = 'EDF6VC_NPC_SNIPER_X_FFFFFF.SGO'
built: list[str] = []


def fake_build_one(root: str | None, game: object, file: str, out: dict[str, bytes]) -> dict[str, bytes]:
    built.append(file)
    if file.startswith('EDF6VC_LO_'):
        return {'OBJECT/' + file: b'tank ' + file.encode(), 'WEAPON/EDF6VC_AP_25.SGO': b'ap 25'}
    return {'OBJECT/' + file: b'soldier ' + file.encode()}


def make_game(root: str) -> str:
    for name in ('EDF6.exe', 'Root.cpk'):
        with open(os.path.join(root, name), 'wb') as f:
            f.write(b'stand-in')
    plugins = os.path.join(root, 'Mods', 'Plugins')
    os.makedirs(plugins)
    with open(os.path.join(plugins, 'EDF6VehicleCrew.ini'), 'wb') as f:
        f.write(INI.encode('utf-8'))
    with open(os.path.join(plugins, sl.PENDING), 'wb') as f:
        f.write((PENDING_NAME + '\r\n').encode())
    for rel in menu_preview.rels():   # the preview's menu SGOs already there (tests/menu_preview_test.py makes them)
        os.makedirs(os.path.dirname(os.path.join(root, 'Mods', rel)), exist_ok=True)
        with open(os.path.join(root, 'Mods', rel), 'wb') as f:
            f.write(b'menu')
    return root


def run_generate(game: str) -> tuple[int, str]:
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = installer.generate_main([game])
    return code, out.getvalue()


def status(game: str) -> dict[str, str]:
    text = read(os.path.join(game, 'Mods', 'Plugins', installer.GENERATE_STATUS)).decode('utf-8')
    check(text.endswith('\r\n') and not text.startswith('﻿'), 'status file: CRLF lines, no BOM')
    return dict(line.split('=', 1) for line in text.splitlines())


real_rename = os.rename
renames: list[tuple[str, str]] = []


def recording_rename(src: str, dst: str) -> None:
    renames.append((src, dst))
    real_rename(src, dst)


saved = (builtins.input, sl._build_one, modfiles.game_running, os.rename, sl.rootcpk.Game, room_layout.build)
LAYOUTS = {'UI/LYT_HUIONLINEROOM.SGO': b'room layout', 'UI/LYT_HUIHQMAIN.SGO': b'hq layout'}
room_layout.build = lambda root: dict(LAYOUTS)   # the fake game's Root.cpk is no archive (the real one: room_layout_test)
builtins.input = no_prompt
sl._build_one = fake_build_one
modfiles.game_running = lambda *a: True   # the game "running": the generator does not care
os.rename = recording_rename
sl.rootcpk.Game = lambda root, archive='Root.cpk': object()   # the fake game's Root.cpk is no archive
try:
    with tempfile.TemporaryDirectory(prefix='edf6vc-generate-') as root:
        game = make_game(root)
        obj = os.path.join(game, 'Mods', 'OBJECT')
        os.makedirs(obj)
        kept = os.path.join(obj, 'EDF6VC_NPC_LANCE_1E3A8A_X.SGO')   # the non-leader: already there, someone's bytes
        with open(kept, 'wb') as f:
            f.write(b'already here')
        pending_before = read(sl.pending_path(game))
        code, out = run_generate(game)
        lines = out.strip().splitlines()
        check(code == 0, f'ok: exit 0 (got {code}: {out})')
        check(lines[-1] == installer.generate_line('ok', 4, 0) == 'EDF6VC_GENERATE state=ok made=4 failed=0',
              'the last line is the machine-readable status')
        names = set(built)
        check(names == {'EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO', PENDING_NAME} | {n for n in names if n.startswith('EDF6VC_LO_TANK_')}
              and len(names) == 3, f'only the missing names built, the existing one not even rebuilt: {built}')
        check(read(kept) == b'already here', 'a file already there is never overwritten')
        for src, dst in renames:
            check(src == f'{dst}.tmp-{os.getpid()}', 'each file goes to a temporary name beside it, then renamed onto it')
        written = {os.path.relpath(dst, os.path.join(game, 'Mods')).replace('\\', '/').upper() for _, dst in renames}
        check({w for w in written if not w.startswith('PLUGINS/')} ==
              {'OBJECT/EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO', 'OBJECT/' + PENDING_NAME, 'WEAPON/EDF6VC_AP_25.SGO'} |
              {w for w in written if w.startswith('OBJECT/EDF6VC_LO_TANK_')} | set(LAYOUTS) | {room_layout.WHITE.upper()},
              f'the new files written: {written}')
        check(not [n for d, _, fs in os.walk(game) for n in fs if '.tmp-' in n], 'no temporary file left')
        check(read(sl.pending_path(game)) == pending_before, 'the pending list is left to the plugin (it appends while running)')
        led = ledger.Ledger(game)
        check(all('roomui' in led.owners(w) for w in [*LAYOUTS, room_layout.WHITE]) and
              all(read(led.disk(w)) == b for w, b in LAYOUTS.items()) and read(led.disk(room_layout.WHITE)) == room_layout.white_dds(),
              'the missing menu layouts and the white texture written as new files, owned by roomui in the ledger')
        check(all('loadout' in led.owners(w) for w in written if not w.startswith(('PLUGINS/', 'UI/'))),
              'the new files are the loadout tool\'s in the ledger: uninstall removes them')
        st = status(game)
        check(st['state'] == 'ok' and st['made'] == '4' and st['failed'] == '0' and st['message'], f'status ok: {st}')

        built.clear()
        renames.clear()
        before = {n: read(os.path.join(dp, n)) for dp, _, fs in os.walk(obj) for n in fs}
        code, out = run_generate(game)
        check(code == 0 and not built and out.strip().splitlines()[-1] == installer.generate_line('ok', 0, 0),
              'a second run: nothing missing, nothing built or written')
        check({n: read(os.path.join(dp, n)) for dp, _, fs in os.walk(obj) for n in fs} == before, 'every file as it was')
        check(status(game)['message'] == '没有需要生成的文件', 'said so')
        with open(led.disk('UI/LYT_HUIHQMAIN.SGO'), 'wb') as f:   # a layout already there (another mod's, or ours older)
            f.write(b'another layout')
        os.remove(led.disk('UI/LYT_HUIONLINEROOM.SGO'))
        code, out = run_generate(game)
        check(code == 0 and read(led.disk('UI/LYT_HUIHQMAIN.SGO')) == b'another layout' and
              read(led.disk('UI/LYT_HUIONLINEROOM.SGO')) == LAYOUTS['UI/LYT_HUIONLINEROOM.SGO'],
              'only the missing layout written, the one there never replaced')

        # A file made by someone else between the check and the rename: theirs stays.
        def racing_rename(src: str, dst: str) -> None:
            with open(dst, 'wb') as f:
                f.write(b'theirs')
            real_rename(src, dst)
        os.rename = racing_rename
        check(led.put_new('loadout', 'OBJECT/RACE.SGO', b'mine') is None, 'lost the race: not written')
        check(read(os.path.join(obj, 'RACE.SGO')) == b'theirs' and not os.path.exists(os.path.join(obj, 'RACE.SGO.tmp-%d' % os.getpid())),
              'the other file kept, the temporary removed')
        os.rename = recording_rename

        # One that cannot be made: the others still are; failed, named, exit 1.
        with open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), 'ab') as f:
            f.write(b'SupportPreset_PLATOON=rocket@00FF00\r\nSupportVehicle_FIGHTER=MK82:6\r\n')

        def half_build(root: str | None, g: object, file: str, out: dict[str, bytes]) -> dict[str, bytes]:
            if file.startswith('EDF6VC_LO_FIGHTER_'):
                raise FileNotFoundError('EDF6VC_JET_FIGHTER.SGO not installed')
            return fake_build_one(root, g, file, out)
        sl._build_one = half_build
        code, out = run_generate(game)
        st = status(game)
        check(code == 1 and out.strip().splitlines()[-1] == installer.generate_line('failed', 1, 1),
              f'one not made: exit 1, failed, the counts (got {code}: {out})')
        check(st['state'] == 'failed' and 'EDF6VC_LO_FIGHTER_' in st['message'] and 'not installed' in st['message'],
              f'the status names the file and why: {st}')
        check(os.path.isfile(os.path.join(obj, 'EDF6VC_NPC_ROCKET_L_00FF00_X.SGO')), 'the other one made all the same')

        # A crash inside: failed in the file, exit 1, never `running` left behind.
        def broken(*_a: object, **_k: object) -> dict[str, bytes]:
            raise ValueError('bad SGO')
        sl._build_one = broken
        with open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), 'ab') as f:
            f.write(b'SupportPreset_SQUAD_F=rifle@123456\r\n')
        saved_build_named = sl.build_named
        sl.build_named = broken
        with contextlib.redirect_stderr(io.StringIO()):
            code, out = run_generate(game)
        sl.build_named = saved_build_named
        check(code == 1 and status(game)['state'] == 'failed' and 'bad SGO' in status(game)['message'],
              'an error: failed with its text, not running')
        sl._build_one = fake_build_one

        # One at a time.
        with installer.generate_lock(game) as held:
            check(held, 'the lock taken')
            code, out = run_generate(game)
            check(code == 3 and out.strip().splitlines()[-1].startswith('EDF6VC_GENERATE state=busy'), 'a second generator: busy, exit 3')
        check(status(game)['state'] == 'failed', 'the busy one leaves the status file alone')

        # Not a game / bad usage: exit 2, nothing written.
        with tempfile.TemporaryDirectory(prefix='edf6vc-notgame-') as other:
            code, out = run_generate(other)
            check(code == 2 and 'state=failed' in out and not os.listdir(other), 'not a game: exit 2, nothing written')
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            check(installer.generate_main([]) == 2 and 'usage' in out.getvalue(), 'no game argument: exit 2, usage')

        # The frozen exe's entry: no banner prompt, no "press Enter" wait.
        sys.frozen = True   # type: ignore[attr-defined]
        argv = sys.argv
        sys.argv = ['EDF6VehicleCrew安装器.exe', 'loadout-generate', game]
        try:
            with contextlib.redirect_stdout(io.StringIO()) as out:
                code = installer.run()
        finally:
            sys.argv = argv
            del sys.frozen
        check(code == 0 and out.getvalue().strip().splitlines()[-1].startswith('EDF6VC_GENERATE state=ok'),
              'run() dispatches loadout-generate and returns without asking')

        # The generator copy install leaves beside the plugin.
        exe = os.path.join(root, 'fake_installer.exe')
        with open(exe, 'wb') as f:
            f.write(b'MZ installer')
        dst = installer.install_generator(game, exe)
        check(dst == os.path.join(game, 'Mods', 'Plugins', installer.GENERATOR) and read(dst) == b'MZ installer', 'copied')
        check(installer.install_generator(game, exe) is None, 'unchanged: not rewritten')
        check(installer.install_generator(game, None) is None, 'from source: none')
        with contextlib.redirect_stdout(io.StringIO()):
            installer.remove_generator(game)
        check(not [n for n in installer.GENERATOR_FILES if os.path.exists(os.path.join(game, 'Mods', 'Plugins', n))],
              "uninstall's remove_generator: the copy, its status file and its lock gone")
finally:
    builtins.input, sl._build_one, modfiles.game_running, os.rename, sl.rootcpk.Game, room_layout.build = saved

undo = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read().split('\ndef uninstall(', 1)[1]
check('remove_generator(game, lock=False)' in undo.split('\ndef ', 1)[0],
      'uninstall removes the generator (its lock file after the flow lets go of it: write_game)')
store = open(os.path.join(ROOT, 'src', 'loadout_store.cpp'), encoding='utf-8').read()
check(f'L"{installer.GENERATOR}"' in store and f'L"{installer.GENERATE_STATUS}"' in store, 'the plugin looks for these names')
check('L"loadout-generate \\"%ls\\""' in store, 'the plugin runs this subcommand with the game directory')
parser = open(os.path.join(ROOT, 'src', 'loadout_file.cpp'), encoding='utf-8').read()
check(set(re.findall(r'ValueOf\(line,"(\w+)"\)', parser)) == {'state', 'made', 'failed', 'message'} and
      all(f'"{s}"' in parser for s in ('running', 'ok', 'failed')), 'the plugin parses the fields and states written here')

# With the game: the real builder for one missing name, in memory (Root.cpk only read; nothing written).
try:
    import rootcpk
    real = rootcpk.default()
except Exception as e:   # no game on this machine (CI)
    print(f'loadout_generate_test: no game ({e}); the real builder not run')
    real = None
if real is not None:
    files = sl.build_named(None, ['EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO'], real)
    check(list(files) == ['OBJECT/EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO'] and files['OBJECT/EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO'][:4],
          'the real builder makes a missing coloured soldier from Root.cpk')
print(f'loadout_generate_test: {checks} checks passed')
