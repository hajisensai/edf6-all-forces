"""The real-game test queue (pylib/gamelease.py): one session drives the game at a time, the others wait in line.

  python tools/gamequeue.py status                       who holds the game, who is waiting (in order)
  python tools/gamequeue.py run [OPTIONS] -- CMD ...     wait for the game, run CMD holding it, give it back
  python tools/gamequeue.py hold [OPTIONS]               wait for the game and keep it (several commands in a row):
        prints `EDF6_GAME_LEASE=<token>`; run each command with that variable set, they then use this lease
        without waiting; ends at `release`, after --minutes (default 60), or when this process is killed
  python tools/gamequeue.py release [TOKEN]              end a `hold` (default: the token in $EDF6_GAME_LEASE)

  OPTIONS: --game DIR (the game folder queued for; default: the one pylib/gamedir.py finds)
           --label TEXT (shown to the others; default: the command line)
           --wait-max-min N (give up after N minutes in line: exit code 75, nothing run; default: wait)
           --minutes N (hold only: give the game back after N minutes)

tests/autopilot/drive.py, testrange/run_test.py and tools/installer.py (run from source) take the lease themselves;
use this for anything else that touches the game folder or the running game.
"""
from __future__ import annotations

import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'pylib'))
import gamedir  # noqa: E402
import gamelease  # noqa: E402


def _age(info: dict) -> str:
    return f"{(time.time() - info.get('since', time.time())) / 60:.1f} min"


def status(game: str) -> int:
    print('game:', game)
    h = gamelease.holder(game)
    if h:
        print(f"held by: {h.get('label')}  (pid {h.get('pid')}, {_age(h)}, {h.get('branch') or '-'}, {h.get('cwd')})")
    else:
        print('the game is free')
    for i, w in enumerate(gamelease.waiters(game), 1):
        print(f"  {i}. {w.get('label')}  (pid {w.get('pid')}, waiting {_age(w)}, {w.get('branch') or '-'}, {w.get('cwd')})")
    return 0


def _flag(game: str, token: str) -> str:
    return os.path.join(gamelease.queue_dir(game), 'release-' + token)


def hold(game: str, label: str | None, wait_max: float | None, minutes: float) -> int:
    with gamelease.lease(game, label or 'hold', wait_max=wait_max) as info:
        flag = _flag(game, info['token'])
        print(f"{gamelease.ENV}={info['token']}", flush=True)
        end = time.time() + minutes * 60
        while time.time() < end and not os.path.exists(flag):
            time.sleep(gamelease.POLL)
        if os.path.exists(flag):
            os.remove(flag)
        else:
            print(f'[game queue] {minutes:g} min passed: giving the game back')
    return 0


def release(game: str, token: str | None) -> int:
    token = token or os.environ.get(gamelease.ENV)
    h = gamelease.holder(game)
    if not token or not h or h.get('token') != token:
        print('no live hold with that token')
        return 1
    open(_flag(game, token), 'w').close()
    for _ in range(int(10 / gamelease.POLL)):
        if (gamelease.holder(game) or {}).get('token') != token:
            print('released')
            return 0
        time.sleep(gamelease.POLL)
    print('the holder did not let go in 10 s')
    return 1


def parse(argv: list[str]) -> tuple[str, dict, list[str]]:
    cmd = argv[0] if argv else ''
    opts: dict = {'game': None, 'label': None, 'wait_max': None, 'minutes': 60.0, 'token': None}
    rest: list[str] = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--':
            rest = argv[i + 1:]
            break
        if a in ('--game', '--label', '--wait-max-min', '--minutes') and i + 1 < len(argv):
            v = argv[i + 1]
            if a == '--game':
                opts['game'] = v
            elif a == '--label':
                opts['label'] = v
            elif a == '--wait-max-min':
                opts['wait_max'] = float(v) * 60
            else:
                opts['minutes'] = float(v)
            i += 2
        elif cmd == 'release' and not opts['token']:
            opts['token'] = a
            i += 1
        else:
            raise SystemExit(f'unknown argument {a!r}\n\n{__doc__}')
    return cmd, opts, rest


def main(argv: list[str]) -> int:
    cmd, opts, rest = parse(argv)
    game = opts['game'] or gamedir.find_or_dev()
    try:
        if cmd == 'status':
            return status(game)
        if cmd == 'release':
            return release(game, opts['token'])
        if cmd == 'hold':
            return hold(game, opts['label'], opts['wait_max'], opts['minutes'])
        if cmd == 'run' and rest:
            with gamelease.lease(game, opts['label'] or ' '.join(rest), wait_max=opts['wait_max']):
                return subprocess.call(rest)
    except gamelease.WaitExpired as e:
        print(f'[game queue] {e}: nothing run')
        return gamelease.WAIT_EXPIRED
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
