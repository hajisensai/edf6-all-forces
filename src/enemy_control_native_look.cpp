// Convert a persistent semantic camera target to each admitted native family's
// pad input. Native target angles integrate every native frame; the smoothed
// rendered angles must never be used as the feedback state (that overshoots).
#include "enemy_control_native.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew::enemy_native {
bool AimToPadExtra(const NativeLease&,float,float,Input*) noexcept;
namespace {
struct LookFingerprint { std::uint32_t rva;unsigned char bytes[16]; };
constexpr LookFingerprint kWalkingLookPrints[]={
    {0x53E834,{0x48,0x8B,0x83,0x40,0x03,0x00,0x00,0x8B,0x48,0x54,0x89,0x8B,0xA0,0x12,0x00,0x00}},
    {0x53E856,{0xF3,0x0F,0x10,0x40,0x3C,0x48,0x8B,0xD7,0x48,0x8B,0x03,0x0F,0x57,0xC6,0x48,0x8B}},
    {0x53D5B6,{0x0F,0x10,0x91,0xA0,0x12,0x00,0x00,0xF3,0x0F,0x10,0x81,0x9C,0x0F,0x00,0x00,0x0F}},
    {0x53D5F2,{0x0F,0x58,0xD9,0x0F,0xC6,0xDB,0x93,0xF3,0x0F,0x10,0xD8,0x0F,0xC6,0xDB,0x39,0x0F}},
    {0x53D62D,{0xF3,0x0F,0x10,0xA1,0x44,0x14,0x00,0x00,0xF3,0x0F,0x10,0x91,0x4C,0x14,0x00,0x00}},
};
constexpr LookFingerprint kHumanoidLookPrints[]={
    {0x470E15,{0x48,0x8B,0x87,0x40,0x03,0x00,0x00,0xF3,0x0F,0x10,0x40,0x3C,0xEB,0x0C,0xF3,0x0F}},
    {0x470E33,{0xF3,0x0F,0x11,0x87,0xE4,0x11,0x00,0x00,0x8B,0x40,0x54,0x89,0x87,0xE0,0x11,0x00}},
    {0x470E4C,{0x0F,0x28,0x0D,0xFD,0xA1,0x2F,0x01,0x0F,0x59,0x8F,0xE0,0x11,0x00,0x00,0x0F,0xC6}},
    {0x472308,{0x0F,0x10,0x87,0xDC,0x10,0x00,0x00,0x0F,0x10,0x8F,0xE0,0x11,0x00,0x00,0x0F,0x58}},
    {0x472329,{0x0F,0x11,0x8F,0xD0,0x10,0x00,0x00,0xF3,0x0F,0x10,0x87,0xD0,0x10,0x00,0x00,0xF3}},
    {0x176B050,{0x89,0x88,0x08,0x3D,0x89,0x88,0x08,0x3D,0x89,0x88,0x08,0x3D,0x89,0x88,0x08,0x3D}},
};
template<std::size_t N> bool LookMatches(const LookFingerprint (&prints)[N]) noexcept {
    if(!image)return false;
    for(const auto& p:prints)
        if(!Readable(image+p.rva,sizeof(p.bytes)) || std::memcmp(image+p.rva,p.bytes,sizeof(p.bytes))!=0)return false;
    return true;
}
float LookAxis(double difference,double step) noexcept {
    const double value=difference/step;
    return static_cast<float>(value<-1.0 ? -1.0 : value>1.0 ? 1.0 : value);
}
}
bool AimToPad(const NativeLease& lease,float desiredYaw,float semanticPitch,Input* out) noexcept {
    if(!out || !lease.object || !lease.profile || !lease.control ||
       !std::isfinite(desiredYaw) || !std::isfinite(semanticPitch))return false;
    __try {
        if(!Readable(lease.object,0x354+1) || !Readable(lease.control,12) ||
           At<const void*>(lease.object,0x30)!=lease.control || At<LONG>(lease.control,8)<=0 ||
           (lease.object[0x18]&4) || lease.object[0x2E8])return false;
        std::size_t targetAt=0;
        double step=0;
        if(lease.profile->kind==Kind::walking && lease.profile->inputRva==0x343DD0) {
            if(!LookMatches(kWalkingLookPrints) || !Readable(lease.object+0xF9C,sizeof(float)))return false;
            // 0x53E834 / 0x53E867: command=(pad54,-pad3C).
            // 0x53D5B6..605: target1450 += command*F9C, no dt multiplier.
            targetAt=0x1450;step=At<float>(lease.object,0xF9C);
        } else if(lease.profile->kind==Kind::humanoid &&
                  (lease.profile->inputRva==0x444E90 || lease.profile->inputRva==0x461590 ||
                   lease.profile->inputRva==0x462E30 || lease.profile->inputRva==0x4A6780)) {
            if(!LookMatches(kHumanoidLookPrints))return false;
            // 0x470E33..6A scales the same signed command by exactly 1/30;
            // 0x472308..329 adds it to target10D0, once per native frame.
            targetAt=0x10D0;step=static_cast<double>(1.0f/30.0f);
        } else return AimToPadExtra(lease,desiredYaw,semanticPitch,out);
        if(!Readable(lease.object+targetAt,2*sizeof(float)) || !std::isfinite(step) || step<=0)return false;
        const double targetPitch=At<float>(lease.object,targetAt),targetYaw=At<float>(lease.object,targetAt+4);
        if(!std::isfinite(targetPitch) || !std::isfinite(targetYaw))return false;
        constexpr double kPi=3.14159265358979323846;
        if(std::fabs(semanticPitch)>kPi/2)return false;
        // Native Rx has forward.y=-sin(pitch); semantic pitch is elevation.
        const double pitchError=-static_cast<double>(semanticPitch)-targetPitch;
        const double yawError=std::remainder(static_cast<double>(desiredYaw)-targetYaw,2*kPi);
        const float pitch=LookAxis(pitchError,step),yaw=-LookAxis(yawError,step);
        out->pitch=pitch;out->yaw=yaw;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace crew::enemy_native
