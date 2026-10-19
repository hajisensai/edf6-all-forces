"""Native audit of the stock join mark and free-squad cover (src/npcai.cpp InstallFreeSquadCover; explicit EDF.dll).

Loads EDF.dll without DllMain or imports, checks the bytes npcai.cpp patches and runs the original join-mark
visit 0xF7280 on stand-in soldiers: it keeps the head of a soldier with +0x540 set and no leader, nobody else.
Does not attach to a game or modify installed files. This is not live evidence of what the mark looks like.
"""
import argparse
import ctypes as C
from pathlib import Path
import struct
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('edf_dll', nargs='?', type=Path)
a = p.parse_args()
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or a.edf_dll is None or not a.edf_dll.is_file():
    print('SKIP: Windows x64 and an explicit supported EDF.dll are required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Native precondition assertions require Python without -O')
import pefile
pe = pefile.PE(str(a.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
img = pe.get_memory_mapped_image()
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase

src = Path(__file__).resolve().parents[1] / 'src' / 'npcai.cpp'
text = src.read_text(encoding='utf-8')


def sig(name):
    start = text.index('const unsigned char %s[]={' % name) + len('const unsigned char %s[]={' % name)
    body = text[start:text.index('};', start)]
    return bytes(int(x, 16) for x in body.replace('\n', ' ').split(',') if x.strip())


# The bytes npcai.cpp checks are the DLL's.
assert img[0xF7280:0xF7280 + len(sig('kJoinMarkSig'))] == sig('kJoinMarkSig')
walk, read = sig('kCoverWalkSig'), sig('kCoverReadSig')
assert len(walk) == 27 and img[0x59B4B4:0x59B4B4 + 27] == walk
assert img[0x59B4CF:0x59B4CF + len(read)] == read
# The slot is the mark object's visit; its vtable is what 0xF5200 (and the inline copy at 0xF4C59) installs.
assert struct.unpack_from('<Q', img, 0x1768BD0)[0] - IMAGE_BASE == 0xF7280
for at in (0xF521A, 0xF4C59):   # lea rax,[rip+disp] -> 0x1768BC8
    assert img[at:at + 3] == b'\x48\x8d\x05' and at + 7 + struct.unpack_from('<i', img, at + 3)[0] == 0x1768BC8
# Its texture: lea r8,[rip+disp] at 0xF52E2 -> "合流マーク.dds".
name = 0xF52E2 + 7 + struct.unpack_from('<i', img, 0xF52E2 + 3)[0]
assert img[name:name + 18].decode('utf-16le') == '合流マーク.dds'
# The cover the walk feeds: 3.0 / 1.0 into +0x478, x0.01 into +0x384, x0.1 for the guns, 2.25 m for the mark.
assert img[0x59B524:0x59B52E] == b'\xc7\x83\x78\x04\x00\x00\x00\x00\x80\x3f'
for at, value in ((0x59B512, 0.01), (0x59B502, 0.1)):
    disp = struct.unpack_from('<i', img, at + 4)[0]
    assert abs(struct.unpack_from('<f', img, at + 8 + disp)[0] - value) < 1e-6
assert struct.unpack_from('<f', img, 0x1768DC4)[0] == 2.25
# The observers' sight: 0x54A330 divides by the target's +0x478.
assert img[0x54A343:0x54A34B] == b'\xf3\x0f\x5e\xa2\x78\x04\x00\x00'

# Run the stock visit on stand-ins.
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
base = k.LoadLibraryExW(str(a.edf_dll.resolve()), None, 1)
assert base
visit = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base + 0xF7280)
keep = []


def buf(size):
    b = C.create_string_buffer(size)
    keep.append(b)
    return C.addressof(b)


points = buf(64 * 16 + 16) + 15 & ~15
marks = buf(0x40)
C.memmove(marks + 0x10, struct.pack('<QQQ', points, 64, 0), 24)   # data, capacity, size


def soldier(flag, leader):
    h = buf(0x600)
    C.memmove(h + 0x90, struct.pack('<4f', 10.0, 1.0, 20.0, 1.0), 16)
    C.memmove(h + 0x540, bytes([flag]), 1)
    C.memmove(h + 0x548, struct.pack('<Q', leader), 8)
    return h


def size():
    return struct.unpack('<Q', C.string_at(marks + 0x20, 8))[0]


visit(marks, soldier(1, 0))
assert size() == 1
assert struct.unpack('<4f', C.string_at(points, 16))[:3] == (10.0, 3.25, 20.0)
visit(marks, soldier(0, 0))
visit(marks, soldier(1, soldier(0, 0)))
assert size() == 1
print('PASS: join mark 0xF7280 keeps the head of a leaderless +0x540 soldier only; cover bytes and constants as patched')
