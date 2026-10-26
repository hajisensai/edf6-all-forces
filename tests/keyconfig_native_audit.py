"""Read the stock key configuration (原版按键设置) contract from the supported EDF.dll; no game execution (docs/keyconfig-re.md).

What the key-binding plan (docs/plan-2026-10-25-keybinds.md) builds on: the HQ key-config menu (HUiHQKeyConfig, 0x896870)
has 11 compiled pages with immediate row counts and compiled row labels; each page's actions come from a fixed table of
11 x 13 action bytes (0x4E ends a page); the bindings themselves live in the input manager, one int32 per
(player, class, action byte), 0xFF actions per class, so the actions the stock game uses (up to 0x4D) leave spare slots.
Stage 3 (the plugin's pages 11..16 in that menu, src/keyconfig.cpp): the game codes and their virtual keys, the stock
defaults, COMMON.CFG saving the whole store raw (the spare slots persist), the runtime binder reading no slot past 0x4E,
src/keyconfig_signatures.inc against the DLL, and what each patched site does (the six page dispatches and their joins,
the page table's six readers, the model's all-sets rows, the redirected calls, the top list's items).
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

# ---- Stage 3 (docs/keyconfig-re.md §5..§7): the codes, the defaults, COMMON.CFG, and every site src/keyconfig.cpp patches.
import re  # noqa: E402
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
img = p.get_memory_mapped_image()
base = p.OPTIONAL_HEADER.ImageBase
import keyconfig_sigs as sigs  # noqa: E402
ok((ROOT / 'src' / 'keyconfig_signatures.inc').read_text(encoding='utf-8') == sigs.render(img),
   'keyconfig_signatures.inc is EDF.dll\'s code (rerun tools/keyconfig_sigs.py)')
for site, callee in sigs.CALLS:
    ok(img[site] == 0xE8 and site + 5 + struct.unpack_from('<i', img, site + 1)[0] == callee, f'{site:#x} calls {callee:#x}')
keybind = (ROOT / 'common' / 'edf' / 'keybind.h').read_text(encoding='utf-8')
menu_h = (ROOT / 'src' / 'keyconfig_menu.h').read_text(encoding='utf-8')

# The keyboard codes 0x12..0x78 and their virtual keys (0x11852C0), the names (0x11856F0) and keybind.h's kKeyVk.
vks = []
for k in range(0x67):
    t = struct.unpack_from('<i', img, 0x118554C + 4 * k)[0]
    a, b2 = list(m.disasm(img[t:t + 8], t))[:2]
    assert a.mnemonic == 'mov' and a.op_str.startswith('eax, ') and b2.mnemonic == 'ret'
    vks.append(int(a.op_str.split(', ')[1], 16))
table_h = re.search(r'kKeyVk\[[^\]]*\]=\{(.*?)\};', keybind, re.S).group(1)
table_h = re.sub(r'//[^\n]*', '', table_h)
ok([int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{2}', table_h)] == vks, 'keybind.h kKeyVk is 0x11852C0\'s 0x67 virtual keys')
ok(ins(0x11852C0, 4)[0x11852C0] == ('cmp', 'ecx, 0x66') and ins(0x11856F0, 4)[0x11856F0] == ('cmp', 'ecx, 0x66'),
   'keyboard codes 0x12..0x78 (index 0..0x66) in both the virtual-key and the name tables')
names = [wstr(struct.unpack_from('<Q', img, 0x1F56050 + 8 * k)[0] - base) for k in range(0x12)]
ok(names[0] == 'OptionPlayer_NoAssign' and names[12:18] == ['OptionPlayer_MouseL', 'OptionPlayer_MouseM', 'OptionPlayer_MouseR',
   'OptionPlayer_MouseWheel', 'OptionPlayer_MouseSide1', 'OptionPlayer_MouseSide2'] and names[1] == 'OptionPlayer_A',
   'codes 0 unassigned, 1..0xB the pad, 0xC..0x11 mouse L M R wheel side1 side2 (0x1F56050)')
b = ins(0xD4290, 0x45)
ok(b[0xD42C9] == ('lea', 'ecx, [rbx - 0x12]') and b[0xD42CC] == ('cmp', 'ecx, 0x66'),
   'a key name: codes 0x12..0x78 by name, the rest index 0x1F56050 unchecked (keyconfig.cpp shows a bad code unassigned)')


def writes(start):
    out = {}
    for i in m.disasm(img[start:start + 0x900], start):
        mm = re.match(r'dword ptr \[r\w+ \+ (0x[0-9a-f]+)\], (0x[0-9a-f]+|\d+)$', i.op_str)
        if i.mnemonic == 'mov' and mm:
            d = int(mm.group(1), 16) - 0x28
            out[(d // 0x3FC, d % 0x3FC // 4)] = int(mm.group(2), 0)
        if i.mnemonic == 'ret':
            break
    return out


# The stock keyboard defaults (0xD4530 flags 0xFFF at the store's construction 0xD3F90).
common = writes(0xD48B0)
ok(all(common[(s_, a)] == v for s_ in range(4) for a, v in
       {0x16: 0x28, 0x17: 0x24, 0x18: 0x12, 0x19: 0x15, 0x1A: 0x16, 0x1B: 0x22, 0x1C: 0x25, 0x1D: 0x58, 0x1E: 0x2D, 0x1F: 0x2E,
        0x20: 0x2F, 0x21: 0x27}.items()),
   'common page defaults W S A D E, Spot Q, Shotcut T, Chat ControlL, 1 2 3, VoiceChat V, in sets 0..3 (0xD48B0)')
drive = writes(0xD52F0)
ok(all(drive[(s_, a)] == v for s_ in range(4) for a, v in {0x32: 0x28, 0x33: 0x24, 0x34: 0x63, 0x35: 0xC, 0x36: 0xD, 0x4D: 0xE}.items()),
   'drive page defaults W S Space, mouse L, M (0x36), R (0x4D) in sets 0..3 (0xD52F0)')
b = ins(0xD46C9, 0x38)
ok(b[0xD46C9] == ('mov', 'dword ptr [rdi], 0xc') and b[0xD46CF] == ('mov', 'dword ptr [rdi + 8], 0x63')
   and b[0xD46DD] == ('mov', 'dword ptr [rdi + 0x10], 0x23') and b[0xD46E4] == ('mov', 'dword ptr [rdi + 0x1c], 0x17')
   and b[0xD46EB] == ('mov', 'dword ptr [rdi + 0x18], 0x2b') and b[0xD46F2] == ('mov', 'dword ptr [rdi + 4], 0xe')
   and b[0xD46F9] == ('mov', 'dword ptr [rdi + 0x14], 0x56'),
   'infantry defaults (actions 0x22..0x29): mouse L, R, Space, the wheel, R, ShiftL, Z, F (0xD46C9, rdi = set + 0xB0)')
b = ins(0xD4821, 0x40)
ok(b[0xD4821] == ('mov', 'dword ptr [rbx + 0x150], 0xc') and b[0xD4835] == ('mov', 'dword ptr [rbx + 0x158], 0x63'),
   'heli defaults (0x4A..0x4C): mouse L, R, Space')

# COMMON.CFG: the whole store, raw, both ways; spare slots persist.
ok(img[0x17F1498:0x17F14A3] == b'COMMON.CFG\0' and rip(0x7C01F6, 8) == 0x17F1498, 'the save\'s file name COMMON.CFG (0x7C01F6)')
b = ins(0x7C01D0, 0x10)
ok(rip(0x7C01D0, 7) == 0x20B2890 and b[0x7C01D7] == ('lea', 'rdx, [rax + 0x318]'), 'the save hands the store [0x20B2890]+0x318')
ok(ins(0x7BBD58, 6)[0x7BBD58] == ('mov', 'r9d, 0x548c') and ins(0x7BCB0D, 6)[0x7BCB0D] == ('mov', 'r9d, 0x548c'),
   'COMMON.CFG write (0x7BBC30) and read (0x7BC9E0): 0x548C bytes, the whole store, every slot')
ok(ins(0xDB8F8, 6)[0xDB8F8] == ('mov', 'r8d, 0x548c'), 'the reset to defaults: the whole store from a fresh one (0xDB8C0)')
ok(re.search(r'kStoreSize=0x548C', keybind) is not None and re.search(r'kReady=0x5488', keybind) is not None,
   'keybind.h: the store size and ready flag')
b = ins(0xD3F90, 0x180)
ok(b[0xD4101] == ('mov', 'dword ptr [rsi + 0x5488], 1'), 'the store built: +0x5488 = 1 (0xD3F90)')
b = ins(0xD5470, 0x3C)
ok(b[0xD5482] == ('mov', 'dword ptr [rcx + rax*4 + 0x28], r9d') and b[0xD548B] == ('mov', 'dword ptr [rcx + rax*4 + 0x424], r9d')
   and b[0xD5497] == ('mov', 'dword ptr [rcx + rax*4 + 0x820], r9d') and b[0xD54A3] == ('mov', 'dword ptr [rcx + rax*4 + 0xc1c], r9d'),
   'an all-sets row writes sets 0..3 (0xD5470)')

# Who reads the store at run time: the binder 0xD43C0, each call an immediate action, none past 0x4E.
text = [s_ for s_ in p.sections if s_.Name.startswith(b'.text')][0]
tb = img[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
xs = [text.VirtualAddress + mm.start() for mm in re.finditer(rb'\xE8', tb)
      if text.VirtualAddress + mm.start() + 5 + struct.unpack_from('<i', tb, mm.start() + 1)[0] == 0xD43C0]
acts = set()
for c in xs:
    for back in range(48, 4, -1):
        seq = list(m.disasm(img[c - back:c + 5], c - back))
        if seq and seq[-1].address == c:
            break
    v = None
    for i in seq[:-1]:
        if i.op_str.startswith(('r9d, ', 'r9b, ')):
            v = i.op_str.split(', ')[1]
        if i.mnemonic == 'xor' and i.op_str == 'r9d, r9d':
            v = '0'
    acts.add(int(v, 0) if v and v[0].isdigit() else -1)
ok(len(xs) == 51 and -1 not in acts and max(acts) == 0x4E,
   f'the runtime binder 0xD43C0: {len(xs)} calls, each an immediate action, the highest 0x4E (slots 0x4F.. unread)')

# The six page dispatches (keyconfig_menu.h kDispatches): site, its stock bytes, back, default, join.
disp = re.findall(r'inline constexpr Dispatch k\w+\{"([^"]+)",(0x[0-9A-F]+),(\d+),(0x[0-9A-F]+),(0x[0-9A-F]+),(0x[0-9A-F]+),(\d)\}', menu_h)
ok(len(disp) == 6, 'keyconfig_menu.h: six dispatches')
for name, site, size, back, default, join, reg in disp:
    site, size, back, default = int(site, 16), int(size), int(back, 16), int(default, 16)
    seq = list(m.disasm(img[site:site + size], site))
    ok(len(seq) == 2 and seq[0].mnemonic == 'cmp' and seq[0].op_str.endswith(('0xa', '9')) and seq[1].mnemonic == 'ja'
       and int(seq[1].op_str, 16) == default and site + size == back,
       f'{name}: {seq[0].op_str}; ja {default:#x}, the jump table right after ({back:#x})')
    t = list(m.disasm(img[back:back + 0x20], back))
    ok(any(i.mnemonic == 'jmp' and i.op_str in ('rcx', 'rax') for i in t[:5]), f'{name}: {back:#x} is the jump table')
j = {n: int(jo, 16) for n, _s, _z, _b, _d, jo, _r in disp}
ok(ins(j['page row'], 9)[j['page row'] + 3] == ('mov', 'dword ptr [rsi + 0x850], ebx'), 'A: its join stores ebx as the table row')
ok(ins(j['row count'], 5)[j['row count']] == ('mov', 'ebx, 1') and
   ins(0x89738C, 0x17)[0x89738C] == ('mov', 'dword ptr [rsi + 0x844], 0xc'),
   'B: its join clones the rows up to +0x844 (page 0 sets +0x844 and [rsp+0x30] then jumps there)')
ok(ins(j['labels'], 0x20)[j['labels']] == ('mov', 'rdx, rax') and ins(0x897E77, 8)[0x897E77] == ('mov', 'dword ptr [r15 + 4], edi'),
   'C: its join takes rax as the label, sets the ids when not null')
ok(ins(j['texts'], 3)[j['texts']] == ('mov', 'r14, rax'), 'D: its join takes rax as the label, r12 the value (0x897FFC)')
ok(ins(0x8980E8, 0x1F)[0x8980EF] == ('mov', 'rcx, r15'), 'D: the item is r15 ([r15] its row, 0x897B9A)')
ok(ins(0x897B9A, 3)[0x897B9A] == ('mov', 'dword ptr [r15], r9d'), 'the item\'s row is the page\'s row r9d')
ok(ins(j['dialog'], 5)[j['dialog']] == ('lea', 'rcx, [rsp + 0x50]') and ins(0x899267, 6)[0x899267] == ('mov', 'byte ptr [rbp + 0x80], bl'),
   'E: from its join the dialog takes r14 label, r15 value, bl action')
b = ins(0x898F6B, 0x50)
ok(b[0x898FA6] == ('mov', 'r14, qword ptr [rcx + r14*8]'), 'E: r14 is the item under the cursor')
ok(ins(j['refresh'], 3)[j['refresh']] == ('mov', 'rbx, rax'), 'F: its join takes rax as the value')
b = ins(0x89AD17, 0x60)
ok(b[0x89AD17] == ('mov', 'rsi, rcx') and b[0x89AD5D] == ('lea', 'rdx, [rsi + 0x798]') and b[0x89AD64] == ('mov', 'rcx, qword ptr [rdi]'),
   'F: rsi the menu, [rdi] the item')

# The page table's readers (keyconfig_menu.h kTableLeas) and the all-sets rows.
leas = [int(x, 16) for x in re.search(r'kTableLeas\[\]=\{([^}]*)\}', menu_h).group(1).split(',')]
ok(len(leas) == 6 and all(rip(a, 7) == 0x17F26B0 for a in leas), 'the six lea\'s of the page table 0x17F26B0')
ok(rip(0x7CD9E0, 7) == 0x17F26B0 + 26, '0x7CD9E0: the common row alone (left on the stock table)')
b = ins(0x7CDE10, 0x3D)
ok(b[0x7CDE36] == ('cmp', 'r9d, 2') and b[0x7CDE3C] == ('lea', 'eax, [r9 - 5]') and b[0x7CDE40] == ('cmp', 'eax, 5')
   and b[0x7CDE45] == ('mov', 'byte ptr [rcx + 0x10], 1') and b[0x7CDE26] == ('cmp', 'eax, 3'),
   'the model: all sets for rows 2 and 5..10 (0x7CDE40 widened to 5..16); the class = page - 1 for pages 1..4, else 0')
b = ins(0x7CDD4C, 0x31)
ok(b[0x7CDD56] == ('cmp', 'byte ptr [rdi + 0x10], bpl') and b[0x7CDD63] == ('call', '0xd5470') and b[0x7CDD77] == ('call', '0xd54b0'),
   'the assignment writes all sets (0xD5470) with the model\'s +0x10, else one (0xD54B0)')
b = ins(0x7CD9B0, 0x30)
ok(b[0x7CD9CA] == ('lea', 'eax, [r9 - 3]') and b[0x7CD9D7] == ('cmp', 'eax, 7'),
   'the common-page check is rows 3..10 only: the plugin rows 11..16 are not held to it (keyconfig-re.md §7)')
ok(ins(0xD4BB0, 0x0B)[0xD4BB3] == ('cmp', 'r8d, 0xa'), 'the stock page defaults: pages 0..10 (the plugin\'s redirected)')
# the top list and its items
ok(ins(0x89304A, 0x0A)[0x89304D] == ('mov', 'r12d, 6'), 'the top list: six keyboard items')
ok(ins(0x8932A9, 0x0A)[0x8932A9] == ('mov', 'dword ptr [rbp + 0x1c8], 5') and ins(0x893325, 7)[0x893325] == ('lea', 'r9, [rbp + 0xa0]'),
   'the top list\'s request: type 5 at +0x128, at rbp+0xA0')
ok(rip(0x893401, 7) == 0x18030C0 and rip(0x893520, 7) == 0x1803088, 'the items: lambda_3 0x18030C0 (a page), lambda_2 0x1803088 (vehicles)')
ok(struct.unpack_from('<Q', img, 0x18030C0 + 16)[0] - base == 0x8928E0, 'lambda_3\'s _Do_call 0x8928E0')
b = ins(0x8928E0, 0x22)
ok(b[0x8928F1] == ('mov', 'edx, dword ptr [rbx + 0x10]') and b[0x8928FD] == ('jmp', '0x88dd00'),
   'lambda_3 opens the key config on its captured page (impl +0x10) by 0x88DD00')
ok(ins(0x7CD940, 0x46)[0x7CD975] == ('call', '0x7ccd10'), 'the dialog\'s key check: the pad / move / chat limits only')
ok(re.search(r'kFirstAction=0x4F', keybind) is not None and max(a for acts_ in PAGES for a in acts_) < 0x4F,
   'the plugins\' slots 0x4F.. are past every stock action and the 0x4E the binder reads')
print(f'{checks} checks passed')
