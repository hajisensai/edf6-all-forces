"""Real EDF state callback -> centre conversion -> model root -> MAB locator.

Private DONT_RESOLVE mapping only. Unrelated input/physics/effects slots are
no-ops; native callback dispatch, centre conversion, root-world and point
transforms execute unchanged. No full mission/Havok world or game is run.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import sys

if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or len(sys.argv) != 2:
    print('SKIP: supported EDF.dll and its Root.cpk required on Windows x64')
    raise SystemExit(77)
path = Path(sys.argv[1])
if not path.is_file() or not (path.parent / 'Root.cpk').is_file():
    print('SKIP: supported EDF.dll and its Root.cpk required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Native safety assertions must be enabled')
import pefile
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
signatures = {
    0x652A60: '4c8b5118488bc14d85d2740b48634920',
    0x64F960: '488b01ffa0e0010000cccccc488b01ff',
    0x652630: '83fa010f85b9000000534883ec30488b',
    0x1100B90: 'c681b0000000010f2802488b41100f29',
    0x6BB420: '4c8b014963400c420f101400488b4108',
    # Model update: vehicle update slot 45 -> 652700 -> 62E7D0 ->
    # ModelInstance::SetWorld(v+EE0, v+60), after the state callback.
    0x630283: '488b07498bd7488bcfff906801000048',
    0x65271D: 'e8aec0fdff488b97b8170000488bcf4c',
    0x62E7E9: '488d5360488d8be00e0000e89723ad00',
}
for at, expected in signatures.items():
    assert pe.get_data(at, 16) == bytes.fromhex(expected), hex(at)
# Audited outer ordering is guarded, not executed as an entire vehicle frame.
# The initializer binds v+16E8 to its state v+1780, whose member callback is
# 64F960. Update dispatches that state before the model-update virtual call.
ordered_blocks = (
    (0x64F0B3, 0xB1, '645e4540521c762532648803dc6f7af14413d31f06c36402041432e5d8b1bfa2'),
    (0x653680, 0x115, '2b70913b9f4d739d7729cadd51b0b86a4c75b1cf061c01e7582f4ca851b37682'),
    (0x630250, 0x42, '413bf365c861237490d33df58c3b7aebe596117ac3268aa24a83780b3c4b7cb6'),
    (0x652700, 0x22, '716e9e378f60d568eb8361adb16768495279492c82ce4db81444957376ccd167'),
    (0x62E7D0, 0x29, '8b4d47acb459eeb1175c24dd5a28c6558a4714180897da3a974b4213a769b5b4'),
)
for at, length, expected in ordered_blocks:
    assert hashlib.sha256(pe.get_data(at, length)).hexdigest() == expected, hex(at)
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import sgo
import vcobjects as vc
game = vc.Game(str(path.parent))
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
k.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
k.VirtualAlloc.restype = C.c_void_p
k.VirtualFree.argtypes = [C.c_void_p, C.c_size_t, C.c_uint]
k.FreeLibrary.argtypes = [C.c_void_p]
base = k.LoadLibraryExW(str(path.resolve()), None, 1)
assert base
mem = k.VirtualAlloc(None, 0x5000, 0x3000, 4)
assert mem
v, vt, bone, record, pair, result = [mem + n for n in (0, 0x2800, 0x3000, 0x3200, 0x3300, 0x3400)]
noop = C.WINFUNCTYPE(None, C.c_void_p)(lambda p: None)


def put(at, fmt, *values):
    C.memmove(at, struct.pack(fmt, *values), struct.calcsize(fmt))


try:
    put(v, '<Q', vt)
    for slot in (55, 57, 58):
        put(vt + slot*8, '<Q', C.cast(noop, C.c_void_p).value)
    put(vt + 60*8, '<Q', base + 0x652630)
    put(v + 0x1790, '<Q', v)
    put(v + 0x1798, '<Q', base + 0x64F960)
    put(v + 0x17A0, '<i', 0)
    put(v + 0xEF0, '<Q', bone)
    put(bone + 0x14, '<i', -1)
    put(record + 0xC, '<i', 0x20)
    put(pair, '<QQ', record, bone)
    dispatch = C.WINFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_void_p, C.c_void_p)(base + 0x652A60)
    model = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base + 0x1100B90)
    point = C.WINFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)(base + 0x6BB420)
    for name in ('edf6tr_jet_carrier_mission', 'edf6tr_jet_blast_carrier_mission',
                 'edf6tr_jet_doll_carrier_mission', 'edf6tr_jet_drone'):
        m = sgo.read(vc.jet_sgo(game, name))[1]
        centre, half = [[vc._value(a) for a in row] for row in m['heli_rigid_body'][:2]]
        local_at, _ = vc.mab_locator(m['animation_model'][2], vc.door_name(m))
        local = struct.unpack_from('<3f', m['animation_model'][2], local_at)
        for yaw, rot in ((0, [1,0,0,0, 0,1,0,0, 0,0,1,0]),
                         (90, [0,0,-1,0, 0,1,0,0, 1,0,0,0])):
            put(v + 0x60, '<12f', *rot)
            put(v + 0x90, '<4f', -297, 9, 16, 1)
            put(v + 0x1680, '<4f', *(-a for a in centre), 0)
            dispatch(v + 0x1780, 1, None, None)
            origin = struct.unpack('<4f', C.string_at(v + 0x90, 16))
            model(v + 0xEE0, v + 0x60)
            root = struct.unpack('<4f', C.string_at(bone + 0xE0, 16))
            assert root == origin
            floor = origin[1] + centre[1] - half[1]
            put(record + 0x20, '<4f', *local, 1)
            point(pair, result)
            world = struct.unpack('<4f', C.string_at(result, 16))
            assert abs(world[1] - floor) < 1e-5, (name, yaw, local, world, floor)
            # Negative control: restore the previous centre-relative Y.
            put(record + 0x20, '<4f', local[0], -half[1], local[2], 1)
            point(pair, result)
            old_y = struct.unpack('<4f', C.string_at(result, 16))[1]
            assert abs(floor - old_y - centre[1]) < 1e-5
            assert floor - old_y > 1.0
            print('PASS', name, 'yaw', yaw, 'old/new door world Y', round(old_y, 4), round(world[1], 4))
finally:
    k.VirtualFree(mem, 0, 0x8000)
    k.FreeLibrary(base)
