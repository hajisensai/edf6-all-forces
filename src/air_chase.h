// When a charge drone goes off at its target (the user, 2026-10-10: "飞行怪好像会导致越飞越高。飞行怪在打无人机，但是无人机和
// 一堆npc竟然没有攻击蜜蜂", and, correcting the first fix: the drones and their carrier are to attack the flyers, not let
// them be; docs/feedback-2026-10-10-air-chase.md). Pure logic, no game state: jet.cpp (JetFrame) calls it;
// tools/air_chase_check.cpp tests it offline.
//
// A charge drone (the blast and doll drones, Weapon::charge) flies at its target and has to touch it. Held off it by the
// target's body (from below or the side: 7-16 m off it for a minute in the 2026-10-10 log, never within its 6 m trigger)
// it goes off where it is once it is within its charge's reach and no longer closing in (Closing, Step).
#pragma once
#include <cstdint>

namespace crew {
namespace airchase {
// Closing: m nearer than its nearest so far that counts as closing in; ms within its charge's reach without closing in
// before it goes off.
constexpr float kCloserStep=1.0f;
constexpr std::uint64_t kHeldMs=1500;

// A charge drone's run at its target: the nearest it has come, and when it last came nearer.
struct Closing { const void* target; float best; std::uint64_t closerAt; };
enum class Verdict : std::uint8_t { chase, detonate };
// This frame of its run at `target`, `dist` m off it at `ms`: it goes off within `trigger`, or within `held` held back
// (`walled`: Sense found it held this frame) or not closing in for kHeldMs; farther out it keeps after it. A new target
// starts a new run.
inline Verdict Step(Closing& c,const void* target,float dist,float trigger,float held,bool walled,std::uint64_t ms) noexcept {
    if(c.target!=target)c=Closing{target,dist,ms};
    else if(dist<c.best-kCloserStep){c.best=dist;c.closerAt=ms;}
    if(dist<trigger)return Verdict::detonate;
    const std::uint64_t still=ms-c.closerAt;
    return dist<held && (walled || still>=kHeldMs) ? Verdict::detonate : Verdict::chase;
}

}  // namespace airchase
}  // namespace crew
