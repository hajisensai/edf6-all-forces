// What the plugin's weapons that cannot reach every enemy go for, and when a charge drone gives up (the user,
// 2026-10-10: "飞行怪好像会导致越飞越高。飞行怪在打无人机，但是无人机和一堆npc竟然没有攻击蜜蜂";
// docs/feedback-2026-10-10-air-chase.md). Pure logic, no game state: jet_combat.cpp (VisitTarget), jet.cpp (JetFrame,
// Rotor) call it; tools/air_chase_check.cpp tests it offline.
//
//  - A charge drone (the blast and doll drones, Weapon::charge) flies at its target and has to touch it. A flyer more than
//    its kind's ceiling over the ground under it is out of its reach: a doll drone climbs 12.5 m/s at the most, and the
//    flying enemies it chased kept above it, so the two climbed together to the game's 1200 m ceiling (the 2026-10-10 log:
//    four doll drones on one target, y 483 -> 985 m in 46 s, 28-110 m off it). Such a target is not taken, nor kept, nor
//    sent drones at by their carrier (Limits).
//  - A charge drone that does not close on its target goes off where it is when it is within its charge's reach (held:
//    pressed against the target's body, from below or the side, 7-16 m off it for a minute in the log) and gives the
//    target up when it is farther (it is outrun, or held off it too far for its charge): the target is shunned for a while,
//    by the drone and its carrier, which launches its drones at the targets they can catch instead (Closing, ShunList).
//  - A gunship's shells and side guns fire only at a target on the ground (jet_bay.cpp): it never takes a flyer, which it
//    circled the whole sortie without a shot when there was nothing else round it (Limits::groundOnly).
#pragma once
#include <cstdint>

namespace crew {
namespace airchase {
// m a charge drone's target may be over the ground under it (or, with no ground seen there, under the drone).
constexpr float kChargeCeiling=200.0f;
// Closing: m nearer than its nearest so far that counts as closing in; ms within its charge's reach without closing in
// before it goes off; ms farther out without closing in before it gives the target up; ms the target is then shunned.
constexpr float kCloserStep=1.0f;
constexpr std::uint64_t kHeldMs=1500,kGiveUpMs=10000,kShunMs=20000;

// What a weapon can strike: only targets on the ground (the gunship's shells), only targets under `ceiling` m over the
// ground (a charge drone, or a carrier of them; 0: no ceiling).
struct Limits { bool groundOnly; float ceiling; };
// Whether a target at `y` (its lock point; `flyer`: off the ground, jet_combat.cpp Flies) is one the weapon can strike.
// `groundSeen`/`groundY`: the ground under it (or a stand-in for it); none, the ceiling cannot be told and is not held.
inline bool Allowed(const Limits& l,bool flyer,float y,bool groundSeen,float groundY) noexcept {
    if(l.groundOnly && flyer)return false;
    return l.ceiling<=0.0f || !groundSeen || y-groundY<=l.ceiling;
}
// A charge drone's goal height `y` over ground `groundY` put under its ceiling.
inline float UnderCeiling(float y,float groundY,float ceiling) noexcept {
    return y-groundY>ceiling ? groundY+ceiling : y;
}

// A charge drone's run at its target: the nearest it has come, and when it last came nearer.
struct Closing { const void* target; float best; std::uint64_t closerAt; };
enum class Verdict : std::uint8_t { chase, detonate, giveUp };
// This frame of its run at `target`, `dist` m off it at `ms`: it goes off within `trigger`, or within `held` held back
// (`walled`: Sense found it held this frame) or not closing in for kHeldMs; it gives up farther out with no closing in for
// kGiveUpMs. A new target starts a new run.
inline Verdict Step(Closing& c,const void* target,float dist,float trigger,float held,bool walled,std::uint64_t ms) noexcept {
    if(c.target!=target)c=Closing{target,dist,ms};
    else if(dist<c.best-kCloserStep){c.best=dist;c.closerAt=ms;}
    if(dist<trigger)return Verdict::detonate;
    const std::uint64_t still=ms-c.closerAt;
    if(dist<held && (walled || still>=kHeldMs))return Verdict::detonate;
    return still>=kGiveUpMs ? Verdict::giveUp : Verdict::chase;
}

// The targets a drone (and its carrier) let be for now: a few, each until a game ms.
constexpr int kShuns=4;
struct ShunList { const void* object[kShuns]; std::uint64_t until[kShuns]; };
inline bool Shunned(const ShunList& s,const void* who,std::uint64_t ms) noexcept {
    for(int i=0;i<kShuns;++i)if(s.object[i]==who && ms<s.until[i])return true;
    return false;
}
// `who` shunned until `until`: its own entry, else the one that ends first (an expired one ends before any other).
inline void Shun(ShunList& s,const void* who,std::uint64_t until) noexcept {
    int at=0;
    for(int i=0;i<kShuns;++i) {
        if(s.object[i]==who){at=i;break;}
        if(s.until[i]<s.until[at])at=i;
    }
    s.object[at]=who;s.until[at]=until;
}
}  // namespace airchase
}  // namespace crew
