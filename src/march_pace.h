// The paced march (the user, 2026-10-23: "支持全体一块战术移动，可选择全速移动或者以最慢单位为基准。因为我想一块指挥所有npc"):
// the units given one move / attack-move together on the map, with the pace key's mode on (MapMarchPace, P), keep
// together on the way: each unit's progress is the share of its own way behind it (1 - left / start, so units sent from
// farther away cover more ground in the same time and all arrive together), and a unit more than its slack ahead of the
// group's slowest is slowed, held where it is at twice that slack. The slowest goes at its own full speed: the group's
// pace is its, whatever kind of unit it is (no unit's speed in m/s is needed: a soldier, a tank, a heli each by its own
// throttle). A unit that has not gained kImprove m on its way for kStuckMs (blocked, fighting its way round) stops being
// the one the others wait for; it goes on at its own speed. Pure (tools: march_pace_test).
#pragma once
#include <cmath>
#include <cstdint>

namespace pace {

constexpr float kSlackShare=0.03f;   // of its way: how far ahead of the slowest a unit still goes at full speed...
constexpr float kSlackLeast=15.0f;   // ...and in metres at the least (a short way's 3% is no room)
constexpr float kImprove=2.0f;       // m nearer its point that counts as getting on
constexpr std::uint64_t kStuckMs=8000;

struct Member {
    bool active=false;               // on its way (not there, its order still this march's)
    float start=0.0f;                // m left when the march began (at least 1)
    float left=0.0f;                 // m left now
    float best=0.0f;                 // the least `left` seen
    std::uint64_t gainedMs=0;        // when `best` last went kImprove down
};

inline void Begin(Member& m,float left,std::uint64_t now) noexcept {
    m.active=true;m.start=std::fmax(1.0f,left);m.left=left;m.best=left;m.gainedMs=now;
}
inline void Update(Member& m,float left,std::uint64_t now) noexcept {
    m.left=left;
    if(left<=m.best-kImprove){m.best=left;m.gainedMs=now;}
}
inline float Progress(const Member& m) noexcept {
    const float p=1.0f-m.left/m.start;
    return p<0.0f ? 0.0f : p>1.0f ? 1.0f : p;
}
inline bool Stuck(const Member& m,std::uint64_t now) noexcept { return now-m.gainedMs>kStuckMs; }

// Each member's share of its full speed (0 hold .. 1 full): `out[i]` for `m[i]`.
inline void Scales(const Member* m,int n,std::uint64_t now,float* out) noexcept {
    float least=2.0f;int pacing=0;
    for(int i=0;i<n;++i) {
        out[i]=1.0f;
        if(!m[i].active || Stuck(m[i],now))continue;
        ++pacing;
        const float p=Progress(m[i]);
        if(p<least)least=p;
    }
    if(pacing<2)return;   // nobody to wait for
    for(int i=0;i<n;++i) {
        if(!m[i].active || Stuck(m[i],now))continue;
        const float slack=std::fmax(kSlackShare,kSlackLeast/m[i].start);
        const float ahead=Progress(m[i])-least;
        const float s=2.0f-ahead/slack;
        out[i]=s<0.0f ? 0.0f : s>1.0f ? 1.0f : s;
    }
}

// The shares the authority's march sets (mapcmd.cpp PaceTick) and the modules driving the units read (npcai.cpp,
// heli.cpp, ground.cpp, npcpost.cpp: MapPaceScale): by the unit's object (a vehicle, a squad's top) and its control
// (the identity's second half: an object at a reused address is another unit). Game thread.
constexpr int kShares=96;   // mapcmd.h kCmdUnits
struct Share { const void* obj=nullptr; const void* ctrl=nullptr; float scale=1.0f; };
inline Share shares[kShares]{};

}  // namespace pace
