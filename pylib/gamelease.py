"""One real-game test at a time per game folder: a lease on the EDF6 install, queued first come, first served.

Several sessions (people, agents) share one game folder and one EDF6.exe: one installing its DLL over another's, or
launching the game in the middle of another's run, spoils both measurements. Whatever writes into the game folder or
drives the game holds this lease for its whole run (tests/autopilot/drive.py, testrange/run_test.py, the installer run
from source, tools/gamequeue.py for anything else).

The lease is an OS file lock, so a holder that dies (crash, kill, closed terminal) releases it at once: nothing is ever
left stale. Waiters line up by ticket: a file in queue/ each waiter keeps locked; only the first live ticket may take
the slot, and a ticket whose waiter died no longer counts. A lease can be taken again inside its holder (a child
process, or a later command of the same session) by the token in $EDF6_GAME_LEASE: no second wait, no deadlock.
Each game folder has its own queue: a test on a scratch copy of the game never waits behind a real run.

On Windows the lock sits on one byte far past the files' content, so the content (who holds / who waits) stays
readable while locked.
"""
from __future__ import annotations

import contextlib
import hashlib
import json
import os
import subprocess
import sys
import time
import uuid
from typing import IO, Callable, Iterator

ENV = 'EDF6_GAME_LEASE'       # the token of the lease this process runs under
DIR_ENV = 'EDF6_QUEUE_DIR'    # where the queues live (tests point it elsewhere)
LOCK_AT = 0x7FFF0000          # the locked byte: past any content, so the content stays readable
POLL = 0.5                    # seconds between looks at the queue
WAIT_EXPIRED = 75             # exit code when a wait limit runs out (EX_TEMPFAIL)


class WaitExpired(RuntimeError):
    pass


def queue_dir(game: str) -> str:
    """The queue of one game folder (the folder and the EDF6.exe run from it are what is contended)."""
    base = os.environ.get(DIR_ENV) or os.path.join(
        os.environ.get('LOCALAPPDATA') or os.path.expanduser('~'), 'EDF6TestQueue')
    key = os.path.normcase(os.path.abspath(game))
    path = os.path.join(base, hashlib.sha1(key.encode('utf-8')).hexdigest()[:12])
    os.makedirs(os.path.join(path, 'queue'), exist_ok=True)
    return path


def _slot_path(game: str) -> str:
    return os.path.join(queue_dir(game), 'slot')


def _try_lock(f: IO[bytes]) -> bool:
    try:
        if os.name == 'nt':
            import msvcrt
            f.seek(LOCK_AT)
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:   # flock, not lockf: a probe from the holder's own process must see the lock, not drop it
            import fcntl
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def _is_held(path: str) -> bool:
    """Someone holds the lock on `path` (probes it: a free lock is taken and dropped again with the file)."""
    try:
        f = open(path, 'a+b')
    except FileNotFoundError:
        return False
    except PermissionError:   # being created or removed by its owner right now
        return True
    with f:
        return not _try_lock(f)


def _read(path: str) -> dict:
    try:
        with open(path, 'rb') as f:
            return json.loads(f.read().decode('utf-8') or '{}')
    except (OSError, ValueError):
        return {}


def _write(f: IO[bytes], info: dict) -> None:
    f.seek(0)
    f.truncate()
    f.write(json.dumps(info, ensure_ascii=False).encode('utf-8'))
    f.flush()


def describe(label: str | None = None) -> dict:
    """Who is asking: the label (default: the command line), process, folder and git branch."""
    cwd = os.getcwd()
    try:
        branch = subprocess.run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], cwd=cwd, capture_output=True,
                                text=True, timeout=5).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        branch = ''
    return {'label': label or ' '.join([os.path.basename(sys.argv[0])] + sys.argv[1:]), 'pid': os.getpid(),
            'cwd': cwd, 'branch': branch, 'since': time.time()}


def holder(game: str) -> dict | None:
    """The live holder's description, or None when the game is free."""
    path = _slot_path(game)
    return _read(path) if _is_held(path) else None


def waiters(game: str) -> list[dict]:
    """The live waiters in queue order; tickets left by dead waiters are removed."""
    out: list[dict] = []
    directory = os.path.join(queue_dir(game), 'queue')
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if _is_held(path):
            out.append(dict(_read(path), ticket=name))
        else:
            with contextlib.suppress(OSError):
                os.remove(path)
    return out


def held_here(game: str) -> bool:
    """This process runs under the live lease (its own, or the one its token in $EDF6_GAME_LEASE names)."""
    token = os.environ.get(ENV)
    h = holder(game) if token else None
    return bool(h and h.get('token') == token)


def _wait_turn(game: str, info: dict, wait_max: float | None, say: Callable[[str], None]) -> IO[bytes]:
    """Line up, wait to be first, take the slot; returns the slot file, locked (the lock lives as long as it)."""
    directory = os.path.join(queue_dir(game), 'queue')
    name = f'{time.time_ns():020d}-{os.getpid()}-{uuid.uuid4().hex[:6]}'
    ticket_path = os.path.join(directory, name)
    ticket = open(ticket_path, 'x+b')
    try:
        if not _try_lock(ticket):
            raise RuntimeError('could not lock our own queue ticket ' + ticket_path)
        _write(ticket, info)
        start = time.time()
        shown = None
        while True:
            line = [w['ticket'] for w in waiters(game)]
            if line and line[0] == name:
                slot = open(_slot_path(game), 'a+b')
                if _try_lock(slot):
                    return slot
                slot.close()
            if wait_max is not None and time.time() - start > wait_max:
                raise WaitExpired(f'still waiting for the game after {wait_max / 60:.1f} min')
            h = holder(game)
            now = (line.index(name) if name in line else -1, (h or {}).get('token'))
            if now != shown:
                shown = now
                who = f"{h.get('label')} (pid {h.get('pid')}, {h.get('branch') or h.get('cwd')})" if h else 'nobody'
                say(f'[game queue] waiting: {now[0]} ahead of us; the game is held by {who}')
            time.sleep(POLL)
    finally:
        ticket.close()
        with contextlib.suppress(OSError):
            os.remove(ticket_path)


@contextlib.contextmanager
def lease(game: str, label: str | None = None, *, wait_max: float | None = None,
          say: Callable[[str], None] = print) -> Iterator[dict]:
    """Hold the game folder `game` for the `with` block, waiting in line first; already held here (see held_here):
    no wait. `wait_max` seconds: WaitExpired instead of waiting longer (default: wait as long as it takes)."""
    if held_here(game):
        yield holder(game) or {}
        return
    info = dict(describe(label), game=os.path.abspath(game), token=uuid.uuid4().hex)
    slot = _wait_turn(game, info, wait_max, say)
    old = os.environ.get(ENV)
    try:
        _write(slot, info)
        os.environ[ENV] = info['token']   # children (and nested leases) run under it
        say(f"[game queue] holding the game: {info['label']}")
        yield info
    finally:
        if old is None:
            os.environ.pop(ENV, None)
        else:
            os.environ[ENV] = old
        with contextlib.suppress(OSError):
            _write(slot, {})
        slot.close()
        say('[game queue] released the game')
