"""Optional check of the EDF.dll code behind a soldier frozen in EDF6's climb (2026-10-19: 「NPC有的会立正」;
tools/make_edf41_objects.py SOLDIER_CLIMB_STATES / drop_climb, src/npcai.cpp StuckClimb).

    python tests/npc_climb_native_audit.py <EDF.dll>

Pass an installed EDF.dll (TimeDateStamp 0x678CCB46); without one it exits 77 (skipped). Static only: reads the DLL's
bytes (pefile), never loads or runs it. What it holds:
  - the climb is the soldier classes' slot 72 (0x5790A0), entered from the ledge test 0x57BF90 through the thunk 0x56C9A8
    (jmp [rax+0x240]); slot 49 (0x598F00) answers 1 for a soldier without a pad (+0x340): every NPC may climb;
  - human_climb_settings is optional: read at 0x56F0E3 and skipped when absent (test r8; je), and the ledge test checks
    the climb data (+0x1298) first;
  - the climb states' indices are resolved into +0x1530..+0x153C (the four names, in that order), the entry writes the
    action limits 0x62D (the only such write in the code: src/npcai.cpp kClimbLimits) and plays the index with 0x116BFC0,
    which refuses -1 without a change;
  - the state's update leaves only through 0x116BBE0 on the main player (+0x1420), which is never true for a state of end
    type 0 (end -1.0, 0x116C2B3): the freeze.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

if sys.platform != 'win32' or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass a supported EDF.dll path')
    raise SystemExit(77)
import pefile  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll TimeDateStamp {pe.FILE_HEADER.TimeDateStamp:#x}')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def at(rva: int, hexbytes: str, why: str) -> None:
    want = bytes.fromhex(hexbytes)
    check(img[rva:rva + len(want)] == want, f'{rva:#x}: {why}')


def rel32(rva: int, size: int) -> int:
    """The target of the rel32 at the end of the `size`-byte instruction at `rva`."""
    return rva + size + struct.unpack_from('<i', img, rva + size - 4)[0]


def wide(rva: int) -> str:
    end = rva
    while img[end:end + 2] != b'\0\0':
        end += 2
    return img[rva:end].decode('utf-16le')


def slot(vtable: int, index: int) -> int:
    return struct.unpack_from('<Q', img, vtable + 8 * index)[0] - base


# The four soldier classes (src/npcai.cpp kSoldiers): AssultSoldier, Engineer, HeavyArmor, PaleWing.
VTABLES = (0x17CDF28, 0x17CF100, 0x17CF5B8, 0x17D0FF8)
for vt in VTABLES:
    check(slot(vt, 72) == 0x5790A0, f'{vt:#x} slot 72 is the climb state 0x5790A0')
    check(slot(vt, 49) == 0x598F00, f'{vt:#x} slot 49 is 0x598F00')
at(0x56C9A8, '488b01ffa040020000', 'the climb thunk: jmp [rax+0x240] (slot 72)')
at(0x57C028, '488d05', 'the ledge test names the climb thunk')
check(rel32(0x57C028, 7) == 0x56C9A8, 'the ledge test enters the climb state')
at(0x598F0A, '4883b94003000000', 'slot 49: a soldier without a pad...')
at(0x598F60, 'b001', '...may climb')
at(0x57BF9F, '4883b99812000000', 'the ledge test checks the climb data first')
at(0x57BFAD, '0f84', 'and leaves without it')
check(rel32(0x57BFAD, 6) == 0x57C070, 'to its no-climb return')
# human_climb_settings optional
at(0x56F0E3, '4c8d05', 'the climb settings key is read')
check(wide(rel32(0x56F0E3, 7)) == 'human_climb_settings', 'by the name human_climb_settings')
at(0x56F0FD, '4d85c00f84', 'and skipped when absent')
# the climb states' indices
names = ('common_elude_large_up', 'common_elude_middle_over', 'common_elude_middle_up', 'common_elude_small_up')
for i, (lea, store) in enumerate(((0x5784D4, 0x5784E9), (0x5784F2, 0x578507), (0x578510, 0x578525), (0x57852E, 0x578543))):
    at(lea, '4c8d05', f'climb state {i} named')
    check(wide(rel32(lea, 7)) == names[i], f'climb state {i} is {names[i]}')
    at(store, '8986' + struct.pack('<I', 0x1530 + 4 * i).hex(), f'its index at +{0x1530 + 4 * i:#x}')
# the entry: action limits 0x62D, the only writer of that value
at(0x5791DA, 'c786d00500002d060000', 'the climb entry writes action limits 0x62D')
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = img[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
writes = [m.start() for m in re.finditer(rb'\xc7[\x80-\x87]\xd0\x05\x00\x00\x2d\x06\x00\x00', code)]
check(writes == [0x5791DA - text.VirtualAddress], f'0x62D is written to +0x5D0 only by the climb entry ({len(writes)})')
at(0x57945C, '458bc7', 'the entry plays the picked index...')
at(0x579476, 'e8', '...with the CAS play')
check(rel32(0x579476, 5) == 0x116BFC0, 'which is 0x116BFC0')
at(0x116BFE0, '4183f8ff750e32c0', '0x116BFC0 refuses index -1 and changes nothing')
# the update's one exit
at(0x5798D2, '8b9620140000', 'the update asks about the main player')
at(0x5798DF, 'e8', 'whether its state is done')
check(rel32(0x5798DF, 5) == 0x116BBE0, 'through 0x116BBE0')
at(0x5798E4, '84c074', 'and leaves only when it is')
at(0x116BBF9, 'f30f108af00000000f57c00f2fc1760332c0c3', 'done is false while the end is negative')
at(0x116C2B3, 'c783f0000000000080bf', 'a state of end type 0 ends at -1.0')
# the plugin's constants
src = (ROOT / 'src' / 'npcai.cpp').read_text(encoding='utf-8')
check('kClimbLimits=0x62D' in src and 'kClimbStates=0x1530' in src, 'src/npcai.cpp reads these offsets')
print(f'OK: {checks} checks')
