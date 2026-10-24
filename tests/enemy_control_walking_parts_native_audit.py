"""Read-only native/stock-asset evidence for complete Walking PartShoot replay.

Never loads EDF.dll or executes game code. No game files are written. These
checks validate instruction/asset contracts, not real two-machine acceptance.
"""
from pathlib import Path
import argparse
import re
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import dsgo
import rootcpk
import sgo
parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
args = parser.parse_args()
game = Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll = Path(args.dll) if args.dll else game / 'EDF.dll'
if not dll.is_file() or not (game / 'Root.cpk').is_file():
    print('SKIP: supported local EDF.dll/Root.cpk absent')
    raise SystemExit(77)
import capstone
import pefile
pe = pefile.PE(str(dll), fast_load=True)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0

def require(ok, why):
    global checks
    assert ok, why
    checks += 1

def ins(at):
    return next(md.disasm(img[at:at + 16], at, count=1))

def exact(at, mnemonic, operands):
    x = ins(at)
    require((x.mnemonic, x.op_str) == (mnemonic, operands), (hex(at), x.mnemonic, x.op_str))

def call(at, target):
    x = ins(at)
    require(x.mnemonic == 'call' and x.operands[0].type == capstone.x86.X86_OP_IMM and x.operands[0].imm == target, (hex(at), x.mnemonic, x.op_str, hex(target)))

def address(at, target):
    x = ins(at)
    require(x.mnemonic == 'lea' and x.operands[1].mem.base == capstone.x86.X86_REG_RIP and x.address + x.size + x.operands[1].mem.disp == target, (hex(at), hex(target)))

require(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll')
source = (ROOT / 'src/enemy_control_walking_parts.cpp').read_text(encoding='utf8')
expected_entry = bytes.fromhex('48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FD FF FF 48 81 EC 30 03 00 00')
require(img[0x33FCB0:0x33FCD0] == expected_entry, 'exact 32-byte complete entry')
entry = list(md.disasm(expected_entry, 0x33FCB0))
require(entry[-1].address + entry[-1].size == 0x33FCD0, '32-byte boundary is not an instruction split')
require(not any(o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP for i in entry for o in i.operands), 'entry trampoline needs no RIP relocation')
entry_source = re.search(r'constexpr unsigned char original\[\]=\{([^}]+)\}', source)
require(entry_source is not None and bytes(int(v, 16) for v in entry_source.group(1).split(',')) == expected_entry, 'production entry fingerprint equals actual bytes')
# The immutable definition path is nested BerserkerSettings.PartsShootList.
address(0x33C344, 0x17AC850)
address(0x33D4D0, 0x17ACCA8)
require(img[0x17AC850:0x17AC850 + 36] == 'BerserkerSettings\0'.encode('utf-16le'), 'actual parent settings key')
require(img[0x17ACCA8:0x17ACCA8 + 30] == 'PartsShootList\0'.encode('utf-16le'), 'actual PartsShootList key')
call(0x33C355, 0xD7480)
call(0x33D4E0, 0xD7480)
call(0x33D4F5, 0x3424D0)
exact(0x33FCF6, 'movsxd', 'r13, edx')
exact(0x33FCF9, 'shl', 'r13, 8')
exact(0x33FCFD, 'add', 'r13, qword ptr [rcx + 0x17f0]')
exact(0x33FD04, 'mov', 'r12, qword ptr [r13 + 8]')
exact(0x33FD08, 'mov', 'rax, qword ptr [r13 + 0x18]')
exact(0x33FD0C, 'lea', 'rcx, [rax + rax*4]')
exact(0x33FD10, 'lea', 'rax, [r12 + rcx*8]')
exact(0x34084E, 'add', 'r12, 0x28')
# Per-part local references; none of these addresses can enter the wire codec.
exact(0x33FE55, 'mov', 'qword ptr [rbp - 0x40], r15')
exact(0x33FE59, 'mov', 'qword ptr [rbp - 0x38], rbx')
exact(0x33FE5D, 'mov', 'rax, qword ptr [r12]')
exact(0x33FE61, 'mov', 'qword ptr [rbp - 0x30], rax')
exact(0x33FD55, 'lea', 'rbx, [r15 + 0x8a0]')
exact(0x3403DB, 'mov', 'rax, qword ptr [rbp - 0x30]')
exact(0x3403DF, 'add', 'rax, 0xb0')
exact(0x3403F5, 'mov', 'qword ptr [rbp - 0x68], rax')
exact(0x340403, 'mov', 'qword ptr [rbp - 0x50], rax')
call(0x340424, 0x351940)
# Constructor reads exactly the admitted input values and local resource refs.
reads = {
 0x351CB3: 'rax, qword ptr [rsi + 8]', 0x351CCB: 'rax, qword ptr [rsi + 0x20]',
 0x351CE5: 'rax, qword ptr [rsi + 0x30]', 0x351CED: 'rax, qword ptr [rsi + 0x38]',
 0x351CF5: 'rax, qword ptr [rsi + 0x40]',
}
for at, op in reads.items(): exact(at, 'mov', op)
word_reads = {0x48:0x351CFD, 0x4C:0x351D07, 0x50:0x351D11, 0xF0:0x351E14, 0xF4:0x351E24, 0x108:0x351E3E, 0x10C:0x351E4E}
for offset, at in word_reads.items():
    x = ins(at)
    require(x.operands[1].type == capstone.x86.X86_OP_MEM and x.operands[1].mem.base == capstone.x86.X86_REG_RSI and x.operands[1].mem.disp == offset, ('scalar input', hex(offset), hex(at)))
for at, offset in [(0x351D1B,0x60),(0x351D23,0x70),(0x351D2B,0x80),(0x351D39,0x90),(0x351D47,0xA0),(0x351D55,0xB0),(0x351E30,0xF8),(0x351E6B,0x118),(0x351E79,0x128),(0x351E87,0x138)]:
    x = ins(at)
    require(x.mnemonic == 'movups' and x.operands[1].mem.base == capstone.x86.X86_REG_RSI and x.operands[1].mem.disp == offset, ('matrix/resource input', hex(at), hex(offset)))
exact(0x351CD3, 'movzx', 'eax, byte ptr [rsi + 0x28]')
exact(0x351E5E, 'movzx', 'eax, byte ptr [rsi + 0x110]')
call(0x351985, 0x545670)
call(0x5456C4, 0x1189C00)
exact(0x1189CAC, 'mov', 'rax, qword ptr [rbp + 8]')
for at in [0x1189CB0,0x1189CB7,0x1189CBF,0x1189CCA]: require(ins(at).mnemonic == 'movaps', 'world matrix must be aligned')
# Root RNG seed and network ordinal are distinct; neither is a dedupe receipt.
exact(0x33FE7E, 'mov', 'rax, qword ptr [r15 + 0x3e8]')
exact(0x33FE8F, 'mov', 'qword ptr [r15 + 0x3e8], rax')
exact(0x33FE96, 'shr', 'rax, 0x20')
exact(0x33FE9A, 'shl', 'rax, 0x10')
exact(0x33FE9E, 'shr', 'rax, 0x20')
exact(0x33FEA2, 'mov', 'dword ptr [rbp + 0x84], eax')
exact(0x340786, 'movsx', 'eax, byte ptr [r15 + 0x1940]')
call(0x3407B7, 0x3557B0)
exact(0x3407F9, 'inc', 'al')
exact(0x3407FB, 'and', 'al, 0xf')
exact(0x3407FD, 'mov', 'byte ptr [r15 + 0x1940], al')
outer = list(md.disasm(img[0x33FCB0:0x340892],0x33FCB0))
require(not any(o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_R15 and o.mem.disp == 0x490 for i in outer for o in i.operands), 'outer PartShoot does not consume root RNG490')
# Actual projectile first, linked wrapper second. Default path is real native
# 29FD80 construction, not an invented empty carrier or CAS-only animation.
exact(0x3521EF, 'mov', 'r8, qword ptr [rbp + 0x138]')
call(0x3525A3, 0x1194280)
call(0x3526B9, 0x351660)
call(0x3516BC, 0x29FD80)
exact(0x352683, 'mov', 'qword ptr [rdi + 0x5d0], r15')
exact(0x352691, 'mov', 'qword ptr [rdi + 0x5d8], r12')
exact(0x351EFB, 'mov', 'qword ptr [rdi + 0x5c0], rax')
exact(0x351F06, 'mov', 'qword ptr [rdi + 0x5c8], rax')
call(0x351E9C, 0x118AF20)
exact(0x118AF35, 'mov', 'qword ptr [rdi + 0x38], rbx')
exact(0x354074, 'mov', 'rax, qword ptr [rbx + 0x5d0]')
exact(0x3540D1, 'movups', 'xmm2, xmmword ptr [rax + 0x90]')
exact(0x354230, 'mov', 'rax, qword ptr [rbx + 0x5d0]')
# The descriptor's final vptr is the full 0x1E0 class, despite the temporary base
# header at constructor entry. Shared deep-copy/destruction is therefore valid.
call(0x351FD2, 0x1001D0)
address(0x1001E7, 0x1762068)
address(0x1002EE, 0x1769028)
exact(0x1002F5, 'mov', 'qword ptr [rbx], rax')
for at, offset in [(0x100328,0x1C8),(0x10032F,0x1D0),(0x100336,0x1D8)]: exact(at,'mov',f'qword ptr [rbx + {hex(offset)}], rsi')
exact(0x32F0D0,'mov','rax, qword ptr [rdi + 0x1c8]')
exact(0x32F0EC,'cmp','qword ptr [rdi + 0x1d8], 0')
exact(0x32F112,'lock inc','dword ptr [rax + 0xc]')
exact(0x100CA2,'mov','rcx, qword ptr [rcx + 0x1d8]')
# Native Part never sets the optional persistent muzzle pointer: 100234 clears
# desc+70; the Part constructor never addresses its rbp+4D0 slot. Its only
# parameter subobject helper 231110 reads params and writes a separate vector.
exact(0x100234,'mov','qword ptr [rcx + 0x40], rsi')
ctor = list(md.disasm(img[0x351940:0x352FF0],0x351940))
require(not any(o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RBP and o.mem.disp == 0x4D0 for i in ctor for o in i.operands), 'native Part does not replace descriptor+70')
call(0x352132,0x231110)
exact(0x231134,'mov','rdi, rcx')
solver=list(md.disasm(img[0x231110:0x23129E],0x231110))
require(not any(o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RDI and o.access & capstone.CS_AC_WRITE for i in solver for o in i.operands), '231110 does not mutate parameter subobject/muzzle pointer')
# Correct secondary vtable includes deleting destructor at slot zero. Binding
# inherits wrapper ownership but cannot enumerate or create its actual bullet.
address(0x351995,0x17AE5D0)
col=struct.unpack_from('<Q',img,0x17AE5D0-8)[0]-base
require(struct.unpack_from('<I',img,col+4)[0]==0x120, 'wrapper NetworkObject offset')
for slot,target in [(13,0x127D30),(14,0x773B50),(15,0x354410)]: require(struct.unpack_from('<Q',img,0x17AE5D0+slot*8)[0]-base==target, ('native network slot',slot))
exact(0x127D30,'xor','eax, eax')
exact(0x127D32,'ret','')
call(0x35587C,0x781C40)
exact(0x781D24,'movzx','r12d, byte ptr [rdi + 8]')
exact(0x781D29,'and','r12b, 1')
call(0x782054,0x7768A0)
call(0x782060,0x774BD0)
call(0x7820C7,0x782720)
call(0x7820DE,0x75BCE0)
exact(0x354448,'cmp','al, 4')
call(0x35446F,0x3554F0)
call(0x354490,0x54D590)

# A separate raw-layout implementation validates the exact semantic digest
# contract: actual nested path, all labels, all doubles and complete VM bytes.
def u32(data, at): return struct.unpack_from('<I',data,at)[0]
def i32(data, at): return struct.unpack_from('<i',data,at)[0]
def text(data, at):
    end=at
    while data[end:end+2] != b'\0\0': end+=2
    return data[at:end]
def container(data,index):
    node=16+index*16
    require(u32(data,node+8)==3,('container',index))
    at=node+i32(data,node)
    n=u32(data,at+12)
    kids=struct.unpack_from('<'+'I'*n,data,at+i32(data,at+8)) if n else ()
    labels={}
    for j in range(u32(data,at+4)):
        row=at+i32(data,at)+j*8
        labels[u32(data,row+4)]=text(data,row+i32(data,row))
    return kids,labels

def member(data,index,key):
    kids,labels=container(data,index)
    return kids[next(i for i,v in labels.items() if v==key.encode('utf-16le'))]

def raw(data,index):
    at=16+16*index;typ=u32(data,at+8)
    if typ==0:return struct.pack('<I',typ)+data[at:at+8]
    block=at+i32(data,at)
    if typ==1:
        value=text(data,block)
        return struct.pack('<II',typ,len(value)//2)+value
    if typ in (2,4):
        n=u32(data,block)
        return struct.pack('<II',typ,n)+data[block+i32(data,block+4):block+i32(data,block+4)+n]
    kids,labels=container(data,index)
    result=struct.pack('<II',typ,len(kids))
    for ordinal,kid in enumerate(kids):
        result+=struct.pack('<I',int(ordinal in labels))
        if ordinal in labels:result+=struct.pack('<I',len(labels[ordinal])//2)+labels[ordinal]
        result+=raw(data,kid)
    return result

def semantic(value):
    if isinstance(value,float):return struct.pack('<Id',0,value)
    if isinstance(value,str):
        b=value.encode('utf-16le');return struct.pack('<II',1,len(b)//2)+b
    if isinstance(value,dsgo.Blob):return struct.pack('<II',value.kind,len(value.data))+value.data
    result=struct.pack('<II',3,len(value.items))
    for i,child in enumerate(value.items):
        result+=struct.pack('<I',int(i in value.names))
        if i in value.names:
            b=value.names[i].encode('utf-16le');result+=struct.pack('<I',len(b)//2)+b
        result+=semantic(child)
    return result

expected={'E602_BERSERKER.SGO','E602_BERSERKERUG.SGO','E602_BERSERKER_ACE.SGO','E602_BERSERKER_GRAPPLE.SGO','E602_BERSERKER_LARGE_PUNCH.SGO','E602_BERSERKER_MIDDLEUG_PUNCH.SGO','E602_BERSERKER_MIDDLE_PUNCH.SGO','E602_BERSERKER_ST.SGO'}
G=rootcpk.Game(str(game));found=set();groups=parts=0
for name in G.names('OBJECT'):
    if 'BERSERKER' not in name.upper() or not name.upper().endswith('.SGO'):continue
    data=G.read('OBJECT',name);values=sgo.load(data=data);settings=values.get('BerserkerSettings',{})
    if not isinstance(settings,dict) or not settings.get('PartsShootList'):continue
    found.add(name)
    doc=dsgo.parse(data);node=doc.root.get('BerserkerSettings').get('PartsShootList')
    require(isinstance(node,dsgo.Node) and len(node.items)==2,(name,'actual two native groups'))
    index=member(data,member(data,0,'BerserkerSettings'),'PartsShootList')
    require(raw(data,index)==semantic(node),(name,'complete original raw definition agrees with lossless parser'))
    alternate=dsgo.write(dsgo.Document(doc.root,list(reversed(doc.pool))))
    alternate_index=member(alternate,member(alternate,0,'BerserkerSettings'),'PartsShootList')
    require(data!=alternate and raw(data,index)==raw(alternate,alternate_index),(name,'definition unchanged by storage/pool permutation'))
    counts=[]
    for group in node.items:
        groups+=1
        require(isinstance(group,dsgo.Node),(name,'group node'))
        physical=group.get('nodes').items if 'nodes' in group.names.values() else [group]
        counts.append(len(physical));parts+=len(physical)
        for item in physical:require(isinstance(item.get('node'),str) and bool(item.get('node')),(name,'real model node'))
        require(group.get('ext_bullet').get('factory')=='EfsBullet',(name,'actual factory input'))
        for key in ('accuracy','velocity','gravity_factor','damage','durability','hit_volume_size','knock_back'):
            require(isinstance(group.get(key),(float,dsgo.Blob,dsgo.Node)),(name,key,'numeric/VM input retained'))
    require(counts==([2,2] if name=='E602_BERSERKER_GRAPPLE.SGO' else [1,1]),(name,'physical native expansion',counts))
    print(name,'physical parts per group',counts)
require(found==expected,('all eight stock PartsShootList variants',found))
require(groups==16 and parts==18,('all native groups/physical parts',groups,parts))
print('Walking complete parts native/asset audit:',checks,'checks passed; 8 stock variants, 16 groups, 18 physical parts')
