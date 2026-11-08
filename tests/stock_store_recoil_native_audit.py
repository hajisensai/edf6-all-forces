"""Execute native stock-vehicle recoil callbacks against private buffers.

Optional: pass the supported EDF.dll; missing file/platform returns skip 77.
No DllMain, game process, or installed files are modified. The actual 505/601/Car/403/404
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
    # 403 (testhub #12): its slot 48, the holder index 0x62DB10, the AimRecoil jitter 0x5F9F10 -> 0x5FA8F0, and the
    # 404's slot 48 that src/storerecoil.cpp runs for a 403 holder past its seats
    (0x5FED60, 0x5FEE8D, '4cee61cb14fe67855e4e1657f093abaafa8d4e11da66d9b43c32d7692a25d33d'),
    (0x62DB10, 0x62DB49, 'a1091279353e7231150dcd347ae8392d0ce7146dbdd7738ac773d7c5328d7bf0'),
    (0x5F9F10, 0x5F9FAF, 'f9031ac42239464468b790a3d64fa609391302a02e0800a0ab940160a930367b'),
    (0x5FA8F0, 0x5FA905, '8fe935dec5b50f37e2a5e9e1384d248cf94fb6af5766c37d76d85cc9f6c4ddef'),
    (0x5FFEE0, 0x5FFFBD, 'cea4aa1a44c435d631ec6f8be5efb9e9d595f66521751910a3265c7df49718e7'),
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

# The Epsilon 403 (testhub #12, docs/recoil-re.md §3.1): its slot 48 branches on the holder's index alone. Holder 0 (the
# rail gun) pushes the hull by its BodyRecoil; holder i != 0 jitters seat i's aim by its AimRecoil, seat i being
# [veh+0x608] + i*0x340 with no test against the seat count (veh+0x618). make_stock_stores appends the AGM-114s as
# holder 3 of a three-seat 403. Four holders, three seats and a fourth seat-sized guard after them.
SEAT = 0x340
holders, seats = buf(0x48 * 4), buf(SEAT * 4)
ptr(vehicle+0x638, holders)
C.c_uint64.from_address(vehicle+0x648).value = 4
ptr(vehicle+0x608, seats)
C.c_uint64.from_address(vehicle+0x618).value = 3
holder_params = [buf(0x10) for _ in range(4)]
for i in range(4):
    h = holders + 0x48*i
    ptr(h+0x10, weapon)
    ptr(h+0x18, transform)
    ptr(h+0x30, holder_params[i])
fire403 = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+0x5FED60)
fire404 = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+0x5FFEE0)


def arm(i, variant, a, b):
    u32(holders + 0x48*i + 0x20, variant)
    flt(holder_params[i]+8, a)
    flt(holder_params[i]+12, b)
    C.memset(linear, 0, 16)
    C.memset(angular, 0, 16)
    C.memset(seats, 0, SEAT*4)
    return holders + 0x48*i


def pushed(thrust, swing, what):
    xyz = struct.unpack('<4f', C.string_at(linear, 16))
    spin = struct.unpack('<4f', C.string_at(angular, 16))
    assert abs(xyz[2] + thrust*60*0.7) < 1e-5, (what, xyz)
    assert abs(spin[0] + swing*0.3) < 1e-5, (what, spin)


def seat_jitter(seat):
    got = C.string_at(seats + SEAT*seat + 0xE0, 0x68)
    return struct.unpack_from('<f', got, 0x24)[0], struct.unpack_from('<f', got, 0x64)[0]


# holder 0, BodyRecoil: the hull push
fire403(vehicle, arm(0, 0, 0.1, 0.05))
pushed(0.1, 0.05, '403 holder 0')
checks += 1
# holder 1 (seat 1's machine gun), AimRecoil: seat 1's aim jitters, no other seat, no hull push
fire403(vehicle, arm(1, 1, 0.5, 0.5))
assert seat_jitter(1) != (0.0, 0.0) and C.string_at(seats + SEAT*3, SEAT) == bytes(SEAT), 'seat 1 jitter only'
pushed(0.0, 0.0, '403 holder 1')
checks += 1
# holder 3 (the store) with its shipped BodyRecoil: the reported crash, EDF+5F9F2B reading 0xC
try:
    fire403(vehicle, arm(3, 0, 0.0, 0.0))
except OSError as e:
    assert 'reading 0x000000000000000C' in str(e), str(e)
else:
    raise AssertionError('403 holder 3 with BodyRecoil did not reproduce the testhub #12 crash')
checks += 1
# holder 3 with an AimRecoil record instead: no crash, but the jitter is written into seat 3, past the three-seat
# vector (here the guard): no recoil record makes the stock code safe for a holder past the seats
fire403(vehicle, arm(3, 1, 0.5, 0.5))
assert seat_jitter(3) != (0.0, 0.0), '403 holder 3 AimRecoil writes past the seats'
checks += 1
# what src/storerecoil.cpp runs for that holder: the 404's slot 48, VehicleBase code, the holder's own BodyRecoil
for thrust, swing in ((0.0, 0.0), (0.1, 0.05)):
    fire404(vehicle, arm(3, 0, thrust, swing))
    pushed(thrust, swing, '404 slot 48 on 403 holder 3')
    assert C.string_at(seats, SEAT*4) == bytes(SEAT*4), 'the hull path touches no seat'
    checks += 1
print('PASS 403: holder 0 BodyRecoil push, holder 1 AimRecoil seat-1 jitter; store holder 3: BodyRecoil reads 0xC, '
      'AimRecoil writes past the seats; 0x5FFEE0 pushes the hull by its BodyRecoil')
print(f'{checks} native recoil checks passed; physics world and animation are fixtures, no live gameplay')
