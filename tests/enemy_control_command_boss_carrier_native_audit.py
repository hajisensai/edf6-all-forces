"""Read-only supported-image evidence for Boss commands and Carrier64 requests.

This script disassembles bytes only. It never loads/executes EDF.dll, starts the
game, mutates a game object, or writes game assets.
"""
from pathlib import Path
import argparse
import struct

parser = argparse.ArgumentParser()
parser.add_argument('native_dll', nargs='?', default='')
parser.add_argument('--dll', default='')
args = parser.parse_args()
path = Path(args.dll or args.native_dll)
if not (args.dll or args.native_dll) or not path.is_file():
    print('SKIP: pass supported EDF.dll path')
    raise SystemExit(77)

import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

pe = pefile.PE(str(path), fast_load=True)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(value, why):
    global checks
    assert value, why
    checks += 1


def ins(at):
    return next(md.disasm(img[at:at + 16], at, count=1))


def exact(at, mnemonic, operands):
    value = ins(at)
    check((value.mnemonic, value.op_str) == (mnemonic, operands),
          f'{at:#x}: {value.mnemonic} {value.op_str}')


def call(at, target):
    value = ins(at)
    check(value.mnemonic == 'call' and value.operands[0].type == X86_OP_IMM
          and value.operands[0].imm == target, f'{at:#x}: call {target:#x}')


def address(at, target):
    value = ins(at)
    check(value.mnemonic == 'lea' and value.operands[1].type == X86_OP_MEM
          and value.operands[1].mem.base == X86_REG_RIP
          and value.address + value.size + value.operands[1].mem.disp == target,
          f'{at:#x}: descriptor {target:#x}')


check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
      and pe.FILE_HEADER.Machine == 0x8664, 'supported x64 EDF image')
for at, wanted in (
    (0x475BE0, '48 8B C4 48 89 58 18 57 48 83 EC 30 80 B9 E8 02'),
    (0x46D5E0, '48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57'),
    (0x444770, '48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57'),
    (0x446D90, '48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 55'),
    (0x4471A0, '48 89 5C 24 08 4C 89 44 24 18 48 89 54 24 10 55'),
    (0x4F6FB0, '48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48'),
):
    check(img[at:at + 16] == bytes.fromhex(wanted), f'fingerprint {at:#x}')

# The host request uses the named native gate; stale stored names do not prove
# entry. It queries the current typed functor, then compares priority/blend.
address(0x475C0E, 0x466E7C)
call(0x475C2D, 0x46D5E0)
exact(0x475C36, 'mov', 'eax, dword ptr [rbx + 0x11b0]')
exact(0x475C3C, 'cmp', 'eax, dword ptr [rdi + 0x20]')
exact(0x475C43, 'movss', 'xmm0, dword ptr [rbx + 0x11b4]')
exact(0x475C4B, 'comiss', 'xmm0, dword ptr [rdi + 0x24]')
exact(0x475C63, 'lea', 'rcx, [rbx + 0xe50]')
exact(0x475C6F, 'mov', 'r8, rdi')
call(0x475C78, 0x446D90)
exact(0x466E7F, 'jmp', 'qword ptr [rax + 0x1e8]')
check(struct.unpack_from('<Q', img, 0x17BEC68 + 61 * 8)[0] - base == 0x447A50,
      'Boss named state override')
call(0x447A5F, 0x47A090)
call(0x447A70, 0x6C8760)
call(0x447A78, 0x444060)

# The complete descriptor is copied into 1190..11BC. Its false tail is the
# native value read by the optional target-turn path, not an unrelated AI cache.
for at, mnemonic, operands in (
    (0x47A135, 'lea', 'rsi, [rbx + 0x1190]'),
    (0x47A15A, 'mov', 'eax, dword ptr [rdi + 0x20]'),
    (0x47A15D, 'mov', 'dword ptr [rsi + 0x20], eax'),
    (0x47A160, 'mov', 'eax, dword ptr [rdi + 0x24]'),
    (0x47A163, 'mov', 'dword ptr [rsi + 0x24], eax'),
    (0x47A166, 'mov', 'eax, dword ptr [rdi + 0x28]'),
    (0x47A169, 'mov', 'dword ptr [rsi + 0x28], eax'),
    (0x47A16C, 'movzx', 'eax, byte ptr [rdi + 0x2c]'),
    (0x47A170, 'mov', 'byte ptr [rsi + 0x2c], al'),
    (0x47A1AB, 'cmp', 'byte ptr [rcx + 0x11bc], 0'),
):
    exact(at, mnemonic, operands)
call(0x47A1C0, 0x30C0F0)
call(0x47A1D9, 0x480440)

# Each state machine owns its registration pool and root pointer. These exact
# fields can be bounded/read before entering or calling the native getter.
address(0x464B28, 0x17C29A8)
exact(0x465CDB, 'mov', 'qword ptr [rbx + 0x28], rdi')
exact(0x465CDF, 'lea', 'rax, [rdi + 0xd90]')
exact(0x465CE6, 'mov', 'qword ptr [rbx + 8], rax')
address(0x4410F6, 0x17BEC50)
exact(0x442B53, 'lea', 'rdx, [r15 + 0x16f0]')
exact(0x442B5A, 'mov', 'r8, r15')
exact(0x442B5D, 'lea', 'rcx, [r15 + 0x17b0]')
call(0x442B64, 0x4446D0)
exact(0x4446D6, 'mov', 'qword ptr [rcx + 8], rdx')
exact(0x4446DD, 'mov', 'qword ptr [rcx + 0x28], r8')
for getter in (0x46D5E0, 0x444770):
    exact(getter + 0x38, 'mov', 'r14, qword ptr [r15 + 8]')
    exact(getter + 0x3C, 'mov', 'rdi, qword ptr [r14 + 0x10]')
    exact(getter + 0x40, 'mov', 'rbp, qword ptr [r14 + 0x20]')
    exact(getter + 0x48, 'add', 'rbp, rdi')

# Replay primitives perform native exit2 and entry0, including changing the
# same callback again. The latter increments +20 rather than being a no-op.
for exit_call, same_test, reset, repeat, active, entry in (
    (0x446E78, 0x446E83, 0x446EDA, 0x446EE3, 0x447114, 0x447135),
    (0x447278, 0x447283, 0x4472DA, 0x4472E3, 0x4474A6, 0x4474C7),
):
    call(exit_call, 0x30C080)
    exact(same_test, 'cmp', 'qword ptr [rsi + 0x10], rbx')
    exact(reset, 'mov', 'dword ptr [rsi + 0x20], 0')
    exact(repeat, 'inc', 'dword ptr [rsi + 0x20]')
    exact(active, 'mov', 'qword ptr [rbx + 0x28], rsi')
    exact(entry, 'call', 'qword ptr [rax + 8]')
exact(0x30C0A6, 'lea', 'edx, [r9 + 2]')
exact(0x30C0AA, 'call', 'qword ptr [rax + 8]')

# ObjectAction4/5/6 have zero entryData. The local OnMessage return comes from
# the base handler, so acceptance must inspect the selected hand state.
for code, site, descriptor_at, callback in (
    (4, 0x444CAA, 0x444CC7, 0x447DA0),
    (5, 0x444CDA, 0x444CF7, 0x447F00),
    (6, 0x444D07, 0x444D1C, 0x447FD0),
):
    check(struct.unpack_from('<I', img, 0x444DC4 + code * 4)[0] == site,
          f'ObjectAction{code} dispatch')
    address(descriptor_at, callback)
exact(0x444D70, 'xor', 'r8d, r8d')
call(0x444D7C, 0x4471A0)
call(0x444D89, 0x46FE80)
exact(0x447DB1, 'mov', 'rax, qword ptr [rbx + 0x3e8]')
exact(0x447DD8, 'mov', 'qword ptr [rbx + 0x3e8], rax')
call(0x447DEB, 0x7748F0)
exact(0x447DF4, 'mov', 'eax, dword ptr [rbx + 0x26c8]')
# Only the native nonnegative RNG range is safe for this signed abs/mod3
# animation-table index; INT_MIN would sign-extend the remainder -2.
exact(0x447DFA, 'cdq', '')
exact(0x447E37, 'movsxd', 'r8, edi')
exact(0x447E3D, 'mov', 'r8, qword ptr [rax + r8*8]')
exact(0x445E51, 'imul', 'rax, rax, 0x7fffffff')
exact(0x445EDD, 'imul', 'rax, rax, 0x7fffffff')

exact(0x447E3D, 'mov', 'r8, qword ptr [rax + r8*8]')
call(0x447E41, 0x116C2D0)
call(0x447EEB, 0x444060) if ins(0x447EEB).mnemonic == 'call' else exact(0x447EEB, 'jmp', '0x444060')

# Carrier64 is an accepted request flag, not an immediate hatch transition.
# Native animation is independently driven and replicated by stock event4.
exact(0x4F6FD2, 'sub', 'eax, 0x20100')
exact(0x4F6FDD, 'sub', 'eax, 1')
exact(0x4F6FE6, 'sub', 'eax, 2')
exact(0x4F6FEB, 'cmp', 'eax, 0xffdff0a')
check(0x20100 + 1 + 2 + 0xFFDFF0A == 0x1000000D, 'Carrier native message ID arithmetic')
exact(0x4F6FF2, 'cmp', 'dword ptr [r8], 0x40')
exact(0x4F6FF8, 'mov', 'dword ptr [rcx + 0xd88], 1')
exact(0x4F7002, 'jmp', '0x4f7165')
exact(0x4F7165, 'mov', 'al, 1')

# The separate stock main-state ordinal receiver can re-enter the same state.
# Its payload reader runs first, so suppressing that final setter preserves
# the packet's typed pose consumption. Blocking the earlier apply predicate
# would skip those typed writes. The named and hand entries do not self-send
# stock ordinal9: its three direct senders belong to independent Boss states.
text_section = next(s for s in pe.sections if s.Name.startswith(b'.text'))
text_start = text_section.VirtualAddress
text_end = text_start + text_section.Misc_VirtualSize


def callers(target):
    found = set()
    for opcode in (b'\xe8', b'\xe9'):
        at = img.find(opcode, text_start, text_end)
        while at >= 0:
            if at + 5 <= text_end and at + 5 + struct.unpack_from('<i', img, at + 1)[0] == target:
                found.add(at)
            at = img.find(opcode, at + 1, text_end)
    return found


check(callers(0x473D20) == {0x447993, 0x447A30, 0x447BB4},
      'all direct stock ordinal senders are independent Boss state transitions')
check(callers(0x4775E0) == {0x476530}, 'single stock ordinal receiver setter')
exact(0x473D78, 'lea', 'rcx, [rdi + 0xe50]')
call(0x473D7F, 0x46A520)
exact(0x473D87, 'mov', 'dl, 9')
call(0x473D8E, 0x12B5790)
exact(0x4764FE, 'mov', 'r10, qword ptr [rcx + 0x260]')
exact(0x476517, 'call', 'r10')
exact(0x476522, 'xor', 'r8d, r8d')
exact(0x476529, 'lea', 'rcx, [rsi + 0xd30]')
call(0x476530, 0x4775E0)
exact(0x477603, 'mov', 'r14, r8')
exact(0x477609, 'mov', 'rax, qword ptr [rcx + 8]')
exact(0x47760D, 'movsx', 'rcx, dl')
exact(0x477614, 'mov', 'rbx, qword ptr [rax + 0x10]')
exact(0x477618, 'mov', 'rdi, qword ptr [rbx + rcx*8 + 8]')
exact(0x47762B, 'mov', 'rbx, qword ptr [rbx + rcx*8]')
call(0x47764B, 0x30C080)
exact(0x4776A7, 'inc', 'dword ptr [rsi + 0x20]')
exact(0x47770B, 'mov', 'r9, r14')
exact(0x477711, 'xor', 'edx, edx')
exact(0x477716, 'call', 'qword ptr [rax + 8]')
exact(0x46A575, 'mov', 'rax, qword ptr [rsi + 0x10]')
exact(0x46A579, 'mov', 'rcx, qword ptr [rsi + 0x20]')
exact(0x46A58E, 'sub', 'rdi, rax')
exact(0x46A591, 'sar', 'rdi, 4')

# Meteor's authoritative send precedes its old local aim callsite. A shared
# direction patch belongs at 443A93, where both the following serializer and
# local factory read the same original stack float4. The guest consumes the
# transmitted seed/origin/direction and calls the same native meteor factory.
check(img[0x443A93:0x443AA1] == bytes.fromhex('48 8D 8E 20 01 00 00 E8 51 0E 33 00 84 C0'),
      'complete 14-byte meteor pre-serialization gate, embedded call must be relocated')
exact(0x4438AB, 'push', 'rbp')
exact(0x4438AC, 'push', 'rdi')
exact(0x4438AD, 'push', 'r14')
exact(0x4438B6, 'sub', 'rsp, 0x660')
exact(0x4438D2, 'mov', 'rsi, rcx')
exact(0x4438DC, 'movaps', 'xmmword ptr [rsp + 0x30], xmm0')
exact(0x443A76, 'movss', 'dword ptr [rsp + 0x20], xmm2')
exact(0x443A93, 'lea', 'rcx, [rsi + 0x120]')
call(0x443A9A, 0x7748F0)
exact(0x443A9F, 'test', 'al, al')
exact(0x443AA1, 'je', '0x443b51')
exact(0x443AA7, 'test', 'byte ptr [rsi + 0x128], 1')
exact(0x443AAE, 'jne', '0x443b63')
exact(0x443ACA, 'mov', 'dl, 0x14')
exact(0x443AD6, 'mov', 'edx, dword ptr [rsi + 0x2714]')
exact(0x443AE6, 'mov', 'rdx, qword ptr [rsi + 0x3e8]')
exact(0x443AF7, 'mov', 'r8d, 0xc')
exact(0x443AFD, 'lea', 'rdx, [rsp + 0x30]')
call(0x443B07, 0x12B5200)
exact(0x443B0C, 'mov', 'r8d, 0xc')
exact(0x443B12, 'lea', 'rdx, [rsp + 0x20]')
call(0x443B1C, 0x12B5200)
exact(0x443B37, 'call', 'qword ptr [rax + 0x70]')
exact(0x443B51, 'lea', 'r8, [rsp + 0x20]')
exact(0x443B56, 'lea', 'rdx, [rsp + 0x30]')
call(0x443B5E, 0x448230)
call(0x446450, 0x12B4B90)
exact(0x446455, 'mov', 'dword ptr [rdi + 0x25f4], eax')
call(0x44645E, 0x12B4BA0)
exact(0x446463, 'mov', 'qword ptr [rdi + 0x2c8], rax')
exact(0x446444, 'mov', 'qword ptr [rsp + 0x68], 0xc')
exact(0x44646A, 'lea', 'r8, [rsp + 0x68]')
exact(0x44646F, 'lea', 'rdx, [rsp + 0x40]')
call(0x446477, 0x12B49D0)
exact(0x446481, 'lea', 'rdx, [rsp + 0x30]')
call(0x446489, 0x12B49D0)
exact(0x44648E, 'lea', 'rcx, [rdi - 0x120]')
exact(0x446495, 'lea', 'r8, [rsp + 0x30]')
exact(0x44649A, 'lea', 'rdx, [rsp + 0x40]')
call(0x44649F, 0x448230)

print(f'Boss command and Carrier request native audit: {checks} checks passed')
