"""Read-only audit of controlled-enemy PvP native ownership contracts.

The DLL is parsed as data, never loaded. This verifies shipped production guards
against the supported native build, plus independent team/owner consumers and
new non-catalog part RTTI. It does not claim game or multiplayer acceptance.
"""
from pathlib import Path
import argparse
import re
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if not args.edf_dll or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll')
    raise SystemExit(77)
import pefile
import capstone
pe = pefile.PE(str(args.edf_dll), fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported native profile'
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
root = Path(__file__).resolve().parents[1]
checks = 0

def expect(at, pattern):
    global checks
    raw = bytes.fromhex(pattern)
    assert pe.get_data(at, len(raw)) == raw, f'native contract changed at {at:#x}'
    checks += 1

def call(at, target):
    global checks
    raw = pe.get_data(at, 5)
    assert raw[0] == 0xE8 and at + 5 + struct.unpack_from('<i', raw, 1)[0] == target, (hex(at), hex(target))
    checks += 1

def name_at(table, name):
    global checks
    col = struct.unpack('<Q', pe.get_data(table - 8, 8))[0] - base
    signature, offset, cd_offset, td, _, self_rva = struct.unpack('<6I', pe.get_data(col, 24))
    assert (signature, offset, cd_offset, self_rva) == (1, 0, 0, col), (hex(table), 'not complete primary object')
    expected = f'.?AV{name}@@\0'.encode('ascii')
    assert pe.get_data(td + 16, len(expected)) == expected, (hex(table), name)
    checks += 1

# Read the guarded bytes from production, not from a copied test image. Support
# both Anchor layouts used by the independent typed family modules.
files = ['enemy_control_pvp_native.cpp', 'enemy_control_pvp_boss.cpp',
         'enemy_control_pvp_transport.cpp', 'enemy_control_pvp_berserker.cpp']
for filename in files:
    source = (root / 'src' / filename).read_text(encoding='utf-8-sig')
    rows = re.findall(r'\{(0x[0-9A-Fa-f]+),\{([^}]+)\},(\d+)\}', source)
    rows += [(at, values, size) for at, size, values in
             re.findall(r'\{(0x[0-9A-Fa-f]+),(\d+),\{([^}]+)\}\}', source)]
    assert len(rows) >= 10, f'no complete production guard set: {filename}'
    for at, values, size in rows:
        value = bytes(int(v.strip(), 0) for v in values.split(','))
        length = int(size)
        assert 0 < length <= len(value), (filename, at, 'invalid guard length')
        expect(int(at, 0), value[:length].hex())

# Independent dispatch/consumer evidence: SetTeam only updates this GameObject;
# old small battery uses owner314, newer GunBattery2 uses owner318 each shot.
expect(0x6D067C, '8b 91 14 03 00 00')
expect(0x6D0682, '89 90 90 00 00 00')
expect(0x6D2A46, '8b 83 18 03 00 00')
expect(0x6D2A4C, '89 85 10 02 00 00')
expect(0x6D31BD, 'ff 50 60')  # Existing held bullet is kept alive, no new factory/team copy.
call(0x6D5734, 0x6D2640)
call(0x3525A3, 0x1194280)
call(0x3526B9, 0x351660)
expect(0x34045F, 'f6 43 18 04')  # Deleted-before-publish RocketPunch has a real safe caller exit.
expect(0x118A273, '80 4b 18 04')
call(0x44884F, 0x474F90)  # Independent Boss meteor publish boundary consumes the by-value weak.
call(0x6E32D1, 0x11945E0)
call(0x1CAF7D, 0x1D8900)  # Capsule safe strong-output wrapper, not null-unsafe component constructor.
call(0x3574F9, 0x11945E0)  # Bommer safe factory caller, before any body construction.
call(0x3528B6, 0x353290)  # Constructor late BulletBase callbacks also need a null-rejection guard.
call(0x352941, 0x353420)
expect(0x1194949, '4c 8d 44 24 48 48 8d 55 b7')
expect(0x1194931, '74 22')  # Incoming no-callback branch lands at 1194955; do not patch that destination.
expect(0x1194952, 'ff 50 10 48 8b 4c 24 38')
expect(0x1194966, 'f6 41 18 04')
expect(0x232D20, '48 63 8e 00 0a 00 00 83 f9 ff 74 37')
expect(0x4A028E, 'ff 50 60')  # Humanoid virtual keepalive, rather than another factory call.
expect(0x4A0272, '83 ba 30 0c 00 00 01')
expect(0x4A027B, '8b 82 34 0c 00 00 c1 e8 0e 24 01')

# Scene-owned structural parts are real GameObjects; their ordinary Contact
# controls and small launchers are components and never receive GOB SetTeam.
for table, name in [
    (0x17AAD48, 'DeiroiFootParts_Base'),
    (0x17AB118, 'DeiroiFootParts_Battery'),
    (0x17AB4B0, 'DeiroiFootParts_Joint'),
    (0x17AB770, 'DeiroiFootParts_Toe'),
    (0x17AE460, 'EDF6_Berserker_Equipment_RocketPunch'),
    (0x17AD518, 'EDF6_Berserker_Equipment_Bomb'),
    (0x17BFE58, 'Humanoid_BigGreyBoss_Meteor'),
]:
    name_at(table, name)

print(f'enemy controlled PvP native audit: {checks} guards/consumers/typed identities verified; no DLL execution')
