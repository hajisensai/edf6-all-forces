"""Read the multiplayer name tag's name source from the supported EDF.dll; no game execution (src/player_name.cpp).

The Q mark names a teammate by the name the game's own name tag shows (the user, 2026-10-10: "真名"). player_name.cpp
calls 0x784830 exactly as the name tag's 0x7FFBD0 does; every fact that call relies on is checked here. Missing input: 77.
"""
from pathlib import Path
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


# The name tag's draw (the function holding 0x804FB3, the tag's font scale hud.cpp copies): an empty std::wstring
# (size 0, capacity 7, first character 0) handed to 0x7FFBD0 with the player's weak reference.
t = ins(0x804E5F, 0x68)
ok(t[0x804E5F] == ('mov', 'qword ptr [rbp + 0x2c8], rdx') and t[0x804E66] == ('mov', 'qword ptr [rbp + 0x2d0], 7')
   and t[0x804E71] == ('mov', 'word ptr [rbp + 0x2b8], dx'), 'name tag: an empty std::wstring (size 0, capacity 7)')
ok(t[0x804EAA] == ('lea', 'r9, [rbp + 0x2b8]') and t[0x804EBF] == ('call', '0x7ffbd0'), 'name tag: 0x7FFBD0 fills it')
ok(0x804C90 <= 0x804FB3 < 0x8066AE, 'the font scale 0x804FB3 is in the same draw 0x804C90')
# 0x7FFBD0: the soldier's user and its control block, a strong reference taken, 0x784830(&name, &copy, false).
f = ins(0x7FFC66, 0x70)
ok(f[0x7FFC66] == ('mov', 'rbx, qword ptr [rdx + 0x1ed8]') and f[0x7FFC7D] == ('mov', 'rsi, qword ptr [rdx + 0x1ed0]'),
   'the user at soldier+0x1ED0, its control block at +0x1ED8')
ok(f[0x7FFCA5] == ('lock inc', 'dword ptr [rbx + 8]') and f[0x7FFCAE] == ('mov', 'qword ptr [rsp + 0x20], rsi')
   and f[0x7FFCB3] == ('mov', 'qword ptr [rsp + 0x28], rbx'), 'a strong reference (ctrl+8) for the copy {user, ctrl}')
ok(f[0x7FFCB8] == ('xor', 'r8d, r8d') and f[0x7FFCBB] == ('lea', 'rdx, [rsp + 0x20]') and f[0x7FFCC0] == ('mov', 'rcx, rbp')
   and f[0x7FFCC3] == ('call', '0x784830'), '0x784830(rcx name, rdx &copy, r8 false)')
ok(ins(0x7FFBF3, 4)[0x7FFBF3] == ('mov', 'rbp, r9'), 'rbp is the caller\'s name string')
# 0x784830: its prologue (player_name.cpp checks these bytes before calling), its three outcomes, the reference released.
ok(p.get_data(0x784830, 16) == bytes([0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x50,0x48]),
   '0x784830 prologue as player_name.cpp checks it')
n = ins(0x784830, 0x1D6)
ok(n[0x784852] == ('mov', 'rdi, rdx') and n[0x784855] == ('mov', 'rbx, rcx'), 'rdi the reference, rbx the name')
ok(n[0x784873] == ('mov', 'rdi, qword ptr [rdi + 8]') and n[0x784883] == ('lock xadd', 'dword ptr [rdi + 8], eax')
   and n[0x7848a8] == ('xor', 'eax, eax'), 'no user: the reference released, 0')
ok(n[0x7848c5] == ('call', '0x12abf50') and n[0x7848d0] == ('call', '0x3cfe0') and n[0x78491b] == ('mov', 'rcx, rdi')
   and n[0x78491e] == ('call', '0x1d6b20') and n[0x784923] == ('mov', 'eax, 1'), 'the nickname: assigned, the reference released, 1')
ok(n[0x784935] == ('call', '0x12abff0') and n[0x7849af] == ('mov', 'rdi, qword ptr [rdi + 8]')
   and n[0x7849bf] == ('lock xadd', 'dword ptr [rdi + 8], eax') and n[0x7849e4] == ('mov', 'eax, 2'),
   'the platform name: moved in, the reference released, 2')
r = ins(0x1D6B20, 0x30)
ok(r[0x1D6B26] == ('mov', 'rbx, qword ptr [rcx + 8]') and r[0x1D6B3B] == ('lock xadd', 'dword ptr [rbx + 8], eax'),
   '0x1D6B20 releases a shared_ptr\'s strong reference')
k = ins(0x12ABF50, 0x40)
ok(k[0x12ABF66] == ('lea', 'rdi, [rcx + 0x78]'), 'the nickname is the std::wstring at user+0x78')
# 0x3D3C0: frees the buffer (capacity 8 and up) and leaves the string empty (capacity 7, size 0).
ok(p.get_data(0x3D3C0, 16) == bytes([0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x18,0x48,0x8B,0xD9,0x48,0x83,0xFA]),
   '0x3D3C0 prologue as player_name.cpp checks it')
d = ins(0x3D3C0, 0x5B)
ok(d[0x3D3CD] == ('cmp', 'rdx, 8') and d[0x3D3D1] == ('jb', '0x3d404') and d[0x3D3FF] == ('call', '0x12d85ec')
   and d[0x3D406] == ('mov', 'qword ptr [rbx + 0x18], 7') and d[0x3D40E] == ('mov', 'qword ptr [rbx + 0x10], rax'),
   '0x3D3C0: the heap buffer freed, the string left empty')
print(f'PASS: {checks} name tag contracts; no game execution')
