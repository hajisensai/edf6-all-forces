"""Read-only native contracts for NPC mech aiming-shape setup; never runs game code.

The production repair binds the configured locator before invoking the existing
63AD50 aimer. This audit checks the exact scalar/array setup, borrowed pair
layout, native vector ownership, and the fire consumer on the supported image.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O: contract assertions must execute')

import capstone
import pefile

p = pefile.PE(sys.argv[1], fast_load=True)
assert p.FILE_HEADER.TimeDateStamp == 0x678CCB46 and p.FILE_HEADER.Machine == 0x8664
base = p.OPTIONAL_HEADER.ImageBase
m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
m.detail = True
checks = 0


def code(rva: int, size: int):
    return {i.address: i for i in m.disasm(p.get_data(rva, size), rva)}


def expect(rows, at: int, text: str):
    global checks
    instruction = rows[at]
    actual = f'{instruction.mnemonic} {instruction.op_str}'.rstrip()
    assert actual == text, f'{at:X}: {actual!r} != {text!r}'
    checks += 1


def rip_target(instruction) -> int:
    mem = next(o.mem for o in instruction.operands
               if o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP)
    return instruction.address + instruction.size + mem.disp


# Check the actual production admission bytes, not a second handwritten copy.
# In particular, Capstone's push-rbx mnemonic hides its optional REX prefix.
source = (Path(__file__).resolve().parents[1] / 'src' / 'npcpost.cpp').read_text(encoding='utf-8-sig')


def source_helper_profile(text: str):
    field = re.search(r'\bkMechAimShapes\s*=\s*(0x[0-9A-Fa-f]+)', text)
    assert field and int(field[1], 16) == 0x2010, 'kMechAimShapes: native vector offset mismatch'
    for rva_name, sig_name, expected_rva in (
            ('kMechFindLocator', 'kMechFindLocatorSig', 0x6BADD0),
            ('kMechResizeShapes', 'kMechResizeShapesSig', 0x648D00)):
        rva_match = re.search(r'\b' + rva_name + r'\s*=\s*(0x[0-9A-Fa-f]+)', text)
        sig_match = re.search(r'\b' + sig_name + r'\[\]\s*=\s*\{([^}]+)\}', text)
        assert rva_match and sig_match, f'missing production profile: {rva_name}'
        rva = int(rva_match[1], 16)
        signature = bytes(int(number.strip(), 0) for number in sig_match[1].split(','))
        assert rva == expected_rva, f'{rva_name}: unexpected native entry {rva:X}'
        assert p.get_data(rva, len(signature)) == signature, f'{sig_name}: production admission bytes mismatch'


source_helper_profile(source)
checks += 3
# Exercise the very same source parser with one production byte and one RVA bit
# changed in memory. No repository or game file is modified by this negative.
for token in ('kMechFindLocatorSig', 'kMechResizeShapesSig', 'kMechFindLocator', 'kMechResizeShapes', 'kMechAimShapes'):
    if token.endswith('Sig'):
        match = re.search(r'\b' + token + r'\[\]\s*=\s*\{(0x[0-9A-Fa-f]+)', source)
    else:
        match = re.search(r'\b' + token + r'\s*=\s*(0x[0-9A-Fa-f]+)', source)
    assert match
    begin, end = match.span(1)
    changed = source[:begin] + hex(int(match[1], 16) ^ 1) + source[end:]
    rejected = False
    try:
        source_helper_profile(changed)
    except AssertionError:
        rejected = True
    assert rejected, f'production profile mutation was admitted: {token}'
    checks += 1
# The production visitor profile is also bound directly to actual table slots.
block = re.search(r'kMechVariantTables\[\]\s*=\s*\{(.*?)\n\};', source, re.S)
assert block
rows = re.findall(r'\{(0x[0-9A-Fa-f]+),\{([^}]+)\}\}', block[1])
assert len(rows) == 6
for table_text, functions_text in rows:
    table = int(table_text, 16)
    targets = [int(value.strip(), 0) for value in functions_text.split(',')]
    assert len(targets) == 3
    for tag, target in enumerate(targets):
        assert struct.unpack('<Q', p.get_data(table + tag * 8, 8))[0] - base == target
        checks += 1


# Native slot 6: lookup the SGO member, dispatch array versus scalar, preserve
# the array's original seat order, and bind MAB table 0 on ModelControl (+E40).
setup = code(0x642970, 0x350)
key = rip_target(setup[0x6429EE])
assert p.get_data(key, 2 * len('begaruta_aiming_shape\0')).decode('utf-16le') == 'begaruta_aiming_shape\0'
checks += 1
expect(setup, 0x6429CF, 'lea r15, [rsi + 0x100]')
expect(setup, 0x642A90, 'cmp byte ptr [rsp + 0x20], 3')
expect(setup, 0x642A95, 'jne 0x642bcc')
expect(setup, 0x642ABD, 'mov edx, dword ptr [rsp + 0x28]')
expect(setup, 0x642AC1, 'lea rcx, [rsi + 0x2010]')
expect(setup, 0x642AC8, 'call 0x648d00')
expect(setup, 0x642B16, 'mov dword ptr [rsp + 0x58], ebx')
expect(setup, 0x642B6E, 'movsxd rdx, ebx')
expect(setup, 0x642B71, 'shl rdx, 4')
expect(setup, 0x642B75, 'add rdx, qword ptr [rsi + 0x2010]')
expect(setup, 0x642B7C, 'mov r9, qword ptr [rsp + 0x40]')
expect(setup, 0x642B81, 'xor r8d, r8d')
expect(setup, 0x642B84, 'lea rcx, [rsi + 0xe40]')
expect(setup, 0x642B8B, 'call 0x6badd0')
expect(setup, 0x642BB7, 'inc ebx')
expect(setup, 0x642BCC, 'mov edx, 1')
expect(setup, 0x642BD1, 'lea rcx, [rsi + 0x2010]')
expect(setup, 0x642BD8, 'call 0x648d00')
expect(setup, 0x642C03, 'mov rdx, qword ptr [rsi + 0x2010]')
expect(setup, 0x642C0A, 'lea rcx, [rsi + 0xe40]')
expect(setup, 0x642C11, 'call 0x6badd0')
# Alternative 2 is an SGO/DSGO node. These visitors are the exact routines used
# by this native setup; the fourth table entry is data, not another callable tag.
for table, target in ((0x17A1CE0, 0x267880), (0x179EEF0, 0x240AF0),
                      (0x179EAF0, 0x2390D0), (0x179EAD8, 0x239090)):
    assert struct.unpack('<Q', p.get_data(table + 16, 8))[0] - base == target
    checks += 1

# Semantic node types used by the production validation: string=1, array=3.
assert p.get_data(0x1F57030 + 8, 1) == b'\x01'
assert p.get_data(0x1F57030 + 24, 1) == b'\x03'
checks += 2

# Bind returns bool and initializes a plain borrowed pair, not shared/weak refs.
# It resolves the MAB locator's parent bone on the same ModelControl instance.
bind = code(0x6BADD0, 0x13C)
expect(bind, 0x6BADEC, 'mov rsi, r9')
expect(bind, 0x6BADEF, 'mov rdi, rdx')
expect(bind, 0x6BADF2, 'mov rbp, rcx')
expect(bind, 0x6BADF8, 'mov qword ptr [rdx + 8], r15')
expect(bind, 0x6BADFC, 'mov qword ptr [rdx], r15')
expect(bind, 0x6BADFF, 'mov rax, qword ptr [rcx + 0x280]')
expect(bind, 0x6BAE0D, 'mov eax, r8d')
expect(bind, 0x6BAE21, 'call 0x68710')
expect(bind, 0x6BAE28, 'je 0x6baef0')
expect(bind, 0x6BAE3B, 'call 0x68740')
expect(bind, 0x6BAE86, 'call 0x11002a0')
expect(bind, 0x6BAED2, 'cmp ebx, -1')
expect(bind, 0x6BAED5, 'je 0x6baef0')
expect(bind, 0x6BAEE0, 'call 0x1100280')
expect(bind, 0x6BAEE5, 'mov qword ptr [rdi + 8], rax')
expect(bind, 0x6BAEE9, 'mov qword ptr [rdi], rsi')
expect(bind, 0x6BAEEC, 'mov al, 1')
expect(bind, 0x6BAEF0, 'xor al, al')
assert not any(i.mnemonic.startswith('lock ') for i in bind.values())
checks += 1

# The native vector has begin/end/capacity pointers and 16-byte trivial entries.
# Growth does not own locator/bone references; the game allocator owns its buffer.
resize = code(0x648D00, 0x7A)
expect(resize, 0x648D04, 'mov rax, qword ptr [rcx + 8]')
expect(resize, 0x648D0B, 'mov r10, qword ptr [rcx]')
expect(resize, 0x648D14, 'sar r8, 4')
expect(resize, 0x648D24, 'mov qword ptr [rcx + 8], rdx')
expect(resize, 0x648D2F, 'mov rcx, qword ptr [rcx + 0x10]')
expect(resize, 0x648D47, 'call 0x634ee0')
expect(resize, 0x648D60, 'mov qword ptr [rax], rcx')
expect(resize, 0x648D63, 'mov qword ptr [rax + 8], rcx')
expect(resize, 0x648D67, 'add rax, 0x10')
grow = code(0x634EE0, 0x17E)
expect(grow, 0x634F9B, 'call 0x12d85b0')
expect(grow, 0x634FF0, 'movups xmm0, xmmword ptr [rax]')
expect(grow, 0x635037, 'call 0x12d85ec')
expect(grow, 0x63503C, 'mov qword ptr [r14], rdi')
expect(grow, 0x635046, 'mov qword ptr [r14 + 8], rsi')
expect(grow, 0x63504E, 'mov qword ptr [r14 + 0x10], rax')
destroy = code(0x637DE0, 0xF0)
expect(destroy, 0x637DF2, 'mov rcx, qword ptr [rcx + 0x140]')
expect(destroy, 0x637DFE, 'mov rdx, qword ptr [rdi + 0x150]')
expect(destroy, 0x637E31, 'call 0x12d85ec')
expect(code(0x638090, 0x1A0), 0x63811E, 'lea rcx, [rdi + 0x1ed0]')

# The unchanged native aimer uses the per-seat locator before bone fallback.
fire = code(0x63AD50, 0x890)
expect(fire, 0x63AE99, 'mov rdx, qword ptr [r15 + 0x2010]')
expect(fire, 0x63AEA0, 'mov rax, qword ptr [r15 + 0x2018]')
expect(fire, 0x63AEAD, 'sar rax, 4')
expect(fire, 0x63AEB4, 'jbe 0x63aed2')
expect(fire, 0x63AEB6, 'shl rcx, 4')
expect(fire, 0x63AEBD, 'cmp qword ptr [rcx], 0')
expect(fire, 0x63AEC1, 'je 0x63aed2')
expect(fire, 0x63AEC7, 'call 0x6bb5a0')
expect(fire, 0x63AED2, 'mov rax, qword ptr [r15 + 0x2008]')
expect(fire, 0x63AED9, 'movups xmm0, xmmword ptr [rax + 0xb0]')
print(f'PASS: {checks} native NPC mech aiming-shape contracts')
