"""One pool of test jobs for tools/check.py and tools/selftest.py: ctest's tests, selftest's and the generated files'
checks side by side, longest first, each holding one of the machine's core slots while it runs (checklock.core).

Why (2026-11-26, the maintainer: 「需要极其优化测试逻辑，太慢了，特别是并发」): check.py ran ctest -j (cores - 8) beside
selftest -j 8. selftest's workers ran out of tests minutes before ctest did, and ctest could not take the cores they left
(~240 s with half the machine idle at the end of a full check); and one check held the machine's single slot through its
whole run, so a second session's waited for all of it. Here every test of every run takes a core slot of its own: one
run's tail is the next run's start, and all of them together never run more tests than there are cores.

ctest's tests run from ctest's own description of them (ctest --show-only=json-v1), as ctest would run them: the
command in its WORKING_DIRECTORY with its ENVIRONMENT, its TIMEOUT (ctest's default 1500 s without one), its
SKIP_RETURN_CODE, after its DEPENDS and its fixtures' setup tests (a test whose fixture's setup failed is not run;
asking for a test asks for its fixtures' setups, as ctest does). A property this runner does not know fails the test
that has it, naming the property: CI runs plain ctest, and a property run differently here would pass here and not
there (or the other way). selftest's tests run in spawned worker processes (each a fresh interpreter, as before).
"""
from __future__ import annotations

import concurrent.futures
import contextlib
import json
import os
import subprocess
import sys
import threading
import time
import traceback
from dataclasses import dataclass
from typing import Callable

import checklock

PASSED, FAILED, SKIPPED, TIMEOUT, NOT_RUN = 'Passed', 'Failed', 'Skipped', 'Timeout', 'Not Run'
BAD = (FAILED, TIMEOUT, NOT_RUN)
UNKNOWN_COST = 1e9   # a test with no recorded time runs first: it may be the longest


@dataclass
class Job:
    name: str
    part: str   # 'ctest' | 'selftest' | 'gen'
    cost: float
    run: Callable[[], tuple[str, str]]   # (status, output)
    after: frozenset = frozenset()   # the jobs (of this run) that end before this one starts
    needs: frozenset = frozenset()   # of those, the ones that must not fail, else this one is not run
    index: int = 0   # ctest's number for it


@dataclass
class Result:
    status: str
    seconds: float
    text: str
    started: float = 0.0
    ended: float = 0.0


def schedule(jobs: list[Job], width: int, caps: dict[str, int] | None = None,
             done: Callable[[Job, Result], None] | None = None) -> dict[str, Result]:
    """Run `jobs`, `width` at most at once and caps[part] at most of a part, each in a core slot, the longest
    (cost) first among those whose `after` have ended; done(job, result) as each ends (from the job's thread, one
    at a time). Returns every job's result."""
    caps = caps or {}
    names = {j.name for j in jobs}
    pending = sorted(jobs, key=lambda j: -j.cost)
    results: dict[str, Result] = {}
    running: dict[str, int] = {}
    cv = threading.Condition()
    say = threading.Lock()

    def take() -> Job | None:
        with cv:
            while pending:
                for i, j in enumerate(pending):
                    if all(a in results for a in j.after if a in names) and running.get(j.part, 0) < caps.get(j.part, width):
                        del pending[i]
                        running[j.part] = running.get(j.part, 0) + 1
                        return j
                if not any(running.values()):   # nothing runs and nothing can start: `after` goes round in a circle
                    j = pending.pop(0)
                    running[j.part] = running.get(j.part, 0) + 1
                    j.run = lambda: (NOT_RUN, 'its DEPENDS / fixtures go round in a circle')
                    return j
                cv.wait()
            return None

    def finish(j: Job, r: Result) -> None:
        with cv:
            results[j.name] = r
            running[j.part] -= 1
            cv.notify_all()
        if done:
            with say:
                done(j, r)

    def worker() -> None:
        while (j := take()) is not None:
            failed = sorted(n for n in j.needs if n in results and results[n].status in BAD)
            if failed:
                now = time.perf_counter()
                finish(j, Result(NOT_RUN, 0.0, f'not run: its fixture setup {", ".join(failed)} failed\n', now, now))
                continue
            with checklock.core():
                started = time.perf_counter()
                try:
                    status, text = j.run()
                except BaseException:  # noqa: BLE001 - one job's crash is its failure, not the pool's
                    status, text = FAILED, traceback.format_exc()
                ended = time.perf_counter()
            finish(j, Result(status, ended - started, text, started, ended))

    threads = [threading.Thread(target=worker, name=f'testpool-{i}', daemon=True)
               for i in range(max(1, min(width, len(jobs))))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return results


# ---- ctest ----

SUPPORTED = {'ENVIRONMENT', 'LABELS', 'WORKING_DIRECTORY', 'SKIP_RETURN_CODE', 'TIMEOUT', 'DEPENDS',
             'FIXTURES_SETUP', 'FIXTURES_REQUIRED'}
DEFAULT_TIMEOUT = 1500.0   # ctest's (DartConfiguration TimeOut) when a test sets none


def ctest_tests(build: str) -> list[dict]:
    """ctest's tests in `build`, as it describes them (--show-only=json-v1), each given its number (from 1)."""
    out = subprocess.run(['ctest', '--test-dir', build, '--show-only=json-v1'], capture_output=True, text=True,
                         encoding='utf-8', errors='replace')
    if out.returncode:
        raise RuntimeError(f'ctest --show-only=json-v1 failed ({out.returncode}): {out.stderr.strip()}')
    tests = json.loads(out.stdout).get('tests', [])
    for i, t in enumerate(tests, 1):
        t['index'] = i
        t['props'] = {p['name']: p.get('value') for p in t.get('properties', [])}
    return tests


def _kill_tree(p: subprocess.Popen) -> None:
    if os.name == 'nt':
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(p.pid)], capture_output=True)
    with contextlib.suppress(OSError):
        p.kill()


def run_command(cmd: list[str], cwd: str, env: dict, timeout: float, skip_code: int | None) -> tuple[str, str]:
    """One ctest test's command as ctest runs it: (status, its output)."""
    try:
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
    except OSError as e:
        return NOT_RUN, f'could not run {cmd[:1]}: {e}\n'
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        _kill_tree(p)
        out, _ = p.communicate()
        return TIMEOUT, out.decode('utf-8', 'replace') + f'\n*** timed out after {timeout:.0f} s\n'
    text = out.decode('utf-8', 'replace')
    if skip_code is not None and p.returncode == skip_code:
        return SKIPPED, text
    return (PASSED, text) if p.returncode == 0 else (FAILED, text + f'\n*** exit code {p.returncode}\n')


def ctest_jobs(tests: list[dict], chosen: set[str] | None, costs: dict[str, float], env: dict,
               build: str) -> list[Job]:
    """The jobs for ctest's `tests`: the `chosen` ones (None: all) and their fixtures' setup tests."""
    by_name = {t['name']: t for t in tests}
    setups: dict[str, list[str]] = {}
    for t in tests:
        for fx in t['props'].get('FIXTURES_SETUP') or []:
            setups.setdefault(fx, []).append(t['name'])
    want = set(by_name) if chosen is None else {n for n in chosen if n in by_name}
    todo = list(want)
    while todo:   # a test asks for its fixtures' setups (ctest does the same)
        for fx in by_name[todo.pop()]['props'].get('FIXTURES_REQUIRED') or []:
            for s in setups.get(fx, []):
                if s not in want:
                    want.add(s)
                    todo.append(s)
    jobs = []
    for t in tests:
        if t['name'] not in want:
            continue
        props = t['props']
        needs = frozenset(s for fx in props.get('FIXTURES_REQUIRED') or [] for s in setups.get(fx, [])) - {t['name']}
        after = needs | frozenset(props.get('DEPENDS') or [])
        unknown = sorted(set(props) - SUPPORTED)
        if unknown:
            def run(unknown=unknown):
                return FAILED, (f'pylib/testpool.py runs ctest tests itself and does not know {", ".join(unknown)}: '
                                f'teach it that property as ctest has it (CI runs plain ctest)\n')
        else:
            test_env = dict(env)
            for kv in props.get('ENVIRONMENT') or []:
                k, _, v = kv.partition('=')
                test_env[k] = v
            skip = props.get('SKIP_RETURN_CODE')

            def run(cmd=list(t.get('command') or []), cwd=props.get('WORKING_DIRECTORY') or build, e=test_env,
                    timeout=float(props.get('TIMEOUT') or DEFAULT_TIMEOUT), skip=None if skip is None else int(skip)):
                if not cmd:
                    return NOT_RUN, 'no command (its executable is not built?)\n'
                return run_command(cmd, cwd, e, timeout, skip)
        jobs.append(Job(t['name'], 'ctest', costs.get(t['name'], UNKNOWN_COST), run, after, needs, t['index']))
    return jobs


def ctest_costs(build: str) -> dict[str, float]:
    """The seconds each ctest test took last (ctest's own CTestCostData.txt, then check.py's record over it)."""
    costs: dict[str, float] = {}
    with contextlib.suppress(OSError):
        with open(os.path.join(build, 'Testing', 'Temporary', 'CTestCostData.txt'), encoding='utf-8',
                  errors='replace') as f:
            for line in f:
                if line.startswith('---'):
                    break
                parts = line.split()
                if len(parts) == 3:
                    with contextlib.suppress(ValueError):
                        costs[parts[0]] = float(parts[2])
    costs.update(_load(os.path.join(build, 'check-ctest-cost.json')))
    return costs


def _load(path: str) -> dict[str, float]:
    try:
        with open(path, encoding='utf-8') as f:
            return {str(k): float(v) for k, v in json.load(f).items()}
    except (OSError, ValueError, AttributeError, TypeError):
        return {}


def save_costs(path: str, took: dict[str, float]) -> None:
    """Merge `took` into the cost record at `path` (this build folder's own: no other run writes it)."""
    if not took or not os.path.isdir(os.path.dirname(path)):
        return
    with contextlib.suppress(OSError):
        with open(path, 'w', encoding='utf-8') as f:
            json.dump({**_load(path), **{k: round(v, 2) for k, v in took.items()}}, f, indent=0, sort_keys=True)


def ctest_line(k: int, n: int, j: Job, r: Result) -> str:
    """ctest's line for a finished test (check.py's outcome parsing and heartbeat.parts_progress read it)."""
    status = 'Passed' if r.status == PASSED else '***' + r.status
    return f'{k:3}/{n} Test {"#" + str(j.index):>4}: {j.name} {"." * max(3, 40 - len(j.name))} {status:>10} {r.seconds:8.2f} sec\n'


# ---- selftest ----

def _selftest():
    """tools/selftest.py's module in a worker: the worker's main module when selftest.py itself started the pool
    (spawn imports it as __mp_main__), else imported (its folder is on the path the pool's starter gave it)."""
    m = sys.modules.get('selftest')
    if m is None:
        main = sys.modules.get('__mp_main__') or sys.modules.get('__main__')
        if main is not None and os.path.basename(getattr(main, '__file__', '') or '') == 'selftest.py':
            m = main
        else:
            import selftest as m  # noqa: PLC0415
        sys.modules['selftest'] = m
    return m


def selftest_names() -> list[str]:
    return [fn.__name__ for fn in _selftest().TESTS]


def selftest_one(name: str) -> tuple[str, str]:
    m = _selftest()
    index = next((i for i, fn in enumerate(m.TESTS) if fn.__name__ == name), None)
    if index is None:
        return NOT_RUN, f'tools/selftest.py has no test {name}\n'
    _, ok, _, text = m._run_one(index)
    return (PASSED if ok else FAILED), text


class SelftestWorkers:
    """Up to `n` spawned worker processes, started as tests need them (each a fresh interpreter: what a test leaves
    in a module's state stays in its worker). A worker that dies (a test that ends its process) fails its test;
    the pool is started anew for the rest."""

    def __init__(self, n: int) -> None:
        import multiprocessing
        self.n = max(1, n)
        self._ctx = multiprocessing.get_context('spawn')
        self._lock = threading.Lock()
        self._pool: concurrent.futures.ProcessPoolExecutor | None = None
        for var in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):   # one BLAS thread a worker
            os.environ.setdefault(var, '1')

    def _get(self) -> concurrent.futures.ProcessPoolExecutor:
        with self._lock:
            if self._pool is None:
                self._pool = concurrent.futures.ProcessPoolExecutor(self.n, mp_context=self._ctx)
            return self._pool

    def call(self, fn, *args):
        pool = self._get()
        try:
            return pool.submit(fn, *args).result()
        except concurrent.futures.process.BrokenProcessPool:
            with self._lock:
                if self._pool is pool:
                    self._pool = None
            raise

    def names(self) -> list[str]:
        return self.call(selftest_names)

    def job(self, name: str, cost: float) -> Job:
        def run() -> tuple[str, str]:
            try:
                return self.call(selftest_one, name)
            except concurrent.futures.process.BrokenProcessPool:
                return FAILED, f'its worker process died: {traceback.format_exc()}'
        return Job(name, 'selftest', cost, run)

    def close(self) -> None:
        with self._lock:
            if self._pool is not None:
                self._pool.shutdown(wait=True, cancel_futures=True)
                self._pool = None


def selftest_line(name: str, r: Result) -> str:
    """selftest.py's line for a finished test (after its output)."""
    return f'{"ok  " if r.status == PASSED else "FAIL"}  {name}  ({r.seconds:.1f} s)\n'


def parts(jobs: list[Job], results: dict[str, Result]) -> dict[str, tuple[int, float]]:
    """Each part's (exit code, wall seconds from its first test's start to its last's end), as check.py says them."""
    out: dict[str, tuple[int, float]] = {}
    for part in dict.fromkeys(j.part for j in jobs):
        rs = [results[j.name] for j in jobs if j.part == part and j.name in results]
        if rs:
            out[part] = (1 if any(r.status in BAD for r in rs) else 0, max(r.ended for r in rs) - min(r.started for r in rs))
    return out
