// How each fixed wing the player flies answers the stick (playerjet.cpp Roll / AimSteer / Air), per kind (2026-10-06, the
// user: "the airframes that turn stiffly anyway turn even more awkwardly and slowly since some change").
//
// What was wrong (tools/pjet_turn_sim.cpp --both prints before / after):
//  - the path rolled at kRollRate 4.2 rad/s for every kind (tuned 2026-10-04 for the hand-made fighter and strike jet),
//    while the body, and the camera on its seat (docs/camera-re.md §1), follows at most Kind::roll (body506.cpp
//    BodyAttitude caps the whole angular velocity): 1.0 rad/s for the gunship, 1.2 the bombers, 1.6 the strike jet. Since
//    every plugin aircraft became boardable (2026-10-05) the big ones' path banked and turned while their body and the
//    view were still rolling into it, 54-68 deg behind: the turn seemed late and slow, and nothing matched the stick;
//  - a full turn stick (a pad's right stick) banked every kind to kTurnBank 69 deg (2.8 g level): the 6 g fighter turned
//    no harder than the 3 g bomber, and the 2 g gunship could not hold the bank and sank;
//  - the mouse's aim asked kSteer * angle * speed of sideways lift for any correction: a few degrees off banked 60-72 deg.
// Now (the NPC's own numbers still set each kind apart, Kind as playerjet_kinds.h derives it):
//  - PathRoll: the path rolls at kPathRollShare x the kind's roll, kPathRollLeast..kRollRate, times ini PlayerJetRollScale;
//  - BodyCap: the body's cap keeps up with that roll and the kind's hardest pitch (its maxG at the corner speed);
//  - TurnBank: a full turn stick banks to what the wing holds level at kTurnBankG of its g at its cruise speed (at most
//    kTurnBankMost; Hold coordinates the turn up to kMinBankCos past it);
//  - AimShare / AimRoll: the aim's sideways demand eases in over kAimFine of the angle off, and the roll rate toward it
//    with it (no step at kAimTurnFrom).
// Pure arithmetic (no game state): tools/pjet_turn_sim.cpp runs the same functions offline.
#pragma once
#include <cmath>

namespace crew {
namespace handling {
constexpr float kG=9.8f;
constexpr float kRollRate=4.2f;        // rad/s: the most a path rolls (the fighter's, a full roll in 1.5 s)
constexpr float kLevelRate=1.8f;       // rad/s: wings levelled / a turn's bank taken, let go
constexpr float kPathRollShare=1.6f,kPathRollLeast=2.0f;
constexpr float kBodyRollOver=1.15f;   // the body's cap over the path's roll
constexpr float kTurnBankG=0.95f,kTurnBankMost=1.3f;   // 74.5 deg at most
constexpr float kMinBankCos=0.2f;      // Hold's coordinated turn up to 78 deg of bank (was 0.25: 75.5 deg, under kTurnBankMost)
constexpr float kCruiseThrottle=0.55f; // playerjet.cpp's: the throttle let go in the air
constexpr float kAimFine=0.35f,kAimFineLeast=0.25f,kAimRollFull=0.3f;   // rad

inline float Clamp(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }

// rad/s the path rolls at for a kind rolling `roll` (Kind::roll), times the ini's `scale`.
inline float PathRoll(float roll,float scale) noexcept { return Clamp(kPathRollShare*roll,kPathRollLeast,kRollRate)*scale; }
// rad/s the body (and the camera) may turn at: the path's roll and the kind's hardest pitch, maxG at `corner`.
inline float BodyCap(float roll,float maxG,float corner,float scale) noexcept {
    return kBodyRollOver*PathRoll(roll,scale)+maxG*kG/(corner>1.0f ? corner : 1.0f);
}
// The kind's cruise (m/s): the speed its engine holds at the throttle let go.
inline float Cruise(float minAir,float top) noexcept { return minAir+kCruiseThrottle*(top-minAir); }
// rad: the bank of a full turn stick.
inline float TurnBank(float maxG,float corner,float minAir,float top) noexcept {
    const float cruise=Cruise(minAir,top),wing=cruise<corner ? (cruise/corner)*(cruise/corner) : 1.0f;
    const float g=kTurnBankG*maxG*wing;
    return g>1.0f ? std::fmin(std::acos(1.0f/g),kTurnBankMost) : 0.0f;
}
// The share of the aim's sideways demand at `off` rad from it.
inline float AimShare(float off) noexcept { return Clamp(off/kAimFine,kAimFineLeast,1.0f); }
// rad/s it banks toward the aim's lift at `off` rad from it: from the gentle levelling rate on the aim to the path's
// roll kAimRollFull off.
// On the ground (rolling or parked, playerjet.cpp Ground; a rotor craft set down) the airframe's contacts own its motion
// along the ground's normal and its pitch and roll; the plugin owns only its motion along the ground and its yaw.
// `lin` / `ang` come in as the solver left them (body506.cpp PhysicsHook reads them before the stock step) and go out
// as the body's velocity: the plugin's `vel` across the normal `up` (the body's own up row: its contacts' plane) with
// the solver's along it, the solver's spin across it with the plugin's `omega` about it.
// The user (2026-10-09): 「飞机没起飞的时候，会在地上一抖一抖的」. Before, every frame overwrote the whole velocity and
// spin: rolling, a vertical velocity of min(0, measured) (the solver's push out dropped, its push in fed back) and a spin
// onto a 6 m probe plane that is not its wheels' (8 m apart); parked, the stock heli step pulled it level against its
// gear (0x654E0F: any contact zeroes the attitude target; 0x6CE8D0's spring at 9/s). Either pressed a wheel into the
// ground for the solver to push it back out, the next frame again (tools/ground_contact_check.cpp).
inline void GroundContact(const float* up,const float* vel,const float* omega,float* lin,float* ang) noexcept {
    float n[3]={up[0],up[1],up[2]};
    const float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(!(l>1e-3f) || !std::isfinite(l)){n[0]=0.0f;n[1]=1.0f;n[2]=0.0f;}
    else for(float& c:n)c/=l;
    const float vn=vel[0]*n[0]+vel[1]*n[1]+vel[2]*n[2],sn=lin[0]*n[0]+lin[1]*n[1]+lin[2]*n[2];
    const float wn=omega[0]*n[0]+omega[1]*n[1]+omega[2]*n[2],an=ang[0]*n[0]+ang[1]*n[1]+ang[2]*n[2];
    for(int i=0;i<3;++i){lin[i]=vel[i]+n[i]*(sn-vn);ang[i]=ang[i]+n[i]*(wn-an);}
}

// Without the solver's velocity (body506.cpp: its getters' code not as expected, Body506ReadsSolver false) the plugin
// cannot leave the normal motion to the contacts: writing its own whole, a parked craft's 0 every frame cancelled the
// gravity, and over ground that fell away (a crater, the edge of a roof) it hung in the air. Then:
//  - a parked craft is the stock step's again (Drives false; it falls and settles as it always did);
//  - a rolling one keeps the measured motion along the normal (its fall, `measured`, m/s) with the plugin's along the
//    ground and its spin (GroundFallback), the pitch and roll unchanged: it follows the ground down.
constexpr bool Drives(bool parked,bool readsSolver) noexcept { return !parked || readsSolver; }
inline void GroundFallback(const float* up,const float* vel,const float* omega,const float* measured,float* lin,float* ang) noexcept {
    float solver[3]={measured[0],measured[1],measured[2]},spin[3]={0.0f,0.0f,0.0f};
    if(!std::isfinite(solver[0]+solver[1]+solver[2])){solver[0]=solver[1]=solver[2]=0.0f;}
    GroundContact(up,vel,omega,solver,spin);
    for(int i=0;i<3;++i){lin[i]=solver[i];ang[i]=spin[i];}
}

// The way it rolls (playerjet.cpp Ground): the level nose `nose` laid along the plane its wheels stand on (`up`, unit: the
// body's up row), unit; false when the nose lies along `up` (`along` then unset).
inline bool AlongPlane(const float* nose,const float* up,float* along) noexcept {
    const float lift=nose[0]*up[0]+nose[1]*up[1]+nose[2]*up[2];
    for(int i=0;i<3;++i)along[i]=nose[i]-up[i]*lift;
    const float l=std::sqrt(along[0]*along[0]+along[1]*along[1]+along[2]*along[2]);
    if(!(l>1e-4f) || !std::isfinite(l))return false;
    for(int i=0;i<3;++i)along[i]/=l;
    return true;
}
// Its speed along the ground now: `vel`, what the ground step sent last frame (along last frame's plane), along `ahead`,
// the way it rolls now (AlongPlane of the level nose). The user (2026-10-09): 「这个飞机起飞的时候撞到东西了，然后弹来弹去的」.
// Until then it was read along the level nose: the velocity lies along the body's plane, so a body pitched by theta (a slope,
// rubble under a wheel, the nose up against what it ran into) kept cos(theta) of its speed a frame: 10 deg took 60% of it
// a second, and the throttle full held it at ~11 m/s; on bumps it fell from 34 m/s to 8 within 2 s (tools/
// ground_contact_check.cpp "rolling with the throttle").
inline float RollSpeed(const float* vel,const float* ahead) noexcept {
    return vel[0]*ahead[0]+vel[1]*ahead[1]+vel[2]*ahead[2];
}
// Whether, rolling `clear` m over the floor (`noGround`: none seen) at `speed` m/s along it, it is flying: over `offGround`
// and at least `floor` (the air's least speed, playerjet.cpp kStallFloor). Slower it is not flying but falling, or lifted
// by what it rolled onto: it stays on its contacts (GroundContact: the fall is the solver's) and lands back on its wheels.
// Handed to the air at 1 m/s, the flight made that 25 m/s along its nose, pitched down off what had lifted it: a 20 m/s
// dive into the ground 6 m under it, destroyed (the log of 2026-10-09 19:21:26).
constexpr bool RollsIntoAir(float clear,float noGround,float offGround,float speed,float floor) noexcept {
    return clear!=noGround && clear>offGround && speed>=floor;
}

inline float AimRoll(float pathRoll,float off) noexcept {
    const float lo=std::fmin(kLevelRate,pathRoll),t=Clamp(off/kAimRollFull,0.0f,1.0f);
    return lo+(pathRoll-lo)*t;
}
}  // namespace handling
}  // namespace crew
