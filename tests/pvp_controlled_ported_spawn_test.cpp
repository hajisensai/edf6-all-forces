// Actual generic core, ported resource resolver, generated 75-row manifest and installed Mother bridge.
// Native/NPC/Robo typed producers below are explicit ABI boundaries; this does not execute Robo::Setup or native weapons.
#include "../src/pvp_controlled.cpp"
#include "../src/pvp_controlled_ported_spawn.cpp"
#include "../src/pvp_controlled_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace crew::enemy_native {
constexpr NativeProfile fixtureProfiles[]={
#include "../src/enemy_control_native_profiles.inc"
};
const NativeProfile* Profiles(std::size_t* count) noexcept {if(count)*count=std::size(fixtureProfiles);return fixtureProfiles;}
}
namespace ported_spawn_fixture {
using namespace crew::controlled_pvp;
struct Object {
    alignas(16) unsigned char bytes[0x2100]{},control[0x20]{},emitters[32][0x220]{};
    const char* type="DragonSmall";unsigned emitted=2;int baseline=6;bool ready=false,initializeOk=true;
};
Object objects[5]{};
unsigned checks=0,failures=0,collects=0,reads=0,writes=0,nativeWrites=0,initializers=0,npcProofs=0,roboProofs=0;
unsigned lastWeapons=0,lastHeld=0;const char* lastNpcClass=nullptr;
bool profileReady=true,npcReady=true,roboReady=true,oversizeProof=false;
int ownerTeam=2;
void Check(bool pass,const char* expression,int line){++checks;if(!pass){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}}
#define CHECK(v) ported_spawn_fixture::Check(!!(v),#v,__LINE__)
void __fastcall DeleteControl(void*) noexcept {}
Object* Find(const void* pointer) noexcept {for(auto& object:objects)if(object.bytes==pointer)return &object;return nullptr;}
unsigned Table(const char* name) noexcept {
    for(const auto& p:crew::enemy_native::fixtureProfiles)if(!std::strcmp(name,p.name))return p.vtableRva;
    if(!std::strcmp(name,"UfoRobo"))return 0x1880000;
    if(!std::strcmp(name,"AssultSoldier"))return 0x1880100;
    if(!std::strcmp(name,"PaleWing"))return 0x1880200;
    if(!std::strcmp(name,"HeavyArmor"))return 0x1880300;
    return 0x1880400;
}
void Init(Object& object,const char* type,int team=6,unsigned emitted=2) {
    object=Object{};object.type=type;object.emitted=emitted;object.baseline=team;
    static void* table[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
    edf::Put<void*>(object.control,0,table);edf::Put<LONG>(object.control,8,1);edf::Put<LONG>(object.control,12,1);
    edf::Put<void*>(object.bytes,0,crew::image+Table(type));edf::Put<void*>(object.bytes,edf::kSelf,object.bytes);
    edf::Put<void*>(object.bytes,edf::kSelfCtrl,object.control);edf::Put<int>(object.bytes,edf::kTeam,team);
    edf::Put<int>(object.bytes,0x318,team);edf::Put<unsigned>(object.bytes,0x380,0x40u);
    for(unsigned i=0;i<32;++i)edf::Put<int>(object.emitters[i],0xD0,team);
    for(unsigned i=0;i<32;++i)object.bytes[0x108+i]=static_cast<unsigned char>(0x30+i);
    edf::Put<unsigned short>(object.bytes,0x128,2);
    edf::Put<void*>(object.bytes,0x1ED0,object.bytes+0x1F00);edf::Put<void*>(object.bytes,0x1ED8,object.bytes+0x1F40);
    std::memset(object.bytes+0x1F00,0x39,0x20);std::memcpy(object.bytes+0x1F40,"authenticated-owner1",21);
}
int Team(const Object& o) noexcept{return edf::At<int>(o.bytes,edf::kTeam);}
LONG Weak(const Object& o) noexcept{return edf::At<LONG>(o.control,12);}
void __fastcall SetTeam(void* raw,int team,bool registered) noexcept {
    ++nativeWrites;CHECK(Find(raw));CHECK(team!=-1 || !registered);
    edf::Put<int>(raw,edf::kTeam,team);if(team>=0)edf::Put<int>(raw,0x318,team);
    const auto flags=edf::At<unsigned>(raw,0x380);edf::Put<unsigned>(raw,0x380,registered ? flags|0x40u : flags&~0x40u);
}
bool Identity(const crew::ObjRef& r) noexcept {
    auto* object=Find(r.obj);return object && r.ctrl==object->control && edf::At<LONG>(r.ctrl,8)>0 &&
        edf::At<const void*>(r.obj,edf::kSelf)==r.obj && edf::At<const void*>(r.obj,edf::kSelfCtrl)==r.ctrl &&
        !(edf::At<unsigned char>(r.obj,0x18)&4u);
}
bool ProviderCollect(void*,const crew::ObjRef& root,crew::ObjRef* members,unsigned capacity,unsigned* count,
                     Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    ++collects;auto* object=Find(root.obj);if(count)*count=0;if(emitted)*emitted=0;
    if(!Identity(root) || !members || !count || !emitted || !capacity || object->emitted>emitterCapacity)return false;
    members[0]=root;*count=1;*emitted=object->emitted;
    for(unsigned i=0;i<object->emitted;++i)emitters[i]={EmitterKind::indirectFireControl,root,object->emitters[i],1};
    return true;
}
Life ProviderRead(void*,const Emitter& e,int* team) noexcept {
    ++reads;auto* object=Find(e.member.obj);if(!object || e.member.ctrl!=object->control)return Life::invalid;
    if(edf::At<LONG>(object->control,8)<=0)return Life::gone;
    if(!Identity(e.member))return Life::invalid;
    for(unsigned i=0;i<object->emitted;++i)if(e.object==object->emitters[i]){if(team)*team=edf::At<int>(e.object,0xD0);return Life::live;}
    return Life::invalid;
}
bool ProviderWrite(void* user,const Emitter& e,int team) noexcept {
    int value=0;if(ProviderRead(user,e,&value)!=Life::live)return false;
    ++writes;edf::Put<int>(const_cast<void*>(e.object),0xD0,team);return true;
}
bool ProviderMemberBaseline(void*,const crew::ObjRef&,const crew::ObjRef& member,int,int,int* team,int* last) noexcept {
    auto* object=Find(member.obj);if(!object || !team || !last)return false;*team=*last=object->baseline;return true;
}
bool ProviderEmitterBaseline(void*,const Emitter& e,int,int,int* team) noexcept {
    auto* object=Find(e.member.obj);if(!object || !team)return false;*team=object->baseline;return true;
}
Hooks ProviderHooks() noexcept{return {nullptr,&ProviderCollect,&ProviderRead,&ProviderWrite,&ProviderMemberBaseline,&ProviderEmitterBaseline};}
struct NpcContext{const char* name;unsigned weapons,held;};
NpcContext npcContexts[]={{"AssultSoldier",1,1},{"AssultSoldier",1,10},{"PaleWing",1,1},{"PaleWing",1,15},{"HeavyArmor",2,1}};
bool NpcCollect(void* user,const crew::ObjRef& root,crew::ObjRef* members,unsigned capacity,unsigned* count,
                Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    auto* object=Find(root.obj);const auto& context=*static_cast<NpcContext*>(user);
    if(!object || std::strcmp(object->type,context.name))return false;
    return ProviderCollect(nullptr,root,members,capacity,count,emitters,emitterCapacity,emitted);
}
bool RoboCollect(void*,const crew::ObjRef& root,crew::ObjRef* members,unsigned capacity,unsigned* count,
                 Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    auto* object=Find(root.obj);if(!object || std::strcmp(object->type,"UfoRobo") || object->emitted)return false;
    return ProviderCollect(nullptr,root,members,capacity,count,emitters,emitterCapacity,emitted);
}
bool RoboInitialize(void*,const crew::ObjRef& child) noexcept {
    ++initializers;auto* object=Find(child.obj);if(!Identity(child) || !object || std::strcmp(object->type,"UfoRobo") || !object->initializeOk)return false;
    if(!object->ready){SetTeam(object->bytes,1,true);object->baseline=1;object->ready=true;}
    return true;
}
void CheckSource(const Object& object) {
    CHECK(edf::At<void*>(object.bytes,0x1ED0)==object.bytes+0x1F00 && edf::At<void*>(object.bytes,0x1ED8)==object.bytes+0x1F40);
    CHECK(!std::strcmp(reinterpret_cast<const char*>(object.bytes+0x1F40),"authenticated-owner1"));
    CHECK(edf::At<unsigned short>(object.bytes,0x128)==2);
    for(unsigned i=0;i<32;++i)CHECK(object.bytes[0x108+i]==0x30+i);
}
const wchar_t* robo=L"app:/object/UFOROBO401.sgo";
const wchar_t* dragon=L"app:/object/EDF41/DRAGONSMALL401.sgo";
const wchar_t* paleTb=L"app:/object/EDF41/AIPALEWING_TB.sgo";
const wchar_t* heavy=L"app:/object/EDF41/AIHEAVYARMOR_SC.sgo";
bool Ambiguous(const wchar_t* path,SpawnProof* out) noexcept{return crew::ControlledPvpPortedSpawnProof(path,out);}
}
namespace edf {
bool Readable(const void* p,std::size_t,bool) noexcept{return p!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept{return true;}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept{return ported_spawn_fixture::ownerTeam;}
bool EnemyControlNativePvpSpawnProof(const wchar_t*,controlled_pvp::SpawnProof* out) noexcept{if(out)*out={};return false;}
bool EnemyControlNativePvpProfileReady(const char* name) noexcept {
    if(!ported_spawn_fixture::profileReady || !name)return false;
    for(const auto& profile:enemy_native::fixtureProfiles)if(!std::strcmp(name,profile.name))return true;return false;
}
bool EnemyControlNativePvpCollect(const ObjRef& root,ObjRef* members,unsigned capacity,unsigned* count,
                                 controlled_pvp::Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
    return ported_spawn_fixture::ProviderCollect(nullptr,root,members,capacity,count,emitters,ec,emitted);
}
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter& e,int* team) noexcept{return ported_spawn_fixture::ProviderRead(nullptr,e,team);}
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter& e,int team) noexcept{return ported_spawn_fixture::ProviderWrite(nullptr,e,team);}
bool EnemyControlNativePvpBaseline(const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {
    return ported_spawn_fixture::ProviderMemberBaseline(nullptr,root,member,team,last,original,originalLast);
}
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter& e,int team,int last,int* original) noexcept {
    return ported_spawn_fixture::ProviderEmitterBaseline(nullptr,e,team,last,original);
}
bool Edf41RoboPvpSpawnProof(controlled_pvp::SpawnProof* out) noexcept {
    using namespace ported_spawn_fixture;++roboProofs;if(out)*out={};if(!out || !roboReady)return false;
    out->hooks=ProviderHooks();out->hooks.collect=&RoboCollect;out->maxMembers=oversizeProof ? 2u : 1u;out->prepareInstance=&RoboInitialize;return true;
}
bool PvpControlledNpcProof(const char* name,unsigned weapons,unsigned held,controlled_pvp::SpawnProof* out) noexcept {
    using namespace ported_spawn_fixture;++npcProofs;lastNpcClass=name;lastWeapons=weapons;lastHeld=held;if(out)*out={};
    if(!out || !name || !npcReady)return false;
    for(auto& context:npcContexts)if(!std::strcmp(context.name,name) && context.weapons==weapons && context.held==held) {
        out->hooks=ProviderHooks();out->hooks.user=&context;out->hooks.collect=&NpcCollect;out->maxMembers=1;
        out->maxEmitters=weapons*(1+2*held)+(oversizeProof ? 1u : 0u);return true;
    }
    return false;
}
}
namespace ported_spawn_fixture {
void ManifestAndUnderlyingContracts() {
    CHECK(std::size(crew::resources)==75);SpawnProof proof{};unsigned heavyRows=0;
    for(const auto& resource:crew::resources) {
        CHECK(crew::ControlledPvpPortedSpawnProof(resource.path,&proof));
        CHECK(proof.maxMembers==resource.members && proof.maxEmitters==resource.emitters);
        CHECK(proof.hooks.collect && proof.hooks.readEmitter && proof.hooks.writeEmitter && proof.hooks.memberBaseline && proof.hooks.emitterBaseline);
        CHECK((proof.prepareInstance!=nullptr)==(!std::strcmp(resource.className,"UfoRobo")));
        if(resource.maxWeapons)CHECK(lastWeapons==resource.maxWeapons && lastHeld==resource.maxHeld && !std::strcmp(lastNpcClass,resource.className));
        if(!std::strcmp(resource.className,"HeavyArmor")){++heavyRows;CHECK(resource.maxWeapons==2 && resource.maxHeld==1 && resource.members==1 && resource.emitters==6);}
        std::wstring uppercase=resource.path;for(auto& c:uppercase)c=std::towupper(c);
        CHECK(crew::ControlledPvpPortedSpawnProof(uppercase.c_str(),&proof));
    }
    CHECK(initializers==0 && nativeWrites==0);CHECK(roboProofs && npcProofs);CHECK(heavyRows==6);
    for(const wchar_t* path:{L"",L"app:/object/UNKNOWN.sgo",L"app:/object/GIANTANT01.sgo",L"app:/object/EDF41/AIHEAVYARMOR_UNREVIEWED.sgo",
                            L"Mods/OBJECT/UFOROBO401.sgo",L"app:/object/../UFOROBO401.sgo",L"app:/object//UFOROBO401.sgo",L"app:/object/UFOROBO401.sgo/trailing"}) {
        proof.maxMembers=1;CHECK(!crew::ControlledPvpPortedSpawnProof(path,&proof) && !proof.maxMembers && !proof.hooks.collect);
    }
    CHECK(!crew::ControlledPvpPortedSpawnProof(nullptr,&proof));CHECK(!crew::ControlledPvpPortedSpawnProof(robo,nullptr));
    std::wstring longPath(261,L'A');CHECK(!crew::ControlledPvpPortedSpawnProof(longPath.c_str(),&proof));
    roboReady=false;CHECK(!crew::ControlledPvpPortedSpawnProof(robo,&proof));roboReady=true;
    npcReady=false;CHECK(!crew::ControlledPvpPortedSpawnProof(paleTb,&proof));npcReady=true;
    profileReady=false;CHECK(!crew::ControlledPvpPortedSpawnProof(dragon,&proof));profileReady=true;
    oversizeProof=true;CHECK(!crew::ControlledPvpPortedSpawnProof(robo,&proof));CHECK(!crew::ControlledPvpPortedSpawnProof(paleTb,&proof));oversizeProof=false;
}
void ExactActualNativeClassAndWeak() {
    SpawnProof proof{};CHECK(crew::ControlledPvpPortedSpawnProof(dragon,&proof));
    Init(objects[1],"DragonSmall",6,2);crew::ObjRef members[2]{};Emitter emitters[4]{};unsigned count=0,emitted=0;
    auto collect=[&](const crew::ObjRef& r){return proof.hooks.collect(proof.hooks.user,r,members,2,&count,emitters,4,&emitted);};
    CHECK(collect(crew::ObjRef::Of(objects[1].bytes)));CHECK(count==1 && emitted==2);
    const auto calls=collects;Init(objects[1],"GiantAnt",6,2);CHECK(!collect(crew::ObjRef::Of(objects[1].bytes)) && collects==calls);
    Init(objects[1],"DragonSmall",6,2);edf::Put<LONG>(objects[1].control,8,0);CHECK(!collect(crew::ObjRef::Of(objects[1].bytes)));edf::Put<LONG>(objects[1].control,8,1);
    unsigned char foreign[16]{};edf::Put<LONG>(foreign,8,1);CHECK(!collect({objects[1].bytes,foreign}));
    edf::Put<void*>(objects[1].bytes,edf::kSelf,objects[0].bytes);CHECK(!collect(crew::ObjRef::Of(objects[1].bytes)));
    edf::Put<void*>(objects[1].bytes,edf::kSelf,objects[1].bytes);
    profileReady=false;CHECK(!collect(crew::ObjRef::Of(objects[1].bytes)));profileReady=true;
}
void InstalledBridgeRoboAndNpcLifecycle() {
    CHECK(!crew::RegisterControlledPvpSpawnResolver(nullptr));CHECK(crew::InstallControlledPvpSpawnBridge());
    CHECK(crew::InstallControlledPvpPortedSpawnResolver());CHECK(crew::InstallControlledPvpSpawnBridge());
    const auto& bridge=crew::mother_control::spawnOwnership;CHECK(bridge.canPrepare && bridge.prepare && bridge.adopt && bridge.finish);
    Init(objects[0],"DragonSmall",5,2);Init(objects[1],"UfoRobo",6,0);Init(objects[2],"UfoRobo",6,0);
    Lease lease{};const auto parent=crew::ObjRef::Of(objects[0].bytes);
    CHECK(Acquire(parent,"owner1",ProviderHooks(),&lease));const auto starts=initializers,written=nativeWrites;std::uint64_t token=99;
    CHECK(!bridge.prepare(bridge.user,parent,L"app:/object/UNKNOWN.sgo",1,&token) && !token);
    CHECK(!bridge.canPrepare(bridge.user,parent,robo,4096));CHECK(!bridge.prepare(bridge.user,parent,robo,4096,&token) && !token);
    CHECK(bridge.canPrepare(bridge.user,parent,robo,2));CHECK(bridge.prepare(bridge.user,parent,robo,2,&token));
    CHECK(initializers==starts && nativeWrites==written && !objects[1].ready && Team(objects[1])==6);
    CHECK(bridge.adopt(bridge.user,token,parent,crew::ObjRef::Of(objects[1].bytes)));
    CHECK(bridge.adopt(bridge.user,token,parent,crew::ObjRef::Of(objects[2].bytes)));bridge.finish(bridge.user,token);
    CHECK(initializers==starts+2 && objects[1].ready && objects[2].ready && Team(objects[1])==2 && Weak(objects[1])==2);
    int team=-1,last=-1;CHECK(ReadBaselineForMember(crew::ObjRef::Of(objects[1].bytes),&team,&last) && team==1 && last==1);
    ownerTeam=0;CHECK(Frame(lease));CHECK(Team(objects[1])==1 && Team(objects[2])==1 && Team(objects[0])==5);
    ownerTeam=2;CHECK(Frame(lease));CHECK(Team(objects[2])==2);CHECK(initializers==starts+2);
    CHECK(Restore(&lease));CHECK(Team(objects[1])==1 && Team(objects[2])==1 && Weak(objects[1])==1 && Weak(objects[2])==1);
    CheckSource(objects[0]);CheckSource(objects[1]);CheckSource(objects[2]);

    Init(objects[0],"DragonSmall",5,2);Init(objects[1],"UfoRobo",6,0);objects[1].initializeOk=false;
    CHECK(Acquire(crew::ObjRef::Of(objects[0].bytes),"owner1",ProviderHooks(),&lease));
    CHECK(bridge.prepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),robo,1,&token));
    CHECK(!bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    CHECK(Team(objects[1])==6 && Weak(objects[1])==1 && !OwnsMember(crew::ObjRef::Of(objects[1].bytes)));
    objects[1].initializeOk=true;CHECK(bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    bridge.finish(bridge.user,token);CHECK(Restore(&lease));CHECK(Team(objects[1])==1);

    Init(objects[0],"DragonSmall",5,2);Init(objects[1],"AssultSoldier",6,31);
    CHECK(Acquire(crew::ObjRef::Of(objects[0].bytes),"owner1",ProviderHooks(),&lease));
    CHECK(bridge.canPrepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),paleTb,16));
    CHECK(!bridge.canPrepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),paleTb,17));
    CHECK(lastWeapons==1 && lastHeld==15 && !std::strcmp(lastNpcClass,"PaleWing"));
    CHECK(bridge.prepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),paleTb,1,&token));
    CHECK(!bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)) && Weak(objects[1])==1);
    Init(objects[1],"PaleWing",6,32);
    CHECK(!bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)) && Weak(objects[1])==1);
    objects[1].emitted=31;CHECK(bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    CHECK(Team(objects[1])==2);for(unsigned i=0;i<31;++i)CHECK(edf::At<int>(objects[1].emitters[i],0xD0)==2);
    bridge.finish(bridge.user,token);CHECK(Restore(&lease));CHECK(Team(objects[1])==6 && Weak(objects[1])==1);
    for(unsigned i=0;i<31;++i)CHECK(edf::At<int>(objects[1].emitters[i],0xD0)==6);
    Init(objects[0],"DragonSmall",5,2);Init(objects[1],"PaleWing",6,6);
    CHECK(Acquire(crew::ObjRef::Of(objects[0].bytes),"owner1",ProviderHooks(),&lease));
    CHECK(bridge.canPrepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),heavy,85));
    CHECK(!bridge.canPrepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),heavy,86));
    CHECK(lastWeapons==2 && lastHeld==1 && !std::strcmp(lastNpcClass,"HeavyArmor"));
    CHECK(bridge.prepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),heavy,1,&token));
    CHECK(!bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    CHECK(Weak(objects[1])==1 && Team(objects[1])==6);
    Init(objects[1],"HeavyArmor",6,7);
    CHECK(!bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    CHECK(Weak(objects[1])==1 && Team(objects[1])==6);
    objects[1].emitted=6;CHECK(bridge.adopt(bridge.user,token,crew::ObjRef::Of(objects[0].bytes),crew::ObjRef::Of(objects[1].bytes)));
    CHECK(Team(objects[1])==2 && Weak(objects[1])==2);
    for(unsigned i=0;i<6;++i)CHECK(edf::At<int>(objects[1].emitters[i],0xD0)==2);
    bridge.finish(bridge.user,token);ownerTeam=0;CHECK(Frame(lease));CHECK(Team(objects[1])==6);
    ownerTeam=2;CHECK(Frame(lease));CHECK(Team(objects[1])==2);CHECK(Restore(&lease));
    CHECK(Team(objects[1])==6 && Weak(objects[1])==1);
    for(unsigned i=0;i<6;++i)CHECK(edf::At<int>(objects[1].emitters[i],0xD0)==6);
    CheckSource(objects[1]);
    CHECK(crew::RegisterControlledPvpSpawnResolver(&Ambiguous));SpawnProof proof{};CHECK(!crew::Resolve(robo,&proof));
    token=99;CHECK(!bridge.prepare(bridge.user,crew::ObjRef::Of(objects[0].bytes),robo,1,&token) && !token);
    CHECK(!HasLeases());
}
}
int main() {
    using namespace ported_spawn_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));CHECK(crew::image);if(!crew::image)return 1;
    for(const auto& p:crew::enemy_native::fixtureProfiles)edf::Put<void*>(crew::image,p.vtableRva-8,reinterpret_cast<void*>(std::uintptr_t{0x100000}+p.vtableRva));
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto target=reinterpret_cast<std::uintptr_t>(&SetTeam);
    std::memcpy(jump+2,&target,8);std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    ManifestAndUnderlyingContracts();ExactActualNativeClassAndWeak();InstalledBridgeRoboAndNpcLifecycle();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;std::printf("pvp_controlled_ported_spawn_test: %u checks, %u failed\n",checks,failures);return failures ? 1 : 0;
}
