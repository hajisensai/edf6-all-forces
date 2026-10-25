"""Read-only PE audit of the native facts src/allclass.cpp (ini AllClassesRide) rests on, on the real EDF.dll.

python tests/allclass_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input returns skip code 77.

Holds, against the DLL itself (docs/all-classes-ride-re.md):
  - every byte signature src/allclass.cpp checks before it hooks (read out of the source, so the two cannot drift);
  - the seat build call 0x629C47 is a rel32 call of 0x62B430 and the only one in .text; the ride state's call 0x57AE8E
    is a rel32 call of 0x116C2D0 whose name argument is seat+0x10 (r8 = [rsi+0x10], rsi = [rider+0x1540]);
  - the stock seat test the widening feeds: 0x6346FC (CanRideSeat) and 0x62DD21 (CanRide) AND the human's +0x31C with
    the seat's +0x34 and test the seat's +0x30; the seat constructor sets +0x34 to all ones (0x6293A4);
  - 0x116C2D0 returns false at once on a name its CAS lacks (0x11664F0 == -1), so asking the bench pose next is safe.
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
    assert img[site] == 0xE8, hex(site)
    return site + 5 + struct.unpack_from('<i', img, site + 1)[0]


source = (ROOT / 'src' / 'allclass.cpp').read_text(encoding='utf-8')
arrays = {name: bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', source)}
consts = {name: int(value, 16) for name, value in re.findall(r'constexpr std::size_t (k\w+)=0x([0-9A-Fa-f]+);', source)}
where = {'kSeatBuildCallSig': consts['kSeatBuildCall'] - 3, 'kSeatBuildSig': consts['kSeatBuild'], 'kMaskStoreSig': 0x62B79C,
         'kVectorSig': 0x62BD57, 'kAppendSig': 0x62BE33, 'kRideStateSig': 0x57AE5C, 'kSetCasStateSig': consts['kSetCasState'],
         'kRiderPlaceSig': 0x5751D3}
assert set(arrays) == set(where), sorted(set(arrays) ^ set(where))
for name, rva in where.items():
    want = arrays[name]
    assert len(want) >= 6, name
    assert at(rva, len(want)) == want, f'{name} at {rva:#x}: {at(rva, len(want)).hex()} != {want.hex()}'
# The Install reads them at the same addresses this audit does.
for name, rva in (('kMaskStoreSig', 0x62B79C), ('kVectorSig', 0x62BD57), ('kAppendSig', 0x62BE33), ('kRideStateSig', 0x57AE5C),
                  ('kRiderPlaceSig', 0x5751D3)):
    assert f'Matches({rva:#X},{name}'.replace('0X', '0x') in source, name

assert rel32_target(consts['kSeatBuildCall']) == consts['kSeatBuild']
assert rel32_target(consts['kRideStateCall']) == consts['kSetCasState']
assert consts['kRideStateCall'] == 0x57AE5C + len(arrays['kRideStateSig']) - 5
# The ride's per-frame copy of the seat's matrix (seat+0x270..+0x2AF) into the rider (+0x60..+0x9F), then the normalise
# PlaceRider wraps (the one writer of the rider's position while riding: a write breakpoint on rider+0x94, 2026-10-25).
assert rel32_target(consts['kRiderPlaceCall']) == consts['kNormalize']
assert consts['kRiderPlaceCall'] == 0x5751D3 + len(arrays['kRiderPlaceSig']) - 5
assert consts['kRiderSeat'] == 0x1540 and at(0x5751D3, 7) == bytes.fromhex('488b8640150000')
# The only rel32 call of the seat build: every vehicle's seats come through the one site that is redirected.
calls = [m.start() for m in re.finditer(b'\xE8', img[lo:hi - 5])]
calls = [lo + p for p in calls if lo + p + 5 + struct.unpack_from('<i', img, lo + p + 1)[0] == consts['kSeatBuild']]
assert calls == [consts['kSeatBuildCall']], [hex(p) for p in calls]

# The stock seat tests: mov eax,[rdx+0x31C]; and eax,[r8+0x34]; test [r8+0x30],eax / mov eax,[rdi+4]; and eax,[rsi+0x31C];
# test [rdi],eax (rdi = seat+0x30 there: add rdi,0x30 at 0x62DCFB).
assert at(0x6346FC, 14) == bytes.fromhex('8b821c0300004123403441854030')
assert at(0x62DCFB, 4) == bytes.fromhex('4883c730')
assert at(0x62DD21, 11) == bytes.fromhex('8b47042386 1c03000085 07'.replace(' ', ''))
assert at(0x6293A4, 7) == bytes.fromhex('c74334ffffffff')
# 0x116C2D0: the name to an index (0x11664F0), -1: return false before any state is set.
assert at(0x116C2EA, 10) == bytes.fromhex('e801a2ffff83f8ff7512')
print('allclass native audit: ok')
