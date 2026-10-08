"""Optional Windows x64 native zero-damage audit (requires Python and pefile).

Usage: python tests/zero_damage_native_audit.py "X:/path/to/EDF.dll"
Omitting the DLL, or an unavailable file/platform, returns skip code 77. No DLL is downloaded.

Maps the supported EDF.dll without DllMain, never attaches to a game process or writes the install.
Executes its damage setter, full InitParam copy, extracted native GDI assignment block and full
lifetime-falloff function against private buffers, including positive damage controls (18 checks).
This verifies zero survives those stages, not full BulletControl construction, every target's hit
reactions (e.g. stagger/impulse), or live multiplayer behavior. The only import resolved is CRT powf.
"""
import argparse
import ctypes as C
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path, help='explicit path to the supported EDF.dll')
args = parser.parse_args()
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8:
    print('SKIP: this native fixture requires Windows x64 Python')
    raise SystemExit(77)
if args.edf_dll is None or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll path for native zero-damage checks')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: native precondition checks must remain enabled')
import pefile

path = str(args.edf_dll.resolve())
pe = pefile.PE(path, fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll profile'
assert pe.FILE_HEADER.Machine == 0x8664, 'EDF.dll must be x64'
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
k.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
k.VirtualAlloc.restype = C.c_void_p
base = k.LoadLibraryExW(path, None, 1)  # DONT_RESOLVE_DLL_REFERENCES, never DllMain
assert base, f'LoadLibraryExW failed: {C.get_last_error()}'
assert C.string_at(base + 0x2B82E0, 9) == bytes.fromhex('f30f1189dc000000c3')
setter = C.WINFUNCTYPE(None, C.c_void_p, C.c_float)(base + 0x2B82E0)
copy = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)(base + 0x2307F0)

# 231080's only external call is a powf import. Resolve only that CRT slot in this private mapping.
assert pe.get_data(0x12DA8E2, 6) == bytes.fromhex('ff2598bb4700')
crt = C.CDLL('api-ms-win-crt-math-l1-1-0.dll')
old = C.c_uint()
assert k.VirtualProtect(base + 0x1756480, 8, 4, C.byref(old))
C.c_void_p.from_address(base + 0x1756480).value = C.cast(crt.powf, C.c_void_p).value
restored = C.c_uint()
assert k.VirtualProtect(base + 0x1756480, 8, old.value, C.byref(restored))
falloff = C.WINFUNCTYPE(C.c_float, C.c_void_p)(base + 0x231080)

# Execute the exact straight-line native GDI damage transfer (no calls, branches or RIP-relative operands),
# with an ABI adapter for RBX. The rest of BulletControl::Init requires the running game's world services.
block = pe.get_data(0x23207F, 0x2320A9 - 0x23207F)
assert block == bytes.fromhex(
    'f30f10830c0a0000f30f5983a00900008b83140a0000898398070000'
    '8b833c0a0000f30f118380070000')
code = bytes.fromhex('534889cb') + block + bytes.fromhex('5bc3')
adapter = k.VirtualAlloc(None, len(code), 0x3000, 0x40)
assert adapter
C.memmove(adapter, code, len(code))
to_gdi = C.WINFUNCTYPE(None, C.c_void_p)(adapter)

ifc = C.create_string_buffer(0x600)
core = C.create_string_buffer(0xE00)
checks = 0
for damage in (0.0, 0.125, 17.0):
    C.memset(C.addressof(ifc), 0, C.sizeof(ifc))
    C.memset(C.addressof(core), 0, C.sizeof(core))
    # Null weak pointers and empty variant tags make the native copy require no game allocator or callbacks.
    struct.pack_into('<H', ifc, 0x70 + 0xC8, 0xFFFF)
    struct.pack_into('<H', core, 0x9A0 + 0xC8, 0xFFFF)
    struct.pack_into('<f', ifc, 0xDC, 999.0)
    struct.pack_into('<f', ifc, 0x70, 2.0)  # nonzero damage multiplier
    setter(C.addressof(ifc), damage)
    assert struct.unpack_from('<f', ifc, 0xDC)[0] == damage
    checks += 1
    copy(C.addressof(core) + 0x9A0, C.addressof(ifc) + 0x70)
    assert struct.unpack_from('<f', core, 0xA0C)[0] == damage
    checks += 1
    to_gdi(C.addressof(core))
    assert struct.unpack_from('<f', core, 0x780)[0] == damage * 2.0
    checks += 1
    # Native lifetime falloff at initial, middle and final age; valid finite SGO factors.
    struct.pack_into('<i', core, 0xA08, 60)
    struct.pack_into('<f', core, 0xA10, 0.5)
    struct.pack_into('<f', core, 0xA18, 1.0)
    for age in (0, 30, 60):
        struct.pack_into('<i', core, 0xAF8, age)
        got = falloff(C.addressof(core))
        expected = damage * (2.0 - age / 60.0)
        assert abs(got - expected) < 1e-5, (damage, age, got, expected)
        checks += 1
    print(f'PASS input {damage}: native setter, native parameter copy, extracted native GDI block, native falloff')
print(f'{checks} native checks passed; no DllMain, no game process, no game-directory changes')
print('LIMIT: full BulletControl initialization, per-class hit reactions and live multiplayer were not executed.')
