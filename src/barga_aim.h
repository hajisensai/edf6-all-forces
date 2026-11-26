// The Balam's impact marker (501_FortressRobo: V515 Retro Balam, V605 Barga Cannon; docs/feature-barga-aim.md): the
// pure part, checked offline by tools/barga_aim_check.cpp. barga_aim.cpp reads the vehicle each frame and fills a
// Readout; hud.cpp BargaAimMarks projects it.
//  - the combo: which attack may start next (0x60F570's rule without the inputs: the attack it follows is the current
//    one +0x23AC, or the current is none (-1) and it follows nothing; its flag a subset of +0x23BC & 1) and how far that
//    attack's fist reaches (src/barga_reach.inc, tools/gen_barga_reach.py: measured on the model's animations);
//  - the hit spheres of a lit group (0x6C8A50 / 0x6BB420): a point's offset (x, y, z, w) times its bone's world rows;
//  - V605's cannon (Weapon_VehicleSwingShoot 0x6B4100 / 0x6B4080 / 0x6B4190): each round of the burst leaves along
//    the direction frozen at the burst's start (weapon +0x1670) turned about its own right axis by
//    a(t) = begin + (end - begin) t^pow (+0x1680 / +0x1690 / +0x16A0), t = 1 - left / FireBurstCount.
#pragma once
#include <cmath>
#include <cstdint>

namespace bargaaim {
// --- the attack tables (generated) ---
// limb: 0 left arm, 1 right arm, 2 left leg, 3 right leg, -1 none (no hit of its own). reach: the hit sphere's centre at
// its farthest forward, in the model's frame (+Z forward, +X the left hand's side, the feet at y 0); radius its radius.
struct Attack {
    const char* name;
    int mask,flag,limb,prevCount;
    int prev[2];
    float reach[3];
    float radius;
    int frame,frames;
};
struct HitGroup { const wchar_t* name; int limb; };   // a group of the model's hit spheres (the SGO's names)
// file: the SGO the list is V515's or V605's; cannon: attack_cannon's index (-1 none), charge: ground_charge's.
struct Variant { const char* file; const Attack* attacks; int count; int cannon; int charge; };
#include "barga_reach.inc"
constexpr int kVariantCount=static_cast<int>(sizeof(kVariants)/sizeof(kVariants[0]));
constexpr int kLeft=0,kRight=1;

// The variant whose attacks follow the ones the game resolved (`prevCount[i]` and `prev[i][k]`, `count` entries:
// vehicle +0x2390 entry +0x30 / +0x20): -1 none (a list the tables do not know: the marker stays off).
inline int MatchVariant(int count,const int* prevCount,const int (*prev)[2]) noexcept {
    for(int v=0;v<kVariantCount;++v) {
        const Variant& t=kVariants[v];
        if(t.count!=count)continue;
        bool same=true;
        for(int i=0;i<count && same;++i) {
            const Attack& a=t.attacks[i];
            if(prevCount[i]!=a.prevCount){same=false;break;}
            for(int k=0;k<a.prevCount;++k)if(prev[i][k]!=a.prev[k])same=false;
        }
        if(same)return v;
    }
    return -1;
}

// Whether attack `i` may start now (0x60F570 less its input masks): `accept` the vehicle's +0x23A8, `current` +0x23AC,
// `flags` +0x23BC & 1.
inline bool Available(const Variant& v,int i,int current,bool accept,unsigned flags) noexcept {
    if(!accept || i<0 || i>=v.count)return false;
    const Attack& a=v.attacks[i];
    if((static_cast<unsigned>(a.flag) & flags)!=static_cast<unsigned>(a.flag))return false;
    if(a.prevCount==0)return current==-1;
    for(int k=0;k<a.prevCount;++k)if(a.prev[k]==current)return true;
    return false;
}

// A fist's reach for the marker: `next` the farthest of the attacks of that fist (limb kLeft / kRight) that may start
// now (Available), else -1; `base` the farthest of its opening attacks (following nothing, no flag: the jab), drawn
// dim when none may start.
struct FistPick { int next,base; };
inline float Horizontal(const Attack& a) noexcept { return std::sqrt(a.reach[0]*a.reach[0]+a.reach[2]*a.reach[2]); }
inline FistPick PickFist(const Variant& v,int side,int current,bool accept,unsigned flags) noexcept {
    FistPick p{-1,-1};
    for(int i=0;i<v.count;++i) {
        const Attack& a=v.attacks[i];
        if(a.limb!=side)continue;
        if(a.prevCount==0 && a.flag==0 && (p.base<0 || Horizontal(a)>Horizontal(v.attacks[p.base])))p.base=i;
        if(Available(v,i,current,accept,flags) && (p.next<0 || Horizontal(a)>Horizontal(v.attacks[p.next])))p.next=i;
    }
    return p;
}

// A point of the model's frame in the world: `rows` the vehicle's matrix (rows right, up, forward, position: +0x60).
inline void ModelToWorld(const float* rows,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*rows[k]+p[1]*rows[4+k]+p[2]*rows[8+k]+rows[12+k];
}

// The ground arc of a reach: `n` points (n >= 2) on the feet's plane (model y 0) at the reach's horizontal distance
// plus its radius, `halfSpan` rad either side of its bearing, in the model's frame.
inline int ReachArc(const Attack& a,float halfSpan,int n,float (*out)[3]) noexcept {
    if(n<2)return 0;
    const float r=Horizontal(a)+a.radius,bearing=std::atan2(a.reach[0],a.reach[2]);
    for(int i=0;i<n;++i) {
        const float b=bearing-halfSpan+2.0f*halfSpan*static_cast<float>(i)/static_cast<float>(n-1);
        out[i][0]=r*std::sin(b);out[i][1]=0.0f;out[i][2]=r*std::cos(b);
    }
    return n;
}

// A hit sphere's centre (0x6BB420): its offset (x, y, z, w) times its bone's world rows (16 floats from bone +0xB0).
inline void SphereCentre(const float* offset,const float* rows,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=offset[0]*rows[k]+offset[1]*rows[4+k]+offset[2]*rows[8+k]+offset[3]*rows[12+k];
}

// --- V605's cannon ---
// The sweep's sense: +1 a positive angle (begin.x 0.75 in V605_BARGA_C_CANNON) turns the barrel down from the frozen
// direction, as 0x64640 (sin(a/2) at x, cos(a/2) at w) and 0x64EF0 (that quaternion on +Z: (0, -sin a, cos a)) decode;
// -1 up. The one sign to flip if the Debug=1 line "BARGA cannon" shows the rounds leave the other way.
constexpr float kSweepSign=1.0f;
struct Sweep { float begin[3],end[3],pow; };
// The burst's progress for a round fired with `left` rounds of `count` still to fire (0x6B4080).
inline float BurstT(int left,int count) noexcept {
    if(count<=0)return 0.0f;
    const float t=1.0f-static_cast<float>(left)/static_cast<float>(count);
    return t<0.0f ? 0.0f : t>1.0f ? 1.0f : t;
}
// 0x6B4190's angle at `t`: its three components are each an X turn (0x64640 builds the first, 0x64730 multiplies the
// other two in about X as well), so they add.
inline float SweepAngle(const Sweep& s,float t) noexcept {
    const float f=t<=0.0f ? 0.0f : std::pow(t,s.pow);
    float a=0.0f;
    for(int k=0;k<3;++k)a+=s.begin[k]+(s.end[k]-s.begin[k])*f;
    return a;
}
// A round's direction: `forward` (the frozen +0x1670) as 0x4E450 orients +Z onto it (pitch then yaw; none: +Z), turned
// down by kSweepSign x `angle` about its own right axis (0x643D0: the orientation after the sweep).
inline void SweepDir(const float* forward,float angle,float* dir) noexcept {
    const float flat=std::sqrt(forward[0]*forward[0]+forward[2]*forward[2]);
    const float yaw=(forward[0]==0.0f && forward[2]==0.0f) ? 0.0f : std::atan2(forward[0],forward[2]);
    const float pitch=(forward[1]==0.0f && flat==0.0f) ? 0.0f : std::atan2(forward[1],flat);
    const float e=pitch-kSweepSign*angle;
    dir[0]=std::sin(yaw)*std::cos(e);dir[1]=std::sin(e);dir[2]=std::cos(yaw)*std::cos(e);
}
// The rounds still to come: `left` (+0xE18) while a burst runs, the whole burst (`count`, +0x370) before it.
inline int RoundsToCome(int left,int count,bool bursting) noexcept {
    if(count<=0)return 0;
    if(!bursting)return count;
    return left<0 ? 0 : left>count ? count : left;
}
// `most` of the `toCome` rounds, evenly spread, the next one and the last always in: each as the `left` it is fired
// with (toCome down to 1). Returns how many.
inline int SampleRounds(int toCome,int most,int* left) noexcept {
    if(toCome<=0 || most<=0)return 0;
    if(toCome<=most){for(int i=0;i<toCome;++i)left[i]=toCome-i;return toCome;}
    if(most==1){left[0]=toCome;return 1;}
    for(int i=0;i<most;++i)left[i]=toCome-static_cast<int>(std::lround(static_cast<double>(i)*(toCome-1)/(most-1)));
    return most;
}

// --- what barga_aim.cpp publishes and hud.cpp draws (world points) ---
constexpr int kArcPoints=13;
constexpr float kArcHalfSpan=0.35f;   // rad either side of a reach's bearing (a draw choice: about 20 deg)
constexpr int kMostSpheres=16,kMostRounds=20;
// Every how many sampled rounds a blast ring is drawn (the last one always): a ring each would bury the line.
constexpr int kRingEvery=4;
struct FistArc {
    bool on,next;              // next: an attack of this fist may start now (drawn bright), else its opening one dim
    int attack;                // its index in the variant's table
    float pts[kArcPoints][3];  // the ground arc
    float strike[3];           // the sphere's centre at its reach
};
struct Sphere {
    float c[3],prev[3],r;      // now, where its last test was (point +0x10), the radius
    int limb;                  // from its group's name (kHitGroups), -1 another
    bool ground; float groundAt[3];   // the ground under it (a map ray down), within r: touching
    bool touch;
    bool target; float targetAt[3];   // an enemy's lock point within r of its sweep (prev -> c)
};
struct Readout {
    int variant,current;
    bool accept;
    FistArc arc[2];
    int spheres;
    Sphere sphere[kMostSpheres];
    bool cannon,bursting;      // the cannon's marks: during ground_charge / attack_cannon, or while a burst runs
    int rounds,toCome;
    float at[kMostRounds][3];
    bool landed[kMostRounds];
    float blast;               // AmmoExplosion (m)
};
}  // namespace bargaaim
