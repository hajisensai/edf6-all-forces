"""Read-only PE audit of the native facts src/greyboss_parts.cpp rests on, on the real EDF.dll.

python tests/greyboss_parts_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input returns skip code 77.

Holds, against the DLL itself (docs/greyboss-parts-re.md):
  - the three call sites the source redirects (read out of the source, so the two cannot drift) are the only rel32 calls
    of 0x4444B0 in .text, one in each Humanoid_BigGreyBoss part event handler (0x443B90 parts_action, 0x443BF0
    パーツ稼働, 0x443C30 パーツ停止), each passing the event's "name" and testing the result for null;
  - the constructor registers those handlers under those event names and builds the parts map at +0x17E0;
  - 0x4444B0's miss path: after the bucket walk finds no key it takes the list's head (mov rbx,r15 at 0x444600) and
    reads the head's +0x48 (0x444641) with no comparison against it -- the read that faulted on 2026-10-24.
"""
from __future__ import annotations

from pathlib import Path
import re
import struct
import sys

if not __debug__:
    raise RuntimeError('Run without -O')
if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
import pefile

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize


def at(rva: int, n: int) -> bytes:
    return img[rva:rva + n]


def rel32_target(site: int) -> int:
    assert img[site] in (0xE8, 0xE9), hex(site)
    return site + 5 + struct.unpack_from('<i', img, site + 1)[0]


def rip_target(site: int, size: int) -> int:
    """The target of a 7-byte rip-relative lea at `site`."""
    return site + size + struct.unpack_from('<i', img, site + size - 4)[0]


def wstr(rva: int) -> str:
    end = rva
    while img[end:end + 2] != b'\0\0':
        end += 2
    return img[rva:end].decode('utf-16le')


source = (ROOT / 'src' / 'greyboss_parts.cpp').read_text(encoding='utf-8')
lookup = int(re.search(r'constexpr unsigned kLookup=0x([0-9A-Fa-f]+);', source).group(1), 16)
body = re.search(r'Site sites\[\]=\{(.*?)\};', source).group(1)
sites = [int(x, 16) for x in re.findall(r'\{0x([0-9A-Fa-f]+)\}', body)]
assert lookup == 0x4444B0 and sites == [0x443BB9, 0x443C15, 0x443C55], (hex(lookup), [hex(s) for s in sites])
head = re.search(r'kPartsHead=0x([0-9A-Fa-f]+),kPartsSize=0x([0-9A-Fa-f]+)', source)
assert (int(head.group(1), 16), int(head.group(2), 16)) == (0x17E8, 0x17F0)

for site in sites:
    assert rel32_target(site) == lookup, hex(site)
# The only rel32 calls / jumps of the lookup: every part event comes through the redirected sites.
calls = []
for m in re.finditer(b'[\xE8\xE9]', img[lo:hi - 5]):
    p = lo + m.start()
    if p + 5 + struct.unpack_from('<i', img, p + 1)[0] == lookup:
        calls.append(p)
assert calls == sites, [hex(p) for p in calls]

# Each handler: rcx = event, 0x528A0(event, L"name"), rdx = node + [node+8] (the string), the lookup, then a null test.
for handler, call, test in ((0x443B90, 0x443BB9, 0x443BD3), (0x443BF0, 0x443C15, 0x443C1A), (0x443C30, 0x443C55, 0x443C5A)):
    name_lea = next(p for p in range(handler, call) if at(p, 3) == bytes.fromhex('488d15'))
    assert wstr(rip_target(name_lea, 7)) == 'name', hex(handler)
    assert rel32_target(name_lea + 7) == 0x528A0, hex(handler)
    assert at(call - 7, 7) == bytes.fromhex('48635008' '4803d0'), at(call - 7, 7).hex()   # movsxd rdx,[rax+8]; add rdx,rax
    assert at(test, 3) in (bytes.fromhex('4885db'), bytes.fromhex('4885c0')), (hex(test), at(test, 3).hex())

# The constructor 0x441040: the parts map's list head allocated at +0x17E0+8 (0x441119 lea r13,[r15+0x17e0]), each
# part inserted by name (0x44270e lea rcx,[r15+0x17e0]), the three handlers registered by event name.
assert at(0x441119, 7) == bytes.fromhex('4d8daf' 'e0170000')
assert at(0x44270E, 7) == bytes.fromhex('498d8f' 'e0170000')
for lea_handler, handler, event in ((0x441541, 0x443B90, 'parts_action'), (0x4412BE, 0x443BF0, 'パーツ稼働'),
                                    (0x441402, 0x443C30, 'パーツ停止')):
    assert rip_target(lea_handler, 7) == handler, hex(lea_handler)
    name_lea = next(p for p in range(lea_handler, lea_handler + 0x50) if at(p, 3) == bytes.fromhex('488d15'))
    assert wstr(rip_target(name_lea, 7)) == event, (hex(lea_handler), wstr(rip_target(name_lea, 7)))

# 0x4444B0's miss path: the walk's end (rbx == the bucket's last, or the list's head r15) lands on mov rbx,r15, and the
# value read follows without any compare of rbx against r15.
assert at(0x444575, 7) == bytes.fromhex('4c8bbfe8170000')       # mov r15,[rdi+0x17e8]: the head
assert at(0x444600, 3) == bytes.fromhex('498bdf')               # mov rbx,r15
assert at(0x444641, 4) == bytes.fromhex('488b5348')             # mov rdx,[rbx+0x48]
assert at(0x44464A, 3) == bytes.fromhex('8b4208')               # mov eax,[rdx+8]: the faulting read
assert at(0x444661, 8) == bytes.fromhex('4c8b7340' '488b5b48')  # the part (+0x40) and its control block (+0x48)
for p in range(0x444603, 0x444641):
    assert at(p, 3) not in (bytes.fromhex('493bdf'), bytes.fromhex('4c3bfb')), hex(p)   # no cmp rbx,r15
print('greyboss parts native audit: ok')
