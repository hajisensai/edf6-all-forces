"""Read the supported EDF.dll and prove the sync load probe's anchors (src/sync_load_probe.cpp, docs/sync-load-re.md):
the four call sites inside the resource cache's get-or-load 0x84E80 are near calls to the stock functions its constants
name, every byte signature it checks at install matches, the read in the miss branch is the reader factory [0x2005540]
then Open (+8) and the blocking Wait(0) (+0x10), the hit and the not-found branches jump to the exit the probe chains,
and nothing between the prologue and the exit moves rsp (the hooks pair calls by their frame).

No binary is loaded or executed. Missing --dll skips with 77.
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
# One frame: the only rsp changes in 0x84E80 are the prologue's pushes and sub, and the epilogue (from 0x85179).
body = binary[0x84E9C:0x85179]
assert b'\x48\x81\xec' not in body and b'\x48\x83\xec' not in body and b'\x48\x81\xc4' not in body
print(f'sync load probe anchors ok ({checked} call sites, {len(sigs)} signatures)')
