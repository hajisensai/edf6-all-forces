"""Read-only audit of complete native enemy skill-table routes and Boss script commands.

No game DLL is loaded or executed; no game files are written. Native code flow,
real SGO variant tables and the embedded E516 AI BVM are independent evidence.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pylib'))
import bvm41
import dsgo
import rootcpk
import sgo

parser=argparse.ArgumentParser()
parser.add_argument('--dll',default='')
parser.add_argument('--game',default='')
args=parser.parse_args()
game=Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll=Path(args.dll) if args.dll else game/'EDF.dll'
if not dll.is_file() or not (game/'Root.cpk').is_file():
    print('SKIP: supported local EDF.dll/Root.cpk not available')
    raise SystemExit(77)
import capstone
import pefile
pe=pefile.PE(str(dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
img=pe.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
md.detail=True
checks=1

def ins(at):
    return next(md.disasm(img[at:at+16],at,count=1))

def call(at,target):
    global checks
    x=ins(at)
    assert x.mnemonic=='call' and x.operands[0].type==capstone.x86.X86_OP_IMM and x.operands[0].imm==target,(hex(at),x.mnemonic,x.op_str,hex(target))
    checks+=1

def lea(at,target):
    global checks
    x=ins(at)
    assert x.mnemonic=='lea' and x.operands[1].mem.base==capstone.x86.X86_REG_RIP
    assert x.address+x.size+x.operands[1].mem.disp==target,(hex(at),hex(target),x.op_str)
    checks+=1

source=(ROOT/'src/enemy_control_catalog.cpp').read_text(encoding='utf-8')
for at,raw in re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}',source):
    data=bytes(int(v,16) for v in raw.split(','))
    assert pe.get_data(int(at,16),len(data))==data,at
    checks+=1
# Monster's actual AI/debug fire helper and the complete native lifecycle.
assert img[0x4B7AC0:0x4B7ACB]==bytes.fromhex('48 83 EC 38 F6 81 B0 08 00 00 04')
lea(0x4B7AD6,0x4BEC00)
assert ins(0x4B7AE4).op_str=='dword ptr [rsp + 0x40], edx'
assert ins(0x4BEC39).op_str=='r8, qword ptr [rcx + 0x14a8]'
assert ins(0x4BEC4C).op_str=='rbx, rax, 0x38'
call(0x4BEC59,0x116C2D0)
call(0x4BECBA,0x116BBE0)
call(0x4BED2E,0x6C0510) if ins(0x4BED2E).mnemonic=='call' else None
assert ins(0x4BED2E).mnemonic=='jmp' and ins(0x4BED2E).operands[0].imm==0x6C0510
checks+=5
# The command-state exit clears the live attack index. ReadAbilityState may
# therefore distinguish a real active native attack from the last chosen one.
assert ins(0x4BED03).op_str=='dword ptr [rcx + 0x14d0], 0xffffffff'
checks+=1
G=rootcpk.Game(str(game))
for name,count in [('MONSTER501.SGO',18),('MONSTER501_KING.SGO',15),('E504_MONSTERA.SGO',38),('E504_MONSTERA_M034_3.SGO',38)]:
    values=sgo.load(data=G.read('OBJECT',name))
    table=values['attack_setting']
    assert len(table)==count
    for entry in table:
        assert isinstance(entry[0],str) and entry[0].isascii()
        assert f'L"{entry[0]}"' in source,(name,entry[0],'unlocalized/missing native table action')
        assert entry[1]>0
        checks+=2
    print(name,'native attacks',count)
# FlyingDyno's native action chooser, original per-variant table and second form.
assert ins(0x3ACAE4).op_str=='r8, rax, 0xe0'
assert ins(0x3ACAEB).op_str=='r8, qword ptr [rcx + 0x13f8]'
call(0x3B40D2,0x3B9340)
checks+=2
for name,count in [('E604_RADON.SGO',10),('E604_RADON_M051.SGO',7),('E604_RADON_M118_5.SGO',10),('E604_RADON_RM034.SGO',10)]:
    values=sgo.load(data=G.read('OBJECT',name))
    table=values['EDF6_FlyingDyno']['action']
    assert len(table)==count
    assert [entry['name'] for entry in table]==['fire_ball','horizontal_flame','horizontal_flame_reverse','vertical_flame','dive','fly_step_l','fly_step_r','form2_horizontal_flame','form2_horizontal_flame_reverse','form2_vertical_flame'][:count]
    checks+=2
    print(name,'native actions',count)
# Merman's caller, not its unchecked starter, owns mode/capability admission.
assert ins(0x3A6732).op_str=='r9, r14, 0x88'
assert ins(0x3A6743).op_str=='r9, qword ptr [rcx + 0x16f8]'
assert ins(0x3A6758).op_str=='byte ptr [rcx + 0x1718], r8b'
checks+=3
for name in ['E603_MERMAN.SGO','E603_MERMAN_LIGHT.SGO','E603_MERMAN_M063.SGO']:
    values=sgo.load(data=G.read('OBJECT',name))
    # Original class puts its table under EDF6_Merman.Action (case preserved).
    params=values['EDF6_Merman']
    table=params.get('action',params.get('Action'))
    assert table is not None,params.keys()
    assert len(table)==10
    # The source table's rows, not the shared CAS union, define the full modes.
    actual=[(entry[0],int(entry[1])) for entry in table]
    assert actual==[('dive',0),('splash',0),('both_hand',0),('encount',0),('wakeup',1),('sliding',1),('hopping',1),('roll_l',1),('roll_r',1),('dive_form_water',3)],actual
    for key,_mode in actual:assert f'L"{key}"' in source
    checks+=12
    print(name,'native mode actions',actual)
# Native1/101,102 descriptor -> full native Humanoid named action; virtual61
# includes E516's real exit cleanup. These are not direct CAS calls by the mod.
call(0x46855E,0x475BE0)
call(0x468673,0x475BE0)
assert ins(0x466E7C).mnemonic=='mov' and ins(0x466E7F).op_str=='qword ptr [rax + 0x1e8]'
assert struct.unpack_from('<Q',img,0x17BEC68+61*8)[0]-pe.OPTIONAL_HEADER.ImageBase==0x447A50
call(0x447A5F,0x47A090)
call(0x447A70,0x6C8760)
call(0x447A78,0x444060)
checks+=2
# Original E516 AI script has unsorted function bodies. Decode those original
# function stubs independently; mission-only parser sorting is not assumed here.
raw=G.read('OBJECT','E516_BIGGREYBOSS.SGO')
blob=dsgo.parse(raw).root.get('ai_script').data
h=struct.unpack_from('<20I',blob)
ng,og,nf,of,code0,fbase,pool=h[6],h[7],h[8],h[9],h[12],h[13],h[14]
assert ng and nf and h[4]==bvm41.VERSION
instructions=bvm41._decode(blob,code0,pool)
positions={x[0]:i for i,x in enumerate(instructions)}
functions=[]
for k in range(nf):
    c,n,_types,_sig=struct.unpack_from('<4I',blob,of+k*16)
    offset,opcode,_width,value,_type=instructions[positions[fbase+c]]
    assert opcode==bvm41.OP_CALL
    functions.append((bvm41._wstr(blob,n),positions[offset+value]))

def body(name):
    start=next(at for fn,at in functions if fn==name)
    ends=[at-3 for _fn,at in functions if at>start]
    end=min(ends) if ends else positions[fbase+struct.unpack_from('<I',blob,of)[0]]
    return instructions[start:end]

for fn,code in [('AiAttack_HandBeam',4),('AiAttack_HandHoming',5),('AiAttack_HandSpear',6)]:
    xs=body(fn)
    assert any(x[1]==bvm41.OP_PUSH and x[3]==code and xs[i+1][1:4:2]==(bvm41.OP_NAT1,230) for i,x in enumerate(xs[:-1])),fn
    checks+=1
for fn,state,native in [('AiAttack_DestroyBeam','attack_rollingLazer',101),('AiAttack_BathingKick','attack_bathingKick',102),('AiAttack_Dive','attack_dive',102),('AiAttack_Genocide','attack_genocide',101),('AiAttack_Meteor','attack_meteorite',101)]:
    xs=body(fn)
    assert state in [bvm41._wstr(blob,pool+x[3]) for x in xs if x[1]==bvm41.OP_PUSHS],fn
    assert any(x[1]==bvm41.OP_NAT1 and x[3]==native for x in xs),fn
    assert f'L"{state}"' in source
    checks+=3
# ObjectAction4..6 actual dispatch to its weapon state machine.
for code,site in [(4,0x444CAA),(5,0x444CDA),(6,0x444D07)]:
    assert struct.unpack_from('<I',img,0x444DC4+code*4)[0]==site
    checks+=1
# All native arm weapons retain their virtual-17 dispatch. Filtering its
# original call inside the body input does not change the equipment vector or
# event/network index, unlike temporarily shrinking that vector.
humanoid=(ROOT/'src/enemy_control_humanoid.cpp').read_text(encoding='utf-8')
weapons=re.findall(r'\{"([^"]+)",(0x[0-9A-F]+),(0x[0-9A-F]+),\{([^}]+)\}',humanoid)
assert len(weapons)==4
for name,vt,fire,raw in weapons:
    vt,fire=int(vt,16),int(fire,16)
    data=bytes(int(value,16) for value in raw.split(','))
    assert pe.get_data(fire,len(data))==data,name
    assert struct.unpack_from('<Q',img,vt+17*8)[0]-pe.OPTIONAL_HEADER.ImageBase==fire,name
    col=struct.unpack_from('<Q',img,vt-8)[0]-pe.OPTIONAL_HEADER.ImageBase
    td=struct.unpack_from('<I',img,col+12)[0]
    assert img[td+16:img.index(b'\0',td+16)].decode()=='.?AV'+name+'@@',name
    checks+=3
assert ins(0x47160A).op_str=='qword ptr [rax + 0x88]'
assert ins(0x471480).op_str=='r8, qword ptr [rdi + 0x1120]'
assert ins(0x471457).op_str=='qword ptr [r12], 0'
assert img[0x45A030:0x45A033]==bytes.fromhex('32 C0 C3') # shield virtual17 is no active shot
assert struct.unpack_from('<Q',img,0x17C1918+14*8)[0]-pe.OPTIONAL_HEADER.ImageBase==0x459ED0
assert ins(0x4A4C9B).op_str=='qword ptr [rax + 0x70]' # native SetUsable invokes real shield deploy lifecycle
checks+=6
soft_variants=0
known={name for name,_vt,_fire,_raw in weapons}|{'EDF6_SoftBodyWeapon_Shield'}
for name in G.names('OBJECT'):
    if not name.endswith('.SGO'):continue
    values=sgo.load(data=G.read('OBJECT',name))
    if values.get('xgs_scene_object_class') not in ('EDF6_Martian','EDF6_Venus'):continue
    soft_variants+=1
    table=values['humanoid_base_weapon_ex']['list']
    assert len(table)==4,name
    for entry in table:
        if not isinstance(entry,dict):continue
        path=entry['object']
        weapon=sgo.load(data=G.read('OBJECT',path.rsplit('/',1)[-1]))
        assert weapon['xgs_scene_object_class'] in known,(name,path,weapon['xgs_scene_object_class'])
        checks+=1
    checks+=1
assert soft_variants==24,soft_variants
assert ins(0x470F98).op_str=='rsi, qword ptr [rdi + 0x1120]'
assert ins(0x470F9F).op_str=='r15, qword ptr [rdi + 0x1130]'
assert ins(0x471480).op_str=='r8, qword ptr [rdi + 0x1120]'
assert ins(0x47160A).mnemonic=='call' and ins(0x47160A).op_str=='qword ptr [rax + 0x88]'
checks+=4
print('Humanoid arm equipment variants',soft_variants,'(actual guns and native passive shields)')
# UFOs use fixed per-SGO configurations. A bomber/ACE variant is not a
# selectable second weapon on the same body. The real native parser and every
# configured battery remain on the original full firing lifecycle.
expected507={
    'E507_GOLDUFO.SGO':(1,0,'BeamBullet01',0),
    'E507_GOLDUFO_HI.SGO':(1,0,'BeamBullet01',0),
    'E507_GOLDUFO_ACE.SGO':(1,1,'LaserBullet01',0),
    'E507_GOLDUFO_CHARGE.SGO':(0,1,'LaserBullet01',0),
    'E507_GOLDUFO_BIG.SGO':(0,2,'PlasmaBullet01',1),
}
for name,expected in expected507.items():
    values=sgo.load(data=G.read('OBJECT',name))
    assert (int(values['ai_attack_pattern']),int(values['bullet_pattern']),values['bullet_class'],int(values.get('object_type',0)))==expected,name
    assert ('bullet_big_bomber_info' in values)==(expected[1]==2),name
    assert not isinstance(values['bullet_class'],list)
    checks+=3
call(0x52B757,0x5318E0)
call(0x52C9B1,0x1194280)
assert ins(0x52F1F3).op_str=='ecx, byte ptr [rax + 0x71]'
assert ins(0x52F1F7).op_str=='byte ptr [rdi + 0x1474], cl'
assert img[0x5320B0:0x5320BC]==bytes.fromhex('83 FA 01 75 3E 80 B9 74 14 00 00 00')
checks+=3
for name,right_delay in [('E515_IMPERIALUFO.SGO',0),('E515_IMPERIALUFO_ACE.SGO',15),('E515_IMPERIALUFO_PROTOTYPE.SGO',0)]:
    values=sgo.load(data=G.read('OBJECT',name))
    assert values['bullet_object'][0]=='PulseBullet01'
    table=values['battery_config']
    assert len(table)==2 and [row[0] for row in table]==['muzzleL01','muzzleR01']
    assert all(row[1][0]=='bullet_object' for row in table)
    assert table[0][2]==0 and table[1][2]==right_delay
    checks+=4
assert ins(0x53743D).op_str=='eax, byte ptr [rax + 0x71]'
assert ins(0x537441).op_str=='byte ptr [rdi + 0x614], al'
call(0x6CF806,0x84C40)
fire=list(md.disasm(img[0x539C20:0x539D20],0x539C20))
assert any(x.mnemonic=='call' and x.operands[0].type==capstone.x86.X86_OP_IMM and x.operands[0].imm==0x5351C0 for x in fire)
assert any(x.mnemonic=='call' and x.operands[0].type==capstone.x86.X86_OP_IMM and x.operands[0].imm==0x6CFC00 for x in fire)
checks+=4
sys.path.insert(0,str(ROOT/'tools'))
import edf41_objects
for path,factory in [('OBJECT/UFO301.SGO','BeamBullet01'),('OBJECT/UFO301ACE.SGO','LaserBullet02')]:
    values=sgo.load(data=edf41_objects.read(path))
    table=values['battery_config']
    assert len(table)==6 and {row[0] for row in table}=={f'muzzle{side}{i:02d}' for side in ['L','R'] for i in range(1,4)}
    assert all(row[1][0]=='bullet_object' for row in table)
    assert values['bullet_object'][0]==factory
    checks+=3
print('UFO fixed-variant weapon coverage: 5 stock507, 3 stock515, 2 ported six-muzzle515')
print('enemy_control_catalog_native_audit:',checks,'checks passed (read-only original code, variant SGOs, embedded AI BVM)')
