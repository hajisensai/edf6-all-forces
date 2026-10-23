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
# Guided and unguided weapons do not share the native acceptance gate. Native
# slot 70 reads this specific first-holder weapon's actual lock list/progress,
# and only writes the primary intent after lock, range and geometric safety pass.
for at,expected in {
    0x65f9ca:'dword ptr [rdi + 0x6b0], 0',
    0x65f9d6:'rdx, qword ptr [rdi + 0xc68]',
    0x65f9e2:'rax, qword ptr [rdi + 0xc60]',
    0x65f9ec:'xmm1, dword ptr [rdi + 0x6d8]',
    0x65fa08:'eax, dword ptr [rdi + 0x6dc]',
    0x65ff2b:'byte ptr [rsp + 0x30], 0',
    0x65ff32:'r14b, r14b',
    0x65ff37:'r15b, r15b',
    0x65ff3c:'dword ptr [r12 + rbx + 0x2e4], 0x3f800000',
}.items():assert lookup[at]==expected,(hex(at),lookup[at])
assert 0x2E0 not in writes # wrapper owns transfer of this exact decision to the independent secondary.
# The real seat input writer populates both controls, including V603's unused
# secondary: pad analog fields, or this seat's configured keyboard row.
for at,expected in {
    0x5731ce:'xmm0, dword ptr [rbx + 0xcc]',
    0x5731dd:'dword ptr [rax + 0x2e0], xmm0',
    0x5731e5:'xmm1, dword ptr [rbx + 0xe4]',
    0x5731f4:'dword ptr [rax + 0x2e4], xmm1',
    0x57339a:'r9d, dword ptr [rdx + 0x2b4]',
    0x5733af:'r8, rcx, 0xa8',
    0x5733b9:'byte ptr [r8 + 0x940], r13b',
    0x5733cb:'dword ptr [rdx + 0x2e0], xmm0',
    0x5733d3:'byte ptr [r8 + 0x958], r13b',
    0x5733ec:'dword ptr [rax + 0x2e4], xmm0',
}.items():
    op=next(m.disasm(p.get_data(at,16),at)).op_str
    assert op==expected,(hex(at),op)
# V603 consumes the primary field only, and pulls the two native cannon holders.
flak=list(m.disasm(p.get_data(0x62149d,0x68),0x62149d))
flak_ops={i.address:i.op_str for i in flak}
assert flak_ops[0x6214a4]=='xmm1, dword ptr [rbx + 0x2e4]'
assert flak_ops[0x6214c3]==flak_ops[0x6214d3]=='0x62c000'
assert flak_ops[0x6214cf]=='rcx, 0x48'
assert not any(o.type==capstone.x86.X86_OP_MEM and o.mem.base==capstone.x86.X86_REG_RBX and o.mem.disp==0x2e0
               for i in flak for o in i.operands)
print('PASS: driver call contexts, per-weapon lock/safety gates, native seat inputs, V603 primary-only paired holders')
