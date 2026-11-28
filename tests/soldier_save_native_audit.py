"""The new classes' save (src/soldier_save.cpp, src/soldier_save_logic.h) against the game's EDF.dll.

Read only; skipped without the game. With it:
- the save request 0x7BCD40 (only caller 0x7C2F66) queues its job at 0x7BCFAF (call 0x7B5660, rdx = {control, job}),
  the job's +0x90 / +0x98 are its copy and its size, the size is the block's: the bytes soldier_save.cpp checks;
- the load completion's tail jump 0x7BE99D (weapon_records.cpp ResultCopyHook, which calls back here) is the game's;
- the class count 0xE34D0 is read as soldier_save.cpp reads it;
- the block's player records are as soldier_save_logic.h lays them out (0xDEDA0: ten classes x six slots from +0x30,
  two records 0x3E60 apart);
- no instruction addresses the reserved slot (class 9 slot 5: GS+0x6F84, GS+0xADE4) by a displacement (needs
  capstone)."""
from __future__ import annotations

import os
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


CALLS = {0x7C2F66: 0x7BCD40, 0x7BCFAF: 0x7B5660, 0x7BCE7C: 0x7BD910}
TAILS = {0x7BE99D: 0x12DA77A}
BYTES = {  # soldier_save.cpp's fingerprints
    0x7BCF9E: '4c8b4da04c8d442450488d542478488bcb',          # r9, r8 = &flags, rdx = &{control, job}, rcx = queue
    0x7BCF74: '488b064889442478488b400848894580',   # [rsp+0x78] = control, [rbp-0x80] = job
    0x7BCE6D: '41b9e8dc0000',                                # the job's size: the block's 0xDCE8
    0x7BDA06: '488b86c0000000488986900000004889be98000000',   # job+0x90 data, +0x98 size
    0xE34D0: '488b41208b4004c3',                             # the class count: [[mode+0x20]+4]
}
LAYOUT = {  # 0xDEDA0, the game status's block constructor (rcx = the block)
    0xDEDB5: '4c8d5128',          # r10 = block+0x28: the first record's current class
    0xDEDE0: '498d420c',          # rax = r10+0xC: the slots from rax-4 = block+0x30
    0xDEDE4: 'b90a000000',        # ten classes
    0xDEE08: '488d4018',          # six slots (0x18 bytes) each
    0xDEE19: '4d8d82f8000000',    # the armour counts at r10+0xF8: the slots end there
    0xDEF20: '4981c2603e0000',    # the next record 0x3E60 on
}
# The reserved slot by its displacements from the game status (block+0x11C / +0x3F7C are member offsets of many other
# objects: a scan for them says nothing; the block is reached only through GS-based displacements and the save job's
# copies, docs/newclass-re/inject.md §4).
SLOT = [(0x6F84, 0x6F88), (0xADE4, 0xADE8)]


def audit(pe, img: bytes) -> None:
    for site, target in CALLS.items():
        rel = struct.unpack_from('<i', img, site + 1)[0]
        check(img[site] == 0xE8 and site + 5 + rel == target, f'EDF+{site:#x} calls {target:#x}')
    for site, target in TAILS.items():
        rel = struct.unpack_from('<i', img, site + 1)[0]
        check(img[site] == 0xE9 and site + 5 + rel == target, f'EDF+{site:#x} tail-jumps to {target:#x}')
    for table in (BYTES, LAYOUT):
        for site, hexbytes in table.items():
            check(img[site:site + len(hexbytes) // 2].hex() == hexbytes, f'EDF+{site:#x} bytes')
    check(0x7BCF9E + len(BYTES[0x7BCF9E]) // 2 == 0x7BCFAF, 'the arguments run up to the queueing call')
    try:
        import capstone
        from capstone import x86
    except ImportError:
        print('soldier_save_native_audit: no capstone; the slot scan is skipped')
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    begin, end = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    found: set[int] = set()
    a = begin
    while a < end:   # linear sweep; an undecodable byte is skipped (padding, data in code)
        decoded = False
        for ins in md.disasm(img[a:end], a):
            decoded = True
            a = ins.address + ins.size
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base != x86.X86_REG_RIP and op.mem.base != x86.X86_REG_RSP \
                        and op.mem.base != x86.X86_REG_RBP and any(lo <= op.mem.disp < hi for lo, hi in SLOT):
                    found.add(ins.address)
        if not decoded or a < end:
            a += 1
    check(not found, f'no instruction addresses the reserved slot: {sorted(hex(x) for x in found)}')


def main() -> int:
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        pe, img = room_preview_sigs.image(dll)
        audit(pe, img)
    else:
        print('soldier_save_native_audit: no game; skipped')
    print(f'soldier_save_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
