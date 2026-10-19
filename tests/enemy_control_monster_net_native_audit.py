"""Read-only evidence for Monster actual projectile creation and local 504 resources.

The supported EDF.dll is parsed, never loaded. This audits the real factory call,
its return handling, and the native C0/model/shared-pair construction contract.
If the sibling runtime source exists, every kSigs entry is checked against the
same image. This is source/ABI evidence, not game or multiplayer acceptance.
"""
from pathlib import Path
import argparse
import ast
import json
import re
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
parser.add_argument('--source', default='')
parser.add_argument('--dump-cadence', action='store_true', help='print read-only stock SGO/MAB cadence metadata as JSON')
args = parser.parse_args()
dll = Path(args.dll) if args.dll else Path(args.game) / 'EDF.dll' if args.game else None
if dll is None or not dll.is_file():
    print('SKIP: pass --dll or --game for the supported local EDF.dll')
    raise SystemExit(77)

import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

pe = pefile.PE(str(dll), fast_load=True)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(value, why):
    global checks
    assert value, why
    checks += 1


def ins(at):
    return next(md.disasm(pe.get_data(at, 16), at, count=1))


def exact(at, mnemonic, operands):
    item = ins(at)
    check((item.mnemonic, item.op_str) == (mnemonic, operands),
          f'{at:#x}: {item.mnemonic} {item.op_str}')


def branch(at, mnemonic, target):
    item = ins(at)
    check(item.mnemonic == mnemonic and item.operands[0].type == X86_OP_IMM
          and item.operands[0].imm == target, f'{at:#x}: {mnemonic} {target:#x}')


def address(at, target):
    item = ins(at)
    check(item.mnemonic == 'lea' and item.operands[1].type == X86_OP_MEM
          and item.operands[1].mem.base == X86_REG_RIP
          and item.address + item.size + item.operands[1].mem.disp == target,
          f'{at:#x}: local address {target:#x}')


def without_comments(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)


def compact(text):
    return re.sub(r'\s+', '', text)


def body(text, name, declaration=False):
    pattern = (r'\bstruct\s+' + re.escape(name) + r'\s*\{' if declaration else
               r'\b' + re.escape(name) + r'\s*\([^{};]*\)\s*(?:noexcept\s*)?\{')
    match = re.search(pattern, text)
    check(match is not None, f'runtime declares {name}')
    start = match.end()
    depth = 1
    for at in range(start, len(text)):
        depth += (text[at] == '{') - (text[at] == '}')
        if not depth:
            return compact(text[start:at])
    raise AssertionError(f'unclosed runtime body: {name}')


def size_expression(text, name, symbols):
    match = re.search(r'\b' + re.escape(name) + r'\s*=\s*([^,;\n]+)', text)
    check(match is not None, f'codec declares {name}')

    def value(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name) and node.id in symbols:
            return symbols[node.id]
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Mult)):
            left, right = value(node.left), value(node.right)
            return left + right if isinstance(node.op, ast.Add) else left * right
        raise AssertionError(f'{name}: size must use explicit known integer fields')

    return value(ast.parse(match.group(1), mode='eval').body)


check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
      and pe.FILE_HEADER.Machine == 0x8664
      and pe.OPTIONAL_HEADER.SizeOfImage == 0x22CE000, 'supported Steam x64 image')
# Read .pdata directly; no PE exception-directory expansion or DLL execution.
directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
ranges = {(start, end) for start, end, _unwind in
          struct.iter_unpack('<III', pe.get_data(directory.VirtualAddress, directory.Size))}
check((0x4B24D0, 0x4B2FF3) in ranges, '504 real slot5 emitter function boundary')
check((0x4B39A0, 0x4B39F9) in ranges, '504 complete slot5 boundary')
check((0x4ACDE7, 0x4ACF04) in ranges, '501 actual-emission split block boundary')
branch(0x4B39BB, 'call', 0x4B24D0)

# Both paths emit through the Unit factory, rather than command/state entry.
branch(0x4ACE88, 'call', 0x1194280)
branch(0x4B2C2C, 'call', 0x1194280)
exact(0x4B2C16, 'mov', 'r9, rbx')
exact(0x4B2C19, 'mov', 'r8, qword ptr [r15 + 0x1a60]')
exact(0x4B2C20, 'lea', 'rdx, [rsp + 0x70]')
exact(0x4B2818, 'lea', 'rbx, [r15 + 0x1a70]')
# 504 checks the returned object. 501 deliberately has no native null guard:
# its next instruction consumes root RNG. The accepted hook must inspect its
# own saved original return in both cases; never infer a shot from state entry.
exact(0x4B2C31, 'mov', 'r8, rax')
exact(0x4B2C34, 'test', 'rax, rax')
branch(0x4B2C37, 'je', 0x4B2D13)
exact(0x4ACE8D, 'mov', 'rax, qword ptr [rbx + 0x3e8]')
# Standard object controls consume strong +8 before the first native callback,
# then weak +C before the second callback. A callback exception cannot safely
# retry the entire decrement sequence from its beginning.
exact(0x4B2CC7, 'mov', 'eax, 0xffffffff')
exact(0x4B2CCC, 'lock xadd', 'dword ptr [rdi + 8], eax')
exact(0x4B2CD1, 'cmp', 'eax, 1')
branch(0x4B2CD4, 'jne', 0x4B2CF6)
exact(0x4B2CD9, 'mov', 'rcx, rdi')
exact(0x4B2CDC, 'call', 'qword ptr [rax]')
exact(0x4B2CDE, 'mov', 'eax, 0xffffffff')
exact(0x4B2CE3, 'lock xadd', 'dword ptr [rdi + 0xc], eax')
exact(0x4B2CE8, 'cmp', 'eax, 1')
branch(0x4B2CEB, 'jne', 0x4B2CF6)
exact(0x4B2CF0, 'mov', 'rcx, rdi')
exact(0x4B2CF3, 'call', 'qword ptr [rax + 8]')

# Each 504 pellet samples root RNG before the final matrix/factory call.
exact(0x4B2832, 'mov', 'rcx, qword ptr [r15 + 0x3e8]')
exact(0x4B2839, 'movabs', 'rdx, 0x5d588b656c078965')
exact(0x4B285D, 'mov', 'dword ptr [r15 + 0x1c10], eax')
exact(0x4B286F, 'mov', 'qword ptr [r15 + 0x3e8], rcx')
# The single local factory is resolved from the original SGO; no pointer or
# process-global registration order is a wire factory identity.
address(0x4B16FA, 0x17BE570)
branch(0x4B172C, 'call', 0x1195C50)
exact(0x4B1731, 'mov', 'qword ptr [r13 + 0x1a60], rax')

# A fresh model payload is needed for every emission: allocate C0, zero it,
# construct the native graphics-buffer member at +48, and set its world matrix.
exact(0x4B298A, 'mov', 'ecx, 0xc0')
branch(0x4B298F, 'call', 0x12D85B0)
exact(0x4B299B, 'test', 'rax, rax')
branch(0x4B299E, 'je', 0x4B2A50)
exact(0x4B29A4, 'xor', 'edx, edx')
exact(0x4B29A6, 'mov', 'r8d, 0xc0')
branch(0x4B29AF, 'call', 0x12DA786)
exact(0x4B29CE, 'lea', 'rcx, [rbx + 0x48]')
branch(0x4B29D2, 'call', 0x111F570)
branch(0x111F579, 'call', 0x113F160)
exact(0x113F16C, 'mov', 'qword ptr [rcx + 8], rax')
exact(0x113F170, 'mov', 'qword ptr [rcx + 0x10], rax')
exact(0x113F174, 'mov', 'qword ptr [rcx + 0x18], rax')
exact(0x111F585, 'mov', 'qword ptr [rbx + 0x20], 0')
for at, operands in (
    (0x4B29D7, 'dword ptr [rbx + 0x98], 0x3f800000'),
    (0x4B29E1, 'qword ptr [rbx + 0x84], 0x3f800000'),
    (0x4B29EC, 'qword ptr [rbx + 0x70], 0x3f800000'),
    (0x4B2A19, 'dword ptr [rbx + 0xac], 0x3f800000'),
):
    exact(at, 'mov', operands)
# Native shared-pair layout is [control*, object*]; the separate 10-byte-hex
# control is two u32 counts followed by a pointer, not a std::shared_ptr control.
exact(0x4B2A63, 'mov', 'ecx, 0x10')
branch(0x4B2A68, 'call', 0x12D85B0)
exact(0x4B2A71, 'test', 'rax, rax')
branch(0x4B2A74, 'je', 0x4B2A86)
exact(0x4B2A76, 'mov', 'ecx, 1')
exact(0x4B2A7B, 'mov', 'dword ptr [rax], ecx')
exact(0x4B2A7D, 'mov', 'dword ptr [rax + 4], ecx')
exact(0x4B2A80, 'mov', 'qword ptr [rax + 8], rbx')

# Model reconstruction is bool(C0*, localGraphicsManager*, modelResource*).
# The manager is an address obtained by LEA, not [image+2136FF0]. It is retained
# by the native buffer and supplies its D3D device (+80) to CreateBuffer (+18).
address(0x4B2B95, 0x2136FF0)
exact(0x4B2B8E, 'mov', 'r8, qword ptr [r15 + 0x1c70]')
exact(0x4B2B9C, 'mov', 'rcx, qword ptr [r15 + 0x1c08]')
branch(0x4B2BA3, 'call', 0x11002D0)
exact(0x11002F2, 'mov', 'r12, rdx')
exact(0x11002F9, 'mov', 'qword ptr [rcx], r8')
exact(0x11004E7, 'mov', 'rdx, r12')
branch(0x11004F2, 'call', 0x111F650)
exact(0x111F746, 'mov', 'qword ptr [rdi + 0x18], rbp')
exact(0x113F53C, 'mov', 'rcx, qword ptr [rdx + 0x80]')
exact(0x113F54F, 'call', 'qword ptr [rax + 0x18]')
# This helper ignores the inner allocation status before returning true. Its
# bool result alone is therefore not proof that every GPU allocation succeeded.
exact(0x11004F7, 'mov', 'rax, qword ptr [rbx + 0x10]')
exact(0x110055F, 'mov', 'al, 1')

# Native pair cleanup can be reused for complete local pairs and partially
# initialized native payloads, with the matching payload-specific destructor.
branch(0x4B2A91, 'call', 0x22F790)  # model pair at descriptor+190
branch(0x4B2BB2, 'call', 0x22F8E0)  # material pair at descriptor+180
exact(0x22F7AA, 'mov', 'qword ptr [rcx + 8], rbp')
exact(0x22F7BF, 'lock cmpxchg', 'qword ptr [rcx], rbp')
branch(0x22F7F8, 'call', 0x111F610)
exact(0x22F88F, 'mov', 'edx, 0xc0')
branch(0x22F897, 'call', 0x12D85EC)
exact(0x22F8FA, 'mov', 'qword ptr [rcx + 8], rbp')
branch(0x22F938, 'call', 0x100E90)
exact(0x22F93D, 'mov', 'edx, 0xe8')
branch(0x22F945, 'call', 0x12D85EC)
# If allocating the control itself fails, there is no pair for 22F790 to own.
# The supported caller also exposes the direct C0 destruction/free sequence.
branch(0x4B2B48, 'call', 0x111F610)
branch(0x4B2B51, 'call', 0x1860D0)
branch(0x4B2B5A, 'call', 0x186270)
exact(0x4B2B5F, 'mov', 'edx, 0xc0')
branch(0x4B2B67, 'call', 0x12D85EC)
# E8 material payloads may be retained through the native assignment helper.
branch(0x2A0C40, 'call', 0x22F8E0)
exact(0x2A0C5A, 'lock cmpxchg', 'dword ptr [rcx + 4], edx')
exact(0x2A0C6A, 'lock cmpxchg', 'dword ptr [rcx], edx')

# Each projectile retains these native resources; they cannot be reused as one
# mutable model instance for all accepted shots or replaced with host pointers.
exact(0x26F644, 'mov', 'r13, qword ptr [rsi + 0x198]')
exact(0x26F6D4, 'mov', 'r8, qword ptr [rsi + 0x180]')
exact(0x26F870, 'mov', 'r8, qword ptr [rsi + 0x190]')
exact(0x22EA5F, 'mov', 'eax, dword ptr [r14 + 0x1a0]')
exact(0x22EA66, 'mov', 'qword ptr [rsi + 0x130], rax')

# Variant1 is an SGO node POINTER; variant2 is a (DSGO tree*, node index)
# HANDLE, not two inline numeric values. Native copy and numeric dispatch tables
# prove why process-address bytes cannot form the cross-peer definition digest.
base = pe.OPTIONAL_HEADER.ImageBase
check(tuple(value - base for value in struct.unpack('<3Q', pe.get_data(0x179E1C0, 24)))
      == (0x22FB60, 0x22FB50, 0x22FB70), 'variant copy table preserves typed pointer/handle shapes')
exact(0x22FB50, 'mov', 'rax, qword ptr [rdx]')
exact(0x22FB53, 'mov', 'rdx, qword ptr [rax]')
exact(0x22FB56, 'mov', 'qword ptr [rcx], rdx')
exact(0x22FB70, 'mov', 'rax, qword ptr [rdx]')
exact(0x22FB73, 'movups', 'xmm0, xmmword ptr [rax]')
exact(0x22FB76, 'movups', 'xmmword ptr [rcx], xmm0')
check(tuple(value - base for value in struct.unpack('<3Q', pe.get_data(0x179EAA8, 24)))
      == (0x238FD0, 0x238E10, 0x2390B0), 'numeric consumer dispatch distinguishes monostate, SGO node and DSGO handle')
exact(0x238FD0, 'mov', 'qword ptr [rdx], 0')
branch(0x2390B9, 'call', 0x52F50)
exact(0x2390BE, 'movsd', 'qword ptr [rbx], xmm0')
address(0x4AB027, 0x17C58B8)
check(pe.get_data(0x17C58B8, 42) == 'BulletCustomParameter'.encode('utf-16-le'),
      '501 resolves the original BulletCustomParameter SGO value')
exact(0x4AB045, 'mov', 'ebx, 1')
exact(0x4AB04F, 'mov', 'word ptr [rsp + 0x48], bx')
exact(0x4AB054, 'mov', 'qword ptr [rsp + 0x38], rax')
exact(0x4AB059, 'lea', 'rcx, [rsi + 0x1c18]')
branch(0x4AB065, 'call', 0x244CB0)
check(0x1B30 + 0xE8 == 0x1C18, '501 descriptor E8 is a typed SGO variant, not raw immediate bytes')

# 504 E8 is selected at emission, not an immutable root descriptor default.
# Its local roster is a supported MSVC map: {head*, size}, node links/color/nil,
# wstring key at +20 and a 30-byte-hex value at +40 ending in the custom SGO ptr.
exact(0x4B1AD0, 'lea', 'rcx, [r13 + 0x1d70]')
branch(0x4B1AD7, 'call', 0x4ADE40)
exact(0x4ADE65, 'mov', 'r13, qword ptr [rcx]')
exact(0x4ADE68, 'mov', 'r10, qword ptr [r13 + 8]')
exact(0x4ADE7C, 'cmp', 'byte ptr [r10 + 0x19], r12b')
exact(0x4ADE95, 'lea', 'r9, [r10 + 0x20]')
exact(0x4ADEA5, 'mov', 'r11, qword ptr [r9 + 0x10]')
exact(0x4ADEA9, 'cmp', 'qword ptr [r9 + 0x18], 8')
exact(0x4ADEB0, 'mov', 'r9, qword ptr [r9]')
exact(0x4ADF6B, 'cmp', 'qword ptr [r15 + 8], rax')
exact(0x4B47AA, 'inc', 'qword ptr [rcx + 8]')
exact(0x4ADF7F, 'mov', 'ecx, 0x70')
branch(0x4ADF84, 'call', 0x12D85B0)
for at, operands in ((0x4ADFC0, 'qword ptr [rax], r13'), (0x4ADFC3, 'qword ptr [rax + 8], r13'),
                     (0x4ADFC7, 'qword ptr [rax + 0x10], r13'), (0x4ADFCB, 'word ptr [rax + 0x18], 0')):
    exact(at, 'mov', operands)
exact(0x4B139F, 'lea', 'rcx, [rbx + 0x1d70]')
branch(0x4B13A6, 'call', 0x4ADE40)
exact(0x4B13AB, 'mov', 'rbp, qword ptr [rax]')
exact(0x4B13AE, 'add', 'rbp, 0x40')
for at, operands in ((0x4B1B27, 'dword ptr [rdi + 0x40], eax'),
                     (0x4B1B32, 'dword ptr [rdi + 0x44], eax'),
                     (0x4B1B4B, 'dword ptr [rdi + 0x48], eax'),
                     (0x4B1B66, 'dword ptr [rdi + 0x4c], eax'),
                     (0x4B1B7C, 'dword ptr [rdi + 0x54], 0'),
                     (0x4B1BDD, 'dword ptr [rdi + 0x60], eax')):
    exact(at, 'mov', operands)
exact(0x4B1BC3, 'add', 'rax, 0x3c')
exact(0x4B1BCA, 'mov', 'qword ptr [rdi + 0x68], rax')
check(0x68 - 0x40 == 0x28, 'custom pointer is value+28, outside the explicit numeric roster fields')
exact(0x4B2677, 'mov', 'rax, qword ptr [rbx + 0x40]')
exact(0x4B267B, 'mov', 'rcx, qword ptr [rax + 0x28]')
exact(0x4B268D, 'mov', 'word ptr [rbp + 0x40], r13w')
exact(0x4B2692, 'mov', 'qword ptr [rbp + 0x30], rcx')
exact(0x4B2696, 'lea', 'rcx, [r15 + 0x1b58]')
branch(0x4B26A1, 'call', 0x244CB0)
check(0x1A70 + 0xE8 == 0x1B58, 'each actual 504 creation receives the currently selected pattern custom variant')
# value+14 is a real level-adjusted numeric, while value+24 is padding. The
# updater has both constructor and later native override callers.
exact(0x4B238C, 'mov', 'rax, qword ptr [rbx + 0x1d70]')
exact(0x4B2393, 'mov', 'rax, qword ptr [rax]')
exact(0x4B23A0, 'mov', 'rdx, qword ptr [rax + 0x68]')
exact(0x4B23B4, 'mov', 'dword ptr [rax + 0x54], r8d')
branch(0x4AE15D, 'call', 0x4B2250)
branch(0x4B33D5, 'call', 0x4B2250)
# The same level updater also changes real 504 descriptor numerics. These are
# accepted sample values, not immutable configuration identity across peers.
exact(0x4B22A3, 'movss', 'dword ptr [rbx + 0x1b30], xmm0')
exact(0x4B22D8, 'movss', 'dword ptr [rbx + 0x1b50], xmm0')
exact(0x4B230D, 'movss', 'dword ptr [rbx + 0x1b20], xmm0')
check({0x1B30 - 0x1A70, 0x1B50 - 0x1A70, 0x1B20 - 0x1A70} == {0xB0, 0xC0, 0xE0},
      '504 native level writes are IP+B0/C0/E0 rather than unrelated actor fields')
# Sentinel initialization and roster destruction remain root-owned; a valid
# strong root pin must cover both the lookup nodes and their source SGO tree.
exact(0x4AE0CB, 'add', 'rbx, 0x310')
exact(0x4AE0D7, 'mov', 'qword ptr [rbx], rbp')
exact(0x4AE0DA, 'mov', 'qword ptr [rbx + 8], rbp')
exact(0x4AE0DE, 'lea', 'ecx, [rbp + 0x70]')
branch(0x4AE0E1, 'call', 0x12D85B0)
exact(0x4AE0F1, 'mov', 'word ptr [rax + 0x18], 0x101')
exact(0x4AE0F7, 'mov', 'qword ptr [rbx], rax')
exact(0x4AEB3D, 'mov', 'r8, qword ptr [rcx + 0x310]')
branch(0x4AEB56, 'call', 0x4ADDC0)
exact(0x4ADE05, 'lea', 'rcx, [rdi + 0x20]')
branch(0x4ADE09, 'call', 0x3CF70)
exact(0x4ADE0E, 'mov', 'edx, 0x70')
branch(0x4ADE16, 'call', 0x12D85EC)
exact(0x4AEB60, 'mov', 'rcx, qword ptr [rdi + 0x310]')
branch(0x4AEB67, 'call', 0x12D85EC)
check(0x1A60 + 0x310 == 0x1D70, 'pattern-map cleanup is part of the already-audited final 504 member destructor')

# IP+70 is retained as a live matrix POINTER, not a one-shot copy. The common
# base passes IP+30 to the component, whose +40 ends at projectile+B20. Later
# native updates dereference the pointer, so a temporary/stale muzzle is unsafe.
exact(0x22EA36, 'lea', 'r8, [r14 + 0x30]')
exact(0x22EA3E, 'lea', 'rcx, [rsi + 0x140]')
branch(0x22EA45, 'call', 0x231CC0)
exact(0x231CEF, 'mov', 'rdx, r8')
exact(0x231CF2, 'add', 'rcx, 0x9a0')
branch(0x231CFC, 'call', 0x2307F0)
exact(0x230895, 'mov', 'rax, qword ptr [rdi + 0x40]')
exact(0x23089C, 'mov', 'qword ptr [rbx + 0x40], rax')
check(0x140 + 0x9A0 + 0x40 == 0xB20 and 0x30 + 0x40 == 0x70,
      'muzzle pointer source/destination offsets include both native subobjects')
for at in (0x23194F, 0x231A21, 0x235F5B):
    exact(at, 'mov', 'rax, qword ptr [rcx + 0x9e0]')
exact(0x23195B, 'movups', 'xmm0, xmmword ptr [rax]')
exact(0x231A2D, 'movaps', 'xmm0, xmmword ptr [rax]')
exact(0x235F69, 'movaps', 'xmm0, xmmword ptr [rax]')
# The supported Monster constructors use the default descriptor initialization,
# which explicitly nulls IP+70. Class-relative writes after construction would
# need a separate lifetime contract before a muzzle-bearing outcome is admitted.
exact(0x100207, 'add', 'rcx, 0x30')
exact(0x100234, 'mov', 'qword ptr [rcx + 0x40], rsi')
exact(0x4AAF29, 'lea', 'rdi, [rsi + 0x1b20]')
exact(0x4AAF35, 'lea', 'rcx, [rdi + 0x10]')
branch(0x4AAF39, 'call', 0x1001D0)
exact(0x4AE080, 'lea', 'rbx, [rdi + 0x1a60]')
exact(0x4AE08C, 'lea', 'rcx, [rbx + 0x10]')
branch(0x4AE090, 'call', 0x1001D0)
for first, last, offset, family in ((0x4A9000, 0x4AE000, 0x1BA0, 501), (0x4AE000, 0x4B4000, 0x1AE0, 504)):
    stores = []
    for start, end in ranges:
        if first <= start < last:
            for item in md.disasm(pe.get_data(start, end - start), start):
                if any(operand.type == X86_OP_MEM and operand.mem.disp == offset
                       and operand.access & capstone.CS_AC_WRITE for operand in item.operands):
                    stores.append(item.address)
    check(not stores, f'{family}: no class-relative later direct stores replace the stock null muzzle')

# The 504 replay uses root-owned static resources. Their writes/clear aliases
# and the final resource-pool destructor are audited separately from death.
exact(0x4B1859, 'mov', 'qword ptr [r13 + 0x1c70], rax')
branch(0x4AE145, 'call', 0x4B16B0)
exact(0x4AE096, 'lea', 'rcx, [rbx + 0x210]')
exact(0x4AE0A4, 'mov', 'qword ptr [rcx + 8], rbp')
exact(0x4AE0A8, 'mov', 'qword ptr [rcx + 0x10], rbp')
exact(0x4B1925, 'lea', 'rcx, [r13 + 0x1c78]')
branch(0x4B1931, 'call', 0x2A0C30)
exact(0x4AF325, 'lea', 'rcx, [rbx + 0x1a60]')
branch(0x4AF32C, 'call', 0x4AEB30)
exact(0x4AEBA9, 'lea', 'rcx, [rdi + 0x218]')
branch(0x4AEBB0, 'call', 0x22F8E0)
check(0x1A60 + 0x218 == 0x1C78, 'material cleanup belongs to the final 504 member destructor')
branch(0x4AF334, 'call', 0x4B68F0)
exact(0x4B69F5, 'lea', 'rcx, [rsi + 0x8c0]')
branch(0x4B69FC, 'call', 0x6B8E70)
exact(0x6B913A, 'lea', 'rcx, [rsi + 0x88]')
branch(0x6B9141, 'call', 0x101100)
exact(0x101136, 'dec', 'dword ptr [rax + 0x80]')
branch(0x101193, 'call', 0x102DF0)
check(0x8C0 + 0x88 == 0x948, 'the model pool is released by the final root/base destructor')
exact(0x4BF053, 'add', 'rcx, 0x1470')
branch(0x4BF05F, 'call', 0x6C8760)
address(0x4BF073, 0x4BDB20)
exact(0x4BDB38, 'mov', 'dword ptr [rdx + rax + 0x104], 0xffffffff')
branch(0x4BF147, 'jmp', 0x118A1B0)
death_releases = {0x4AEB30, 0x4B68F0, 0x6B8E70, 0x101100, 0x22F790, 0x22F8E0}
check(not any(item.mnemonic in ('call', 'jmp') and item.operands[0].type == X86_OP_IMM
              and item.operands[0].imm in death_releases
              for item in md.disasm(pe.get_data(0x4BF030, 0x120), 0x4BF030)),
      'ordinary death callback does not invoke the resource-pair or model-pool destructors')

# Scheduling counters advance once per real native update invocation. Neither
# counter uses the physics/render rate, so these values must not become Hz.
address(0x4AC07F, 0x179E8C0)
check(pe.get_data(0x179E8C0, 10) == 'time\0'.encode('utf-16-le'), '501 MAB fire reads its actual time argument')
exact(0x4AC08B, 'mov', 'edx, dword ptr [rax + 8]')
exact(0x4AC08E, 'mov', 'dword ptr [rbx + 0x1d10], edx')
exact(0x4AC094, 'mov', 'dword ptr [rbx + 0x1d14], 2')
branch(0x4ACDB9, 'call', 0x4BD100)
exact(0x4ACDBE, 'mov', 'eax, dword ptr [rbx + 0x1d10]')
exact(0x4ACDCC, 'dec', 'eax')
exact(0x4ACDD0, 'mov', 'dword ptr [rbx + 0x1d10], eax')
exact(0x4ACDD6, 'div', 'dword ptr [rbx + 0x1d14]')
exact(0x4ACDDC, 'test', 'edx, edx')
branch(0x4ACDDE, 'jne', 0x4ACF04)
exact(0x4B25F7, 'dec', 'dword ptr [rbx + 0x54]')
branch(0x4B25FE, 'jg', 0x4B275C)
exact(0x4B2611, 'mov', 'ecx, dword ptr [rax + 4]')
exact(0x4B2614, 'mov', 'dword ptr [rbx + 0x54], ecx')
exact(0x4B261A, 'mov', 'dword ptr [rbx + 0x50], eax')
exact(0x4B26CF, 'mov', 'eax, dword ptr [rcx]')
exact(0x4B271E, 'cvtsi2ss', 'xmm0, dword ptr [rax + 8]')
exact(0x4B2726, 'cvtsi2ss', 'xmm1, dword ptr [rax + 0xc]')
exact(0x4B2737, 'addss', 'xmm1, xmm6')
exact(0x4B273B, 'cvttss2si', 'edi, xmm1')
pellet_rounding = ins(0x4B25AC)
check(pellet_rounding.mnemonic == 'movss' and pellet_rounding.operands[1].mem.base == X86_REG_RIP,
      '504 pellet rounding constant is read from the supported image')
rounding_at = pellet_rounding.address + pellet_rounding.size + pellet_rounding.operands[1].mem.disp
check(struct.unpack('<f', pe.get_data(rounding_at, 4))[0] == 0.5,
      '504 positive interpolated pellet counts use truncation after adding one half')
exact(0x4B2791, 'imul', 'rax, qword ptr [rbx + 0x38], 0x310')
exact(0x4B2E4C, 'cmp', 'r12d, edi')
branch(0x4B2E56, 'jl', 0x4B2832)
exact(0x4B2F50, 'add', 'r13, 0x310')
exact(0x4B2F74, 'add', 'rbx, 0x58')

source = Path(args.source) if args.source else ROOT / 'src/enemy_control_monster_net.cpp'
if source.is_file():
    text = source.read_text(encoding='utf-8')
    table = re.search(r'\bkSigs\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S)
    check(table is not None, 'runtime declares its supported-image kSigs table')
    signatures = re.findall(r'\{\s*(0x[0-9A-Fa-f]+)[uUlL]*\s*,\s*\{([^{}]*)\}\s*\}', table.group(1))
    check(bool(signatures), 'runtime fingerprints are source driven, not a second hand-maintained list')
    for at, raw in signatures:
        values = [part.strip() for part in raw.split(',') if part.strip()]
        check(all(re.fullmatch(r'0x[0-9A-Fa-f]{1,2}[uUlL]*', value) for value in values),
              f'{at}: signature uses explicit byte literals')
        data = bytes(int(re.sub(r'[uUlL]+$', '', value), 16) for value in values)
        check(len(data) == 16 and pe.get_data(int(at, 16), len(data)) == data,
              f'{at}: every runtime signature matches the actual supported DLL')
    print('runtime fingerprints:', len(signatures), 'checked', file=sys.stderr if args.dump_cadence else sys.stdout)

    # Source contracts are scoped to complete function bodies, independent of
    # line numbers, whitespace, comments and the number/order of fingerprints.
    runtime = without_comments(text)
    install = body(runtime, 'InstallMonsterAcceptedProjectiles')
    registered = re.findall(r'RegisterNativeUnitFactoryCallsite\((0x[0-9A-Fa-f]+)\)', install)
    check(len(registered) == 2 and {int(site, 16) for site in registered} == {0x4ACE88, 0x4B2C2C},
          'only the two actual creation calls register with the central dispatcher')
    check(all(f'NativeUnitFactoryCallsiteReady({site})' in install for site in registered)
          and 'RegisterNativeUnitFactoryProvider(provider)' in install
          and 'NativeUnitFactoryProviderReady(provider)' in install
          and 'InstallNativeUnitFactoryDispatcher()' in install,
          'provider and both exact callsites must pass the shared dispatcher admission')
    check(not re.search(r'\b(?:RedirectCall|PatchVtableSlot|RegisterControlledAiExtension)\s*\(', runtime),
          'Monster runtime installs no whole-slot5, AI or independent factory patch')

    invoke = body(runtime, 'Invoke')
    capture_at = invoke.find('CaptureNativeProjectileSample(l,descriptor,matrix,descriptor,&e.sample)')
    create_at = invoke.find('InvokeOriginalNativeUnitFactory(', capture_at)
    queue_at = invoke.find('s->queue[')
    check(0 <= capture_at < create_at < queue_at,
          'host captures the actual matrix/descriptor before the original factory and queue commit')
    check('if(!authority)returnnullptr;' in invoke[:capture_at]
          and 'ActualResources(l,s->family,descriptor)' in invoke[:capture_at],
          'guest autonomous emission is suppressed and host resources are checked before capture')
    check(re.search(r'\bresult=InvokeOriginalNativeUnitFactory\([^;]+\);if\(!result\)returnnullptr;', invoke) is not None,
          'only a nonnull saved original factory return can reach the accepted event queue')
    check('s->lost=true' in invoke[create_at:queue_at]
          and '++s->count;++s->next;' in invoke[queue_at:],
          'post-create queue exhaustion remains a lost outcome instead of fabricated acceptance')
    check(0 <= invoke.find('++s->nativeCalls;') < create_at
          and '__finally{--s->nativeCalls;}' in invoke[queue_at:],
          'native factory notification reentry cannot restore the stream before its accepted result is journalled')

    actual = body(runtime, 'ActualResources')
    check('At<constvoid*>(l.object,0x1C70)' in actual
          and 'At<constunsignedchar*>(l.object,0x1C78)' in actual,
          '504 actual resource checks resolve this root local model and material')
    check('At<constvoid*>(descriptor,0x180)!=material' in actual
          and 'At<constvoid*>(descriptor,0x188)!=(material?At<constvoid*>(material,8):nullptr)' in actual,
          '504 actual material control and payload must equal the root owned local resource')
    check('At<constvoid*>(descriptor,0x198)==payload' in actual
          and 'At<constvoid*>(payload,0)==modelAsset' in actual,
          '504 actual model pair payload must reconstruct this exact local model asset')
    definition = body(runtime, 'Definition')
    check('for(unsignedoffset:{0xE8u,0x1B0u})if(!(family==2&&offset==0xE8)&&!monster_definition::HashVariant(static_cast<constunsignedchar*>(descriptor)+offset,&hash))return0;' in definition,
          '501/custom defaults use typed hashes while 504 selected E8 is excluded from the static base digest')
    check('At<unsignedchar>(descriptor,offset+i)' not in definition
          and 'reinterpret_cast<std::uintptr_t>' not in definition,
          'definition digest does not hash process-local variant pointer/handle bytes')
    check('!(family==2&&(offset==0xB0||offset==0xC0||offset==0xE0))' in definition
          and 'At<std::uint32_t>(descriptor,offset)' in definition,
          '504 level-derived B0/C0/E0 are excluded from static identity while other-family static words remain hashed')
    pattern_hash = body(runtime, 'PatternHash')
    check('Put<constvoid*>(variant,0,custom)' in pattern_hash
          and 'Put<std::uint16_t>(variant,16,custom?1u:0xFFFFu)' in pattern_hash
          and 'monster_definition::HashVariant(variant,&hash)' in pattern_hash,
          'pattern custom identity hashes an explicit local typed variant, never the SGO pointer bytes')
    patterns = body(runtime, 'Patterns')
    scan_pattern = body(runtime, 'ScanPattern')
    check('At<std::size_t>(l.object,0x1D78)' in patterns
          and 'At<constunsignedchar*>(l.object,0x1D70)' in patterns
          and 'count>kPatterns' in patterns and 'head[0x19]!=1' in patterns
          and 'scan.seen==count&&s.patternCount==count' in patterns,
          'local 504 roster validates the real map head, declared size and complete node count')
    check('depth>=32' in scan_pattern and 'scan.seen>=scan.expected' in scan_pattern
          and '!Readable(node,0x70)' in scan_pattern and 'node[0x19]' in scan_pattern
          and 'At<constvoid*>(node,8)!=parent' in scan_pattern
          and 'scan.nodes[i]==node' in scan_pattern,
          'pattern traversal rejects unbounded depth, duplicate nodes, stale links and nonlocal map payloads')
    check('At<std::size_t>(node,0x30)' in scan_pattern
          and 'At<std::size_t>(node,0x38)' in scan_pattern
          and 'capacity<8?node+0x20:At<constunsignedchar*>(node,0x20)' in scan_pattern
          and 'At<std::uint16_t>(name,i*2)' in scan_pattern,
          'roster name identity uses the supported wstring layout and UTF16 content')
    check('for(unsignedoffset=0;offset<=0x20;offset+=4)if(offset!=0x14)' in scan_pattern
          and 'At<std::uint32_t>(node,0x40+offset)' in scan_pattern
          and 'At<constunsignedchar*>(node,0x68)' in scan_pattern
          and 'PatternHash(custom)' in scan_pattern,
          'roster digest includes explicit static numerics and typed custom data, excluding level+14 and padding+24')
    actual_pattern = body(runtime, 'ActualPattern')
    accepted_pattern = body(runtime, 'AcceptedPattern')
    check('At<std::uint16_t>(descriptor,0xF8)' in actual_pattern
          and 's.patterns[i].custom==custom&&PatternHash(custom)==s.patterns[i].hash' in actual_pattern,
          'host selected E8 must belong to the retained local roster with unchanged typed content')
    check('Number(s.digest,s.patterns[i].hash,8)==digest' in accepted_pattern
          and 'ActualPattern(*s,descriptor)' in invoke[:capture_at]
          and 'e.digest=Number(s->digest,pattern->hash,8)' in invoke[:capture_at],
          'each accepted 504 outcome digest identifies the selected local custom variant on both peers')
    check('At<constvoid*>(descriptor,0x70)' in invoke[:capture_at]
          and 'e.sample.hasMuzzle' in invoke[capture_at:create_at],
          'host refuses both a live native muzzle pointer and a muzzle-bearing sampled outcome')

    before = body(runtime, 'Before')
    check('Pin(Reference(l))' in before
          and 'Fn<CopyFn>(0x32EF10)(s.base,descriptor)' in before
          and 'Fn<VoidFn>(0x22F790)(s.base+0x190)' in before
          and 'Fn<VoidFn>(0x22F8E0)(s.base+0x180)' in before
          and 'Resources(' not in before,
          'admission pins the root and keeps an owned base with no shared per-shot mutable model')
    check(before.index('Pin(Reference(l))') < before.index('Patterns(s,l)') < before.index('Fn<CopyFn>(0x32EF10)(s.base,descriptor)'),
          'root ownership is retained before scanning or caching any pattern SGO node')
    check(before.index('Pin(Reference(l))') < before.index('s.rootHeld=true;') < before.index('Patterns(s,l)')
          and 's->rootHeld&&!s->cleanupEntered' in before,
          'successful admission records its one consumable strong pin and rejects existing closing/released roots')
    resources = body(runtime, 'Resources')
    check('Fn<AllocateFn>(0x12D85B0)(0x10)' in resources
          and 'Fn<AllocateFn>(0x12D85B0)(0xC0)' in resources
          and resources.index('Fn<AllocateFn>(0x12D85B0)(0x10)') < resources.index('Fn<AllocateFn>(0x12D85B0)(0xC0)'),
          'each replay allocates fresh native control and C0 payload, with control-first failure cleanup')
    check('std::memset(payload,0,0xC0)' in resources
          and 'Fn<VoidFn>(0x111F570)(payload+0x48)' in resources
          and 'for(unsignedoffset:{0x70u,0x84u,0x98u,0xACu})Put<float>(payload,offset,1.0f)' in resources,
          'fresh C0 uses the supported zero, graphics member and identity-matrix defaults')
    check(all(field in resources for field in ('Put<LONG>(control,0,1)', 'Put<LONG>(control,4,1)',
                                               'Put<void*>(control,8,payload)', 'Put<void*>(descriptor,0x190,control)',
                                               'Put<void*>(descriptor,0x198,payload)')),
          'fresh model transfers one owned native shared-pair reference to the scratch descriptor')
    check('Fn<bool(__fastcall*)(void*,void*,constvoid*)>(0x11002D0)(payload,image+0x2136FF0,model)' in resources,
          'model creation uses the local model asset and local graphics-manager address')
    check('if(!payload){Fn<FreeFn>(0x12D85EC)(control,0x10);stream.orphanControl=nullptr;returnfalse;}' in resources
          and 'stream.orphanControl=control;' in resources and 'stream.orphanPayload=payload;' in resources,
          'allocation failure frees an unowned control while persistent journals retain uncertain orphans')

    apply = body(runtime, 'Apply')
    copied = apply.find('Fn<CopyFn>(0x32EF10)(descriptor,s->base)')
    reconstructed = apply.find('Resources(*s,l,s->family,descriptor,event.sample.resourceMask)')
    preflight = apply.find('CanPrepareNativeProjectileSample(l,descriptor,s->muzzle,event.sample)')
    emitted = apply.find('WithNativeProjectileSampleInPlace(s->projectile,l,descriptor,s->muzzle,event.sample,Create,&unit)')
    committed = apply.find('s->applied=event.serial')
    check(0 <= copied < reconstructed < preflight < emitted < committed,
          'each new accepted event copies the native base, builds fresh resources, preflights and emits before receipt commit')
    selected = apply.find('AcceptedPattern(*s,event.digest)')
    restored_custom = apply.find('Fn<void(__fastcall*)(void*,constvoid*)>(0x244CB0)(descriptor+0xE8,variant)')
    check(0 <= selected < copied < restored_custom < reconstructed
          and 'pattern->custom' in apply[copied:restored_custom],
          'guest resolves the accepted pattern locally and reconstructs native E8 before resources or factory creation')
    check('event.sample.hasMuzzle' in apply[:copied],
          'guest refuses muzzle-bearing events before copying descriptors or emitting native projectiles')
    check('!NativeProjectileJournalEmpty(s->projectile)' in apply[:copied]
          and 's->faulted' in apply[:copied],
          'retained helper work or a previous emission fault blocks native copy and factory retry')
    check('if(!WithNativeProjectileSampleInPlace(s->projectile,l,descriptor,s->muzzle,event.sample,Create,&unit)){s->faulted=true;returnResult::failed;}' in apply[:committed],
          'helper cleanup failure faults the lease and cannot publish a receipt for an already-created projectile')
    check(0 <= apply.find('if(event.serial<=s->applied)') < copied
          and 'r.serial==event.serial&&r.hash==hash' in apply[:copied],
          'receipt duplicates are checked before any model allocation or native creation')
    finally_at = apply.find('__finally')
    check(finally_at > emitted and 'Fn<VoidFn>(0x100C90)(descriptor)' in apply[finally_at:]
          and 's->scratchMade=false' in apply[finally_at:],
          'native scratch destruction is a finally operation after every replay attempt')
    stream = body(runtime, 'Stream', declaration=True)
    check('scratch[0x1E0]' in stream and 'muzzle[16]' in stream
          and 'NativeProjectileJournalprojectile{};' in stream
          and all(field in stream for field in ('orphanControl', 'orphanPayload', 'scratchMade', 'uncertain')),
          'descriptor, muzzle, helper ownership and uncertain allocation journals live with the retained stream')
    check('rootHeld=false' in stream and 'cleanupEntered=false' in stream,
          'stream distinguishes retained ledger identity from consumable root ownership and cleanup reentry')
    empty = body(runtime, 'Empty')
    restore = body(runtime, 'Restore')
    blockers = ('count', 'nativeCalls', 'lost', 'uncertain', 'scratchMade', 'orphanControl', 'orphanPayload')
    check(all('!s->' + field in empty for field in blockers),
          'every accepted queue or uncertain resource prevents Empty')
    check('NativeProjectileJournalEmpty(s->projectile)' in empty,
          'a helper descriptor, pin, created result or uncertain cleanup also prevents Empty')
    check('s->rootHeld&&!s->cleanupEntered' in empty,
          'a released strong root or an in-progress cleanup cannot report an empty lease')
    capture = body(runtime, 'Capture')
    for name, function in (('Invoke', invoke), ('Apply', apply), ('Capture', capture)):
        check('!s->rootHeld' in function and 's->cleanupEntered' in function,
              f'{name} refuses both a consumed root pin and native cleanup reentry')
    guard = re.search(r'if\(([^;{}]*s->count[^;{}]*)\)returnfalse;', restore)
    check(guard is not None and all('s->' + field in guard.group(1) for field in blockers)
          and '!CanUnpin(s->root)' in guard.group(1) and '!s->rootHeld' in guard.group(1)
          and 's->cleanupEntered' in guard.group(1),
          'Restore retains the role and root for all pending, lost or uncertain native work')
    check('s->cleanupEntered=true;' not in restore[:guard.end()]
          and 's->rootHeld=false;' not in restore[:guard.end()],
          'root-unpin preflight rejection is mutation-free and can be retried')
    can_unpin = body(runtime, 'CanUnpin')
    check('At<LONG>(r.control,8)<=0' in can_unpin and 'At<LONG>(r.control,12)<=0' in can_unpin
          and 'table[0]&&table[1]' in can_unpin,
          'strong and weak counts plus both native destructor targets are admitted before decrement')
    unpin = body(runtime, 'Unpin')
    strong_at = unpin.find('InterlockedDecrement(reinterpret_cast<volatileLONG*>(r.control+8))')
    destroy_at = unpin.find('reinterpret_cast<VoidFn>(table[0])(r.control)')
    weak_at = unpin.find('InterlockedDecrement(reinterpret_cast<volatileLONG*>(r.control+12))')
    delete_at = unpin.find('reinterpret_cast<VoidFn>(table[1])(r.control)')
    check(0 <= strong_at < destroy_at < weak_at < delete_at,
          'runtime root release follows the proven strong-destroy then weak-delete ABI')
    destroyed = restore.find('Fn<VoidFn>(0x100C90)(s->base)')
    helper_restored = restore.find('RestoreNativeProjectileJournal(s->projectile)')
    cleanup_started = restore.find('s->cleanupEntered=true;')
    root_consumed = restore.find('s->rootHeld=false;')
    unpinned = restore.find('Unpin(s->root);')
    cleared = restore.find('std::memset(s,0,sizeof(*s))')
    check(0 <= cleanup_started < helper_restored < destroyed < root_consumed < unpinned < cleared
          and 's->uncertain=true' in restore[:destroyed]
          and 's->uncertain=true;' in restore[root_consumed:unpinned]
          and 's->uncertain=true' in restore[cleared:],
          'cleanup reentry is blocked before helper/base destruction and consumes root ownership before either destructor callback')
    check('if(!RestoreNativeProjectileJournal(s->projectile)){s->cleanupEntered=false;returnfalse;}' in restore
          and restore.count('s->cleanupEntered=false;') == 1
          and restore.count('s->rootHeld=false;') == 1 and 's->rootHeld=true;' not in restore,
          'only recoverable helper refusal reopens cleanup; native root ownership is consumed exactly once')
    failed_restore = restore[restore.index('__except'):]
    check('s->faulted=true;s->uncertain=true;' in failed_restore
          and all(token not in failed_restore for token in ('s->cleanupEntered=false', 's->rootHeld=true', 's->held=false', 'std::memset')),
          'native strong/weak destructor exceptions retain the permanent ledger and never rearm cleanup or the consumed root')
    abort = body(runtime, 'Abort')
    check('returnRestore(l);' in abort and 's->projectile' not in abort
          and 'std::memset' not in abort and 's->uncertain=false' not in abort,
          'trusted abort may abandon queued events but cannot erase helper ownership or uncertain native cleanup')
    check('if(s->cleanupEntered||s->uncertain)returnfalse;' in abort,
          'trusted abort cannot erase or retry an uncertain/reentrant native root cleanup')

    codec = without_comments((ROOT / 'src/enemy_control_monster_net_codec.h').read_text(encoding='utf-8'))
    projectile = without_comments((ROOT / 'src/enemy_control_projectile_native.h').read_text(encoding='utf-8'))
    symbols = {}
    scalar_offsets = set()
    for name in ('kProjectileScalar32', 'kProjectileScalar8'):
        declaration = re.search(r'\b' + name + r'\s*\[[^\]]*\]\s*=\s*\{([^}]+)\}', projectile)
        check(declaration is not None, f'projectile helper declares {name}')
        symbols['kProjectileWords' if name.endswith('32') else 'kProjectileBytes'] = len(declaration.group(1).split(','))
        if name == 'kProjectileScalar32':
            scalar_offsets = {int(value.strip(), 16) for value in declaration.group(1).split(',')}
    check({0xB0, 0xC0, 0xE0}.issubset(scalar_offsets),
          'all three native 504 level-derived descriptor words belong to the explicit typed sample')
    helper = without_comments((ROOT / 'src/enemy_control_projectile_native.cpp').read_text(encoding='utf-8'))
    captured_sample = body(helper, 'CaptureNativeProjectileSample')
    replayed_wrapper = body(helper, 'WithNativeProjectileSampleInPlace')
    replayed_sample = body(helper, 'WithNativeProjectileOutcomeInPlace')
    check('constautooutcome=WithNativeProjectileOutcomeInPlace(j,l,local,muzzle,s,create,user);' in replayed_wrapper
          and 'returnoutcome.cleanupComplete?outcome.created:nullptr;' in replayed_wrapper,
          'Monster legacy replay enters the typed creation helper and exposes its pointer only after descriptor cleanup succeeds')
    check('void*result=j.created;constboolcleaned=RestoreNativeProjectileJournal(j);return{result,cleaned};' in replayed_sample
          and 'return{j.created,false};' in replayed_sample,
          'typed helper preserves actual creation separately from successful or uncertain native descriptor cleanup')
    check('if(!NativeProjectileJournalEmpty(j)||!create||!CanPrepareNativeProjectileSample(l,local,muzzle,s))return{};' in replayed_sample
          and replayed_sample.index('if(!NativeProjectileJournalEmpty(j)') < replayed_sample.index('j.created=create('),
          'an unresolved typed journal prevents another native factory callback for the same retained ownership')
    capture_words = 'for(unsignedi=0;i<kProjectileWords;++i)s.scalar[i]=At<std::uint32_t>(actual,kProjectileScalar32[i]);'
    replay_words = 'for(unsignedi=0;i<kProjectileWords;++i)Put<std::uint32_t>(j.descriptor,kProjectileScalar32[i],s.scalar[i]);'
    check(capture_words in captured_sample and replay_words in replayed_sample
          and replayed_sample.index(replay_words) < replayed_sample.index('j.created=create('),
          'capture keeps host B0/C0/E0 values and persistent replay writes every typed word before the actual factory callback')
    event_bytes = size_expression(codec, 'kEventBytes', symbols)
    header_bytes = size_expression(codec, 'kHeaderBytes', symbols)
    maximum = size_expression(codec, 'kMaxBytes', symbols)
    batch = size_expression(codec, 'kMaxBatch', symbols)
    expected = 8 + 8 + 1 + 128 + symbols['kProjectileWords'] * 4 + 12 + symbols['kProjectileBytes'] + 2 + 64 + 4 + 3
    check(event_bytes == expected and header_bytes == 8 and batch == 1 and header_bytes + event_bytes <= maximum == 512,
          'one complete explicit projectile event fits the unchanged reliable 512-byte envelope')
    writer = body(codec, 'Write')
    check('sizeof(' not in writer and all('sample.' + field in writer for field in
          ('matrix', 'muzzleMatrix', 'scalar', 'random', 'shot', 'flags', 'bytes', 'scalar178', 'target', 'extra', 'lock',
           'hasMuzzle', 'extraObject', 'resourceMask')),
          'codec explicitly emits every typed sample field without native structure padding')
    check('for(autovalue:sample.scalar)Put(out,value,4);' in writer,
          'wire carries every captured scalar word, including 504 native level outcomes, without a static-word filter')
elif args.source:
    raise AssertionError(f'expected runtime source missing: {source}')
else:
    print('runtime fingerprint checks pending: sibling runtime source not present', file=sys.stderr if args.dump_cadence else sys.stdout)

game = Path(args.game) if args.game else dll.parent
cadence = {'clock': 'native update invocations; not physics/render Hz', 'nativeTickHz': None,
           'animationTimeUnit': 'native CAS/MAB timeline units, separate from the update counter', 'assets': []}
if (game / 'Root.cpk').is_file():
    sys.path.insert(0, str(ROOT / 'pylib'))
    import rootcpk
    import sgo
    import mab_legacy
    import cas_legacy
    archive = rootcpk.Game(str(game))

    def native_text(data, at):
        end = at
        while data[end:end + 2] != b'\0\0':
            end += 2
        return data[at:end].decode('utf-16-le')

    def fire_keys(data):
        mab_legacy.mab_layout(data)
        total = struct.unpack_from('<H', data, 0xE)[0]
        first = struct.unpack_from('<I', data, 0x18)[0]
        result = []
        for track_index in range(total):
            track = first + track_index * 16
            keys, _end, name, count = struct.unpack_from('<4i', data, track)
            for key_index in range(count):
                key = track + keys + key_index * 16
                name_offset, time, _zero, params = struct.unpack_from('<ifii', data, key)
                event = native_text(data, key + name_offset)
                if event not in ('fire_begin', 'fire', 'fire_end'):
                    continue
                values = sgo.read(data[key + params:])[1] if params else {}
                result.append({'track': track_index, 'trackName': native_text(data, track + name),
                               'key': key_index, 'keyOffset': key, 'parameterOffset': key + params if params else None,
                               'event': event, 'animationTime': time, 'parameters': values})
        return result

    for name in ('MONSTER501.SGO', 'MONSTER501_KING.SGO'):
        data = archive.read('OBJECT', name)
        values = sgo.load(data=data)
        keys = fire_keys(sgo.read(data)[1]['animation_model'][2])
        fired = [key for key in keys if key['event'] == 'fire']
        check([(key['track'], key['trackName'], key['key'], key['parameters']) for key in fired] ==
              [(48, 'attack_breath_b', 4, {'time': 120}), (49, 'attack_breath_t', 4, {'time': 140})],
              f'{name}: every stock breath trigger and exact native counter count')
        check(abs(fired[0]['animationTime'] - 218.6) < 0.0001 and fired[1]['animationTime'] == 210,
              f'{name}: breath starts at its real delayed MAB key')
        cas_name = values['animation_model'][1].split('/')[-1].upper()
        cas = cas_legacy.cas_layout(archive.read('OBJECT', cas_name))
        attacks = []
        for key in fired:
            clips = [(index, clip) for index, clip in enumerate(cas.canm.clips) if clip.name == key['trackName']]
            rows = [(index, row) for index, row in enumerate(values['attack_setting']) if row[0] == key['trackName']]
            check(len(clips) == 1 and clips[0][1].loop == 0 and clips[0][1].duration == 480
                  and len(rows) == 1 and rows[0][1][1] == 1.0,
                  f'{name}/{key["trackName"]}: one nonloop stock clip and the real attack-table rate')
            duration = key['parameters']['time']
            attacks.append({**key, 'counterNativeTicks': duration, 'emissionEveryNativeTicks': 2,
                            'emissionTickOffsetsOneBased': list(range(2, duration + 1, 2)),
                            'maxFactoryCallsPerFire': duration // 2, 'attackSettingIndex': rows[0][0],
                            'attackSettingRate': rows[0][1][1], 'cas': f'OBJECT/{cas_name}',
                            'clipIndex': clips[0][0], 'clipMabTicks': clips[0][1].duration})
        cadence['assets'].append({'sgo': f'Root.cpk/OBJECT/{name}', 'mab': 'animation_model[2]',
                                  'maxFactoryCallsPerNativeUpdateTick': 1, 'maxCounterNativeTicks': 140,
                                  'absoluteRetriggerCooldownNativeTicks': None, 'fire': attacks})

    for name in ('E504_MONSTERA.SGO', 'E504_MONSTERA_M034_3.SGO'):
        data = archive.read('OBJECT', name)
        values = sgo.load(data=data)
        check(values['bullet_factory'] == 'Volcanicrock01', f'{name}: one stable local factory ordinal zero')
        check(values['bullet_model'] == 'volcanicrock01.mdb', f'{name}: local model resource configuration')
        groups = values['muzzle_group_setting']
        check([row[0] for row in groups] == ['right_high', 'left_high', 'right_low', 'left_low']
              and all(len(row[1]) == 2 for row in groups), f'{name}: four stable groups, each with two real muzzle nodes')
        patterns = []
        expected_patterns = [('bullet_pattern_mine', 3, 15, [4, 3, 2]),
                             ('bullet_pattern_mine2', 4, 15, [6, 4, 3, 1]),
                             ('bullet_pattern_snipe', 3, 15, [4, 4, 4]),
                             ('bullet_pattern_rolling', 3, 15, [2, 2, 2])]
        for pattern, rounds, interval, pellets in expected_patterns:
            row = values[pattern]
            pair = row[2] if isinstance(row[2], list) else [row[2], row[2]]
            check(row[:2] == [rounds, interval] and pair == [pellets[0], pellets[-1]],
                  f'{name}/{pattern}: actual count, native interval and first/last pellet fields')
            patterns.append({'path': pattern, 'fireCount': rounds, 'fireIntervalNativeTicks': interval,
                             'pelletsFirst': pair[0], 'pelletsLast': pair[1], 'pelletsPerWave': pellets,
                             'waveTickOffsetsOneBased': [1 + wave * interval for wave in range(rounds)]})
        keys = fire_keys(sgo.read(data)[1]['animation_model'][2])
        fired = [key for key in keys if key['event'] == 'fire']
        check(len(fired) == 10, f'{name}: all ten stock MAB fire triggers enumerated')
        cas_name = values['animation_model'][1].split('/')[-1].upper()
        cas = cas_legacy.cas_layout(archive.read('OBJECT', cas_name))
        armed = {}
        for key in keys:
            if key['event'] == 'fire_begin':
                armed[key['trackName']] = key['parameters']['bullet_pattern']
            elif key['event'] == 'fire':
                key['pattern'] = armed[key['trackName']]
                selected = key['parameters']['group_list']
                key['groups'] = [row[0] for row in groups] if selected == -1 else selected
                key['activeMuzzles'] = sum(len(row[1]) for row in groups if row[0] in key['groups'])
                pattern = next(row for row in patterns if row['path'] == key['pattern'])
                key['maxFactoryCallsPerNativeUpdateTick'] = key['activeMuzzles'] * max(pattern['pelletsPerWave'])
                key['maxFactoryCallsPerFire'] = key['activeMuzzles'] * sum(pattern['pelletsPerWave'])
                clips = [(index, clip) for index, clip in enumerate(cas.canm.clips) if clip.name == key['trackName']]
                check(len(clips) == 1 and not clips[0][1].loop, f'{name}/{key["trackName"]}: one actual nonloop firing clip')
                key['cas'] = f'OBJECT/{cas_name}'
                key['clipIndex'] = clips[0][0]
                key['clipMabTicks'] = clips[0][1].duration
        peak = max(key['maxFactoryCallsPerNativeUpdateTick'] for key in fired)
        check(peak == 32 and all(key['pattern'] == 'bullet_pattern_mine' for key in fired
                               if key['maxFactoryCallsPerNativeUpdateTick'] == peak),
              f'{name}: actual stock single-action peak is the eight-muzzle mine wave, not forced all-group mine2')
        volumes = {}
        for key in fired:
            volumes[key['trackName']] = volumes.get(key['trackName'], 0) + key['maxFactoryCallsPerFire']
        check(volumes == {'attack_sideSpark_l': 56, 'attack_sideSpark_r': 56, 'attack_spark': 72,
                          'attack_sniperSpark': 24, 'attack_rolling_thorn': 48, 'attack_rolling2_thorn': 96},
              f'{name}: all stock single-action factory-volume upper bounds preserve every MAB fire key')
        cadence['assets'].append({'sgo': f'Root.cpk/OBJECT/{name}', 'mab': 'animation_model[2]',
                                  'muzzleGroups': [{'index': index, 'name': row[0], 'muzzles': len(row[1])}
                                                   for index, row in enumerate(groups)], 'patterns': patterns,
                                  'maxFactoryCallsPerNativeUpdateTick': peak,
                                  'maxFactoryCallsPerStockAction': volumes,
                                  'allGroupsAnyPatternStructuralUpperBound': 48,
                                  'successfulFactoryCallsBoundedByAttempts': True,
                                  'absoluteRetriggerCooldownNativeTicks': None,
                                  'fire': fired, 'timeline': keys})
elif args.dump_cadence:
    raise AssertionError('stock cadence metadata requires the local Root.cpk')

if args.dump_cadence:
    print(json.dumps(cadence, ensure_ascii=True, indent=2))

print('Monster actual projectile native audit:', checks, 'checks passed', file=sys.stderr if args.dump_cadence else sys.stdout)
