"""The 整备 look page (src/armory_look.cpp, src/armory_look_logic.h) against EDF.dll read as a file: its signatures
(src/armory_look_signatures.inc) are the game's code, and the GameStatus offsets the page uses are the ones that code
reads / writes. The EDF.dll as the argument (else the game gamedir finds); without one: the source checks only, exit 77.
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import gamedir  # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def logic_constant(text: str, name: str) -> int | None:
    m = re.search(rf'\b{name}=(0x[0-9A-Fa-f]+|\d+)', text)
    return int(m.group(1), 0) if m else None


def audit_source() -> dict[str, int]:
    import armory_look_sigs as sigs
    logic = read('src/armory_look_logic.h')
    cpp = read('src/armory_look.cpp')
    want = {'kPlayerStride': 0x3E60, 'kGsClass': 0x6E90, 'kGsOutfit': 0x6E94, 'kGsColours': 0x6FB0,
            'kChannelStride': 0xC4, 'kSelected': 0xC0, 'kSwatchStride': 0x10, 'kSwatches': 12, 'kChannels': 2,
            'kOutfits': 4, 'kColourClasses': 10}
    got = {}
    for name, value in want.items():
        got[name] = logic_constant(logic, name)
        check(got[name] == value, f'armory_look_logic.h {name}={value:#x} (got {got[name]})')
    check(want['kSwatches'] * want['kSwatchStride'] == want['kSelected'], 'the selected index follows the 12 swatches')
    check(want['kColourClasses'] * want['kOutfits'] * want['kChannels'] * want['kChannelStride'] <= want['kPlayerStride'],
          'the colour blocks fit the player stride')
    rvas = {rva for rva, _, _ in sigs.SIGNATURES}
    m = re.search(r'kPlayerOutfit=0x([0-9A-F]+)', cpp)
    check(bool(m) and int(m.group(1), 16) in rvas, 'armory_look.cpp kPlayerOutfit is a signed function')
    check('kGsUnlocked=0x300' in cpp, 'armory_look.cpp kGsUnlocked=0x300')
    check('kGameStatus=0x20B2890' in cpp, 'armory_look.cpp kGameStatus=0x20B2890')
    check('CheckSignatures(kSignatures,"ARMORY look")' in cpp, 'armory_look.cpp checks its signatures')
    return got


def audit_code(img: bytes) -> None:
    import armory_look_sigs as sigs
    with open(sigs.INC, encoding='utf-8') as f:
        check(f.read() == sigs.render(img), 'armory_look_signatures.inc is EDF.dll\'s code (rerun tools/armory_look_sigs.py)')

    def has(rva: int, size: int, hexbytes: str) -> bool:
        return bytes.fromhex(hexbytes) in img[rva:rva + size]

    def rip(at: int, size: int) -> int:   # a rip-relative operand: the instruction at `at`, `size` long, disp last
        return at + size + struct.unpack_from('<i', img, at + size - 4)[0]
    # 0xD8700(GS, player): imul edx, player, 0x3E60; mov eax, [rdx+rcx+0x6E94]
    check(has(0xD8700, 0x28, '69d0603e0000') and has(0xD8700, 0x28, '946e0000'), 'worn outfit: GS+0x6E94 + player*0x3E60')
    # 0x8AB300: mov rax, [GS]; movzx eax, byte [rax+0x300]
    check(rip(0x8AB300, 7) == 0x20B2890 and has(0x8AB300, 0x0F, '0fb68000030000'), 'unlock: byte GS+0x300')
    # 0x8AD5DC: the stock outfit item writes GS+0x6E94 + player*0x3E60
    check(has(0x8AD5DC, 0x31, '69d0603e0000') and has(0x8AD5DC, 0x31, '946e000002000000'), 'stock outfit item: GS+0x6E94 = 2')
    check(rip(0x8AD5DC + 0x1F, 7) == 0x20B2890, 'stock outfit item: GS at EDF+0x20B2890')
    # 0x882790: imul r10, ..., 0xC4; imul r9, player, 0x3E60; mov [r9+r10+0x7070], r8d (0x6FB0 + 0xC0)
    check(has(0x882790, 0x42, '4c69d2c4000000') and has(0x882790, 0x42, '69c8603e0000') and
          has(0x882790, 0x42, '70700000'), 'colour setter: GS+0x7070 + player*0x3E60 + block*0xC4')
    # 0x8817A3: (player*0x3E6 + 0x6FB + idx)*0x10 + block*0xC4
    check(has(0x8817A3, 0x57, 'e6030000') and has(0x8817A3, 0x57, 'fb060000') and has(0x8817A3, 0x57, '48c1e204') and
          has(0x8817A3, 0x57, 'c4000000'), 'swatch read: (player*0x3E6 + 0x6FB + idx)*0x10 + block*0xC4')


def main() -> int:
    got = audit_source()
    if got.get('kGsColours') is not None and got.get('kSelected') is not None:
        check(got['kGsColours'] + got['kSelected'] == 0x7070, 'kGsColours + kSelected is the setter\'s 0x7070')
    arg = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else ''
    game_dir = os.path.dirname(os.path.abspath(arg)) if arg else gamedir.find()
    dll = arg or (os.path.join(game_dir, 'EDF.dll') if game_dir else '')
    skipped = not (dll and os.path.isfile(dll))
    if not skipped:
        import room_preview_sigs
        _, img = room_preview_sigs.image(dll)
        audit_code(img)
    else:
        print('armory_look_native_audit: no game; the EDF.dll checks skipped')
    print(f'armory_look_native_audit: {checks - failed}/{checks} checks passed')
    if failed:
        return 1
    return 77 if skipped else 0


if __name__ == '__main__':
    sys.exit(main())
