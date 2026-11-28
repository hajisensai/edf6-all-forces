"""The Vehicler's vehicles (docs/newclass-re/vehicler.md §1.2, §5 M1; pylib/zw_vehicles.py, tools/make_zw_vehicles.py,
tools/gen_zw_vehicles.py): every vehicle row has its vehicle and the other way round, the generated src/zw_vehicles.inc
is current, the files' ledger lifecycle, the Gyrojet's final numbers, and, when Root.cpk is at hand, the built SGOs:
mission_setup == vehicle_setup, class / seats / vtable as the table says, each gun found by the vehicle-weapon scan, the
envelope covering the stock model's bounds. Plain asserts: python tests/zw_vehicles_test.py"""
from pathlib import Path
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
sys.path.insert(0, str(ROOT / 'tools'))
import gamedir  # noqa: E402
import gen_vehicle_weapons  # noqa: E402
import gen_zw_vehicles  # noqa: E402
import ledger  # noqa: E402
import make_zw_vehicles as mzv  # noqa: E402
import zw_vehicles as zv  # noqa: E402
import zw_weapons as zw  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    assert ok, why


# rows <-> vehicles: each vehicle row (category 400, aSupportNone, its texts) names its vehicle, in the registry's order
rows = [w for w in zw.WEAPONS if w.kind == 'vehicle']
check([w.id for w in rows] == ['ZW_V1_HSTVL', 'ZW_V2_M247', 'ZW_V3_MBT70'], 'vehicle rows, in order')
check([w.vehicle for w in rows] == [v.row for v in zv.VEHICLES] == [w.id for w in rows], 'row -> vehicle')
for w in rows:
    check(w.category == 400 and w.template == zw.NONE_TEMPLATE and set(w.text) == set(zw.LANGS), f'{w.id}: a V row')
check(zw.BY_ID['ZW_VehicleNone'].kind == 'none', 'ZW_VehicleNone stays: on foot')
check(all(w.vehicle == '' for w in zw.WEAPONS if w.kind != 'vehicle'), 'only vehicle rows name a vehicle')
check(zw.RELEASED['vehicler stage 2 (2026-11-28)'][0] == len(zw.IDS) == 8, 'stage 2 released at 8 rows')

# the registry: class known, guns one per stock weapon, files once each, the V3 todo kept, placeholders marked
check(mzv.OWNER in ledger.OWNERS, 'zwvehicle is a ledger owner')
for v in zv.VEHICLES:
    check(v.cls in zv.CLASS_VTABLES and v.seats >= 1 and all(x > 0 for x in v.envelope), f'{v.row}: spec')
    check(v.state == 'placeholder', f'{v.row}: stage 2 model is a placeholder')
    check(v.logistics_cost > 0 and v.speed > 0 and v.durability > 0, f'{v.row}: numbers')
check(any('液压' in t for t in zv.BY_ROW['ZW_V3_MBT70'].todo), 'V3: hydraulic suspension left as todo')
check(all(g.values['LockonTargetType'] == zv.MARK_AIR for g in zv.BY_ROW['ZW_V2_M247'].guns), 'V2: radar air mark')
check(zv.BY_ROW['ZW_V1_HSTVL'].speed > max(v.speed for v in zv.VEHICLES if v.row != 'ZW_V1_HSTVL'), 'V1 fastest')
check(len(zv.files()) == len(set(zv.files())), 'files once each')

# the generated table is current and says the registry
check(gen_zw_vehicles.render() == (ROOT / 'src' / 'zw_vehicles.inc').read_text(encoding='utf-8').replace('\r\n', '\n'),
      'src/zw_vehicles.inc is current (python tools/gen_zw_vehicles.py)')
inc = gen_zw_vehicles.render()
for v in zv.VEHICLES:
    check(f'L"{v.row}",L"{v.path}",L"{v.sgo.upper()}.SGO",0x{v.vtable:X}u,{v.seats}u' in inc, f'{v.row}: in the table')

# the Gyrojet's final numbers: six single shots, a round that speeds up (accel > 0) to about 280 m
check(zw.GYROJET['AmmoCount'] == 6.0 and zw.GYROJET_CUSTOM[3] > 0, 'gyrojet: 6 rounds, accelerating')
check(250.0 <= zw.gyrojet_range() <= 300.0, f'gyrojet range {zw.gyrojet_range():.0f} m')
check(not any('（试作型）' in w.text['SC'][0] for w in zw.WEAPONS if w.id == 'ZW_Gyrojet01'), 'gyrojet: final name')

# ledger lifecycle in a scratch game dir: write, refresh with one fewer (released), remove
with tempfile.TemporaryDirectory(prefix='edf6vc-zwv-') as game:
    files = {rel: f'fake {rel}'.encode() for rel in zv.files()}
    written = mzv.install(game, files)
    check(len(written) == len(files) and all(os.path.isfile(p) for p in written), 'install writes every file')
    dropped = zv.files()[-1]
    again = mzv.install(game, {k: b for k, b in files.items() if k != dropped})
    check(len(again) == len(files) - 1 and not os.path.exists(written[-1]), 'refresh releases what it no longer writes')
    deleted, kept = mzv.remove(game)
    check(len(deleted) == len(files) - 1 and not kept and not any(os.path.exists(p) for p in written), 'remove all')

game_root = gamedir.find()
if game_root is None:
    print('zw_vehicles: no game here, Root.cpk part skipped')
else:
    import dsgo  # noqa: E402
    import jet_models  # noqa: E402
    import rootcpk  # noqa: E402
    import sgo  # noqa: E402
    from mdb import mdb_read, rab_read  # noqa: E402
    built = mzv.build(game_root)
    check(list(built) == zv.files(), 'build writes every registered file')
    game = rootcpk.Game(game_root)
    for v in zv.VEHICLES:
        m = sgo.load(data=built[v.rel])
        check(m['xgs_scene_object_class'] == v.cls and zv.CLASS_VTABLES[m['xgs_scene_object_class']] == v.vtable,
              f'{v.row}: class / vtable')
        check(len(m['vehicle_riding_position']) == v.seats, f'{v.row}: seats')
        check(m['mission_setup'] == m['vehicle_setup'], f'{v.row}: mission_setup == vehicle_setup')
        setup = m['vehicle_setup']
        check(abs(setup[1][1] - v.speed) < 1e-4 and abs(setup[1][2] - v.force) < 1e-6, f'{v.row}: speed / force')
        check(m['game_object_durability'] == v.durability, f'{v.row}: durability')
        check(gen_vehicle_weapons.weapons_of(built[v.rel]) == [g.path for g in v.guns], f'{v.row}: weapon scan')
        for g in v.guns:
            gm = sgo.load(data=built[g.rel])
            check(gm['xgs_scene_object_class'] == 'Weapon_VehicleShoot', f'{g.file}: a vehicle gun')
            check(all(abs(gm[k] - x) < 1e-4 for k, x in g.values.items()), f'{g.file}: values')
            check(all(gm[zv.NAME_KEYS[lang]] == g.names[lang] for lang in zv.LANGS), f'{g.file}: names')
        # the envelope covers the stock model's bind-pose bounds (rounded out)
        ref = m['animation_model'][0]
        rab = rab_read(game.read('OBJECT', ref[0].split('/')[-1]))
        model = mdb_read(next(f.data for f in rab.files if f.name.lower() == ref[1].lower()))
        lo, hi = jet_models.bbox(jet_models.bind_positions(model))
        half_w, half_l, height = v.envelope
        check(max(-lo[0], hi[0]) <= half_w and max(-lo[2], hi[2]) <= half_l and hi[1] <= height,
              f'{v.row}: envelope {v.envelope} covers {lo} .. {hi}')
    # the vehicle rows' SGOs: inert accessories named as the vehicle, custom_parameter empty
    import call_weapons as cw  # noqa: E402
    sgos = zw.build(lambda rel: cw.stock(game_root, rel))
    for w in rows:
        r = dsgo.parse(sgos[zw.sgo_file(w)]).root
        check(r.get('xgs_scene_object_class') == 'Weapon_Accessory' and dsgo.to_py(r.get('custom_parameter')) == [],
              f'{w.id}: inert accessory')
        check(r.get('name.sc') == w.text['SC'][0], f'{w.id}: named')
    g = dsgo.parse(sgos[zw.sgo_file(zw.BY_ID['ZW_Gyrojet01'])]).root
    check(dsgo.to_py(g.get('Ammo_CustomParameter')) == zw.GYROJET_CUSTOM and g.get('AmmoSpeed') == zw.GYROJET['AmmoSpeed'],
          'gyrojet SGO: accelerating rocket read back')
print(f'zw_vehicles: {checks} checks passed')
