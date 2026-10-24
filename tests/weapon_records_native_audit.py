"""The weapon records' patch sites (src/weapon_records_sites.inc, src/weapon_records.cpp) against the game's EDF.dll.

Read only; skipped without the game. With it:
- every row of the site list is the instruction at its address, and decodes with the constant where the row says;
- the whole code section holds no other instruction using one of the record offsets (0xEB50, 0xEB54, 0xEB44 GS-based;
  0x7CE8, 0x7CEC, 0x7CF4 block-based; 0x139C, 0xA69 folded) than the listed ones and the known unrelated ones below,
  and no other `mov ecx, 0x150A0` (the game status's allocation);
- every call the plugin wraps calls what it expects, the import calls and the weapon select screen's reads are the
  bytes weapon_records.cpp checks."""
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


OFFSETS = {0xEB50, 0xEB54, 0xEB44, 0x7CE8, 0x7CEC, 0x7CF4, 0x139C, 0xA69}
# Instructions using one of those numbers for something else (H: read in their functions), or on purpose not patched.
NOT_RECORDS = {
    0xDEF30: '0xDEDA0 block init: stack / record copies of the block too (the game status call is wrapped instead)',
    0x1E99E0: 'byte store into a 0x139C+ stack frame (0x1E31F0)',
    0x21366F: 'cmp edx, 0x139C: an id compare (0x20FF70)',
    0x267A27: 'float member +0x139C of another object (0x267910)',
    0x26B08A: 'float member +0x139C of another object (0x26AF70)',
    0x4CF1D9: 'byte member +0x139C of another object (0x4CEEB0)',
    0x4D25D4: 'member address +0x139C of another object (0x4D24D0)',
    0x14D8496: 'stack slot rbp+0x139C (0x14D793B)',
    0x15AB962: 'stack slot rbp+0x139C (0x15A92B7)',
    0x15ABD63: 'stack slot rbp+0x139C (0x15A92B7)',
}
CALLS = {  # site: expected target (weapon_records.cpp)
    0x70AF25: 0x12DA77A, 0x70B079: 0x12DA77A, 0x70B61C: 0x12DA77A, 0x70B79C: 0x12DA77A,
    0x70B23A: 0x12DA77A, 0x70B421: 0x12DA77A, 0x70B956: 0x12DA77A, 0xDB96D: 0x12DA77A,
    0x7C1AD1: 0x12DA77A, 0x8B49B1: 0x12DA77A, 0xD6468: 0xDEDA0, 0x7C2F66: 0x7BCD40, 0x7C228B: 0x7B9990,
}
BYTES = {
    0x7B90CD: 'ff158dc5f900', 0x7B9C28: 'ff1532baf900', 0x7B69F1: 'ff1561ecf900', 0x7B7FEB: 'ff1567d6f900',
    0x8B4F70: '0fb6848be87c0000', 0x8B51A0: '418b948de8840000',
}


def sites() -> list[tuple[int, int, int, int, bytes]]:
    with open(os.path.join(ROOT, 'src', 'weapon_records_sites.inc'), encoding='utf-8') as f:
        text = f.read()
    out = []
    for rva, size, field, value, code in re.findall(r'\{0x([0-9A-F]+),(\d+),(\d+),0x([0-9A-F]+),\{([^}]*)\}\}', text):
        out.append((int(rva, 16), int(size), int(field), int(value, 16), bytes(int(b, 16) for b in code.split(','))))
    return out


def audit(pe, img: bytes) -> None:
    rows = sites()
    check(len(rows) == 101, f'101 rows in the site list (has {len(rows)})')
    for rva, size, field, value, code in rows:
        check(img[rva:rva + size] == code, f'EDF+{rva:#x} is the listed instruction')
        check(struct.unpack_from('<I', code, field)[0] == value, f'EDF+{rva:#x}: the constant at +{field}')
    for site, target in CALLS.items():
        rel = struct.unpack_from('<i', img, site + 1)[0]
        check(img[site] == 0xE8 and site + 5 + rel == target, f'EDF+{site:#x} calls {target:#x}')
    for site, hexbytes in BYTES.items():
        check(img[site:site + len(hexbytes) // 2].hex() == hexbytes, f'EDF+{site:#x} bytes')
    try:
        import capstone
        from capstone import x86
    except ImportError:
        print('weapon_records_native_audit: no capstone; the exhaustive scan is skipped')
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    begin, end = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    listed = {rva for rva, *_ in rows}
    found: set[int] = set()
    alloc: set[int] = set()
    a = begin
    while a < end:   # linear sweep; an undecodable byte is skipped (padding, data in code)
        decoded = False
        for ins in md.disasm(img[a:end], a):
            decoded = True
            a = ins.address + ins.size
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base != x86.X86_REG_RIP and op.mem.disp in OFFSETS:
                    found.add(ins.address)
                if op.type == x86.X86_OP_IMM and op.imm in OFFSETS:
                    found.add(ins.address)
            if ins.bytes[:5] == b'\xb9\xa0\x50\x01\x00':
                alloc.add(ins.address)
        if not decoded or a < end:
            a += 1
    record_rows = {rva for rva, size, field, value, code in rows if value in OFFSETS}
    check(found - record_rows - set(NOT_RECORDS) == set(),
          f'no unlisted record access: {sorted(hex(x) for x in found - record_rows - set(NOT_RECORDS))}')
    check(record_rows <= found, f'every record row decodes as one: {sorted(hex(x) for x in record_rows - found)}')
    alloc_rows = {rva for rva, size, field, value, code in rows if value == 0x150A0}
    check(alloc == alloc_rows, f'the allocations are the twelve listed: {sorted(hex(x) for x in alloc ^ alloc_rows)}')
    check(listed == record_rows | alloc_rows | {rva for rva, size, field, value, code in rows if value in (0x800, 0x7FF)},
          'every row is a record offset, a record count or an allocation')


def main() -> int:
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        pe, img = room_preview_sigs.image(dll)
        audit(pe, img)
    else:
        print('weapon_records_native_audit: no game; skipped')
    print(f'weapon_records_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
