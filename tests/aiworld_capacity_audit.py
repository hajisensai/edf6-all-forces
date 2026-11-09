"""ai::World's agent capacity sites (src/aiworld_capacity_logic.h) against the real EDF.dll, read as a file (never
loaded), docs/aiworld-re.md:
- every site holds its stock bytes, is one whole instruction, and its 4-byte field is that instruction's
  displacement / immediate;
- the World is made only by 0xEBE40 (the one call of its constructor, the one allocation sized 0x2180) and freed only
  by its vtable slot 1 (0xEED00, the only caller of the destructor; the only references to the vtable are the
  constructor's and the destructor's);
- no access to the stock agent table is left: every [base+index*8+0x80] qword access / lea in .text, in .pdata
  functions and in leaf code without .pdata alike, is a site or in a function reviewed as not the World's
  (NOT_WORLD); and in the World's own code (the ai module 0xE4000..0xF8000 with or without .pdata, and the vtables of
  the World, its CostModifier and its AvoidanceFilter; an argument that is the World or holds it at +0x18) nothing
  else reaches +0x80..+0x2087.
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
AI = (0xE4000, 0xF8000)   # the ai module: World, its agents, CostModifier, AvoidanceFilter


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def pdata_func(rva: int) -> tuple[int, int] | None:
    i = bisect.bisect_right(STARTS, rva) - 1
    return PDATA[i] if i >= 0 and PDATA[i][0] <= rva < PDATA[i][1] else None


def padded(a: int) -> bool:
    """a 16-aligned address right after int3 padding: where MSVC starts the next function."""
    return a % 16 == 0 and img[a - 1] == 0xCC


def unit(rva: int) -> tuple[int, int]:
    """The code unit holding rva: its .pdata function, or for leaf code without .pdata (no unwind info needed, so
    MSVC emits none) the run between the neighbouring .pdata functions and int3 padding."""
    f = pdata_func(rva)
    if f:
        return f
    i = bisect.bisect_right(STARTS, rva) - 1
    lo = max(PDATA[i][1] if i >= 0 else TEXT[0], TEXT[0])
    hi = PDATA[i + 1][0] if i + 1 < len(PDATA) else TEXT[1]
    start = rva
    while start > lo and not padded(start):
        start -= 1
    end = rva + 1
    while end < hi and not padded(end):
        end += 1
    while end > start and img[end - 1] == 0xCC:
        end -= 1
    return start, end


_dis: dict[tuple[int, int], list] = {}


def disasm(f: tuple[int, int]) -> list:
    if f not in _dis:
        _dis[f] = list(md.disasm(img[f[0]:f[1]], f[0]))
    return _dis[f]


def insn_at(rva: int):
    return next((i for i in disasm(unit(rva)) if i.address == rva), None)


# --- the sites, as the header lists them --------------------------------------------------------------------------
logic = open(os.path.join(ROOT, 'src', 'aiworld_capacity_logic.h'), encoding='utf-8').read()
num = lambda t: int(t, 0)  # noqa: E731
sites = []   # (rva, size, field, kind, stock)
for rva, size, field, kind, stock in re.findall(r'\{(0x[0-9A-F]+),(\d+),(\d),Kind::(\w+),\{([^}]*)\}', logic):
    sites.append((num(rva), int(size), int(field), kind, bytes(num(b) for b in stock.split(','))))
for macro, rva, ops in re.findall(r'AIW_SLOT(8|12)\((0x[0-9A-F]+),([^)]*)\)', logic):
    head = bytes(num(b) for b in ops.split(','))
    sites.append((num(rva), int(macro), 4, 'Slot', head + b'\x80\0\0\0' + b'\0' * (int(macro) - 8)))
SLOT_SITES = 11
check(len(sites) == 6 + SLOT_SITES, f'{6 + SLOT_SITES} sites, found {len(sites)}')
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
for f in (pdata_func(MAKER), pdata_func(DELETING)):
    sized = [i.address for i in disasm(f) for op in i.operands if op.type == X.X86_OP_IMM and op.imm == 0x2180]
    check(set(sized) <= site_rvas, f'every 0x2180 in {f[0]:#x} is a site: {[hex(a) for a in sized]}')

# --- coverage: every [b+i*8+0x80] access in .text, with or without .pdata --------------------------------------------
# Each byte-pattern hit (ModRM mod=10 rm=100, SIB scale 8, disp32 0x80) is decoded by its code unit's linear
# disassembly, so leaf functions MSVC left without .pdata are covered too. Qword accesses and lea count (a World slot
# is a qword pointer; the non-qword ones are xmm / byte data). Reviewed by hand (docs/aiworld-re.md): base not a World.
NOT_WORLD = {
    0x95F930: 'its own this (r14, fields +0xC0..): qword pairs +0x80/+0x88 at index*2',
    0x1004548: 'an array read from [r8+0x18], filled +0x78/+0x80 with pointers into a 0x30-stride buffer',
    0x12F8B30: 'a two-entry table picked by a flag bit',
    0x15917CB: 'Havok: an 0x90-stride block array (index = i*9*2)',
}
indexed = no_pdata = 0
for m in re.finditer(rb'[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC][\xC0-\xFF]\x80\x00\x00\x00', text, re.DOTALL):
    hit = TEXT[0] + m.start()
    f = unit(hit)
    # the instruction whose disp32 is this hit's, on the unit's instruction boundaries
    i = next((i for i in disasm(f) if i.address < hit < i.address + i.size and i.address + i.disp_offset == hit + 2), None)
    if i is None:
        # the linear decode does not own these bytes as a displacement: then no decode starting 1..5 bytes back may
        # be an indexed qword +0x80 access either, else the unit's decode is out of step and has to be looked at
        for k in range(1, 6):
            j = next(md.disasm(img[hit - k:hit - k + 15], hit - k, 1), None)
            if j is None or j.disp_offset != k + 2:
                continue
            ops = [o for o in j.operands if o.type == X.X86_OP_MEM and o.mem.disp == 0x80 and o.mem.scale == 8]
            check(not ops or ops[0].mem.base in (X.X86_REG_RSP, 0) or (ops[0].size != 8 and j.mnemonic != 'lea'),
                  f'{hit:#x}: decodes from {hit - k:#x} as {j.mnemonic} {j.op_str} but not in {f[0]:#x}\'s linear decode')
        continue
    for op in i.operands:
        if op.type != X.X86_OP_MEM or op.mem.disp != 0x80 or op.mem.scale != 8 or not op.mem.index:
            continue
        if op.mem.base in (X.X86_REG_RSP, X.X86_REG_RIP, 0) or (op.size != 8 and i.mnemonic != 'lea'):
            continue
        indexed += 1
        if not pdata_func(i.address):
            no_pdata += 1
            check(i.address in site_rvas, f'{i.address:#x} (no .pdata, {f[0]:#x}) {i.mnemonic} {i.op_str}: not a site')
        check(i.address in site_rvas or f[0] in NOT_WORLD,
              f'{i.address:#x} ({f[0]:#x}) {i.mnemonic} {i.op_str}: a qword [b+i*8+0x80] neither a site nor reviewed')
check(indexed == SLOT_SITES + 22, f'{SLOT_SITES} sites and 22 reviewed qword [b+i*8+0x80] accesses, found {indexed}')
check(no_pdata == 4, f'the AvoidanceFilter\'s 4 are the accesses without .pdata, found {no_pdata}')

# --- coverage: the World's own code ---------------------------------------------------------------------------------
# Units: every .pdata function and every leaf without .pdata in the ai module, and every function in the vtables of
# the World (this = World) and of its sub-objects CostModifier / AvoidanceFilter (World = [this+0x18]). Each argument
# register is followed as a World (depth 0) and as the holder of one at +0x18 (depth 1). A followed value is a World
# when the unit touches a World-only field (+0x2088..+0x2180) through it, or the vtable says so; then every access
# through it to +0x80..+0x2087, indexed or not (an lea of world+id*8 included), has to be a site.
R = lambda n: getattr(X, 'X86_REG_' + n.upper())  # noqa: E731
VOLATILE = {R(n) for n in 'rax rcx rdx r8 r9 r10 r11'.split()}
ARGS = [R(n) for n in ('rcx', 'rdx', 'r8', 'r9')]
W32 = {R(a): R(b) for a, b in (('eax', 'rax'), ('ebx', 'rbx'), ('ecx', 'rcx'), ('edx', 'rdx'), ('esi', 'rsi'),
                                ('edi', 'rdi'), ('ebp', 'rbp'), ('esp', 'rsp'))}
W32.update({R(f'r{k}d'): R(f'r{k}') for k in range(8, 16)})
VT_WORLD, VT_COST, VT_AVOID = VTABLE, 0x17689E0, 0x1768A18


def vtable_funcs(vt: int, n: int) -> list[int]:
    return [struct.unpack_from('<Q', img, vt + 8 * k)[0] - BASE for k in range(n)]


check(vtable_funcs(VT_COST, 6)[5] == 0xF1440 and vtable_funcs(VT_AVOID, 5)[3:] == [0xF4160, 0xF41B0],
      'CostModifier slot 5 is 0xF1440, AvoidanceFilter slots 3/4 are 0xF4160/0xF41B0')
check(lea_refs(VT_AVOID) == [0xEF446] and img[0xEF450:0xEF454] == b'\x48\x89\x78\x18',
      'the AvoidanceFilter is made only at 0xEF43E, with [filter+0x18] = the World (rdi)')
forced: dict[tuple[int, int], set] = {}   # unit -> {(depth, seed)} known to be / to hold the World
for vt, depth in ((VT_WORLD, 0), (VT_COST, 1), (VT_AVOID, 1)):
    for fn in vtable_funcs(vt, 8):
        if TEXT[0] <= fn < TEXT[1]:
            forced.setdefault(unit(fn), set()).add((depth, R('rcx')))
units = set(forced)
units.update(f for f in PDATA if AI[0] <= f[0] < AI[1])
a = AI[0]
while a < AI[1]:   # leaves without .pdata
    f = pdata_func(a)
    if f:
        a = f[1]
    elif img[a] == 0xCC:
        a += 1
    else:
        u = unit(a)
        units.add(u)
        a = max(u[1], a + 1)


def follow(f: tuple[int, int], depth: int, seed: int) -> tuple[bool, list]:
    held, holder = ({seed}, set()) if depth == 0 else (set(), {seed})
    hits, world = [], False
    for i in disasm(f):
        if i.mnemonic == 'call':
            held -= VOLATILE
            holder -= VOLATILE
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
        ops = i.operands
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X.X86_OP_REG:
            dst = W32.get(ops[0].reg, ops[0].reg)
            if ops[1].type == X.X86_OP_REG:
                src = W32.get(ops[1].reg, ops[1].reg)
                (held.add if src in held else held.discard)(dst)
                (holder.add if src in holder else holder.discard)(dst)
                continue
            mem = ops[1].mem
            if ops[1].type == X.X86_OP_MEM and mem.base in holder and not mem.index and mem.disp == 0x18 and ops[1].size == 8:
                holder.discard(dst)
                held.add(dst)
                continue
        try:
            written = {W32.get(r, r) for r in i.regs_access()[1]}
        except capstone.CsError:
            written = set()
        held -= written
        holder -= written
        if not held and not holder:
            break
    return world, hits


world_units, world_no_pdata = set(), set()
for f in sorted(units):
    for depth in (0, 1):
        for seed in ARGS:
            world, hits = follow(f, depth, seed)
            if not (world or (depth, seed) in forced.get(f, ())):
                continue
            world_units.add(f)
            if not pdata_func(f[0]):
                world_no_pdata.add(f[0])
            for i in hits:
                check(i.address in site_rvas,
                      f'{i.address:#x} ({f[0]:#x}, depth {depth}) {i.mnemonic} {i.op_str}: World +0x80..+0x2087 not a site')
check(len(world_units) >= 40, f'the World\'s own code found ({len(world_units)} units)')
check({0xF4160, 0xF41B0} <= world_no_pdata, f'the AvoidanceFilter leaves are World code: {sorted(map(hex, world_no_pdata))}')
print(f'aiworld_capacity_native: {checks} checks passed ({indexed} indexed qword accesses, {no_pdata} without .pdata; '
      f'{len(world_units)} World units of {len(units)}, without .pdata: {", ".join(map(hex, sorted(world_no_pdata)))})')
