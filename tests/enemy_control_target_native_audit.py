"""Read-only native contract audit. Never loads EDF.dll or touches the game."""
from pathlib import Path
import re
import struct
import sys
path = Path(sys.argv[1]) if len(sys.argv) > 1 else None
if path is None or not path.is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)

import pefile
import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
pe = pefile.PE(str(path), fast_load=True)
checks = 0

def check(value, message):
    global checks
    checks += 1
    assert value, message

check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported timestamp')
check(pe.FILE_HEADER.Machine == 0x8664, 'x64 ABI')
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
root = Path(__file__).resolve().parents[1]
source = (root / 'src/enemy_control_target.cpp').read_text(encoding='utf-8-sig')
fixture = (root / 'tests/enemy_control_target_fixture.cpp').read_text(encoding='utf-8-sig')
constants = {name: int(value, 16) for name, value in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)', source)}
for name, body in re.findall(r'constexpr unsigned char (k\w+Sig)\[\]=\{([^}]+)\};', source):
    at_name = {'kWalkSig':'kWalk', 'kBodySig':'kBodyObject', 'kSetSig':'kNativeSet', 'kCtorSig':'kStreamCtor',
               'kByteSig':'kStreamByte', 'kDtorSig':'kStreamDtor', 'kOnlineSig':'kOnline'}[name]
    expected = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]+)', body))
    check(pe.get_data(constants[at_name], len(expected)) == expected, name)

def instructions(at, count):
    return list(md.disasm(pe.get_data(at, count), at))

def instruction(at, mnemonic, operands):
    ins = instructions(at, 15)[0]
    check(ins.mnemonic == mnemonic and ins.op_str == operands,
          f'{at:X}: expected {mnemonic} {operands}; got {ins.mnemonic} {ins.op_str}')
    return ins

def call(at, target):
    ins = instructions(at, 15)[0]
    check(ins.mnemonic == 'call' and ins.operands[0].type == X86_OP_IMM and ins.operands[0].imm == target,
          f'call {at:X} -> {target:X}')

# Actual GameObject -> LockPoint weak lookup. RCX is a real actor; no null check.
instruction(0x548EF0, 'movsxd', 'r9, r8d')
instruction(0x548EF3, 'cmp', 'r9, qword ptr [rcx + 0x378]')
instruction(0x548F07, 'add', 'r9, qword ptr [rcx + 0x368]')
instruction(0x548F15, 'mov', 'rcx, qword ptr [r9 + 8]')
instruction(0x548F28, 'lock inc', 'dword ptr [rcx + 0xc]')
instruction(0x548F30, 'xor', 'eax, eax')
# Native LockPoint constructor stores its real owner and eligibility fields.
instruction(0x22DB90, 'mov', 'word ptr [rcx + 0x29], 0x101')
instruction(0x22DB96, 'mov', 'qword ptr [rcx + 8], rdx')
instruction(0x22DB9A, 'mov', 'dword ptr [rcx], r8d')
# Real hostile enumeration filters directed relation=2, dead and NoTarget.
instruction(0x5E0F94, 'cmp', 'dword ptr [rax + rbp], 2')
instruction(0x5E0FB6, 'cmp', 'byte ptr [rdx + 0x2e8], 0')
instruction(0x5E0FBF, 'test', 'byte ptr [rdx + 0x380], 0x80')
instruction(0x5E0FD2, 'call', 'qword ptr [rax + 8]')
# SetTeam preserves +318 when entering the temporary -1 team.
instruction(0x54EEE0, 'mov', 'dword ptr [rbx + 0x314], edi')
instruction(0x54EEE6, 'cmp', 'edi, -1')
instruction(0x54EEE9, 'je', '0x54eef1')
instruction(0x54EEEB, 'mov', 'dword ptr [rbx + 0x318], edi')
# Physical layer11 query's body -> real SceneObject (property key zero).
call(0x108271, 0x11AD2E0)
instruction(0x10828A, 'xor', 'r8d, r8d')
instruction(0x1082B1, 'call', 'qword ptr [rax + 0xe8]')
# Native selection slot14 returns a raw actor in RAX. Its caller then captures
# weak-this +28/+30 and performs the real GameObjectBase dynamic cast.
call(0x54C506, 0x5E0F20)
instruction(0x54C50C, 'mov', 'rax, qword ptr [rsp + 0x50]')
instruction(0x54C010, 'call', 'qword ptr [rax + 0x70]')
instruction(0x54C01C, 'mov', 'rsi, qword ptr [rax + 0x30]')
instruction(0x54C025, 'mov', 'r10, qword ptr [rax + 0x28]')
call(0x54C06A, 0x12DA7AA)
# The fixture executes the exact complete setter, including the by-value input
# weak release. No RIP data or external direct call may be hidden in it.
body = re.search(r'const unsigned char kSetterCode\[\]=(.*?);', fixture, re.S).group(1)
expected = bytes(int(value, 16) for value in re.findall(r'\\x([0-9A-Fa-f]{2})', body))
check(len(expected) == 0xD4 and pe.get_data(0x364EB0, len(expected)) == expected, 'whole native setter fixture bytes')
setter = instructions(0x364EB0, 0xD4)
for ins in setter:
    check(not any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands), 'setter has no RIP dependencies')
    check(not (ins.mnemonic == 'call' and ins.operands[0].type == X86_OP_IMM), 'setter calls only native weak destructors')
instruction(0x364EDC, 'lock inc', 'dword ptr [rax + 0xc]')
instruction(0x364EE3, 'mov', 'qword ptr [rdi + 0x528], rcx')
instruction(0x364EF6, 'mov', 'qword ptr [rdi + 0x530], rdx')
instruction(0x364F23, 'lock inc', 'dword ptr [rax + 0xc]')
instruction(0x364F31, 'mov', 'qword ptr [rdi + 0x518], rbp')
instruction(0x364F38, 'mov', 'qword ptr [rdi + 0x520], rdx')
instruction(0x364F5F, 'lock xadd', 'dword ptr [rcx + 0xc], ebx')
# The sender is the exact stock Update target-change path, including per-object
# remote-owned bit0. 7748F0 is online-session state, not object ownership.
call(0x54C164, 0x7748F0)
instruction(0x54C171, 'test', 'byte ptr [rdi + 0x128], 1')
instruction(0x54C17A, 'cmp', 'byte ptr [rdi + 0x591], 0')
instruction(0x54C183, 'cmp', 'byte ptr [rdi + 0x2e8], 0')
call(0x54C196, 0x79A460)
instruction(0x54C19C, 'mov', 'dl, 1')
call(0x54C1A3, 0x12B5790)
instruction(0x54C1AB, 'mov', 'rbx, qword ptr [rax + 0x128]')
instruction(0x54C1C5, 'call', 'rbx')
instruction(0x54C1CE, 'mov', 'r8b, 1')
instruction(0x54C1DD, 'call', 'qword ptr [rax + 0x70]')
call(0x54C1EB, 0x760180)
# Stream cursor storage +5F0 fits the real stack object used by the helper.
instruction(0x12B4577, 'mov', 'qword ptr [rcx + 0x5f0], 0')
# Base serializer maps real actor identity to native network ID; receiver maps
# that ID back to a real actor and uses native current-target weak assignment.
call(0x54CAE4, 0x785050)
call(0x54CAF7, 0x774B90)
call(0x54C92F, 0x784C60)
call(0x54C9A3, 0x387550)
instruction(0x3875F2, 'mov', 'qword ptr [rbp + 0x518], rbx')
instruction(0x387600, 'mov', 'qword ptr [rbp + 0x520], rdx')
for slot, target in [(14, 0x54C4B0), (37, 0x54CAA0), (38, 0x54C900)]:
    check(struct.unpack('<Q', pe.get_data(0x17CD3F0 + slot * 8, 8))[0] == pe.OPTIONAL_HEADER.ImageBase + target,
          f'GOB virtual slot {slot}')
# Shared unit firing re-reads owner current target, but first copies its optional
# target template. Empty root current alone does not prove that template empty.
call(0x6D5734, 0x6D2640)
instruction(0x6D274F, 'lea', 'r15, [rsi + 0x330]')
call(0x6D28B6, 0x32F130)
instruction(0x32F179, 'mov', 'qword ptr [rbx + 0x30], rcx')
instruction(0x32F17D, 'mov', 'qword ptr [rbx + 0x38], rcx')
instruction(0x32F188, 'mov', 'rax, qword ptr [rdx + 0x30]')
instruction(0x32F194, 'mov', 'qword ptr [rbx + 0x38], rax')
instruction(0x32F198, 'lock inc', 'dword ptr [rax + 0xc]')
instruction(0x6D2F3B, 'mov', 'rdx, qword ptr [rbx + 0x520]')
instruction(0x6D2F69, 'mov', 'rdi, qword ptr [rbx + 0x518]')
call(0x6D2FCB, 0x548EF0)
instruction(0x6D2FDE, 'mov', 'qword ptr [rbp + 0x1e0], rcx')
instruction(0x6D2FEC, 'mov', 'qword ptr [rbp + 0x1e8], rdx')
call(0x6D068F, 0x6CEBB0)
instruction(0x6CEBBA, 'mov', 'r8, qword ptr [rdx + 8]')
instruction(0x6CEBFF, 'mov', 'rbp, qword ptr [rdx]')
check('VirtualQuery(' in source and 'bool TryClear(' in source, 'physical protection proof and recoverable cleanup API')
check('PlayerHuman(' not in source and 'CameraRay(' not in source, 'no primary-player/global camera fallback')
check('0x548EF0)(' not in source and 'AddLockPoint(' not in source, 'no fake target or generated lockpoint')
check('fullNativeId[32]' in (root / 'src/enemy_control_target.h').read_text(encoding='utf-8-sig'), 'full native ID API')
print(f'enemy target native audit: {checks} checks passed; DLL read only')
