// Execute the production adapter with ordinary native GameObjects and real x64 hook thunks.
#include "../src/enemy_control_pvp_transport.cpp"
#include <cstdio>
#include <utility>
#ifdef EDF_TRANSPORT_REAL_CORE
#include "../src/pvp_controlled.cpp"
#endif

namespace transport_fixture {
using crew::ObjRef;using crew::controlled_pvp::Emitter;
struct Object {alignas(16) unsigned char bytes[0x1800]{};alignas(16) unsigned char control[0x20]{};unsigned char head[0x20]{};};
struct Generator {alignas(16) unsigned char bytes[0x380]{};alignas(16) unsigned char control[0x20]{};unsigned char head[0x20]{},remote[0x20]{};};
Object hull{},pending{},child{},anchor{},capsule{},barrier{},other{};
Generator generator{};Object* pendingTarget=&pending;Object* createTarget=&child;
unsigned char componentHead[0x20]{},componentNodes[2][0x20]{},components[2][0x60]{},componentControls[2][0x20]{};
ObjRef leased[16]{};unsigned leasedCount=0;
bool proofOk=true,prepareOk=true,adoptOk=true,applied=true,createOk=true,registerCalls=true,adoptThrow=false;
unsigned creates=0,spawns=0,teams=0,deletes=0,prepares=0,adopts=0,finishes=0,faults=0,redirects=0,controlDeletes=0;
int baselineAtAdopt=-99,lastAtAdopt=-99,ownerTeam=2;unsigned createKind=0x16000;
int checks=0,failures=0;bool sealedBeam=false;bool selectorOwned=false;void* selectorClone[65]{};const void* selectorFunction=reinterpret_cast<const void*>(0x12345678);
void Check(bool value,const char* why,int line){++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,why);}}
#define CHECK(value) transport_fixture::Check(!!(value),#value,__LINE__)
void __fastcall DestroyControl(void*) noexcept {++controlDeletes;}
void __fastcall DestroyObject(void*) noexcept {}
void Control(unsigned char* out) {
    static void* table[2]={reinterpret_cast<void*>(&DestroyObject),reinterpret_cast<void*>(&DestroyControl)};
    edf::Put<void*>(out,0,table);edf::Put<LONG>(out,8,1);edf::Put<LONG>(out,12,1);
}
void Make(Object& out,unsigned kind,int team=1) {
    out=Object{};Control(out.control);
    edf::Put<void*>(out.bytes,0,crew::image+kind);edf::Put<void*>(out.bytes,edf::kSelf,out.bytes);edf::Put<void*>(out.bytes,edf::kSelfCtrl,out.control);
    edf::Put<int>(out.bytes,edf::kTeam,team);edf::Put<int>(out.bytes,0x318,team>=0 ? team : 1);
    edf::Put<void*>(out.bytes,0x550,out.head);edf::Put<void*>(out.head,0,out.head);edf::Put<void*>(out.head,8,out.head);
}
void Lease(const Object& object) {leased[leasedCount++]=ObjRef::Of(object.bytes);}
bool Owned(const ObjRef& ref) {for(unsigned i=0;i<leasedCount;++i)if(crew::transport_pvp_detail::Same(leased[i],ref))return true;return false;}
void AttachGen(Object& owner,unsigned at=0xD60) {
    generator=Generator{};Control(generator.control);
    edf::Put<void*>(generator.bytes,0xE0,owner.bytes);edf::Put<int>(generator.bytes,0xC0,1);edf::Put<int>(generator.bytes,0x68,1);
    edf::Put<void*>(generator.bytes,0x230,generator.head);edf::Put<void*>(generator.head,0,generator.head);edf::Put<void*>(generator.head,8,generator.head);
    edf::Put<void*>(generator.bytes,0x318,generator.remote);edf::Put<void*>(generator.remote,0,generator.remote);edf::Put<void*>(generator.remote,8,generator.remote);
    edf::Put<void*>(owner.bytes,at,generator.bytes);edf::Put<void*>(owner.bytes,at+8,generator.control);
}
void Reset() {
    crew::EnemyControlTransportPvpReset();
    Make(hull,crew::transport_pvp_detail::kDrop);Make(pending,crew::transport_pvp_detail::kSpawner);
    Make(child,0x16000);Make(anchor,crew::transport_pvp_detail::kAnchor);Make(capsule,crew::transport_pvp_detail::kCapsule);
    Make(barrier,crew::transport_pvp_detail::kBarrier);Make(other,0x16008);
    AttachGen(hull);leasedCount=0;proofOk=prepareOk=adoptOk=applied=createOk=registerCalls=true;selectorOwned=false;sealedBeam=false;
    creates=spawns=teams=deletes=prepares=adopts=finishes=faults=redirects=controlDeletes=0;baselineAtAdopt=lastAtAdopt=-99;adoptThrow=false;ownerTeam=2;createKind=0x16000;pendingTarget=&pending;createTarget=&child;
    crew::transport_pvp_detail::installed=true;crew::transport_pvp_detail::incomplete=false;
}
void Jump(unsigned rva,const void* target) {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto value=reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(code+2,&value,8);std::memcpy(crew::image+rva,code,sizeof(code));
}
void __fastcall NativeTeam(void* object,int team,bool registered) noexcept {
    ++teams;
    if(object==hull.bytes && crew::transport_pvp_detail::Kind(ObjRef::Of(object))==crew::transport_pvp_detail::kTime) {
        auto* head=edf::At<unsigned char*>(object,0x1128);
        if(head)for(auto* node=edf::At<unsigned char*>(head,0);node!=head;node=edf::At<unsigned char*>(node,0)) {
            auto* component=edf::At<unsigned char*>(node,0x10);auto* part=edf::At<unsigned char*>(component,0x48);
            const auto* control=edf::At<const unsigned char*>(component,0x50);
            if(part && control && edf::At<LONG>(control,8)>0)NativeTeam(part,team,registered);
        }
        edf::Put<int>(object,0x1160,team);
    }
    if(crew::transport_pvp_detail::Kind(ObjRef::Of(object))==crew::transport_pvp_detail::kBarrier)edf::Put<int>(object,0xF98,team);
    edf::Put<int>(object,edf::kTeam,team);if(team>=0)edf::Put<int>(object,0x318,team);
    auto flags=edf::At<unsigned>(object,0x380);edf::Put<unsigned>(object,0x380,registered ? flags|0x40u : flags&~0x40u);
}
void __fastcall NativeDelete(void* object) noexcept {
    ++deletes;auto* bytes=static_cast<unsigned char*>(object);bytes[0x18]|=4;
    auto* ctrl=edf::At<unsigned char*>(object,edf::kSelfCtrl);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+8))==0 && InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12))==0)DestroyControl(ctrl);
}
unsigned char* __fastcall NativeCreate(void*,const float*,const wchar_t*,void*) noexcept {++creates;if(!createOk)return nullptr;Make(*createTarget,createKind);return createTarget->bytes;}
ObjRef* __fastcall NativePending(void*,ObjRef* out,const float*,void*) noexcept {
    ++spawns;Object& target=*pendingTarget;Make(target,crew::transport_pvp_detail::kSpawner);
    edf::Put<void*>(target.bytes,0x6C8,edf::At<void*>(generator.bytes,0xE0));
    const auto* parent=edf::At<const unsigned char*>(generator.bytes,0xE0);
    edf::Put<const void*>(target.bytes,0x6D0,edf::At<const void*>(parent,edf::kSelfCtrl));
    edf::Put<int>(target.bytes,0x620,edf::At<int>(generator.bytes,0xC0));*out=ObjRef::Of(target.bytes);return out;
}
ObjRef* __fastcall NativeCapsuleCrue(void*,ObjRef* out,const unsigned char*) noexcept {
    ++creates;if(!createOk){*out={};return out;}
    Make(child,0x16000);NativeTeam(child.bytes,1,true);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(child.control+8));*out=ObjRef::Of(child.bytes);return out;
}
alignas(16) unsigned char capsuleSeats[3][0x40]{},capsuleParams[0xA0]{},capsuleFunctor[16]{},capsuleString[32]{};
const wchar_t capsulePath[]=L"known.sgo";
void PrepareCapsule() {
    std::memset(capsuleSeats,0,sizeof(capsuleSeats));std::memset(capsuleParams,0,sizeof(capsuleParams));std::memset(capsuleFunctor,0,sizeof(capsuleFunctor));std::memset(capsuleString,0,sizeof(capsuleString));
    edf::Put<void*>(capsule.bytes,0x1088,capsuleSeats);edf::Put<std::size_t>(capsule.bytes,0x1090,3);edf::Put<std::size_t>(capsule.bytes,0x1098,3);edf::Put<unsigned>(capsule.bytes,0x107C,0);
    edf::Put<void*>(capsuleParams,0x78,capsuleFunctor);edf::Put<int>(capsuleParams,0x88,1);
    edf::Put<void*>(capsuleFunctor,0,crew::image+0x1791E18);edf::Put<void*>(capsuleFunctor,8,capsuleString);
    edf::Put<const void*>(capsuleString,0,capsulePath);edf::Put<std::size_t>(capsuleString,16,9);edf::Put<std::size_t>(capsuleString,24,9);
    edf::Put<void*>(crew::image,0x1791E18+16,crew::image+0x1BC3F0);
}
bool Collect(const ObjRef& root,ObjRef* members,unsigned capacity,unsigned* used,Emitter* emitted,unsigned ecap,unsigned* count) {
    return crew::EnemyControlTransportPvpCollect(root,members,capacity,used,emitted,ecap,count);
}
unsigned CollectHull(ObjRef* members,Emitter* emitters,unsigned* emitted) {
    unsigned count=0;CHECK(Collect(ObjRef::Of(hull.bytes),members,32,&count,emitters,32,emitted));return count;
}
Emitter MirrorAt(const Emitter* emitters,unsigned count,const void* at) {
    for(unsigned i=0;i<count;++i)if(emitters[i].object==at)return emitters[i];CHECK(false);return {};
}
void BeginPending() {ObjRef out{};float matrix[16]{};CHECK(crew::transport_pvp_detail::CreatePending(nullptr,&out,matrix,nullptr,generator.bytes)==&out);CHECK(out.Is(pendingTarget->bytes));}
void Components() {
    std::memset(componentHead,0,sizeof(componentHead));std::memset(componentNodes,0,sizeof(componentNodes));std::memset(components,0,sizeof(components));
    edf::Put<void*>(hull.bytes,0x1128,componentHead);edf::Put<std::size_t>(hull.bytes,0x1130,2);edf::Put<int>(hull.bytes,0x1160,1);
    edf::Put<void*>(componentHead,0,componentNodes[0]);edf::Put<void*>(componentHead,8,componentNodes[1]);
    for(unsigned i=0;i<2;++i) {
        Control(componentControls[i]);edf::Put<void*>(componentNodes[i],0,i ? componentHead : componentNodes[1]);
        edf::Put<void*>(componentNodes[i],8,i ? componentNodes[0] : componentHead);
        edf::Put<void*>(componentNodes[i],0x10,components[i]);edf::Put<void*>(componentNodes[i],0x18,componentControls[i]);
        edf::Put<void*>(components[i],0x18,hull.bytes);
        Object& object=i ? capsule : anchor;edf::Put<void*>(components[i],0x48,object.bytes);edf::Put<void*>(components[i],0x50,object.control);
    }
}
void* Harness(void* target,const void* source,unsigned sourceRegister) {
    if(sourceRegister==13) {
        unsigned char code[]={0x41,0x55,0x48,0x83,0xEC,0x20,0x49,0xBD,0,0,0,0,0,0,0,0,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x20,0x41,0x5D,0xC3};
        const auto value=reinterpret_cast<std::uintptr_t>(source),function=reinterpret_cast<std::uintptr_t>(target);
        std::memcpy(code+8,&value,8);std::memcpy(code+18,&function,8);
        auto* memory=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(memory);if(memory)std::memcpy(memory,code,sizeof(code));return memory;
    }
    unsigned char code[]={0x53,0x48,0x83,0xEC,0x20,0x48,0xBB,0,0,0,0,0,0,0,0,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x20,0x5B,0xC3};
    if(sourceRegister==6){code[0]=0x56;code[6]=0xBE;code[31]=0x5E;}
    const auto value=reinterpret_cast<std::uintptr_t>(source),function=reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(code+7,&value,8);std::memcpy(code+17,&function,8);
    auto* memory=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(memory);if(memory)std::memcpy(memory,code,sizeof(code));return memory;
}
void* CapsuleHarness(void* target,const ObjRef& parent) {
    unsigned char code[]={0x48,0x83,0xEC,0x58,0x48,0xB8,0,0,0,0,0,0,0,0,0x48,0x89,0x44,0x24,0x40,
        0x48,0xB8,0,0,0,0,0,0,0,0,0x48,0x89,0x44,0x24,0x48,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x58,0xC3};
    const auto object=reinterpret_cast<std::uintptr_t>(parent.obj),control=reinterpret_cast<std::uintptr_t>(parent.ctrl),function=reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(code+6,&object,8);std::memcpy(code+21,&control,8);std::memcpy(code+36,&function,8);
    auto* memory=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(memory);if(memory)std::memcpy(memory,code,sizeof(code));return memory;
}
void TestTypeAndInstallProof() {
    Reset();const auto root=ObjRef::Of(hull.bytes);const auto* original=edf::At<const unsigned char*>(hull.bytes,0);
    std::memcpy(selectorClone,original-8,50*8);edf::Put<void*>(hull.bytes,0,selectorClone+1);
    CHECK(crew::EnemyControlTransportPvpSupports(root));
    selectorClone[1+22]=const_cast<void*>(selectorFunction);CHECK(!crew::EnemyControlTransportPvpSupports(root));selectorClone[1+22]=edf::At<void*>(original,22*8);
    selectorClone[1+14]=const_cast<void*>(selectorFunction);CHECK(!crew::EnemyControlTransportPvpSupports(root));
    selectorOwned=true;CHECK(crew::EnemyControlTransportPvpSupports(root));selectorClone[1+14]=reinterpret_cast<void*>(3);CHECK(!crew::EnemyControlTransportPvpSupports(root));
    edf::Put<const void*>(hull.bytes,0,original);CHECK(crew::EnemyControlTransportPvpSupports(root));
    using namespace crew::transport_pvp_detail;
    for(const auto pair:{std::pair<unsigned,unsigned>{kCreateSpawnerSite,kCreateSpawner},{kCreateFinalSite,kCreate},{kTeamFinalSite,crew::kSetTeam},{kCreatePartSite,kCreate},{kCapsuleCrueSite,kCapsuleCrueCreate}}) {
        crew::image[pair.first]=0xE8;edf::Put<std::int32_t>(crew::image,pair.first+1,static_cast<std::int32_t>(pair.second-pair.first-5));
    }
    attempted=installed=false;CHECK(crew::EnemyControlTransportPvpInstall());CHECK(redirects==5 && crew::EnemyControlTransportPvpReady());
    CHECK(crew::EnemyControlTransportPvpInstall() && redirects==5);
    crew::image[anchors[0].rva]^=1;CHECK(!crew::EnemyControlTransportPvpReady());crew::image[anchors[0].rva]^=1;
}
void TestTypedGeneratorAndMirrors() {
    Reset();ObjRef members[32]{};Emitter emitters[32]{};unsigned count=0;
    CHECK(CollectHull(members,emitters,&count)==1 && count==2);CHECK(edf::At<LONG>(hull.control,12)==1 && edf::At<LONG>(generator.control,12)==1);
    const auto mirror=MirrorAt(emitters,count,generator.bytes+0xC0);int team=-1;
    CHECK(crew::EnemyControlTransportPvpRead(mirror,&team)==crew::controlled_pvp::Life::live && team==1);
    CHECK(crew::EnemyControlTransportPvpWrite(mirror,2));CHECK(edf::At<int>(generator.bytes,0xC0)==2);
    CHECK(crew::EnemyControlTransportPvpBaseline(mirror,2,2,&team) && team==1);
    edf::Put<void*>(generator.bytes,0xE0,other.bytes);
    CHECK(crew::EnemyControlTransportPvpRead(mirror,&team)==crew::controlled_pvp::Life::invalid);
    CHECK(!crew::EnemyControlTransportPvpWrite(mirror,1));edf::Put<void*>(generator.bytes,0xE0,hull.bytes);
    CHECK(!crew::EnemyControlTransportPvpWrite(mirror,7));CHECK(crew::EnemyControlTransportPvpWrite(mirror,1));
    CHECK(edf::At<LONG>(hull.control,12)==2 && edf::At<LONG>(generator.control,12)==2);
    crew::EnemyControlTransportPvpReset();CHECK(edf::At<LONG>(hull.control,12)==1 && edf::At<LONG>(generator.control,12)==1);
}
void TestBirthBaselineAndInfiniteClosure() {
    Reset();ObjRef members[32]{};Emitter emitters[32]{};unsigned count=0;CollectHull(members,emitters,&count);
    const auto mirror=MirrorAt(emitters,count,generator.bytes+0xC0);CHECK(crew::EnemyControlTransportPvpWrite(mirror,2));
    Lease(hull);BeginPending();CHECK(crew::transport_pvp_detail::Recorded(ObjRef::Of(pending.bytes))->cache==1);
    float matrix[16]{};auto* result=crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes);
    CHECK(result==child.bytes && creates==1 && prepares==1 && adopts==1 && finishes==1);
    CHECK(baselineAtAdopt==1 && lastAtAdopt==1 && edf::At<int>(child.bytes,edf::kTeam)==2);
    crew::transport_pvp_detail::SetFinalTeam(child.bytes,2,true,pending.bytes);CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    const auto* record=crew::transport_pvp_detail::Recorded(ObjRef::Of(child.bytes));CHECK(record && record->team==1 && record->last==1);
    CHECK(CollectHull(members,emitters,&count)==2); // Adopted child lives in its independent controlled spawn group.
    int team=0,last=0;CHECK(crew::EnemyControlTransportPvpBaseline(ObjRef::Of(hull.bytes),ObjRef::Of(child.bytes),1,1,&team,&last) && team==1 && last==1);
    auto pendingMirror=MirrorAt(emitters,count,pending.bytes+0x620);
    CHECK(crew::EnemyControlTransportPvpBaseline(pendingMirror,1,1,&team) && team==1);
    CHECK(crew::EnemyControlTransportPvpWrite(pendingMirror,2));CHECK(edf::At<int>(pending.bytes,0x620)==2);
    const auto weak=edf::At<LONG>(child.control,12);CHECK(weak==2);
    crew::EnemyControlTransportPvpReset();CHECK(edf::At<LONG>(child.control,12)==1 && edf::At<LONG>(hull.control,12)==1);
}
void TestPreflightAndRollback() {
    for(unsigned which=0;which<4;++which) {
        Reset();Lease(hull);BeginPending();proofOk=which!=0;prepareOk=which!=1;adoptOk=which!=2;createOk=which!=3;
        float matrix[16]{};CHECK(!crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"unknown.sgo",nullptr,pending.bytes));
        if(which<2)CHECK(creates==0 && adopts==0 && faults==1);
        if(which==2)CHECK(creates==1 && adopts==1 && deletes==1 && faults==1 && child.bytes[0x18]==4);
        if(which==3)CHECK(creates==1 && adopts==0 && deletes==0 && finishes==1);
    }
    Reset();BeginPending();proofOk=prepareOk=false;float matrix[16]{};
    CHECK(crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"unknown.sgo",nullptr,pending.bytes)==child.bytes);
    CHECK(creates==1 && prepares==0 && faults==0);crew::transport_pvp_detail::SetFinalTeam(child.bytes,3,true,pending.bytes);
    CHECK(edf::At<int>(child.bytes,edf::kTeam)==3 && crew::transport_pvp_detail::Recorded(ObjRef::Of(child.bytes))->team==3);
    Reset();Lease(hull);BeginPending();
    for(unsigned i=1;i<crew::transport_pvp_detail::kBornLimit;++i)crew::transport_pvp_detail::born[i].child=ObjRef::Of(child.bytes);
    CHECK(!crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes));CHECK(creates==0 && faults==1);
    for(unsigned i=1;i<crew::transport_pvp_detail::kBornLimit;++i)crew::transport_pvp_detail::born[i]={};
}
void TestCapsuleBirthAndCleanup() {
    Reset();PrepareCapsule();Lease(capsule);const auto parent=ObjRef::Of(capsule.bytes);ObjRef out{};
    CHECK(crew::transport_pvp_detail::CapsuleResource(capsuleParams)==capsulePath);
    CHECK(crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&parent)==&out && out.Is(child.bytes));
    CHECK(creates==1 && prepares==1 && adopts==1 && baselineAtAdopt==1 && edf::At<int>(child.bytes,edf::kTeam)==2);
    CHECK(edf::At<LONG>(child.control,8)==2);crew::transport_pvp_detail::ReleaseStrong(out);CHECK(!out && edf::At<LONG>(child.control,8)==1);
    for(unsigned bad=0;bad<4;++bad) {
        Reset();PrepareCapsule();Lease(capsule);out={};
        if(bad==0)edf::Put<void*>(capsuleFunctor,0,crew::image+1);
        if(bad==1)edf::Put<unsigned>(capsule.bytes,0x107C,3);
        if(bad==2)proofOk=false;
        if(bad==3)prepareOk=false;
        CHECK(crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&parent)==&out && !out);
        CHECK(creates==0 && faults==1);
    }
    Reset();PrepareCapsule();Lease(capsule);adoptOk=false;out={};
    CHECK(crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&parent)==&out && !out);
    CHECK(creates==1 && deletes==1 && faults==1 && edf::At<LONG>(child.control,8)==0);
    crew::transport_pvp_detail::Sweep();CHECK(edf::At<LONG>(child.control,12)==0 && controlDeletes==1);
    Reset();PrepareCapsule();Lease(capsule);adoptThrow=true;out={};
    CHECK(!crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&parent)->obj);
    CHECK(deletes==1 && faults==1 && finishes==1 && edf::At<LONG>(child.control,8)==0);
    Reset();PrepareCapsule();proofOk=false;out={};
    CHECK(crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&parent)==&out && out.Is(child.bytes));
    CHECK(prepares==0 && adopts==0 && faults==0 && edf::At<int>(child.bytes,edf::kTeam)==1);
    crew::transport_pvp_detail::ReleaseStrong(out);
    Reset();Lease(hull);BeginPending();adoptThrow=true;float matrix[16]{};
    CHECK(!crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes));
    CHECK(creates==1 && deletes==1 && faults==1 && finishes==1 && edf::At<LONG>(child.control,8)==0);
}
void TestRetainedBeamDelegation() {
    Reset();sealedBeam=true;edf::Put<int>(child.bytes,0xB40,2);
    const Emitter emitter{crew::controlled_pvp::EmitterKind::ownerLastTeamCache,ObjRef::Of(anchor.bytes),child.bytes+0xB40,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(child.control)),ObjRef::Of(child.bytes)};
    NativeDelete(anchor.bytes);int team=0;
    CHECK(crew::EnemyControlTransportPvpRead(emitter,&team)==crew::controlled_pvp::Life::live && team==2);
    CHECK(crew::EnemyControlTransportPvpWrite(emitter,1));CHECK(edf::At<int>(child.bytes,0xB40)==1);
}
void TestTimeShipTypedParts() {
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Components();
    edf::Put<void*>(anchor.bytes,0x1088,barrier.bytes);edf::Put<void*>(anchor.bytes,0x1090,barrier.control);edf::Put<int>(barrier.bytes,0xF98,1);
    ObjRef members[32]{};Emitter emitters[32]{};unsigned used=0,count=0;
    CHECK(Collect(ObjRef::Of(hull.bytes),members,32,&used,emitters,32,&count));CHECK(used==4 && count==3);
    Lease(hull);Lease(anchor);int team=-1,last=-1;
    CHECK(crew::EnemyControlTransportPvpBaseline(ObjRef::Of(hull.bytes),ObjRef::Of(capsule.bytes),1,1,&team,&last) && team==1 && last==1);
    CHECK(crew::EnemyControlTransportPvpBaseline(ObjRef::Of(hull.bytes),ObjRef::Of(barrier.bytes),1,1,&team,&last) && team==1 && last==1);
    CHECK(!crew::EnemyControlTransportPvpBaseline(ObjRef::Of(other.bytes),ObjRef::Of(capsule.bytes),1,1,&team,&last));
    edf::Put<void*>(components[0],0x18,other.bytes);used=count=99;
    CHECK(!Collect(ObjRef::Of(hull.bytes),members,32,&used,emitters,32,&count));CHECK(used==0 && count==0);
    edf::Put<void*>(components[0],0x18,hull.bytes);edf::Put<void*>(componentNodes[1],8,componentHead);
    CHECK(!Collect(ObjRef::Of(hull.bytes),members,32,&used,emitters,32,&count));
}
void TestOrdinaryCapsuleBridge() {
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Components();hull.bytes[0x18]|=4;
    crew::transport_pvp_detail::installed=false;crew::transport_pvp_detail::incomplete=true;
    const auto root=ObjRef::Of(hull.bytes),part=ObjRef::Of(capsule.bytes);
    std::uint64_t token=99;CHECK(crew::EnemyControlTransportPvpCanPrepareCapsules(root,1));
    CHECK(crew::EnemyControlTransportPvpPrepareCapsules(root,1,&token) && !token);
    const auto calls=teams;CHECK(!crew::transport_pvp_detail::Recorded(part));
    CHECK(crew::EnemyControlTransportPvpAdoptCapsule(token,root,part));CHECK(teams==calls && edf::At<int>(capsule.bytes,edf::kTeam)==1);
    CHECK(!crew::EnemyControlTransportPvpAdoptCapsule(0,root,ObjRef::Of(other.bytes)));
    edf::Put<void*>(components[1],0x18,other.bytes);CHECK(!crew::EnemyControlTransportPvpAdoptCapsule(0,root,part));
    edf::Put<void*>(components[1],0x18,hull.bytes);
    CHECK(!crew::EnemyControlTransportPvpCanPrepareCapsules(root,0));
    CHECK(!crew::EnemyControlTransportPvpCanPrepareCapsules(root,crew::controlled_pvp::kMaxMembers+1));
    CHECK(!crew::EnemyControlTransportPvpPrepareCapsules(root,1,nullptr));
    edf::Put<void*>(hull.bytes,edf::kSelfCtrl,other.control);CHECK(!crew::EnemyControlTransportPvpCanPrepareCapsules(root,1));
    edf::Put<void*>(hull.bytes,edf::kSelfCtrl,hull.control);crew::EnemyControlTransportPvpFinishCapsules(0);
}
#ifdef EDF_TRANSPORT_REAL_CORE
bool RootCollect(void*,const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,Emitter* emitters,unsigned ecap,unsigned* emitted) noexcept {return Collect(root,members,cap,count,emitters,ecap,emitted);}
crew::controlled_pvp::Life RootRead(void*,const Emitter& emitter,int* team) noexcept {return crew::EnemyControlTransportPvpRead(emitter,team);}
bool RootWrite(void*,const Emitter& emitter,int team) noexcept {return crew::EnemyControlTransportPvpWrite(emitter,team);}
bool RootMemberBaseline(void*,const ObjRef& root,const ObjRef& member,int team,int last,int* original,int* originalLast) noexcept {return crew::EnemyControlTransportPvpBaseline(root,member,team,last,original,originalLast);}
bool RootEmitterBaseline(void*,const Emitter& emitter,int team,int last,int* original) noexcept {return crew::EnemyControlTransportPvpBaseline(emitter,team,last,original);}
void TestRealCore() {
    Reset();
    const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    const auto root=ObjRef::Of(hull.bytes);crew::controlled_pvp::Lease lease{};
    CHECK(crew::controlled_pvp::Probe(root,hooks));CHECK(edf::At<LONG>(hull.control,12)==1 && edf::At<LONG>(generator.control,12)==1);
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));CHECK(lease.id);
    CHECK(edf::At<int>(hull.bytes,edf::kTeam)==2 && edf::At<int>(generator.bytes,0x68)==2 && edf::At<int>(generator.bytes,0xC0)==2);
    BeginPending();CHECK(crew::controlled_pvp::Frame(lease));CHECK(edf::At<int>(pending.bytes,0x620)==2);
    float matrix[16]{};CHECK(crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes)==child.bytes);
    CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    crew::transport_pvp_detail::SetFinalTeam(child.bytes,2,true,pending.bytes);CHECK(crew::controlled_pvp::Frame(lease));
    crew::controlled_pvp::Status status{};CHECK(crew::controlled_pvp::ReadStatus(lease,&status));CHECK(status.followsOwnerTeam && status.members==3);
    ownerTeam=0;CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(edf::At<int>(hull.bytes,edf::kTeam)==1 && edf::At<int>(child.bytes,edf::kTeam)==1 && edf::At<int>(pending.bytes,0x620)==1);
    CHECK(edf::At<int>(generator.bytes,0x68)==1 && edf::At<int>(generator.bytes,0xC0)==1);
    // A native task may change generator teams while this same control lease is stably mode-off.
    edf::Put<int>(generator.bytes,0x68,4);edf::Put<int>(generator.bytes,0xC0,4);
    CHECK(crew::controlled_pvp::Frame(lease));
    ownerTeam=2;CHECK(crew::controlled_pvp::Frame(lease));CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    pendingTarget=&anchor;BeginPending();CHECK(crew::transport_pvp_detail::Recorded(ObjRef::Of(anchor.bytes))->cache==4);
    CHECK(crew::controlled_pvp::Frame(lease));
    crew::transport_pvp_detail::incomplete=true;
    CHECK(crew::controlled_pvp::Restore(&lease));CHECK(!lease.id);
    CHECK(edf::At<int>(hull.bytes,edf::kTeam)==1 && edf::At<int>(child.bytes,edf::kTeam)==1 && edf::At<int>(pending.bytes,0x620)==1);
    CHECK(edf::At<int>(generator.bytes,0x68)==4 && edf::At<int>(generator.bytes,0xC0)==4);
    ObjRef members[32]{};Emitter emitters[32]{};unsigned count=0,emitted=0;
    CHECK(!Collect(root,members,32,&count,emitters,32,&emitted));CHECK(count==0 && emitted==0);
    crew::transport_pvp_detail::incomplete=false;
    edf::Put<int>(generator.bytes,0x68,3);edf::Put<int>(generator.bytes,0xC0,3);
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));
    CHECK(edf::At<int>(generator.bytes,0x68)==2 && edf::At<int>(generator.bytes,0xC0)==2);
    pendingTarget=&other;createTarget=&barrier;BeginPending();
    CHECK(crew::transport_pvp_detail::Recorded(ObjRef::Of(other.bytes))->cache==3);
    CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,other.bytes)==barrier.bytes);
    CHECK(edf::At<int>(barrier.bytes,edf::kTeam)==2);
    CHECK(crew::controlled_pvp::Restore(&lease));CHECK(edf::At<int>(barrier.bytes,edf::kTeam)==3);
    CHECK(edf::At<int>(generator.bytes,0x68)==3 && edf::At<int>(generator.bytes,0xC0)==3);
    crew::EnemyControlTransportPvpReset();CHECK(edf::At<LONG>(hull.control,12)==1 && edf::At<LONG>(pending.control,12)==1 && edf::At<LONG>(child.control,12)==1);
}
void TestRealCoreCreatorGone() {
    Reset();const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    const auto root=ObjRef::Of(hull.bytes);crew::controlled_pvp::Lease lease{};
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));BeginPending();CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(edf::At<int>(pending.bytes,0x620)==2);
    // Model a separate strong release/detach, not normal SceneObject Delete's recursive child deletion.
    edf::Put<LONG>(hull.control,8,0);if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(hull.control+12))==0)DestroyControl(hull.control);
    float matrix[16]{};CHECK(crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes)==child.bytes);
    crew::transport_pvp_detail::SetFinalTeam(child.bytes,edf::At<int>(pending.bytes,0x620),true,pending.bytes);
    CHECK(edf::At<int>(child.bytes,edf::kTeam)==1 && edf::At<int>(pending.bytes,0x620)==1);
    CHECK(crew::controlled_pvp::Restore(&lease));CHECK(!lease.id);
    crew::EnemyControlTransportPvpReset();
}
void TestRealCoreDetachedAnchor() {
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Components();AttachGen(anchor,0x10B0);
    const auto root=ObjRef::Of(hull.bytes),parent=ObjRef::Of(anchor.bytes);
    CHECK(crew::transport_pvp_detail::Record(root,parent,1,1));
    const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    crew::controlled_pvp::Lease lease{};
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));BeginPending();CHECK(crew::controlled_pvp::Frame(lease));
    edf::Put<LONG>(anchor.control,8,0);if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(anchor.control+12))==0)DestroyControl(anchor.control);
    ObjRef actualRoot{};CHECK(crew::transport_pvp_detail::LeaseOwner(parent,&actualRoot));CHECK(crew::transport_pvp_detail::Same(actualRoot,root));
    float matrix[16]{};CHECK(crew::transport_pvp_detail::CreateFinal(nullptr,matrix,L"known.sgo",nullptr,pending.bytes)==child.bytes);
    CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    crew::transport_pvp_detail::SetFinalTeam(child.bytes,2,true,pending.bytes);CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(crew::controlled_pvp::Restore(&lease));CHECK(edf::At<int>(child.bytes,edf::kTeam)==1);
    crew::EnemyControlTransportPvpReset();
}
void TestRealCoreLateCapsules() {
    for(unsigned mode=0;mode<5;++mode) {
        Reset();Make(hull,crew::transport_pvp_detail::kTime,mode==1 ? -1 : mode==3 ? 3 : 1);Components();
        if(mode==1)edf::Put<int>(hull.bytes,0x318,4);
        if(mode==3)ownerTeam=0;
        const auto root=ObjRef::Of(hull.bytes);
        const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
        crew::controlled_pvp::Lease lease{};CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));
        if(mode==1)NativeTeam(hull.bytes,2,true); // The saved root was closed; the native task physically reopened it.
        if(mode==2)NativeTeam(hull.bytes,-1,false);
        if(mode!=4)hull.bytes[0x18]|=4; // The accepted action retains the exact strong root after logical deletion.
        CHECK(crew::transport_pvp_detail::KnownCurrent(root));
        CHECK(crew::transport_pvp_detail::Identity(root)==(mode==4 ? crew::controlled_pvp::Life::live : crew::controlled_pvp::Life::gone));
        std::uint64_t token=0;CHECK(crew::EnemyControlTransportPvpCanPrepareCapsules(root,1));
        CHECK(crew::EnemyControlTransportPvpPrepareCapsules(root,1,&token) && token);
        float matrix[16]{};createKind=crew::transport_pvp_detail::kCapsule;
        CHECK(crew::transport_pvp_detail::CreateTimePart(nullptr,matrix,L"capsule.sgo",nullptr,hull.bytes)==child.bytes);
        const auto ref=ObjRef::Of(child.bytes);const auto* row=crew::transport_pvp_detail::Recorded(ref);
        const int original=mode==1 ? 4 : mode==2 ? -1 : mode==3 ? 3 : 1;
        CHECK(row && row->team==original && row->last==(original<0 ? 1 : original));
        NativeTeam(child.bytes,edf::At<int>(hull.bytes,edf::kTeam),true); // Actual 3EC8D6 occurs after the factory hook returns.
        if(mode==4){edf::Put<void*>(components[1],0x48,child.bytes);edf::Put<void*>(components[1],0x50,child.control);}
        CHECK(!crew::EnemyControlTransportPvpAdoptCapsule(0,root,ref)); // Owned actors cannot bypass their accepted budget.
        CHECK(crew::EnemyControlTransportPvpAdoptCapsule(token,root,ref));
        const int wanted=mode==2 ? -1 : mode==3 ? 3 : 2;
        CHECK(edf::At<int>(child.bytes,edf::kTeam)==wanted); // No Frame is available on the closing controller.
        crew::EnemyControlTransportPvpFinishCapsules(token);crew::EnemyControlTransportPvpFinishCapsules(token);
        CHECK(crew::controlled_pvp::OwnsMember(ref));CHECK(crew::controlled_pvp::Restore(&lease));
        CHECK(edf::At<int>(child.bytes,edf::kTeam)==original && edf::At<int>(child.bytes,0x318)==(original<0 ? 1 : original));
        crew::EnemyControlTransportPvpReset();CHECK(edf::At<LONG>(child.control,12)==1);
    }
}
void TestRealCoreLateCapsuleReject() {
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Components();
    const auto root=ObjRef::Of(hull.bytes);const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    crew::controlled_pvp::Lease lease{};CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));hull.bytes[0x18]|=4;
    std::uint64_t token=0;CHECK(crew::EnemyControlTransportPvpPrepareCapsules(root,1,&token));
    float matrix[16]{};createKind=crew::transport_pvp_detail::kCapsule;
    CHECK(crew::transport_pvp_detail::CreateTimePart(nullptr,matrix,L"capsule.sgo",nullptr,hull.bytes)==child.bytes);
    NativeTeam(child.bytes,2,true);
    // A populated newborn exceeds the exact body-only proof; it must remain unpublished for caller rollback.
    std::memset(capsuleSeats,0,sizeof(capsuleSeats));edf::Put<void*>(child.bytes,0x1088,capsuleSeats);
    edf::Put<std::size_t>(child.bytes,0x1090,1);edf::Put<std::size_t>(child.bytes,0x1098,1);
    edf::Put<void*>(capsuleSeats[0],0x30,other.bytes);edf::Put<void*>(capsuleSeats[0],0x38,other.control);
    CHECK(!crew::EnemyControlTransportPvpAdoptCapsule(token,root,ObjRef::Of(child.bytes)));
    CHECK(!crew::controlled_pvp::OwnsMember(ObjRef::Of(child.bytes)) && edf::At<int>(child.bytes,edf::kTeam)==2);
    NativeDelete(child.bytes);crew::EnemyControlTransportPvpFinishCapsules(token);
    CHECK(crew::controlled_pvp::Restore(&lease));crew::EnemyControlTransportPvpReset();
}
void TestRealCoreTimeClosure() {
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Components();
    edf::Put<void*>(anchor.bytes,0x1088,barrier.bytes);edf::Put<void*>(anchor.bytes,0x1090,barrier.control);
    NativeTeam(barrier.bytes,-1,false);
    const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    const auto root=ObjRef::Of(hull.bytes);crew::controlled_pvp::Lease lease{};
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));
    CHECK(edf::At<int>(hull.bytes,0x1160)==2 && edf::At<int>(barrier.bytes,edf::kTeam)==-1 && edf::At<int>(barrier.bytes,0xF98)==-1);
    NativeTeam(hull.bytes,-1,false);CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(edf::At<int>(hull.bytes,edf::kTeam)==-1 && edf::At<int>(hull.bytes,0x1160)==-1 && edf::At<int>(barrier.bytes,0xF98)==-1);
    NativeTeam(hull.bytes,1,true);CHECK(crew::controlled_pvp::Frame(lease));
    CHECK(edf::At<int>(hull.bytes,edf::kTeam)==2 && edf::At<int>(hull.bytes,0x1160)==2 && edf::At<int>(barrier.bytes,0xF98)==-1);
    CHECK(crew::controlled_pvp::Restore(&lease));CHECK(edf::At<int>(barrier.bytes,edf::kTeam)==-1 && edf::At<int>(barrier.bytes,0xF98)==-1);
    crew::EnemyControlTransportPvpReset();
}
void TestRealCoreCapsule() {
    Reset();PrepareCapsule();
    const crew::controlled_pvp::Hooks hooks{nullptr,&RootCollect,&RootRead,&RootWrite,&RootMemberBaseline,&RootEmitterBaseline};
    const auto root=ObjRef::Of(capsule.bytes);crew::controlled_pvp::Lease lease{};
    CHECK(crew::controlled_pvp::Acquire(root,"authenticated-owner",hooks,&lease));
    ObjRef out{};CHECK(crew::transport_pvp_detail::CreateCapsuleCrue(nullptr,&out,capsuleParams,&root)==&out && out.Is(child.bytes));
    edf::Put<void*>(capsuleSeats[0],0x30,const_cast<void*>(out.obj));edf::Put<const void*>(capsuleSeats[0],0x38,out.ctrl);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(child.control+12));
    crew::transport_pvp_detail::ReleaseStrong(out);
    CHECK(crew::controlled_pvp::Frame(lease));CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    NativeDelete(capsule.bytes);CHECK(crew::controlled_pvp::Restore(&lease));CHECK(edf::At<int>(child.bytes,edf::kTeam)==1);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(child.control+12))==0)DestroyControl(child.control); // Native capsule seat's weak release.
    crew::EnemyControlTransportPvpReset();CHECK(edf::At<LONG>(child.control,12)==1);
}
#endif
void TestRealThunkArgumentAbi() {
    Reset();float matrix[16]{};ObjRef out{};
    auto* pendingThunk=crew::transport_pvp_detail::FiveArgThunk(nullptr,reinterpret_cast<void*>(&crew::transport_pvp_detail::CreatePending),6);
    auto* harness=Harness(pendingThunk,generator.bytes,6);CHECK(harness);
    if(harness)CHECK(reinterpret_cast<crew::transport_pvp_detail::SpawnerFn>(harness)(nullptr,&out,matrix,nullptr)==&out && out.Is(pending.bytes));
    if(harness)VirtualFree(harness,0,MEM_RELEASE);if(pendingThunk)VirtualFree(pendingThunk,0,MEM_RELEASE);
    Lease(hull);
    auto* finalThunk=crew::transport_pvp_detail::FiveArgThunk(nullptr,reinterpret_cast<void*>(&crew::transport_pvp_detail::CreateFinal),3);
    harness=Harness(finalThunk,pending.bytes,3);CHECK(harness);
    if(harness)CHECK(reinterpret_cast<crew::transport_pvp_detail::CreateFn>(harness)(nullptr,matrix,L"known.sgo",nullptr)==child.bytes);
    if(harness)VirtualFree(harness,0,MEM_RELEASE);if(finalThunk)VirtualFree(finalThunk,0,MEM_RELEASE);
    auto* teamThunk=crew::transport_pvp_detail::TeamThunk(nullptr);harness=Harness(teamThunk,pending.bytes,3);CHECK(harness);
    if(harness)reinterpret_cast<crew::transport_pvp_detail::TeamFn>(harness)(child.bytes,1,true);
    CHECK(edf::At<int>(child.bytes,edf::kTeam)==2);
    if(harness)VirtualFree(harness,0,MEM_RELEASE);if(teamThunk)VirtualFree(teamThunk,0,MEM_RELEASE);
    Reset();Make(hull,crew::transport_pvp_detail::kTime);Lease(hull);createKind=crew::transport_pvp_detail::kCapsule;
    auto* partThunk=crew::transport_pvp_detail::FiveArgThunk(nullptr,reinterpret_cast<void*>(&crew::transport_pvp_detail::CreateTimePart),13);
    harness=Harness(partThunk,hull.bytes,13);CHECK(harness);
    if(harness)CHECK(reinterpret_cast<crew::transport_pvp_detail::CreateFn>(harness)(nullptr,matrix,L"capsule.sgo",nullptr)==child.bytes);
    auto* captured=crew::transport_pvp_detail::Recorded(ObjRef::Of(child.bytes));CHECK(captured && captured->parent.Is(hull.bytes) && captured->team==1);
    if(harness)VirtualFree(harness,0,MEM_RELEASE);if(partThunk)VirtualFree(partThunk,0,MEM_RELEASE);
    Reset();PrepareCapsule();Lease(capsule);out={};
    auto* capsuleThunk=crew::transport_pvp_detail::CapsuleCrueThunk(nullptr);harness=CapsuleHarness(capsuleThunk,ObjRef::Of(capsule.bytes));CHECK(harness);
    if(harness)CHECK(reinterpret_cast<crew::transport_pvp_detail::CapsuleCrueFn>(harness)(nullptr,&out,capsuleParams)==&out && out.Is(child.bytes));
    CHECK(adopts==1 && baselineAtAdopt==1 && edf::At<int>(child.bytes,edf::kTeam)==2);
    crew::transport_pvp_detail::ReleaseStrong(out);
    if(harness)VirtualFree(harness,0,MEM_RELEASE);if(capsuleThunk)VirtualFree(capsuleThunk,0,MEM_RELEASE);
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {return pointer!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
void* AllocateNearCode(const void*,const unsigned char* bytes,std::size_t size) noexcept {
    auto* memory=VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(memory)std::memcpy(memory,bytes,size);return memory;
}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept {++transport_fixture::redirects;changed=true;return transport_fixture::registerCalls;}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
bool EnemyControlOwnsTargetSelector(const void* object,const void* function) noexcept {return transport_fixture::selectorOwned && object==transport_fixture::hull.bytes && function==transport_fixture::selectorFunction && At<const void*>(object,0)==transport_fixture::selectorClone+1;}
bool EnemyControlNativePvpCollect(const ObjRef& root,ObjRef* out,unsigned capacity,unsigned* count,controlled_pvp::Emitter*,unsigned,unsigned* emitted) noexcept {
    *count=*emitted=0;if(!capacity || At<const void*>(root.obj,0)!=image+0x16000)return false;out[0]=root;*count=1;return true;
}
bool EnemyControlNativePvpCacheRetained(const controlled_pvp::Emitter& e) noexcept {
    using namespace transport_fixture;return sealedBeam && e.storage.Is(child.bytes) && e.member.obj==anchor.bytes && e.member.ctrl==anchor.control && e.object==child.bytes+0xB40;
}
controlled_pvp::Life EnemyControlNativePvpReadRetainedCache(const controlled_pvp::Emitter& e,int* team) noexcept {
    if(!team || !EnemyControlNativePvpCacheRetained(e))return controlled_pvp::Life::invalid;
    *team=At<int>(e.object,0);return controlled_pvp::Life::live;
}
bool EnemyControlNativePvpWriteRetainedCache(const controlled_pvp::Emitter& e,int team) noexcept {
    if(!EnemyControlNativePvpCacheRetained(e))return false;Put<int>(const_cast<void*>(e.object),0,team);return true;
}
bool EnemyControlNativePvpCollectBigBattery(const ObjRef& owner,const unsigned char* unit,const void* control,controlled_pvp::Emitter* out,unsigned cap,unsigned* count) noexcept {
    if(!count || *count==cap)return false;
    out[(*count)++]={controlled_pvp::EmitterKind::liveOwnerLastTeam,owner,unit,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control))};return true;
}
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter&,int*) noexcept {return controlled_pvp::Life::invalid;}
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter&,int) noexcept {return false;}
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter&,int,int,int*) noexcept {return false;}
bool ResourceCollect(void*,const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,controlled_pvp::Emitter* emitters,unsigned ecap,unsigned* emitted) noexcept {
    return EnemyControlNativePvpCollect(root,members,cap,count,emitters,ecap,emitted);
}
bool EnemyControlNativePvpSpawnProof(const wchar_t*,controlled_pvp::SpawnProof* out) noexcept {
    if(out){*out={};out->maxMembers=1;out->maxEmitters=0;out->hooks.collect=&ResourceCollect;}
    return transport_fixture::proofOk;
}
#ifdef EDF_TRANSPORT_REAL_CORE
int PvpSupportTeam(const char*) noexcept {return transport_fixture::ownerTeam;}
#endif
#ifndef EDF_TRANSPORT_REAL_CORE
namespace controlled_pvp {
bool OwnsMember(const ObjRef& ref) noexcept {return transport_fixture::Owned(ref);}
bool ReadControlRoot(const ObjRef& ref,ObjRef* root) noexcept {if(!root || !OwnsMember(ref))return false;*root=transport_fixture::leased[0];return true;}
bool HasTemporaryTeam(const ObjRef& ref) noexcept {return OwnsMember(ref) && transport_fixture::applied;}
bool ReadEmitterBaseline(const Emitter&,int* team) noexcept {*team=1;return true;}
bool ReadBaselineForMember(const ObjRef& ref,int* team,int* last) noexcept {if(!OwnsMember(ref))return false;*team=*last=1;return true;}
bool ReadBaseline(const ObjRef&,const ObjRef& member,int* team,int* last) noexcept {return ReadBaselineForMember(member,team,last);}
bool ReadAppliedTeam(const ObjRef& ref,int* team) noexcept {if(!OwnsMember(ref) || !transport_fixture::applied)return false;*team=2;return true;}
bool CanPrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count) noexcept {
    return OwnsMember(parent) && proof.maxMembers==1 && proof.maxEmitters==0 && count && transport_fixture::prepareOk;
}
bool PrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count,std::uint64_t* token) noexcept {
    ++transport_fixture::prepares;if(token)*token=0;
    if(!token || !CanPrepareAcceptedSpawns(parent,proof,count))return false;*token=7;return true;
}
bool PrepareSpawns(const ObjRef&,const SpawnProof&,unsigned,std::uint64_t* token) noexcept {++transport_fixture::prepares;*token=transport_fixture::prepareOk ? 7 : 0;return transport_fixture::prepareOk;}
bool AdoptSpawn(std::uint64_t,const ObjRef&,const ObjRef& child) noexcept {
    if(transport_fixture::adoptThrow)RaiseException(0xE1234567,0,0,nullptr);
    ++transport_fixture::adopts;transport_fixture::baselineAtAdopt=At<int>(child.obj,kTeam);transport_fixture::lastAtAdopt=At<int>(child.obj,0x318);
    if(!transport_fixture::adoptOk)return false;
    transport_fixture::leased[transport_fixture::leasedCount++]=child;Put<int>(const_cast<void*>(child.obj),kTeam,2);Put<int>(const_cast<void*>(child.obj),0x318,2);return true;
}
void FinishSpawns(std::uint64_t token) noexcept {if(token)++transport_fixture::finishes;}
bool FaultForMember(const ObjRef&,Result) noexcept {++transport_fixture::faults;transport_fixture::applied=false;return true;}
}
#endif
}
int main() {
    using namespace transport_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));CHECK(crew::image);if(!crew::image)return 1;
    for(const auto& bytes:crew::transport_pvp_detail::anchors)std::memcpy(crew::image+bytes.rva,bytes.bytes,bytes.size);
    unsigned nativeIndex=0;
    for(const auto& type:crew::transport_pvp_detail::types) {
        const unsigned col=0x100000+(nativeIndex*0x100),descriptor=0x110000+(nativeIndex++*0x100);
        edf::Put<void*>(crew::image,type.vtable-8,crew::image+col);edf::Put<unsigned>(crew::image,col,1);
        edf::Put<void*>(crew::image,type.vtable+22*8,crew::image+crew::transport_pvp_detail::Setter(type.vtable));edf::Put<unsigned>(crew::image,col+12,descriptor);edf::Put<unsigned>(crew::image,col+20,col);
        std::memcpy(crew::image+descriptor+16,".?AV",4);const auto n=std::strlen(type.name);
        std::memcpy(crew::image+descriptor+20,type.name,n);std::memcpy(crew::image+descriptor+20+n,"@@",3);
    }
    Jump(crew::transport_pvp_detail::kCreate,reinterpret_cast<const void*>(&NativeCreate));Jump(crew::transport_pvp_detail::kCreateSpawner,reinterpret_cast<const void*>(&NativePending));
    Jump(crew::transport_pvp_detail::kCapsuleCrueCreate,reinterpret_cast<const void*>(&NativeCapsuleCrue));Jump(crew::kSetTeam,reinterpret_cast<const void*>(&NativeTeam));Jump(crew::transport_pvp_detail::kDelete,reinterpret_cast<const void*>(&NativeDelete));
#ifdef EDF_TRANSPORT_REAL_CORE
    TestOrdinaryCapsuleBridge();TestRealCore();TestRealCoreCapsule();TestRealCoreTimeClosure();TestRealCoreCreatorGone();TestRealCoreDetachedAnchor();TestRealCoreLateCapsules();TestRealCoreLateCapsuleReject();
#else
    TestOrdinaryCapsuleBridge();TestTypeAndInstallProof();TestTypedGeneratorAndMirrors();TestBirthBaselineAndInfiniteClosure();TestPreflightAndRollback();TestCapsuleBirthAndCleanup();TestRetainedBeamDelegation();TestTimeShipTypedParts();TestRealThunkArgumentAbi();
#endif
    crew::EnemyControlTransportPvpReset();VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("enemy_control_pvp_transport_test: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;
}
