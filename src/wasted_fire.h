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
#include "edf/hash_index.h"
#include <cstdint>
#include <vector>

namespace crew {
namespace wastedfire {
constexpr int kWastedHits=4;                 // hits in a row that hurt nothing: the shooter's fire at it is wasted
constexpr std::uint64_t kWastedHoldMs=6000;  // ms it holds its fire at that target before it tries again
constexpr std::uint64_t kForgetMs=15000;     // ms without a hit on it after which the count starts over

struct Pair {
    const void* shooter;
    const void* target;
    int futile;                  // hits in a row that hurt nothing
    std::uint64_t hitAt;         // the last hit
    std::uint64_t holdUntil;     // its fire at the target wasted until then (0: not)
};
// Every pair hit lately, however many shooters a battle puts on a ship (a fixed 256, least recently hit going, dropped the
// holds of a crowd's shooters): rows indexed by (shooter, target), a pair not hit for kForgetMs given up (it holds nothing
// then: its count starts over and its hold, kWastedHoldMs < kForgetMs after its last hit, has run out -- as if never made).
struct Book {
    std::vector<Pair> pair;
    edf::PairIndex index;
    std::vector<int> free;       // rows given up, taken again first
    std::uint64_t sweptAt=0;
    // the latest holdUntil any pair has been given (never lowered): past it no pair is held, so Wasted
    // answers without the lookup (nearly every frame no hold runs at all)
    std::uint64_t latestHold=0;
};

// The pair's entry, nullptr when not there.
inline const Pair* Find(const Book& b,const void* shooter,const void* target) noexcept {
    const int i=b.index.Find(edf::PtrPair{shooter,target});
    return i<0 ? nullptr : &b.pair[static_cast<std::size_t>(i)];
}
// Its entry, made when not there: a row given up, else a new one. Every kForgetMs the pairs forgotten are given up (one
// pass over the rows: linear in the pairs, not per hit).
inline Pair* Make(Book& b,const void* shooter,const void* target,std::uint64_t ms) {
    const edf::PtrPair key{shooter,target};
    if(const int i=b.index.Find(key);i>=0)return &b.pair[static_cast<std::size_t>(i)];
    if(ms-b.sweptAt>kForgetMs) {
        b.sweptAt=ms;
        for(std::size_t i=0;i<b.pair.size();++i) {
            Pair& p=b.pair[i];
            if(p.shooter && ms-p.hitAt>kForgetMs){b.index.Erase(edf::PtrPair{p.shooter,p.target});p=Pair{};b.free.push_back(static_cast<int>(i));}
        }
    }
    int row;
    if(!b.free.empty()){row=b.free.back();b.free.pop_back();}
    else {row=static_cast<int>(b.pair.size());b.pair.emplace_back();}
    b.pair[static_cast<std::size_t>(row)]=Pair{shooter,target,0,0,0};
    b.index.Set(key,row);
    return &b.pair[static_cast<std::size_t>(row)];
}

// A hit by `shooter` on `target` at `ms` settled: it hurt (its HP fell) or not. True when it starts a hold.
inline bool Hit(Book& b,const void* shooter,const void* target,bool hurt,std::uint64_t ms) noexcept {
    if(!shooter || !target || shooter==target)return false;
    Pair* p=nullptr;
    if(!hurt)p=Make(b,shooter,target,ms);   // a hurting hit makes no record, it only clears one
    else if(const int i=b.index.Find(edf::PtrPair{shooter,target});i>=0)p=&b.pair[static_cast<std::size_t>(i)];
    if(!p)return false;
    if(hurt){p->futile=0;p->holdUntil=0;p->hitAt=ms;return false;}
    if(ms-p->hitAt>kForgetMs)p->futile=0;
    p->hitAt=ms;
    if(ms<p->holdUntil)return false;   // held already (a player's own fire goes on): no new hold until it runs out
    if(++p->futile<kWastedHits)return false;
    p->futile=0;p->holdUntil=ms+kWastedHoldMs;
    if(p->holdUntil>b.latestHold)b.latestHold=p->holdUntil;
    return true;
}

// Whether `shooter`'s fire at `target` is wasted at `ms` (its hits on it have hurt nothing lately).
inline bool Wasted(const Book& b,const void* shooter,const void* target,std::uint64_t ms) noexcept {
    if(!shooter || !target || ms>=b.latestHold)return false;   // every pair's hold run out (or none given)
    const Pair* p=Find(b,shooter,target);
    return p && ms<p->holdUntil;
}

}  // namespace wastedfire

// How often the plan for a special target is logged (wastedfire.cpp, WASTEDFIRE plan). One line a shooter and target kind
// every kEveryMs, saying how many plans it stands for, and at most kPerSecond lines a second in all: a moving fortress has
// a hundred parts, each its own target, and a line a target (the watch table thrashing, each new entry logging at once)
// wrote ~10 MB a second on 2026-10-31 and rotated every other line out of the log within seconds.
namespace planlog {
constexpr std::uint64_t kEveryMs=3000;   // a shooter's plans on one kind of target: one line this often
constexpr int kPerSecond=4;              // all lines together, in any game second (more shooters than kKeys still bounded)
constexpr int kKeys=64;                  // shooter-kind pairs remembered (the least recently seen one goes)

struct Key { const void* shooter; int kind; std::uint64_t loggedAt,seenAt; int quiet; bool logged; };
struct Gate { Key key[kKeys]; std::uint64_t secondAt; int inSecond; };

// A plan by `shooter` on a target of `kind` at `ms`: whether it gets a line; then *quiet is how many of its plans since
// the last one did not.
inline bool Pass(Gate& g,const void* shooter,int kind,std::uint64_t ms,int* quiet) noexcept {
    Key* k=nullptr;
    Key* oldest=&g.key[0];
    for(Key& x:g.key) {
        if(x.shooter==shooter && x.kind==kind){k=&x;break;}
        if(x.seenAt<oldest->seenAt)oldest=&x;
    }
    if(!k){k=oldest;*k=Key{shooter,kind,0,0,0,false};}
    k->seenAt=ms;
    if(k->logged && ms-k->loggedAt<kEveryMs){++k->quiet;return false;}
    if(ms-g.secondAt>=1000){g.secondAt=ms;g.inSecond=0;}
    if(g.inSecond>=kPerSecond){++k->quiet;return false;}
    ++g.inSecond;
    *quiet=k->quiet;
    k->quiet=0;k->loggedAt=ms;k->logged=true;
    return true;
}
}  // namespace planlog
}  // namespace crew
