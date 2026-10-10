"""The debug spawn tool is off unless the player turns it on (the user, 2026-10-09: 默认关闭这个工具): the shipped ini says
DebugSpawn=0 with its keys, the plugin's default is off and it reads the key, an old player's ini the installer upgrades
gets DebugSpawn=0 appended (never 1), and the installer's default flips (NEW_DEFAULTS) never touch it. The keys it ships
are none of the ones another setting takes. Pure text; no game files."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, p) for p in ('tools', 'pylib', 'testrange')]
import installer  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8-sig') as f:
        return f.read()


def values(text: str) -> dict[str, str]:
    """[VehicleCrew] key (as written) -> value, comments stripped."""
    out: dict[str, str] = {}
    for line in text.splitlines():
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*=([^;]*)', line)
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


KEYS = ('DebugSpawn', 'DebugSpawnKey', 'DebugSpawnPrevKey', 'DebugSpawnNextKey', 'DebugSpawnCategoryKey',
        'DebugSpawnSpawnKey', 'DebugSpawnRange', 'DebugSpawnDistance')


def main() -> int:
    shipped = read('EDF6VehicleCrew.ini')
    ship = values(shipped)
    for key in KEYS:
        check(key in ship, f'the shipped ini has {key}')
    check(ship['DebugSpawn'] == '0', 'the shipped ini ships the tool off')

    # The plugin's own default (crew.h Config) is off, and LoadConfig reads the key with it as the fallback.
    crew_h = read('src/crew.h')
    check(re.search(r'\bbool debugSpawn=false;', crew_h) is not None, 'Config::debugSpawn defaults to false')
    plugin = read('src/plugin.cpp')
    check('n.debugSpawn=ReadBool(L"DebugSpawn",n.debugSpawn);' in plugin, 'LoadConfig reads DebugSpawn over the default')
    # Each key's default in crew.h is the one the ini ships.
    for field, key in (('debugSpawnKey', 'DebugSpawnKey'), ('debugSpawnPrevKey', 'DebugSpawnPrevKey'),
                       ('debugSpawnNextKey', 'DebugSpawnNextKey'), ('debugSpawnCategoryKey', 'DebugSpawnCategoryKey'),
                       ('debugSpawnSpawnKey', 'DebugSpawnSpawnKey')):
        m = re.search(rf'\bint {field}=(0x[0-9A-Fa-f]+);', crew_h)
        check(m is not None and int(m.group(1), 16) == int(ship[key], 0), f'{key}: crew.h and the ini agree')
        check(f'ReadInt(L"{key}"' in plugin, f'LoadConfig reads {key}')
    for field, key in (('debugSpawnRange', 'DebugSpawnRange'), ('debugSpawnDistance', 'DebugSpawnDistance')):
        m = re.search(rf'\bfloat {field}=([0-9.]+)f;', crew_h)
        check(m is not None and float(m.group(1)) == float(ship[key]), f'{key}: crew.h and the ini agree')

    # The tool's keys clash with no other key the ini sets (a *Key setting, not a pad button).
    others = {int(v, 0) for k, v in ship.items() if k.endswith('Key') and not k.startswith('DebugSpawn') and v}
    mine = [int(ship[k], 0) for k in KEYS if k.endswith('Key')]
    check(len(set(mine)) == len(mine), 'the tool\'s five keys are distinct')
    check(not (set(mine) & others), f'the tool\'s keys are no other setting\'s (taken: {sorted(hex(x) for x in others)})')

    # An old player's ini (before this version: no DebugSpawn at all) as the installer upgrades it: the tool off.
    old = '\n'.join(line for line in shipped.splitlines() if not re.match(r'^\s*DebugSpawn', line)) + '\n'
    check('DebugSpawn' not in values(old), 'the fixture is an ini without the tool\'s keys')
    text, added, gone, flipped = installer.planned_ini(old, shipped)
    up = values(text)
    check(all(k in added for k in KEYS), 'the upgrade appends every key of the tool')
    check(up.get('DebugSpawn') == '0', 'after the upgrade the tool is off')
    check('DebugSpawn' not in flipped, 'the installer\'s default flips leave it alone')
    check(all('DebugSpawn' not in k for k in installer.NEW_DEFAULTS.get(installer.SECTION, {})),
          'DebugSpawn is not a NEW_DEFAULTS key (no install ever turns it on)')
    # A player who turned it on keeps it on through an upgrade; one who turned it off keeps it off.
    for value in ('1', '0'):
        mine_ini = re.sub(r'(?m)^DebugSpawn=0', f'DebugSpawn={value}', shipped)
        check(values(installer.planned_ini(mine_ini, shipped)[0])['DebugSpawn'] == value, f'the player\'s DebugSpawn={value} kept')
    print(f'debug_spawn_ini_test: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
