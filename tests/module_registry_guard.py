"""The module registry (src/module.h, src/module_table.h, docs/module-registry.md) is where the plugin's lifecycle is
listed: plugin.cpp and mission.cpp hold no list of features' installs / resets of their own, and every module of the
table keeps the reset rule (a stateful one has its one reset entry; a stateless one says why, and has none). The rule is
static_asserted on the real tables when the plugin builds; this reads them again from the source, so a table edit that
the build would refuse is named here by module, with no compiler. Source only, no game."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(*parts: str) -> str:
    return open(os.path.join(ROOT, *parts), encoding='utf-8').read()


def code(text: str) -> str:
    return re.sub(r'//[^\n]*', '', text)   # comments may name anything


def table(text: str, name: str) -> str:
    start = text.index(f'constexpr {name}[]={{')
    return text[start:text.index('\n};', start)]


def body(text: str, signature: str) -> str:
    """The function's body (braces counted) after its signature."""
    start = text.index(signature)
    at = text.index('{', start)
    depth = 0
    for i in range(at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[at:i + 1]
    raise AssertionError(signature)


src = code(read('src', 'module_table.h'))
rows = re.findall(r'M\((k\w+),"([^"]*)",(nullptr|kLoadOnly|"[^"]*")\)', src)
check(len(rows) > 100, f'the rows read from module_table.h ({len(rows)})')
ids = [r[0] for r in rows]
check(len(set(ids)) == len(ids), 'every module id once')
names = [r[1] for r in rows]
check(all(names) and len(set(names)) == len(names), 'every module named, each name once (the fault log names it)')

resets = re.findall(r'Id::(k\w+)', table(src, 'ResetEntry kResets'))
passes = {name: re.findall(r'Id::(k\w+)', table(src, name)) for name in
          ('InstallEntry kInstalls', 'ResetEntry kResets', 'TickEntry kTicks', 'VehicleEntry kVehicleSteps')}
for module, name, why in rows:
    n = resets.count(module)
    if why == 'nullptr':
        check(n == 1, f'{module} ("{name}") keeps state: its one reset entry in kResets (it has {n}); '
                      'or, if it keeps nothing of a mission, its stateless reason in its row')
    else:
        check(n == 0, f'{module} ("{name}") says it keeps nothing ({why[:60]}) but has a reset entry')
        check(why == 'kLoadOnly' or len(why) > 12, f'{module} ("{name}"): a stateless reason that says something')
    check(any(module in entries for entries in passes.values()), f'{module} ("{name}") in some pass')
for entries in passes.values():
    for module in entries:
        check(module in ids, f'{module}: an entry of a row the table has')

# The lists themselves are the registry's: plugin.cpp's load and mission.cpp's mission start call its passes, and call
# no feature's install / reset beside it (a feature added beside them would miss the order, the fault id, the rule).
call = re.compile(r'(?<![\w:.>])((?:\w+::)?(?:Reset|Install)\w*|\w+::(?:Reset|Install))\s*\(')
allowed = {
    # the room screen's own sync point (the room's members read into this mission's record): its menu let go there too,
    # before the mission's start (where the registry's room ui reset runs again)
    ('mission.cpp', 'ResetRoomUi'),
}
for file in ('mission.cpp', 'plugin.cpp'):
    text = code(read('src', file))
    for m in call.finditer(text):
        before = text[max(0, m.start() - 40):m.start()]
        if m.group(1).startswith('modules::'):
            continue   # the registry's own passes
        if re.search(r'\b(?:void|bool|int|auto)\s+$', before):
            continue   # a definition (plugin.cpp ResetPlayer, mission.cpp InstallMission), not a call
        check((file, m.group(1)) in allowed, f'src/{file} calls {m.group(1)}() itself: a module\'s entry of '
                                             'src/module_table.h instead')
mission = body(code(read('src', 'mission.cpp')), 'void MissionStart() noexcept')
check(mission.count('modules::ResetAll();') == 1, 'MissionStart runs the reset pass once')
load = body(code(read('src', 'plugin.cpp')), 'EML6_Load(')
check(load.count('modules::InstallAll(') == 1, 'the load runs the install pass once')
check(not re.search(r'\bplugin_net::Reset\s*\(', code(read('src', 'support_net.cpp'))),
      'the plugin channels\' resets are the registry\'s plugin net module, not the support session\'s')

# The seams: crew.cpp InputHook runs every vehicle phase once, in the enum's order, the stock input between
# kBeforeStockInput and kAfterStockInput, kFrame after the Enabled test and the shared frame tick (with the plugin off:
# only its kOff entries, then out); the shared tick runs kTicks once. A phase not run would drop its entries silently.
logic = code(read('src', 'module_logic.h'))
phases = re.findall(r'^\s*(k\w+),', logic.split('enum class VehiclePhase', 1)[1].split('};', 1)[0], re.M)
check(phases[:2] == ['kBeforeGears', 'kBeforeStockInput'] and phases[-1] == 'kFrame', f'VehiclePhase {phases}')
crew = code(read('src', 'crew.cpp'))
hook = body(crew, 'template<int I> void __fastcall InputHook(')
runs = re.findall(r'modules::RunVehicle\(VehiclePhase::(k\w+),v,I,(.*?)\);', hook)
check([p for p, _ in runs] == phases + ['kFrame'], f'InputHook runs the vehicle phases in order: {runs}')
check(all(on == 'Cfg().enabled' for p, on in runs if p != 'kFrame'), 'the phases before kFrame told the plugin\'s state')
stock = hook.index('nextInput[I](vehicle,hasInput,a3,a4);')
check(hook.index('VehiclePhase::kBeforeStockInput,') < stock < hook.index('VehiclePhase::kAfterStockInput,'),
      'the stock input between kBeforeStockInput and kAfterStockInput')
off = hook.index('if(!Cfg().enabled){modules::RunVehicle(VehiclePhase::kFrame,v,I,false);return;}')
tick = hook.index('FrameTick();')
check(hook.index('VehiclePhase::kAfterSound,') < off < tick < hook.index('modules::RunVehicle(VehiclePhase::kFrame,v,I,true);'),
      'kFrame after the Enabled test and the shared frame tick; off: only the kOff entries')
shared = body(crew, 'void SharedFrameTick()')
check(shared.count('modules::RunTicks();') == 1, 'the shared frame tick runs kTicks once')

cmake = read('CMakeLists.txt')
check('add_test(NAME module_registry COMMAND module_registry_test)' in cmake, 'the rules\' own test is registered')
print(f'module_registry_guard: {checks} checks, {len(rows)} modules')
