"""Source guard of the plugin's launched aircraft and their crews: python tests/unmanned_launch_guard.py [--root DIR],
exit 1 on a failure. Since the real crews (2026-10-08, docs/real-npc-crew.md) no soldier is made inside a vehicle and none
can walk aboard one made in the air; the plugin's launches waited for a pilot that never came, and fell (2026-10-09 log:
six doll drones driver=0, seats=[-], under the ground and on down from 40 km). What it holds in place:
  - jet_internal.h Unmanned is the drones' roles (drone, blast, doll) and nothing crewed; FliesItself = launched && Unmanned;
  - jet_spawn.cpp Launch refuses a crewed body before it makes anything (crewed aircraft come through the support
    deployment, made with their real crew inside: support_dispatch.cpp BoardAirborne);
  - jet_spawn.cpp SpawnJet asks soldiers aboard (SeatNpcRider) only for a crewed body; a drone gets the native init alone;
  - heli.cpp HeliFrame flies a jet that JetFliesItself before its "only NPC pilots" gate, behind JetPilot and OnlineRunsHere;
  - crew.cpp Crew never recruits for a drone (JetFliesItself before SeatNpcRider), nor playerjet_board.inc HandBack;
  - the paths that need "it flies itself here" (CrewedPilot, ResumeNpc, CommandVehicleLive) take a drone as flown.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def read(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return code_only(f.read())


def body(text: str, signature: str) -> str:
    """The braces-balanced body of the first definition whose header matches `signature` (a regex), '' when none."""
    m = re.search(signature + r'[^;{]*\{', text)
    if not m:
        return ''
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def before(text: str, first: str, second: str, where: str) -> None:
    a, b = re.search(first, text), re.search(second, text)
    if not a:
        fail(f'{where}: no {first}')
    elif not b:
        fail(f'{where}: no {second}')
    elif a.start() > b.start():
        fail(f'{where}: {first} comes after {second}')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root

    internal = read(root, 'src/jet_internal.h')
    m = re.search(r'constexpr\s+bool\s+Unmanned\s*\(\s*Role\s+r\s*\)\s*noexcept\s*\{\s*return([^;]*);', internal)
    roles = set(re.findall(r'Role::(\w+)', m.group(1))) if m else set()
    if roles != {'drone', 'blast', 'doll'}:
        fail(f'jet_internal.h Unmanned: roles {sorted(roles)}, want blast, doll, drone')
    if not re.search(r'FliesItself\s*\(\s*const\s+Jet&\s*j\s*\)\s*noexcept\s*\{\s*return\s+j\.launched\s*&&\s*Unmanned\s*\(\s*j\.role\s*\)', internal):
        fail('jet_internal.h FliesItself: not launched && Unmanned(role)')

    spawn = read(root, 'src/jet_spawn.cpp')
    launch = body(spawn, r'Jet\*\s+Launch\s*\(\s*Body\s+b')
    before(launch, r'if\s*\(\s*!\s*Unmanned\s*\(\s*Row\s*\(\s*b\s*\)\s*\.role\s*\)\s*\)\s*\{[^}]*return\s+nullptr', r'\bSpawnJet\s*\(',
           'jet_spawn.cpp Launch (a crewed body refused before it is made)')
    make = body(spawn, r'unsigned\s+char\*\s+SpawnJet\s*\(')
    if not re.search(r'if\s*\(\s*!\s*Unmanned\s*\(\s*row\.role\s*\)\s*\)\s*SeatNpcRider\s*\(\s*v\s*,\s*true\s*\)', make):
        fail('jet_spawn.cpp SpawnJet: SeatNpcRider not under !Unmanned(row.role)')
    if len(re.findall(r'\bSeatNpcRider\s*\(', make)) != 1:
        fail('jet_spawn.cpp SpawnJet: SeatNpcRider called other than once (under !Unmanned)')
    if not re.search(r'\bPrepareNpcVehicle\s*\(\s*v\s*,\s*true\s*\)', make):
        fail('jet_spawn.cpp SpawnJet: a drone loses its native init (PrepareNpcVehicle(v,true))')

    heli = read(root, 'src/heli.cpp')
    frame = body(heli, r'void\s+HeliFrame\s*\(\s*unsigned\s+char\*')
    gate = re.search(r'if\s*\(\s*IsJet\s*\(\s*vehicle\s*\)\s*&&\s*JetFliesItself\s*\(\s*vehicle\s*\)\s*\)\s*\{', frame)
    if not gate:
        fail('heli.cpp HeliFrame: no IsJet && JetFliesItself branch')
    else:
        before(frame, re.escape(gate.group(0)), r'if\s*\(\s*!\s*NpcDriver\s*\(\s*vehicle\s*\)\s*\)', 'heli.cpp HeliFrame')
        branch = body(frame[gate.start():], re.escape('if'))
        if not re.search(r'Cfg\(\)\.jetPilot\s*&&\s*OnlineRunsHere\s*\(\s*vehicle\s*\)\s*\)\s*JetFrame\s*\(\s*vehicle\s*\)', branch):
            fail('heli.cpp HeliFrame: the drone branch does not fly it behind JetPilot and OnlineRunsHere')

    crew = read(root, 'src/crew.cpp')
    crew_fn = body(crew, r'void\s+Crew\s*\(\s*unsigned\s+char\*\s*vehicle\s*,\s*int\s+cls\s*\)')
    before(crew_fn, r'\bJetFliesItself\s*\(\s*vehicle\s*\)', r'\bSeatNpcRider\s*\(', 'crew.cpp Crew (no crew recruited for a drone)')

    board = read(root, 'src/playerjet_board.inc')
    hand = body(board, r'void\s+HandBack\s*\(')
    if not re.search(r'const\s+bool\s+drone\s*=\s*JetFliesItself\s*\(\s*v\s*\)', hand) or \
       not re.search(r'if\s*\(\s*!\s*drone\s*&&[^;]*SeatNpcRider\s*\(', hand):
        fail('playerjet_board.inc HandBack: SeatNpcRider not kept from a drone')

    for rel, sig, what in (('src/playerjet.cpp', r'bool\s+CrewedPilot\s*\(', 'CrewedPilot'),
                           ('src/jet.cpp', r'void\s+jet::ResumeNpc\s*\(', 'ResumeNpc'),
                           ('src/command_unit.cpp', r'bool\s+CommandVehicleLive\s*\(', 'CommandVehicleLive')):
        if not re.search(r'\bJetFliesItself\s*\(', body(read(root, rel), sig)):
            fail(f'{rel} {what}: a drone flown with no one aboard is not taken as flown here')

    for f in failures:
        print('FAIL', f)
    print('unmanned launch guard:', 'FAILED' if failures else 'passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
