// The rounds that glow: any ammo whose SGO declares a glow gets an additive halo drawn at its head each frame by our
// renderer (gpufx.cpp WalkGlows -> WorldEnd, Kind::glow), the round's own model and its stock trail untouched.
// The pure half, kept apart from the game so it can be checked off it (tests/gpufx_glow_test.cpp); docs/round-glow.md.
//
// The declaration: the round's Ammo_CustomParameter (the list every round's bullet core keeps a copy of, core+0xA58,
// tag word core+0xA68; docs/missile-re.md) gets one more entry at its end, a list
//     [kMark, r, g, b, intensity, size]
// r g b the halo's colour (linear, 0..1 each, normalised by the largest), intensity its HDR scale (the colour times it,
// like the stock glowing trails' AmmoColor above 1), size the halo's half width in metres. It must sit past every
// entry the round's class reads (Kinds below): there the stock never looks, so the stock round is the same with or
// without it. Rounds of any other class are not looked at (add the class here once its reads are known).
#pragma once
#include "gpufx_sim.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew::gpufx::glow {

constexpr double kMark=7146.0;   // the first number: this list is a glow declaration (pylib/vcobjects.py GLOW_MARK)
constexpr int kFields=6;         // mark, r, g, b, intensity, size
constexpr int kMostFields=16;    // more numbers than that: not ours
constexpr float kMostIntensity=64.0f,kMostSize=50.0f;

// The classes looked at, by the round's vtable (core-0x140) and the first entry index their constructor never reads:
//  - MissileBullet01 (ctor 0x267910): CP[0..11], CP[12] only when the list has more than 12 (`0xc < count`, its bool
//    at +0x13C0 otherwise 0): a declaration at 13 or later (the list padded with CP[12] = 0, the same as none).
//  - MissileBullet02 (ctor 0x26AF70): CP[0..15] unconditionally, nothing past 15: 16 or later.
struct Kind { unsigned vtable; int firstFree; };
constexpr Kind kKinds[]={{0x17A1C10u,13},{0x17A1E28u,16}};
constexpr int kKindCount=static_cast<int>(sizeof(kKinds)/sizeof(kKinds[0]));
// The first free index of the class at vtable RVA `vt`; -1: not a class looked at.
constexpr int FirstFree(unsigned vt) noexcept {
    for(const Kind& k:kKinds)if(k.vtable==vt)return k.firstFree;
    return -1;
}
// Whether a list of `count` entries can carry a declaration (its last) for a class whose first free index is `free`.
constexpr bool CanDeclare(int count,int free) noexcept { return free>=0 && count>free; }

struct Glow { float color[3]; float size; };   // colour HDR (r g b normalised, times the intensity), half width m

// The declaration's numbers `v` (its `n` entries): false (none) unless the mark, all finite, a colour not all zero
// and in range, intensity in (0, kMostIntensity], size in (0, kMostSize].
inline bool Parse(const double* v,int n,Glow* out) noexcept {
    if(n<kFields || n>kMostFields || v[0]!=kMark)return false;
    for(int i=1;i<kFields;++i)if(!std::isfinite(v[i]))return false;
    const double peak=std::fmax(v[1],std::fmax(v[2],v[3]));
    if(v[1]<0.0 || v[2]<0.0 || v[3]<0.0 || !(peak>0.0) || peak>1.0+1e-6)return false;
    if(!(v[4]>0.0) || v[4]>kMostIntensity || !(v[5]>0.0) || v[5]>kMostSize)return false;
    for(int i=0;i<3;++i)out->color[i]=static_cast<float>(v[1+i]/peak*v[4]);
    out->size=static_cast<float>(v[5]);
    return true;
}

// The declarations already read, by the round's CP variant (its 16 bytes: the document and the node): one read per
// weapon's ammo, not one per round a frame. An entry is read again after kRecheckMs (a document freed and another
// loaded at the same address with another node there: at most that long stale).
constexpr int kCacheSize=32;
constexpr std::uint64_t kRecheckMs=1000;
struct Cache {
    struct Entry { unsigned char key[16]; std::uint64_t at; bool used,glows; Glow glow; };
    Entry e[kCacheSize]{};
    int next=0;
    // The entry for `key` read at or after now - kRecheckMs, or null.
    const Entry* Find(const unsigned char* key,std::uint64_t now) const noexcept {
        for(const Entry& x:e)if(x.used && std::memcmp(x.key,key,16)==0 && now-x.at<kRecheckMs)return &x;
        return nullptr;
    }
    // Keeps `key`'s read (`glow` null: none), replacing its stale entry or else the oldest written.
    const Entry* Put(const unsigned char* key,std::uint64_t now,const Glow* glow) noexcept {
        Entry* slot=nullptr;
        for(Entry& x:e)if(x.used && std::memcmp(x.key,key,16)==0){slot=&x;break;}
        if(!slot){slot=&e[next];next=(next+1)%kCacheSize;}
        std::memcpy(slot->key,key,16);slot->at=now;slot->used=true;slot->glows=glow!=nullptr;
        slot->glow=glow ? *glow : Glow{};
        return slot;
    }
    void Clear() noexcept { *this=Cache{}; }
};

// The halos a frame at most (a salvo's 256 carriers then their 1024 children, with room): past it each round is kept
// with the stable chance most / count (by its id), the kept ones brighter by the inverse so the swarm's light stays.
// The fill budget (ThinToBudget, WorldEnd) then trims what the camera is inside of.
constexpr int kMostGlows=2048;
constexpr float kLeadSteps=1.0f;   // the position read a step before the round's own step (SharedFrameTick, M)
constexpr float kStep=1.0f/60.0f;  // s a step (the core's velocity is m/s)
inline float KeepShare(int count) noexcept { return count>kMostGlows ? static_cast<float>(kMostGlows)/static_cast<float>(count) : 1.0f; }
// A round's id from its core's address (stable while it flies: the node is reused only after it dies).
inline std::uint32_t IdOf(std::uintptr_t core) noexcept {
    return static_cast<std::uint32_t>(core>>4)^static_cast<std::uint32_t>(static_cast<std::uint64_t>(core)>>36);
}
inline bool Kept(std::uint32_t id,float share) noexcept { return share>=1.0f || SerialUnit(id^0x9E3779B9u)<share; }

// The halo of a round at `pos` flying at `vel` (m/s): one camera-facing card (Kind::glow), its centre led by
// kLeadSteps, `share` the KeepShare it was kept by. Soft against the scene over a quarter of its size.
inline Instance Make(const Glow& g,const float pos[3],const float vel[3],float share) noexcept {
    Instance in{};
    const float lead=kLeadSteps*kStep;
    for(int i=0;i<3;++i)in.pos[i]=pos[i]+(std::isfinite(vel[i]) ? vel[i]*lead : 0.0f);
    in.size=g.size;
    const float k=share>0.0f && share<1.0f ? 1.0f/share : 1.0f;
    for(int i=0;i<3;++i)in.color[i]=g.color[i]*k;
    in.color[3]=1.0f;
    in.rot=0.0f;in.frame=0.0f;in.soft=(std::fmax)(0.05f,g.size*0.25f);in.emissive=1.0f;
    return in;
}

}  // namespace crew::gpufx::glow
