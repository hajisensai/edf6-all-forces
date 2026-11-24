"""The vehicles' weapon SGOs the plugin queues with them (src/vehicle_weapons.h), on the installed game's real SGOs.

Every vehicle row of the spawn catalog (src/spawn_catalog.inc), its SGO read as the game loads it (Root.cpk's own, the
installed Mods copy, the bundled EDF4.1 objects, or the range's derived one as its install writes it), is scanned by the
plugin's own scan (vehicle_weapons_test --scan, src/vehicle_weapons_logic.h ScanSgo):
- a stock row's scan is its src/vehicle_weapons.inc list (the generated table and the run-time scan agree);
- every weapon file the SYNC LOAD probe saw read on the game thread in the hitch2 test range run (41 reads: 27 stock
  V_*, 14 of the plugin's EDF6VC_*) is in the union: each is now queued with its vehicle.

  python tests/vehicle_weapons_native_audit.py EXE [--game DIR]     exit 77 without the game's Root.cpk
"""
from pathlib import Path
import argparse
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import rootcpk  # noqa: E402

# The hitch2 SYNC LOAD list (weapon file names, lower case, no extension).
HITCH2 = '''
edf6vc_katyusha_rockets edf6vc_howitzer_l edf6vc_howitzer_r edf6vc_drill_bit edf6vc_sz_rifle edf6vc_sz_missile
edf6vc_vulcan_he_2000 edf6vc_vulcan_ap_2000 edf6vc_bmpt_l edf6vc_bmpt_r edf6vc_bmpt_atgm_4 edf6vc_bmpt_ap_150
edf6vc_bmpt_ap_r_150 edf6vc_bmpt_atgmf_4
v_fuel01 v_503_bike_gun_l v_503_bike_gun_r v_506heli_gatling01_l v_506heli_gatling01_r v_506heli_missile01
v_409heli_gatling01 v_409heli_missile01 v_410heli_gatling01 v_602heli_gatling01_l v_602heli_gatling01_r
v_602heli_missile01 v_403tank_machinegun v_404bigtank_maincannon v_404bigtank_subcannon v_404bigtank_gatling
v_404bigtank_sidegrenade_l v_404bigtank_sidegrenade_r v_502_groundrobo_gatling v_502_groundrobo_cannon01_l
v_502_groundrobo_cannon01_r v_510_maser_thunder_mission v_407bigbegaruta_cannon v_407bigbegaruta_missile
v_505tank_cannon01
'''.split()


def base(path: str) -> str:
    return path.rsplit('/', 1)[-1].rsplit('.', 1)[0].lower()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('exe', type=Path)
    parser.add_argument('--game', type=Path, default=Path(rootcpk.DEFAULT_GAME))
    args = parser.parse_args()
    if not (args.game / 'Root.cpk').is_file():
        print('SKIP: the game\'s Root.cpk is not here')
        return 77
    assert len(HITCH2) == 39, len(HITCH2)   # the 39 SGO files hitch2-handover.md names for the 41 reads
    sys.path.insert(0, str(ROOT / 'tools'))
    sys.path.insert(0, str(ROOT / 'testrange'))
    import edf41_objects  # noqa: E402
    import gen  # noqa: E402
    game = rootcpk.Game(str(args.game))
    stock = {name: [w for w in weapons.split('\\0') if w]   # the C literal's \0
             for name, weapons in re.findall(r'^\s*\{L"([^"]+)",L"([^"]*)"\},', (ROOT / 'src/vehicle_weapons.inc').read_text(
                 encoding='utf-8'), re.MULTILINE)}
    rows = re.findall(r'\{Category::\w+,How::vehicle,"([^"]+)",L"[^"]*",L"app:/object/([^"]+)",[^,]*,[^,]*,(true|false),',
                      (ROOT / 'src/spawn_catalog.inc').read_text(encoding='utf-8'))
    assert rows, 'no vehicle row in src/spawn_catalog.inc'
    mods = args.game / 'Mods'
    union: set[str] = set()
    checks = skipped = 0
    with tempfile.TemporaryDirectory() as scratch:
        for ident, name, mod in rows:
            stem = name.rsplit('.', 1)[0].upper()
            rel = f'OBJECT/{stem}.SGO'
            if mod == 'false':
                data = game.read('OBJECT', name)
            elif (mods / rel).is_file():
                data = (mods / rel).read_bytes()
            elif rel in edf41_objects.registry()['files']:
                data = edf41_objects.read(rel)
            elif ident.startswith('EDF6TR_'):
                data = gen.vehicle_sgo(gen.Game(str(args.game)), stem.lower())
            else:
                print(f'{name}: not installed here, not checked')
                skipped += 1
                continue
            file = Path(scratch) / f'{stem}.SGO'
            file.write_bytes(data)
            out = subprocess.run([str(args.exe), '--scan', str(file)], capture_output=True, text=True, check=True).stdout
            scanned = list(dict.fromkeys(line.strip() for line in out.splitlines() if line.strip()))
            if mod == 'false':
                assert stem in stock, f'{stem}: a stock vehicle row without a src/vehicle_weapons.inc row'
                # the same files (the pool's order is not the tree's)
                assert {w.lower() for w in scanned} == {w.lower() for w in stock[stem]}, (stem, scanned, stock[stem])
                checks += 1
            union.update(base(w) for w in scanned)
            checks += 1
    lacking = [w for w in HITCH2 if w not in union]
    assert not lacking, f'hitch2 weapon files no vehicle row queues: {lacking}'
    checks += len(HITCH2)
    print(f'vehicle_weapons_native_audit: {checks} checks passed ({len(rows)} vehicle rows, {skipped} not installed here; '
          f'all {len(HITCH2)} hitch2 weapon files queued with their vehicles)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
