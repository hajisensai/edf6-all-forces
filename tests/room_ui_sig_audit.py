"""The room / HQ menu screen's code checks (src/room_ui.cpp) against the real EDF.dll, read as a file (never loaded, the
game never touched): every signature InstallRoomUi compares is the bytes at its address, both OnUpdate slots hold the
stock functions in the file (the plugin reads the live value and chains whatever is there), the offsets the comments
name are in the instructions the signatures cover. (2026-11-24: the old overlay's texture skin / tint signatures went with
the overlay; tools/room_layout.py now adds only vcRoot, a Transparent_skin layout, and an uninstantiated button.) An
EDF.dll path as the argument, else the game gamedir finds; exit 77
(skipped) without one."""
from __future__ import annotations

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
import pefile  # noqa: E402

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the signatures are for')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
src = open(os.path.join(ROOT, 'src', 'room_ui.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


consts = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', src)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', src, re.S)}
uses = re.findall(r'\{(k\w+|0x[0-9A-Fa-f]+),(k\w+Sig),sizeof\(\2\)\}', src)
check(len(arrays) >= 18 and len(uses) == len(arrays), f'every signature array is checked once: {len(arrays)} arrays, {len(uses)} uses')
for rva_text, name in uses:
    rva = int(rva_text, 16) if rva_text.startswith('0x') else consts[rva_text]
    want = arrays[name]
    check(img[rva:rva + len(want)] == want, f'{name} at {rva:#x}: {img[rva:rva + len(want)].hex(" ")} != {want.hex(" ")}')
for slot, stock in (('kRoomSlot', 'kRoomUpdate'), ('kHqSlot', 'kHqUpdate')):
    value = struct.unpack_from('<Q', img, consts[slot])[0] - base
    check(value == consts[stock], f'{slot} {consts[slot]:#x} holds {value:#x}, not {stock} {consts[stock]:#x}')
# The screens' destructors (screen identity: a new screen at a dead one's address is a new screen). Slot 5 of each vtable
# is its scalar deleting destructor, and the destructor it runs is that class's own (it stores that vtable first).
for slot, stock, vt in (('kRoomGoneSlot', 'kRoomGone', 0x180AC40), ('kHqGoneSlot', 'kHqGone', 0x1804DD8)):
    check(consts[slot] == vt + 5 * 8, f'{slot} is slot 5 of the vtable {vt:#x}')
    value = struct.unpack_from('<Q', img, consts[slot])[0] - base
    check(value == consts[stock], f'{slot} holds {value:#x}, not {stock} {consts[stock]:#x}')
check(consts['kRoomSlot'] == 0x180AC40 + 8 and consts['kHqSlot'] == 0x1804DD8 + 8, 'the OnUpdate slots are slot 1 of the same vtables')
room_gone = arrays['kRoomGoneSig']
dtor = consts['kRoomGone'] + 20 + struct.unpack_from('<i', room_gone, 16)[0]
check(room_gone[15] == 0xE8 and dtor == 0x8F5950, f'HUiRoom\'s deleting destructor calls ~HUiRoom 0x8F5950 (got {dtor:#x})')
check(0x8F5962 - dtor < 0x20 and 0x8F5962 + 7 + struct.unpack_from('<i', arrays['kRoomDtorSig'], 3)[0] == 0x180AC40,
      '~HUiRoom stores HUiRoom\'s vtable first')
check(room_gone[25] == 0xBA and struct.unpack_from('<I', room_gone, 26)[0] == 0xCA0, 'HUiRoom is deleted with its size 0xCA0')
hq_gone = arrays['kHqGoneSig']
check(consts['kHqGone'] + 17 + struct.unpack_from('<i', hq_gone, 13)[0] == 0x1804DD8 and hq_gone[22:25] == bytes.fromhex('488901'),
      '~HUiHQMain (inline in its deleting destructor) stores HUiHQMain\'s vtable first')
check(img[0x89F562:0x89F567] == bytes.fromhex('ba10080000') and img[0x89F55C:0x89F560] == bytes.fromhex('40f6c501'),
      'HUiHQMain is deleted with its size 0x810 when flags bit 0 is set')
check('void* __fastcall RoomGoneHook(unsigned char* self,unsigned flags) { armoryui::ScreenGone(self); NoteGone(self); return screens[0].gone(self,flags); }' in src and
      'void* __fastcall HqGoneHook(unsigned char* self,unsigned flags) { armoryui::ScreenGone(self); NoteGone(self); AutoGone(self); return screens[1].gone(self,flags); }' in src,
      'both destructor hooks note the end and chain on')
# The HQ's mission select pressed by the stock ButtonApply (docs/worldmap-hq-open-re.md §5.1; kAutoMissionSigs).
def rel(at: int, size: int, disp: int) -> int:
    return at + size + struct.unpack_from('<i', img, at + disp)[0]


def wide(at: int) -> str:
    end = at
    while img[end:end + 2] != b'\0\0':
        end += 2
    return img[at:end].decode('utf-16-le')


check(rel(0x848247, 7, 3) == consts['kButtonApply'], "the action table takes ButtonApply's address at 0x848247")
reg = img[0x84824E:0x848600]
names = [rel(0x84824E + i, 7, 3) for i in range(len(reg) - 7) if reg[i:i + 3] in (b'\x48\x8d\x15', b'\x4c\x8d\x05')]
check(any(img[n:n + 24] == 'ButtonApply'.encode('utf-16-le') + b'\0\0' for n in names if 0 < n < len(img) - 24),
      'ButtonApply is registered under the name "ButtonApply" right after its address')
check(rel(0x84EBA3, 5, 1) == consts['kDynamicCast'] and rel(0x84EB90, 7, 3) == consts['kButtonType'] and
      rel(0x84EB97, 7, 3) == consts['kComponentType'], 'ButtonApply casts Component -> HUiButton by __RTDynamicCast')
pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
iat = rel(consts['kDynamicCast'], 6, 2)
check(any(imp.address - base == iat and imp.name == b'__RTDynamicCast' for e in pe.DIRECTORY_ENTRY_IMPORT for imp in e.imports),
      "the thunk 0x12DA7AA jumps through __RTDynamicCast's import")
check(wide(rel(0x84EC00, 7, 3)) == 'Click' and rel(0x84EC0A, 5, 1) == consts['kHasAction'], 'ButtonApply asks HasAction(button, "Click")')
check(rel(0x84EC20, 5, 1) == 0x83A0C0 and img[0x83A0C0:0x83A0C7] == bytes.fromhex('488b8970040000'),
      "then the window's id -> name map (0x83A0C0 reads window+0x470), not a focus call")
check(wide(rel(0x84ED6F, 7, 3)) == 'Click' and rel(0x84ED79, 5, 1) == 0x7EAB90 and rel(0x84ED5C, 7, 3) == 0x17FE090,
      'ButtonApply fires "Click" through FireAction with {0x17FE090, button+0x2B0}')
check(struct.unpack_from('<Q', img, 0x17FE090)[0] - base == 0x84E780 and img[0x84E780:0x84E785] == bytes.fromhex('488b4108c3'),
      "the event info's slot 0 returns its +8 (button+0x2B0)")
check(img[0x84ED7F:0x84ED83] == bytes.fromhex('488d45e7') and img[0x84ECFA:0x84ED01] == bytes.fromhex('498d9eb0060000'),
      "the window's action path (+0x6B0) is saved before and put back after the Click")
check('ApplyInfo info{kApplyInfoVtable,&button};' in src and
      'void* const kApplyInfoVtable[]={reinterpret_cast<void*>(&ApplyInfoTarget)};' in src,
      "the plugin's info for ButtonApply: slot 0 gives the {button, control} pair it found")
# The offsets the code uses are the ones the checked instructions name.
check(consts['kPadTable'] == 0x1188315 + 7 + struct.unpack_from('<i', arrays['kPadBaseSig'], 3)[0], 'the pad table is the loop base')
check(not ({'kTint', 'kTintFrom', 'kTextureSkin', 'kSetTexture'} & set(consts)) and
      not ({'kTintSig', 'kTintFromSig', 'kTintInitSig', 'kTextureSkinSig', 'kSetTextureSig'} & set(arrays)),
      "the overlay's texture skin and tint are gone (nothing binds or tints a texture any more)")
check(arrays['kFontColourSig'][2:6] == struct.pack('<I', consts['kFontColour']), 'the font colour offset')
check(arrays['kShowSig'][2:6] == struct.pack('<I', consts['kShown']), 'the shown flag offset')
check(arrays['kSetTextSig'][3:7] == struct.pack('<I', consts['kTextFont']), 'the font test SetText starts with is kTextFont')
print(f'room_ui_sig_audit: {checks} checks passed')
