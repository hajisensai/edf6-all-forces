"""Read the supported EDF.dll and prove the sync load probe's anchors (src/sync_load_probe.cpp, docs/sync-load-re.md):
the four call sites inside the resource cache's get-or-load 0x84E80 are near calls to the stock functions its constants
name, every byte signature it checks at install matches, the read in the miss branch is the reader factory [0x2005540]
then Open (+8) and the blocking Wait(0) (+0x10), the hit and the not-found branches jump to the exit the probe chains,
and, decoded instruction by instruction, nothing between the prologue and the epilogue writes rsp but a call (no
push, pop, sub or add: the hooks pair calls by their frame).

pefile + capstone over the file; no binary is loaded or executed. Missing --dll skips with 77.
"""
from pathlib import Path
import argparse
import re
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
args = parser.parse_args()
if not args.dll or not Path(args.dll).is_file():
    print('SKIP: EDF6_NATIVE_DLL required')
    raise SystemExit(77)
import capstone  # noqa: E402
import pefile  # noqa: E402

pe = pefile.PE(args.dll, fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
binary = pe.get_memory_mapped_image()
source = (Path(__file__).resolve().parents[1] / 'src' / 'sync_load_probe.cpp').read_text(encoding='utf-8')


def value(name: str) -> int:
    return int(re.search(r'\b' + name + r'=0x([0-9A-Fa-f]+)', source).group(1), 16)


def call(at: int, target: int) -> None:
    assert binary[at] == 0xE8 and at + 5 + struct.unpack_from('<i', binary, at + 1)[0] == target, (hex(at), hex(target))


def jump(at: int) -> int:
    if binary[at] == 0xE9:
        return at + 5 + struct.unpack_from('<i', binary, at + 1)[0]
    assert binary[at] == 0xEB, hex(at)
    return at + 2 + struct.unpack_from('<b', binary, at + 1)[0]


checked = 0
for site, target in (('kLockCall', 'kLock'), ('kLookupCall', 'kLookup'), ('kMissCall', 'kMiss'),
                     ('kFailCall', 'kFail'), ('kExitCall', 'kExit')):
    call(value(site), value(target))
    checked += 1
sigs = re.findall(r'\{0x([0-9A-Fa-f]+),"([0-9A-F ]+)"\}', re.search(r'kSigs\[\]=\{(.*?)\n\};', source, re.S).group(1))
assert len(sigs) >= 10, sigs
for rva, hexes in sigs:
    want = bytes.fromhex(hexes)
    at = int(rva, 16)
    assert binary[at:at + len(want)] == want, (rva, binary[at:at + len(want)].hex(), hexes)
# The miss's read: call [rip+disp] at 0x85012 is the reader factory global 0x2005540.
assert binary[0x85012:0x85014] == b'\xff\x15'
assert 0x85018 + struct.unpack_from('<i', binary, 0x85014)[0] == 0x2005540
# The hit (0x84F2E) and the not-found branch (0x8510F) reach the exit block 0x85159 whose unlock is the chained call.
exit_block = value('kExitCall') - 4
assert jump(0x84F2E) == exit_block and jump(0x8510F) == exit_block
# The hit test: je 0x84F33 (the miss) right after the lookup.
assert 0x84EDC + 2 + struct.unpack_from('<b', binary, 0x84EDD)[0] == 0x84F33
# One frame: decoded from the entry to its ret, the only instructions writing rsp are the prologue's pushes and sub
# (before 0x84E9C), the epilogue's add and pops (from 0x85179), and calls (which give it back). A push, pop, sub, add,
# lea or mov of rsp in the body would move the frame between two sites of one call.
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
moves, at = [], 0x84E80
for insn in md.disasm(binary[0x84E80:0x851A0], 0x84E80):
    assert insn.address == at, hex(at)   # decoded without a gap
    at += insn.size
    if insn.mnemonic == 'ret':
        break
    if insn.mnemonic != 'call' and capstone.x86.X86_REG_RSP in insn.regs_access()[1]:
        moves.append((insn.address, insn.mnemonic))
assert at == 0x8518C, hex(at)   # the ret at 0x8518B ends it
prologue = [m for m in moves if m[0] < 0x84E9C]
epilogue = [m for m in moves if m[0] >= 0x85179]
assert [m for _, m in prologue] == ['push'] * 7 + ['sub'], prologue
assert [m for _, m in epilogue] == ['add'] + ['pop'] * 7, epilogue
assert len(moves) == len(prologue) + len(epilogue), [(hex(a), m) for a, m in moves]
print(f'sync load probe anchors ok ({checked} call sites, {len(sigs)} signatures)')
