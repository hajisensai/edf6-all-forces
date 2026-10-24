"""The BMPT's twin barrels in turn (src/twin_cadence.h, src/payload.cpp TwinBarrelFrame): the weapon fields it reads, as
the supported EDF.dll uses them. Read-only; no game run.

- the shot sets the cooldown +0xE0C to FireInterval +0x36C (0x6981E0 / 0x6981F7);
- the weapon update counts +0xE0C down by +0xE10, clamps it at 0 and tests it against FLT_EPSILON (0x693A58..0x693A95);
- the same update reads and clears the trigger latch +0x139 (0x6935B1) that the holder pull 0x62C000 sets: what the
  cadence clears for a barrel that must wait;
- the V603 input pulls holders 0 and 1 together on its one primary trigger (0x6214A4 region), the pair being in turn
  only by the plugin, and its slot 48 plays the one 'Fire' state for any holder's shot (0x621700).
"""
from pathlib import Path
import struct
import sys

if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: contract assertions must execute')
import capstone
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def code(rva: int, size: int) -> list:
    return list(md.disasm(pe.get_data(rva, size), rva))


def text(rva: int, size: int) -> list[str]:
    return [f'{i.mnemonic} {i.op_str}' for i in code(rva, size)]


# The bytes payload.cpp InstallPayload matches before it turns the cadence on.
assert pe.get_data(0x6981E0, 7) == bytes.fromhex('418b876c030000')
assert pe.get_data(0x6981F7, 9) == bytes.fromhex('f3410f11870c0e0000')
assert pe.get_data(0x693A58, 20) == bytes.fromhex('4c8dbe0c0e0000f3410f1007f30f5c86100e0000')
assert pe.get_data(0x6935B1, 7) == bytes.fromhex('4488ae39010000')

# The shot: FireInterval (or the burst's own interval while a burst runs) into the cooldown, as a float.
shot = text(0x6981D6, 0x2A)
assert shot[0] == 'cmp dword ptr [r15 + 0xe18], 0' and shot[2] == 'mov eax, dword ptr [r15 + 0x36c]', shot
assert 'cvtdq2ps xmm0, xmm0' in shot and 'movss dword ptr [r15 + 0xe0c], xmm0' in shot, shot
# The countdown: cooldown - step, clamped below at xmm6 (0), stored, then above FLT_EPSILON means still cooling (state 2).
step = text(0x693A58, 0x4D)
assert step[:3] == ['lea r15, [rsi + 0xe0c]', 'movss xmm0, dword ptr [r15]', 'subss xmm0, dword ptr [rsi + 0xe10]'], step
assert 'movss dword ptr [r15], xmm1' in step and 'comiss xmm1, xmm8' in step, step
assert 'mov dword ptr [rsi + 0xbd4], 2' in step, step
eps = 0x693A95 + 0x15A2EEB   # the rip-relative constant 0x693A8C loads into xmm8 (movss xmm8,[rip+0x15a2eeb], 9 bytes)
assert code(0x693A8C, 9)[0].op_str == 'xmm8, dword ptr [rip + 0x15a2eeb]'
assert struct.unpack('<f', pe.get_data(eps, 4))[0] == struct.unpack('<f', struct.pack('<f', 1.1920929e-07))[0]
# The latch: copied to +0x13A, then cleared by the update; the pull sets it (0x62C000 + 0x13: mov byte [rax+0x139], 1).
latch = text(0x69358D, 0x2B)
assert latch[0] == 'movzx eax, byte ptr [rsi + 0x139]' and 'mov byte ptr [rsi + 0x139], r13b' in latch, latch
assert pe.get_data(0x62C013, 7) == bytes.fromhex('c6803901000001')
# V603 slot 48 (0x621700, vtable 0x17DC620 + 48*8): one 'Fire' state for any holder ([+0x2AC8] on player [+0x2AC0]).
vt = struct.unpack('<Q', pe.get_data(0x17DC620 + 48 * 8, 8))[0] - pe.OPTIONAL_HEADER.ImageBase
assert vt == 0x621700, hex(vt)
fired = text(0x621700, 0x4B)
assert 'call 0x62ea10' in fired and 'mov r8d, dword ptr [rbx + 0x2ac8]' in fired and 'call 0x116bfc0' in fired, fired
print('PASS: cooldown = FireInterval at a shot, counted down by its step to FLT_EPSILON, the latch the update clears; '
      'V603 slot 48 plays one Fire state for any holder')
