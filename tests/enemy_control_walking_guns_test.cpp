// Production independent Large gun adapter on private memory, with real identity
// and native ABI seams. No game process or installed DLL is used.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include "../src/enemy_control_walking_guns.h"
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew {unsigned char* image=nullptr;}
#include "../src/enemy_control_walking_guns.cpp"
namespace {
using namespace crew;
using namespace crew::enemy_native;
using namespace crew::enemy_native::walking_guns;
int checks=0,failures=0;unsigned starts[kMaxGuns]{},stops[kMaxGuns]{},seeds=0;
bool active=true,aimReady=true,havePoint=true,session=false,roleKnown=false,isAuthority=false,clearLeaseOnSeed=false,throwStart=false,contextKnown=true,contextOnline=false;
bool marker=false,badMarker=false,refuseStart=false,throwStop=false,clearOwnerDuringStart=false,canQueue=true,queueResult=true;
unsigned queued=0;bool restoreDuringStart=false,restoreDuringStop=false,nestedStart=false;unsigned reentrantRefusals=0;bool nestedResult=false;
float canonical[3]{50,60,70},ranges[2]{400,500};
alignas(16) unsigned char body[0x1AE0]{},weak[32]{},gunBlocks[kMaxGuns][0xA0]{},batteryBlocks[kMaxGuns][0x6D0]{},rows[kMaxGuns][16]{};
NativeProfile profiles[]={
    {"EDF6_Berserker_Large",0x17ADA78,0,0,123,86,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_Middle",0x17AE088,0,0,124,86,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeShooter",0x17AF9F8,0,0,125,86,0x31,Kind::walking,7,4}};
NativeProfile& profile=profiles[0];
void* weakTable[2]{};
alignas(16) unsigned char definitions[2][8192]{};unsigned nodeMap[64]{};
void Check(bool yes,const char* label) {++checks;if(!yes){++failures;std::printf("FAIL %s\n",label);}}
unsigned char* GunAt(unsigned ordinal){return gunBlocks[ordinal]+0x10;}
unsigned char* BatteryAt(unsigned ordinal){return batteryBlocks[ordinal]+0x10;}
unsigned Ordinal(const void* gun) {
    for(unsigned i=0;i<kMaxGuns;++i)if(gun==GunAt(i))return i;
    return kMaxGuns;
}
void Jump(unsigned char* at,const void* fn){at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0;}
void __fastcall Destroy(void*){}
bool __fastcall NativeBusy(const void* pointer) {
    const auto gun=static_cast<const unsigned char*>(pointer);const auto battery=At<const unsigned char*>(gun,0);
    if(At<int>(gun,0x38)>0)return true;
    const unsigned left=At<unsigned>(battery,0x2E0);
    return ((At<unsigned>(battery,0x2C0)&1) && left==0 ? At<unsigned>(battery,0x30) : left)!=0;
}
void __fastcall NativeStart(void* pointer) {
    auto* gun=static_cast<unsigned char*>(pointer);const auto i=Ordinal(gun);
    if(throwStart)RaiseException(0xE0420003,0,0,nullptr);
    if(i==kMaxGuns || NativeBusy(gun) || refuseStart)return;
    ++starts[i];
    if(nestedStart) {NativeLease lease{};lease.object=body;lease.control=weak;lease.profile=&profile;nestedResult=ExecuteWalkingGun(lease,i,2,22);}
    if(restoreDuringStart) {NativeLease lease{};lease.object=body;lease.control=weak;lease.profile=&profile;if(!RestoreWalkingGunControl(lease))++reentrantRefusals;}
    float point[3]{};
    if(!WalkingGunAimPoint(gun,point))RaiseException(0xE0420001,0,0,nullptr);
    Put<int>(gun,0x38,static_cast<int>(i*60));
    std::memcpy(gun+0x50,point,sizeof(point));std::memcpy(gun+0x60,point,sizeof(point));std::memcpy(gun+0x70,point,sizeof(point));
    if(clearOwnerDuringStart)Put<const void*>(body,0x30,nullptr);
}
void __fastcall NativeStop(void* pointer) {
    if(throwStop)RaiseException(0xE0420002,0,0,nullptr);
    if(restoreDuringStop) {NativeLease lease{};lease.object=body;lease.control=weak;lease.profile=&profile;if(!RestoreWalkingGunControl(lease))++reentrantRefusals;}
    auto* battery=static_cast<unsigned char*>(pointer);unsigned i=kMaxGuns;
    for(unsigned j=0;j<kMaxGuns;++j)if(battery==BatteryAt(j)){i=j;break;}
    if(i<kMaxGuns)++stops[i];
    Put<unsigned>(battery,0x2C0,At<unsigned>(battery,0x2C0)&~1u);
    Put<unsigned>(battery,0x2E8,0);Put<unsigned>(battery,0x2E0,0);
}
Handle* __fastcall NativeChild(const Handle* parent,Handle* out,const wchar_t* key) {
    *out={nullptr,0xFFFFFFFFu,0};
    if(parent->tree==body+0xA8 && parent->index==0 && !std::wcscmp(key,L"gun_control"))*out={body+0xA8,1,0};
    if(parent->tree==body+0xA8 && parent->index==0 && !std::wcscmp(key,L"gun_battery_settings"))*out={body+0xA8,2,0};
    if(marker && parent->index==0 && !std::wcscmp(key,L"edf41_hector"))*out={body,1,0};
    if(parent->index==1 && !std::wcscmp(key,L"ranges"))*out={body,2,0};
    return out;
}
int __fastcall NativeCount(const Handle*){return badMarker ? 3 : 2;}
unsigned __fastcall NativeIndex(const Handle*,unsigned i){return i+3;}
float __fastcall NativeFloat(const Handle* value){return value->index>=3 && value->index<=4 ? ranges[value->index-3] : 0;}
void BuildDefinition(unsigned char* raw,unsigned count,bool moved) {
    std::memset(raw,0,8192);const unsigned nodeCount=2+count*6;
    Put<unsigned>(raw,0,0x4F475344);Put<unsigned>(raw,4,16);Put<unsigned>(raw,8,nodeCount);Put<unsigned>(raw,12,16);
    unsigned cursor=16+nodeCount*16+(moved ? 64 : 0);
    const auto text=[&](const wchar_t* value) {
        const unsigned at=cursor;const auto bytes=static_cast<unsigned>((std::wcslen(value)+1)*2);
        std::memcpy(raw+cursor,value,bytes);cursor+=bytes;return at;
    };
    const auto stringNode=[&](unsigned index,const wchar_t* value) {
        const unsigned node=16+index*16,at=text(value);
        Put<int>(raw,node,static_cast<int>(at-node));Put<unsigned>(raw,node+8,1);
    };
    const auto container=[&](unsigned index,const unsigned* children,unsigned childrenCount,const wchar_t* const* names) {
        const unsigned node=16+index*16,block=cursor;
        cursor+=16+childrenCount*4+(names ? childrenCount*8 : 0);
        Put<int>(raw,node,static_cast<int>(block-node));Put<unsigned>(raw,node+8,3);
        Put<unsigned>(raw,block,names ? 16+childrenCount*4 : 0);Put<unsigned>(raw,block+4,names ? childrenCount : 0);
        Put<unsigned>(raw,block+8,childrenCount ? 16 : 0);Put<unsigned>(raw,block+12,childrenCount);
        for(unsigned i=0;i<childrenCount;++i) {
            Put<unsigned>(raw,block+16+i*4,children[i]);
            if(names) {const unsigned entry=block+16+childrenCount*4+i*8,at=text(names[i]);Put<int>(raw,entry,static_cast<int>(at-entry));Put<unsigned>(raw,entry+4,i);}
        }
    };
    const unsigned one[]={1};const wchar_t* rootNames[]={L"gun_control"};container(0,one,1,rootNames);
    unsigned gunIndices[kMaxGuns]{};for(unsigned i=0;i<count;++i)gunIndices[i]=2+i*6;container(1,gunIndices,count,nullptr);
    for(unsigned i=0;i<count;++i) {
        const unsigned gun=2+i*6,children[]={gun+1,gun+2,gun+3,gun+4,gun+5};
        const wchar_t* names[]={L"factory",L"charge",L"expression",L"muzzle",L"mode"};container(gun,children,5,names);
        stringNode(gun+1,L"PulseBullet01");Put<double>(raw,16+(gun+2)*16,300.0);Put<unsigned>(raw,16+(gun+2)*16+8,0);
        const unsigned blobNode=16+(gun+3)*16,block=cursor;cursor+=12;
        Put<int>(raw,blobNode,static_cast<int>(block-blobNode));Put<unsigned>(raw,blobNode+8,4);
        Put<unsigned>(raw,block,4);Put<unsigned>(raw,block+4,8);raw[block+8]=0x54;raw[block+9]=0x33;raw[block+10]=0x12;raw[block+11]=0x21;
        wchar_t muzzle[32]{};_snwprintf_s(muzzle,_TRUNCATE,L"muzzle_%u",i);stringNode(gun+4,muzzle);
        Put<double>(raw,16+(gun+5)*16,1.0);Put<unsigned>(raw,16+(gun+5)*16+8,0);
    }
}

NativeLease Fixture(unsigned count=4) {
    std::memset(body,0,sizeof(body));std::memset(weak,0,sizeof(weak));std::memset(gunBlocks,0,sizeof(gunBlocks));
    std::memset(batteryBlocks,0,sizeof(batteryBlocks));std::memset(rows,0,sizeof(rows));
    std::memset(starts,0,sizeof(starts));std::memset(stops,0,sizeof(stops));
    weakTable[0]=reinterpret_cast<void*>(&Destroy);weakTable[1]=reinterpret_cast<void*>(&Destroy);
    Put<const void*>(weak,0,weakTable);Put<LONG>(weak,8,1);Put<LONG>(weak,12,1);Put<const void*>(body,0x30,weak);
    Put<const void*>(body,0x1AA8,rows);Put<std::size_t>(body,0x1AB0,count);Put<std::size_t>(body,0x1AB8,count);
    for(unsigned i=0;i<count && i<kMaxGuns;++i) {
        Put<const void*>(gunBlocks[i],0,weakTable);Put<LONG>(gunBlocks[i],8,1);Put<LONG>(gunBlocks[i],12,1);
        Put<const void*>(batteryBlocks[i],0,weakTable);Put<LONG>(batteryBlocks[i],8,1);Put<LONG>(batteryBlocks[i],12,1);
        Put<const void*>(rows[i],0,GunAt(i));Put<const void*>(rows[i],8,gunBlocks[i]);
        Put<const void*>(GunAt(i),0,BatteryAt(i));Put<const void*>(GunAt(i),8,batteryBlocks[i]);
        Put<const void*>(GunAt(i),0x10,body);Put<int>(GunAt(i),0x38,-1);
        Put<const void*>(BatteryAt(i),0x2B0,body);Put<const void*>(BatteryAt(i),0x2B8,weak);Put<unsigned>(BatteryAt(i),0x30,90);
    }
    active=true;aimReady=true;havePoint=true;session=false;roleKnown=false;isAuthority=false;clearLeaseOnSeed=false;throwStart=false;contextKnown=true;contextOnline=false;
    marker=false;badMarker=false;refuseStart=false;throwStop=false;clearOwnerDuringStart=false;canQueue=true;queueResult=true;queued=0;restoreDuringStart=false;restoreDuringStop=false;nestedStart=false;nestedResult=false;reentrantRefusals=0;ranges[0]=400;ranges[1]=500;
    canonical[0]=50;canonical[1]=60;canonical[2]=70;seeds=0;
    if(count<=kMaxGuns) {BuildDefinition(definitions[0],count,false);BuildDefinition(definitions[1],count,true);}
    Put<const void*>(body+0xA8,0x50,definitions[0]);
    for(unsigned& item:nodeMap)item=UINT32_MAX;
    NativeLease lease{};lease.object=body;lease.control=weak;lease.profile=&profile;return lease;
}
}
namespace crew {
bool InSession() noexcept{return session;}
bool EnemyControlNetworkContext(const void* pointer,bool* online) noexcept {
    if(pointer!=body || !online || !contextKnown)return false;
    *online=contextOnline;return true;
}
bool EnemyControlTargetActive(const void* pointer) noexcept{return active && pointer==body;}
bool EnemyControlWeaponAim(const void* pointer,float out[3]) noexcept {
    if(pointer!=body || !havePoint)return false;
    std::memcpy(out,canonical,sizeof(canonical));return true;
}
bool EnemyControlWeaponAimReady(const enemy_native::NativeProfile* value) noexcept{return aimReady && value==&profile;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{*count=3;return profiles;}
bool InstallWalkingAcceptedActions() noexcept{return true;}
bool WalkingAcceptedCanQueue(const NativeLease&) noexcept{return canQueue;}
bool QueueWalkingAcceptedGun(const NativeLease&,unsigned,unsigned,std::uint64_t,std::uint64_t,const float[3]) noexcept {++queued;return queueResult;}
bool NativeActionRole(const void* pointer,bool* authority) noexcept{*authority=isAuthority;return roleKnown && pointer==body;}
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept {
    ++seeds;Put<std::uint64_t>(lease.object,0x3E8,seed);Put<std::uint64_t>(lease.object,0x490,seed);
    if(clearLeaseOnSeed)active=false;
    return true;
}
}
int main() {
    using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::walking_guns;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    Check(!InstallWalkingGunControl(),"unsupported code fails exact native admission");
    for(const auto& print:kPrints)std::memcpy(image+print.at,print.bytes,16);
    Check(InstallWalkingGunControl(),"every exact native signature admits together");
    Jump(image+kStart,reinterpret_cast<const void*>(&NativeStart));Jump(image+kBusy,reinterpret_cast<const void*>(&NativeBusy));
    Jump(image+kStop,reinterpret_cast<const void*>(&NativeStop));Jump(image+0xD7480,reinterpret_cast<const void*>(&NativeChild));
    Jump(image+0x53060,reinterpret_cast<const void*>(&NativeCount));Jump(image+0x52DE0,reinterpret_cast<const void*>(&NativeIndex));
    Jump(image+0x52F30,reinterpret_cast<const void*>(&NativeFloat));FlushInstructionCache(GetCurrentProcess(),image,0x1800000);
    auto lease=Fixture();NativeAbility list[4]{};
    Check(ListWalkingGunAbilities(lease,list,2)==4,"bounded list returns the total actual gun count");
    Check(list[0].argument==0 && list[1].argument==1 && list[1].kind==AbilityKind::walkingGun && !list[1].held && !list[1].pressed && !list[1].action,"individual guns are typed commands with original native ordinals");
    const char* stable=list[1].id;ListWalkingGunAbilities(lease,list,4);Check(stable==list[1].id && !std::strcmp(stable,"walking_gun_1"),"immutable label identity survives repeated enumeration");
    Put<LONG>(gunBlocks[1],8,0);Check(ListWalkingGunAbilities(lease,list,4)==3 && list[1].argument==2,"expired gun does not renumber a surviving native ordinal");Put<LONG>(gunBlocks[1],8,1);
    Put<std::size_t>(body,0x1AB0,3);Check(!ListWalkingGunAbilities(lease,list,4),"invalid native vector capacity fails closed");Put<std::size_t>(body,0x1AB0,4);
    Put<std::size_t>(body,0x1AB8,257);Check(!ListWalkingGunAbilities(lease,list,4),"unsupported oversized table is never truncated");Put<std::size_t>(body,0x1AB8,4);
    NativeProfile copy=profile;lease.profile=&copy;Check(!ListWalkingGunAbilities(lease,list,4),"a copied unadmitted profile is rejected");lease.profile=&profile;
    Put<const void*>(body,0,reinterpret_cast<const void*>(0x1234));Check(ListWalkingGunAbilities(lease,list,4)==4,"the root's legitimate per-object cloned vtable does not break its admitted profile identity");
    std::uint64_t digest=0,again=0;
    Check(WalkingGunDefinition(lease,0,&digest) && digest,"the entire actual gun SGO subtree has a stable semantic definition");
    Put<const void*>(body+0xA8,0x50,definitions[1]);
    Check(WalkingGunDefinition(lease,0,&again) && again==digest,"moving every raw block and pool changes no semantic definition hash");
    Put<const void*>(body+0xA8,0x50,definitions[0]);
    Check(WalkingGunDefinition(lease,1,&again) && again!=digest,"original ordinal and actual per-gun muzzle are part of definition identity");
    Put<double>(definitions[0],16+4*16,301.0);
    Check(WalkingGunDefinition(lease,0,&again) && again!=digest,"literal numeric gun settings are included without float conversion");Put<double>(definitions[0],16+4*16,300.0);
    const unsigned blobNode=16+5*16,blobBlock=blobNode+At<unsigned>(definitions[0],blobNode);
    definitions[0][blobBlock+8]^=1;
    Check(WalkingGunDefinition(lease,0,&again) && again!=digest,"raw expression VM bytes are hashed without current-difficulty evaluation");definitions[0][blobBlock+8]^=1;
    Put<const void*>(body+0xA8,8,nodeMap);Put<std::size_t>(body+0xA8,0x18,26);nodeMap[2]=0;
    Check(!WalkingGunDefinition(lease,0,&again),"an overridden selected native node is rejected instead of hashing stale raw data");nodeMap[2]=UINT32_MAX;
    nodeMap[0]=0;Check(!WalkingGunDefinition(lease,0,&again),"an overridden root lookup cannot be confused with the raw definition");nodeMap[0]=UINT32_MAX;
    nodeMap[8]=0;Check(WalkingGunDefinition(lease,0,&again) && again==digest,"an unrelated gun override does not alter this actual selected subtree");nodeMap[8]=UINT32_MAX;
    const unsigned gunNode=16+2*16,gunBlock=gunNode+At<unsigned>(definitions[0],gunNode),oldChild=At<unsigned>(definitions[0],gunBlock+16);
    Put<unsigned>(definitions[0],gunBlock+16,2);Check(!WalkingGunDefinition(lease,0,&again),"cyclic malformed SGO graphs fail bounded traversal");Put<unsigned>(definitions[0],gunBlock+16,oldChild);
    Put<unsigned>(definitions[0],gunBlock+12,1025);Check(!WalkingGunDefinition(lease,0,&again),"oversized definition containers fail closed without truncation");Put<unsigned>(definitions[0],gunBlock+12,5);
    Put<unsigned>(definitions[0],gunBlock+16,65536);Check(!WalkingGunDefinition(lease,0,&again),"an invalid child index cannot escape the native raw node table");Put<unsigned>(definitions[0],gunBlock+16,oldChild);
    NativeLease resolved{};unsigned key=99;void* gunPointer=nullptr;
    Check(ResolveWalkingControlledGun(GunAt(2),&resolved,&key) && resolved.object==body && resolved.control==weak && key==2 && ResolveWalkingGunKey(lease,2,&gunPointer) && gunPointer==GunAt(2),"accepted codec resolves real gun membership in both directions without serializing pointers");
    Check(WalkingGunReady(lease,0) && !WalkingGunCommitted(lease,0),"accepted readiness and commitment are separate truthful native states");
    Check(WalkingBatteryDefinition(lease,0,&again) && again==digest,"shared battery definition preserves the exact Large ordinal digest");
    Put<const void*>(body,0x1A60,BatteryAt(0));Put<const void*>(body,0x1A68,batteryBlocks[0]);lease.profile=profiles+1;
    Check(WalkingBatteryDefinition(lease,0,&again) && again!=digest,"Middle's actual single root gun_battery_settings uses its family-bound definition");
    Check(!WalkingBatteryDefinition(lease,1,&again),"Middle has no invented second battery ordinal");
    const auto middleDigest=again;lease.profile=profiles+2;
    Check(WalkingBatteryDefinition(lease,0,&again) && again!=middleDigest,"Shooter's family is part of the shared native battery definition");
    Put<const void*>(BatteryAt(0),0x2B8,nullptr);Check(!WalkingBatteryDefinition(lease,0,&again),"shared battery digest also requires the exact native owner weak identity");
    lease=Fixture(2);marker=true;ranges[1]=0;Check(ListWalkingGunAbilities(lease,list,4)==1 && list[0].argument==0,"explicit Hector marker excludes its zero-range shield placeholder");
    unsigned preparationCount=99;void* prepared=nullptr;std::uint64_t preparedDigest=0,abilityDigest=0;
    Check(WalkingGunPreparationCount(lease,&preparationCount) && preparationCount==2,"baseline preparation counts the complete raw roster including a hidden shield slot");
    Check(ResolveWalkingGunPreparationKey(lease,1,&prepared,&preparedDigest) && prepared==GunAt(1) && preparedDigest,"baseline preparation resolves the shield by its real ordinal and full native definition");
    Check(!WalkingGunDefinition(lease,1,&abilityDigest) && !ResolveWalkingGunKey(lease,1,&prepared) && !ExecuteWalkingGun(lease,1,1,11),"read-only preparation access never creates a selectable shield attack");
    Check(ResolveWalkingGunPreparationKey(lease,0,&prepared,&preparedDigest) && WalkingGunDefinition(lease,0,&abilityDigest) && preparedDigest==abilityDigest,"raw preparation and selectable gun definitions share exactly the same digest domain");
    Check(!starts[0] && !starts[1] && !stops[0] && !stops[1] && !seeds && At<LONG>(weak,12)==1,"full preparation metadata queries never trigger, stop or retain a native gun");
    Check(!WalkingGunPreparationCount(lease,nullptr) && !ResolveWalkingGunPreparationKey(lease,1,nullptr,&preparedDigest) && !ResolveWalkingGunPreparationKey(lease,1,&prepared,nullptr),"preparation output pointers are required");
    badMarker=true;Check(WalkingGunPreparationCount(lease,&preparationCount) && preparationCount==2 && ResolveWalkingGunPreparationKey(lease,1,&prepared,&preparedDigest),"baseline cleanup does not depend on optional selector shield metadata");badMarker=false;
    Put<LONG>(gunBlocks[1],8,0);preparationCount=99;
    Check(!WalkingGunPreparationCount(lease,&preparationCount) && preparationCount==99,"an incomplete raw roster fails without publishing a truncated preparation count");Put<LONG>(gunBlocks[1],8,1);
    Put<const void*>(BatteryAt(1),0x2B8,nullptr);prepared=reinterpret_cast<void*>(0x1234);preparedDigest=77;
    Check(!ResolveWalkingGunPreparationKey(lease,1,&prepared,&preparedDigest) && prepared==reinterpret_cast<void*>(0x1234) && preparedDigest==77,"a hidden slot still requires the exact battery owner weak identity and publishes no partial outputs");Put<const void*>(BatteryAt(1),0x2B8,weak);
    Check(!ResolveWalkingGunPreparationKey(lease,2,&prepared,&preparedDigest),"preparation never invents slots beyond the original raw vector");
    marker=false;Check(ListWalkingGunAbilities(lease,list,4)==2,"generic zero range is not inferred to be a shield");
    marker=true;badMarker=true;Check(!ListWalkingGunAbilities(lease,list,4),"malformed explicit Hector marker is not silently accepted");
    lease=Fixture();enemy_control::Ability state{};state.id=123;wcscpy_s(state.name,L"identity");
    Check(ReadWalkingGunState(lease,1,&state) && state.state==enemy_control::AbilityState::native && !state.cooldownKnown && state.id==123 && !std::wcscmp(state.name,L"identity"),"state refresh preserves identity and reports native readiness without made-up seconds");
    Put<unsigned>(BatteryAt(0),0x2E0,7);
    Check(ExecuteWalkingGun(lease,1,1,111) && starts[1]==1 && starts[0]==0,"one ready gun starts independently while another gun remains active");
    Check(seeds==1 && At<std::uint64_t>(body,0x3E8)==111 && At<std::uint64_t>(body,0x490)==111 && At<std::uint64_t>(GunAt(1),0x80)==111,"semantic seed reaches both root RNGs and the separate per-gun aim RNG");
    Check(At<const void*>(body,0x1AA8)==rows && At<std::size_t>(body,0x1AB8)==4,"independent selection never edits native vector identity or network ordinals");
    Check(ReadWalkingGunState(lease,1,&state) && state.state==enemy_control::AbilityState::active && !state.cooldownKnown,"scheduled first-shot delay is active native work rather than invented cooldown seconds");
    Check(ExecuteWalkingGun(lease,1,1,111) && starts[1]==1 && !ExecuteWalkingGun(lease,1,0,111),"duplicate accepted command returns its result exactly once and zero sequence is rejected");
    Check(!ExecuteWalkingGun(lease,1,2,222) && starts[1]==1 && seeds==1,"native busy refusal does not mutate seed or report false acceptance");
    Check(!ExecuteWalkingGun(lease,1,2,222) && starts[1]==1,"a duplicate refused sequence cannot later become an accepted command");
    Check(ExecuteWalkingGun(lease,2,1,333) && starts[2]==1,"the same input sequence may independently select a second native gun");
    float point[3]{};canonical[0]=91;Check(WalkingGunAimPoint(GunAt(1),point) && point[0]==91,"an owned burst follows the last confirmed canonical point");
    havePoint=false;canonical[0]=999;Check(WalkingGunAimPoint(GunAt(1),point) && point[0]==91,"lost canonical input retains its confirmed point and never resumes AI aiming");
    Check(!WalkingGunAimPoint(GunAt(3),point),"unowned whole-gun/native paths are left to their existing aim adapter");
    Check(RestoreWalkingGunControl(lease) && stops[1]==1 && stops[2]==1 && At<int>(GunAt(1),0x38)==-1 && At<LONG>(weak,12)==1 && At<LONG>(gunBlocks[1],12)==1,"restore uses native stop, removes owned delays, and releases retained weak identity");
    Check(At<unsigned>(BatteryAt(0),0x2E0)==7,"restore never cancels an unowned gun's preexisting burst");
    havePoint=true;Check(ExecuteWalkingGun(lease,1,1,111) && starts[1]==2,"a new root lease can reuse input sequence one after explicit restoration");RestoreWalkingGunControl(lease);
    lease=Fixture();havePoint=false;Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0] && !seeds,"new skill cannot start without a confirmed canonical point");havePoint=true;
    Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0],"the same refused missing-point event is not replayed when input returns");
    Check(ExecuteWalkingGun(lease,0,2,11) && starts[0]==1,"a later fresh skill event is accepted once");
    Check(ReadWalkingGunState(lease,0,&state) && state.state==enemy_control::AbilityState::active,"interval zero still represents an accepted native scheduled attack");
    RestoreWalkingGunControl(lease);
    lease=Fixture();refuseStart=true;Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0],"a native void method that refuses to schedule is never reported accepted");
    Check(RestoreWalkingGunControl(lease),"a failed native acceptance retains enough journal to restore");
    lease=Fixture();aimReady=false;Check(!ExecuteWalkingGun(lease,0,1,11),"incomplete canonical consumer installation refuses new fire");aimReady=true;
    active=false;Check(!ExecuteWalkingGun(lease,0,1,11),"an inactive source lease has no gun trigger authority");active=true;
    canonical[0]=std::numeric_limits<float>::quiet_NaN();Check(!ExecuteWalkingGun(lease,0,1,11),"nonfinite canonical aim cannot enter the native weapon path");RestoreWalkingGunControl(lease);
    lease=Fixture();contextKnown=false;
    Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0] && !seeds,"unknown retained controller context never falls through to offline emission");
    contextKnown=true;contextOnline=true;session=true;roleKnown=false;
    Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0],"an immutable online controller context cannot emit without its accepted role");
    roleKnown=true;isAuthority=false;Check(ExecuteWalkingGun(lease,0,1,11) && !starts[0],"a guest consumes input without starting a second local gun lifecycle");
    session=false;contextOnline=false;contextKnown=false;
    Check(ExecuteWalkingGun(lease,0,2,11) && !starts[0],"a bound guest remains networked even when room and unbound context probes cannot prove online");
    session=true;contextOnline=true;contextKnown=true;
    isAuthority=true;canQueue=false;Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0] && !queued,"authority refuses a full accepted queue before changing a native gun");
    canQueue=true;Check(ExecuteWalkingGun(lease,0,2,11) && starts[0]==1 && queued==1,"authority records its actual successful native start in the accepted queue");
    Check(ExecuteWalkingGun(lease,0,2,11) && queued==1,"duplicate authority input cannot duplicate its accepted event");
    RestoreWalkingGunControl(lease);
    lease=Fixture();session=true;roleKnown=true;isAuthority=false;const float acceptedPoint[3]{7,8,9};canonical[0]=999;
    Check(ApplyWalkingGunAcceptedControl(lease,1,0,1,4,123,acceptedPoint) && starts[1]==1 && !queued,"only an accepted guest command enters its native visual lifecycle");
    Check(At<float>(GunAt(1),0x50)==7,"guest native start uses the exact accepted point while reentrant aim queries run");
    Check(ApplyWalkingGunAcceptedControl(lease,1,0,1,4,123,acceptedPoint) && starts[1]==1,"duplicate accepted serial never restarts a native gun");
    const float changedPoint[3]{17,8,9};Check(!ApplyWalkingGunAcceptedControl(lease,1,0,1,4,123,changedPoint),"same accepted serial with a changed point is rejected");
    Check(ApplyWalkingGunAcceptedControl(lease,1,1,2,5,123,acceptedPoint) && !WalkingGunCommitted(lease,1),"an accepted cancel uses native cleanup once without queueing it again");
    Check(ApplyWalkingGunAcceptedControl(lease,1,1,2,5,123,acceptedPoint) && stops[1]==2,"duplicate accepted cancel never repeats native stop");
    RestoreWalkingGunControl(lease);
    lease=Fixture();Check(ExecuteWalkingGun(lease,0,1,11),"cleanup fixture starts the real typed path");
    throwStop=true;Check(!RestoreWalkingGunControl(lease) && At<LONG>(weak,12)==2 && At<LONG>(gunBlocks[0],12)==2,"native cleanup failure preserves root and gun weak journals for retry");
    throwStop=false;Check(RestoreWalkingGunControl(lease) && At<LONG>(weak,12)==1 && At<LONG>(gunBlocks[0],12)==1,"a later successful cleanup completes retained ownership release");
    lease=Fixture();Check(ExecuteWalkingGun(lease,2,1,11),"dead-root cleanup fixture starts");body[0x2E8]=1;
    Check(RestoreWalkingGunControl(lease) && stops[2]==1,"an actually dead root still cancels its owned native burst before releasing identity");
    lease=Fixture();clearLeaseOnSeed=true;
    Check(!ExecuteWalkingGun(lease,0,1,11) && !starts[0],"reentrant ownership loss from seed callbacks is rechecked before native start");
    Check(RestoreWalkingGunControl(lease) && At<LONG>(weak,8)==1 && At<LONG>(gunBlocks[0],8)==1,"reentrant seed refusal unwinds both strong pins and weak journals");
    lease=Fixture();throwStart=true;
    Check(!ExecuteWalkingGun(lease,0,1,11) && At<LONG>(weak,8)==1 && At<LONG>(gunBlocks[0],8)==1,"native start exception unwinds root and gun strong pins");
    throwStart=false;Check(!ExecuteWalkingGun(lease,1,2,11),"a partially failed native start blocks further gun actions until restoration");
    Check(ReadWalkingGunState(lease,0,&state) && state.state==enemy_control::AbilityState::unavailable && !WalkingGunReady(lease,0),"faulted native work is unavailable to both the UI and the accepted admission bridge");
    Check(RestoreWalkingGunControl(lease),"faulted native start retains a recoverable cancellation journal");
    lease=Fixture();restoreDuringStart=true;
    Check(ExecuteWalkingGun(lease,0,1,11) && reentrantRefusals==1 && At<LONG>(weak,8)==1 && At<LONG>(gunBlocks[0],8)==1,"reentrant restore cannot erase the active start transaction or lose its strong-pin identity");
    restoreDuringStart=false;restoreDuringStop=true;
    Check(RestoreWalkingGunControl(lease) && reentrantRefusals==2 && At<LONG>(weak,12)==1,"reentrant native-stop restoration is bounded and preserves the outer restore transaction");
    lease=Fixture();nestedStart=true;
    Check(ExecuteWalkingGun(lease,0,1,11) && !nestedResult && starts[0]==1,"a reentrant newer command is refused before it can overwrite the outer gun journal");
    nestedStart=false;Check(!ExecuteWalkingGun(lease,0,2,22),"retrying a refused nested command cannot inherit the outer accepted result");
    Check(At<std::uint64_t>(GunAt(0),0x80)==11,"a refused nested command never replaces the actually accepted native gun seed");
    RestoreWalkingGunControl(lease);
    lease=Fixture();Put<const void*>(GunAt(0),0x10,nullptr);Check(!ExecuteWalkingGun(lease,0,1,11),"a foreign gun owner is rejected before native dispatch");
    lease=Fixture();Put<const void*>(BatteryAt(0),0x2B8,nullptr);Check(!ExecuteWalkingGun(lease,0,1,11),"battery root weak identity must match the controlled lease");
    lease=Fixture();Put<const void*>(body,0x30,nullptr);Check(!ListWalkingGunAbilities(lease,list,4) && !ExecuteWalkingGun(lease,0,1,11),"a reused root address cannot inherit the old controller's weapons");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy control walking guns: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
