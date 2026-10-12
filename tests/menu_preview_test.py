"""The room / HQ 3D preview's menu SGOs (tools/menu_preview.py): install / install_new / remove through the ledger on a
fake game directory, the installer's wiring (install, uninstall, loadout-generate), and with the game installed the files
built from Root.cpk (only read; nothing written to the game) parsed back: the n601 body, everything else the stock
P505_RANGER.SGO's, the leader's change_color naming materials the model has."""
from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import installer  # noqa: E402
import ledger  # noqa: E402
import menu_preview as mp  # noqa: E402
import sgo  # noqa: E402

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


FAKE = {rel: b'menu ' + rel.encode() for rel in mp.rels()}


def check_names() -> None:
    check(mp.FILES == ('EDF6VC_MENU_RANGER.SGO', 'EDF6VC_MENU_RANGER_L.SGO'), 'the file names')
    check(mp.rels() == ['MENUOBJECT/EDF6VC_MENU_RANGER.SGO', 'MENUOBJECT/EDF6VC_MENU_RANGER_L.SGO'], 'the Mods paths')
    check(mp.MENU_PATHS == {'EDF6VC_MENU_RANGER.SGO': 'app:/MENUOBJECT/EDF6VC_MENU_RANGER.SGO',
                            'EDF6VC_MENU_RANGER_L.SGO': 'app:/MENUOBJECT/EDF6VC_MENU_RANGER_L.SGO'}, 'the paths the plugin loads')
    check(mp.OWNER in ledger.OWNERS, 'a ledger owner')


def check_install(game: str) -> None:
    written = mp.install(game, FAKE)
    check(len(written) == 2 and all(read(p) == FAKE[r] for p, r in zip(written, FAKE)), 'install writes both')
    led = ledger.Ledger(game)
    check(all(led.owners(r) == [mp.OWNER] for r in FAKE), 'recorded as the tool\'s')
    stamps = [os.stat(p).st_mtime_ns for p in written]
    check(mp.install(game, FAKE) == [] and [os.stat(p).st_mtime_ns for p in written] == stamps, 'unchanged: nothing rewritten')
    check(mp.missing(game) == [] and mp.install_new(game, {r: b'other' for r in FAKE}) == [], 'install_new: names taken')
    check(all(read(p) == FAKE[r] for p, r in zip(written, FAKE)), 'install_new never overwrites')
    deleted, kept = mp.remove(game)
    check(sorted(deleted) == sorted(written) and not kept and not any(os.path.exists(p) for p in written), 'remove deletes')
    check(not os.path.exists(os.path.join(game, 'Mods', ledger.MANIFEST)), 'nothing left in the ledger')
    check(len(mp.install_new(game, FAKE)) == 2 and mp.missing(game) == [], 'install_new writes free names')
    mp.remove(game)


def check_generate(game: str) -> None:
    import rootcpk
    calls: list[str] = []
    saved = mp.build, rootcpk.Game
    rootcpk.Game = lambda root, archive='Root.cpk': object()   # the fake game has no Root.cpk

    def fake_build(_g: object) -> dict[str, bytes]:
        calls.append('build')
        return dict(FAKE)
    mp.build = fake_build
    try:
        errors: dict[str, str] = {}
        check(len(installer.generate_menu_preview(game, errors)) == 2 and not errors, 'loadout-generate makes them')
        check(installer.generate_menu_preview(game, errors) == [] and calls == ['build'], 'there already: not even built')
        mp.remove(game)
        mp.build = lambda _g: (_ for _ in ()).throw(ValueError('bad'))
        check(installer.generate_menu_preview(game, errors) == [] and 'bad' in errors.get(mp.RANGER, ''), 'failure reported')
        with contextlib.redirect_stdout(io.StringIO()) as out:
            check(installer.write_menu_preview(game) is False, 'install: a failure returns False')
        check('ValueError: bad' in out.getvalue() and mp.missing(game) == mp.rels(), 'said why, nothing written')
    finally:
        mp.build, rootcpk.Game = saved


def check_wiring() -> None:
    # install / uninstall live in the installer; loadout-generate's body in the slim generator (tools/loadout_generator.py).
    src = read(os.path.join(ROOT, 'tools', 'installer.py')).decode('utf-8')
    gen = read(os.path.join(ROOT, 'tools', 'loadout_generator.py')).decode('utf-8')
    body = {name: src.split(f'\ndef {name}(', 1)[1].split('\ndef ', 1)[0] for name in ('install', 'uninstall')}
    body['generate_variants'] = gen.split('\ndef generate_variants(', 1)[1].split('\ndef ', 1)[0]
    check('write_menu_preview(game)' in body['install'], 'install writes them')
    check('menu_preview.remove' in body['uninstall'], 'uninstall removes them')
    check('generate_menu_preview(game, errors)' in body['generate_variants'], 'loadout-generate makes missing ones')


def stock_members(game: object) -> dict:
    return sgo.read(game.read('MENUOBJECT', mp.STOCK_RANGER))[1]


def check_real(game: object) -> None:
    files = mp.build(game)
    check(list(files) == mp.rels() and files == mp.build(game), 'both built, the same bytes every time')
    stock = stock_members(game)
    every = mp.colourable(game, 'app:/Object/n601_common_ranger.mrab', 'n601_common_ranger.mdb')
    check(every == {'p609_proto_ranger_common_body', 'p609_proto_ranger_common_head'}, f'colourable materials: {every}')
    for rel, data in files.items():
        version, members = sgo.read(data)
        check(sgo.write_depth_first(version, members) == data, f'{rel}: parses back exactly')
        check(members['animation_model'][0] == ['app:/Object/n601_common_ranger.mrab', 'n601_common_ranger.mdb'],
              f'{rel}: the n601 body')
        check(members['animation_model'][1:] == stock['animation_model'][1:], f'{rel}: the stock menu CAS')
        check({k: v for k, v in members.items() if k not in ('animation_model', 'change_color')} ==
              {k: v for k, v in stock.items() if k not in ('animation_model', 'change_color')}, f'{rel}: the rest stock')
        check([c[1] for c in members['change_color']] == list(mp.CHANNELS), f'{rel}: colour 0 / 1 in order')
    plain = sgo.read(files[mp.rels()[0]])[1]['change_color']
    leader = sgo.read(files[mp.rels()[1]])[1]['change_color']
    check(plain == stock['change_color'] == [[0, 'change_color0'], [0, 'change_color1']], 'plain: every material')
    check([c[0] for c in leader] == ['p609_proto_ranger_common_head'] * 2 and {c[0] for c in leader} <= every,
          f'leader: the head material the _LEADER templates colour: {leader}')
    picked = mp.leader_files(game)
    check({n for n, f in picked.items() if f == mp.RANGER_LEADER} ==
          {f'N601_COMMON_RANGER_{k}_LEADER' for k in ('FL', 'RL', 'SG', 'SN')}, f'which leaders use _L: {picked}')
    check(picked['N601_COMMON_RANGER_AF_LEADER'] == mp.RANGER, 'AF leader colours head and body: every material')


def main() -> int:
    check_names()
    check_wiring()
    with tempfile.TemporaryDirectory(prefix='edf6vc-menu-') as game:
        check_install(game)
        check_generate(game)
    try:
        import rootcpk
        real = rootcpk.default()
    except Exception as e:   # no game on this machine (CI)
        print(f'menu_preview_test: no game ({e}); the real builder not run')
        real = None
    if real is not None:
        check_real(real)
    print(f'menu_preview_test: {checks} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
