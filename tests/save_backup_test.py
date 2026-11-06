"""The saves' backup before every start (tools/save_backup.py), offline in a temporary folder:
  - keep: every backup of the last day (at most DAY_MOST), then the newest of each day / ISO week / month / year;
    a year of ten starts a day keeps a few dozen, the newest always;
  - backup: a copy with its digest, none when the saves are the newest backup's, a cut-off copy (.partial) removed;
  - the generator runs it as `backup-saves`; the plugin starts that when the game loads it, before anything else
    could refuse the load (src/plugin.cpp, src/loadout_store.cpp).
Exit code 1 when one fails.
"""
from __future__ import annotations

import contextlib
import datetime
import io
import os
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'pylib')]
import save_backup as sb  # noqa: E402
import loadout_generator as lg  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why, file=sys.stderr)
        sys.exit(1)


T0 = datetime.datetime(2026, 1, 1, 9, 0, 0)
name = lambda t: t.strftime(sb.STAMP)  # noqa: E731

# keep: a year of ten starts a day, thinned after each one as backup() does
names: list[str] = []
now = T0
for day in range(400):
    for k in range(10):
        now = T0 + datetime.timedelta(days=day, hours=k)
        names.append(name(now))
        names = sorted(sb.keep(names, now))
kept = [sb._when(n) for n in names]
check(name(now) in names, 'the newest is kept')
last_day = [t for t in kept if now - t <= datetime.timedelta(days=1)]
check(len(last_day) == 11, f'every start of the last 24 hours kept (today 10, yesterday 18:00): {len(last_day)}')
week = {t.date() for t in kept if datetime.timedelta(days=1) < now - t <= datetime.timedelta(days=7)}
check(len(week) >= 6, f'one a day for the week before: {sorted(week)}')
check(sum(1 for t in kept if datetime.timedelta(days=1) < now - t <= datetime.timedelta(days=7)) == len(week),
      'only one of each of those days')
months = [t for t in kept if datetime.timedelta(days=30) < now - t <= datetime.timedelta(days=365)]
check(len({(t.year, t.month) for t in months}) == len(months) and 10 <= len(months) <= 13, f'one a month for the year: {len(months)}')
check(any(now - t > datetime.timedelta(days=365) for t in kept), 'older than a year: one kept for its year')
check(len(names) < 60, f'a year of 4000 starts keeps a few dozen: {len(names)}')
check(len(sb.keep([name(T0 + datetime.timedelta(minutes=m)) for m in range(100)], T0 + datetime.timedelta(hours=2)))
      == sb.DAY_MOST, 'at most DAY_MOST within a day')
check('notes.txt' in sb.keep(['notes.txt', name(T0)], T0), "what is not a backup's name is not removed")

with tempfile.TemporaryDirectory(prefix='edf6vc-savebackup-') as root:
    saves = os.path.join(root, sb.SAVES, '76561198000000000', 'saveslot00')
    os.makedirs(saves)
    with open(os.path.join(saves, 'MAIN.GST'), 'wb') as f:
        f.write(b'save 1')
    home = os.path.join(root, sb.BACKUPS)
    check(sb.backup(os.path.join(root, 'none'), T0) is None, 'no saves: nothing')
    made = sb.backup(root, T0)
    check(made and open(os.path.join(made, '76561198000000000', 'saveslot00', 'MAIN.GST'), 'rb').read() == b'save 1',
          'a backup of the saves')
    check(sb.digest(made) == sb.digest(os.path.join(root, sb.SAVES)), 'the backup holds what the saves hold')
    check(sb.backup(root, T0 + datetime.timedelta(minutes=5)) is None and len(os.listdir(home)) == 1,
          'saves unchanged since: no new backup')
    with open(os.path.join(saves, 'MAIN.GST'), 'wb') as f:
        f.write(b'save 2')
    os.makedirs(os.path.join(home, name(T0) + '-9' + sb.PARTIAL))
    second = sb.backup(root, T0 + datetime.timedelta(minutes=10))
    check(second and len(os.listdir(home)) == 2, f'changed: a second backup, the cut-off copy gone: {os.listdir(home)}')
    with open(os.path.join(saves, 'MAIN.GST'), 'wb') as f:
        f.write(b'save 3')
    third = sb.backup(root, T0 + datetime.timedelta(minutes=10))
    check(third and third != second and os.path.isdir(second), 'the same second: a distinct name')

    os.environ['LOCALAPPDATA'] = root
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        check(lg.run(['backup-saves']) == 0, 'the generator runs backup-saves')
    check(out.getvalue().startswith('EDF6VC_SAVE_BACKUP'), f'one status line: {out.getvalue()!r}')

plugin = open(os.path.join(ROOT, 'src', 'plugin.cpp'), encoding='utf-8').read()
store = open(os.path.join(ROOT, 'src', 'loadout_store.cpp'), encoding='utf-8').read()
check(0 <= plugin.index('StartSaveBackup();') < plugin.index('edf::IdentifyImage('), 'the plugin starts it before its checks')
check('backup-saves' in store and 'CREATE_NO_WINDOW' in store, 'hidden, as backup-saves')
print(f'save_backup: {checks} checks OK')
