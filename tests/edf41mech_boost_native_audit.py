"""EDF.dll facts src/edf41mech_boost.cpp relies on (the 406 Begaruta's jump crash, 2026-10-25).

    python tests/edf41mech_boost_native_audit.py <EDF.dll>     (exit 77: no supported EDF.dll given)

Static only (pefile + capstone over the file):
  * the bytes src/edf41mech_boost.h checks are at their RVAs (the slot writes, the step's four-slot loop, the old
    parser's two-slot loop);
  * VehicleBegaruta's setup picks the old parser 0x640340 for the key "begaruta_boost" (0x636DCE..0x636E07) after
    "begaruta_boost6" (0x63DEC0) and "begaruta_boost5" (0x63CEC0), each on the jet group vehicle+0x1BB0;
  * the old parser stores a slot's emitter at +0x70 of the group (slot i at +0x20 + i*0xA0, its +0x50) inside a loop
    over rsi < 2, and the step 0x6482A0 (called from the update at 0x638CE7 / 0x638D1D) reads +0x50 of four slots;
  * the boost stop's loop 0x638EE0 walks the same four slots (vehicle+0x1BD0 .. +0x1E50);
  * no other code in the class's range writes through a slot's emitter (+0x3EC / +0x3F0) than the two patched loops.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: a supported EDF.dll is required (EDF6_NATIVE_DLL)')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('assertions must be enabled')
import capstone
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, hex(pe.FILE_HEADER.TimeDateStamp)
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def call_target(rva: int) -> int:
    assert img[rva] == 0xE8, hex(rva)
    return rva + 5 + struct.unpack_from('<i', img, rva + 1)[0]


def lea_target(rva: int) -> int:
    i = next(md.disasm(img[rva:rva + 16], rva))
    m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', i.op_str)
    assert i.mnemonic == 'lea' and m, (hex(rva), i.mnemonic, i.op_str)
    return i.address + i.size + int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)


def wide(rva: int) -> str:
    end = rva
    while img[end:end + 2] != b'\0\0':
        end += 1
    return img[rva:end + 1].decode('utf-16le', 'replace').rstrip('\0')


def ascii_at(rva: int) -> str:
    return img[rva:img.index(b'\0', rva)].decode()


src = (ROOT / 'src' / 'edf41mech_boost.h').read_text(encoding='utf-8')
consts = {name: int(value, 16) for name, value in re.findall(r'\b(k[A-Za-z]+)=(0x[0-9A-Fa-f]+)', src)}
arrays = 0
for name, body in re.findall(r'unsigned char (k[A-Za-z]+)Code\[\]=\{([^}]*)\}', src):
    expected = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    assert img[consts[name]:consts[name] + len(expected)] == expected, (name, hex(consts[name]))
    arrays += 1
assert arrays == 5, arrays

# the setup's three parsers on the jet group, the old one for "begaruta_boost"
keys = {0x636D45: 'begaruta_boost6', 0x636D8E: 'begaruta_boost5', 0x636DCE: 'begaruta_boost'}
for at, key in keys.items():
    assert wide(lea_target(at)) == key, (hex(at), wide(lea_target(at)))
for lea, call, parser in ((0x636D7D, 0x636D84, 0x63DEC0), (0x636DC0, 0x636DC7, 0x63CEC0), (0x636E00, 0x636E07, 0x640340)):
    i = next(md.disasm(img[lea:lea + 8], lea))
    assert i.op_str == 'rcx, [rdi + 0x1bb0]', (hex(lea), i.op_str)
    assert call_target(call) == parser, (hex(call), hex(call_target(call)))

# the old parser: slot base r13 + r15 (r15 = rsi * 0xA0), the emitter at +0x70, two slots
code = list(md.disasm(img[0x640340:0x640D20], 0x640340))
ops = {i.address: (i.mnemonic, i.op_str) for i in code}
assert ops[0x6404C3] == ('lea', 'r15, [rsi + rsi*4]') and ops[0x6404C7] == ('shl', 'r15, 5'), 'old parser slot index'
assert ops[0x6408D7] == ('mov', 'qword ptr [r15 + r13 + 0x70], rdi'), ops[0x6408D7]
assert ops[0x640CB3] == ('cmp', 'rsi, 2'), ops[0x640CB3]

# the step reads every slot's emitter; its callers are the update's two boost transitions
assert call_target(0x638CE7) == 0x6482A0 and call_target(0x638D1D) == 0x6482A0
for at in (0x638CDF, 0x638D15):
    assert next(md.disasm(img[at - 1:at + 7], at - 1)).op_str == 'rcx, [rdi + 0x1bb0]', hex(at)

# nothing else in the Begaruta family's code writes through an emitter's +0x3EC / +0x3F0
writers = []
for i in md.disasm(img[0x630000:0x650000], 0x630000):
    if re.search(r'\[r\w+ \+ 0x3e[c]\]|\[r\w+ \+ 0x3f0\]', i.op_str) and i.mnemonic.startswith('mov') and i.op_str.startswith(('dword', 'qword')):
        writers.append(i.address)
assert set(writers) == {0x64833E, 0x64834B, 0x638F01}, [hex(w) for w in writers]
print('edf41mech_boost_native_audit: ok')
