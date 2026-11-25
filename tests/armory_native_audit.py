"""整备's own native addresses (src/armory.cpp, src/armory_presets.cpp) against the real EDF.dll, read as a file (never
loaded, the game never touched). docs/armory-re.md §1 / §2 and docs/loadout-re.md §1 / §8.1:

  - armory.cpp: every signature InstallArmory compares is the bytes at its address; the two Draw slots are slot 2 of
    ui::HUiHQMain's / ui::HUiRoom's vtables (RTTI names) and both hold HUiBase::Draw 0x7FA970 in the file; the draw
    context's +8 (the ID3D11DeviceContext the viewport setter calls RSSetViewports on, vtable +0x160) and +0x398 (the
    viewport {x, y, w, h} it stores) are the displacements in the signed instructions, the same vtables room_ui.cpp hooks.
  - armory_presets.cpp: its weapon table functions, GameStatus pointer and player offsets are weapon_equip.cpp's (whose
    function bytes tools/weapon_equip_sigs.py signs, and weapon_equip's Ready() gates every preset write), and the
    game's own code shows them: callers pass `[EDF+0x20B2890] + 0x130` to FindRow, the three functions read the table
    node at cfg+0x188, GetRow writes the name pointer at Row+0 and the category at Row+0x10 and never past 0x2D + 127
    bytes (the plugin's 0x100 buffer), set_player_soldier writes GS+0x6E90 + player*0x3E60 and set_weapon
    GS+0x6E98 + player*0x3E60 + class*0x18 + slot*4.

An EDF.dll path as the argument, else the game gamedir finds; exit 77 (skipped) without one."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

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
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def source(name: str) -> str:
    with open(os.path.join(ROOT, 'src', name), encoding='utf-8') as f:
        return f.read()


def constants(text: str) -> dict[str, int]:
    return {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', text)}


def rip(at: int, size: int) -> int:
    """The address a rip-relative instruction at `at` (`size` bytes, disp32 last) refers to."""
    return at + size + struct.unpack_from('<i', img, at + size - 4)[0]


def qword(rva: int) -> int:
    return struct.unpack_from('<Q', img, rva)[0]


def rtti_name(vtable: int) -> str:
    """The class name of `vtable` from its CompleteObjectLocator (vtable[-1]; x64: RVAs, the type descriptor at +0xC)."""
    col = qword(vtable - 8) - base
    check(struct.unpack_from('<I', img, col)[0] == 1, f'{vtable:#x}: an x64 CompleteObjectLocator')
    td = struct.unpack_from('<I', img, col + 0xC)[0]
    end = img.index(b'\0', td + 0x10)
    return img[td + 0x10:end].decode('ascii')


# --- armory.cpp ---
armory = source('armory.cpp')
ac = constants(armory)
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', armory, re.S)}
uses = re.findall(r'\{(k\w+|0x[0-9A-Fa-f]+),(k\w+Sig),sizeof\(\2\)\}', armory)
check(len(arrays) == 3 and sorted(n for _, n in uses) == sorted(arrays),
      f'every armory.cpp signature array is checked once: {sorted(arrays)}, {uses}')
for rva_text, name in uses:
    rva = int(rva_text, 16) if rva_text.startswith('0x') else ac[rva_text]
    want = arrays[name]
    check(img[rva:rva + len(want)] == want, f'{name} at {rva:#x}: {img[rva:rva + len(want)].hex(" ")} != {want.hex(" ")}')
for slot, vtable, cls in (('kHqDrawSlot', 0x1804DD8, 'HUiHQMain'), ('kRoomDrawSlot', 0x180AC40, 'HUiRoom')):
    check(ac[slot] == vtable + 2 * 8, f'{slot} {ac[slot]:#x} is slot 2 of the vtable {vtable:#x}')
    check(rtti_name(vtable) == f'.?AV{cls}@ui@@', f'{vtable:#x} is ui::{cls}\'s vtable (RTTI {rtti_name(vtable)})')
    check(qword(ac[slot]) - base == ac['kStockDraw'], f'{slot} holds {qword(ac[slot]) - base:#x}, not HUiBase::Draw')
    check(f'{vtable:#X}'.replace('0X', '0x') in armory, f'armory.cpp names the vtable {vtable:#x}')
check(ac['kStockDraw'] == 0x7FA970 and img[0x7FA970:0x7FA975] == bytes.fromhex('48895c2418'), 'HUiBase::Draw 0x7FA970')
# The same vtables room_ui.cpp hooks OnUpdate / the destructor in (slot 1).
rc = constants(source('room_ui.cpp'))
check(rc['kHqSlot'] == ac['kHqDrawSlot'] - 8 and rc['kRoomSlot'] == ac['kRoomDrawSlot'] - 8,
      'room_ui.cpp\'s OnUpdate slots are slot 1 of the same vtables')
# The viewport setter 0x113E8F0(ctx, const int vp[4] + float z[2]): vp stored at ctx+0x398, then
# ctx+8 ->RSSetViewports (ID3D11DeviceContext vtable index 44 = +0x160) with the viewport converted to floats.
check(struct.unpack_from('<I', arrays['kViewportSig'], 3)[0] == ac['kDrawViewport'] == 0x398 and
      img[0x113E903:0x113E906] == bytes.fromhex('0f1002'), 'ctx+0x398: the 16 bytes {x, y, w, h} copied from the argument')
check(arrays['kDeviceSig'][3] == ac['kDrawDevice'] == 8, 'ctx+8: the signed load\'s displacement')
check(img[0x113E981:0x113E98A] == bytes.fromhex('488b01ff9060010000'), 'ctx+8 is the D3D11 context: call [vtbl+0x160] RSSetViewports')
check(0x113E8F0 < 0x113E90B < 0x113E928 < 0x113E981 and img[0x113E8F0:0x113E8F4] == bytes.fromhex('4883ec48') and
      img[0x113E99B] == 0xC3, 'the three offsets are one function 0x113E8F0..0x113E99B')

# --- armory_presets.cpp goes through weapon_equip.cpp only (the one reader / writer of the weapon table and GS+0x6E90 /
# +0x6E98): it keeps no GameStatus / Row offsets of its own; the offsets below are weapon_equip.cpp's.
presets = source('armory_presets.cpp')
check(not constants(presets), 'armory_presets.cpp has no raw offsets / RVAs of its own')
check(re.search(r'\bwequip::(FindRow|ReadRow|SetClassSlots|SetClass|ReadEquipped)\b', presets) is not None,
      'armory_presets.cpp reads and writes through weapon_equip')
wc = constants(source('weapon_equip.cpp'))
pc = {'kGameStatus': wc['kGameStatus'], 'kFindRow': wc['kFindRow'], 'kGetRow': wc['kGetRow'], 'kRowCount': wc['kRowCount'],
      'kCfg': wc['kGsTable'], 'kClassStride': wc['kGsClassStride']}
weq = source('weapon_equip.cpp')
check(re.search(r'\bkRowName=0x00\b', weq) and wc['kRowCategory'] == 0x10, 'weapon_equip.cpp Row name +0, category +0x10')
check(wc['kGsTable'] == 0x130 and wc['kTableRows'] == 0x188, 'weapon_equip.cpp cfg GS+0x130, table node cfg+0x188')
check(re.search(r'unsigned char buffer\[0x100\]', weq) is not None, 'weapon_equip.cpp reads a Row into 0x100 bytes')
check('bool Ready() noexcept' in weq, 'weapon_equip.cpp writes only behind its checked code (Ready)')
import weapon_equip_sigs  # noqa: E402

signed = {rva for rva, _, _ in weapon_equip_sigs.SIGNATURES}
check({pc['kFindRow'], pc['kGetRow'], pc['kRowCount']} <= signed, 'FindRow / GetRow / RowCount are weapon_equip\'s signed functions')
with open(weapon_equip_sigs.INC, encoding='utf-8') as f:
    check(f.read() == weapon_equip_sigs.render(img), 'weapon_equip_signatures.inc is this EDF.dll\'s code')
# cfg = [EDF+0x20B2890] + 0x130 at FindRow's callers (0xAC9E0 set_weapon, 0x5A41DA).
for load, add, call in ((0xAC9ED, 0xAC9F4, 0xACA03), (0x5A41DA, 0x5A41E1, 0x5A41EB)):
    check(img[load:load + 3] == bytes.fromhex('488b0d') and rip(load, 7) == pc['kGameStatus'], f'{load:#x}: rcx = GS')
    check(img[add:add + 3] == bytes.fromhex('4881c1') and struct.unpack_from('<I', img, add + 3)[0] == pc['kCfg'],
          f'{add:#x}: rcx += 0x130')
    check(img[call] == 0xE8 and rip(call, 5) == pc['kFindRow'], f'{call:#x}: call FindRow')
# The table node at cfg+0x188 in all three.
check(img[0xE1BB4:0xE1BBB] == bytes.fromhex('0f108188010000'), 'FindRow reads cfg+0x188')
check(img[0xE1D12:0xE1D19] == bytes.fromhex('0f108188010000'), 'GetRow reads cfg+0x188')
check(img[0xE23F0:0xE23F7] == bytes.fromhex('488b9188010000'), 'RowCount reads cfg+0x188')
# GetRow(cfg, Row* out = rdx -> r14, id): +0 the name (col 0's string), +0x10 the category (col 2), the col 6 bytes
# from +0x2D, as many as the signed byte count at +0x2C (at most 127): the Row ends by 0x2D + 0x7F.
check(img[0xE1D24:0xE1D27] == bytes.fromhex('4c8bf2'), 'GetRow: r14 = out')
check(img[0xE1DEC] == 0xE8 and rip(0xE1DEC, 5) == 0x551E0 and img[0xE1DF1:0xE1DF4] == bytes.fromhex('498906'),
      'GetRow: Row+0 = col 0\'s string (0x551E0)')
check(img[0xE1F17:0xE1F1B] == bytes.fromhex('41894610'), 'GetRow: Row+0x10 = col 2 (the category)')
check(img[0xE22C7:0xE22CB] == bytes.fromhex('41884e2c') and img[0xE2382:0xE2387] == bytes.fromhex('410fbe462c') and
      0x2D + 0x7F <= 0x100, 'GetRow: the col 6 count is a signed byte, the Row fits 0x100 bytes')
check(img[0xE23C0:0xE23C3] == bytes.fromhex('498bc6'), 'GetRow returns out')
# set_player_soldier / set_weapon: the class at GS+0x6E90 + player*0x3E60, the weapons at
# GS+0x6E98 + (player*0xF98 + slot + class*6)*4.
check(img[0xAC688:0xAC68F] == bytes.fromhex('4869d0603e0000') and rip(0xAC68F, 7) == pc['kGameStatus'] and
      img[0xAC696:0xAC69D] == bytes.fromhex('898c02906e0000'), 'set_player_soldier writes GS+0x6E90 + player*0x3E60')
check(img[0xACA67:0xACA6E] == bytes.fromhex('4869d0980f0000') and img[0xACA71:0xACA75] == bytes.fromhex('488d0c5b') and
      img[0xACA84:0xACA88] == bytes.fromhex('488d144a') and rip(0xACA7D, 7) == pc['kGameStatus'] and
      img[0xACA88:0xACA8F] == bytes.fromhex('89b490986e0000') and 6 * 4 == pc['kClassStride'] and 0xF98 * 4 == wc['kGsPlayerStride'],
      'set_weapon writes GS+0x6E98 + player*0x3E60 + class*0x18 + slot*4')
print(f'armory_native_audit: {checks} checks passed')
