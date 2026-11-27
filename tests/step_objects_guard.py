"""The facts that hold for a frame are read once a period (common/edf/step_objects.h stepobj::Once): the session
(src/netprobe.cpp InSession), the pause (src/crew.cpp GamePaused) and the host (src/online_authority.cpp Host). Their
Readable / native reads sit in their refresh functions only (ReadSession, ReadPaused, ReadHost), never in the cached
entry points the hot paths call. And the System's release / teardown hooks keep stepobj's period order (Releasing
before the stock call, Released after; a teardown closes). Source only, no game."""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import srcscan  # noqa: E402  (the one C++ reader: comments, literals, bodies, calls)

checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


# (file, the cached entry point, its refresh function)
CACHED = (
    ('src/netprobe.cpp', 'InSession', 'ReadSession'),
    ('src/crew.cpp', 'GamePaused', 'ReadPaused'),
    ('src/online_authority.cpp', 'Host', 'ReadHost'),
)
for rel, entry, refresh in CACHED:
    text = srcscan.read(rel, ROOT)
    body = srcscan.function(text, entry)
    check(body, f'{rel}: {entry} is defined')
    check(srcscan.calls(body, 'Once') and srcscan.mentions(body, refresh),
          f'{rel}: {entry} is stepobj::Once over {refresh} (read once a period)')
    check(not srcscan.calls(body, 'Readable'), f'{rel}: {entry} asks no Readable itself')
    check(srcscan.function(text, refresh), f'{rel}: {refresh} is defined')
    readers = srcscan.callers(text, 'Readable')
    for name in ('InSession', 'GamePaused', 'Host'):
        if name == entry:
            check(name not in readers, f'{rel}: only {refresh} asks Readable for {entry}')

# The hooks: Releasing, the stock call, Released (a teardown then Close); and the scene's end closes too.
hook = srcscan.read('src/step_objects_hook.cpp', ROOT)
for name, closes in (('FrameRelease', False), ('FlushRelease', False), ('Teardown', True)):
    body = srcscan.function(hook, name)
    check(body and srcscan.order(body, 'Releasing', 'Released'), f'step_objects_hook.cpp {name}: Releasing then Released')
    check(srcscan.calls(body, 'Close') == closes, f'step_objects_hook.cpp {name}: {"closes" if closes else "does not close"}')
check(srcscan.calls(srcscan.function(hook, 'FrameRelease'), 'Released') and 'Released(true)' in srcscan.code(hook),
      'the frame\'s release opens the period (Released(true))')
mission = srcscan.read('src/mission.cpp', ROOT)
check(srcscan.calls(srcscan.function(mission, 'ReleaseGroupHook'), 'Close'),
      'mission.cpp ReleaseGroupHook: the scene\'s end closes stepobj')
table = srcscan.read('src/module_table.h', ROOT)
check('&Install<&InstallStepObjects>' in srcscan.code(table), 'module_table.h installs the step objects at load')

print(f'step_objects_guard: {checks} checks passed')
