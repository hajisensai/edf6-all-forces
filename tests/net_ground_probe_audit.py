"""Read-only native audit for the ground-vehicle rows of the NET probe (src/netprobe.cpp, docs/online-re.md 4.6).

Binds every offset the `NET ground` row reads to the instructions of the real EDF.dll that write or read it:
the CarBase / TankBase network step (slot 51 0x672AD0), the replication writer (0x676B40 / 0x676F50) and reader
(0x66BAC0). Never loads or runs a game DLL.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: contract assertions must execute')

import capstone
import pefile

p = pefile.PE(sys.argv[1], fast_load=True)
assert p.FILE_HEADER.TimeDateStamp == 0x678CCB46 and p.FILE_HEADER.Machine == 0x8664
base = p.OPTIONAL_HEADER.ImageBase
m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
checks = 0


def code(at: int, size: int):
    return {i.address: f'{i.mnemonic} {i.op_str}'.rstrip() for i in m.disasm(p.get_data(at, size), at)}


def expect(rows, at: int, text: str):
    global checks
    assert rows.get(at) == text, f'{at:X}: {rows.get(at)!r} != {text!r}'
    checks += 1


def slot(vtable: int, index: int) -> int:
    return struct.unpack('<Q', p.get_data(vtable + index * 8, 8))[0] - base


source = (Path(__file__).resolve().parents[1] / 'src' / 'netprobe.cpp').read_text(encoding='utf-8-sig')


def const(name: str) -> int:
    match = re.search(r'\b' + name + r'\s*=\s*(0x[0-9A-Fa-f]+|\d+)', source)
    assert match, f'missing {name}'
    return int(match[1], 0)


# The classes whose main slot 51 the probe recognizes: CarBase, TankBase and the stock tanks / cars.
GROUND = const('kGroundSlot51')
assert GROUND == 0x672AD0 and const('kSlot51') == 51
for vtable in (0x17E0A80, 0x17E1828, 0x17D8B50, 0x17D8FA0, 0x17D9458, 0x17DC250, 0x17DC620, 0x17DCAB0):
    assert slot(vtable, 51) == GROUND, f'{vtable:X} slot 51'
    checks += 1
# 404's NetworkObject: writer / reader.
assert slot(0x17D96F0, 7) == 0x676B40 and slot(0x17D96F0, 8) == 0x66BA60
checks += 1

step = code(0x672C9B, 0x40)
expect(step, 0x672CB1, 'mov ecx, dword ptr [rsi + 0x2624]')        # the stale-frame count
assert const('kGroundStale') == 0x2624
reader = code(0x66BAC0, 0x60)
expect(reader, 0x66BAE8, 'lea rdx, [rcx + 0x2630]')                # received position
expect(reader, 0x66BAF7, 'mov byte ptr [rbx + 0x2650], 0')         # position settled cleared by a packet
expect(reader, 0x66BB04, 'lea rdx, [rbx + 0x2640]')                # received rotation
expect(reader, 0x66BB13, 'mov byte ptr [rbx + 0x2651], 0')         # rotation settled cleared by a packet
assert const('kGroundRxPos') == 0x2630 and const('kGroundRxSettled') == 0x2650
writer = code(0x676B40, 0x80)
expect(writer, 0x676B80, 'add rcx, -0x120')                        # this = vehicle + 0x120
expect(writer, 0x676BA7, 'lea rcx, [rsi + 0x2540]')                # the position throttle (this-relative)
assert const('kGroundTx') == 0x120 + 0x2540
serialize = code(0x676F82, 0x20)
expect(serialize, 0x676F92, 'movups xmmword ptr [rcx + 0x26b0], xmm0')   # the last sent position
assert const('kGroundTxPos') == 0x26B0
# The AI-list bit the row prints is the one 0x62D850 tests (the writer's no-velocity branch).
expect(code(0x62D850, 8), 0x62D850, 'test byte ptr [rcx + 0x1a], 8')
assert const('kListFlags') == 0x1A
print(f'net ground probe audit: {checks} checks')
