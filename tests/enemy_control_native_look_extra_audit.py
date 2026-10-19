"""Read-only additional family input/target-state audit for supported EDF.dll.

Never loads the native DLL or starts the game. Missing file returns CTest skip 77.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O')
import pefile

root = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
source = (root / 'src/enemy_control_native_look_extra.cpp').read_text(encoding='utf-8-sig')
prints = re.findall(r'\{0x([A-Fa-f0-9]+),\{([^}]+)\}\}', source)
assert len(prints) >= 75, 'family consumer fingerprint coverage missing'
checks = 1
for addr, values in prints:
    expected = bytes(int(v, 16) for v in re.findall(r'0x([A-Fa-f0-9]{2})', values))
    rva = int(addr, 16)
    assert len(expected) == 16 and img[rva:rva + 16] == expected, 'production look signature ' + addr
    checks += 1


def raw(at, expected, reason):
    global checks
    expected = bytes.fromhex(expected)
    assert img[at:at + len(expected)] == expected, reason
    checks += 1


def constant(at, expected, reason):
    global checks
    value = struct.unpack_from('<f', img, at)[0]
    want = struct.unpack('<f', struct.pack('<f', expected))[0]
    assert value == want, reason
    checks += 1


# Independent consumer destinations distinguish persistent target fields from
# interpolated rendering fields, and distinguish enabling from disabling flags.
raw(0x31EF70, 'f30f1087200f0000', 'Deiroi reads native yaw increment F20')
raw(0x31EF80, 'f30f1187440f0000', 'Deiroi integrates target F44')
raw(0x4A9228, 'f30f118b70100000', 'Insect persists angular velocity 1070')
raw(0x4A926A, '488d8b000e0000', 'Insect uses native cling controller E00')
raw(0x4E02C0, '488d8fe00d0000', 'Nephila uses native cling controller DE0')
raw(0x6DB1D2, '488d4b30', 'Cling integrates target quaternion +30')
raw(0x6DAA5E, '0f104140', 'Cling render quaternion +40 is a separate state')
raw(0x36879A, '0f588eb00f0000', 'Large Dango integrates target FB0')
raw(0x3B420B, '0f118bb0100000', 'FlyingDyno integrates target 10B0')
raw(0x3C0BB1, '0f118f00100000', 'ShellFish integrates target 1000')
raw(0x3D62DD, '0f118ec00f0000', 'Spinner integrates target FC0')
raw(0x3E2496, '0f118fc0120000', 'Squid integrates target 12C0')
raw(0x3EE593, 'f68668110000027418', 'TimeShip bit2 set clears native rotation command')
raw(0x3EE5CD, '4c8d86c00f0000', 'TimeShip reads target quaternion FC0')
raw(0x3A4F80, '0f109760170000', 'Merman reads target 1760')
raw(0x3A4F9B, '0f119760170000', 'Merman integrates target 1760')
raw(0x4BCD4C, 'f30f1187f4140000', 'Monster integrates yaw target 14F4')
raw(0x4F772F, '0f118f00060000', 'Carrier integrates target 600')
raw(0x52F337, 'f30f1187d4060000', 'Ufo507 integrates yaw 6D4')
constant(0x1765A84, -.01, 'Deiroi raw yaw scale')
constant(0x17C5560, -.05, 'Insect/Nephila raw yaw scale')
constant(0x1765A0C, .1, 'Insect native velocity interpolation')
constant(0x17A1140, .2, 'FlyingDyno original command scale before optional clamp')
constant(0x176C230, .1, 'Flying families angular command scale')
constant(0x17690F8, .02, 'Merman scale before per-shape DSGO turn rate')
constant(0x1765A00, .01, 'Monster and Carrier native yaw scale')
constant(0x1765A18, .3, 'Shield carrier native turn action start threshold')
print(f'{checks} extra native absolute-look checks passed; {len(prints)} consumer fingerprints; no game execution')
