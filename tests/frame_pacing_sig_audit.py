"""The two waits src/frame_pacing.cpp redirects, checked against the real EDF.dll read as a file (never loaded): each
site is `call [Sleep]` with the argument load the plugin compares before it, both import slots are KERNEL32's Sleep,
and the loops around them are the ones the comments describe (the window thread's loop back to its pump, the frame
step's loop back to its tick count 0x11DAE60), and the tick clock TickWait reads to wait for the next boundary
(0x11DAE60 byte for byte, its base 0x2139D10 set from QPC at start-up, its last boundary 0x2139D18, the period
freq*100/5994 frame_pacing_logic.h repeats). An EDF.dll path as the argument, else the game gamedir finds; exit 77
(skipped) without one."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if not path:
    try:
        import gamedir
        path = os.path.join(gamedir.find_or_dev(), 'EDF.dll')
    except Exception:
        path = None
if not path or not os.path.isfile(path):
    print('SKIP: no EDF.dll')
    raise SystemExit(77)
import pefile  # noqa: E402

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the signatures are for')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
imports = {i.address - base: (d.dll.decode().upper(), (i.name or b'').decode())
           for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports}
src = open(os.path.join(ROOT, 'src', 'frame_pacing.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


consts = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', src)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'\b(k\w+(?:Sig|Arg))\[\]=\{([^}]*)\}', src)}
check(set(arrays) == {'kPumpArg', 'kTickArg', 'kPumpSig', 'kTickSig', 'kTickCountSig'}, f'the signature arrays: {sorted(arrays)}')


def rel_target(at: int, length: int) -> int:
    return at + length + struct.unpack_from('<i', img, at + length - 4)[0]


for site, name in (('kPumpSleep', 'Pump'), ('kTickSleep', 'Tick')):
    rva = consts[site]
    sig, arg = arrays[f'k{name}Sig'], arrays[f'k{name}Arg']
    check(img[rva:rva + 6] == sig, f'{site} {rva:#x}: {img[rva:rva + 6].hex(" ")} != {sig.hex(" ")}')
    check(img[rva - len(arg):rva] == arg, f'{site}: the argument load before it {img[rva - len(arg):rva].hex(" ")}')
    slot = rel_target(rva, 6)
    check(imports.get(slot) == ('KERNEL32.DLL', 'Sleep'), f'{site}: its import slot {slot:#x} is {imports.get(slot)}')
check(img[consts['kPumpSleep'] - 2:consts['kPumpSleep']] == b'\x33\xc9', 'the window thread sleeps 0 ms')
check(img[consts['kTickSleep'] - 5:consts['kTickSleep']] == b'\xb9\x01\x00\x00\x00', 'the frame step sleeps 1 ms')

# The window thread's loop 0x1184480..0x11844F4: its pump 0x1183880, the wait, then `cmp byte [rdi+0x29],0; je` back.
after = consts['kPumpSleep'] + 6
check(img[after:after + 4] == b'\x80\x7f\x29\x00' and img[after + 4] == 0x74 and
      after + 6 + struct.unpack_from('<b', img, after + 5)[0] == 0x1184480, 'the window loop goes back after the wait')
check(img[0x11844D8] == 0xE8 and rel_target(0x11844D8, 5) == 0x1183880, 'the window loop pumps through 0x1183880')
# The frame step: the wait, `call 0x11DAE60` (the 1/60 s ticks since the last frame), `test eax,eax; je` back to the wait.
after = consts['kTickSleep'] + 6
check(img[after] == 0xE8 and rel_target(after, 5) == 0x11DAE60, 'the frame step counts its ticks after the wait')
je = after + 5 + 4   # mov ebx,eax (2) + test eax,eax (2)
check(img[je] == 0x74 and je + 2 + struct.unpack_from('<b', img, je + 1)[0] == consts['kTickSleep'] - 5,
      'the frame step waits again while no tick has passed')
# Both calls in the frame step go to the tick clock (0x11A2F4F before the loop, 0x11A2F6B after the wait).
check(img[0x11A2F4F] == 0xE8 and rel_target(0x11A2F4F, 5) == consts['kTickCount'], 'the frame step counts its ticks first')
check(rel_target(after, 5) == consts['kTickCount'], 'the call after the wait is kTickCount')

# The tick clock TickWait reads (frame_pacing_logic.h): the whole function is the signature the plugin compares, and
# what it does is what the header repeats.
tc = consts['kTickCount']
sig = arrays['kTickCountSig']
check(img[tc:tc + len(sig)] == sig, f'kTickCount {tc:#x}: the function is not kTickCountSig')
check(img[tc + len(sig) - 1] == 0xC3 and img[tc + len(sig)] == 0xCC, 'kTickCountSig is the function to its ret')


def rip(at: int, length: int) -> int:   # the rip-relative operand of the instruction at `at`
    return at + length + struct.unpack_from('<i', img, at + length - 4)[0]


check(img[tc + 6:tc + 9] == b'\x48\x8d\x0d' and rip(tc + 6, 7) == consts['kTickBase'], 'lea rcx,[kTickBase]')
check(img[tc + 13] == 0xE8 and rel_target(tc + 13, 5) == 0x66CD0, 'elapsed = 0x66CD0(&kTickBase)')
qpc_slot = rip(0x66CDE, 6)
check(img[0x66CDE:0x66CE0] == b'\xff\x15' and imports.get(qpc_slot) == ('KERNEL32.DLL', 'QueryPerformanceCounter'),
      '0x66CD0 reads QueryPerformanceCounter')
check(img[0x66CE9:0x66CEC] == b'\x48\x2b\x03', '0x66CD0 subtracts the reading at its argument: QPC - base')
check(img[tc + 21] == 0xE8 and rel_target(tc + 21, 5) == 0x66D60, 'the period from 0x66D60')
check(img[0x66D69:0x66D6B] == b'\xff\x15' and
      imports.get(rip(0x66D69, 6)) == ('KERNEL32.DLL', 'QueryPerformanceFrequency'), '0x66D60 is QueryPerformanceFrequency')
check(img[tc + 26:tc + 29] == b'\x4c\x8b\x05' and rip(tc + 26, 7) == consts['kTickLast'], 'mov r8,[kTickLast]')
check(img[tc + 33:tc + 37] == b'\x48\x6b\xc8\x64', 'freq*100')
magic = struct.unpack_from('<Q', img, tc + 42)[0]
check(img[tc + 40:tc + 42] == b'\x48\xb8' and img[tc + 61:tc + 65] == b'\x49\xc1\xe9\x0c', 'the multiply-shift by 76')
check(magic == -(-(1 << 76) // 5994) and (magic * 5994 - (1 << 76)) < (1 << 12),
      f'{magic:#x} >> 76 is an exact division by 5994 for every 64-bit numerator')
check(re.search(r'freq\*100u/5994u', open(os.path.join(ROOT, 'src', 'frame_pacing_logic.h'), encoding='utf-8').read())
      is not None, 'frame_pacing_logic.h TickPeriod divides by the same 5994')
check(img[tc + 84:tc + 87] == b'\x4c\x89\x05' and rip(tc + 84, 7) == consts['kTickLast'], 'mov [kTickLast],r8 (last += n*period)')
# kTickBase is set from QPC at start-up: a CRT initializer (0x316B0) and the game's init (0x11DAED0 from 0x11A3796).
for fn, sub in ((0x316B0, 0x66CA0), (0x11DAED0, 0x66CC0)):
    check(img[fn:fn + 3] == b'\x48\x8d\x0d' and rip(fn, 7) == consts['kTickBase'] and img[fn + 7] == 0xE9 and
          rel_target(fn + 7, 5) == sub, f'{fn:#x} hands kTickBase to {sub:#x}')
check(imports.get(rip(0x66CA9, 6)) == ('KERNEL32.DLL', 'QueryPerformanceCounter') and
      imports.get(rip(0x66CC0, 7)) == ('KERNEL32.DLL', 'QueryPerformanceCounter'), 'both store a QPC reading')
check(img[0x11A3796] == 0xE8 and rel_target(0x11A3796, 5) == 0x11DAED0, 'the game init resets kTickBase')
check(re.search(r'image\+kTickBase', src) and re.search(r'image\+kTickLast', src) and
      re.search(r'image,kTickCount,kTickCountSig', src), 'TickWait reads the words install compared the clock for')

# Each wait hooked at its own site: the window thread's must wake on input (PumpWait), the frame step's need not.
for site, sig, hook in (('kPumpSleep', 'kPumpSig', 'PumpWait'), ('kTickSleep', 'kTickSig', 'TickWait')):
    check(re.search(rf'RedirectImportCall\(image\+{site},{sig},reinterpret_cast<void\*>\(&{hook}\)', src) is not None,
          f'{hook} is what {site} is redirected to')
print(f'frame pacing signatures: {checks} checks ok')
