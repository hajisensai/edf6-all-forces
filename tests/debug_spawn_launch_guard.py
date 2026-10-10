"""Source guard: the debug spawn tool never launches a crewed aircraft without its crew. Since the real crews (#88) an
aircraft JetLaunch / HeliLaunch makes in the air has no pilot (HeliFrame turns it away at "only NPC pilots") and falls
(2026-10-09 log); #103 makes Launch refuse crewed bodies and HeliLaunch is going away. So in src/debug_spawn.cpp:
  - no JetLaunch / HeliLaunch / HeliCalled / SpawnJet / PrepareSupportAircraft call at all;
  - the crewed aircraft rows are support calls (SupportCallAt: the map's air support, real crew seated aboard), each key
    one of the calls tools/calls.py ships (src/calls.inc), and none of them a drone-only or submarine call;
  - the one launched aircraft is the gun drone (JetLaunchDrone), which flies with no one aboard;
  - the online refusal comes before any spawn path.
python tests/debug_spawn_launch_guard.py [--root DIR]; exit 1 on a failure."""
from __future__ import annotations

import argparse
import os
import re
import sys


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root
    read = lambda rel: open(os.path.join(root, rel), encoding='utf-8').read()  # noqa: E731
    cpp, header, calls = code_only(read('src/debug_spawn.cpp')), code_only(read('src/debug_spawn.h')), read('src/calls.inc')
    failures: list[str] = []
    for banned in ('JetLaunch', 'HeliLaunch', 'HeliCalled', 'SpawnJet', 'PrepareSupportAircraft', 'JetLaunchThrown'):
        if re.search(rf'\b{banned}\s*\(', cpp):
            failures.append(f'debug_spawn.cpp calls {banned}: a crewed aircraft without its crew falls')
    if not re.search(r'\bSupportCallAt\s*\(', cpp):
        failures.append('debug_spawn.cpp: the crewed aircraft do not go through SupportCallAt')
    if len(re.findall(r'\bJetLaunchDrone\s*\(', cpp)) != 1:
        failures.append('debug_spawn.cpp: exactly one JetLaunchDrone (the gun drone) expected')
    spawn = cpp.split('void Spawn(int row,unsigned char* human)', 1)[-1]
    if 'InSession()' not in spawn or spawn.index('InSession()') > min(
            spawn.index(x) for x in ('SupportCallAt(', 'JetLaunchDrone(', 'SpawnEnemy(', 'SpawnVehicle(') if x in spawn):
        failures.append('debug_spawn.cpp Spawn: the online refusal must come before every spawn path')
    shipped = set(re.findall(r'L"EDF6VC_CALL_(\w+)"\}', calls))
    brings = {key: what for what, key in re.findall(r'Brings::(\w+),[^\n]*L"EDF6VC_CALL_(\w+)"\}', calls)}
    table = header.split('kSupportCalls[]={', 1)
    keys = re.findall(r'L"(\w+)"', table[1].split('};', 1)[0]) if len(table) == 2 else []
    if not keys:
        failures.append('debug_spawn.h: no kSupportCalls')
    for key in keys:
        if key not in shipped:
            failures.append(f'kSupportCalls {key}: no such call in src/calls.inc')
        elif brings.get(key) not in ('jets', 'helis'):
            failures.append(f'kSupportCalls {key}: brings {brings.get(key)}, not a crewed aircraft')
    rows = re.findall(r'\{Category::aircraft,How::(\w+),"(\w+)"', header)
    if not rows or any(how not in ('supportCall', 'drone') for how, _ in rows):
        failures.append(f'an aircraft row is neither a support call nor the drone: {rows}')
    if [ident for how, ident in rows if how == 'drone'] != ['jet_drone']:
        failures.append('the drone row is the one launched aircraft')
    for f in failures:
        print('FAIL', f)
    print(f'debug_spawn_launch_guard: {len(keys)} support calls, {len(rows)} aircraft rows, '
          f'{"FAILED" if failures else "passed"}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
