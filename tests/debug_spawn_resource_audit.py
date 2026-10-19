"""Read-only audit of the debug spawn tool against the installed game. No DLL is loaded or code executed.

- every native signature src/debug_spawn.cpp checks at run time is the installed EDF.dll's (TimeDateStamp 0x678CCB46), and
  the call sites name the type descriptors and functions the tool calls (the script's CreateEnemy / CreateVehicle2 casts,
  CreateEnemy's team 1 and its activation call, the __RTDynamicCast thunk);
- every stock SGO of src/debug_spawn.h is in Root.cpk; a ground vehicle's has the mission_setup block ApplyMissionSetup
  reads (a call-in SGO without it crashes the game, testrange/gen.py) and a vehicle class; an enemy's is no vehicle.

Run with --game DIR; otherwise the normal game discovery. Exit 77 when the local game data is absent.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import rootcpk  # noqa: E402
import sgo  # noqa: E402

# The game's vehicle classes the stock rows use (xgs_scene_object_class); a new row's class must be added knowingly.
VEHICLE_CLASSES = {'Vehicle505_Tank', 'Vehicle601_Tank', 'Vehicle403_Tank', 'Vehicle404_Tank', 'Vehicle603_Flak', 'Vehicle510_Maser',
                   'Vehicle501_FortressRobo', 'Vehicle507_Rescuetank', 'Vehicle_Car', 'Vehicle503_Bike',
                   'Vehicle504_begaruta', 'VehicleBigBegaruta', 'Vehicle612_nix',
                   # 2026-10-19: the NPC-crewed kinds' `_ai` SGOs placed empty (testrange/catalog.py STOCK_VEHICLES)
                   'Vehicle601_Tank', 'Vehicle607_RoboTruck', 'Vehicle60X_Truck', 'Vehicle402_Rocket'}


def rel32_target(pe, at: int, length: int) -> int:
    """The target of the rel32 at the end of the instruction `length` bytes long at `at`."""
    disp = struct.unpack('<i', pe.get_data(at + length - 4, 4))[0]
    return at + length + disp


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path(rootcpk.DEFAULT_GAME))
    args = parser.parse_args()
    if not (args.game / 'Root.cpk').is_file() or not (args.game / 'EDF.dll').is_file():
        print('SKIP: optional local Root.cpk/EDF.dll not available')
        return 77
    import pefile
    pe = pefile.PE(str(args.game / 'EDF.dll'), fast_load=True)
    assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
    source = (ROOT / 'src/debug_spawn.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/debug_spawn.h').read_text(encoding='utf-8')
    checks = 0
    sigs = dict(re.findall(r'const unsigned char (k\w+)Sig\[\]=\{([^}]+)\}', source))
    addresses = dict((k, int(v, 16)) for k, v in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)', source))
    addresses['kSetTeam'] = 0x54EE70   # crew.h
    assert len(sigs) == 17, sorted(sigs)
    for name, text in sigs.items():
        data = bytes(int(x.strip(), 16) for x in text.split(','))
        assert pe.get_data(addresses[name], len(data)) == data, name
        checks += 1
    # The call sites say what the tool relies on: the casts' type descriptors and the thunk, the activation's callee.
    def lea(at: int) -> int:
        return rel32_target(pe, at, 7)
    for site, target in ((addresses['kEnemyCastSite'], addresses['kGameObjectType']),
                         (addresses['kVehicleCastSite'], addresses['kVehicleType'])):
        assert lea(site) == target and lea(site + 7) == addresses['kSceneObjectType'], hex(site)
        call = site + (20 if site == addresses['kEnemyCastSite'] else 19)
        assert rel32_target(pe, call, 5) == addresses['kCast'], hex(call)
        checks += 3
    assert rel32_target(pe, addresses['kActivateSite'] + 13, 5) == addresses['kActivate']
    checks += 1
    # An empty vehicle is CreateVehicle2's: its level through 0xD7AE0 and its setup through 0x633280 (never SetLevel,
    # whose tail 0x54E902 puts the object on the native AI list: tests/debug_spawn_vehicle_level_guard.py).
    site = addresses['kCreateVehicleLevelSite']
    assert rel32_target(pe, site + 10, 5) == addresses['kLevelScale'], hex(site)
    assert rel32_target(pe, site + 48, 5) == addresses['kVehicleLevelSetup'], hex(site)
    assert rel32_target(pe, 0x54E902, 5) == 0x118A4B0, 'SetLevel no longer enters the AI list where expected'
    # The loading on demand asks the manager the script's IsCompleteResource asks (0x70ED20: mov rcx,[mgr]; jmp).
    assert rel32_target(pe, addresses['kLoadDoneScript'] + 7, 5) == addresses['kLoadDone']
    assert rel32_target(pe, addresses['kLoadDoneScript'], 7) == addresses['kPreloadMgr']
    checks += 5
    for name, cls in (('kSceneObjectType', b'.?AVSceneObject@game@xgs@@'), ('kGameObjectType', b'.?AVGameObjectBase@@'),
                      ('kVehicleType', b'.?AVVehicleBase@@')):
        assert pe.get_data(addresses[name] + 0x10, len(cls)) == cls, name
        checks += 1
    # The thunk jumps through the import table to vcruntime's __RTDynamicCast.
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
    slot = rel32_target(pe, addresses['kCast'], 6)
    names = [i.name for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.address - pe.OPTIONAL_HEADER.ImageBase == slot]
    assert names == [b'__RTDynamicCast'], names
    checks += 1

    game = rootcpk.Game(str(args.game))
    # The object rows: src/spawn_catalog.inc (tools/gen_spawn_catalog.py from testrange/catalog.py), included by the header.
    assert '#include "spawn_catalog.inc"' in header, 'the header no longer takes the generated rows'
    catalog = (ROOT / 'src/spawn_catalog.inc').read_text(encoding='utf-8')
    rows = re.findall(r'\{Category::(\w+),How::(\w+),"([^"]+)",L"[^"]*",L"app:/object/([^"]+)",\d+,[0-9.]+f,(true|false),(true|false),(?:nullptr|L"[^}]*")\}',
                      catalog)
    assert rows and not re.findall(r'L"app:/object/', header), 'object rows outside the generated file'
    sys.path.insert(0, str(ROOT / 'tools'))
    sys.path.insert(0, str(ROOT / 'testrange'))
    import edf41_objects  # noqa: E402  (the bundled, pre-converted 4.1 objects: no EDF4.1 needed)
    import gen  # noqa: E402
    import catalog as spawn_catalog  # noqa: E402
    row_definitions = {row.ident: row for row in spawn_catalog.rows()}
    mods = args.game / 'Mods'
    skipped = 0
    for category, how, ident, name, mod, _preload in rows:
        rel = f'OBJECT/{name.split(".")[0].upper()}.SGO'
        expected = row_definitions.get(ident)
        assert expected is not None and expected.rel == rel, (ident, name, expected)
        if mod == 'false':                                   # Root.cpk's own
            data, where = game.read('OBJECT', name), 'Root.cpk'
        elif (mods / rel).is_file():                          # installed: what the game loads
            data, where = (mods / rel).read_bytes(), 'Mods'
        elif rel in edf41_objects.registry()['files']:        # EDF4.1's, as the installer ships it
            data, where = edf41_objects.read(rel), 'edf41port'
        elif ident.startswith('EDF6TR_'):                    # the range's own, as its install writes it
            data, where = gen.vehicle_sgo(gen.Game(str(args.game)), name.split('.')[0].lower()), 'testrange/gen.py'
        else:
            print(f'{name}: not installed here, not checked')
            skipped += 1
            continue
        members = sgo.load(data=data)
        cls = members.get('xgs_scene_object_class')
        if how == 'vehicle':
            assert category in ('vehicle', 'ours'), (name, category)
            assert cls in VEHICLE_CLASSES if mod == 'false' else str(cls).startswith('Vehicle'), (name, cls)
            assert members.get('mission_setup'), f'{name}: no mission_setup'
        else:
            assert how == 'enemy' and category in ('enemy', 'enemy41') and cls and not str(cls).startswith('Vehicle'), (name, cls)
        checks += 2
        print(f'{name}: {cls}{" (mission_setup)" if how == "vehicle" else ""} [{where}]')
    print(f'debug_spawn_resource_audit: {checks} checks passed, {skipped} rows not installed here '
          '(read-only Root.cpk / EDF.dll / Mods / edf41port)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
