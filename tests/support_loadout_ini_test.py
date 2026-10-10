"""The out-of-game loadouts' editor and generator (tools/support_loadout.py) against the plugin (src/support_loadout.h,
src/support_soldier.cpp kBodies, src/support_spawn.h, src/support_variants.cpp, src/jet_internal.h): the same kinds,
templates, stores, round counts, bodies, keys, variants, file names and hash, the same verdict on the same values, the
editor writing only values the plugin accepts, the pending list. With the game installed, the generated files from its
own files (read only; nothing is written to the game): a coloured soldier differs from its stock template only in
soldier_color, a loaded vehicle from its base only in its weapon list, holders and preloads."""
from __future__ import annotations

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import support_config as sc  # noqa: E402
import support_loadout as sl  # noqa: E402

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


def refused_with(error: type, fn, *args) -> bool:
    try:
        fn(*args)
    except error:
        return True
    return False


def refused(fn, *args) -> bool:
    try:
        fn(*args)
    except sl.Invalid:
        return True
    return False


header = read('src/support_loadout.h')
kinds = re.findall(r'\{SupportWeapon::(\w+),L"(\w+)",L"([^"]+)"\}', header)
check([(n, label) for _, n, label in kinds] == [(n, label) for n, label, _ in sl.KINDS], 'kind names and labels equal the plugin\'s')
check([n for _, n, _ in kinds][:5] == [n for n, _ in sc.WEAPONS], 'the Rangers\' kind names are the ini weapons\'')
bodies = re.findall(r'L"app:/object/(\w+)\.sgo",L"app:/object/(\w+)\.sgo"', read('src/support_soldier.cpp'))
check([(t, t + '_LEADER') for _, _, t in sl.KINDS] == bodies, 'each kind\'s template (and its _LEADER) is the plugin\'s')
stores = re.findall(r'\{(\d+),L"(\w+)",L"([^"]+)",LoadRole::(\w+),(true|false),(true|false)\}', header)
check([(int(c), n, label, role, j == 'true', t == 'true') for c, n, label, role, j, t in stores] == list(sl.LOAD_STORES),
      'pylon stores equal the plugin\'s')
check(re.search(r'kRoundCounts\[16\]=\{([^}]*)\}', header).group(1).split(',') == [str(n) for n in sl.ROUND_COUNTS],
      'round counts equal')
rows = re.findall(r'\{VehicleBody::\w+,L"(\w+)",L"([\w.]+)",(\d+),(true|false)\}', header)
check({n: (b, int(m), t == 'true') for n, b, m, t in rows} == sl.BODIES, 'vehicle bodies equal')
check(dict(re.findall(r'\{L"(\w+)",VehicleBody::(\w+)\}', header)) == {k: b.lower() for k, b in sl.VEHICLE_KEYS.items()},
      'vehicle keys equal')
check(set(sl.VEHICLE_KEYS) <= set(sc.UNIT_KEYS), 'vehicle keys are catalog keys')
check(f'L"{sl.BODIES["TANK"][0]}"' in header and f'L"app:/Object/{sl.BODIES["TANK"][0]}"' in read('src/support_spawn.h'),
      'the loaded tank is made from the support tank')
check(all(f'L"{base}"' in read('src/jet_internal.h') for base, _, tank in sl.BODIES.values() if not tank),
      'a loaded jet is made from the plugin\'s jet of that body')
check('kVariantApplied=1ull<<63,kVariantVehicle=1ull<<62' in header and sl.APPLIED == 1 << 63 and sl.VEHICLE == 1 << 62, 'variant bits')
check(f'L"Plugins\\\\{sl.PENDING}"' in read('src/support_variants.cpp'), 'the pending list is the one the plugin writes')
check('L"SupportPreset_%ls"' in read('src/support_config.cpp') and 'L"SupportVehicle_%ls"' in read('src/support_config.cpp'),
      'the plugin reads both keys')
prefer = re.search(r'constexpr Prefer LoadoutPrefer\(.*?\n}', read('src/jet_internal.h'), re.S).group(0)
check(prefer in read('tests/support_loadout_test.cpp'), 'the jets\' load preference tested is the plugin\'s own text')
spawn = read('src/support_spawn.h')
rows_seats = dict(re.findall(r'L"app:/Object/(\w+)\.SGO",(\d+)', spawn))
check(sl.SEATS['TRANSPORT_CREWED'] == int(rows_seats['V507_RESCUETANK_AI']) - 1 and
      sl.SEATS['TRUCK_CREWED'] == int(rows_seats['V512_KEITRUCK_BGP']) - 1, 'the crewed vehicles\' passenger seats')

# The same values, the same verdict (tests/support_loadout_test.cpp has the C++ side of each).
p = sl.parse_preset('rifle@1E3A8A*2, rocket ，sniper@x:ffffff', 4)
check(p == [('rifle', (0x1E3A8A, -1))] * 2 + [('rocket', (-1, -1)), ('sniper', (-1, 0xFFFFFF))], 'a preset parses as in the plugin')
for bad, seats in (('rifle*5', 4), ('rifle@12345', 12), ('rifle@GGGGGG', 12), ('rifle@123456:', 12), ('rifle*0', 12),
                   ('bogus', 12), ('rifle*13', 12), ('@123456', 12)):
    check(refused(sl.parse_preset, bad, seats), f'preset {bad!r} refused as in the plugin')
check(sl.parse_vehicle('HE,AP:25', 'TANK') == sl.Loadout('TANK', False, [('AP', 25)]), 'tank: HE gun, 25 AP rounds beside it')
check(sl.parse_vehicle('ap , AP:20 , GLM:4', 'TANK') == sl.Loadout('TANK', True, [('AP', 20), ('GLM', 4)]), 'tank: AP gun')
check(sl.parse_vehicle('MK82:6,MK82:6,MK82:4', 'FIGHTER').pylons == [('MK82', 6), ('MK82', 6), ('MK82', 4)], 'jet: all bombs')
check(sl.parse_vehicle('GUNS', 'MULTIROLE') == sl.Loadout('MULTIROLE'), 'jet: the guns alone')
check(sl.parse_vehicle('', 'STRIKE') is None, 'empty: stock')
for bad, body in (('AP:20', 'TANK'), ('HE,MK82:6', 'TANK'), ('AP:20', 'FIGHTER'), ('MK82:7', 'FIGHTER'), ('MK82', 'FIGHTER'),
                  ('GUNS,MK82:6', 'STRIKE'), ('HE,AP:20,AP:20,AP:20,AP:20,AP:20', 'TANK'),
                  ('MK82:2,MK82:2,MK82:2,MK82:2,MK82:2,MK82:2', 'STRIKE'), ('HE', 'SQUAD')):
    check(refused(sl.parse_vehicle, bad, body), f'loadout {bad!r} on {body} refused as in the plugin')
tank = sl.parse_vehicle('HE,AP:25', 'TANK')
check(sl.vehicle_file(tank) == 'EDF6VC_LO_TANK_4000000000000C81.SGO', 'the tank\'s file name as the plugin\'s')
check(sl.vehicle_file(sl.parse_vehicle('MK82:6,MK82:6', 'FIGHTER')) == 'EDF6VC_LO_FIGHTER_4000000000056562.SGO', 'a jet\'s')
check(sl.of_variant(sl.variant(tank) | sl.APPLIED, 'TANK') == tank and sl.of_variant(sl.variant(tank), 'FIGHTER') is None,
      'a variant decodes back for its body only')
check(sl.look_file('rifle', True, (0x1E3A8A, -1)) == 'EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO', 'a look file name as the plugin\'s')
check(sl.variant_hash('EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO') == 0x69A64712A798737F and '0x69A64712A798737Full' in
      read('tests/support_loadout_test.cpp'), 'the filter\'s hash is the plugin\'s')
check(sl.decode_name('edf6vc_lo_tank_4000000000000c81.sgo') == ('lo', tank) and
      sl.decode_name('EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO') == ('npc', 'lance', True, (0x1E3A8A, -1)), 'names decode back')
for bad in ('EDF6VC_NPC_RIFLE_X_X.SGO', 'EDF6VC_LO_TANK_C000000000000C81.SGO', 'EDF6VC_LO_TANK_4000000000000C8F.SGO',
            'EDF6VC_LO_HELI_4000000000000000.SGO', 'EDF6VC_NPC_GATLING_FFFFFF_X.SGO', 'readme.txt'):
    check(sl.decode_name(bad) is None, f'{bad} makes no file')

# The editor: menu 7 -> l. Soldiers, then a vehicle's pylons (载具 → 挂点 → 武器); only values the plugin accepts.
ini = '[VehicleCrew]\r\nEnabled=1\r\n'
answers = iter(['l', '6', 'lance@1E3A8A*4,cannon*4', '1', 'rifle*13',
                'v1', 'a', '1', '25', '', 'v5', 'a', '6', '6', 'a', '6', '6', '2', '4', '2', '', '', ''])
out = sc.edit(ini, lambda _prompt: next(answers))
check(sl.get(out, 'SupportPreset_PLATOON_HELI') == 'lance@1E3A8A*4,cannon*4' and sl.get(out, 'SupportPreset_SQUAD') == '',
      'a preset written, a refused one not')
check(sl.get(out, 'SupportVehicle_TANK_CREWED') == 'HE,AP:25', 'tank: an AP pylon of 25 beside the HE gun')
check(sl.get(out, 'SupportVehicle_FIGHTER') == 'MK82:6,AGM:2', 'fighter: pylon 1 a bomb load, pylon 2 changed to Mavericks')
check(out.endswith('\r\n') and '\n' not in out.replace('\r\n', ''), 'CRLF kept')
cleared = sl.edit(out, lambda _p, a=iter(['v1', 's', '']): next(a))
check(sl.get(cleared, 'SupportVehicle_TANK_CREWED') == '' and 'TANK_CREWED' not in sl.vehicles(cleared), 's restores the stock')
untouched = sl.edit(out, lambda _p, a=iter(['v3', '', '']): next(a))
check(sl.get(untouched, 'SupportVehicle_STRIKE') == '', 'opening a vehicle and leaving it writes nothing')
check(sl.wanted_files(out, ['EDF6VC_NPC_RIFLE_X_FFFFFF.SGO', 'junk']) ==
      ['EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO', 'EDF6VC_NPC_LANCE_1E3A8A_X.SGO', 'EDF6VC_LO_TANK_4000000000000C81.SGO',
       sl.vehicle_file(sl.parse_vehicle('MK82:6,AGM:2', 'FIGHTER')), 'EDF6VC_NPC_RIFLE_X_FFFFFF.SGO'],
      'the files wanted: the presets\' looks, the loadouts, the pending list\'s decodable names')
shipped = read('EDF6VehicleCrew.ini')
check(re.search(r'SupportPreset_[A-Z_]+=[a-z]', shipped) and re.search(r'SupportVehicle_[A-Z_]+=[A-Z]', shipped) and
      'SupportTankRounds' not in shipped, 'the shipped ini documents both keys (and no retired one)')

# The pending list (src/support_variants.cpp writes it, this tool clears what it made).
with tempfile.TemporaryDirectory(prefix='edf6vc-pending-') as root:
    os.makedirs(os.path.join(root, 'Mods', 'Plugins'))
    with open(sl.pending_path(root), 'w', encoding='utf-8', newline='') as f:
        f.write('EDF6VC_NPC_RIFLE_X_FFFFFF.SGO\r\nEDF6VC_LO_TANK_4000000000000C81.SGO\r\nnot a file\r\n')
    check(sl.read_pending(root) == ['EDF6VC_NPC_RIFLE_X_FFFFFF.SGO', 'EDF6VC_LO_TANK_4000000000000C81.SGO', 'not a file'], 'read')
    gone = sl.clear_pending(root, {'OBJECT/EDF6VC_NPC_RIFLE_X_FFFFFF.SGO': b''})
    check(gone == ['EDF6VC_NPC_RIFLE_X_FFFFFF.SGO', 'not a file'] and sl.read_pending(root) == ['EDF6VC_LO_TANK_4000000000000C81.SGO'],
          'what was made (and what no file can be made from) leaves the list; the rest stays')
    sl.clear_pending(root, {'OBJECT/EDF6VC_LO_TANK_4000000000000C81.SGO': b''})
    check(not os.path.isfile(sl.pending_path(root)), 'an empty list is removed')

# One file that cannot be made (a pending name for a jet this machine lacks: jet_sgo raises) leaves out that one alone,
# reported by name; the others are made, and install keeps the copy already on disk (the 2026-10-10 review: one failure
# threw away the whole build, every run, and the coloured soldiers of this machine's own presets with it).
bad_name, good_name = 'EDF6VC_NPC_RIFLE_X_FFFFFF.SGO', 'EDF6VC_NPC_RIFLE_X_000000.SGO'
check(sl.decode_name(bad_name) and sl.decode_name(good_name), 'two decodable soldier names')
real_soldier_file = sl.soldier_file


def fake_soldier_file(game, kind, leader, look):
    if look == sl.decode_name(bad_name)[3]:
        raise ValueError('its template is not installed here')
    return b'made'


sl.soldier_file = fake_soldier_file
try:
    errors: dict[str, str] = {}
    files = sl.build(None, '', object(), pending=[bad_name, good_name], errors=errors)
    check(files == {'OBJECT/' + good_name: b'made'} and list(errors) == [bad_name] and 'not installed' in errors[bad_name],
          'one file that cannot be made: left out and reported, the other made')
    check(refused_with(ValueError, sl.build, None, '', object(), [bad_name, good_name]), 'without errors given: it raises')
finally:
    sl.soldier_file = real_soldier_file
with tempfile.TemporaryDirectory(prefix='edf6vc-keep-') as root:
    for folder in ('OBJECT', 'WEAPON', 'Plugins'):
        os.makedirs(os.path.join(root, 'Mods', folder))
    sl.install(root, {'OBJECT/' + bad_name: b'old', 'OBJECT/' + good_name: b'old'})
    sl.install(root, {'OBJECT/' + good_name: b'made'}, keep=['OBJECT/' + bad_name])
    check(os.path.isfile(os.path.join(root, 'Mods', 'OBJECT', bad_name)), 'a file not made this time is kept on disk')
    sl.install(root, {'OBJECT/' + good_name: b'made'})
    check(not os.path.isfile(os.path.join(root, 'Mods', 'OBJECT', bad_name)), 'one no longer wanted is released')

# With the game: the generated files, from its own files (read only).
try:
    import rootcpk
    game = rootcpk.default()
except Exception as e:   # no game on this machine (CI): the text checks above stand
    print(f'support_loadout_ini_test: no game ({e}); generated files not checked')
    game = None
if game is not None:
    import sgo
    real = rootcpk.DEFAULT_GAME
    jets_installed = os.path.isfile(os.path.join(real, 'Mods', 'OBJECT', 'EDF6VC_JET_FIGHTER.SGO'))
    text = out if jets_installed else sl.put(out, 'SupportVehicle_FIGHTER', '')
    files = sl.build(real if jets_installed else None, text, game)
    for leader in (True, False):
        made = sgo.load(data=files['OBJECT/' + sl.look_file('lance', leader, (0x1E3A8A, -1))])
        stock = sgo.load(data=game.read('OBJECT', 'N606_AIPALEWING_LANCE' + ('_LEADER' if leader else '') + '.SGO'))
        check({k for k in stock if made[k] != stock[k]} == {'soldier_color'}, 'a coloured soldier: only soldier_color differs')
    made = sgo.load(data=files['OBJECT/EDF6VC_LO_TANK_4000000000000C81.SGO'])
    check([w[0] for w in made['mission_setup'][2]] == [sl.HE_GUN, 'app:/weapon/edf6vc_ap_25.sgo'] and
          len(made['vehicle_weapon_setting']) == 2 and made['mission_setup'][2][1][1] == made['mission_setup'][2][0][1],
          'the tank: its HE gun, 25 AP rounds on a holder of their own, recoiling as the gun')
    if 'WEAPON/EDF6VC_AP_25.SGO' in files:
        ap = sgo.load(data=files['WEAPON/EDF6VC_AP_25.SGO'])
        check(ap['AmmoCount'] == 25.0 and ap['AmmoClass'] == 'SolidBullet01Rail', 'the 25-round AP weapon made from the stock round')
    if jets_installed:
        f = sgo.load(data=files['OBJECT/' + sl.vehicle_file(sl.parse_vehicle('MK82:6,AGM:2', 'FIGHTER'))])
        check([w[0] if isinstance(w, list) else w for w in f['mission_setup'][3]] ==
              [*sl.GUN_FILES, 'app:/weapon/edf6vc_mk82_6.sgo', sl.FUEL, 'app:/weapon/edf6vc_agm_2.sgo'],
              'the fighter: guns, its bombs, the fuel tank fourth, its Mavericks')
        guns = sl.jet_sgo(real, game, sl.Loadout('FIGHTER'))
        check(len(sgo.load(data=guns)['vehicle_weapon_setting']) == 4, 'guns alone: four holders (an empty one), as the 506 builds')
    else:
        print('support_loadout_ini_test: the jets are not installed here; loaded jets not checked')
print(f'support_loadout_ini_test: {checks} checks passed')
