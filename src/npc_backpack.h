// When an NPC soldier uses its backpack weapon (docs/npc-backpack-re.md; the user, 2026-10-18: 「让npc可以使用背包位武器如突击
// 的手雷，飞翼的短剑等」). Pure logic, no game: src/npcai.cpp reads the soldier, its target and the friends round it and asks
// these; tools/npc_backpack_check.cpp tests them offline.
//
//  - thrown (npc_pack_table.h PackUse::thrown: grenades, thermite / binary rounds, the Wing Diver's plasma balls): at a group
//    (kGroupLeast enemies within kGroup m of the target, or one large one), on the ground or low (never a flyer), on the
//    low arc its own speed and gravity give (LobPitch: the look raised by the arc less the weapon's FireVector lift), no
//    farther than its true reach or kFlightMost seconds of flight, no nearer than its own round reaches it; no friend
//    where it lands (its blast, or its area for a burning / gas round, LandingClear) nor on its whole actual flight
//    (ArcClear, with terrain checked on every segment); at most once every kThrowGapMs.
//  - direct (the sabers, the spears): at the target within kDirectShare of its true reach, the round's line clear of
//    friends (npc_logic.h ShotClear); a swing every kDirectGapMs while it stays in reach.
// Each use waits for the look to be on it (AimOn) and holds that look kHoldFrames afterwards: the weapon lets go on its
// animation's frame, not on the press.
#pragma once
#include "npc_logic.h"
#include "npc_pack_table.h"
#include <cmath>
#include <cstdint>

namespace crew::npcpack {
constexpr float kGravity=14.7f;            // m/s^2 at gravity factor 1 (heli.cpp kGravity, measured)
constexpr float kFlightMost=2.5f;          // s: a longer throw is not made (the target has walked off by then)
constexpr float kThrowMost=150.0f;         // m: never farther, whatever a slow round's life says
constexpr float kDirectShare=0.9f;         // of a direct weapon's true reach
constexpr float kGroup=8.0f;               // m round the target the group counts within (at least the round's own radius)
constexpr int kGroupLeast=2;               // enemies in it, the target among them (or one large one)
constexpr float kAirAbove=15.0f;           // m over the soldier: a flyer (npcai.cpp kAirAbove)
constexpr float kThrowCone=0.05f;          // rad: the look on a throw
constexpr float kDirectHit=1.5f;           // m: the look on a direct target (npcai.cpp kHitRadius)
constexpr float kDirectConeLeast=0.05f;
constexpr std::uint64_t kThrowGapMs=4000,kDirectGapMs=700,kReloadGapMs=15000;   // kReloadGapMs: an empty one asked again
constexpr int kHoldFrames=45;              // 0.75 s
constexpr float kArcStep=3.0f,kArcTimeStep=0.1f; // the whole flight: no more than 3 m / 0.1 s between map rays

// The elevation (rad, up positive) a round of `speed` (m/s) falling at `gravity` (m/s^2) leaves at to land `rise` m above
// where it leaves, `horiz` m away: the low arc; `flight` its time (s). False when it cannot get there. No gravity: the
// straight line.
inline bool LobPitch(float horiz,float rise,float speed,float gravity,float* pitch,float* flight) noexcept {
    if(!(horiz>0.01f) || !(speed>0.0f) || !std::isfinite(rise))return false;
    float angle;
    if(!(gravity>0.0f))angle=std::atan2(rise,horiz);
    else {
        const float v2=speed*speed;
        const float disc=v2*v2-gravity*(gravity*horiz*horiz+2.0f*rise*v2);
        if(disc<0.0f)return false;
        angle=std::atan((v2-std::sqrt(disc))/(gravity*horiz));
    }
    const float c=std::cos(angle);
    if(!(c>1e-4f))return false;
    if(pitch)*pitch=angle;
    if(flight)*flight=horiz/(speed*c);
    return true;
}

// What npcai.cpp reads of one backpack weapon (the weapon object's own values: +0x224 reach, +0x8B0 blast, +0x894 speed,
// +0x8E0 gravity factor, +0xBE8 rounds; the table's use, lift and area).
struct Weapon { PackUse use; float reach,blast,area,speed,gravity,lift; bool ready; };
// The target as seen from the soldier's eye.
struct Target { float dist,horiz,rise; int group; bool large; };
inline float Radius(const Weapon& w) noexcept { return w.blast>w.area ? w.blast : w.area; }
// Its own round keeps off it: no nearer than its radius allows (npc_logic.h MinRange's margin).
inline float Nearest(const Weapon& w) noexcept { const float r=Radius(w);return r>0.0f ? r*1.5f+2.0f : 0.0f; }
// How far a throw is made: its reach, kFlightMost of flight and kThrowMost, whichever is least.
inline float ThrowReach(const Weapon& w) noexcept {
    float r=w.reach>0.0f ? w.reach : 0.0f;
    if(w.speed>0.0f && w.speed*kFlightMost<r)r=w.speed*kFlightMost;
    return r<kThrowMost ? r : kThrowMost;
}
// The group's radius round the target.
inline float GroupRadius(const Weapon& w) noexcept { const float r=Radius(w);return r>kGroup ? r : kGroup; }

// Whether `w` is for `t` now (its look aside: AimOn), and for a throw the elevation it leaves at (`pitch`).
inline bool Fits(const Weapon& w,const Target& t,float* pitch) noexcept {
    if(!w.ready || !(t.dist>0.0f))return false;
    if(w.use==PackUse::direct)return w.reach>0.0f && t.dist<=w.reach*kDirectShare;
    if(t.rise>kAirAbove || t.horiz<Nearest(w) || t.dist>ThrowReach(w))return false;
    if(t.group<kGroupLeast && !t.large)return false;
    float flight=0.0f;
    return LobPitch(t.horiz,t.rise,w.speed,w.gravity,pitch,&flight) && flight<=kFlightMost;
}
// The look (world direction) a throw takes: towards the target over the ground, raised by `pitch` less the weapon's lift
// (it leaves above the look by atan(lift): its FireVector).
inline void ThrowLook(const float* from,const float* to,float pitch,float lift,float* dir) noexcept {
    float h[3]={to[0]-from[0],0.0f,to[2]-from[2]};
    const float n=std::sqrt(h[0]*h[0]+h[2]*h[2]);
    if(!(n>1e-6f)){dir[0]=0.0f;dir[1]=1.0f;dir[2]=0.0f;return;}
    const float e=pitch-std::atan(lift>0.0f ? lift : 0.0f);
    dir[0]=h[0]/n*std::cos(e);dir[1]=std::sin(e);dir[2]=h[2]/n*std::cos(e);
}
// Whether the look (pitch, yaw: the soldier's view) is on `dir` enough to let go.
inline bool AimOn(PackUse use,float viewPitch,float viewYaw,const float* dir,float dist) noexcept {
    float cone=kThrowCone;
    if(use==PackUse::direct) {
        cone=std::atan(kDirectHit/(dist>1.0f ? dist : 1.0f));
        if(cone<kDirectConeLeast)cone=kDirectConeLeast;
    }
    return npc::AimError(viewPitch,viewYaw,dir)<=cone;
}
// No friend within `radius` (and its own) of where the round lands, kept 1.25 times as the stock blast test is.
inline bool LandingClear(const float* at,float radius,const npc::Friend* friends,int n) noexcept {
    if(!(radius>0.0f))return true;
    for(int i=0;i<n;++i)if(npc::Dist(at,friends[i].pos)<radius*1.25f+friends[i].radius)return false;
    return true;
}
// The actual round's full low arc, not the soldier's look (ThrowLook removes the weapon's FireVector lift from that
// look). Any friend or terrain collision along it rules the throw out: a contact grenade would explode there, not at
// the intended target; a timed / bouncing grenade's eventual landing cannot be predicted after that collision.
// `blocked(a,b)` tests the map segment. At most 0.1 s per chord also bounds the gravity sag between its ends.
template<class Blocked>
inline bool ArcClear(const Weapon& w,const float* from,const float* to,float pitch,float spread,
                     const npc::Friend* friends,int n,Blocked blocked) noexcept {
    const float horiz=npc::Horiz(from,to),c=std::cos(pitch);
    if(!(horiz>0.01f) || !(w.speed>0.0f) || !(c>1e-4f))return false;
    const float flight=horiz/(w.speed*c);
    if(!std::isfinite(flight) || !(flight>0.0f) || flight>kFlightMost)return false;
    const float byTime=flight/kArcTimeStep,byLength=(w.speed+w.gravity*flight)*flight/kArcStep;
    const float segments=std::ceil(byTime>byLength ? byTime : byLength);
    if(!std::isfinite(segments) || segments>256.0f)return false;
    const int steps=segments>1.0f ? static_cast<int>(segments) : 1;
    const float vx=(to[0]-from[0])/flight,vz=(to[2]-from[2])/flight,vy=w.speed*std::sin(pitch);
    float previous[3]={from[0],from[1],from[2]};
    for(int i=1;i<=steps;++i) {
        const float t=flight*static_cast<float>(i)/static_cast<float>(steps);
        const float point[3]={from[0]+vx*t,from[1]+vy*t-0.5f*w.gravity*t*t,from[2]+vz*t};
        if(!npc::ShotClear(previous,point,spread,0.0f,friends,n) || blocked(previous,point))return false;
        for(int k=0;k<3;++k)previous[k]=point[k];
    }
    return LandingClear(to,Radius(w),friends,n);
}
// Whether it has rested since its last use (`sinceMs`; a first use any time).
inline bool Rested(PackUse use,bool used,std::uint64_t sinceMs) noexcept {
    return !used || sinceMs>=(use==PackUse::thrown ? kThrowGapMs : kDirectGapMs);
}
}  // namespace crew::npcpack
