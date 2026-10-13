"""Execute the native 505/601/Car slot 48 and the VehicleBase part 0x62EA10 against private buffers.

The user, 2026-10-12: 「同轴机枪开火的时候主炮会伸缩」 (docs/feedback-2026-10-12-coax-recoil.md). Shows on the real
EDF.dll what src/payload.cpp InstallCoaxMotion relies on:
- the 505's and the 601's slot 48 play the class's firing motion 0x116BFC0(veh+0x1170, [veh+0x2AC0], [veh+0x2AC8], 1)
  once for a shot of any holder, a zero-recoil (coax) holder too; the Car's (Grape 401) plays none;
- 0x62EA10, the part the plugin keeps for a coax holder, plays no motion, pushes no hull, and only raises the camera
  kick +0x670 from the gun's recoil strength +0xBD8.
Optional: pass the supported EDF.dll; missing file/platform returns skip 77. No DllMain, game process or installed
file is touched; the animation service and the physics world's velocity accessors are recording fixtures.
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
for start, end, digest in (   # the same bytes tests/stock_store_recoil_native_audit.py pins
    (0x5FA150, 0x5FA161, 'adb6f8e6246ce424ed57d96ace072f850f79df6fb758d850fcbb191b02e33b79'),
    (0x5F9FB0, 0x5FA136, '48a2e3e785888cad5264781d6473778af512211a6dde5df1f3bd0af89d893ab8'),
    (0x61AEA0, 0x61AF99, '3b4d33430066f29c64abd53e712fb6940bada93d3140b7e4ce8d33fc9c42cb96'),
    (0x620960, 0x620A59, 'fa6ffe9bbd2cc1bee878c7613751fabb20278509a399bfbbc08f8a79f3dc31ae'),
    (0x65A740, 0x65A851, '018c66ae177f8bf41cd0d1235e5ad317aa3e813647f3305e8c11afb7146bd478'),
):
    assert hashlib.sha256(pe.get_data(start, end-start)).hexdigest() == digest, hex(start)


def rel_call(at: int) -> int:
    code = pe.get_data(at, 5)
    assert code[0] == 0xE8, hex(at)
    return at + 5 + struct.unpack('<i', code[1:])[0]


# The call sites src/payload.cpp checks before it chains (kMotionClasses, kFiredVehicleCallAt).
for fired, motion in ((0x61AEA0, 0x61AEE6), (0x620960, 0x6209A6)):
    assert rel_call(fired + 0x10) == 0x62EA10, hex(fired)
    assert rel_call(motion) == 0x116BFC0, hex(motion)
image = pe.get_memory_mapped_image()
for vtable, fired in ((0x17DADB0, 0x61AEA0), (0x17DC250, 0x620960)):
    slot = struct.unpack_from('<Q', image, vtable + 48*8)[0] - pe.OPTIONAL_HEADER.ImageBase
    assert slot == fired, (hex(vtable), hex(slot))

k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
base = k.LoadLibraryExW(str(args.edf_dll.resolve()), None, 1)
assert base
keep = []


def buf(size: int) -> int:
    b = C.create_string_buffer(size + 15)
    keep.append(b)
    return (C.addressof(b) + 15) & ~15


def ptr(p: int, value: int) -> None:
    C.c_void_p.from_address(p).value = value


def u32(p: int, value: int) -> None:
    C.c_uint32.from_address(p).value = value


def flt(p: int, value: float) -> None:
    C.c_float.from_address(p).value = value


def get_flt(p: int) -> float:
    return C.c_float.from_address(p).value


def patch(rva: int, code: bytes) -> None:
    old = C.c_uint()
    assert k.VirtualProtect(base+rva, len(code), 0x40, C.byref(old))
    C.memmove(base+rva, code, len(code))
    restored = C.c_uint()
    assert k.VirtualProtect(base+rva, len(code), old.value, C.byref(restored))


def hook(rva: int, callback: object) -> None:
    keep.append(callback)
    patch(rva, bytes.fromhex('ff2500000000') + struct.pack('<Q', C.cast(callback, C.c_void_p).value))


motions: list[tuple[int, int, int, int]] = []
play = C.WINFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)(
    lambda controller, motion, blend, start: motions.append((controller, motion, blend, start)))
hook(0x116BFC0, play)

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
u32(holder+0x20, 0)           # BodyRecoil, as every coax mount (make_stock_stores._params)
flt(params+8, 0.0)            # the coax's [0, 0]
flt(params+12, 0.0)
u32(vehicle+0x2AC0, 0x1234)   # the class's firing motion
u32(vehicle+0x2AC8, 7)
for off in (0x50, 0x64, 0x78, 0x8C, 0xB0, 0xC4, 0xD8, 0xEC):
    flt(transform+off, 1.0)
get = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)
put = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)
velocity_writes: list[str] = []
hook(0x11B1060, get(lambda *_: angular))
hook(0x11B1300, get(lambda *_: linear))
hook(0x11B1760, put(lambda _, src: (velocity_writes.append('angular'), C.memmove(angular, src, 16))))
hook(0x11B18F0, put(lambda _, src: (velocity_writes.append('linear'), C.memmove(linear, src, 16))))

checks = 0
for name, rva, plays in (('505', 0x61AEA0, True), ('601', 0x620960, True), ('Car', 0x65A740, False)):
    fire = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+rva)
    motions.clear()
    flt(weapon+0xBD8, 0.2)
    fire(vehicle, holder)
    want = [(vehicle+0x1170, 0x1234, 7, 1)] if plays else []
    assert motions == want, (name, motions)
    checks += 1
    print(f'PASS {name}: a zero-recoil holder\'s shot plays {len(motions)} firing motion(s)')

vehicle_part = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+0x62EA10)
motions.clear()
velocity_writes.clear()
flt(vehicle+0x670, 0.0)
flt(weapon+0xBD8, 0.2)
vehicle_part(vehicle, holder)
assert motions == [] and velocity_writes == [], (motions, velocity_writes)
assert abs(get_flt(vehicle+0x670) - 0.4) < 1e-6, get_flt(vehicle+0x670)   # max(kick, 2 x strength)
checks += 1
print('PASS 0x62EA10: no motion, no hull push, the camera kick from the gun\'s strength only')
print(f'{checks} native coax-motion checks passed; animation and physics world are fixtures, no live gameplay')
