"""Read-only absolute-look input/angle integration audit for supported EDF.dll.

No DLL loading or game execution. Missing explicit file returns CTest skip 77.
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

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
root = Path(__file__).resolve().parents[1]
source = (root / 'src/enemy_control_native_look.cpp').read_text(encoding='utf-8-sig')
prints = re.findall(r'\{0x([A-Fa-f0-9]+),\{([^}]+)\}\}', source)
assert len(prints) >= 11, 'native look consumer fingerprints missing'
checks = 1
for addr, values in prints:
    want = bytes(int(x, 16) for x in re.findall(r'0x([A-Fa-f0-9]{2})', values))
    rva = int(addr, 16)
    assert len(want) == 16 and img[rva:rva + 16] == want, 'production native look signature ' + addr
    checks += 1


def raw(at, expected, why):
    global checks
    want = bytes.fromhex(expected)
    assert img[at:at + len(want)] == want, why
    checks += 1


# Independent fixed instructions identify the state being fed back: commands
# integrate into target angles, then native interpolation changes rendered angles.
raw(0x53D605, '0f119950140000', 'Walking target-angle vector destination +1450')
raw(0x53D657, '0f5cd9', 'Walking interpolation subtracts actual from target')
raw(0x53D677, '0f119940140000', 'Walking rendered angles are +1440, not target +1450')
raw(0x53E867, 'f30f1183a4120000', 'Walking yaw command stores negated pad3C at +12A4')
raw(0x470E2F, '410f57c3', 'Humanoid yaw negates pad3C')
raw(0x472316, '0f588fd0100000', 'Humanoid adds command to target +10D0')
raw(0x472329, '0f118fd0100000', 'Humanoid target-angle vector destination +10D0')
raw(0x4723BB, '0f118fe0100000', 'Humanoid rendered-angle vector destination +10E0')
raw(0x4E1A0, '0f57351989be01', 'Native Euler pitch is negative elevation')
assert struct.unpack_from('<4I', img, 0x1C36AC0) == (0x80000000,) * 4, 'Native sign vector'
checks += 1
assert struct.unpack_from('<4f', img, 0x176B050) == (struct.unpack('<f', struct.pack('<f', 1 / 30))[0],) * 4
checks += 1
assert struct.unpack_from('<4f', img, 0x1C36A70) == (0.5,) * 4, 'Humanoid native interpolation'
checks += 1
print(f'{checks} native absolute-look checks passed; {len(prints)} consumer fingerprints; no game execution')
