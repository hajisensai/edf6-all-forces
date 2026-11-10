"""One full offline check on this machine at a time, at below-normal priority (tools/check.py, tools/selftest.py).

Every worktree's session ran its own ctest + selftest: three or four at once asked 16 logical cores for 50+ processes,
each run took 3-7x its quiet time (the same selftest: ~1000 s of test time alone, 5000-7000 s with the others; ctest
~2500 s against 7000-11000 s, the build dirs' cost files, 2026-11-10) and the desktop crawled. In line one after the
other each run is that much shorter, and the machine stays usable.

The lock is an OS file lock under %LOCALAPPDATA%: the system drops it when its holder exits, killed or not.
A process run inside a held check (EDF6_CHECK_LOCK set by the holder) does not wait for it again.
"""
from __future__ import annotations

import contextlib
import json
import os
import time
from typing import Iterator

LOCK_DIR = os.path.join(os.environ.get('LOCALAPPDATA') or os.path.expanduser('~'), 'edf6-all-forces')
LOCK_FILE = os.path.join(LOCK_DIR, 'check.lock')
HELD = 'EDF6_CHECK_LOCK'


def below_normal() -> None:
    """This process below normal priority; on Windows every process it starts inherits the class."""
    if os.name == 'nt':
        import ctypes
        k = ctypes.windll.kernel32
        k.SetPriorityClass(k.GetCurrentProcess(), 0x4000)   # BELOW_NORMAL_PRIORITY_CLASS
    else:
        with contextlib.suppress(OSError):
            os.nice(10)


def _try_lock(fd: int) -> bool:
    try:
        if os.name == 'nt':
            import msvcrt
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def _holder() -> dict:
    try:
        with open(LOCK_FILE + '.json', encoding='utf-8') as h:
            return json.load(h)
    except (OSError, ValueError):
        return {}


@contextlib.contextmanager
def held(label: str) -> Iterator[None]:
    """Wait for the machine's check slot (saying who has it), keep it for the block."""
    if os.environ.get(HELD):
        yield
        return
    os.makedirs(LOCK_DIR, exist_ok=True)
    fd = os.open(LOCK_FILE, os.O_RDWR | os.O_CREAT)
    try:
        said = False
        while not _try_lock(fd):
            if not said:
                who = _holder()
                since = time.strftime('%H:%M:%S', time.localtime(who.get('since', time.time())))
                print(f"waiting for the machine's check slot: {who.get('label', '?')} in {who.get('cwd', '?')} "
                      f"(pid {who.get('pid', '?')}, since {since})", flush=True)
                said = True
            time.sleep(5)
        with open(LOCK_FILE + '.json', 'w', encoding='utf-8') as h:
            json.dump({'label': label, 'cwd': os.getcwd(), 'pid': os.getpid(), 'since': time.time()}, h)
        os.environ[HELD] = str(os.getpid())
        try:
            yield
        finally:
            os.environ.pop(HELD, None)
    finally:
        os.close(fd)   # closing the handle releases the lock


def status() -> str:
    os.makedirs(LOCK_DIR, exist_ok=True)
    fd = os.open(LOCK_FILE, os.O_RDWR | os.O_CREAT)
    try:
        if _try_lock(fd):
            return 'the check slot is free'
        who = _holder()
        return f"held by {who.get('label', '?')} in {who.get('cwd', '?')} (pid {who.get('pid', '?')})"
    finally:
        os.close(fd)
