"""Read the stock spot (原版 Q 标记) ray contract from the supported EDF.dll; no game execution (src/spot_ray.h).

The soldier update's spot branch, riding, takes the seat camera's locators as the ray, calls 0x5A1120 at 0x59B75C;
0x5A1120 lifts the origin by 2.0 m, casts 1000 m, filters out the soldier and its own vehicle. Missing input: 77.
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
print(f'PASS: {checks} stock spot ray contracts; no game execution')
