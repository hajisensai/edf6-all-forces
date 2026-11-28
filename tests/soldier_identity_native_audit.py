"""src/soldier_identity_logic.h's EDF.dll addresses against the real EDF.dll, read as a file (never loaded, the game never
touched): the four stock soldier vtables are AssultSoldier / PaleWing / HeavyArmor / Engineer by their RTTI, AssultSoldier's
vtable-8 is the locator kAssultSoldierCol (x64, offset 0, its own RVA at +0x14), that locator names kAssultSoldierTd and
the type descriptor's name is kAssultSoldierName. Also that the clones the new classes use keep vt[-1]
(edf41classes.cpp BuildClone copies it), the fact the online identity rests on.

An EDF.dll path as the argument, else the game gamedir finds; exit 77 (skipped) without one."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

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
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the addresses are for')
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


def source(name: str) -> str:
    with open(os.path.join(ROOT, 'src', name), encoding='utf-8') as f:
        return f.read()


logic = source('soldier_identity_logic.h')
k = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', logic)}
stock = [int(v, 16) for v in re.search(r'kStockVtables\[\]=\{([^}]*)\}', logic)[1].split(',')]
name = re.search(r'kAssultSoldierName\[\]="([^"]+)"', logic)[1]
check(k['kColTd'] == 0xC and k['kColSelf'] == 0x14 and k['kTdName'] == 0x10, 'the x64 locator / descriptor offsets')


def locator(vtable: int) -> tuple[int, tuple[int, ...]]:
    col = struct.unpack_from('<Q', img, vtable - 8)[0] - base
    return col, struct.unpack_from('<6I', img, col)


def td_name(td: int) -> str:
    end = img.index(b'\0', td + k['kTdName'])
    return img[td + k['kTdName']:end].decode('ascii')


for vtable, cls in zip(stock, ('AssultSoldier', 'PaleWing', 'HeavyArmor', 'Engineer')):
    col, f = locator(vtable)
    check(f[0] == 1 and f[1] == 0 and f[5] == col, f'{vtable:#x}: an x64 primary locator at {col:#x}')
    check(td_name(f[3]) == f'.?AV{cls}@@', f'{vtable:#x} is {cls}: {td_name(f[3])}')

col, f = locator(k['kAssultSoldierVtable'])
check(k['kAssultSoldierVtable'] == stock[0], 'AssultSoldier is the first stock vtable')
check(col == k['kAssultSoldierCol'], f'AssultSoldier vtable-8 -> {col:#x}, kAssultSoldierCol {k["kAssultSoldierCol"]:#x}')
check(f[3] == k['kAssultSoldierTd'], f'its type descriptor {f[3]:#x}, kAssultSoldierTd {k["kAssultSoldierTd"]:#x}')
check(td_name(f[3]) == name, f'the descriptor names {td_name(f[3])}, kAssultSoldierName {name}')
# The relocation the plugin compares against: vtable-8 is relocated (a base relocation covers it), so image+COL at run time.
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
target = k['kAssultSoldierVtable'] - 8
relocated = any(e.rva == target for block in pe.DIRECTORY_ENTRY_BASERELOC for e in block.entries if e.type == 10)
check(relocated, f'EDF+{target:#x} has a DIR64 base relocation (the loaded image holds image+COL)')

clone = source('edf41classes.cpp')
check('c.block[0]=const_cast<void*>(vt[-1]);' in clone, 'BuildClone keeps the stock vtable\'s RTTI pointer at block[0]')
print(f'soldier_identity native: {checks} checks ok')
