// Production accepted codec and transaction against separate host/guest worlds.
// The native spies derive IDs, append real-shaped links, register and detach;
// notably the guest DropAll RNG spy must never be called.
#define ENEMY_CONTROL_TIMESHIP_PODS_NET_TEST
#include "../src/enemy_control_timeship_pods_net.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::timeship_pods_net;
constexpr NativeProfile profile{"EDF6_TimeShip",kRootVtable,0x3EE300,0x3EDE70,3,55,0xB,Kind::flying,15,4};
unsigned checks=0,failures=0,creates=0,dropAlls=0,singles=0,activations=0,registrations=0,deletes=0;
unsigned rootDestructors=0,rootReleases=0;bool destroyRootOnCreate=false;
void __fastcall RootDestructor(void*){++rootDestructors;}
void __fastcall RootRelease(void*){++rootReleases;}
const void* rootControlTable[]={reinterpret_cast<const void*>(&RootDestructor),reinterpret_cast<const void*>(&RootRelease)};
bool online=true,hasRole=true,authority=true,active=true,hook=true,failResolve=false,wrongRegistration=false,raiseCreate=false;
unsigned configuration=8,partial=8,failRegisterAt=0,alreadyRegistered=0,deleteFailures=0,corruptRow=0;float observedInner=0,observedOuter=0;int observedSeed=0;
NativeActionExtension extension{};
bool pvpReady=true;unsigned pvpPrepare=0,pvpAdopt=0,pvpFinish=0,pvpFailAdopt=0;std::uint64_t fixturePvpToken=0;
void Check(bool good,const char* text,int line){++checks;if(!good){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct Fixture;
Fixture* current=nullptr;std::vector<Fixture*> fixtures;
struct Fixture {
    unsigned char* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* object=nullptr;unsigned char* control=nullptr;unsigned char* rows=nullptr;NativeLease lease{};bool rootDestroyed=false;
    Fixture(){
        if(!memory)std::exit(2);object=memory;control=memory+0x2000;rows=memory+0x4000;
        Put<const void*>(control,0,rootControlTable);Put<LONG>(control,8,1);Put<LONG>(control,12,1);Put<const void*>(object,0x30,control);
        Put<const void*>(object,0x1128,memory+0x3500);Put<const void*>(object,0x1140,rows);Put<std::size_t>(object,0x1148,kMaxPods);
        Put<unsigned>(object,0x128,2);Put<const void*>(object,0x130,memory+0x3000);Put<LONG>(memory+0x3000,0,1);
        Put<const void*>(memory+0x3000,8,memory+0x3100);Put<unsigned>(memory+0x3100,0,0xBBAA5511);
        lease.object=object;lease.control=control;lease.profile=&profile;fixtures.push_back(this);
    }
    void Select(bool host){current=this;authority=host;}
    void Add(unsigned count){
        const auto old=At<std::size_t>(object,0x1150);
        for(unsigned i=0;i<count;++i){
            const auto index=old+i;auto* ref=memory+0x5000+index*0x100;auto* link=ref+16;
            auto* body=memory+0x10000+index*0x300;auto* weak=body+0x200;
            Put<LONG>(ref,8,1);Put<const void*>(rows+index*16,0,link);Put<const void*>(rows+index*16,8,ref);
            Put<const void*>(link,0x18,object);Put<const void*>(link,0x48,body);Put<const void*>(link,0x50,weak);
            Put<LONG>(weak,8,1);Put<LONG>(weak,12,1);Put<const void*>(body,0,image+kCapsuleVtable);Put<const void*>(body,0x30,weak);Put<const void*>(body,0x38,object);
        }
        Put<std::size_t>(object,0x1150,old+count);
    }
    unsigned char* Link(unsigned i)const{return At<unsigned char*>(rows+i*16,0);}
    unsigned char* Body(unsigned i)const{return At<unsigned char*>(Link(i),0x48);}
    ~Fixture(){CHECK(Abort(lease));CHECK(At<LONG>(control,12)==(rootDestroyed ? 0 : 1));for(auto& f:fixtures)if(f==this)f=nullptr;VirtualFree(memory,0,MEM_RELEASE);}
};
Fixture* Of(const void* object){for(auto* f:fixtures)if(f && object==f->object)return f;return nullptr;}
void __fastcall CreateSpy(void* object,unsigned requested){
    ++creates;CHECK(current && object==current->object);CHECK(At<LONG>(current->control,8)>0 && Find(current->lease) && Find(current->lease)->strong);CHECK(!RestoreTimeShipPodsNetwork(current->lease));CHECK(!RestoreTimeShipPodsNetworkHooks());const auto count=requested<configuration ? requested : configuration;
    current->Add(count<partial ? count : partial);if(alreadyRegistered && alreadyRegistered<=count)Put<unsigned>(current->Body(alreadyRegistered-1),0x128,1);if(corruptRow && corruptRow<=count)Put<void*>(current->rows+(corruptRow-1)*16,8,nullptr);if(destroyRootOnCreate){current->object[0x18]|=4;InterlockedDecrement(reinterpret_cast<volatile LONG*>(current->control+8));current->rootDestroyed=true;}if(raiseCreate)RaiseException(0xE0427411,0,0,nullptr);
}
void __fastcall ReleaseSpy(void* raw,const float* point){
    auto* link=static_cast<unsigned char*>(raw);CHECK(Point(point));link[0x70]=1;
    Put<void*>(link,0x48,nullptr);Put<void*>(link,0x50,nullptr);
}
void __fastcall DropSpy(void* object,const float* point,float inner,float outer,int seed){
    ++dropAlls;CHECK(current && current->object==object);CHECK(At<LONG>(current->control,8)>0 && Find(current->lease) && Find(current->lease)->strong);CHECK(!RestoreTimeShipPodsNetwork(current->lease));observedInner=inner;observedOuter=outer;observedSeed=seed;
    const auto count=At<std::size_t>(object,0x1150);
    for(unsigned i=0;i<count;++i){float target[4]{point[0]+static_cast<float>(i)*13+7,point[1]-3,point[2]+static_cast<float>(i)*17+11,1};
        auto* link=current->Link(i);if(!i)ReleaseObserved(link,target);
        else {Put<int>(link,0x58,static_cast<int>(i*30));std::memcpy(link+0x60,target,16);}}
}
void __fastcall SingleSpy(void* object,unsigned index,int delay,const float* point){
    ++singles;CHECK(current && current->object==object);CHECK(At<LONG>(current->control,8)>0 && Find(current->lease) && Find(current->lease)->strong);CHECK(!RestoreTimeShipPodsNetwork(current->lease));CHECK(index<At<std::size_t>(object,0x1150));
    auto* link=current->Link(index);if(delay<=0)ReleaseSpy(link,point);else {Put<int>(link,0x58,delay);std::memcpy(link+0x60,point,16);}
}
void __fastcall ActivateSpy(void* link){++activations;CHECK(At<void*>(link,0x48)!=nullptr);}
void __fastcall DeleteSpy(void* body){++deletes;if(deleteFailures){--deleteFailures;return;}auto* weak=At<unsigned char*>(body,0x30);Put<LONG>(weak,8,0);}
void* __fastcall DeriveSpy(void* raw,const void* entry,unsigned ordinal){
    auto* id=static_cast<unsigned char*>(raw);Put<unsigned>(id,0,At<unsigned>(entry,0));Put<unsigned>(id,4,ordinal);
    Put<unsigned>(id,8,0x11223344);Put<unsigned>(id,12,5);Put<unsigned>(id,24,0xABCD);return raw;
}
const unsigned char* __fastcall RegisterSpy(void* manager,ObjRef* consumed,const unsigned char* id){
    CHECK(manager==image+0x2100000);++registrations;auto* body=static_cast<unsigned char*>(const_cast<void*>(consumed->obj));
    auto* weak=static_cast<unsigned char*>(const_cast<void*>(consumed->ctrl));InterlockedDecrement(reinterpret_cast<volatile LONG*>(weak+12));*consumed={};
    if(failRegisterAt==registrations)return nullptr;
    CHECK(!(At<unsigned>(body,0x128)&3));Put<unsigned>(body,0x128,authority ? 2u : 1u);Put<void*>(body,0x130,body+0x220);std::memcpy(body+0x180,id,32);
    if(wrongRegistration)body[0x180]^=1;return body+0x180;
}
Handle* __fastcall ChildSpy(const Handle* from,Handle* to,const wchar_t* key){to->tree=from->tree;to->index=key[0]=='E' ? 1u : 2u;return to;}
int __fastcall CountSpy(const Handle* from){CHECK(from->index==2);return static_cast<int>(configuration);}
std::vector<unsigned char> Take(Fixture& f){f.Select(true);std::vector<unsigned char> packet(512);std::size_t used=0;CHECK(extension.Capture(f.lease,packet.data(),packet.size(),&used));packet.resize(used);return packet;}
enemy_control::ActionResult Accept(Fixture& f,const std::vector<unsigned char>& packet){f.Select(false);return extension.Apply(f.lease,packet.data(),packet.size());}
void Admission(){
    Fixture f;f.Select(true);CHECK(RegisterTimeShipPodsNetwork());CHECK(extension.tag==kTag);
    CHECK(TimeShipPodsNetworkAvailable(f.lease,true));CHECK(!TimeShipPodsNetworkAvailable(f.lease,false));
    NativeProfile forged=profile;auto l=f.lease;l.profile=&forged;CHECK(!TimeShipPodsNetworkAvailable(l,true));
    CHECK(!ExecuteTimeShipPodsNetwork(f.lease,7,1));CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,8,0));
    hasRole=false;CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,8,1));hasRole=true;
    f.Select(false);CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,8,1));f.Select(true);
    online=false;Input input{};CHECK(extension.Before(f.lease,true,&input));f.Select(false);CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,8,1));f.Select(true);online=true;
    active=false;CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,8,1));active=true;
    CHECK(ExecuteTimeShipPodsNetworkCreate(f.lease,0,1));CHECK(Take(f).empty());
}
void NativeIdsAndActualThrows(){
    Fixture host,guest;host.Select(true);const auto initialCreates=creates,initialDrops=dropAlls;
    CHECK(ExecuteTimeShipPodsNetwork(host.lease,10,1));CHECK(creates==initialCreates+1);
    CHECK(ExecuteTimeShipPodsNetwork(host.lease,10,1));CHECK(creates==initialCreates+1);
    const auto birth=Take(host);CHECK(birth.size()==sizeof(Event));CHECK(extension.Owns(birth.data(),birth.size()));
    online=false;CHECK(Accept(guest,birth)==enemy_control::ActionResult::applied);online=true;CHECK(creates==initialCreates+2);
    CHECK(Accept(guest,birth)==enemy_control::ActionResult::applied);CHECK(creates==initialCreates+2);
    for(unsigned i=0;i<8;++i){CHECK(!std::memcmp(host.Body(i)+0x180,guest.Body(i)+0x180,32));CHECK((At<unsigned>(host.Body(i),0x128)&3)==2);CHECK((At<unsigned>(guest.Body(i),0x128)&3)==1);}
    host.Select(true);CHECK(ExecuteTimeShipPodsNetwork(host.lease,11,3));CHECK(dropAlls==initialDrops+1);CHECK(observedSeed==25200);
    const auto throwing=Take(host);const auto* event=reinterpret_cast<const Event*>(throwing.data());CHECK(event->kind==2 && event->sourceSeed==25200);
    CHECK(event->rows[0].point[0]==19 && event->rows[0].point[1]==31 && event->rows[0].point[2]==67); // actual ray result, not requested aim
    const auto initialSingles=singles;CHECK(Accept(guest,throwing)==enemy_control::ActionResult::applied);CHECK(dropAlls==initialDrops+1);CHECK(singles==initialSingles+8);
    CHECK(!guest.Body(0));for(unsigned i=1;i<8;++i){CHECK(!std::memcmp(host.Link(i)+0x60,guest.Link(i)+0x60,16));CHECK(At<int>(guest.Link(i),0x58)==static_cast<int>(i*30));}
    CHECK(Accept(guest,throwing)==enemy_control::ActionResult::applied);CHECK(singles==initialSingles+8);
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(RestoreTimeShipPodsNetwork(guest.lease));CHECK(At<int>(guest.Link(7),0x58)==210);
}
void ExactScriptParametersAndRepeatedBirths(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,100));auto packet=Take(host);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(At<std::size_t>(guest.object,0x1150)==4);
    alignas(16) const float aim[4]{-10,99,-50,1};host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,aim,12.5f,147.0f,-42,101));packet=Take(host);
    CHECK(observedInner==12.5f && observedOuter==147.0f && observedSeed==-42);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    // Simulate native delayed completion, retaining empty root links just as native does.
    for(unsigned i=1;i<4;++i){ReleaseSpy(host.Link(i),reinterpret_cast<const float*>(host.Link(i)+0x60));ReleaseSpy(guest.Link(i),reinterpret_cast<const float*>(guest.Link(i)+0x60));Put<int>(host.Link(i),0x58,0);Put<int>(guest.Link(i),0x58,0);}
    host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,99,102));packet=Take(host);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    CHECK(At<std::size_t>(guest.object,0x1150)==12);host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,aim,0,10,0,103));packet=Take(host);
    const auto* event=reinterpret_cast<const Event*>(packet.data());CHECK(event->count==8 && event->rows[0].index==4 && event->rows[0].delay==120);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(At<int>(guest.Link(4),0x58)==120);
}
void MutationFreeRejection(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto packet=Take(host);auto changed=packet;
    auto* event=reinterpret_cast<Event*>(changed.data());event->ordinal=2;const auto initial=creates;CHECK(Accept(guest,changed)==enemy_control::ActionResult::pending);CHECK(creates==initial);
    event->ordinal=1;event->reserved=1;CHECK(Accept(guest,changed)==enemy_control::ActionResult::failed);CHECK(creates==initial);
    changed=packet;event=reinterpret_cast<Event*>(changed.data());event->rows[1].id[8]^=1;CHECK(Accept(guest,changed)==enemy_control::ActionResult::failed);CHECK(creates==initial);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::failed); // native preflight failed: lease is terminal, never replayed
    CHECK(creates==initial);
}
void NativeFailureRollbackAndNoRetry(){
    Fixture host;host.Select(true);partial=3;const auto oldDeletes=deletes,oldCreates=creates;CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));CHECK(creates==oldCreates+1);CHECK(deletes==oldDeletes+3);
    CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,2));CHECK(creates==oldCreates+1);CHECK(Take(host).empty());partial=8;
    CHECK(RestoreTimeShipPodsNetwork(host.lease));host.Select(true);raiseCreate=true;const auto before=deletes;CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,3));CHECK(deletes==before+8);CHECK(!dropping);raiseCreate=false;
    Fixture second;second.Select(true);failRegisterAt=registrations+3;const auto start=deletes;CHECK(!ExecuteTimeShipPodsNetworkCreate(second.lease,8,10));CHECK(deletes==start+8);failRegisterAt=0;
}
void FullBirthPreflightAndRetainedRollback(){
    Fixture host;host.Select(true);alreadyRegistered=3;const auto initial=registrations;CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));CHECK(registrations==initial);alreadyRegistered=0;
    CHECK(RestoreTimeShipPodsNetwork(host.lease));host.Select(true);partial=3;deleteFailures=1;CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,2));partial=8;
    CHECK(Find(host.lease)->rollbackFrom==8);deleteFailures=1;CHECK(!RestoreTimeShipPodsNetwork(host.lease));CHECK(Find(host.lease)!=nullptr);
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(!Find(host.lease));
}
void CorruptTailDoesNotLeakProvenPrefix(){
    Fixture host;host.Select(true);corruptRow=3;const auto initial=deletes;CHECK(!ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));CHECK(deletes==initial+7);
    CHECK(Find(host.lease)->rollbackFrom==0);CHECK(!RestoreTimeShipPodsNetwork(host.lease));CHECK(deletes==initial+7);
    Put<void*>(host.rows+2*16,8,host.memory+0x5000+2*0x100);corruptRow=0;
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(deletes==initial+8);
}
void NativeCallbackCannotDestroyPinnedRootEarly(){
    Fixture host;host.Select(true);destroyRootOnCreate=true;const auto oldDeletes=deletes,oldDtors=rootDestructors,oldReleases=rootReleases;
    CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));destroyRootOnCreate=false;
    CHECK(deletes==oldDeletes);CHECK(At<LONG>(host.control,8)==1);CHECK(rootDestructors==oldDtors);CHECK(rootReleases==oldReleases);
    CHECK(!Take(host).empty());CHECK(extension.Quiesce(host.lease));CHECK(RestoreTimeShipPodsNetwork(host.lease));
    CHECK(rootDestructors==oldDtors+1);CHECK(rootReleases==oldReleases+1);
}
void NativeSingleDrop(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto birth=Take(host);CHECK(Accept(guest,birth)==enemy_control::ActionResult::applied);
    alignas(16) const float target[4]{19,-3,42,1};host.Select(true);const auto initial=activations;
    CHECK(!ExecuteTimeShipPodsNetworkDropOne(host.lease,4,0,target,2));CHECK(!ExecuteTimeShipPodsNetworkDropOne(host.lease,0,-1,target,3));
    CHECK(ExecuteTimeShipPodsNetworkDropOne(host.lease,2,0,target,4));auto packet=Take(host);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    CHECK(!guest.Body(2) && guest.Body(0));CHECK(activations==initial); // AS DropOne never does DropAll's separate Activate
    host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDropOne(host.lease,1,127,target,5));packet=Take(host);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    CHECK(At<int>(guest.Link(1),0x58)==127);CHECK(!std::memcmp(guest.Link(1)+0x60,target,16));CHECK(activations==initial);
}
void AcceptedExitAndBackpressure(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,8,1));CHECK(!RestoreTimeShipPodsNetwork(host.lease));CHECK(extension.Quiesce(host.lease));
    CHECK(!ExecuteTimeShipPodsNetwork(host.lease,11,2));auto packet=Take(host);CHECK(extension.Empty(host.lease));CHECK(RestoreTimeShipPodsNetwork(host.lease));
    guest.Select(false);CHECK(extension.Quiesce(guest.lease));CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    CHECK(RestoreTimeShipPodsNetwork(guest.lease));CHECK(guest.Body(7)!=nullptr); // exit preserves accepted gameplay children
    // Counter survives release and re-entry, so an old detached child ID cannot be reused.
    Fixture repeat;repeat.Select(true);const auto counter=nextOrdinal;CHECK(ExecuteTimeShipPodsNetworkCreate(repeat.lease,1,3));CHECK(nextOrdinal==counter+1);
    CHECK(Abort(repeat.lease));CHECK(nextOrdinal==counter+1);
}
void DropAllAfterSingleReleasedFirst(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto packet=Take(host);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    const float target[4]{8,16,32,1};host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDropOne(host.lease,0,0,target,2));packet=Take(host);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,target,10,20,3,3));packet=Take(host);const auto* event=reinterpret_cast<const Event*>(packet.data());
    CHECK(event->count==3 && event->rows[0].index==1 && event->rows[0].delay==30);CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    CHECK(At<int>(guest.Link(1),0x58)==30);CHECK(!Find(host.lease)->faulted);
}
void BoundAuthoritySurvivesStatusProbe(){
    Fixture host,guest;host.Select(true);online=false;const auto initial=creates;
    CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,2,1));auto packet=Take(host);CHECK(creates==initial+1);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(creates==initial+2);
    guest.Select(false);CHECK(!ExecuteTimeShipPodsNetworkCreate(guest.lease,2,2));
    host.Select(true);const float aim[4]{4,8,16,1};CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,aim,10,20,5,3));packet=Take(host);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(At<int>(guest.Link(1),0x58)==30);online=true;
}
void QueueAndBinaryAdmission(){
    Fixture f;f.Select(true);const float target[4]{1,2,3,1};
    for(unsigned i=0;i<8;++i){CHECK(ExecuteTimeShipPodsNetworkCreate(f.lease,1,i*2+1));CHECK(ExecuteTimeShipPodsNetworkDropOne(f.lease,i,0,target,i*2+2));}
    CHECK(Find(f.lease)->count==kQueued);const auto initial=creates;CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,1,100));CHECK(creates==initial);
    char tiny[4]{};std::size_t used=77;CHECK(!extension.Capture(f.lease,tiny,sizeof(tiny),&used));CHECK(!used && Find(f.lease)->count==kQueued);
    CHECK(!RestoreTimeShipPodsNetwork(f.lease));CHECK(extension.Quiesce(f.lease));CHECK(!extension.Empty(f.lease));
    while(Find(f.lease)->count){auto packet=Take(f);CHECK(!packet.empty());}CHECK(extension.Empty(f.lease));CHECK(RestoreTimeShipPodsNetwork(f.lease));
    admittedImage=nullptr;image[kSigs[0].rva]^=1;CHECK(!RegisterTimeShipPodsNetwork());image[kSigs[0].rva]^=1;CHECK(RegisterTimeShipPodsNetwork());
    hasRole=false;online=false;CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,1,200));online=true;hasRole=true;
    Put<void*>(image,0x20B2AC8,nullptr);CHECK(!ExecuteTimeShipPodsNetworkCreate(f.lease,1,201));CHECK(creates==initial);Put<void*>(image,0x20B2AC8,image+0x2100000);
}
void LogicalDeath(Fixture& f){f.object[0x18]|=4;f.object[0x2E8]=1;InterlockedDecrement(reinterpret_cast<volatile LONG*>(f.control+8));f.rootDestroyed=true;}
void LateAcceptedBirthAndDropUseBoundRetainedRoot(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,1,1));auto birth=Take(host);
    const float aim[4]{5,10,15,1};host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDropOne(host.lease,0,0,aim,2));auto drop=Take(host);
    guest.Select(false);Input input{};CHECK(extension.Before(guest.lease,false,&input));LogicalDeath(guest);
    CHECK(Accept(guest,birth)==enemy_control::ActionResult::applied);CHECK(guest.Body(0) && MatchesId(guest.Body(0),reinterpret_cast<const Event*>(birth.data())->rows[0].id));
    CHECK(Accept(guest,drop)==enemy_control::ActionResult::applied && !guest.Body(0));
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(RestoreTimeShipPodsNetwork(guest.lease));
    Fixture neverBound;neverBound.Select(false);LogicalDeath(neverBound);
    CHECK(Accept(neverBound,birth)==enemy_control::ActionResult::failed); // authenticated payload alone cannot revive an unbound corpse
    CHECK(!Find(neverBound.lease));
    // There was no lease strong pin in this negative fixture: balance its simulated world teardown.
    Put<LONG>(neverBound.control,12,0);
}
void LateAcceptedDropSurvivesRootDeath(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto packet=Take(host);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    const float aim[4]{8,16,32,1};host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDropOne(host.lease,2,0,aim,2));packet=Take(host);
    const auto initialSingles=singles;LogicalDeath(host);LogicalDeath(guest);
    CHECK(At<LONG>(host.control,8)==1 && At<LONG>(guest.control,8)==1);
    host.Select(true);CHECK(extension.Quiesce(host.lease));auto settled=Take(host);CHECK(!settled.empty());
    guest.Select(false);CHECK(extension.Quiesce(guest.lease));
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(!guest.Body(2));
    CHECK(Accept(guest,settled)==enemy_control::ActionResult::applied);CHECK(singles==initialSingles+1);
    CHECK(extension.Empty(host.lease));CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(RestoreTimeShipPodsNetwork(guest.lease));
}
void AlreadyReleasedDelayedCapsuleIsReconciledBeforeDeath(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto packet=Take(host);
    CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    const float aim[4]{3,6,9,1};host.Select(true);CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,aim,10,20,3,2));packet=Take(host);
    // Native slot5 reached this host delay before death and before the guest received DropAll.
    ReleaseSpy(host.Link(1),reinterpret_cast<const float*>(host.Link(1)+0x60));Put<int>(host.Link(1),0x58,0);
    LogicalDeath(host);LogicalDeath(guest);host.Select(true);CHECK(extension.Quiesce(host.lease));auto settled=Take(host);
    CHECK(reinterpret_cast<const Event*>(settled.data())->kind==4 && reinterpret_cast<const Event*>(settled.data())->reserved==1);
    guest.Select(false);CHECK(extension.Quiesce(guest.lease));CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);
    auto* canceled2=guest.Body(2);auto* canceled3=guest.Body(3);const auto initialSingles=singles,initialDeletes=deletes;
    CHECK(Accept(guest,settled)==enemy_control::ActionResult::applied);
    CHECK(!guest.Body(0) && !guest.Body(1));CHECK(singles==initialSingles+1 && deletes==initialDeletes+2);
    CHECK(At<LONG>(At<const void*>(canceled2,0x30),8)==0 && At<LONG>(At<const void*>(canceled3,0x30),8)==0);
    CHECK(Accept(guest,settled)==enemy_control::ActionResult::applied && singles==initialSingles+1 && deletes==initialDeletes+2);
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(RestoreTimeShipPodsNetwork(guest.lease));
}
void PinnedCleanupRetainsReadonlyCounters(){
    Fixture f;f.Select(true);Input input{};CHECK(extension.Before(f.lease,true,&input));
    const auto strong=At<LONG>(f.control,8),weak=At<LONG>(f.control,12);DWORD old=0;
    VirtualProtect(f.control,0x1000,PAGE_READONLY,&old);
    CHECK(!RestoreTimeShipPodsNetwork(f.lease));CHECK(Find(f.lease) && Find(f.lease)->strong && At<LONG>(f.control,8)==strong && At<LONG>(f.control,12)==weak);
    VirtualProtect(f.control,0x1000,old,&old);const auto tick=GetTickCount64();while(GetTickCount64()==tick)Sleep(1);
    CHECK(RestoreTimeShipPodsNetwork(f.lease));CHECK(At<LONG>(f.control,8)==1 && At<LONG>(f.control,12)==1);
}
void PvpPreflightAndBirthTransaction(){
    Fixture host,guest;host.Select(true);fixturePvpToken=17;const auto beforePrepare=pvpPrepare,beforeAdopt=pvpAdopt,beforeFinish=pvpFinish;
    CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,2,1));auto packet=Take(host);
    CHECK(pvpPrepare==beforePrepare+1 && pvpAdopt==beforeAdopt+2 && pvpFinish==beforeFinish+1);
    guest.Select(false);Input neutral{};CHECK(extension.Before(guest.lease,false,&neutral));const auto beforeCreates=creates;const auto guestStrong=At<LONG>(guest.control,8);
    pvpReady=false;CHECK(Accept(guest,packet)==enemy_control::ActionResult::pending);CHECK(creates==beforeCreates && At<LONG>(guest.control,8)==guestStrong && At<std::size_t>(guest.object,0x1150)==0);
    pvpReady=true;CHECK(Accept(guest,packet)==enemy_control::ActionResult::applied);CHECK(creates==beforeCreates+1);
    CHECK(RestoreTimeShipPodsNetwork(host.lease));CHECK(RestoreTimeShipPodsNetwork(guest.lease));
    Fixture rejected;rejected.Select(true);const auto beforeRegistration=registrations,beforeDelete=deletes;const auto finishBeforeFailure=pvpFinish;
    pvpFailAdopt=pvpAdopt+2;CHECK(!ExecuteTimeShipPodsNetworkCreate(rejected.lease,4,2));pvpFailAdopt=0;
    CHECK(registrations==beforeRegistration && deletes==beforeDelete+4 && pvpFinish==finishBeforeFailure+1);
    CHECK(RestoreTimeShipPodsNetwork(rejected.lease));fixturePvpToken=0;
}
void GuestIdentityAndAimGates(){
    Fixture host,guest;host.Select(true);CHECK(ExecuteTimeShipPodsNetworkCreate(host.lease,4,1));auto birth=Take(host);CHECK(Accept(guest,birth)==enemy_control::ActionResult::applied);
    host.Select(true);alignas(16) float aim[4]{1,2,3,0};const auto old=dropAlls;
    CHECK(!ExecuteTimeShipPodsNetworkDrop(host.lease,aim,1,2,3,2));aim[3]=1;aim[0]=std::numeric_limits<float>::infinity();CHECK(!ExecuteTimeShipPodsNetworkDrop(host.lease,aim,1,2,3,3));aim[0]=1;
    CHECK(!ExecuteTimeShipPodsNetworkDrop(host.lease,aim,3,2,3,4));CHECK(dropAlls==old);
    CHECK(ExecuteTimeShipPodsNetworkDrop(host.lease,aim,1,2,3,5));auto thrown=Take(host);guest.Body(2)[0x180]^=1;
    const auto initial=singles;CHECK(Accept(guest,thrown)==enemy_control::ActionResult::failed);CHECK(singles==initial);
    guest.Body(2)[0x180]^=1;CHECK(Accept(guest,thrown)==enemy_control::ActionResult::failed);CHECK(singles==initial);
}
}
namespace crew {
bool EnemyControlTransportPvpCanPrepareCapsules(const ObjRef&,unsigned count) noexcept {return pvpReady && count>0 && count<=8;}
bool EnemyControlTransportPvpPrepareCapsules(const ObjRef& parent,unsigned count,std::uint64_t* token) noexcept {
    ++pvpPrepare;*token=0;if(!EnemyControlTransportPvpCanPrepareCapsules(parent,count))return false;*token=fixturePvpToken;return true;
}
bool EnemyControlTransportPvpAdoptCapsule(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {
    ++pvpAdopt;CHECK(token==fixturePvpToken && parent.obj==current->object && child);
    CHECK(!(At<unsigned>(child.obj,0x128)&3u)); // Every body must be tagged before native registration.
    return pvpAdopt!=pvpFailAdopt;
}
void EnemyControlTransportPvpFinishCapsules(std::uint64_t token) noexcept {CHECK(token==fixturePvpToken || token==0);++pvpFinish;}
bool InSession() noexcept{return online;}
bool EnemyControlTargetActive(const void*) noexcept{return active;}
bool EnemyControlWeaponAim(const void*,float* out) noexcept{out[0]=12;out[1]=34;out[2]=56;return true;}
bool ReadNativeObjectId(const unsigned char* body,unsigned char* id) noexcept {
    if(!body || !(At<unsigned>(body,0x128)&3))return false;std::memcpy(id,body+0x180,32);return true;
}
bool ResolveMarkIdentities(const unsigned char*,const unsigned char* id,MarkIdentities* out) noexcept {
    *out={};if(failResolve || !current)return false;
    const auto total=At<std::size_t>(current->object,0x1150);
    for(unsigned i=0;i<total;++i){auto* body=current->Body(i);unsigned char actual[32]{};if(body && ReadNativeObjectId(body,actual) && !std::memcmp(id,actual,32))out->target=ObjRef::Of(body);}
    return true;
}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{if(count)*count=1;return &profile;}
bool NativeActionRole(const void*,bool* out) noexcept{if(out)*out=authority;return hasRole;}
bool NativeActionQuiescing(const void*) noexcept{return false;}
bool RegisterNativeActionExtension(const NativeActionExtension& value) noexcept{extension=value;return true;}
bool TimeShipPodsNetTestHook() noexcept{return hook;}
void* TimeShipPodsNetTestFunction(unsigned rva) noexcept{
    if(rva==0x3EC680)return reinterpret_cast<void*>(&CreateSpy);if(rva==0x3ECC40)return reinterpret_cast<void*>(&DropSpy);
    if(rva==0x3ECC10)return reinterpret_cast<void*>(&SingleSpy);if(rva==0x3ED9C0)return reinterpret_cast<void*>(&ActivateSpy);
    if(rva==kRelease)return reinterpret_cast<void*>(&ReleaseSpy);if(rva==0x118A1B0)return reinterpret_cast<void*>(&DeleteSpy);
    if(rva==0x781950)return reinterpret_cast<void*>(&RegisterSpy);if(rva==0x776790)return reinterpret_cast<void*>(&DeriveSpy);if(rva==0xD7480)return reinterpret_cast<void*>(&ChildSpy);if(rva==0x53060)return reinterpret_cast<void*>(&CountSpy);return nullptr;
}
}
int main(){
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!crew::image)return 2;
    for(const auto& s:kSigs)std::memcpy(crew::image+s.rva,s.bytes,sizeof(s.bytes));Put<void*>(crew::image,0x20B2AC8,crew::image+0x2100000);
    Admission();NativeIdsAndActualThrows();ExactScriptParametersAndRepeatedBirths();MutationFreeRejection();NativeFailureRollbackAndNoRetry();FullBirthPreflightAndRetainedRollback();CorruptTailDoesNotLeakProvenPrefix();NativeCallbackCannotDestroyPinnedRootEarly();NativeSingleDrop();AcceptedExitAndBackpressure();DropAllAfterSingleReleasedFirst();BoundAuthoritySurvivesStatusProbe();QueueAndBinaryAdmission();GuestIdentityAndAimGates();LateAcceptedBirthAndDropUseBoundRetainedRoot();LateAcceptedDropSurvivesRootDeath();AlreadyReleasedDelayedCapsuleIsReconciledBeforeDeath();PinnedCleanupRetainsReadonlyCounters();PvpPreflightAndBirthTransaction();
    hookPatch.site=image+0x1000;const unsigned char original[5]{0xE8,1,2,3,4},ours[5]{0xE8,5,6,7,8};
    std::memcpy(hookPatch.original,original,5);std::memcpy(hookPatch.patched,ours,5);std::memcpy(hookPatch.site,ours,5);hookPatch.owned=true;
    CHECK(RestoreTimeShipPodsNetworkHooks());CHECK(!std::memcmp(image+0x1000,original,5));CHECK(RegisterTimeShipPodsNetwork());
    hookPatch.site=image+0x1000;std::memcpy(hookPatch.original,original,5);std::memcpy(hookPatch.patched,ours,5);std::memset(hookPatch.site,0xCC,5);hookPatch.owned=true;
    CHECK(!RestoreTimeShipPodsNetworkHooks());CHECK(image[0x1000]==0xCC);std::memcpy(hookPatch.site,original,5);
    CHECK(RestoreTimeShipPodsNetworkHooks());CHECK(!hookInstalled && admittedImage==nullptr);CHECK(RegisterTimeShipPodsNetwork());
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("TimeShip accepted capsule transactions: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
