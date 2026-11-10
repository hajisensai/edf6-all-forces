"""Read-only PE audit of the stock radar's friendly row that mission_crew_radar.cpp filters (测试站 #41).

python tests/mission_crew_radar_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input returns skip code 77.
"""
from pathlib import Path
import re
import struct
import sys

if not __debug__:
    raise RuntimeError('Run without -O')
if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46


def data(rva, size):
    return pe.get_data(rva, size)


def rel32(at, size):
    """The target of a call/jmp/rip operand whose disp32 ends the `size`-byte instruction at `at`."""
    return at + size + struct.unpack('<i', data(at + size - 4, 4))[0]


root = Path(__file__).resolve().parents[1]
source = (root / 'src' / 'mission_crew_radar.cpp').read_text(encoding='utf-8-sig')
body = re.search(r'kWalkCode\[\]=\{([^}]*)\}', source)
assert body, 'kWalkCode not found in production'
code = bytes(int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{2}', body.group(1)))
assert re.search(r'kWalk=0x82B220\b', source)
assert len(code) == 0xE9
assert data(0x82B220, len(code)) == code, 'the production guard is the walk in the DLL'

# The layout the filter mirrors, read from the walk itself.
assert data(0x82B22A, 8) == bytes.fromhex('48c7421800000000')        # list size (+0x18) = 0 first
assert data(0x82B238, 4) == bytes.fromhex('486bd838')                # team row stride 0x38
assert rel32(0x82B23C, 7) == 0x20B2978                               # the team manager pointer
header = re.search(r'kTeamManager=(0x[0-9A-Fa-f]+)', (root / 'src' / 'mission_crew_radar.h').read_text(encoding='utf-8'))
assert header and int(header.group(1), 16) == 0x20B2978
assert data(0x82B243, 4) == bytes.fromhex('488b4838')                # manager +0x38: the team rows
assert data(0x82B24B, 7) == bytes.fromhex('488b1b807b1900')          # head->left; isnil +0x19
assert data(0x82B268, 4) == bytes.fromhex('488b7320')                # node +0x20: the object
assert data(0x82B297, 7) == bytes.fromhex('0f288690000000')          # its +0x90 is the row
assert data(0x82B288, 8) == bytes.fromhex('48c1e10448034f08')        # rows of 16 bytes at +8
assert data(0x82B2A2, 4) == bytes.fromhex('488b4310')                # right +0x10
assert data(0x82B2AC, 4) == bytes.fromhex('488b4308')                # parent +8

# Its callers: the radar update's four rows. 0x82BB50 (team 2, the friendly row) is the one pvp_native.cpp redirects.
calls = {0x82B907: 'lea r8d,[rdi+5]', 0x82B94A: 'mov r8d,4', 0x82BB50: 'mov r8d,2', 0x82BBED: 'lea r8d,[rdi+5]'}
for at in calls:
    assert data(at, 1) == b'\xe8' and rel32(at, 5) == 0x82B220, hex(at)
assert data(0x82BB42, 14) == bytes.fromhex('41b802000000488d5424304' '98bce')
pvp = (root / 'src' / 'pvp_native.cpp').read_text(encoding='utf-8-sig')
assert 'RedirectCall(image+0x82BB50,image+0x82B220' in pvp
friends = pvp[pvp.index('void __fastcall RadarFriends'):]
friends = friends[:friends.index('\n}\n')]
assert friends.index('image+0x82B220') < friends.index('MissionCrewRadarFilter(list,selected)'), \
    'the filter runs after the stock walk filled the row'

text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
blob = text.get_data()
callers = []
for m in re.finditer(b'\xe8', blob):
    at = text.VirtualAddress + m.start()
    if at + 5 + struct.unpack('<i', blob[m.start() + 1:m.start() + 5])[0] == 0x82B220:
        callers.append(at)
assert sorted(callers) == sorted(calls), [hex(c) for c in callers]
print('mission_crew_radar_native: OK (walk 0x82B220 guarded, 4 radar rows, friendly row filtered after the walk)')
