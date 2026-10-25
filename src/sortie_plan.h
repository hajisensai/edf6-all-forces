// The sorties' geometry without the game (src/jet_sortie.cpp and src/heli.cpp fly them, tools/sortie_check.cpp checks
// it; docs/plan-2026-10-25-strikes.md §1.2): a sortie's aircraft make one pass at the mark and leave the map.
//   Line   the pass: level, along the way the aircraft came in, through the mark moved across by where it came in
//          beside its formation (a formation's aircraft keep their places abreast: three bombers lay three bands)
//   Lap    a circle round the mark counted as it is flown (the heli's death circle, the gunship's marking lap)
// and the few numbers each pass is made of.
#pragma once
#include <cmath>

namespace crew {
namespace sortie {
constexpr float kPi=3.14159265358979f;

// The passes (crew.h SortiePass's order after its `none`; tools/calls.py SORTIE_PASSES).
// Jets
constexpr float kMaxOffset=80.0f;         // m: a formation's aircraft at most this far across their line from the mark
constexpr float kPassBeyond=900.0f;       // m past the mark along its line a level pass ends (it withdraws)
constexpr float kMissilePassAlt=320.0f;   // m over the mark: the missile pass
constexpr int kPassMissiles=4;            // missiles each jet of a missile pass fires at most
constexpr float kCarpetAlt=260.0f;        // m over the mark: the carpet bombers' level
constexpr float kCarpetHalf=150.0f;       // m: their bombs land within this of the mark along their line
constexpr float kPopAlt=110.0f;           // m over the mark: the hit and run's run in
constexpr float kPopDrop=45.0f;           // m: its bombs go when they would land this near the mark
constexpr int kPopBombs=2;
constexpr float kPopClimb=1.6f;           // its climb away: up this much for each 1 along
constexpr unsigned long long kPopClimbMs=4500;
constexpr float kRainAlt=900.0f;          // m over the mark: the missile rain's dive starts there
constexpr float kRainFrom=2600.0f,kRainTo=900.0f;   // m from the mark: it fires inside the first, withdraws at the second
constexpr unsigned long long kLockMs=700,kMissileGapMs=700;   // a sortie's missiles: the nose held, then one a gap
constexpr unsigned long long kSearchMs=45000;   // a pass that found nothing to strike this long after it came gives up
// Helicopters
constexpr float kHeliStrafeAlt=35.0f,kHeliStrafeLead=200.0f,kHeliPassBeyond=450.0f;
constexpr float kHunterStation=300.0f,kHunterAlt=90.0f,kHunterNear=60.0f;   // m from the mark, over it, how near is there
constexpr int kHunterMissiles=8;
constexpr unsigned long long kHunterMs=40000,kHunterGapMs=1200;
constexpr float kCircleRadius=110.0f,kCircleAlt=40.0f,kCircleSpeed=16.0f,kCircleGain=0.5f,kCircleNear=40.0f;

inline float Dot2(const float* a,const float* b) noexcept { return a[0]*b[0]+a[2]*b[2]; }

struct Line { float at[3]; float dir[3]; float side[3]; };

// The pass of an aircraft at `from` flying `heading`, at the mark `mark`: level along the heading through the mark moved
// across to where the aircraft is beside it (at most kMaxOffset).
inline Line LineThrough(const float* mark,const float* from,const float* heading) noexcept {
    Line l{};
    float d[2]={heading[0],heading[2]};
    float n=std::sqrt(d[0]*d[0]+d[1]*d[1]);
    if(n<1e-4f){d[0]=mark[0]-from[0];d[1]=mark[2]-from[2];n=std::sqrt(d[0]*d[0]+d[1]*d[1]);}
    if(n<1e-4f){d[0]=0.0f;d[1]=1.0f;n=1.0f;}
    l.dir[0]=d[0]/n;l.dir[1]=0.0f;l.dir[2]=d[1]/n;
    l.side[0]=l.dir[2];l.side[1]=0.0f;l.side[2]=-l.dir[0];
    const float off[3]={from[0]-mark[0],0.0f,from[2]-mark[2]};
    float across=Dot2(off,l.side);
    if(across>kMaxOffset)across=kMaxOffset;
    if(across<-kMaxOffset)across=-kMaxOffset;
    for(int i=0;i<3;++i)l.at[i]=mark[i]+l.side[i]*across;
    return l;
}
// m along the line from its point (negative: short of it).
inline float Along(const Line& l,const float* p) noexcept {
    const float d[3]={p[0]-l.at[0],0.0f,p[2]-l.at[2]};
    return Dot2(d,l.dir);
}
// The point on the line `lead` m on from where `p` is along it (the line's height).
inline void Ahead(const Line& l,const float* p,float lead,float* out) noexcept {
    const float a=Along(l,p)+lead;
    for(int i=0;i<3;++i)out[i]=l.at[i]+l.dir[i]*a;
}

// s a bomb takes to fall `over` m with `vy` m/s up at release under `g`.
inline float FallTime(float over,float vy,float g) noexcept {
    const float d=vy*vy+2.0f*g*(over>0.0f ? over : 0.0f);
    return (vy+std::sqrt(d))/g;
}
// Where a bomb let go at `pos` with velocity `vel` lands on the height `ground` (no drag: the plugin's Bomb() reckons so).
inline void Impact(const float* pos,const float* vel,float ground,float g,float* out) noexcept {
    const float t=FallTime(pos[1]-ground,vel[1],g);
    out[0]=pos[0]+vel[0]*t;out[1]=ground;out[2]=pos[2]+vel[2]*t;
}
// The carpet's window: a bomb landing within kCarpetHalf of the mark along the line.
inline bool InCarpet(const Line& l,const float* impact) noexcept { return std::fabs(Along(l,impact))<=kCarpetHalf; }
// The gap between bombs that spreads `bombs` over the carpet at `speed` m/s (ms).
inline unsigned long long CarpetGapMs(int bombs,float speed) noexcept {
    if(bombs<2 || !(speed>1.0f))return 250;
    const float s=2.0f*kCarpetHalf/speed/static_cast<float>(bombs-1);
    return static_cast<unsigned long long>(s*1000.0f);
}

// A circle round `centre` counted as it is flown: the angle swept since the first point (either way round).
struct Lap { bool set=false; float last=0.0f,swept=0.0f; };
inline void LapStep(Lap& l,const float* centre,const float* pos) noexcept {
    const float a=std::atan2(pos[2]-centre[2],pos[0]-centre[0]);
    if(!l.set){l.set=true;l.last=a;return;}
    float d=a-l.last;
    while(d>kPi)d-=2.0f*kPi;
    while(d<-kPi)d+=2.0f*kPi;
    l.swept+=d;l.last=a;
}
inline bool LapDone(const Lap& l) noexcept { return std::fabs(l.swept)>=2.0f*kPi; }

// The circle's velocity at `pos` (level, m/s): `speed` round `centre` (anticlockwise seen from above with `turn` 1,
// clockwise -1) and back onto `radius` at kCircleGain per metre off it.
inline void CircleVelocity(const float* centre,const float* pos,float radius,float speed,int turn,float* out) noexcept {
    float r[2]={pos[0]-centre[0],pos[2]-centre[2]};
    float d=std::sqrt(r[0]*r[0]+r[1]*r[1]);
    if(d<1e-3f){r[0]=1.0f;r[1]=0.0f;d=1.0f;}
    r[0]/=d;r[1]/=d;
    const float t[2]={-r[1]*static_cast<float>(turn),r[0]*static_cast<float>(turn)};
    float in=(radius-d)*kCircleGain;
    if(in>speed)in=speed;
    if(in<-speed)in=-speed;
    out[0]=t[0]*speed+r[0]*in;out[1]=0.0f;out[2]=t[1]*speed+r[1]*in;
}
}  // namespace sortie
}  // namespace crew
