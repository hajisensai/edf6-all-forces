// Real BigGreyBoss provider + real generic controlled-PVP core seam.
// Only native function addresses and an unrelated generic-native child provider are stand-ins.
#define EDF6_ENEMY_BOSS_TEST
#include "../src/pvp_controlled.cpp"
#include "../src/enemy_control_pvp_boss.cpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace boss_core_fixture {
using namespace crew;
using namespace crew::controlled_pvp;
unsigned checks=0,failures=0,published=0,deleted=0,nativeWrites=0,controlDeletes=0;
int ownerTeam=1;bool genericBaselineKnown=false;
const void* genericObject=nullptr;
crew::ObjRef retainedOwner{},retainedStorage{};bool retainedEnabled=false,retainedListed=false;
const void* watchedOwner=nullptr;unsigned watchedOwnerReads=0;constexpr std::uint64_t retainedIncarnation=0xB055;
crew::ObjRef selectorOwner{};const void* selectorTable=nullptr;const void* selectorFunction=nullptr;
void Check(bool pass,const char* expression,int line){++checks;if(!pass){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}}
#define CHECK(value) boss_core_fixture::Check(!!(value),#value,__LINE__)
void __fastcall DeleteControl(void*) noexcept {++controlDeletes;}
void __fastcall Publish(void*,const ObjRef* argument,unsigned kind,unsigned,int,int) noexcept {
    ++published;CHECK(kind==2);CHECK(At<int>(argument->obj,kTeam)==ownerTeam);boss_pvp_detail::DropControl(argument->ctrl);
}
void __fastcall Delete(void* object) noexcept {++deleted;Put<unsigned char>(object,0x18,4);}
void __fastcall SetTeam(void* object,int team,bool registered) noexcept {
    ++nativeWrites;CHECK(team!=-1 || !registered);Put<int>(object,kTeam,team);if(team!=-1)Put<int>(object,0x318,team);
    const auto flags=At<unsigned>(object,0x380);Put<unsigned>(object,0x380,registered ? flags|0x40u : flags&~0x40u);
}
struct Object {
    unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    unsigned char* c=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    explicit Object(unsigned type=0,int team=5) {
        if(!p || !c)std::exit(2);static void* controls[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
        Put<void*>(c,0,controls);Put<LONG>(c,8,1);Put<LONG>(c,12,1);
        Put<const void*>(p,0,image+boss_pvp_detail::kTypes[type].table);Put<void*>(p,kSelf,p);Put<void*>(p,kSelfCtrl,c);
        Put<int>(p,kTeam,team);Put<int>(p,0x318,team);Put<unsigned>(p,0x380,0x40u);
        auto* head=p+0x3000;Put<void*>(head,0,head);Put<void*>(head,8,head);Put<void*>(p,0x550,head);
        auto* parts=p+0x3100;Put<void*>(parts,0,parts);Put<void*>(parts,8,parts);Put<void*>(p,0x17E8,parts);
    }
    ObjRef Ref()const noexcept{return {p,c};}
    ~Object(){VirtualFree(p,0,MEM_RELEASE);VirtualFree(c,0,MEM_RELEASE);}
};
void Image() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)std::exit(2);
    for(const auto& anchor:boss_pvp_detail::kAnchors)std::memcpy(image+anchor.rva,anchor.bytes,anchor.size);
    std::memcpy(image+boss_pvp_detail::kPublishCall,boss_pvp_detail::kPublishBytes,5);
    Put<void*>(image+0x17E6EE0,8,image+0x6CF7D0);Put<void*>(image+0x17E6EE0,16,image+0x6CFEF0);
    Put<void*>(image+0x17E6F08,8,image+0x6CF910);Put<void*>(image+0x17E6F08,16,image+0x6CFF00);
    unsigned index=0;
    for(const auto& type:boss_pvp_detail::kTypes) {
        const unsigned col=0x2000000+index*0x400,descriptor=col+0x100;
        Put<void*>(image+type.table-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,12,descriptor);Put<unsigned>(image+col,20,col);
        auto* name=image+descriptor+16;std::memcpy(name,".?AV",4);const auto n=std::strlen(type.name);std::memcpy(name+4,type.name,n);std::memcpy(name+4+n,"@@",3);
        for(unsigned slot=0;slot<type.slots;++slot)Put<void*>(image+type.table,slot*8,image+0x1000+slot*16);
        Put<void*>(image+type.table,22*8,image+0x54ABD0);++index;
    }
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(&SetTeam);std::memcpy(jump+2,&address,8);std::memcpy(image+kSetTeam,jump,sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(),image+kSetTeam,sizeof(jump));
}
bool Collect(void*,const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
    return EnemyControlBossPvpCollect(root,members,cap,count,emitters,ec,emitted);
}
Life Read(void*,const Emitter& emitter,int* team) noexcept {
    return (EnemyControlNativePvpCacheRetained(emitter) || EnemyControlBossPvpSupports(emitter.member)) ? EnemyControlBossPvpRead(emitter,team) : EnemyControlNativePvpRead(emitter,team);
}
bool Write(void*,const Emitter& emitter,int team) noexcept {
    return (EnemyControlNativePvpCacheRetained(emitter) || EnemyControlBossPvpSupports(emitter.member)) ? EnemyControlBossPvpWrite(emitter,team) : EnemyControlNativePvpWrite(emitter,team);
}
bool MemberBaseline(void*,const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {
    return EnemyControlBossPvpSupports(member) ? EnemyControlBossPvpBaseline(root,member,team,last,original,originalLast) :
        EnemyControlNativePvpBaseline(root,member,team,last,original,originalLast);
}
bool EmitterBaseline(void*,const Emitter& emitter,int team,int last,int* original) noexcept {
    return (EnemyControlNativePvpCacheRetained(emitter) || EnemyControlBossPvpSupports(emitter.member)) ? EnemyControlBossPvpBaseline(emitter,team,last,original) :
        EnemyControlNativePvpBaseline(emitter,team,last,original);
}
Hooks Adapter() noexcept {return {nullptr,&Collect,&Read,&Write,&MemberBaseline,&EmitterBaseline};}
void Born(Object& root,Object& child,int index,int total=20,unsigned wave=1) {
    Put<int>(child.p,kTeam,At<int>(root.p,kTeam));Put<int>(child.p,0x318,At<int>(root.p,0x318));
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(child.c+12));ObjRef temporary=child.Ref();
    boss_pvp_detail::OnMeteor(root.p,&temporary,2,wave,index,total);
}
void ExactOwnedSelectorCloneThroughRealCore() {
    Object root;alignas(16) unsigned char clone[(86+1)*8]{},stale[(86+1)*8]{};
    std::memcpy(clone,image+boss_pvp_detail::kTypes[0].table-8,sizeof(clone));
    auto* table=clone+8;Put<void*>(root.p,0,table);Put<void*>(table,14*8,image+0x9900);
    CHECK(!Probe(root.Ref(),Adapter()));selectorOwner=root.Ref();selectorTable=table;selectorFunction=image+0x9900;
    CHECK(Probe(root.Ref(),Adapter()));Lease lease{};ownerTeam=1;CHECK(Acquire(root.Ref(),"owner1",Adapter(),&lease));
    int applied=0;CHECK(At<int>(root.p,kTeam)==1 && ReadAppliedTeam(root.Ref(),&applied) && applied==1);ownerTeam=2;CHECK(Frame(lease));
    CHECK(At<int>(root.p,kTeam)==2);CHECK(Restore(&lease));CHECK(lease.id==0 && At<int>(root.p,kTeam)==5);
    Put<void*>(table,14*8,image+0x9910);CHECK(!Probe(root.Ref(),Adapter()));Put<void*>(table,14*8,const_cast<void*>(selectorFunction));
    std::memcpy(stale,clone,sizeof(stale));Put<void*>(root.p,0,stale+8);CHECK(!Probe(root.Ref(),Adapter()));
    Put<void*>(root.p,0,table);CHECK(Probe(root.Ref(),Adapter()));Put<void*>(root.p,0,image+boss_pvp_detail::kTypes[0].table);
    selectorOwner={};selectorTable=selectorFunction=nullptr;CHECK(EnemyControlBossPvpReset());
}
void ActualTwentyMeteorWave() {
    Object root;Lease lease{};ownerTeam=1;const auto before=published;CHECK(Acquire(root.Ref(),"owner1",Adapter(),&lease));
    Put<float>(root.p,0x26B0,3.0f);CHECK(EnemyControlBossPvpPreflight(root.Ref()));
    std::vector<std::unique_ptr<Object>> meteors;meteors.reserve(20);
    for(int i=0;i<20;++i) {
        meteors.emplace_back(std::make_unique<Object>(7));Born(root,*meteors.back(),i);
        CHECK(OwnsMember(meteors.back()->Ref()));CHECK(At<int>(meteors.back()->p,kTeam)==1);
        CHECK(At<LONG>(meteors.back()->c,12)==3); // native implicit + Boss ledger + real generic lease
    }
    CHECK(published==before+20);Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.members==21 && status.emitters==21 && status.followsOwnerTeam);
    ObjRef members[kMaxMembers]{};Emitter emitters[kMaxEmitters]{};unsigned count=0,emitted=0;
    CHECK(EnemyControlBossPvpCollect(root.Ref(),members,kMaxMembers,&count,emitters,kMaxEmitters,&emitted));CHECK(count==1 && emitted==1);
    CHECK(!EnemyControlBossPvpReset());ownerTeam=2;CHECK(Frame(lease));
    for(const auto& meteor:meteors)CHECK(At<int>(meteor->p,kTeam)==2 && At<int>(meteor->p,0x318)==2);
    ownerTeam=0;CHECK(Frame(lease));CHECK(At<int>(root.p,kTeam)==5);
    for(const auto& meteor:meteors)CHECK(At<int>(meteor->p,kTeam)==5 && At<int>(meteor->p,0x318)==5);
    ownerTeam=1;CHECK(Frame(lease));CHECK(Restore(&lease));CHECK(lease.id==0);
    for(const auto& meteor:meteors)CHECK(At<int>(meteor->p,kTeam)==5 && At<LONG>(meteor->c,12)==2 && !OwnsMember(meteor->Ref()));
    CHECK(EnemyControlBossPvpReset());CHECK(At<LONG>(root.c,12)==1);
    for(const auto& meteor:meteors)CHECK(At<LONG>(meteor->c,12)==1);
}
void DeniedPublicationWithdrawsTemporaryWeak() {
    Object root,child(7);Lease lease{};ownerTeam=1;CHECK(Acquire(root.Ref(),"owner1",Adapter(),&lease));
    const auto beforePublished=published,beforeDeleted=deleted;ownerTeam=-1;Born(root,child,0);
    CHECK(published==beforePublished && deleted==beforeDeleted+1);CHECK(At<LONG>(child.c,12)==1);
    CHECK((At<unsigned char>(child.p,0x18)&4u)!=0 && !OwnsMember(child.Ref()));Status status{};CHECK(ReadStatus(lease,&status));
    CHECK(status.restoring && OwnsMember(root.Ref()));ownerTeam=1;CHECK(Restore(&lease));CHECK(lease.id==0);
    CHECK(EnemyControlBossPvpReset());CHECK(At<LONG>(root.c,12)==1 && At<LONG>(child.c,12)==1);
}
void RetainedCacheSharedBoundaryUsesRealBossAndCore() {
    Object root,bullet(7);retainedOwner=root.Ref();retainedStorage=bullet.Ref();retainedEnabled=retainedListed=true;
    Put<const void*>(bullet.p,0xAE8,root.p);Put<const void*>(bullet.p,0xAF0,root.c);Put<int>(bullet.p,0xB40,4);Put<int>(bullet.p,0x894,3);
    Lease lease{};ownerTeam=1;CHECK(Acquire(root.Ref(),"owner1",Adapter(),&lease));CHECK(At<int>(bullet.p,0xB40)==1 && At<int>(bullet.p,0x894)==1);
    CHECK(At<LONG>(bullet.c,12)==3);retainedListed=false;CHECK(Frame(lease));
    CHECK(At<int>(bullet.p,0xB40)==4 && At<int>(bullet.p,0x894)==3 && At<LONG>(bullet.c,12)==1);
    CHECK(Restore(&lease));CHECK(EnemyControlBossPvpReset());retainedEnabled=false;
    Object deadOwner,surviving(7);retainedOwner=deadOwner.Ref();retainedStorage=surviving.Ref();retainedEnabled=retainedListed=true;
    Put<const void*>(surviving.p,0xAE8,deadOwner.p);Put<const void*>(surviving.p,0xAF0,deadOwner.c);Put<int>(surviving.p,0xB40,4);Put<int>(surviving.p,0x894,3);
    CHECK(Acquire(deadOwner.Ref(),"owner1",Adapter(),&lease));CHECK(At<LONG>(surviving.c,12)==3);
    Put<LONG>(deadOwner.c,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(deadOwner.c+12))==1);
    DWORD previous=0;CHECK(VirtualProtect(deadOwner.p,0x4000,PAGE_NOACCESS,&previous)!=0);watchedOwner=deadOwner.p;watchedOwnerReads=0;
    CHECK(Restore(&lease));CHECK(At<int>(surviving.p,0xB40)==4 && At<int>(surviving.p,0x894)==3 && At<LONG>(surviving.c,12)==1);
    CHECK(watchedOwnerReads==0 && At<LONG>(deadOwner.c,12)==0);watchedOwner=nullptr;
    DWORD ignored=0;CHECK(VirtualProtect(deadOwner.p,0x4000,previous,&ignored)!=0);retainedEnabled=retainedListed=false;
    retainedOwner=retainedStorage={};CHECK(EnemyControlBossPvpReset());
}
void UnknownGenericChildBaselineKeepsRealLease() {
    Object root,unknown;Put<const void*>(unknown.p,0,image+0x1F0000);genericObject=unknown.p;genericBaselineKnown=false;
    Lease lease{};ownerTeam=1;CHECK(Acquire(root.Ref(),"owner1",Adapter(),&lease));
    Put<int>(unknown.p,kTeam,1);Put<int>(unknown.p,0x318,1);
    auto* head=root.p+0x3000;auto* node=root.p+0x3200;
    Put<void*>(head,0,node);Put<void*>(head,8,node);Put<void*>(node,0,head);Put<void*>(node,8,head);Put<void*>(node,0x10,unknown.p);Put<std::size_t>(root.p,0x558,1);
    CHECK(!Frame(lease));CHECK(OwnsMember(unknown.Ref()) && At<LONG>(unknown.c,12)==2);
    CHECK(!Restore(&lease) && lease.id!=0 && At<LONG>(unknown.c,12)==2);Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.restoring);
    genericBaselineKnown=true;CHECK(Restore(&lease));CHECK(lease.id==0);
    CHECK(At<int>(unknown.p,kTeam)==5 && At<int>(unknown.p,0x318)==5 && At<LONG>(unknown.c,12)==1);
    CHECK(EnemyControlBossPvpReset());CHECK(At<LONG>(root.c,12)==1);genericObject=nullptr;
}
}
namespace crew {
using boss_core_fixture::retainedOwner;using boss_core_fixture::retainedStorage;using boss_core_fixture::retainedEnabled;
using boss_core_fixture::retainedListed;using boss_core_fixture::retainedIncarnation;
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept {return boss_core_fixture::ownerTeam;}
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept {
    return boss_core_fixture::selectorOwner.obj==root && boss_core_fixture::selectorOwner.ctrl &&
        At<LONG>(boss_core_fixture::selectorOwner.ctrl,8)>0 && boss_core_fixture::selectorOwner.Is(root) &&
        boss_core_fixture::selectorTable && At<const void*>(root,0)==boss_core_fixture::selectorTable && function==boss_core_fixture::selectorFunction;
}
void* EnemyBossTestFunction(unsigned rva) noexcept {
    if(rva==boss_pvp_detail::kPublish)return reinterpret_cast<void*>(&boss_core_fixture::Publish);
    if(rva==boss_pvp_detail::kDelete)return reinterpret_cast<void*>(&boss_core_fixture::Delete);
    if(rva==kSetTeam)return reinterpret_cast<void*>(&boss_core_fixture::SetTeam);return nullptr;
}
// This fixture's boss has no native +1120 weapon container. The real helper is independently provider-tested.
bool EnemyControlNativePvpCollectHumanoidWeapons(const ObjRef& root,controlled_pvp::Emitter* output,unsigned capacity,unsigned* count) noexcept {
        if(!count)return false;
    if(retainedEnabled && retainedListed && root.obj==retainedOwner.obj && root.ctrl==retainedOwner.ctrl) {
        if(*count+2>capacity)return false;
        output[(*count)++]={controlled_pvp::EmitterKind::ownerLastTeamCache,root,static_cast<const unsigned char*>(retainedStorage.obj)+0xB40,retainedIncarnation,retainedStorage};
        output[(*count)++]={controlled_pvp::EmitterKind::ownerLastTeamCache,root,static_cast<const unsigned char*>(retainedStorage.obj)+0x894,retainedIncarnation,retainedStorage};return true;
    }
    return Readable(root.obj,0x1128) && At<const void*>(root.obj,0x1120)==nullptr; // append nothing, preserve prior count
}
// Explicit shared-provider ABI stand-in; Boss and generic ownership/restore implementations remain real.
bool EnemyControlNativePvpCacheRetained(const controlled_pvp::Emitter& e) noexcept {
    return retainedEnabled && e.kind==controlled_pvp::EmitterKind::ownerLastTeamCache && e.incarnation==retainedIncarnation &&
        e.member.obj==retainedOwner.obj && e.member.ctrl==retainedOwner.ctrl && e.storage.obj==retainedStorage.obj && e.storage.ctrl==retainedStorage.ctrl &&
        (e.object==static_cast<const unsigned char*>(retainedStorage.obj)+0xB40 || e.object==static_cast<const unsigned char*>(retainedStorage.obj)+0x894);
}
controlled_pvp::Life EnemyControlNativePvpReadRetainedCache(const controlled_pvp::Emitter& e,int* team) noexcept {
    if(!team || !EnemyControlNativePvpCacheRetained(e) || !Readable(e.storage.ctrl,16))return controlled_pvp::Life::invalid;
    if(At<LONG>(e.storage.ctrl,8)<=0)return controlled_pvp::Life::gone;
    if(!Readable(e.storage.obj,0xB48) || !e.storage.Is(e.storage.obj) ||
       At<const void*>(e.storage.obj,0xAE8)!=retainedOwner.obj || At<const void*>(e.storage.obj,0xAF0)!=retainedOwner.ctrl)return controlled_pvp::Life::invalid;
    *team=At<int>(e.object,0);return controlled_pvp::Life::live;
}
bool EnemyControlNativePvpWriteRetainedCache(const controlled_pvp::Emitter& e,int team) noexcept {
    int actual=-1;if(EnemyControlNativePvpReadRetainedCache(e,&actual)!=controlled_pvp::Life::live || !Readable(e.object,4,true))return false;
    Put<int>(const_cast<void*>(e.object),0,team);return true;
}
bool EnemyControlNativePvpCollect(const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,controlled_pvp::Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
    if(count)*count=0;if(emitted)*emitted=0;
    if(root.obj!=boss_core_fixture::genericObject || !count || !emitted || !cap || !ec)return false;
    members[0]=root;*count=1;emitters[0]={controlled_pvp::EmitterKind::liveOwnerTeam,root,root.obj,1};*emitted=1;return true;
}
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {
    if(e.member.obj!=boss_core_fixture::genericObject || e.object!=e.member.obj || !team)return controlled_pvp::Life::invalid;
    if(At<LONG>(e.member.ctrl,8)<=0)return controlled_pvp::Life::gone;*team=At<int>(e.member.obj,kTeam);return controlled_pvp::Life::live;
}
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {int actual=-1;return EnemyControlNativePvpRead(e,&actual)==controlled_pvp::Life::live && actual==team;}
bool EnemyControlNativePvpBaseline(const ObjRef&,const ObjRef& member,int,int,int* team,int* last) noexcept {
    if(!boss_core_fixture::genericBaselineKnown || member.obj!=boss_core_fixture::genericObject)return false;*team=*last=5;return true;
}
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter&,int,int,int* team) noexcept {if(!boss_core_fixture::genericBaselineKnown)return false;*team=5;return true;}
}
namespace edf {
bool Readable(const void* pointer,std::size_t size,bool) noexcept {
    if(pointer==boss_core_fixture::watchedOwner && pointer)++boss_core_fixture::watchedOwnerReads;
    if(!pointer)return false;auto at=reinterpret_cast<std::uintptr_t>(pointer);if(size>UINTPTR_MAX-at)return false;const auto end=at+size;
    while(at<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(at),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=at)return false;at=next;}return true;
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
bool RedirectCall(unsigned char* p,void*,void*,bool& changed) noexcept {p[1]^=0x55;changed=true;return true;}
}
int main() {
    using namespace boss_core_fixture;Image();CHECK(EnemyControlBossPvpReady());CHECK(EnemyControlBossPvpInstall());
    ExactOwnedSelectorCloneThroughRealCore();ActualTwentyMeteorWave();DeniedPublicationWithdrawsTemporaryWeak();RetainedCacheSharedBoundaryUsesRealBossAndCore();UnknownGenericChildBaselineKeepsRealLease();
    CHECK(EnemyControlBossPvpReset());CHECK(controlDeletes==1);VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy_control_pvp_boss_core_test: %u checks, %u failed\n",checks,failures);return failures ? 1 : 0;
}






