"""Read-only Walking GB2 accepted-outcome/native/asset audit.

Reuses the shared projectile ABI audit, then independently verifies Walking
providers, central dispatch sites, native post-create ownership and all 25
Large stock/ported plus 11 Middle/Shooter configurations. No DLL loading,
game execution, installation, or writes to game files.
"""
from pathlib import Path
import re
import runpy
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
shared = runpy.run_path(str(ROOT / 'tests/enemy_control_projectile_native_audit.py'))
p,b,d = shared['p'],shared['b'],shared['d']
check,ins,pointer,address = (shared[k] for k in ('check','ins','pointer','address'))
initial = check.__globals__['n']
args = shared['args']
game = Path(args.game) if args.game else Path(shared['dll']).parent
if not (game / 'Root.cpk').is_file():
    print('SKIP: local Root.cpk absent')
    raise SystemExit(77)
sys.path[:0] = [str(ROOT / 'pylib'),str(ROOT / 'tools')]
import rootcpk
import sgo
import edf41_objects

source = (ROOT / 'src/enemy_control_walking_net.cpp').read_text(encoding='utf8')
central = (ROOT / 'src/enemy_control_gun_factory.cpp').read_text(encoding='utf8')
for text,label in [(source,'Walking provider'),(central,'central dispatcher')]:
    prints = re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}',text)
    check(bool(prints),(label,'native fingerprints absent'))
    for at,raw in prints:
        value = bytes(int(x.strip(),16) for x in raw.split(','))
        check(len(value)==16 and p.get_data(int(at,16),16)==value,(label,at))

# Real native call graph and layouts; values are not inferred from provider code.
MARKERS = """
6D2707|mov|r13, qword ptr [rsi + 0x278]
6D270E|imul|r14, qword ptr [rsi + 0x288], 0xd0
6D26B9|mov|rdx, qword ptr [rcx + 0x2b8]
6D26E0|mov|rax, qword ptr [rsi + 0x2b0]
6D107B|mov|qword ptr [rsi + 0x2b0], rcx
6D1089|mov|qword ptr [rsi + 0x2b8], rdx
6D3114|movups|xmmword ptr [r14], xmm4
6D3118|movups|xmmword ptr [r14 + 0x10], xmm3
6D311D|movups|xmmword ptr [r14 + 0x20], xmm2
6D3122|movups|xmmword ptr [r14 + 0x30], xmm1
6D32B3|mov|qword ptr [rbp + 0x1f0], r14
6D3308|mov|r8, qword ptr [rsi + 0x2f0]
6D331D|call|0x1194280
6D3325|test|rax, rax
6D339B|mov|rdx, qword ptr [rax + 0x30]
6D33C0|mov|r10, qword ptr [r8 + 0x28]
6D33E3|call|0x12da7aa
6D34BE|cmp|dword ptr [rbx + 0xc30], 1
6D34E4|mov|qword ptr [r13 + 0xb0], rcx
6D34F2|mov|qword ptr [r13 + 0xb8], rax
6D3518|lea|r14, [rsi + 0x290]
6D3545|lock inc|dword ptr [rdi + 0xc]
6D3553|mov|r15, qword ptr [r14]
6D3576|mov|ecx, 0x20
6D357B|call|0x12d85b0
6D3584|mov|qword ptr [rax + 0x10], rcx
6D358C|mov|qword ptr [rax + 0x18], rcx
6D3598|inc|qword ptr [r14 + 8]
6D359C|mov|rcx, qword ptr [r15 + 8]
6D35A0|mov|qword ptr [rax], r15
6D35A3|mov|qword ptr [rax + 8], rcx
6D35AF|mov|qword ptr [r15 + 8], rax
6D35B3|mov|qword ptr [rcx], rax
6D35B6|mov|rcx, qword ptr [rsi + 0x650]
6D35D5|call|qword ptr [rax + 0x10]
6D35D8|mov|r14, qword ptr [rsi + 0x658]
6D31BA|mov|rax, qword ptr [rcx]
6D31BD|call|qword ptr [rax + 0x60]
6D31C0|jmp|0x6d36f6
22F390|cmp|dword ptr [rcx + 0xc30], 1
22F39F|mov|eax, edx
22F3A1|shr|eax, 0xe
22F3A8|bts|edx, 0xd
22F3AC|mov|dword ptr [rcx + 0xc34], edx
233C53|mov|ecx, dword ptr [rbx + 0xaf0]
233C7F|mov|rdx, rdi
233C82|mov|rcx, rbx
233C92|jmp|0x234420
234437|cmp|dword ptr [rcx + 0xbc0], 1
23446E|mov|eax, dword ptr [rcx + 0xaf4]
234478|bt|eax, 0xd
234482|bts|eax, 0xe
234486|mov|dword ptr [rcx + 0xaf4], eax
35021C|call|0x6d0d90
35022A|mov|qword ptr [rdi + 0x1a60], rax
350238|mov|qword ptr [rdi + 0x1a68], rsi
35F175|call|0x3500c0
35F1E5|mov|rdi, qword ptr [rsi + 0x1a60]
"""
for line in MARKERS.strip().splitlines():
    at,mn,op=line.split('|',2);ins(int(at,16),mn,op)
address(0x6D33D0,0x2022E80);address(0x6D33D7,0x2006450)
check(b[0x6D31BA:0x6D31C5]==bytes.fromhex('48 8b 01 ff 50 60 e9 31 05 00 00'),'exact eleven-byte shared refresh relay site')
# Preparation must remove pre-existing AI work before a lease admits new shots.
# These are independent actual-image checks, not source guards for an unfinished
# preparation adapter. Both routines take the native battery as this and return
# void; acceptance is established from native state, never a leftover AL value.
baseline_start = check.__globals__['n']
BASELINE_PRINTS = {
    0x6D3EC0: '40 53 48 83 ec 20 83 a1 c0 02 00 00 fe 48 8b d9',
    0x6D4040: '48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57',
}
for at,raw in BASELINE_PRINTS.items():
    check(p.get_data(at,16)==bytes.fromhex(raw),('preparation native entry fingerprint',hex(at)))
BASELINE_MARKERS = """
6D3EC6|and|dword ptr [rcx + 0x2c0], 0xfffffffe
6D3ECD|mov|rbx, rcx
6D3ED0|call|0x6d3d80
6D3ED5|xor|eax, eax
6D3ED7|mov|rcx, rbx
6D3EDA|mov|dword ptr [rbx + 0x2e8], eax
6D3EE0|mov|dword ptr [rbx + 0x2e0], eax
6D3EE6|lea|edx, [rax + 0x10]
6D3EEE|jmp|0x7a8cd0
6D3D9A|mov|rbp, qword ptr [rcx + 0x278]
6D3DA1|imul|r14, qword ptr [rcx + 0x288], 0xd0
6D3DC0|mov|rax, qword ptr [rbp + 0x20]
6D3DCD|mov|rdi, qword ptr [rax + 0x20]
6D3DD1|imul|rsi, qword ptr [rax + 0x30], 0x58
6D3E31|call|0x2edde0
6D3E72|add|rdi, 0x58
6D3E7F|add|rbp, 0xd0
6D3E89|jne|0x6d3dc0
6D4054|mov|rsi, qword ptr [rcx + 0x278]
6D405B|imul|rbp, qword ptr [rcx + 0x288], 0xd0
6D4072|lea|rdi, [rsi + 0xb8]
6D4089|mov|rdx, qword ptr [rdi]
6D4091|mov|eax, dword ptr [rdx + 8]
6D40A3|lock cmpxchg|dword ptr [rdx + 8], ecx
6D40C1|call|0x118a1b0
6D40D1|lock xadd|dword ptr [rbx + 8], eax
6D40ED|lock xadd|dword ptr [rbx + 0xc], eax
6D4102|add|rsi, 0xd0
6D4109|add|rdi, 0xd0
6D4113|jne|0x6d4080
6D412D|ret|
6D412E|mov|rcx, qword ptr [rdi - 8]
6D413F|jmp|0x6d40bc
34F913|mov|ecx, dword ptr [rcx + 0x38]
34F916|test|ecx, ecx
34F918|js|0x34f92c
34F91A|lea|eax, [rcx - 1]
34F91D|mov|dword ptr [rbx + 0x38], eax
34F920|jne|0x34f92c
34F922|mov|rax, qword ptr [rbx]
34F925|or|dword ptr [rax + 0x2c0], 1
58EB50|xor|ecx, ecx
58EB52|mov|qword ptr [rdx], rcx
58EB55|mov|qword ptr [rdx + 8], rcx
58EB59|mov|rcx, qword ptr [rbx + 8]
58EB5D|mov|qword ptr [rbx], rax
58EB60|mov|qword ptr [rbx + 8], r8
58EB6E|lock xadd|dword ptr [rcx + 0xc], eax
"""
for line in BASELINE_MARKERS.strip().splitlines():
    at,mn,op=line.split('|',2);ins(int(at,16),mn,op)
# The original delete-cache loop only writes stack temporaries and reference
# counters. It does NOT reset either cached weak slot. A still-lockable deleted
# cache would make the next native GB2 tick refresh/skip its real factory again.
capstone = shared['capstone']
delete_loop = list(d.disasm(b[0x6D4040:0x6D4144],0x6D4040))
check(delete_loop and delete_loop[-1].address+delete_loop[-1].size==0x6D4144,
      'complete native cached-beam delete routine decoded')
direct_calls=[]
for item in delete_loop:
    if item.mnemonic=='call' and item.operands[0].type==capstone.x86.X86_OP_IMM:
        direct_calls.append(item.operands[0].imm)
    for operand in item.operands:
        if operand.type==capstone.x86.X86_OP_MEM and operand.access&capstone.CS_AC_WRITE:
            base_name=item.reg_name(operand.mem.base)
            check(base_name=='rsp' or (base_name=='rbx' and operand.mem.disp in (8,12)),
                  ('delete-cache writes only stack/refcounts, never cache slots',hex(item.address),item.op_str))
check(direct_calls==[0x118A1B0],'delete-cache has only native SceneObject Delete as a direct call')
# Stop cannot cancel the separate Large controller's scheduled start. A delay
# reaching zero re-arms battery2C0 even after 6D3EC0 cleared it, hence preparation
# must also use the constructor's -1 unscheduled controller state.
ins(0x34D20A,'mov','ebp, 0xffffffff');ins(0x34D20F,'mov','dword ptr [rcx + 0x38], ebp')
print('Walking preparation baseline:',check.__globals__['n']-baseline_start,'actual native checks')

# All concrete asset bullet factories preserve BulletBase's void(this) refresh.
factory_classes = {
 'PulseBullet01':(0x17A0F80,0x25E3D0,0x25E6D9,0x17A0FC0),
 'EfsBullet':(0x179E2D0,0x2389F0,0x23926A,0x179E368),
 'BeamBullet01':(0x179F7C8,0x24BF70,0x24C25A,0x179F818),
 'SolidBullet02':(0x17A4208,0x28C3B0,0x28C504,0x17A4298),
 'PlasmaBullet01':(0x17A0B38,0x25B580,0x25B6A5,0x17A0BC0),
 'RocketBullet01':(0x17A3878,0x286B30,0x286CAB,0x17A38E8),
}
for name,(vt,create,base_call,body_vt) in factory_classes.items():
    pointer(vt+16,create);ins(base_call,'call','0x22e9c0');pointer(body_vt+96,0x22F390)
    getter=struct.unpack_from('<Q',b,vt+8)[0]-p.OPTIONAL_HEADER.ImageBase
    x=shared['instruction'](getter)
    check(x.mnemonic=='lea' and b[getter+7]==0xC3,('native factory name getter',name))
    target=x.address+x.size+x.operands[1].mem.disp
    check(b[target:target+len((name+'\0').encode('utf-16le'))]==(name+'\0').encode('utf-16le'),('actual native factory identity',name))

# Scope/ownership integration: only the shared dispatcher changes code bytes.
check(not re.search(r'\b(?:RedirectCall|PatchCode|PatchVtableSlot|VirtualProtect|AllocateNearCode)\s*\(',source),'provider installed a private competing hook')
check(re.findall(r'RegisterNativeUnitFactoryCallsite\((0x[0-9A-F]+)\)',source)==['0x6D331D'],'provider owns only its exact registered native factory site')
for token in ['RegisterNativeUnitFactoryProvider(kUnitProvider)','RegisterNativeBeamRefreshProvider(kRefreshProvider)',
              'RegisterNativeBeamStepProvider(kStepProvider)','ReplayNativeProjectileSampleInPlace(',
              'At<const void*>(p.battery.object,0x650)','At<const void*>(p.battery.object,0x658)',
              'InvokeOriginalNativeBeamRefresh','InvokeOriginalNativeBeamStep','r.native','NativeProjectileJournalEmpty']:
    check(token in source,('Walking native bridge integration',token))
for token in ['kBeamSite=0x6D31BA','kBeamStepSite=0x233C92','kBeamStep=0x234420',
              'static_cast<unsigned char*>(state)-0x140','static_cast<unsigned char*>(bullet)+0x140',
              'matches>1','if(!selected)','selected->Invoke']:
    check(token in central,('central scoped native dispatch',token))
# Both modules consume exactly the independently proven portable scalar list.
for name,expected in [('kScalar32',shared['scalar32']),('kScalar8',shared['scalar8'])]:
    match=re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]+)\}',source)
    check(match and [int(x.strip(),0) for x in match.group(1).split(',')]==expected,('Walking typed scalar list',name))

G=rootcpk.Game(str(game));large=[];middle=[];shooter=[];factories=set()
def gun_asset(label,battery):
    shape=battery['muzzle']['shape'];names=shape if isinstance(shape,list) else [shape]
    check(1<=len(names)<=16 and all(isinstance(x,str) and x for x in names),(label,'real configured native muzzles'))
    factory=battery['bullet']['factory'];check(factory in factory_classes,(label,'native factory supported',factory));factories.add(factory)
    check(all(k in battery['fire'] for k in ('count','interval')) and 'time' in battery['charge'],(label,'native charge/burst fields'))
for name in G.names('OBJECT'):
    if not name.upper().startswith('E602_BERSERKER') or not name.upper().endswith('.SGO'):continue
    value=sgo.load(data=G.read('OBJECT',name));family=value.get('xgs_scene_object_class')
    if family=='EDF6_Berserker_Large':
        large.append(name);guns=value['gun_control'];check(len(guns)==4,(name,'four native stable gun ordinals'))
        for i,g in enumerate(guns):gun_asset((name,i),g['gun_battery'])
    elif family in ('EDF6_Berserker_Middle','EDF6_Berserker_TypeShooter'):
        (middle if family.endswith('_Middle') else shooter).append(name);gun_asset(name,value['gun_battery_settings'])
        check(len(value['gun_battery_settings']['muzzle']['shape'])==(2 if family.endswith('_Middle') else 1),(name,'actual head muzzle count'))
        if family.endswith('_TypeShooter'):check(value['gun_battery_settings']['bullet']['factory']=='BeamBullet01',(name,'real beam factory'))
ports=[r for r in edf41_objects.registry()['objects'] if r.get('class')=='EDF6_Berserker_Large']
for row in ports:
    value=sgo.load(data=edf41_objects.read(row['object']));guns=value['gun_control']
    check(len(guns)==2 and len(value['edf41_hector']['ranges'])==2,(row['object'],'original arm slots, including passive shield'))
    for i,g in enumerate(guns):gun_asset((row['object'],i),g['gun_battery'])
check(len(large)==4 and len(ports)==21,('25 Large stock/ported bodies',large,len(ports)))
check(len(middle)==7 and len(shooter)==4,('11 original Middle/Shooter bodies',middle,shooter))
check(factories==set(factory_classes),('actual configured factory coverage',factories))
print('Walking net native/asset audit:',check.__globals__['n']-initial,'additional checks; shared projectile ABI:',initial,'checks; 25 Large + 11 Middle/Shooter assets')
