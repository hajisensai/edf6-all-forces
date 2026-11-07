"""Optional check of the game lists' own node counts, which bound the plugins' walks over them instead of fixed caps a
battle passes: the lock registry (src/heli.cpp ForEachEnemyOf / VisitLockPoints, autoturret/src/plugin.cpp RefreshWorld:
kRegSize) and the dropped item boxes (src/npcai.cpp Boxes: kBoxCount).

    python tests/list_count_native_audit.py <EDF.dll>

Pass an installed EDF.dll (TimeDateStamp 0x678CCB46); without one it exits 77 (skipped). Static only: reads the DLL's
bytes (pefile), never loads or runs it. What it holds: the registry's add (0x22E210, called from AddLockTarget 0x547A50
with *(img+0x20B2AB0)) takes the list at registry+8 (lea rbx,[r13+8]; mov r13,[rbx]: the head), checks [rbx+8] against
the MSVC list's max_size (0x0666666666666666 for a 0x28-byte node), bumps it (inc qword [rbx+8]) and links the new node
before the head: registry+8 is the head, registry+0x10 the node count -- an MSVC std::list, no fixed capacity. The drop
manager's boxes: the collect pass (0x2CA3FA) walks the list at +0xDE0 and its erase unlinks a node and decrements +0xDE8
(0x2CA446): the head at +0xDE0, the count at +0xDE8.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

if sys.platform != 'win32' or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass a supported EDF.dll path')
    raise SystemExit(77)
import pefile  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll TimeDateStamp {pe.FILE_HEADER.TimeDateStamp:#x}')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def at(rva: int, hexbytes: str, why: str) -> None:
    want = bytes.fromhex(hexbytes)
    check(img[rva:rva + len(want)] == want, f'{rva:#x}: {why}')


at(0x22E2DA, '498d5d08', 'lea rbx,[r13+8]: the list at registry+8')
at(0x22E2DE, '4c8b2b', 'mov r13,[rbx]: its head')
at(0x22E2E1, '48b86666666666666606', 'mov rax,0x0666666666666666: max_size of a list of 0x28-byte nodes')
at(0x22E2EB, '48394308', 'cmp [rbx+8],rax: the count at registry+0x10 against it')
at(0x22E330, '48ff4308', 'inc qword [rbx+8]: one node more')
at(0x22E338, '4c8929', 'mov [rcx],r13: the new node\'s next is the head')
at(0x22E33F, '49894d08', 'mov [r13+8],rcx: linked before the head (push_back)')

at(0x2CA3FA, '488b86e00d0000', "mov rax,[rsi+0xDE0]: the box list's head")
at(0x2CA42D, '488b4308', "mov rax,[rbx+8]: the node's prev (unlink)")
at(0x2CA446, '48ff8ee80d0000', 'dec qword [rsi+0xDE8]: one box node fewer')
npcai = (ROOT / 'src/npcai.cpp').read_text(encoding='utf-8')
check('kBoxList=0xDE0' in npcai and 'constexpr std::size_t kBoxCount=kBoxList+8;' in npcai, "npcai.cpp: the box list's head and count")
check('kMaxBoxes' not in npcai and 'guard<count;' in npcai, 'npcai.cpp: the box walk bounded by the count, no fixed cap')

# The plugins read the same fields: the head at +8 and the walk bounded by the count at +0x10, no fixed node cap.
for path in ('src/heli.cpp', 'autoturret/src/turret.h'):
    text = (ROOT / path).read_text(encoding='utf-8')
    check('kRegList=0x8' in text, f'{path}: the head at registry+8')
    check('constexpr std::size_t kRegSize=0x10;' in text, f'{path}: the count at registry+0x10')
    check('kMaxNodes' not in text, f'{path}: no fixed node cap')
for path, walks in (('src/heli.cpp', 2), ('autoturret/src/plugin.cpp', 1)):
    text = (ROOT / path).read_text(encoding='utf-8')
    check(len(re.findall(r'const std::uint64_t nodes=At<std::uint64_t>\(registry,kRegSize\);', text)) == walks and
          len(re.findall(r'node!=head && n<nodes;', text)) == walks, f'{path}: every registry walk bounded by the count')

print(f'list_count_native_audit: {checks} checks passed')
