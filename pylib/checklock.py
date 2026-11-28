"""The machine's slots the offline checks share, at below-normal priority (tools/check.py, tools/selftest.py).

Every worktree's session ran its own ctest + selftest: three or four at once asked 16 logical cores for 50+ processes,
each run took 3-7x its quiet time (the same selftest: ~1000 s of test time alone, 5000-7000 s with the others; ctest
~2500 s against 7000-11000 s, the build dirs' cost files, 2026-11-10) and the desktop crawled. One check at a time
fixed that but made the queue the cost (2026-11-26 「太慢了，特别是并发」: one session's 3-minute build and 10-minute
tests held every other session's; the Nth in line waited (N-1) x 10+ minutes). Now:
- build_slot(): BUILDS builds at once on the machine, apart from the tests.
- core(): one of cores() core slots for each test any check or selftest runs: all of them together never run more
  tests than there are cores, and a core one run leaves is the next test's, whoever's.
The slots are named mutexes (Windows): the system lets go of a dead holder's, killed or not, and a waiter waits in the
kernel, woken the moment a slot is let go. A process started by a run (EDF6_CHECK_LOCK naming that run) waits for
none again.
"""
from __future__ import annotations

import contextlib
import json
import os
import threading
import time
from typing import Iterator

import heartbeat

LOCK_DIR = os.path.join(os.environ.get('LOCALAPPDATA') or os.path.expanduser('~'), 'edf6-all-forces')
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
    # The compiler's PDB server (mspdbsrv.exe, /Zi /FS) is one per endpoint for the whole machine: the first compile
    # to need it starts it, and here that compile is in the job, so the server is too and ends with this process,
    # under every other session's build connected to it (their compiles fail on the PDB). This process's builds get
    # a server of their own instead, started in the job and ended with it.
    os.environ['_MSPDBSRV_ENDPOINT_'] = f'edf6-check-{os.getpid()}'


def below_normal() -> None:
    """This process at idle priority (the name kept: what it asks is "below everything the user runs"); on Windows
    every process it starts inherits the class. Idle, not below normal: a perfrun's game runs below normal, and a
    check's ctest beside it at the same class took its cores (2026-11-28: the game at 5% of a core, 921 ms frames)."""
    if os.name == 'nt':
        import ctypes
        k = ctypes.windll.kernel32
        k.SetPriorityClass(k.GetCurrentProcess(), 0x40)   # IDLE_PRIORITY_CLASS: below the game's BELOW_NORMAL
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


def cores() -> int:
    """The machine's core slots: one a logical core (WaitForMultipleObjects waits on 64 at most)."""
    return max(1, min(64, os.cpu_count() or 8))


BUILDS = 2   # builds at once on the machine (2026-11-26: one session's build no longer holds every other's tests)


def _inside() -> bool:
    """Started by a check or selftest that holds the slots for it (HELD names that process, not this one)."""
    v = os.environ.get(HELD)
    return bool(v) and v != str(os.getpid())


class Slots:
    """`n` slots the whole machine shares, named `name`: a process holds one for a while and the system gives it back
    when that process ends, killed or not. Windows: `n` named mutexes, a wait for any one of them in the kernel
    (WaitForMultipleObjects), so a waiter takes a slot the moment one is let go and the waiters are served about in
    the order they came; a dead holder's mutex is abandoned, the next waiter gets it (WAIT_ABANDONED). A mutex is its
    thread's: the thread that took a slot lets it go. Elsewhere (no Windows on CI or here): `n` lock files."""

    def __init__(self, name: str, n: int) -> None:
        self.name, self.n = name, max(1, min(64, n))
        self._handles = None
        self._fds: list[int] = []
        self._init = threading.Lock()
        self._local = threading.Lock()
        self._mine: set[int] = set()

    def _open(self) -> None:
        with self._init:
            if self._handles is not None or self._fds:
                return
            if os.name == 'nt':
                import ctypes
                from ctypes import wintypes
                k = ctypes.WinDLL('kernel32', use_last_error=True)
                k.CreateMutexW.restype = wintypes.HANDLE
                k.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
                k.WaitForMultipleObjects.restype = wintypes.DWORD
                k.WaitForMultipleObjects.argtypes = [wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE), wintypes.BOOL,
                                                     wintypes.DWORD]
                k.ReleaseMutex.argtypes = [wintypes.HANDLE]
                handles = (wintypes.HANDLE * self.n)()
                for i in range(self.n):
                    h = k.CreateMutexW(None, False, f'edf6-all-forces-{self.name}-{i}')
                    if not h:
                        raise ctypes.WinError(ctypes.get_last_error())
                    handles[i] = h
                self._k, self._handles = k, handles
            else:
                os.makedirs(LOCK_DIR, exist_ok=True)
                self._fds = [os.open(os.path.join(LOCK_DIR, f'{self.name}-{i}.lock'), os.O_RDWR | os.O_CREAT)
                             for i in range(self.n)]

    def take(self, timeout: float | None = None) -> int | None:
        """A slot's index, waiting for one up to `timeout` seconds (None: as long as it takes); None when none came."""
        self._open()
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            # the wait is the kernel's: a slot let go wakes it at once. Its second at most only lets Ctrl+C in.
            left = 1.0 if deadline is None else max(0.0, min(1.0, deadline - time.monotonic()))
            got = self._try(left)
            if got is not None or (deadline is not None and time.monotonic() >= deadline):
                return got

    def _try(self, seconds: float) -> int | None:
        if self._handles is not None:
            r = self._k.WaitForMultipleObjects(self.n, self._handles, False, int(seconds * 1000))
            if r < self.n:
                return r
            if 0x80 <= r < 0x80 + self.n:   # WAIT_ABANDONED_0: its holder died holding it; ours now
                return r - 0x80
            if r == 0x102:   # WAIT_TIMEOUT
                return None
            import ctypes
            raise ctypes.WinError(ctypes.get_last_error())
        end = time.monotonic() + seconds
        while True:
            with self._local:
                for i, fd in enumerate(self._fds):
                    if i not in self._mine and _try_lock(fd):
                        self._mine.add(i)
                        return i
            if time.monotonic() >= end:
                return None
            time.sleep(0.05)

    def give(self, i: int) -> None:
        if self._handles is not None:
            if not self._k.ReleaseMutex(self._handles[i]):
                import ctypes
                raise ctypes.WinError(ctypes.get_last_error())
            return
        import fcntl
        with self._local:
            fcntl.flock(self._fds[i], fcntl.LOCK_UN)
            self._mine.discard(i)

    def free(self) -> int:
        """How many slots no one else holds right now (a glance: each one taken and given back at once)."""
        self._open()
        if self._handles is not None:
            from ctypes import wintypes
            self._k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            self._k.WaitForSingleObject.restype = wintypes.DWORD
            n = 0
            for h in self._handles:
                if self._k.WaitForSingleObject(h, 0) in (0, 0x80):   # WAIT_OBJECT_0 / WAIT_ABANDONED: free
                    self._k.ReleaseMutex(h)
                    n += 1
            return n
        n = 0
        with self._local:
            import fcntl
            for i, fd in enumerate(self._fds):
                if i not in self._mine and _try_lock(fd):
                    fcntl.flock(fd, fcntl.LOCK_UN)
                    n += 1
        return n


_CORES = Slots('core', cores())
_BUILDS = Slots('build', BUILDS)


def _record(path: str, label: str) -> None:
    with contextlib.suppress(OSError):
        with open(path, 'w', encoding='utf-8') as h:
            json.dump({'label': label, 'cwd': os.getcwd(), 'pid': os.getpid(), 'since': time.time()}, h)


def _read(path: str) -> dict:
    try:
        with open(path, encoding='utf-8') as h:
            who = json.load(h)
    except (OSError, ValueError):
        return {}
    return who if isinstance(who, dict) and isinstance(who.get('pid'), int) and alive(who['pid']) else {}


def _say_who(who: dict) -> str:
    since = time.strftime('%H:%M:%S', time.localtime(who.get('since', time.time())))
    return f"{who.get('label', '?')} in {who.get('cwd', '?')} (pid {who.get('pid', '?')}, since {since})"


def builders() -> list[dict]:
    """Who holds the build slots, as each wrote itself down (a dead one's record: no one)."""
    return [w for i in range(BUILDS) if (w := _read(os.path.join(LOCK_DIR, f'build-{i}.json')))]


@contextlib.contextmanager
def build_slot(label: str) -> Iterator[float]:
    """One of the machine's BUILDS build slots for the block, the seconds waited for it. A build takes every core it
    can: more at once only make each slower; the tests' core slots are not the builds' (a build never waits for
    another session's tests, 2026-11-26 「特别是并发」). Started by a check (HELD): no wait."""
    if _inside():
        yield 0.0
        return
    os.makedirs(LOCK_DIR, exist_ok=True)
    started = time.perf_counter()
    i = _BUILDS.take(0)
    if i is None:
        print(f"waiting for a build slot ({BUILDS} at once): " + '; '.join(_say_who(w) for w in builders()), flush=True)
        with heartbeat.beating('waiting for a build slot', lambda: '; '.join(w.get('label', '?') for w in builders())):
            i = _BUILDS.take()
    path = os.path.join(LOCK_DIR, f'build-{i}.json')
    _record(path, label)
    try:
        yield time.perf_counter() - started
    finally:
        with contextlib.suppress(OSError):
            open(path, 'w').close()   # emptied, not deleted: a reader may have it open (see worktree_once)
        _BUILDS.give(i)


@contextlib.contextmanager
def core() -> Iterator[None]:
    """One of the machine's core slots (cores()) for the block: a test, while it runs. Every check and selftest on
    the machine takes one for each test it runs, so all of them together never run more tests than there are cores
    (2026-11-10: three or four sessions' ctest -j 8 + selftest -j 8 at once made each run 3-7x slower), and the core
    one of them leaves is the next test's, whoever's: two checks share the machine instead of one queueing behind the
    other's whole run. Started by a check (HELD): no wait, its test holds the core already."""
    if _inside():
        yield
        return
    i = _CORES.take()
    try:
        yield
    finally:
        _CORES.give(i)


def cores_free() -> int:
    """The core slots free right now."""
    return _CORES.free()


RUNS = os.path.join(LOCK_DIR, 'runs')


def runs() -> list[dict]:
    """The checks and selftests running tests on the machine now (running()'s records of the live ones)."""
    try:
        names = os.listdir(RUNS)
    except OSError:
        return []
    return [w for n in sorted(names) if n.endswith('.json') and (w := _read(os.path.join(RUNS, n)))]


@contextlib.contextmanager
def running(label: str) -> Iterator[list[dict]]:
    """This process written down as running tests for the block, and HELD naming it: what it starts does not wait for
    the slots again (its test holds one); the other runs on the machine when it starts."""
    others = [w for w in runs() if w.get('pid') != os.getpid()]
    os.makedirs(RUNS, exist_ok=True)
    path = os.path.join(RUNS, f'{os.getpid()}.json')
    _record(path, label)
    saved = os.environ.get(HELD)
    os.environ[HELD] = str(os.getpid())
    try:
        yield others
    finally:
        if saved is None:
            os.environ.pop(HELD, None)
        else:
            os.environ[HELD] = saved
        with contextlib.suppress(OSError):
            os.remove(path)   # a reader holding it open: left behind, read as no one once this process is gone


def say_others(others: list[dict]) -> str:
    return '; '.join(_say_who(w) for w in others)


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
    process is written down: the holder has the lock and has not written itself yet, or has emptied its record and
    is letting go (half a record does not parse: the same), or a holder that was killed left its record."""
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
        # The record is rewritten in place, never renamed over or deleted: on Windows a rename onto, or a delete of,
        # a file another process has open (a second check.py reading who has the lock) fails with PermissionError,
        # which killed the holder at its start or replaced its exit code at its end. Rewriting needs only the write
        # sharing every reader gives (Python's open shares read and write). A reader that finds it empty, half
        # written, or naming a dead process knows the holder has not written itself down yet or is letting go.
        with open(path + '.json', 'w', encoding='utf-8') as h:
            json.dump({'root': os.path.abspath(root), 'pid': os.getpid(), 'since': time.time()}, h)
        try:
            yield
        finally:
            try:
                open(path + '.json', 'w').close()   # emptied while the lock is still held: "letting go"
            except OSError:
                pass   # not emptied: it names this process, which is about to end; read as "letting go" then too
    finally:
        os.close(fd)
