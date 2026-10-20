"""The code the EDF4.1 outfit rests on (src/outfit41.cpp, docs/edf41-outfits.md), as byte signatures read from EDF.dll
(TimeDateStamp 0x678CCB46): writes src/outfit41_signatures.inc. tests/outfit41_native_audit.py checks the .inc against
the game's EDF.dll, and the call sites, vtables and offsets the hooks rely on.

    python tools/outfit41_sigs.py [EDF.dll]
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import room_preview_sigs  # noqa: E402  (image(): the TimeDateStamp check)

INC = os.path.join(HERE, '..', 'src', 'outfit41_signatures.inc')

# (rva, size, what it holds for the outfit)
SIGNATURES: tuple[tuple[int, int, str], ...] = (
    (0x0E34E0, 0x3F, 'the battle model path (tables, class, outfit): SoldierInit[class][2][outfit]'),
    (0x0E3420, 0x48, 'the menu model path (tables, class, outfit): SoldierInit[class][5][1][outfit]'),
    (0x0D8700, 0x28, 'the player outfit (GS, player): GS +0x6E94 + player * 0x3E60, below 0 the mission default'),
    (0x0D8730, 0x4B, 'the player battle path (GS, player, class): ends in a jump to 0xE34E0'),
    (0x59DE73, 0x23, 'own preload (0x59DE50): player in ecx, class GS +0x6E90, the path from 0xD8730'),
    (0x59DD0F, 0x1D, 'session preload (0x59DC90): the member record\'s class and outfit, the path from 0xE34E0'),
    (0x591227, 0x12, 'online creation: 0x5A3F90(desc from 0x595960, ...)'),
    (0x59120E, 0x10, 'online creation: 0x595960(desc, &member, ...) right before it, in the same 0x591130'),
    # Only the read of +0x48 (4 bytes): EDF6Coop's [Mission] Extend rewrites the `cmp 4; jcc` right after both
    # (0x5959E1, 0x59DCEA: jumps of its own, multislot/src/patches.cpp) and loads before us.
    (0x5959DD, 0x04, "online desc: the member's mission index +0x48 picks its record"),
    (0x59DCD9, 0x04, "session preload: the member's mission index +0x48 picks its record"),
    (0x9393B0, 0x28, "3D view constructor: OnMessage(this, 3, {this +0x2AC player, ...}) called directly, not by the vtable"),
    (0x8A9B15, 0x14, 'soldier screen: the outfit change unlocked flag GS +0x300 copied to screen +0x7A4'),
    (0x8AB585, 0x1E, 'outfit popup: built only while screen +0x7A4 is set'),
    (0x70D912, 0x28, "mission result: GS +0x300 set once the current list's last row is cleared (0xDF990)"),
    (0x0DEDD5, 0x0B, "new save: the outfit +0x6E94 is -1 (the mission's)"),
    (0x59144D, 0x13, 'offline creation: 0x5A3F90(desc from 0x595C80, ...)'),
    (0x595AE2, 0x0F, 'online desc +0: -1 for a remote member (0x12AC420), else 0'),
    (0x595CBD, 0x06, 'offline desc +0: the local slot'),
    (0x12AC420, 0x08, 'the member\'s remote bit: +0x10 bit 1'),
    (0x5A3FE3, 0x1E, 'creation: class desc +8, outfit desc +0xC into 0xE34E0'),
    (0x5A4087, 0x2E, 'creation: desc +0 == -1 marks the object remote (+0x128 bit 0)'),
    (0x93B412, 0x19, 'view: msg-3 Desc menu path from 0xE3420'),
    (0x93BCEE, 0x1A, 'view: msg-6 menu path from 0xE3420'),
    (0x8AB642, 0x07, 'popup item 0: the mission-default lambda (vtable 0x1805CF0)'),
    (0x8ABAE0, 0x56, 'popup: the items copied into the request\'s vector (+0x238 data, +0x240 capacity, +0x248 size)'),
    (0x8ABB49, 0x4E, 'popup: type 6, the cursor from the screen\'s outfit (+0x7A0) at request +0x148'),
    (0x8ABBFE, 0x17, 'popup: 0x119C600(system, out, type, request at rbp+0xE0)'),
    (0x8AD560, 0x55, 'the mission-default lambda\'s call: screen +0x798 / +0x7A0 / +0x818, GS +0x6E94, 0x7E2EB0'),
    (0x8AD410, 0x16, 'its _Copy: vtable and the captured screen'),
    (0x8AD780, 0x16, 'its _Move'),
    (0x089950, 0x20, 'the popup item\'s copy constructor (vector, dst, src)'),
    (0x0B91D0, 0x23, 'the item vector\'s reserve (vector, capacity)'),
    (0x03D450, 0x18, 'std::wstring::assign(ptr, len)'),
    (0x7E2EB0, 0x1E, 'the popup closed (the lambdas\' tail)'),
    (0x8BDE80, 0x61, 'the HQ 3D view rebuilt (player, class, outfit): message 3'),
    # The outfit change still locked (outfit41_logic.h OfferLocked): what the unlocked screen does that we repeat.
    (0x8AB310, 0x20, 'HUiHQSoldierSelect OnUpdate (vtable 0x1805788 slot 1): it builds the outfit popup'),
    (0x8A9B37, 0x34, 'soldier screen, unlocked: player 0 off the pad (0x1183D70): the keyboard help key from [0x20B29A8] by 0x7A3500'),
    (0x8A9B8A, 0x2F, 'soldier screen, unlocked: the help text assigned to +0x738, +0x6F2 set'),
    (0x8A9C6E, 0x13, 'soldier screen, unlocked: else the pad help key'),
    (0x18058F0, 0x5E, 'the keyboard help key Sys_Control_Keyboard_Description_SoldierSelect (UTF-16, NUL)'),
    (0x18058A0, 0x4C, 'the pad help key Sys_Control_Description_SoldierSelect (UTF-16, NUL)'),
    (0x1183D70, 0x08, 'the pad-mode flag the help asks'),
    (0x7A3500, 0x10, 'the text table lookup (table, key) -> text'),
    (0x8ABC4D, 0x19, 'the popup: its five items destroyed by 0x8E170, 0x90 bytes each'),
    (0x08E170, 0x2A, 'the popup item destructor: its +0x80 tree, its std::function, its text'),
    # The EDF4.1 outfit's own colours (outfit41_logic.h Reconcile): the battle reads a player's colour where the plugin
    # swaps the block: GS +0x6FB0 / +0x7070 + player*0x3E60 + ((0xD8700 outfit + class*4)*2 + channel)*0xC4.
    (0x591965, 0x65, "battle colour: the block of (0xD8700's outfit + class*4), channel * 0xC4, index +0x7070, chips +0x6FB0"),
    # Online, the others see this machine's colours in its member record (2026-10-20 review of #226): the script's
    # MissionSync_Begin (AngelScript binding 0x7920E0, "int MissionSync_Begin(int)") packs it by 0x78E9A0 from GS, the
    # four outfits' picked colours of the player's class (0x78EC70..0x78ECB4). The colour swap is made right before it.
    (0x7920E0, 0x17, 'MissionSync_Begin(int): 0x78E9A0(edx), the member record packed from GS'),
    (0x78EC70, 0x46, 'member record: the class\'s four outfits\' picked primary (+0x6EEC) / secondary (+0x6FB0) chips'),
    # The save (MAIN.GST, GS +0x6E68 .. +0x14B50, 0xDCE8 bytes) is a copy: 0x7C2E30 (0x7C2F0C: GS + 0x6E68) hands the
    # block to 0x7BCD40, whose job constructor 0x7BD910 (called at 0x7BCE7C) memcpys it into the job (its vector +0xC0,
    # then +0x90 data, +0x98 size); the run 0x7C1CB0 checksums (0x7C1270) and writes that copy. The plugin puts the stock
    # colours into the copy, never into GS: the save holds no EDF4.1 colours, the game's view never changes.
    (0x7C2F0C, 0x07, 'MAIN.GST save: the block GS + 0x6E68'),
    (0x7BCE6D, 0x14, 'MAIN.GST save: 0x7BD910(job, ..., the block, 0xDCE8, ...)'),
    (0x7BD9F4, 0x27, 'save job: memcpy(the vector +0xC0, block, size), then +0x90 = data, +0x98 = size'),
    (0x7C2228, 0x1D, 'save run: the header\'s length from +0x98, checksum 0x7C1270(+0x90, +0x98)'),
)
# The save's block and the job's copy (outfit41.cpp SaveJobHook).
MAIN_BLOCK, MAIN_SIZE, JOB_DATA, JOB_SIZE = 0x6E68, 0xDCE8, 0x90, 0x98

# The five stock items' std::function vtables: (vtable rva, its _Do_call) in the popup's order (outfit -1, 2, 3, 1, 0).
ITEM_VTABLES: tuple[tuple[int, int, int], ...] = (
    (0x1805CF0, 0x8AD560, -1), (0x1805D28, 0x8AD5C0, 2), (0x1805D60, 0x8AD620, 3), (0x1805D98, 0x8AD680, 1),
    (0x1805DD0, 0x8AD6E0, 0),
)
# The HQ / room 3D view (UiHQ_3DModelView): its vtable and OnMessage, the slot the menu path hook chains (its player at
# +0x2AC while it asks 0xE3420 at 0x93B426 / 0x93BD03, both inside OnMessage).
VIEW_VTABLE, VIEW_SLOT_MESSAGE, VIEW_ONMESSAGE = 0x180FA60, 3, 0x93B320
# The soldier screen (HUiHQSoldierSelect): its vtable and OnUpdate, the slot the locked-outfit hook chains.
SOLDIER_VTABLE, SOLDIER_SLOT_UPDATE, SOLDIER_UPDATE = 0x1805788, 1, 0x8AB310
# rel32 calls the hooks redirect: (call site, the callee it must hold)
CALLS: tuple[tuple[int, int], ...] = (
    (0x591234, 0x5A3F90), (0x59145B, 0x5A3F90), (0x5A3FFC, 0x0E34E0), (0x59DD27, 0x0E34E0), (0x59DE91, 0x0D8730),
    (0x591219, 0x595960), (0x9393D3, 0x93B320), (0x93B426, 0x0E3420), (0x93BD03, 0x0E3420), (0x8ABC10, 0x119C600),
    (0x7920EA, 0x78E9A0), (0x7BCE7C, 0x7BD910), (0x7C2F66, 0x7BCD40),
)


def render(img: bytes) -> str:
    lines = ['// Generated by tools/outfit41_sigs.py from EDF.dll 0x678CCB46: do not edit. (outfit41.cpp CheckCode)']
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
