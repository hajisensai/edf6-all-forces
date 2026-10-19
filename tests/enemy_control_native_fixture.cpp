// Executes the production enemy pad/lease adapter against synthetic memory and
// a callback at its admitted RVA. No EDF.dll, game process or copied scope logic.
// Native signatures/RTTI are populated before admission; afterwards an executable
// test trampoline replaces one input body to observe the real RunInput finally.
#include "../src/enemy_control_native.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

namespace crew { unsigned char* image=nullptr; }
namespace {
using namespace crew;
using namespace crew::enemy_native;
int checks=0,failures=0;
void Check(bool value,const char* expression,int line) {
    ++checks;
    if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}
}
#define CHECK(value) Check((value),#value,__LINE__)
float PadFloat(const unsigned char* pad,std::size_t offset) {
    float value=0;std::memcpy(&value,pad+offset,sizeof(value));return value;
}
struct TestFingerprint { std::uint32_t rva;unsigned char bytes[16]; };
constexpr TestFingerprint signatures[]={
#include "../src/enemy_control_native_signatures.inc"
};
struct Fixture {
    static constexpr std::size_t kObjectBytes=0x3000;
    unsigned char* object=static_cast<unsigned char*>(VirtualAlloc(nullptr,kObjectBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    static constexpr std::size_t kControlBytes=0x20;
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,kControlBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* replacementControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,kControlBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Fixture(){if(!object || !control || !replacementControl)std::exit(2);}
    ~Fixture(){VirtualFree(object,0,MEM_RELEASE);VirtualFree(control,0,MEM_RELEASE);VirtualFree(replacementControl,0,MEM_RELEASE);}
    unsigned char oldPad[0x200]{},replacementPad[0x200]{};
    const NativeProfile* profile=nullptr;
    void Reset(const NativeProfile& selected) {
        std::memset(object,0xA5,kObjectBytes);
        std::memset(control,0,kControlBytes);std::memset(replacementControl,0,kControlBytes);
        profile=&selected;
        Put<const void*>(object,0,image+selected.vtableRva);
        Put<const void*>(object,0x30,control);Put<LONG>(control,8,1);Put<LONG>(replacementControl,8,1);
        object[0x18]=0;object[0x2E8]=0;object[0x354]=6;Put<const void*>(object,0x340,oldPad);
    }
    NativeLease Lease() { NativeLease lease{};CHECK(Capture(object,&lease));return lease; }
};
const NativeProfile& Find(const char* name) {
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(std::strcmp(profiles[i].name,name)==0)return profiles[i];
    std::fprintf(stderr,"missing fixture profile %s\n",name);std::exit(2);
}
void BuildImage() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)std::exit(2);
    for(const auto& signature:signatures)std::memcpy(image+signature.rva,signature.bytes,sizeof(signature.bytes));
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i) {
        const auto& p=profiles[i];
        const auto colRva=static_cast<std::uint32_t>(0x10000+i*0x100);
        const auto tdRva=colRva+0x40;
        unsigned char* const col=image+colRva;
        Put<const void*>(image+p.vtableRva-8,0,col);
        Put<std::uint32_t>(col,0,1);Put<std::uint32_t>(col,4,0);
        Put<std::uint32_t>(col,12,tdRva);Put<std::uint32_t>(col,20,colRva);
        std::snprintf(reinterpret_cast<char*>(image+tdRva+16),0xC0,".?AV%s@@",p.name);
        Put<const void*>(image+p.vtableRva,static_cast<std::size_t>(p.inputSlot)*8,image+p.inputRva);
        Put<const void*>(image+p.vtableRva,kAiSlot*8,image+p.aiRva);
    }
}
void PadCases() {
    alignas(16) unsigned char pad[kPadBytes];
    Input input{};input.right=0.25f;input.forward=-0.75f;input.yaw=0.5f;input.pitch=-0.125f;input.vertical=0.375f;
    input.held=(1u<<0)|(1u<<4)|(1u<<15);input.pressed=(1u<<1)|(1u<<4)|(1u<<14);
    std::memset(pad,0xFF,sizeof(pad));BuildPad(input,pad);
    CHECK(PadFloat(pad,0x0C)==0.25f);CHECK(PadFloat(pad,0x24)==-0.75f);
    CHECK(PadFloat(pad,0x3C)==0.5f);CHECK(PadFloat(pad,0x54)==-0.125f);
    CHECK(PadFloat(pad,0xCC)==0.375f);CHECK(PadFloat(pad,0xE4)==0.0f);
    for(unsigned i=0;i<kButtonCount;++i) {
        const std::size_t at=0x70+static_cast<std::size_t>(i)*0x18;
        CHECK(pad[at]==((input.held>>i)&1));CHECK(pad[at+1]==((input.pressed>>i)&1));
    }
    CHECK(pad[0]==0 && pad[0x6F]==0 && pad[kPadBytes-1]==0);
    input={};input.right=4;input.forward=-5;input.yaw=std::numeric_limits<float>::infinity();
    input.pitch=std::numeric_limits<float>::quiet_NaN();input.vertical=-3;
    BuildPad(input,pad);
    CHECK(PadFloat(pad,0x0C)==1 && PadFloat(pad,0x24)==-1);
    CHECK(PadFloat(pad,0x3C)==0 && PadFloat(pad,0x54)==0);
    CHECK(PadFloat(pad,0xCC)==0 && PadFloat(pad,0xE4)==1);
    input.vertical=std::numeric_limits<float>::quiet_NaN();BuildPad(input,pad);
    CHECK(PadFloat(pad,0xCC)==0 && PadFloat(pad,0xE4)==0);
    for(unsigned i=0;i<kButtonCount;++i)CHECK(pad[0x70+static_cast<std::size_t>(i)*0x18]==0);
}
void AdmissionCases(Fixture& fixture) {
    const auto& profile=Find("GiantAnt");fixture.Reset(profile);
    NativeLease lease{};
    image[signatures[0].rva]^=1;
    CHECK(!Capture(fixture.object,&lease) && !lease.object && !lease.control && !lease.profile);
    image[signatures[0].rva]^=1;
    CHECK(Capture(fixture.object,&lease) && lease.profile==&profile && lease.control==fixture.control);
    CHECK(!Capture(fixture.object,nullptr));
    Put<const void*>(fixture.object,0,image+profile.vtableRva+8);CHECK(!Profile(fixture.object));
    Put<const void*>(fixture.object,0,image+profile.vtableRva);
    const auto col=const_cast<unsigned char*>(At<const unsigned char*>(image+profile.vtableRva-8,0));
    Put<std::uint32_t>(col,4,8);CHECK(!Profile(fixture.object));Put<std::uint32_t>(col,4,0);
    const auto td=image+At<std::uint32_t>(col,12);td[20]^=1;CHECK(!Profile(fixture.object));td[20]^=1;
    const auto old=At<const void*>(image+profile.vtableRva,kAiSlot*8);
    Put<const void*>(image+profile.vtableRva,kAiSlot*8,image+1);CHECK(!Profile(fixture.object));
    Put<const void*>(image+profile.vtableRva,kAiSlot*8,old);
    Put<LONG>(fixture.control,8,0);CHECK(!Capture(fixture.object,&lease) && !lease.object);
    Put<LONG>(fixture.control,8,1);fixture.object[0x18]=4;CHECK(!Capture(fixture.object,&lease));
    fixture.object[0x18]=0;fixture.object[0x2E8]=1;CHECK(!Capture(fixture.object,&lease));
    fixture.object[0x2E8]=0;CHECK(Capture(fixture.object,&lease));
}
enum class CallbackMode { observe,raise,handoverPad,handoverPlayer,reuseAddress,readOnly,noAccessSelf,noAccessControl,dead,flagged,readOnlyDead };
Fixture* current=nullptr;
CallbackMode callbackMode=CallbackMode::observe;
unsigned callbackCalls=0;
void* expectedDt=nullptr;
std::uint64_t expectedSeed=0,expectedVariantSeed=0;
unsigned char expectedHeld=1,expectedPressed=1;
std::uint64_t __fastcall ObserveNative(void* object,void* dt) {
    ++callbackCalls;
    CHECK(current && object==current->object && dt==expectedDt);
    const auto pad=At<const unsigned char*>(object,0x340);
    CHECK(pad!=current->oldPad && Readable(pad,kPadBytes));
    CHECK(At<unsigned char>(object,0x354)==1);
    CHECK(At<std::uint64_t>(object,0x3E8)==expectedSeed);
    CHECK(At<std::uint64_t>(object,0x490)==expectedVariantSeed);
    CHECK(PadFloat(pad,0x0C)==0.25f && PadFloat(pad,0x24)==-0.5f);
    CHECK(PadFloat(pad,0x3C)==0.75f && PadFloat(pad,0x54)==-1.0f);
    CHECK(PadFloat(pad,0xCC)==0 && PadFloat(pad,0xE4)==0.5f);
    CHECK(pad[0x70+4*0x18]==expectedHeld && pad[0x70+4*0x18+1]==expectedPressed);
    if(callbackMode==CallbackMode::raise)RaiseException(0xE0123456,0,0,nullptr);
    if(callbackMode==CallbackMode::handoverPad) {
        Put<const void*>(object,0x340,current->replacementPad);Put<unsigned char>(object,0x354,3);
    }
    if(callbackMode==CallbackMode::handoverPlayer)Put<unsigned char>(object,0x354,2);
    if(callbackMode==CallbackMode::reuseAddress) {
        Put<const void*>(object,0x30,current->replacementControl);
        Put<const void*>(object,0x340,current->replacementPad);Put<unsigned char>(object,0x354,4);
    }
    if(callbackMode==CallbackMode::dead || callbackMode==CallbackMode::readOnlyDead)
        Put<unsigned char>(object,0x2E8,1);
    if(callbackMode==CallbackMode::flagged)
        Put<unsigned char>(object,0x18,At<unsigned char>(object,0x18)|4u);
    if(callbackMode==CallbackMode::readOnly || callbackMode==CallbackMode::readOnlyDead) {
        DWORD was=0;CHECK(VirtualProtect(object,0x1000,PAGE_READONLY,&was)!=0);
        // Expire the real memory primitive's per-tick VirtualQuery cache.
        Sleep(20);
    }
    if(callbackMode==CallbackMode::noAccessSelf || callbackMode==CallbackMode::noAccessControl) {
        void* const target=callbackMode==CallbackMode::noAccessSelf ? object : current->control;
        DWORD was=0;CHECK(VirtualProtect(target,0x1000,PAGE_NOACCESS,&was)!=0);Sleep(20);
    }
    return 0xC0FFEE1234567890ull;
}
void InstallFunction(std::uint32_t rva,InputFn function) {
    unsigned char code[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(function);
    std::memcpy(code+2,&address,sizeof(address));std::memcpy(image+rva,code,sizeof(code));
    CHECK(FlushInstructionCache(GetCurrentProcess(),image+rva,sizeof(code))!=0);
}
void InstallCallback(const NativeProfile& profile) { InstallFunction(profile.inputRva,&ObserveNative); }
void ScopeCases(Fixture& fixture) {
    const auto& profile=Find("GiantAnt");fixture.Reset(profile);auto lease=fixture.Lease();
    InstallCallback(profile);current=&fixture;expectedDt=fixture.oldPad;
    const auto original=reinterpret_cast<InputFn>(image+profile.inputRva);
    Input input{};input.right=.25f;input.forward=-.5f;input.yaw=.75f;input.pitch=-2;input.vertical=-.5f;
    input.held=input.pressed=1u<<4;expectedPressed=1;input.seed=expectedSeed=expectedVariantSeed=0x1122334455667788ull;
    std::uint64_t result=0;
    CHECK(RunInput(lease,input,original,expectedDt,&result));
    CHECK(result==0xC0FFEE1234567890ull && callbackCalls==1);
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    CHECK(!lease.borrowed && lease.previousHeld==(1u<<4));
    // A continued held skill must preserve native RNG progress between edges.
    input.pressed=0;expectedPressed=0;input.seed=0xDEADBEEFull;expectedSeed=0x9988776655443322ull;
    Put<std::uint64_t>(fixture.object,0x3E8,expectedSeed);
    expectedVariantSeed=0x123456789ABCDEFull;Put<std::uint64_t>(fixture.object,0x490,expectedVariantSeed);
    CHECK(RunInput(lease,input,original,expectedDt,&result));
    CHECK(At<std::uint64_t>(fixture.object,0x3E8)==expectedSeed);
    CHECK(At<std::uint64_t>(fixture.object,0x490)==expectedVariantSeed);
    input.held=0;expectedHeld=0;input.seed=0xBAD0C0FFEEull;
    CHECK(RunInput(lease,input,original,expectedDt,&result));
    CHECK(At<std::uint64_t>(fixture.object,0x3E8)==expectedSeed);
    CHECK(At<std::uint64_t>(fixture.object,0x490)==expectedVariantSeed && lease.previousHeld==0);
    input.held=1u<<4;expectedHeld=1;
    input.pressed=1u<<4;expectedPressed=1;input.seed=expectedSeed=expectedVariantSeed=0x1122334455667788ull;
    callbackMode=CallbackMode::raise;result=7;
    CHECK(!RunInput(lease,input,original,expectedDt,&result) && result==0);
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    callbackMode=CallbackMode::handoverPlayer;
    CHECK(RunInput(lease,input,original,expectedDt,nullptr));
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==2);
    fixture.object[0x354]=6;callbackMode=CallbackMode::handoverPad;
    CHECK(RunInput(lease,input,original,expectedDt,&result));
    CHECK(At<const void*>(fixture.object,0x340)==fixture.replacementPad && fixture.object[0x354]==3);
    Put<const void*>(fixture.object,0x340,fixture.oldPad);fixture.object[0x354]=6;
    callbackMode=CallbackMode::reuseAddress;
    CHECK(RunInput(lease,input,original,expectedDt,&result));
    CHECK(At<const void*>(fixture.object,0x340)==fixture.replacementPad && fixture.object[0x354]==4);
    const unsigned before=callbackCalls;
    CHECK(!RunInput(lease,input,original,expectedDt,&result) && result==0 && callbackCalls==before);
    fixture.Reset(profile);lease=fixture.Lease();callbackMode=CallbackMode::observe;
    CHECK(!RunInput(lease,input,&ObserveNative,expectedDt,&result) && callbackCalls==before);
    CHECK(!RunInput(lease,input,nullptr,expectedDt,&result) && callbackCalls==before);
    Put<LONG>(fixture.control,8,0);CHECK(!RunInput(lease,input,original,expectedDt,&result) && callbackCalls==before);
    Put<LONG>(fixture.control,8,1);fixture.object[0x18]=4;CHECK(!RunInput(lease,input,original,expectedDt,&result));
    fixture.object[0x18]=0;fixture.object[0x2E8]=1;CHECK(!RunInput(lease,input,original,expectedDt,&result));
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    fixture.Reset(profile);lease=fixture.Lease();callbackMode=CallbackMode::readOnly;
    CHECK(!RunInput(lease,input,original,expectedDt,&result));
    CHECK(lease.borrowed && lease.object==fixture.object && lease.control==fixture.control);
    CHECK(At<const void*>(fixture.object,0x340)==lease.pad && lease.savedPad==fixture.oldPad);
    CHECK(fixture.object[0x354]==1 && lease.savedPlayer==6);
    CHECK(!Restore(&lease));
    CHECK(lease.borrowed && lease.object==fixture.object && lease.savedPad==fixture.oldPad);
    CHECK(At<const void*>(fixture.object,0x340)==lease.pad);
    DWORD was=0;CHECK(VirtualProtect(fixture.object,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
    CHECK(Restore(&lease));
    CHECK(!lease.object && !lease.control && !lease.profile && !lease.borrowed);
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    callbackMode=CallbackMode::observe;
    for(const auto mode:{CallbackMode::noAccessSelf,CallbackMode::noAccessControl}) {
        fixture.Reset(profile);lease=fixture.Lease();callbackMode=mode;
        CHECK(!RunInput(lease,input,original,expectedDt,&result));
        CHECK(lease.borrowed && lease.object==fixture.object && lease.control==fixture.control);
        CHECK(!Restore(&lease));
        CHECK(lease.borrowed && lease.object==fixture.object && lease.savedPad==fixture.oldPad);
        void* const target=mode==CallbackMode::noAccessSelf ? fixture.object : fixture.control;
        CHECK(VirtualProtect(target,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
        CHECK(At<const void*>(fixture.object,0x340)==lease.pad && fixture.object[0x354]==1);
        CHECK(Restore(&lease));
        CHECK(!lease.object && !lease.borrowed);
        CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    }
    // An unreadable self with a proven dead control may be forgotten without
    // reading/writing that object. Unknown memory above must retain the lease.
    fixture.Reset(profile);lease=fixture.Lease();callbackMode=CallbackMode::noAccessSelf;
    CHECK(!RunInput(lease,input,original,expectedDt,&result));
    const auto borrowedPad=lease.pad;
    Put<LONG>(fixture.control,8,0);CHECK(Restore(&lease));
    CHECK(!lease.object && !lease.borrowed);
    CHECK(VirtualProtect(fixture.object,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
    CHECK(At<const void*>(fixture.object,0x340)==borrowedPad && fixture.object[0x354]==1);
    // Model the documented interrupted-write state: controller pointer was
    // restored but player-mode restoration has not completed. Recovery must
    // finish the player byte rather than misclassifying this as a handover.
    fixture.Reset(profile);lease=fixture.Lease();lease.borrowed=true;lease.pointerRestored=true;
    lease.savedPad=fixture.oldPad;lease.savedPlayer=6;fixture.object[0x354]=1;
    CHECK(Restore(&lease));
    CHECK(!lease.object && !lease.borrowed && !lease.pointerRestored);
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    callbackMode=CallbackMode::observe;
}
void InactiveScopeCases(Fixture& fixture) {
    const auto& profile=Find("GiantAnt");
    const auto original=reinterpret_cast<InputFn>(image+profile.inputRva);
    Input input{};input.right=.25f;input.forward=-.5f;input.yaw=.75f;input.pitch=-2;input.vertical=-.5f;
    input.held=input.pressed=1u<<4;input.seed=expectedSeed=expectedVariantSeed=0x1122334455667788ull;
    expectedHeld=expectedPressed=1;current=&fixture;expectedDt=fixture.oldPad;
    for(const auto mode:{CallbackMode::dead,CallbackMode::flagged}) {
        fixture.Reset(profile);auto lease=fixture.Lease();callbackMode=mode;
        unsigned char expected[Fixture::kObjectBytes];std::memcpy(expected,fixture.object,sizeof(expected));
        Put<std::uint64_t>(expected,0x3E8,input.seed);Put<std::uint64_t>(expected,0x490,input.seed);
        expected[mode==CallbackMode::dead ? 0x2E8 : 0x18]=mode==CallbackMode::dead ? 1u : 4u;
        std::uint64_t result=0;CHECK(RunInput(lease,input,original,expectedDt,&result));
        CHECK(result==0xC0FFEE1234567890ull && !lease.borrowed);
        CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
        CHECK(std::memcmp(fixture.object,expected,sizeof(expected))==0);
        const unsigned before=callbackCalls;
        CHECK(!RunInput(lease,input,original,expectedDt,&result) && callbackCalls==before);
        CHECK(!Neutralize(lease));CHECK(Restore(&lease));
        CHECK(!lease.object && !lease.borrowed);
        // Inactive objects retain their native death/animation intents; only
        // the temporary controller ownership must be returned.
        CHECK(std::memcmp(fixture.object,expected,sizeof(expected))==0);
    }
    fixture.Reset(profile);auto lease=fixture.Lease();callbackMode=CallbackMode::readOnlyDead;
    unsigned char expected[Fixture::kObjectBytes];std::memcpy(expected,fixture.object,sizeof(expected));
    Put<std::uint64_t>(expected,0x3E8,input.seed);Put<std::uint64_t>(expected,0x490,input.seed);expected[0x2E8]=1;
    std::uint64_t result=0;CHECK(!RunInput(lease,input,original,expectedDt,&result));
    CHECK(lease.borrowed && lease.object==fixture.object && lease.control==fixture.control);
    CHECK(At<const void*>(fixture.object,0x340)==lease.pad && fixture.object[0x354]==1);
    CHECK(!Restore(&lease));CHECK(lease.borrowed && lease.savedPad==fixture.oldPad && lease.savedPlayer==6);
    DWORD was=0;CHECK(VirtualProtect(fixture.object,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
    CHECK(Restore(&lease));
    CHECK(!lease.object && !lease.borrowed);
    CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
    CHECK(std::memcmp(fixture.object,expected,sizeof(expected))==0);
    callbackMode=CallbackMode::observe;
}
void SeedWriteCases(Fixture& fixture) {
    // Straddle a page boundary so +3E8 remains writable while +490 is read-only.
    // SetSeed must preflight both native LCG states before changing either one.
    auto* const allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x5000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(allocation!=nullptr);if(!allocation)return;
    unsigned char* const object=allocation+0xB80;
    fixture.Reset(Find("GiantAnt"));
    std::memcpy(object,fixture.object,Fixture::kObjectBytes);
    NativeLease lease{object,fixture.profile,fixture.control};
    constexpr std::uint64_t primary=0x1122334455667788ull,variant=0x8877665544332211ull,next=0xA5A500000000FFFFull;
    Put<std::uint64_t>(object,0x3E8,primary);Put<std::uint64_t>(object,0x490,variant);
    DWORD was=0;CHECK(VirtualProtect(allocation+0x1000,0x1000,PAGE_READONLY,&was)!=0);Sleep(20);
    CHECK(!SetSeed(lease,next));
    CHECK(At<std::uint64_t>(object,0x3E8)==primary && At<std::uint64_t>(object,0x490)==variant);
    CHECK(VirtualProtect(allocation+0x1000,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
    CHECK(SetSeed(lease,next));
    CHECK(At<std::uint64_t>(object,0x3E8)==next && At<std::uint64_t>(object,0x490)==next);
    // The inverse protection failure also leaves both states untouched.
    Put<std::uint64_t>(object,0x3E8,primary);Put<std::uint64_t>(object,0x490,variant);
    CHECK(VirtualProtect(allocation,0x1000,PAGE_READONLY,&was)!=0);Sleep(20);
    CHECK(!SetSeed(lease,next));
    CHECK(At<std::uint64_t>(object,0x3E8)==primary && At<std::uint64_t>(object,0x490)==variant);
    CHECK(VirtualProtect(allocation,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
    CHECK(VirtualFree(allocation,0,MEM_RELEASE)!=0);
}
bool gateAllows=false,gateThrows=false;
unsigned gateCalls=0;
bool RestoreGate(NativeLease& lease) noexcept {
    ++gateCalls;CHECK(lease.borrowed && lease.savedPad!=nullptr && lease.savedPlayer==6);
    if(gateThrows)RaiseException(0xE0234567,0,0,nullptr);
    return gateAllows;
}
void RestoreGateCases(Fixture& fixture) {
    const auto& profile=Find("GiantAnt");
    const auto original=reinterpret_cast<InputFn>(image+profile.inputRva);
    Input input{};input.right=.25f;input.forward=-.5f;input.yaw=.75f;input.pitch=-2;input.vertical=-.5f;
    input.held=input.pressed=1u<<4;input.seed=expectedSeed=expectedVariantSeed=0x1122334455667788ull;
    expectedHeld=expectedPressed=1;current=&fixture;expectedDt=fixture.oldPad;
    for(unsigned state=0;state<3;++state) {
        fixture.Reset(profile);auto lease=fixture.Lease();callbackMode=state==1 ? CallbackMode::readOnlyDead : CallbackMode::readOnly;
        std::uint64_t result=0;CHECK(!RunInput(lease,input,original,expectedDt,&result));
        CHECK(lease.borrowed && lease.savedPad==fixture.oldPad);
        DWORD was=0;CHECK(VirtualProtect(fixture.object,0x1000,PAGE_READWRITE,&was)!=0);Sleep(20);
        if(state==2)Put<LONG>(fixture.control,8,0);
        lease.restoreGate=&RestoreGate;
        unsigned char baseline[sizeof(lease)],objectBefore[Fixture::kObjectBytes];
        std::memcpy(baseline,&lease,sizeof(lease));std::memcpy(objectBefore,fixture.object,sizeof(objectBefore));
        gateAllows=false;gateThrows=false;const unsigned before=gateCalls;
        CHECK(!Restore(&lease) && gateCalls==before+1);
        CHECK(std::memcmp(&lease,baseline,sizeof(lease))==0);
        CHECK(std::memcmp(fixture.object,objectBefore,sizeof(objectBefore))==0);
        gateThrows=true;CHECK(!Restore(&lease) && gateCalls==before+2);
        CHECK(std::memcmp(&lease,baseline,sizeof(lease))==0);
        CHECK(std::memcmp(fixture.object,objectBefore,sizeof(objectBefore))==0);
        gateThrows=false;gateAllows=true;CHECK(Restore(&lease) && gateCalls==before+3);
        CHECK(!lease.object && !lease.control && !lease.profile && !lease.restoreGate && !lease.borrowed);
        if(state<2) {
            CHECK(At<const void*>(fixture.object,0x340)==fixture.oldPad && fixture.object[0x354]==6);
            if(state==1) {
                Put<const void*>(objectBefore,0x340,fixture.oldPad);objectBefore[0x354]=6;
                CHECK(std::memcmp(fixture.object,objectBefore,sizeof(objectBefore))==0);
            }
        } else CHECK(std::memcmp(fixture.object,objectBefore,sizeof(objectBefore))==0);
    }
    callbackMode=CallbackMode::observe;
}
unsigned maintenanceCalls=0,nativeAiCalls=0,seedCalls=0;
bool maintenanceThrows=false,seedThrows=false,seedAllows=true,ownsThrows=false;
void* extensionExpectedObject=nullptr;
void* extensionExpectedDt=nullptr;
std::uint64_t __fastcall OwnedAiMarker(void*,void*) { return 0; }
bool OwnsAiSlot(const void* slot) noexcept {
    if(ownsThrows)RaiseException(0xE0345678,0,0,nullptr);
    return slot==reinterpret_cast<const void*>(&OwnedAiMarker);
}
std::uint64_t __fastcall OriginalAi(void*,void*) { ++nativeAiCalls;return 0xBADull; }
std::uint64_t __fastcall ControlledMaintenance(void* object,void* dt) {
    ++maintenanceCalls;CHECK(object==extensionExpectedObject && dt==extensionExpectedDt);
    if(maintenanceThrows)RaiseException(0xE0456789,0,0,nullptr);
    return 0x123456789ABCDEFull;
}
bool SeedExtension(void* object,std::uint64_t seed) noexcept {
    ++seedCalls;CHECK(object==extensionExpectedObject);
    if(seedThrows)RaiseException(0xE056789A,0,0,nullptr);
    if(!seedAllows)return false;
    Put<std::uint64_t>(object,0x500,seed);return true;
}
void ExtensionCases(Fixture& fixture) {
    // This executable is single-threaded; private registry reset only separates
    // fixture cases. Production registry/API and locks are the actual source.
    for(auto& extension:extensions)extension={};extensionCount=0;
    const auto& profile=Find("GiantAnt");fixture.Reset(profile);
    const auto nativeAi=image+profile.aiRva;
    InstallFunction(profile.aiRva,&OriginalAi);
    auto setAi=[&](const void* pointer){Put<const void*>(image+profile.vtableRva,kAiSlot*8,pointer);};
    const auto wrapper=reinterpret_cast<const void*>(&OwnedAiMarker);
    CHECK(!RegisterControlledAiExtension(0,profile.aiRva,&OwnsAiSlot,&ControlledMaintenance,&SeedExtension));
    CHECK(!RegisterControlledAiExtension(profile.vtableRva,profile.aiRva+1,&OwnsAiSlot,&ControlledMaintenance,&SeedExtension));
    CHECK(!RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,nullptr,&ControlledMaintenance,&SeedExtension));
    CHECK(!RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,&OwnsAiSlot,nullptr,&SeedExtension));
    CHECK(Profile(fixture.object)==&profile);
    setAi(wrapper);CHECK(!Profile(fixture.object));
    CHECK(RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,&OwnsAiSlot,&ControlledMaintenance,&SeedExtension));
    CHECK(RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,&OwnsAiSlot,&ControlledMaintenance,&SeedExtension));
    CHECK(extensionCount==1);
    CHECK(!RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,&OwnsAiSlot,&OriginalAi,&SeedExtension));
    CHECK(!RegisterControlledAiExtension(profile.vtableRva,profile.aiRva,&OwnsAiSlot,&ControlledMaintenance,nullptr));
    CHECK(Profile(fixture.object)==&profile);
    auto lease=fixture.Lease();extensionExpectedObject=fixture.object;extensionExpectedDt=fixture.oldPad;
    std::uint64_t result=0;CHECK(RunControlledAiExtension(lease,extensionExpectedDt,&result));
    CHECK(result==0x123456789ABCDEFull && maintenanceCalls==1 && nativeAiCalls==0);
    setAi(image+1);CHECK(!Profile(fixture.object));result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0 && maintenanceCalls==1 && nativeAiCalls==0);
    setAi(nativeAi);CHECK(Profile(fixture.object)==&profile);result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0 && nativeAiCalls==0);
    setAi(wrapper);maintenanceThrows=true;result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0 && maintenanceCalls==2 && nativeAiCalls==0);
    maintenanceThrows=false;CHECK(RunControlledAiExtension(lease,extensionExpectedDt,nullptr) && maintenanceCalls==3 && nativeAiCalls==0);
    constexpr std::uint64_t seed=0xBEEFFACE12345678ull;
    CHECK(SetSeed(lease,seed) && seedCalls==1);
    CHECK(At<std::uint64_t>(fixture.object,0x3E8)==seed && At<std::uint64_t>(fixture.object,0x490)==seed &&
          At<std::uint64_t>(fixture.object,0x500)==seed);
    seedAllows=false;CHECK(!SetSeed(lease,seed+1) && seedCalls==2);
    CHECK(At<std::uint64_t>(fixture.object,0x500)==seed);
    seedAllows=true;seedThrows=true;CHECK(!SetSeed(lease,seed+2) && seedCalls==3);seedThrows=false;
    CHECK(At<std::uint64_t>(fixture.object,0x500)==seed);
    // A rejected private seed prevents dispatch of the actual native input.
    Input input{};input.pressed=1u<<4;input.seed=seed+3;seedAllows=false;
    const unsigned before=callbackCalls;result=7;
    CHECK(!RunInput(lease,input,reinterpret_cast<InputFn>(image+profile.inputRva),nullptr,&result));
    CHECK(result==0 && callbackCalls==before && !lease.borrowed && seedCalls==4);seedAllows=true;
    ownsThrows=true;CHECK(!Profile(fixture.object));result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0);
    CHECK(!SetSeed(lease,seed+4));ownsThrows=false;
    Put<LONG>(fixture.control,8,0);result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0);
    Put<LONG>(fixture.control,8,1);fixture.object[0x2E8]=1;result=7;
    CHECK(!RunControlledAiExtension(lease,extensionExpectedDt,&result) && result==0);fixture.object[0x2E8]=0;
    setAi(nativeAi);
    // onSeed is optional: an owned maintenance wrapper without private RNG
    // still accepts both native LCG writes and never calls another extension.
    const auto& other=Find("GiantSpider");fixture.Reset(other);
    CHECK(RegisterControlledAiExtension(other.vtableRva,other.aiRva,&OwnsAiSlot,&ControlledMaintenance));
    Put<const void*>(image+other.vtableRva,kAiSlot*8,wrapper);
    lease=fixture.Lease();const unsigned oldSeeds=seedCalls;
    CHECK(SetSeed(lease,seed) && seedCalls==oldSeeds);
    CHECK(At<std::uint64_t>(fixture.object,0x3E8)==seed && At<std::uint64_t>(fixture.object,0x490)==seed);
    Put<const void*>(image+other.vtableRva,kAiSlot*8,image+other.aiRva);
    CHECK(nativeAiCalls==0);
    for(auto& extension:extensions)extension={};extensionCount=0;
}
struct Range { std::size_t at,bytes; };
void ReleaseCaseImpl(Fixture& fixture,const char* name,const Range* ranges,std::size_t count,
                 std::size_t maskAt=0,std::uint32_t mask=0,std::size_t indexAt=0,bool monster=false) {
    fixture.Reset(Find(name));auto lease=fixture.Lease();
    unsigned char expected[Fixture::kObjectBytes];std::memcpy(expected,fixture.object,sizeof(expected));
    for(std::size_t i=0;i<count;++i)std::memset(expected+ranges[i].at,0,ranges[i].bytes);
    if(maskAt)Put<std::uint32_t>(expected,maskAt,At<std::uint32_t>(expected,maskAt)&~mask);
    if(indexAt)Put<std::int32_t>(expected,indexAt,-1);
    if(monster){Put<std::uint32_t>(expected,0x8A4,0xFFFFFFFFu);Put<std::uint32_t>(expected,0x8A8,0x8FFFFFFFu);}
    CHECK(Neutralize(lease));CHECK(std::memcmp(fixture.object,expected,sizeof(expected))==0);
    CHECK(lease.object==fixture.object && lease.control==fixture.control);
    CHECK(Restore(&lease));CHECK(!lease.object && !lease.control && !lease.profile);
    CHECK(std::memcmp(fixture.object,expected,sizeof(expected))==0);
}
template<std::size_t N>
void ReleaseCase(Fixture& fixture,const char* name,const Range (&ranges)[N],
                 std::size_t maskAt=0,std::uint32_t mask=0,std::size_t indexAt=0,bool monster=false) {
    ReleaseCaseImpl(fixture,name,ranges,N,maskAt,mask,indexAt,monster);
}
void ReleaseCases(Fixture& fixture) {
    const Range trailer[]={{0xCB0,12},{0xCC0,5}};ReleaseCase(fixture,"AlienTrailer",trailer);
    const Range carrier[]={{0xB40,12},{0xB50,12}};ReleaseCase(fixture,"UfoCarrier508",carrier);
    const Range timeship[]={{0x10E0,12},{0x10F0,12},{0x1100,1}};ReleaseCase(fixture,"EDF6_TimeShip",timeship);
    const Range insect[]={{0xFC0,12},{0xFD0,4},{0xFE0,12},{0xFF0,4}};
    ReleaseCase(fixture,"GiantAnt",insect);ReleaseCase(fixture,"GiantDango",insect);
    const Range walker[]={{0x1290,12},{0x12A0,8},{0x12C0,4}};
    ReleaseCase(fixture,"EDF6_Berserker_Large",walker);
    const Range longleg[]={{0xF10,12},{0xF20,6}};
    ReleaseCase(fixture,"Deiroi",longleg);ReleaseCase(fixture,"Nephila",longleg);
    const Range pillbug[]={{0x12E0,12},{0x12F0,12},{0x1300,3}};
    ReleaseCase(fixture,"EDF6_GiantDangoLarge",pillbug);
    const Range merman[]={{0x1BC0,12},{0x1BD0,5}};
    ReleaseCase(fixture,"EDF6_Merman",merman,0,0,0x1BD8);
    const Range dyno[]={{0x13C0,12},{0x13D0,12},{0x13E0,16}};
    ReleaseCase(fixture,"EDF6_FlyingDyno",dyno);
    const Range shellfish[]={{0xFC0,12},{0xFD0,12}};
    ReleaseCase(fixture,"EDF6_ShellFish",shellfish,0xFE0,15);
    const Range spinner[]={{0x1160,12},{0x1170,12}};
    ReleaseCase(fixture,"EDF6_SpinnerUfo",spinner,0x1180,7);
    const Range squid[]={{0x1230,12},{0x1240,12},{0x1250,1}};
    ReleaseCase(fixture,"EDF6_SquidSmall",squid);
    const Range monster[]={{0x880,12},{0x894,4},{0x8A0,1}};
    ReleaseCase(fixture,"Monster501",monster,0,0,0,true);ReleaseCase(fixture,"Monster504",monster,0,0,0,true);
    const Range humanoid[]={{0x11D0,12},{0x11E0,8},{0x11F0,6}};
    ReleaseCase(fixture,"Humanoid_BigGreyBoss",humanoid);ReleaseCase(fixture,"EDF6_Martian",humanoid);
    ReleaseCase(fixture,"EDF6_Venus",humanoid);ReleaseCase(fixture,"Humanoid_Basic",humanoid);
    const Range ufo507[]={{0x1460,12},{0x1470,5}};ReleaseCase(fixture,"UfoSmall507",ufo507);
    const Range ufo515[]={{0x600,12},{0x610,5}};ReleaseCase(fixture,"UfoSmall515",ufo515);
    ReleaseCaseImpl(fixture,"DropBoat512",nullptr,0);
    fixture.Reset(Find("GiantAnt"));auto lease=fixture.Lease();
    unsigned char before[Fixture::kObjectBytes];std::memcpy(before,fixture.object,sizeof(before));
    Put<LONG>(fixture.control,8,0);CHECK(!Neutralize(lease));
    CHECK(std::memcmp(fixture.object,before,sizeof(before))==0);
    Put<LONG>(fixture.control,8,1);Put<const void*>(fixture.object,0x30,fixture.replacementControl);
    std::memcpy(before,fixture.object,sizeof(before));
    CHECK(!Neutralize(lease));CHECK(Restore(&lease));
    CHECK(!lease.object && !lease.control && !lease.profile && std::memcmp(fixture.object,before,sizeof(before))==0);
    fixture.Reset(Find("GiantAnt"));lease=fixture.Lease();Put<LONG>(fixture.control,8,0);
    std::memcpy(before,fixture.object,sizeof(before));
    CHECK(Restore(&lease));CHECK(!lease.object && std::memcmp(fixture.object,before,sizeof(before))==0);
    NativeLease empty{};CHECK(!Neutralize(empty));CHECK(Restore(nullptr));CHECK(Restore(&empty));
}
}
int main() {
    PadCases();BuildImage();Fixture fixture{};
    AdmissionCases(fixture);ScopeCases(fixture);InactiveScopeCases(fixture);SeedWriteCases(fixture);RestoreGateCases(fixture);ExtensionCases(fixture);ReleaseCases(fixture);
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy native input fixture: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
