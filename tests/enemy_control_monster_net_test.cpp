#define ENEMY_CONTROL_MONSTER_NET_TEST
#include "../src/enemy_control_monster_net.cpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::monster_net;
constexpr NativeProfile profiles[]={
    {"Monster501",0x17C5628,0,0,0x501,64,0,Kind::monster,3,4},
    {"Monster504",0x17C5A60,0,0,0x504,64,0,Kind::monster,3,4},
};
unsigned checks=0,failures=0,creates=0,captures=0,prepared=0,copies=0,destructs=0,models=0,allocations=0,frees=0;
bool rejectCreate=false,rejectPreflight=false,rejectCapture=false,throwInit=false,throwDestruct=false,helperCleanupFault=false,helperCleanupBlocked=false,traceDumpActive=false;unsigned failAllocation=0;
NativeUnitFactoryProvider provider{};NativeActionExtension extension{};unsigned callsites=0;float actualX=0;std::uint32_t actualSeed=0,actualPattern=0,actualB0=0,actualC0=0,actualE0=0;void* actualModel=nullptr;const void* actualCustom=nullptr;
const NativeLease* restoreDuringFactory=nullptr;bool reentrantEmpty=true,reentrantRestore=true;
const NativeLease* rootDtorReenterLease=nullptr;bool throwRootStrong=false,throwRootWeak=false,rootReentryBlocked=true,rootOwnershipConsumed=true;
unsigned rootStrongDtors=0,rootWeakDtors=0,rootReentries=0;
void Check(bool value,const char* text,int line){++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
void ReenterRootCleanup() {
    if(!rootDtorReenterLease)return;++rootReentries;const auto* s=Find(*rootDtorReenterLease);
    rootOwnershipConsumed=rootOwnershipConsumed && s && s->held && !s->rootHeld && s->cleanupEntered && s->uncertain;
    rootReentryBlocked=rootReentryBlocked && !Restore(*rootDtorReenterLease) && !Abort(*rootDtorReenterLease) && !Empty(*rootDtorReenterLease);
}
void __fastcall ReleaseStrong(void*){++rootStrongDtors;ReenterRootCleanup();if(throwRootStrong)RaiseException(0xE1000003,0,0,nullptr);}
void __fastcall ReleaseWeak(void*){++rootWeakDtors;ReenterRootCleanup();if(throwRootWeak)RaiseException(0xE1000004,0,0,nullptr);}
void* releaseVt[]={reinterpret_cast<void*>(&ReleaseStrong),reinterpret_cast<void*>(&ReleaseWeak)};
void* New(std::size_t size){void* p=std::calloc(1,size);if(!p)std::exit(2);return p;}
void __fastcall ClearModel(void* pair) {
    auto* control=At<unsigned char*>(pair,0);Put<void*>(pair,0,nullptr);Put<void*>(pair,8,nullptr);if(!control)return;
    if(!InterlockedDecrement(reinterpret_cast<volatile LONG*>(control))) {std::free(At<void*>(control,8));++frees;}
    if(!InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+4))){std::free(control);++frees;}
}
void __fastcall ClearMaterial(void* pair){ClearModel(pair);}
void* __fastcall Copy(void* out,const void* in) {
    ++copies;std::memcpy(out,in,0x1E0);
    for(unsigned offset:{0x180u,0x190u})if(auto* c=At<unsigned char*>(out,offset)){InterlockedIncrement(reinterpret_cast<volatile LONG*>(c));InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+4));}
    if(auto* c=At<unsigned char*>(out,0x40))InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+12));return out;
}
void __fastcall Destruct(void* p) {
    if(throwDestruct)RaiseException(0xE1000001,0,0,nullptr);++destructs;ClearModel(static_cast<unsigned char*>(p)+0x190);ClearMaterial(static_cast<unsigned char*>(p)+0x180);
    if(auto* c=At<unsigned char*>(p,0x40))InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12));Put<void*>(p,0x40,nullptr);
}
void* __fastcall Allocate(std::size_t size){++allocations;if(failAllocation && allocations==failAllocation)return nullptr;return New(size);}
void __fastcall Free(void* p,std::size_t){std::free(p);++frees;}
void __fastcall InitModel(void*){if(throwInit)RaiseException(0xE1000002,0,0,nullptr);}
bool __fastcall BindModel(void* p,void* manager,const void* asset) {
    CHECK(manager==image+0x2136FF0);CHECK(asset);CHECK(At<float>(p,0x70)==1 && At<float>(p,0x84)==1 && At<float>(p,0x98)==1 && At<float>(p,0xAC)==1);
    Put<const void*>(p,0,asset);++models;return true;
}
void __fastcall SetVariant(void* dst,const void* src){std::memcpy(dst,src,24);}
struct Fixture {
    unsigned char* actor=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* weak=static_cast<unsigned char*>(New(32));void* factory[3]{};NativeLease l{};unsigned family;bool authority,bound=true,worldOwnerDropped=false;
    Fixture(unsigned f,bool host):family(f),authority(host) {
        if(!actor)std::exit(2);l.object=actor;l.control=weak;l.profile=profiles+f-1;
        Put<const void*>(actor,0,image+l.profile->vtableRva);Put<const void*>(actor,0x30,weak);Put<const void*>(actor,0x28,actor);
        Put<void*>(weak,0,releaseVt);Put<LONG>(weak,8,1);Put<LONG>(weak,12,1);factory[0]=image+FactoryVtable(f);
        Put<void*>(actor,DescriptorOffset(f)-0x10,factory);auto* desc=actor+DescriptorOffset(f);Put<const void*>(desc,0,image+0x1769028);
        Put<const void*>(desc,0x38,actor);Put<const void*>(desc,0x40,weak);Put<std::uint16_t>(desc,0xF8,0xFFFF);Put<std::uint16_t>(desc,0x1C0,0xFFFF);
        Put<float>(desc,0xC0,4);Put<unsigned>(desc,0x1A0,17);
        if(f==2){Put<const void*>(actor,0x1C70,actor+0x3000);auto* material=static_cast<unsigned char*>(New(16));Put<LONG>(material,0,1);Put<LONG>(material,4,1);Put<void*>(material,8,New(0xE8));Put<void*>(actor,0x1C78,material);
            auto* head=actor+0x2500;head[0x19]=1;Put<const void*>(actor,0x1D70,head);Put<std::size_t>(actor,0x1D78,4);Put<const void*>(head,0,actor+0x2600);Put<const void*>(head,8,actor+0x2600);Put<const void*>(head,0x10,actor+0x2900);
            const wchar_t* patternNames[]={L"bullet_pattern_mine",L"bullet_pattern_mine2",L"bullet_pattern_rolling",L"bullet_pattern_snipe"};
            const unsigned counts[]={3,4,3,3},first[]={4,6,2,4},last[]={2,1,2,4};
            for(unsigned i=0;i<4;++i){auto* node=actor+0x2600+i*0x100;auto* custom=actor+0x3200+i*16;
                Put<const void*>(node,0,head);Put<const void*>(node,8,i ? node-0x100 : head);Put<const void*>(node,0x10,i<3 ? node+0x100 : head);Put<const wchar_t*>(node,0x20,patternNames[i]);Put<std::size_t>(node,0x30,std::wcslen(patternNames[i]));Put<std::size_t>(node,0x38,31);
                Put<unsigned>(node,0x40,counts[i]);Put<unsigned>(node,0x44,15);Put<unsigned>(node,0x48,first[i]);Put<unsigned>(node,0x4C,last[i]);Put<float>(node,0x50,i==1 ? 1.0471976f : 0.5235988f);Put<float>(node,0x58,1);Put<float>(node,0x5C,1);Put<float>(node,0x60,1);
                Put<unsigned>(custom,0,1);Put<unsigned>(custom,4,4);Put<unsigned>(custom,8,i==3 ? 0u : 1u);Put<const void*>(node,0x68,custom);
            }
            Put<const void*>(desc,0xE8,actor+0x3200);Put<std::uint16_t>(desc,0xF8,1);
        }
    }
    void Bind(){CHECK(Before(l,authority,nullptr));CHECK(At<LONG>(weak,8)==2);}
    void DropWorldOwner(){InterlockedDecrement(reinterpret_cast<volatile LONG*>(weak+8));worldOwnerDropped=true;}
    void* Desc() const{return actor+DescriptorOffset(family);}
    ~Fixture(){CHECK(Abort(l));CHECK(At<LONG>(weak,8)==(worldOwnerDropped ? 0 : 1));CHECK(At<LONG>(weak,12)==(worldOwnerDropped ? 0 : 1));if(family==2){void* pair[2]={At<void*>(actor,0x1C78),nullptr};ClearMaterial(pair);}VirtualFree(actor,0,MEM_RELEASE);std::free(weak);}
};
Fixture* roles[8]{};
void SetRole(unsigned slot,Fixture* fixture){roles[slot]=fixture;}
alignas(16) float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1};
void* Emit(Fixture& f){return provider.Invoke(At<void*>(image+0x20B2958,0),matrix,f.factory,f.Desc());}
bool Packet(Fixture& f,unsigned char* out,std::size_t* used){return Capture(f.l,out,512,used);}
void TestAcceptance() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();const auto initial=creates;
    CHECK(provider.Owns(host.factory,host.Desc()));CHECK(!provider.Owns(host.factory,host.actor+0x1B31));
    CHECK(!Emit(guest) && creates==initial);rejectCreate=true;CHECK(!Emit(host));rejectCreate=false;CHECK(Empty(host.l));CHECK(creates==initial+1);
    Put<unsigned>(host.Desc(),0x1A0,0x12345678);CHECK(Emit(host));CHECK(!Empty(host.l));CHECK(!Restore(host.l));
    unsigned char bytes[512]{};std::size_t used=0;CHECK(!Capture(host.l,bytes,64,&used) && !used);CHECK(Packet(host,bytes,&used) && used==369);
    CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(actualSeed==0x12345678 && actualX==10);CHECK(creates==initial+3);
    CHECK(Apply(guest.l,bytes,used)==Result::applied && creates==initial+3);bytes[30]^=1;CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==initial+3);bytes[30]^=1;
    CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestOrderingAndPreflight() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Emit(host));unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));
    monster_wire::Event event{};unsigned count=0;CHECK(monster_wire::Decode(bytes,used,&event,1,&count));const auto before=creates;
    event.serial=2;CHECK(monster_wire::Encode(&event,1,bytes,512,&used));CHECK(Apply(guest.l,bytes,used)==Result::pending && creates==before);
    event.serial=1;CHECK(monster_wire::Encode(&event,1,bytes,512,&used));rejectPreflight=true;CHECK(Apply(guest.l,bytes,used)==Result::pending && creates==before && !Find(guest.l)->applied);rejectPreflight=false;
    CHECK(Apply(guest.l,bytes,used)==Result::applied);event.digest^=1;CHECK(monster_wire::Encode(&event,1,bytes,512,&used));CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before+1);
    CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void Test504FreshResources() {
    Fixture host(2,true),guest(2,false);SetRole(0,&host);SetRole(1,&guest);
    // Joining peers may be in different old MAB patterns before ownership.
    // The static roster digest must ignore only the currently selected E8.
    Put<const void*>(guest.Desc(),0xE8,guest.actor+0x3230);Put<unsigned>(guest.actor+0x2600,0x54,37);
    for(unsigned offset:{0xB0u,0xC0u,0xE0u})Put<unsigned>(guest.Desc(),offset,0x3F123456);
    host.Bind();guest.Bind();unsigned char bytes[512]{};std::size_t used=0;void* oldModel=nullptr;
    for(unsigned i=0;i<3;++i) {
        auto* control=static_cast<unsigned char*>(New(16));Put<LONG>(control,0,1);Put<LONG>(control,4,1);Put<void*>(control,8,New(0xC0));Put<const void*>(At<void*>(control,8),0,At<const void*>(host.actor,0x1C70));Put<void*>(host.Desc(),0x190,control);Put<void*>(host.Desc(),0x198,At<void*>(control,8));
        auto* material=At<unsigned char*>(host.actor,0x1C78);InterlockedIncrement(reinterpret_cast<volatile LONG*>(material));InterlockedIncrement(reinterpret_cast<volatile LONG*>(material+4));Put<void*>(host.Desc(),0x180,material);Put<void*>(host.Desc(),0x188,At<void*>(material,8));
        const auto* custom=host.actor+0x3200+(i==0 ? 3u : 0u)*16;Put<const void*>(host.Desc(),0xE8,custom);Put<unsigned>(host.Desc(),0x98,4+i);
        Put<unsigned>(host.Desc(),0x1A0,100+i);CHECK(Emit(host));ClearModel(static_cast<unsigned char*>(host.Desc())+0x190);ClearMaterial(static_cast<unsigned char*>(host.Desc())+0x180);CHECK(Packet(host,bytes,&used));
        const auto modelCount=models;CHECK(Apply(guest.l,bytes,used)==Result::applied && actualCustom!=custom && PatternHash(static_cast<const unsigned char*>(actualCustom))==PatternHash(custom));CHECK(models==modelCount+1 && actualModel && actualModel!=oldModel && actualPattern==4+i && actualB0==At<unsigned>(host.Desc(),0xB0) && actualC0==At<unsigned>(host.Desc(),0xC0) && actualE0==At<unsigned>(host.Desc(),0xE0));oldModel=actualModel;
        CHECK(At<LONG>(At<void*>(guest.actor,0x1C78),0)==1);CHECK(Apply(guest.l,bytes,used)==Result::applied && models==modelCount+1);
    }
    CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestFaultAndDrain() {
    Fixture host(1,true);SetRole(0,&host);host.Bind();const auto initial=creates;
    for(unsigned i=0;i<kQueued;++i)CHECK(Emit(host));CHECK(Find(host.l)->count==kQueued);CHECK(Emit(host));CHECK(Find(host.l)->lost && Find(host.l)->faulted);
    CHECK(!Emit(host) && creates==initial+kQueued+1);CHECK(!Empty(host.l));CHECK(!Restore(host.l));CHECK(Quiesce(host.l));
    unsigned char bytes[512]{};std::size_t used=0;for(unsigned i=0;i<kQueued;++i)CHECK(Packet(host,bytes,&used));CHECK(!Packet(host,bytes,&used) && !used);CHECK(!Empty(host.l) && !Restore(host.l));CHECK(Abort(host.l));SetRole(0,nullptr);
}
void TestClosingAndRole() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Emit(host));CHECK(Quiesce(host.l));const auto before=creates;CHECK(!Emit(host) && creates==before);
    unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));guest.actor[0x2E8]=1;CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(Restore(host.l));CHECK(Restore(guest.l));
    // Outer root Role may outlive this producer while another restores. Its
    // late guest Factory is still suppressed without recreating an old stream.
    CHECK(provider.Owns(guest.factory,guest.Desc()));CHECK(!Emit(guest));SetRole(0,nullptr);SetRole(1,nullptr);CHECK(!provider.Owns(guest.factory,guest.Desc()));
    const auto unbound=creates;CHECK(Emit(guest) && creates==unbound+1);
}
void TestFactoryFailure() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();restoreDuringFactory=&host.l;CHECK(Emit(host) && !reentrantEmpty && !reentrantRestore);restoreDuringFactory=nullptr;unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));
    rejectCreate=true;CHECK(Apply(guest.l,bytes,used)==Result::failed);rejectCreate=false;CHECK(Find(guest.l)->faulted && !Find(guest.l)->applied);CHECK(Apply(guest.l,bytes,used)==Result::failed);CHECK(Abort(guest.l));CHECK(Restore(host.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestLocalIdentityContracts() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();
    void* cloned[65]{};cloned[0]=At<void*>(image+guest.l.profile->vtableRva-8,0);Put<void*>(guest.actor,0,cloned+1);CHECK(provider.Owns(guest.factory,guest.Desc()));CHECK(!Emit(guest));
    auto* originalVt=At<void*>(guest.weak,0);Put<void*>(guest.weak,0,nullptr);CHECK(!Restore(guest.l));CHECK(At<LONG>(guest.weak,8)==2 && Find(guest.l));CHECK(!Emit(guest));Put<void*>(guest.weak,0,originalVt);CHECK(Restore(guest.l));
    const auto before=creates;void* modifiedFactory[3]{};CHECK(provider.Owns(modifiedFactory,host.Desc()));CHECK(!provider.Invoke(At<void*>(image+0x20B2958,0),matrix,modifiedFactory,host.Desc()));CHECK(creates==before && Find(host.l)->faulted);
    CHECK(Abort(host.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void CustomParameter(Fixture& f,float first) {
    auto* node=f.actor+0x3200;Put<unsigned>(node,0,0);Put<int>(node,4,5);Put<int>(node,8,12);
    const float values[]={first,3,3,0.15f,1};for(unsigned i=0;i<5;++i){auto* child=node+12+i*12;Put<unsigned>(child,0,2);Put<int>(child,4,4);Put<float>(child,8,values[i]);}
    Put<const void*>(f.Desc(),0xE8,node);Put<std::uint16_t>(f.Desc(),0xF8,1);
}
void TestPortableCustomParameter() {
    Fixture host(1,true),guest(1,false),changed(1,false);SetRole(0,&host);SetRole(1,&guest);SetRole(2,&changed);CustomParameter(host,0);CustomParameter(guest,0);CustomParameter(changed,1);
    host.Bind();guest.Bind();changed.Bind();CHECK(At<const void*>(host.Desc(),0xE8)!=At<const void*>(guest.Desc(),0xE8));CHECK(Find(host.l)->digest==Find(guest.l)->digest);CHECK(Find(host.l)->digest!=Find(changed.l)->digest);
    CHECK(Emit(host));unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));const auto before=creates;
    CHECK(Apply(changed.l,bytes,used)==Result::failed && creates==before);CHECK(Apply(guest.l,bytes,used)==Result::applied && creates==before+1);
    CHECK(Restore(host.l));CHECK(Restore(guest.l));CHECK(Restore(changed.l));SetRole(0,nullptr);SetRole(1,nullptr);SetRole(2,nullptr);
}
void TestMuzzleLifetimeContract() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Emit(host));unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));const auto before=creates;
    Put<const float*>(host.Desc(),0x70,matrix);CHECK(!Emit(host) && creates==before && Find(host.l)->faulted);
    Event event{};unsigned count=0;CHECK(monster_wire::Decode(bytes,used,&event,1,&count));event.sample.hasMuzzle=true;std::memcpy(event.sample.muzzleMatrix,matrix,64);CHECK(monster_wire::Encode(&event,1,bytes,512,&used));
    CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before && !Find(guest.l)->applied);CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestPersistentHelperCleanup() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Emit(host));unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));const auto before=creates;
    helperCleanupFault=true;CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before+1);helperCleanupFault=false;auto* s=Find(guest.l);
    CHECK(s->projectile.created && s->projectile.uncertain && s->projectile.cleanupEntered && s->projectile.made && !s->applied);CHECK(At<LONG>(guest.weak,8)==3);
    CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before+1);CHECK(!Empty(guest.l) && !Restore(guest.l) && !Abort(guest.l));
    // The injected fault occurred before any decrement. Only this test can
    // therefore repair its synthetic journal; production never clears it.
    s->projectile.uncertain=false;s->projectile.cleanupEntered=false;CHECK(Abort(guest.l));CHECK(Restore(host.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestRecoverableHelperCleanup() {
    Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Emit(host));unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));const auto before=creates;
    helperCleanupBlocked=true;CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before+1);auto* s=Find(guest.l);CHECK(s->projectile.created && !s->projectile.uncertain && s->projectile.made);
    CHECK(!Empty(guest.l) && !Restore(guest.l));helperCleanupBlocked=false;CHECK(Restore(guest.l));CHECK(creates==before+1);CHECK(Restore(host.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestAllocationFailureAndJournal() {
    Fixture host(2,true),guest(2,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();Event event{};event.serial=1;event.digest=Number(Find(host.l)->digest,Find(host.l)->patterns[0].hash,8);event.family=2;std::memcpy(event.sample.matrix,matrix,64);event.sample.resourceMask=3;
    unsigned char bytes[512]{};std::size_t used=0;CHECK(monster_wire::Encode(&event,1,bytes,512,&used));const auto before=creates;failAllocation=allocations+2;const auto freeBefore=frees;
    CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before);CHECK(frees==freeBefore+1 && !Find(guest.l)->scratchMade && !Find(guest.l)->orphanControl);failAllocation=0;
    throwInit=true;CHECK(Apply(guest.l,bytes,used)==Result::failed && creates==before);throwInit=false;auto* s=Find(guest.l);CHECK(s->uncertain && s->orphanControl && s->orphanPayload);CHECK(!Empty(guest.l) && !Restore(guest.l) && !Abort(guest.l));CHECK(At<LONG>(guest.weak,8)==2);
    // Test-only repair after an injected exception known to occur BEFORE init.
    std::free(s->orphanPayload);std::free(s->orphanControl);s->orphanPayload=nullptr;s->orphanControl=nullptr;s->uncertain=false;CHECK(Abort(guest.l));
    CHECK(Restore(host.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestRootReleasePreflightRetry() {
    Fixture host(1,true);SetRole(0,&host);host.Bind();host.DropWorldOwner();auto* s=Find(host.l);CHECK(At<LONG>(host.weak,8)==1 && s->rootHeld);
    const auto strongBefore=rootStrongDtors,weakBefore=rootWeakDtors,baseBefore=destructs;Put<void*>(host.weak,0,nullptr);
    CHECK(!Restore(host.l) && s==Find(host.l) && s->rootHeld && !s->cleanupEntered && !s->uncertain);
    CHECK(At<LONG>(host.weak,8)==1 && destructs==baseBefore && rootStrongDtors==strongBefore && rootWeakDtors==weakBefore);
    Put<void*>(host.weak,0,releaseVt);CHECK(Restore(host.l));CHECK(At<LONG>(host.weak,8)==0 && At<LONG>(host.weak,12)==0);
    CHECK(rootStrongDtors==strongBefore+1 && rootWeakDtors==weakBefore+1 && destructs==baseBefore+1);
    CHECK(Restore(host.l) && rootStrongDtors==strongBefore+1 && rootWeakDtors==weakBefore+1);SetRole(0,nullptr);
}
void TestRootDestructorReentry() {
    Fixture host(1,true);SetRole(0,&host);host.Bind();host.DropWorldOwner();rootDtorReenterLease=&host.l;rootReentryBlocked=true;rootOwnershipConsumed=true;
    const auto strongBefore=rootStrongDtors,weakBefore=rootWeakDtors,reentriesBefore=rootReentries;
    CHECK(Restore(host.l));rootDtorReenterLease=nullptr;
    CHECK(rootReentries==reentriesBefore+2 && rootReentryBlocked && rootOwnershipConsumed);
    CHECK(rootStrongDtors==strongBefore+1 && rootWeakDtors==weakBefore+1);
    CHECK(!Find(host.l) && At<LONG>(host.weak,8)==0 && At<LONG>(host.weak,12)==0);SetRole(0,nullptr);
}
void TestRootDestructorExceptionsRetainLedger() {
    // These two fake game objects intentionally survive until this test process
    // exits. An entered, uncertain native destructor has no safe retry/forget
    // operation in production; freeing its synthetic journal would hide that.
    for(unsigned stage=0;stage<2;++stage) {
        auto* host=new Fixture(1,true);SetRole(stage,host);host->Bind();host->DropWorldOwner();rootDtorReenterLease=&host->l;
        rootReentryBlocked=true;rootOwnershipConsumed=true;const auto strongBefore=rootStrongDtors,weakBefore=rootWeakDtors;
        throwRootStrong=stage==0;throwRootWeak=stage==1;CHECK(!Restore(host->l));throwRootStrong=false;throwRootWeak=false;rootDtorReenterLease=nullptr;
        auto* s=Find(host->l);CHECK(s && s->held && !s->rootHeld && s->cleanupEntered && s->uncertain && s->faulted);
        CHECK(rootReentryBlocked && rootOwnershipConsumed && !s->baseMade);
        CHECK(At<LONG>(host->weak,8)==0 && At<LONG>(host->weak,12)==(stage==0 ? 1 : 0));
        CHECK(rootStrongDtors==strongBefore+1 && rootWeakDtors==weakBefore+stage);
        for(unsigned retry=0;retry<3;++retry)CHECK(!Restore(host->l) && !Abort(host->l) && !Empty(host->l) && Find(host->l)==s);
        CHECK(rootStrongDtors==strongBefore+1 && rootWeakDtors==weakBefore+stage && At<LONG>(host->weak,8)==0);
    }
}
struct TraceSpec {
    const char* name;unsigned family,countdown,cycles;bool failOnce;
    const char* sourceSgo;const char* mabAction;
};
void TraceHex(FILE* output,const unsigned char* bytes,std::size_t size) {
    static constexpr char digits[]="0123456789abcdef";
    for(std::size_t i=0;i<size;++i){std::fputc(digits[bytes[i]>>4],output);std::fputc(digits[bytes[i]&15],output);}
}
void TraceSourceCall(Fixture& host,unsigned nativeTick,unsigned muzzle,unsigned pellet,bool fail) {
    // The scheduling below mirrors the audited native integer counters. This
    // fixture substitutes the animated final matrix and actual Unit allocation.
    // It does not load EDF.dll, execute MAB/CAS, or equate ticks with any Hz.
    auto state=At<std::uint64_t>(host.actor,0x3E8)*0x5D588B656C078965ull+0x269EC3u;
    Put<std::uint32_t>(host.Desc(),0x1A0,static_cast<std::uint32_t>(state>>48));
    const auto after=state*0x5D588B656C078965ull+0x269EC3u;Put<std::uint64_t>(host.actor,0x3E8,host.family==1 ? state : after);
    matrix[12]=static_cast<float>(nativeTick)*0.01f;matrix[13]=static_cast<float>(muzzle);matrix[14]=static_cast<float>(pellet);
    if(host.family==2)CHECK(Resources(*Find(host.l),host.l,2,host.Desc(),3));
    const auto before=creates;rejectCreate=fail;void* result=Emit(host);rejectCreate=false;CHECK(creates==before+1);CHECK((result!=nullptr)!=fail);
    if(host.family==1)Put<std::uint64_t>(host.actor,0x3E8,after); // original 4ACE8D..4ACEF7 post-create RNG/effect
    if(host.family==2){ClearModel(static_cast<unsigned char*>(host.Desc())+0x190);ClearMaterial(static_cast<unsigned char*>(host.Desc())+0x180);}
}
void TraceScenario(FILE* output,const TraceSpec& spec) {
    Fixture host(spec.family,true),guest(spec.family,false);SetRole(0,&host);SetRole(1,&guest);
    if(spec.family==1){CustomParameter(host,0);CustomParameter(guest,0);}host.Bind();guest.Bind();
    Put<std::uint64_t>(host.actor,0x3E8,0x5015041234ull);
    const bool saturated=spec.cycles>1;const unsigned cycleTicks=spec.family==1 ? spec.countdown : 31u;
    const unsigned totalTicks=cycleTicks*spec.cycles;unsigned totalAttempts=0,totalAccepted=0,totalPackets=0,queuePeak=0,failed=0;
    std::fprintf(output,"{\"kind\":\"metadata\",\"schema\":\"monster-native-factory-trace-v1\",\"scenario\":\"%s\",\"family\":%u,\"sourceSgo\":\"%s\",\"mabAction\":\"%s\",\"callsite\":\"%s\",\"nativeEmitter\":\"%s\",\"nativeTickUnit\":\"one-root-slot5-emitter-invocation\",\"nativeTickIndexBase\":0,\"renderOrPhysicsHz\":null,\"realSkillRepeatNativeTicks\":null,\"clipMabTicks\":%u,\"clipMabTicksAreNativeTicks\":false,\"cycles\":%u,\"totalNativeTicks\":%u,\"upperBoundCycleNativeTicks\":%u,\"immediateRearmUpperBound\":%s,\"factoryFailureOnce\":%s,\"maximumPacketBytes\":512,\"payloadSource\":\"production-Invoke-to-nonnull-Unit-mock-to-Capture-Encode\",\"replayVerification\":\"production-Apply-and-duplicate-Apply-with-persistent-helper-mock\",\"matrixSource\":\"fixture-final-world-matrix-with-stable-muzzle-offsets\",\"descriptorSource\":\"native-ABI-fixture-not-game-memory-dump\",\"sharedSampleCapture\":\"fixture-explicit-numeric-field-copy\",\"cadence\":",
        spec.name,spec.family,spec.sourceSgo,spec.mabAction,spec.family==1 ? "4ACE88" : "4B2C2C",spec.family==1 ? "4ACDB0" : "4B24D0",spec.family==1 ? 480u : 320u,spec.cycles,totalTicks,cycleTicks,saturated ? "true" : "false",spec.failOnce ? "true" : "false");
    if(spec.family==1)std::fprintf(output,"{\"mabParameter\":\"fire.time\",\"mabTrack\":%u,\"mabKey\":4,\"mabFireAt\":%.7f,\"initialCountdown\":%u,\"intervalNativeTicks\":2,\"counterDecrementBeforeModulo\":true,\"attemptsPerBurst\":%u,\"source\":\"4AC070->root1D10/1D14;4ACDCC..4ACDDE\"}",spec.countdown==140 ? 49u : 48u,spec.countdown==140 ? 210.0 : 218.6000061,spec.countdown,spec.countdown/2);
    else std::fprintf(output,"{\"pattern\":\"bullet_pattern_mine\",\"fireCount\":3,\"intervalNativeTicks\":15,\"pelletsFirst\":4,\"pelletsLast\":2,\"muzzleGroups\":4,\"muzzlesPerGroup\":2,\"mabTrack\":67,\"mabBeginKey\":1,\"mabBeginAt\":104,\"mabFireKey\":2,\"mabFireAt\":125,\"mabEndKey\":8,\"mabEndAt\":256,\"waveNativeTicks\":[0,15,30],\"attemptsPerWave\":[32,24,16],\"attemptsPerBurst\":72,\"source\":\"muzzle_group_setting;bullet_pattern_mine;4B25EC..4B261A;4B2717..4B273F;4B2E45..4B2E56\"}");
    std::fputs("}\n",output);
    unsigned remaining=0;
    for(unsigned tick=0;tick<totalTicks;++tick) {
        const unsigned local=tick%cycleTicks;unsigned attempts=0;unsigned pellets=0;
        if(spec.family==1){if(!local)remaining=spec.countdown;if(remaining && !(--remaining%2))attempts=1;pellets=1;}
        else if(local==0 || local==15 || local==30){pellets=4-local/15;attempts=8*pellets;}
        unsigned accepted=0,acceptedCalls[32]{};std::fprintf(output,"{\"kind\":\"tick\",\"scenario\":\"%s\",\"nativeTick\":%u,\"cycle\":%u,\"factoryCalls\":[",spec.name,tick,tick/cycleTicks);
        for(unsigned call=0;call<attempts;++call) {
            ++totalAttempts;const bool fail=spec.failOnce && totalAttempts==1;if(fail)++failed;
            TraceSourceCall(host,tick,spec.family==1 ? 0u : call/pellets,call%pellets,fail);
            std::fprintf(output,"%s{\"ordinal\":%u,\"accepted\":%s}",call ? "," : "",totalAttempts,fail ? "false" : "true");
            if(!fail)acceptedCalls[accepted++]=totalAttempts;
        }
        totalAccepted+=accepted;auto* stream=Find(host.l);if(stream->count>queuePeak)queuePeak=stream->count;CHECK(stream->count==accepted);
        std::fprintf(output,"],\"factoryAttempts\":%u,\"factoryAccepted\":%u,\"packets\":[",attempts,accepted);
        unsigned packets=0;while(stream->count) {
            unsigned char bytes[512]{};std::size_t used=0;CHECK(Packet(host,bytes,&used));CHECK(used==369);Event event{};unsigned count=0;CHECK(monster_wire::Decode(bytes,used,&event,1,&count));
            const auto before=creates;CHECK(Apply(guest.l,bytes,used)==Result::applied && creates==before+1);CHECK(Apply(guest.l,bytes,used)==Result::applied && creates==before+1);
            CHECK(actualCustom!=At<const void*>(host.Desc(),0xE8) && PatternHash(static_cast<const unsigned char*>(actualCustom))==PatternHash(At<const unsigned char*>(host.Desc(),0xE8)));
            std::fprintf(output,"%s{\"factoryCallOrdinal\":%u,\"serial\":%llu,\"actualUnits\":1,\"bytes\":%zu,\"hex\":\"",packets ? "," : "",acceptedCalls[packets],static_cast<unsigned long long>(event.serial),used);TraceHex(output,bytes,used);std::fputs("\"}",output);++packets;
        }
        CHECK(packets==accepted);totalPackets+=packets;std::fprintf(output,"],\"queuedAfterCapture\":%u}\n",stream->count);
    }
    CHECK(Quiesce(host.l));CHECK(Quiesce(guest.l));const bool empty=Empty(host.l) && Empty(guest.l);CHECK(empty);const bool restored=Restore(host.l) && Restore(guest.l);CHECK(restored);
    CHECK(totalAccepted==totalPackets && totalAttempts==totalAccepted+failed && failed==(spec.failOnce ? 1u : 0u));
    std::fprintf(output,"{\"kind\":\"drain_summary\",\"scenario\":\"%s\",\"totalNativeTicks\":%u,\"totalAttempts\":%u,\"totalAccepted\":%u,\"totalPackets\":%u,\"factoryFailures\":%u,\"queuePeak\":%u,\"queueCapacity\":4096,\"overflowUsed\":false,\"empty\":%s,\"normalRestore\":%s,\"aborted\":false}\n",spec.name,totalTicks,totalAttempts,totalAccepted,totalPackets,failed,queuePeak,empty ? "true" : "false",restored ? "true" : "false");
    SetRole(0,nullptr);SetRole(1,nullptr);
}
int DumpTrace(const char* path) {
    FILE* output=nullptr;if(fopen_s(&output,path,"wb") || !output){std::fprintf(stderr,"cannot open trace: %s\n",path);return 2;}
    traceDumpActive=true;
    const TraceSpec cases[]={
        {"501_breath_t_single",1,140,1,false,"MONSTER501.SGO","attack_breath_t"},
        {"501_king_breath_b_single",1,120,1,false,"MONSTER501_KING.SGO","attack_breath_b"},
        {"501_breath_t_saturation_upper_bound",1,140,96,false,"MONSTER501.SGO","attack_breath_t"},
        {"504_attack_spark_single",2,0,1,false,"E504_MONSTERA.SGO","attack_spark"},
        {"501_factory_failure_once",1,140,1,true,"MONSTER501.SGO","attack_breath_t"},
        {"504_factory_failure_once",2,0,1,true,"E504_MONSTERA.SGO","attack_spark"},
    };
    for(const auto& spec:cases)TraceScenario(output,spec);
    const bool error=std::ferror(output)!=0;if(std::fclose(output) || error){std::fputs("trace write failed\n",stderr);return 2;}
    std::printf("Monster production trace: %s; %u checks, %u failures\n",path,checks,failures);return failures ? 1 : 0;
}
} // namespace
namespace edf {bool Readable(const void* p,std::size_t n,bool write) noexcept {if(!p || !n)return false;MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(p,&info,sizeof(info)) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;if(write && !(info.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY)))return false;return reinterpret_cast<std::uintptr_t>(p)+n<=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;}}
namespace crew::enemy_native {
void* MonsterNetTestFunction(unsigned rva) noexcept {switch(rva){case 0x32EF10:return reinterpret_cast<void*>(&Copy);case 0x100C90:return reinterpret_cast<void*>(&Destruct);case 0x22F790:return reinterpret_cast<void*>(&ClearModel);case 0x22F8E0:return reinterpret_cast<void*>(&ClearMaterial);case 0x12D85B0:return reinterpret_cast<void*>(&Allocate);case 0x12D85EC:return reinterpret_cast<void*>(&Free);case 0x111F570:return reinterpret_cast<void*>(&InitModel);case 0x11002D0:return reinterpret_cast<void*>(&BindModel);case 0x244CB0:return reinterpret_cast<void*>(&SetVariant);default:return nullptr;}}
const NativeProfile* Profiles(std::size_t* n) noexcept {*n=2;return profiles;}
bool NativeActionRole(const void* root,bool* host) noexcept {for(const auto* f:roles)if(f && f->bound && f->actor==root && At<const void*>(root,0x30)==f->weak){*host=f->authority;return true;}return false;}
bool NativeActionQuiescing(const void*) noexcept {return false;}
bool RegisterNativeActionExtension(const NativeActionExtension& e) noexcept {extension=e;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& p) noexcept {provider=p;return true;}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t rva) noexcept {if(rva==0x4ACE88)callsites|=1;if(rva==0x4B2C2C)callsites|=2;return true;}
bool InstallNativeUnitFactoryDispatcher() noexcept {return true;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider&) noexcept {return true;}
bool NativeUnitFactoryCallsiteReady(std::uint32_t rva) noexcept {return rva==0x4ACE88 || rva==0x4B2C2C;}
void* InvokeOriginalNativeUnitFactory(void* manager,const float* m,void*,void* d){CHECK(manager==At<void*>(image+0x20B2958,0));++creates;if(restoreDuringFactory){reentrantEmpty=monster_net::Empty(*restoreDuringFactory);reentrantRestore=monster_net::Restore(*restoreDuringFactory);}actualX=m[12];actualSeed=At<std::uint32_t>(d,0x1A0);actualPattern=At<std::uint32_t>(d,0x98);actualB0=At<unsigned>(d,0xB0);actualC0=At<unsigned>(d,0xC0);actualE0=At<unsigned>(d,0xE0);actualModel=At<void*>(d,0x198);actualCustom=At<std::uint16_t>(d,0xF8)==1 ? At<const void*>(d,0xE8) : nullptr;if(actualModel && !traceDumpActive && !rejectCreate){ // simulate native projectile retaining its fresh model
    auto* c=At<unsigned char*>(d,0x190);InterlockedIncrement(reinterpret_cast<volatile LONG*>(c));InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+4));
}return rejectCreate ? nullptr : reinterpret_cast<void*>(static_cast<std::uintptr_t>(creates+0x1000));}
bool ValidNativeProjectileSample(const NativeProjectileSample& s) noexcept {for(float v:s.matrix)if(!std::isfinite(v))return false;return s.matrix[15]==1 && s.resourceMask<=3;}
bool CaptureNativeProjectileSample(const NativeLease&,const void* local,const float* m,const void* d,NativeProjectileSample* out) noexcept {++captures;CHECK(local==d);if(rejectCapture)return false;NativeProjectileSample s{};std::memcpy(s.matrix,m,64);for(unsigned i=0;i<kProjectileWords;++i)s.scalar[i]=At<std::uint32_t>(d,kProjectileScalar32[i]);for(unsigned i=0;i<kProjectileBytes;++i)s.bytes[i]=At<unsigned char>(d,kProjectileScalar8[i]);s.scalar178=At<std::uint16_t>(d,0x178);s.random=At<std::uint32_t>(d,0x1A0);s.shot=At<std::uint32_t>(d,0x1A4);s.flags=At<std::uint32_t>(d,0x1A8);s.resourceMask=static_cast<unsigned char>((At<const void*>(d,0x180) ? 1 : 0)|(At<const void*>(d,0x190) ? 2 : 0));*out=s;return true;}
bool CanPrepareNativeProjectileSample(const NativeLease&,const void* local,float*,const NativeProjectileSample& s) noexcept {return !rejectPreflight && s.resourceMask==((At<const void*>(local,0x180) ? 1 : 0)|(At<const void*>(local,0x190) ? 2 : 0));}
void* WithNativeProjectileSample(const NativeLease&,const void* local,float*,const NativeProjectileSample& s,NativeProjectileCreateFn fn,void* user) noexcept {++prepared;alignas(16) unsigned char d[0x1E0]{};Copy(d,local);Put<std::uint32_t>(d,0x1A0,s.random);void* result=fn(user,s.matrix,d);Destruct(d);return result;}
bool NativeProjectileJournalEmpty(const NativeProjectileJournal& j) noexcept {return !j.ownerObject && !j.ownerControl && !j.heldMask && !j.made && !j.busy && !j.copyEntered && !j.createEntered && !j.cleanupEntered && !j.uncertain;}
bool RestoreNativeProjectileJournal(NativeProjectileJournal& j) noexcept {
    if(NativeProjectileJournalEmpty(j))return true;if(helperCleanupBlocked || j.busy || j.uncertain || j.copyEntered || j.createEntered || j.cleanupEntered)return false;
    __try {if(j.made){j.cleanupEntered=true;Destruct(j.descriptor);j.made=false;j.cleanupEntered=false;}
        if(j.heldMask&1){InterlockedDecrement(reinterpret_cast<volatile LONG*>(const_cast<void*>(j.controls[0]))+2);j.heldMask&=~1u;}j={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){j.uncertain=true;return false;}
}
void* WithNativeProjectileSampleInPlace(NativeProjectileJournal& j,const NativeLease& l,const void* local,float*,const NativeProjectileSample& s,NativeProjectileCreateFn fn,void* user) noexcept {
    if(!NativeProjectileJournalEmpty(j))return nullptr;++prepared;j.ownerObject=l.object;j.ownerControl=l.control;j.objects[0]=l.object;j.controls[0]=l.control;j.heldMask=1;InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<void*>(l.control))+2);j.busy=true;
    __try {j.copyEntered=true;Copy(j.descriptor,local);j.made=true;j.copyEntered=false;for(unsigned i=0;i<kProjectileWords;++i)Put<std::uint32_t>(j.descriptor,kProjectileScalar32[i],s.scalar[i]);for(unsigned i=0;i<kProjectileBytes;++i)Put<unsigned char>(j.descriptor,kProjectileScalar8[i],s.bytes[i]);Put<std::uint16_t>(j.descriptor,0x178,s.scalar178);Put<std::uint32_t>(j.descriptor,0x1A0,s.random);Put<std::uint32_t>(j.descriptor,0x1A4,s.shot);Put<std::uint32_t>(j.descriptor,0x1A8,s.flags);j.createEntered=true;j.created=fn(user,s.matrix,j.descriptor);j.createEntered=false;j.busy=false;
        if(helperCleanupFault){j.cleanupEntered=true;j.uncertain=true;return nullptr;}void* result=j.created;return RestoreNativeProjectileJournal(j) ? result : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){j.uncertain=true;j.busy=false;return nullptr;}
}
}
int main(int argc,char** argv) {
    using namespace crew::enemy_native::monster_net;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2140000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!crew::image)return 2;
    for(const auto& s:kSigs)std::memcpy(crew::image+s.rva,s.bytes,16);
    for(const auto& p:profiles)Put<const void*>(crew::image+p.vtableRva-8,0,crew::image+p.vtableRva-0x100);
    Put<const void*>(crew::image+FactoryVtable(1),16,crew::image+0x259D80);Put<const void*>(crew::image+FactoryVtable(2),16,crew::image+0x26EFC0);
    auto* manager=New(0xD00);Put<void*>(crew::image,0x20B2958,manager);CHECK(InstallMonsterAcceptedProjectiles());CHECK(callsites==3 && extension.tag==monster_wire::kTag);
    if(argc==3 && !std::strcmp(argv[1],"--dump-trace"))return DumpTrace(argv[2]);
    if(argc!=1){std::fputs("usage: enemy_control_monster_net_test [--dump-trace path]\n",stderr);return 2;}
    TestAcceptance();TestOrderingAndPreflight();Test504FreshResources();TestFaultAndDrain();TestClosingAndRole();TestFactoryFailure();TestLocalIdentityContracts();TestPortableCustomParameter();TestMuzzleLifetimeContract();TestPersistentHelperCleanup();TestRecoverableHelperCleanup();TestAllocationFailureAndJournal();TestRootReleasePreflightRetry();TestRootDestructorReentry();TestRootDestructorExceptionsRetainLedger();
    std::printf("monster accepted projectiles: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
