"""The room / HQ menu screen's code checks (src/room_ui.cpp) against the real EDF.dll, read as a file (never loaded, the
game never touched): every signature InstallRoomUi compares is the bytes at its address, both OnUpdate slots hold the
stock functions in the file (the plugin reads the live value and chains whatever is there), the offsets the comments
name are in the instructions the signatures cover, and the texture skin the menu's layout uses draws nothing until a
texture is bound (Skin_Texture's draw skips an element with no texture; a state with an empty path binds none): what
keeps the screens stock without the plugin. An EDF.dll path as the argument, else the game gamedir finds; exit 77
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
# The offsets the code uses are the ones the checked instructions name.
check(consts['kPadTable'] == 0x1188315 + 7 + struct.unpack_from('<i', arrays['kPadBaseSig'], 3)[0], 'the pad table is the loop base')
check(arrays['kTintSig'][4:8] == struct.pack('<I', consts['kTint']) and
      arrays['kTintFromSig'][4:8] == struct.pack('<I', consts['kTintFrom']) and
      arrays['kTintInitSig'][2:6] == struct.pack('<I', consts['kTint']), 'the tint offsets (+0xF0, +0x100)')
check('kModelParam' not in consts and 'L"ColorChipBox"' not in src and '0x824D40' not in src,
      'the model skin call is gone (the chips are texture skins now)')
check(arrays['kFontColourSig'][2:6] == struct.pack('<I', consts['kFontColour']), 'the font colour offset')
check(arrays['kShowSig'][2:6] == struct.pack('<I', consts['kShown']), 'the shown flag offset')
check(arrays['kSetTextSig'][3:7] == struct.pack('<I', consts['kTextFont']), 'the font test SetText starts with is kTextFont')
# Skin_Texture (vtable 0x17F6228): slot 3 is the draw the tint signatures sit in; its constructor sets the tint.
vt = 0x17F6228
check(struct.unpack_from('<Q', img, vt + 3 * 8)[0] - base == 0x7F7040, 'Skin_Texture slot 3 is the draw 0x7F7040')
check(0x7F7040 < 0x7F707F < 0x7F70FC < 0x7F7190, 'the tint reads are in the draw, before its texture test')
# The draw: rax = [rdi+0A0h] (0x7F7110); test rax,rax / je out; cmp qword [rax],0 / je out (0x7F7190): no texture, no draw.
check(img[0x7F7110:0x7F7117] == bytes.fromhex('488b87a0000000'), 'the draw reads the bound texture (+0xA0)')
check(img[0x7F7190:0x7F7193] == bytes.fromhex('4885c0') and img[0x7F7193:0x7F7195] == bytes.fromhex('0f84') and
      img[0x7F7199:0x7F719D] == bytes.fromhex('48833800') and img[0x7F719D:0x7F719F] == bytes.fromhex('0f84'),
      'no texture bound: the draw returns before drawing')
out = 0x7F7195 + 4 + struct.unpack_from('<i', img, 0x7F7195)[0]
check(out == 0x7F725A and out == 0x7F719F + 4 + struct.unpack_from('<i', img, 0x7F719F)[0], 'both skip to the epilogue')
# The state change (slot 4, 0x7F7270): a state whose path is empty never calls 0x7F63E0 (cmp qword [rsp+30h],0 / je).
check(struct.unpack_from('<Q', img, vt + 4 * 8)[0] - base == 0x7F7270, 'Skin_Texture slot 4 is the state change 0x7F7270')
check(img[0x7F73E3:0x7F73E9] == bytes.fromhex('48837c243000') and img[0x7F73E9] == 0x74 and
      0x7F73F3 + 5 == 0x7F73F8 and img[0x7F73F3] == 0xE8 and
      0x7F73F8 + struct.unpack_from('<i', img, 0x7F73F4)[0] == consts['kSetTexture'],
      'an empty state path: the texture call 0x7F63E0 is jumped over')
print(f'room_ui_sig_audit: {checks} checks passed')
