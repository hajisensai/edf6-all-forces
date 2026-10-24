"""Read-only supported-native audit for final common-Unit guide weak replacement.

Never loads or executes EDF.dll, starts a game, or writes the game directory.
The final boundary is after mutable callbacks and before actual projectile creation.
"""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
args = parser.parse_args()
game = Path(args.game) if args.game else None
dll = Path(args.dll) if args.dll else game / 'EDF.dll' if game else None
if dll is None or not dll.is_file():
    print('SKIP: pass --dll or --game containing the supported local EDF.dll')
    raise SystemExit(77)

import pefile
import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RBP

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/enemy_control_unit_guide.cpp').read_text(encoding='utf-8-sig')
pe = pefile.PE(str(dll), fast_load=True)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def code(at, length):
    return pe.get_data(at, length)


def instruction(at):
    return next(md.disasm(code(at, 15), at, count=1))


def exact(at, mnemonic, operands):
    ins = instruction(at)
    check((ins.mnemonic, ins.op_str) == (mnemonic, operands),
          f'{at:X}: expected {mnemonic} {operands}; got {ins.mnemonic} {ins.op_str}')
    return ins


def branch(at, target, mnemonic='call'):
    ins = instruction(at)
    check(ins.mnemonic == mnemonic and ins.operands[0].type == X86_OP_IMM and
          ins.operands[0].imm == target, f'{at:X}: expected {mnemonic} {target:X}')


def raw(at, expected, label):
    check(code(at, len(expected)) == expected, label)


check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'supported Steam timestamp')
check(pe.FILE_HEADER.Machine == 0x8664, 'x64 ABI')

# The source's immutable patch bytes must describe all complete instructions.
original = bytes.fromhex('4C 8D 8D 80 01 00 00 4C 8B 86 F0 02 00 00')
match = re.search(r'\bkOriginal(?:14)?\s*\[[^\]]*\]\s*=\s*\{([^}]+)\}', source)
check(match is not None, 'production complete final patch fingerprint exists')
source_original = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]+)', match.group(1)))
check(source_original == original, 'production patch preserves the exact complete final 14 bytes')
raw(0x6D3301, original, 'supported final Unit guide patch bytes')

# Check declared production RVA/byte fingerprints without depending on their names.
fingerprints = {}
for address, body in re.findall(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\{([^}]+)\}\s*\}', source):
    values = re.findall(r'0x([0-9A-Fa-f]+)', body)
    if not values:
        continue
    address = int(address, 16)
    expected = bytes(int(value, 16) for value in values)
    raw(address, expected, f'production native fingerprint {address:X}')
    fingerprints[address] = expected
move_arrays = re.findall(r'\b(k\w*Move\w*)\s*\[[^\]]*\]\s*=\s*\{([^}]+)\}', source)
for name, body in move_arrays:
    values = re.findall(r'0x([0-9A-Fa-f]+)', body)
    if values:
        expected = bytes(int(value, 16) for value in values)
        raw(0x58EB40, expected, f'production native move fingerprint {name}')
        fingerprints[0x58EB40] = expected
check(0x58EB40 in fingerprints, 'production validates native owned-weak move entrypoint')

# No direct control-flow edge may enter the displaced instruction interiors.
body = list(md.disasm(code(0x6D2640, 0x6D3A3F - 0x6D2640), 0x6D2640))
check(len(body) == 1244 and body[-1].address == 0x6D3A3E and body[-1].mnemonic == 'ret',
      'complete supported common Unit creation body is decoded')
entry_edges = []
for ins in body:
    if not (ins.mnemonic.startswith('j') or ins.mnemonic == 'call'):
        continue
    if ins.operands[0].type == X86_OP_IMM:
        target = ins.operands[0].imm
        check(not 0x6D3302 <= target <= 0x6D330E, f'{ins.address:X}: no displaced-interior entry')
        if target == 0x6D3301:
            entry_edges.append((ins.address, ins.mnemonic))
    else:
        check(ins.mnemonic == 'call', f'{ins.address:X}: no indirect jump into patch interior')
check(entry_edges == [(0x6D32C4, 'je')], 'sole branch enters the exact final-boundary start')

# This is a plain native Unit, not an actor with its own fabricated weak-this.
exact(0x6D0DC0, 'xor', 'r14d, r14d')
exact(0x6D0DC3, 'mov', 'qword ptr [rcx], r14')
exact(0x6D1015, 'mov', 'rdx, qword ptr [r15 + 0x30]')
exact(0x6D1035, 'mov', 'r10, qword ptr [r15 + 0x28]')
branch(0x6D1054, 0x12DA7AA)  # true owner SceneObject -> GameObject dynamic cast
exact(0x6D1071, 'lock inc', 'dword ptr [rbx + 0xc]')
exact(0x6D107B, 'mov', 'qword ptr [rsi + 0x2b0], rcx')
exact(0x6D1089, 'mov', 'qword ptr [rsi + 0x2b8], rdx')
exact(0x6D1530, 'lock xadd', 'dword ptr [rcx + 0xc], eax')
exact(0x6D153D, 'call', 'qword ptr [rax + 8]')

# The factory frame locks that exact Unit owner weak before creating anything.
exact(0x6D26A6, 'mov', 'rsi, rcx')
exact(0x6D26A9, 'xor', 'r12d, r12d')
exact(0x6D26B9, 'mov', 'rdx, qword ptr [rcx + 0x2b8]')
exact(0x6D26D3, 'lock cmpxchg', 'dword ptr [rdx + 8], ecx')
exact(0x6D26E0, 'mov', 'rax, qword ptr [rsi + 0x2b0]')
exact(0x6D26E7, 'mov', 'qword ptr [rbp - 0x58], rax')
exact(0x6D26EB, 'mov', 'rax, qword ptr [rsi + 0x2b8]')
exact(0x6D26F2, 'mov', 'qword ptr [rbp - 0x50], rax')
branch(0x6D2701, 0x6D39D9, 'je')
exact(0x6D39D9, 'mov', 'rcx, qword ptr [rbp - 0x50]')
branch(0x6D39E2, 0x52AF0)

# BulletCreateInfo+30 InitParam is copied from unit+330, including template guide.
exact(0x6D274F, 'lea', 'r15, [rsi + 0x330]')
exact(0x6D28AC, 'mov', 'rdx, r15')
exact(0x6D28AF, 'lea', 'rcx, [rbp + 0x1b0]')
branch(0x6D28B6, 0x32F130)
exact(0x32F179, 'mov', 'qword ptr [rbx + 0x30], rcx')
exact(0x32F17D, 'mov', 'qword ptr [rbx + 0x38], rcx')
exact(0x32F181, 'cmp', 'qword ptr [rdx + 0x38], 0')
exact(0x32F188, 'mov', 'rax, qword ptr [rdx + 0x30]')
exact(0x32F18C, 'mov', 'qword ptr [rbx + 0x30], rax')
exact(0x32F190, 'mov', 'rax, qword ptr [rdx + 0x38]')
exact(0x32F194, 'mov', 'qword ptr [rbx + 0x38], rax')
exact(0x32F198, 'lock inc', 'dword ptr [rax + 0xc]')

# Original current-target branches may leave that copied guide untouched.
exact(0x6D2F37, 'mov', 'rbx, qword ptr [rbp - 0x58]')
exact(0x6D2F3B, 'mov', 'rdx, qword ptr [rbx + 0x520]')
for at, mnemonic in [(0x6D2F45, 'je'), (0x6D2F50, 'je'), (0x6D2F64, 'jmp'),
                     (0x6D2FBB, 'jbe'), (0x6D3014, 'je'), (0x6D3023, 'jne'),
                     (0x6D302D, 'je'), (0x6D303C, 'jne'), (0x6D3053, 'jne')]:
    branch(at, 0x6D305E, mnemonic)
exact(0x6D2F59, 'lock cmpxchg', 'dword ptr [rdx + 8], ecx')
exact(0x6D2F69, 'mov', 'rdi, qword ptr [rbx + 0x518]')
exact(0x6D2F70, 'mov', 'rbx, qword ptr [rbx + 0x520]')
exact(0x6D2F8A, 'lock xadd', 'dword ptr [rbx + 8], eax')
exact(0x6D2FB4, 'cmp', 'dword ptr [rdi + 0x378], 0')
exact(0x6D2FC1, 'xor', 'r8d, r8d')  # native ignores the player's selected ordinal
exact(0x6D2FC4, 'lea', 'rdx, [rbp + 0x50]')
exact(0x6D2FC8, 'mov', 'rcx, rdi')
branch(0x6D2FCB, 0x548EF0)
exact(0x548EF0, 'movsxd', 'r9, r8d')
exact(0x548EF3, 'cmp', 'r9, qword ptr [rcx + 0x378]')
exact(0x548F07, 'add', 'r9, qword ptr [rcx + 0x368]')
exact(0x548F15, 'mov', 'rcx, qword ptr [r9 + 8]')
exact(0x548F28, 'lock inc', 'dword ptr [rcx + 0xc]')

# Native move consumes the returned owned weak, releases old template guide,
# and later destroys the final per-shot guide exactly once.
exact(0x6D2FD7, 'mov', 'qword ptr [rax], r12')
exact(0x6D2FDA, 'mov', 'qword ptr [rax + 8], r12')
exact(0x6D2FDE, 'mov', 'qword ptr [rbp + 0x1e0], rcx')
exact(0x6D2FE5, 'mov', 'rcx, qword ptr [rbp + 0x1e8]')
exact(0x6D2FEC, 'mov', 'qword ptr [rbp + 0x1e8], rdx')
exact(0x6D2FFD, 'lock xadd', 'dword ptr [rcx + 0xc], eax')
exact(0x6D300A, 'call', 'qword ptr [rax + 8]')
exact(0x6D300D, 'mov', 'rcx, qword ptr [rbp + 0x58]')
exact(0x6D301B, 'lock xadd', 'dword ptr [rcx + 0xc], eax')
exact(0x6D3971, 'mov', 'rcx, qword ptr [rbp + 0x1e8]')
exact(0x6D3982, 'lock xadd', 'dword ptr [rcx + 0xc], eax')
exact(0x6D398F, 'call', 'qword ptr [rax + 8]')
move = bytes.fromhex(
    '40 53 48 83 EC 20 48 8B 02 48 8B D9 4C 8B 42 08 33 C9 '
    '48 89 0A 48 89 4A 08 48 8B 4B 08 48 89 03 4C 89 43 08 '
    '48 85 C9 74 15 B8 FF FF FF FF F0 0F C1 41 0C 83 F8 01 '
    '75 06 48 8B 01 FF 50 08 48 8B C3 48 83 C4 20 5B C3')
raw(0x58EB40, move, 'complete native owned-weak move, no shallow overwrite')
exact(0x58EB52, 'mov', 'qword ptr [rdx], rcx')
exact(0x58EB55, 'mov', 'qword ptr [rdx + 8], rcx')
exact(0x58EB59, 'mov', 'rcx, qword ptr [rbx + 8]')
exact(0x58EB5D, 'mov', 'qword ptr [rbx], rax')
exact(0x58EB60, 'mov', 'qword ptr [rbx + 8], r8')
exact(0x58EB6E, 'lock xadd', 'dword ptr [rcx + 0xc], eax')
exact(0x58EB7B, 'call', 'qword ptr [rax + 8]')

# Both callbacks can mutate the descriptor; the hook follows both, not just the
# current-target join. No instruction between it and factory can rewrite guide.
exact(0x6D2F1B, 'mov', 'rcx, qword ptr [rsi + 0x5d0]')
exact(0x6D2F2A, 'lea', 'r8, [rbp + 0x180]')
exact(0x6D2F34, 'call', 'qword ptr [rax + 0x10]')
exact(0x6D32BA, 'mov', 'rbx, qword ptr [rsi + 0x610]')
exact(0x6D32D5, 'lea', 'rax, [rbp + 0x180]')
exact(0x6D32DC, 'mov', 'qword ptr [rbp + 0x30], rax')
exact(0x6D32FE, 'call', 'qword ptr [rax + 0x10]')
exact(0x6D3301, 'lea', 'r9, [rbp + 0x180]')
exact(0x6D3308, 'mov', 'r8, qword ptr [rsi + 0x2f0]')
exact(0x6D330F, 'lea', 'rdx, [rbp + 0xf0]')
exact(0x6D3316, 'mov', 'rcx, qword ptr [rip + 0x19df63b]')
branch(0x6D331D, 0x1194280)
final = [ins.address for ins in body if 0x6D3301 <= ins.address <= 0x6D331D]
check(final == [0x6D3301, 0x6D3308, 0x6D330F, 0x6D3316, 0x6D331D],
      'final descriptor handoff has no remaining mutable callback or weak assignment')
for ins in body:
    if not 0x6D3301 <= ins.address < 0x6D331D:
        continue
    check(not any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RBP and
                  op.mem.disp in (0x1E0, 0x1E8) for op in ins.operands),
          f'{ins.address:X}: no final guide overwrite after the boundary')
exact(0x119429A, 'mov', 'qword ptr [r9 + 8], rdx')
exact(0x11942C0, 'call', 'qword ptr [rax + 0x10]')

# Existing-beam refresh is a different native operation, with no new factory.
exact(0x6D31BA, 'mov', 'rax, qword ptr [rcx]')
exact(0x6D31BD, 'call', 'qword ptr [rax + 0x60]')
branch(0x6D31C0, 0x6D36F6, 'jmp')

# These independently audited consumers are not common Unit factories. Their
# own aim/guide consumers must retain family-specific readiness requirements.
exact(0x3E7413, 'lea', 'r9, [rbp + 0x10]')
exact(0x3E7417, 'mov', 'r8, qword ptr [rbx + 0x130]')
branch(0x3E742A, 0x1194280)  # Squid custom component
exact(0x52C8AF, 'lea', 'r9, [rsi + 0x700]')
exact(0x52C8B6, 'mov', 'r8, qword ptr [rsi + 0x6f0]')
branch(0x52C9B1, 0x1194280)  # UfoSmall507 embedded descriptor
branch(0x535AEF, 0x6CFC60)   # UfoSmall515 individual real gun holder
branch(0x6CFE2A, 0x6D05C0)
exact(0x6D05F3, 'mov', 'r15, rdx')
exact(0x6D05F6, 'mov', 'rdi, rcx')

print(f'enemy control final Unit guide native audit: {checks} checks passed; DLL read only')
