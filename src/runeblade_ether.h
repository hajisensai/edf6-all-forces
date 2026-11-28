// The Rune Blade's ether gauge for the other modules (docs/newclass-re/runeblade-plan.md §1.1): R3 Seven Stars and M4
// Falling Star spend it, the HUD reads it. The gauge is the soldier's own energy fields (max +0x304, level +0x308, depleted
// +0x2FC); its authority the machine the Rune Blade plays on. Only there does a spend take anything: on every other
// machine's copy (written by the ZWRB channel, runeblade.cpp) each spend is refused and each read the last heard.
// Game thread (the soldier's frame, a weapon's frame, the channel's update).
#pragma once
#include <cmath>
#include <cstddef>

namespace crew::runeblade {
bool IsRuneBlade(const void* soldier) noexcept;      // a live ZwRuneBlade object (soldier_factory.h ZwType == 5)
bool EtherOwned(const void* soldier) noexcept;       // a Rune Blade of this machine's (its spends take)
float EtherLevel(const void* soldier) noexcept;      // the level; -1 not a Rune Blade
float EtherMax(const void* soldier) noexcept;        // the max; -1 not a Rune Blade
bool EtherDepleted(const void* soldier) noexcept;    // spent out, not yet refilled to kRecoverAt
// Take `amount` if there is that much: true. Too little, depleted, not this machine's Rune Blade: nothing taken, false.
bool EtherSpend(void* soldier,float amount) noexcept;
// Take all that is left (a depleting attack): how much (0: nothing to take). Depletes.
float EtherDrain(void* soldier) noexcept;
// Take up to `amount` (a held drain, a frame's): how much was taken. Reaching 0 depletes.
float EtherBleed(void* soldier,float amount) noexcept;
}  // namespace crew::runeblade

// A plan-only copy of the gauge (RB-MELEE's step logic, tests/runeblade_melee_test.cpp): Snapshot() fills it from the
// live API above; only EtherSpend / EtherDrain change the soldier's.
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
}  // namespace crew::runeblade::ether

namespace crew::runeblade::ether {
inline Gauge Snapshot(const void* soldier) noexcept {
    Gauge g;
    g.now=EtherLevel(soldier);g.most=EtherMax(soldier);g.spent=EtherDepleted(soldier);
    if(g.now<0.0f || g.most<=0.0f){g.now=0.0f;g.most=0.0f;g.spent=true;}
    return g;
}
}  // namespace crew::runeblade::ether
