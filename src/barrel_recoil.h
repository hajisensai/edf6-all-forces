// Each twin barrel's own recoil on its own shot (the BMPT's 2A42 pair: the user, 2026-10-21: 「bmpt应该是双炮管轮流开火的，
// 而且开火动画没了」; docs/feedback-2026-10-21-bmpt-fire.md). The V603's native firing motion is one 'Fire' state on the
// 'main' CAS player for any holder's shot (slot 48 0x621700 -> 0x116BFC0): fire_loop, a 12-frame jitter of both
// cannon_slide bones together on one shared translation channel (at most 4.4 cm). It cannot show which barrel fired,
// so with the barrels in turn (twin_cadence.h) each barrel's slide is also driven back by its own shot here: a kick of
// kTravel along the barrel at the frame it fires, run out again over kReturnFrames. The bone is the one the barrel's
// muzzle hangs on (the weapon's first muzzle's bone record: its holder's vehicle_weapon_setting bone), not a name or a
// position: another model's barrels recoil the same way. Applied at the 603's last pose step before its world build
// (vulcan_rotor.cpp Build, 0x621654), on top of whatever the CAS left in the local this frame, never on its own last
// write (Base): a frame the CAS does not rewrite the local does not add the kick twice. Pure: barrel_recoil.cpp reads
// the weapons and the bone records.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
void BarrelRecoilPose(unsigned char* vehicle) noexcept;   // vulcan_rotor.cpp Build, before the native world build
void ResetBarrelRecoil() noexcept;
namespace recoil {
// A 30 mm autocannon's barrel run back: the 2A42's recoil travel is a few centimetres; drawn at 3rd-person distance
// that is hardly seen, so the kick is a visible 12 cm, out again in 5 frames (well inside the pair's 6-frame spacing at
// FireInterval 12: one barrel is back in battery as the other fires).
constexpr float kTravel=0.12f;
constexpr int kReturnFrames=5;
// How far back the barrel is `since` frames after its shot (0 the shot's own frame): the full travel, then out again.
constexpr float Offset(std::uint64_t since) noexcept {
    return since>=static_cast<std::uint64_t>(kReturnFrames) ? 0.0f :
        kTravel*(1.0f-static_cast<float>(since)/static_cast<float>(kReturnFrames));
}
// A barrel's shot this frame from its own counters (a frame old: `prior`): rounds down by its own shots (this machine
// fired it), or the shots received from its operator's machine up past those played (a copy: 0x690420 stores them,
// the same test as vulcanrotor::Observe). A reload (rounds up), a new weapon (no prior) and old counters are no shot.
struct Counters { const void* weapon=nullptr;std::int32_t rounds=0,received=0,played=0; };
constexpr bool Shot(const Counters& prior,const Counters& now) noexcept {
    return now.weapon && prior.weapon==now.weapon &&
        ((now.rounds>=0 && now.rounds<prior.rounds) || (now.received>prior.received && now.received>now.played));
}
// The local (rows right, up, forward, position) moved `back` along its own forward row. False: not a frame to move.
inline bool Back(const float* base,float back,float* out) noexcept {
    std::memcpy(out,base,64);
    const float* f=base+8;
    const float n=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(!std::isfinite(n) || n<0.5f || n>2.0f || !std::isfinite(back))return false;
    for(int c=0;c<3;++c)out[12+c]=base[12+c]-back*f[c]/n;
    for(int i=0;i<16;++i)if(!std::isfinite(out[i]))return false;
    return true;
}
// What the CAS left in a bone's local this frame: `current`, unless it is still exactly what this file wrote last
// (no animation rewrote it since), then the base it was written from.
struct Bone { const void* record=nullptr;float written[16]{},base[16]{};bool known=false; };
inline const float* Base(const Bone& bone,const float* current) noexcept {
    return bone.known && std::memcmp(current,bone.written,64)==0 ? bone.base : current;
}
}  // namespace recoil
}  // namespace crew
