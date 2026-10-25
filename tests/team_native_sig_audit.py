"""The team manager's capacity sites (src/team_native_logic.h) against the real EDF.dll, read as a file (never loaded):
every site holds its stock bytes and is one whole instruction; the constructor and the every-team walk hold no other
7-row constant; each reset call site calls / jumps to its reset; the signatures team_native.cpp compares are the bytes
there. An EDF.dll path as the argument, else gamedir finds one; exit 77 (skipped) without one."""
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
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the sites are for')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
logic = open(os.path.join(ROOT, 'src', 'team_native_logic.h'), encoding='utf-8').read()
code = open(os.path.join(ROOT, 'src', 'team_native.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def num(text: str) -> int:
    return int(text, 0)


sites = re.findall(r'\{(0x[0-9A-F]+),(\d),\{([^}]*)\},\{[^}]*\}\}', logic)
check(len(sites) == 15, f'fifteen sites, found {len(sites)}')
try:
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
except ImportError:
    md = None
for rva_text, size_text, stock_text in sites:
    rva, size = num(rva_text), int(size_text)
    stock = bytes(num(b) for b in stock_text.split(','))
    check(len(stock) == size, f'{rva:#x}: {size} stock bytes')
    check(img[rva:rva + size] == stock, f'{rva:#x}: {img[rva:rva + size].hex(" ")} != {stock.hex(" ")}')
    if md:
        insn = next(md.disasm(img[rva:rva + 16], rva))
        check(insn.size == size, f'{rva:#x} is one instruction ({insn.mnemonic} {insn.op_str}, {insn.size} bytes)')

if md:
    # No other 7 / 0x188 in the constructor's sizing part or the every-team walk.
    covered = {num(r) for r, _, _ in sites}
    for start, end in ((0x5E03C0, 0x5E058A), (0x5E0C80, 0x5E0D52)):
        for insn in md.disasm(img[start:end], start):
            if re.search(r'(?<![0-9a-fx])(7|0x188)\]?$', insn.op_str) and 'rsp' not in insn.op_str:
                check(insn.address in covered, f'{insn.address:#x} {insn.mnemonic} {insn.op_str} is a site')

consts = {m[0]: num(m[1]) for m in re.findall(r'\b(k\w+)=(0x[0-9A-F]+)\b', logic)}
for rva_text, target in re.findall(r'\{(0x[0-9A-F]+),(kReset\w+)\}', logic):
    rva = num(rva_text)
    op = img[rva]
    check(op in (0xE8, 0xE9), f'{rva:#x} is a call or jmp')
    dest = rva + 5 + struct.unpack_from('<i', img, rva + 1)[0]
    check(dest == consts[target], f'{rva:#x} goes to {dest:#x}, not {target}')

sigs = {name: bytes(num(b) for b in body.split(','))
        for name, body in re.findall(r'constexpr unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', code)}
where = {'kResetDefaultsSig': 'kResetDefaults', 'kResetIdentitySig': 'kResetIdentity',
         'kLockEnterSig': 'kLockEnter', 'kLockLeaveSig': 'kLockLeave'}
check(set(sigs) == set(where), f'the signatures team_native.cpp compares: {sorted(sigs)}')
for name, want in sigs.items():
    rva = consts[where[name]]
    check(img[rva:rva + len(want)] == want, f'{name} at {rva:#x}: {img[rva:rva + len(want)].hex(" ")}')
print(f'team_native_sigs: {checks} checks passed')
