"""Read-only ground-support resource and native-profile audit. No DLL is loaded or code executed.

Run with --game DIR to select Root.cpk/EDF.dll; otherwise use normal game discovery.
Exit 77 if the optional local game data is absent, never download substitute assets.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import rootcpk
import sgo
from mdb import rab_read, mdb_read
from jet_models import bind_positions, bbox
from vcobjects import mab_locator


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path(rootcpk.DEFAULT_GAME))
    args = parser.parse_args()
    if not (args.game / 'Root.cpk').is_file() or not (args.game / 'EDF.dll').is_file():
        print('SKIP: optional local Root.cpk/EDF.dll not available')
        return 77
    import pefile
    pe = pefile.PE(str(args.game / 'EDF.dll'), fast_load=True)
    assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
    base = pe.OPTIONAL_HEADER.ImageBase
    source = (ROOT / 'src/support_spawn.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/support_spawn.h').read_text(encoding='utf-8')
    signatures = dict(re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]+)\}', source))
    addresses = dict((k, int(v, 16)) for k, v in re.findall(r'(k\w+)=(0x[0-9A-Fa-f]+)', source))
    addresses['kSetTeam'] = 0x54EE70
    checks = 0
    for name, text in signatures.items():
        data = bytes(int(x.strip(), 16) for x in text.split(','))
        assert pe.get_data(addresses[name[:-3]], len(data)) == data, name
        checks += 1
    game = rootcpk.Game(str(args.game))
    rows = re.findall(r'L"app:/Object/([^"]+)",(\d+),([\d.]+)f,([\d.]+)f,([\d.]+)f,(0x[0-9A-Fa-f]+)', header)
    assert len(rows) == 3
    expected = [('Vehicle505_Tank', 0x61B060), ('Vehicle507_Rescuetank', 0x61C430), ('Vehicle_Car', 0x65A910)]
    for row, (native_class, setup_rva) in zip(rows, expected):
        name, seats, half_width, half_length, height, vtable = row
        raw = game.read('OBJECT', name)
        _, members = sgo.read(raw)
        assert members['xgs_scene_object_class'] == native_class
        assert len(members['vehicle_riding_position']) == int(seats)
        assert isinstance(members['mission_setup'], list) and members['mission_setup']
        # Both the boarding/exit door and every sitting locator must really exist in this stock MAB.
        # This is not permission to append more seats or claim a drill transport animation.
        for seat in members['vehicle_riding_position']:
            for locator in seat[:2]:
                position, record = mab_locator(members['animation_model'][2], locator)
                assert position > 0 and record > 0
                checks += 1
        ref = members['animation_model'][0]
        model = mdb_read(next(f.data for f in rab_read(game.read('OBJECT', ref[0].split('/')[-1])).files
                             if f.name.lower() == ref[1].lower()))
        low, high = bbox(bind_positions(model))
        assert max(abs(low[0]), abs(high[0])) <= float(half_width)
        assert max(abs(low[2]), abs(high[2])) <= float(half_length)
        assert high[1] <= float(height) and low[1] >= -0.001
        native_setup = struct.unpack('<Q', pe.get_data(int(vtable, 16) + 46 * 8, 8))[0] - base
        assert native_setup == setup_rva
        checks += 6
        print(f'{name}: {seats} real seats; door/seat locators present; bound {low}..{high}; setup {setup_rva:#x}')
    # The spawn path must stay independent of the dummy constructor, including empty delivery.
    executable_source = re.sub(r'//[^\n]*', '', source)
    assert not re.search(r'\b(?:SeatNpcRider|kRideAi|kSlotRideAi)\b', executable_source)
    print(f'support_resource_audit: {checks + 1} checks passed (read-only resources/native bytes)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
