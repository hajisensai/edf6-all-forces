"""Read the supported native driver AI and per-seat aiming contract; no game execution."""
from pathlib import Path
import sys
if len(sys.argv)!=2 or not Path(sys.argv[1]).is_file():
    print('SKIP: supported EDF.dll path required');raise SystemExit(77)
if not __debug__: raise RuntimeError("Run without -O: contract assertions must execute")
import pefile
import capstone
p=pefile.PE(sys.argv[1],fast_load=True)
assert p.FILE_HEADER.TimeDateStamp==0x678CCB46 and p.FILE_HEADER.Machine==0x8664
assert p.get_data(0x661733,17)==bytes.fromhex('488b034c8bc533d2488bcbff9030020000')
assert p.get_data(0x6617d0,17)==bytes.fromhex('488b034533c033d2488bcbff9030020000')
# Seat list -> first holder -> weapon. Both the aim function and its ballistic helper agree.
m=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);m.detail=True
code=[]
for i in m.disasm(p.get_data(0x65f6f0,0xa00),0x65f6f0):
    code.append(i)
    if i.mnemonic=='ret':break
assert code[-1].address==0x65ff80
writes={i.operands[0].mem.disp for i in code if i.operands and i.operands[0].type==capstone.x86.X86_OP_MEM and i.operands[0].access & capstone.CS_AC_WRITE}
assert 0x2D0 in writes and 0x2D4 in writes and 0x2E4 in writes
assert 0x2C0 not in writes and 0x2C4 not in writes
lookup={i.address:i.op_str for i in code}
assert '+ 0xc8]' in lookup[0x65f74f] and lookup[0x65f757]=='rdx, qword ptr [rax]' and lookup[0x65f75a]=='rdi, qword ptr [rdx + 0x10]'
helper=list(m.disasm(p.get_data(0x660019,15),0x660019))
assert '+ 0xc8]' in helper[0].op_str and helper[1].op_str=='rcx, qword ptr [rax]' and '+ 0x10]' in helper[2].op_str
print('PASS: both seat-0 native call contexts, same target/idle dispatch, first-holder weapon, aim/fire writes without driver movement writes')
