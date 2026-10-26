// The plugin missiles' warhead safety (missile.cpp): a warhead is armed only while its burst cannot reach its own
// launcher. The burst is the stock one: the round's ApplyAreaDamage (0x542670 from the hit 0x232689 or the expiry 0x236853)
// is an AABB query of radius AmmoExplosion (core+0xA20) round the burst point (src/edf41robo.cpp, docs/decoy-blast-re.md
// §1.1), and a player's vehicle weapon carries the friendly-fire flag (weapon+0x8B6 -> core+0xA26 -> GDI+0x60 0x20: no team
// filter, docs/swarm-team-re.md), so the launcher's own bodies are in it. The user, 2026-10-26: 「天启导弹打到自己是指会在
// 自己身上爆炸，射不出去」: locked on something 12.5 m off, the proximity fuse (6.6 m off its lock point) went off 7 frames out of
// the pod, 6.8 m from the Apocalypse, and its 12 m blast took 175 off the tank, every shot (docs/feedback-2026-10-25-missiles.md
// 后续 3).
// A round without that flag (an NPC's weapon: RideAi clears it) has the stock team filter and is armed from the pod.
// Unarmed, the proximity fuse holds and a hit is the round's single-target damage (core+0xAF4 0x10 off: the hit 0x232616
// takes 0x543920 instead of the area damage), as a real missile inside its arming distance strikes without bursting.
// Pure functions (no game): tests/fuze_test.cpp.
#pragma once
#include <cstdint>

namespace crew {
namespace fuze {
constexpr std::uint32_t kAreaOnHit=0x10;   // core+0xAF4: a hit takes the area damage (set by the stock init when AmmoExplosion > 0)

// Whether the stock blast query of radius `blast` round `at` (a cube: the AABB query) meets the world box `box` (min xyz,
// then max xyz).
inline bool Reaches(const float* at,float blast,const float* box) noexcept {
    for(int k=0;k<3;++k)if(at[k]+blast<box[k] || at[k]-blast>box[3+k])return false;
    return true;
}

// Armed: no box of the launcher's `n` bodies (`boxes`, 6 floats each) is in reach of a burst here. Nothing to burst
// (blast 0) or no launcher left: armed.
inline bool Armed(const float* at,float blast,const float (*boxes)[6],int n) noexcept {
    if(!(blast>0.0f))return true;
    for(int i=0;i<n;++i)if(Reaches(at,blast,boxes[i]))return false;
    return true;
}

// The round's flags with its hit's area damage as armed says: on armed, off unarmed. A round that had none (its stock
// init gave it no blast: `explosive` false) is left as it is.
constexpr std::uint32_t HitFlags(std::uint32_t flags,bool explosive,bool armed) noexcept {
    return !explosive ? flags : armed ? (flags|kAreaOnHit) : (flags&~kAreaOnHit);
}
}  // namespace fuze
}  // namespace crew
