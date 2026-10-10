// A support vehicle delivered by the plugin's transport helicopter in the game's own container (airdrop.h; the stock chain
// and its parameters: airdrop_logic.h, docs/airdrop-vehicle-re.md). The helicopter is the carrier the stock
// Transporter508 is:
//  - made under it at once: CreateObject of the container's SGO with ContainerInitParam (the helicopter has no network
//    identity: the derived id is the empty one, the counter unset), then the container's configure (0x5E8B40): no
//    requester (its delivery event is skipped, 0x5E911C), the vehicle's SGO, an empty VehicleSetup, level 1;
//  - held: its world matrix written under the helicopter each frame (airdrop::CarryMatrix), as the transporter's update
//    writes it from its locator (0x5E592F); the helicopter flies to the point and hovers over it (heli.cpp HeliFerry);
//  - let go over the point (airdrop::ReleaseNow): 0x5E84D0, the stock release; the helicopter leaves (HeliStartLeaving:
//    support_dispatch.cpp Retire deletes it out of sight with its pilot);
//  - the container falls by its own physics and makes the vehicle (0x5E8FDC): that call is watched here (a rel32
//    redirect), so a vehicle made for one of these containers gets its own mission setup at once (the stock request's
//    setup is its weapon's SGO value, which the plugin's call does not have; support_spawn.cpp ApplySupportVehicleSetup
//    is the ground support's own step), before the container sets its team (5) and deletes itself.
// Game thread only: AirdropTick from map.cpp's frame, the watched call from the container's own update.
#include "airdrop.h"
#include "airdrop_logic.h"
#include "crew.h"
#include "heli.h"
#include "memory.h"
#include <cstring>
#include <cwchar>

namespace crew {
bool ApplySupportVehicleSetup(unsigned char* vehicle) noexcept;   // support_spawn.cpp
namespace {
constexpr unsigned kCreateObject=0x11945E0,kConfigure=0x5E8B40,kRelease=0x5E84D0,kVehicleCall=0x5E8FDC,kPreload=0x7A3780;
constexpr unsigned kContainerParamVtable=0x17D7818,kContainerVtable=0x17D7CB8;
constexpr std::size_t kObjectMgr=0x20B2958,kPreloadMgr=0x20B29A8;
constexpr std::size_t kContainerVehicleSgo=0xB80;   // the configured SGO path (a std::wstring; 0x5E8BC7)
const wchar_t kContainerSgo[]=L"app:/object/v509_transportbox.sgo";   // the stock request's (probe: init arg 5)
const unsigned char kCreateSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; push rdi; sub rsp,20h; mov rax,[rdx+8]; mov rsi,rcx
const unsigned char kConfigureSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,
                                     0x83,0xEC,0x20,0x48,0x8B,0x42,0x08,0x48,0x8B,0xF1};
// mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,30h; lea rdi,[rcx+0B40h] (its state machine)
const unsigned char kReleaseSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48,0x8D,0xB9,
                                   0x40,0x0B,0x00,0x00};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};

using CreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,void*);
using ConfigureFn=void(*)(unsigned char*,const airdrop::SharedRef*,const wchar_t*,const airdrop::VehicleSetup*,float);
using ReleaseFn=void(*)(unsigned char*);

enum class Phase : std::uint8_t { free, carried, falling };
struct Drop {
    Phase phase=Phase::free;
    ObjRef carrier,box;
    SupportVehicleKind kind{};
    float target[3]{};
    float closest=1e30f;
    ULONGLONG since=0,closerAt=0;   // closerAt: when the carrier last came airdrop::kCloser nearer the point
    const wchar_t* vehicleSgo=nullptr;   // the container's configured path, as its vehicle step hands it to CreateObject
};
constexpr int kDrops=8;
Drop drops[kDrops]{};
// AirdropTest (tests only): the vehicle delivered last, the player put into it kTestBoardMs after (BoardingRequest, the
// boarding gun's path: the stock board button), once.
constexpr ULONGLONG kTestBoardMs=4000;
ObjRef testVehicle{};ULONGLONG testVehicleAt=0;
bool profile=false,watching=false,boxPreloaded=false;

// A scene object still the one referred to (its weak self block unchanged and alive: lockon.cpp's test).
bool Alive(const ObjRef& ref) noexcept {
    if(!ref || !Readable(ref.obj,kSelfCtrl+8) || !ref.Is(ref.obj))return false;
    return Readable(ref.ctrl,0x10) && At<std::int32_t>(ref.ctrl,8)>0;
}
bool CarrierAlive(const ObjRef& ref) noexcept {
    return Alive(ref) && Readable(ref.obj,kDead+1) && !static_cast<const unsigned char*>(ref.obj)[kDead];
}
unsigned char* Raw(const ObjRef& ref) noexcept {return static_cast<unsigned char*>(const_cast<void*>(ref.obj));}

// The container's configured vehicle path as its vehicle step reads it (0x5E8FB9: the buffer, or the pointer when the
// capacity is 8 or more): the key that tells its CreateObject from any other container's.
const wchar_t* ConfiguredPath(const unsigned char* box) noexcept {
    const unsigned char* s=box+kContainerVehicleSgo;
    return At<std::uint64_t>(s,0x18)>=8 ? At<const wchar_t*>(s,0) : reinterpret_cast<const wchar_t*>(s);
}

float Level(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

void Hold(Drop& d) noexcept {
    alignas(16) float m[16];
    airdrop::CarryMatrix(reinterpret_cast<const float*>(Raw(d.carrier)+kMatrix),m);
    std::memcpy(Raw(d.box)+kMatrix,m,sizeof(m));
}

void LetGo(Drop& d,const char* why) noexcept {
    __try {
        reinterpret_cast<ReleaseFn>(image+kRelease)(Raw(d.box));
        d.phase=Phase::falling;d.since=GameMs();
        const float* at=reinterpret_cast<const float*>(Raw(d.box)+kPosition);
        Log("AIRDROP container %p let go %s at (%.0f,%.0f,%.0f), %.0f m from its point",d.box.obj,why,at[0],at[1],at[2],
            Level(at,d.target));
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("AIRDROP container %p: release fault",d.box.obj);d=Drop{};return;}
    if(CarrierAlive(d.carrier))HeliStartLeaving(d.carrier.obj);
}

void Frame(Drop& d,ULONGLONG ms) noexcept {
    if(!Alive(d.box)) {
        Log("AIRDROP container %p gone %s",d.box.obj,d.phase==Phase::carried ? "while carried" : "before its vehicle was made");
        if(d.phase==Phase::carried && CarrierAlive(d.carrier))HeliStartLeaving(d.carrier.obj);
        d=Drop{};return;
    }
    if(d.phase==Phase::falling) {
        if(ms-d.since>airdrop::kFallMs){Log("AIRDROP container %p made no vehicle in %llu s",d.box.obj,airdrop::kFallMs/1000);d=Drop{};}
        return;
    }
    if(!CarrierAlive(d.carrier)){LetGo(d,"(its helicopter lost)");return;}
    Hold(d);
    const float dist=Level(reinterpret_cast<const float*>(Raw(d.carrier)+kPosition),d.target);
    if(dist<d.closest-airdrop::kCloser || !d.closerAt){d.closest=dist;d.closerAt=ms;}
    if(airdrop::ReleaseNow(dist,d.closest,ms-d.closerAt))LetGo(d,dist<=airdrop::kOverPoint ? "over its point" : "where it hovers");
    else if(ms-d.since>airdrop::kCarryMs)LetGo(d,"(its helicopter never reached the point)");
}

// The container's vehicle step (0x5E8FDC): the stock CreateObject, then, for a container carried here, its vehicle's own
// mission setup (the stock request hands its weapon's; this call has none).
unsigned char* VehicleStep(void* mgr,const float* m,const wchar_t* sgo,void* param) {
    unsigned char* const v=reinterpret_cast<CreateFn>(image+kCreateObject)(mgr,m,sgo,param);
    __try {
        for(auto& d:drops) {
            if(d.phase!=Phase::falling || sgo!=d.vehicleSgo || !Alive(d.box))continue;
            const auto spec=SupportVehicleInfo(d.kind);
            const bool same=v && spec && At<const void*>(v,0)==image+spec->vtable;
            const bool setup=same && ApplySupportVehicleSetup(v);
            const float* at=v ? reinterpret_cast<const float*>(v+kPosition) : d.target;
            Log("AIRDROP vehicle %p (%ls) made by container %p at (%.0f,%.0f,%.0f), %.0f m from its point: %s",v,
                spec ? spec->sgo : L"?",d.box.obj,at[0],at[1],at[2],Level(at,d.target),
                !v ? "CreateObject failed" : !same ? "not the expected class (setup not applied)" :
                setup ? "its mission setup applied" : "its mission setup FAILED");
            if(v && Cfg().airdropTest){testVehicle=ObjRef::Of(v);testVehicleAt=GameMs();}
            d=Drop{};
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("AIRDROP vehicle step fault");}
    return v;
}
}  // namespace

void PreloadAirdrop() noexcept {
    boxPreloaded=false;
    if(!profile)return;
    __try {
        const auto mgr=At<void*>(image,kPreloadMgr);
        if(!mgr)return;
        reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,kContainerSgo,2,-1);
        boxPreloaded=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("AIRDROP container preload fault: no airdrops this mission");}
}

bool AirdropReady(SupportVehicleKind kind) noexcept {
    return profile && watching && boxPreloaded && SupportVehicleReady(kind,SupportCrewMode::unmanned);
}

bool AirdropBegin(const void* carrier,SupportVehicleKind kind,const float* target) noexcept {
    const auto spec=SupportVehicleInfo(kind);
    if(!carrier || !target || !spec || !AirdropReady(kind) || InSession())return false;
    Drop* d=nullptr;
    for(auto& row:drops)if(row.phase==Phase::free){d=&row;break;}
    if(!d){Log("AIRDROP no free drop slot");return false;}   // before the ferry: a false return leaves the helicopter's call alone
    // The helicopter on its way to hover over the point first: no container is made for one that cannot go there.
    if(!HeliFerry(carrier,target,false)){Log("AIRDROP helicopter %p: no ferry",carrier);return false;}
    unsigned char* box=nullptr;
    __try {
        alignas(16) float m[16];
        airdrop::CarryMatrix(reinterpret_cast<const float*>(static_cast<const unsigned char*>(carrier)+kMatrix),m);
        airdrop::ContainerInitParam param{};
        param.vtable=image+kContainerParamVtable;
        param.counter=airdrop::kNoCounter;   // the helicopter has no derived-id counter: unset, as an offline stock carrier
        const auto mgr=At<void*>(image,kObjectMgr);
        if(!mgr){HeliFerry(carrier,nullptr,false);return false;}
        box=reinterpret_cast<CreateFn>(image+kCreateObject)(mgr,m,kContainerSgo,&param);
        if(!box || At<const void*>(box,0)!=image+kContainerVtable) {
            Log("AIRDROP container not made (%p)",box);
            HeliFerry(carrier,nullptr,false);
            return false;
        }
        const airdrop::SharedRef none{};   // no requester: the container's delivery event goes to nobody (0x5E911C)
        const auto setup=airdrop::EmptySetup();
        reinterpret_cast<ConfigureFn>(image+kConfigure)(box,&none,spec->sgo,&setup,1.0f);
        *d=Drop{};
        d->phase=Phase::carried;d->carrier=ObjRef::Of(carrier);d->box=ObjRef::Of(box);d->kind=kind;
        std::memcpy(d->target,target,12);d->since=GameMs();d->vehicleSgo=ConfiguredPath(box);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("AIRDROP container making fault (%p)",box);
        *d=Drop{};HeliFerry(carrier,nullptr,false);return false;
    }
    Log("AIRDROP helicopter %p carries container %p for %ls to (%.0f,%.0f,%.0f)",carrier,box,spec->sgo,target[0],target[1],
        target[2]);
    return true;
}

void AirdropTick() noexcept {
    static ULONGLONG frame=~0ull;
    if(frame==GameFrame())return;
    frame=GameFrame();
    __try {
        const ULONGLONG ms=GameMs();
        for(auto& d:drops)if(d.phase!=Phase::free)Frame(d,ms);
        if(testVehicle && ms-testVehicleAt>=kTestBoardMs) {
            if(CarrierAlive(testVehicle)) {
                Log("AIRDROP AirdropTest: the player put into vehicle %p (team %d)",testVehicle.obj,At<std::int32_t>(testVehicle.obj,kTeam));
                BoardingRequest(Raw(testVehicle));
            }
            testVehicle=ObjRef{};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("AIRDROP frame fault");}
}

void ResetAirdrops() noexcept {
    for(auto& d:drops)d=Drop{};
    testVehicle=ObjRef{};testVehicleAt=0;
}

bool InstallAirdrop() noexcept {
    profile=Matches(kCreateObject,kCreateSig,sizeof(kCreateSig)) && Matches(kConfigure,kConfigureSig,sizeof(kConfigureSig)) &&
            Matches(kRelease,kReleaseSig,sizeof(kReleaseSig)) && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) &&
            Readable(image+kContainerParamVtable,8) && Readable(image+kContainerVtable,8);
    if(!profile){Log("AIRDROP profile mismatch: no container airdrops");return false;}
    bool changed=false;
    watching=RedirectCall(image+kVehicleCall,image+kCreateObject,reinterpret_cast<void*>(&VehicleStep),changed);
    if(!watching)Log("AIRDROP the container's vehicle step not watched%s: no container airdrops",changed ? " (half patched)" : "");
    return watching;
}
}  // namespace crew
