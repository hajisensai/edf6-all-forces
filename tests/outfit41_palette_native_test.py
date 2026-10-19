"""Execute EDF6's colour routes on a private Unicorn image, without loading or driving a game.

The native HQ lookup, battle lookup (including D8700), live colour message, and
material-constant write are the instructions being tested, rather than Python
reimplementations. Only the C runtime memcpy boundary is supplied by the fixture.
Missing EDF.dll or the optional Unicorn dependency returns CTest skip code 77.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]

try:
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_R8, UC_X86_REG_R12, UC_X86_REG_R13,
                                   UC_X86_REG_R15, UC_X86_REG_RSI, UC_X86_REG_RDI,
                                   UC_X86_REG_RBX, UC_X86_REG_RBP, UC_X86_REG_RSP,
                                   UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RIP)
except ImportError:
    print('SKIP: pefile and Unicorn are required for native colour execution')
    raise SystemExit(77)

if not __debug__:
    raise RuntimeError('Run without -O: native assertions must execute')

CHECKS = 0


def check(ok: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not ok:
        raise AssertionError(message)


def main() -> int:
    import gamedir
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / 'EDF.dll'
    if not path.is_file():
        print('SKIP: supported EDF.dll is required')
        return 77
    pe = pefile.PE(str(path), fast_load=True)
    check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll timestamp')
    check(pe.FILE_HEADER.Machine == 0x8664, 'native colour fixture requires x64')
    image = pe.get_memory_mapped_image()
    spans = (
        (0x93B466, 0x7D, 'b15cf2e6a0f080bdb84a3595d587a2849c349d73bb645536619b1c2b14a46de9'),
        (0x591965, 0x97, 'e433d7edccdb220835326825aa1b076f5a4aa28c15ef3fe43770d51a1406661d'),
        (0x0D8700, 0x28, 'f129264fb7f1242ce3f591762b066ff1f115764414eebdcd92086e7b6396c451'),
        (0x93C202, 0x49, '9142d8676f3c6a530c636a6bf5e99cabe6a0ef55d2abc442fec58b61b82e03d2'),
        (0x6C32B3, 0x46, '5d06b6c4b5f62ead0fb2b19b5e1db0222a0187fdeced5fe40bd27238292c6d64'),
    )
    for rva, count, digest in spans:
        check(hashlib.sha256(image[rva:rva + count]).hexdigest() == digest,
              f'native colour code changed at {rva:#x}')

    base, heap, stack = 0x180000000, 0x100000000, 0x200000000
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(base, (len(image) + 0xFFF) & ~0xFFF)
    uc.mem_write(base, image)
    uc.mem_map(heap, 0x100000)
    uc.mem_map(stack, 0x20000)
    gs, desc, view, payload = (heap + n for n in (0x10000, 0x1000, 0x2000, 0x3000))
    rsp = stack + 0x10000
    uc.mem_write(base + 0x20B2890, struct.pack('<Q', gs))

    def put(address: int, fmt: str, *values) -> None:
        uc.mem_write(address, struct.pack(fmt, *values))

    def run(begin: int, end: int) -> None:
        uc.reg_write(UC_X86_REG_RSP, rsp)
        uc.emu_start(base + begin, base + end, count=1000)
        check(uc.reg_read(UC_X86_REG_RIP) == base + end, f'native block {begin:#x} failed to complete')

    # A valid saved palette may have independently customised colours in every
    # class/outfit/player row. Distinct exactly representable values catch reading
    # another player's slot, another base outfit, or swapping primary/secondary.
    expected = {}
    for player in range(2):
        for cls in range(4):
            for outfit in range(4):
                for channel in range(2):
                    row = gs + player * 0x3E60 + (2 * (outfit + cls * 4) + channel) * 0xC4
                    selected = (player * 7 + cls * 5 + outfit * 3 + channel) % 12
                    for chip in range(12):
                        value = ((1 + player * 64 + cls * 12 + outfit * 2 + channel) / 128,
                                 (chip + 1) / 16, (channel + 1) / 4, 1.0)
                        put(row + 0x6FB0 + chip * 16, '<4f', *value)
                        if chip == selected:
                            expected[player, cls, outfit, channel] = struct.pack('<4f', *value)
                    put(row + 0x7070, '<I', selected)

    for player in range(2):
        for cls in range(4):
            for outfit in range(4):
                put(gs + player * 0x3E60 + 0x6E90, '<ii', cls, outfit)
                uc.reg_write(UC_X86_REG_R13, player)
                uc.reg_write(UC_X86_REG_R12, outfit)
                uc.reg_write(UC_X86_REG_R15, cls)
                uc.reg_write(UC_X86_REG_RSI, desc)
                run(0x93B466, 0x93B4E3)
                for channel in range(2):
                    colour = expected[player, cls, outfit, channel]
                    check(bytes(uc.mem_read(desc + 0x24 + 16 * channel, 16)) == colour,
                          'HQ must read this player/class/original outfit colour slot')
                    uc.reg_write(UC_X86_REG_R8, gs)
                    uc.reg_write(UC_X86_REG_R12, player)
                    uc.reg_write(UC_X86_REG_RBX, channel)
                    run(0x591965, 0x5919FC)
                    check(bytes(uc.mem_read(rsp + 0x50, 16)) == colour,
                          'battle must read the same saved colour as HQ')

    # Live colour editing remains scoped to the exact view identity. The sixth
    # outfit choice keeps the native outfit identity, so these are its messages too.
    for changed_field in (-1, 0, 1, 2):
        for channel in range(2):
            uc.mem_write(view, bytes(0x450))
            put(view + 0x2AC, '<i', 1)
            put(view + 0x180, '<i', 2)
            put(view + 0x178, '<i', 3)
            fields = [1, 2, 3, channel]
            if changed_field >= 0:
                fields[changed_field] += 1
            colour = expected[1, 2, 3, channel]
            put(payload, '<4i', *fields)
            uc.mem_write(payload + 16, colour)
            uc.reg_write(UC_X86_REG_RSI, payload)
            uc.reg_write(UC_X86_REG_RDI, view)
            stop = 0x93C2B9 if changed_field < 0 else 0x93C27C
            run(0x93C202, stop)
            wanted = colour if changed_field < 0 else bytes(16)
            check(bytes(uc.mem_read(view + 0x158 + 16 * channel, 16)) == wanted,
                  'live colour target must accept exactly matching player/class/outfit')
            check(bytes(uc.mem_read(view + 0x33C + 16 * channel, 16)) == wanted,
                  'live colour edit must update the retained model descriptor too')
            other = 1 - channel
            check(bytes(uc.mem_read(view + 0x158 + 16 * other, 16)) == bytes(16),
                  'live edit must preserve the other channel')

    # Execute the real material setter's upload, including its 16-byte maximum
    # and dirty flag. This shader reflects float3 colours: copy 12 bytes only and
    # leave adjacent padding untouched. memcpy is a C runtime boundary.
    holder, handle, definition, params, buffers, buffer, constants, source = (
        heap + n for n in (0x4000, 0x4100, 0x4200, 0x4500, 0x4600, 0x4700, 0x4800, 0x4900))
    put(holder, '<Q', handle)
    put(handle, '<Q', definition)
    put(handle + 0x38, '<Q', buffers)
    put(definition + 0x138, '<Q', params)
    put(buffers, '<Q', buffer)
    put(buffer + 0x30, '<Q', constants)
    frame = stack + 0x18000
    put(frame - 0x49, '<Q', source)

    def memcpy(native, address, _size, _user):
        if address != base + 0x12DA77A:
            return
        dst = native.reg_read(UC_X86_REG_RCX)
        src = native.reg_read(UC_X86_REG_RDX)
        count = native.reg_read(UC_X86_REG_R8)
        native.mem_write(dst, bytes(native.mem_read(src, count)))
        sp = native.reg_read(UC_X86_REG_RSP)
        target, = struct.unpack('<Q', native.mem_read(sp, 8))
        native.reg_write(UC_X86_REG_RSP, sp + 8)
        native.reg_write(UC_X86_REG_RIP, target)

    uc.hook_add(UC_HOOK_CODE, memcpy)
    for channel in range(2):
        colour = expected[1, 2, 3, channel]
        offset = 16 + 16 * channel
        put(params + 24 * channel, '<QIIQ', 0, offset, 0, 12)
        uc.mem_write(constants, bytes([0xCD]) * 64)
        uc.mem_write(source, colour)
        uc.mem_write(buffer + 0x48, b'\0')
        uc.reg_write(UC_X86_REG_R12, holder)
        uc.reg_write(UC_X86_REG_RBX, channel)
        uc.reg_write(UC_X86_REG_RBP, frame)
        run(0x6C32B3, 0x6C32F9)
        check(bytes(uc.mem_read(constants + offset, 12)) == colour[:12],
              'actual native material upload must reach reflected shader RGB constants')
        check(bytes(uc.mem_read(constants + offset + 12, 4)) == bytes([0xCD]) * 4,
              'float3 upload must preserve constant-buffer padding')
        check(bytes(uc.mem_read(buffer + 0x48, 1)) == b'\1', 'material upload must mark GPU constants dirty')
    print(f'outfit41_palette_native: {CHECKS}/{CHECKS} native execution checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
