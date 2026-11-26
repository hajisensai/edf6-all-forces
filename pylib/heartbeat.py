"""A line every few minutes while a long offline step runs: tools/check.py, tools/selftest.py, and the waits for the
machine's check slot (pylib/checklock.py) and for the game (pylib/gamelease.py).

Why (the maintainer, 2026-11-21: 「优化下测试的时候会把缓存清空的问题，不要出现这种问题」, the cache being a Claude Code
subagent's): a subagent's prompt cache lives 5 minutes from its last request. A subagent waiting on a 10-30 minute check
(or in line behind another session's) sends none, so the turn after the run re-reads its whole context uncached. With
the run's output watched by the Monitor tool (CLAUDE.md 验证), each of these lines is an event that wakes it: one small
request that reads the cache at a tenth of the price and starts its 5 minutes again, instead of writing the whole
context anew after the run. The maintainer chose this over the 1-hour cache ("不要改1小时，这样反而用的额度更多了":
its writes cost more, on every subagent request).

EDF6_HEARTBEAT_S: the interval in seconds (default 240: under the 5 minutes, with room for the wake itself); 0: none.
Every line starts with PREFIX, so a watcher's filter can take them (grep --line-buffered '\\[heartbeat\\]').
"""
from __future__ import annotations

import contextlib
import os
import threading
import time
from typing import Callable, Iterator

ENV = 'EDF6_HEARTBEAT_S'
DEFAULT = 240.0
CACHE_TTL = 300.0   # a subagent's prompt cache (Claude Code's default for everything but the main conversation)
PREFIX = '[heartbeat]'


def interval() -> float:
    """The seconds between two lines (ENV, else DEFAULT); 0: no lines."""
    try:
        every = float(os.environ.get(ENV, DEFAULT))
    except ValueError:
        every = DEFAULT
    return every if every > 0 else 0.0


def _say(line: str) -> None:
    print(line, flush=True)


@contextlib.contextmanager
def beating(label: str, status: Callable[[], str] | None = None,
            say: Callable[[str], None] = _say) -> Iterator[None]:
    """While the block runs, `say` '[heartbeat] <label>: <seconds> s[, <status()>]' every interval(); nothing in a block
    shorter than that. `status`: what is done so far (a failure in it is said, never raised into the block)."""
    every = interval()
    if not every:
        yield
        return
    stop = threading.Event()
    started = time.monotonic()

    def run() -> None:
        while not stop.wait(every):
            extra = ''
            if status is not None:
                try:
                    extra = status()
                except Exception as e:  # noqa: BLE001 - the line still goes out: it is what keeps the watcher's cache
                    extra = f'(status failed: {e})'
            with contextlib.suppress(Exception):
                say(f'{PREFIX} {label}: {time.monotonic() - started:.0f} s' + (f', {extra}' if extra else ''))

    beat = threading.Thread(target=run, name='heartbeat', daemon=True)
    beat.start()
    try:
        yield
    finally:
        stop.set()
        beat.join()
