"""The new classes' player object and per-type table patches (src/soldier_sites.inc, src/soldier_type_logic.h,
src/soldier_factory.cpp, src/soldier_type_patches.cpp) against the game's EDF.dll.

Read only. Without the game only the source checks run. With it:
- every SOLDIER_PRINT row is the code at its address, every SOLDIER_PATCH row's `from` is (and `to` has its length);
- the three AssultSoldier vtables (0x17CDF28 94 slots, 0x17CE220 22, 0x17CE2D8 2) are the slot lists the clone is built from,
  each with a complete object locator before it (the clone copies it: the dynamic_cast at 0x5A4065 needs it);
- the slot 83 (+0x298) calls the patches leave stock are the ones docs/newclass-re/inject.md §2 lists (no new one);
- the key config set index soldier+0xD40 is written by the class constructors only (AssultSoldier's writes 0)."""
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


SITES = read('src/soldier_sites.inc')
LOGIC = read('src/soldier_type_logic.h')
PRINTS = [(int(rva, 16), bytes.fromhex(hexs), what) for rva, hexs, what in
          re.findall(r'SOLDIER_PRINT\(0x([0-9A-F]+),"([0-9a-f ]+)",\s*"([^"]*)"\)', SITES)]
PATCHES = [(int(rva, 16), bytes.fromhex(a), bytes.fromhex(b), what) for rva, a, b, what in
           re.findall(r'SOLDIER_PATCH\(0x([0-9A-F]+),"([0-9a-f ]+)","([0-9a-f ]+)",\s*"([^"]*)"\)', SITES)]


def table(name: str) -> list[int]:
    m = re.search(r'k' + name + r'\[[A-Za-z0-9]+\]=\{([^}]*)\}', LOGIC)
    return [int(x, 16) for x in re.findall(r'0x([0-9A-F]+)', m.group(1))] if m else []


def constant(name: str) -> int:
    m = re.search(r'\b' + name + r'=0x([0-9A-F]+)', LOGIC)
    return int(m.group(1), 16) if m else -1


def source_checks() -> None:
    lines = sum(1 for line in SITES.splitlines() if line.startswith(('SOLDIER_PRINT', 'SOLDIER_PATCH')))
    check(len(PRINTS) + len(PATCHES) == lines and lines > 0, f'every site row parses ({len(PRINTS)} + {len(PATCHES)} of {lines})')
    check(all(len(a) == len(b) for _, a, b, _ in PATCHES), 'every patch writes as many bytes as it expects')
    check(len({rva for rva, *_ in PRINTS}) == len(PRINTS), 'no print listed twice')
    check(max(len(b) for _, b, _ in PRINTS) <= 48, 'no print longer than kMostSiteBytes')
    check(len(table('MainVtable')) == 94 and len(table('Vtable120')) == 22 and len(table('Vtable1940')) == 2,
          'the clone tables have 94 / 22 / 2 slots')
    check(table('MainVtable')[83] == 0x550A20 and table('MainVtable')[1] == constant('kDtor'),
          'slot 83 is the type, slot 1 the deleting destructor')
    check(table('Vtable120')[0] == 0x550414 and table('Vtable1940')[0] == 0x550420, 'the secondary slot 0s are the dtor thunks')
    factory = read('src/soldier_factory.cpp')
    check('L"ZwVehicler"' in factory and 'L"ZwRuneBlade"' in factory, 'the factories are the plan §1 class names')
    check('BuildClone(' in factory and 'SetSlot(' in factory, 'the factory builds on edf41classes clones')


def rd64(img: bytes, rva: int) -> int:
    return struct.unpack_from('<Q', img, rva)[0]


def audit(pe, img: bytes) -> None:
    base = pe.OPTIONAL_HEADER.ImageBase
    for rva, code, what in PRINTS:
        check(img[rva:rva + len(code)] == code, f'EDF+{rva:#x} ({what}): {img[rva:rva + len(code)].hex()} is not {code.hex()}')
    for rva, a, b, what in PATCHES:
        check(img[rva:rva + len(a)] == a, f'EDF+{rva:#x} ({what}): {img[rva:rva + len(a)].hex()} is not {a.hex()}')
    for name, at in (('MainVtable', constant('kVtable')), ('Vtable120', constant('kTable120')), ('Vtable1940', constant('kTable1940'))):
        slots = table(name)
        live = [rd64(img, at + 8 * i) - base for i in range(len(slots))]
        check(live == slots, f'vtable {at:#x}: the file has {[hex(x) for x in live]}')
        col = rd64(img, at - 8) - base
        check(0 < col < len(img) and struct.unpack_from('<I', img, col)[0] == 1, f'vtable {at:#x} has a complete object locator')
    # Where the 22-byte damage site sits, nothing jumps into it (the stub replaces whole instructions; 0x81F85A is the way back).
    site, size = constant('kDamageSite'), 22
    try:
        import capstone
        from capstone import x86
    except ImportError:
        print('soldier_native_audit: no capstone; the scans are skipped')
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    begin, end = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    into: list[int] = []
    type_calls: set[int] = set()
    d40_writes: set[int] = set()
    a = begin
    while a < end:   # linear sweep; an undecodable byte is skipped
        decoded = False
        for ins in md.disasm(img[a:end], a):
            decoded = True
            a = ins.address + ins.size
            if ins.group(x86.X86_GRP_JUMP) or ins.group(x86.X86_GRP_CALL):
                op = ins.operands[0] if ins.operands else None
                if op is not None and op.type == x86.X86_OP_IMM:
                    if site < op.imm < site + size or any(s < op.imm < s + 6 for s in (0x1B5095, 0x222813)):
                        into.append(ins.address)
                if (ins.group(x86.X86_GRP_CALL) and op is not None and op.type == x86.X86_OP_MEM
                        and op.mem.base != x86.X86_REG_RIP and op.mem.disp == 0x298):
                    type_calls.add(ins.address)
            if (ins.mnemonic == 'mov' and len(ins.operands) == 2 and ins.operands[0].type == x86.X86_OP_MEM
                    and ins.operands[0].mem.disp == 0xD40 and ins.operands[0].size == 4):
                d40_writes.add(ins.address)
        if not decoded or a < end:
            a += 1
    check(not into, f'nothing jumps into a patched site: {[hex(x) for x in into]}')
    # docs/newclass-re/inject.md §2: the 18 `call [reg+0x298]`, 14 of them SoldierBase slot 83.
    listed = {0x467ADD, 0x47E0D0, 0x65A514, 0x65A5D3, 0x1B5095, 0x222813, 0x59945C, 0x95F5EE, 0x5D9876, 0x5D9890,
              0x5D98AB, 0x5D98C6, 0x5DF7BC, 0x5DF7D1, 0x5DF7E7, 0x5DF7FD, 0x81F83E, 0x83107E}
    check(type_calls == listed, f'the slot 83 calls are the 18 reviewed: extra {sorted(hex(x) for x in type_calls - listed)}, '
                                f'missing {sorted(hex(x) for x in listed - type_calls)}')
    # The key config set (soldier+0xD40, read as [player block +0x340] + set*0xA80 by 0x56D8B9 .. 0x570BA3): the class
    # constructors write it (AssultSoldier 0, Wing Diver 2, Air Raider 1, ...), so a clone keeps the Ranger's set 0.
    check(0x55004D in d40_writes, 'AssultSoldier constructor writes +0xD40')


def main() -> int:
    source_checks()
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        pe, img = room_preview_sigs.image(dll)
        audit(pe, img)
    else:
        print('soldier_native_audit: no game; the EDF.dll checks are skipped')
    print(f'soldier_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
