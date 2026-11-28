// The Vehicler's start seat (vehicler_start.h; docs/newclass-re/vehicler-plan.md §2.1, §2.2, §5 M3).
//  - Who asks: this machine's players (firstperson.h LocalPlayers: split screen two offline; online PlayerHuman() alone,
//    as RequestZwStart takes only it), each object once a mission. Local: +0x128 bit 0 clear and a pad at +0x340. Its
//    equipment is the game status's player +0x338 (the pad index 0x54ED40 writes: 0 / 1 offline), online player 0's.
//  - When: StartGate (vehicler_start_logic.h). Online it waits for the support session to take requests
//    (support_net.h SupportRequestsOpen, the gate SubmitSupportRequest refuses by), no other wait.
//  - The ride: on the owner's machine alone, RideVehicle 0x5765E0(human, &shared_ptr, seat 0) with one reference taken
//    first (it releases one at 0x57690D on every path); the board button's tail +0x3F0 (0x56D7D1); then 0x5763E0 announces
//    it (online message 6: every peer's copy of the player replays it by RideVehicle at 0x577661; offline nothing).
//    RideVehicle checks neither reach, nor team, nor class mask; the class seat rule 0x5DF770 passes type 4.
#include "vehicler_start.h"
#include "vehicler_start_logic.h"
#include "ride_native.h"
#include "zw_vehicle_dispatch.h"
#include "zw_vehicle_catalog.h"
#include "soldier_factory.h"
#include "support_net.h"
#include "support_policy.h"
#include "firstperson.h"
#include "netprobe.h"
#include "scene.h"
#include "objref.h"
#include "edf/step_objects.h"
#include "edf_layout.h"
#include "memory.h"
#include "plugin_log.h"
#include <intrin.h>
#include <cstdint>

namespace crew {
namespace {
using namespace ride_native;
constexpr std::size_t kHumanLocalFlags=0x128,kHumanPadIndex=0x338,kHumanBusy=0x39C,kHumanReads=0x1558;
constexpr int kLocalMost=2;

bool ok=false;
enum class Phase : std::uint8_t { asked,done };
struct Rec { ObjRef human; Phase phase=Phase::asked; bool waitLogged=false; };
Rec recs[kLocalMost]{};
// The objects seen waiting (dead, the handshake) before they were asked: their wait logged once.
ObjRef waitLogged[kLocalMost]{};

Rec* Find(const unsigned char* h) noexcept {
    for(auto& r:recs)if(r.human.Is(h))return &r;
    return nullptr;
}
Rec* Make(unsigned char* h) noexcept {
    for(auto& r:recs)if(!r.human || !objref::Exists(r.human)){r=Rec{};r.human=ObjRef::Of(h);return &r;}
    return nullptr;
}
bool LocalHuman(const unsigned char* h) noexcept {
    return !(At<unsigned char>(h,kHumanLocalFlags)&1u) && At<const void*>(h,kHumanPad)!=nullptr;
}
int PlayerOf(const unsigned char* h,bool online) noexcept {
    if(online)return 0;
    const int index=At<std::int32_t>(h,kHumanPadIndex);
    return index>=0 && index<kLocalMost ? index : -1;
}
vstart::Asked Mirror(ZwStartState s) noexcept {
    switch(s) {
    case ZwStartState::none:return vstart::Asked::none;
    case ZwStartState::pending:return vstart::Asked::pending;
    case ZwStartState::ready:return vstart::Asked::ready;
    case ZwStartState::refused:return vstart::Asked::refused;
    }
    return vstart::Asked::none;
}

// The stock ride on the driver seat; true when the human sits in `v` after it. No C++ objects here (__try).
bool Ride(unsigned char* h,unsigned char* v) noexcept {
    __try {
        auto* ctrl=At<unsigned char*>(v,kSelfCtrl);
        if(!ctrl || !Readable(ctrl,12) || At<std::int32_t>(ctrl,8)<=0)return false;
        _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+8));   // RideVehicle's own release takes it back
        SharedRef ref{v,ctrl};
        reinterpret_cast<RideFn>(image+kRideVehicle)(h,&ref,static_cast<int>(vstart::kDriverSeat));
        if(At<const void*>(h,kHumanRiding)!=v)return false;
        const auto* seat=At<const unsigned char*>(h,kHumanSeat);
        if(seat && Readable(seat,kSeatKeyRow+4) && vstart::SetsSeatFlag(At<std::int32_t>(seat,kSeatKeyRow)))h[kHumanSeatFlag]=1;
        reinterpret_cast<AnnounceFn>(image+kAnnounceRide)(h);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void Walk(Rec& r,const unsigned char* h,const char* why) noexcept {
    r.phase=Phase::done;
    Log("VEHICLER start %p: walks in (%s)",static_cast<const void*>(h),why);
}

void After(Rec& r,unsigned char* h) noexcept {
    unsigned char* v=nullptr;
    vstart::AfterFacts f;
    f.state=Mirror(ZwStartStatus(h,&v));
    f.haveVehicle=v!=nullptr;
    f.alive=!h[kDead];
    f.onFoot=At<const void*>(h,kHumanRiding)==nullptr;
    f.busy=At<std::int32_t>(h,kHumanBusy)!=0;
    const vstart::AfterWhy why=vstart::AfterAskWhy(f);
    switch(vstart::AfterAsk(f)) {
    case vstart::Next::wait:
        if(why==vstart::AfterWhy::busy && !r.waitLogged){r.waitLogged=true;Log("VEHICLER start %p: %s",static_cast<void*>(h),vstart::AfterText(why));}
        return;
    case vstart::Next::walk:Walk(r,h,vstart::AfterText(why));return;
    case vstart::Next::ride:break;
    }
    r.phase=Phase::done;
    const int kind=ZwVehicleKind(v);
    if(!Ride(h,v)){Log("VEHICLER start %p: walks in (RideVehicle did not seat it in %p)",static_cast<void*>(h),static_cast<void*>(v));return;}
    Log("VEHICLER start %p: seated in its %ls %p (driver seat, %s)",static_cast<void*>(h),
        kind>=0 ? kZwVehicles[kind].row : L"?",static_cast<void*>(v),InSession() ? "announced to the room" : "offline");
}

void Consider(unsigned char* h,bool online) noexcept {
    if(!h || !stepobj::Reads(h,kHumanReads))return;
    Rec* r=Find(h);
    if(r) {
        if(r->phase==Phase::asked)After(*r,h);
        return;
    }
    vstart::Facts f;
    f.zwType=soldier::ZwType(h);
    if(f.zwType!=vstart::kVehiclerType)return;   // the common case, before anything else is read
    const int index=PlayerOf(h,online);
    f.local=LocalHuman(h) && index>=0 && (!online || h==PlayerHuman());
    f.alive=!h[kDead];
    f.underground=SupportMissionPolicy().underground;
    f.kind=index>=0 ? ZwMissionVehicle(index) : -1;
    f.kindReady=f.kind>=0 && ZwVehicleReady(f.kind);
    f.online=online;
    f.sessionOpen=online && SupportRequestsOpen();
    const vstart::Why why=vstart::StartWhy(f);
    switch(vstart::GateOf(why)) {
    case vstart::Gate::ignore:return;
    case vstart::Gate::wait: {
        for(auto& w:waitLogged)if(w.Is(h))return;
        for(auto& w:waitLogged)if(!w || !objref::Exists(w)) {
            w=ObjRef::Of(h);Log("VEHICLER start %p: waits (%s)",static_cast<void*>(h),vstart::WhyText(why));break;
        }
        return;
    }
    case vstart::Gate::walk:
    case vstart::Gate::request:break;
    }
    r=Make(h);
    if(!r){Log("VEHICLER start %p: no record free (more than %d local players?)",static_cast<void*>(h),kLocalMost);return;}
    if(vstart::GateOf(why)==vstart::Gate::walk){Walk(*r,h,vstart::WhyText(why));return;}
    Log("VEHICLER start %p: asks for its %ls (player %d)",static_cast<void*>(h),kZwVehicles[f.kind].row,index);
    if(!RequestZwStart(h,f.kind)){Walk(*r,h,"the request was not made (zw_vehicle_dispatch logged why)");return;}
    After(*r,h);   // offline the hull is made and ready at once
}
}  // namespace

bool InstallVehiclerStart() noexcept {
    if(ok)return true;
    bool matches=false;
    __try {
        matches=Matches(kRideVehicle,kRideSig,sizeof(kRideSig)) && Matches(kRideRelease,kRideReleaseSig,sizeof(kRideReleaseSig)) &&
            Matches(kAnnounceRide,kAnnounceRideSig,sizeof(kAnnounceRideSig)) &&
            Matches(kRemoteRideCall-7,kRemoteRideSig,sizeof(kRemoteRideSig));
    } __except(EXCEPTION_EXECUTE_HANDLER){matches=false;}
    if(!matches){Log("VEHICLER start off: the ride's natives (0x5765E0 / 0x57690D / 0x5763E0 / 0x577661) not as read");return false;}
    ok=true;
    Log("VEHICLER start on");
    return true;
}

void VehiclerStartTick() noexcept {
    if(!ok || !soldier::FactoriesReady())return;
    const bool online=InSession();
    if(online){Consider(PlayerHuman(),true);return;}
    unsigned char* humans[kLocalMost]{};
    const int n=LocalPlayers(humans,kLocalMost);
    bool sawPlayer=false;
    unsigned char* const own=PlayerHuman();
    for(int i=0;i<n;++i){sawPlayer=sawPlayer || humans[i]==own;Consider(humans[i],false);}
    if(!sawPlayer)Consider(own,false);
}

void ResetVehiclerStart() noexcept {
    for(auto& r:recs)r=Rec{};
    for(auto& w:waitLogged)w=ObjRef{};
}
}  // namespace crew
