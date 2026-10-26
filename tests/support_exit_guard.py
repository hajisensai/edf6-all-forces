"""Source guard of the support aircraft's way out (the user, 2026-10-26: 「空中支援不会退场」): python tests/support_exit_guard.py
[--root DIR], exit 1 on a failure. What it holds in place:
  - heli.cpp HeliCrewed starts a heli's entry afresh (Heli{}): it is called only where an entry is made, by the frame
    (HeliFrame, a heli with none yet) and by the call (HeliCalled). HeliSortie called it too (PR #253) and wiped the call
    HeliCalled had just given the heli (called, its post, its fuel): the sortie refused itself and the heli, no longer a
    called one, never left (no LeaveReason / StartLeave / reap) and followed the player for the rest of the mission.
    HeliSortie looks its entry up (Find) and takes only a called one;
  - jet_spawn.cpp ActivateSupportAircraft calls HeliCalled before HeliSortie (the sortie is laid on the call);
  - a withdrawing jet is not held by its soft edge (jet_internal.h SoftEdgeHolds): jet_flight.cpp SoftEdge keeps the
    edge's state on a copy for it and Hover does not clamp its goal, so it leaves the map and Away (jet_flight.cpp, the
    one the jet's frame flies: jet.cpp Guide) reaps it out there. Flown: tools/jet_obstacle_sim --withdraw-suite.
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
    """The text with // comments cut (no // inside the string literals these checks read)."""
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


def functions_calling(text: str, callee: str) -> set[str]:
    """The names of the top-level definitions in `text` whose bodies call `callee`."""
    names: set[str] = set()
    for m in re.finditer(r'^[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?noexcept\s*\{', text, re.M):
        b = body(text[m.start():], re.escape(m.group(1)) + r'\s*\(')
        if re.search(r'\b' + re.escape(callee) + r'\s*\(', b):
            names.add(m.group(1))
    return names


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root

    heli = read(root, 'src/heli.cpp')
    makers = functions_calling(heli, 'HeliCrewed') - {'HeliCrewed'}
    if makers != {'HeliFrame', 'HeliCalled'}:
        fail(f'heli.cpp: HeliCrewed (it starts an entry afresh) called from {sorted(makers)}, want only HeliFrame and HeliCalled')
    sortie = body(heli, r'void\s+HeliSortie\s*\(')
    if not sortie:
        fail('heli.cpp: no HeliSortie')
    else:
        if not re.search(r'\bFind\s*\(\s*vehicle\s*\)', sortie):
            fail('heli.cpp HeliSortie: does not look its entry up with Find(vehicle)')
        if not re.search(r'!\s*h\s*->\s*called', sortie):
            fail('heli.cpp HeliSortie: does not refuse a heli that is not a called one')

    spawn = read(root, 'src/jet_spawn.cpp')
    activate = body(spawn, r'bool\s+ActivateSupportAircraft\s*\(')
    called, laid = re.search(r'\bHeliCalled\s*\(', activate), re.search(r'\bHeliSortie\s*\(', activate)
    if not called or not laid:
        fail('jet_spawn.cpp ActivateSupportAircraft: HeliCalled / HeliSortie not both called')
    elif laid.start() < called.start():
        fail('jet_spawn.cpp ActivateSupportAircraft: HeliSortie before HeliCalled (the sortie is laid on the call)')

    internal = read(root, 'src/jet_internal.h')
    holds = re.search(r'bool\s+SoftEdgeHolds\s*\(\s*const\s+Jet\s*&\s*j\s*\)\s*noexcept\s*\{([^}]*)\}', internal)
    if not holds or not re.search(r'j\s*\.\s*mode\s*!=\s*Mode::withdraw', holds.group(1)):
        fail('jet_internal.h: SoftEdgeHolds is not `j.mode!=Mode::withdraw`')

    flight = read(root, 'src/jet_flight.cpp')
    edge = body(flight, r'void\s+SoftEdge\s*\(\s*Jet\s*&')
    if not re.search(r'\bleaving\s*=\s*!\s*SoftEdgeHolds\s*\(\s*j\s*\)', edge) or \
       not re.search(r'KeepIn\s*\([^;]*\bleaving\s*\?\s*kept\s*:\s*want', edge):
        fail('jet_flight.cpp SoftEdge: a withdrawing jet\'s want is turned by KeepIn (it must run on the copy, `kept`)')
    hover = body(flight, r'void\s+Hover\s*\(\s*Jet\s*&')
    if not re.search(r'if\s*\(\s*npcGoal\s*&&\s*SoftEdgeHolds\s*\(\s*j\s*\)\s*\)\s*airbound::ClampIn', hover):
        fail('jet_flight.cpp Hover: the NPC goal is clamped into the soft box without SoftEdgeHolds (a withdrawal held in)')
    if not body(flight, r'void\s+Away\s*\(\s*Jet\s*&'):
        fail('jet_flight.cpp: no Away (the withdrawal the jet flies and the sim flies)')

    jet = read(root, 'src/jet.cpp')
    if re.search(r'^\s*void\s+Away\s*\(', jet, re.M):
        fail('jet.cpp: a second Away (the one in jet_flight.cpp is the one tools/jet_obstacle_sim flies)')
    guide = body(jet, r'void\s+Guide\s*\(\s*Jet\s*&')
    if not re.search(r'case\s+Mode::withdraw\s*:\s*Away\s*\(', guide):
        fail('jet.cpp Guide: a withdrawing jet is not flown by Away')

    for f in failures:
        print('FAIL', f)
    print('support exit guard:', 'FAILED' if failures else 'passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
