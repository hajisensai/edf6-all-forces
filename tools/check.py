"""The offline checks CI runs (.github/workflows/build.yml), in one command, gentle on the machine it runs on.

  python tools/check.py [-j N] [--no-build] [--no-ctest] [--no-selftest] [--no-gen] [--normal-priority]

  build.cmd + offline_checks, then side by side: ctest, tools/selftest.py, py_compile and the generated files' checks.
  -j N: the processes the tests take in all (default: one a logical core), shared between ctest and selftest.

Why (the maintainer, 2026-11-10: 「太慢了 ... 特别吃性能，电脑很卡」):
- Below-normal priority (Windows): this process and everything it starts (the compiler, ctest, every test, selftest's
  workers inherit it), so the desktop, the game and the editor keep the cores whenever they want them.
- One full check on this machine at a time (pylib/checklock.py): every worktree's session ran its own
  ctest -j 8 + selftest -j 8 at once, 16 cores asked for 32+ processes and each run took twice as long.
- ctest and selftest side by side instead of one after the other: selftest's wall time is its longest test, most of
  its workers idle at its end, while ctest still had work queued; together they keep the cores busy till both end.
Each part's output goes to build/check-<part>.log; the failures' tails are printed at the end. Exit 1 on a failure.
"""
from __future__ import annotations

import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import checklock  # noqa: E402

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
]


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


def gen_checks(log: str) -> tuple[int, float]:
    started = time.perf_counter()
    failed = 0
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        py = [p for p in subprocess.run(['git', '-c', 'core.quotepath=off', 'ls-files', 'tools', 'pylib', 'testrange',
                                         'autoturret/tools', 'autoturret/third_party/edf6-cpk'],
                                        cwd=ROOT, capture_output=True, text=True, encoding='utf-8').stdout.splitlines()
              if p.endswith('.py')]
        steps = [[sys.executable, '-m', 'py_compile', *py]]
        steps += [[sys.executable, *args] for args, _ in GENERATED]
        for cmd, (_, files) in zip(steps, [(None, None)] + GENERATED):
            out.write(f'$ {" ".join(cmd[:4])}\n')
            out.flush()
            code = subprocess.call(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
            if code == 0 and files:
                code = subprocess.call(['git', 'diff', '--exit-code', '--', *files], cwd=ROOT, stdout=out,
                                       stderr=subprocess.STDOUT)
            if code:
                out.write(f'FAIL ({code}): {" ".join(cmd[1:])}\n')
                failed += 1
    return (1 if failed else 0), time.perf_counter() - started


def main(argv: list[str]) -> int:
    known = {'-j', '--no-build', '--no-ctest', '--no-selftest', '--no-gen', '--normal-priority'}
    if any(a.startswith('-') and a not in known for a in argv):
        print(__doc__)
        return 0 if {'-h', '--help'} & set(argv) else 2
    jobs = os.cpu_count() or 8
    if '-j' in argv:
        jobs = max(2, int(argv[argv.index('-j') + 1]))
    if '--normal-priority' not in argv:
        checklock.below_normal()
    os.makedirs(BUILD, exist_ok=True)
    results: dict[str, tuple[int, float]] = {}
    logs = {k: os.path.join(BUILD, f'check-{k}.log') for k in ('build', 'ctest', 'selftest', 'gen')}
    started = time.perf_counter()
    with checklock.held(f'tools/check.py ({os.path.basename(ROOT)})'):
        # taken inside the lock: it carries checklock.HELD, so selftest.py under this check does not wait for it
        env = {**os.environ, 'PYTHONIOENCODING': 'utf-8'}
        print(f'check.py: {ROOT}, {jobs} processes, below-normal priority; logs build/check-*.log', flush=True)
        if '--no-build' not in argv:
            cmd = f'call "{os.path.join(ROOT, "build.cmd")}" && call "{os.path.join(ROOT, "tools", "msvc-x64-env.cmd")}" ' \
                  f'&& cmake --build "{BUILD}" --target offline_checks'
            results['build'] = run(cmd, logs['build'], env, shell=True)
            print(f"build: {'ok' if not results['build'][0] else 'FAIL'} ({results['build'][1]:.0f} s)", flush=True)
            if results['build'][0]:
                print(tail(logs['build']))
                return 1
        # selftest's wall time is its longest test: half the processes keep it there, ctest takes the rest and the
        # cores selftest's workers leave when they run out of tests.
        st_jobs = max(1, min(8, jobs // 2))
        parts = []
        if '--no-ctest' not in argv:
            parts.append(('ctest', lambda: run(['ctest', '--test-dir', BUILD, '--output-on-failure',
                                                '-j', str(max(1, jobs - st_jobs))], logs['ctest'], env)))
        if '--no-selftest' not in argv:
            parts.append(('selftest', lambda: run([sys.executable, 'tools/selftest.py', '-j', str(st_jobs)],
                                                  logs['selftest'], env)))
        if '--no-gen' not in argv:
            parts.append(('gen', lambda: gen_checks(logs['gen'])))

        def go(name, fn):
            results[name] = fn()
            print(f"{name}: {'ok' if not results[name][0] else 'FAIL'} ({results[name][1]:.0f} s)", flush=True)
        threads = [threading.Thread(target=go, args=p) for p in parts]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    failed = [k for k, (code, _) in results.items() if code]
    for k in failed:
        print(f'\n===== {k} failed: the end of build/check-{k}.log =====')
        print(tail(logs[k]))
    print(f"check.py: {'FAIL ' + ', '.join(failed) if failed else 'all passed'} in {time.perf_counter() - started:.0f} s")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
