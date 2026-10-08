"""Optional supported EDF.dll native Proteus collision-size audit.
Maps the DLL privately without DllMain; calls full native InitParam copy and extracts
its exact sphere-radius arithmetic block. No game process or installation writes.
This is not a full bullet controller or live collision/gameplay test.
"""
import argparse
import ctypes as C
from pathlib import Path
import struct
import re
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if args.edf_dll is None:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
    import gamedir
    game = gamedir.find_or_dev()
    args.edf_dll = Path(game) / 'EDF.dll' if game else None
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or not args.edf_dll or not args.edf_dll.is_file():
    print('SKIP: requires Windows x64 and supported EDF.dll')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: native preconditions must remain enabled')
import pefile
pe = pefile.PE(str(args.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
k.VirtualAlloc.restype = C.c_void_p
base = k.LoadLibraryExW(str(args.edf_dll.resolve()), None, 1)
assert base
copy = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)(base + 0x2307F0)
# BulletControl::Init: AmmoHitSizeAdjust * AmmoSize, before the Havok sphere stores it at +BE0.
block = pe.get_data(0x231EEF, 16)
assert block == bytes.fromhex('f30f1083340a0000f30f5983300a0000')
code = bytes.fromhex('534889cb') + block + bytes.fromhex('5bc3')
adapter = k.VirtualAlloc(None, len(code), 0x3000, 0x40)
assert adapter
C.memmove(adapter, code, len(code))
radius = C.WINFUNCTYPE(C.c_float, C.c_void_p)(adapter)
source = (Path(__file__).resolve().parents[1] / 'src' / 'jet_bay.cpp').read_text(encoding='utf8')
function = source.split('bool ProteusSalvoRound(', 1)[1].split('void ProteusRoundsReady(', 1)[0]
match = re.search(r'Put<float>\(o\+kDemoIfc,kIfcAmmoSize,([0-9.]+)f\)', function)
assert match, 'production Proteus salvo must specialize the IFC collision size'
fixed_size = float(match.group(1))
# Verify this profile against the actual stock weapon, not a guessed smaller radius.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import rootcpk
import dsgo
weapon = dsgo.parse(rootcpk.Game(str(args.edf_dll.parent)).read('WEAPON', 'V_407BIGBEGARUTA_MISSILE.SGO')).root
assert abs(weapon.get('AmmoSize') - fixed_size) < 1e-5
assert weapon.get('AmmoHitSizeAdjust') == 1.0
ifc = C.create_string_buffer(0x600)
core = C.create_string_buffer(0xE00)
for size in (10.0, fixed_size):
    C.memset(C.addressof(ifc), 0, C.sizeof(ifc))
    C.memset(C.addressof(core), 0, C.sizeof(core))
    struct.pack_into('<H', ifc, 0x70 + 0xC8, 0xFFFF)
    struct.pack_into('<H', core, 0x9A0 + 0xC8, 0xFFFF)
    struct.pack_into('<f', ifc, 0x100, size)
    struct.pack_into('<f', ifc, 0x104, 1.0)
    struct.pack_into('<f', ifc, 0xF0, 25.0)
    struct.pack_into('<f', ifc, 0xDC, 150.0)
    copy(C.addressof(core) + 0x9A0, C.addressof(ifc) + 0x70)
    assert abs(radius(C.addressof(core)) - size) < 1e-5
    assert struct.unpack_from('<f', core, 0xA20)[0] == 25.0
    assert struct.unpack_from('<f', core, 0xA0C)[0] == 150.0
    print(f'PASS native radius={size}: blast=25, damage=150 preserved')
print('6 native checks passed; full live collision and multiplayer not executed')
