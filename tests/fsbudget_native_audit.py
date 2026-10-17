"""Read-only PE audit of the native facts src/fsbudget.cpp (CRI File System's loader pool) rests on, on the real EDF.dll.

python tests/fsbudget_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input returns skip code 77.

Holds, against the DLL itself (docs/cri-fs-loaders-re.md):
  - every byte signature src/fsbudget.cpp checks before it hooks (read out of the source, so the two cannot drift);
  - the two init call sites are rel32 calls of criFs_InitializeLibrary 0x1092990 and the only ones from the game's own
    file-system setup; each passes the config at rsp+0x20 whose +0x08 (num_loaders) the site stored 0x60 into;
  - criFs_InitializeLibrary checks the config (0x109317C) before it sizes its work: the raised count is the one used;
  - the open of a file for reading creates one CriFsLoader (0x741C0 -> 0x1099184 at 0x742BA), and that create is
    where "Can not allocate loader handle (Increase num_loaders of CriFsConfig.)" is raised;
  - the stock TextureResource load (0x1110E80) hands the HD archive record of 0x1133C50 to 0x1134B10, which reads
    record+0x58 at 0x1134CCB without a null check: the crash a failed open leads to.
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
import pefile

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize


def at(rva: int, n: int) -> bytes:
    return img[rva:rva + n]


def rel32_target(site: int) -> int:
    assert img[site] == 0xE8, hex(site)
    return site + 5 + struct.unpack_from('<i', img, site + 1)[0]


def rip_target(site: int, length: int, disp_at: int) -> int:
    return site + length + struct.unpack_from('<i', img, site + disp_at)[0]


source = (ROOT / 'src' / 'fsbudget.cpp').read_text(encoding='utf-8')
arrays = {name: bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig\w*)\[\]=\{([^}]*)\};', source)}
consts = {name: int(value, 16) for name, value in re.findall(r'constexpr std::size_t (k\w+)=0x([0-9A-Fa-f]+);', source)}
lists = {name: [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', body)]
         for name, body in re.findall(r'constexpr std::size_t (k\w+)\[\]=\{([^}]*)\};', source)}
min_loaders = int(re.search(r'constexpr std::int32_t kMinLoaders=0x([0-9A-Fa-f]+);', source).group(1), 16)

init_calls, stores = lists['kInitCalls'], lists['kLoadersStores']
init, check, manager = consts['kInitLibrary'], consts['kConfigCheck'], consts['kFsManager']
assert consts['kNumLoaders'] == 0x08
assert min_loaders > 0x60, min_loaders

# The signatures where the Install reads them.
where = {'kInitLibrarySig': [init], 'kConfigCheckSig': [check], 'kManagerStoreSig': [0x73DB6],
         'kInitArgsSigA': [0x73E84], 'kInitArgsSigB': [0x73FC3], 'kLoadersStoreSig': stores}
assert set(arrays) == set(where), sorted(set(arrays) ^ set(where))
for name, rvas in where.items():
    want = arrays[name]
    assert len(want) >= 7, name
    for rva in rvas:
        assert at(rva, len(want)) == want, f'{name} at {rva:#x}: {at(rva, len(want)).hex()} != {want.hex()}'
for rva in (0x73DB6, 0x73E84, 0x73FC3):
    assert f'Matches({rva:#X},'.replace('0X', '0x') in source, hex(rva)

# Each site: the config (rsp+0x20, lea right before the call) whose +0x08 the same function stored 0x60 into; then the call.
assert init_calls == [0x73E8E, 0x73FCD]
for site, args, store in zip(init_calls, (0x73E84, 0x73FC3), stores):
    assert rel32_target(site) == init, hex(site)
    assert args + 10 == site
    assert at(store, 8) == bytes.fromhex('c744242860000000')   # mov dword [rsp+0x28],0x60: cfg(rsp+0x20)+0x08
    assert args - 0x200 < store < args
# The game's only calls of criFs_InitializeLibrary besides CRI's own (ADX2's internal init in 0xFF....)
calls = [lo + m.start() for m in re.finditer(b'\xE8', img[lo:hi - 5])]
calls = [p for p in calls if p + 5 + struct.unpack_from('<i', img, p + 1)[0] == init]
assert [p for p in calls if p < 0xF00000] == init_calls, [hex(p) for p in calls]
# The manager global the Install reads: stored at 0x73DB6 (before the call) and read at the init's head (0x73D22).
assert rip_target(0x73DB6, 7, 3) == manager
assert at(0x73D22, 3) == bytes.fromhex('488b0d') and rip_target(0x73D22, 7, 3) == manager
# criFs_InitializeLibrary: the config (or its default when null) checked at 0x1092A00 before the work is sized.
assert rel32_target(0x1092A00) == check
assert at(check + 6, 4) == bytes.fromhex('837924ff')   # cmp dword [rcx+0x24],-1: the version field of CriFsConfig

# The open for reading: 0x742BA calls criFsLoader_Create 0x1099184, whose failure names num_loaders.
assert at(0x742B6, 4) == bytes.fromhex('488d4b70') and rel32_target(0x742BA) == 0x1099184
msg = img.find(b'E2008070931:Can not allocate loader handle. (Increase num_loaders of CriFsConfig.)')
assert msg > 0
refs = [lo + m.start() for m in re.finditer(b'\x48\x8D\x15', img[lo:hi - 7])]
refs = [p for p in refs if rip_target(p, 7, 3) == msg]
assert refs and all(0x1099000 <= p < 0x109A100 for p in refs), [hex(p) for p in refs]

# The crash a failed open leads to: TextureResource (vtable 0x1A3B0E8) load 0x1110E80 -> record 0x1133C50 -> 0x1134B10,
# which loads record = [r15+8] and reads +0x58 with no test in between.
assert rel32_target(0x1110F4E) == 0x1133C50 and rel32_target(0x1110F7B) == 0x1134B10
assert at(0x1134CC4, 11) == bytes.fromhex('498b4708498bd4488b4858')
assert rel32_target(0x1134CCF) == 0x71230
print('fsbudget native audit: ok')
