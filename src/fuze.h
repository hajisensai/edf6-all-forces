// The plugin missiles' warhead safety (missile.cpp): a warhead is armed only while its burst cannot reach its own
// launcher. The burst is the stock one: the round's ApplyAreaDamage (0x542670 from the hit 0x232689 or the expiry 0x236853)
// is an AABB query of radius GDI+0x58 = core+0x788 (copied from AmmoExplosion core+0xA20 by the init, 0x2320A9 -> 0x2320BD)
// round the burst point (src/edf41robo.cpp, docs/decoy-blast-re.md §1.1), and a player's vehicle weapon carries the
// friendly-fire flag (weapon+0x8B6 -> core+0xA26 -> GDI+0x60 0x20: no team filter, docs/swarm-team-re.md), so the
// launcher's own bodies are in it. The user, 2026-10-26: 「天启导弹打到自己是指会在自己身上爆炸，射不出去」: locked on
// something 12.5 m off, the proximity fuse (6.6 m off its lock point) went off 7 frames out of the pod, 6.8 m from the
// Apocalypse, and its 12 m blast took 175 off the tank, every shot (docs/feedback-2026-10-25-missiles.md 后续 3).
// A round without that flag (an NPC's weapon: RideAi clears it) has the stock team filter and is armed from the pod.
// Unarmed, the proximity fuse holds and a hit is a dud (docs/feedback-2026-11-21-missile-dud.md), as a real missile
// inside its arming distance strikes without bursting. A MissileBullet01 hit shows three things, each the round's own:
//  - its damage: core+0xAF4 0x10 off makes the hit 0x232616 take the single-target 0x543920 instead of the area damage;
//  - its sound: AmmoHitSe (the preset at core+0xA70; common_damages_explode_* on 106 of the 113 stock missile weapons)
//    played by the hit at 0x2325EE only with core+0xAF4 0x400 (set by the init, 0x231D3D `or r9d,0x600`; read nowhere
//    else, 0x2325CD);
//  - its fireball: the hit records itself (0x230CA0 -> the list at core+0x6C0, count core+0x6D8) and the class's next
//    update draws EffectGenUtil::Explosion there (0x26A8AC -> 0x2E5ED0) at size core+0xA20 / 5 (0x26A8C1), then ends the
//    round (0x2318E0). core+0xA20 is read by nothing else after the init (the burst radius is core+0x788).
// Unarmed all three are off: 0x10 and 0x400 cleared, the fireball's size 0; armed they are the stock ones again.
// Pure functions (no game): tests/fuze_test.cpp.
#pragma once
#include <cstdint>

namespace crew {
namespace fuze {
constexpr std::uint32_t kAreaOnHit=0x10;   // core+0xAF4: a hit takes the area damage (set by the stock init when AmmoExplosion > 0)
constexpr std::uint32_t kHitSound=0x400;   // core+0xAF4: a hit plays AmmoHitSe (set by the stock init on every round)

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

// The round's flags with its hit's area damage and blast sound as armed says: on armed, off unarmed. A round that had
// no blast (its stock init gave it none: `explosive` false) is left as it is.
constexpr std::uint32_t HitFlags(std::uint32_t flags,bool explosive,bool armed) noexcept {
    return !explosive ? flags : armed ? (flags|kAreaOnHit|kHitSound) : (flags&~(kAreaOnHit|kHitSound));
}

// The size its hit's fireball is drawn from (core+0xA20, drawn at a fifth of it): its blast armed, none unarmed. A round
// that had no blast is left as it is (its own value, 0).
constexpr float HitFireball(float blast,bool explosive,bool armed) noexcept {
    return explosive && !armed ? 0.0f : blast;
}
}  // namespace fuze
}  // namespace crew
