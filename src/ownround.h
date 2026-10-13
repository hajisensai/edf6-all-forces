// The bullets' candidate collector's verdict for the plugin's jets, the pure part (no game, no Windows:
// tools/own_round_check.cpp includes it alone; jet_hooks.cpp AddBodyHook asks it for every candidate body of a round while
// a plugin jet flies). The user, 2026-10-10: 「炮舰机的机炮有可能会打在自己身上，导致没打出去」.
//
// Two verdicts take a body out of a round's candidates (docs/bullet-pass-re.md §3: a body the collector 0x232AA0 leaves
// out is never shape-cast, the round flies through it):
//  - own: the round's owner (core+0x9A8) is a jet of the plugin's and the body is that jet's own: its object (body id ->
//    object, 0x108260) is the jet, or the body id is one of the jet's own bodies (its flight body veh+0x1650 and every
//    ragdoll part's body, veh+0x1398 stride 0xC0, wrapper +0x50; a wrapper's id at +0xF0, read by 0x11B15E0 and 0x108260
//    alike). The stock collector spares the owner by the same pointer test, but only while the bullet's "may hit its owner"
//    bit (core+0xAF4 & 0x80) is clear, and two of the bullets' move paths set it (0x236662 in 0x2364D0, 0x2361E4 in
//    0x235FA0) under conditions not resolved for the rounds an IFC fires (docs/bullet-pass-re.md §3.2 #1). This test does
//    not read that bit: a plugin jet's round never hits the jet itself, by its object or by any of its bodies.
//  - wingman: as before (docs/bullet-pass-re.md §4): the owner a jet of the same flight as the body's jet, or a bomb of a
//    bomber of that flight (the stock bombers have no body to hit, ours do, and their bombs leave the bay inside them).
// Everything else is the stock collector's (friendly fire stays as it is).
#pragma once
#include <cstdint>

namespace ownround {
constexpr int kMaxBodies=16;   // a jet's bodies looked at: the flight body and its ragdoll parts (V506: 8 parts)

// A flown jet as the collector's hook sees it (jet_hooks.cpp Publish, once a frame on the game thread).
struct Craft {
    const void* vehicle;     // the jet (its SceneObject: what 0x108260 returns for its bodies)
    const void* bombOwner;   // whose bombs its bay drops (the bomb rounds' owner)
    unsigned flight;
    bool bombs;              // its bay open or its bombs still falling
    bool gunship;            // Role::gunship: what the log calls it
    int bodies;
    std::uint32_t body[kMaxBodies];
};

enum class Verdict { stock, own, wingman };

struct Judged {
    Verdict verdict;
    const Craft* shooter;    // own: the jet whose round it is
    bool byBody;             // own: matched by body id only (the body's object is not the jet itself)
};

inline const Craft* Find(const Craft* c,int n,const void* v) noexcept {
    if(!v)return nullptr;
    for(int i=0;i<n;++i)if(c[i].vehicle==v)return &c[i];
    return nullptr;
}

inline bool HasBody(const Craft& c,std::uint32_t id) noexcept {
    for(int i=0;i<c.bodies && i<kMaxBodies;++i)if(c.body[i]==id)return true;
    return false;
}

// Whether a round of `owner` is a bomb of a bomber in `flight`.
inline bool BombOf(const Craft* c,int n,const void* owner,unsigned flight) noexcept {
    if(!owner)return false;
    for(int i=0;i<n;++i)if(c[i].flight==flight && c[i].bombs && c[i].bombOwner==owner)return true;
    return false;
}

// The verdict on candidate body `body` (its object `target`, nullptr when it has none) of a round of `owner`, among the
// `n` flown jets `c`.
inline Judged Judge(const Craft* c,int n,const void* owner,const void* target,std::uint32_t body) noexcept {
    const Craft* const s=Find(c,n,owner);
    if(s && (target==s->vehicle || HasBody(*s,body)))return Judged{Verdict::own,s,target!=s->vehicle};
    const Craft* const t=Find(c,n,target);
    if(!t)return Judged{Verdict::stock,nullptr,false};
    const bool pass=s ? s->flight==t->flight : BombOf(c,n,owner,t->flight);
    return Judged{pass ? Verdict::wingman : Verdict::stock,nullptr,false};
}
}  // namespace ownround
