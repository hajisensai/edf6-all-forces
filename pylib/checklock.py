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


_JOB = None   # the job object children_die_with_me() put this process in: open as long as the process lives


def children_die_with_me() -> None:
    """Every process this one starts from now on, and theirs, ends when this one does, however it ends (killed
    included): Windows puts them in a job object set to KILL_ON_JOB_CLOSE whose only handle this process holds, so the
    system closes it, and ends them, when this process goes. A check.py killed alone left its ctest and selftest
    running against the same build folder, under the next check. Elsewhere: nothing (CI and this machine are Windows)."""
    global _JOB
    if os.name != 'nt' or _JOB is not None:
        return
    import ctypes
    from ctypes import wintypes
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.CreateJobObjectW.restype = wintypes.HANDLE
    k.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    k.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    k.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    k.GetCurrentProcess.restype = wintypes.HANDLE

    class Basic(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64),
                    ('LimitFlags', wintypes.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                    ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wintypes.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]

    class Io(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ('ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
                                                   'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]

    class Extended(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', Basic), ('IoInfo', Io), ('ProcessMemoryLimit', ctypes.c_size_t),
                    ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t),
                    ('PeakJobMemoryUsed', ctypes.c_size_t)]

    job = k.CreateJobObjectW(None, None)   # not inheritable: no child holds it open after this process is gone
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    info = Extended()
    info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not k.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):   # ExtendedLimitInformation
        raise ctypes.WinError(ctypes.get_last_error())
    if not k.AssignProcessToJobObject(job, k.GetCurrentProcess()):
        raise ctypes.WinError(ctypes.get_last_error())
    _JOB = job


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


class Busy(Exception):
    """This worktree's check is already queued or running: {'pid', 'root', 'since'} of the one that has it, or
    {'starting': True} when its holder has the lock and has not written itself down yet (or is letting it go)."""


def alive(pid: int) -> bool:
    """Whether process `pid` is running."""
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.OpenProcess.restype = wintypes.HANDLE
        h = k.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return ctypes.get_last_error() == 5   # access denied: it exists
        try:
            code = wintypes.DWORD()
            return bool(k.GetExitCodeProcess(wintypes.HANDLE(h), ctypes.byref(code))) and code.value == 259   # STILL_ACTIVE
        finally:
            k.CloseHandle(wintypes.HANDLE(h))
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def worktree_lock(root: str) -> str:
    """The lock file of `root`'s check; its holder's record is the same path + '.json'."""
    import hashlib
    key = hashlib.sha1(os.path.normcase(os.path.abspath(root)).encode('utf-8')).hexdigest()[:16]
    return os.path.join(LOCK_DIR, f'check-{key}.lock')


def worktree_holder(path: str) -> dict:
    """Who holds the lock at `path` (worktree_lock), as its holder wrote it down; {'starting': True} when no live
    process is written down: the holder has the lock and not written itself yet (the record is written after the lock
    is taken, and removed before it is let go), or a holder that was killed left its record."""
    try:
        with open(path + '.json', encoding='utf-8') as h:
            who = json.load(h)
    except (OSError, ValueError):
        return {'starting': True}
    return who if isinstance(who.get('pid'), int) and alive(who['pid']) else {'starting': True}


@contextlib.contextmanager
def worktree_once(root: str) -> Iterator[None]:
    """One check.py per worktree (2026-11-24: a session that started a second check of the same tree while the first
    still waited in the queue doubled the queue): raises Busy at once, without waiting, when another holds it."""
    os.makedirs(LOCK_DIR, exist_ok=True)
    path = worktree_lock(root)
    fd = os.open(path, os.O_RDWR | os.O_CREAT)
    try:
        if not _try_lock(fd):
            raise Busy(worktree_holder(path))
        # the record whole or not at all (a reader never sees half of it), and only while the lock is held: a reader
        # that finds none, or a dead process in it, knows the holder has not written itself down yet
        tmp = f'{path}.{os.getpid()}.tmp'
        with open(tmp, 'w', encoding='utf-8') as h:
            json.dump({'root': os.path.abspath(root), 'pid': os.getpid(), 'since': time.time()}, h)
        os.replace(tmp, path + '.json')
        try:
            yield
        finally:
            with contextlib.suppress(FileNotFoundError):
                os.remove(path + '.json')
    finally:
        os.close(fd)
