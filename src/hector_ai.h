// EDF4.1 Hector's attack behaviour that EDF6's Berserker keys cannot say (src/edf41ai.cpp hooks the game with it,
// tools/hector_ai_check.cpp checks it offline). No game, no Windows. Source: EDF41.exe, class Hector (vtable 0xA987E0;
// jobs/4bf89026/tmp/behav_hector/NOTES.md and hector_all.txt there): weapon pick 0x223B10, the AI 0x223470, the phase
// reset 0x223970.
//  - Weapon pick (every 300 + rand(300) frames; EDF6's gun_attack interval in the object's data): the arms whose
//    hector_weapon_range is greater than the horizontal (x, z) distance to the target are the candidates. None: nothing
//    fires. One: that arm. Both: if rand(2) == 0 or the two arms hold the same weapon class (typeid equal) both fire,
//    otherwise one of them, chosen by another rand(2).
//  - Back-off cycle: a counter starts at dwellBase + rand(dwellRand) frames and counts down only while the target is
//    within `near` (150) units, by 2 a frame inside the stop radius (stopMin + rand * (stopMax - stopMin), drawn again
//    every cycle) and by 1 outside it. At zero the Hector walks backward facing the target (speed -1, the same speed
//    as forward) for retreatBase + rand(retreatRand) frames, then the cycle starts again. Inside `near` it walks up
//    to the target in a straight line and stands inside the stop radius.
#pragma once
#include <cstdint>

namespace hector {
constexpr int kArms=2;
constexpr unsigned kArmLeft=1u,kArmRight=2u;       // PickArms' mask: arms_l is gun_control's entry 0

// hector_ai_setting [720, 1440, 540, 720, 150, 20, 80] of the 4.1 object, and what each arm holds.
struct Config {
    float range[kArms]{};       // hector_weapon_range of arms_l, arms_r
    bool sameWeapon=false;      // both arms hold the same weapon class
    float dwellBase=720.0f,dwellRand=1440.0f;
    float retreatBase=540.0f,retreatRand=720.0f;
    float closeIn=150.0f;
    float stopMin=20.0f,stopMax=80.0f;
};

// A small xorshift: the cycle needs rand(n), not a particular sequence.
struct Rng {
    std::uint64_t s=0x9E3779B97F4A7C15ull;
    void Seed(std::uint64_t v) noexcept { s=v ? v : 0x9E3779B97F4A7C15ull; for(int i=0;i<4;++i)Next(); }
    std::uint64_t Next() noexcept {
        s^=s<<13; s^=s>>7; s^=s<<17;
        return s;
    }
    float Unit() noexcept { return static_cast<float>(Next()>>40)/16777216.0f; }                 // [0, 1)
    int Below(int n) noexcept { return n<=1 ? 0 : static_cast<int>(Next()>>33)%n; }               // [0, n)
};

enum class Phase { Approach, Retreat };
enum class Event { None, BackoffStart, BackoffEnd };

struct Cycle {
    Phase phase=Phase::Approach;
    float counter=0.0f;         // frames left of the advance dwell
    int retreatLeft=0;          // frames left of the back-off
    float stopRadius=50.0f;
};

inline int Roll(float base,float spread,Rng& rng) noexcept { return static_cast<int>(base)+rng.Below(static_cast<int>(spread)); }

// The cycle's start: the advance phase with a fresh counter and stop radius.
inline Cycle Begin(const Config& c,Rng& rng) noexcept {
    Cycle y;
    y.phase=Phase::Approach;
    y.counter=static_cast<float>(Roll(c.dwellBase,c.dwellRand,rng));
    y.stopRadius=c.stopMin+rng.Unit()*(c.stopMax-c.stopMin);
    y.retreatLeft=0;
    return y;
}

// One frame with the target `dist` away (horizontal). Returns what changed.
inline Event Step(Cycle& y,const Config& c,Rng& rng,float dist) noexcept {
    if(y.phase==Phase::Retreat) {
        if(--y.retreatLeft>0)return Event::None;
        y=Begin(c,rng);
        return Event::BackoffEnd;
    }
    if(dist>=c.closeIn)return Event::None;
    y.counter-=dist<y.stopRadius ? 2.0f : 1.0f;
    if(y.counter>0.0f)return Event::None;
    y.phase=Phase::Retreat;
    y.retreatLeft=Roll(c.retreatBase,c.retreatRand,rng);
    return Event::BackoffStart;
}

// How the walk is steered this frame. `takeover` false: the game's own walk (outside `near`, it knows the way round
// obstacles). `retreat`: walk away from the target. `go` false: stand (inside the stop radius).
struct Walk {
    bool takeover=false;
    bool retreat=false;
    bool go=true;
};

inline Walk Steer(const Cycle& y,const Config& c,float dist) noexcept {
    Walk w;
    if(y.phase==Phase::Retreat) { w.takeover=true; w.retreat=true; return w; }
    if(dist>=c.closeIn)return w;
    w.takeover=true;
    w.go=dist>y.stopRadius;
    return w;
}

// The arms that start an attack (kArmLeft | kArmRight) with the target `dist` away; 0 when no arm reaches it.
inline unsigned PickArms(const Config& c,Rng& rng,float dist) noexcept {
    const bool left=c.range[0]>dist,right=c.range[1]>dist;
    if(!left && !right)return 0u;
    if(left!=right)return left ? kArmLeft : kArmRight;
    if(rng.Below(2)==0 || c.sameWeapon)return kArmLeft|kArmRight;
    return rng.Below(2)==0 ? kArmLeft : kArmRight;
}
}  // namespace hector
