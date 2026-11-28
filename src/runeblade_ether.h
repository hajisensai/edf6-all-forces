// The RuneBlade's ether gauge (docs/plan-2026-11-28-zero-worldline-gear.md §2.1; docs/newclass-re/gauge.md §1, §7): the
// minimal interface RB-MELEE needs until RB-CORE (the gauge's owner: its refill on the ground, its HUD, its online sync)
// lands; the parent session aligns the two when they merge.
// The gauge is the stock energy every GameObject carries (GameObject ctor 0x545670, 0x546272..0x54628D): +0x304 the
// most, +0x308 the current, +0x2FC the "spent" flag (1: emptied, the stock HUD's overheated style; cleared again once
// full). Only PaleWing's code writes them in stock (gauge.md §1), so on a RuneBlade they are the class's own.
// The pure part (Gauge) is what tests/runeblade_melee_test.cpp checks; Read / Spend / Drain touch a live soldier and are
// for the game thread, the caller's SEH around them (runeblade_melee.cpp).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::runeblade::ether {

constexpr std::size_t kMost=0x304,kNow=0x308,kSpent=0x2FC;

struct Gauge {
    float now=0.0f,most=0.0f;
    bool spent=false;   // emptied and not yet full again: nothing may spend
};

inline bool Valid(const Gauge& g) noexcept {
    return std::isfinite(g.now) && std::isfinite(g.most) && g.most>0.0f && g.now>=0.0f && g.now<=g.most;
}
// Whether `amount` (> 0) can be paid now: not spent, and enough left.
inline bool CanSpend(const Gauge& g,float amount) noexcept {
    return Valid(g) && !g.spent && std::isfinite(amount) && amount>0.0f && g.now+1e-4f>=amount;
}
// Pays `amount` (false and unchanged when it cannot); reaching 0 sets `spent`.
inline bool Spend(Gauge& g,float amount) noexcept {
    if(!CanSpend(g,amount))return false;
    g.now-=amount;
    if(g.now<=1e-4f){g.now=0.0f;g.spent=true;}
    return true;
}
// Takes all that is left (the hammer's smash, §2.2 M4); the amount taken (0: nothing, or spent).
inline float Drain(Gauge& g) noexcept {
    if(!Valid(g) || g.spent || g.now<=0.0f)return 0.0f;
    const float took=g.now;
    g.now=0.0f;g.spent=true;
    return took;
}

// ---- a live soldier (game thread, the caller's SEH) ----
inline bool Read(const void* soldier,Gauge* out) noexcept {
    if(!soldier || !out)return false;
    const auto* s=static_cast<const unsigned char*>(soldier);
    Gauge g;
    std::memcpy(&g.most,s+kMost,4);std::memcpy(&g.now,s+kNow,4);
    std::int32_t spent=0;std::memcpy(&spent,s+kSpent,4);
    g.spent=spent==1;
    if(!Valid(g))return false;
    *out=g;
    return true;
}
inline void Write(void* soldier,const Gauge& g) noexcept {
    auto* s=static_cast<unsigned char*>(soldier);
    std::memcpy(s+kNow,&g.now,4);
    const std::int32_t spent=g.spent ? 1 : 0;
    std::memcpy(s+kSpent,&spent,4);
}
inline bool Spend(void* soldier,float amount) noexcept {
    Gauge g;
    if(!Read(soldier,&g) || !Spend(g,amount))return false;
    Write(soldier,g);
    return true;
}
inline float Drain(void* soldier) noexcept {
    Gauge g;
    if(!Read(soldier,&g))return 0.0f;
    const float took=Drain(g);
    if(took>0.0f)Write(soldier,g);
    return took;
}

}  // namespace crew::runeblade::ether
