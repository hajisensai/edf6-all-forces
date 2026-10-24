"""Read-only native ABI/profile audit for the public InSession lifecycle guard.

This checks the real EDF.dll and binds every production offset and the entire
native predicate profile to that image. Malformed/absent runtime data is covered
by the production-code C++ fixture; this script never loads or runs a game DLL.
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
m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
m.detail = True
checks = 0


def code(at: int, size: int):
    return {i.address: i for i in m.disasm(p.get_data(at, size), at)}


def expect(rows, at: int, text: str):
    global checks
    i = rows[at]
    actual = f'{i.mnemonic} {i.op_str}'.rstrip()
    assert actual == text, f'{at:X}: {actual!r} != {text!r}'
    checks += 1


def displacement(i) -> int:
    return next(o.mem.disp for o in i.operands if o.type == capstone.x86.X86_OP_MEM)


# This entire 43-byte function is the original online predicate. It ignores RCX;
# its first load comes from a process-lifetime singleton that may still be null.
native = code(0x7748F0, 0x2B)
expect(native, 0x7748F0, 'mov rax, qword ptr [rip + 0x193df99]')
expect(native, 0x7748F7, 'mov ecx, dword ptr [rax + 0x38]')
expect(native, 0x7748FA, 'cmp ecx, -1')
expect(native, 0x7748FD, 'jne 0x774902')
expect(native, 0x7748FF, 'xor al, al')
expect(native, 0x774901, 'ret')
expect(native, 0x774902, 'mov rax, qword ptr [rax + 0x20]')
expect(native, 0x774906, 'mov rcx, qword ptr [rax + rcx*8]')
expect(native, 0x77490A, 'mov rdx, qword ptr [rcx + 0x10]')
expect(native, 0x77490E, 'movsxd rax, dword ptr [rdx + 8]')
expect(native, 0x774912, 'cmp dword ptr [rax + rdx + 0x68], 0')
expect(native, 0x774917, 'setne al')
expect(native, 0x77491A, 'ret')
slot = native[0x7748F0].address + native[0x7748F0].size + displacement(native[0x7748F0])
assert slot == 0x20B2890
checks += 1

# Constructor and publisher establish why first-call absence is temporary.
ctor = code(0xD62C0, 0x1EF)
expect(ctor, 0xD631B, 'mov qword ptr [r14 + 0x20], r15')
expect(ctor, 0xD631F, 'mov qword ptr [r14 + 0x28], r15')
expect(ctor, 0xD6323, 'mov qword ptr [r14 + 0x30], r15')
expect(ctor, 0xD64A0, 'mov dword ptr [r14 + 0x38], 0xffffffff')
publish = code(0x7C0C35, 0x3D)
expect(publish, 0x7C0C41, 'jne 0x7c0c72')
expect(publish, 0x7C0C4A, 'mov ecx, 0x150a0')
expect(publish, 0x7C0C61, 'call 0xd62c0')
for at in (0x7C0C35, 0x7C0C43, 0x7C0C6B):
    i = publish[at]
    assert i.address + i.size + displacement(i) == slot
    checks += 1
expect(publish, 0x7C0C43, 'mov qword ptr [rip + 0x18f1c46], rbx')
expect(publish, 0x7C0C6B, 'mov qword ptr [rip + 0x18f1c1e], rax')

# The modes container is based at GS+18; its size at container+18 is a qword.
# No fixed maximum mode count is part of the session predicate.
load = code(0xDAC64, 0x171)
expect(load, 0xDAC64, 'lea rbx, [r15 + 0x18]')
expect(load, 0xDACD7, 'mov rax, qword ptr [rbx + 0x10]')
expect(load, 0xDACDB, 'cmp qword ptr [rbx + 0x18], rax')
expect(load, 0xDAD97, 'mov rcx, qword ptr [rbx + 0x18]')
expect(load, 0xDAD9B, 'mov rax, qword ptr [rbx + 8]')
expect(load, 0xDAD9F, 'lea rdx, [rax + rcx*8]')
expect(load, 0xDADA7, 'mov qword ptr [rbx + 0x18], rax')

source = (Path(__file__).resolve().parents[1] / 'src' / 'netprobe.cpp').read_text(encoding='utf-8-sig')
expected = {
    'kOnline': 0x7748F0,
    'kGameStatus': slot,
    'kModeTable': displacement(native[0x774902]),
    'kModeCount': displacement(load[0xDAC64]) + displacement(load[0xDAD97]),
    'kCurrentMode': displacement(native[0x7748F7]),
    'kModeRaw': displacement(native[0x77490A]),
    'kRawRelative': displacement(native[0x77490E]),
    'kRawOnline': displacement(native[0x774912]),
}


def production_profile(text: str):
    for name, want in expected.items():
        match = re.search(r'\b' + name + r'\s*=\s*(0x[0-9A-Fa-f]+|\d+)', text)
        assert match and int(match[1], 0) == want, f'{name}: production/native layout mismatch'
    match = re.search(r'\bkOnlineCode\s*\[\s*\]\s*=\s*\{([^}]+)\}', text)
    assert match, 'missing production complete online-predicate profile'
    blob = bytes(int(n.strip(), 0) for n in match[1].split(',') if n.strip())
    assert len(blob) == 0x2B and blob == p.get_data(expected['kOnline'], 0x2B), 'production online code profile differs from native'


production_profile(source)
checks += len(expected) + 1
# Mutate the actual production declarations in memory and use the same verifier.
# This proves the audit is not only comparing independent handwritten copies.
for name in expected:
    match = re.search(r'\b' + name + r'\s*=\s*(0x[0-9A-Fa-f]+|\d+)', source)
    assert match
    begin, end = match.span(1)
    changed = source[:begin] + hex(int(match[1], 0) ^ 1) + source[end:]
    rejected = False
    try:
        production_profile(changed)
    except AssertionError:
        rejected = True
    assert rejected, f'changed production layout was admitted: {name}'
    checks += 1
match = re.search(r'\bkOnlineCode\s*\[\s*\]\s*=\s*\{\s*(0x[0-9A-Fa-f]+)', source)
assert match
begin, end = match.span(1)
changed = source[:begin] + hex(int(match[1], 0) ^ 1) + source[end:]
rejected = False
try:
    production_profile(changed)
except AssertionError:
    rejected = True
assert rejected, 'changed production native profile was admitted'
checks += 1
print(f'PASS: {checks} native session lifecycle/profile contracts')
