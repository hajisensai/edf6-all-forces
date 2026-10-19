"""Read-only instruction and original-asset audit for manual Deiroi foot guns.

Does not load/execute EDF.dll, run the game or write game files.
"""
from pathlib import Path
import argparse
import re
import struct
import json
from fractions import Fraction
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pylib'))
import rootcpk
import sgo
parser=argparse.ArgumentParser()
parser.add_argument('--dll',default='')
parser.add_argument('--game',default='')
parser.add_argument('--trace',default='',help='optional workspace tmp/build JSON; never a game-directory write')
args=parser.parse_args()
game=Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll=Path(args.dll) if args.dll else game/'EDF.dll'
if not dll.is_file() or not (game/'Root.cpk').is_file():
    print('SKIP: supported local EDF.dll/Root.cpk absent');raise SystemExit(77)
import pefile
import capstone
pe=pefile.PE(str(dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
checks=1

def instruction(at):
    return next(md.disasm(img[at:at+16],at,count=1))

def exact(at,mnemonic,operands):
    global checks
    x=instruction(at)
    assert (x.mnemonic,x.op_str)==(mnemonic,operands),(hex(at),x.mnemonic,x.op_str)
    checks+=1

def call(at,target):
    global checks
    x=instruction(at)
    assert x.mnemonic=='call' and x.operands[0].type==capstone.x86.X86_OP_IMM and x.operands[0].imm==target,(hex(at),x.op_str)
    checks+=1

def address(at,target):
    global checks
    x=instruction(at)
    assert x.mnemonic=='lea' and x.operands[1].mem.base==capstone.x86.X86_REG_RIP
    assert x.address+x.size+x.operands[1].mem.disp==target,(hex(at),hex(target))
    checks+=1

source=(ROOT/'src/enemy_control_deiroi.cpp').read_text(encoding='utf8')
for at,raw in re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}',source):
    data=bytes(int(v,16) for v in raw.split(','))
    assert pe.get_data(int(at,16),len(data))==data,at
    checks+=1
# Trusted configuration ordering: native SGO leg rows and repeated part records.
exact(0x31D88D,'mov','r14d, ebx')
exact(0x31D890,'shl','r14, 6')
exact(0x31D894,'mov','r15, qword ptr [rsi + 0xae8]')
exact(0x31D8D8,'mov','qword ptr [r15 + r14 + 0x28], rdi')
exact(0x32471F,'mov','qword ptr [rdi], r14')
exact(0x3247C0,'mov','ecx, 0x28')
exact(0x324824,'mov','qword ptr [r14 + 8], rax')
exact(0x324AB0,'shl','rcx, 4')
exact(0x324AB4,'add','rcx, qword ptr [rdi + 0x10]')
exact(0x324ABB,'mov','qword ptr [rdi + 0x20], rax')
call(0x3248BD,0x118AF20)
exact(0x118AF35,'mov','qword ptr [rdi + 0x38], rbx')
exact(0x118AF39,'mov','r8, qword ptr [rbx + 0x58]')
exact(0x118AF3D,'mov','rdx, qword ptr [rbx + 0x50]')
exact(0x118AF5B,'mov','rax, qword ptr [rbx + 0x48]')
exact(0x118AF70,'mov','qword ptr [rcx], rdi')
# Native exact class; no actor substitutions or fabricated target objects.
vt=0x17AB118
assert struct.unpack_from('<Q',img,vt+5*8)[0]-base==0x32B210
assert struct.unpack_from('<Q',img,vt+7*8)[0]-base==0x54A000
col=struct.unpack_from('<Q',img,vt-8)[0]-base
td=struct.unpack_from('<I',img,col+12)[0]
assert img[td+16:td+46]==b'.?AVDeiroiFootParts_Battery@@\0'
checks+=3
# Only these original configured joint/toe classes are weaponless. Unknown
# configured vtables must fail completeness rather than becoming a zero-gun list.
for part_vtable,part_frame in [(0x17AB4B0,0x328410),(0x17AB770,0x32DAE0)]:
    assert struct.unpack_from('<Q',img,part_vtable+5*8)[0]-base==part_frame
    checks+=1
exact(0x32B367,'mov','rcx, qword ptr [rbx + 0x5c0]')
# The hook filters ONLY the automatic idle->charge call; updates/network remain.
call(0x32D286,0x32C310)
address(0x32D260,0x32C750)
call(0x32D172,0x32BA80)
exact(0x32D245,'mov','ecx, dword ptr [rbx + 0xb00]')
exact(0x32D24E,'mov','dword ptr [rbx + 0xb00], eax')
exact(0x32D156,'mov','dword ptr [rbx + 0xb00], ecx')
# Complete real charging, burst and cancellation; AI delay is not weapon ammo.
exact(0x32C7EB,'cmp','dword ptr [rbx + 0xb78], ecx')
address(0x32C7FB,0x32C880)
call(0x32C821,0x32C310)
call(0x32C9B8,0x6CFBC0)
call(0x32CFE8,0x6CFC60)
call(0x32D004,0x6D0210)
call(0x32D097,0x6CFC00)
# Visible pose and the actual gun use one canonical point. The same mutable stack
# matrix is read by the native network sender after the hooked weapon call.
call(0x32B422,0x3296E0)
exact(0x32CFB1,'lea','rdx, [rsp + 0x30]')
exact(0x32CFF0,'lea','rdx, [rsp + 0x30]')
call(0x32CFF8,0x32B460)
exact(0x32CA6D,'mov','eax, dword ptr [rsi + 0xb30]')
exact(0x32C945,'movups','xmmword ptr [rsi + 0xb40], xmm6')
call(0x32CE79,0x6BB5A0)
assert 'EnemyControlWeaponAim(root.ref.object,point)' in source
assert 'if(EnemyControlTargetActive(root.ref.object))return false' in source
assert 'gun->owned=true;Set(*gun,kCharge);gun->manual=true' in source
checks+=3
# Actual firing contract: config[1] total shots, [6] frame interval, [7]
# per-update factory count (default1). 34 is an attempted-emission counter, not\n# proof of an actual Unit allocation; the accepted module observes its result.
exact(0x6D00DB,'mov','eax, dword ptr [rcx + rax + 0x14]')
exact(0x6D00DF,'mov','dword ptr [rsi + 0x3c], eax')
exact(0x6D00C8,'mov','ecx, dword ptr [rax + rcx + 0x50]')
exact(0x6D00D0,'mov','dword ptr [rsi + 0x30], ecx')
exact(0x6D00F6,'mov','ecx, 1')
exact(0x6D00FB,'mov','dword ptr [rsi + 0x44], ecx')
exact(0x32AC83,'mov','eax, dword ptr [rax + r15 + 0x2c]')
exact(0x32AC88,'mov','dword ptr [rdi + 0xb78], eax')
exact(0x6D093F,'inc','dword ptr [rdi + 0x34]')
# Root main cannon has a separate plain vector and direct-callback ABI.
exact(0x319759,'mov','qword ptr [r14 + 0xc0], r14')
exact(0x319764,'mov','qword ptr [r14 + 0xb8], rbx')
exact(0x318F3A,'mov','qword ptr [rsi + 0x1638], r14')
exact(0x318F41,'mov','qword ptr [rsi + 0x1640], r14')
exact(0x318F48,'mov','qword ptr [rsi + 0x1648], r14')
exact(0x31F192,'mov','rbp, qword ptr [rdi + 0x1638]')
exact(0x31F199,'imul','r15, qword ptr [rdi + 0x1648], 0x290')
exact(0x31F235,'add','rbx, 0x290')
exact(0x32121D,'mov','qword ptr [rdi + 0x18], rbp')
address(0x31F141,0x31CB60)
call(0x31F148,0x3211C0)
address(0x31CD58,0x31CDB0)
exact(0x31CD67,'jmp','0x3211c0')
call(0x31CB9A,0x6CFBC0)
call(0x31CD29,0x6CFC60)
call(0x31CD78,0x6CFC00)
exact(0x31CDB0,'ret','0')
assert 'EnemyControlNetworkContext(root->ref.object,&online)' in source
assert 'EnemyControlCurrentInput(root->ref.object,&input)' in source
assert 'input.sequence!=sequence' in source
checks+=3
# Independently count every original configured foot gun, including long variants
# and all repeated cells. These indices are exactly the adapter's wire ordinals.
G=rootcpk.Game(str(game))
expected={'E509_DEIROI.SGO':12,'E509_DEIROI_L.SGO':12,'E509_DEIROI_L2.SGO':9,
          'E509_DEIROI_M.SGO':3,'E509_DEIROI_S.SGO':3,'E509_DEIROI_SM.SGO':3,
          'E509_DEIROI_XL.SGO':33,'E509_DEIROI_XL2.SGO':30}
found={}
cache={}
variants={}
for name in G.names('OBJECT'):
    if 'DEIROI' not in name.upper() or not name.upper().endswith('.SGO'):continue
    values=sgo.load(data=G.read('OBJECT',name))
    if values.get('xgs_scene_object_class')!='Deiroi':continue
    # All eight native root variants currently own one main cannon row.
    assert len(values['deiroi_battery_config'])==1
    checks+=1
    keys=[];guns=[]
    for leg,leg_row in enumerate(values['body_config']):
        assert leg<32
        part=0
        for object_name,repeats in values[leg_row[1]]:
            assert isinstance(repeats,int) and 0<=repeats<=128
            child_name=object_name.rsplit('/',1)[-1]
            if child_name not in cache:cache[child_name]=sgo.load(data=G.read('OBJECT',child_name))
            child=cache[child_name]
            for _ in range(repeats):
                assert part<128
                if child.get('xgs_scene_object_class')=='DeiroiFootParts_Battery':
                    key=(leg<<16)|part;keys.append(key)
                    battery=child['deiroi_battery_config'];weapon=battery[1]
                    total=weapon[1];interval=weapon[6] if len(weapon)>6 else 0;factory_count=weapon[7] if len(weapon)>7 else 1
                    charge=battery[3]
                    assert all(isinstance(v,int) and v>=0 for v in [total,interval,charge,factory_count])
                    assert total>0 and factory_count==1
                    guns.append({'key':key,'object':child_name,'total':total,'intervalFrames':interval,'chargeFrames':charge,'factoryCount':factory_count})
                    checks+=2
                part+=1
            checks+=1
    assert 0<len(keys)<=256 and len(keys)==len(set(keys))
    found[name]=len(keys);variants[name]=guns;checks+=2
    print(name,'configured foot batteries',len(keys),'stable keys',','.join(hex(k) for k in keys))
assert found==expected,(found,expected)
checks+=1
xl=variants['E509_DEIROI_XL.SGO'];xl2=variants['E509_DEIROI_XL2.SGO']
beam=[g for g in xl if g['total']==85 and g['intervalFrames']==7 and g['chargeFrames']==240]
missile=[g for g in xl if g['total']==4 and g['intervalFrames']==8 and g['chargeFrames']==120]
assert len(beam)==30 and len(missile)==3 and len(xl2)==30 and all(g['total']==85 for g in xl2)
peak=sum((Fraction(60*g['factoryCount'],max(1,g['intervalFrames'])) for g in xl),Fraction())
assert peak==Fraction(3915,14) # 279.642857 factory emissions/sec at60 native updates/sec
checks+=3
# This is a deterministic asset/native-derived attempted-emission upper trace, NOT a gameplay
# recording. All33 eligible guns start together. Zero idle AI delay and zero CAS
# close delay deliberately overestimate sustained traffic; no accepted shot is
# coalesced. It feeds independent100/250ms reliable-window fixtures.
frames=60*60
trace=[{'frame':i,'shots':0,'phases':0} for i in range(frames)]
trace[0]['phases']+=len(xl) # admitted baseline
for gun in xl:
    cycle=0
    while cycle<frames:
        first=cycle+gun['chargeFrames']+2
        last=first+(gun['total']-1)*max(1,gun['intervalFrames'])
        for phase_frame in [cycle,first-1,last+1,last+2]:
            if phase_frame<frames:trace[phase_frame]['phases']+=1
        for shot in range(gun['total']):
            at=first+shot*max(1,gun['intervalFrames'])
            if at<frames:trace[at]['shots']+=gun['factoryCount']
        cycle=last+3
summary={'kind':'asset/native-derived conservative attempted-emission trace, not gameplay','nativeUpdatesPerSecond':60,
         'shotsMeaning':'upper bound assuming each native attempt allocates a real Unit; not observed successful shots',
         'assumptions':'all33 eligible together; zero AI idle/close delay; original SGO totals/interval/charge/factoryCount',
         'variant':'E509_DEIROI_XL.SGO','beamGuns':len(beam),'missileGuns':len(missile),
         'instantaneousRateUpper':str(peak),'shots':sum(t['shots'] for t in trace),'phaseRecords':sum(t['phases'] for t in trace),
         'maxShotsOneFrame':max(t['shots'] for t in trace),'guns':xl,'frames':[t for t in trace if t['shots'] or t['phases']]}
print('XL actual topology:30Beam(85/7frames,charge240)+3Missile(4/8frames,charge120), factoryCount1')
print('derived60s trace:',summary['shots'],'attemptUpper,',summary['phaseRecords'],'phases; conservative peak',float(peak),'attempts/sec')
if args.trace:
    target=Path(args.trace).resolve()
    allowed=[(ROOT/'tmp').resolve(),(ROOT/'build').resolve()]
    assert any(target.is_relative_to(folder) for folder in allowed),'trace output must remain in workspace tmp/build'
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print('derived trace artifact:',target)
print('Deiroi native/asset audit:',checks,'checks passed')

