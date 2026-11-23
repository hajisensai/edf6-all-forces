"""The unified model files (tools/menu_preview.py, pylib/menu_model.py, docs/unified-model-re.md): one file per unit
that both the battle loader and the room / HQ 3D preview's menu renderer read.

Offline: the names the plugin builds (src/room_preview_logic.h kSoldierTemplates / kJetMenuCas / kMenuModelKey) match the
tool's; install / install_new / remove through the ledger on a fake game directory, the old menu-only files released;
the installer's wiring; menu_model's refusals on synthetic files (a missing key, a CAS without `main`, a battle key moved,
a key colliding with another value).

With the game installed (Root.cpk only read; nothing written to the game): every soldier template's unified file, a
coloured / packed support_loadout variant, every plugin jet and a loaded tank have every key the menu needs, a menu CAS
with an anmgroup named exactly `main`, and every key the battle reads equal to the battle file's."""
from __future__ import annotations

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
import menu_model as mm  # noqa: E402
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


def raises(fn, *args) -> str:
    try:
        fn(*args)
    except ValueError as e:
        return str(e)
    return ''


FAKE = {rel: b'unified ' + rel.encode() for rel in mp.rels()}


def check_names() -> None:
    import support_loadout
    names = mp.template_names()
    check(len(names) == 28 and len(set(names)) == 28, f'14 templates and their _LEADER: {len(names)}')
    check(mp.rels() == [f'OBJECT/{n}.SGO' for n in names] + ['OBJECT/V504_BEGARUTA.SGO', 'MENUOBJECT/EDF6VC_MENU_V506_HELI.CAS'], 'the Mods paths')
    check(mp.OWNER in ledger.OWNERS, 'a ledger owner')
    hdr = read(os.path.join(ROOT, 'src', 'room_preview_logic.h')).decode('utf-8')
    table = hdr.split('kSoldierTemplates[kSupportWeaponCount]={', 1)[1].split('};', 1)[0]
    listed = re.findall(r'L"([^"]*)"', table)
    want = [t if support_loadout.has_looks(n) else 'N601_COMMON_RANGER_AF' for n, _, t in support_loadout.KINDS]
    check(listed == want, f'kSoldierTemplates: KINDS in SupportWeapon order, the X-Morph infantry as the rifle: {listed}')
    check('kJetMenuCas=L"' + mp.jet_cas_rel().replace('/', '\\\\') + '"' in hdr, 'kJetMenuCas the tool\'s jet menu CAS')
    check(f'kMenuModelKey=L"{mm.MENU_MODEL}"' in hdr, 'the key the hook reads is the one the tool writes')
    check(mm.menu_cas_path(mm.JET_CAS) == 'app:/menuobject/edf6vc_menu_v506_heli.cas', 'the path jets name')
    loadout = read(os.path.join(ROOT, 'src', 'support_loadout.h')).decode('utf-8')
    for body, (base, *_rest) in support_loadout.BODIES.items():
        if body not in ('TANK', 'MECH'):
            check(f'L"{body}",L"{base}"' in loadout, f'{body}: kVehicleBodies base {base} (VehicleFileOf names it)')


def check_install(game: str) -> None:
    led = ledger.Ledger(game)
    old = 'MENUOBJECT/EDF6VC_MENU_RANGER.SGO'   # a menu-only file the tool wrote before
    old_path = led.put(mp.OWNER, old, b'old menu sgo')
    written = mp.install(game, FAKE)
    check(len(written) == len(FAKE) and all(read(p) == FAKE[r] for p, r in zip(written, FAKE)), 'install writes them all')
    check(not os.path.exists(old_path) and ledger.key(old) not in ledger.Ledger(game).owned_by(mp.OWNER),
          'the old menu-only file released')
    led = ledger.Ledger(game)
    check(all(led.owners(r) == [mp.OWNER] for r in FAKE), "recorded as the tool's")
    stamps = [os.stat(p).st_mtime_ns for p in written]
    check(mp.install(game, FAKE) == [] and [os.stat(p).st_mtime_ns for p in written] == stamps, 'unchanged: nothing rewritten')
    check(mp.missing(game) == [] and mp.install_new(game, {r: b'other' for r in FAKE}) == [], 'install_new: names taken')
    check(all(read(p) == FAKE[r] for p, r in zip(written, FAKE)), 'install_new never overwrites')
    deleted, kept = mp.remove(game)
    check(sorted(deleted) == sorted(written) and not kept and not any(os.path.exists(p) for p in written), 'remove deletes')
    check(not os.path.exists(os.path.join(game, 'Mods', ledger.MANIFEST)), 'nothing left in the ledger')
    check(len(mp.install_new(game, FAKE)) == len(FAKE) and mp.missing(game) == [], 'install_new writes free names')
    mp.remove(game)


def check_generate(game: str) -> None:
    import rootcpk
    calls: list[str] = []
    saved = mp.build, rootcpk.Game
    rootcpk.Game = lambda root, archive='Root.cpk': object()   # the fake game has no Root.cpk

    def fake_build(_g: object, *_root: object) -> dict[str, bytes]:
        calls.append('build')
        return dict(FAKE)
    mp.build = fake_build
    try:
        errors: dict[str, str] = {}
        check(len(installer.generate_menu_preview(game, errors)) == len(FAKE) and not errors, 'loadout-generate makes them')
        check(installer.generate_menu_preview(game, errors) == [] and calls == ['build'], 'there already: not even built')
        mp.remove(game)
        mp.build = lambda _g, *_r: (_ for _ in ()).throw(ValueError('bad'))
        check(installer.generate_menu_preview(game, errors) == [] and 'bad' in errors.get(mp.OWNER, ''), 'failure reported')
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
    wmp = src.split('\ndef write_menu_preview(', 1)[1].split('\ndef ', 1)[0]
    check('menu_preview.build(rootcpk.Game(game)' in wmp, 'install builds them from Root.cpk')
    check('menu_preview.remove' in body['uninstall'], 'uninstall removes them')
    check('generate_menu_preview(game, errors)' in body['generate_variants'], 'loadout-generate makes missing ones')
    # No menu-only model file path is left (docs/unified-model-re.md: no dual path, no fallback).
    for path in ('tools/menu_preview.py', 'tools/installer.py', 'src/room_preview.cpp', 'src/room_preview_logic.h'):
        text = read(os.path.join(ROOT, *path.split('/'))).decode('utf-8')
        for gone in ('EDF6VC_MENU_RANGER', 'EDF6VC_MENU_V_', 'P505_RANGER.SGO"', 'MenuPathOf', 'VehicleMenuOf'):
            check(gone not in text, f'{path}: {gone} gone')


def synthetic() -> tuple[int, dict]:
    """A battle-file-like classic SGO (animation_model and a few battle keys)."""
    return 0x102, {'animation_model': [['app:/object/x.mrab', 'x.mdb'], 'app:/object/x.cas', 'app:/object/x.mab'],
                   'soldier_weapon_slot': [[[0.0], 'weapon', 0, 0]], 'durability': mm._f(100.0)}


def check_model_offline() -> None:
    version, battle = synthetic()
    before = sgo.write_depth_first(version, battle)
    stock = {'animation_model': [['a', 'b'], 'app:/menuobject/edf6armysoldier.cas', 'app:/menuobject/m.mab'],
             'change_color': [[0, 'change_color0'], [0, 'change_color1']], 'random_animation': [['idle', 1.0]],
             'soldier_weapon_slot': [[[0.0], 'weapon', 0, 0]], 'soldier_weapon_grab_adjust': [[0, 0, 0], [0, 0, 0]]}
    keys = mm.soldier_menu_keys(stock, 'app:/object/x.mrab', 'x.mdb', [mm.EVERY, 'head'])
    check(keys[mm.MENU_MODEL] == [['app:/object/x.mrab', 'x.mdb'], stock['animation_model'][1], stock['animation_model'][2]],
          "the menu model: the battle file's body, the stock menu CAS / MAB")
    check(keys['change_color'] == [[0, 'change_color0'], ['head', 'change_color1']], 'selectors one a channel')
    after = sgo.write_depth_first(version, mm.add_sgo(battle, keys))
    check(mm.battle_equal(before, after) == [], 'only menu keys added: every battle key equal')
    # Offline the CAS bytes are stand-ins: b'CAS' has the menu's `main`, b'HELI' only the 506's `Main` (#50).
    groups = mm.cas_groups
    mm.cas_groups = lambda cas: {b'CAS': ['weapon', 'main'], b'HELI': ['Roter', 'Main']}[cas]
    has_main = lambda _p: b'CAS'  # noqa: E731
    check(mm.menu_problems(after, has_main) == [], 'a unified file')
    heli = mm.menu_problems(after, lambda _p: b'HELI')
    check(heli == [f"菜单 CAS {stock['animation_model'][1]} 没有名为 'main' 的动画组（['Roter', 'Main']）：渲染器会拿 -1 当下标"],
          f'a menu CAS without `main`: refused (the -1 index) {heli}')
    missing = mm.menu_problems(before, has_main)
    check(missing == [f'缺 {k}' for k in (mm.MENU_MODEL, 'change_color', 'random_animation')], f'battle-only: {missing}')
    moved = dict(mm.add_sgo(battle, keys))
    moved['durability'] = mm._f(1.0)
    check(mm.battle_equal(before, sgo.write_depth_first(version, moved)) == ['durability'], 'a battle key moved: named')
    clash = dict(battle, change_color=[[0, 'change_color0']])
    check('冲突' in raises(mm.add_sgo, clash, keys), 'a key there with another value: refused, never overwritten')
    check(mm.add_sgo(mm.add_sgo(battle, keys), keys) == mm.add_sgo(battle, keys), 'adding again: the same file')
    no_main = mm.menu_problems(after, lambda _p: None)
    check(no_main == [f'菜单 CAS {stock["animation_model"][1]} 不存在'], f'the menu CAS nowhere: refused {no_main}')
    bad = dict(mm.add_sgo(battle, keys))
    bad[mm.MENU_MODEL] = ['x.cas']
    check(mm.menu_problems(sgo.write_depth_first(version, bad), has_main) == [f'{mm.MENU_MODEL} 不是 [[archive, model], CAS, MAB]'],
          'a malformed menu model refused')
    mm.cas_groups = groups


def check_real(game: object, game_dir: str) -> None:
    import dsgo
    import support_loadout
    import vcobjects
    files = mp.build(game)
    check(list(files) == mp.rels() and files == mp.build(game), 'every file built, the same bytes every time')
    jet_cas = files[mp.jet_cas_rel()]
    heli = game.read('OBJECT', 'V506_HELI.CAS')
    check([n for _, n in mp.anmgroups(heli)] == ['Roter', 'Main'], "the jets' CAS as the game ships it (no `main`: #50)")
    check([n for _, n in mp.anmgroups(jet_cas)] == ['Roter', 'main'] and len(jet_cas) == len(heli) and
          sum(a != b for a, b in zip(jet_cas, heli)) == 1, 'the jet menu CAS: only that name lower-cased, in place')
    check(mp.menu_cas(game.read('OBJECT', 'V505_TANK.CAS')) is None, "the tank's CAS has `main`: used as it is")

    import jet_models
    built = {f'app:/object/{c.lower()}': m for m, c in jet_models.ANIMATIONS.items()}

    def own_cas(cas: str) -> bytes | None:
        """A jet CAS as its tool makes it: the 506's from Root.cpk, the carrier's / drone's built here, an X-Morph
        jet's from the game's Mods (built from the model pack at install; None: that jet is not installed)."""
        if cas == mm.JET_CAS:
            return game.read(*mp.cas_ref(cas))
        if cas in built:
            return jet_models.animation(game, built[cas])
        path = os.path.join(game_dir, 'Mods', *mp.cas_ref(cas))
        return read(path) if os.path.isfile(path) else None
    sources = {c: own_cas(c) for c in mp.jet_cas_sources()}
    for cas, source in sources.items():
        if source is not None:
            made = mp.menu_cas(source)
            check(made is not None and [n for _, n in mp.anmgroups(made)] == ['Roter', 'main'],
                  f'{cas}: a heli retarget (`Main`), its menu copy has `main`')

    def read_cas(path: str) -> bytes | None:
        for cas, source in sources.items():
            if path.lower() == mm.menu_cas_path(cas):
                return None if source is None else mp.menu_cas(source)
        try:
            return game.read(*mp.cas_ref(path))
        except KeyError:
            return None
    check(mm.menu_problems(sgo.write_depth_first(*synthetic_jet(game)), read_cas)[-1:] ==
          [f"菜单 CAS {mm.JET_CAS} 没有名为 'main' 的动画组（['Roter', 'Main']）：渲染器会拿 -1 当下标"],
          'a jet naming its battle CAS in the menu: refused (the -1 index)')
    head = {f'N601_COMMON_RANGER_{k}_LEADER' for k in ('FL', 'RL', 'SG', 'SN')}
    for rel, data in files.items():
        if not rel.startswith('OBJECT/') or rel == mp.MECH_BASE:
            continue
        name = rel[len('OBJECT/'):-len('.SGO')]
        battle = game.read('OBJECT', name + '.SGO')
        check(data[:4] == b'DSGO' and dsgo.write(dsgo.parse(data)) == data, f'{name}: a DSGO, parses back exactly')
        check(mm.menu_problems(data, read_cas) == [], f'{name}: everything the menu needs ({mm.menu_problems(data, read_cas)})')
        check(mm.battle_equal(battle, data) == [], f'{name}: every battle key equal ({mm.battle_equal(battle, data)})')
        m, b = mm.members_of(data), mm.members_of(battle)
        stock = mm.members_of(game.read(mm.MENU_FOLDER, mm.STOCK_MENU[name[:4]]))
        check(m[mm.MENU_MODEL] == [b['animation_model'][0][:2], *stock['animation_model'][1:]],
              f"{name}: its own body with the class's menu CAS / MAB")
        check(m['random_animation'] == stock['random_animation'], f"{name}: the class's idle poses")
        want = ['p609_proto_ranger_common_head'] * 2 if name in head else [float(mm.EVERY)] * 2
        check([c[0] for c in m['change_color']] == want, f'{name}: colour selectors {m["change_color"]}')
        if name.startswith('N607'):
            slots = b['soldier_weapon_slot']
            check([s[1] for s in slots] == ['weapon_r', 'weapon_l'] and [s[0] for s in slots] == [[0.0], [1.0]],
                  f'{name}: weapon [0] right, [1] left (room_preview_logic.h MenuWeaponsOf: the shield second)')
    # A coloured, packed variant (support_loadout's EDF6VC_NPC_*): the same keys over its own battle values.
    look = (0x1E3A8A, support_loadout.STOCK)
    for kind, leader in (('lance', True), ('rocket', True), ('cannon', False)):
        template = {n: t for n, _, t in support_loadout.KINDS}[kind] + ('_LEADER' if leader else '')
        battle = support_loadout.with_pack(support_loadout.recolour(game.read('OBJECT', template + '.SGO'), look),
                                           support_loadout.PACKS[0][1])
        made = support_loadout.soldier_file(game, kind, leader, look, support_loadout.PACKS[0][1])
        check(mm.menu_problems(made, read_cas) == [] and mm.battle_equal(battle, made) == [],
              f'{kind} variant: unified, its battle keys the variant\'s')
    # Every plugin jet (pylib/vcobjects.py jet_sgo): unified at the source, its menu CAS always the renamed copy of its
    # own (the carrier's, the drone's and the X-Morph jets' retargets too: `Main` only, the -1 index of #50).
    unchecked = []
    for name in vcobjects.JETS:
        made = vcobjects.jet_sgo(game, name)
        m = mm.members_of(made)
        check(m[mm.MENU_MODEL][1] == mm.menu_cas_path(m['animation_model'][1]), f'jet {name}: the renamed menu CAS')
        if sources.get(m['animation_model'][1].lower(), b'') is None:   # an X-Morph jet not installed on this machine
            unchecked.append(name)
            continue
        problems = mm.menu_problems(made, read_cas)
        check(problems == [], f'jet {name}: {problems}')
        check(m[mm.MENU_MODEL][0] == m['animation_model'][0][:2] and m[mm.MENU_MODEL][2] == m['animation_model'][2],
              f'jet {name}: its own model and MAB')
    if unchecked:
        print(f'menu_preview_test: {len(unchecked)} X-Morph jets not installed here, their menu CAS not read: {unchecked[:4]}')
    # A loaded tank (support_loadout's EDF6VC_LO_*): its unified file over the loaded battle file.
    errors: dict[str, str] = {}
    with contextlib.redirect_stdout(io.StringIO()):
        out = support_loadout.build(game_dir, '[VehicleCrew]\nSupportVehicle_TANK_CREWED=AP\n', game, errors=errors)
    tanks = {k: v for k, v in out.items() if k.startswith('OBJECT/EDF6VC_LO_TANK_')}
    check(not errors and len(tanks) == 1, f'one loaded tank: {sorted(out)} {errors}')
    for rel, made in tanks.items():
        check(mm.menu_problems(made, read_cas) == [], f'{rel}: unified')
        m = mm.members_of(made)
        check(m[mm.MENU_MODEL] == m['animation_model'], f"{rel}: the tank's own CAS (has `main`) for the menu too")
    check_vehicle_bases(game, game_dir, files, read_cas)


def check_vehicle_bases(game: object, game_dir: str, files: dict[str, bytes], read_cas) -> None:
    """The tank's and the mech's bases (stock SGOs) made unified by their writers, and a loaded mech."""
    import support_loadout
    import mech_parts
    for rel in (mp.TANK_BASE, mp.MECH_BASE):
        folder, name = rel.split('/')
        battle = support_loadout._source(game_dir, game, folder, name)
        made = mp.unify_vehicle(game, rel, battle, game_dir)
        m = mm.members_of(made)
        check(mm.menu_problems(made, read_cas) == [] and mm.battle_equal(battle, made) == [],
              f'{rel}: unified, every battle key equal')
        check(m[mm.MENU_MODEL] == m['animation_model'], f'{rel}: its own model, CAS (has `main`) and MAB')
        check(mp.unify_vehicle(game, rel, made, game_dir) == made, f'{rel}: unified again, the same bytes')
        # A file built on the unified base whose model changed: the menu keys follow the battle model.
        version, members = sgo.read(made)
        moved = dict(members)
        moved['animation_model'] = [['app:/object/x.mrab', 'x.mdb'], *members['animation_model'][1:]]
        again = mm.members_of(mp.unify_vehicle(game, rel, sgo.write_depth_first(version, moved), game_dir))
        check(again[mm.MENU_MODEL][0] == ['app:/object/x.mrab', 'x.mdb'], f"{rel}: a variant never keeps the base's model")
    check(files[mp.MECH_BASE] == mp.unify_vehicle(game, mp.MECH_BASE, game.read('OBJECT', 'V504_BEGARUTA.SGO'), game_dir),
          "the mech's base: this tool's, the stock file unified")
    # A loaded mech (frame + core + four weapons, support_loadout's EDF6VC_LO_MECH_*): the battle variant is the
    # menu's file.
    full = max((r for r in mech_parts.REQUESTS if '-' not in r.slots), key=lambda r: r.level)
    core = next(c for c in mech_parts.CORES if c.token != full.core and c.level <= full.level)
    mech = mech_parts.Mech(full.token, frame=mech_parts.FRAMES[1].token, weapons=tuple(full.slots), core=core.token)
    text = '[VehicleCrew]\nSupportVehicle_OWN_MECH=' + mech_parts.format_mech(mech) + '\n'
    errors: dict[str, str] = {}
    with contextlib.redirect_stdout(io.StringIO()):
        out = support_loadout.build(game_dir, text, game, errors=errors)
    import menu_graft
    mechs = {k: v for k, v in out.items() if k.startswith('OBJECT/EDF6VC_LO_MECH_') and k.endswith('.SGO')}
    check(not errors and len(mechs) == 1, f'one loaded mech: {sorted(out)} {errors}')
    for rel, made in mechs.items():
        check(mm.menu_problems(made, read_cas) == [], f'{rel}: unified')
        m = mm.members_of(made)
        name = rel.split('/')[1]
        check(m[mm.MENU_MODEL][0] == menu_graft.menu_model_ref(name) and
              m[mm.MENU_MODEL][1:] == m['animation_model'][1:],
              f'{rel}: the menu draws its merged model (frame + weapons) with the frame CAS and MAB')
        check(menu_graft.archive_rel(name) in out and menu_graft.callouts_rel(name) in out,
              f'{rel}: its merged model and callouts written beside it')


def synthetic_jet(game: object) -> tuple[int, dict]:
    """A jet-like vehicle file naming the battle heli CAS in its menu model (what #50 crashed on)."""
    version, m = sgo.read(game.read('OBJECT', 'V505_TANK_MISSION.SGO'))
    m['animation_model'] = list(m['animation_model'])
    m['animation_model'][1] = mm.JET_CAS
    _, stock = sgo.read(game.read(mm.MENU_FOLDER, mm.VEHICLE_STOCK))
    return version, mm.add_sgo(m, mm.vehicle_menu_keys(stock, m['animation_model'], mm.JET_CAS))


def main() -> int:
    check_names()
    check_wiring()
    check_model_offline()
    with tempfile.TemporaryDirectory(prefix='edf6vc-menu-') as game:
        check_install(game)
        check_generate(game)
    try:
        import gamedir
        import rootcpk
        real = rootcpk.default()
        real_dir = gamedir.find_or_dev()
    except Exception as e:   # no game on this machine (CI)
        print(f'menu_preview_test: no game ({e}); the real builder not run')
        real = None
    if real is not None:
        check_real(real, real_dir)
    print(f'menu_preview_test: {checks} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
