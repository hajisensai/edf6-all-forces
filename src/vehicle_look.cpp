// Vehicle input owns the observation angles; the final camera only reads them.
// This precedes native input AND plugin flight/weapon controllers, so looking
// round cannot become a command to the turret or the mouse-flight aim.
#include "crew.h"
#include "vehicle_look.h"
#include "fp_view.h"
#include "memory.h"
#include "vecmath.h"
#include <cmath>
#include <atomic>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kHumanVehicle=0x1548,kStick=0x2D0,kPad=0x2B0,kButtons=0x2E8;
constexpr float kPi=3.14159265f,kYawLimit=150*kPi/180,kPitchLimit=80*kPi/180;
constexpr float kReturn=0.22f,kDone=0.001f;
struct Look {
    ObjRef vehicle,human;
    unsigned seat=0;
    ULONGLONG frame=~0ULL,seen=0;
    float yaw=0,pitch=0;
    bool held=false,keys=true,offered=false;
};
Look look{};
std::atomic<bool> cameraReady{false};
SRWLOCK lookLock=SRWLOCK_INIT;
Look ReadLook() noexcept {
    AcquireSRWLockShared(&lookLock);const Look copy=look;ReleaseSRWLockShared(&lookLock);return copy;
}

bool InputAllowed() noexcept {
    if(MapHoldsKeys() || GamePaused())return false;
    DWORD pid=0;GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Current(const Look& s,const void* v) noexcept {
    if(!cameraReady.load() || !Cfg().enabled || !s.offered || !s.vehicle.Is(v) || GameMs()-s.seen>250)return false;
    const auto vehicle=static_cast<const unsigned char*>(v);
    const auto human=static_cast<const unsigned char*>(s.human.obj);
    return human && s.human.Is(human) && !human[kDead] && !vehicle[kDead] &&
        !HumanOnFoot(human) && At<const void*>(human,kHumanVehicle)==v && s.seat<SeatCount(vehicle) &&
        At<const void*>(SeatAt(const_cast<unsigned char*>(vehicle),s.seat),kSeatRider)==human;
}
// Sazabi already uses middle-click/R3 for its own lock. Rebinding free look to
// another key/button enables it there without consuming that existing action.
bool Offered(const unsigned char* v,bool keys) noexcept {
    const Config& c=Cfg();
    const int binding=keys ? c.freeLookKey : c.freeLookButton;
    return binding>0 && !(IsSazabi(v) && (keys ? binding==c.sazabiLockKey : (binding&c.sazabiLockButton)!=0));
}
unsigned char* Begin(unsigned char* v,float* saved) noexcept {
    if(!cameraReady.load() || !Cfg().enabled) {look=Look{};return nullptr;}
    const auto human=PlayerHuman();
    if(!human || human[kDead] || HumanOnFoot(human) || At<const void*>(human,kHumanVehicle)!=v || v[kDead])return nullptr;
    if(!InputAllowed() || HighCamOn(v) || TurretCamHighTransition(v)){look=Look{};return nullptr;}
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i) {
        auto seat=SeatAt(v,i);
        if(At<const void*>(seat,kSeatRider)!=human || !edf::IsPlayer(human))continue;
        if(!look.vehicle.Is(v) || !look.human.Is(human) || look.seat!=i)look=Look{};
        look.vehicle=ObjRef::Of(v);look.human=ObjRef::Of(human);look.seat=i;
        look.keys=At<unsigned char>(seat,kPad)==0;look.offered=Offered(v,look.keys);look.seen=GameMs();
        if(!look.offered){look.yaw=look.pitch=0;look.held=false;return nullptr;}
        if(look.frame!=GameFrame()) {
            look.frame=GameFrame();
            const Config& c=Cfg();
            const bool held=look.keys ? (edf::AsyncKey(c.freeLookKey)&0x8000)!=0 :
                (At<std::uint16_t>(seat,kButtons)&c.freeLookButton)!=0;
            const bool changed=held!=look.held;look.held=held;
            if(held) {
                const float x=At<float>(seat,kStick),y=At<float>(seat,kStick+4);
                const float rate=c.turretCamRate*kPi/180/60;
                if(std::isfinite(x+y)) {
                    look.yaw=vec::Clamp(look.yaw+x*rate,-kYawLimit,kYawLimit);
                    look.pitch=vec::Clamp(look.pitch-y*rate,-kPitchLimit,kPitchLimit);
                }
            } else {
                look.yaw*=1-kReturn;look.pitch*=1-kReturn;
                if(std::fabs(look.yaw)+std::fabs(look.pitch)<kDone)look.yaw=look.pitch=0;
            }
            if(changed && c.debug)Log("VEHICLELOOK v=%p seat %u held=%d yaw=%.2f pitch=%.2f",v,i,held,look.yaw,look.pitch);
        }
        if(!look.held && look.yaw==0 && look.pitch==0)return nullptr;
        std::memcpy(saved,seat+kStick,8);std::memset(seat+kStick,0,8);
        return seat;
    }
    return nullptr;
}
}  // namespace

VehicleLookInput::VehicleLookInput(unsigned char* vehicle) noexcept {
    AcquireSRWLockExclusive(&lookLock);
    __try {
        seat_=Begin(vehicle,saved_);
        if(seat_){vehicle_=vehicle;ctrl_=At<const void*>(vehicle,kSelfCtrl);}
    } __except(EXCEPTION_EXECUTE_HANDLER){look=Look{};seat_=nullptr;}
    ReleaseSRWLockExclusive(&lookLock);
}
VehicleLookInput::~VehicleLookInput() noexcept {
    __try {
        if(seat_ && At<const void*>(vehicle_,kSelfCtrl)==ctrl_)std::memcpy(seat_+kStick,saved_,8);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void VehicleLookSetReady(bool ready) noexcept { cameraReady.store(ready); }
bool VehicleLookActive(const void* vehicle) noexcept {
    const Look s=ReadLook();
    __try { return Current(s,vehicle) && (s.held || s.yaw!=0 || s.pitch!=0); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool VehicleLookCamera(const unsigned char* human,float* matrix) noexcept {
    const Look s=ReadLook();
    __try {
        if(human!=s.human.obj || !Current(s,s.vehicle.obj) || (!s.held && s.yaw==0 && s.pitch==0) || !InputAllowed())return false;
        const float pivot[3]={0,0,0};
        fpview::M4 base;std::memcpy(base.m,matrix,64);
        // Native camera rows point left, up, forward (firstperson.cpp). Local
        // negative yaw looks right; retain the seat's pitch/roll and eye position.
        const auto rotated=fpview::Mul(fpview::MotionMatrix(0,0,0,s.pitch,-s.yaw,0,pivot),base);
        if(!fpview::Finite(rotated))return false;
        std::memcpy(matrix,rotated.m,64);
        static ULONGLONG logAt=0;
        const ULONGLONG ms=GetTickCount64();
        if(Cfg().debug && s.held && ms-logAt>=500) {
            logAt=ms;
            const auto seat=SeatAt(static_cast<unsigned char*>(const_cast<void*>(s.vehicle.obj)),s.seat);
            const float axisYaw=At<float>(seat,kSeatAim+kAimAxes+kAxisAngle);
            const float axisPitch=At<float>(seat,kSeatAim+kAimAxes+kAxisStride+kAxisAngle);
            Log("VEHICLELOOK camera yaw=%.3f pitch=%.3f axes=(%.3f %.3f) forward=(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)",
                s.yaw,s.pitch,axisYaw,axisPitch,base.m[8],base.m[9],base.m[10],rotated.m[8],rotated.m[9],rotated.m[10]);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool VehicleLookHint(bool* keys) noexcept {
    const Look s=ReadLook();
    __try { if(!Current(s,s.vehicle.obj))return false;*keys=s.keys;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
