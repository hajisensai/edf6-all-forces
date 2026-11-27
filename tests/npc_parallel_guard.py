"""Source guard: the soldiers' parallel Decide (src/npcai.cpp mt::Decide, docs/npc-ai-design.md「并行决策与串行提交」)
runs on the pool's threads during the stock AI pass, so everything it can reach must be a read of the frame's world.

Its call graph (npcai.cpp's functions it calls, transitively; the grid / logic / reference / index headers they call
into, whole) must not
  - write the wastedfire book or ask what writes it (WastedFire, HoldFire, WastedFireHit, WatchTarget, WastedFirePrime:
    only WastedFirePeek, the read-only side);
  - pick a weapon, pull a trigger, look or move (kPickWeapon, kTrigger, Look, Walk, Stand, Put<...>, ChooseArm);
  - read the clock, the config, the frame counter or log (GameMs, GameFrame, GetTickCount64, Cfg, Log), or the memo's
    game-thread state (applying, stats);
  - draw a random number (rand, std::mt19937, the game's LCG 0x705570);
  - call a native object's virtual function or an address of the game (a function pointer cast, a vtable).
And each name it reaches outside these files is one of the known pure leaves (KNOWN). MapRay is the one native call:
the Havok world's ray query (heli.h MapRay -> CastRay -> hknpWorld::castRay, docs/havok-mt-re.md), read-only, made
while nothing steps the world.

    python tests/npc_parallel_guard.py
"""
from __future__ import annotations

import os
import re
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'pylib'))
import srcscan  # noqa: E402  (the one C++ reader: comments, literals, bodies, calls)

checks = failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


# The files whose functions the Decide can reach (npcai.cpp's own, the grid, the logic, the references, the index).
FILES = ('src/npcai.cpp', 'src/npc_grid.h', 'src/npc_logic.h', 'src/objref.h', 'common/edf/hash_index.h')
# Names called that are not functions of FILES: the language's, the library's, and the reads they are.
KNOWN = {
    'static_cast', 'reinterpret_cast', 'const_cast', 'sizeof', 'alignof', 'decltype', 'noexcept', 'return',
    'if', 'for', 'while', 'switch', 'memcpy', 'memcmp', 'memset',
    'At',               # edf::At<T>(p,off): a typed read
    'WastedFirePeek',   # wastedfire.cpp's read-only side
    'MapRay',           # the Havok ray query (see above)
}
FORBIDDEN_CALLS = ('WastedFire', 'HoldFire', 'WastedFireHit', 'WatchTarget', 'WastedFirePrime', 'GameMs', 'GameFrame',
                   'GetTickCount64', 'QueryPerformanceCounter', 'Cfg', 'Log', 'rand', 'srand', 'Look', 'Walk', 'Stand',
                   'Put', 'Readable', 'ChooseArm', 'PlanFor', 'Run')
FORBIDDEN_WORDS = ('kPickWeapon', 'kTrigger', 'mt19937', 'random_device', 'applying', 'stats')

npcai = srcscan.read('src/npcai.cpp')
HEADERS = {rel: srcscan.read(rel) for rel in FILES if rel != 'src/npcai.cpp'}
own: dict[str, list[str]] = {}
for d in srcscan.definitions(npcai):
    own.setdefault(d.name, []).append(d.body)
header_code = '\n'.join(srcscan.masked(t) for t in HEADERS.values())
decide = srcscan.function(npcai, 'Decide')
check(bool(decide), 'src/npcai.cpp defines mt::Decide')


def in_headers_of(text: str, name: str) -> bool:
    return re.search(rf'(?<![\w]){name}\s*(?:<[^;(){{}}]*>\s*)?\(', srcscan.masked(text)) is not None


def in_headers(name: str) -> bool:
    return in_headers_of(header_code, name)


# A call: a member's (.x( / ->x(: a grid's, an index's, a reference's: the headers'), a qualified one (npc:: / edf::: the
# headers'; std::: the library's) or a bare name (npcai.cpp's own: followed into, every overload of the name).
CALL = re.compile(r'(?<![\w:])(?P<member>(?:\.|->)\s*)?(?P<q>(?:\w+::)*)(?P<name>\w+)\s*(?:<[^;(){}]*>\s*)?\(')
reached: dict[str, str] = {'Decide': decide}
unknown: set[str] = set()
todo = [decide]
while todo:
    for m in CALL.finditer(srcscan.masked(todo.pop())):
        name, q = m.group('name'), m.group('q')
        if name in KNOWN or q.startswith('std::'):
            continue
        if m.group('member') or q.startswith(('npc::', 'edf::')):
            if not in_headers(name):
                unknown.add(q + name)
            continue
        if q:
            unknown.add(q + name)   # another namespace's function (workers::, online::, ...)
            continue
        if name in reached:
            continue
        if name in own:
            reached[name] = '\n'.join(own[name])
            todo.extend(own[name])
        elif not in_headers(name):
            unknown.add(name)

print(f"npc_parallel_guard: Decide reaches {len(reached)} of npcai.cpp's functions: {', '.join(sorted(reached))}")
check(not unknown, f'mt::Decide reaches names outside {", ".join(FILES)} that are not known pure leaves: {sorted(unknown)}')


def clean(name: str, body: str, words: bool) -> None:
    for f in FORBIDDEN_CALLS:
        check(not srcscan.calls(body, f), f'mt::Decide reaches {name}, which calls {f}')
    if words:
        for w in FORBIDDEN_WORDS:
            check(not srcscan.mentions(body, w), f'mt::Decide reaches {name}, which uses {w}')
    mask = srcscan.masked(body)
    check(not re.search(r'\(\s*(?:__fastcall|__cdecl|__stdcall|__thiscall)?\s*\*\s*\)\s*\(', mask),
          f'mt::Decide reaches {name}, which casts to a function pointer (a native call)')
    check(not re.search(r'0x705570|vtable', mask, re.I), f'mt::Decide reaches {name}, which names a vtable or the LCG')


for name, body in sorted(reached.items()):
    clean(name, body, True)
# The headers it reaches through: the grid and the logic whole (pure geometry and scoring: no clock, config, log or
# write of the game's); of the reference and the index, the members it calls (ObjRef::Is, HashIndex::Find).
for rel in ('src/npc_grid.h', 'src/npc_logic.h'):
    clean(rel, HEADERS[rel], False)
for rel, member in (('src/objref.h', 'Is'), ('common/edf/hash_index.h', 'Find')):
    bodies = [d.body for d in srcscan.definitions(HEADERS[rel]) if d.name == member]
    check(bool(bodies), f'{rel} defines {member}')
    for b in bodies:
        clean(f'{rel} {member}', b, False)
members = {m.group('name') for body in reached.values() for m in CALL.finditer(srcscan.masked(body)) if m.group('member')}
for m in sorted(members):
    in_ref = any(d.name == m for r in ('src/objref.h', 'common/edf/hash_index.h') for d in srcscan.definitions(HEADERS[r]))
    in_pure = any(in_headers_of(HEADERS[r], m) for r in ('src/npc_grid.h', 'src/npc_logic.h'))
    check(m in ('Is', 'Find') or not in_ref or in_pure, f'mt::Decide calls {m} of objref.h / hash_index.h, not checked here')

# The fork: the book primed, its version taken, then the pool; the Decide is what the pool runs.
fork = srcscan.function(npcai, 'Fork')
check(srcscan.order(fork, 'WastedFirePrime', 'WastedFireVersion', 'Run'), 'mt::Fork primes the book, takes its version, then runs the pool')
check(srcscan.has(fork, 'workers::Run(&Decide'), 'mt::Fork runs mt::Decide on the pool')
# Every memo wrapper keeps an answer only for the same inputs bit for bit (Same3), and asks again otherwise.
for wrapper, now in (('ShotClearOf', 'ShotClearNow'), ('FriendNear', 'FriendNearNow'), ('MapRayQ', 'MapRay'),
                     ('PickTarget', 'PickRowNow')):
    body = srcscan.function(npcai, wrapper)
    check(srcscan.calls(body, 'Same3') and srcscan.calls(body, now), f'{wrapper} keeps an answer only for the same inputs, else asks {now}')
# The holds kept only under the book's version at the Decide (a settled hit since: asked again).
check(srcscan.calls(srcscan.function(npcai, 'KeptHold'), 'Fresh') and
      srcscan.calls(srcscan.function(npcai, 'Fresh'), 'WastedFireVersion'), 'the kept holds stand only under the same WastedFireVersion')
# Drive's asks go through the wrappers: a raw WastedFire / HoldFire / map ray would pass the memo by.
drive = srcscan.function(npcai, 'Drive')
for raw in ('WastedFire', 'HoldFire', 'ShotClearNow', 'FriendNearNow', 'PickRowNow', 'MapRay'):
    check(not srcscan.calls(drive, raw), f'Drive asks {raw} directly (use its memo wrapper)')
for fn in ('ShotOk', 'Veto', 'ChooseArm', 'Backpack'):
    body = srcscan.function(npcai, fn)
    # Backpack's thrown arc asks the map for each segment (not asked by the Decide: made at Drive, as before).
    for raw in ('WastedFire', 'HoldFire', 'ShotClearNow', 'FriendNearNow') + (() if fn == 'Backpack' else ('MapRay',)):
        check(not srcscan.calls(body, raw), f'{fn} asks {raw} directly (use its memo wrapper)')
    if fn == 'Backpack':
        check(srcscan.calls(body, 'MapRayQ'), 'Backpack asks its straight shot\'s map ray through MapRayQ')

print(f'npc_parallel_guard: {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
