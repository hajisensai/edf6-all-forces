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
VEHICLE_CLASSES = {'Vehicle505_Tank', 'Vehicle403_Tank', 'Vehicle404_Tank', 'Vehicle603_Flak', 'Vehicle510_Maser',
                   'Vehicle501_FortressRobo', 'Vehicle507_Rescuetank', 'Vehicle_Car', 'Vehicle503_Bike',
                   'Vehicle504_begaruta', 'VehicleBigBegaruta', 'Vehicle612_nix'}


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
    assert len(sigs) == 12, sorted(sigs)
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
    rows = re.findall(r'\{Category::(\w+),How::(\w+),"([^"]+)",L"[^"]*",L"app:/object/([^"]+)"', header)
    assert rows, 'no stock rows read'
    for category, how, ident, name in rows:
        members = sgo.load(data=game.read('OBJECT', name))
        cls = members.get('xgs_scene_object_class')
        assert name.upper().startswith(ident.upper()), (ident, name)
        if how == 'stockVehicle':
            assert category == 'vehicle' and cls in VEHICLE_CLASSES, (name, cls)
            assert members.get('mission_setup'), f'{name}: no mission_setup'
        else:
            assert how == 'enemy' and category == 'enemy' and cls and not str(cls).startswith('Vehicle'), (name, cls)
        checks += 2
        print(f'{name}: {cls}{" (mission_setup)" if how == "stockVehicle" else ""}')
    print(f'debug_spawn_resource_audit: {checks} checks passed (read-only Root.cpk / EDF.dll)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
