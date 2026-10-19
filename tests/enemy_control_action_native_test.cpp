// Runs the production main-action gate, codec and replay against synthetic native
// callback objects. Native setter/dispatch test seams observe real lifecycle calls.
#define ENEMY_CONTROL_ACTION_TEST
#include "../src/enemy_control_action_native.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;
using namespace crew::enemy_native;
using namespace crew::enemy_native::action_native;
constexpr NativeProfile profiles[]={
    {"Deiroi",0x17AA648,0x31ED90,0x31E680,1,45,0x30,Kind::deiroi,3,4},
    {"EDF6_GiantDangoLarge",0x17AFEE0,0x368310,0x367800,2,55,3,Kind::insect,7,4},
    {"EDF6_TimeShip",0x17B93C0,0x3EE300,0x3EDE70,3,55,0xB,Kind::flying,15,4},
    {"FixtureRegisteredWalking",0x17B36F0,0x444E90,0x443500,4,55,0,Kind::walking,3,4},
};
unsigned checks=0,failures=0,entries=0,exits=0,updates=0,setters=0,effects=0;
const void* resolvedTarget=nullptr;bool expiredTarget=false,blockTargetRelease=false,failTargetRelease=false;unsigned targetPins=0,targetReleases=0;
bool reject=false;unsigned expectedSemantic=0;
void Check(bool good,const char* text,int line){++checks;if(!good){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct Fixture {
    unsigned char* object=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* rows=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* legs=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const Contract* contract=nullptr;NativeLease lease{};
    explicit Fixture(unsigned family) {
        if(!object || !control || !rows || !legs)std::exit(2);
        contract=&kContracts[family];const auto& c=*contract;
        Put<LONG>(control,8,1);Put<const void*>(object,0x30,control);Put<const void*>(object,0,image+c.vtable);
        Put<const void*>(object+c.machine,0,image+c.machineVtable);
        if(c.family==Family::deiroi)Put<const void*>(object+c.machine,0x28,object);Put<const void*>(object+c.machine,8,object+c.context);
        Put<const void*>(object+c.context,c.family==Family::deiroi ? 0x28 : 8,object+c.machine);
        if(c.family==Family::deiroi) {
            Put<const void*>(object+c.context,0x10,rows);Put<std::uint64_t>(object+c.context,0x18,6);Put<std::uint64_t>(object+c.context,0x20,6);
            for(unsigned i=0;i<6;++i) {
                auto* entry=rows+0x100+i*0x40;auto* ref=rows+0x300+i*0x20;
                Put<const void*>(rows+i*16,0,entry);Put<const void*>(rows+i*16,8,ref);Put<LONG>(ref,8,1);
                Put<const void*>(entry,8,image+kDeiroi[i]);Put<const void*>(entry,24,object);
            }
            Put<const void*>(object,0xAE8,legs);Put<std::uint64_t>(object,0xAF0,3);Put<std::uint64_t>(object,0xAF8,3);
            Put<std::uint64_t>(object,0x1A60,1);Put<float>(object,0x1A7C,1);Put<float>(object,0x161C,1);
            auto* target=legs+0x400;auto* targetControl=legs+0x700;Put<LONG>(targetControl,8,1);Put<const void*>(target,0x28,target);Put<const void*>(target,0x30,targetControl);
            for(unsigned leg=0;leg<3;++leg){auto* controller=rows+0x500+leg*0x160;Put<const void*>(legs+leg*0x40,0x28,controller);Put<const void*>(controller,0x140,target);Put<const void*>(controller,0x148,targetControl);}
        } else {
            Put<const void*>(object+c.machine,0x10,object);
            if(c.family==Family::timeShip){for(unsigned i=0;i<4;++i)Put<float>(object,0x1430+i*20,1);object[0xBBC]=1;Put<const void*>(object,0xDF8,legs);Put<std::uint64_t>(object,0xE08,1);}
        }
        lease.object=object;lease.profile=profiles+family;lease.control=control;State(0,0,0);
    }
    ~Fixture(){CHECK(RestoreNativeActions(lease));VirtualFree(object,0,MEM_RELEASE);VirtualFree(control,0,MEM_RELEASE);VirtualFree(rows,0,MEM_RELEASE);VirtualFree(legs,0,MEM_RELEASE);}
    void State(unsigned semantic,unsigned frame,float seconds) {
        const auto& c=*contract;
        if(c.family==Family::deiroi)Put<const void*>(object+c.machine,0x10,At<const void*>(rows+semantic*16,0));
        else Put<const void*>(object+c.machine,0x18,image+Callback(c.family,semantic));
        Put<float>(object,c.context,seconds);Put<unsigned>(object,c.context+4,frame);
    }
    void Invoke(unsigned operation) {
        auto* machine=object+contract->machine;auto* vt=At<const void* const*>(machine,0);
        float dt[4]={1.0f/60.0f,0,0,0};reinterpret_cast<Dispatch>(const_cast<void*>(vt[1]))(machine,operation,dt,nullptr);
        if(operation==1){Put<float>(object,contract->context,At<float>(object,contract->context)+dt[0]);Put<unsigned>(object,contract->context+4,At<unsigned>(object,contract->context+4)+1u);}
    }
};
void __fastcall VisibilitySpy(void* object) {
    ++effects;auto* actor=static_cast<unsigned char*>(object);actor[0xD20]=1;
    const auto records=At<unsigned char*>(actor,0xDF8);const auto count=At<std::uint64_t>(actor,0xE08);
    for(std::size_t i=0;i<count;++i)records[i*0xC0+0xA4]=1;
}
void __fastcall DispatchSpy(void* machine,unsigned operation,void*,void*) {
    if(operation==0)++entries;else if(operation==2)++exits;else if(operation==1)++updates;
    CHECK(machine!=nullptr);
    for(auto& b:bindings)if(b.object && static_cast<const unsigned char*>(b.object)+b.contract->machine==machine &&
        b.contract->family==Family::timeShip && operation==1) {
        unsigned semantic=0;if(Semantic(b,&semantic) && semantic==6 && At<unsigned>(b.object,b.contract->context+4)==1)VisibilitySpy(const_cast<void*>(b.object));
    }
}
void SetCommon(void* machine,unsigned semantic) {
    ++setters;if(reject)return;CHECK(expectedSemantic==99 || semantic==expectedSemantic);
    auto* m=static_cast<unsigned char*>(machine);const auto* context=At<const unsigned char*>(m,8);
    auto* clock=const_cast<unsigned char*>(context);
    auto* vt=At<const void* const*>(m,0);const auto dispatch=reinterpret_cast<Dispatch>(const_cast<void*>(vt[1]));
    dispatch(machine,2,nullptr,nullptr);
    const bool deiroi=At<const void*>(context,0x28)==machine;
    if(deiroi)Put<const void*>(m,0x10,At<const void*>(At<const unsigned char*>(context,0x10)+semantic*16,0));
    else {
        const auto* actor=At<const unsigned char*>(m,0x10);const auto family=At<const void*>(actor,0)==image+0x17AFEE0 ? Family::large : Family::timeShip;
        Put<const void*>(m,0x18,image+Callback(family,semantic));
    }
    Put<float>(clock,0,0);Put<unsigned>(clock,4,0);dispatch(machine,0,nullptr,nullptr);
}
void __fastcall SetOrdinal(void* machine,unsigned ordinal,void*){
    if(ordinal==5)CHECK(StompTarget(At<void*>(machine,0x28))==resolvedTarget);
    SetCommon(machine,ordinal);
}
void __fastcall SetDescriptor(void* machine,const Desc* desc,void*) {
    unsigned semantic=99;const auto actor=At<const unsigned char*>(machine,0x10);
    const auto family=At<const void*>(actor,0)==image+0x17AFEE0 ? Family::large : Family::timeShip;
    for(unsigned i=0;i<Count(family);++i)if(desc->callback==image+Callback(family,i))semantic=i;
    CHECK(desc->adjustor==0);
    if(family==Family::timeShip)CHECK(At<float>(actor,0x90)==42);
    SetCommon(machine,semantic);
}
void BuildImage() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)std::exit(2);
    for(const auto& s:kSigs)std::memcpy(image+s.rva,s.bytes,16);
    for(const auto& c:kContracts) {
        Put<const void*>(image+c.machineVtable-8,0,image+0x1000);Put<const void*>(image+c.machineVtable,0,image+0x2000);
        Put<const void*>(image+c.machineVtable,8,image+c.dispatch);
    }
}
std::size_t CapturePacket(Fixture& host,unsigned char* bytes) {
    std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,512,&used));CHECK(used>=kHeaderBytes+kStateBytes && used<=512);return used;
}
void Lifecycle(unsigned family) {
    Fixture host(family),guest(family);if(family==0)resolvedTarget=guest.legs+0x400;Input input{};input.held=input.pressed=0xFFFF;
    CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(input.held==0xFFFF);
    input.held=input.pressed=0xFFFF;if(family==2){guest.object[0x1410]=1;Put<unsigned>(guest.object,0x1474,1);}CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    if(family==2)CHECK(!guest.object[0x1410] && At<unsigned>(guest.object,0x1474)==0);
    const auto mask=family==0 ? 0x30u : family==1 ? 3u : 0xBu;CHECK(!(input.held&mask) && !(input.pressed&mask));
    CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};const auto first=CapturePacket(host,bytes);
    const auto entryBefore=entries;CHECK(ApplyNativeAction(guest.lease,bytes,first)==Result::applied);CHECK(entries==entryBefore);
    const unsigned live=family==0 ? 5u : family==1 ? 1u : 6u;
    expectedSemantic=live;host.State(live,1,0.016f);if(family==1){Put<float>(host.object,0x143C,1);Put<float>(host.object,0x1484,1);Put<float>(host.object,0x148C,1);Put<float>(host.object,0xF90,12);Put<float>(host.object,0xFA0,8);}if(family==2){Put<float>(host.object,0x90,42);Put<float>(guest.object,0x90,-19);}Put<std::uint64_t>(host.object,0x3E8,0x123456789ABCDEFull);Put<std::uint64_t>(host.object,0x490,0x9876543211234567ull);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    const auto action=CapturePacket(host,bytes);const auto oldSetters=setters;
    CHECK(ApplyNativeAction(guest.lease,bytes,action)==Result::applied);CHECK(setters==oldSetters+1);
    if(family==1)CHECK(At<float>(guest.object,0x143C)==1 && At<float>(guest.object,0x1484)==1 && At<float>(guest.object,0x148C)==1 && At<float>(guest.object,0xF90)==12 && At<float>(guest.object,0xFA0)==8);
    if(family==2){CHECK(At<float>(guest.object,0x90)==-19);CHECK(!guest.object[0x1410] && At<unsigned>(guest.object,0x1474)==0);} // native entry context is restored
    CHECK(At<std::uint64_t>(guest.object,0x3E8)==0x123456789ABCDEFull && At<std::uint64_t>(guest.object,0x490)==0x9876543211234567ull);
    CHECK(At<unsigned>(guest.object,guest.contract->context+4)==1);
    const auto setOnce=setters,enterOnce=entries;CHECK(ApplyNativeAction(guest.lease,bytes,action)==Result::applied);
    CHECK(setters==setOnce && entries==enterOnce); // duplicate accepted action cannot restart MAB
    const auto updateBefore=updates;guest.Invoke(1);CHECK(updates==updateBefore);host.Invoke(1);CHECK(updates==updateBefore+1);
    host.State(live,90,1.5f);CHECK(AfterNativeActionInput(host.lease));const auto jump=CapturePacket(host,bytes);
    CHECK(ApplyNativeAction(guest.lease,bytes,jump)==Result::applied);CHECK(setters==setOnce && entries==enterOnce);
    CHECK(At<unsigned>(guest.object,guest.contract->context+4)==90);guest.Invoke(1);CHECK(updates==updateBefore+1);
    if(family==2){const auto once=effects;CHECK(RestoredTimeShip(*Find(guest.lease)));CHECK(ApplyNativeAction(guest.lease,bytes,jump)==Result::applied);CHECK(effects==once);}
    host.State(live,1,0.01f);CHECK(AfterNativeActionInput(host.lease));const auto restart=CapturePacket(host,bytes);
    CHECK(ApplyNativeAction(guest.lease,bytes,restart)==Result::applied);CHECK(setters==setOnce+1 && entries==enterOnce+1);
    // Corrupt/noncanonical wires never become an ACK-worthy native application.
    bytes[7]=1;CHECK(ApplyNativeAction(guest.lease,bytes,restart)==Result::failed);bytes[7]=0;
    CHECK(ApplyNativeAction(host.lease,bytes,restart)==Result::failed);
    // Complete preflight of a whole batch: a read-only final page means pending
    // with no entry effects or typed prefix writes.
    host.State(live,20,0.5f);CHECK(AfterNativeActionInput(host.lease));const auto pending=CapturePacket(host,bytes);
    DWORD old=0;CHECK(VirtualProtect(guest.object,0x4000,PAGE_READONLY,&old)!=0);Sleep(20);
    const auto pendingSetters=setters;CHECK(ApplyNativeAction(guest.lease,bytes,pending)==Result::pending);CHECK(setters==pendingSetters);
    DWORD ignored=0;CHECK(VirtualProtect(guest.object,0x4000,old,&ignored)!=0);Sleep(20);
    CHECK(ApplyNativeAction(guest.lease,bytes,pending)==Result::applied);CHECK(setters==pendingSetters);
    CHECK(RestoreNativeActions(guest.lease));CHECK(At<const void*>(guest.object+guest.contract->machine,0)==image+guest.contract->machineVtable);
}
void Throughput(unsigned rttMs) {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));unsigned received=0,sent=0;unsigned char bytes[512]{};
    unsigned nextCapture=0;const auto before=setters;
    for(unsigned frame=0;frame<1800;++frame) {
        host.State(frame&1,1,0.016f);Put<std::uint64_t>(host.object,0x3E8,0x1122334455667788ull);Put<std::uint64_t>(host.object,0x490,0x8877665544332211ull);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));++sent;
        const unsigned now=frame*1000/60;
        if(now<nextCapture)continue;
        std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));
        if(used){received+=bytes[5];expectedSemantic=99;
            // Setter expectation is checked per semantic in this multi-step batch.
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);}
        nextCapture=now+rttMs;
    }
    for(;;){std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));if(!used)break;received+=bytes[5];CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);}
    CHECK(received==sent);CHECK(setters-before==sent);expectedSemantic=0;
}
void StockDamagePending() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));host.State(1,1,0.016f);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};const auto used=CapturePacket(host,bytes);const auto before=setters;
    Put<const void*>(guest.object+guest.contract->machine,0x18,image+0x36CE90);
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::pending);CHECK(setters==before);
    const auto beforeUpdate=updates;guest.Invoke(1);CHECK(updates==beforeUpdate+1); // stock flinch keeps progressing
    guest.State(0,1,0.016f);expectedSemantic=1;
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(setters==before+1);expectedSemantic=0;
    Put<const void*>(guest.object+guest.contract->machine,0x18,image+0x36C2D0);
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed); // death is not a retryable flinch
}
void AppliedBodyDamageProgresses() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));unsigned char bytes[512]{};
    auto used=CapturePacket(host,bytes);CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);
    Put<const void*>(guest.object+guest.contract->machine,0x18,image+0x36CE90);Put<unsigned>(guest.object,guest.contract->context+4,10);
    Put<float>(guest.object,guest.contract->context,1.0f);
    CHECK(BeforeNativeActionInput(guest.lease,false,&input));CHECK(At<unsigned>(guest.object,guest.contract->context+4)==10);
    guest.Invoke(1);CHECK(AfterNativeActionInput(guest.lease));CHECK(At<unsigned>(guest.object,guest.contract->context+4)==11);
    host.State(1,1,0.016f);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));used=CapturePacket(host,bytes);
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::pending);CHECK(At<unsigned>(guest.object,guest.contract->context+4)==11);
    guest.State(0,1,0.016f);expectedSemantic=1;CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);expectedSemantic=0;
}
void DamageReturnDoesNotRestartMab() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));unsigned char bytes[512]{};
    auto used=CapturePacket(host,bytes);CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);
    for(auto* actor:{&host,&guest}) {
        Put<const void*>(actor->object+actor->contract->machine,0x18,image+0x36CE90);actor->Invoke(2);
        actor->State(0,0,0);actor->Invoke(0); // original damage state naturally returned to normal once
    }
    CHECK(AfterNativeActionInput(host.lease));used=CapturePacket(host,bytes);const auto before=setters;
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(setters==before); // no second idle MAB/state0
}
void FirstSameSemanticAcceptance() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));
    // First host observation itself can be a newly accepted same-callback enter.
    // A cold preexisting baseline is entryKnown=false; this one is true.
    host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};const auto used=CapturePacket(host,bytes);const auto before=setters;expectedSemantic=0;
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(setters==before+1);
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(setters==before+1);
}
void WholeBatchPending() {
    Fixture host(0),guest(0);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));
    host.State(5,1,0.016f);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};const auto used=CapturePacket(host,bytes);CHECK(bytes[5]==2);
    const auto before=setters;resolvedTarget=nullptr;
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::pending);CHECK(setters==before);
    resolvedTarget=guest.legs+0x400;expectedSemantic=5;
    CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(setters==before+1);expectedSemantic=0;
}
void StompTargetLifetime() {
    for(unsigned scenario=0;scenario<4;++scenario) {
        Fixture host(0),guest(0);Input input{};
        CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
        CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));
        unsigned char bytes[512]{};const auto baseline=CapturePacket(host,bytes);
        CHECK(ApplyNativeAction(guest.lease,bytes,baseline)==Result::applied);
        host.State(5,1,0.016f);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
        const auto used=CapturePacket(host,bytes);expectedSemantic=5;
        resolvedTarget=guest.legs+0x400;auto* targetControl=guest.legs+0x700;
        const auto before=setters,pins=targetPins,releases=targetReleases;
        if(scenario==0) {
            // The real 325350 target gate accepts dead actors which still have
            // an allocation. Mark's alive-only resolver would strand this event.
            guest.legs[0x418]|=4;
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);
            CHECK(setters==before+1 && targetPins==pins+1 && targetReleases==releases+1);
            CHECK(At<LONG>(targetControl,8)==1 && NativeActionsEmpty(guest.lease));
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);
            CHECK(setters==before+1 && targetPins==pins+1 && targetReleases==releases+1);
        } else if(scenario==1) {
            // Registry-known expiration is not a successful stomp: its native
            // null-target branch would return without starting the leg action.
            expiredTarget=true;
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);
            CHECK(setters==before && targetPins==pins && targetReleases==releases);
            CHECK(At<LONG>(targetControl,8)==1 && NativeActionsEmpty(guest.lease));expiredTarget=false;
        } else if(scenario==2) {
            // A cleanup preflight refusal leaves exactly one retained strong
            // reference. Restore may safely retry only that unconsumed release.
            blockTargetRelease=true;
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);
            CHECK(setters==before+1 && targetPins==pins+1 && targetReleases==releases);
            CHECK(At<LONG>(targetControl,8)==2 && !NativeActionsEmpty(guest.lease));
            bool authority=true;CHECK(NativeActionRole(guest.object,&authority) && !authority);
            CHECK(QuiesceNativeActions(guest.lease));CHECK(!RestoreNativeActions(guest.lease));
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);CHECK(setters==before+1);
            blockTargetRelease=false;CHECK(RestoreNativeActions(guest.lease));
            CHECK(targetReleases==releases+1 && At<LONG>(targetControl,8)==1);
            CHECK(!NativeActionRole(guest.object,&authority));
        } else {
            // Once native release has entered, an exception is uncertain even
            // if the count looks recoverable. Never decrement it a second time.
            failTargetRelease=true;
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);failTargetRelease=false;
            CHECK(setters==before+1 && targetPins==pins+1 && targetReleases==releases+1);
            CHECK(At<LONG>(targetControl,8)==1 && !NativeActionsEmpty(guest.lease));
            CHECK(!RestoreNativeActions(guest.lease));CHECK(!AbortNativeActions(guest.lease));
            CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);
            CHECK(setters==before+1 && targetReleases==releases+1);
            auto* binding=Find(guest.lease);CHECK(binding && binding->targetsUncertain && !binding->targetsHeld);
            bool authority=true;CHECK(NativeActionRole(guest.object,&authority) && !authority);
            // Test-only disposal of the intentionally terminal fake exception.
            // Production retains the role and journal for an uncertain release.
            if(binding)binding->targetsUncertain=false;
        }
        expectedSemantic=0;resolvedTarget=nullptr;
    }
}
bool extensionAttachLarge=false,extensionWantEmit=false,extensionRestoreAllowed=true,extensionQuiesceAllowed=true;
const void* extensionOwner=nullptr;unsigned extensionQueued=0,extensionApplied=0,extensionBefore=0,extensionQuiesced=0,extensionWitness=0;std::size_t extensionCapacity=0;
constexpr std::uint32_t extensionMagic=0x58545845;
bool ExtensionNeeds(const NativeLease& l) noexcept {return l.profile==profiles+3 || (extensionAttachLarge && l.profile==profiles+1);}
bool ExtensionBefore(const NativeLease& l,bool authority,Input*) noexcept {
    bool role=false;if(!NativeActionRole(l.object,&role) || role!=authority)return false;++extensionBefore;return true;
}
bool ExtensionAfter(const NativeLease& l) noexcept {
    bool authority=false;if(!NativeActionRole(l.object,&authority))return false;
    if(authority && extensionWantEmit){extensionOwner=l.object;++extensionQueued;extensionWantEmit=false;}return true;
}
bool ExtensionCapture(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    extensionCapacity=capacity;*used=0;if(extensionOwner!=l.object || !extensionQueued)return true;if(capacity<8)return false;
    auto* p=static_cast<unsigned char*>(bytes);PutWire(p,extensionMagic,4);PutWire(p,extensionQueued,4);extensionQueued=0;*used=8;return true;
}
bool ExtensionOwns(const void* bytes,std::size_t size) noexcept {
    if(!bytes || size<4)return false;auto p=static_cast<const unsigned char*>(bytes);return GetWire(p,4)==extensionMagic;
}
Result ExtensionApply(const NativeLease&,const void* bytes,std::size_t size) noexcept {
    if(!ExtensionOwns(bytes,size) || size!=8)return Result::failed;++extensionApplied;return Result::applied;
}
bool ExtensionQuiesce(const NativeLease&) noexcept {++extensionQuiesced;return extensionQuiesceAllowed;}
bool ExtensionWitnessQuiesce(const NativeLease&) noexcept {++extensionWitness;return true;}
bool ExtensionEmpty(const NativeLease& l) noexcept {return extensionOwner!=l.object || !extensionQueued;}
bool ExtensionAbort(const NativeLease& l) noexcept {if(extensionOwner==l.object)extensionQueued=0;return true;}
bool ExtensionRestore(const NativeLease& l) noexcept {
    if(!extensionRestoreAllowed || !ExtensionEmpty(l))return false;if(extensionOwner==l.object)extensionOwner=nullptr;return true;
}
void ExtensionRolesAndFairness() {
    const NativeActionExtension ext{extensionMagic,&ExtensionNeeds,&ExtensionBefore,&ExtensionAfter,&ExtensionCapture,&ExtensionOwns,&ExtensionApply,
        &ExtensionQuiesce,&ExtensionEmpty,&ExtensionAbort,&ExtensionRestore};
    CHECK(RegisterNativeActionExtension(ext));CHECK(RegisterNativeActionExtension(ext));
    {
        Fixture host(1),guest(1);host.lease.profile=profiles+3;guest.lease.profile=profiles+3;
        const auto untouched=At<const void*>(host.object+host.contract->machine,0);Input input{};
        CHECK(NeedsNativeActions(host.lease));CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
        CHECK(At<const void*>(host.object+host.contract->machine,0)==untouched); // role-only, no unrelated vtable patch
        extensionWantEmit=true;CHECK(AfterNativeActionInput(host.lease));CHECK(!NativeActionsEmpty(host.lease));
        unsigned char bytes[1024]{};std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));CHECK(used==8 && extensionCapacity==512);
        const auto before=extensionApplied;CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);CHECK(extensionApplied==before+1);
        CHECK(QuiesceNativeActions(host.lease));CHECK(NativeActionQuiescing(host.object));
        extensionWantEmit=true;CHECK(AfterNativeActionInput(host.lease));CHECK(NativeActionsEmpty(host.lease));extensionWantEmit=false;
        extensionRestoreAllowed=false;CHECK(!RestoreNativeActions(host.lease));bool role=false;CHECK(NativeActionRole(host.object,&role) && role);
        extensionRestoreAllowed=true;CHECK(RestoreNativeActions(host.lease));CHECK(!NativeActionRole(host.object,&role));CHECK(RestoreNativeActions(guest.lease));
    }
    {
        extensionAttachLarge=true;Fixture host(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));
        extensionWantEmit=true;CHECK(AfterNativeActionInput(host.lease));unsigned char bytes[512]{};std::size_t used=0;
        CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used) && used && !ExtensionOwns(bytes,used));
        // Root and extension were both queued; the next producer gets its turn.
        CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used) && used && ExtensionOwns(bytes,used));
        CHECK(QuiesceNativeActions(host.lease));CHECK(NativeActionsEmpty(host.lease));CHECK(RestoreNativeActions(host.lease));extensionAttachLarge=false;
    }
}
void QuiesceEveryProducer() {
    const NativeActionExtension ext{extensionMagic+1,&ExtensionNeeds,&ExtensionBefore,nullptr,&ExtensionCapture,&ExtensionOwns,&ExtensionApply,
        &ExtensionWitnessQuiesce,&ExtensionEmpty,&ExtensionAbort,&ExtensionRestore};
    CHECK(RegisterNativeActionExtension(ext));Fixture actor(1);actor.lease.profile=profiles+3;Input input{};
    CHECK(BeforeNativeActionInput(actor.lease,true,&input));extensionQuiesceAllowed=false;
    const auto before=extensionWitness;CHECK(!QuiesceNativeActions(actor.lease));CHECK(extensionWitness==before+1);
    extensionQuiesceAllowed=true;CHECK(QuiesceNativeActions(actor.lease));CHECK(extensionWitness==before+2);CHECK(RestoreNativeActions(actor.lease));
}
void DeadGuestCannotEnter() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));host.State(0,0,0);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};const auto used=CapturePacket(host,bytes);CHECK(QuiesceNativeActions(guest.lease));guest.object[0x2E8]=1;
    const auto before=setters;CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::failed);CHECK(setters==before);
}
void ClosingDrain() {
    Fixture host(1),guest(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(BeforeNativeActionInput(guest.lease,false,&input));
    CHECK(AfterNativeActionInput(guest.lease));CHECK(AfterNativeActionInput(host.lease));
    host.State(1,1,0.016f);host.Invoke(0);CHECK(AfterNativeActionInput(host.lease));
    CHECK(QuiesceNativeActions(host.lease));CHECK(QuiesceNativeActions(guest.lease));CHECK(NativeActionQuiescing(host.object));
    bool authority=false;CHECK(NativeActionRole(host.object,&authority) && authority);
    CHECK(!NativeActionsEmpty(host.lease));CHECK(!RestoreNativeActions(host.lease));
    const auto beforeUpdate=updates;host.Invoke(1);CHECK(updates==beforeUpdate); // no future main trigger
    host.State(1,200,3.4f);CHECK(AfterNativeActionInput(host.lease)); // no new periodic sample while draining
    unsigned char bytes[512]{};expectedSemantic=1;
    for(;;){std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));if(!used)break;CHECK(ApplyNativeAction(guest.lease,bytes,used)==Result::applied);}
    CHECK(NativeActionsEmpty(host.lease));CHECK(RestoreNativeActions(host.lease));CHECK(RestoreNativeActions(guest.lease));expectedSemantic=0;
}
void QuiescedStockDamageCanFinish() {
    Fixture host(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));
    Put<const void*>(host.object+host.contract->machine,0x18,image+0x36CE90);
    CHECK(QuiesceNativeActions(host.lease));host.Invoke(2);host.State(0,0,0);host.Invoke(0);
    CHECK(NativeActionsEmpty(host.lease));CHECK(RestoreNativeActions(host.lease)); // natural stock cleanup is not a new player action
}
void DeadBodyCachedDrain() {
    Fixture host(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(AfterNativeActionInput(host.lease));
    CHECK(QuiesceNativeActions(host.lease));host.object[0x2E8]=1;bool role=false;CHECK(NativeActionRole(host.object,&role) && role);
    CHECK(!NativeActionsEmpty(host.lease));unsigned char bytes[512]{};std::size_t used=0;
    CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used) && used);CHECK(NativeActionsEmpty(host.lease));
    CHECK(RestoreNativeActions(host.lease)); // bound, dead body metadata can drain without a new native skill entry
}
void ReusedAddressRoleAndGate() {
    Fixture actor(1);Input input{};CHECK(BeforeNativeActionInput(actor.lease,true,&input));CHECK(AfterNativeActionInput(actor.lease));
    CHECK(QuiesceNativeActions(actor.lease));NativeLease old=actor.lease;
    auto* replacement=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(replacement!=nullptr);
    Put<LONG>(actor.control,8,0);Put<LONG>(replacement,8,1);Put<const void*>(actor.object,0x30,replacement);
    Put<const void*>(actor.object+actor.contract->machine,0,image+actor.contract->machineVtable);actor.lease.control=replacement;
    CHECK(BeforeNativeActionInput(actor.lease,false,&input));bool authority=true;
    CHECK(NativeActionRole(actor.object,&authority) && !authority);CHECK(!NativeActionQuiescing(actor.object));
    const auto before=updates;const auto phase=At<unsigned>(actor.object,actor.contract->context+4);actor.Invoke(1);
    CHECK(updates==before && At<unsigned>(actor.object,actor.contract->context+4)==phase); // new guest role, not old authority
    CHECK(AbortNativeActions(old));CHECK(RestoreNativeActions(old));CHECK(RestoreNativeActions(actor.lease));VirtualFree(replacement,0,MEM_RELEASE);
}
void FullFinalRecordDrain() {
    Fixture host(1);Input input{};CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(AfterNativeActionInput(host.lease));
    for(unsigned i=1;i<kQueued;++i){host.State(i&1,1,0.016f);CHECK(AfterNativeActionInput(host.lease));}
    host.State(0,1,0.016f);host.Invoke(0);CHECK(!AfterNativeActionInput(host.lease)); // 65th actual accepted state retained separately
    CHECK(QuiesceNativeActions(host.lease));CHECK(!NativeActionsEmpty(host.lease));CHECK(!RestoreNativeActions(host.lease));
    unsigned received=0;unsigned char bytes[1024]{};
    for(;;){std::size_t used=0;CHECK(CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used));CHECK(used<=512);if(!used)break;received+=bytes[5];}
    CHECK(received==65);CHECK(NativeActionsEmpty(host.lease));CHECK(RestoreNativeActions(host.lease));
}
void AdmissionAndOverflow() {
    Fixture host(1);Input input{};image[kSigs[0].rva]^=1;CHECK(!BeforeNativeActionInput(host.lease,true,&input));image[kSigs[0].rva]^=1;
    CHECK(BeforeNativeActionInput(host.lease,true,&input));CHECK(AfterNativeActionInput(host.lease));
    for(unsigned i=1;i<kQueued;++i){host.State(i&1,1,0.01f);CHECK(AfterNativeActionInput(host.lease));}
    host.State(0,1,0.01f);CHECK(!AfterNativeActionInput(host.lease));
    unsigned char bytes[512]{};std::size_t used=99;CHECK(!CaptureNativeAction(host.lease,bytes,sizeof(bytes),&used) && used==0);
    CHECK(!RestoreNativeActions(host.lease));CHECK(!NativeActionsEmpty(host.lease));
    CHECK(AbortNativeActions(host.lease)); // simulated host fault deadline retires this failed lease, never normal Exit
    CHECK(RestoreNativeActions(host.lease));CHECK(RestoreNativeActions(host.lease));
}
}
namespace crew {
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(!object || !out || !Readable(object,0x38) || !At<const void*>(object,0x30))return false;
    std::memset(out,0,32);out[0]=0x42;return true;
}
bool ResolveMarkIdentities(const unsigned char*,const unsigned char id[32],MarkIdentities* out) noexcept {
    if(!out || !id || id[0]!=0x42 || !resolvedTarget)return false;
    const auto* target=static_cast<const unsigned char*>(resolvedTarget);if(target[0x18]&4)return false;*out={};out->target={target,At<const void*>(target,0x30)};return true;
}
}
namespace crew::enemy_native {
bool ResolveNativeProjectileActor(const unsigned char id[32],void** object,const void** control) noexcept {
    *object=nullptr;*control=nullptr;if(expiredTarget)return true;if(!resolvedTarget || id[0]!=0x42)return false;
    auto* actor=static_cast<unsigned char*>(const_cast<void*>(resolvedTarget));auto* c=At<unsigned char*>(actor,0x30);
    if(!c || At<LONG>(c,8)<=0)return false;InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+8));++targetPins;*object=actor;*control=c;return true;
}
bool ReleaseNativeProjectileLock(void*,const void* control,bool* consumed) noexcept {
    if(consumed)*consumed=false;if(blockTargetRelease)return false;
    if(!control)return true;if(consumed)*consumed=true;InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(control))+8));++targetReleases;return !failTargetRelease;
}
bool InstallDeiroiAcceptedActions() noexcept {return true;}
bool QuiesceDeiroiAcceptedActions(const NativeLease&) noexcept {return true;}
bool DeiroiAcceptedActionsEmpty(const NativeLease&) noexcept {return true;}
bool AbortDeiroiAcceptedActions(const NativeLease&) noexcept {return true;}
bool CaptureDeiroiAcceptedAction(const NativeLease&,void*,std::size_t,std::size_t* used) noexcept {if(used)*used=0;return true;}
bool OwnsDeiroiAcceptedAction(const void*,std::size_t) noexcept {return false;}
enemy_control::ActionResult ApplyDeiroiAcceptedAction(const NativeLease&,const void*,std::size_t) noexcept {return enemy_control::ActionResult::failed;}
bool RestoreDeiroiAcceptedActions(const NativeLease&) noexcept {return true;}
const NativeProfile* Profiles(std::size_t* count) noexcept {if(count)*count=4;return profiles;}
void* ActionTestFunction(unsigned rva) noexcept {
    if(rva==0x321300)return reinterpret_cast<void*>(&SetOrdinal);
    if(rva==0x3F01F0)return reinterpret_cast<void*>(&VisibilitySpy);
    if(rva==0x369F20 || rva==0x3F00D0)return reinterpret_cast<void*>(&SetDescriptor);
    if(rva==0x31E790 || rva==0x367930 || rva==0x3EDF30)return reinterpret_cast<void*>(&DispatchSpy);
    return nullptr;
}
}
void UnreadableActionRestoration() {
    for(unsigned family=0;family<3;++family) {
        Fixture actor(family);Input input{};
        CHECK(BeforeNativeActionInput(actor.lease,false,&input));CHECK(AfterNativeActionInput(actor.lease));
        const auto* table=At<const void* const*>(actor.object+actor.contract->machine,0);
        const auto dispatch=table[1];
        for(void* page:{static_cast<void*>(actor.object),static_cast<void*>(actor.control)}) {
            DWORD protection=0,ignored=0;
            CHECK(VirtualProtect(page,0x1000,PAGE_NOACCESS,&protection)!=0);Sleep(20);
            CHECK(!RestoreNativeActions(actor.lease));CHECK(Find(actor.lease)!=nullptr);
            CHECK(table[1]==dispatch);
            CHECK(VirtualProtect(page,0x1000,protection,&ignored)!=0);Sleep(20);
            CHECK(At<const void*>(actor.object+actor.contract->machine,0)==table);
        }
        const auto previous=updates;actor.Invoke(1);CHECK(updates==previous);
        CHECK(RestoreNativeActions(actor.lease));CHECK(!Find(actor.lease));
        CHECK(At<const void*>(actor.object+actor.contract->machine,0)==image+actor.contract->machineVtable);
    }
}
int main() {
    BuildImage();for(unsigned i=0;i<3;++i)Lifecycle(i);Throughput(50);Throughput(100);Throughput(250);StockDamagePending();AppliedBodyDamageProgresses();DamageReturnDoesNotRestartMab();FirstSameSemanticAcceptance();WholeBatchPending();StompTargetLifetime();ClosingDrain();QuiescedStockDamageCanFinish();DeadBodyCachedDrain();ReusedAddressRoleAndGate();FullFinalRecordDrain();AdmissionAndOverflow();ExtensionRolesAndFairness();QuiesceEveryProducer();DeadGuestCannotEnter();
    UnreadableActionRestoration();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("enemy main accepted action fixture: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
