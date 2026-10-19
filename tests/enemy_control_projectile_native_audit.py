"""Read-only audit of the supported native projectile descriptor/replay contract.

Decodes EDF.dll as data using pefile/Capstone. Never loads or executes the DLL,
runs the game, installs a plugin, or writes a game file. Native code evidence
and synthetic fixture tests do not establish real-game/two-machine acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dll', default='', help='Supported local EDF.dll')
parser.add_argument('--game', default='', help='Local game directory containing EDF.dll')
args = parser.parse_args()
dll = Path(args.dll) if args.dll else Path(args.game) / 'EDF.dll' if args.game else None
if dll is None or not dll.is_file():
    print('SKIP: supported local EDF.dll absent (pass --dll or --game)')
    raise SystemExit(77)

import capstone
import pefile
p = pefile.PE(str(dll), fast_load=True)
b = p.get_memory_mapped_image()
d = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
d.detail = True
n = 0
checked_instructions = set()
checked_pointers = set()


def check(value, evidence):
    global n
    if not value:
        raise AssertionError(evidence)
    n += 1


def instruction(at):
    decoded = list(d.disasm(b[at:at + 16], at, count=1))
    if not decoded:
        raise AssertionError(('instruction absent', hex(at)))
    return decoded[0]


def ins(at, mnemonic, operands):
    evidence = (at, mnemonic, operands)
    if evidence in checked_instructions:
        return
    checked_instructions.add(evidence)
    x = instruction(at)
    check((x.mnemonic, x.op_str) == (mnemonic, operands),
          (hex(at), x.mnemonic, x.op_str, mnemonic, operands))


def address(at, target):
    x = instruction(at)
    check(x.mnemonic == 'lea' and len(x.operands) == 2
          and x.operands[1].type == capstone.x86.X86_OP_MEM
          and x.operands[1].mem.base == capstone.x86.X86_REG_RIP
          and x.address + x.size + x.operands[1].mem.disp == target,
          ('RIP-relative address', hex(at), x.op_str, hex(target)))


def pointer(at, target):
    if (at, target) in checked_pointers:
        return
    checked_pointers.add((at, target))
    check(struct.unpack_from('<Q', b, at)[0] - p.OPTIONAL_HEADER.ImageBase == target,
          ('native pointer', hex(at), hex(target)))


check(p.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll timestamp')
source = (ROOT / 'src/enemy_control_projectile_native.cpp').read_text(encoding='utf8')
header = (ROOT / 'src/enemy_control_projectile_native.h').read_text(encoding='utf8')
fingerprints = re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}', source)
check(len(fingerprints) == 7, 'production native admission fingerprints changed')
for at, raw in fingerprints:
    value = bytes(int(v.strip(), 16) for v in raw.split(','))
    check(len(value) == 16 and p.get_data(int(at, 16), 16) == value,
          ('production admission fingerprint', at))

# Original GB2 factory/copy/destructor/weak/refresh/post-create contracts.
for at,m,o in [
 (0x6D3301,'lea','r9, [rbp + 0x180]'),(0x6D3308,'mov','r8, qword ptr [rsi + 0x2f0]'),
 (0x6D330F,'lea','rdx, [rbp + 0xf0]'),(0x6D331D,'call','0x1194280'),
 (0x119429A,'mov','qword ptr [r9 + 8], rdx'),(0x11942A0,'mov','qword ptr [r9 + 0x10], rdi'),
 (0x11942A4,'mov','qword ptr [r9 + 0x18], rdi'),(0x11942AD,'mov','qword ptr [r9 + 0x20], rax'),
 (0x11942C0,'call','qword ptr [rax + 0x10]'),(0x11943B6,'test','byte ptr [rcx + 0x18], 4'),
 (0x11943BA,'cmove','rdi, rcx'),(0x11943F4,'mov','rax, rdi'),
 (0x6D0F54,'lea','rcx, [rsi + 0x300]'),(0x6D0F5B,'call','0x1001d0'),
 (0x32EF60,'call','0x32f130'),(0x32EF98,'lea','ecx, [rax + 1]'),
 (0x32EF9B,'lock cmpxchg','dword ptr [r8 + 4], ecx'),(0x32EFAD,'lock cmpxchg','dword ptr [r8], ecx'),
 (0x32EFC9,'mov','rcx, qword ptr [rax + 8]'),(0x32F073,'mov','dword ptr [rbx + 0x1a0], eax'),
 (0x32F07F,'mov','dword ptr [rbx + 0x1a4], eax'),(0x32F08B,'mov','dword ptr [rbx + 0x1a8], eax'),
 (0x32F0C4,'mov','r8, qword ptr [r8 + rdx*8]'),(0x32F112,'lock inc','dword ptr [rax + 0xc]'),
 (0x100CFD,'call','0x1006b0'),(0x100D09,'call','0x1008a0'),(0x100D12,'call','0x100d40'),
 (0x58EB52,'mov','qword ptr [rdx], rcx'),(0x58EB55,'mov','qword ptr [rdx + 8], rcx'),
 (0x58EB5D,'mov','qword ptr [rbx], rax'),(0x58EB60,'mov','qword ptr [rbx + 8], r8'),
 (0x58EB6E,'lock xadd','dword ptr [rcx + 0xc], eax'),
 (0x6D2FC1,'xor','r8d, r8d'),(0x6D2FCB,'call','0x548ef0'),
 (0x6D2FDE,'mov','qword ptr [rbp + 0x1e0], rcx'),(0x6D2FEC,'mov','qword ptr [rbp + 0x1e8], rdx'),
 (0x6D32B3,'mov','qword ptr [rbp + 0x1f0], r14'),(0x6D32FE,'call','qword ptr [rax + 0x10]'),
 (0x6D31BA,'mov','rax, qword ptr [rcx]'),(0x6D31BD,'call','qword ptr [rax + 0x60]'),
 (0x6D31C0,'jmp','0x6d36f6'),(0x22F390,'cmp','dword ptr [rcx + 0xc30], 1'),
 (0x22F3A8,'bts','edx, 0xd'),(0x22F3AC,'mov','dword ptr [rcx + 0xc34], edx'),
 (0x6D34BE,'cmp','dword ptr [rbx + 0xc30], 1'),(0x6D357B,'call','0x12d85b0'),
 (0x6D35D5,'call','qword ptr [rax + 0x10]'),(0x6D3621,'call','0x784510'),
 (0x6D3630,'mov','r9d, dword ptr [r14]'),(0x6D3637,'mov','dword ptr [r14], eax'),
 (0x6D3641,'mov','r8d, dword ptr [r14 + 4]'),(0x6D3645,'call','0x7639b0'),
 (0x7639E7,'cmp','dword ptr [rdx + 8], -1'),(0x7639FC,'shl','r9d, 8'),
 (0x763A00,'or','r9d, r8d'),(0x763A03,'mov','dword ptr [rdx + 8], r9d'),
 (0x6D3ED0,'call','0x6d3d80'),(0x6D3E31,'call','0x2edde0'),
 (0x6D40C1,'call','0x118a1b0')]:ins(at,m,o)
for vt,fn in [(0x17A0FC0,0x22F390),(0x179E368,0x22F390)]:
 check(struct.unpack_from('<Q',b,vt+0x60)[0]-p.OPTIONAL_HEADER.ImageBase==fn,('refresh slot',hex(vt)))
for i,fn in enumerate([0x22FB60,0x22FB50,0x22FB70]):
 check(struct.unpack_from('<Q',b,0x179E1C0+i*8)[0]-p.OPTIONAL_HEADER.ImageBase==fn,('variant copier',i))
check(struct.unpack_from('<Q',b,0x1769028)[0]-p.OPTIONAL_HEADER.ImageBase==0x101510,'descriptor deleting destructor')

# The actual stock Large projectile factory entries, not unrelated bullet classes.
for table, name_getter, create, name in (
    (0x17A0F80, 0x25E3C0, 0x25E3D0, 'PulseBullet01'),
    (0x179E2D0, 0x2389E0, 0x2389F0, 'EfsBullet'),
):
    pointer(table + 8, name_getter)
    pointer(table + 0x10, create)
    x = instruction(name_getter)
    name_at = x.address + x.size + x.operands[1].mem.disp
    check(b[name_at:name_at + (len(name) + 1) * 2].decode('utf-16le') == name + '\0',
          ('actual native factory name', name))
for at, mnemonic, operands in [
    (0x25E3D6, 'mov', 'rbx, rdx'), (0x25E3D9, 'mov', 'ecx, 0x1030'),
    (0x25E3F3, 'call', '0x25e690'), (0x25E6D6, 'mov', 'r8b, 1'),
    (0x25E6D9, 'call', '0x22e9c0'),
    (0x2389F6, 'mov', 'rbx, rdx'), (0x2389F9, 'mov', 'ecx, 0x1010'),
    (0x238A13, 'call', '0x239210'), (0x239267, 'mov', 'r8b, 1'),
    (0x23926A, 'call', '0x22e9c0'),
    (0x22E9D9, 'mov', 'r14, rdx'), (0x22E9DF, 'call', '0x1189c00'),
    (0x1189CAC, 'mov', 'rax, qword ptr [rbp + 8]'),
    (0x1189CB0, 'movaps', 'xmm0, xmmword ptr [rax]'),
    (0x1189CB7, 'movaps', 'xmm1, xmmword ptr [rax + 0x10]'),
    (0x1189CBF, 'movaps', 'xmm0, xmmword ptr [rax + 0x20]'),
    (0x1189CCA, 'movaps', 'xmm1, xmmword ptr [rax + 0x30]'),
    (0x1189CD5, 'mov', 'rsi, qword ptr [rbp + 0x10]'),
    (0x1189D50, 'mov', 'rcx, qword ptr [rbp + 0x18]'),
    (0x1189E31, 'mov', 'rax, qword ptr [rbp + 0x20]'),
    (0x22EA36, 'lea', 'r8, [r14 + 0x30]'),
    (0x22EA45, 'call', '0x231cc0'), (0x231CFC, 'call', '0x2307f0'),
    (0x22EA5F, 'mov', 'eax, dword ptr [r14 + 0x1a0]'),
    (0x22EA66, 'mov', 'qword ptr [rsi + 0x130], rax'),
    (0x22EADE, 'mov', 'rdx, qword ptr [r14 + 0x40]'),
    (0x22EAF3, 'lock cmpxchg', 'dword ptr [rdx + 8], ecx'),
    (0x22EB5E, 'mov', 'rcx, qword ptr [r14 + 0x38]'),
    (0x22EB10, 'call', '0x118adf0'),
    (0x25E8E8, 'add', 'r15, 0xe8'),
    (0x23B754, 'cmp', 'qword ptr [rdi + 0x188], 0'),
    (0x23B778, 'lea', 'rax, [rdi + 0x190]'),
    (0x23B77F, 'lea', 'r9, [rdi + 0x180]'),
    (0x23B7B4, 'call', '0x22eb80'),
]:
    ins(at, mnemonic, operands)
address(0x32EF66, 0x1769028)
address(0x1002EE, 0x1769028)

# Every wire scalar has an independently proven native field width. In particular
# no padding, weak pair, variant, pointer, or process-local resource ID is sent.
def source_array(name):
    match = re.search(r'\b' + name + r'\[\]\s*=\s*\{([^}]+)\}', header)
    check(bool(match), ('scalar array missing', name))
    return [int(v.strip(), 16) for v in match.group(1).split(',')]

scalar32 = [0x30, 0x50, 0x54, 0x58, 0x5C, 0x90, 0x94, 0x98, 0x9C,
            0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xC0, 0xC4, 0xC8, 0xCC,
            0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0x108, 0x10C, 0x110,
            0x114, 0x118, 0x170, 0x174]
scalar8 = [0x28, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
           0xBB, 0xBC, 0x11C]
check(source_array('kProjectileScalar32') == scalar32, 'u32 wire whitelist differs from native POD evidence')
check(source_array('kProjectileScalar8') == scalar8, 'u8 wire whitelist differs from native POD evidence')
check(len(set(scalar32)) == 30 and len(set(scalar8)) == 11, 'wire scalar duplication')
for at, mnemonic, operands in [
    (0x32EF51, 'movzx', 'eax, byte ptr [rdx + 0x28]'),
    (0x32EF55, 'mov', 'byte ptr [rcx + 0x28], al'),
    (0x32F145, 'movss', 'xmm0, dword ptr [rdx]'),
    (0x32F171, 'movups', 'xmm0, xmmword ptr [rdx + 0x20]'),
    (0x32F175, 'movups', 'xmmword ptr [rbx + 0x20], xmm0'),
]:
    ins(at, mnemonic, operands)
# POD subfields are selected from decoded native load widths below, rather than
# assuming compiler-selected load registers or instruction scheduling.
copy = list(d.disasm(b[0x32F130:0x32F3B0], 0x32F130))
for offset in scalar32[:23]:
    relative = offset - 0x30
    found = any(x.mnemonic in ('mov', 'movss', 'movups') and len(x.operands) == 2
                and x.operands[1].type == capstone.x86.X86_OP_MEM
                and x.operands[1].mem.base == capstone.x86.X86_REG_RDX
                and x.operands[1].mem.disp <= relative
                and relative + 4 <= x.operands[1].mem.disp + x.operands[1].size
                and x.operands[1].size in (4, 16) for x in copy)
    check(found, ('native scalar copy width', hex(offset)))
for offset in scalar8[1:10]:
    relative = offset - 0x30
    check(any(x.mnemonic == 'movzx' and len(x.operands) == 2
              and x.operands[1].type == capstone.x86.X86_OP_MEM
              and x.operands[1].mem.base == capstone.x86.X86_REG_RDX
              and x.operands[1].mem.disp == relative and x.operands[1].size == 1 for x in copy),
          ('native byte copy width', hex(offset)))
for at, mnemonic, operands in [
    (0x7B3207, 'mov', 'dword ptr [rcx + 0xc], 0x3f800000'),
    (0x7B3215, 'mov', 'dword ptr [rcx + 8], r9d'),
    (0x7B321E, 'mov', 'byte ptr [rcx + 0x1c], r9b'),
    (0x7B3222, 'mov', 'dword ptr [rcx + 0x10], 0x3f800000'),
    (0x7B3229, 'mov', 'dword ptr [rcx + 0x14], 0x447a0000'),
    (0x7B3230, 'mov', 'dword ptr [rcx + 0x18], 0x40800000'),
    (0x7B3240, 'mov', 'qword ptr [rax], r9'),
    (0x7B3243, 'mov', 'dword ptr [rax + 8], 0xffffffff'),
    (0x7B324A, 'lea', 'rax, [rax + 0x10]'),
    (0x7B327B, 'mov', 'dword ptr [rcx + 0x70], 0x3f800000'),
    (0x7B3282, 'mov', 'dword ptr [rcx + 0x74], 0x3f800000'),
    (0x7B3289, 'mov', 'word ptr [rcx + 0x78], r9w'),
    (0x32F33B, 'movups', 'xmm0, xmmword ptr [rdi + 0xd0]'),
    (0x32F342, 'movups', 'xmmword ptr [rbx + 0xd0], xmm0'),
    (0x32F39D, 'movups', 'xmm1, xmmword ptr [rdi + 0x140]'),
    (0x32F3A4, 'movups', 'xmmword ptr [rbx + 0x140], xmm1'),
]:
    ins(at, mnemonic, operands)
for offset, size in [(x, 4) for x in scalar32] + [(x, 1) for x in scalar8] + [(0x178, 2)]:
    # Excluded process-local pointer/control pairs, both variants and resource IDs.
    excluded = [(0x38, 0x18), (0x60, 0x30), (0xE8, 0x20), (0x120, 0x50), (0x180, 0x20)]
    check(all(offset + size <= start or offset >= start + length for start, length in excluded),
          ('wire scalar overlaps local reference', hex(offset)))

# LockPoint is a per-actor row identity, not the actor weak pointer itself.
for at, mnemonic, operands in [
    (0x548EF0, 'movsxd', 'r9, r8d'),
    (0x548EF3, 'cmp', 'r9, qword ptr [rcx + 0x378]'),
    (0x548EFC, 'test', 'r8d, r8d'), (0x548F03, 'shl', 'r9, 4'),
    (0x548F07, 'add', 'r9, qword ptr [rcx + 0x368]'),
    (0x548F15, 'mov', 'rcx, qword ptr [r9 + 8]'),
    (0x548F1E, 'mov', 'rax, qword ptr [r9]'),
    (0x548F28, 'lock inc', 'dword ptr [rcx + 0xc]'),
    (0x6D2A46, 'mov', 'eax, dword ptr [rbx + 0x318]'),
    (0x6D2A4C, 'mov', 'dword ptr [rbp + 0x210], eax'),
    (0x6D2A58, 'mov', 'rax, qword ptr [rbx + 0x30]'),
    (0x6D2A61, 'mov', 'rcx, qword ptr [rbx + 0x28]'),
    (0x6D2A6C, 'mov', 'qword ptr [rbp + 0x1b8], rcx'),
    (0x6D2A7A, 'mov', 'qword ptr [rbp + 0x1c0], rdx'),
    (0x6D2FB4, 'cmp', 'dword ptr [rdi + 0x378], 0'),
    (0x6D3114, 'movups', 'xmmword ptr [r14], xmm4'),
    (0x6D3122, 'movups', 'xmmword ptr [r14 + 0x30], xmm1'),
    (0x6D31DD, 'cmp', 'qword ptr [rsi + 0x658], 0'),
    (0x6D31EB, 'mov', 'rax, qword ptr [rsi + 0x670]'),
    (0x6D31FB, 'mov', 'rcx, qword ptr [rsi + 0x668]'),
    (0x6D3202, 'lock inc', 'dword ptr [rax + 0xc]'),
    (0x6D3209, 'mov', 'qword ptr [rbp + 0x1f8], rcx'),
    (0x6D3217, 'mov', 'qword ptr [rbp + 0x200], rdx'),
    (0x6D3263, 'mov', 'qword ptr [rbp + 0x208], 0'),
    (0x6D3343, 'mov', 'rax, qword ptr [rsi + 0x668]'),
    (0x6D334A, 'mov', 'qword ptr [rbp + 0x208], rax'),
    (0x557521, 'mov', 'rcx, qword ptr [r13 + 0x30]'),
    (0x557531, 'mov', 'rax, qword ptr [r13 + 0x28]'),
    (0x557550, 'mov', 'qword ptr [rdx + 0x668], r12'),
    (0x55755E, 'mov', 'qword ptr [rdx + 0x670], rcx'),
]:
    ins(at, mnemonic, operands)

# Scalar speed and final matrices are actual outcomes: replay must not rerun RNG.
for at, mnemonic, operands in [
    (0x6D3080, 'lea', 'rdi, [rsi + 0x4e0]'),
    (0x6D313A, 'mov', 'rax, qword ptr [rdi]'),
    (0x6D3151, 'mov', 'qword ptr [rdi], rax'),
    (0x6D3180, 'movss', 'dword ptr [rbp + 0x214], xmm0'),
    (0x6D3285, 'mov', 'qword ptr [rdi], rax'),
    (0x6D3299, 'mov', 'dword ptr [rbp + 0x320], eax'),
    (0x6D329F, 'mov', 'eax, dword ptr [rsi + 0x4e8]'),
    (0x6D32A5, 'mov', 'dword ptr [rbp + 0x324], eax'),
    (0x6D32AD, 'mov', 'dword ptr [rsi + 0x4e8], eax'),
    (0x6D0EAF, 'xor', 'ebx, ebx'),
    (0x6D0FD8, 'mov', 'qword ptr [rsi + 0x5d0], rbx'),
    (0x6D0FDF, 'mov', 'qword ptr [rsi + 0x610], rbx'),
    (0x6D0FE6, 'mov', 'qword ptr [rsi + 0x650], rbx'),
    (0x6D0FED, 'mov', 'qword ptr [rsi + 0x658], rbx'),
    (0x34CB10, 'call', '0x34d1c0'),
    (0x34D314, 'call', '0x6d0d90'),
    (0x35021C, 'call', '0x6d0d90'),
    (0x35F175, 'call', '0x3500c0'),
    (0x35F1E5, 'mov', 'rdi, qword ptr [rsi + 0x1a60]'),
    (0x35F904, 'lea', 'rcx, [rdi + 0x5d8]'),
    (0x35F90F, 'call', '0x35edf0'),
    (0x360350, 'add', 'rcx, 0x10'), (0x360354, 'jmp', '0x35fb30'),
    (0x35FE58, 'movss', 'dword ptr [rax + 0x94], xmm6'),
    (0x360172, 'movups', 'xmmword ptr [rax], xmm0'),
    (0x360175, 'movups', 'xmmword ptr [rax + 0x10], xmm6'),
    (0x36017E, 'movups', 'xmmword ptr [rax + 0x20], xmm3'),
    (0x360182, 'movups', 'xmmword ptr [rax + 0x30], xmm2'),
]:
    ins(at, mnemonic, operands)
address(0x35F886, 0x17AFDA8)
pointer(0x17AFDA8 + 0x10, 0x360350)

# Full reviewed construction blocks protect the absence of hidden stock
# post-create callback/network installation; single zero-write checks would not.
for start, end, digest in (
    (0x34C580, 0x34D1B7, '1f677b47d268b271951c06bf81359eee6194bfdcbecbc5d81db3f7767beb3f70'),
    (0x34D1C0, 0x34D3D1, '625c2df4b544c9bd0775ee12f7f365e56628bf3a253f5890312a65696f52f3a3'),
    (0x3500C0, 0x350419, '625aea4cf97498b20110a3e72f2b6c9c47a7cdbae9b74707fe8d51f5524f6971'),
    (0x35F150, 0x35F97F, '2824f1bd9c42f489023ca16c1e01ba03ae601a9eb7cd9c06dc7422b94df46d78'),
    (0x6D0D90, 0x6D1260, '27962a7cb37b9121d3966db9d3ef51b1cf2a37191dfc384ecc8c22c0c5088634'),
    (0x6D4B90, 0x6D51BA, '133aa54248ccd3d9cc462d3813de68083bb83218833427f067a4ccae35c2a748'),
    (0x6D7500, 0x6D7C87, 'a61218bc35c55094274347a0b29cd15a0c2cccbc44250742bf482ce39dc09630'),
    (0x6D7C90, 0x6D893A, '010471fc2b8a42a9d371b790318d7545f80831b05030e3eaa4b13fbf08fa45dd'),
    (0x6D59A0, 0x6D651B, '72fe45c84a6df05086b2befe5075b1fdc8847481a4972e53df229bda62d269c3'),
    (0x6D6550, 0x6D74F4, 'dda4da11b58d868632710636fde270a43bc279121c01964e952e715f1c714797'),
    (0x6D49A0, 0x6D4B89, '3ed1a0ce091f46b5bc42f84f2c482334a688d89c414e5b5d91d16d9af1cc3881'),
):
    check(hashlib.sha256(b[start:end]).hexdigest() == digest,
          ('reviewed stock constructor/configuration block', hex(start)))


# The real descriptor owner weak is copied into BulletBase and the native GDI.
# This is attacker preservation evidence, not proof that arbitrary callbacks
# preserve the owner: production separately rejects a changed owner weak.
GDI_MARKERS = """
6D26B9|mov|rdx, qword ptr [rcx + 0x2b8]
6D26E0|mov|rax, qword ptr [rsi + 0x2b0]
6D26E7|mov|qword ptr [rbp - 0x58], rax
6D26F6|mov|rbx, qword ptr [rbp - 0x58]
6D2A58|mov|rax, qword ptr [rbx + 0x30]
6D2A61|mov|rcx, qword ptr [rbx + 0x28]
6D2A68|lock inc|dword ptr [rax + 0xc]
6D2A6C|mov|qword ptr [rbp + 0x1b8], rcx
6D2A7A|mov|qword ptr [rbp + 0x1c0], rdx
6D3301|lea|r9, [rbp + 0x180]
6D331D|call|0x1194280
11942BA|mov|rdx, r9
11942C0|call|qword ptr [rax + 0x10]
25E3F3|call|0x25e690
25E6D9|call|0x22e9c0
22EA36|lea|r8, [r14 + 0x30]
22EA45|call|0x231cc0
231CF2|add|rcx, 0x9a0
231CFC|call|0x2307f0
23080D|mov|rax, qword ptr [rdx + 0x10]
23081D|mov|rcx, qword ptr [rdi + 8]
230828|mov|qword ptr [rbx + 8], rcx
230835|mov|qword ptr [rbx + 0x10], rdx
231FD1|mov|rax, qword ptr [rbx + 0x9b0]
231FDD|mov|rdi, qword ptr [rbx + 0x9a8]
231FF2|mov|qword ptr [rbx + 0x740], rdi
231FF9|mov|qword ptr [rbx + 0x748], rdx
230E9A|lea|r8, [rbx + 0x730]
230EA6|call|0x541ff0
5421EB|mov|rax, qword ptr [r12 + 0x18]
5421FB|mov|rcx, qword ptr [r12 + 0x10]
5422E5|add|rcx, 0x10
5422ED|call|0x114210
114251|mov|rax, qword ptr [rdx + 0x10]
114255|mov|qword ptr [rcx + 0x10], rax
114259|mov|rax, qword ptr [rdx + 0x18]
11425D|mov|qword ptr [rcx + 0x18], rax
543AB1|lea|rax, [rsi + 0x10]
543AB9|mov|dword ptr [rbp + 0x6f], 0x10000000
543ACE|call|qword ptr [rax + 0x50]
543ADE|call|qword ptr [rax + 0x48]
543AEE|call|qword ptr [rax + 0x58]
547C8D|lea|r12, [r13 + 0x10]
547E5A|call|0x22fca0
547E7B|movzx|edx, byte ptr [rax + 8]
547E7F|and|dl, 1
22FCB6|mov|r8, qword ptr [rdx + 8]
22FD50|call|0x12da7aa
"""
for line in GDI_MARKERS.strip().splitlines():
    at, mnemonic, operands = line.split('|', 2)
    ins(int(at, 16), mnemonic, operands)
for at, name in [(0x2006450, '.?AVSceneObject@game@xgs@@'),
                 (0x200CB20, '.?AVNetworkObject@net@@')]:
    check(b[at + 16:at + 16 + len(name) + 1] == name.encode('ascii') + b'\0',
          ('real RTTI used by native attacker cast', hex(at), name))

# The helper calls the actual native copy constructor and matching destructor,
# retains before consumed-weak move, and owns an aligned temporary matrix.
# Functional lifetime/refusal behavior belongs to the production C++ fixture.
for evidence in [
    'Fn<CopyFn>(0x32EF10)',
    'Fn<VoidFn>(0x100C90)',
    'InterlockedIncrement(reinterpret_cast<volatile LONG*>(source.control+12))',
    'Fn<MoveFn>(0x58EB40)(target,&source)',
    'Put<float*>(j.descriptor,8,j.matrix)',
    'std::memcmp(actual,id,32)',
    'At<const void*>(lock.object,8)!=actor.object',
    'At<const void*>(actual,0x40)!=l.control',
    'raw && raw!=extra.object',
]:
    check(evidence in source, ('production replay native contract anchor', evidence))


# Native copy/assignment intentionally skip CoreParam+18 (descriptor+48).
# It is padding, not an asset pointer. Comparing it would reject legitimate
# stack descriptors whose uninitialized padding differs from the baseline.
for at, mnemonic, operands in [
    (0x32F153, 'mov', 'qword ptr [rbx + 0x10], rcx'),
    (0x32F171, 'movups', 'xmm0, xmmword ptr [rdx + 0x20]'),
    (0x32F175, 'movups', 'xmmword ptr [rbx + 0x20], xmm0'),
    (0x230835, 'mov', 'qword ptr [rbx + 0x10], rdx'),
    (0x230850, 'movups', 'xmm0, xmmword ptr [rdi + 0x20]'),
    (0x23085A, 'movups', 'xmmword ptr [rbx + 0x20], xmm0'),
    (0x7B3219, 'mov', 'edx, 5'),
    (0x7B3243, 'mov', 'dword ptr [rax + 8], 0xffffffff'),
    (0x7B324A, 'lea', 'rax, [rax + 0x10]'),
]:
    ins(at, mnemonic, operands)
for start, end, registers in [
    (0x32F130, 0x32F3B0, {capstone.x86.X86_REG_RDX, capstone.x86.X86_REG_RBX}),
    (0x2307F0, 0x230B30, {capstone.x86.X86_REG_RDI, capstone.x86.X86_REG_RBX}),
]:
    decoded = list(d.disasm(b[start:end], start))
    touched_padding = [(hex(x.address), x.op_str) for x in decoded for op in x.operands
                       if op.type == capstone.x86.X86_OP_MEM and op.mem.base in registers
                       and op.mem.index == 0 and op.mem.disp < 0x20
                       and op.mem.disp + op.size > 0x18]
    check(not touched_padding, ('native CoreParam+18 padding is not read/written', hex(start), touched_padding))
# Each resource handle is 16 bytes, but its integer ID is only the DWORD at +8.
# +C..+F is padding and must not be included in an At<uint64_t>(idOffset) guard.
check(0x128 not in scalar32 and all(x not in scalar32 for x in (0x12C, 0x13C, 0x14C, 0x15C, 0x16C)),
      'process-local resource ID/padding entered wire POD list')

# Real AddLockPoint construction establishes the reverse owner and row identity.
LOCKPOINT_MARKERS = """
547A6A|mov|rbx, rcx
547A70|mov|r8, rcx
547A7F|call|0x22e210
22E231|mov|rdi, r8
22E283|lea|rcx, [rbx + 0x10]
22E28A|mov|rdx, rdi
22E28D|call|0x22db80
22DB96|mov|qword ptr [rcx + 8], rdx
22DBBD|movups|xmm0, xmmword ptr [rdx + 0x90]
547A97|add|rbx, 0x360
547A9E|mov|rax, qword ptr [rbx + 0x10]
547AA5|cmp|qword ptr [rbx + 0x18], rax
547B8D|shl|rcx, 4
547B91|add|rcx, qword ptr [rbx + 8]
"""
for line in LOCKPOINT_MARKERS.strip().splitlines():
    at, mnemonic, operands = line.split('|', 2)
    ins(int(at, 16), mnemonic, operands)


# Real session and native actor registry. The engine hashes NativeNetId+24,
# but its original equality compares only the first20 bytes. The production
# resolver additionally checks the whole semantic suffix before using a weak.
REGISTRY_MARKERS = """
784C72|mov|rax, qword ptr [rip + 0x192de47]
784C7E|add|rax, -0x98
784C84|mov|rbx, qword ptr [rax + 0xd8]
784C90|lock inc|dword ptr [rbx + 8]
784C9B|mov|rcx, qword ptr [rax + 0xd0]
784CB7|mov|rcx, qword ptr [rcx + 0x50]
784CBB|call|0x75c7d0
75C809|call|0x777410
75C80E|mov|rdx, qword ptr [rdi + 0xf0]
75C815|mov|r9, qword ptr [rdi + 0xc8]
75C823|add|rdx, qword ptr [rdi + 0xd8]
75C873|mov|rbx, qword ptr [rax + 0x38]
75C884|mov|rdx, qword ptr [rax + 0x30]
75C896|mov|rcx, qword ptr [rdx + 0x18]
75C89F|mov|rax, qword ptr [rdx + 0x10]
75C92B|call|0x777410
75C930|mov|rdx, qword ptr [rbx + 0xf0]
75C937|mov|r9, qword ptr [rbx + 0xc8]
75C93E|and|rdx, rax
75C941|shl|rdx, 4
75C945|add|rdx, qword ptr [rbx + 0xd8]
75C94C|mov|rax, qword ptr [rdx + 8]
75C950|cmp|rax, r9
75C955|mov|rdx, qword ptr [rdx]
75C960|cmp|r8, qword ptr [rax + 0x10]
75C966|mov|rcx, qword ptr [rax + 0x18]
75C970|mov|ecx, dword ptr [rax + 0x20]
75C978|cmp|rax, rdx
75C97D|mov|rax, qword ptr [rax + 8]
75C98F|mov|rdx, qword ptr [rax + 0x30]
75C99C|mov|rcx, qword ptr [rdx + 0x18]
75C9A5|mov|rax, qword ptr [rdx + 0x10]
75C9B0|lock inc|dword ptr [rcx + 0xc]
777410|mov|rax, qword ptr [rcx + 0x18]
"""
for line in REGISTRY_MARKERS.strip().splitlines():
    at, mnemonic, operands = line.split('|', 2)
    ins(int(at, 16), mnemonic, operands)
x = instruction(0x784C72)
check(x.address + x.size + x.operands[1].mem.disp == 0x20B2AC0,
      'real native Session view global')
for evidence in [
    'std::memcmp(node+0x10,id,20)', 'std::memcmp(node+0x28,id+24,8)',
    'At<const unsigned char*>(node,0x30)', 'At<void*>(record,0x10)',
    'At<unsigned char*>(record,0x18)', 'At<LONG>(actor.control,8)==0',
    'j.copyEntered=true', 'j.createEntered=true', 'j.cleanupEntered=true',
    'if(!NativeProjectileJournalsEmpty(l))return nullptr;',
]:
    check(evidence in source, ('persistent ownership/native registry contract', evidence))

check('alignas(16) float matrix[16]' in header, 'persistent aligned journal matrix')
# Only descriptor94 is unconditional speed; 50/54/58 have a real NaN
# sentinel and must follow the native optional-add gate instead of a blanket scan.
VELOCITY_MARKERS = """
2308F7|mov|eax, dword ptr [rdi + 0x64]
2308FA|mov|dword ptr [rbx + 0x64], eax
231DC3|movups|xmm2, xmmword ptr [rbp + 0x20]
231DC7|movss|xmm3, dword ptr [rbx + 0xa04]
231DD3|mulps|xmm3, xmm2
231E04|movups|xmmword ptr [rbx + 0xb90], xmm3
231E0B|movss|xmm0, dword ptr [rbx + 0x9c4]
231E13|addss|xmm0, dword ptr [rbx + 0x9c0]
231E1B|addss|xmm0, dword ptr [rbx + 0x9c8]
231E23|ucomiss|xmm0, xmm0
231E26|setp|al
231E2B|je|0x231e52
231E3C|addps|xmm1, xmm3
231E4B|movups|xmmword ptr [rbx + 0xb90], xmm1
"""
for line in VELOCITY_MARKERS.strip().splitlines():
    at, mnemonic, operands = line.split('|', 2)
    ins(int(at, 16), mnemonic, operands)
for evidence in ['Velocity(s)', 's.matrix[8+i]*speed', 'std::isfinite(speed)',
                 'const float sum=(inherited[1]+inherited[0])+inherited[2]',
                 'if(!std::isnan(sum))', 'std::isfinite(velocity[i]+inherited[i])']:
    check(evidence in source, ('native finite velocity and optional NaN contract', evidence))
# Pure lookup borrows immutable game-thread snapshots. The only strong CAS
# in a public resolver acquires its final output, with no later cleanup call.
actor_lookup = source.split('bool NativeActor(', 1)[1].split('bool ResolveActor(', 1)[0]
lock_lookup = source.split('bool ResolveNativeProjectileLock(', 1)[1].split('bool ReleaseNativeProjectileLock(', 1)[0]
check('Pin(' not in actor_lookup and 'Unpin(' not in actor_lookup,
      'native lookup cannot lose temporary references through a destructor')
check('Unpin(' not in lock_lookup and lock_lookup.count('Pin(lock)') == 1 and 'Pin(actor)' not in lock_lookup,
      'lock handoff has no post-publication temporary cleanup')
check('!stable() || !sameRecord()' in actor_lookup,
      'borrowed registry and record snapshots are checked before publishing')
print('Native projectile ABI audit:', n, 'checks passed')
