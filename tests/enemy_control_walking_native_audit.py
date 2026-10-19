"""Read-only supported EDF.dll / Root.cpk audit for complete Walking native attacks.

No game code is loaded or executed. It checks the actual native input, chooser,
full FSM and all stock Berserker AttackSetting tables independently of fixtures.
"""
from pathlib import Path
import argparse
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pylib'))
import rootcpk
import sgo
import cas_legacy
sys.path.insert(0,str(ROOT/'tools'))
import edf41_objects
parser=argparse.ArgumentParser()
parser.add_argument('--dll',default='')
parser.add_argument('--game',default='')
args=parser.parse_args()
game=Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll=Path(args.dll) if args.dll else game/'EDF.dll'
if not dll.is_file() or not (game/'Root.cpk').is_file():
    print('SKIP: supported local EDF.dll/Root.cpk unavailable')
    raise SystemExit(77)
import pefile
import capstone
pe=pefile.PE(str(dll),fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46
img=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
checks=1

def ins(at):
    return next(md.disasm(img[at:at+16],at,count=1))
def same(at,mnemonic,operand):
    global checks
    item=ins(at)
    assert (item.mnemonic,item.op_str)==(mnemonic,operand),(hex(at),item.mnemonic,item.op_str)
    checks+=1

def call(at,target):
    global checks
    item=ins(at)
    assert item.mnemonic=='call' and item.operands[0].type==capstone.x86.X86_OP_IMM and item.operands[0].imm==target,(hex(at),item.op_str,hex(target))
    checks+=1

source=(ROOT/'src/enemy_control_walking.cpp').read_text(encoding='utf-8-sig')
for at,raw in re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}',source):
    data=bytes(int(v,16) for v in raw.split(','))
    assert img[int(at,16):int(at,16)+len(data)]==data,at
    checks+=1
# The ordinary player input records route the class's ORIGINAL full StartAttack.
for vt,attack in [(0x17ADA78,0x34EEE0),(0x17AE088,0x350D10),(0x17AE968,0x3563A0),
                  (0x17AED50,0x358760),(0x17AF200,0x35B9A0),(0x17AF9F8,0x350D10)]:
    assert struct.unpack_from('<Q',img,vt+0x248)[0]-base==attack
    assert struct.unpack_from('<Q',img,vt+0x268)[0]-base==0x345310
    checks+=2
same(0x33F7C6,'cmp','byte ptr [rdx + 0x71], al')
same(0x33F7D7,'cmp','byte ptr [rdx + 0xe9], 0')
same(0x33F7EC,'cmp','byte ptr [rdx + 0xd1], 0')
same(0x35ABA3,'cmp','byte ptr [rdx + 0xd0], 0')
same(0x343E2D,'call','qword ptr [rax + 0x248]')
same(0x343E47,'call','qword ptr [rax + 0x248]')
call(0x350DDA,0x33EDA0)
call(0x35646A,0x33EDA0)
call(0x35888F,0x33EDA0)
call(0x34FF51,0x33EDA0)
same(0x358882,'or','dword ptr [rdi + 0x380], 0x10000000')
# Native phase0 consumes the full actual SGO row, advances its original RNG,
# starts animation/rate and owns the real phase1/2 cleanup. The narrow hook does
# not replace any of those transitions or call the CAS player directly.
call(0x34534B,0x52DE0)
same(0x34539B,'mov','r8, qword ptr [rdi + 0x3e8]')
same(0x3453AC,'imul','r8, rax')
same(0x3453B9,'mov','qword ptr [rdi + 0x3e8], r8')
same(0x3453C4,'imul','r8, rax')
same(0x3453C8,'shr','r8, 0x20')
same(0x345444,'movups','xmmword ptr [rdi + 0x17d8], xmm0')
call(0x34544B,0xD7480)
call(0x345459,0x52DE0)
call(0x345473,0x551E0)
call(0x34548B,0x116C2D0)
call(0x3454AE,0x116DF20)
call(0x3454BA,0x77C8D0)
call(0x3454E1,0x116BBE0)
call(0x34553F,0x344E10)
same(0x34555B,'jmp','0x77c8d0')
# Normal attack completion leaves the old action descriptor while main returns
# to stand; status must also inspect the actual player state and ready phase.
same(0x116C1FE,'mov','dword ptr [rbx + 0x104], r15d')
same(0x5405BD,'mov','dword ptr [rcx + 0x12dc], edx')
same(0x5405C3,'mov','byte ptr [rcx + 0x12d0], 7')
# Narrow callsite key is genuinely animation_speed; lookup is a pure SGO read.
item=ins(0x345428);target=item.address+item.size+item.operands[1].mem.disp
assert img[target:target+32].decode('utf-16-le').split('\0')[0]=='animation_speed'
checks+=1
same(0xD748A,'mov','rbx, rdx')
same(0xD7490,'mov','rdx, qword ptr [rcx]')
same(0xD753E,'mov','dword ptr [rbx + 8], eax')
# The old tag10 carries the native expanded-part ordinal. Detached MAB creation
# binds each child with that ordinal, then increments it modulo 16.
same(0x34EF83,'movsx','edx, byte ptr [rbx + 0x1940]')
call(0x34EF8F,0x12B5580)
same(0x340786,'movsx','eax, byte ptr [r15 + 0x1940]')
call(0x3407B7,0x3557B0)
same(0x3407F9,'inc','al')
same(0x3407FB,'and','al, 0xf')
same(0x3407FD,'mov','byte ptr [r15 + 0x1940], al')
# Enumerate concrete source variants, not the much larger union of all CAS states.
G=rootcpk.Game(str(game))
families={
 'EDF6_Berserker_Large':4,'EDF6_Berserker_Middle':7,'EDF6_Berserker_TypeA':4,
 'EDF6_Berserker_TypeBommer':3,'EDF6_Berserker_TypeGrapple':1,'EDF6_Berserker_TypeShooter':4,
}
seen={k:[] for k in families}
cas_cache={}
def main_states(data):
    def name(at):
        return cas_legacy._text(data,at+struct.unpack_from('<i',data,at)[0])
    groups={name(at):{name(state.at) for state in states} for at,states in cas_legacy.cas_layout(data).tree.anmgroups}
    assert groups.get('main'), 'actual initialized main CAS table is required'
    return groups['main']
for name in G.names('OBJECT'):
    if not name.upper().startswith('E602_BERSERKER') or not name.upper().endswith('.SGO'):continue
    values=sgo.load(data=G.read('OBJECT',name));family=values.get('xgs_scene_object_class')
    if family not in families:continue
    rows=values['BerserkerSettings']['AttackSetting'][1]
    assert rows and all(isinstance(row,list) and row and all(isinstance(value,str) for value in row) for row in rows),(name,rows)
    for row in rows:
        assert f'L"{row[0]}"' in source,(name,row[0],'missing menu localization')
        checks+=1
    cas_name=values['animation_model'][1].split('/')[-1]
    if cas_name not in cas_cache:cas_cache[cas_name]=main_states(G.read('OBJECT',cas_name))
    for row in rows:
        assert all(state in cas_cache[cas_name] for state in row),(name,row,'stock real state missing')
        checks+=len(row)
    seen[family].append(name)
    if family=='EDF6_Berserker_Large':
        guns=values['gun_control'];assert len(guns)==4
        assert [entry['interval'] for entry in guns]==[0.,60.,120.,180.]
        factory='EfsBullet' if name.upper().endswith('_PUNCH.SGO') else 'PulseBullet01'
        assert [entry['gun_battery']['bullet']['factory'] for entry in guns]==[factory]*4
        checks+=3
    if family=='EDF6_Berserker_TypeBommer':
        assert len(values['Bombs'])==2
        assert rows==[['attack_suicideBomb_banzai']] or rows==[['attack_suicideBomb_banzai'],['attack_suicideBomb_try']]
        checks+=2
    print(name,rows)
for family,count in families.items():
    assert len(seen[family])==count,(family,seen[family],count)
    checks+=1
# Older converted Hectors retain unused Large AttackSetting template rows, but
# their actual main CAS has none of those stomps. A real initialized CAS with
# zero usable rows is valid; fabricating those inherited actions is not.
hectors=[row for row in edf41_objects.registry()['objects'] if row.get('class')=='EDF6_Berserker_Large']
assert len(hectors)==21
checks+=1
for row in hectors:
    values=sgo.load(data=edf41_objects.read(row['object']))
    cas_path='OBJECT/'+values['animation_model'][1].split('/')[-1]
    if cas_path not in cas_cache:cas_cache[cas_path]=main_states(edf41_objects.read(cas_path))
    actual=[attack for attack in values['BerserkerSettings']['AttackSetting'][1] if all(state in cas_cache[cas_path] for state in attack)]
    assert actual==[],(row['object'],actual,'unexpected real Hector melee skill')
    checks+=1
print('Walking native chooser / 23 stock variants + 21 real Hector CAS:',checks,'checks passed')
