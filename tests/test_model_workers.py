"""Real spawn jobs preserve order, errors, CPU policy and child thread limits."""
from pathlib import Path
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import model_workers


def echo(value: bytes, delay: float) -> bytes:
    time.sleep(delay)
    return str(os.getpid()).encode() + b'|' + value + b'|' + os.environ.get('OPENBLAS_NUM_THREADS', '').encode()


def meet(folder: str, label: str, peers: tuple[str, ...]) -> bytes:
    """Marks `label` running, then returns only once every one of `peers` is running too: two jobs that both return
    ran at the same time, in two processes (a worker runs one job at a time). However long the second worker takes to
    start; a pool that never runs them together hangs here, and that is the CTest TIMEOUT (CMakeLists.txt)."""
    Path(folder, label).touch()
    while not all(Path(folder, peer).exists() for peer in peers):
        time.sleep(0.01)
    return str(os.getpid()).encode() + b'|' + label.encode() + b'|' + os.environ.get('OPENBLAS_NUM_THREADS', '').encode()


def fail() -> bytes:
    raise ValueError('model generation failed')


def nested(workers: int) -> bytes:
    """A job running jobs of its own (an asset group built beside the others and its models: installer.build_assets)."""
    return b','.join(v.split(b'|')[1] for v in model_workers.run_jobs(
        [(echo, (b'in1', 0), 'inner 1'), (echo, (b'in2', 0), 'inner 2')], workers))


class ModelWorkerTests(unittest.TestCase):
    def test_cpu_and_available_memory_limit(self) -> None:
        self.assertEqual(model_workers.workers_for(16, 8 * 1024**3), 2)
        self.assertEqual(model_workers.workers_for(2, 8 * 1024**3), 1)
        self.assertEqual(model_workers.workers_for(16, 3 * 1024**3), 1)
        self.assertEqual(model_workers.workers_for(16, None), 1)

    def test_real_parallel_order_and_environment_restoration(self) -> None:
        # The two jobs wait for each other (meet): they must run at once, so in two workers. They used to sleep 0.3 s
        # and 0.1 s and be expected on two PIDs, but the pool starts its workers on demand and either may take both
        # jobs: under a busy ctest -j the second worker's interpreter took longer than 0.3 s to start, the first had
        # finished `first` and took `second` as well (one PID; 111 of 160 runs, 16 at a time on a busy machine).
        before = os.environ.get('OPENBLAS_NUM_THREADS')
        with tempfile.TemporaryDirectory() as folder:
            result = model_workers.run_jobs([(meet, (folder, 'first', ('second',)), 'first'),
                                             (meet, (folder, 'second', ('first',)), 'second')], workers=2)
        first, second = [value.split(b'|') for value in result]
        self.assertNotEqual(first[0], second[0])
        self.assertNotEqual(int(first[0]), os.getpid())
        self.assertEqual((first[1:], second[1:]), ([b'first', b'1'], [b'second', b'1']))
        self.assertEqual(os.environ.get('OPENBLAS_NUM_THREADS'), before)

    def test_child_error_is_not_silently_retried(self) -> None:
        with self.assertRaisesRegex(ValueError, 'model generation failed'):
            model_workers.run_jobs([(fail, (), 'broken'), (echo, (b'other', 0), 'other')], workers=2)

    def test_ordered_jobs_keep_order_and_bound_what_is_held(self) -> None:
        # the slow first job is taken first; a later job does not start before an earlier result is taken
        jobs = [(echo, (str(i).encode(), 0.4 if i == 0 else 0.05), f'job {i}') for i in range(5)]
        taken = []
        for value in model_workers.ordered_jobs(jobs, 2):
            taken.append(value.split(b'|')[1])
        self.assertEqual(taken, [b'0', b'1', b'2', b'3', b'4'])
        self.assertEqual(model_workers.workers_for(16, 8 * 1024**3, cap=4, per_worker=2 * 1024**3), 3)
        self.assertEqual(model_workers.workers_for(16, 64 * 1024**3, cap=4, per_worker=2 * 1024**3), 4)
        self.assertEqual(model_workers.workers_for(4, 64 * 1024**3, cap=4, per_worker=2 * 1024**3), 2)

    def test_ordered_jobs_error_at_its_turn_and_one_worker_runs_here(self) -> None:
        results = model_workers.ordered_jobs([(echo, (b'a', 0), 'a'), (fail, (), 'broken'), (echo, (b'c', 0), 'c')], 2)
        self.assertEqual(next(results).split(b'|')[1], b'a')
        with self.assertRaisesRegex(ValueError, 'model generation failed'):
            next(results)
        here = list(model_workers.ordered_jobs([(echo, (b'x', 0), 'x'), (echo, (b'y', 0), 'y')], 1))
        self.assertEqual([int(v.split(b'|')[0]) for v in here], [os.getpid()] * 2)

    def test_redirected_output_survives_a_non_chinese_code_page(self) -> None:
        # CI 2026-10-14: the frozen --check-workers died on its first Chinese line, stdout a cp1252 pipe.
        import subprocess
        root = Path(__file__).resolve().parents[1]
        env = {**os.environ, 'PYTHONIOENCODING': 'cp1252'}
        env.pop('PYTHONPATH', None)
        run = subprocess.run([sys.executable, str(root / 'tools' / 'installer.py'), '--check-workers'],
                             capture_output=True, env=env, timeout=120)
        self.assertEqual(run.returncode, 0, run.stderr.decode('utf-8', 'replace'))
        out = run.stdout.decode('utf-8')
        self.assertIn('EDF6VC_WORKERS_OK', out)
        self.assertIn('[entry check 1] 开始生成', out)

    def test_jobs_inside_a_job(self) -> None:
        # 2026-11-10: a group built in its own process ran its models here (one worker) through _run's utf8_pipes on
        # the group's tagged stdout, which had no isatty
        self.assertEqual(model_workers.run_jobs([(nested, (1,), 'group 1'), (nested, (2,), 'group 2')], workers=2),
                         [b'in1,in2', b'in1,in2'])
        self.assertEqual(nested(1), b'in1,in2')


if __name__ == '__main__':
    unittest.main()
