// Shots that cannot hurt their target (the user, 2026-10-12: 「ai还会无脑攻击金色飞船浪费子弹，应该只有能打到弱点的时候才开火」;
// docs/feedback-2026-10-12-ai-targeting.md). Pure logic, no game state: wastedfire.cpp feeds it every hit the damage
// function settles (damagestats.cpp DamageHook) and the AI asks it before it picks a target or pulls a trigger;
// tools/wasted_fire_check.cpp tests it offline.
//
// The game decides per hit whether it hurts: 0x547C30 asks the target's slot 16 about the hit and leaves its HP alone
// when that says no (0x54808A, exit 0x548625). A teleportation ship (UfoCarrier) says no to every hit while its hatch's
// damage window is shut (+0xD85 = 0, its animation's ダメージ禁止 event) and, while it is open, to every hit off its core
// part (+0xD8C): hull hits are settled, booked against it and change nothing. So whether a shooter's rounds do anything is
// what the game says about its own hits: a shooter whose last kWastedHits hits on a target all left its HP where it was
// (none hurt it in between) holds its fire at that target for kWastedHoldMs, then tries again (the hatch may have opened,
// the shooter moved under it). One hit that hurts clears it at once.
#pragma once
#include <cstdint>

namespace crew {
namespace wastedfire {
constexpr int kWastedHits=4;                 // hits in a row that hurt nothing: the shooter's fire at it is wasted
constexpr std::uint64_t kWastedHoldMs=6000;  // ms it holds its fire at that target before it tries again
constexpr std::uint64_t kForgetMs=15000;     // ms without a hit on it after which the count starts over
constexpr int kPairs=256;                    // shooter-target pairs remembered (the least recently hit one goes)

struct Pair {
    const void* shooter;
    const void* target;
    int futile;                  // hits in a row that hurt nothing
    std::uint64_t hitAt;         // the last hit
    std::uint64_t holdUntil;     // its fire at the target wasted until then (0: not)
};
struct Book { Pair pair[kPairs]; };

// The pair's entry, or (make) the least recently hit one taken over for it; nullptr when not there and not made.
inline Pair* Find(Book& b,const void* shooter,const void* target,bool make) noexcept {
    Pair* oldest=&b.pair[0];
    for(Pair& p:b.pair) {
        if(p.shooter==shooter && p.target==target)return &p;
        if(p.hitAt<oldest->hitAt)oldest=&p;
    }
    if(!make)return nullptr;
    *oldest=Pair{shooter,target,0,0,0};
    return oldest;
}

// A hit by `shooter` on `target` at `ms` settled: it hurt (its HP fell) or not. True when it starts a hold.
inline bool Hit(Book& b,const void* shooter,const void* target,bool hurt,std::uint64_t ms) noexcept {
    if(!shooter || !target || shooter==target)return false;
    Pair* p=Find(b,shooter,target,!hurt);
    if(!p)return false;
    if(hurt){p->futile=0;p->holdUntil=0;p->hitAt=ms;return false;}
    if(ms-p->hitAt>kForgetMs)p->futile=0;
    p->hitAt=ms;
    if(ms<p->holdUntil)return false;   // held already (a player's own fire goes on): no new hold until it runs out
    if(++p->futile<kWastedHits)return false;
    p->futile=0;p->holdUntil=ms+kWastedHoldMs;
    return true;
}

// Whether `shooter`'s fire at `target` is wasted at `ms` (its hits on it have hurt nothing lately).
inline bool Wasted(const Book& b,const void* shooter,const void* target,std::uint64_t ms) noexcept {
    if(!shooter || !target)return false;
    for(const Pair& p:b.pair)
        if(p.shooter==shooter && p.target==target)return ms<p.holdUntil;
    return false;
}

}  // namespace wastedfire
}  // namespace crew
