"""Read-only sight input audit against the real game's Root.cpk. This does not execute the game or native HUD.

python tests/sight_data_audit.py --game "D:/.../EARTH DEFENSE FORCE 6"
Native weapon/seat behavior is covered separately by sightzoom_test and turret_cam_runtime_check.
"""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import rootcpk
import sgo
import make_katyusha
import make_artillery

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', default='')
args = parser.parse_args()
if not args.game or not (Path(args.game) / 'Root.cpk').is_file():
    print('SKIP no game directory with Root.cpk given')
    sys.exit(77)
game = rootcpk.Game(args.game)
cases = (
    ('V_505TANK_DLC_CANNON04L.SGO', 'RocketBullet01', 0, 600, 'direct cannon'),
    ('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 0, 100, 'short grenade ballistic sight'),
    ('V_409HELI_MISSILE01.SGO', 'MissileBullet01', 0, 2400, 'unguided rocket ballistic sight'),
    ('V_506HELI_MISSILE01.SGO', 'MissileBullet01', 1, 2400, 'guided missile lock sight'),
    ('V_FUEL01.SGO', 'SolidBullet01', 0, 1, 'fuel holder excluded'),
)
for name, ammo, lock, alive, label in cases:
    fields = sgo.load(data=game.read('WEAPON', name))
    assert (fields['AmmoClass'], fields['LockonType'], fields['AmmoAlive']) == (ammo, lock, alive), name
    print(f'PASS real Root.cpk {name}: {label}')
for label, fields, mark in (('Katyusha', make_katyusha.ROCKETS, 7303), ('howitzer', make_artillery.SHELLS, 7302)):
    assert fields['AmmoClass'] == 'GrenadeBullet01' and fields['AmmoAlive'] >= 600 and fields['LockonTargetType'] == mark
    print(f'PASS generator parameters {label}: indirect fire (not a generated-asset or gameplay test)')
# The sights' style rows (tools/reticle_check.cpp kWeapons): the numbers its Classify cases are built from must be the
# game's own (AmmoClass, LockonType, FireInterval, AmmoSpeed, AmmoExplosion, SecondaryFire_Type), so the style each
# real weapon is checked to get is the one the plugin will pick from what it reads in game.
import re
row = re.compile(r'\{"(V[^"]+\.SGO)","([^"]+)",(\d+),(\d+),([\d.]+)f,([\d.]+)f,(\d+),"[^"]+",Style::\w+,Shell::\w+\}')
rows = row.findall((ROOT / 'tools' / 'reticle_check.cpp').read_text(encoding='utf-8'))
assert len(rows) >= 20, len(rows)
for name, ammo, lock, interval, speed, blast, scope in rows:
    f = sgo.load(data=game.read('WEAPON', name))
    got = (f['AmmoClass'], int(f['LockonType']), int(f['FireInterval']), float(f['AmmoSpeed']), float(f['AmmoExplosion']),
           int(f.get('SecondaryFire_Type', 0)))
    want = (ammo, int(lock), int(interval), float(speed), float(blast), int(scope))
    assert got[:3] == want[:3] and abs(got[3]-want[3]) < 1e-3 and abs(got[4]-want[4]) < 1e-3 and got[5] == want[5], (name, got, want)
    print(f'PASS real Root.cpk {name}: the numbers reticle_check classifies')
print(f'5 real archive cases + 2 generator parameter cases + {len(rows)} sight style rows passed; no installation writes')
