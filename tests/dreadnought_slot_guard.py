"""Source guard of the Dreadnought's per-ship state and its pilots' way back: python tests/dreadnought_slot_guard.py
[--root DIR], exit 1 on a failure (review 2026-10-24). What it holds in place:
  - every array kept per ship slot (`X name[kMaxShips]` in src/dreadnought.cpp and the .inc files it includes) is reset
    together in ResetSlot, and nowhere else: a slot reused for a new ship once kept the last ship's turrets (remote
    gunners, network gate, the rig's bone records) and hangar (escorts, sorties); ResetSlot also drops its hull's
    bodies (HullBodies(i,nullptr));
  - a ship is only taken on through Take, which calls ResetSlot before it writes the slot; ResetDreadnought (a mission's
    start) is ResetSlot for every slot;
  - DreadnoughtTakesBack (no parachute for a fighter's pilot) decides by dreadnought_deck.h StandsAboard: the bay whose
    jet it is (b.jet.Is(fighter)) has it at that bay's own mouth putting its pilot out (BayJob::alight), not by the hull's
    box (DeckUnder); playerjet.cpp Leave skips the ejection only on it, and playerjet_board.inc Left hands the fighter
    back on it only when Leave did not eject; the hangar goes to alight only with its player's fighter brought home
    (b.home) and puts them out there (PutOut);
  - its hangars per socket (the user, 2026-10-24): RigHangar takes a bay as fitted only when its module's bone
    dn_hangar_<m> is in the model; a player's sortie leaves from a bay of the hangar they stand over (HangarUnder ->
    BoardBay over that m -> SendOut(..., true)), the board press reaching it from crew.cpp FindSeatHook; their fighter is
    called home to the bay it came out of (CallPlayerHome: the bay whose jet it is); the foredeck's ready fighter is gone
    (no dn_ready, kDeckRunSec, nextKind, DreadnoughtParked anywhere);
  - the Dreadnought's frame and input run under their own fault step (crew.cpp kStepDreadnought), named in kStepNames.
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root

    files = ('src/dreadnought.cpp', 'src/dreadnought_turrets.inc', 'src/dreadnought_hangar.inc')
    texts = {rel: read(root, rel) for rel in files}
    whole = '\n'.join(texts.values())
    slots = re.findall(r'^\s*(\w+)\s+(\w+)\s*\[\s*kMaxShips\s*\]\s*\{\s*\}\s*;', whole, re.M)
    names = {name: kind for kind, name in slots}
    if not {'ships', 'turrets', 'hangars'} <= set(names):
        fail(f'per-slot arrays found {sorted(names)}, want ships, turrets and hangars among them')
    cpp = texts['src/dreadnought.cpp']
    reset = body(cpp, r'void\s+ResetSlot\s*\(\s*int\s+i\s*\)')
    if not reset:
        fail('dreadnought.cpp: no ResetSlot(int i)')
    for name, kind in names.items():
        if not re.search(rf'\b{name}\s*\[\s*i\s*\]\s*=\s*{kind}\s*\{{\s*\}}\s*;', reset):
            fail(f'dreadnought.cpp ResetSlot: {name}[i] not reset to {kind}{{}}')
        # ...and nowhere else: a whole slot reset outside it would leave the others behind
        outside = whole.replace(reset, '')
        for m in re.finditer(rf'=\s*{kind}\s*\{{\s*\}}\s*;', outside):
            fail(f'{kind}{{}} assigned outside ResetSlot: ...{outside[max(0, m.start() - 60):m.end()]}')
    if not re.search(r'\bHullBodies\s*\(\s*i\s*,\s*nullptr\s*\)', reset):
        fail('dreadnought.cpp ResetSlot: its hull\'s bodies not dropped (HullBodies(i,nullptr))')

    take = body(cpp, r'Ship\*\s+Take\s*\(\s*unsigned\s+char\*\s*v\s*\)')
    r, w = re.search(r'\bResetSlot\s*\(\s*i\s*\)', take), re.search(r'->\s*vehicle\s*=\s*v', take)
    if not r or not w or r.start() > w.start():
        fail('dreadnought.cpp Take: the slot not reset (ResetSlot(i)) before the new ship is written into it')
    if re.search(r'\bMake\s*\(', cpp):
        fail('dreadnought.cpp: a ship taken on by Make (not through Take / ResetSlot)')
    reset_all = body(cpp, r'void\s+ResetDreadnought\s*\(\s*\)')
    if not re.search(r'for\s*\(\s*int\s+i\s*=\s*0\s*;\s*i\s*<\s*kMaxShips\s*;\s*\+\+i\s*\)\s*ResetSlot\s*\(\s*i\s*\)', reset_all):
        fail('dreadnought.cpp ResetDreadnought: not ResetSlot for every slot')

    back = body(cpp, r'bool\s+DreadnoughtTakesBack\s*\(')
    if not re.search(r'dreadnought::StandsAboard\s*\(', back):
        fail('dreadnought.cpp DreadnoughtTakesBack: not decided by dreadnought_deck.h StandsAboard')
    if re.search(r'\bDeckUnder\s*\(', back):
        fail('dreadnought.cpp DreadnoughtTakesBack: the hull\'s box (DeckUnder) is no proof the pilot lands on the ship')
    leave = body(read(root, 'src/playerjet.cpp'), r'void\s+Leave\s*\(\s*PJet&\s*j')
    if not re.search(r'const\s+bool\s+home\s*=[^;]*DreadnoughtTakesBack\s*\(\s*v\s*\)', leave) or \
       not re.search(r'const\s+bool\s+eject\s*=\s*!\s*home\s*&&', leave):
        fail('playerjet.cpp Leave: the ejection not skipped on DreadnoughtTakesBack alone')
    left = body(read(root, 'src/playerjet_board.inc'), r'void\s+Left\s*\(\s*PJet&\s*j')
    if not re.search(r'if\s*\(\s*!\s*eject\s*&&\s*DreadnoughtTakesBack\s*\(\s*v\s*\)\s*\)', left):
        fail('playerjet_board.inc Left: the fighter handed back on DreadnoughtTakesBack after an ejection')

    hangar = texts['src/dreadnought_hangar.inc']
    if not re.search(r'const\s+bool\s+alighting\s*=\s*b\.job\s*==\s*BayJob::alight\b', back) or \
       not re.search(r'if\s*\(\s*!\s*b\.fitted\s*\|\|\s*!\s*b\.jet\.Is\s*\(\s*fighter\s*\)\s*\)\s*continue', back) or \
       not re.search(r'BoneAt\s*\(\s*b\.mouth\s*\)', back) or \
       not re.search(r'StandsAboard\s*\(\s*h\.authority\s*,\s*alighting\s*,', back):
        fail('dreadnought.cpp DreadnoughtTakesBack: not "its own bay holds it at that bay\'s mouth putting its pilot out"')
    frame = body(hangar, r'void\s+BayFrame\s*\(')
    touch = re.search(r'if\s*\(\s*b\.home\s*&&\s*Flown\s*\(\s*f\s*\)\s*\)\s*\{([^}]*)\}', frame)
    if not touch or not re.search(r'b\.job\s*=\s*BayJob::alight', touch.group(1)) or not re.search(r'PutOut\s*\(', touch.group(1)):
        fail('dreadnought_hangar.inc BayFrame: alight only for its player brought home (b.home), putting them out there')
    if len(re.findall(r'BayJob::alight\s*;', frame)) != 1:
        fail('dreadnought_hangar.inc BayFrame: a bay goes to alight somewhere else than at its home touch')
    rig = body(hangar, r'bool\s+RigHangar\s*\(')
    if not re.search(r'L"dn_hangar_%d"', rig) or not re.search(r'b\.fitted\s*=\s*\(\s*\(\s*h\.fitted\s*>>\s*dreadnought::HangarOfBay\s*\(\s*i\s*\)', rig):
        fail('dreadnought_hangar.inc RigHangar: a bay not fitted by its module\'s bone dn_hangar_<m>')
    board = body(hangar, r'bool\s+HangarBoard\s*\(')
    if not re.search(r'const\s+int\s+m\s*=\s*HangarUnder\s*\(', board) or \
       not re.search(r'const\s+int\s+i\s*=\s*dreadnought::BoardBay\s*\([^;]*,\s*m\s*\)', board) or \
       not re.search(r'SendOut\s*\(\s*h\s*,\s*v\s*,\s*i\s*,\s*true\s*\)', board):
        fail('dreadnought_hangar.inc HangarBoard: a sortie not out of a bay of the hangar stood over')
    home = body(hangar, r'void\s+CallPlayerHome\s*\(')
    if not re.search(r'!\s*b\.jet\.Is\s*\(\s*f\s*\)', home) or not re.search(r'b\.job\s*=\s*BayJob::recover\s*;\s*b\.home\s*=\s*true', home):
        fail('dreadnought_hangar.inc CallPlayerHome: a player\'s fighter not called home to the bay it came out of')
    if not re.search(r'DreadnoughtHangarBoard\s*\(', body(read(root, 'src/crew.cpp'), r'FindSeatHook\s*\(')):
        fail('crew.cpp FindSeatHook: the board press never reaches the Dreadnought\'s hangars (DreadnoughtHangarBoard)')
    everything = '\n'.join(read(root, rel) for rel in ('src/dreadnought.cpp', 'src/dreadnought_hangar.inc', 'src/crew.h',
                                                       'src/playerjet_board.inc', 'src/playerjet.cpp'))
    for gone in ('dn_ready', 'kDeckRunSec', 'nextKind', 'DreadnoughtParked', 'DeckSpot'):
        if gone in everything:
            fail(f'the foredeck\'s ready fighter is gone (2026-10-24), yet {gone} is still there')

    crew = read(root, 'src/crew.cpp')
    for fn in ('DreadnoughtFrame', 'DreadnoughtInput'):
        if not re.search(rf'Guarded\s*\(\s*kStepDreadnought\s*,\s*&{fn}\b', crew):
            fail(f'crew.cpp: {fn} not under kStepDreadnought')
    steps = re.search(r'enum\s+Step\s*\{([^}]*)\}', crew)
    names_init = re.search(r'kStepNames\s*\[\s*\]\s*=\s*\{([^}]*)\}', crew)
    if not steps or not names_init:
        fail('crew.cpp: no Step enum or kStepNames[]')
    else:
        enum = [s.strip() for s in steps.group(1).split(',') if s.strip() and s.strip() != 'kStepCount']
        labels = re.findall(r'"([^"]*)"', names_init.group(1))
        if len(enum) != len(labels):
            fail(f'crew.cpp: {len(enum)} steps, {len(labels)} names')
        elif labels[enum.index('kStepDreadnought')] != 'dreadnought':
            fail('crew.cpp: kStepDreadnought not named "dreadnought"')

    for f in failures:
        print('FAIL', f)
    print('dreadnought slot guard:', 'FAILED' if failures else 'passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
