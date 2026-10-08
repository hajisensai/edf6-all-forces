"""Read-only PE/RTTI audit that every stock RideAi vehicle class is covered.

python tests/mission_crew_native_audit.py /path/to/EDF.dll
Does not load or execute the DLL. Missing input returns skip code 77.
"""
from pathlib import Path
import re
import struct
import sys

if not __debug__:
    raise RuntimeError('Run without -O')
if len(sys.argv)<2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
import pefile

pe=pefile.PE(sys.argv[1],fast_load=True)
assert pe.FILE_HEADER.Machine==0x8664
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
assert pe.OPTIONAL_HEADER.SizeOfImage==0x22CE000
base=pe.OPTIONAL_HEADER.ImageBase
size=pe.OPTIONAL_HEADER.SizeOfImage

def u32(rva):
    return struct.unpack('<I',pe.get_data(rva,4))[0]

def u64(rva):
    return struct.unpack('<Q',pe.get_data(rva,8))[0]

def type_name(rva):
    return pe.get_string_at_rva(rva+16).decode('ascii')

found={}
for section in pe.sections:
    if section.Name.rstrip(b'\0')!=b'.rdata':
        continue
    data=section.get_data()
    for target in (0x633030,0x6490C0):
        needle=struct.pack('<Q',base+target)
        offset=0
        while True:
            offset=data.find(needle,offset)
            if offset<0:
                break
            table=section.VirtualAddress+offset-50*8
            offset+=1
            if table<8 or table%8:
                continue
            locator=u64(table-8)-base
            if not 0<locator<size-24:
                continue
            # MSVC x64 CompleteObjectLocator: signature=1, primary base offset=0,
            # self RVA must resolve back to this locator. Excludes incidental
            # function pointers and secondary NetworkObject vtables.
            if u32(locator)!=1 or u32(locator+4)!=0 or u32(locator+20)!=locator:
                continue
            hierarchy=u32(locator+16)
            assert 0<hierarchy<size-16
            count=u32(hierarchy+8)
            assert 0<count<100
            array=u32(hierarchy+12)
            bases=[type_name(u32(u32(array+i*4))) for i in range(count)]
            if '.?AVVehicleBase@@' not in bases:
                continue
            name=type_name(u32(locator+12))
            assert name==bases[0]
            found[table]=name

root=Path(__file__).resolve().parents[1]
source=(root/'src/crew.cpp').read_text(encoding='utf-8')
layout=(root/'src/layout.h').read_text(encoding='utf-8')
aliases={key:int(value,16) for key,value in re.findall(r'\b(kVt\w+)\s*=\s*(0x[0-9A-Fa-f]+)',layout)}
body=source.split('const VehicleClass kClasses[]={',1)[1].split('};',1)[0]
entries=re.findall(r'\{\s*(0x[0-9A-Fa-f]+|kVt\w+)\s*,',body)
declared={int(entry,16) if entry.startswith('0x') else aliases[entry] for entry in entries}
assert len(entries)==len(declared), 'duplicate production vehicle entry'
assert found, 'no trusted RTTI vehicle candidates found'
missing=set(found)-declared
extra=declared-set(found)
assert not missing, f'unhooked stock RideAi classes: {[(hex(v),found[v]) for v in sorted(missing)]}'
assert not extra, f'production classes without stock RideAi: {[hex(v) for v in sorted(extra)]}'
assert 'crewTables[i]=kClasses[i].vtable' in source
assert 'InstallMissionCrewHooks(crewTables,kClassCount)' in source
print(f'PASS all {len(found)} trusted RTTI VehicleBase primary vtables match production hook list')
print('PASS includes Proteus override, abstract bases, all stock helicopter/ground families')
