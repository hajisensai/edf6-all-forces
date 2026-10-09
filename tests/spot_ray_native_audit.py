"""Read the stock spot (原版 Q 标记) contract from the supported EDF.dll; no game execution (src/spot_ray.h, turretaim.cpp).

The soldier update's spot branch, riding, takes the seat camera's locators as the ray, calls 0x5A1120 at 0x59B75C;
0x5A1120 lifts the origin by 2.0 m, casts 1000 m, filters out the soldier and its own vehicle, places the SpotEffect.
VanillaSpot=0 returns from the redirected call without casting: that is the branch not taken only if the spot flag
soldier+0xD7A has no other reader, the branch has no other effect and its not-taken target is the call's return address.
Those are checked here over the whole .text (every instruction with displacement 0xD7A). Missing input: 77.
"""
from pathlib import Path
import struct
import sys
if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required');raise SystemExit(77)
if not __debug__: raise RuntimeError("Run without -O: contract assertions must execute")
import pefile
import capstone
p = pefile.PE(sys.argv[1], fast_load=True)
assert p.FILE_HEADER.TimeDateStamp == 0x678CCB46 and p.FILE_HEADER.Machine == 0x8664
m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
checks = 0


def ok(cond, what):
    global checks
    assert cond, what
    checks += 1
    print('PASS', what)


def ins(start, size):
    return {i.address: (i.mnemonic, i.op_str) for i in m.disasm(p.get_data(start, size), start)}


# The call site: rel32 to 0x5A1120 with rcx = soldier, rdx = &origin, r8 = &dir.
call = p.get_data(0x59B75C, 5)
ok(call[0] == 0xE8 and 0x59B75C + 5 + struct.unpack('<i', call[1:])[0] == 0x5A1120, 'spot cast call 0x59B75C -> 0x5A1120')
b = ins(0x59B689, 0xE0)
ok(b[0x59B689] == ('cmp', 'byte ptr [rbx + 0xd7a], r15b'), 'spot flag soldier+0xD7A')
ok(b[0x59B699] == ('mov', 'rax, qword ptr [rbx + 0x1550]'), 'riding test soldier+0x1550')
ok(b[0x59B6AB] == ('mov', 'rdi, qword ptr [rbx + 0x1540]') and b[0x59B6B2] == ('add', 'rdi, 0x200'), 'seat camera Type seat+0x200')
ok(b[0x59B6C1] == ('movups', 'xmm1, xmmword ptr [rbx + 0x80]'), 'on foot: dir soldier+0x80')
ok(b[0x59B6E0] == ('call', '0x6bb5a0') and b[0x59B6E5][1].endswith('[rbp + 0x30]') and b[0x59B6EE][1].endswith('[rbp + 0x20]'),
   'Type 0: eye locator matrix, origin row 3, dir row 2')
ok(b[0x59B707] == ('call', '0x6bb420') and b[0x59B717] == ('lea', 'rdx, [rbp - 0x10]') and b[0x59B713] == ('lea', 'rcx, [rdi + 0x18]')
   and b[0x59B726] == ('subps', 'xmm1, xmm6'), 'Type 1: origin seat+0x208 eye locator, dir seat+0x218 look-at minus eye')
ok(b[0x59B750] == ('lea', 'r8, [rsp + 0x60]') and b[0x59B755] == ('lea', 'rdx, [rbp - 0x40]') and b[0x59B759] == ('mov', 'rcx, rbx'),
   'arguments: rdx origin, r8 dir, rcx soldier')
c = ins(0x5A11E0, 0x40)
ok(c[0x5A11EC] == ('addss', 'xmm0, dword ptr [rip + 0x16957c4]'), 'origin y lifted (0x5A11EC)')
ok(struct.unpack('<f', p.get_data(0x1C369B8, 4))[0] == 2.0, 'lift 2.0 m (0x1C369B8)')
ok(struct.unpack('<f', p.get_data(0x1765A80, 4))[0] == 1000.0, 'reach 1000 m (0x1765A80)')
f = ins(0x58F300, 0x5C)
ok(f[0x58F32D] == ('mov', 'rcx, qword ptr [rbx + 0x1550]') and f[0x58F33F] == ('mov', 'rcx, qword ptr [rbx + 0x1540]')
   and f[0x58F346] == ('cmp', 'rax, qword ptr [rcx + 8]'), 'cast filter 0x58F300 skips the soldier and its own vehicle')

# VanillaSpot=0 (turretaim.cpp SpotHook returns without the cast): the whole effect of the spot flag is that call.
ok(b[0x59B690] == ('je', '0x59b761') and 0x59B75C + 5 == 0x59B761, "flag clear: je to 0x59B761, the cast call's return address")
branch = [a for a in sorted(b) if 0x59B696 <= a < 0x59B761]
ok([b[a] for a in branch if b[a][0] == 'call'] == [('call', '0x6bb5a0'), ('call', '0x6bb420'), ('call', '0x6bb420'), ('call', '0x5a1120')],
   'the branch calls only the two locator reads and the cast')
ok(not [a for a in branch if b[a][0] not in ('cmp', 'test') and '[' in b[a][1].split(',')[0] and 'rbp' not in b[a][1].split(',')[0] and 'rsp' not in b[a][1].split(',')[0]],
   'the branch writes nothing but its own stack (no soldier field)')
c2 = ins(0x5A1120, 0x400)
ok(('call', '0x59f630') in c2.values(), 'the cast places the SpotEffect (0x59F630) itself')
# Every instruction that addresses [reg + 0xD7A], found exactly: each occurrence of the displacement's bytes in .text is
# decoded from the start of the function that holds it (.pdata), so only real instructions are seen, never a decode
# begun mid-instruction. An occurrence outside every function's range, or one no instruction of its function covers as
# that displacement, is an immediate or data (counted, not a field access).
p.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
funcs = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in p.DIRECTORY_ENTRY_EXCEPTION)
starts = [f[0] for f in funcs]
text = next(s for s in p.sections if s.Name.startswith(b'.text'))
data = text.get_data()
base = text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
import bisect
from capstone import x86
reads, writes, seen = set(), set(), set()
at = data.find(b'\x7a\x0d\x00\x00')
while at >= 0:
    rva = base + at
    k = bisect.bisect_right(starts, rva) - 1
    if k >= 0 and funcs[k][0] <= rva < funcs[k][1] and funcs[k] not in seen:
        seen.add(funcs[k])
        fb, fe = funcs[k]
        for ins_ in md.disasm(p.get_data(fb, fe - fb), fb):
            for op in ins_.operands:
                if op.type == x86.X86_OP_MEM and op.mem.disp == 0xD7A and op.mem.index == 0:
                    first = ins_.op_str.split(',')[0]
                    kind = writes if (ins_.mnemonic.startswith('mov') and '[' in first) else reads
                    kind.add((ins_.address, ins_.mnemonic, ins_.op_str))
    at = data.find(b'\x7a\x0d\x00\x00', at + 1)
ok(reads == {(0x59B689, 'cmp', 'byte ptr [rbx + 0xd7a], r15b')}, 'soldier+0xD7A has one reader: the spot branch 0x59B689')
ok({w[0] for w in writes} == {0x570847, 0x57093C, 0x570A72, 0x570B35},
   'soldier+0xD7A is written from the input mapping alone (0x570847 / 0x57093C / 0x570A72 / 0x570B35)')
# The stock spot has no sound of its own (the custom Q's cues are the plugin's, vsynth.h MarkOwn ...): nothing reached by
# direct calls from the cast 0x5A1120 (three deep) or from the SpotEffect's constructor and virtual functions plays one of
# the game's sound effects (0x7B4510 a preset, 0x7B2A80 a voice: jetsound.cpp, emc.cpp). So VanillaSpot=0 leaves no
# stray stock sound either.
ends = {b0: e0 for b0, e0 in funcs}
base_img = p.OPTIONAL_HEADER.ImageBase
vt = p.get_data(0x17A91C8, 8 * 15)
vfuncs = [struct.unpack('<Q', vt[i:i + 8])[0] - base_img for i in range(0, 8 * 15, 8)]
ok(0x3047C0 in ends and all(f in ends or f < 0x400000 for f in vfuncs[:3]), 'SpotEffect constructor 0x3047C0 and vtable 0x17A91C8 read')
sound_calls, walked = [], set()


def walk(f, depth):
    if f in walked or depth < 0 or f not in ends:
        return
    walked.add(f)
    for i in md.disasm(p.get_data(f, min(ends[f] - f, 0x6000)), f):
        if i.mnemonic in ('call', 'jmp') and i.operands and i.operands[0].type == x86.X86_OP_IMM:
            t = i.operands[0].imm
            if t in (0x7B4510, 0x7B2A80):
                sound_calls.append((hex(f), hex(i.address)))
            if i.mnemonic == 'call':
                walk(t, depth - 1)


walk(0x5A1120, 3)
walk(0x3047C0, 2)
for f in vfuncs:
    walk(f, 2)
ok(len(walked) > 20 and not sound_calls, f'no stock sound under the spot ({len(walked)} functions walked)')
print(f'PASS: {checks} stock spot contracts; no game execution')
