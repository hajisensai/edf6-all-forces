"""The Vehicler on foot (src/vehicler_foot_logic.h, src/vehicler_foot.cpp) against the game's EDF.dll.

Read only. Without the supported EDF.dll only the source checks run (exit 77). With it:
- every kPrints row is the code at its address;
- AssultSoldier's vtable 0x17CDF28 slot 5 is the frame 0x550C30 the dash hook chains, and its call at 0x550C3C is the SoldierBase
  frame 0x59ABB0; the dash gate's `je` and the dash start's lea land where the notes say (past the dash, 0x552040);
- +0xD7B (the dash button) is written only by the input fill (slot 4: 0x550A30 -> 0x572DF0) and the clear in 0x572DF0, and read only
  by the four classes' frames and the dash itself: nothing in slot 5 before the check rewrites the byte the hook clears;
- the falloff 0x231080 is called by the gate at 0x235E8F, and every other caller is outside 0x235D50;
- 0x231080 run natively (DLL mapped without DllMain, only the CRT powf import resolved, as tests/zero_damage_native_audit.py):
  stock [0.5, 1] falls, the Gyrojet's AmmoDamageReduce (tools/zw_weapons.py) grows from AmmoDamage to a times, as the mirror says.
python tests/vehicler_foot_native_audit.py [EDF.dll]"""
from __future__ import annotations

import ctypes as C
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gamedir  # noqa: E402
import zw_weapons as zw  # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


with open(os.path.join(ROOT, 'src', 'vehicler_foot_logic.h'), encoding='utf-8') as f:
    LOGIC = f.read()
PRINTS = [(int(rva, 16), bytes.fromhex(hexs), what) for rva, hexs, what in
          re.findall(r'\{0x([0-9A-F]+),"([0-9a-f ]+)",\s*"([^"]*)"\}', LOGIC)]


def constant(name: str) -> int:
    m = re.search(r'\b' + name + r'=0x([0-9A-F]+)', LOGIC)
    return int(m.group(1), 16) if m else -1


# ---- source ----
check(len(PRINTS) == LOGIC.count('\n    {0x') and len(PRINTS) >= 9, 'every print parsed')
check(len({rva for rva, _, _ in PRINTS}) == len(PRINTS), 'prints unique')
check(all(len(b) <= 40 for _, b, _ in PRINTS), 'prints fit kMostPrintBytes')
gate = next((b for rva, b, _ in PRINTS if rva == constant('kGate')), b'')
check(len(gate) == 17 and constant('kGate') + 17 == constant('kRefresh'), 'the gate is the 17 bytes before the refresh')
check(constant('kStockFrame') == 0x550C30 and constant('kSoldierVtable') == 0x17CDF28, 'the frame slot')

path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if not path:
    game = gamedir.find()
    path = os.path.join(game, 'EDF.dll') if game else None
if not path or not os.path.isfile(path):
    print(f'vehicler_foot_native: {checks} source checks, {failed} failed; no EDF.dll, its part skipped')
    sys.exit(1 if failed else 77)
import pefile  # noqa: E402

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'vehicler_foot_native: {checks} source checks, {failed} failed; EDF.dll is not the supported build, skipped')
    sys.exit(1 if failed else 77)
base = pe.OPTIONAL_HEADER.ImageBase


def at(rva: int, n: int) -> bytes:
    return pe.get_data(rva, n)


def rel32(rva: int, length: int) -> int:
    return rva + length + struct.unpack_from('<i', at(rva + length - 4, 4))[0]


for rva, data, what in PRINTS:
    check(at(rva, len(data)) == data, f'0x{rva:X} {what}')
# the dash
check(struct.unpack('<Q', at(0x17CDF28 + 5 * 8, 8))[0] - base == 0x550C30, 'AssultSoldier slot 5 is 0x550C30')
check(struct.unpack('<Q', at(0x17CDF28 + 4 * 8, 8))[0] - base == 0x550A30, 'AssultSoldier slot 4 is 0x550A30')
check(at(0x550C3C, 1) == b'\xe8' and rel32(0x550C3C, 5) == 0x59ABB0, 'slot 5 first calls the SoldierBase frame 0x59ABB0')
check(at(0x550C5D, 2) == bytes.fromhex('7445') and 0x550C5F + 0x45 > 0x550C80, "the gate's je goes past the dash start")
check(rel32(0x550C79, 7) == 0x552040, 'the dash action 0x552040')
check(at(0x550A30, 1) == b'\xe9' and rel32(0x550A30, 5) == 0x572DF0, 'slot 4 is the input fill 0x572DF0')
check(at(0x572FFB, 1) == b'\xe8' and rel32(0x572FFB, 5) == 0x570650, '0x572DF0 calls 0x570650 (the +0xD7B write path)')

text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
code = text.get_data()
start = text.VirtualAddress
# the accesses to [reg+0xD7B] the vanilla index lists (exactly these nine instructions): each still there
sites = sorted(start + m.start() for m in re.finditer(re.escape(b'\x7b\x0d\x00\x00'), code))
known = {0x550C56: 'Ranger frame dash gate', 0x55250D: 'Ranger dash action', 0x5677CB: 'HeavyArmor frame',
         0x570855: 'input fill', 0x57094A: 'input fill', 0x570A80: 'input fill', 0x570B43: 'input fill',
         0x573B66: '0x572DF0 clear', 0x580B06: 'PaleWing frame'}
found = set()
for disp in sites:
    for back in (2, 3):   # opcode + ModRM (+ REX)
        rva = disp - back
        b = at(rva, back + 4)
        if rva in known and b[-4:] == b'\x7b\x0d\x00\x00':
            found.add(rva)
check(found == set(known), f'+0xD7B accesses as listed (found {sorted(hex(x) for x in found)})')
# the falloff's callers (the vanilla index's refs: these four)
for rva in (0x230CFF, 0x2324A9, 0x235E8F, 0x263BE2):
    check(at(rva, 1) == b'\xe8' and rel32(rva, 5) == 0x231080, f'0x{rva:X} calls 0x231080')

# ---- 0x231080 run natively ----
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8:
    print('vehicler_foot_native: not Windows x64, native run skipped')
else:
    k = C.WinDLL('kernel32', use_last_error=True)
    k.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
    k.LoadLibraryExW.restype = C.c_void_p
    k.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.POINTER(C.c_uint)]
    mod = k.LoadLibraryExW(os.path.abspath(path), None, 1)   # DONT_RESOLVE_DLL_REFERENCES: no DllMain
    check(bool(mod), 'EDF.dll mapped')
    if mod:
        check(at(0x12DA8E2, 6) == bytes.fromhex('ff2598bb4700'), 'powf thunk')
        crt = C.CDLL('api-ms-win-crt-math-l1-1-0.dll')
        old = C.c_uint()
        check(bool(k.VirtualProtect(mod + 0x1756480, 8, 4, C.byref(old))), 'powf slot writable')
        C.c_void_p.from_address(mod + 0x1756480).value = C.cast(crt.powf, C.c_void_p).value
        k.VirtualProtect(mod + 0x1756480, 8, old.value, C.byref(C.c_uint()))
        falloff = C.WINFUNCTYPE(C.c_float, C.c_void_p)(mod + 0x231080)
        core = C.create_string_buffer(0xC00)

        def run(d: float, a: float, b: float, age: int, life: int) -> float:
            struct.pack_into('<f', core, 0xA0C, d)
            struct.pack_into('<f', core, 0x9A0, 1.0)
            struct.pack_into('<f', core, 0xA10, a)
            struct.pack_into('<f', core, 0xA18, b)
            struct.pack_into('<i', core, 0xA08, life)
            struct.pack_into('<i', core, 0xAF8, age)
            return falloff(C.addressof(core))

        def mirror(d: float, a: float, b: float, age: int, life: int) -> float:
            t = min(max(age / life, 0.0), 1.0)
            return (1.0 - t) ** b * (d - d * a) + d * a

        a, b = zw.GYROJET_DAMAGE_REDUCE
        d, life = zw.GYROJET['AmmoDamage'], int(zw.GYROJET['AmmoAlive'])
        for args in [(100.0, 0.5, 1.0, 0, 60), (100.0, 0.5, 1.0, 30, 60), (100.0, 0.5, 1.0, 60, 60)] + \
                [(d, a, b, age, life) for age in (0, 15, 30, 75, 120, 150, 300)]:
            got, want = run(*args), mirror(*args)
            check(abs(got - want) <= 1e-3 * (abs(want) + 1), f'native falloff {args}: {got} vs {want}')
        check(abs(run(d, a, b, 0, life) - d) < 1e-3 and abs(run(d, a, b, life, life) - d * a) < 1e-3,
              'the Gyrojet grows from AmmoDamage to a times')
print(f'vehicler_foot_native: {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
