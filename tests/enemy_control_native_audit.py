"""Read-only native enemy player-control contract audit, supported EDF.dll only.

Checks the production profile table against primary RTTI, complete virtual-table
lengths, native input/AI entries and real pad byte reads. Never loads the DLL.
Missing explicit file returns CTest skip code 77.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O')
import pefile

root = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
checks = 1

def check(ok, why):
    global checks
    assert ok, why
    checks += 1

profiles = (root / 'src/enemy_control_native_profiles.inc').read_text(encoding='utf-8-sig')
rows = re.findall(r'\{"([^"]+)",0x([A-F0-9]+),0x([A-F0-9]+),0x([A-F0-9]+),0x([A-F0-9]+),(\d+),0x([A-F0-9]+),Kind::(\w+),(\d+),(\d+)\}', profiles)
check(len(rows) >= 31, 'profile table not found')
for name, vt, fn, ai, hashed, slots, mask, kind, axes, slot in rows:
    vt, fn, ai, slots, slot = int(vt, 16), int(fn, 16), int(ai, 16), int(slots), int(slot)
    check(slot in (4, 5), name + ' native input phase')
    col = struct.unpack_from('<Q', img, vt - 8)[0] - base
    sig, offset, cd, td, hierarchy, self_rva = struct.unpack_from('<6I', img, col)
    check((sig, offset, cd, self_rva) == (1, 0, 0, col), name + ' primary RTTI COL')
    actual_name = img[td + 16:img.index(b'\0', td + 16)].decode()
    check(actual_name == '.?AV' + name + '@@', name + ' exact native type')
    check(struct.unpack_from('<Q', img, vt + slot * 8)[0] == base + fn, name + ' real input slot')
    check(struct.unpack_from('<Q', img, vt + 7 * 8)[0] == base + ai, name + ' real AI slot')
    values = struct.unpack_from('<' + 'Q' * (slots + 1), img, vt)
    check(all(base + 0x1000 <= x < base + 0x1755000 for x in values[:-1]) and
          not base + 0x1000 <= values[-1] < base + 0x1755000, name + ' full vtable extent')

sigs = (root / 'src/enemy_control_native_signatures.inc').read_text(encoding='utf-8-sig')
prints = re.findall(r'\{0x([A-F0-9]+),\{([^}]+)\}\}', sigs)
for addr, values in prints:
    want = bytes(int(x, 16) for x in re.findall(r'0x([A-F0-9]{2})', values))
    rva = int(addr, 16)
    check(len(want) == 16 and img[rva:rva + 16] == want, 'production signature ' + addr)

def target(at, opcode=0xE8):
    check(img[at] == opcode, 'branch opcode ' + hex(at))
    return at + 5 + struct.unpack_from('<i', img, at + 1)[0]

check(target(0x343DD0, 0xE9) == 0x53E750, 'Berserker input thunk reaches WalkingEnemyBase')
check(target(0x42D569) == 0x4A8FD0, 'GiantDango input reaches InsectBase')
check(target(0x4ACD80) == 0x4BC8B0, 'Monster501 input reaches MonsterBase')
check(target(0x444EBD) == 0x470D10, 'Boss update reaches Humanoid native input')
check(target(0x46178F) == 0x470D10, 'Martian update reaches Humanoid native input')
check(target(0x462E5F) == 0x461590, 'Venus update reaches Martian/Humanoid native input')
check(target(0x4A6780, 0xE9) == 0x470D10, 'Humanoid_Basic update thunk')
# Actual supported binary reads one byte at button 0, never a float. Held and
# pressed occupy adjacent bytes of records 0x18 apart, starting at pad+0x70.
for at, want in ((0x470EB0, '0fb64870'), (0x470F00, '0fb68001010000'),
                 (0x33F7C6, '384271'), (0x4A9187, '0fb687d0000000'),
                 (0x4A9194, '0fb687e8000000'), (0x4A91A1, '0fb68700010000'),
                 (0x4A91AE, '0fb68718010000')):
    raw = bytes.fromhex(want)
    check(img[at:at + len(raw)] == raw, 'native button record read ' + hex(at))
# Native player entry gates and control pointer, different families/phases.
for at, want in ((0x4A9057, '4438bb54030000'), (0x4A9064, '488bbb40030000'),
                 (0x53E78F, '4038b354030000'), (0x4BC907, '4438a754030000')):
    raw = bytes.fromhex(want)
    check(img[at:at + len(raw)] == raw, 'native player branch ' + hex(at))
# Native actions use the object's own 64-bit LCG state.
# Ant's real capture action demonstrates read, multiply, add and write back.
for at, want in ((0x40E95D, '4c8b81e8030000'), (0x40E97F, '48b86589076c658b585d'),
                 (0x40E989, '4c0fafc0'), (0x40E992, '4981c0c39e2600'),
                 (0x40E999, '4c8981e8030000')):
    raw = bytes.fromhex(want)
    check(img[at:at + len(raw)] == raw, 'per-object skill RNG instruction ' + hex(at))
check(target(0x4F955B) == 0x4F74D0, 'Carrier508 update reaches native player input')
for at, want in ((0x6CDE0A, '488b02'), (0x6CDE15, '48b96589076c658b585d'),
                 (0x6CDE27, '480fafc1'), (0x6CDE3F, '4805c39e2600'),
                 (0x6CDE45, '488902'), (0x35A790, '488d9790040000')):
    raw = bytes.fromhex(want)
    check(img[at:at + len(raw)] == raw, 'independent SGO range RNG ' + hex(at))
check(target(0x35A797) == 0x6CDE00, 'range sampler consumes object+490 state')

source = (root / 'src/enemy_control_native.cpp').read_text(encoding='utf-8-sig')
check('__finally' in source and 'savedPad' in source and 'savedPlayer' in source, 'scoped native controller restoration')
check('!Readable(dt' not in source, 'opaque native second argument is forwarded')
check('0x118A4B0' not in source and '0x118A3E0' not in source, 'no AI-list reconstruction')
print(f'{checks} enemy-control native checks passed; {len(rows)} exact native classes, {len(prints)} fingerprints; no game execution')
