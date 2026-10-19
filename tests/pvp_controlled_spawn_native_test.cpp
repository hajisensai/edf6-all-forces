// Execute the real native resource proof, resolver/bridge and generic ownership core.
// Native provider Collect/Read/Write/ProfileReady are isolated ABI stand-ins here;
// this does not replace the separate native-provider or in-game acceptance tests.
#include "../src/pvp_controlled.cpp"
#include "../src/enemy_control_pvp_spawn_native.cpp"
#include "../src/pvp_controlled_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace crew::enemy_native {
constexpr NativeProfile productionProfiles[]={
#include "../src/enemy_control_native_profiles.inc"
};
const NativeProfile* Profiles(std::size_t* count) noexcept {if(count)*count=std::size(productionProfiles);return productionProfiles;}
}
namespace spawn_native_fixture {
using namespace crew::controlled_pvp;
struct Object {
    alignas(16) unsigned char bytes[0x2100]{},control[0x20]{},emitter[3][0x220]{};
    unsigned emitted=2;int baseline=6;
};
Object objects[3]{};
unsigned checks=0,failures=0,providerCollects=0,providerReads=0,providerWrites=0,nativeWrites=0,controlDeletes=0;
bool profileReady=true;
int ownerTeam=1;
void Check(bool pass,const char* expression,int line) {++checks;if(!pass){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}}
#define CHECK(value) spawn_native_fixture::Check(!!(value),#value,__LINE__)
void __fastcall DeleteControl(void*) noexcept {++controlDeletes;}
Object* Find(const void* pointer) noexcept {for(auto& object:objects)if(object.bytes==pointer)return &object;return nullptr;}
void Init(Object& object,unsigned vtable,int team,unsigned emitted=2) {
    object=Object{};object.baseline=team;object.emitted=emitted;
    static void* controls[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
    edf::Put<void*>(object.control,0,controls);edf::Put<LONG>(object.control,8,1);edf::Put<LONG>(object.control,12,1);
    edf::Put<void*>(object.bytes,0,crew::image+vtable);edf::Put<void*>(object.bytes,edf::kSelf,object.bytes);
    edf::Put<void*>(object.bytes,edf::kSelfCtrl,object.control);edf::Put<int>(object.bytes,edf::kTeam,team);
    edf::Put<int>(object.bytes,0x318,team);edf::Put<unsigned>(object.bytes,0x380,0x40u);
    for(unsigned i=0;i<3;++i)edf::Put<int>(object.emitter[i],0xD0,team);
}
void __fastcall SetTeam(void* pointer,int team,bool registered) noexcept {
    ++nativeWrites;CHECK(Find(pointer));CHECK(team!=-1 || !registered);
    edf::Put<int>(pointer,edf::kTeam,team);if(team>=0)edf::Put<int>(pointer,0x318,team);
    const auto flags=edf::At<unsigned>(pointer,0x380);edf::Put<unsigned>(pointer,0x380,registered ? flags|0x40u : flags&~0x40u);
}
const wchar_t* ant=L"app:/object/GIANTANT01.sgo";
const wchar_t* monster=L"app:/object/MONSTER501.sgo";
bool AmbiguousResolver(const wchar_t* path,SpawnProof* out) noexcept {return crew::EnemyControlNativePvpSpawnProof(path,out);}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {return pointer!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept {return spawn_native_fixture::ownerTeam;}
// This original-native-only fixture excludes the ported resolver; the separate ported fixture executes its real installation.
bool InstallControlledPvpPortedSpawnResolver() noexcept {return true;}
bool EnemyControlNativePvpProfileReady(const char* name) noexcept {
    if(!spawn_native_fixture::profileReady || !name)return false;
    for(const auto& profile:enemy_native::productionProfiles)if(!std::strcmp(name,profile.name))return true;
    return false;
}
bool EnemyControlNativePvpCollect(const ObjRef& root,ObjRef* members,unsigned capacity,unsigned* count,
                                 controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    ++spawn_native_fixture::providerCollects;
    auto* object=spawn_native_fixture::Find(root.obj);
    if(!object || root.ctrl!=object->control || At<LONG>(object->control,8)<=0 || !capacity || object->emitted>emitterCapacity)return false;
    members[0]=root;*count=1;*emitted=object->emitted;
    for(unsigned i=0;i<object->emitted;++i)emitters[i]={controlled_pvp::EmitterKind::indirectFireControl,root,object->emitter[i],1};
    return true;
}
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter& emitter,int* team) noexcept {
    ++spawn_native_fixture::providerReads;auto* object=spawn_native_fixture::Find(emitter.member.obj);
    if(!object || object->control!=emitter.member.ctrl)return controlled_pvp::Life::invalid;
    if(At<LONG>(object->control,8)<=0)return controlled_pvp::Life::gone;
    for(unsigned i=0;i<object->emitted && i<3;++i)if(emitter.object==object->emitter[i]) {
        if(team)*team=At<int>(object->emitter[i],0xD0);return controlled_pvp::Life::live;
    }
    return controlled_pvp::Life::invalid;
}
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter& emitter,int team) noexcept {
    int actual=0;if(EnemyControlNativePvpRead(emitter,&actual)!=controlled_pvp::Life::live)return false;
    ++spawn_native_fixture::providerWrites;Put<int>(const_cast<void*>(emitter.object),0xD0,team);return true;
}
bool EnemyControlNativePvpBaseline(const ObjRef&,const ObjRef& member,int,int,int* team,int* last) noexcept {
    auto* object=spawn_native_fixture::Find(member.obj);if(!object)return false;*team=*last=object->baseline;return true;
}
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter& emitter,int,int,int* team) noexcept {
    auto* object=spawn_native_fixture::Find(emitter.member.obj);if(!object)return false;*team=object->baseline;return true;
}
}
namespace spawn_native_fixture {
void ResourceManifestAndExactClass() {
    CHECK(std::size(crew::resources)==62);SpawnProof proof{};
    for(const auto& resource:crew::resources) {
        CHECK(crew::EnemyControlNativePvpSpawnProof(resource.path,&proof));
        CHECK(proof.maxMembers==resource.members && proof.maxEmitters==resource.emitters);
        CHECK(proof.hooks.collect && proof.hooks.readEmitter && proof.hooks.writeEmitter && proof.hooks.memberBaseline && proof.hooks.emitterBaseline);
    }
    CHECK(crew::EnemyControlNativePvpSpawnProof(L"APP:/OBJECT/giantant01.SGO",&proof));CHECK(proof.maxMembers==1 && proof.maxEmitters==2);
    CHECK(crew::EnemyControlNativePvpSpawnProof(monster,&proof));CHECK(proof.maxMembers==1 && proof.maxEmitters==1);
    CHECK(crew::EnemyControlNativePvpSpawnProof(L"app:/object/E515_IMPERIALUFO.sgo",&proof));CHECK(proof.maxMembers==1 && proof.maxEmitters==2);
    CHECK(!crew::EnemyControlNativePvpSpawnProof(L"app:/object/EDF41_GIANTANT01.sgo",&proof));CHECK(proof.maxMembers==0 && !proof.hooks.collect);
    CHECK(!crew::EnemyControlNativePvpSpawnProof(L"Mods/OBJECT/GIANTANT01.sgo",&proof));
    CHECK(!crew::EnemyControlNativePvpSpawnProof(L"app:/object/GIANTANT01.sgo/unknown",&proof));
    profileReady=false;CHECK(!crew::EnemyControlNativePvpSpawnProof(ant,&proof));profileReady=true;
    CHECK(crew::EnemyControlNativePvpSpawnProof(ant,&proof));Init(objects[1],0x17BB798,6,2);
    crew::ObjRef members[4]{};Emitter emitted[4]{};unsigned count=0,emitterCount=0;
    CHECK(proof.hooks.collect(proof.hooks.user,crew::ObjRef::Of(objects[1].bytes),members,4,&count,emitted,4,&emitterCount));
    CHECK(count==1 && emitterCount==2);int team=0;CHECK(proof.hooks.readEmitter(proof.hooks.user,emitted[0],&team)==Life::live && team==6);
    CHECK(proof.hooks.writeEmitter(proof.hooks.user,emitted[0],4));CHECK(edf::At<int>(objects[1].emitter[0],0xD0)==4);
    const auto before=providerCollects;Init(objects[1],0x17BE120,6,2); // another admitted native profile cannot impersonate the configured ant
    CHECK(!proof.hooks.collect(proof.hooks.user,crew::ObjRef::Of(objects[1].bytes),members,4,&count,emitted,4,&emitterCount));CHECK(providerCollects==before);
    Init(objects[1],0x17BB798,6,2);edf::Put<LONG>(objects[1].control,8,0);
    CHECK(!proof.hooks.collect(proof.hooks.user,crew::ObjRef::Of(objects[1].bytes),members,4,&count,emitted,4,&emitterCount));
    edf::Put<LONG>(objects[1].control,8,1);unsigned char wrongControl[16]{};edf::Put<LONG>(wrongControl,8,1);
    CHECK(!proof.hooks.collect(proof.hooks.user,{objects[1].bytes,wrongControl},members,4,&count,emitted,4,&emitterCount));
    profileReady=false;CHECK(!proof.hooks.collect(proof.hooks.user,crew::ObjRef::Of(objects[1].bytes),members,4,&count,emitted,4,&emitterCount));profileReady=true;
}
void InstalledBridgeLifecycle() {
    CHECK(!crew::RegisterControlledPvpSpawnResolver(nullptr));CHECK(crew::InstallControlledPvpSpawnBridge());
    CHECK(crew::InstallControlledPvpSpawnBridge());CHECK(crew::RegisterControlledPvpSpawnResolver(&crew::EnemyControlNativePvpSpawnProof));
    const auto& bridge=crew::mother_control::spawnOwnership;
    CHECK(crew::mother_control::spawnOwnershipConfigured && bridge.canPrepare && bridge.prepare && bridge.adopt && bridge.finish);
    Init(objects[0],0x17C5628,5,1);Init(objects[1],0x17BB798,6,2);Init(objects[2],0x17BB798,4,2);
    SpawnProof parentProof{};CHECK(crew::EnemyControlNativePvpSpawnProof(monster,&parentProof));Lease lease{};
    const auto parent=crew::ObjRef::Of(objects[0].bytes);CHECK(Acquire(parent,"owner1",parentProof.hooks,&lease));
    unsigned char before[2][sizeof(objects[0].bytes)]{};std::memcpy(before[0],objects[1].bytes,sizeof(before[0]));std::memcpy(before[1],objects[2].bytes,sizeof(before[1]));
    std::uint64_t token=99;CHECK(!bridge.prepare(bridge.user,parent,L"app:/object/UNKNOWN.sgo",1,&token) && token==0);
    CHECK(!bridge.canPrepare(bridge.user,parent,ant,4096));CHECK(!bridge.prepare(bridge.user,parent,ant,4096,&token) && token==0);
    edf::Put<LONG>(objects[0].control,8,0);CHECK(!bridge.prepare(bridge.user,parent,ant,1,&token) && token==0);edf::Put<LONG>(objects[0].control,8,1);
    CHECK(bridge.prepare(bridge.user,parent,ant,2,&token));
    CHECK(bridge.adopt(bridge.user,token,parent,crew::ObjRef::Of(objects[1].bytes)));
    CHECK(bridge.adopt(bridge.user,token,parent,crew::ObjRef::Of(objects[2].bytes)));bridge.finish(bridge.user,token);
    CHECK(edf::At<int>(objects[1].bytes,edf::kTeam)==1 && edf::At<int>(objects[2].bytes,edf::kTeam)==1);
    ownerTeam=2;CHECK(Frame(lease));CHECK(edf::At<int>(objects[1].bytes,edf::kTeam)==2 && edf::At<int>(objects[2].emitter[1],0xD0)==2);
    ownerTeam=0;CHECK(Frame(lease));CHECK(edf::At<int>(objects[1].bytes,edf::kTeam)==6 && edf::At<int>(objects[2].bytes,edf::kTeam)==4);
    ownerTeam=1;CHECK(Frame(lease));CHECK(Restore(&lease));CHECK(lease.id==0);
    CHECK(std::memcmp(before[0],objects[1].bytes,sizeof(before[0]))==0 && std::memcmp(before[1],objects[2].bytes,sizeof(before[1]))==0);
    CHECK(edf::At<LONG>(objects[1].control,12)==1 && edf::At<LONG>(objects[2].control,12)==1 && controlDeletes==0);
    CHECK(providerCollects>0 && providerReads>0 && providerWrites>0 && nativeWrites>0);
    SpawnProof resolved{};CHECK(crew::Resolve(ant,&resolved));
    CHECK(crew::RegisterControlledPvpSpawnResolver(&AmbiguousResolver));CHECK(!crew::Resolve(ant,&resolved));
    token=99;CHECK(!bridge.prepare(bridge.user,parent,ant,1,&token) && token==0); // duplicate accepted proof is ambiguous even though both resolvers are trusted
}
}
int main() {
    using namespace spawn_native_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image);if(!crew::image)return 1;
    for(const auto& profile:crew::enemy_native::productionProfiles)
        edf::Put<void*>(crew::image,profile.vtableRva-8,reinterpret_cast<void*>(std::uintptr_t{0x100000}+profile.vtableRva));
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(&SetTeam);std::memcpy(jump+2,&address,8);std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    ResourceManifestAndExactClass();InstalledBridgeLifecycle();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("pvp_controlled_spawn_native_test: %u checks, %u failed\n",checks,failures);return failures ? 1 : 0;
}
