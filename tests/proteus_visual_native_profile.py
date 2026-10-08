"""Read-only EDF.dll profile plus private native bone composition, no game entrypoint."""
import ctypes as C
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import gamedir

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / 'EDF.dll'
if sys.platform != 'win32' or not path.exists():
    print('SKIP: supported EDF.dll needed for native Proteus bone composition')
    raise SystemExit(77)
import pefile
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
image = pe.get_memory_mapped_image()
assert struct.unpack_from('<Q', image, 0x17DEC40 + 45 * 8)[0] - pe.OPTIONAL_HEADER.ImageBase == 0x6437D0
assert image[0x6437DC:0x6437E4] == bytes.fromhex('48 8b f1 e8 ec af fe ff')
assert image[0x62E7E9:0x62E7F9] == bytes.fromhex('48 8d 53 60 48 8d 8b e0 0e 00 00 e8 97 23 ad 00')

kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint32]
kernel.LoadLibraryExW.restype = C.c_void_p
module = kernel.LoadLibraryExW(str(path), None, 1)  # DONT_RESOLVE_DLL_REFERENCES
assert module
storage = []
def aligned(n):
    buf = C.create_string_buffer(n + 15); storage.append(buf)
    return (C.addressof(buf) + 15) & ~15
def put(addr, fmt, *values):
    data = struct.pack(fmt, *values); C.memmove(addr, data, len(data))
def matrix(addr):
    return struct.unpack('<16f', C.string_at(addr, 64))
identity = [1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
inst, bones, world = aligned(0xC0), aligned(2 * 0x110), aligned(64)
put(inst + 0x10, '<Q', bones); put(inst + 0x20, '<Q', 2)
put(bones + 0x14, '<i', 1); put(bones + 0x20, '<i', -1)
put(bones + 0x110 + 8, '<B', 1); put(bones + 0x110 + 0x10, '<i', 0)
root = identity.copy(); root[12:15] = [10.,20.,30.]
put(world, '<16f', *root)
compose = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(module + 0x1100B90)
for visible in [False, True, False]:
    panel = identity.copy()
    if not visible:
        panel[0] = panel[5] = panel[10] = 0.
    put(bones + 0x110 + 0x70, '<16f', *panel)
    compose(inst, world)
    result = matrix(bones + 0x110 + 0xB0)
    assert result[12:15] == tuple(root[12:15])
    assert result[0] == result[5] == result[10] == float(visible)
assert C.c_ubyte.from_address(inst + 0xB0).value == 1  # native palette dirtied
print('proteus_visual_native_profile: original pose entry, SetWorld ABI and native hide/show/hide composition passed')
