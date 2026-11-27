"""The offline checks CI runs (.github/workflows/build.yml), in one command, gentle on the machine it runs on.

  python tools/check.py [--full | --dry-run] [--base REV] [-j N] [--no-build] [--no-ctest] [--no-selftest] [--no-gen]
                        [--normal-priority]

  build.cmd + offline_checks, then side by side: ctest, tools/selftest.py, py_compile and the generated files' checks.
  -j N: the processes the tests take in all (default: one a logical core), shared between ctest and selftest.
  ctest and selftest run only the tests this branch's changes reach (tools/test_domains.py select_files), chosen once
  the slot is held and the build is done: the files changed since the merge-base with origin/main, uncommitted and
  untracked ones included, against each test's own dependencies (the Python it imports or runs, the files it reads,
  and for a C++ test the sources its exe compiled, from ninja's records). A CMakeLists.txt change selects the tests and
  targets the changed commands name; one it cannot place (flags, if(), a foreach body), a change to cmake/, build.cmd,
  CI, check.py, test_domains.py or the check's own modules (FULL), or no merge-base runs every test. The build,
  py_compile, the docs' links and the labels' check always run; a generator only when the change reaches its code,
  inputs or output. --full: every test and generator (CI always runs everything). --dry-run: say what would run (needs
  a configured build/), run nothing.
  --base REV: compare with REV instead of the merge-base (--base HEAD: only the uncommitted changes).
  One check per worktree: a second one while the first is queued or running exits 3 at once.

Why (the maintainer, 2026-11-10: 「太慢了 ... 特别吃性能，电脑很卡」):
- Below-normal priority (Windows): this process and everything it starts (the compiler, ctest, every test, selftest's
  workers inherit it), so the desktop, the game and the editor keep the cores whenever they want them.
- One full check on this machine at a time (pylib/checklock.py): every worktree's session ran its own
  ctest -j 8 + selftest -j 8 at once, 16 cores asked for 32+ processes and each run took twice as long.
- ctest and selftest side by side instead of one after the other: selftest's wall time is its longest test, most of
  its workers idle at its end, while ctest still had work queued; together they keep the cores busy till both end.
- Only the tests a change can affect (2026-11-24: 「类似的测试优化都要做。太慢了，性能浪费很严重」): a src/-only branch
  built every model for 10+ minutes in ctest and selftest. 2026-11-26 (「check.py 太长了 ... 十分钟太浪费时间了」):
  by domain, any pylib/ or tools/ change still selected ~all of both and CMakeLists.txt / common/ ran everything in
  two of three merges; by file, a test runs when a file it depends on changed.
Each part's output goes to build/check-<part>.log; the failures' tails are printed at the end. Exit 1 on a failure,
3 when this worktree's check is already queued or running.
"""
from __future__ import annotations

import contextlib
import json
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import checklock  # noqa: E402
import gamedir  # noqa: E402
import heartbeat  # noqa: E402
import test_domains  # noqa: E402  (tools/, this script's folder)

BUSY = 3   # exit code: this worktree's check is already queued or running

# (a generator's command line, the files it rewrites that must then be unchanged in git; None: it checks itself)
GENERATED = [
    (['tools/gen_calls.py'], ['src/calls.inc']),
    (['tools/gen_stores.py'], ['src/stores.inc']),
    (['tools/gen_cockpit_fields.py', '--check'], None),
    (['tools/gen_spawn_catalog.py', '--check'], None),
    (['tools/gen_npc_tiers.py', '--check'], None),
    (['tools/gen_pvp_units.py', '--check'], None),
    (['tools/gen_nozzles.py', '--check'], None),
    (['tools/gen_katyusha_cab.py', '--check'], None),
    (['tools/gen_barga_reach.py', '--check'], None),
    (['tools/gen_mech_parts.py', '--check'], None),
    (['tools/gen_weapon_series.py', '--check'], None),
    (['tools/gen_worldmap.py', '--check'], None),
]   # gen_checks also runs tools/doc_links_check.py and test_domains' --check, on every run


def game_defines() -> tuple[list[str], str | None]:
    """The -D the build configures with: the game pylib/gamedir finds, so the native audits (ctest's tests given
    ${EDF6_NATIVE_DLL} / ${EDF6_GAME_DIR}) read its EDF.dll and data instead of skipping. They only read the files,
    the game may be running. No game (CI): both empty, those tests skip. Raises on an EDF.dll that is not the one
    the plugins support: the audits would compare the sources with the wrong bytes."""
    game = gamedir.find()
    if not game:
        return ['-DEDF6_GAME_DIR=', '-DEDF6_NATIVE_DLL='], None
    stamp = gamedir.edf_stamp(game)
    if stamp != gamedir.EDF_STAMP:
        raise SystemExit(f'check.py: {os.path.join(game, "EDF.dll")} TimeDateStamp {stamp and hex(stamp)} is not the supported '
                         f'{gamedir.EDF_STAMP:#x}: update the game or point EDF6_DIR at a supported install')
    game = game.replace(os.sep, '/')
    return [f'"-DEDF6_GAME_DIR={game}"', f'"-DEDF6_NATIVE_DLL={game}/EDF.dll"'], game


def native_tests() -> set[str]:
    """ctest's tests that read the game: any argument or environment naming the configured EDF.dll / game dir."""
    out = subprocess.run(['ctest', '--test-dir', BUILD, '--show-only=json-v1'], cwd=ROOT, capture_output=True,
                         text=True, encoding='utf-8', errors='replace').stdout
    try:
        tests = json.loads(out).get('tests', [])
    except ValueError:
        return set()
    cache = {}
    with open(os.path.join(BUILD, 'CMakeCache.txt'), encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.match(r'(EDF6_GAME_DIR|EDF6_NATIVE_DLL):\w+=(.*)', line.strip())
            if m and m.group(2):
                cache[m.group(1)] = m.group(2)
    if not cache:
        return set()
    names = set()
    for t in tests:
        props = ' '.join(str(p.get('value')) for p in t.get('properties', []))
        if any(v in ' '.join(t.get('command') or []) + props for v in cache.values()):
            names.add(t['name'])
    return names


def ctest_outcomes(log: str) -> dict[str, str]:
    out = {}
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'Test\s+#\d+: (\S+) \.*\s*(\*\*\*\w+|Passed)', line)
            if m:
                out[m.group(1)] = m.group(2).lstrip('*')
    return out


def _git(*args: str) -> tuple[int, str]:
    r = subprocess.run(['git', '-c', 'core.quotepath=off', *args], cwd=ROOT, capture_output=True, text=True,
                       encoding='utf-8', errors='replace')
    return r.returncode, r.stdout


BASE: str | None = None   # --base REV


def base_of() -> str:
    """The commit the changes are counted from: --base REV, else the merge-base of HEAD and origin/main ('' without)."""
    code, base = _git('rev-parse', '--verify', BASE + '^{commit}') if BASE else _git('merge-base', 'HEAD', 'origin/main')
    return '' if code else base.strip()


def changed_files() -> tuple[list[str], set[str], str | None]:
    """This branch's changes: the files changed since the merge-base with origin/main (committed or not, both sides
    of a rename) and the untracked ones, and those of them that are gone; (files, deleted, None), or ([], set(), why)
    when there is no base to compare with."""
    base = base_of()
    if not base:
        return [], set(), f'no commit {BASE}' if BASE else 'no merge-base with origin/main (git fetch origin?)'
    code, diff = _git('diff', '--name-only', '--no-renames', base)
    code2, new = _git('ls-files', '--others', '--exclude-standard')
    if code or code2:
        return [], set(), f'git diff against the merge-base {base[:10]} failed'
    files = sorted({f for f in (diff + new).splitlines() if f})
    return files, {f for f in files if not os.path.exists(os.path.join(ROOT, f))}, None


def cmake_change(files: list[str], names: set[str]) -> tuple[set[str], str | None]:
    """What this branch's change to the CMakeLists.txt files selects (test_domains.cmake_effects): (the tests and
    targets it names, None), or (set(), why): run everything."""
    out: set[str] = set()
    base = base_of()
    for f in test_domains.CMAKE_FILES:
        if f not in files:
            continue
        code, old = _git('show', f'{base}:{f}')
        code2, diff = _git('diff', '-U0', base, '--', f)
        if code2:
            return set(), f'git diff {f} failed'
        try:
            with open(os.path.join(ROOT, f), encoding='utf-8') as h:
                new = h.read()
        except OSError:
            new = ''
        got, why = test_domains.cmake_effects('' if code else old, new, *test_domains.diff_sides(diff), names)
        if why:
            return set(), f'{f}: {why}'
        out |= got
    return out, None


def plan(argv: list[str]) -> dict:
    """The tests to run: test_domains.select_files's answer for this branch's changes, from the code as it is now
    (every test's dependencies, the build's records of what each C++ target compiled) and the build configured."""
    started = time.perf_counter()
    files, deleted, why = changed_files()
    if '--full' in argv:
        why = '--full'
    repo = test_domains.Repo()
    deps = test_domains.test_deps(BUILD, repo)
    cpp = test_domains.cpp_files(BUILD)
    names = None
    if test_domains.SELFTEST in files and not why:   # which of its tests and helpers the change is in
        code, diff = _git('diff', '-U0', base_of(), '--', test_domains.SELFTEST)
        with open(os.path.join(ROOT, test_domains.SELFTEST), encoding='utf-8') as f:
            names = test_domains.changed_names(f.read(), test_domains.diff_lines(diff)) if not code else None
    cmake = cmake_change(files, {n.lower() for n in deps['ctest']} | repo.target_names) if not why else (set(), None)
    sel = test_domains.select_files(files, deps, cpp, deleted, names, cmake)
    sel['full'] = why or sel['full']
    sel.update(files=files, deleted=deleted, deps=deps, repo=repo, cpp=cpp, cmake=cmake[0],
               seconds=time.perf_counter() - started)
    return sel


def say_plan(sel: dict) -> None:
    every = {part: len(sel['deps'][part]) for part in ('ctest', 'selftest')}
    if sel['full']:
        print(f'check.py: every test ({sel["full"]})', flush=True)
        return
    n = len(sel['files'])
    print(f'check.py: the tests {n} changed file{"s" * (n != 1)} reach (tools/test_domains.py select_files, '
          f'{sel["seconds"]:.0f} s; --full for all):')
    for d, files in sorted(sel['domains'].items()):
        print(f'  changed {d:16} {", ".join(files[:3])}{f" (+{len(files) - 3})" if len(files) > 3 else ""}')
    if sel['cmake']:
        names = sorted(sel['cmake'])
        print(f'  CMake   names {", ".join(names[:6])}{" ..." if len(names) > 6 else ""}')
    if 'selftest_names' in sel:
        names = sel['selftest_names']
        print(f'  selftest.py changed in {", ".join(names[:6])}{" ..." if len(names) > 6 else ""}')
    for part in ('ctest', 'selftest'):
        print(f'  {part}: {len(sel[part])} of {every[part]} tests')
    sys.stdout.flush()


def skip_file(sel: dict, part: str) -> str | None:
    """The file naming the tests of `part` no change selects; None: all run."""
    if sel['full']:
        return None
    path = os.path.join(BUILD, f'check-{part}-skip.txt')
    chosen = set(sel[part])
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(''.join(f'{n}\n' for n in sel['deps'][part] if n not in chosen))
    return path


def run(cmd: list[str] | str, log: str, env: dict | None = None, shell: bool = False) -> tuple[int, float]:
    started = time.perf_counter()
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        code = subprocess.call(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env, shell=shell)
    return code, time.perf_counter() - started


def tail(log: str, lines: int = 60) -> str:
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            return ''.join(f.readlines()[-lines:])
    except OSError:
        return ''


def generators(sel: dict) -> list[tuple[list[str], list[str] | None]]:
    """The GENERATED steps this change reaches (each generator's code, inputs and output: test_domains
    generator_deps); all of them on a full run."""
    if sel['full']:
        return list(GENERATED)
    return [g for g in GENERATED if sel['hits'](test_domains.generator_deps(sel['repo'], g[0][0]))]


def gen_checks(log: str, sel: dict) -> tuple[int, float]:
    """py_compile of every tracked Python file, the generators the change reaches, the docs' links, and the test labels
    (test_domains --check, from the dependencies the selection already computed)."""
    started = time.perf_counter()
    failed = 0
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        py = [p for p in subprocess.run(['git', '-c', 'core.quotepath=off', 'ls-files', 'tools', 'pylib', 'testrange',
                                         'autoturret/tools', 'autoturret/third_party/edf6-cpk'],
                                        cwd=ROOT, capture_output=True, text=True, encoding='utf-8').stdout.splitlines()
              if p.endswith('.py')]
        chosen = generators(sel)
        out.write(f'generators: {len(chosen)} of {len(GENERATED)} ({", ".join(a[0] for a, _ in chosen) or "none"}); '
                  f'skipped: {", ".join(a[0] for a, _ in GENERATED if (a, _) not in chosen) or "none"}\n')
        steps = [([sys.executable, '-m', 'py_compile', *py], None)]
        steps += [([sys.executable, *args], files) for args, files in chosen]
        steps += [([sys.executable, 'tools/doc_links_check.py'], None)]   # docs <-> code paths (eng-12)
        for cmd, files in steps:
            out.write(f'$ {" ".join(cmd[:4])}\n')
            out.flush()
            code = subprocess.call(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
            if code == 0 and files:
                code = subprocess.call(['git', 'diff', '--exit-code', '--', *files], cwd=ROOT, stdout=out,
                                       stderr=subprocess.STDOUT)
            if code:
                out.write(f'FAIL ({code}): {" ".join(cmd[1:])}\n')
                failed += 1
        # every test in a domain, the labels current
        out.write('$ tools/test_domains.py --check (in check.py, on the selection\'s dependencies)\n')
        code, text = test_domains.check_report(BUILD, sel['deps'], sel['repo'])
        out.write(text + '\n')
        if code:
            out.write('FAIL (1): tools/test_domains.py --check\n')
            failed += 1
    return (1 if failed else 0), time.perf_counter() - started


def main(argv: list[str]) -> int:
    known = {'-j', '--no-build', '--no-ctest', '--no-selftest', '--no-gen', '--normal-priority', '--full', '--dry-run', '--base'}
    if any(a.startswith('-') and a not in known for a in argv):
        print(__doc__)
        return 0 if {'-h', '--help'} & set(argv) else 2
    jobs = os.cpu_count() or 8
    if '-j' in argv:
        jobs = max(2, int(argv[argv.index('-j') + 1]))
    if '--base' in argv:
        global BASE
        BASE = argv[argv.index('--base') + 1]
    if '--dry-run' in argv:   # on the build as it is (not rebuilt): what a check would run
        if not os.path.isfile(os.path.join(BUILD, 'CTestTestfile.cmake')):
            print('check.py --dry-run: no configured build (build.cmd first)')
            return 2
        sel = plan(argv)
        say_plan(sel)
        for part in ('ctest', 'selftest'):
            if not sel['full']:
                print(f'{part}: ' + ' '.join(sel[part]))
        print('generators: ' + ' '.join(a[0] for a, _ in generators(sel)))
        return 0
    checklock.children_die_with_me()   # killed alone, it would leave ctest / selftest running in the same build folder
    try:
        with checklock.worktree_once(ROOT):
            return _main(argv, jobs)
    except checklock.Busy as e:
        print(busy_text(e.args[0] if e.args else {}), flush=True)
        return BUSY


def busy_text(who: dict) -> str:
    """What a second check of this worktree says (checklock.Busy's record of the first)."""
    if who.get('pid'):
        since = time.strftime('%H:%M:%S', time.localtime(who.get('since', time.time())))
        first = f'已在排队/运行（pid {who["pid"]}，{since} 起）'
    else:
        first = '已在排队/运行（另一个 check.py 刚拿到这个 worktree 的锁，还没写下 pid；或正在退出）'
    return (f'check.py: {first}: {ROOT} 的检查只跑一套，等它的结果；前一套只覆盖它开跑时（拿到全机检查槽那一刻）的改动，'
            f'之后的改动等它结束再跑一次（exit {BUSY}）')


@contextlib.contextmanager
def slot():
    """The machine's check slot: the seconds queued. The tests are chosen once it is held and the build is done
    (_main: plan), from the changes on disk then: the queue can take tens of minutes, and the choice reads the code
    and the build as they are when the tests run."""
    started = time.perf_counter()
    with checklock.held(f'tools/check.py ({os.path.basename(ROOT)})'):
        yield time.perf_counter() - started


def _main(argv: list[str], jobs: int) -> int:
    if '--normal-priority' not in argv:
        checklock.below_normal()
    os.makedirs(BUILD, exist_ok=True)
    results: dict[str, tuple[int, float]] = {}
    logs = {k: os.path.join(BUILD, f'check-{k}.log') for k in ('build', 'ctest', 'selftest', 'gen')}
    # the parts' output goes to their logs: a line every few minutes for whoever watches this run (pylib/heartbeat.py),
    # from when the slot is ours (checklock says its own while waiting)
    with slot() as waited, heartbeat.beating('check.py', lambda: heartbeat.parts_progress(results, logs)):
        for log in logs.values():   # a part's log is this run's from its start on: parts_progress() never reads a stale one
            with contextlib.suppress(OSError):
                os.remove(log)
        # taken inside the lock: it carries checklock.HELD, so selftest.py under this check does not wait for it
        env = {**os.environ, 'PYTHONIOENCODING': 'utf-8'}
        started = time.perf_counter()   # the queue is not the check's time
        print(f'check.py: {ROOT}, {jobs} processes, below-normal priority (waited {waited:.0f} s for the single '
              f'check slot); logs build/check-*.log', flush=True)
        defines, game = game_defines()
        print(f'check.py: native audits {"read " + game if game else "skip (no game found)"}', flush=True)
        if '--no-build' not in argv:
            cmd = f'call "{os.path.join(ROOT, "build.cmd")}" {" ".join(defines)} && call "{os.path.join(ROOT, "tools", "msvc-x64-env.cmd")}" ' \
                  f'&& cmake --build "{BUILD}" --target offline_checks'
            results['build'] = run(cmd, logs['build'], env, shell=True)
            print(f"build: {'ok' if not results['build'][0] else 'FAIL'} ({results['build'][1]:.0f} s)", flush=True)
            if results['build'][0]:
                print(tail(logs['build']))
                return 1
        # chosen now: the code on disk and the build the tests will run
        sel = plan(argv)
        say_plan(sel)
        # selftest's wall time is its longest test: half the processes keep it there, ctest takes the rest and the
        # cores selftest's workers leave when they run out of tests.
        st_jobs = max(1, min(8, jobs // 2))
        parts = []
        if '--no-ctest' not in argv and (sel['full'] or sel['ctest']):
            ct_skip = skip_file(sel, 'ctest')
            parts.append(('ctest', lambda: run(['ctest', '--test-dir', BUILD, '--output-on-failure',
                                                '-j', str(max(1, jobs - st_jobs)),
                                                *(['--exclude-from-file', ct_skip] if ct_skip else [])],
                                               logs['ctest'], env)))
        if '--no-selftest' not in argv and (sel['full'] or sel['selftest']):
            st_skip = skip_file(sel, 'selftest')
            parts.append(('selftest', lambda: run([sys.executable, 'tools/selftest.py', '-j', str(st_jobs),
                                                   *(['--skip', st_skip] if st_skip else [])], logs['selftest'], env)))
        if '--no-gen' not in argv:
            parts.append(('gen', lambda: gen_checks(logs['gen'], sel)))

        def go(name, fn):
            results[name] = fn()
            print(f"{name}: {'ok' if not results[name][0] else 'FAIL'} ({results[name][1]:.0f} s)", flush=True)
        threads = [threading.Thread(target=go, args=p) for p in parts]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    failed = [k for k, (code, _) in results.items() if code]
    if 'ctest' in results:
        native = native_tests()
        outcome = ctest_outcomes(logs['ctest'])
        ran = sorted(n for n in native if outcome.get(n) not in (None, 'Skipped'))
        skipped = sorted(n for n in native if outcome.get(n) == 'Skipped')
        print(f'check.py: native audits run {len(ran)}, skipped {len(skipped)}'
              f'{"" if game else " (no game: every native audit skips)"}'
              f'{"" if sel["full"] else f", not selected {len(native) - len(ran) - len(skipped)}"}')
        # the game is here: a selected audit that skips lost its input, it did not pass (the ones no change selected
        # did not run at all)
        if game and skipped:
            print('  skipped with the game present: ' + ', '.join(skipped))
            failed.append('native-skips')
    for k in failed:
        if k not in logs:
            continue
        print(f'\n===== {k} failed: the end of build/check-{k}.log =====')
        print(tail(logs[k]))
    print(f"check.py: {'FAIL ' + ', '.join(failed) if failed else 'all passed'} in {time.perf_counter() - started:.0f} s")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
