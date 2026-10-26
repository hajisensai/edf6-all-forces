"""The code the plugin's key config pages rest on (src/keyconfig.cpp, src/keyconfig_menu.h, docs/keyconfig-re.md §7), as
byte signatures read from EDF.dll (TimeDateStamp 0x678CCB46): writes src/keyconfig_signatures.inc. EDF6VehicleCrew checks
every one before it patches anything (keyconfig.cpp CheckCode); tests/keyconfig_native_audit.py checks the .inc against the
game's EDF.dll and what each site does.

    python tools/keyconfig_sigs.py [EDF.dll]
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import room_preview_sigs  # noqa: E402  (image(): the TimeDateStamp check)

INC = os.path.join(HERE, '..', 'src', 'keyconfig_signatures.inc')

# (rva, size, what it holds for the pages)
SIGNATURES: tuple[tuple[int, int, str], ...] = (
    # HUiHQKeyConfig's build 0x896870: the six page dispatches (keyconfig_menu.h kDispatches) and their joins.
    (0x896CF9, 0x1F, 'A page row: lea edx,[rax-1]; cmp edx,9; ja 0x896D63 (row 2); jump table 0x89886C'),
    (0x896D63, 0x09, 'A join: xor r15d,r15d; mov [rsi+0x850],ebx (the page\'s table row)'),
    (0x896D86, 0x31, 'the model 0x7CDE10(this+0x798, player +0x520, page +0x84C, row ebx) for the keyboard'),
    (0x897366, 0x26, 'B row count: movsxd rax,[rsi+0x84C]; mov edx,ebx; cmp eax,0xA; ja 0x89727B; jump table 0x898894'),
    (0x89727B, 0x12, 'B default: [rsp+0x30] = 11 layout rows; the row elements cloned up to [rsi+0x844] from 0x897288'),
    (0x89738C, 0x17, 'B page 0: [rsi+0x844] = [rsp+0x30] = 12, then 0x897288'),
    (0x897B87, 0x34, 'C labels: [r15] = row r9d; cmp r8d,0xA; ja 0x897E7F; jump table 0x8988C0'),
    (0x897E59, 0x2D, 'C join: the text lookup, mov rdx,rax; the ids edi / ebx into the item when the label is not null'),
    (0x8980C5, 0x23, 'D texts: movsxd rax,[rsi+0x84C]; cmp eax,0xA; ja 0x897F28; jump table 0x89893C'),
    (0x8980E8, 0x1F, 'D page 0: r12 = the value (0x899830), the label (0x899750), then 0x897F25'),
    (0x897F25, 0x0A, 'D join: mov r14,rax; xor edi,edi; test r14,r14; je 0x897EE9'),
    (0x897F5B, 0x2F, 'D: the label r14 set on the item\'s name element [r15+4]'),
    (0x897FFC, 0x2F, 'D: the value r12 set on the item\'s value element [r15+8]'),
    (0x898F6B, 0x6F, 'E dialog: the item under the cursor +0x840 in r14, bl = 0, the page dispatch (jump table 0x8993F4)'),
    (0x8991AC, 0x0F, 'E join: the dialog\'s parameters built'),
    (0x89920A, 0x12, 'E: the dialog gets the page +0x84C and the row +0x850'),
    (0x899267, 0x06, 'E: the dialog\'s action = bl'),
    (0x89AD17, 0x46, 'F refresh: each item [rdi], the page dispatch (jump table 0x89B014)'),
    (0x89AF3E, 0x08, 'F join: mov rbx,rax (the value text)'),
    # The model (+0x798 of the menu and of the dialog) and the store.
    (0x7CDE10, 0x3D, 'the model 0x7CDE10: player +4, page +0xC, row +8, set +0x14, all-sets +0x10 for rows 2 and 5..10'),
    (0x89BEC8, 0x20, 'the dialog\'s own model 0x7CDE10(model +0x798, player +0x7D8, page +0x898, row +0x89C)'),
    (0x7CD2B0, 0x1E, 'the key name of (model, action): store [0x20B2890]+0x318, 0xD4290'),
    (0xD4290, 0x45, 'the key name: the slot read, codes 0x12..0x78 by 0x11856F0'),
    (0x7CD2D0, 0x30, 'a page\'s conflict: the row\'s list 0x17F26B0 + row * 13'),
    (0x7CDA40, 0x6A, 'the assignment: the old key, the row\'s list, the pad / move / chat limits, 0x7CD9B0'),
    (0x7CDB34, 0x13, 'the assignment: r14 = the page table 0x17F26B0 (a kTableLeas lea), the key list on the stack'),
    (0x7CDBE4, 0x12, 'the assignment: the row\'s list walked from r15'),
    (0x7CDC46, 0x1D, 'the assignment: a conflict in the row swaps (the list at 0x17F26B0)'),
    (0x7CDD4C, 0x31, 'the assignment written: +0x10 all sets by 0xD5470, else the one set by 0xD54B0'),
    (0x7CD9B0, 0x28, 'a class / vehicle row\'s key must not be the common page\'s: rows 3..10 only'),
    (0x7CCF10, 0x12, 'the row\'s list 0x17F26B0 + row * 13'),
    (0x7CCE80, 0x12, 'the dialog\'s common-page check: the rows\' lists'),
    (0xD5470, 0x3C, 'write an action to sets 0..3'),
    (0xD54B0, 0x28, 'write an action to one set'),
    (0xD4260, 0x24, 'read an action of a set'),
    (0xD48B0, 0x1E1, 'the common page\'s defaults into sets 0..3 (Chat 0x1D and VoiceChat 0x21 changed by keyconfig_menu.h kStockDefaults)'),
    # The page table and its readers (keyconfig_menu.h kTableLeas).
    (0x17F26B0, 0x8F, 'the page table: 11 rows of 13 bytes, 0x4E ends each row\'s actions'),
    (0x7CD9E0, 0x07, 'the common row\'s list (row 2) for 0x7CD9B0: left on the stock table'),
    # 「恢复默认」, the dialog's key check, the top list.
    (0x89A279, 0x1A, 'the page\'s defaults 0xD4BB0(store, player, page); then for page > 0 the row max(page, 3)'),
    (0x89A59D, 0x14, 'the defaults: the row\'s list from 0x7CCF10'),
    (0xD4BB0, 0x23, 'the page\'s defaults: pages 0..10 only (cmp r8d,0xA; ja)'),
    (0x89D1D3, 0x25, 'the dialog: 0x7CD940(model, ?, action +0x8E0, key) before the conflict check 0x7CCD50'),
    (0x7CD940, 0x46, 'may this key go to this action: the pad, move and chat limits only'),
    (0x89304A, 0x0A, 'the top list: six keyboard items (r12d = 6)'),
    (0x8932A9, 0x0A, 'the top list: request type 5'),
    (0x8933F8, 0x17, 'the top list: an item captures (screen, index) with lambda_3 0x18030C0'),
    (0x893520, 0x07, 'the top list: the vehicles\' item, lambda_2 0x1803088'),
    (0x893325, 0x17, 'the top list: 0x119C600(system, out, type, request at rbp+0xA0)'),
    (0x8928E0, 0x22, 'lambda_3\'s call: the popup closed, 0x88DD00(screen, [impl+0x10])'),
    (0x88DD00, 0x24, 'the key config opened on a page'),
    (0x7A3500, 0x10, 'the text table lookup (table, key) -> text'),
    (0x119C600, 0x10, 'the dialog shown'),
    (0x0B91D0, 0x23, 'the item vector\'s reserve (vector, capacity)'),
    (0x089950, 0x20, 'the item\'s copy constructor (vector, dst, src)'),
    (0x08E170, 0x2A, 'the item\'s destructor'),
    (0x03D450, 0x18, 'std::wstring::assign(ptr, len)'),
)
# rel32 calls the hooks redirect: (call site, the callee it must hold)
CALLS: tuple[tuple[int, int], ...] = ((0x89A286, 0xD4BB0), (0x89D1F1, 0x7CD940), (0x893337, 0x119C600))


def render(img: bytes) -> str:
    lines = ['// Generated by tools/keyconfig_sigs.py from EDF.dll 0x678CCB46: do not edit. (keyconfig.cpp CheckCode)']
    for i, (rva, size, what) in enumerate(SIGNATURES):
        data = ','.join(f'0x{b:02X}' for b in img[rva:rva + size])
        lines.append(f'// {what}')
        lines.append(f'constexpr unsigned char kSig{i}[]={{{data}}};')
    lines.append('constexpr Signature kSignatures[]={')
    lines += [f'    {{0x{rva:X},kSig{i},sizeof(kSig{i})}},' for i, (rva, _, _) in enumerate(SIGNATURES)]
    lines.append('};')
    return '\n'.join(lines) + '\n'


def main(argv: list[str]) -> int:
    import gamedir
    path = argv[1] if len(argv) > 1 else os.path.join(gamedir.find_or_dev(), 'EDF.dll')
    _, img = room_preview_sigs.image(path)
    with open(INC, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render(img))
    print(f'wrote {os.path.normpath(INC)}: {len(SIGNATURES)} signatures')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
