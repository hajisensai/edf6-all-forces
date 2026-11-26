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

// The button side an attack is on: its input mask 1 the left fist's button (LB), 2 the right's (RB; the 2026-11-27 run:
// pad_rb started attack 1, jab_r), else -1 (the feet, the charge, the cannon). By the button, not by `limb`: the limb is
// the one the generator saw move forward most, and a whole-body move may lead with the other arm (V515's maxBeat, on
// the right button, has its left arm farthest out); the reach drawn stays the limb's.
constexpr int kLeftButton=1,kRightButton=2;
inline int FistSide(const Attack& a) noexcept {
    return a.mask==kLeftButton ? kLeft : a.mask==kRightButton ? kRight : -1;
}
// A fist's reach for the marker: `next` the farthest of the attacks on that side's button (FistSide kLeft / kRight)
// that may start now (Available), else -1; `base` the farthest of its opening attacks (following nothing, no flag: the
// jab), drawn dim when none may start.
struct FistPick { int next,base; };
inline float Horizontal(const Attack& a) noexcept { return std::sqrt(a.reach[0]*a.reach[0]+a.reach[2]*a.reach[2]); }
inline FistPick PickFist(const Variant& v,int side,int current,bool accept,unsigned flags) noexcept {
    FistPick p{-1,-1};
    for(int i=0;i<v.count;++i) {
        const Attack& a=v.attacks[i];
        if(FistSide(a)!=side || a.limb<0)continue;
        if(a.prevCount==0 && a.flag==0 && (p.base<0 || Horizontal(a)>Horizontal(v.attacks[p.base])))p.base=i;
        if(Available(v,i,current,accept,flags) && (p.next<0 || Horizontal(a)>Horizontal(v.attacks[p.next])))p.next=i;
    }
    return p;
}

// A point of the model's frame in the world: `rows` the vehicle's matrix (rows right, up, forward, position: +0x60).
inline void ModelToWorld(const float* rows,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*rows[k]+p[1]*rows[4+k]+p[2]*rows[8+k]+rows[12+k];
}

// Where to point at a mark the camera does not show (off the screen or behind it): `cx`, `cy` its clip x and y (the
// view-projection's, before the divide by w). They point the way to turn from the screen's middle whatever w's sign:
// in front (w > 0) they are the screen offset times w; behind (w <= 0) a point to the right still has cx > 0, where
// dividing by w would flip it. The chevron goes where that ray from the middle meets the screen's border less `inset`
// px; dx, dy its unit direction in pixels (y down). Straight behind (cx = cy = 0): down, the way to look round.
struct EdgeMark { float x,y,dx,dy; };
inline EdgeMark ToEdge(float cx,float cy,float width,float height,float inset) noexcept {
    float dx=cx*width*0.5f,dy=-cy*height*0.5f;
    const float len=std::sqrt(dx*dx+dy*dy);
    if(len>1e-6f){dx/=len;dy/=len;}
    else{dx=0.0f;dy=1.0f;}
    const float hw=std::fmax(width*0.5f-inset,0.0f),hh=std::fmax(height*0.5f-inset,0.0f);
    const float kx=std::fabs(dx)>1e-6f ? hw/std::fabs(dx) : 1e30f,ky=std::fabs(dy)>1e-6f ? hh/std::fabs(dy) : 1e30f;
    const float k=std::fmin(kx,ky);
    return {width*0.5f+dx*k,height*0.5f+dy*k,dx,dy};
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
// Where the burst's state machine (weapon +0x1650, its handler at +0x1668: 0x6B3FE0 stores it at +0x18) is:
//  - waiting (0x6B4100): the next burst freezes the source's direction (*(+0x120) +0x80) and starts at t 0;
//  - sweeping (0x6B4080): each round along the frozen +0x1670. Its t is 1 - left / count, but with no round left and
//    the trigger still held (0x691080: +0x13D, or +0x13A for a single-round weapon not set to +0x143) it takes left as
//    count, so the next burst sweeps again from t 0 along the OLD frozen direction (no re-freeze); with the trigger let
//    go it goes back to waiting.
enum class SweepState { other, waiting, sweeping };
// Whether the frozen direction (+0x1670) is the one the rounds to come leave along (else the source's).
inline bool FromFrozen(SweepState s) noexcept { return s==SweepState::sweeping; }
// The rounds still to come: waiting, the whole burst (`count`, +0x370); sweeping, `left` (+0xE18), or with none left
// and the trigger `held` the whole next burst (0x6B4080 above); else none.
inline int RoundsToCome(SweepState s,int left,int count,bool held) noexcept {
    if(count<=0)return 0;
    if(s==SweepState::waiting)return count;
    if(s!=SweepState::sweeping)return 0;
    if(left<=0)return held ? count : 0;
    return left>count ? count : left;
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
constexpr int kMostSpheres=16,kMostRounds=20;
// Every how many sampled rounds a blast ring is drawn (the last one always): a ring each would bury the line.
constexpr int kRingEvery=4;
// A fist's mark: a ring as wide as its hit sphere at the reach of the attack its button would start now. No sweep line:
// the tables hold each attack's farthest point only, not the path to it (a hook's sideways travel is not measured).
struct FistMark {
    bool on,next;              // next: an attack of this fist may start now (drawn bright), else its opening one dim
    int attack;                // its index in the variant's table
    float at[3];               // the sphere's centre at its reach (world)
    float r;                   // the sphere's radius (m)
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
    FistMark fist[2];          // kLeft (LB), kRight (RB)
    int spheres;
    Sphere sphere[kMostSpheres];
    bool cannon,bursting;      // the cannon's marks: during ground_charge / attack_cannon, or while a burst runs
    int rounds,toCome;
    float at[kMostRounds][3];
    bool landed[kMostRounds];
    float blast;               // AmmoExplosion (m)
};
}  // namespace bargaaim
