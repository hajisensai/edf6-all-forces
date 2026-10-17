"""Audit src/supportunit.cpp's signatures against the supported EDF.dll (docs/supportunit-allies-re.md).

Static only: reads the PE file, does not load or run it. Checks that the hook site is the one
SupportUnitBullet01 ctor store KittopiaCreator's AOB names (the AOB unique in .text), that the ctor
writes SupportUnitBullet01's vtable (RTTI), and that the plugin's signature arrays are the DLL's bytes.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) < 2 or not sys.argv[1] or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported local EDF.dll')
    raise SystemExit(77)
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, hex(pe.FILE_HEADER.TimeDateStamp)
base = pe.OPTIONAL_HEADER.ImageBase


def data(rva, size):
    return pe.get_data(rva, size)


source = (Path(__file__).resolve().parent.parent/'src'/'supportunit.cpp').read_text(encoding='utf-8')


def const(name):
    return int(re.search(name+r'=(0x[0-9A-Fa-f]+)', source).group(1), 16)


def array(name):
    body = re.search(r'const unsigned char '+name+r'\[\]=\{([^}]*)\}', source).group(1)
    return bytes(int(x, 16) for x in body.replace(' ', '').split(','))


ctor_vtable, ctor_store, hit_filter = const('kCtorVtable'), const('kCtorStore'), const('kHitFilter')
store_sig = array('kCtorStoreSig')
assert data(ctor_vtable, len(array('kCtorVtableSig'))) == array('kCtorVtableSig')
assert data(ctor_store-6, len(store_sig)) == store_sig
assert data(hit_filter, len(array('kHitFilterSig'))) == array('kHitFilterSig')

# KittopiaCreator's AOB (89 87 0C 0F 00 00) is found once in .text, at the plugin's hook site.
text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
code = text.get_data()
aob = bytes.fromhex('89870C0F0000')
hits = [m.start()+text.VirtualAddress for m in re.finditer(re.escape(aob), code)]
assert hits == [ctor_store], [hex(h) for h in hits]

# The ctor's lea rax,[rip+disp] is SupportUnitBullet01's vtable; its RTTI says so.
disp = struct.unpack('<i', data(ctor_vtable+3, 4))[0]
vtable = ctor_vtable+7+disp
locator = struct.unpack('<Q', data(vtable-8, 8))[0]-base
type_rva = struct.unpack('<I', data(locator+12, 4))[0]
name = data(type_rva+16, 64).split(b'\0')[0]
assert name == b'.?AVSupportUnitBullet01@@', name

# The byte the cave clears (+0xB68) is the bullet part's (+0x140) +0xA28 the hit filter reads.
assert 0xB68-0x140 == 0xA28 and data(hit_filter+9, 7) == b'\x80\xBE\x28\x0A\x00\x00\x00'
assert b'\xC6\x87\x68\x0B\x00\x00\x00' in bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b',
                                                                                 re.search(r'cave\[\]=\{([^}]*)\}', source).group(1)))
print('supportunit allies: signatures, AOB, RTTI and the cleared byte match EDF.dll 0x678CCB46')
