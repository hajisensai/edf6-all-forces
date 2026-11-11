"""The world map select's code checks (src/worldmap.cpp InstallWorldMap) against the real EDF.dll, read as a file (never
loaded, the game never touched): every signature it compares is the bytes at its address, the three vtable slots it
chains hold the stock functions in the file (the plugin reads the live values and chains whatever is there), and the
offsets and calls the comments name are in the instructions the signatures cover. An EDF.dll path as the argument, else
the game gamedir finds; exit 77 (skipped) without one."""
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
src = open(os.path.join(ROOT, 'src', 'worldmap.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def rel(at: int, size: int, disp: int) -> int:
    """The target of the rip-relative operand at `at` (an instruction of `size` bytes, its disp32 at +disp)."""
    return at + size + struct.unpack_from('<i', img, at + disp)[0]


consts = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', src)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', src, re.S)}
uses = re.findall(r'\{(k\w+|0x[0-9A-Fa-f]+),(k\w+Sig),sizeof\(\2\)\}', src)
check(len(arrays) >= 18 and len(uses) == len(arrays), f'every signature array is checked once: {len(arrays)} arrays, {len(uses)} uses')
for rva_text, name in uses:
    rva = int(rva_text, 16) if rva_text.startswith('0x') else consts[rva_text]
    want = arrays[name]
    check(img[rva:rva + len(want)] == want, f'{name} at {rva:#x}: {img[rva:rva + len(want)].hex(" ")} != {want.hex(" ")}')

# The vtable 0x18052E8 (ui::HUiHQMissionSelect): slots 1, 2, 5 hold the functions the plugin chains.
vt = 0x18052E8
for slot, stock, index in (('kUpdateSlot', 'kUpdate', 1), ('kDrawSlot', 'kDraw', 2), ('kGoneSlot', 'kGone', 5)):
    check(consts[slot] == vt + 8 * index, f'{slot} is slot {index} of the vtable')
    value = struct.unpack_from('<Q', img, consts[slot])[0] - base
    check(value == consts[stock], f'{slot} {consts[slot]:#x} holds {value:#x}, not {stock} {consts[stock]:#x}')
# The vtable is the select's: its COL names HUiHQMissionSelect.
col = struct.unpack_from('<Q', img, vt - 8)[0] - base
td = struct.unpack_from('<I', img, col + 12)[0]
name = img[td + 16:td + 16 + 40].split(b'\0')[0]
check(name == b'.?AVHUiHQMissionSelect@ui@@', f'the vtable 0x18052E8 is ui::HUiHQMissionSelect\'s ({name!r})')
# The destructor calls ~HUiHQMissionSelect 0x8A31B0 and deletes the object's 0x920 bytes.
check(rel(0x8A365F, 5, 1) == 0x8A31B0 and struct.unpack_from('<I', img, 0x8A366A)[0] == 0x920, 'slot 5: the dtor + delete 0x920')
# The UI draw executor 0x11A43A0 jumps to slot 2 with rdx the xgl context.
check(0x11A43A0 < 0x11A43EA < 0x11A4400, 'the jmp [rax+10h] is in the draw executor')
# The context's D3D11 context (+8) and viewport (+0x398, 16 bytes copied by movups) as the signatures name them.
check(arrays['kViewportSig'][3:7] == struct.pack('<I', consts['kDrawViewport']), 'the viewport offset')
check(arrays['kDeviceSig'][3] == consts['kDrawDevice'], 'the D3D context offset')
# The update context: +8 the input, +0x10 dt.
check(arrays['kInputSig'][3] == consts['kCtxInput'] and arrays['kDtSig'][5] == consts['kCtxDt'], 'the update context offsets')
# The cursor, the display list and its 0x14-byte entries.
check(arrays['kCursorSig'][3:7] == struct.pack('<I', consts['kCursor']) and
      arrays['kCursorSig'][23:27] == struct.pack('<I', consts['kEntries']) and consts['kEntriesEnd'] == consts['kEntries'] + 8,
      'the cursor +0x778 and the display list +0x798 (std::vector: end at +8)')
check(arrays['kCursorSig'][16:20] == bytes.fromhex('488d1489') and consts['kEntrySize'] == 0x14,
      'lea rdx,[rcx+rcx*4] then *4: 0x14-byte entries')
# The list: [GS]+0x130 the mode config (the constructor's row count reads it), +0xF0 its table handle.
check(rel(0x8A2BC1, 7, 3) == consts['kGameStatus'] and struct.unpack_from('<I', img, 0x8A2BCB)[0] == consts['kModeCfg'],
      'mov rcx,[GS]; add rcx,130h')
check(rel(0x8A2BCF, 5, 1) == consts['kRowCount'], 'then the row count 0xE0650')
check(arrays['kRowCountSig'][3:7] == struct.pack('<I', consts['kListDoc']) and
      arrays['kKeyReadSig'][3:7] == struct.pack('<I', consts['kListDoc']), 'the table handle at +0xF0')
# 0xE0D80 (the row's key): the row's child of the table, its child 2, that node's text.
check(rel(0xE0DBB, 5, 1) == consts['kChildIndex'] and img[0xE0DCA:0xE0DCD] == bytes.fromhex('8d5602') and
      rel(0xE0DD1, 5, 1) == consts['kChildIndex'] and rel(0xE0DE1, 5, 1) == consts['kNodeText'],
      'the key reader: child(row), child(2), text')
# The shown flag every Component's constructor sets.
check(arrays['kShowSig'][2:6] == struct.pack('<I', consts['kShown']), 'the shown flag offset')
# Stage 2: the locked row's title key, read through the text table with 0x7A3500.
check(rel(0x9591AF, 7, 3) == consts['kTextTable'] and rel(0x9591B6, 7, 3) == consts['kNotOpenKey'] and
      rel(0x9591BD, 5, 1) == consts['kTextOf'], 'mov rcx,[texts]; lea rdx,[key]; call 0x7A3500')
key = 'MissionSelect_NotOpenMissionTitle'.encode('utf-16-le') + bytes(2)
check(img[consts['kNotOpenKey']:consts['kNotOpenKey'] + len(key)] == key, 'the key is L"MissionSelect_NotOpenMissionTitle"')
# A row's title: [GS]+0x248 (the TXT list), as 0x958A60 reads it.
check(rel(0x958A66, 7, 3) == consts['kGameStatus'] and arrays['kTitleSig'][13:17] == struct.pack('<I', consts['kTitles']),
      'mov rax,[GS]; mov r10,[rax+248h]')
# The current mission row GS+0x48 (0xDC0B0, called by the cursor refresh 0x8A5B2F) and ModeSave+0 at +0x57A4.
check(arrays['kCurrentSig'][2] == consts['kCurrent'] and rel(0x8A5B2F, 5, 1) == 0xDC0B0, 'GS+0x48 written by the refresh')
# The select's mode bit, the input's repeat mask.
check(arrays['kModeSig'][2:6] == struct.pack('<I', consts['kSelectMode']), 'the confirmable bit +0x7B4')
check(arrays['kRepeatSig'][3:7] == struct.pack('<I', consts['kRepeat']), 'the merged repeat mask +0x4528')
# The confirm 0x8A85C0 is the constructor's cbRequestMission; 0x8A27B8 calls the focus 0x953350 on this+0x770.
check(img[0x8A27B8:0x8A27BF] == bytes.fromhex('488d8e70070000') and struct.unpack_from('<I', img, 0x8A27BB)[0] == consts['kButtons'] and
      rel(0x8A27BF, 5, 1) == consts['kFocus'], 'the constructor: lea rcx,[rsi+770h]; call 0x953350 (the first focus)')
print(f'worldmap_sig_audit: {checks} checks passed')
