"""Source guard of the doll drone's doll (src/jet_carrier.cpp): python tests/doll_follow_guard.py [--root DIR], exit 1 on
a failure. What it holds in place:
  - the doll's matrix (DollPose) is written only where the doll is made (DollMake) and by the once-a-frame follow
    (DollsFollow), never by a flight step: posed from the flight steps (JetFrame, HoverDone) a drone moved without one
    (no pilot, the player out of it, bumped, falling) left its doll behind (2026-10-09 "人偶无人机，被碰到的时候会把人偶和
    无人机分开");
  - DollsFollow poses from every live entry's own vehicle, whoever flies it (no Flown / seen / pilot test in it);
  - jet.cpp Sweep (once a game frame from JetReap) calls DollsFollow after the finished entries are released;
  - no flight file (jet.cpp JetFrame, playerjet*.inc / .cpp) poses a doll itself.
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

    carrier = read(root, 'src/jet_carrier.cpp')
    posers = functions_calling(carrier, 'DollPose') - {'DollPose'}
    if posers != {'DollMake', 'DollsFollow'}:
        fail(f'jet_carrier.cpp: DollPose called from {sorted(posers)}, want only DollMake and DollsFollow')
    follow = body(carrier, r'void\s+DollsFollow\s*\(')
    if not follow:
        fail('jet_carrier.cpp: no DollsFollow')
    else:
        if not re.search(r'\bAlive\s*\(\s*jets\s*\[\s*i\s*\]\s*\.ref\s*\)', follow):
            fail('jet_carrier.cpp DollsFollow: does not test its entry with Alive(jets[i].ref)')
        if not re.search(r'\bDollPose\s*\(\s*i\s*,', follow):
            fail('jet_carrier.cpp DollsFollow: does not pose doll i')
        for word in ('Flown', 'seen', 'NpcDriver', 'SeatRider', 'PlayerJetHolds', 'm.ready'):
            if re.search(r'\b' + re.escape(word) + r'\b', follow):
                fail(f'jet_carrier.cpp DollsFollow: tests {word}: a doll must follow a drone nobody flies this frame too')

    jet = read(root, 'src/jet.cpp')
    sweep = body(jet, r'void\s+Sweep\s*\(\s*ULONGLONG')
    if not sweep:
        fail('jet.cpp: no Sweep(ULONGLONG)')
    else:
        at = re.search(r'\bDollsFollow\s*\(\s*\)', sweep)
        released = re.search(r'\bRelease\s*\(', sweep)
        if not at:
            fail('jet.cpp Sweep: does not call DollsFollow')
        elif released and released.start() > at.start():
            fail('jet.cpp Sweep: DollsFollow before the finished entries are released')
    reap = body(jet, r'void\s+JetReap\s*\(')
    if not re.search(r'\bSweep\s*\(\s*ms\s*\)', reap):
        fail('jet.cpp JetReap: no longer sweeps once a frame')

    flight = ['src/jet.cpp', 'src/jet_flight.cpp'] + sorted(
        os.path.join('src', n).replace('\\', '/') for n in os.listdir(os.path.join(root, 'src')) if n.startswith('playerjet'))
    for rel in flight:
        if not os.path.exists(os.path.join(root, rel)):
            continue
        text = read(root, rel)
        for name in ('DollPose', 'DollFrame', 'DollsFollow'):
            for m in re.finditer(r'\b' + name + r'\s*\(', text):
                if rel == 'src/jet.cpp' and name == 'DollsFollow' and m.start() >= jet.find(sweep) >= 0 and \
                   m.start() < jet.find(sweep) + len(sweep):
                    continue
                line = text.count('\n', 0, m.start()) + 1
                fail(f'{rel}:{line}: {name} outside jet.cpp Sweep: a flight step poses the doll again')

    for f in failures:
        print('FAIL', f)
    print('doll follow guard:', 'FAILED' if failures else 'passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
