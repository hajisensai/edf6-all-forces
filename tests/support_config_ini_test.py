"""The installer's support menu (tools/support_config.py) against the plugin's parser (src/support_config.cpp,
src/support_dispatch.cpp SupportCallKey) and the call table (tools/calls.py): the same keys and weapons, and the menu
writes only values the plugin accepts. Pure text; no game files."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import calls  # noqa: E402
import support_config as sc  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


cpp = read('src/support_config.cpp')
weapons = re.findall(r'\{SupportWeapon::(\w+),L"(\w+)",L"([^"]+)"\}', cpp)
check([(n, l) for _, n, l in weapons] == list(sc.WEAPONS), 'weapon names and labels equal the plugin parser\'s')
dispatch = read('src/support_dispatch.cpp')
ground = re.search(r'static const wchar_t\* keys\[\]=\{([^}]*)\}', dispatch).group(1)
check(tuple(re.findall(r'L"(\w+)"', ground)) == sc.GROUND_KEYS, 'ground / infantry keys equal SupportCallKey\'s')
flown = tuple(c.id[len(calls.ID_PREFIX):] for c in calls.CALLS if c.flown)
check(flown == sc.AIR_KEYS, 'air keys: the flown calls in calls.py order (airstrike.cpp SupportAirCallKey)')
check(len(sc.LABELS) == len(sc.UNIT_KEYS) == 36, 'one label per catalog entry')
ini = read('EDF6VehicleCrew.ini')
for key, value in sc.DEFAULTS.items():
    check(sc.get(ini, key) == value, f'shipped ini {key} = the default {value!r}')
for header in ('SupportSquadWeapon', 'SupportPlatoonWeapons', 'SupportDisabled'):
    check(header in cpp, f'{header} is read by the plugin')

text = '[VehicleCrew]\r\nEnabled=1\r\nSupportDisabled=FIGHTER\r\n\r\n[Other]\r\nSupportDisabled=SQUAD\r\n'
check(sc.disabled(text) == ['FIGHTER'], 'only [VehicleCrew] is read')
# Read as the plugin reads it: ';' separates (no inline comment), spaces do not.
hand = '[VehicleCrew]\r\nSupportDisabled=SQUAD;TANK_CREWED 、 FIGHTER\r\nSupportSquadWeapon=rocket ;x\r\n'
check(sc.disabled(hand) == ['SQUAD', 'TANK_CREWED', 'FIGHTER'], "';' and '、' separate SupportDisabled, as in the plugin")
check(sc.disabled('[VehicleCrew]\r\nSupportDisabled=SQUAD TANK_CREWED\r\n') == [], 'a space does not separate')
check(sc.get(hand, 'SupportSquadWeapon') == 'rocket ;x', 'no inline comment taken off (the plugin rejects this value)')
toggled = sc.edit(hand, lambda _p, a=iter(['1', '1', '']): next(a))
check(sorted(sc.disabled(toggled)) == ['FIGHTER', 'SQUAD', 'TANK_CREWED'], 'a toggle round trip keeps every hand-written unit')
out = sc.put(text, 'SupportSquadWeapon', 'rocket')
check('SupportSquadWeapon=rocket\r\n\r\n[Other]' in out and out.endswith('\r\n') and '\n' not in out.replace('\r\n', ''),
      'a new key goes at the end of its own section, keeping CRLF')
check(sc.put(out, 'SupportSquadWeapon', 'flame').count('SupportSquadWeapon=') == 1, 'an existing key is replaced, not duplicated')
check(sc.weapon('Sniper') == 'sniper' and sc.weapon('火焰') == 'flame' and sc.weapon('3') == 'rocket', 'weapon by name, label or number')
for bad in ('laser', '', '6'):
    try:
        sc.weapon(bad)
        check(False, f'weapon {bad!r} refused')
    except sc.Invalid:
        check(True, '')
check(sc.platoon('flame，rocket rifle') == 'flame,rocket,rifle', 'platoon accepts any separator')
for bad in ('rifle,rifle', 'rifle,rifle,rifle,rifle', 'rifle,x,rifle'):
    try:
        sc.platoon(bad)
        check(False, f'platoon {bad!r} refused')
    except sc.Invalid:
        check(True, '')
try:
    sc.units('FIGHTER,BOGUS')
    check(False, 'unknown unit refused')
except sc.Invalid as e:
    check('BOGUS' in str(e), 'the unknown unit is named')
check(sc.aircraft_count('3') == '3', 'aircraft count in range')
for bad in ('9', '-1', 'two'):
    try:
        sc.aircraft_count(bad)
        check(False, f'count {bad!r} refused')
    except sc.Invalid:
        check(True, '')

# The menu itself, with scripted answers: toggle two units on/off, a weapon, the platoon, a count, an invalid answer.
answers = iter(['7', '22', '7', 'w', '1', 'shotgun', 'p', 'sniper,flame,rocket', 'c', 'heli_f', '3', 'w', '2', 'laser', ''])
edited = sc.edit(ini, lambda _prompt: next(answers))
check(sc.disabled(edited) == ['SQUAD'], 'units toggled off and back on')
check(sc.get(edited, 'SupportSquadWeapon') == 'shotgun' and sc.get(edited, 'SupportSquadLeaderWeapon') == 'rifle',
      'a weapon set; the invalid one left unchanged')
check(sc.get(edited, 'SupportPlatoonWeapons') == 'sniper,flame,rocket' and sc.get(edited, 'SupportAircraftCount_HELI_F') == '3',
      'platoon and aircraft count written')
check(edited.replace(sc.get(edited, 'SupportDisabled'), '') != '' and 'Enabled=1' in edited, 'everything else of the ini kept')
print(f'support_config_ini_test: {checks} checks passed')
