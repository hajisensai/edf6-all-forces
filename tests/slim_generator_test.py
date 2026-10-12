"""The slim generator (tools/loadout_generator.py; tools/build_release.py build_generator): what install copies into
Mods/Plugins as EDF6VehicleCrew.generator.exe for the in-game loadout menu and the room pre-sync, instead of the whole
~100 MB installer. Offline, nothing frozen, no PyInstaller run (build_release's commands are captured, not executed):
  - the generator imports nothing of the installer's network, menus or model builders (every import statement, function
    level too, since PyInstaller follows those), and installer.py takes its loadout-generate from it (one copy);
  - its entry dispatches only `loadout-generate <game>` and never waits for input;
  - install copies the slim exe the release bundles, else the frozen installer itself, else nothing;
  - build_release freezes loadout_generator.py alone with the heavy modules excluded, bundles the result beside the
    plugin files (where install looks), and builds it before the installer.
Exit code 1 when one fails.
"""
from __future__ import annotations

import ast
import builtins
import contextlib
import io
import os
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib')]
import loadout_generator as lg  # noqa: E402
import menu_preview  # noqa: E402
import room_layout  # noqa: E402
import installer  # noqa: E402
import build_release  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why, file=sys.stderr)   # stderr: a failure inside a redirect_stdout block is still said
        sys.exit(1)


def imports_of(path: str) -> set[str]:
    with open(path, encoding='utf-8') as f:
        tree = ast.parse(f.read())
    out: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            out |= {a.name.split('.')[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
            out.add(node.module.split('.')[0])
    return out


# What the generator may import (stdlib, and the repository's own SGO / CPK readers the real builder was measured to use).
ALLOWED = {'__future__', 'contextlib', 'os', 'sys', 'traceback', 'typing', 'msvcrt', 'gamedir', 'modfiles', 'support_loadout',
           'menu_preview', 'rootcpk', 'room_layout'}   # the preview's menu SGOs (stage 7), the screen's layouts (stage 4)
got = imports_of(os.path.join(ROOT, 'tools', 'loadout_generator.py'))
check(got <= ALLOWED, f'loadout_generator imports only {sorted(ALLOWED)}: {sorted(got - ALLOWED)}')
for heavy in build_release.GENERATOR_EXCLUDES:
    check(heavy not in got, f'loadout_generator does not import {heavy}')
# support_loadout's own imports: the generator's whole tree is these (vcobjects lazily for the pylon stores).
sl_imports = imports_of(os.path.join(ROOT, 'tools', 'support_loadout.py'))
check(not sl_imports & {'testhub', 'installer', 'tkinter', 'numpy', 'PIL', 'procmesh', 'jet_models'},
      f'support_loadout pulls in nothing heavy: {sorted(sl_imports)}')
mp_imports = imports_of(os.path.join(ROOT, 'tools', 'menu_preview.py'))
check(not mp_imports & set(build_release.GENERATOR_EXCLUDES),
      f'menu_preview pulls in nothing the slim generator excludes: {sorted(mp_imports)}')

# One copy: the installer's loadout-generate is the generator's.
for name in ('generate_main', 'generate_line', 'generate_lock', 'loadout_generate', 'write_generate_status', 'generate_variants'):
    check(getattr(installer, name) is getattr(lg, name), f'installer.{name} is loadout_generator.{name}')
check((installer.GENERATOR, installer.GENERATE_STATUS) == (lg.GENERATOR, lg.GENERATE_STATUS) == (
      'EDF6VehicleCrew.generator.exe', 'EDF6VehicleCrew.generate_status.txt'), 'the names the plugin looks for')
store = open(os.path.join(ROOT, 'src', 'loadout_store.cpp'), encoding='utf-8').read()
check(f'L"{lg.GENERATOR}"' in store and f'L"{lg.GENERATE_STATUS}"' in store, 'src/loadout_store.cpp uses the same names')


def no_prompt(*_a: object) -> str:
    raise AssertionError('the generator asked for input')


builtins.input = no_prompt
for argv, code in (([], 2), (['install'], 2), (['loadout-generate'], 2), (['loadout-generate', 'a', 'b'], 2)):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        check(lg.run(argv) == code, f'run({argv}) exits {code}')
    check(out.getvalue().strip().splitlines()[-1].startswith('EDF6VC_GENERATE state=failed'), f'run({argv}): the status line')
with tempfile.TemporaryDirectory(prefix='edf6vc-slim-') as root:
    for name in ('EDF6.exe', 'Root.cpk'):
        open(os.path.join(root, name), 'wb').close()
    os.makedirs(os.path.join(root, 'Mods', 'Plugins'))
    with open(os.path.join(root, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini'), 'wb') as f:
        f.write(b'[VehicleCrew]\r\nEnabled=1\r\n')
    for rel in menu_preview.rels():   # the preview's menu SGOs already there (the empty Root.cpk cannot make them)
        os.makedirs(os.path.dirname(os.path.join(root, 'Mods', rel)), exist_ok=True)
        open(os.path.join(root, 'Mods', rel), 'wb').close()
    os.makedirs(os.path.join(root, 'Mods', 'UI'))
    for name in room_layout.LAYOUTS:   # the menu screen's layouts already there (the empty Root.cpk cannot make them)
        open(os.path.join(root, 'Mods', 'UI', name), 'wb').close()
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        check(lg.run(['loadout-generate', root]) == 0, 'nothing to make: exit 0')
    check(out.getvalue().strip().splitlines()[-1] == 'EDF6VC_GENERATE state=ok made=0 failed=0', 'and says so')
    status = open(os.path.join(root, 'Mods', 'Plugins', lg.GENERATE_STATUS), encoding='utf-8').read()
    check('state=ok' in status, 'the status file written')

    # install: the bundled slim exe, else the frozen installer, else nothing.
    bundle = os.path.join(root, 'bundle')
    os.makedirs(bundle)
    real_bundle = installer.bundle_dir
    installer.bundle_dir = lambda: bundle
    try:
        check(installer.generator_source() is None, 'from source, no slim exe: none')
        sys.frozen = True   # type: ignore[attr-defined]
        try:
            check(installer.generator_source() == sys.executable, 'a frozen installer without the slim exe: itself')
            slim = os.path.join(bundle, lg.GENERATOR)
            with open(slim, 'wb') as f:
                f.write(b'MZ slim')
            check(installer.generator_source() == slim, 'with the slim exe bundled: that one')
        finally:
            del sys.frozen
        check(installer.generator_source() == slim, 'from source with a slim exe beside the plugin files: that one')
        dst = installer.install_generator(root, installer.generator_source())
        check(dst and open(dst, 'rb').read() == b'MZ slim', 'install copies the slim exe as the generator')
    finally:
        installer.bundle_dir = real_bundle
inst = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
check('install_generator(game, generator_source())' in inst, 'install copies generator_source()')

# build_release: the commands, captured.
cmd = build_release.generator_cmd()
check(cmd[-1] == os.path.join(ROOT, 'tools', 'loadout_generator.py') and '--onefile' in cmd, 'the generator frozen alone, one file')
check(cmd[cmd.index('--name') + 1] + '.exe' == lg.GENERATOR, 'named as the plugin runs it')
excluded = {cmd[i + 1] for i, a in enumerate(cmd) if a == '--exclude-module'}
check({'numpy', 'PIL', 'tkinter', 'testhub', 'installer'} <= excluded, f'the heavy modules excluded: {sorted(excluded)}')
check('--hidden-import' not in cmd, 'nothing extra pulled in')
ran: list[list[str]] = []
real_run, real_check = subprocess.run, build_release.check_bundled
subprocess.run = lambda c, check=False: ran.append(list(c))   # type: ignore[assignment]
build_release.check_bundled = lambda: None
try:
    with contextlib.redirect_stdout(io.StringIO()):
        gen = build_release.build_generator()
        build_release.build_exe('EDF6VehicleCrew-test', gen)
finally:
    subprocess.run, build_release.check_bundled = real_run, real_check
check(gen == os.path.join(build_release.WORK, 'gen-dist', lg.GENERATOR), 'build_generator returns its exe')
check(len(ran) == 2 and ran[0] == cmd, 'the generator built first')
check(f'{gen}{os.pathsep}plugin' in ran[1], 'the installer bundles it beside the plugin files (installer.bundle_dir)')
main_src = open(os.path.join(ROOT, 'tools', 'build_release.py'), encoding='utf-8').read().split('def main(', 1)[1]
check(main_src.index('build_generator()') < main_src.index('build_exe('), 'main builds the generator before the installer')
print(f'slim_generator_test: {checks} checks passed')
