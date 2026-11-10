"""Read-only audit: the seat inputs the stock vehicle classes read, against the keys the plugin gives their add-on
weapons (src/payload.cpp FreeSecondary / SwitchButton, src/crew.h VehicleFire4Button, pylib/vcobjects.py STOCK_MOUNTS).

The player, 2026-11-08 (docs/feedback-2026-11-08-grape-keys.md): on the Grape 「LT and LB are assigned to forward and
reverse movement ... you cannot move forward unless you keep firing the coaxial machine gun, and you cannot move backward
unless you change your weapon loadout」. Pinned (docs/stock-payload-re.md §2.1):
  - the Car (vtable 0x17E01B0, slot 55 0x65A390, its player path 0x65A548..0x65A61C) reads seat +0x2E0 as its
    throttle, button 0x10 (LB) as its brake, 0x01 (A) its handbrake, 0x40 (L3) its horn; 0x20 (RB) calls its slot 83,
    a bare `ret 0`; it tests no other button;
  - the 505, 601 and 603 inputs (0x61ACD0, 0x620790, 0x621460, and the functions they call two levels down) read the
    primary trigger +0x2E4 and no +0x2E0 or button word;
  - the plugin: the coax keeps the second trigger only on the 505 / 601, the Grape's coax is on fire key 4 whose pad
    default (RB) the Car leaves unread, the Car's store switch is a button the Car never tests (X), LB elsewhere.

No game process, no game writes: EDF.dll is only read as a file. Usage: python tests/car_input_native_audit.py [EDF.dll]
Without an argument the game is looked up (pylib/gamedir.py); no supported EDF.dll: SKIP (77).
"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib')]
if not __debug__:
    raise RuntimeError('Run without -O: contract assertions must execute')

import gamedir  # noqa: E402

dll = Path(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else None
if dll is None or not dll.is_file():
    game = gamedir.find()
    dll = Path(game) / 'EDF.dll' if game else None
if dll is None or not dll.is_file():
    print('SKIP: no EDF.dll')
    raise SystemExit(77)
import pefile  # noqa: E402

pe = pefile.PE(str(dll), fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print('SKIP: not the supported EDF.dll')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
FUNCS = {e.struct.BeginAddress: e.struct.EndAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
IMAGE = pe.get_memory_mapped_image()
BASE = pe.OPTIONAL_HEADER.ImageBase


def data(at: int, n: int) -> bytes:
    return pe.get_data(at, n)


def slot(vtable: int, i: int) -> int:
    return struct.unpack_from('<Q', IMAGE, vtable + 8 * i)[0] - BASE


# --- The Car
CAR = 0x17E01B0
assert slot(CAR, 55) == 0x65A390 and slot(CAR, 82) == 0x65A680 and slot(CAR, 83) == 0x65A670
assert data(0x65A670, 3) == b'\xc2\x00\x00', 'Car slot 83 (the second weapon): ret 0'
assert data(0x65A680, 8) == bytes.fromhex('488b8938060000e9'), 'Car slot 82: the holder at +0x638 (the main gun)'
assert FUNCS.get(0x65A548) == 0x65A61C, 'the player path is its own unwind range'
path = data(0x65A548, 0x65A61C - 0x65A548)
assert path.startswith(bytes.fromhex('8b87e0020000' '418900')), '+0x2E0 is the drive input out[0] (the throttle)'
assert data(0x65A5A7, 8) == bytes.fromhex('f30f108fe4020000'), '+0x2E4 the primary trigger'
assert data(0x65A55A, 8) == bytes.fromhex('f30f1005') + struct.pack('<i', 0x1C36994 - 0x65A562)
assert struct.unpack('<f', data(0x1C36994, 4))[0] == 1.0, 'LB: brake 1.0'
tested = {path[m.end()] for m in re.finditer(re.escape(bytes.fromhex('f687e8020000')), path)}
assert tested == {0x10, 0x01, 0x20}, sorted(tested)
assert data(0x65A605, 12) == bytes.fromhex('0fb687e8020000' 'c0e806' '2401'), 'button 0x40: shifted out (the horn)'
# both 0x20 tests (0x65A505 in the AI frame's tail, 0x65A5C4 the player's) only call slot 83
for at in (0x65A505, 0x65A5C4):
    assert data(at, 8) == bytes.fromhex('f687e8020000' '20' '74'), hex(at)   # test 0x20; je
    assert data(at + 9, 12) == bytes.fromhex('488b03' '488bcb' 'ff9098020000'), hex(at)   # call [vtable+0x298]: slot 83
CAR_READS = {'trigger2': True, 0x01: 'handbrake', 0x10: 'brake', 0x40: 'horn'}   # 0x20: slot 83, nothing

# --- The keyboard: which key-config action feeds which seat input (docs/keyconfig-re.md §8)
# HumanBase 0x572DF0 fills the seat inputs from row (seat +0x2B4) of the key objects, 7 of 0x18 bytes each:
# +0x940 -> +0x2E0, +0x958 -> +0x2E4, +0x970 -> bit 0x10, +0x988 -> bit 0x20, +0x9A0 -> bit 0x01, +0x9B8 -> bit 0x40,
# +0x9D0 -> bit 0x08 (row 3 only).
for at, obj in ((0x5733B9, 0x940), (0x5733D3, 0x958), (0x573413, 0x9A0), (0x5734F0, 0x970), (0x573521, 0x988), (0x573552, 0x9B8)):
    assert data(at, 7) == bytes.fromhex('4538a8') + struct.pack('<I', obj), hex(at)   # cmp [r8+obj], r13b
assert data(0x57351D, 4) == bytes.fromhex('6683c920'), '+0x988 sets bit 0x20 (RB)'
assert data(0x57354E, 4) == bytes.fromhex('6683c940'), '+0x9B8 sets bit 0x40 (L3)'
# 0x78890 binds each row's 7 key objects to the actions of a 7-byte row table it builds at [rbp+0x48] (0x4E: none).
ROWS = bytes.fromhex('3235334d34364e' '3837' '4e4e4e394e' '3b3a3d3c3e4e4e' '4241403f4e4344' '48474645494e4e'
                     '4c4a4e4b4e4e4e' '38374e4e4e4e4e')
moves = bytes.fromhex('c74548' '3235334d' 'c7454c' '34364e38' 'c74550' '374e4e4e' 'c74554' '394e3b3a' 'c74558' '3d3c3e4e'
                      'c7455c' '4e424140' 'c74560' '3f4e4344' 'c74564' '48474645' 'c74568' '494e4e4c' 'c7456c' '4a4e4b4e'
                      'c74570' '4e4e3837' 'c74574' '4e4e4e4e' 'c64578' '4e')
assert data(0x7A536, len(moves)) == moves, "the key objects' row table (0x7A536)"
assert ROWS[3 * 7 + 6] != 0x4E and all(ROWS[r * 7 + 6] == 0x4E for r in range(7) if r != 3), 'bit 0x08: row 3 only'
DRIVE = ROWS[0:7]   # the Car's row: its throttle (+0x2E0) is the drive page's DriveAccel 0x32, on no other row
assert DRIVE[0] == 0x32 and all(ROWS[r * 7] != 0x32 for r in range(1, 7))
assert DRIVE[3] == 0x4D, "the drive page's 「DriveHorn」 (default right button) is bit 0x20: the Car's empty slot 83"
assert DRIVE[5] == 0x36, "the drive page's 「TanksAttack2」 (default middle button) is bit 0x40: the Car's horn"
HORN_DEFAULT_VK = 0x04   # 0x36's stock default, the middle button (0xD52F0; tests/keyconfig_native_audit.py)
free_look = int(re.search(r'int freeLookKey=(0x[0-9A-Fa-f]+|\d+);', (ROOT / 'src' / 'crew.h').read_text(encoding='utf-8')).group(1), 0)
assert free_look != HORN_DEFAULT_VK, "free look on the key that sounds the Grape's horn (2026-11-10)"


# --- The 505 / 601 / 603: the primary trigger and the left stick, no +0x2E0, no button word
def calls(begin: int, end: int) -> set[int]:
    out = set()
    code = data(begin, end - begin)
    for i in range(len(code) - 4):
        if code[i] == 0xE8:
            t = begin + i + 5 + struct.unpack_from('<i', code, i + 1)[0]
            if t in FUNCS and 0x5F0000 <= t < 0x6A0000:
                out.add(t)
    return out


def reads(begin: int, depth: int, seen: set[int]) -> list[tuple[int, int]]:
    if begin in seen:
        return []
    seen.add(begin)
    end = FUNCS[begin]
    code = data(begin, end - begin)
    hits = [(begin + m.start(), d) for d in (0x2E0, 0x2E8) for m in re.finditer(re.escape(struct.pack('<I', d)), code)]
    if depth:
        for t in sorted(calls(begin, end)):
            hits += reads(t, depth - 1, seen)
    return hits


assert len(reads(0x65A548, 0, set())) == 5, "the scan finds the Car's own reads (+0x2E0, four of the button word)"
for name, vtable, fn in (('505', 0x17DADB0, 0x61ACD0), ('601', 0x17DC250, 0x620790), ('603', 0x17DC620, 0x621460)):
    assert slot(vtable, 55) == fn, name
    assert data(fn + 0x44, 8) == bytes.fromhex('f30f108be4020000'), f'{name}: +0x2E4 the primary trigger'
    assert reads(fn, 2, set()) == [], f'{name} reads +0x2E0 or the button word'

# --- The plugin's keys
payload = (ROOT / 'src' / 'payload.cpp').read_text(encoding='utf-8')
crew_h = (ROOT / 'src' / 'crew.h').read_text(encoding='utf-8')
free = re.search(r'constexpr bool FreeSecondary\(Class c\) noexcept \{ return ([^;]*); \}', payload).group(1)
assert 'Class::car' not in free and 'Class::other' not in free, 'the Car drives on +0x2E0: no coax there'
assert set(re.findall(r'Class::(\w+)', free)) == {'tank505', 'tank601'}, free
buttons = dict((k, int(v, 0)) for k, v in re.findall(r'kButton(\w+)=(0x[0-9A-Fa-f]+)', payload))
switch = re.search(r'SwitchButton\(Class c\) noexcept \{ return c==Class::car \? kButton(\w+) : kButton(\w+); \}', payload)
car_switch, other_switch = buttons[switch.group(1)], buttons[switch.group(2)]
assert car_switch not in CAR_READS and car_switch != 0x20, 'the Car\'s switch on a button it never tests'
assert other_switch == 0x10
XINPUT_TO_SEAT = {0x1000: 0x01, 0x2000: 0x02, 0x4000: 0x04, 0x8000: 0x08, 0x100: 0x10, 0x200: 0x20, 0x40: 0x40, 0x80: 0x80}
fire4 = int(re.search(r'vehicleFire4Button=(0x[0-9A-Fa-f]+|\d+)', crew_h).group(1), 0)
assert XINPUT_TO_SEAT.get(fire4) == 0x20, 'fire key 4 on a pad: RB, which the Car leaves to its empty slot 83'
import vcobjects as vc  # noqa: E402
coax = vc.STOCK_MOUNTS['ifv']['MG401']
assert coax.key == 4 and vc.STORES[vc.mounted('MG401', coax)].role == 'coax', 'the Grape\'s coax on fire key 4'
print("car input audit: Car reads +0x2E0 throttle, LB brake, A handbrake, L3 horn, RB nothing; the keyboard's horn is the "
      "drive page's middle-button row, free look off it; 505/601/603 no +0x2E0; "
      f'the Grape\'s coax on key 4 (pad {fire4:#x}), its switch on seat button {car_switch:#x}: OK')
