"""Native audit of the NPC soldiers' own dashes (src/npcai.cpp Mobility, docs/npc-mobility-re.md; explicit EDF.dll).

Checks the bytes npcai.cpp's kWingDashSigs / kFencerMoveSigs check against the DLL, the code they stand for (which
state each dash enters, which vtable slots read the presses, which code writes d7B and d72), and runs the Wing Diver's
energy spend 0x5804B0 (with 0x54A2E0) on a stand-in soldier: a dash that costs what is left empties the tank, one that
costs more overheats it, an overheated one spends nothing. Loads EDF.dll without DllMain or imports; does not attach to a
game or modify installed files. This is not live evidence of how far a dash carries.
"""
import argparse
import ctypes as C
from pathlib import Path
import re
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
import capstone
import pefile

pe = pefile.PE(str(a.edf_dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
img = pe.get_memory_mapped_image()
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
TVA, TSZ = text.VirtualAddress, text.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

src = (Path(__file__).resolve().parents[1] / 'src' / 'npcai.cpp').read_text(encoding='utf-8')


def table(name):
    """The rows of `const Signature name[]={...};` as (rva, bytes)."""
    body = src.split('const Signature %s[]={' % name, 1)[1].split('\n};', 1)[0]
    rows = []
    for m in re.finditer(r'\{(0x[0-9A-Fa-f]+),\{([^}]*)\},(\d+)\}', body):
        data = bytes(int(x, 16) for x in m.group(2).split(',') if x.strip())
        assert len(data) == int(m.group(3)), m.group(0)
        rows.append((int(m.group(1), 16), data))
    assert rows, name
    return rows


checks = 0
for name in ('kWingDashSigs', 'kFencerMoveSigs'):
    for rva, data in table(name):
        assert img[rva:rva + len(data)] == data, '%s %#x: %s' % (name, rva, img[rva:rva + len(data)].hex(' '))
        checks += 1


def ins_at(rva):
    return next(md.disasm(img[rva:rva + 16], rva))


def lea_target(rva):
    i = ins_at(rva)
    assert i.mnemonic == 'lea' and i.size == 7, (hex(rva), i.mnemonic, i.op_str)
    return rva + 7 + struct.unpack_from('<i', img, rva + 3)[0]


def call_target(rva):
    assert img[rva] == 0xE8, hex(rva)
    return rva + 5 + struct.unpack_from('<i', img, rva + 1)[0]


def slot(vtable, k):
    return struct.unpack_from('<Q', img, vtable + 8 * k)[0] - IMAGE_BASE


# The Wing Diver (PaleWing vtable 0x17D0FF8): slot 5 reads d7B and picks the dash state by +0x1264 (frames in the air:
# 0x583820 airborne, 0x583D60 on the ground); slot 31 spends the energy.
WD, FENCER = 0x17D0FF8, 0x17CF5B8
assert slot(WD, 5) == 0x5808B0 and slot(WD, 31) == 0x5804B0
assert img[0x580BB7:0x580BBD] == b'\xff\x90\xf8\x00\x00\x00' and img[0x580BC4:0x580BCA] == b'\xff\x90\xf8\x00\x00\x00'  # slot 31
assert img[0x580BAE:0x580BB5] == b'\x44\x39\xbf\x64\x12\x00\x00'   # cmp [+0x1264], r15d; jle -> the ground one
assert lea_target(0x580B66) == 0x583820 and lea_target(0x580BCA) == 0x583D60
assert call_target(0x5804EC) == 0x54A2E0 and call_target(0x580B8D) == 0x590D70
# +0x1264 counts the frames off the ground (+0x711 != 2), 0 again on landing.
assert img[0x573F83:0x573F8B] == b'\x74\x0b\xff\x86\x64\x12\x00\x00'
checks += 7
# The Fencer: slot 5 (0x567780) runs the shared weapon-set pass 0x59ABB0 that takes d72+k; the 0x2003 message enters the
# dash state 0x569D50 (through 0x590A10(h,0,2)) and the boost 0x566BE0 its launch state 0x569810.
assert slot(FENCER, 5) == 0x567780 and call_target(0x567795) == 0x59ABB0
assert lea_target(0x56733D) == 0x569D50 and lea_target(0x566BFB) == 0x569810
assert call_target(0x567377) == 0x590A10 and call_target(0x566C4E) == 0x590A10
assert img[0x567309] == 0xE9 and 0x567309 + 5 + struct.unpack_from('<i', img, 0x56730A)[0] == 0x566BE0   # kind 0: boost
# 0x590A10: a lock held by nobody (the dash's) is taken again by nobody at once (the dash-jump's boost passes).
assert img[0x590A4A:0x590A4F] == b'\x48\x3b\xce\x74\x21'
checks += 5


def writers(disp):
    """Every instruction in .text that writes [reg + disp] (byte or word)."""
    pat = struct.pack('<I', disp)
    out = []
    t = img[TVA:TVA + TSZ]
    at = t.find(pat)
    while at >= 0:
        for back in range(2, 6):
            st = TVA + at - back
            i = next(md.disasm(img[st:st + 16], st), None)
            if i is None or i.address + i.size < TVA + at + 4 or i.address + i.size > TVA + at + 4 + 2:
                continue
            ops = i.operands
            if i.mnemonic == 'mov' and ops and ops[0].type == capstone.x86.X86_OP_MEM and ops[0].mem.disp == disp \
                    and ops[0].mem.index == 0:
                out.append(i.address)
            break
        at = t.find(pat, at + 1)
    return sorted(set(out))


# d7B: written by the input map's four copies (0x570757..0x570B43: pad, keyboard, the vehicle modes) and cleared by
# slot 4 (0x573B66) only: no stock AI ever presses the dash.
assert writers(0xD7B) == [0x570855, 0x57094A, 0x570A80, 0x570B43, 0x573B66], [hex(x) for x in writers(0xD7B)]
# d72: the input map's copies, slot 4's clear, and the stock Fencer AI's every-other-frame presses (0x566699 in slot 91,
# 0x56706C in slot 88) that FencerMove takes off its own hands.
d72 = writers(0xD72)
assert {0x566699, 0x56706C, 0x573B4C} <= set(d72) and all(x in (0x566699, 0x56706C, 0x573B4C) or 0x570700 <= x < 0x570B00
                                                         for x in d72), [hex(x) for x in d72]
assert img[0x566692:0x56669F] == b'\x0f\xb6\x48\x04\x80\xe1\x01\x88\x8b\x72\x0d\x00\x00'
checks += 3

# Run the energy spend on a stand-in Wing Diver.
k = C.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k.LoadLibraryExW.restype = C.c_void_p
base = k.LoadLibraryExW(str(a.edf_dll.resolve()), None, 1)
assert base
spend = C.WINFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_int)(base + 0x5804B0)
h = C.create_string_buffer(0x2400)
hp = C.addressof(h)


def run(energy, most, cost, overheated=0):
    C.memmove(hp + 0x304, struct.pack('<ff', most, energy), 8)
    C.memmove(hp + 0x2FC, struct.pack('<i', overheated), 4)
    spend(hp, cost, 0)
    e = struct.unpack('<f', C.string_at(hp + 0x308, 4))[0]
    return e, struct.unpack('<i', C.string_at(hp + 0x2FC, 4))[0]


assert run(100.0, 100.0, 30.0) == (70.0, 0)
assert run(30.0, 100.0, 30.0) == (0.0, 0), 'a dash that costs all that is left empties the tank, no overheat'
assert run(29.0, 100.0, 30.0) == (0.0, 1), 'a dash dearer than what is left overheats (+0x2FC)'
assert run(80.0, 100.0, 30.0, overheated=1) == (80.0, 1), 'overheated: nothing spent'
checks += 4
print('npc_mobility_native_audit: %d checks' % checks)
