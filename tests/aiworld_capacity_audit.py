"""ai::World's agent capacity sites (src/aiworld_capacity_logic.h) against the real EDF.dll, read as a file (never
loaded), docs/aiworld-re.md:
- every site holds its stock bytes, is one whole instruction, and its 4-byte field is that instruction's
  displacement / immediate;
- the World is made only by 0xEBE40 (the one call of its constructor, the one allocation sized 0x2180) and freed only
  by its vtable slot 1 (0xEED00, the only caller of the destructor; the only references to the vtable are the
  constructor's and the destructor's);
- no access to the stock agent table is left: every [base+index*8+0x80] qword access in .text is a site or in a
  function reviewed as not the World's (NOT_WORLD), and in the World's own code (the ai module 0xE4000..0xF8000, any
  function touching World-only fields +0x2088..+0x2180 through an argument) nothing else reaches +0x80..+0x2087.
An EDF.dll path as the argument, else gamedir finds one; exit 77 (skipped) without one or without capstone."""
from __future__ import annotations

import bisect
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
try:
    import capstone
    import capstone.x86 as X
    import pefile
except ImportError as e:
    print(f'SKIP: {e.name} not installed')
    raise SystemExit(77)

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the sites are for')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
img = pe.get_memory_mapped_image()
BASE = pe.OPTIONAL_HEADER.ImageBase
TEXT = next((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Name.startswith(b'.text'))
PDATA = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
STARTS = [b for b, _ in PDATA]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def func(rva: int) -> tuple[int, int] | None:
    i = bisect.bisect_right(STARTS, rva) - 1
    return PDATA[i] if i >= 0 and PDATA[i][0] <= rva < PDATA[i][1] else None


_dis: dict[int, list] = {}


def disasm(f: tuple[int, int]) -> list:
    if f[0] not in _dis:
        _dis[f[0]] = list(md.disasm(img[f[0]:f[1]], f[0]))
    return _dis[f[0]]


def insn_at(rva: int):
    f = func(rva)
    return next((i for i in disasm(f) if i.address == rva), None) if f else None


# --- the sites, as the header lists them --------------------------------------------------------------------------
logic = open(os.path.join(ROOT, 'src', 'aiworld_capacity_logic.h'), encoding='utf-8').read()
num = lambda t: int(t, 0)  # noqa: E731
sites = []   # (rva, size, field, kind, stock)
for rva, size, field, kind, stock in re.findall(r'\{(0x[0-9A-F]+),(\d+),(\d),Kind::(\w+),\{([^}]*)\}', logic):
    sites.append((num(rva), int(size), int(field), kind, bytes(num(b) for b in stock.split(','))))
for macro, rva, ops in re.findall(r'AIW_SLOT(8|12)\((0x[0-9A-F]+),([^)]*)\)', logic):
    head = bytes(num(b) for b in ops.split(','))
    sites.append((num(rva), int(macro), 4, 'Slot', head + b'\x80\0\0\0' + b'\0' * (int(macro) - 8)))
check(len(sites) == 13, f'thirteen sites, found {len(sites)}')
want_field = {'WorldSize': 0x2180, 'ZeroBase': 0x80, 'ZeroCount': 0x80, 'Slot': 0x80, 'IdBound': 0x401}
site_rvas = set()
for rva, size, field, kind, stock in sites:
    site_rvas.add(rva)
    check(len(stock) == size, f'{rva:#x}: {size} stock bytes')
    check(img[rva:rva + size] == stock, f'{rva:#x}: {img[rva:rva + size].hex(" ")} != {stock.hex(" ")}')
    insn = insn_at(rva)
    check(insn is not None and insn.size == size, f'{rva:#x} is one instruction on its function\'s boundaries')
    check(struct.unpack_from('<I', stock, field)[0] == want_field[kind], f'{rva:#x}: stock field is {want_field[kind]:#x}')
    if kind in ('Slot', 'ZeroBase'):
        mem = [op for op in insn.operands if op.type == X.X86_OP_MEM]
        check(len(mem) == 1 and mem[0].mem.disp == 0x80 and insn.disp_offset == field,
              f'{rva:#x}: the field is the +0x80 displacement ({insn.mnemonic} {insn.op_str})')
        if kind == 'Slot':
            check(mem[0].mem.scale == 8 and mem[0].mem.index != 0 and mem[0].size == 8,
                  f'{rva:#x}: a qword slot indexed by id*8')
    else:
        check(insn.imm_offset == field, f'{rva:#x}: the field is the immediate ({insn.mnemonic} {insn.op_str})')

# --- the World's one maker and one free -----------------------------------------------------------------------------
CTOR, DTOR, DELETING, MAKER, VTABLE, ALLOC, FREE = 0xEE020, 0xEE770, 0xEED00, 0xEBE40, 0x1768A48, 0x97A6B0, 0x97A760
text = bytes(img[TEXT[0]:TEXT[1]])


def rel_refs(target: int, opcodes: bytes) -> list[int]:
    out = []
    for m in re.finditer(b'[' + re.escape(opcodes) + b']', text):
        at = TEXT[0] + m.start()
        if at + 5 <= TEXT[1] and at + 5 + struct.unpack_from('<i', img, at + 1)[0] == target:
            out.append(at)
    return out


def lea_refs(target: int) -> list[int]:
    out = []
    for m in re.finditer(rb'[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]', text, re.DOTALL):
        at = TEXT[0] + m.start()
        if at + 7 + struct.unpack_from('<i', img, at + 3)[0] == target:
            out.append(at)
    return out


CALLS = b'\xE8\xE9'
check(rel_refs(CTOR, CALLS) == [0xEBE76], 'the constructor is called only at 0xEBE76')
check(rel_refs(DTOR, CALLS) == [0xEED0F], 'the destructor is called only by the deleting destructor')
check(rel_refs(DELETING, CALLS) == [], 'the deleting destructor is reached only through the vtable')
check(sorted(lea_refs(VTABLE)) == [0xEE040, 0xEE782], 'the vtable is referenced only by the constructor and destructor')
check(struct.unpack_from('<Q', img, VTABLE + 8)[0] - BASE == DELETING, 'vtable slot 1 is the deleting destructor')
check(img[0xEBE64] == 0xE8 and 0xEBE64 + 5 + struct.unpack_from('<i', img, 0xEBE65)[0] == ALLOC,
      '0xEBE5F sizes the allocation 0xEBE64 makes')
check(img[0xEED2D] == 0xE8 and 0xEED2D + 5 + struct.unpack_from('<i', img, 0xEED2E)[0] == FREE,
      '0xEED25 sizes the free 0xEED2D makes')
for f in (func(MAKER), func(DELETING)):
    sized = [i.address for i in disasm(f) for op in i.operands if op.type == X.X86_OP_IMM and op.imm == 0x2180]
    check(set(sized) <= site_rvas, f'every 0x2180 in {f[0]:#x} is a site: {[hex(a) for a in sized]}')

# --- coverage: every [b+i*8+0x80] qword access in .text -------------------------------------------------------------
# Reviewed by hand (docs/aiworld-re.md): their base is not a World.
NOT_WORLD = {
    0x95F930: 'its own this (r14, fields +0xC0..): qword pairs +0x80/+0x88 at index*2',
    0x1004548: 'an array read from [r8+0x18], filled +0x78/+0x80 with pointers into a 0x30-stride buffer',
    0x12F8B30: 'a two-entry table picked by a flag bit',
    0x15917CB: 'Havok: an 0x90-stride block array (index = i*9*2)',
}
# ModRM mod=10 rm=100 (SIB follows), SIB scale 8, disp32 0x80.
cand_funcs = set()
for m in re.finditer(rb'[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC][\xC0-\xFF]\x80\x00\x00\x00', text, re.DOTALL):
    f = func(TEXT[0] + m.start())
    if f:
        cand_funcs.add(f)
indexed = 0
for f in sorted(cand_funcs):
    for i in disasm(f):
        for op in i.operands:
            if op.type != X.X86_OP_MEM or op.mem.disp != 0x80 or op.mem.scale != 8 or not op.mem.index:
                continue
            if op.size != 8 or op.mem.base in (X.X86_REG_RSP, X.X86_REG_RIP, 0) or i.mnemonic == 'lea':
                continue
            indexed += 1
            check(i.address in site_rvas or f[0] in NOT_WORLD,
                  f'{i.address:#x} ({f[0]:#x}) {i.mnemonic} {i.op_str}: a qword [b+i*8+0x80] neither a site nor reviewed')
check(indexed == 7 + 22, f'7 sites and 22 reviewed qword [b+i*8+0x80] accesses, found {indexed}')

# --- coverage: the World's own code ---------------------------------------------------------------------------------
R = lambda n: getattr(X, 'X86_REG_' + n.upper())  # noqa: E731
VOLATILE = {R(n) for n in 'rax rcx rdx r8 r9 r10 r11'.split()}
W32 = {R(a): R(b) for a, b in (('eax', 'rax'), ('ebx', 'rbx'), ('ecx', 'rcx'), ('edx', 'rdx'), ('esi', 'rsi'),
                                ('edi', 'rdi'), ('ebp', 'rbp'), ('esp', 'rsp'))}
W32.update({R(f'r{k}d'): R(f'r{k}') for k in range(8, 16)})
world_funcs = 0
for f in PDATA:
    if not 0xE4000 <= f[0] < 0xF8000:
        continue
    for seed in (R('rcx'), R('rdx'), R('r8'), R('r9')):
        held, hits, world = {seed}, [], False
        for i in disasm(f):
            if i.mnemonic == 'call':
                held -= VOLATILE
                continue
            for op in i.operands:
                if op.type != X.X86_OP_MEM:
                    continue
                if op.mem.base in held and not op.mem.index:
                    world |= 0x2088 <= op.mem.disp < 0x2180
                    if 0x80 <= op.mem.disp < 0x2088:
                        hits.append(i)
                elif op.mem.base in held or op.mem.index in held:
                    hits.append(i)
            if i.mnemonic == 'mov' and len(i.operands) == 2 and all(o.type == X.X86_OP_REG for o in i.operands):
                dst, src = (W32.get(o.reg, o.reg) for o in i.operands)
                (held.add if src in held else held.discard)(dst)
                continue
            try:
                written = i.regs_access()[1]
            except capstone.CsError:
                written = []
            held -= {W32.get(r, r) for r in written}
            if not held:
                break
        if world:
            world_funcs += 1
            for i in hits:
                check(i.address in site_rvas, f'{i.address:#x} ({f[0]:#x}) {i.mnemonic} {i.op_str}: World +0x80..+0x2087 not a site')
check(world_funcs >= 30, f'the World\'s own functions found ({world_funcs})')
print(f'aiworld_capacity_native: {checks} checks passed ({indexed} indexed qword accesses, {world_funcs} World functions)')
