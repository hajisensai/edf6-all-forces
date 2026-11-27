"""The sites src/object_update_mt.cpp (slot5 bones -> collision proxies on worker threads, docs/object-update-mt-re.md)
patches, checked against the real EDF.dll read as a file (never loaded). Every constant comes from
src/object_update_mt_logic.h by name: the code fingerprints the install checks, the update window's lock / unlock calls
(System::update) and their targets, the InsectBase / HumanBase calls to 0x6ED800 and the 0x6EDCA0 call before each, the
gate sites' bytes (mid-function, inside their function's .pdata range, no rip-relative operand displaced) and the
stock 0x6EDC0C call into 0x109B50 (native_body_math redirects it; the commit keeps its prefetch). An EDF.dll path as
the argument, else the game gamedir finds; exit 77 (skipped) without one."""
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
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
img = pe.get_memory_mapped_image()
functions = sorted((f.struct.BeginAddress, f.struct.EndAddress) for f in pe.DIRECTORY_ENTRY_EXCEPTION)
src = open(os.path.join(ROOT, 'src', 'object_update_mt_logic.h'), encoding='utf-8').read()
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


consts = {m[0]: int(m[1], 0) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+|\d+)\b', src)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})\b', body))
          for name, body in re.findall(r'\b(k\w+)\[\]\{([^}]*)\}', src)}


def fnv(b: int, e: int) -> int:
    h = 14695981039346656037
    for x in img[b:e]:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def call_target(at: int) -> int:
    check(img[at] == 0xE8, f'EDF+{at:#x} is not a rel32 call')
    return at + 5 + struct.unpack_from('<i', img, at + 1)[0]


def function_of(at: int) -> tuple[int, int] | None:
    for b, e in functions:
        if b <= at < e:
            return b, e
    return None


# fingerprints
ranges = re.findall(r'\{(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+)ull\}', src)
check(len(ranges) == 9, f'{len(ranges)} code ranges in the header')
for b, e, h in ranges:
    check(fnv(int(b, 16), int(e, 16)) == int(h, 16), f'fingerprint EDF+{b}..{e}')

# the window and the registrations
for site, name, target in (('kWindowOpen', 'kCallLock', 'kProxyLock'), ('kWindowClose', 'kCallUnlock', 'kProxyUnlock'),
                           ('kRegisterInsect', 'kCallInsect', 'kBonesToProxies'),
                           ('kRegisterHuman', 'kCallHuman', 'kBonesToProxies')):
    at = consts[site]
    check(img[at:at + 5] == arrays[name], f'{site} bytes')
    check(call_target(at) == consts[target], f'{site} calls {target}')
for lock in ('kProxyLock', 'kProxyUnlock'):   # both `ret 0`: no state to keep across the window
    check(img[consts[lock]:consts[lock] + 3] == b'\xC2\x00\x00', f'{lock} is ret 0')
check(function_of(consts['kWindowOpen']) == function_of(consts['kWindowClose']), 'window calls in one function')
check(consts['kWindowOpen'] < consts['kWindowClose'], 'open before close')
# physics -> bones right before each registration (the pose the bones -> proxies pass reads)
for site, before in (('kRegisterInsect', 0x4A9835), ('kRegisterHuman', 0x57580D)):
    check(call_target(before) == consts['kPhysicsToBones'], f'0x6EDCA0 before {site}')
    check(function_of(before) == function_of(consts[site]), f'{site} and its 0x6EDCA0 in one function')
# the node pose call native_body_math redirects (and the commit keeps its prefetch)
check(call_target(0x6EDC0C) == consts['kSetNodeMatrix'], '0x6EDC0C calls 0x109B50')
tail = [at for at in range(0x6EDC11, 0x6EDC92 - 4) if img[at] == 0xE8 and
        at + 5 + struct.unpack_from('<i', img, at + 1)[0] in (consts['kCompoundUpdate'], consts['kProxyUnlock'])]
check(len(tail) == 2, '0x6ED800 ends with the compound update and the unlock')

# gates
gates = re.findall(r'\{(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+),(\d+),Src::k\w+,Gate::k\w+,\{([^}]*)\}\}', src)
check(len(gates) == 11, f'{len(gates)} gates in the header')
for fn, site, size, body in gates:
    fn, site, size = int(fn, 16), int(site, 16), int(size)
    expect = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})\b', body))
    check(len(expect) == size and 5 <= size <= 16, f'gate EDF+{site:#x} size')
    check(img[site:site + size] == expect, f'gate EDF+{site:#x} bytes')
    owner = function_of(site)
    check(owner is not None and owner[0] == fn and site > fn and site + size <= owner[1], f'gate EDF+{site:#x} inside {fn:#x}')
    # no rip-relative operand among the displaced bytes (ModRM mod 00 rm 101)
    check(not any((b & 0xC7) == 0x05 for b in expect[1:]), f'gate EDF+{site:#x} rip-relative')

# data the stock code reads
for name in ('kRestMask', 'kRestTolerance', 'kStepDt', 'kAabbInitMin', 'kAabbInitMax', 'kAabbDefaultMin',
             'kAabbDefaultMax', 'kFilterWrapper'):
    check(consts[name] < pe.OPTIONAL_HEADER.SizeOfImage, f'{name} inside the image')
check(struct.unpack_from('<4f', img, consts['kAabbDefaultMin']) == (-1.0, -1.0, -1.0, 1.0), 'default min')
check(struct.unpack_from('<4f', img, consts['kAabbDefaultMax']) == (1.0, 1.0, 1.0, 1.0), 'default max')
# 0x1061E0 reads the 32 layer masks from +0x24 of the filter the wrapper at 0x20B2970 holds
flt = consts['kCollisionFilter']
check(function_of(flt) is None and img[flt - 1] == 0xCC, 'collision filter is a leaf function start')
# what FilterMayHitProxies mirrors: the group bits 0x7FFF8000 tested first, then [wrapper+8] and the layer mask
# dword [filter + layer*4 + 0x24] anded with 1 << other layer
check(img[flt + 0x08:flt + 0x0D] == b'\xA9\x00\x80\xFF\x7F', 'collision filter group mask 0x7FFF8000')
check(img[flt + 0x4F:flt + 0x53] == b'\x48\x8B\x51\x08', 'collision filter reads [wrapper+8]')
check(img[flt + 0x62:flt + 0x67] == b'\x46\x8B\x4C\x82\x24', 'collision filter reads the masks at +0x24')
print(f'object update multithread signatures: {checks} checks, {len(ranges)} fingerprints, {len(gates)} gates ok')
