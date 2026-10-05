"""python tools/atlas_fuzz.py [seeds] [steps]   (docs/hud-re.md §2.2)

Drive EDF.dll's own glyph-atlas page allocator outside the game and look for two live cells that overlap.

EDF.dll is mapped with DONT_RESOLVE_DLL_REFERENCES (no DllMain, no imports run); the CRT / kernel imports are
bound by hand, every other import points at a trap. Only the allocator's code runs.
"""
import ctypes
import ctypes.wintypes as wt
import os
import random
import shutil
import struct
import sys
import tempfile
from pathlib import Path

import pefile

SRC = Path(os.environ.get('EDF6_DIR', r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6')) / 'EDF.dll'
DLL = Path(tempfile.gettempdir()) / 'edf6vc_atlas_fuzz_EDF.dll'   # a copy: the game's own file stays untouched
if not DLL.exists():
    shutil.copyfile(SRC, DLL)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.LoadLibraryExW.restype = wt.HMODULE
k32.LoadLibraryExW.argtypes = [wt.LPCWSTR, wt.HANDLE, wt.DWORD]
k32.LoadLibraryW.restype = wt.HMODULE
k32.LoadLibraryW.argtypes = [wt.LPCWSTR]
k32.GetProcAddress.restype = ctypes.c_void_p
k32.GetProcAddress.argtypes = [wt.HMODULE, ctypes.c_char_p]
k32.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, ctypes.POINTER(wt.DWORD)]

base = k32.LoadLibraryExW(str(DLL), None, 1)
if not base:
    sys.exit(f'map failed {ctypes.get_last_error()}')

SAFE = ('kernel32', 'msvcp140', 'vcruntime140', 'api-ms-win-crt', 'ucrtbase', 'user32', 'gdi32', 'advapi32',
        'ole32', 'oleaut32', 'shell32', 'shlwapi', 'imm32', 'ws2_32', 'winmm', 'iphlpapi')
pe = pefile.PE(str(DLL), fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
trap = ctypes.c_void_p(0)  # calling an unbound import faults at address 0: loud, never silent
bound = stubbed = 0
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    name = entry.dll.decode().lower()
    mod = k32.LoadLibraryW(name) if name.startswith(SAFE) else None
    for imp in entry.imports:
        slot = base + (imp.address - pe.OPTIONAL_HEADER.ImageBase)
        addr = 0
        if mod:
            addr = k32.GetProcAddress(mod, imp.name if imp.name else ctypes.c_char_p(imp.ordinal)) or 0
        old = wt.DWORD()
        k32.VirtualProtect(slot, 8, 0x04, ctypes.byref(old))
        ctypes.c_uint64.from_address(slot).value = addr
        k32.VirtualProtect(slot, 8, old.value, ctypes.byref(old))
        if addr:
            bound += 1
        else:
            stubbed += 1
print(f'EDF.dll mapped at {base:#x}: {bound} imports bound, {stubbed} left null')

P = ctypes.c_void_p
U16 = ctypes.c_uint16
fn = lambda rva, res, *args: ctypes.CFUNCTYPE(res, *args)(base + rva)
Ctor = fn(0x1179130, P, P)                                   # allocator ctor (page+0x40)
FindOrAdd = fn(0x1178B10, P, P, P, P)                        # unordered_map<height, shelf>::try_emplace
AddFree = fn(0x117A070, None, P, P, ctypes.c_int64, ctypes.c_int64, ctypes.c_uint32, ctypes.c_uint32)
Alloc = fn(0x11795E0, P, P, U16, U16, P, P)                  # (alloc, w, h, &a, &b) -> cell or null
Free = fn(0x117A8B0, None, P, P)                             # (alloc, cell)

W = H = 1024


def new_page() -> ctypes.Array:
    page = (ctypes.c_ubyte * 0xA0)()
    a = ctypes.addressof(page)
    Ctor(a + 0x40)
    out = (ctypes.c_uint64 * 2)()
    key = ctypes.c_uint32(H)
    FindOrAdd(a + 0x40, ctypes.addressof(out), ctypes.addressof(key))
    node = out[0]
    AddFree(node + 0x18, a + 0x80, 0, 0, W, H)
    return page


def cell_rect(cell: int) -> tuple[int, int, int, int]:
    x, y, w, h = struct.unpack('<4H', ctypes.string_at(cell, 8))
    return x, y, w, h


def run(seed: int, steps: int) -> bool:
    rnd = random.Random(seed)
    page = new_page()
    alloc = ctypes.addressof(page) + 0x40
    live: dict[int, tuple] = {}
    fails = 0
    for step in range(steps):
        if live and (rnd.random() < (0.2 if (step // 500) % 2 else 0.7) or fails > 3):
            cell = rnd.choice(list(live))
            del live[cell]
            Free(alloc, cell)
            fails = 0
            continue
        w, h = rnd.randint(4, 120), (rnd.randint(4, 120) if seed % 2 else rnd.choice((24, 32, 40, 48, 56, 64, 72, 85, 96)))
        a, b = U16(0xFFFF), U16(0xFFFF)
        cell = Alloc(alloc, w, h, ctypes.addressof(a), ctypes.addressof(b))
        if not cell:
            fails += 1
            continue
        fails = 0
        r = (a.value, b.value, w, h)
        if r[0] + w > W or r[1] + h > H:
            print(f'seed {seed} step {step}: cell {r} outside the page'); return False
        for other, o in live.items():
            if r[0] < o[0] + o[2] and o[0] < r[0] + w and r[1] < o[1] + o[3] and o[1] < r[1] + h:
                print(f'seed {seed} step {step}: NEW {r} overlaps LIVE {o}  (live {len(live)})'); return False
        if cell in live:
            print(f'seed {seed} step {step}: handle {cell:#x} handed out twice'); return False
        live[cell] = r
    return True


if __name__ == '__main__':
    seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    bad = [s for s in range(seeds) if not run(s, steps)]
    print(f'{seeds} seeds x {steps} steps: {len(bad)} with an overlap {bad[:10]}')
