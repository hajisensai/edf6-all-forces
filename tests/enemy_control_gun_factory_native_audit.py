"""Parse the supported image; never load/execute the DLL or touch game assets."""
from pathlib import Path
import argparse
import re
import sys
import pefile
import capstone
from capstone.x86 import X86_OP_IMM
parser=argparse.ArgumentParser()
parser.add_argument('--dll',default='')
args=parser.parse_args()
if not args.dll or not Path(args.dll).is_file():
    print('SKIP: pass --dll for the supported EDF.dll')
    raise SystemExit(77)
p=pefile.PE(args.dll,fast_load=True)
img=p.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
md.detail=True
checks=0
def check(value,why):
    global checks
    assert value,why
    checks+=1
def ins(at):
    return next(md.disasm(img[at:at+16],at,count=1))
def exact(at,mnemonic,operands):
    i=ins(at)
    check(i.mnemonic==mnemonic and i.op_str==operands,f'{at:#x}: {i.mnemonic} {i.op_str}')
def call(at,target):
    i=ins(at)
    check(i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==target,f'{at:#x}: call {target:#x}')
check(p.FILE_HEADER.TimeDateStamp==0x678CCB46 and p.FILE_HEADER.Machine==0x8664,'supported x64 image')
root=Path(__file__).resolve().parents[1]
source=(root/'src/enemy_control_gun_factory.cpp').read_text(encoding='utf-8-sig')
section=re.search(r'constexpr Sig signatures\[\]=\{(.*?)\n\};',source,re.S)
check(section is not None,'runtime admission table')
prints=re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}',section.group(1))
check(len(prints)==3,'both original native factories and continuous beam consumer admitted')
for rva,values in prints:
    wanted=bytes(int(x,16) for x in values.split(','))
    check(len(wanted)==16 and img[int(rva,16):int(rva,16)+16]==wanted,f'actual supported fingerprint {rva}')
call(0x6CFE2A,0x6D05C0)
for at in (0x84C5A,0x4ACE88,0x4B2C2C,0x6D331D):call(at,0x1194280)
# The upper function advances counters even when the lower factory returned no
# object. These are attempts; a provider must inspect the actual Unit result.
exact(0x6D0935,'call','qword ptr [rax + 8]')
exact(0x6D0938,'inc','dword ptr [r14 + 0x1a4]')
exact(0x6D093F,'inc','dword ptr [rdi + 0x34]')
# 84C40 returns its outWeak address on failure; only the inner 1194280 RAX is
# the real registered SceneObject result. The central Unit ABI avoids confusing them.
exact(0x84C46,'mov','rax, r9')
exact(0x84C49,'mov','r10, r8')
exact(0x84C4C,'mov','r9, qword ptr [rsp + 0x50]')
exact(0x84C54,'mov','r8, rax')
exact(0x84C57,'mov','rdx, r10')
exact(0x84C64,'test','rax, rax')
exact(0x84C69,'mov','qword ptr [rsi], rdx')
exact(0x84C6C,'mov','rax, rsi')
exact(0x84C6F,'mov','qword ptr [rsi + 8], rdx')
check('matches>1' in source and 'FaultGun(holder)' in source and 'FaultUnit(factory,descriptor)' in source,'ambiguous providers fail explicitly')
check('!p.Fault' in source and 'gunFaulted' not in source and 'unitFaulted' not in source,'failures are delegated to the owning lease, never process-global stopped guns')
check('if(!selected){Fn<Gun>(kGun)' in source and 'if(!selected)return Fn<Unit>(kUnit)' in source,'unowned native callers remain transparent')
check('std::memcmp(image+s.rva,s.relay,5)' in source and 'return function==s.function' in source and 'thunk[10]!=0xFF' in source,'actual installed relay and thunk ownership checked')
check('Same(guns[i],p)' in source and 'Same(units[i],p)' in source,'provider registration identity checked')
exact(0x6D31BA,'mov','rax, qword ptr [rcx]')
exact(0x6D31BD,'call','qword ptr [rax + 0x60]')
i=ins(0x6D31C0)
check(i.mnemonic=='jmp' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==0x6D36F6,'beam refresh continuation is native weak cleanup')
exact(0x6D36F6,'mov','rcx, qword ptr [rbp - 0x60]')
exact(0x22F390,'cmp','dword ptr [rcx + 0xc30], 1')
exact(0x22F39F,'mov','eax, edx')
exact(0x22F3A1,'shr','eax, 0xe')
exact(0x22F3A8,'bts','edx, 0xd')
exact(0x22F3AC,'mov','dword ptr [rcx + 0xc34], edx')
check('kBeamContinue-(kBeamSite+10)' in source and 'replacement[11]' in source,'beam relay reconstructs its new jump rather than copying a rel32 instruction')
check('selected->Invoke(bullet)' in source and 'InvokeOriginalNativeBeamRefresh(bullet)' in source,'beam refresh belongs to exactly one typed provider')
i=ins(0x233C92)
check(i.mnemonic=='jmp' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==0x234420,'unique continuous-beam consume tail')
exact(0x234478,'bt','eax, 0xd')
exact(0x234482,'bts','eax, 0xe')
exact(0x234486,'mov','dword ptr [rcx + 0xaf4], eax')
exact(0x234596,'mov','rax, qword ptr [rcx + 0x9e0]')
exact(0x23459D,'mov','rcx, qword ptr [rcx + 0xbf0]')
exact(0x2345A4,'movups','xmm0, xmmword ptr [rax]')
exact(0x2345A7,'movups','xmmword ptr [rcx], xmm0')
check('static_cast<unsigned char*>(state)-0x140' in source and 'static_cast<unsigned char*>(bullet)+0x140' in source,'beam consumers use exact core/bullet adjustors')
print(f'central native Factory audit: {checks} checks passed')
