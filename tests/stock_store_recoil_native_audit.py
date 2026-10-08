"""Execute native stock-vehicle recoil callbacks against private buffers.

Optional: pass the supported EDF.dll; missing file/platform returns skip 77.
No DllMain, game process, or installed files are modified. The actual 505/601/Car
callbacks, recoil variant getter, strength getter and BodyRecoil arithmetic run.
Only animation and physics world services are fixtures. The old AimRecoil input
must reproduce the user's read-at-0xC exception; valid BodyRecoil must complete.
This does not execute projectile flight, networking, or game-session gameplay.
"""
import argparse
import ctypes as C
import hashlib
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if not __debug__:
    raise RuntimeError('Native precondition checks require Python without -O')
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or not args.edf_dll or not args.edf_dll.is_file():
    print('SKIP: supply supported EDF.dll and Windows x64 Python')
    raise SystemExit(77)
import pefile

pe = pefile.PE(str(args.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
assert pe.FILE_HEADER.Machine == 0x8664
for start, end, digest in (
    (0x5FA150, 0x5FA161, 'adb6f8e6246ce424ed57d96ace072f850f79df6fb758d850fcbb191b02e33b79'),
    (0x5F9FB0, 0x5FA136, '48a2e3e785888cad5264781d6473778af512211a6dde5df1f3bd0af89d893ab8'),
    (0x61AEA0, 0x61AF99, '3b4d33430066f29c64abd53e712fb6940bada93d3140b7e4ce8d33fc9c42cb96'),
    (0x620960, 0x620A59, 'fa6ffe9bbd2cc1bee878c7613751fabb20278509a399bfbbc08f8a79f3dc31ae'),
    (0x65A740, 0x65A851, '018c66ae177f8bf41cd0d1235e5ad317aa3e813647f3305e8c11afb7146bd478'),
):
    assert hashlib.sha256(pe.get_data(start, end-start)).hexdigest() == digest, hex(start)

k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
base = k.LoadLibraryExW(str(args.edf_dll.resolve()), None, 1)
assert base
keep = []

def buf(size):
    b = C.create_string_buffer(size + 15)
    keep.append(b)
    return (C.addressof(b) + 15) & ~15

def ptr(p, value):
    C.c_void_p.from_address(p).value = value

def u32(p, value):
    C.c_uint32.from_address(p).value = value

def flt(p, value):
    C.c_float.from_address(p).value = value

def patch(rva, code):
    old = C.c_uint()
    assert k.VirtualProtect(base+rva, len(code), 0x40, C.byref(old))
    C.memmove(base+rva, code, len(code))
    restored = C.c_uint()
    assert k.VirtualProtect(base+rva, len(code), old.value, C.byref(restored))

def hook(rva, callback):
    keep.append(callback)
    patch(rva, bytes.fromhex('ff2500000000') + struct.pack('<Q', C.cast(callback, C.c_void_p).value))

# No animation controller exists in a private image. Keep 62EA10's native camera
# recoil notification; only the independent animation service is a return stub.
patch(0x116BFC0, b'\xc3')
vehicle, holder, weapon, transform = buf(0x2B00), buf(0x48), buf(0xC00), buf(0x100)
body, system, world, vtable = buf(0x110), buf(0x60), buf(0x180), buf(0x80)
params, linear, angular = buf(0x10), buf(0x10), buf(0x10)
ptr(vehicle+0x1698, body)
ptr(body+0x100, system)
ptr(system+0x58, world)
ptr(world+0x20, vtable)
alive = C.WINFUNCTYPE(C.c_int, C.c_void_p, C.c_uint)(lambda *_: 1)
keep.append(alive)
ptr(vtable+0x70, C.cast(alive, C.c_void_p).value)
ptr(holder+0x10, weapon)
ptr(holder+0x18, transform)
ptr(holder+0x30, params)
ptr(weapon+0x1D0, transform)  # Car's muzzle transform
flt(weapon+0xBD8, 1.0)
for off in (0x50, 0x64, 0x78, 0x8C, 0xB0, 0xC4, 0xD8, 0xEC):
    flt(transform+off, 1.0)
get = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)
put = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)
hook(0x11B1060, get(lambda *_: angular))
hook(0x11B1300, get(lambda *_: linear))
hook(0x11B1760, put(lambda _, src: C.memmove(angular, src, 16)))
hook(0x11B18F0, put(lambda _, src: C.memmove(linear, src, 16)))

checks = 0
for name, rva in (('505', 0x61AEA0), ('601', 0x620960), ('Car', 0x65A740)):
    fire = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+rva)
    u32(holder+0x20, 1)  # old shipped AimRecoil variant
    flt(params+8, 0.0)
    flt(params+12, 0.0026)
    try:
        fire(vehicle, holder)
    except OSError as e:
        assert 'reading 0x000000000000000C' in str(e), (name, str(e))
    else:
        raise AssertionError(f'{name}: old AimRecoil did not reproduce the crash')
    checks += 1
    for thrust, swing in ((0.0, 0.0), (0.1, 0.05)):
        u32(holder+0x20, 0)  # BodyRecoil required by these callbacks
        flt(params+8, thrust)
        flt(params+12, swing)
        C.memset(linear, 0, 16)
        C.memset(angular, 0, 16)
        fire(vehicle, holder)
        xyz = struct.unpack('<4f', C.string_at(linear, 16))
        spin = struct.unpack('<4f', C.string_at(angular, 16))
        assert abs(xyz[2] + thrust*60*0.7) < 1e-5, (name, xyz)
        assert abs(spin[0] + swing*0.3) < 1e-5, (name, spin)
        checks += 1
    print(f'PASS {name}: old AimRecoil reads 0xC; zero/positive BodyRecoil completes with expected velocity')
print(f'{checks} native recoil checks passed; physics world and animation are fixtures, no live gameplay')
