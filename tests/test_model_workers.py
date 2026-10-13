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


if __name__ == '__main__':
    unittest.main()
