"""The game's saves backed up before every start: the plugin runs the slim generator hidden as `backup-saves` when the
game loads it (src/loadout_store.cpp StartSaveBackup: a mod start), and 原版启动 calls backup() before a vanilla start
(tools/vanilla_launch.py). Local only, never synced: %LOCALAPPDATA%\\EarthDefenceForce6\\SAVE_DATA.backups\\<time>.

A start whose saves are byte for byte the newest backup's makes none (DIGEST). What is kept thins out with age (keep):
every backup of the last day (the newest DAY_MOST), then the newest of each day for a week, of each ISO week for a
month, of each month for a year, of each year after. A backup is copied into <name>.partial and renamed when whole, so
a start cut off mid-copy never leaves something that looks like a backup.

Stdlib and pylib/modfiles only: frozen into the slim generator (tests/slim_generator_test.py).
"""
from __future__ import annotations

import datetime
import hashlib
import os
import shutil

SAVES = 'SAVE_DATA'
BACKUPS = SAVES + '.backups'
DIGEST = '.digest'           # in each backup: the SHA-256 of what it holds (paths and bytes)
PARTIAL = '.partial'
STAMP = '%Y%m%d-%H%M%S'
DAY_MOST = 30


def save_root() -> str:
    return os.path.join(os.environ.get('LOCALAPPDATA') or os.path.expanduser('~'), 'EarthDefenceForce6')


def digest(path: str) -> str:
    h = hashlib.sha256()
    for d, dirs, files in os.walk(path):
        dirs.sort()
        for n in sorted(files):
            full = os.path.join(d, n)
            rel = os.path.relpath(full, path).replace(os.sep, '/')
            if rel == DIGEST:
                continue
            h.update(rel.encode('utf-8') + b'\0')
            with open(full, 'rb') as f:
                h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()


def _when(name: str) -> datetime.datetime | None:
    try:
        return datetime.datetime.strptime(name[:15], STAMP)
    except ValueError:
        return None


def keep(names: list[str], now: datetime.datetime) -> set[str]:
    """Which of the backups `names` to keep at `now` (names that are no backup's are kept: not ours to remove)."""
    dated = sorted(((t, n) for n in names if (t := _when(n))), reverse=True)   # newest first
    kept = {n for n in names if not _when(n)}
    seen: set[tuple] = set()
    day = 0
    for t, n in dated:
        age = now - t
        if age <= datetime.timedelta(days=1):
            day += 1
            if day <= DAY_MOST:
                kept.add(n)
            continue
        if age <= datetime.timedelta(days=7):
            key: tuple = ('day', t.date())
        elif age <= datetime.timedelta(days=30):
            key = ('week', *t.isocalendar()[:2])
        elif age <= datetime.timedelta(days=365):
            key = ('month', t.year, t.month)
        else:
            key = ('year', t.year)
        if key not in seen:   # the newest of its period
            seen.add(key)
            kept.add(n)
    return kept


def backup(root: str | None = None, now: datetime.datetime | None = None) -> str | None:
    """SAVE_DATA copied into BACKUPS/<time> and the old ones thinned (keep). Returns the new backup's path; None when
    there are no saves or they are the newest backup's."""
    root = root or save_root()
    now = now or datetime.datetime.now()
    src, home = os.path.join(root, SAVES), os.path.join(root, BACKUPS)
    if not os.path.isdir(src):
        return None
    os.makedirs(home, exist_ok=True)
    for n in os.listdir(home):   # a copy cut off before
        if n.endswith(PARTIAL):
            shutil.rmtree(os.path.join(home, n), ignore_errors=True)
    names = sorted((n for n in os.listdir(home) if _when(n)), key=lambda n: (_when(n), n))
    have = digest(src)
    if names:
        try:
            with open(os.path.join(home, names[-1], DIGEST), encoding='ascii') as f:
                if f.read().strip() == have:
                    return None
        except OSError:
            pass
    stamp = now.strftime(STAMP)
    name, i = stamp, 1
    while os.path.exists(os.path.join(home, name)):
        name, i = f'{stamp}-{i}', i + 1
    tmp = os.path.join(home, name + PARTIAL)
    shutil.copytree(src, tmp)
    with open(os.path.join(tmp, DIGEST), 'w', encoding='ascii') as f:
        f.write(have)
    os.rename(tmp, os.path.join(home, name))
    names = [n for n in os.listdir(home) if not n.endswith(PARTIAL)]
    for n in set(names) - keep(names, now):
        shutil.rmtree(os.path.join(home, n), ignore_errors=True)
    return os.path.join(home, name)


def main() -> int:
    """`backup-saves`: the plugin's hidden run; one line, no prompt."""
    try:
        said = backup()
    except OSError as e:
        print(f'EDF6VC_SAVE_BACKUP failed {e}')
        return 1
    print(f'EDF6VC_SAVE_BACKUP {"made " + said if said else "unchanged"}')
    return 0
