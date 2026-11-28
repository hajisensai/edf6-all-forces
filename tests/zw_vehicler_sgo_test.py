"""The Vehicler on foot's data (docs/newclass-re/vehicler-plan.md §5 M5; tools/zw_weapons.py, tools/make_zw_classes.py): the Gyrojet
is six single shots that leave slowly and speed up, a thin short trail, and damage that grows with flight (AmmoDamageReduce
[a > 1, b], the gate src/vehicler_foot_logic.h widens); the battle SGO is the Ranger's but for its class (the dash is no class SGO
field: docs/newclass-re/motion.md §3, held back in the plugin instead). With Root.cpk at hand, the SGOs built are checked too.
Plain asserts: python tests/zw_vehicler_sgo_test.py"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
sys.path.insert(0, str(ROOT / 'tools'))
import call_weapons as cw  # noqa: E402
import gamedir  # noqa: E402
import make_zw_classes as zc  # noqa: E402
import zw_weapons as zw  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    assert ok, why


def falloff(d: float, a: float, b: float, age: int, life: int) -> float:
    """0x231080 (src/vehicler_foot_logic.h Falloff)."""
    t = min(max(age / life, 0.0), 1.0)
    return (1.0 - t) ** b * (d - d * a) + d * a


def flown(frames: int) -> float:
    """Metres a Gyrojet round has flown after `frames` (RocketBullet01: AmmoSpeed, + accel each frame)."""
    v0, acc = zw.GYROJET['AmmoSpeed'], zw.GYROJET_CUSTOM[3]
    return v0 * frames + acc * frames * (frames - 1) / 2


g = zw.GYROJET
# plan §1.3: six rounds, single shots
check(g['AmmoCount'] == 6.0, 'six rounds')
check(g['FireInterval'] >= 10.0, 'single shots, not a stream')
# slow at the muzzle, then accelerating, to a useful range
check(g['AmmoSpeed'] <= 0.3, 'leaves the muzzle slowly (m/frame)')
check(zw.GYROJET_CUSTOM[0] == 0.0 and zw.GYROJET_CUSTOM[3] > 0.0, 'RocketBullet01 mode 0, accelerating')
check(g['AmmoSpeed'] + zw.GYROJET_CUSTOM[3] * g['AmmoAlive'] >= 10 * g['AmmoSpeed'], 'ten times faster by the end')
check(150.0 <= flown(int(g['AmmoAlive'])) <= 400.0, f'range {flown(int(g["AmmoAlive"])):.0f} m')
# damage: low point-blank, growing with flight, never falling
a, b = zw.GYROJET_DAMAGE_REDUCE
d, life = g['AmmoDamage'], int(g['AmmoAlive'])
check(a > 1.0 and b > 0.0, 'AmmoDamageReduce is growth (a > 1, the plugin-widened gate) with a finite curve (b > 0)')
check(falloff(d, a, b, 0, life) == d, 'point-blank damage is AmmoDamage')
curve = [falloff(d, a, b, age, life) for age in range(life + 1)]
check(all(x <= y for x, y in zip(curve, curve[1:])), 'damage never falls with flight')
check(curve[-1] == d * a, 'the end of life is a times')
near = max(age for age in range(life + 1) if flown(age) <= 5.0)
check(curve[near] < 2.0 * d, 'within 5 m it stays under twice AmmoDamage')
# the plugin's gate rule agrees: a > 1 is refreshed, every stock value ([1, 1] and a < 0.99) as before
logic = (ROOT / 'src' / 'vehicler_foot_logic.h').read_text(encoding='utf-8')
check('kStockGate>a || a>1.0f' in logic, 'the gate rule the data relies on')
# thin, short trail: smaller than Cascade's (AmmoSize 0.22, Ammo_CustomParameter [0, 20, 100, 0.1])
check(g['AmmoSize'] < 0.22 and zw.GYROJET_CUSTOM[1] < 20.0 and zw.GYROJET_CUSTOM[2] < 100.0, 'a thin, short trail')
check(g['AmmoExplosion'] <= 3.0, 'a small blast')
# the shown stats: the damage range
check(zw.GYROJET_STATS[0] == '6' and zw.GYROJET_STATS[2] == f'{d:.0f}-{d * a:.0f}', 'stat lines match the data')
# the texts say it (every language)
gyro = next(w for w in zw.WEAPONS if w.id == 'ZW_Gyrojet01')
for lang in ('SC', 'CN', 'JA', 'EN', 'KR'):
    check(re.search(r'5|five', gyro.text[lang][1]) is not None, f'{lang}: the growth named')

game_root = gamedir.find()
if game_root is None:
    print('zw_vehicler_sgo: no game here, Root.cpk part skipped')
else:
    import dsgo  # noqa: E402
    import vcobjects as vc  # noqa: E402
    sgos = zw.build(lambda rel: cw.stock(game_root, rel))
    built = dsgo.to_py(dsgo.parse(sgos[zw.sgo_file(gyro)]).root)
    check(built['AmmoClass'] == 'RocketBullet01', 'RocketBullet01')
    check(built['FireCount'] == 1.0 and built['FireBurstCount'] == 1.0, 'one round a shot')
    check(built['AmmoDamageReduce'] == [a, b], 'AmmoDamageReduce written as a list')
    check(built['Ammo_CustomParameter'] == list(zw.GYROJET_CUSTOM), 'custom parameter')
    check(all(built[k] == v for k, v in g.items()), 'every Gyrojet scalar')
    # the Vehicler battle SGO is the Ranger's but for xgs_scene_object_class
    game = vc.Game(game_root)
    ranger = zc._config(game)
    files = zc.build(game_root)
    vehicler = next(c for c in zc.CLASSES if c.type == 4)
    for n in range(1, zc.APPEARANCES + 1):
        stock = dsgo.to_py(dsgo.parse(game.read(*zc._rel(ranger[2][n - 1]))).root)
        mine = dsgo.to_py(dsgo.parse(files[vehicler.battle(n)]).root)
        check(mine['xgs_scene_object_class'] == 'ZwVehicler', f'battle {n}: ZwVehicler')
        check({k: v for k, v in mine.items() if k != 'xgs_scene_object_class'}
              == {k: v for k, v in stock.items() if k != 'xgs_scene_object_class'}, f'battle {n}: otherwise the Ranger')
        check(not any('dash' in k.lower() for k in mine), f'battle {n}: no dash field in the class SGO')
print(f'zw_vehicler_sgo: {checks} checks passed')
