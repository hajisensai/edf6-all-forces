"""Read-only EDF.dll audit of the round model contract (src/ifc_model.h; test hub report #6, the Proteus shield crash).

  python tests/ifc_model_native_audit.py [EDF.dll]     77 (skipped) without the supported EDF.dll

Checks, on the real code, each link of the chain the crash went through and the fix relies on:
  - BarrierBullet01's ctor builds its model from InitParam +0x1B0 (0x28FCEC / 0x28FD02 -> 0x6BB890), and 0x6BB890 throws
    (0x6BB927 -> 0x6BF033 -> _CxxThrowException 0x12DA768, returning to 0x6BF044: the crash stack's EDF+6BF044) when
    that variant is valueless;
  - the InitParam ctor leaves it valueless (0x100321) and the IFC's config 0x2B5F40 never writes IFC +0x1F0..+0x201,
    while a weapon's init fills it with its SGO's animation_model (0x68DA8A lookup, 0x68DAD5 into weapon +0x9B0);
  - the IFC hands IFC +0x40 to the factory (0x2B9F55), so the slot is IFC +0x1F0 (ifc_model.h kIfcModel);
  - the DemoIndirectFire reads its SGO root at +0x100 with the visitor tables 0x179EBA8 (find) / 0x179EAF0 (get), whose
    first three entries are code and the fourth is not; the node-reference get (alternative 2, 0x2390D0) writes
    alternative 2 and its destructor (0x89940) is a bare ret;
  - the plugin's signature table (src/jet_bay.cpp kIfcModelSigs) matches the DLL byte for byte.
"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import gamedir

path = Path(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else Path(gamedir.find_or_dev()) / 'EDF.dll'
if not path.exists():
    print('SKIP: supported EDF.dll needed for the round model audit')
    raise SystemExit(77)
import capstone
import pefile

pe = pefile.PE(str(path), fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the supported build')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
TEXT = range(text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
failures = []


def check(ok: bool, what: str) -> None:
    if not ok:
        failures.append(what)


def ins(rva: int):
    return next(md.disasm(img[rva:rva + 16], rva))


def text_of(rva: int) -> str:
    i = ins(rva)
    return f'{i.mnemonic} {i.op_str}'


# BarrierBullet01's ctor and the throw.
check(text_of(0x28FCEC) == 'lea r8, [rsi + 0x1b0]', 'barrier ctor: r8 = InitParam + 0x1B0')
check(text_of(0x28FD02) == 'call 0x6bb890', 'barrier ctor: builds its model with 0x6BB890')
check(text_of(0x6BB91E) == 'movzx eax, word ptr [r8 + 0x10]' and text_of(0x6BB927) == 'je 0x6bf033',
      '0x6BB890 tests the variant\'s alternative first and branches away when valueless (r13w = 0xFFFF)')
check(text_of(0x6BF03F) == 'call 0x12da768' and 0x6BF03F + ins(0x6BF03F).size == 0x6BF044,
      'the valueless branch throws (_CxxThrowException), returning to 0x6BF044 (the crash stack)')
branches = [i.address for i in md.disasm(img[0x6BB890:0x6BF033], 0x6BB890) if i.op_str == '0x6bf033']
check(branches == [0x6BB927], 'only the model variant\'s valueless test reaches that throw')

# The InitParam's slot: left valueless, never filled by the IFC, filled by a weapon.
check(text_of(0x100321) == 'mov word ptr [rbx + 0x1c0], di', 'InitParam ctor: +0x1B0 valueless (+0x1C0 = 0xFFFF)')
check(text_of(0x2B9F55) == 'lea r9, [r14 + 0x40]' and text_of(0x2B9F68) == 'call 0x1194280',
      'the IFC fires with InitParam = IFC + 0x40')
writes = []
for i in md.disasm(img[0x2B5F40:0x2B7B90], 0x2B5F40):
    if re.search(r'\[r14 \+ 0x(1f[0-9a-f]|20[01])\]', i.op_str):
        writes.append(f'{i.address:#x} {i.mnemonic} {i.op_str}')
check(not writes, f'the IFC config never touches IFC +0x1F0..+0x201: {writes}')
check(text_of(0x68DA8A).startswith('lea r8, [rip + ') and text_of(0x68DAD5) == 'lea rcx, [rsi + 0x9b0]'
      and text_of(0x68DAE3) == 'call 0x244cb0', 'a weapon copies its SGO member into weapon +0x9B0 (+0x800 + 0x1B0)')
name = ins(0x68DA8A)
at = 0x68DA8A + name.size + struct.unpack_from('<i', img, 0x68DA8A + 3)[0]
check(img[at:at + 32] == 'animation_model\0'.encode('utf-16le'), '...the member named animation_model')

# The DemoIndirectFire's SGO and the visitors.
check(text_of(0x5B56F9) == 'lea rbx, [rdi + 0x100]', 'DemoIndirectFire: SGO root at +0x100')
check(text_of(0x5B5723) == 'mov r8, qword ptr [r12 + rax*8 + 0x179eba8]', '...find visitor table 0x179EBA8')
check(text_of(0x5B5767) == 'mov r8, qword ptr [r12 + rax*8 + 0x179eaf0]', '...get visitor table 0x179EAF0')
for table in (0x179EBA8, 0x179EAF0):
    entries = [struct.unpack_from('<Q', img, table + k * 8)[0] - base for k in range(4)]
    check(all(e in TEXT for e in entries[:3]) and entries[3] not in TEXT, f'{table:#x}: three alternatives (code), then data')
get2 = struct.unpack_from('<Q', img, 0x179EAF0 + 2 * 8)[0] - base
body = [f'{i.mnemonic} {i.op_str}' for i in md.disasm(img[get2:get2 + 0x60], get2)]
check(get2 == 0x2390D0 and 'mov eax, 2' in body and 'mov word ptr [rbx + 0x10], ax' in body,
      'the node-reference get writes alternative 2 into the out variant')
check(struct.unpack_from('<Q', img, 0x1765220 + 2 * 8)[0] - base == 0x89940 and img[0x89940:0x89943] == b'\xc2\x00\x00',
      'alternative 2 has nothing to destroy')

# The plugin's signatures.
bay = (ROOT / 'src/jet_bay.cpp').read_text(encoding='utf-8')
sigs = re.search(r'const Sig kIfcModelSigs\[\]=\{(.*?)\n\};', bay, re.S)
check(bool(sigs), 'kIfcModelSigs found')
if sigs:
    rows = re.findall(r'\{(0x[0-9A-F]+),\{([^}]*)\}\}', sigs.group(1))
    check(len(rows) == 6, f'six model signatures ({len(rows)})')
    for rva, data in rows:
        want = bytes(int(b, 16) for b in data.split(','))
        check(img[int(rva, 16):int(rva, 16) + len(want)] == want, f'signature {rva} matches EDF.dll')

for f in failures:
    print('FAIL', f)
print(f'ifc_model_native: {"FAILED" if failures else "ok"}')
sys.exit(1 if failures else 0)
