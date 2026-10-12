"""The room / HQ menu screen's code checks (src/room_ui.cpp) against the real EDF.dll, read as a file (never loaded, the
game never touched): every signature InstallRoomUi compares is the bytes at its address, both OnUpdate slots hold the
stock functions in the file (the plugin reads the live value and chains whatever is there), and the offsets the comments
name are in the instructions the signatures cover. An EDF.dll path as the argument, else the game gamedir finds; exit 77
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
check(arrays['kChipSiteSig'][3:7] == struct.pack('<I', consts['kSkinPtr']) and
      arrays['kChipSiteSig'][14:18] == struct.pack('<I', consts['kSkinCtrl']), 'the skin weak_ptr offsets')
check(arrays['kFontColourSig'][2:6] == struct.pack('<I', consts['kFontColour']), 'the font colour offset')
check(arrays['kShowSig'][2:6] == struct.pack('<I', consts['kShown']), 'the shown flag offset')
ref = 0x880179 + 4 + 5   # call 0x824D40 at the end of the chip site
check(ref + struct.unpack_from('<i', img, 0x88017E)[0] == consts['kModelParam'], 'the chip site calls the model parameter setter')
text = 0x880172 + 7 + struct.unpack_from('<i', img, 0x880175)[0]
check(img[text:text + 26].decode('utf-16le') == 'ColorChipBox\0', 'the chip site names L"ColorChipBox"')
print(f'room_ui_sig_audit: {checks} checks passed')
