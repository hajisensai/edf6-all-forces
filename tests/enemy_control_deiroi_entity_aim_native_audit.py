"""Read-only instruction audit; no DLL execution or game-directory write."""
from pathlib import Path
import argparse,struct,re
p=argparse.ArgumentParser()
p.add_argument('--dll',default='')
p.add_argument('--game',default='')
a=p.parse_args()
dll=Path(a.dll) if a.dll else Path(a.game)/'EDF.dll' if a.game else Path(r'D:/steam/steamapps/common/EARTH DEFENSE FORCE 6/EDF.dll')
if not dll.is_file():print('SKIP: supported EDF.dll absent');raise SystemExit(77)
import pefile,capstone
pe=pefile.PE(str(dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
im=pe.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
checks=1
def exact(at,mn,op):
    global checks
    x=next(md.disasm(im[at:at+16],at,count=1))
    assert (x.mnemonic,x.op_str)==(mn,op),(hex(at),x.mnemonic,x.op_str)
    checks+=1
exact(0x6CF941,'mov','dword ptr [rcx + 0x2b8], r9d')
exact(0x6CF952,'mov','edi, 0xffffffff')
exact(0x6CF957,'xor','r12d, r12d')
exact(0x6CF95A,'test','rdx, rdx')
exact(0x6CF95D,'je','0x6cfa71')
exact(0x6CF963,'mov','eax, dword ptr [rdx + 8]')
exact(0x6CF966,'test','eax, eax')
exact(0x6CF968,'je','0x6cfa71')
exact(0x6CF9C8,'cmp','dword ptr [r14 + 0x378], r12d')
exact(0x6CF9D4,'call','0x8dd10')
exact(0x6CF9D9,'jmp','0x6cfba3')
exact(0x6CFA71,'lea','rax, [rbp + 0x280]')
exact(0x6CFA90,'call','0x84c40')
exact(0x6CFA96,'mov','qword ptr [rbp + 0x2d0], r12')
exact(0x6CFAF4,'call','0x6d0220')
exact(0x6CFB50,'call','0x6d02b0')
exact(0x6D03C2,'call','0x781630')
exact(0x7766D4,'mov','dword ptr [rcx + 0xc], 4')
root=Path(__file__).resolve().parents[1]
source=(root/'src/enemy_control_deiroi_entity_aim.cpp').read_text(encoding='utf8')
raw=re.search(r'kOriginal\[\]=\{([^}]+)\}',source).group(1)
raw=bytes(int(x,16) for x in raw.split(','))
assert im[0x6CF95A:0x6CF95A+len(raw)]==raw and len(raw)==14
checks+=1
assert 'DeiroiAcceptedEntityFactoryScope(parameter)' in source
assert 'Jump(code,image+kNative)' in source
assert 'code.Data(kOriginal+9,5)' in source
checks+=3
print('Deiroi TypeEntity inline native audit:',checks,'checks passed')
