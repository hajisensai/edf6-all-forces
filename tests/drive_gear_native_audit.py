"""Read-only audit of where the cruise gears read each ground vehicle's own top speed (src/drivegear.cpp TopOf;
docs/vehicle-top-speed-re.md). No game process, no game writes; EDF.dll and Root.cpk are only read as files.

Usage: python tests/drive_gear_native_audit.py [EDF.dll]
Without an argument the game is looked up (pylib/gamedir.py); no supported EDF.dll: SKIP (77).

Pinned:
  - the gear table drivegear.cpp reads (kGearRows / kGearCount / kGearRow) is the one the native top getter 0x66CF00
    reads (the last forward row's speed) and the setup's scaler 0x66E1A0 scales;
  - every class drivegear.cpp gears (Geared) reaches, from its own slot 46 (setup), a base setup that calls 0x66E1A0
    with setup[1][1] as the speed factor;
  - the gear rows' speed is compared with the hull's linear velocity (0x11B1300, m/s) along its nose: m/s, not m a frame;
  - with Root.cpk: the stock objects' tops come out of their SGOs as setup[1][1] x the last forward row.
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
    print('SKIP: supported EDF.dll not found')
    raise SystemExit(77)

import capstone  # noqa: E402
import pefile  # noqa: E402

pe = pefile.PE(str(dll), fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: {dll} is not the supported Steam EDF.dll')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
checks = 0


def ok(cond: bool, what: str) -> None:
    global checks
    assert cond, what
    checks += 1
    print('PASS', what)


def code(at: int, n: int) -> list[str]:
    out = []
    for i in md.disasm(img[at:at + n * 16], at):
        out.append(f'{i.mnemonic} {i.op_str}'.strip())
        if len(out) == n:
            break
    return out


def calls(f: int, limit: int = 0x1400) -> list[tuple[int, int]]:
    out = []
    for i in md.disasm(img[f:f + limit], f):
        if i.mnemonic in ('call', 'jmp') and i.op_str.startswith('0x'):
            out.append((i.address, int(i.op_str, 16)))
        if i.mnemonic == 'int3':
            break
    return out


def slot(vt: int, index: int) -> int:
    return struct.unpack_from('<Q', img, vt + index * 8)[0] - base


src = (ROOT / 'src' / 'drivegear.cpp').read_text(encoding='utf-8')
const = {k: int(v, 16) for k, v in re.findall(r'\b(kGear\w+)=0x([0-9A-Fa-f]+)', src)}
ok(set(const) == {'kGearRows', 'kGearCount', 'kGearRow'}, f'drivegear.cpp names the gear table: {sorted(const)}')
rows, count, row = const['kGearRows'], const['kGearCount'], const['kGearRow']
ok(row == 0x10, 'a gear row is 16 bytes')

# The native top getter: max(|last forward row speed|, |reverse speed|) - the last forward row is [begin + count*16 - 16].
ok(code(0x66CF00, 6) == [f'mov rdx, qword ptr [rcx + {count:#x}]', f'mov rax, qword ptr [rcx + {rows:#x}]',
                         'add rdx, rdx', 'movss xmm2, dword ptr [rcx + 0x1788]',
                         code(0x66CF00, 6)[4], 'movaps xmm0, xmm2']
   and code(0x66CF27, 1) == ['movss xmm1, dword ptr [rax + rdx*8 - 0x10]'],
   '0x66CF00 reads the last forward row speed (begin +0x1760, 64-bit count +0x1770, 16-byte rows)')
# The setup's scaler: every row's speed x xmm1 (the up / down shift speeds follow it), its force x xmm2.
s = code(0x66E1A0, 16)
ok(s[0] == f'mov rdx, qword ptr [rcx + {rows:#x}]' and s[2] == f'mov rax, qword ptr [rcx + {count:#x}]'
   and s[4] == 'shl rax, 4' and 'mulss xmm1, dword ptr [rdx - 8]' in s and 'movss dword ptr [rdx - 0x18], xmm1' in s,
   '0x66E1A0 scales each row speed [row+0] by the speed factor (xmm1)')
# The simple gear (tanks): one row, its speed car_base_engine_simple_gear[0][0], the reverse its negative.
ok(code(0x66DA98, 1) == ['lea rcx, [rbx + 0x1758]'] and code(0x66DAB3, 2) == ['mov edx, 1', 'call 0x675ea0']
   and code(0x66DABD, 1) == [f'mov rdi, qword ptr [rbx + {rows:#x}]'] and code(0x66DB3B, 1) == ['movss dword ptr [rdi], xmm0'],
   'car_base_engine_simple_gear: a one-row table, row 0 speed from [0][0] (0x66DA98..0x66DB3B)')

# The three base setups (slot 46) call the scaler with setup[1][1] / setup[1][2].
BASES = {0x67FB00: (0x67FF2C, 0x67FF37, 'xmm10', 'xmm9'), 0x658F10: (0x659242, 0x65924C, 'xmm8', 'xmm7'),
         0x65A910: (0x65AD86, 0x65AD90, 'xmm8', 'xmm7')}
for f, (pre_at, at, speed, force) in BASES.items():
    pre = code(pre_at, 4)
    ok(pre[:2] == [f'movaps xmm2, {force}', f'movaps xmm1, {speed}'] and pre[3] == 'call 0x66e1a0',
       f'base setup {f:#x} calls 0x66E1A0 at {at:#x} (speed {speed}, force {force})')
ok(slot(0x17E1828, 46) == 0x67FB00 and slot(0x17DFDC8, 46) == 0x658F10 and slot(0x17E01B0, 46) == 0x65A910,
   'TankBase / BikeBase / Car slot 46 are those setups')
# The setup's speed factor is setup[1][1]: TankBase reads setup[1] then its element 1 into xmm10 (0x67FC59 / 0x67FCB5).
ok(code(0x67FB7F, 1) == ['mov dword ptr [rsp + 0x70], 1'] and code(0x67FC59, 1) == ['mov dword ptr [rbp - 0x70], 1']
   and code(0x67FCB5, 1) == ['cvtpd2ps xmm10, xmm0'], 'TankBase: the speed factor is setup[1][1]')
ok(code(0x65A994, 1) == ['mov dword ptr [rbp - 8], 1'] and code(0x65AA18, 1) == ['mov dword ptr [rbp - 8], 1']
   and code(0x65AA75, 1) == ['cvtpd2ps xmm8, xmm0'], 'Car: the speed factor is setup[1][1]')

# Every class drivegear.cpp gears reaches one of them from its own slot 46.
crew = (ROOT / 'src' / 'crew.cpp').read_text(encoding='utf-8')
vts = {name: int(vt, 16) for vt, name in re.findall(r'\{(0x[0-9A-Fa-f]+),[^{}"]*"(\w+)"', crew)}
geared = re.search(r'bool Geared\(.*?\{(.*?)\n\}', src, re.S)
ok(geared is not None, 'drivegear.cpp has Geared')
names = re.findall(r'"(\w+)"', geared.group(1))
ok(len(names) >= 13, f'Geared lists the stock ground classes ({len(names)})')
for name in names:
    ok(name in vts, f'{name}: crew.cpp kClasses has its vtable')
    s46 = slot(vts[name], 46)
    reach = s46 in BASES or any(t in BASES for _, t in calls(s46))
    ok(reach, f'{name}: slot 46 {s46:#x} runs a base setup that scales its gear table')

# Units: the forward speed the rows are compared with is the hull's linear velocity (0x11B1300, physics.cpp kGetLinVel,
# m/s) along +0x1B00: CarBase's frame (0x674074..0x67412A) and the gear choice (slot 58 0x66DF70) against [row+4].
ok(code(0x67407B, 1) == ['call 0x11b1300'] and code(0x67412A, 2) == ['movups xmm0, xmmword ptr [rdi + 0x1b00]',
                                                                    'mulps xmm0, xmmword ptr [rbp - 0x70]'],
   'CarBase frame: forward speed = linear velocity (0x11B1300) . nose (+0x1B00)')
ok(slot(0x17E0A80, 58) == 0x66DF70 and code(0x66DF7F, 2) == ['movups xmm0, xmmword ptr [rcx + 0x1b00]',
                                                             'mulps xmm0, xmmword ptr [r8]']
   and code(0x66DFFC, 1) == ['comiss xmm3, dword ptr [rbx + rcx*8 + 4]'],
   'gear choice (slot 58): that forward speed against the row shift speeds (same unit as the row speed: m/s)')

# With Root.cpk: the stock objects' tops from their SGOs (setup[1][1] x the last forward row).
try:
    import rootcpk
    import sgo
    g = rootcpk.Game(str(dll.parent))
except Exception as e:  # noqa: BLE001 - no Root.cpk: the binary checks above still stand
    g = None
    print(f'note: no Root.cpk ({e}); SGO cross-check not run')
if g is not None:
    WANT = {'VEHICLE401_STRIKER.SGO': 27.0, 'VEHICLE402_ROCKET.SGO': 5.0, 'VEHICLE403_TANK.SGO': 14.0,
            'VEHICLE404_BIGTANK.SGO': 6.0, 'V503_BIKE.SGO': 57.2, 'V505_TANK.SGO': 10.0, 'V507_RESCUETANK.SGO': 10.0,
            'V510_MASER.SGO': 6.0, 'V601_TANK.SGO': 10.0, 'V603_FLAK.SGO': 10.0, 'V607_ROBOTRUCK.SGO': 27.0}
    for name, want in WANT.items():
        d = sgo.load(data=g.read('OBJECT', name))
        if 'car_base_engine_gear' in d:
            last = d['car_base_engine_gear'][0][-1][0]
        else:
            last = d['car_base_engine_simple_gear'][0][0]
        top = d['vehicle_setup'][1][1] * last
        ok(abs(top - want) < 1e-3, f'{name}: top {top:.1f} m/s = setup[1][1] {d["vehicle_setup"][1][1]} x last row {last}')
print(f'drive_gear_native: {checks} checks passed')
