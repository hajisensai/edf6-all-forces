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
    record+0x58 at 0x1134CCB without a null check: the crash a failed open leads to;
  - the pools the config holds and what takes them (docs/cri-fs-loaders-re.md): each pool's 0x60 and the caller's
    extras (0x7AED20: loaders +0x22 = 130, binders +0xB, binds +0xB, files +0xB, stdio +0), so the added loaders are
    over the 130 the game builds; the binds are taken only where the game mounts (0x74C70), a stdio handle only by the
    open's existence check (closed at once) or a write, a file handle only by CRI's IO open (no game call reaches it):
    only the loaders grow with the archives a preload keeps open;
  - the added loaders are the spawn catalog's model archives (src/spawn_catalog_archives.h).
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
assert 'constexpr std::int32_t kAddedLoaders=debugspawn::kCatalogModelArchives;' in source
assert '#include "spawn_catalog_archives.h"' in source
added = int(re.search(r'inline constexpr int kCatalogModelArchives=(\d+);',
                      (ROOT / 'src' / 'spawn_catalog_archives.h').read_text(encoding='utf-8')).group(1))

init_calls, stores = lists['kInitCalls'], lists['kLoadersStores']
init, check, manager = consts['kInitLibrary'], consts['kConfigCheck'], consts['kFsManager']
assert consts['kNumLoaders'] == 0x08
assert added > 0, added

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

# The pools (docs/cri-fs-loaders-re.md): the config at rsp+0x20 gets 0x60 in num_loaders (+0x08), num_stdio_handles
# (+0x10), max_binds (+0x18), max_files (+0x1C), one binder (+0x04); then, with the caller's struct (rdi), each its
# extra added (binders +1).
cfg_stores = {0x73E1A: (0x08, 0x60), 0x73E22: (0x04, 1), 0x73E2A: (0x1C, 0x60), 0x73E32: (0x18, 0x60), 0x73E3A: (0x10, 0x60)}
for site, (field, value) in cfg_stores.items():
    assert at(site, 4) == bytes([0xC7, 0x44, 0x24, 0x20 + field]) and struct.unpack_from('<i', img, site + 4)[0] == value, hex(site)
extras = {0x73E53: (0x08, 0x73E59, 0x08), 0x73E66: (0x1C, 0x73E6C, 0x1C), 0x73E70: (0x18, 0x73E76, 0x18), 0x73E7A: (0x10, 0x73E80, 0x10)}
for load, (src_field, store, dst_field) in extras.items():
    assert at(load, 3) == bytes([0x8B, 0x47, src_field]) and at(load + 3, 3) == bytes.fromhex('83c060'), hex(load)   # mov eax,[rdi+f]; add eax,0x60
    assert at(store, 4) == bytes([0x89, 0x44, 0x24, 0x20 + dst_field]), hex(store)
assert at(0x73E5D, 3) == bytes.fromhex('8b4704') and at(0x73E60, 2) == bytes.fromhex('ffc0')   # binders: [rdi+4] + 1
# The one caller (0x703588; the twin builder 0x73EE0 has none) passes the struct 0x7AED20 filled just before.
calls_of = lambda target: [lo + m.start() for m in re.finditer(rb'\xE8', img[lo:hi - 5])
                           if lo + m.start() + 5 + struct.unpack_from('<i', img, lo + m.start() + 1)[0] == target]
assert calls_of(0x73D10) == [0x703588] and calls_of(0x73EE0) == []
assert at(0x70357B, 4) == bytes.fromhex('488d4d30') and rel32_target(0x70357F) == 0x7AED20
assert at(0x703584, 4) == bytes.fromhex('488d4d30')
want_extras = {0x08: 0x22, 0x04: 0x0B, 0x1C: 0x0B, 0x18: 0x0B}
for field, value in want_extras.items():
    store = bytes([0xC7, 0x41, field]) + struct.pack('<i', value)
    assert img.find(store, 0x7AED20, 0x7AED64) > 0, (hex(field), value)
assert at(0x7AED39, 4) == bytes.fromhex('48895110')   # mov [rcx+0x10],rdx (rdx 0): stdio handles and installers +0
assert at(0x7AED20, 2) == bytes.fromhex('33d2')
stock_loaders = 0x60 + want_extras[0x08]
assert stock_loaders == 130 and '130' in source
# The binds (max_binds, "No more resources for binding") are the bind calls' alone; the game makes them only in its
# mount 0x74C70 (CRI's own ADX2 ones aside).
binds_msg = [img.find(b'E2008121602:No more resources for binding.'), img.find(b'E2008082621:No more resources for binding.')]
assert all(m > 0 for m in binds_msg)
lea_of = lambda target: [lo + m.start() for m in re.finditer(rb'\x48\x8D\x15', img[lo:hi - 7]) if rip_target(lo + m.start(), 7, 3) == target]
assert all(all(0x1096D34 <= p < 0x1096E80 for p in lea_of(m)) and lea_of(m) for m in binds_msg)
for bind in (0x1093F68, 0x1093F94, 0x1093D50, 0x1093E08):
    game = [p for p in calls_of(bind) if p < 0xF00000]
    assert game and all(0x74C70 <= p < 0x75078 for p in game), (hex(bind), [hex(p) for p in game])
# A stdio handle (0x109B1F0, "Can not allocate Stdio handle", reached through the stdio open 0x109B3F8 alone): the
# game opens one only in the open 0x741C0: its existence check of an unbound path (0x74284, closed at 0x74298 right
# after) and the write mode (0x74345).
assert sorted(calls_of(0x109B1F0)) == [0x109B442, 0x109B50A] and calls_of(0x109B4F0) == [0x109B429]
assert 0x109B3F8 <= 0x109B442 < 0x109B4F0 and 0x109B3F8 <= 0x109B429 < 0x109B4F0
stdio_msg = img.find(b'E2008090105:Can not allocate Stdio handle.')
assert stdio_msg > 0 and lea_of(stdio_msg) == [0x109B22B]
assert [p for p in calls_of(0x109B3F8) if p < 0xF00000] == [0x74284, 0x74345]
assert rel32_target(0x74298) == 0x109B1E8 and at(0x74289, 3) == bytes.fromhex('4885c0') and at(0x74295, 3) == bytes.fromhex('488bc8')
# A file handle (max_files): taken by CRI's IO open (0x10A31E8 / 0x10A9CB4, both through 0x10A29C8), which no call
# reaches directly: only its IO interface tables hold them (CRI's own reads go through those).
for opener, table in ((0x10A31E8, 0x1FC4C88), (0x10A9CB4, 0x1FC4D48)):
    assert calls_of(opener) == [] and struct.unpack_from('<Q', img, table)[0] == base + opener, hex(opener)
    assert any(rel32_target(p) == 0x10A29C8 for p in range(opener, opener + 0x200) if img[p] == 0xE8 and
               p + 5 + struct.unpack_from('<i', img, p + 1)[0] == 0x10A29C8)
print(f'fsbudget native audit: ok (num_loaders {stock_loaders} -> {stock_loaders + added})')
