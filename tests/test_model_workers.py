"""Real spawn jobs preserve order, errors, CPU policy and child thread limits."""
from pathlib import Path
import os
import sys
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import model_workers


def echo(value: bytes, delay: float) -> bytes:
    time.sleep(delay)
    return str(os.getpid()).encode() + b'|' + value + b'|' + os.environ.get('OPENBLAS_NUM_THREADS', '').encode()


def fail() -> bytes:
    raise ValueError('model generation failed')


class ModelWorkerTests(unittest.TestCase):
    def test_cpu_and_available_memory_limit(self) -> None:
        self.assertEqual(model_workers.workers_for(16, 8 * 1024**3), 2)
        self.assertEqual(model_workers.workers_for(2, 8 * 1024**3), 1)
        self.assertEqual(model_workers.workers_for(16, 3 * 1024**3), 1)
        self.assertEqual(model_workers.workers_for(16, None), 1)

    def test_real_parallel_order_and_environment_restoration(self) -> None:
        before = os.environ.get('OPENBLAS_NUM_THREADS')
        result = model_workers.run_jobs([(echo, (b'first', 0.3), 'first'), (echo, (b'second', 0.1), 'second')], workers=2)
        first, second = [value.split(b'|') for value in result]
        self.assertNotEqual(first[0], second[0])
        self.assertNotEqual(int(first[0]), os.getpid())
        self.assertEqual((first[1:], second[1:]), ([b'first', b'1'], [b'second', b'1']))
        self.assertEqual(os.environ.get('OPENBLAS_NUM_THREADS'), before)

    def test_child_error_is_not_silently_retried(self) -> None:
        with self.assertRaisesRegex(ValueError, 'model generation failed'):
            model_workers.run_jobs([(fail, (), 'broken'), (echo, (b'other', 0), 'other')], workers=2)

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
        self.assertIn('[入口检查 1] 开始生成', out)


if __name__ == '__main__':
    unittest.main()
