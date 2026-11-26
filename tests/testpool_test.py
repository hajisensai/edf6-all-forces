"""pylib/testpool.py: check.py's and selftest.py's one pool of tests (2026-11-26 「太慢了，特别是并发」): the longest
first, a job after its DEPENDS and fixtures, no more at once than asked, and ctest's tests run as ctest runs them."""
from __future__ import annotations

import os
import sys
import tempfile
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import testpool  # noqa: E402


def job(name: str, cost: float = 1.0, part: str = 'ctest', status: str = testpool.PASSED, sleep: float = 0.0,
        log: list | None = None, **kw) -> testpool.Job:
    def run() -> tuple[str, str]:
        if log is not None:
            log.append(('start', name))
        time.sleep(sleep)
        if log is not None:
            log.append(('end', name))
        return status, name
    return testpool.Job(name, part, cost, run, **kw)


class Schedule(unittest.TestCase):
    def test_longest_first(self) -> None:
        log: list = []
        testpool.schedule([job('a', 1, log=log), job('b', 9, log=log), job('c', 5, log=log)], 1)
        self.assertEqual([n for e, n in log if e == 'start'], ['b', 'c', 'a'])

    def test_after_and_needs(self) -> None:
        log: list = []
        jobs = [job('setup', 1, log=log, status=testpool.FAILED, sleep=0.1),
                job('dep', 1, log=log, sleep=0.1),
                job('user', 99, log=log, after=frozenset({'setup', 'dep', 'not-in-this-run'}), needs=frozenset({'setup'})),
                job('later', 50, log=log, after=frozenset({'dep'}))]
        out = testpool.schedule(jobs, 4)
        self.assertEqual(out['user'].status, testpool.NOT_RUN)   # its fixture's setup failed
        self.assertIn('setup', out['user'].text)
        self.assertNotIn(('start', 'user'), log)
        self.assertLess(log.index(('end', 'dep')), log.index(('start', 'later')))
        self.assertEqual(out['later'].status, testpool.PASSED)

    def test_a_circle_is_not_run_not_a_hang(self) -> None:
        out = testpool.schedule([job('x', after=frozenset({'y'})), job('y', after=frozenset({'x'}))], 2)
        self.assertIn(testpool.NOT_RUN, {out['x'].status, out['y'].status})

    def test_width_and_caps(self) -> None:
        lock = threading.Lock()
        now = {'all': 0, 'selftest': 0}
        most = {'all': 0, 'selftest': 0}

        def counted(name: str, part: str) -> testpool.Job:
            def run() -> tuple[str, str]:
                with lock:
                    for k in ('all', part):
                        if k in now:
                            now[k] += 1
                            most[k] = max(most[k], now[k])
                time.sleep(0.05)
                with lock:
                    for k in ('all', part):
                        if k in now:
                            now[k] -= 1
                return testpool.PASSED, ''
            return testpool.Job(name, part, 1.0, run)
        jobs = [counted(f's{i}', 'selftest') for i in range(8)] + [counted(f'c{i}', 'ctest') for i in range(8)]
        out = testpool.schedule(jobs, 4, {'selftest': 2})
        self.assertEqual(len(out), 16)
        self.assertLessEqual(most['all'], 4)
        self.assertLessEqual(most['selftest'], 2)
        self.assertEqual(most['all'], 4)   # the cap leaves the others' share to ctest

    def test_a_crash_is_its_jobs_failure(self) -> None:
        def boom() -> tuple[str, str]:
            raise RuntimeError('boom')
        out = testpool.schedule([testpool.Job('b', 'ctest', 1, boom), job('ok')], 2)
        self.assertEqual(out['b'].status, testpool.FAILED)
        self.assertIn('boom', out['b'].text)
        self.assertEqual(out['ok'].status, testpool.PASSED)

    def test_parts(self) -> None:
        jobs = [job('a'), job('b', status=testpool.SKIPPED), job('s', part='selftest', status=testpool.FAILED)]
        out = testpool.schedule(jobs, 3)
        parts = testpool.parts(jobs, out)
        self.assertEqual(parts['ctest'][0], 0)
        self.assertEqual(parts['selftest'][0], 1)


def ctest(name: str, command: list[str], **props) -> dict:
    return {'name': name, 'command': command, 'index': 0, 'props': props}


class CTest(unittest.TestCase):
    PY = sys.executable

    def run_one(self, t: dict) -> testpool.Result:
        jobs = testpool.ctest_jobs([t], None, {}, dict(os.environ), ROOT)
        return testpool.schedule(jobs, 1)[t['name']]

    def test_as_ctest_runs_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            r = self.run_one(ctest('env', [self.PY, '-c', 'import os; print(os.getcwd(), os.environ["X_T"])'],
                                   WORKING_DIRECTORY=tmp, ENVIRONMENT=['X_T=a=b']))
            self.assertEqual(r.status, testpool.PASSED)
            self.assertEqual(r.text.split(), [os.path.realpath(tmp), 'a=b'])
        self.assertEqual(self.run_one(ctest('skip', [self.PY, '-c', 'raise SystemExit(77)'], SKIP_RETURN_CODE=77)).status,
                         testpool.SKIPPED)
        self.assertEqual(self.run_one(ctest('fail', [self.PY, '-c', 'raise SystemExit(77)'])).status, testpool.FAILED)
        r = self.run_one(ctest('slow', [self.PY, '-c', 'import time; time.sleep(60)'], TIMEOUT=1))
        self.assertEqual(r.status, testpool.TIMEOUT)
        self.assertLess(r.seconds, 30)
        self.assertEqual(self.run_one(ctest('gone', [os.path.join(ROOT, 'no-such.exe')])).status, testpool.NOT_RUN)

    def test_a_property_it_does_not_know_fails_the_test(self) -> None:
        r = self.run_one(ctest('w', [self.PY, '-c', 'pass'], WILL_FAIL=True))
        self.assertEqual(r.status, testpool.FAILED)
        self.assertIn('WILL_FAIL', r.text)

    def test_a_chosen_test_brings_its_fixtures_setups(self) -> None:
        tests = [ctest('setup', [self.PY, '-c', 'pass'], FIXTURES_SETUP=['fx']),
                 ctest('user', [self.PY, '-c', 'pass'], FIXTURES_REQUIRED=['fx'], DEPENDS=['other']),
                 ctest('other', [self.PY, '-c', 'pass'])]
        jobs = {j.name: j for j in testpool.ctest_jobs(tests, {'user'}, {}, dict(os.environ), ROOT)}
        self.assertEqual(set(jobs), {'user', 'setup'})
        self.assertEqual(jobs['user'].needs, {'setup'})
        self.assertEqual(jobs['user'].after, {'setup', 'other'})

    def test_every_property_the_build_gives_is_known(self) -> None:
        """The build's tests use only what the runner knows (else check.py fails them; CI's plain ctest would not)."""
        build = os.path.join(ROOT, 'build')
        if not os.path.isfile(os.path.join(build, 'CTestTestfile.cmake')):
            self.skipTest('no configured build')
        used = {p for t in testpool.ctest_tests(build) for p in t['props']}
        self.assertLessEqual(used, testpool.SUPPORTED, used - testpool.SUPPORTED)


if __name__ == '__main__':
    unittest.main()
