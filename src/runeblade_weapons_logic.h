// The Rune Blade's weapons (docs/newclass-re/runeblade-weapons-plan.md; tools/zw_weapons.py RUNE_MARKS), the pure part
// (tests/runeblade_weapons_test.cpp; tests/runeblade_weapons_test.py holds the marks equal to the Python side):
//  - Which rounds are whose. A Rune Blade weapon whose rounds the plugin has to find writes AmmoHitSizeAdjust as the float
//    of bits kMarkBase | its Mark (about 1.0005: the hit radius as good as unchanged). Every stock AmmoHitSizeAdjust near 1.0
//    is exactly 1.0 (tools/calls.py THROW_MARK_BASE's scan); the thrown drones' / gravity gun's / boarding gun's marks are
//    under 0x3F801000, the barrages' 0x3F801000..0x3F801FFF (barrage.cpp), the Andryusha's 0x3F8F4A36/7 (andryusha.h).
//    The round's core keeps it at +0xA34 from its birth (barrage.cpp kMark, read at 0x231EEF), as the barrages read theirs.
//  - Matins Bell's pierce limit (plan §2.3 R1: 「穿透 2 敌」). Its SGO is AmmoIsPenetration 1 (through everything); the
//    plugin counts the distinct enemies a marked carbine round has hit and ends the round (its age to its life, as barrage.cpp
//    puts out a cluster carrier) once it has hit kCarbinePierce. The counting is here; the hook on the round's hit is not
//    in yet (the plan's §1 R1 lists the site to find). Online each machine runs its own copy of a fired round from the
//    same SGO, and the damage counts where the shooter is (as every stock round), so the same limit holds everywhere.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::runeblade {

constexpr std::uint32_t kMarkBase=0x3F802000u,kMarkMask=0xFFFFFF00u;

// tools/zw_weapons.py RUNE_MARKS (same numbers; the Python test reads this enum).
enum class Mark : std::uint32_t {
    none=0,
    carbine=1,   // R1 Matins Bell: the 2-enemy pierce limit
    saber=2,     // M1 Daybreak: the third cut's lingering trace
    lance=3,     // M2 Firmament: the burst rune circle on a hit
    ring=4,      // M3 Stars: the runes left on what it cuts, set off as the ring returns
    hammer=5,    // M4 Falling Star: damage by the height of the dive
    iai=6,       // M5 Afterglow: the dial runes bursting 1 s later
    beacon=7,    // R4 the barrage beacon: where it lands
};
constexpr std::uint32_t kMarks=8;

constexpr std::uint32_t MarkBits(Mark m) noexcept { return kMarkBase|static_cast<std::uint32_t>(m); }

inline std::uint32_t FloatBits(float f) noexcept { std::uint32_t b; std::memcpy(&b,&f,sizeof b); return b; }

// The mark an AmmoHitSizeAdjust stands for (Mark::none: not one of ours).
inline Mark MarkOf(float hitSizeAdjust) noexcept {
    const std::uint32_t b=FloatBits(hitSizeAdjust);
    if((b&kMarkMask)!=kMarkBase)return Mark::none;
    const std::uint32_t n=b&~kMarkMask;
    return n>0 && n<kMarks ? static_cast<Mark>(n) : Mark::none;
}

// ---- Matins Bell's pierce limit ----
constexpr int kCarbinePierce=2;
// The rounds in flight at once: five a magazine, one every 45 frames, 20 frames' life (zw_weapons.py CARBINE), so a few
// per shooter; four players online.
constexpr int kPierceRounds=32;
constexpr int kPierceTargets=kCarbinePierce;

struct PierceRound {
    const void* round=nullptr;   // the round (its object); nullptr: a free entry
    const void* hit[kPierceTargets]={};
    int n=0;
};

struct PierceTable {
    PierceRound rounds[kPierceRounds];

    PierceRound* Find(const void* round) noexcept {
        for(auto& r:rounds)if(r.round==round)return &r;
        return nullptr;
    }
    // A hit of `target` by `round`: true while the round may go on (a first or second enemy, or one it already hit), false
    // once this hit is past the limit (the caller ends the round and lets this hit go). A round no entry is left for goes
    // through as the SGO says (never cut short by a full table: said by the caller's log).
    bool Hit(const void* round,const void* target,bool* full=nullptr) noexcept {
        if(full)*full=false;
        PierceRound* r=Find(round);
        if(!r) {
            r=Find(nullptr);
            if(!r) { if(full)*full=true; return true; }
            *r=PierceRound{};
            r->round=round;
        }
        for(int i=0;i<r->n;++i)if(r->hit[i]==target)return true;
        if(r->n>=kCarbinePierce)return false;
        r->hit[r->n++]=target;
        return true;
    }
    // The round is gone (expired, ended, or the mission over).
    void Drop(const void* round) noexcept { if(PierceRound* r=Find(round))*r=PierceRound{}; }
    void Clear() noexcept { for(auto& r:rounds)r=PierceRound{}; }
};

}  // namespace crew::runeblade
