"""The out-of-game loadouts' editor and generator (tools/support_loadout.py) against the plugin (src/support_loadout.h,
src/support_soldier.cpp kBodies, src/support_spawn.h): the same kinds, templates, rounds and file names, the same verdict
on the same values, the editor writing only values the plugin accepts. With the game installed, the generated files
from its Root.cpk (only read; nothing is written to the game): a coloured soldier differs from its stock template only
in soldier_color, the AP tank from the support tank only in its gun."""
from __future__ import annotations

import os
import re
import sys

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
order = re.search(r'enum class SupportWeapon[^{]*\{([^}]*)\}', read('src/support_call.h')).group(1)
check([w for w, _, _ in kinds] == [w.strip() for w in order.split(',')][:-1], 'kinds in SupportWeapon order')
bodies = re.findall(r'L"app:/object/(\w+)\.sgo",L"app:/object/(\w+)\.sgo"', read('src/support_soldier.cpp'))
check([(t, t + '_LEADER') for _, _, t in sl.KINDS] == bodies, 'each kind\'s template (and its _LEADER) is the plugin\'s')
rounds = re.findall(r'\{TankRound::(\w+),L"(\w+)",L"([^"]+)"\}', header)
check([(n, label) for _, n, label in rounds] == list(sl.ROUNDS), 'rounds equal the plugin\'s')
check(f'kSupportTankApFile=L"{sl.TANK_FILE}"' in header and
      f'kSupportTankApPath=L"app:/object/{sl.TANK_FILE.lower()}"' in header, 'the AP tank\'s file and path')
spawn = read('src/support_spawn.h')
check(f'L"app:/Object/{sl.TANK_STOCK}"' in spawn, 'the AP tank is made from the support tank')
check(re.search(r'kRoundMixMost=(\d+)', header).group(1) == str(sl.ROUND_MIX_MOST) and
      re.search(r'kRoundWeightMost=(\d+)', header).group(1) == str(sl.ROUND_WEIGHT_MOST), 'mix limits equal')
check(re.search(r'kSupportLooksMost=(\d+)', read('src/support_call.h')).group(1) == str(sl.LOOKS_MOST), 'looks a mission takes')
# Seats (src/support_dispatch.cpp SupportCallSeats): the infantry and the transports 12, a crewed APC / truck its stock
# rows but the driver's (src/support_spawn.h kSupportVehicles).
rows = dict(re.findall(r'L"app:/Object/(\w+)\.SGO",(\d+)', spawn))
check(sl.SEATS['TRANSPORT_CREWED'] == int(rows['V507_RESCUETANK_AI']) - 1 and
      sl.SEATS['TRUCK_CREWED'] == int(rows['V512_KEITRUCK_BGP']) - 1, 'the crewed vehicles\' passenger seats')
check(set(sl.SEATS) <= set(sc.GROUND_KEYS) and all(sl.SEATS[k] == 12 for k in sl.SEATS if 'CREWED' not in k),
      'seated entries are catalog keys; the infantry and the transports take 12')
check('L"SupportPreset_%ls"' in read('src/support_config.cpp') and 'L"SupportTankRounds"' in read('src/support_config.cpp'),
      'the plugin reads both keys')

# The same values, the same verdict (tests/support_loadout_test.cpp has the C++ side of each).
p = sl.parse_preset('rifle@1E3A8A*2, rocket ，sniper@x:ffffff', 4)
check(p == [('rifle', (0x1E3A8A, -1))] * 2 + [('rocket', (-1, -1)), ('sniper', (-1, 0xFFFFFF))], 'a preset parses as in the plugin')
check(sl.parse_preset('', 12) == [] and len(sl.parse_preset('lance*4,cannon*4,rifle*4', 12)) == 12, 'empty / a full platoon')
for bad, seats in (('lance*4,cannon*4,rifle*5', 12), ('rifle*5', 4), ('rifle', 0), ('rifle@12345', 12), ('rifle@GGGGGG', 12),
                   ('rifle@123456:', 12), ('rifle*0', 12), ('rifle*x', 12), ('bogus', 12), ('rifle*13', 12), ('@123456', 12)):
    check(refused(sl.parse_preset, bad, seats), f'preset {bad!r} refused as in the plugin')
check(sl.format_preset(sl.parse_preset('rifle@1e3a8a,rifle@1E3A8A,rocket,lance@X:00ff00', 12)) == 'rifle@1E3A8A*2,rocket,lance@X:00FF00',
      'a preset is written back in its shortest form')
check(sl.parse_rounds('') == (False, []) and sl.pick_round(False, [], 5) == 'HE', 'no rounds: HE')
ratio, entries = sl.parse_rounds('AP:1,HE:1')
check(ratio and [sl.pick_round(ratio, entries, k) for k in range(6)] == ['AP', 'HE'] * 3, '1:1 alternates, AP first')
for n in range(1, 41):
    check(sum(sl.pick_round(ratio, entries, k) == 'AP' for k in range(n)) == (n + 1) // 2, '1:1: ceil(n/2) AP')
ratio, entries = sl.parse_rounds('AP:30,HE:70')
check(sum(sl.pick_round(ratio, entries, k) == 'AP' for k in range(10)) == 3, '30:70 over 10 tanks: 3 AP')
check(sl.parse_rounds('AP*2,HE') == (False, [('AP', 1), ('AP', 1), ('HE', 1)]), 'a list with *n')
check(sl.parse_rounds('ap:50%,he:50%') == (True, [('AP', 50), ('HE', 50)]), 'percent weights')
for bad in ('AP,HE:1', 'AP:0,HE:1', 'AP:1001', 'APFSDS', 'AP*17', 'AP:1,HE', 'AP*2:1'):
    check(refused(sl.parse_rounds, bad), f'rounds {bad!r} refused as in the plugin')
check(sl.look_file('rifle', True, (0x1E3A8A, -1)) == 'EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO' and
      sl.look_file('pilebanker', False, (-1, 0x0A0B0C)) == 'EDF6VC_NPC_PILEBANKER_X_0A0B0C.SGO', 'look file names as the plugin\'s')

# The editor: menu 7 -> l. A valid preset is written in the plugin's syntax, an invalid one never.
ini = '[VehicleCrew]\r\nEnabled=1\r\n'
answers = iter(['l', '6', 'lance@1E3A8A*4,cannon*4', '1', 'rifle*13', 't', 'AP:1,HE:1', '', ''])
out = sc.edit(ini, lambda _prompt: next(answers))
check(sl.get(out, 'SupportPreset_PLATOON_HELI') == 'lance@1E3A8A*4,cannon*4', 'the preset written')
check(sl.get(out, 'SupportPreset_SQUAD') == '', 'a refused preset is not written')
check(sl.get(out, 'SupportTankRounds') == 'AP:1,HE:1' and out.endswith('\r\n') and '\n' not in out.replace('\r\n', ''),
      'rounds written, CRLF kept')
cleared = sl.edit(out, lambda _p, a=iter(['6', '-', '']): next(a))
check(sl.get(cleared, 'SupportPreset_PLATOON_HELI') == '' and sl.presets(cleared) == {}, '- clears a preset')
check(sl.looks_wanted(out) == [('lance', True, (0x1E3A8A, -1)), ('lance', False, (0x1E3A8A, -1))],
      'the coloured soldiers the plugin preloads: a leader and a member look')
check('SupportTankRounds=' in read('EDF6VehicleCrew.ini') and re.search(r'SupportPreset_[A-Z_]+=[a-z]', read('EDF6VehicleCrew.ini')),
      'the shipped ini has the key and an example')

# With the game: the generated files, from its own files (read only).
try:
    import rootcpk
    game = rootcpk.default()
except Exception as e:   # no game on this machine (CI): the text checks above stand
    print(f'support_loadout_ini_test: no game ({e}); generated files not checked')
    game = None
if game is not None:
    import sgo
    files = sl.build(None, out, game)
    check(set(files) == {'OBJECT/EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO', 'OBJECT/EDF6VC_NPC_LANCE_1E3A8A_X.SGO',
                         'OBJECT/' + sl.TANK_FILE}, 'one file per coloured soldier, and the AP tank')
    for leader in (True, False):
        made = sgo.load(data=files['OBJECT/' + sl.look_file('lance', leader, (0x1E3A8A, -1))])
        stock = sgo.load(data=game.read('OBJECT', 'N606_AIPALEWING_LANCE' + ('_LEADER' if leader else '') + '.SGO'))
        check({k for k in stock if made[k] != stock[k]} == {'soldier_color'}, 'a coloured soldier: only soldier_color differs')
        for (sel, rgba), (_, stock_rgba) in zip(made['soldier_color'], stock['soldier_color']):
            channel, mesh = sel[0][1], sel[0][0]
            if channel == 'change_color0' and not (isinstance(mesh, str) and 'face' in mesh):
                check(abs(rgba[0] - 0x1E / 255) < 1e-6 and abs(rgba[2] - 0x8A / 255) < 1e-6 and rgba[3] == stock_rgba[3], 'primary set')
            else:
                check(rgba == stock_rgba, 'secondary (X) and a face keep the stock colour')
    tank = sgo.load(data=files['OBJECT/' + sl.TANK_FILE])
    check(tank['xgs_scene_object_class'] == 'Vehicle505_Tank' and tank['mission_setup'][2][0][0] == sl.AP_GUN and
          tank['vehicle_setup'][2][0][0] == sl.AP_GUN and sl.AP_GUN in tank['resource'], 'the AP tank mounts the A1 gun')
    sl.check_tank(files['OBJECT/' + sl.TANK_FILE], game.read('OBJECT', sl.TANK_STOCK))
    gun = sgo.load(data=game.read('WEAPON', 'V_505TANK_CANNON01S.SGO'))
    check(gun['AmmoClass'] == 'SolidBullet01Rail' and gun['AmmoIsPenetration'] == 1 and gun['AmmoExplosion'] == 0,
          'the AP gun is a penetrating round with no blast (and HE is the stock howitzer)')
print(f'support_loadout_ini_test: {checks} checks passed')
