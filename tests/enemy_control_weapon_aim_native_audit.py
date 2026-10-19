"""Read-only supported-native weapon aim contracts; never loads or drives the game."""
from pathlib import Path
import re
import struct
import sys
import json

if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass supported EDF.dll');raise SystemExit(77)
if not __debug__:raise RuntimeError('Run without -O')
import pefile
import capstone
ROOT=Path(__file__).resolve().parents[1]
pe=pefile.PE(sys.argv[1],fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46 and pe.FILE_HEADER.Machine==0x8664
img=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
source=(ROOT/'src/enemy_control_weapon_aim.cpp').read_text(encoding='utf-8-sig')
checks=1

def check(ok,why):
    global checks
    assert ok,why
    checks+=1

def blob(text):return bytes(int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b',text))
def target(at,expected):
    check(img[at] in (0xE8,0xE9) and at+5+struct.unpack_from('<i',img,at+1)[0]==expected,f'original native branch {at:X}->{expected:X}')

prints=source.split('constexpr Print kPrints[]={',1)[1].split('};',1)[0]
for at,raw in re.findall(r'\{0x([0-9A-F]+),\{([^}]+)\}\}',prints):
    at=int(at,16);want=blob(raw);check(len(want)==16 and img[at:at+16]==want,f'production helper fingerprint {at:X}')
for table in ('kInlineSites','kStackSites'):
    body=source.split(f'constexpr {"InlineSite" if table=="kInlineSites" else "StackSite"} {table}[]={{',1)[1].split('};',1)[0]
    for row in re.findall(r'\{0x([0-9A-F]+),([^{}]+)\{([^}]+)\}([^}]*)\}',body):
        at=int(row[0],16);want=blob(row[2]);check(img[at:at+len(want)]==want,f'whole stolen instruction window {at:X}')

sites=re.search(r'constexpr unsigned sites\[\]=\{([^}]+)\};\s*constexpr unsigned originals\[\]=\{([^}]+)\};',source)
check(sites is not None,'typed hook table')
site_list=[int(x,0) for x in sites[1].split(',')];originals=[int(x,0) for x in sites[2].split(',')]
check(len(site_list)==len(originals),'typed ABI table extent')
for at,original in zip(site_list,originals):target(at,original)

# Large's two actual emitter paths use the same float4 callback; no target object escapes that function.
target(0x34E231,0x34E470);target(0x34F978,0x34E470);target(0x34E484,0x30C0F0)
check(img[0x34E480:0x34E484]==bytes.fromhex('488b4910'),'real gun-control owner +10')
# Shared native unit owns a real actor weak pair; point-mode math never consumes an AI target or its velocity.
target(0x6D550B,0x6D1800)
check(img[0x6D51FD:0x6D5204]==bytes.fromhex('4c8b81b8020000'),'unit owner weak +2B8')
check(img[0x6D5222:0x6D5229]==bytes.fromhex('498b87b0020000') or img[0x6D5222:0x6D5229]==bytes.fromhex('488b87b0020000'),'unit owner object +2B0')
m=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
unit=list(m.disasm(img[0x6D1800:0x6D20F3],0x6D1800))
check(not any('0x518]' in i.op_str or '0x520]' in i.op_str for i in unit),'point solver never reads actor AI weak target')
check(not any(i.mnemonic=='call' and i.op_str=='0x30c0f0' for i in unit),'point solver never obtains a target object')
check(any(i.address==0x6D1989 and i.op_str=='xmm2, xmmword ptr [rbx + 0x2d0]' for i in unit),'native point-mode coordinate')
# Preserve an explicitly authored fixed vector; this is NOT an AI target cache.
check('unit)+0x250' not in source and 'unit,0x250' not in source,'no global fixed-vector erasure')
# Inline emitters retain the original native solver after coordinate selection, including no-object blind fire.
for at,destination in ((0x40DD1D,0x40DFA7),(0x4246EF,0x424980),(0x33AF45,0x33B1CA),(0x52C2A6,0x52C537),(0x5351F0,0x53537B)):
    check(img[at:at+2]==b'\x0f\x84' and at+6+struct.unpack_from('<i',img,at+2)[0]==destination,f'original no-target branch {at:X}')
for at in (0x40DD44,0x424716,0x33AF6C,0x52C2CC):
    check(img[at:at+3]==bytes.fromhex('0f1010'),f'original target float4 load {at:X}')
# Spider and Nephila still call the native speed/gravity helper; their merge hooks recompute it with the canonical point.
target(0x43612E,0x2312A0);target(0x4E3707,0x2312A0)
# EX families inherit base attack callbacks and fields, rather than replacing them with only their extra unit.
for at,ctor in ((0x416508,0x4057C0),(0x42544A,0x41AAA0),(0x43DEB8,0x433EC0)):target(at,ctor)
# Ordinary humanoid weapon ownership and per-hand physical aim, before the final native gravity/spread emitter.
target(0x46F96C,0x4A1D40);target(0x4A064D,0x2312A0);target(0x4A0ECD,0x1194280)
check(img[0x4A1D40:0x4A1D4B]==bytes.fromhex('0f10020f118120070000c3'),'native weapon target setter only copies a float4')
# Boss target-free paths retain mode 0/2 dispatch, original speed/gravity calculation and a genuine/empty lock weak pair.
target(0x44E39B,0x231110);target(0x44E40A,0x548EF0);target(0x44E4B8,0x6CFC60)
check(img[0x44E400:0x44E403]==bytes.fromhex('4533c0'),'Boss original third weak-constructor argument is zero')
check(img[0x548EF3:0x548EFA]==bytes.fromhex('4c3b8978030000'),'native weak constructor directly dereferences actor+378; null is forbidden')
# Monster look is its real bone/neck solver, not the unrelated 6DC8B0 material curve update.
target(0x4BD3C1,0x4B9FC0);target(0x4BA0B5,0x6BB5A0)
check(img[0x4BA24B:0x4BA252]==bytes.fromhex('0f1093b0060000'),'monster solver consumes its canonical scratch after the locator call')
check('0x6DC8B0' not in source,'material curve update is not misidentified as a target solver')
# Boss meteor canonical direction must precede native packet serialization, then local/guest share that value.
check(img[0x443A93:0x443AA1]==bytes.fromhex('488d8e20010000e8510e330084c0'),'Boss complete pre-sender direction gate instruction window')
target(0x443A9A,0x7748F0);target(0x443B1C,0x12B5200);target(0x443B5E,0x448230);target(0x44649F,0x448230)
check(img[0x443B12:0x443B17]==bytes.fromhex('488d542420'),'native sender serializes the same actual rsp+20 direction')
check(img[0x443B51:0x443B56]==bytes.fromhex('4c8d442420'),'native local factory receives the same actual rsp+20 direction')
meteor_delegate=source.split('void __fastcall BossMeteor(',1)[1].split('void* meteorCode',1)[0]
check('Point(' not in meteor_delegate and 'image+0x448230' in meteor_delegate,'no post-packet second reticle evaluation/local re-aim')
# Deiroi main batteries are separate from the feet. Their real vector rows have no SceneObject control.
for at,original in ((0x31CB8B,0x6BB5A0),(0x31CC0B,0x6BB5A0),(0x31CC3B,0x30C0F0),(0x31CC9D,0x2312A0),(0x31CCCB,0x4E820),(0x31CCDB,0x4E220),(0x31CD29,0x6CFC60)):
    target(at,original)
check(img[0x31CC4B:0x31CC5A]==bytes.fromhex('4885c0745a488b8f20010000488b11'),'Deiroi main whole target/null/parameter-getter instruction window')
check(img[0x31F192:0x31F199]==bytes.fromhex('488baf38160000'),'Deiroi real main battery vector root+1638')
check(img[0x31F22E:0x31F235]==bytes.fromhex('4881c590020000'),'Deiroi native main battery row stride290')
check('Jump(b,image+0x31CCAA)' in source and 'Dword(b,0x170)' in source,'controlled main gun keeps actual native muzzle and original post-solver spread/RNG')
# No global GetTarget detour or replacement/fabricated GameObject is installed.
check('0x30C0F0}' not in source and 'reinterpret_cast<PointFn>(image+0x30C0F0)' not in source,'real actor weak-target getter remains native')
# The admitted family set is exact and each family has a concrete projectile or explicitly physical/transport contract.
contract=json.loads((ROOT/'tests/enemy_control_weapon_aim_contract.json').read_text(encoding='utf-8'))
profiles=re.findall(r'\{"([^"]+)"', (ROOT/'src/enemy_control_native_profiles.inc').read_text(encoding='utf-8-sig'))
ready_table=source.split('constexpr const char* complete[]={',1)[1].split('};',1)[0]
ready_names=re.findall(r'"([^"]+)"',ready_table)
family_rows=contract['families']
check(len(profiles)==34 and set(profiles)==set(ready_names)=={r['name'] for r in family_rows},'all 34 exact native classes require an explicit real aim route')
check(len(ready_names)==len(set(ready_names)),'no duplicate admission row')
check('enemy_native::DeiroiWeaponAimReady()' in source,'foot-gun adapter readiness is a required separate capability')
check('WalkingAcceptedAimPoint(owner,point)' in source,'current accepted native burst uses the host-frozen point')
check('if(!Integrity())return false;' in source and 'for(const auto& p:kPrints)' in source.split('bool Integrity()',1)[1].split('} // namespace',1)[0],'later foreign patches or helper changes revoke admission')
for at,expected in ((0x32D286,0x32C310),(0x32B422,0x3296E0),(0x32CFE8,0x6CFC60),(0x340424,0x351940)):
    target(at,expected)
check(img[0x3C466C:0x3C467A]==bytes.fromhex('4885d2751d33c0c741180000803f'),'Shell original complete null/real-target instructions')
check(img[0x44E1E6:0x44E1F5]==bytes.fromhex('488b9320050000beffffffff4885d2'),'Boss original actor-weak gate complete instructions')
check(img[0x44E36F:0x44E380]==bytes.fromhex('4983be780300000075094d8d8690000000'),'Boss original point/null-actor window complete instructions')
# Read stock resources straight from Root.cpk when it accompanies the explicitly supplied native image.
# This is data access only; no DLL loading, game process, archive writes or extracted asset changes.
archive=Path(sys.argv[1]).parent/'Root.cpk'
resource_count=0
if archive.is_file():
    sys.path.insert(0,str(ROOT/'pylib'))
    import rootcpk,sgo
    game=rootcpk.Game(str(archive.parent))
    def walk(value,path=''):
        if isinstance(value,dict):
            for key,item in value.items():
                yield path+'.'+key,item
                yield from walk(item,path+'.'+key)
        elif isinstance(value,list):
            for index,item in enumerate(value):yield from walk(item,path+f'[{index}]')
    for row in family_rows:
        check(bool(row['stock_objects']) and bool(row['sites']) and bool(row['routes']),row['name']+' concrete stock contract')
        for name in row['stock_objects']:
            obj=sgo.load(data=game.read('OBJECT',name));resource_count+=1
            check(obj.get('xgs_scene_object_class')==row['name'],name+' actual native class')
            fields=list(walk(obj))
            if row['name'] in ('AlienTrailer','EDF6_TimeShip','DropBoat512','UfoCarrier508'):
                check(not any(any(key in path.lower() for key in ('bullet','weapon','shoot')) for path,value in fields),name+' original physical/barrier/transport, no projectile settings')
            if row['name']=='EDF6_Berserker_TypeBommer':
                parts=obj.get('BerserkerSettings',{}).get('PartsShootList',[])
                check(parts==[] and {b['node'] for b in obj['Bombs']}=={'hand_l','hand_r'},name+' real attached bombs, no aimed detached projectile')
            if row['name'] in ('GiantAntEX','GiantBeeEX','GiantSpiderEX','EDF6_Berserker_Middle','EDF6_Berserker_TypeShooter','EDF6_GiantDangoLarge','EDF6_FlyingDyno'):
                check(not any(path.endswith('.fix_vector') for path,value in fields),name+' direct batteries have no authored fixed-vector override')
    check(resource_count==183,'all admitted stock variants audited')
print(f'{checks} native weapon-aim contracts passed; {len(site_list)} typed calls, 34 families, {resource_count} stock resources; no game execution')
