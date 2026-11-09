"""hknpEventDispatcher's uint32 index sites (src/havok_props_logic.h) against the real EDF.dll, read as a file (never
loaded), docs/havok-props-re.md:
1. every site holds its stock bytes and covers whole instructions of its function's linear decode; the patched bytes
   decode on the same boundaries, every branch target in the dispatcher's code still starts a patched instruction;
   the two hkArray reserves hold the bytes the header expects;
2. in the patched dispatcher code (every .pdata function in 0xE75840..0xE76E10 and every leaf without .pdata between
   them) no uint16 index access is left: 16-bit operands, movzx of 16 bits, 0xFFFF, element size / stride 2, calls of
   the uint16 reserve; the only ones left are the event-type key's and the realloc's redundant +2 copy (KEEP);
   reverse case: un-patching any one site makes this check fail at that site;
3. the dispatcher code is reached from outside only by the calls, jumps and pointers listed (EXTERNAL): a new caller
   that could inline or bypass the patched code fails;
4. no code outside reaches the moved fields: every qword loaded from [x+0x980] (hknpWorld+0x980, the dispatcher) or
   [x+0x968] (the body manager's world+0x980) is followed through its unit, and nothing reads or writes +0x38..+0x8F
   through it; the uint16 list patterns (16-bit [r+0x38]/[r+0x60] with 0xFFFF, word [b+i*8] / [b+i*8+2] node reads)
   outside the dispatcher are only the reviewed other structures (NOT_DISPATCHER); reverse case: with 0xE76010 taken
   out of the dispatcher's range the scan finds it;
5. the install guard's two globals are written only where docs/havok-props-re.md says (the application's init).
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
    import numpy as np
    import pefile
except ImportError as e:
    print(f'SKIP: {e.name} not installed')
    raise SystemExit(77)

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the sites are for')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
STOCK = bytes(pe.get_memory_mapped_image())
BASE = pe.OPTIONAL_HEADER.ImageBase
TEXT = next((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Name.startswith(b'.text'))
PDATA_SEC = next((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Name.startswith(b'.pdata'))
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


# --- the header ------------------------------------------------------------------------------------------------------
logic = open(os.path.join(ROOT, 'src', 'havok_props_logic.h'), encoding='utf-8').read()
num = lambda t: int(t, 0)  # noqa: E731
SITES = [(num(r), int(n), bytes(num(b) for b in s.split(',')), bytes(num(b) for b in p.split(',')))
         for r, n, s, p in re.findall(r'\{(0x[0-9A-F]+),(\d),\{([^}]*)\},\{([^}]*)\}\}', logic)]
const = lambda name: num(re.search(name + r'=(0x[0-9A-Fa-f]+)', logic).group(1))  # noqa: E731
FAMILY = (const('kFamilyBegin'), const('kFamilyEnd'))
RESERVE_U16, RESERVE_U32, RESERVE_SIZE = const('kReserveU16'), const('kReserveU32'), const('kReserveSize')
FS_MANAGER, PHYSICS_SYSTEM = const('kFsManager'), const('kPhysicsSystem')


def array(name: str) -> bytes:
    body = re.search(name + r'\[kReserveSize\]=\{([^}]*)\}', logic).group(1)
    return bytes(num(b) for b in body.replace('\n', '').split(','))


check(len(SITES) == 137 and sum(n for _, n, _, _ in SITES) == 548, f'137 sites, 548 bytes: {len(SITES)}')
check(all(len(s) == n and len(p) == n for _, n, s, p in SITES), 'site byte counts')
check(STOCK[RESERVE_U16:RESERVE_U16 + RESERVE_SIZE] == array('kReserveU16Bytes'), 'the uint16 reserve 0xA55860')
check(STOCK[RESERVE_U32:RESERVE_U32 + RESERVE_SIZE] == array('kReserveU32Bytes'), 'the uint32 reserve 0x978600')
for f in (RESERVE_U16, RESERVE_U32):
    check((f, f + RESERVE_SIZE) in PDATA, f'{f:#x} is one whole function of {RESERVE_SIZE:#x} bytes')


def patched(skip: int | None = None) -> bytes:
    img = bytearray(STOCK)
    for rva, n, s, p in SITES:
        if rva != skip:
            img[rva:rva + n] = p
    return bytes(img)


PATCHED = patched()


# --- code units ------------------------------------------------------------------------------------------------------
def pdata_func(rva: int) -> tuple[int, int] | None:
    i = bisect.bisect_right(STARTS, rva) - 1
    return PDATA[i] if i >= 0 and PDATA[i][0] <= rva < PDATA[i][1] else None


def padded(a: int) -> bool:
    return a % 16 == 0 and STOCK[a - 1] == 0xCC


def unit(rva: int) -> tuple[int, int]:
    """The code unit holding rva: its .pdata function, or for leaf code without .pdata the run between the
    neighbouring .pdata functions and int3 padding."""
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
    while end > start and STOCK[end - 1] == 0xCC:
        end -= 1
    return start, end


def family_units(lo: int, hi: int) -> list[tuple[int, int]]:
    """Every .pdata function and every leaf without .pdata in [lo, hi)."""
    out, a = [], lo
    while a < hi:
        f = pdata_func(a)
        if f:
            out.append(f)
            a = f[1]
        elif STOCK[a] == 0xCC:
            a += 1
        else:
            u = unit(a)
            out.append(u)
            a = max(u[1], a + 1)
    return out


_dis: dict = {}


def disasm(f: tuple[int, int], img: bytes = STOCK) -> list:
    key = (f, id(img))
    if key not in _dis:
        _dis[key] = list(md.disasm(img[f[0]:f[1]], f[0]))
    return _dis[key]


UNITS = family_units(*FAMILY)
check(len(UNITS) >= 16, f'the dispatcher\'s functions: {len(UNITS)} units')
# 0xE762A0 (vtable slot 4, the command-type switch 0..0x4D, no .pdata) is followed by its jump tables.
CODE_END = {0xE762A0: 0xE763BD}
for u in UNITS:
    end = u[0] + sum(i.size for i in disasm(u))
    check(end == CODE_END.get(u[0], u[1]), f'{u[0]:#x}: decodes to {end:#x}')

# --- 1. sites on instruction boundaries, before and after --------------------------------------------------------------
site_at = {rva: (n, s, p) for rva, n, s, p in SITES}
for rva, n, s, p in SITES:
    check(STOCK[rva:rva + n] == s, f'{rva:#x}: {STOCK[rva:rva + n].hex(" ")} != {s.hex(" ")}')
    u = unit(rva)
    check(FAMILY[0] <= u[0] and u[1] <= FAMILY[1], f'{rva:#x} in the dispatcher\'s code')
    for img in (STOCK, PATCHED):
        starts = {i.address for i in disasm(u, img)}
        check(rva in starts and (rva + n in starts or rva + n == u[1]), f'{rva:#x}: whole instructions ({"stock" if img is STOCK else "patched"})')
for u in UNITS:
    targets = set()
    for i in disasm(u):
        if i.group(capstone.CS_GRP_JUMP) and i.operands and i.operands[0].type == X.X86_OP_IMM:
            targets.add(i.operands[0].imm)
    starts = {i.address for i in disasm(u, PATCHED)}
    inside = [t for t in targets if u[0] <= t < u[1]]
    check(all(t in starts for t in inside), f'{u[0]:#x}: every branch target starts a patched instruction')


# --- 2. no uint16 index access left in the patched dispatcher ---------------------------------------------------------
def residue(img: bytes, units: list) -> dict[int, str]:
    """Every instruction in units that works on a uint16 index the stock way."""
    out = {}
    for u in units:
        for i in disasm(u, img):
            if i.mnemonic == 'nop':
                continue
            why = None
            ops = i.operands
            if i.mnemonic == 'movzx' and ops[1].size == 2:
                why = 'movzx of 16 bits'
            elif any(o.size == 2 for o in ops):
                why = '16-bit operand'
            elif any(o.type == X.X86_OP_IMM and o.imm & 0xFFFFFFFF == 0xFFFF for o in ops):
                why = '0xFFFF'
            elif i.mnemonic == 'call' and ops[0].type == X.X86_OP_IMM and ops[0].imm == RESERVE_U16:
                why = 'the uint16 reserve'
            elif i.mnemonic in ('mov', 'lea') and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_R8D and (
                    (ops[1].type == X.X86_OP_IMM and ops[1].imm == 2) or
                    (ops[1].type == X.X86_OP_MEM and ops[1].mem.disp == 2 and not ops[1].mem.index)):
                why = 'element size 2'
            elif i.mnemonic == 'add' and ops[1].type == X.X86_OP_IMM and ops[1].imm == 2 and ops[0].size == 8:
                why = 'stride 2'
            elif i.mnemonic == 'lea' and ops[1].mem.disp == 2 and not ops[1].mem.index and ops[0].size == 8 \
                    and ops[1].mem.base == ops[0].reg:
                why = 'stride 2'
            elif any(o.type == X.X86_OP_MEM and o.mem.scale == 2 and o.mem.base not in (0, X.X86_REG_RIP) for o in ops):
                why = 'index*2'
            if why:
                out[i.address] = f'{why}: {i.mnemonic} {i.op_str}'
    return out


# What stays 16 bits: the event type (node +2, event +4) and the realloc's +2 word copy, now redundant.
KEEP = {
    0xE758FA: 'realloc: movzx eax, word [rdx+r10-6] (node +2 key copy, after the qword +0..+7 copy)',
    0xE75900: 'realloc: mov word [rdx-6], ax',
    0xE75CE0: 'allocEntry: mov edx, [rcx+r8*2] (r8 = 2*id: the uint32 head at 4*id)',
    0xE75CED: 'allocEntry: mov [rcx+r8*2], esi (the same)',
}
# The event type: node +2 (read at [b+i*8+2] with i = 2*index, written / compared at [node+2]), the event header's
# +4, and the type switches (cmp ax,0xc; sub ax,0xd; cmp ax,0xb). Each reviewed in the patched decode.
KEY = {
    0xE75D96, 0xE75D9F, 0xE75DC4, 0xE75DCD, 0xE75E27, 0xE75E30, 0xE75E5E, 0xE75E62, 0xE75E96, 0xE75E9F,   # 0xE75D30
    0xE7614A, 0xE76163,                                                                                   # 0xE76120
    0xE7625A, 0xE76273,                                                                                   # 0xE76180
    0xE762A0,                                                                                             # slot 4
    0xE76862, 0xE768C6, 0xE768CF, 0xE768F4, 0xE768F8, 0xE76967, 0xE76970, 0xE769AE, 0xE769B2, 0xE769E6,   # 0xE76850
    0xE769EF,
    0xE76C46, 0xE76C4F, 0xE76C79, 0xE76C7D, 0xE76CD7, 0xE76CE0, 0xE76D17, 0xE76D1B, 0xE76D56, 0xE76D5F,   # 0xE76B70
}
res = residue(PATCHED, UNITS)
other = {a: w for a, w in res.items() if a not in KEEP and a not in KEY}
for a, w in sorted(other.items()):
    print(f'  {a:#x}: {w}')
check(not other, f'{len(other)} uint16 index accesses left in the patched dispatcher (above)')
check(set(KEEP) | KEY == set(res), f'KEEP / KEY stale: {sorted(map(hex, (set(KEEP) | KEY) - set(res)))}')
for a in KEY:
    w = res[a]
    check(bool(re.search(r'word ptr \[\w+ \+ (\w+\*8 \+ 2|[24])\]', w) or re.search(r'\bax, 0x[bcd]$', w)),
          f'{a:#x} reads like the event type: {w}')
for a in KEY:
    check(a not in site_at, f'{a:#x}: a key access is not a site')

# reverse: each site un-patched alone is caught
caught = 0
for rva, n, s, p in SITES:
    img = patched(skip=rva)
    u = unit(rva)
    got = residue(img, [u])
    extra = {a for a in got if rva <= a < rva + n and got[a] != res.get(a)}
    check(bool(extra), f'reverse: un-patching {rva:#x} alone is not detected')
    caught += 1
    _dis.pop((u, id(img)), None)

# --- 3. who reaches the dispatcher from outside ------------------------------------------------------------------------
VT_BASE, VT_DERIVED = 0x18C1628, 0x18C1678
text = STOCK[TEXT[0]:TEXT[1]]


def in_family(t: int) -> bool:
    return FAMILY[0] <= t < FAMILY[1]


refs = set()
for m in re.finditer(rb'[\xE8\xE9]', text):
    at = TEXT[0] + m.start()
    if at + 5 <= TEXT[1] and in_family(at + 5 + struct.unpack_from('<i', STOCK, at + 1)[0]) and not in_family(at):
        refs.add((at, 'rel32'))
for m in re.finditer(rb'\x0F[\x80-\x8F]', text):
    at = TEXT[0] + m.start()
    if at + 6 <= TEXT[1] and in_family(at + 6 + struct.unpack_from('<i', STOCK, at + 2)[0]) and not in_family(at):
        refs.add((at, 'jcc'))


def disp_hits(lo: int, hi: int, tails: tuple[int, ...] = (0, 1, 4)) -> dict[int, set[int]]:
    """{target: {k}}: every disp32 at text offset k whose rip-relative target (the instruction ending 0, 1 or 4
    immediate bytes after it) lies in [lo, hi)."""
    out: dict[int, set[int]] = {}
    for a in range(4):
        n = (len(text) - a) // 4
        d = np.frombuffer(text, dtype='<i4', count=n, offset=a).astype(np.int64)
        k = TEXT[0] + a + 4 * np.arange(n, dtype=np.int64)
        for tail in tails:
            t = k + 4 + tail + d
            for at in np.nonzero((t >= lo) & (t < hi))[0]:
                out.setdefault(int(t[at]), set()).add(int(k[at]))
    return out


def rip_insn(k: int, target: int):
    """The rip-relative instruction whose disp32 is at k and whose target is target, or None: the one in k's code
    unit's linear decode, else (bytes the decode does not own) the longest decode ending there."""
    j = next((i for i in disasm(unit(k)) if i.address < k < i.address + i.size), None)
    if j is not None:
        ok = j.disp_offset and j.address + j.disp_offset == k and 'rip' in j.op_str and \
            j.address + j.size + struct.unpack_from('<i', STOCK, k)[0] == target
        return j if ok else None
    for back in range(7, 0, -1):
        j = next(md.disasm(STOCK[k - back:k - back + 15], k - back, 1), None)
        if j and j.disp_offset == back and 'rip' in j.op_str and j.address + j.size + struct.unpack_from('<i', STOCK, k)[0] == target:
            return j
    return None


for lo, hi in (FAMILY, (VT_BASE, VT_BASE + 1), (VT_DERIVED, VT_DERIVED + 1)):
    for t, ks in disp_hits(lo, hi).items():
        for k in ks:
            j = rip_insn(k, t)
            if j and not in_family(j.address):
                refs.add((j.address, 'rip'))
q64 = np.frombuffer(STOCK, dtype='<u8', count=len(STOCK) // 8)
pointers = {8 * int(k) for k in np.nonzero((q64 >= BASE + FAMILY[0]) & (q64 < BASE + FAMILY[1]))[0]}
EXTERNAL = {
    (0xDA2878, 'rel32'),   # hknpWorld ctor: the derived constructor 0xE76640
    (0xDA2894, 'rel32'),   # hknpWorld ctor: the base constructor 0xE75980
    (0xDA8E1F, 'rel32'),   # hknpWorld body removal loop: onBodyRemoved 0xE76460
    (0xDAB934, 'jcc'),     # world wrapper: jne getBodySignal 0xE76180
    (0xDAB93A, 'rel32'),   # world wrapper: jmp getGlobalSignal 0xE76120
    (0xDAB989, 'rel32'),   # world wrapper: getBodySignal
    (0xDAB998, 'rel32'),   # world wrapper: getGlobalSignal
    (0xDAD159, 'rel32'),   # getGlobalSignal
    (0xDAD18E, 'rel32'),   # getGlobalSignal
}
extra = refs - EXTERNAL
missing = EXTERNAL - refs
for a, k in sorted(extra):
    print(f'  unexpected reference {a:#x} ({k})')
check(not extra and not missing, f'the dispatcher is reached from outside only by EXTERNAL (missing {sorted(missing)})')
vt_slots = {VT_BASE + 8 * s for s in range(6)} | {VT_DERIVED + 8 * s for s in range(6)}
check(pointers <= vt_slots, f'64-bit pointers into the dispatcher only in its vtables: {sorted(map(hex, pointers - vt_slots))}')
check(len(pointers) >= 8, f'the vtables point into the dispatcher ({len(pointers)} slots)')
# vtable references anywhere: only the two constructors' lea, and no stored pointer to either vtable
vt_refs = {j.address for t in (VT_BASE, VT_DERIVED) for k in disp_hits(t, t + 1).get(t, ())
           if (j := rip_insn(k, t)) is not None}
check(vt_refs == {0xE7599A, 0xE7664E}, f'the vtables are loaded only by the two constructors: {sorted(map(hex, vt_refs))}')
q = np.frombuffer(STOCK, dtype='<u8', count=len(STOCK) // 8)
check(not np.any((q == BASE + VT_BASE) | (q == BASE + VT_DERIVED)), 'no stored pointer to a dispatcher vtable')
# 32-bit RVAs of the family's entry points outside .pdata: only unwind data (.rdata), never code or other tables
entries = sorted({u[0] for u in UNITS})
rva_hits = []
for e in entries:
    for m in re.finditer(re.escape(struct.pack('<I', e)), STOCK):
        if not PDATA_SEC[0] <= m.start() < PDATA_SEC[1]:
            rva_hits.append((m.start(), e))


def operand_at(a: int) -> bool:
    """An instruction of a's code unit holds a 4-byte displacement or immediate at a (not bytes across instructions,
    nor a call / jump's relative displacement, which the rel32 scan above covers)."""
    return any(i.address + (i.disp_offset or -99) == a or i.address + (i.imm_offset or -99) == a
               for i in disasm(unit(a)) if i.address <= a < i.address + i.size
               and not (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)))


in_code = [(hex(a), hex(e)) for a, e in rva_hits if TEXT[0] <= a < TEXT[1] and operand_at(a)]
check(not in_code, f'no 32-bit RVA of a dispatcher function as an operand in code: {in_code}')

# --- 4. nobody else reaches the moved fields -----------------------------------------------------------------------
R = lambda n: getattr(X, 'X86_REG_' + n.upper())  # noqa: E731
VOLATILE = {R(n) for n in 'rax rcx rdx r8 r9 r10 r11'.split()}
W32 = {R(a): R(b) for a, b in (('eax', 'rax'), ('ebx', 'rbx'), ('ecx', 'rcx'), ('edx', 'rdx'), ('esi', 'rsi'),
                                ('edi', 'rdi'), ('ebp', 'rbp'), ('esp', 'rsp'))}
W32.update({R(f'r{k}d'): R(f'r{k}') for k in range(8, 16)})


def follow(f: tuple[int, int], start: int, reg: int) -> list:
    """Accesses through reg (and its copies) from the instruction after start to the next write or call."""
    held, hits, on = {reg}, [], False
    for i in disasm(f):
        if not on:
            on = i.address == start
            continue
        if i.mnemonic == 'call':
            held -= VOLATILE
        for op in i.operands:
            if op.type == X.X86_OP_MEM and (op.mem.base in held or op.mem.index in held):
                hits.append(i)
        ops = i.operands
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[1].type == X.X86_OP_REG:
            src, dst = W32.get(ops[1].reg, ops[1].reg), W32.get(ops[0].reg, ops[0].reg)
            (held.add if src in held else held.discard)(dst)
            continue
        try:
            written = {W32.get(r, r) for r in i.regs_access()[1]}
        except capstone.CsError:
            written = set()
        held -= written
        if not held or i.mnemonic in ('ret', 'jmp'):
            break
    return hits


loads = 0
for disp in (0x980, 0x968):
    for m in re.finditer(re.escape(struct.pack('<I', disp)), text):
        hit = TEXT[0] + m.start()
        f = unit(hit)
        i = next((i for i in disasm(f) if i.address < hit < i.address + i.size and i.disp_offset and
                  i.address + i.disp_offset == hit), None)
        if i is None or i.mnemonic != 'mov' or len(i.operands) != 2 or i.operands[0].type != X.X86_OP_REG \
                or i.operands[1].type != X.X86_OP_MEM or i.operands[1].size != 8 or i.operands[1].mem.index:
            continue
        loads += 1
        for j in follow(f, i.address, i.operands[0].reg):
            for op in j.operands:
                if op.type == X.X86_OP_MEM and op.mem.index == 0:
                    check(not 0x38 <= op.mem.disp < 0x90 or in_family(j.address),
                          f'{j.address:#x} ({f[0]:#x}) {j.mnemonic} {j.op_str}: +0x38..+0x8F of a qword from [x+{disp:#x}] ({i.address:#x})')
check(loads >= 40, f'qword loads from [x+0x980] / [x+0x968] followed: {loads}')


def list_shape(f: tuple[int, int], img: bytes) -> set[str]:
    """What of the dispatcher's list idiom a unit shows."""
    h = set()
    for i in disasm(f, img):
        if i.mnemonic == 'nop':
            continue
        for o in i.operands:
            if o.type == X.X86_OP_MEM and o.size == 2:
                if o.mem.disp in (0x38, 0x60) and not o.mem.index:
                    h.add('head')            # a uint16 head in the object
                if o.mem.scale == 8 and o.mem.disp in (0, 2):
                    h.add('node8')           # word [nodes + (2*index)*8 (+2)]: a 16-byte node
                if o.mem.disp == 2:
                    h.add('key')             # node +2: the event type
            if o.type == X.X86_OP_IMM and o.imm & 0xFFFFFFFF == 0xFFFF:
                h.add('ffff')                # the end of a list
        if i.mnemonic == 'shl' and i.operands[1].type == X.X86_OP_IMM and i.operands[1].imm == 4:
            h.add('shl4')                    # index*16: a 16-byte node
    return h


def list_patterns(img: bytes, lo: int, hi: int) -> set[int]:
    """Units outside [lo, hi) with the dispatcher's list idiom: 16-byte nodes reached by a uint16 index (word
    [b+i*8] / shl 4), a uint16 +2 key and an 0xFFFF end. Byte prefilter: a 16-bit [r+0x38]/[r+0x60] access or a
    movzx of word [b+i*8(+disp8)]; each hit's unit decoded (leaves without .pdata included)."""
    cand = set()
    pat = (rb'\x0F\xB7[\x40-\x7F][\x38\x60]|\x66[\x40-\x4F]?[\x89\x8B\x3B\x39][\x40-\x7F][\x38\x60]|'
           rb'\x0F\xB7[\x04\x0C\x14\x1C\x24\x2C\x34\x3C\x44\x4C\x54\x5C\x64\x6C\x74\x7C][\xC0-\xFF]')
    for m in re.finditer(pat, img[TEXT[0]:TEXT[1]]):
        hit = TEXT[0] + m.start()
        if not lo <= hit < hi:
            cand.add(unit(hit))
    out = set()
    for f in cand:
        if lo <= f[0] < hi:
            continue
        h = list_shape(f, img)
        if {'ffff', 'key'} <= h and ('node8' in h or 'shl4' in h) and ('node8' in h or 'head' in h):
            out.add(f[0])
    return out


# Reviewed by hand (docs/havok-props-re.md): other structures with 16-byte uint16-linked records, not the
# dispatcher's lists. None of them loads [x+0x980] / [x+0x968] or reaches the dispatcher's code (checked below).
NOT_DISPATCHER = {
    0xC02B9B: 'a builder over its own hkArrays (+0x40/+0x48/+0x50/+0x60/+0x70 of other objects), word [rcx+0x38] a count',
    0xD11F90: 'a 0xD0Dxxx/0xD42xxx structure: uint16 records with fields +0xD8 / +0x1A',
    0xD123A5: 'the same structure as 0xD11F90',
    0xD41D60: 'the 0xD42xxx family: word [this+0xF8] / [x+0xE8] counts, 8-byte uint16 records',
    0xD420B0: 'the 0xD42xxx family',
    0xD42250: 'the 0xD42xxx family',
    0xD42460: 'the 0xD42xxx family',
    0xD42660: 'the 0xD42xxx family',
    0x1538660: 'word [rbx+rax*8+8] records with +8 / +0xA fields (0x1533xxx / 0x1535xxx helpers)',
    0x1598B1F: 'a 0x3600-byte function over uint16 index arrays [rax+rcx*2]',
}
found = list_patterns(STOCK, *FAMILY)
unreviewed = sorted(found - set(NOT_DISPATCHER))
check(not unreviewed, f'uint16 list code outside the dispatcher, not reviewed: {[hex(a) for a in unreviewed]}')
check(set(NOT_DISPATCHER) <= found, f'NOT_DISPATCHER stale: {sorted(map(hex, set(NOT_DISPATCHER) - found))}')
caller_units = {unit(a)[0] for a, _ in refs}
for f0 in NOT_DISPATCHER:
    f = unit(f0)
    body = STOCK[f[0]:f[1]]
    check(f0 not in caller_units and struct.pack('<I', 0x980) not in body and struct.pack('<I', 0x968) not in body,
          f'{f0:#x} neither reaches the dispatcher nor loads [x+0x980] / [x+0x968]')
# reverse: the base dispatch 0xE75D30 taken out of the dispatcher's range is found
check(0xE75D30 in list_patterns(STOCK, 0xE75E00, FAMILY[1]),
      'reverse: the scan finds the base dispatch 0xE75D30 when it is not excluded')


# --- 5. the install guard's globals --------------------------------------------------------------------------------
def writes(target: int) -> list[int]:
    out = set()
    for k in disp_hits(target, target + 1, (0, 1, 4)).get(target, ()):
        j = rip_insn(k, target)
        if j and j.mnemonic == 'mov' and j.operands[0].type == X.X86_OP_MEM:
            out.add(j.address)
    return sorted(out)


check(writes(PHYSICS_SYSTEM) == [0x11A3575, 0x11A359D, 0x11A38FF],
      f'the physics system is written only by its init 0x11A3410 and its shutdown: {[hex(a) for a in writes(PHYSICS_SYSTEM)]}')
check(pdata_func(0x11A359D)[0] == 0x11A3410 and STOCK[0x11A3400] == 0xE8 and pdata_func(0x11A3400)[0] == 0x11A3340
      and STOCK[0x700A5F] == 0xE8 and 0x700A5F + 5 + struct.unpack_from('<i', STOCK, 0x700A60)[0] == 0x11A3340
      and pdata_func(0x700A5F)[0] == 0x7004F0, 'the physics system is made under the application init 0x7004F0')
check(STOCK[0x1193420:0x1193427] == bytes.fromhex('488b15') + struct.pack('<i', PHYSICS_SYSTEM - 0x1193427)
      and 0x1193427 + 5 + struct.unpack_from('<i', STOCK, 0x1193428)[0] == 0x11AD340,
      'the world maker 0x11AD340 is handed the physics system')
check(0x73DB6 in writes(FS_MANAGER), f'the file-system manager is set at 0x73DB6: {[hex(a) for a in writes(FS_MANAGER)]}')

print(f'havok_props_native: {checks} checks passed ({len(SITES)} sites in {len(UNITS)} units; {len(KEY)} key accesses kept; '
      f'{caught} sites each caught un-patched; {loads} [x+0x980]/[x+0x968] loads followed)')
