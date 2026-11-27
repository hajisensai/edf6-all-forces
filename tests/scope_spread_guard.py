"""Offline guard of the scoped weapons' spread (src/scope_spread*.{h,cpp}, docs/scope-accuracy-re.md):
  - the weapon table (docs/weapons.csv): category 2 is Ranger Weapon_SniperRifle and 104 Wing Diver Weapon_Pale_Long,
    exactly the ones scope_spread_logic.h counts as sniper rifles; the ported EDF5 / EDF4.1 sniper rifles
    (edf5port / edf41port weapons.json, mapped from their source category: EDF4.1 Wing Diver long range 14) carry the same;
  - the ini (EDF6VehicleCrew.ini), crew.h's defaults, plugin.cpp's reads, clamps (the logic's ranges) and its CONFIG line
    agree on the five keys;
  - the shot entry hook writes and restores around the stock shot; the host's word carries the values (kFeatScopeSpread).
"""
import csv
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = 0


def ok(cond: bool, what: str) -> None:
    global checks
    if not cond:
        print('FAIL', what)
        raise SystemExit(1)
    checks += 1


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8-sig')


logic = read('src/scope_spread_logic.h')
sniper = {int(v) for v in re.findall(r'k(?:SniperRifle|PaleLong)=(\d+)', logic)}
ok(sniper == {2, 104}, f'scope_spread_logic.h: sniper categories {sniper}')

rows = list(csv.DictReader(open(ROOT / 'docs' / 'weapons.csv', encoding='utf-8-sig')))
names = {(r['category'], r['category_name'], r['class']) for r in rows}
ok(('2', 'Weapon_SniperRifle', 'Ranger') in names and ('104', 'Weapon_Pale_Long', 'WingDiver') in names,
   'weapons.csv: 2 Ranger SniperRifle, 104 Wing Diver Pale_Long')
ok(all(r['category_name'] in ('Weapon_SniperRifle', 'Weapon_Pale_Long') for r in rows if int(r['category']) in sniper),
   'no other kind of weapon in those categories')
ok(sum(int(r['category']) in sniper for r in rows) >= 60, 'the stock sniper rifles are there (Ranger 39 + Wing Diver 27)')

# The source games' sniper categories and the EDF6 one each port maps them to (EDF5 kept EDF6's numbers; EDF4.1's Wing
# Diver long range was 14).
for reg, source, mapping in (('edf5port/weapons.json', 'edf5_category', {2: 2}),
                             ('edf41port/weapons.json', 'edf41_category', {2: 2, 14: 104})):
    weapons = json.loads(read(reg))['weapons']
    for w in weapons:
        if w.get(source) in mapping:
            ok(w['category'] == mapping[w[source]], f'{reg} {w["id"]}: a ported sniper rifle is a sniper rifle here')
        elif w['category'] in sniper:
            ok(False, f'{reg} {w["id"]}: category {w["category"]} from source category {w.get(source)}')
    ok(any(w['category'] in sniper for w in weapons), f'{reg}: ported sniper rifles counted as sniper rifles')

ini = read('EDF6VehicleCrew.ini')
crew = read('src/crew.h')
plugin = read('src/plugin.cpp')
defaults = {'ScopeSpread': ('scopeSpread', '1', 'true'), 'ScopeSniperHipSpread': ('scopeSniperHipSpread', '4.0', '4.0f'),
            'ScopeSniperHipMinSpread': ('scopeSniperHipMinSpread', '0.015', '0.015f'),
            'ScopeHipSpread': ('scopeHipSpread', '1.25', '1.25f'), 'ScopeAimSpread': ('scopeAimSpread', '0.8', '0.8f')}
for key, (field, value, member) in defaults.items():
    ok(re.search(rf'^{key}={re.escape(value)}$', ini, re.M) is not None, f'EDF6VehicleCrew.ini {key}={value}')
    ok(re.search(rf'\b{field}={re.escape(member)};', crew) is not None, f'crew.h {field}={member}')
    ok(f'L"{key}",n.{field})' in plugin, f'plugin.cpp reads {key}')
    ok(f'n.{field}' in plugin.split('Log("CONFIG scopeSpread=', 1)[1].split(';', 1)[0], f'the CONFIG line prints {key}')
for key, rng in (('ScopeSniperHipSpread', 'kSniperHipRange'), ('ScopeSniperHipMinSpread', 'kSniperHipMinRange'),
                 ('ScopeHipSpread', 'kHipRange'), ('ScopeAimSpread', 'kAimRange')):
    ok(re.search(rf'Fix\("{key}",n\.\w+,scopespread::{rng}\.lo,scopespread::{rng}\.hi\);', plugin) is not None,
       f'plugin.cpp clamps {key} to {rng}')

heli = read('src/heli.cpp')
hook = re.search(r'void __fastcall MedicShotHook\(.*?\n\}', heli, re.S).group(0)
at = [hook.find(s) for s in ('ScopeSpreadShot(weapon,replay,&scope);', 'medicShotNext(', 'ScopeSpreadRestore(scope);')]
ok(min(at) >= 0 and at == sorted(at), 'MedicShotHook writes, shoots, restores')
installs = read('src/module_table.h')
installs = installs[installs.index('constexpr InstallEntry kInstalls[]={'):]
ok(installs[:installs.index('\n};')].count('Id::kScopeSpread,&Install<&InstallScopeSpread>}') == 1,
   'the load installs it (module_table.h kInstalls)')
ok('target_sources(EDF6VehicleCrew PRIVATE src/scope_spread.cpp)' in read('CMakeLists.txt'), 'the plugin builds it')

host = read('src/host_settings_logic.h')
features = re.search(r'kFeatures=([^;]*);', host)
ok('kFeatScopeSpread=1024u' in host and features is not None and
   'kFeatScopeSpread' in re.findall(r'\w+', features.group(1)), 'the room word carries the feature bit')
ok('ScopeSpreadInEffect()' in read('src/scope_spread.cpp'), 'a round plays by the values in effect (the host\'s online)')
spread = read('src/scope_spread.cpp')
ok('SCOPESPREAD REFUSED call site' in spread and 'SCOPESPREAD installed' in spread, 'the install logs its outcome')
ok('infantryscope' not in spread.lower().replace('infantryscope.cpp', ''), 'no dependence on the PiP scope renderer')

print(f'scope_spread_guard: {checks} checks passed')
