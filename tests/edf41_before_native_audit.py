"""EDF.dll facts src/edf41before.cpp relies on (EDF4.1's pre-mission scenes without the menu's black fade).

    python tests/edf41_before_native_audit.py <EDF.dll>     (exit 77: no supported EDF.dll given)

Static only (pefile + capstone over the file, nothing loaded or run):
  * every byte pattern the plugin checks before patching (its kXxxCode arrays, read from the source) is at its RVA;
  * the patched call at 0x70DBE3 calls the script loader 0x1F68D0, inside "int Before()" (registered at 0x71D8DC with
    the declaration at 0x71D90A), after the '/before.bvm' existence test;
  * 0x710300 is what MAINSCRIPT's "void _DeleteAllMenuUi()" runs (registered at 0x71D3CA / 0x71D433);
  * the UI name EDF4.1's scenes fade with is unknown to EDF6 and the one pylib/bvm41.py UI_NAMES maps it to is known.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: a supported EDF.dll is required (EDF6_NATIVE_DLL)')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('assertions must be enabled')
import capstone
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, hex(pe.FILE_HEADER.TimeDateStamp)
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def ins(rva: int):
    return next(md.disasm(img[rva:rva + 16], rva))


def rip_target(rva: int) -> int:
    i = ins(rva)
    m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', i.op_str)
    assert m, (hex(rva), i.mnemonic, i.op_str)
    return i.address + i.size + int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)


def call_target(rva: int) -> int:
    assert img[rva] == 0xE8, hex(rva)
    return rva + 5 + struct.unpack_from('<i', img, rva + 1)[0]


def wide(rva: int) -> str:
    end = rva
    while img[end:end + 2] != b'\0\0':
        end += 2
    return img[rva:end].decode('utf-16le')


def ascii_at(rva: int) -> str:
    return img[rva:img.index(b'\0', rva)].decode()


# the plugin's own checks, as its source states them
src = (ROOT / 'src' / 'edf41before.cpp').read_text(encoding='utf-8')
consts = {name: int(value, 16) for name, value in re.findall(r'\b(k[A-Za-z]+)=(0x[0-9A-Fa-f]+)', src)}
arrays = 0
for name, body in re.findall(r'const unsigned char (k[A-Za-z]+)Code\[\]=\{([^}]*)\}', src):
    expected = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    rva = consts[name]
    assert img[rva:rva + len(expected)] == expected, (name, hex(rva))
    arrays += 1
assert arrays == 3, arrays
site, loader, delete_all = consts['kSceneLoadCall'], consts['kScriptLoad'], consts['kDeleteAllMenuUi']
assert call_target(site) == loader, hex(call_target(site))

# "int Before()": the registration takes the function in rcx and the declaration in rdx
assert rip_target(0x71D8DC) == 0x70DA00 and ascii_at(rip_target(0x71D90A)) == 'int Before()'
assert 0x70DA00 < consts['kBeforeOpen'] < site < 0x70DCD3          # the call is Before()'s own, before its ret
assert wide(rip_target(0x70DA9E)) == '/before.bvm'                 # appended to the mission folder ...
assert call_target(consts['kBeforeOpen'] + 4) == 0x50E20            # ... and tested for opening (0 skips the scene)
assert ins(0x70DB21).mnemonic == 'je' and ins(0x70DB21).op_str == '0x70dc53'

# "void _DeleteAllMenuUi()" is 0x710300 (what UTILITY.H's DeleteAllMenuUi ends with)
assert rip_target(0x71D3CA) == delete_all and ascii_at(rip_target(0x71D433)) == 'void _DeleteAllMenuUi()'

# the 4.1 fade screen's name: EDF6 has none of it; the one UI_NAMES gives is registered (its class name getter)
sys.path.insert(0, str(ROOT / 'pylib'))
import bvm41  # noqa: E402
raw = Path(sys.argv[1]).read_bytes()
for old, new in bvm41.UI_NAMES.items():
    assert old.encode('utf-16le') not in raw and old.encode() not in raw, old
    assert new.encode('utf-16le') in raw, new
assert wide(rip_target(0x7FBDF0)) == bvm41.UI_NAMES['ui_fade_screen_test']
print('edf41 before scene: patch site, loader, _DeleteAllMenuUi and the fade screen name verified')
