// The turrets' aim link with EDF6AutoTurret (common/edf/aimlink.h): this plugin hands it the camera's view ray and the
// map ray its lock-by-look needs, and takes its player turret readout for the HUD (hud.cpp TurretAimMarks); V2, who
// turns the player's turret: this plugin says when its turret camera turns a seat after the view (CameraTurret), that
// one when it turned it this frame (Steers, turretcam.cpp asks). Either DLL alone: nothing found, nothing drawn, the
// other works as before.
#include "crew.h"
#include "turretaim.h"
#include "memory.h"

namespace crew {
namespace {
edf::aimlink::TurretReadoutFn turretReadout=nullptr;
edf::aimlink::SeatQueryFn turretSteers=nullptr;
edf::aimlink::AwareFn turretAware=nullptr;
edf::aimlink::ModeBindingFn turretBinding=nullptr;
ULONGLONG bindingTried=0;
ULONGLONG turretTried=0,steersTried=0,awareTried=0;
}  // namespace

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
        const auto kind=SightZoomView(vehicle);
        return sightzoom::Magnifies(kind);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_TurretObserverV1(const void* vehicle,unsigned seat) {
    using namespace crew;
    __try {return Cfg().enabled && CurrentTurretPlayer(vehicle,seat) && (HighCamOn(vehicle) || TurretCamHighTransition(vehicle));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
