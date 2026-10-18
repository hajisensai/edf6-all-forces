"""Read-only PE audit of what src/killcount.cpp (the EDF4.1 kill counters in the battle record) rests on, on the real
EDF.dll (docs/kill-counter-re.md).

python tests/killcount_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input (or capstone) returns skip code 77.

Holds, against the DLL itself:
  - every byte signature src/killcount.cpp checks before it patches (read out of the source, so the two cannot drift);
  - the battle record's constructor 0x893C40 is reached only from 0x893BF3 (its allocation 0x893BD0);
  - its fill 0x893CD7..0x8947C3 decodes (the plugin's own decoder, re-done here) to 90 rows at rbp+0x270, the last one
    ending at rbp+0x810, where the frame's security cookie is (0x893C69): the table cannot grow in place;
  - in the whole constructor the table is read only at the sites the plugin moves (the lea at 0x8947C9 and the four
    [rbp+r13*8+...] reads), and both loops end at the 0x5A compares it rewrites;
  - the kill's tally (0x5499F0) reads game_object_kill_counter and adds through 0xCF640 on [0x20B2890]+0x14B50; 0xCF640
    adds only to a variable the map holds (find == end: nothing), the store the page reads too;
  - TROPHY.DAT (0xCFC50 / 0xD00B0): 200 event bytes, then the variables' values in map order, by position.
"""
from __future__ import annotations

from pathlib import Path
import re
import struct
import sys

if not __debug__:
    raise RuntimeError('Run without -O')
if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
try:
    import capstone
except ImportError:
    print('SKIP: capstone not installed')
    raise SystemExit(77)
import pefile

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

source = (ROOT / 'src' / 'killcount.cpp').read_text(encoding='utf-8')
arrays = {name: bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', source)}
consts = {name: int(v, 16) for name, v in re.findall(r'\b(k\w+)=0x([0-9A-Fa-f]+)\b', source)}
lists = {name: [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', body)]
         for name, body in re.findall(r'\b(k\w+)\[\]=\{(0x[^}]*)\}', source)}

where = {'kRowPointerSig': [consts['kRowPointer']], 'kBound1Sig': [consts['kBound1']], 'kRowNameSig': [consts['kRowName']],
         'kRowScaleSig': lists['kRowScale'], 'kBound2Sig': [consts['kBound2']], 'kRowFormatSig': [consts['kRowFormat']],
         'kCtorSig': [consts['kCtor']], 'kStoreSig': [consts['kStoreUse']]}
assert set(arrays) == set(where), sorted(set(arrays) ^ set(where))
for name, rvas in where.items():
    for rva in rvas:
        assert img[rva:rva + len(arrays[name])] == arrays[name], (name, hex(rva), img[rva:rva + len(arrays[name])].hex())

ctor, call = consts['kCtor'], consts['kCtorCall']
assert ctor == 0x893C40 and call == 0x893BF3


def calls_to(target: int) -> list[int]:
    out = []
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    for m in re.finditer(rb'\xE8', img[lo:hi]):
        at = lo + m.start()
        if at + 5 + struct.unpack_from('<i', img, at + 1)[0] == target:
            out.append(at)
    return out


assert calls_to(ctor) == [call], [hex(c) for c in calls_to(ctor)]


def function(begin: int, end: int) -> list:
    return list(md.disasm(img[begin:end], begin))


ins = function(ctor, 0x895EC7)
text = {i.address: f'{i.mnemonic} {i.op_str}' for i in ins}
assert text[0x893C69] == 'mov qword ptr [rbp + 0x810], rax', text[0x893C69]   # the cookie, right after the table

# the fill, decoded as src/killcount_logic.h ParseRows does
rows: dict[int, dict] = {}
rax = xmm0 = None
for i in function(consts['kFillBegin'], consts['kFillEnd']):
    s = f'{i.mnemonic} {i.op_str}'
    m = re.fullmatch(r'lea rax, \[rip \+ (0x[0-9a-f]+)\]', s)
    if m:
        rax = i.address + i.size + int(m.group(1), 16)
        continue
    m = re.fullmatch(r'movss xmm0, dword ptr \[rip \+ (0x[0-9a-f]+)\]', s)
    if m:
        xmm0 = struct.unpack_from('<f', img, i.address + i.size + int(m.group(1), 16))[0]
        continue
    m = re.fullmatch(r'mov (qword|byte|dword) ptr \[rbp \+ (0x[0-9a-f]+)\], (rax|0x[0-9a-f]+|\d+)', s) or \
        re.fullmatch(r'(movss) dword ptr \[rbp \+ (0x[0-9a-f]+)\], (xmm0)', s)
    assert m, (hex(i.address), s)
    off = int(m.group(2), 16) - 0x270
    row, field = divmod(off, 16)
    r = rows.setdefault(row, {})
    if field == 0:
        assert m.group(3) == 'rax'
        end = img.index(b'\0\0', rax)
        while (end - rax) % 2:
            end = img.index(b'\0\0', end + 1)
        r['name'] = img[rax:end].decode('utf-16le')
    elif field == 8:
        r['format'] = int(m.group(3), 0)
    elif field == 12:
        r['scale'] = xmm0 if m.group(3) == 'xmm0' else struct.unpack('<f', struct.pack('<I', int(m.group(3), 0)))[0]
    else:
        raise AssertionError((hex(i.address), s))
assert sorted(rows) == list(range(90)), sorted(rows)
assert all(set(r) == {'name', 'format', 'scale'} for r in rows.values())
names = [rows[i]['name'] for i in range(90)]
assert len(set(names)) == 90 and names[0] == 'FrameCount' and names[-1] == 'HornetNestSnallKillCount', names
assert 0x270 + 16 * 90 == 0x810
kills = [n for n in names if n.endswith('KillCount')]
assert all(rows[names.index(n)]['format'] == 0 and rows[names.index(n)]['scale'] == 1.0 for n in kills)

# the table's every read in the constructor: the sites the plugin moves (and the fill's own stores)
reads = sorted(a for a, s in text.items() if re.search(r'\[rbp \+ r13\*8 \+ 0x2[78]', s) or s.startswith('lea r14, [rbp + 0x27c]'))
moved = sorted([consts['kRowPointer'], consts['kRowName'], *lists['kRowScale'], consts['kRowFormat']])
assert reads == moved, ([hex(a) for a in reads], [hex(a) for a in moved])
in_table = [a for a, s in text.items() if not consts['kFillBegin'] <= a < consts['kFillEnd'] and a not in moved
            and any(f'[rbp + {hex(o)}]' in s for o in range(0x270, 0x810))]
assert in_table == [], [(hex(a), text[a]) for a in in_table]
assert text[consts['kBound1']] == 'cmp r12d, 0x5a' and text[consts['kBound2']] == 'cmp eax, 0x5a'
assert text[consts['kBound1'] + 4].startswith('jae') and text[consts['kBound2'] + 3].startswith('jb')
assert sum(1 for s in text.values() if re.fullmatch(r'cmp \w+, 0x5a', s)) == 2

# the tally: the name read from the SGO, added through 0xCF640 on the store
tally = {i.address: f'{i.mnemonic} {i.op_str}' for i in function(0x5499F0, 0x549BCD)}
lea = next(a for a, s in tally.items() if s.startswith('lea rcx, [rip + '))
target = lea + 7 + struct.unpack_from('<i', img, lea + 3)[0]
assert img[target:target + 50].decode('utf-16le').startswith('game_object_kill_counter')
assert tally[0x549B49] == 'lea rcx, [rbx + 0x14b50]' and tally[0x549B50] == 'call 0xcf640'
add = {i.address: f'{i.mnemonic} {i.op_str}' for i in function(0xCF640, 0xCF68C)}
assert add[0xCF655] == 'call 0xcfab0' and add[0xCF65F] == 'cmp rax, qword ptr [rbx]' and add[0xCF662].startswith('je')
assert add[0xCF66A] == 'add dword ptr [rax + 0x44], edi'
# TROPHY.DAT: the event bytes up to 0xC8, then the values in map order from +0xC8 (save) and back (load)
save = {i.address: f'{i.mnemonic} {i.op_str}' for i in function(0xCFC50, 0xCFD7D)}
assert save[0xCFC7B] == 'mov esi, 0xc8' and save[0xCFCD5] == 'lea r8, [rbp + 0xc8]' and save[0xCFCE0] == 'mov ecx, dword ptr [rax + 0x44]'
load = {i.address: f'{i.mnemonic} {i.op_str}' for i in function(0xD00B0, 0xD0270)}
assert load[0xD0179] == 'lea r8, [rsi + 0xc8]' and load[0xD0187] == 'mov dword ptr [rcx + 0x44], eax'
print('killcount native audit ok: 90 rows, the cookie at +0x810, the moved reads', ', '.join(hex(a) for a in moved))
