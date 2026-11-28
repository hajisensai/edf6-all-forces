// V7 1K17's beam heat and V8 TV-8's radiation zone (docs/newclass-re/vehicler-v4v9-plan.md §3; zw_vehicle_behavior.cpp):
// the pure part, frame counts at the game's 60 a second, no game (tests/zw_v4v9_test.cpp).
//
// Heat: 0 cold .. 1 overheated. Each frame the beam fires it rises by kRise (about kFireFrames of firing to the top), each frame
// it does not it falls by kCool. The damage goes with it: Damage() = 1 + kGain x heat (x3 hot). At 1 the beam overheats: it
// cannot fire until the heat is back to kResume, falling by kOverCool a frame (a forced cool-down, longer than the plain one).
//
// Fallout: where a TV-8 is destroyed, a zone of kZoneRadius m that deals kZoneDamage every kZonePeriod frames for kZoneFrames.
#pragma once
#include <cstdint>

namespace crew::zwheat {
inline constexpr int kFireFrames=360;   // 6 s of beam from cold to overheated
inline constexpr float kRise=1.0f/kFireFrames;
inline constexpr float kCool=1.0f/240.0f;       // 4 s from the top idling (not overheated)
inline constexpr float kOverCool=1.0f/300.0f;   // overheated: 5 s down to kResume
inline constexpr float kResume=0.0f;
inline constexpr float kGain=2.0f;

struct Heat {
    float heat=0.0f;
    bool overheated=false;
};
constexpr float Damage(const Heat& h) noexcept { return 1.0f+kGain*h.heat; }
// One frame: `firing` the beam fired this frame (its rounds went down). Returns whether it may fire next frame.
constexpr bool Step(Heat& h,bool firing) noexcept {
    if(h.overheated) {
        h.heat-=kOverCool;
        if(h.heat<=kResume){h.heat=kResume;h.overheated=false;}
        return !h.overheated;
    }
    h.heat+=firing ? kRise : -kCool;
    if(h.heat<0.0f)h.heat=0.0f;
    if(h.heat>=1.0f){h.heat=1.0f;h.overheated=true;}
    return !h.overheated;
}
// The beam's cooldown (weapon +0xE0C, frames) held while overheated: at least this, so the stock update never lets it fire.
inline constexpr float kHoldCooldown=2.0f;

inline constexpr float kZoneRadius=15.0f;
inline constexpr float kZoneDamage=60.0f;
inline constexpr int kZonePeriod=60;
inline constexpr int kZoneFrames=30*60;
struct Zone {
    float pos[3]{};
    int left=0;     // frames
    int next=0;     // frames to the next pulse
};
constexpr Zone StartZone(const float* pos) noexcept {
    Zone z;
    z.pos[0]=pos[0];z.pos[1]=pos[1];z.pos[2]=pos[2];
    z.left=kZoneFrames;z.next=0;
    return z;
}
// One frame of a zone; true: pulse now (deal kZoneDamage in kZoneRadius). A zone with left 0 is over.
constexpr bool Tick(Zone& z) noexcept {
    if(z.left<=0)return false;
    --z.left;
    if(z.next>0){--z.next;return false;}
    z.next=kZonePeriod-1;
    return true;
}
}  // namespace crew::zwheat
