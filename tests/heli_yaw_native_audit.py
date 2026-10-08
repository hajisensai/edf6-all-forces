"""Verify the native helicopter angle-to-rate contract in a private EDF.dll mapping.

Runs the actual 6CE8D0 angular spring with a fixture identity body and recording Havok velocity
accessors. It does not run DllMain, launch/attach to the game, or write installed files. This
proves the native rate conversion, not the full game's collision/camera/flight behavior.
"""
import ctypes as C
import hashlib
from pathlib import Path
import math
import struct
import sys

if not __debug__:
    raise RuntimeError('native precondition checks require Python without -O')
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or len(sys.argv) < 2:
    print('SKIP: pass the supported local EDF.dll on Windows x64')
    raise SystemExit(77)
path = Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(77)
import pefile
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
assert hashlib.sha256(pe.get_data(0x6CE8D0, 0x6CEA6D-0x6CE8D0)).hexdigest() == 'a6ba9207f2f00b082b5ad281dcbffc6676cbbe9b205532a71f0dd7d8dbf8b278'
assert hashlib.sha256(pe.get_data(0x654E3F, 0x654E69-0x654E3F)).hexdigest() == 'd6e9399d812523ae48bcbff9e8d119e6ec164ca2d7c9e9f41c5b9b9891a95584'
assert hashlib.sha256(pe.get_data(0x654A80, 0x654F5F-0x654A80)).hexdigest() == 'e752c3f0547b9a5875392abef6c0bbf4e09fe48bfafb19f6b9ef067de0876ed2'
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
base = k.LoadLibraryExW(str(path.resolve()), None, 1)  # DONT_RESOLVE_DLL_REFERENCES
assert base


def patch(rva, data):
    old = C.c_uint()
    assert k.VirtualProtect(base+rva, len(data), 0x40, C.byref(old))
    C.memmove(base+rva, data, len(data))


keep = []
def buf(size):
    data = C.create_string_buffer(size)
    keep.append(data)
    return C.addressof(data)


def ptr(where, value):
    C.c_void_p.from_address(where).value = value


def hook(rva, callback):
    keep.append(callback)
    patch(rva, b'\xff\x25\0\0\0\0'+struct.pack('<Q', C.cast(callback, C.c_void_p).value))


pe.parse_data_directories(directories=[1])
mathdll = C.CDLL('api-ms-win-crt-math-l1-1-0.dll')
for dll in pe.DIRECTORY_ENTRY_IMPORT:
    if b'crt-math' not in dll.dll:
        continue
    for entry in dll.imports:
        if entry.name:
            address = C.cast(getattr(mathdll, entry.name.decode()), C.c_void_p).value
            patch(entry.address-pe.OPTIONAL_HEADER.ImageBase, struct.pack('<Q', address))

# Actual native spring gets the body rotation through this service; identity is sufficient to expose
# the angle/rate unit error. Native quaternion construction, axis/angle conversion and interpolation run.
matrix = buf(64)
for i in (0, 5, 10, 15):
    C.c_float.from_address(matrix+i*4).value = 1.
wrapper, body, world, service, vtable = [buf(n) for n in (8, 0x110, 0x60, 0x30, 0x90)]
ptr(wrapper, body); ptr(body+0x100, world); ptr(world+0x58, service); ptr(service+0x20, vtable)
get_transform = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint)(lambda *_: matrix)
keep.append(get_transform); ptr(vtable+0x80, C.cast(get_transform, C.c_void_p).value)
old_velocity = [0., 0., 0.]
recorded = [0., 0., 0.]


@C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)
def get_velocity(_, out):
    C.memmove(out, struct.pack('<4f', *old_velocity, 0.), 16)
    return out


@C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)
def set_velocity(_, value):
    recorded[:] = struct.unpack('<3f', C.string_at(value, 12))


hook(0x11B1060, get_velocity)
hook(0x11B1760, set_velocity)
spring = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_float, C.c_float)(base+0x6CE8D0)
target = buf(16)
checks = 0
for angle in (-.2, -.05, .05, .2):
    for blend in (.025, .125, 1.):
        old_velocity[:] = [0., .3, 0.]
        C.memmove(target, struct.pack('<4f', 0., math.sin(angle/2), 0., math.cos(angle/2)), 16)
        spring(wrapper, target, .15, blend)
        expected = .3+(angle*.15*60.-.3)*blend
        assert abs(recorded[1]-expected) < 0.001, (angle, blend, recorded, expected)
        assert abs(recorded[0])+abs(recorded[2]) < 1e-5
        checks += 1
print(f'{checks} native helicopter angular-spring checks passed (angle * spring * 60, then body blend)')


def closed_loop(compensate):
    """Execute the complete native attitude and spring functions on Brute's actual response parameters.

    Only Havok integration is replaced: each frame the recorded native angular velocity advances
    the fixture body matrix. Controller formulas mirror heliaim.h; the C++ production test calls
    AimFly itself, so these are complementary checks, not a claim of native gameplay execution.
    """
    attitude, inputs, contact = buf(0x100), buf(32), buf(1)
    for destination in (attitude, matrix):
        C.memset(destination, 0, 64)
        for i in (0, 5, 10, 15):
            C.c_float.from_address(destination+i*4).value = 1.
    limit = math.radians(50.)
    for offset, value in ((0x60, .15), (0x64, .025), (0x74, limit), (0x78, .005), (0x80, .61), (0x84, .0025)):
        C.c_float.from_address(attitude+offset).value = value
    native = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)(base+0x654A80)
    C.c_float.from_address(inputs+12).value = 1.
    old_velocity[:] = [0., 0., 0.]
    heading = previous = measured = late_error = 0.
    for frame in range(1800):
        basis = math.atan2(C.c_float.from_address(attitude+32).value, C.c_float.from_address(attitude+40).value)
        measured += (math.remainder(basis-previous, 2*math.pi)*60.-measured)*.3
        previous = basis
        desired = max(-limit, min(limit, 2.*math.remainder(.4-basis, 2*math.pi)))
        demand = max(-limit, min(limit, desired+1.5*(desired-measured)))
        state = C.c_float.from_address(attitude+0x44).value
        command = (demand/9.-state*.995)/(limit*.005) if compensate else demand/limit
        C.c_float.from_address(inputs+16).value = max(-1., min(1., command))
        native(attitude, wrapper, inputs, contact)
        heading += recorded[1]/60.
        old_velocity[:] = recorded[:]
        co, si = math.cos(heading), math.sin(heading)
        C.memmove(matrix, struct.pack('<16f', co, 0, -si, 0, 0, 1, 0, 0, si, 0, co, 0, 0, 0, 0, 1), 64)
        if frame >= 1200:
            late_error = max(late_error, abs(math.remainder(heading-.4, 2*math.pi)))
    return late_error


old_error, fixed_error = closed_loop(False), closed_loop(True)
assert old_error > .5, 'negative control must reproduce continuing Brute oscillation'
assert fixed_error < .01, 'compensated controller must settle under the complete native attitude law'
print(f'Native Brute closed loop: late heading error {math.degrees(old_error):.3f} -> {math.degrees(fixed_error):.3f} deg')
