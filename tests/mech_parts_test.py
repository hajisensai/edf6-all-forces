"""The custom mech parts (pylib/mech_parts.py, docs/feature-2026-11-21-mech-custom.md): the rules the plugin shares
(src/mech_loadout.h, src/support_loadout.h; the same values give the same answers), and, with the game, every table entry
against Root.cpk and the generated mech of every stock request against that request's own setup.

  python tests/mech_parts_test.py
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_mech_parts  # noqa: E402
import mech_parts as mp  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print(f'FAIL {why}')
        sys.exit(1)


def refused(text: str, says: str) -> bool:
    try:
        mp.parse(text.split(','))
    except mp.Invalid as e:
        return says in e.args[0]
    return False


# --- The rules (tests/support_loadout_test.cpp runs the same values through the C++ parser) ---
with open(os.path.join(ROOT, 'src', 'mech_parts.inc'), encoding='utf-8', newline='') as f:
    check(f.read().replace('\r\n', '\n') == gen_mech_parts.render(), 'src/mech_parts.inc is what tools/gen_mech_parts.py writes')
m = mp.parse('eweapon378, NIX_RED ,EMLASER,EMLASER,PBEAM,MIRAGE,-'.split(','))
check(m == mp.Mech('EWEAPON378', 'NIX_RED', ('EMLASER', 'EMLASER', 'PBEAM', 'MIRAGE'), ''), 'a mech parses (any case, spaces)')
check(mp.format_mech(m) == 'EWEAPON378,NIX_RED,EMLASER,EMLASER,PBEAM,MIRAGE,-', 'and formats back')
check(mp.variant(m) == 0x600000185CB2C1D8, 'its variant: the C++ test holds the same number')
check(mp.of_variant(mp.variant(m) | 1 << 63) == m, 'variant round trip (the applied bit ignored)')
check(mp.of_variant(mp.variant(m) | 1 << 40) is None and mp.of_variant(mp.variant(m) & ~mp.MECH) is None,
      'stray bits or no mech bit: no loadout')
for text, says in (('EWEAPON356,-,SABER,-,-,-,-', '等级'), ('EWEAPON378,-,PBEAM,-,-,-,-', '右臂装不了'),
                   ('EWEAPON378,-,-,-,MIRAGE,-,-', '右肩装不了'), ('EWEAPON378,-,-,-,-,-,-', '至少'),
                   ('EWEAPON378,-,EMLASER,-,-,-', '7 项'), ('EWEAPON378,-,EMLASER,-,-,-,-,-', '7 项'),
                   ('EWEAPON999,-,EMLASER,-,-,-,-', '机甲请求'), ('-,-,EMLASER,-,-,-,-', '底座请求'),
                   ('EWEAPON378,PINK,EMLASER,-,-,-,-', '机体'), ('EWEAPON356,-,MACHINEGUN,-,-,-,MPACK_A_WEAPON075', '核心')):
    check(refused(text, says), f'refused, the reason named: {text}')
for r in mp.REQUESTS:
    stock = mp.Mech(r.token, '', tuple('' if s == mp.NONE else s for s in r.slots), '')
    mp.check(stock)
    check(mp.of_variant(mp.variant(stock)) == stock, f'{r.token}: as it comes, a valid loadout')
pairs = [(r.vehicle, mp.f32(r.level)) for r in mp.REQUESTS]
check(len(set(pairs)) == len(pairs), 'no two stock requests share a vehicle and a level (the summon tells them apart so)')
check(all(mp.cents(x.level) == round(x.level * 100) and abs(x.level * 100 - round(x.level * 100)) < 1e-4
          for x in (*mp.WEAPONS, *mp.CORES, *mp.REQUESTS)), 'every level a whole number of hundredths')

# --- With the game: the tables are Root.cpk's ---
try:
    import rootcpk
    game = rootcpk.default()
    game.read('WEAPON', 'WEAPONTABLE.SGO')
except Exception as e:   # no game (CI): the rules above stand
    print(f'mech_parts_test: no game ({e}); the tables not compared with Root.cpk')
    print(f'mech_parts_test: {checks} checks passed')
    sys.exit(0)

import sgo  # noqa: E402


def f32s(v: object) -> object:
    if isinstance(v, list):
        return [f32s(x) for x in v]
    return mp.f32(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else v


table = sgo.load(data=game.read('WEAPON', 'WEAPONTABLE.SGO'))['table']
texts = {lang: sgo.load(data=game.read('WEAPON', f'WEAPONTEXT.{lang}.SGO'))['text_table'] for lang in ('EN', 'SC', 'CN', 'JA')}
frame_files = {f.file for f in mp.FRAMES}
found = {}
for i, row in enumerate(table):
    path = str(row[1]).split('/')[-1]
    try:
        acp = sgo.load(data=game.read('WEAPON', path)).get('Ammo_CustomParameter')
        vehicle = acp[4][2].split('/')[-1].upper()
    except Exception:
        continue
    if vehicle in frame_files:
        found[str(row[0]).upper()] = (i, path.upper(), mp.f32(row[4]), vehicle, acp[4][3])
check(set(found) == set(mp.REQUEST_BY), f'REQUESTS are every stock request of a mech frame: {sorted(set(found) ^ set(mp.REQUEST_BY))}')

carried: dict[str, float] = {}
thrusts: dict[str, list] = {}
for r in mp.REQUESTS:
    i, path, level, vehicle, setup = found[r.token]
    check(path == r.file and vehicle == r.vehicle and level == mp.f32(r.level), f'{r.token}: file, vehicle, level')
    check(mp.FRAME_BY[r.frame].file == r.vehicle, f'{r.token}: its frame is its vehicle')
    check(r.labels == tuple(re.sub(r'\s+', ' ', texts[lang][i][0]).strip() for lang in ('EN', 'SC', 'CN', 'JA')),
          f'{r.token}: its four names are WEAPONTEXT\'s')
    check(f32s(setup[0]) == f32s(r.armor) and f32s(setup[1]) == f32s(r.move) and f32s(setup[3]) == f32s(r.aim) and
          f32s(setup[5]) == f32s(r.tail) and f32s(setup[2]) == f32s(mp.CORE_BY[r.core].thrust), f'{r.token}: its setup numbers')
    rows = [x[0].lower() if isinstance(x, list) and x and isinstance(x[0], str) else None for x in setup[4]]
    want = [None] * mp.ROWS
    for slot, t in enumerate(r.slots):
        if t != mp.NONE:
            w = mp.WEAPON_BY[t]
            want[mp.row_of(w, slot)] = mp.weapon_path(w, slot)
            carried[t] = min(carried.get(t, 99.0), r.level)
    check(rows == want, f'{r.token}: its stock weapons in their slots ({rows} / {want})')
    thrusts.setdefault(repr(f32s(setup[2])), []).append(r)
for w in mp.WEAPONS:
    check(mp.cents(w.level) == mp.cents(carried[w.token]), f'{w.token}: its level the lowest carrying it')
    for slot, side in ((0, w.right), (1, w.left)):
        file = mp.weapon_path(w, slot if w.slot != 'shoulder' else slot + 2).split('/')[-1]
        try:
            data = game.read('WEAPON', file)
        except KeyError:
            data = None
        check((data is not None) == side, f'{w.token}: the {"right" if slot == 0 else "left"} file {file} exists exactly when offered')
check(len(thrusts) == len(mp.CORES), 'one core a distinct thruster block')
for c in mp.CORES:
    same = thrusts[repr(f32s(c.thrust))]
    lowest = min(same, key=lambda r: (mp.cents(r.level), r.token))
    check(c.token == lowest.token and mp.cents(c.level) == mp.cents(lowest.level), f'{c.token}: named after its lowest request')

# The generated mech of each stock request carries that request's own setup (its weapons' recoil the lowest request's).
for r in mp.REQUESTS:
    stock = mp.Mech(r.token, '', tuple('' if s == mp.NONE else s for s in r.slots), '')
    made = mp.mech_sgo(game, stock)
    mp.check_sgo(made, game.read('OBJECT', stock.frame_file()), stock)
    got, want = sgo.load(data=made)['mission_setup'], found[r.token][4]
    check(f32s(got[:4]) == f32s(want[:4]) and f32s(got[5]) == f32s(want[5]), f'{r.token}: the generated setup is the request\'s')
    check([x[0].lower() if isinstance(x, list) and isinstance(x[0], str) else None for x in got[4]] ==
          [x[0].lower() if isinstance(x, list) and x and isinstance(x[0], str) else None for x in want[4]],
          f'{r.token}: the generated weapon rows are the request\'s')
for fr in mp.FRAMES:   # every frame takes the widest loadout
    wide = mp.Mech('MPACK_B_WEAPON109', fr.token, ('HYPERLASER', 'SABER', 'PBEAM', 'POWERDYNE_GC'), 'MPACK_A_WEAPON076')
    mp.check_sgo(mp.mech_sgo(game, wide), game.read('OBJECT', fr.file), wide)
check(struct.calcsize('<f') == 4, 'float32')
print(f'mech_parts_test: {checks} checks passed')
