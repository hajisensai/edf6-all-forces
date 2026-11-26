"""Optional check of the lock registry's node count, which bounds the plugins' registry walks (src/heli.cpp ForEachEnemyOf /
VisitLockPoints, autoturret/src/plugin.cpp RefreshWorld: common/edf/world_tables.h kRegSize) instead of a fixed node cap a battle passes.

    python tests/lock_registry_native_audit.py <EDF.dll>

Pass an installed EDF.dll (TimeDateStamp 0x678CCB46); without one it exits 77 (skipped). Static only: reads the DLL's
bytes (pefile), never loads or runs it. What it holds: the registry's add (0x22E210, called from AddLockTarget 0x547A50
with *(img+0x20B2AB0)) takes the list at registry+8 (lea rbx,[r13+8]; mov r13,[rbx]: the head), checks [rbx+8] against
the MSVC list's max_size (0x0666666666666666 for a 0x28-byte node), bumps it (inc qword [rbx+8]) and links the new node
before the head: registry+8 is the head, registry+0x10 the node count -- an MSVC std::list, no fixed capacity.
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

# Both plugins read the same fields through one header (common/edf/world_tables.h LockList): the head at +8 and the count
# at +0x10, no fixed node cap.
tables = (ROOT / 'common/edf/world_tables.h').read_text(encoding='utf-8')
check('kLockRegistry=0x20B2AB0,kRegList=0x8,kRegSize=0x10;' in tables, 'world_tables.h: the head at registry+8, the count at +0x10')
check(len(re.findall(r'nodes=At<std::uint64_t>\(registry,kRegSize\);', tables)) == 1, 'world_tables.h: LockList reads the count')
check('kMaxNodes' not in tables, 'world_tables.h: no fixed node cap')
for path in ('src/heli.cpp', 'autoturret/src/turret.h', 'autoturret/src/plugin.cpp'):
    text = (ROOT / path).read_text(encoding='utf-8')
    check('kMaxNodes' not in text and '0x20B2AB0' not in text, f'{path}: no registry address or node cap of its own')
# The live walks and both snapshots are bounded by the registry count.
for path, lists, walks in (('src/heli.cpp', 3, 1), ('autoturret/src/plugin.cpp', 1, 0), ('common/edf/enemy_points.h', 0, 1)):
    text = (ROOT / path).read_text(encoding='utf-8')
    check(text.count('edf::LockList(image,head,nodes)') == lists and
          len(re.findall(r'node!=head && n<nodes;', text)) == walks, f'{path}: every registry walk bounded by the count')
plugin = (ROOT / 'autoturret/src/plugin.cpp').read_text(encoding='utf-8')
check('enemypoints::Collect(head,nullptr,worldPoints,nodes)' in plugin, 'autoturret/src/plugin.cpp: the snapshot given the count')
heli = (ROOT / 'src/heli.cpp').read_text(encoding='utf-8')
check(heli.count('enemypoints::Walk(') == 1 and 'enemypoints::Walk(head,relation,v,static_cast<F&&>(f),nodes);' in heli,
      'src/heli.cpp: the shared walk given the registry count')
check(heli.count('enemypoints::Collect(') == 1 and 'enemypoints::Collect(head,nullptr,stepRegistry,nodes)' in heli,
      'src/heli.cpp: the AI scene-step snapshot given the registry count')

print(f'lock_registry_native_audit: {checks} checks passed')
