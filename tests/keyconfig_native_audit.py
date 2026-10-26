"""Read the stock key configuration (原版按键设置) contract from the supported EDF.dll; no game execution (docs/keyconfig-re.md).

What the key-binding plan (docs/plan-2026-10-25-keybinds.md) builds on: the HQ key-config menu (HUiHQKeyConfig, 0x896870)
has 11 compiled pages with immediate row counts and compiled row labels; each page's actions come from a fixed table of
11 x 13 action bytes (0x4E ends a page); the bindings themselves live in the input manager, one int32 per
(player, class, action byte), 0xFF actions per class, so the actions the stock game uses (up to 0x4D) leave spare slots.
Missing input: 77.
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


def rip(at, size):
    """Target of the rip-relative operand of the instruction at `at` (its length is `size`)."""
    i = next(m.disasm(p.get_data(at, size), at))
    assert 'rip' in i.op_str, i.op_str
    disp = int(i.op_str.split('rip')[1].strip(' +]').split(']')[0].replace(' ', ''), 16)
    return at + i.size + (disp if '+' in i.op_str.split('rip')[1] else -disp)


def wstr(rva):
    b = p.get_data(rva, 160)
    return b[:next(k for k in range(0, len(b), 2) if b[k:k + 2] == b'\0\0')].decode('utf-16le')


# The page action table: 11 pages x 13 bytes, each list ended by 0x4E (the stock actions only).
PAGES = [
    [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x09],
    [0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15],
    [0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1C, 0x1E, 0x1F, 0x20, 0x21],
    [0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x29, 0x28],
    [0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31],
    [0x32, 0x33, 0x34, 0x35, 0x4D, 0x36],
    [0x37, 0x38, 0x39],
    [0x3A, 0x3B, 0x3C, 0x3D, 0x3E],
    [0x3F, 0x40, 0x41, 0x42, 0x43, 0x44],
    [0x45, 0x46, 0x47, 0x48, 0x49],
    [0x4A, 0x4B, 0x4C],
]
table = p.get_data(0x17F26B0, 13 * len(PAGES))
for k, acts in enumerate(PAGES):
    row = table[13 * k:13 * k + 13]
    ok(list(row[:len(acts)]) == acts and row[len(acts)] == 0x4E and not any(row[len(acts) + 1:]),
       f'page action list {k}: {len(acts)} actions, 0x4E, zero padding')
ok(max(a for acts in PAGES for a in acts) == 0x4D, 'highest stock action byte 0x4D (the store holds 0xFF per class)')
b = ins(0x7CD2E4, 0x20)
ok(b[0x7CD2EA] == ('imul', 'rbx, rax, 0xd') and rip(0x7CD2EE, 7) == 0x17F26B0, 'page list = 0x17F26B0 + page*13 (0x7CD2EA)')

# The binding store: input manager [0x20B2890] + 0x318; one int32 at +0x28 + (player*0xA00 + class*0xFF + action)*4.
b = ins(0x7CD2B0, 0x1E)
ok(rip(0x7CD2B0, 7) == 0x20B2890 and b[0x7CD2C2] == ('lea', 'rcx, [rax + 0x318]') and b[0x7CD2C9] == ('jmp', '0xd4290'),
   'key name of a binding: manager [0x20B2890] + 0x318 -> 0xD4290')
b = ins(0xD4260, 0x24)
ok(b[0xD4265] == ('imul', 'r8, rdx, 0xff') and b[0xD426C] == ('lea', 'r10, [rax + rax*4]')
   and b[0xD4270] == ('movzx', 'edx, r9b') and b[0xD4274] == ('shl', 'r10, 9')
   and b[0xD427E] == ('mov', 'eax, dword ptr [rcx + r10*4 + 0x28]'), 'binding read 0xD4260: [player*0xA00 + class*0xFF + action]')
b = ins(0xD41F0, 0x60)
ok(b[0xD4210] == ('lea', 'rcx, [rbx + rbx*4]') and b[0xD4214] == ('shl', 'rcx, 0xb') and b[0xD4222] == ('cmp', 'ebx, 2'),
   'two players of 0x2800 bytes each (0xD41F0)')

# The menu: 11 pages, immediate row counts, compiled labels; a 22-entry edit buffer in the menu object.
b = ins(0x897366, 0x30)
ok(b[0x89736F] == ('cmp', 'eax, 0xa') and b[0x897372] == ('ja', '0x89727b'), 'keyboard pages 0..10 (0x89736F)')
ROWS = {0: 0xC, 1: 8, 2: 7, 3: 7, 4: 8, 5: 'r12d', 6: 3, 7: 3, 8: 5, 9: 'r12d', 10: 5}
for page, rows in ROWS.items():
    t = struct.unpack('<i', p.get_data(0x898894 + 4 * page, 4))[0]
    i = next(m.disasm(p.get_data(t, 12), t))
    want = rows if isinstance(rows, str) else (hex(rows) if rows > 9 else str(rows))
    ok((i.mnemonic, i.op_str) == ('mov', f'dword ptr [rsi + 0x844], {want}'), f'page {page} row count {rows} is an immediate')
ok(ins(0x896A34, 0x8)[0x896A34] == ('mov', 'r12d, 6'), 'r12d = 6 rows for the Drive page')
b = ins(0x7CD0A0, 0x10)
ok(b[0x7CD0A3] == ('add', 'eax, -0x16') and b[0x7CD0A6] == ('cmp', 'eax, 0x2e'), 'action label switch: 0x16..0x44 only (0x7CD0A0)')
ok(wstr(rip(0x897E52, 7)) == 'OptionPlayer_KeyboardHeliAttack1' and wstr(rip(0x897E40, 7)) == 'OptionPlayer_KeyboardHeliFlight',
   'Heli page labels compiled in 0x896870')
b = ins(0x8969FB, 0xE8)
ok(b[0x8969FB] == ('mov', 'dword ptr [rsi + 0x7b0], r15d') and b[0x896AD2] == ('mov', 'dword ptr [rsi + 0x804], 0x15'),
   'menu edit buffer this+0x7B0..0x804: 22 dwords')
print(f'{checks} checks passed')
