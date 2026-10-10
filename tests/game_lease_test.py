"""The real-game test queue (pylib/gamelease.py, tools/gamequeue.py) across real processes: one holder at a time,
first come first served, a dead holder or waiter frees its place at once, a token holder takes the lease again without
waiting, separate game folders do not wait on each other, and `hold` / `release` hand the game over."""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamelease  # noqa: E402

QUEUE = os.path.join(ROOT, 'tools', 'gamequeue.py')
# A holder: takes the lease on argv[1] with label argv[2], appends the label to argv[3], keeps it until argv[4] exists.
HOLDER = r'''
import os, sys, time
sys.path.insert(0, {pylib!r})
import gamelease
game, label, order, until = sys.argv[1:5]
with gamelease.lease(game, label, say=lambda s: None):
    with open(order, 'a') as f:
        f.write(label + '\n')
    while not os.path.exists(until):
        time.sleep(0.05)
'''.format(pylib=os.path.join(ROOT, 'pylib'))


def wait_until(check, seconds: float = 15) -> bool:  # noqa: ANN001 - a no-argument predicate
    end = time.time() + seconds
    while time.time() < end:
        if check():
            return True
        time.sleep(0.05)
    return False


class GameLeaseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.env = dict(os.environ, **{gamelease.DIR_ENV: os.path.join(self.tmp.name, 'queues')})
        self.env.pop(gamelease.ENV, None)
        self.old = {k: os.environ.get(k) for k in (gamelease.DIR_ENV, gamelease.ENV)}
        os.environ[gamelease.DIR_ENV] = self.env[gamelease.DIR_ENV]
        os.environ.pop(gamelease.ENV, None)
        self.game = os.path.join(self.tmp.name, 'game')
        self.order = os.path.join(self.tmp.name, 'order.txt')
        self.procs: list[subprocess.Popen] = []

    def tearDown(self) -> None:
        for p in self.procs:
            if p.poll() is None:
                p.kill()
            p.wait()
            if p.stdout:
                p.stdout.close()
        for k, v in self.old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self.tmp.cleanup()

    def holder(self, label: str, game: str | None = None) -> tuple[subprocess.Popen, str]:
        until = os.path.join(self.tmp.name, 'done-' + label)
        p = subprocess.Popen([sys.executable, '-c', HOLDER, game or self.game, label, self.order, until], env=self.env)
        self.procs.append(p)
        return p, until

    def order_seen(self) -> list[str]:
        try:
            with open(self.order) as f:
                return f.read().split()
        except FileNotFoundError:
            return []

    def holding(self, label: str, game: str | None = None) -> bool:
        return (gamelease.holder(game or self.game) or {}).get('label') == label

    def test_one_holder_and_a_wait_limit(self) -> None:
        self.assertIsNone(gamelease.holder(self.game))
        _, until = self.holder('a')
        self.assertTrue(wait_until(lambda: self.holding('a')))
        r = subprocess.run([sys.executable, QUEUE, 'run', '--game', self.game, '--wait-max-min', '0.02', '--',
                            sys.executable, '-c', 'raise SystemExit(3)'], env=self.env, capture_output=True, text=True)
        self.assertEqual(r.returncode, gamelease.WAIT_EXPIRED, r.stdout + r.stderr)
        self.assertIn('nothing run', r.stdout)
        open(until, 'w').close()
        self.assertTrue(wait_until(lambda: gamelease.holder(self.game) is None))
        r = subprocess.run([sys.executable, QUEUE, 'run', '--game', self.game, '--', sys.executable, '-c',
                            'raise SystemExit(3)'], env=self.env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)   # the command's own exit code

    def test_a_dead_holder_frees_the_game_at_once(self) -> None:
        p, _ = self.holder('a')
        self.assertTrue(wait_until(lambda: self.holding('a')))
        p.kill()
        p.wait()
        self.assertIsNone(gamelease.holder(self.game))
        _, until = self.holder('b')
        self.assertTrue(wait_until(lambda: self.holding('b')))
        open(until, 'w').close()

    def test_first_come_first_served_and_dead_waiters_leave_the_line(self) -> None:
        _, until_a = self.holder('a')
        self.assertTrue(wait_until(lambda: self.holding('a')))
        dead, _ = self.holder('dead')
        self.assertTrue(wait_until(lambda: len(gamelease.waiters(self.game)) == 1))
        _, until_b = self.holder('b')
        self.assertTrue(wait_until(lambda: len(gamelease.waiters(self.game)) == 2))
        _, until_c = self.holder('c')
        self.assertTrue(wait_until(lambda: [w['label'] for w in gamelease.waiters(self.game)] == ['dead', 'b', 'c']))
        dead.kill()
        dead.wait()
        self.assertTrue(wait_until(lambda: [w['label'] for w in gamelease.waiters(self.game)] == ['b', 'c']))
        for until, nxt in ((until_a, 'b'), (until_b, 'c'), (until_c, None)):
            open(until, 'w').close()
            if nxt:
                self.assertTrue(wait_until(lambda: self.holding(nxt)), nxt)
        self.assertTrue(wait_until(lambda: len(self.order_seen()) == 3))
        self.assertEqual(self.order_seen(), ['a', 'b', 'c'])

    def test_a_free_game_goes_to_the_first_in_line_only(self) -> None:
        # A live earlier ticket (its waiter has not looked at the slot yet): a later waiter must not jump it.
        path = os.path.join(gamelease.queue_dir(self.game), 'queue', '0' * 20 + '-earlier')
        with open(path, 'x+b') as earlier:
            self.assertTrue(gamelease._try_lock(earlier))
            self.assertIsNone(gamelease.holder(self.game))
            with self.assertRaises(gamelease.WaitExpired):
                with gamelease.lease(self.game, 'later', wait_max=1, say=lambda s: None):
                    pass
        with gamelease.lease(self.game, 'later', wait_max=1, say=lambda s: None):   # its waiter gone: no longer ahead
            self.assertTrue(self.holding('later'))

    def test_the_token_holder_takes_it_again_without_waiting(self) -> None:
        with gamelease.lease(self.game, 'outer', say=lambda s: None) as info:
            self.assertEqual(os.environ[gamelease.ENV], info['token'])
            with gamelease.lease(self.game, 'inner', wait_max=0, say=lambda s: None):
                self.assertTrue(self.holding('outer'))
            env = dict(self.env, **{gamelease.ENV: info['token']})
            r = subprocess.run([sys.executable, QUEUE, 'run', '--game', self.game, '--wait-max-min', '0', '--',
                                sys.executable, '-c', 'pass'], env=env, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            # A wrong token waits like anyone else.
            env[gamelease.ENV] = 'f' * 32
            r = subprocess.run([sys.executable, QUEUE, 'run', '--game', self.game, '--wait-max-min', '0', '--',
                                sys.executable, '-c', 'pass'], env=env, capture_output=True, text=True)
            self.assertEqual(r.returncode, gamelease.WAIT_EXPIRED, r.stdout + r.stderr)
        self.assertNotIn(gamelease.ENV, os.environ)
        self.assertIsNone(gamelease.holder(self.game))

    def test_other_game_folders_do_not_wait(self) -> None:
        _, until = self.holder('a')
        self.assertTrue(wait_until(lambda: self.holding('a')))
        other = os.path.join(self.tmp.name, 'scratch-copy')
        with gamelease.lease(other, 'b', wait_max=0, say=lambda s: None):
            self.assertTrue(self.holding('b', other))
        open(until, 'w').close()

    def test_hold_and_release(self) -> None:
        p = subprocess.Popen([sys.executable, QUEUE, 'hold', '--game', self.game, '--label', 'session'],
                             env=self.env, stdout=subprocess.PIPE, text=True)
        self.procs.append(p)
        token = ''
        while not token:
            line = p.stdout.readline()
            self.assertTrue(line, 'hold ended without a token')
            if line.startswith(gamelease.ENV + '='):
                token = line.strip().split('=', 1)[1]
        self.assertTrue(self.holding('session'))
        status = subprocess.run([sys.executable, QUEUE, 'status', '--game', self.game], env=self.env,
                                capture_output=True, text=True).stdout
        self.assertIn('held by: session', status)
        r = subprocess.run([sys.executable, QUEUE, 'release', '--game', self.game, token], env=self.env,
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(p.wait(15), 0)
        self.assertIsNone(gamelease.holder(self.game))


if __name__ == '__main__':
    unittest.main()
