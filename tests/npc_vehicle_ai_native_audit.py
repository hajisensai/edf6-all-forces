"""The native facts the real NPC driver's vehicle AI rests on (src/npcai.cpp DriveVehicleAi, src/npcpost.cpp
EnsureMechAiSetup, src/payload.cpp NpcPayloadSelect, src/stab.cpp Wanted); no game execution."""
from pathlib import Path
import struct
import sys
if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required'); raise SystemExit(77)
if not __debug__: raise RuntimeError("Run without -O: contract assertions must execute")
import capstone
import pefile

p = pefile.PE(sys.argv[1], fast_load=True)
assert p.FILE_HEADER.TimeDateStamp == 0x678CCB46 and p.FILE_HEADER.Machine == 0x8664
base = p.OPTIONAL_HEADER.ImageBase
m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def code(rva: int, size: int) -> dict[int, str]:
    return {i.address: f'{i.mnemonic} {i.op_str}' for i in m.disasm(p.get_data(rva, size), rva)}


def slot(vtable: int, index: int) -> int:
    return struct.unpack('<Q', p.get_data(vtable + index * 8, 8))[0] - base


# The object manager's flag setter: OR into +0x1A; a newly set bit 3 (the AI list) calls slot 6, rolled back on false.
s = code(0x118A4B0, 0x50)
assert s[0x118A4C7] == 'movzx ebx, byte ptr [rcx + 0x1a]' and s[0x118A4CE] == 'or al, byte ptr [rdx]'
assert s[0x118A4E2] == 'test al, 8' and s[0x118A4E9] == 'call qword ptr [rax + 0x30]' and s[0x118A4F0] == 'mov byte ptr [rdi + 0x1a], bl'

# Every class's slot 7 (the AI pass) and its AI action (CarBase slot 72, the mechs' slot 55).
CARBASE = {'402': 0x17D8B50, '403': 0x17D8FA0, '404': 0x17D9458, '505': 0x17DADB0, '510': 0x17DB9D8, '601': 0x17DC250,
           '603': 0x17DC620, '507': 0x17DB590, 'Car': 0x17E01B0}
MECHS = {'504': 0x17DA960, '612': 0x17DD440, 'Begaruta': 0x17DE0A8, 'BigBegaruta': 0x17DEC40}
for name, vt in CARBASE.items():
    assert slot(vt, 7) == 0x673300 and slot(vt, 72) == 0x661440, name
for name, vt in MECHS.items():
    assert slot(vt, 7) == 0x643530 and slot(vt, 55) in (0x63C1C0, 0x648F70), name

# The passes: clear every seat, then the registered action (edx 1, r8 the step) only when one is there.
car = code(0x673300, 0xB3)
assert car[0x673315].startswith('call 0x54a000') and car[0x673349] == 'call 0x62c120'
assert car[0x673358] == 'mov rcx, qword ptr [rbx + 0x2508]' and car[0x67335F] == 'test rcx, rcx'
assert car[0x673367] == 'mov byte ptr [rbx + 0x2590], 1' and car[0x673378] == 'call qword ptr [rax + 8]'
mech = code(0x643530, 0x124)
assert mech[0x6435E9] == 'call 0x62c120' and mech[0x6435F8] == 'mov rcx, qword ptr [rdi + 0x1f08]'
assert mech[0x64360C] == 'mov byte ptr [rdi + 0x1f90], 1' and mech[0x64361D] == 'call qword ptr [rax + 8]'
# Both actions take (vehicle, edx == 1).
assert code(0x661440, 8)[0x661440] == 'cmp edx, 1' and code(0x63C1C0, 8)[0x63C1C0] == 'cmp edx, 1'
# The action thunk the mechs' slot 6 registers forwards to slot 55.
t = code(0x638310, 9)
assert t[0x638310] == 'mov rax, qword ptr [rcx]' and t[0x638313] == 'jmp qword ptr [rax + 0x1b8]'

# The mechs' slot 6: ai_attack_setting into the table at +0x1FE8; the aim origin +0x2008 = the `spine` bone's record.
six = code(0x642970, 0xB50)
assert six[0x642DBC] == 'lea rbx, [rsi + 0x1fe8]' and six[0x642CA8] == 'mov qword ptr [rsi + 0x2008], rax'
assert p.get_data(0x17DE970, 12).decode('utf-16le') == 'spine\0'
assert p.get_data(0x17D9E60, 36).decode('utf-16le') == 'ai_attack_setting\0'
# The fire step: the table's rows {min, max, seat, weapon} gate the trigger; the origin is read without a null test.
fire = code(0x63AD50, 0x890)
assert fire[0x63AED2] == 'mov rax, qword ptr [r15 + 0x2008]' and fire[0x63AED9] == 'movups xmm0, xmmword ptr [rax + 0xb0]'
assert fire[0x63B274] == 'mov rbx, qword ptr [r15 + 0x1ff0]' and fire[0x63B27E] == 'mov rcx, qword ptr [r15 + 0x2000]'
assert fire[0x63B296] == 'movsxd rax, dword ptr [rbx + 8]' and fire[0x63B29F] == 'movsxd rax, dword ptr [rbx + 0xc]'
assert fire[0x63B49D] == 'cmp byte ptr [rsp + 0x31], 0'
# The table's release, which the vehicle's own destruction uses: the game's operator delete below 0x1000 bytes.
rel = code(0x646300, 0x62)
assert rel[0x646309] == 'mov rcx, qword ptr [rcx + 8]' and rel[0x646312] == 'mov rdx, qword ptr [rbx + 0x10]'
assert rel[0x646346] == 'call 0x12d85ec'

# The Barga: slot 6 builds its state machine, slot 7 clears its own block and runs the current action (+0x1648); its
# slot 4 reads that block only when 0x62D850(veh, 0) answers 2, which is the AI-list bit for seat 0.
BARGA = 0x17D98C8
assert slot(BARGA, 6) == 0x609D70 and slot(BARGA, 7) == 0x60A520 and slot(BARGA, 4) == 0x60AEC0
bp = code(0x60A520, 0xE3)
assert bp[0x60A535] == 'call 0x54a000' and bp[0x60A579] == 'mov rcx, qword ptr [rbx + 0x1648]'
assert bp[0x60A580] == 'mov byte ptr [rbx + 0x16d0], 1' and bp[0x60A598] == 'call qword ptr [rax + 8]'
assert p.get_data(0x60AEFE, 10) == bytes.fromhex('33d2488bcbe848290200')   # xor edx,edx; mov rcx,rbx; call 0x62D850
b4 = code(0x60AEC0, 0x60)
assert b4[0x60AF14] == 'cmp eax, 2' and b4[0x60AF19] == 'movups xmm0, xmmword ptr [rbx + 0x1610]'
kind = code(0x62D850, 0x37)
assert kind[0x62D850] == 'test byte ptr [rcx + 0x1a], 8' and kind[0x62D859] == 'mov eax, 1' and kind[0x62D864] == 'mov eax, 2'
assert code(0x60A110, 7)[0x60A110] == 'lea r8, [rip + 0x11cfd49]'   # the Barga's slot 6 reads the punch combos

# The CarBase seat aim's trigger: only with the range flag, no limit veto (r14b) and on target (r15b).
aim = code(0x65FF26, 0x30)
assert aim[0x65FF2B] == 'cmp byte ptr [rsp + 0x30], 0' and aim[0x65FF32] == 'test r14b, r14b'
assert aim[0x65FF37] == 'test r15b, r15b' and aim[0x65FF3C] == 'mov dword ptr [r12 + rbx + 0x2e4], 0x3f800000'
print('PASS: AI-list flag -> slot 6, the three AI passes and actions (the Barga slot 4 kind), the mech attack table and aim origin, the seat aim gate')
