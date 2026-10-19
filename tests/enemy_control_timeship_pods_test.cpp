// Execute production ownership, availability, admission, native outcome checks
// and accepted hatch codec against independent native factory/dispatch spies.
#define ENEMY_CONTROL_TIMESHIP_PODS_TEST
#include "../src/enemy_control_timeship_pods.cpp"
#include "../src/enemy_control_timeship_pods_script.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::timeship_pods;
constexpr NativeProfile profile{"EDF6_TimeShip",kRootVtable,0x3EE300,0x3EDE70,3,55,0xB,Kind::flying,15,4};
unsigned checks=0,failures=0,createCalls=0,dropCalls=0;bool online=false,hasRole=false,authority=false,active=true;
unsigned configuration=8;bool failCreate=false,failDrop=false,closing=false,uiCall=false,probeRestore=false;unsigned netCreates=0,netDrops=0,netSingles=0,rawSingles=0,rawCrues=0;int lastSeed=0;float lastPoint[4]{};
bool contextKnown=true,contextOnline=false;
NativeActionExtension extension{};
void Check(bool good,const char* text,int line){++checks;if(!good){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct Fixture {
    unsigned char* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* object=nullptr;unsigned char* control=nullptr;unsigned char* controller=nullptr;unsigned char* rows=nullptr;
    NativeLease lease{};
    Fixture(){
        if(!memory)std::exit(2);object=memory;control=memory+0x2000;controller=memory+0x3010;rows=memory+0x4000;
        Put<LONG>(control,8,1);Put<LONG>(control,12,1);Put<const void*>(object,0x30,control);
        Put<const void*>(object,0x1110,controller);Put<const void*>(object,0x1118,controller-16);
        Put<LONG>(controller-16,8,1);Put<LONG>(controller-16,12,1);Put<const void*>(controller,0,image+kControllerVtable);Put<const void*>(controller,0x20,object);
        Put<const void*>(object,0x1128,memory+0x3500);Put<const void*>(object,0x1140,rows);Put<std::size_t>(object,0x1148,kMaxPods);
        lease.object=object;lease.control=control;lease.profile=&profile;
    }
    void Add(unsigned count){
        const auto old=At<std::size_t>(object,0x1150);
        for(unsigned i=0;i<count;++i){
            const auto index=old+i;auto* ref=memory+0x5000+index*0x100;auto* link=ref+16;
            auto* body=memory+0xA000+index*0x100;auto* weak=body+0x80;
            Put<LONG>(ref,8,1);Put<const void*>(rows+index*16,0,link);Put<const void*>(rows+index*16,8,ref);
            Put<const void*>(link,0x18,object);Put<const void*>(link,0x48,body);Put<const void*>(link,0x50,weak);
            Put<LONG>(weak,8,1);Put<const void*>(body,0,image+kCapsuleVtable);Put<const void*>(body,0x30,weak);Put<const void*>(body,0x38,object);
        }
        Put<std::size_t>(object,0x1150,old+count);
    }
    ~Fixture(){CHECK(Abort(lease));CHECK(At<LONG>(control,12)==1);CHECK(At<LONG>(controller-16,12)==1);VirtualFree(memory,0,MEM_RELEASE);}
};
Fixture* current=nullptr;
void __fastcall CreateSpy(void* object,unsigned count){++createCalls;CHECK(object==current->object);CHECK(count==configuration);if(uiCall){CHECK(At<LONG>(current->control,8)==2);if(probeRestore)CHECK(!RestoreTimeShipPodControl(current->lease));}if(!failCreate)current->Add(count);}
void __fastcall DropSpy(void* object,const float* point,float inner,float outer,int seed){
    ++dropCalls;CHECK(object==current->object);if(uiCall){CHECK(At<LONG>(current->control,8)==2);if(probeRestore)CHECK(!RestoreTimeShipPodControl(current->lease));}CHECK(inner==50 && outer==100);lastSeed=seed;std::memcpy(lastPoint,point,sizeof(lastPoint));
    if(failDrop)return;
    const auto count=At<std::size_t>(current->object,0x1150);
    for(std::size_t i=0;i<count;++i){auto* link=At<unsigned char*>(current->rows+i*16,0);
        if(i==0){Put<void*>(link,0x48,nullptr);Put<void*>(link,0x50,nullptr);link[0x70]=1;}
        else {Put<int>(link,0x58,static_cast<int>(i*30));std::memcpy(link+0x60,point,16);}}
}
Handle* __fastcall ChildSpy(const Handle* from,Handle* to,const wchar_t* key){
    CHECK(from->tree!=nullptr);to->tree=from->tree;to->index=key[0]=='E' ? 1u : 2u;return to;
}
int __fastcall CountSpy(const Handle* from){CHECK(from->index==2);return static_cast<int>(configuration);}
void __fastcall SingleSpy(void*,unsigned index,int delay,const float* point){++rawSingles;CHECK(index==2 && delay==0 && point[3]==1);}
void* __fastcall CrueSpy(void* wrapper,unsigned,const void*,unsigned,float,bool){++rawCrues;return wrapper;}
NativeAbility Ability(int argument){return {"test",L"test",0,0,AbilityKind::timeShipCommand,nullptr,argument};}
bool Execute(Fixture& f,int command,std::uint64_t sequence){current=&f;uiCall=true;const bool result=ExecuteTimeShipPodAbility(f.lease,Ability(command),sequence,123);uiCall=false;return result;}
void Admission(){
    Fixture f;NativeAbility list[4]{};CHECK(ListTimeShipPodAbilities(f.lease,list,4)==4);CHECK(list[2].argument==10 && !list[2].pressed);
    NativeProfile forged=profile;auto l=f.lease;l.profile=&forged;CHECK(!ListTimeShipPodAbilities(l,list,4));
    admittedImage=nullptr;image[kSigs[0].rva]^=1;CHECK(!ListTimeShipPodAbilities(f.lease,list,4));CHECK(RestoreTimeShipPodScriptGates());image[kSigs[0].rva]^=1;
    CHECK(ListTimeShipPodAbilities(f.lease,nullptr,0)==4);
    configuration=0;enemy_control::Ability state{};CHECK(ReadTimeShipPodAbilityState(f.lease,Ability(10),&state));CHECK(state.state==enemy_control::AbilityState::unavailable);
    CHECK(!Execute(f,10,1));CHECK(createCalls==0);configuration=8;
}
void NativePods(){
    Fixture f;const unsigned initial=createCalls;probeRestore=true;
    CHECK(!Execute(f,11,1));CHECK(Execute(f,10,2));CHECK(createCalls==initial+1);CHECK(At<std::size_t>(f.object,0x1150)==8);
    CHECK(Execute(f,10,2));CHECK(createCalls==initial+1);CHECK(!Execute(f,10,1));CHECK(!Execute(f,10,3));
    // The actual accepted aim, homogeneous w and AS seed scale reach the native API.
    CHECK(Execute(f,11,4));CHECK(dropCalls==1);CHECK(lastPoint[0]==12 && lastPoint[1]==34 && lastPoint[2]==56 && lastPoint[3]==1);
    CHECK(lastSeed==25200);CHECK(Execute(f,11,4));CHECK(dropCalls==1);CHECK(!Execute(f,11,5));
    auto* link=At<unsigned char*>(f.rows+16,0);CHECK(At<int>(link,0x58)==30);
    CHECK(RestoreTimeShipPodControl(f.lease));CHECK(At<int>(link,0x58)==30);probeRestore=false; // release never rewinds accepted future drops
}
void ExistingPassengersAndFaults(){
    Fixture f;f.Add(2);const auto old=At<const void*>(At<unsigned char*>(f.rows,0),0x48);const unsigned initial=createCalls;
    CHECK(!Execute(f,10,1));CHECK(createCalls==initial);CHECK(At<const void*>(At<unsigned char*>(f.rows,0),0x48)==old);
    failDrop=true;CHECK(!Execute(f,11,2));CHECK(!Execute(f,11,2));failDrop=false;CHECK(!Execute(f,11,3));CHECK(RestoreTimeShipPodControl(f.lease));
    // A failed native creation is recorded before calling it; identical delivery cannot create twice.
    Fixture g;failCreate=true;CHECK(!Execute(g,10,1));const auto calls=createCalls;CHECK(!Execute(g,10,1));CHECK(createCalls==calls);failCreate=false;
    CHECK(!Execute(g,10,2));CHECK(RestoreTimeShipPodControl(g.lease));
}
void ExpiredAndBounds(){
    Fixture f;f.Add(1);auto* link=At<unsigned char*>(f.rows,0);auto* weak=At<unsigned char*>(link,0x50);Put<LONG>(weak,8,0);
    Roster roster{};CHECK(ReadRoster(f.lease,&roster) && !roster.attached);CHECK(Execute(f,10,1));
    Put<std::size_t>(f.object,0x1150,kMaxPods+1);CHECK(!ReadRoster(f.lease,&roster));CHECK(!Execute(f,11,2));Put<std::size_t>(f.object,0x1150,9);
}
void HatchAccepted(){
    Fixture host,guest;online=true;hasRole=true;authority=true;
    CHECK(!Execute(host,10,1));CHECK(!Execute(host,11,2));CHECK(Execute(host,7,3));CHECK(host.controller[0x123]==1);
    CHECK(Execute(host,7,3));CHECK(Find(host.lease)->count==1);Event event{};std::size_t used=0;
    CHECK(extension.Capture(host.lease,&event,sizeof(event),&used) && used==sizeof(event));CHECK(event.ordinal==1 && event.value==1);
    authority=false;CHECK(!Execute(guest,7,3));CHECK(guest.controller[0x123]==0);
    CHECK(extension.Apply(guest.lease,&event,sizeof(event))==enemy_control::ActionResult::applied);CHECK(guest.controller[0x123]==1);
    CHECK(extension.Apply(guest.lease,&event,sizeof(event))==enemy_control::ActionResult::applied);
    Event changed=event;changed.value=0;CHECK(extension.Apply(guest.lease,&changed,sizeof(changed))==enemy_control::ActionResult::failed);
    Event gap=event;gap.ordinal=3;CHECK(extension.Apply(guest.lease,&gap,sizeof(gap))==enemy_control::ActionResult::pending);
    changed=event;changed.reserved=2;CHECK(extension.Apply(guest.lease,&changed,sizeof(changed))==enemy_control::ActionResult::failed);
    CHECK(extension.Quiesce(guest.lease));changed=event;changed.ordinal=2;changed.input=4;changed.value=0;
    CHECK(extension.Apply(guest.lease,&changed,sizeof(changed))==enemy_control::ActionResult::applied); // drain accepted after close
    CHECK(RestoreTimeShipPodControl(guest.lease));CHECK(guest.controller[0x123]==0);
    authority=true;CHECK(Execute(host,8,4));CHECK(!RestoreTimeShipPodControl(host.lease));
    CHECK(extension.Quiesce(host.lease));CHECK(!Execute(host,7,5));CHECK(!extension.Empty(host.lease));
    CHECK(extension.Capture(host.lease,&event,sizeof(event),&used));CHECK(extension.Empty(host.lease));CHECK(RestoreTimeShipPodControl(host.lease));CHECK(host.controller[0x123]==0);
    online=false;authority=false;CHECK(!Execute(host,7,9));online=true;hasRole=false;contextOnline=true;CHECK(!Execute(host,7,10));CHECK(!Execute(host,10,11));CHECK(!Execute(host,11,12));online=false;contextOnline=false;
}
void ScriptAuthority(){
    Fixture f;current=&f;hasRole=false;online=false;authority=false;
    CHECK(Execute(f,7,1));timeship_pods_script::Hatch(f.object,true);CHECK(RestoreTimeShipPodControl(f.lease));CHECK(f.controller[0x123]==1);f.controller[0x123]=0;
    const auto raw=createCalls;timeship_pods_script::Create(f.object,8);CHECK(createCalls==raw+1);
    alignas(16) float point[4]={1,2,3,1};timeship_pods_script::DropOne(f.object,2,0,point);CHECK(rawSingles==1);
    online=false;hasRole=true;authority=false;const auto nativeCalls=createCalls;
    timeship_pods_script::Create(f.object,8);timeship_pods_script::Hatch(f.object,true);CHECK(createCalls==nativeCalls && f.controller[0x123]==0);
    authority=true;timeship_pods_script::Create(f.object,8);timeship_pods_script::Drop(f.object,point,1,2,3);timeship_pods_script::DropOne(f.object,2,0,point);
    CHECK(netCreates==1 && netDrops==1 && netSingles==1 && createCalls==nativeCalls);
    closing=true;timeship_pods_script::Create(f.object,8);timeship_pods_script::Hatch(f.object,true);CHECK(netCreates==1 && f.controller[0x123]==0);closing=false;
    timeship_pods_script::Hatch(f.object,true);CHECK(f.controller[0x123]==1);
    Event event{};std::size_t used=0;CHECK(extension.Capture(f.lease,&event,sizeof(event),&used) && used==sizeof(event));CHECK(event.reserved==1);
    CHECK(RestoreTimeShipPodControl(f.lease));CHECK(f.controller[0x123]==1); // permanent AS baseline survives exit
    Fixture g;authority=false;CHECK(extension.Apply(g.lease,&event,sizeof(event))==enemy_control::ActionResult::applied);
    CHECK(RestoreTimeShipPodControl(g.lease));CHECK(g.controller[0x123]==1);
    auto* wrapper=f.memory+0x3A00;Put<const void*>(wrapper,8,f.object);Put<const void*>(wrapper,16,f.control);
    timeship_pods_script::originalCrue=&CrueSpy;
    CHECK(timeship_pods_script::Crue(wrapper,0,nullptr,1,1,true)==wrapper && rawCrues==0);
    hasRole=false;online=true;contextOnline=true;timeship_pods_script::Create(f.object,8);CHECK(createCalls==nativeCalls);
    online=false;contextKnown=false;active=false;CHECK(timeship_pods_script::Crue(wrapper,0,nullptr,1,1,true)==wrapper && rawCrues==1);active=true;contextKnown=true;contextOnline=false;
    timeship_pods_script::originalCrue=nullptr;
}
void ExactCoreContext(){
    Fixture f;current=&f;hasRole=false;authority=false;online=false;contextKnown=false;contextOnline=false;active=true;
    const auto raw=createCalls;
    CHECK(!Execute(f,7,1));CHECK(!Execute(f,10,2));CHECK(!Execute(f,11,3));
    enemy_control::Ability state{};CHECK(ReadTimeShipPodAbilityState(f.lease,Ability(7),&state));CHECK(state.state==enemy_control::AbilityState::unavailable);
    timeship_pods_script::Create(f.object,8);timeship_pods_script::Hatch(f.object,true);CHECK(createCalls==raw && f.controller[0x123]==0);
    contextKnown=true;contextOnline=true; // known online survives the global probe being false
    CHECK(!Execute(f,7,4));CHECK(!Execute(f,10,5));timeship_pods_script::Create(f.object,8);CHECK(createCalls==raw);
    contextOnline=false;online=true; // known offline does not depend on the global probe
    CHECK(Execute(f,7,6));CHECK(f.controller[0x123]==1);CHECK(RestoreTimeShipPodControl(f.lease));CHECK(f.controller[0x123]==0);
    hasRole=true;authority=false;contextKnown=false;online=false;CHECK(!Execute(f,7,7));timeship_pods_script::Create(f.object,8);CHECK(createCalls==raw);
    authority=true;CHECK(Execute(f,7,8));CHECK(f.controller[0x123]==1);CHECK(!RestoreTimeShipPodControl(f.lease));
    Event event{};std::size_t used=0;CHECK(extension.Capture(f.lease,&event,sizeof(event),&used) && used==sizeof(event));CHECK(RestoreTimeShipPodControl(f.lease));
    hasRole=false;authority=false;active=false; // no retained core context: autonomous AS stays vanilla
    timeship_pods_script::Create(f.object,8);CHECK(createCalls==raw+1);
    active=true;contextKnown=true;contextOnline=false;
}
void ScriptInstallation(){
    using namespace timeship_pods_script;
    Fixture f;hasRole=false;online=false;authority=false;CHECK(RestoreTimeShipPodScriptGates());
    for(const auto& s:sites){image[s.rva]=0xE8;const auto relative=static_cast<std::int32_t>(s.target-s.rva-5);std::memcpy(image+s.rva+1,&relative,4);}
    std::memcpy(image+kHatch,kHatchOriginal,sizeof(kHatchOriginal));std::memcpy(image+kCrue,kCrueOriginal,sizeof(kCrueOriginal));
    image[kCrue]^=1;CHECK(!InstallTimeShipPodControl());CHECK(!installedImage);image[kCrue]^=1;
    CHECK(InstallTimeShipPodControl());CHECK(Complete());CHECK(InstallTimeShipPodControl());
    CHECK(image[kHatch]==0x49 && image[kHatch+1]==0xBB); // R11 jump must preserve native RAX root
    unsigned char entry[]={0x53,0x48,0x83,0xEC,0x30,0x8A,0xDA,0x48,0x8B,0xC1,0x49,0xBB,0,0,0,0,0,0,0,0,0x41,0xFF,0xE3};
    const auto target=reinterpret_cast<std::uintptr_t>(image+kHatch);std::memcpy(entry+12,&target,8);
    void* probe=AllocateNearCode(image+kHatch,entry,sizeof(entry));CHECK(probe!=nullptr);
    // A synthetic continuation uses the exact native wrapper's stack alignment,
    // shadow space and nonvolatile BL, executing the installed inline detour.
    constexpr unsigned char exit[]={0x48,0x83,0xC4,0x30,0x5B,0xC3};
    DWORD previous=0;CHECK(VirtualProtect(image+kHatch,0x1000,PAGE_EXECUTE_READWRITE,&previous)!=0);
    std::memcpy(image+kHatch+sizeof(kHatchOriginal),exit,sizeof(exit));FlushInstructionCache(GetCurrentProcess(),image+kHatch,32);
    using Probe=void(__fastcall*)(void*,bool);reinterpret_cast<Probe>(probe)(f.object,true);CHECK(f.controller[0x123]==1);
    reinterpret_cast<Probe>(probe)(f.object,false);CHECK(f.controller[0x123]==0);
    VirtualFree(probe,0,MEM_RELEASE);CHECK(RestoreTimeShipPodScriptGates());CHECK(!installedImage);
    CHECK(!std::memcmp(image+kCrue,kCrueOriginal,sizeof(kCrueOriginal)));CHECK(!std::memcmp(image+kHatch,kHatchOriginal,sizeof(kHatchOriginal)));
    for(const auto& s:sites){const auto relative=At<std::int32_t>(image+s.rva,1);CHECK(image+s.rva+5+relative==image+s.target);}
}
void QueueAndIdentity(){
    Fixture f;online=true;hasRole=true;authority=true;
    for(unsigned i=1;i<=kQueued;++i)CHECK(Execute(f,(i&1) ? 7 : 8,i));
    CHECK(!Execute(f,7,kQueued+1));CHECK(!RestoreTimeShipPodControl(f.lease));CHECK(Abort(f.lease));CHECK(f.controller[0x123]==0);
    online=false;hasRole=false;CHECK(Execute(f,7,100));auto* original=f.controller;Put<const void*>(f.object,0x1110,nullptr);CHECK(!RestoreTimeShipPodControl(f.lease));
    Put<const void*>(f.object,0x1110,original);CHECK(RestoreTimeShipPodControl(f.lease));
    auto lease=f.lease;lease.control=f.control+16;CHECK(!ExecuteTimeShipPodAbility(lease,Ability(10),101,0));
    active=false;CHECK(!Execute(f,10,102));active=true;
}
}
namespace crew {
bool InSession() noexcept{return online;}
bool EnemyControlNetworkContext(const void*,bool* out) noexcept{if(!contextKnown || !out)return false;*out=contextOnline;return true;}
bool EnemyControlTargetActive(const void*) noexcept{return active;}
bool EnemyControlWeaponAim(const void*,float* out) noexcept{out[0]=12;out[1]=34;out[2]=56;return true;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{if(count)*count=1;return &profile;}
bool NativeActionRole(const void*,bool* out) noexcept{if(out)*out=authority;return hasRole;}
bool NativeActionQuiescing(const void*) noexcept{return closing;}
bool RegisterTimeShipPodsNetwork() noexcept{return true;}
bool RestoreTimeShipPodsNetworkHooks() noexcept{return true;}
bool TimeShipPodsNetworkAvailable(const NativeLease&,bool) noexcept{return false;}
bool ExecuteTimeShipPodsNetwork(const NativeLease&,int,std::uint64_t) noexcept{return false;}
bool RestoreTimeShipPodsNetwork(const NativeLease&) noexcept{return true;}
bool ExecuteTimeShipPodsNetworkCreate(const NativeLease&,unsigned count,std::uint64_t sequence) noexcept{CHECK(count==8 && sequence>0);++netCreates;return true;}
bool ExecuteTimeShipPodsNetworkDrop(const NativeLease&,const float*,float inner,float outer,int seed,std::uint64_t sequence) noexcept{CHECK(inner==1 && outer==2 && seed==3 && sequence>0);++netDrops;return true;}
bool ExecuteTimeShipPodsNetworkDropOne(const NativeLease&,unsigned index,int delay,const float*,std::uint64_t sequence) noexcept{CHECK(index==2 && delay==0 && sequence>0);++netSingles;return true;}
bool RegisterNativeActionExtension(const NativeActionExtension& value) noexcept{extension=value;return true;}
void* TimeShipPodsTestFunction(unsigned rva) noexcept{
    if(rva==0x3EC680)return reinterpret_cast<void*>(&CreateSpy);if(rva==0x3ECC40)return reinterpret_cast<void*>(&DropSpy);
    if(rva==0x3ECC10)return reinterpret_cast<void*>(&SingleSpy);if(rva==0xD7480)return reinterpret_cast<void*>(&ChildSpy);if(rva==0x53060)return reinterpret_cast<void*>(&CountSpy);return nullptr;
}
}
int main(){
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!crew::image)return 2;
        for(const auto& s:kSigs)std::memcpy(crew::image+s.rva,s.bytes,sizeof(s.bytes));
    for(const auto& s:timeship_pods_script::sites){crew::image[s.rva]=0xE8;const auto relative=static_cast<std::int32_t>(s.target-s.rva-5);std::memcpy(crew::image+s.rva+1,&relative,4);}
    std::memcpy(crew::image+timeship_pods_script::kCrue,timeship_pods_script::kCrueOriginal,sizeof(timeship_pods_script::kCrueOriginal));
    Admission();NativePods();ExistingPassengersAndFaults();ExpiredAndBounds();HatchAccepted();QueueAndIdentity();ScriptAuthority();ExactCoreContext();ScriptInstallation();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("TimeShip native pods/hatch: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
