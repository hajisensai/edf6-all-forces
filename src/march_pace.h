// The paced march (the user, 2026-10-23: "支持全体一块战术移动，可选择全速移动或者以最慢单位为基准。因为我想一块指挥所有npc"):
// the units given one move / attack-move together on the map, with the pace key's mode on (MapMarchPace, P), keep
// together on the way: each unit's progress is the share of its own way behind it (1 - left / start, so units sent from
// farther away cover more ground in the same time and all arrive together), and a unit more than the march's slack ahead
// of the group's slowest is slowed, held where it is at twice that slack. The slack is one share of progress for the
// whole march (progress is the march's shared clock: the same share is the same time for every unit, so a unit sent from
// near is not let in that much earlier than one sent from far). The slowest goes at its own full speed: the group's pace
// is its, whatever kind of unit it is (no unit's speed in m/s is needed: a soldier, a tank, a heli each by its own
// throttle).
// Stuck: a unit that has not gained kImprove m on its way in kStuckMs of the time it was let go (blocked, fighting its way
// round) stops being the one the others wait for; it goes on at its own speed. Only the time it was let go counts, each
// moment by the share of its full speed it had (Member::scale, what Scales gave it last): a unit held for the slowest is
// not getting on because it was told not to, and counting that time made a near fast unit held behind a far slow one
// "stuck" in 8 s, let go at full speed, caught by the pace again 2 m on, over and over (review of PR #241). Pure (tools:
// march_pace_test).
#pragma once
#include <cmath>
#include <cstdint>

namespace pace {

constexpr float kSlackShare=0.03f;   // of the march's progress: how far ahead of the slowest a unit still goes at full speed...
constexpr float kSlackLeast=15.0f;   // ...and in metres of the march's longest way at the least (a short march's 3% is no room)
constexpr float kImprove=2.0f;       // m nearer its point that counts as getting on
constexpr float kStuckMs=8000.0f;    // ms let go at full speed (or its equal at a share of it) without getting on

struct Member {
    bool active=false;               // on its way (not there, its order still this march's)
    float start=0.0f;                // m left when the march began (at least 1)
    float left=0.0f;                 // m left now
    float best=0.0f;                 // the least `left` seen
    float idleMs=0.0f;               // the time let go since `best` last went kImprove down, by its share (Stuck)
    float scale=1.0f;                // the share of its full speed Scales gave it last (1 at the start)
    std::uint64_t at=0;              // when it was last updated
};

inline void Begin(Member& m,float left,std::uint64_t now) noexcept {
    m=Member{};
    m.active=true;m.start=std::fmax(1.0f,left);m.left=left;m.best=left;m.at=now;
}
// Where it is now: `left` m from its point. The time since the last update counts toward Stuck by the share it went at.
inline void Update(Member& m,float left,std::uint64_t now) noexcept {
    if(now>m.at)m.idleMs+=static_cast<float>(now-m.at)*m.scale;
    m.at=now;m.left=left;
    if(left<=m.best-kImprove){m.best=left;m.idleMs=0.0f;}
}
inline float Progress(const Member& m) noexcept {
    const float p=1.0f-m.left/m.start;
    return p<0.0f ? 0.0f : p>1.0f ? 1.0f : p;
}
inline bool Stuck(const Member& m) noexcept { return m.idleMs>kStuckMs; }

// Each member's share of its full speed (0 hold .. 1 full): `out[i]` for `m[i]`, also kept as `m[i].scale` (the share it
// goes at until the next Update, which counts that time toward Stuck by it).
inline void Scales(Member* m,int n,float* out) noexcept {
    float least=2.0f,longest=1.0f;int pacing=0;
    for(int i=0;i<n;++i) {
        out[i]=1.0f;
        if(!m[i].active || Stuck(m[i]))continue;
        ++pacing;
        const float p=Progress(m[i]);
        if(p<least)least=p;
        if(m[i].start>longest)longest=m[i].start;
    }
    if(pacing>=2) {   // else nobody to wait for
        const float slack=std::fmax(kSlackShare,kSlackLeast/longest);
        for(int i=0;i<n;++i) {
            if(!m[i].active || Stuck(m[i]))continue;
            const float s=2.0f-(Progress(m[i])-least)/slack;
            out[i]=s<0.0f ? 0.0f : s>1.0f ? 1.0f : s;
        }
    }
    for(int i=0;i<n;++i)m[i].scale=out[i];
}

// The shares the authority's march sets (mapcmd.cpp PaceTick) and the modules driving the units read (npcai.cpp,
// heli.cpp, ground.cpp, npcpost.cpp: MapPaceScale): by the unit's object (a vehicle, a squad's top) and its control
// (the identity's second half: an object at a reused address is another unit). Two a unit: itself and the transport
// carrying it there (transport.cpp: a squad's trip, driven by the vehicle's own driver). Game thread.
constexpr int kShares=2*96;   // mapcmd.h kCmdUnits
struct Share { const void* obj=nullptr; const void* ctrl=nullptr; float scale=1.0f; };
inline Share shares[kShares]{};

}  // namespace pace
