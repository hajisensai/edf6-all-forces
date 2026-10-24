"""Bounded, read-only model jobs, with a spawn-safe frozen installer entry."""
from __future__ import annotations

from collections import deque
from concurrent.futures import ProcessPoolExecutor, as_completed
import contextlib
import ctypes
import multiprocessing
import os
import sys
import time
from typing import Any, Callable, Iterator, TextIO

Job = tuple[Callable[..., bytes], tuple[Any, ...], str]


def workers_for(cpus: int, available: int | None, cap: int = 2, per_worker: int = 2 * 1024**3) -> int:
    cpu_limit = max(1, cpus // 2)
    # Reserve 2 GiB for the desktop/parent and budget `per_worker` (2 GiB) per model worker.
    memory_limit = max(1, (available - 2 * 1024**3) // per_worker) if available is not None else 1
    return min(cap, cpu_limit, memory_limit)


def available_memory() -> int | None:
    """Physical memory free now (bytes), None when it cannot be told."""
    if os.name != 'nt':
        return None
    class MemoryStatus(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [
            (name, ctypes.c_ulonglong) for name in
            ('total', 'available', 'total_page', 'available_page', 'total_virtual', 'available_virtual', 'extended')]
    status = MemoryStatus()
    status.length = ctypes.sizeof(status)
    return status.available if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)) else None


def worker_count() -> int:
    return workers_for(os.cpu_count() or 1, available_memory())


def utf8_pipes() -> None:
    """stdout / stderr as UTF-8 when they are a pipe or a file: the installer speaks Chinese, and a redirected stream
    otherwise takes the ANSI code page (cp1252 on CI and English Windows: UnicodeEncodeError on the first line).
    A console is left alone: Python writes it with WriteConsoleW whatever the code page."""
    for stream in (sys.stdout, sys.stderr):
        if stream is not None and not stream.isatty() and hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')


class _TaggedOutput:
    def __init__(self, stream: TextIO, label: str) -> None:
        self.stream, self.label, self.pending = stream, label, ''

    def write(self, value: str) -> int:
        self.pending += value
        while '\n' in self.pending:
            line, self.pending = self.pending.split('\n', 1)
            self.stream.write(f'  [{self.label}] {line}\n')
        return len(value)

    def flush(self) -> None:
        if self.pending:
            self.stream.write(f'  [{self.label}] {self.pending}\n')
            self.pending = ''
        self.stream.flush()


def _run(job: Job) -> bytes:
    function, arguments, label = job
    utf8_pipes()   # a spawned child's streams are its own (frozen children also pass the installer's __main__)
    started = time.perf_counter()
    output = _TaggedOutput(sys.stdout, label)
    with contextlib.redirect_stdout(output):
        print('开始生成……', flush=True)
        result = function(*arguments)
        print(f'生成完成（{time.perf_counter()-started:.1f} 秒）', flush=True)
    return result


@contextlib.contextmanager
def _one_blas_thread() -> Iterator[None]:
    names = ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS')
    saved = {name: os.environ.get(name) for name in names}
    try:
        # Set before spawning/importing NumPy; avoid a BLAS thread pool per child.
        for name in names:
            os.environ[name] = '1'
        yield
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def ordered_jobs(jobs: list[Job], workers: int) -> Iterator[Any]:
    """Each job's result in the jobs' order, as soon as it and those before it are done: at most `workers` jobs run or
    wait to be taken at a time, so no more than that many results are held (a map's files are hundreds of MB). One
    worker (or one job): the jobs run here, one by one, as the results are taken. A job's error is raised when its
    result is taken; the jobs after it are not waited for."""
    count = min(len(jobs), workers)
    if count <= 1:
        for job in jobs:
            yield _run(job)
        return
    print(f'  使用 {count} 个转换进程（共 {len(jobs)} 个任务）', flush=True)
    with _one_blas_thread(), ProcessPoolExecutor(max_workers=count, mp_context=multiprocessing.get_context('spawn')) as pool:
        waiting: deque = deque()
        queue = iter(jobs)
        try:
            for job in queue:
                waiting.append(pool.submit(_run, job))
                if len(waiting) < count:
                    continue
                yield waiting.popleft().result()
            while waiting:
                yield waiting.popleft().result()
        finally:
            for future in waiting:
                future.cancel()


def run_jobs(jobs: list[Job], workers: int | None = None) -> list[bytes]:
    count = min(len(jobs), worker_count() if workers is None else workers)
    if count <= 1:
        return [_run(job) for job in jobs]
    print(f'  使用 {count} 个生成进程（共 {len(jobs)} 个模型任务）', flush=True)
    with _one_blas_thread(), ProcessPoolExecutor(max_workers=count, mp_context=multiprocessing.get_context('spawn')) as pool:
        results = [b''] * len(jobs)
        pending = {pool.submit(_run, job): i for i, job in enumerate(jobs)}
        for future in as_completed(pending):
            results[pending[future]] = future.result()
        return results


def _process_identity() -> bytes:
    return str(os.getpid()).encode('ascii')


def check_entry() -> None:
    result = run_jobs([(_process_identity, (), '入口检查 1'), (_process_identity, (), '入口检查 2')], workers=2)
    assert all(int(value) != os.getpid() for value in result), 'model jobs ran in the installer process'
    print('EDF6VC_WORKERS_OK spawn entry', flush=True)
