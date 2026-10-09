// The turrets' aim link with EDF6AutoTurret (common/edf/aimlink.h): this plugin hands it the camera's view ray and the
// map ray its lock-by-look needs, and takes its player turret readout for the HUD (hud.cpp TurretAimMarks); V2, who
// turns the player's turret: this plugin says when its turret camera turns a seat after the view (CameraTurret), that
// one when it turned it this frame (Steers, turretcam.cpp asks). Either DLL alone: nothing found, nothing drawn, the
// other works as before.
#include "crew.h"
#include "turretaim.h"
#include "memory.h"
#include "spot_ray.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
edf::aimlink::TurretReadoutFn turretReadout=nullptr;
edf::aimlink::SeatQueryFn turretSteers=nullptr;
edf::aimlink::AwareFn turretAware=nullptr;
edf::aimlink::ModeBindingFn turretBinding=nullptr;
edf::aimlink::PlayerAimFn playerAim=nullptr;
ULONGLONG bindingTried=0;
ULONGLONG turretTried=0,steersTried=0,awareTried=0,playerAimTried=0;
}  // namespace

// --- The stock spot (原版 Q 标记) from a vehicle: cast along the camera actually drawn (spot_ray.h) ---
namespace {
constexpr unsigned kSpotCall=0x59B75C,kSpotCast=0x5A1120,kSpotLift=0x5A11EC,kSpotLiftValue=0x1C369B8,kSpotReach=0x1765A80;
constexpr std::size_t kSoldierVehicleCtrl=0x1550;
// 0x5A1120's prologue; the riding branch (cmp [rbx+D7A],r15b; je; ...; mov rax,[rbx+1550]; test; je; cmp [rax+8],r15d;
// je; mov rdi,[rbx+1540]; add rdi,200); the lift (addss xmm0,[rip+..]).
const unsigned char kSpotCastCode[]={0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8D,0xAC,0x24,0xE0,0xFE,0xFF,0xFF};
const unsigned char kSpotBranchCode[]={0x44,0x38,0xBB,0x7A,0x0D,0x00,0x00,0x0F,0x84,0xCB,0x00,0x00,0x00,0x49,0x8B,0xFF,
                                       0x48,0x8B,0x83,0x50,0x15,0x00,0x00};
const unsigned char kSpotLiftCode[]={0xF3,0x0F,0x58,0x05,0xC4,0x57,0x69,0x01};
using SpotCastFn=void(__fastcall*)(void*,const float*,const float*);
SpotCastFn stockSpot=nullptr;
// The local player riding (the stock riding test, soldier+0x1550 alive) under a camera the HUD drew: the drawn camera's
// ray. On foot, another soldier, the map's view or no camera yet: the stock arguments.
bool SpotFromView(const unsigned char* soldier,float* origin,float* dir) noexcept {
    __try {
        if(!Cfg().enabled || !soldier || soldier!=PlayerHuman() || !IsPlayer(soldier) || MapOwnsView())return false;
        const auto ctrl=At<const unsigned char*>(soldier,kSoldierVehicleCtrl);
        if(!ctrl || !Readable(ctrl,12) || At<std::int32_t>(ctrl,8)==0)return false;
        float eye[3],look[3];
        return CameraRay(eye,look) && spotray::NativeArgs(eye,look,origin,dir);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall SpotHook(void* soldier,const float* origin,const float* dir) {
    alignas(16) float o[4],d[4];
    if(SpotFromView(static_cast<const unsigned char*>(soldier),o,d)){stockSpot(soldier,o,d);return;}
    stockSpot(soldier,origin,dir);
}
}  // namespace

bool InstallSpotRay() noexcept {
    __try {
        float lift=0.0f,reach=0.0f;
        const bool code=image && Matches(kSpotCast,kSpotCastCode,sizeof(kSpotCastCode)) &&
            Matches(0x59B689,kSpotBranchCode,sizeof(kSpotBranchCode)) && Matches(kSpotLift,kSpotLiftCode,sizeof(kSpotLiftCode));
        if(code){std::memcpy(&lift,image+kSpotLiftValue,4);std::memcpy(&reach,image+kSpotReach,4);}
        if(!code || lift!=spotray::kLift || reach!=spotray::kReach) {
            Log("HOOK spot ray=0 (unexpected EDF.dll code: the stock spot keeps the stock riding camera's ray)");
            return false;
        }
        stockSpot=reinterpret_cast<SpotCastFn>(image+kSpotCast);
        bool changed=false;
        const bool ok=RedirectCall(image+kSpotCall,image+kSpotCast,reinterpret_cast<void*>(&SpotHook),changed);
        Log("HOOK spot ray=%d (the stock spot from a vehicle goes where the drawn camera looks)",ok);
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool PlayerTurretLead(const void* vehicle,unsigned seat,const void* gun,float* point) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kPlayerAim,playerAim,playerAimTried);
    link::PlayerAimV4 r{};
    if(!Cfg().enabled || !fn || !fn(vehicle,seat,gun,&r) || !r.steer)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(r.point[i]))return false;
    std::memcpy(point,r.point,sizeof(r.point));
    return true;
}

bool AutoTurretReadout(edf::aimlink::TurretReadoutV1* out) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kTurretReadout,turretReadout,turretTried);
    return fn && fn(out);
}

bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept {
    return Cfg().enabled && Cfg().turretAimHud && AutoTurretReadout(out);
}

bool PlayerTurretBinding(edf::aimlink::ModeBindingV1* out) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kModeBinding,turretBinding,bindingTried);
    return Cfg().enabled && Cfg().turretAimHud && fn && fn(out);
}

bool CurrentTurretPlayer(const void* object,unsigned index) noexcept {
    const auto v=static_cast<unsigned char*>(const_cast<void*>(object));
    if(!Readable(v,kSeatCount+8) || v[kDead] || index>=SeatCount(v))return false;
    const auto seat=SeatAt(v,index);
    return SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman();
}

int AutoTurretSteers(const void* vehicle,unsigned seat) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kSteers,turretSteers,steersTried);
    return fn ? (fn(vehicle,seat) ? 1 : 0) : -1;
}

// Asked per aim step of every held seat: the module looked up at most once a second (plugins are never unloaded, so
// once found it stays; once aware, always aware).
int AutoTurretStabAware() noexcept {
    namespace link=edf::aimlink;
    static int known=-1;
    static ULONGLONG checkedAt=0;
    if(known==1)return 1;
    const ULONGLONG now=GetTickCount64();
    if(checkedAt && now-checkedAt<1000)return known;
    checkedAt=now;
    if(!GetModuleHandleW(link::kTurretDll))return known=-1;
    const auto fn=link::Resolve(link::kTurretDll,link::kStabilizerAware,turretAware,awareTried);
    return known=fn && fn() ? 1 : 0;
}
}  // namespace crew

extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_ViewRayV1(float* eye,float* dir) {
    if(!eye || !dir)return false;
    __try { return crew::CameraRay(eye,dir); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// EDF6AutoTurret asks whether the turret camera turns `vehicle`'s seat `seat` after the view (aimlink.h CameraTurret).
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_CameraTurretV2(const void* vehicle,unsigned seat) {
    __try { return crew::TurretCamTurret(vehicle,seat); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// EDF6AutoTurret asks whether the turret camera steers `vehicle`'s seat `seat` onto its PlayerAimV4 point (aimlink.h V4):
// the camera's decoupled seat is the one hand on the player's turret.
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_AimsTurretV4(const void* vehicle,unsigned seat) {
    __try { return crew::TurretCamTurret(vehicle,seat); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

extern "C" __declspec(dllexport) float __cdecl EDF6VehicleCrew_MapRayV1(const float* a,const float* b,float* hit) {
    if(!a || !b || !hit)return -1.0f;
    __try { return crew::MapRay(a,b,hit); }
    __except(EXCEPTION_EXECUTE_HANDLER){return -1.0f;}
}

// Reserve a configured sight binding before the first press, not only while zoomed.
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_SightBindingV1(const void* vehicle,unsigned seat,bool keys,int binding) {
    using namespace crew;
    __try {
        if(!Cfg().enabled || !Cfg().sightZoom || binding<=0 || !CurrentTurretPlayer(vehicle,seat))return false;
        if(binding!=(keys ? Cfg().sightZoomKey : Cfg().sightZoomButton))return false;
        return SightZoomCanMount(vehicle,seat);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_TurretObserverV1(const void* vehicle,unsigned seat) {
    using namespace crew;
    __try {return Cfg().enabled && CurrentTurretPlayer(vehicle,seat) && (HighCamOn(vehicle) || TurretCamHighTransition(vehicle) || SightZoomMounted(vehicle));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
