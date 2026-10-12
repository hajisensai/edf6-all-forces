"""The review fixes of the room loadout menu on the installer side (docs/feature-2026-10-12-room-loadout-menu.md 阶段 R):
  - the ledger (pylib/ledger.py) merges what an object changed into the file as it is when it saves: an object loaded
    before another process (the background generator) saved never writes that process's entries back out;
  - every installer flow that writes Mods holds the generator's lock (waits for a running generator, then refuses with
    nothing written), and the generator refuses while an installer flow holds it;
  - the room layouts: any failure building or writing them is said and the install goes on;
  - the generated variant files no ini / pending entry names any more are removed (ledger-owned only), in install and
    offered in menu 8, never while the game runs.
A fake game directory; nothing of the real game is read or written."""
from __future__ import annotations

import builtins
import contextlib
import io
import json
import os
import re
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import installer  # noqa: E402
import ledger  # noqa: E402
import loadout_generator  # noqa: E402
import modfiles  # noqa: E402
import support_loadout as sl  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def make_game(root: str, ini: str = '') -> str:
    game = os.path.join(root, 'game')
    os.makedirs(os.path.join(game, 'Mods', 'Plugins'))
    os.makedirs(os.path.join(game, 'Mods', 'OBJECT'))
    with open(os.path.join(game, 'EDF6.exe'), 'wb') as f:
        f.write(b'MZ')
    with open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), 'w', encoding='utf-8') as f:
        f.write('[VehicleCrew]\r\nEnabled=1\r\n' + ini)
    return game


def on_disk(game: str) -> dict:
    with open(os.path.join(game, 'Mods', ledger.MANIFEST), encoding='utf-8') as f:
        return json.load(f)['files']


@contextlib.contextmanager
def quiet():
    with contextlib.redirect_stdout(io.StringIO()) as out:
        yield out


# ---------------------------------------------------------------- the ledger: merged on save
with tempfile.TemporaryDirectory(prefix='edf6vc-ledger-') as temp:
    game = make_game(temp)
    a = ledger.Ledger(game)                          # the installer loads the ledger...
    b = ledger.Ledger(game)                          # ...the generator (another process) loads it too
    b.put_new('loadout', 'OBJECT/EDF6VC_NPC_RIFLE_1E3A8A_X.SGO', b'gen')
    a.put('jets', 'OBJECT/JET.SGO', b'jet')         # a's snapshot predates b's save
    files = on_disk(game)
    check(set(files) == {'OBJECT/EDF6VC_NPC_RIFLE_1E3A8A_X.SGO', 'OBJECT/JET.SGO'},
          'a stale object\'s save keeps what another process saved meanwhile')
    check(set(a.files) == set(files), 'the object follows the file after saving')
    c = ledger.Ledger(game)
    b.need('optics', 'OBJECT/JET.SGO')               # b never saw JET.SGO: it re-reads before the change
    c.release('jets', ['OBJECT/JET.SGO'])            # c drops jets: optics (added since c loaded) still needs it
    files = on_disk(game)
    check(files['OBJECT/JET.SGO']['owners'] == ['optics'] and os.path.isfile(os.path.join(game, 'Mods', 'OBJECT', 'JET.SGO')),
          'an owner another process added meanwhile is kept: the file stays')
    # A tool editing .files directly then _save (make_optics, make_proteus): merged the same way.
    d = ledger.Ledger(game)
    ledger.Ledger(game).put('sub', 'OBJECT/SUB.SGO', b'sub')
    d.files['OBJECT/EXTRA.SGO'] = {'owners': ['proteus'], 'sha': 'x'}
    d._save()
    check({'OBJECT/SUB.SGO', 'OBJECT/EXTRA.SGO', 'OBJECT/JET.SGO'} <= set(on_disk(game)), 'a direct edit merged too')
    # Everything released by a stale object: the file goes only when nothing is left.
    e = ledger.Ledger(game)
    ledger.Ledger(game).put('katyusha', 'OBJECT/K.SGO', b'k')
    for owner in ('loadout', 'optics', 'sub', 'proteus'):
        e.release(owner)
    check(set(on_disk(game)) == {'OBJECT/K.SGO'}, 'releasing all of a stale view leaves the later entry')
    ledger.Ledger(game).release('katyusha')
    check(not os.path.isfile(os.path.join(game, 'Mods', ledger.MANIFEST)), 'nothing left: the ledger file removed')
    m = ledger.merge_files({'A': {'owners': ['x'], 'sha': '1'}}, {'A': {'owners': ['x', 'y'], 'sha': '2'}},
                           {'A': {'owners': ['x', 'z'], 'sha': '1'}, 'B': {'owners': ['w'], 'sha': '3'}})
    check(m == {'A': {'owners': ['x', 'z', 'y'], 'sha': '2'}, 'B': {'owners': ['w'], 'sha': '3'}},
          'merge: my added owner and sha onto the disk entry, theirs kept, untouched entries as on disk')

# ---------------------------------------------------------------- one writer at a time: the generator's lock
with tempfile.TemporaryDirectory(prefix='edf6vc-lock-') as temp:
    game = make_game(temp)
    loadout_generator.LOCK_WAIT_S = 0.6
    said: list[str] = []
    with loadout_generator.generate_lock(game) as held:   # a background generator running (it outlives the game)
        check(held, 'the generator holds the lock')
        with loadout_generator.wait_generate_lock(game, say=said.append, poll=0.1) as got:
            check(not got and len(said) == 2 and '等它结束' in said[0] and '没有写入任何文件' in said[1],
                  'an installer flow waits, says so once, then refuses')
        called: list[str] = []
        real_install, real_uninstall = installer.install, installer.uninstall
        installer.install = lambda g, *a, **k: called.append('install')
        installer.uninstall = lambda g: called.append('uninstall')
        try:
            with quiet():
                codes = [installer.write_game(mode, game) for mode in ('install', 'uninstall')]
        finally:
            installer.install, installer.uninstall = real_install, real_uninstall
        check(codes == [1, 1] and not called, 'install / uninstall refused while the generator runs: nothing written')
        old_input = builtins.input
        builtins.input = lambda *_: (_ for _ in ()).throw(AssertionError('asked'))
        real_edit = sl.edit
        sl.edit = lambda text, ask: text + 'SupportPreset_SQUAD=rifle\r\n'
        try:
            with quiet():
                code = installer.manage_support(game, loadout_only=True)
        finally:
            sl.edit = real_edit
            builtins.input = old_input
        ini = open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), encoding='utf-8').read()
        check(code == 1 and 'SupportPreset_SQUAD' not in ini, 'menu 8 saves nothing while the generator runs')
    # Released mid-wait: the flow goes on.
    holder_ready, release = threading.Event(), threading.Event()

    def hold() -> None:
        with loadout_generator.generate_lock(game):
            holder_ready.set()
            release.wait(5)
    t = threading.Thread(target=hold)
    t.start()
    holder_ready.wait(5)
    threading.Timer(0.2, release.set).start()
    loadout_generator.LOCK_WAIT_S = 5
    with loadout_generator.wait_generate_lock(game, say=lambda *_: None, poll=0.05) as got:
        check(got, 'the generator finishing within the wait: the flow takes the lock')
        with loadout_generator.generate_lock(game) as other:
            check(not other, 'and a generator started meanwhile does not run beside it (it exits busy)')
    t.join()
    with loadout_generator.generate_lock(game) as held:
        check(held, 'let go after the flow')
    src = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
    body = src.split('def write_game(', 1)[1].split('\ndef ', 1)[0]
    check('with wait_generate_lock(game) as held:' in body and body.index('wait_generate_lock') < body.index('install if mode'),
          'install, uninstall and the campaign flow run under the lock')
    support = src.split('def manage_support(', 1)[1].split('\ndef ', 1)[0]
    check(support.index('wait_generate_lock') < support.index('modfiles.atomic_write(path'), 'menu 7 / 8 write under the lock')
    check("remove_generator(game, lock=False)" in src, 'uninstall does not delete the lock file it holds')

# ---------------------------------------------------------------- the room layouts: a failure never stops the install
import room_layout  # noqa: E402
with tempfile.TemporaryDirectory(prefix='edf6vc-layout-') as temp:
    game = make_game(temp)
    real_build, real_install = room_layout.build, room_layout.install
    try:
        for error in (TypeError('bad value'), NotImplementedError('compression'), RuntimeError('x'), MemoryError()):
            def boom(*_a, error=error):
                raise error
            room_layout.build = boom
            with quiet() as out:
                got = installer.build_room_layouts(game)
            check(got is None and type(error).__name__ in out.getvalue() and '画面布局没有生成' in out.getvalue(),
                  f'building the layouts raising {type(error).__name__}: said, None, the install goes on')
            room_layout.install = boom
            with quiet() as out:
                ok = installer.write_room_layouts(game, {'UI/LYT_HUIHQMAIN.SGO': b'x'})
            check(not ok and '画面布局没有写入' in out.getvalue(),
                  f'writing them raising {type(error).__name__}: said, False, the install goes on')
    finally:
        room_layout.build, room_layout.install = real_build, real_install
    src = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
    inst = src.split('\ndef install(', 1)[1].split('\ndef ', 1)[0]
    check('write_room_layouts(game, room)' in inst and 'room_layout.install(' not in inst,
          'install writes the layouts through the guarded writer')
    check('except Exception as e' in src.split('def build_room_layouts(', 1)[1].split('\ndef ', 1)[0],
          'every failure building them is caught')

# ---------------------------------------------------------------- stale generated variant files
with tempfile.TemporaryDirectory(prefix='edf6vc-stale-') as temp:
    wanted_npc = 'EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO'
    tank = sl.vehicle_file(sl.parse_vehicle('HE,AP:25', 'TANK'))
    game = make_game(temp, 'SupportPreset_SQUAD=lance@1E3A8A*2\r\nSupportVehicle_TANK_CREWED=HE,AP:25\r\n')
    pending_npc = 'EDF6VC_NPC_SNIPER_X_FFFFFF.SGO'
    with open(sl.pending_path(game), 'w', encoding='utf-8') as f:
        f.write(pending_npc + '\r\n')
    led = ledger.Ledger(game)
    stale_names = ['EDF6VC_NPC_RIFLE_123456_X.SGO', 'EDF6VC_NPC_ROCKET_X_654321.SGO']
    for name in [wanted_npc, tank, pending_npc] + stale_names:
        led.put('loadout', 'OBJECT/' + name, name.encode())
    led.put('loadout', 'WEAPON/EDF6VC_AP_25.SGO', b'ap')
    led.put('jets', 'OBJECT/EDF6VC_NPC_SHOTGUN_ABCDEF_X.SGO', b'shared')
    led.need('loadout', 'OBJECT/EDF6VC_NPC_SHOTGUN_ABCDEF_X.SGO')
    foreign = os.path.join(game, 'Mods', 'OBJECT', 'EDF6VC_NPC_FLAME_111111_X.SGO')   # not in the ledger: not ours
    with open(foreign, 'wb') as f:
        f.write(b'someone else')
    text = open(os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), encoding='utf-8').read()
    got = sl.stale(game, text, sl.read_pending(game))
    check(got == sorted(['OBJECT/' + n for n in stale_names] + ['OBJECT/EDF6VC_NPC_SHOTGUN_ABCDEF_X.SGO']),
          'stale: this tool\'s files the ini and the pending list do not name (the wanted ones, the pending one and the '
          'tank\'s pylon weapon kept)')
    real_running = modfiles.game_running
    modfiles.game_running = lambda *_: True
    try:
        with quiet():
            check(installer.cleanup_variants(game) == 0 and installer.offer_variant_cleanup(game) == 0,
                  'the game running: nothing removed, nothing offered')
    finally:
        modfiles.game_running = real_running
    modfiles.game_running = lambda *_: False
    old_ask = installer.ask
    try:
        installer.ask = lambda *_: 'n'
        with quiet():
            check(installer.offer_variant_cleanup(game) == 0, 'menu 8 offers; declined: nothing removed')
        installer.ask = lambda *_: 'y'
        with quiet():
            removed = installer.offer_variant_cleanup(game)
    finally:
        installer.ask = old_ask
        modfiles.game_running = real_running
    disk = os.path.join(game, 'Mods')
    check(removed == 2 and not any(os.path.isfile(os.path.join(disk, 'OBJECT', n)) for n in stale_names),
          'accepted: the two stale files deleted')
    check(all(os.path.isfile(os.path.join(disk, 'OBJECT', n)) for n in (wanted_npc, tank, pending_npc)) and
          os.path.isfile(os.path.join(disk, 'WEAPON', 'EDF6VC_AP_25.SGO')), 'the named ones and the pylon weapon kept')
    check(os.path.isfile(os.path.join(disk, 'OBJECT', 'EDF6VC_NPC_SHOTGUN_ABCDEF_X.SGO')) and
          ledger.Ledger(game).owners('OBJECT/EDF6VC_NPC_SHOTGUN_ABCDEF_X.SGO') == ['jets'],
          'one another tool also owns: kept, only this tool\'s claim dropped')
    check(os.path.isfile(foreign), 'a file the ledger does not give this tool: never touched')
    src = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
    wl = src.split('def write_loadout_files(', 1)[1].split('\ndef ', 1)[0]
    check(wl.index('cleanup_variants(game)') < wl.index('clear_pending'), 'install cleans before the pending list is cleared')

print(f'installer_lock_test: {checks} checks passed')
