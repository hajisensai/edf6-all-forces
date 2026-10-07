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
// The ground's up under a wing rolling on it (playerjet.cpp Ground), from the floor's heights `fore` / `aft` along its
// level unit `nose` and `right` / `left` along the level unit `side`, `span` m either side of its centre. The body
// follows the slope its wheels stand on: held level on uneven ground it was turned against its contacts every physics
// step and shook (the user, 2026-10-07: "the aircraft shake on the ground"). Tilted at most `most` rad: a steeper
// floor than that is no runway (a rock, a wall's foot), the contacts alone have that.
inline void GroundUp(float fore,float aft,float right,float left,float span,const float* nose,const float* side,float most,
                     float* up) noexcept {
    const float gf=(fore-aft)/(2.0f*span),gs=(right-left)/(2.0f*span);   // the rise a metre along nose / side
    float n[3];
    for(int i=0;i<3;++i)n[i]=-gf*nose[i]-gs*side[i];
    n[1]+=1.0f;
    const float h=std::sqrt(n[0]*n[0]+n[2]*n[2]);
    if(h>std::tan(most)*n[1]){const float k=std::tan(most)*n[1]/h;n[0]*=k;n[2]*=k;}
    const float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    for(int i=0;i<3;++i)up[i]=n[i]/l;
}

inline float AimRoll(float pathRoll,float off) noexcept {
    const float lo=std::fmin(kLevelRate,pathRoll),t=Clamp(off/kAimRollFull,0.0f,1.0f);
    return lo+(pathRoll-lo)*t;
}
}  // namespace handling
}  // namespace crew
