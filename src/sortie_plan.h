// The sorties' geometry without the game (src/jet_sortie.cpp and src/heli.cpp fly them, tools/sortie_check.cpp checks
// it; docs/plan-2026-10-25-strikes.md §1.2): a sortie's aircraft make one pass at the mark and leave the map.
//   Line   the pass: level, along the way the aircraft came in, through the mark moved across by where it came in
//          beside its formation (a formation's aircraft keep their places abreast: three bombers lay three bands)
//   Lap    a circle round the mark counted as it is flown (the heli's death circle, the gunship's marking lap)
// and the few numbers each pass is made of.
#pragma once
#include "salvo_volley.h"
#include "py_consts.inc"
#include <cmath>

namespace crew {
namespace sortie {
constexpr float kPi=3.14159265358979f;

// The passes (crew.h SortiePass's order after its `none`; tools/calls.py SORTIE_PASSES).
// Jets
constexpr float kMaxOffset=80.0f;         // m: a formation's aircraft at most this far across their line from the mark
constexpr float kPassBeyond=900.0f;       // m past the mark along its line a level pass ends (it withdraws)
constexpr float kMissilePassAlt=320.0f;   // m over the mark: the missile pass
constexpr float kCarpetAlt=260.0f;        // m over the mark: the carpet bombers' level
constexpr float kCarpetHalf=150.0f;       // m: their bombs land within this of the mark along their line
constexpr float kPopAlt=110.0f;           // m over the mark: the hit and run's run in
constexpr float kPopDrop=45.0f;           // m: its bombs go when they would land this near the mark
constexpr int kPopBombs=2;
constexpr float kPopClimb=1.6f;           // its climb away: up this much for each 1 along
constexpr unsigned long long kPopClimbMs=4500;
constexpr float kRainAlt=320.0f;
constexpr float kRainFrom=4000.0f,kRainTo=450.0f,kRainSpeed=155.0f;
// The 256 rounds go in volleys (salvo_volley.h: 8 every 3 frames, the last 1.55 s after the first). Keep flying the
// pass while the burst is draining, and as long before the next trigger.
constexpr unsigned long long kRainBurstMs=2000;
static_assert(salvo::VolleyFrames(salvo::kRounds)*1000/60<kRainBurstMs,"the pass outlasts the burst");
constexpr bool SalvoDraining(unsigned long long now,unsigned long long firedAt) noexcept {
    return firedAt!=0 && now>=firedAt && now-firedAt<=kRainBurstMs;
}
// One lock is enough (after the lock dwell, kLockMs): the burst fires the whole magazine whatever it was started with,
// over every enemy locked by then and since (src/lockon.cpp SalvoVolley).
constexpr bool SalvoLocked(int loaded,int locked) noexcept { return loaded>0 && locked>0; }
// The salvo's AI trigger: the lock cone is 1.40 rad off the nose (pylib/vcobjects.py SALVO_LOCK_CONE) and its rounds
// fan out and turn onto their locks themselves, so the jet fires with its aim this far off the nose.
constexpr float kRainFireCone=0.8f;
// What its rounds fly at, for the fire-or-hold check (wastedfire HoldFire): the children's launch speed, m/s
// (SALVO_CHILD_SPEED x 60), with no gravity (the carrier's AmmoGravityFactor acts only in the drop before ignition).
constexpr float kRainRoundSpeed=pyconst::kSalvoChildSpeed;
// The rain's hold before it opens fire (the user, 2026-11-26: 「不需要一进场就发射其实，可以稍微等一会」): its nose goes on the
// target from kRainFrom out, but it fires only once inside kRainOpen of the mark (over the target area) and after its
// nose has stayed within kRainSteadyCone of its aim for kRainHoldMs on end (a turn or a pull-up starts the count again);
// once open it stays open for the rest of the pass, so the lock's dwell and the burst are not cut off by a wobble.
constexpr float kRainOpen=2400.0f;           // m from the mark (level): over the target area
constexpr float kRainSteadyCone=0.25f;       // rad between the nose and its aim: the nose settled
constexpr unsigned long long kRainHoldMs=2500;
static_assert(kRainOpen<kRainFrom && kRainOpen>kRainTo+kRainSpeed*(kRainHoldMs+kRainBurstMs)/1000.0f,
              "the hold and the burst fit between kRainOpen and the end of the window");
static_assert(kRainSteadyCone<kRainFireCone,"settled is tighter than the fire cone");
// One step of the hold. `phase` 0 waiting, 1 settled since `since`, 2 open; true: it may fire now.
inline bool RainHoldStep(int& phase,unsigned long long& since,float dist,float noseOff,unsigned long long now) noexcept {
    if(phase==2)return true;
    if(!(dist<kRainOpen) || !(noseOff<=kRainSteadyCone)){phase=0;return false;}
    if(phase!=1){phase=1;since=now;}
    if(now-since<kRainHoldMs)return false;
    phase=2;
    return true;
}
constexpr unsigned long long kLockMs=700,kMissileGapMs=700;   // a sortie's missiles: the nose held, then one a gap
constexpr unsigned long long kSearchMs=45000;   // a pass that found nothing to strike this long after it came gives up
constexpr float kRunMin=100.0f,kRunMax=2400.0f;
// A call's way in (the user, 2026-11-26: 「叫支援的时候应该可以调整入场方向」): (x, z) the unit heading it flies in on, or
// none (0, 0: from the requester toward the target); `half` > 0 only with a gun run's or a carpet's line on the map (its
// half length, the target its centre). An aircraft comes in on that heading exactly (support_entry.h AirRoute).
struct RunSpec { float x=0.0f,z=0.0f,half=0.0f; };
inline bool HasHeading(const RunSpec& r) noexcept { return r.x!=0.0f || r.z!=0.0f; }
inline bool RunValid(const RunSpec& r) noexcept {
    if(r.half==0.0f && !HasHeading(r))return true;
    if(!std::isfinite(r.x+r.z+r.half) || std::fabs(r.x*r.x+r.z*r.z-1.0f)>=0.001f)return false;
    return r.half==0.0f || (r.half>=kRunMin*0.5f && r.half<=kRunMax*0.5f);
}
// The heading alone (no line): (x, z) made unit; none for a zero one.
inline RunSpec HeadingRun(float x,float z) noexcept {
    const float n=std::sqrt(x*x+z*z);
    if(!(n>1e-4f) || !std::isfinite(n))return RunSpec{};
    return RunSpec{x/n,z/n,0.0f};
}
inline bool RunFromPoints(const float* a,const float* b,RunSpec* out,float* centre) noexcept {
    const float x=b[0]-a[0],z=b[2]-a[2],length=std::hypot(x,z);
    if(!(length>=kRunMin && length<=kRunMax))return false;
    *out=RunSpec{x/length,z/length,length*0.5f};
    for(int i=0;i<3;++i)centre[i]=a[i]+(b[i]-a[i])*0.5f;
    return true;
}
// What one aircraft of a ground run (a gun run's or a carpet's line) lets go along it: its bombs, its rockets (rounds),
// its air-to-ground missiles. Its stores' rates on the line: a bomb every kBombGapMs (the hit and run's pair too; the
// store's own ripple is 8 frames, pylib/vcobjects.py Bomb.interval), a rocket ripple of kRocketRipple every kRocketGapMs
// (jet_combat.cpp Fire; vcobjects.py RKT burst), a missile after the lock's kLockMs and one every kMissileGapMs.
struct RunLoad { int bombs=0,rockets=0,missiles=0; };
constexpr unsigned long long kBombGapMs=150,kRocketGapMs=600;
constexpr int kRocketRipple=4;
// The shortest line it all goes down on (the user, 2026-11-27: the length "最短也得是能投完这架飞机所有对地挂载的距离"):
// the time its slowest store takes to empty at `speed` m/s (a carpet lets go only its bombs), within kRunMin..kRunMax.
inline float RunLeast(const RunLoad& l,float speed,bool carpet) noexcept {
    float seconds=0.0f;
    if(l.bombs>1)seconds=static_cast<float>(l.bombs-1)*static_cast<float>(kBombGapMs)*0.001f;
    if(!carpet) {
        const int ripples=(l.rockets+kRocketRipple-1)/kRocketRipple;
        if(ripples>1)seconds=std::fmax(seconds,static_cast<float>(ripples-1)*static_cast<float>(kRocketGapMs)*0.001f);
        if(l.missiles>0)
            seconds=std::fmax(seconds,(static_cast<float>(kLockMs)+static_cast<float>(l.missiles-1)*static_cast<float>(kMissileGapMs))*0.001f);
    }
    const float length=(speed>0.0f && std::isfinite(speed) ? speed : 0.0f)*seconds;
    return length<kRunMin ? kRunMin : length>kRunMax ? kRunMax : length;
}
// The map's ground run (the user, 2026-11-27: the way first, then the length; Broken Arrow's order drag): pressed at its
// start `a`, dragged toward `b`: along a -> b, as long as the drag and at least `least` (at most kRunMax). A press let go
// short of kRunDragLeast keeps the way it had (x, z: from the player toward the point, or turned) at `least`. `centre`
// its middle (the request's target), `end` where it ends. False for no way at all.
constexpr float kRunDragLeast=25.0f;   // m on the ground: a press let go nearer its start is a click
inline bool RunFromDrag(const float* a,const float* b,float least,float x,float z,RunSpec* out,float* centre,float* end) noexcept {
    const float dx=b[0]-a[0],dz=b[2]-a[2],dragged=std::hypot(dx,dz);
    if(dragged>=kRunDragLeast && std::isfinite(dragged)){x=dx/dragged;z=dz/dragged;}
    else {
        const float n=std::hypot(x,z);
        if(!(n>1e-4f) || !std::isfinite(n))return false;
        x/=n;z/=n;
    }
    least=least<kRunMin ? kRunMin : least>kRunMax ? kRunMax : least;
    const float length=dragged>=kRunDragLeast ? (dragged<least ? least : dragged>kRunMax ? kRunMax : dragged) : least;
    *out=RunSpec{x,z,length*0.5f};
    end[0]=a[0]+x*length;end[1]=b[1];end[2]=a[2]+z*length;
    for(int i=0;i<3;++i)centre[i]=(a[i]+end[i])*0.5f;
    return true;
}
// A line shorter than `least` (the host flies stock stores where the requester asked with its own pylons:
// support_dispatch.cpp DecideVariant) made `least` long from the same start on the same way: its new centre.
inline void RunLengthen(RunSpec& r,float* centre,float least) noexcept {
    const float half=(least>kRunMax ? kRunMax : least)*0.5f;
    if(!(r.half>0.0f) || r.half>=half)return;
    centre[0]+=r.x*(half-r.half);centre[2]+=r.z*(half-r.half);
    r.half=half;
}
// The gun run's strafe along a line (the user, 2026-11-26: 「扫射时间是不是不对」「只有机枪」). The nose (and the path,
// the guns and rockets along it) on the point `lead` m on along the line: that point walks at the jet's speed, so the
// whole line from -half to +half is under fire for 2 half / speed s, and the jet comes down as exp(-x / lead) (the slope
// to the point is its height over its lead). A fixed lead of 260 m from 160 m came down to the old 70 m floor in some
// 215 m (2.5 s) and fired no more. The lead is chosen so the height it comes in at lasts the whole line down to
// kStrafeFloor; the height it comes in at so the lead the guns reach (StrafeHeight) does.
constexpr float kStrafeFloor=45.0f;                      // m over the mark: the run ends with the jet about this low
constexpr float kStrafeLow=160.0f,kStrafeTop=600.0f;      // m over the mark: the run in's height bounds
constexpr float kStrafeLeadMost=1100.0f;                  // m: the rockets' reach (jet_combat.cpp kRocketOpen) inside
constexpr float kStrafeCross=65.0f;                       // m beside its line a jet strafes it from
constexpr float kStrafeAngle=0.35f;                       // rad off its line a jet strafes it at (20 deg)
constexpr float kStrafeSettleS=2.0f;                      // s its nose is on the point before the window opens
inline float StrafeLead(float half,float over,float least,float most) noexcept {
    const float ratio=over/kStrafeFloor;
    float lead=ratio>1.05f ? 2.0f*half/std::log(ratio) : most;
    if(lead>most)lead=most;
    if(lead<least)lead=least;
    return lead;
}
inline float StrafeHeight(float half,float lead) noexcept {
    const float h=kStrafeFloor*std::exp(2.0f*half/(lead>1.0f ? lead : 1.0f));
    return h<kStrafeLow ? kStrafeLow : h>kStrafeTop ? kStrafeTop : h;
}
// The aim on the line (along + lead): from -half the window is open, past +half it is over.
inline bool StrafeOpen(float along,float lead,float half) noexcept { return along+lead>=-half; }
inline bool StrafeOver(float along,float lead,float half) noexcept { return along+lead>half; }
// m over the mark after `flown` m with the nose on the point `lead` on (from `over`).
inline float StrafeDescent(float over,float lead,float flown) noexcept { return over*std::exp(-flown/lead); }
// s the strafe lasts at `speed` m/s.
inline float StrafeSeconds(float half,float speed) noexcept { return speed>1.0f ? 2.0f*half/speed : 0.0f; }

// m along its line a jet `cross` m beside it (signed along the line's side) heading `angle` rad off it (signed: toward
// +side) needs, turning at `radius`, to be on it and along it: turned straight first (its sideways drift with it), then
// an S of two arcs (or two quarter circles and the rest straight across) onto it; straight on and one arc where it
// already closes on it at that angle. Heading away (a quarter circle or more off): it cannot, short of a lap (1e9).
// The gun run's capture (jet_sortie.cpp): onto its line by turns it can make, small misses mended, never a lap.
inline float CaptureDistance(float cross,float angle,float radius) noexcept {
    const float a=std::fabs(angle);
    if(a>=kPi*0.5f)return 1e9f;
    const float r=radius>1.0f ? radius : 1.0f;
    const float arc=r*(1.0f-std::cos(a));   // the sideways an arc of `a` covers
    const float left=std::fabs(cross+(angle>0.0f ? arc : -arc));
    const float first=r*std::sin(a);
    float turned;   // turned straight, then the S across what is left
    if(left>=2.0f*r)turned=first+2.0f*r;
    else {
        const float c=1.0f-left/(2.0f*r);
        turned=first+2.0f*r*std::sin(std::acos(c<-1.0f ? -1.0f : c>1.0f ? 1.0f : c));
    }
    if(a>1e-3f && cross*angle<0.0f && std::fabs(cross)>=arc) {   // closing: straight on, then one arc onto it
        const float closing=(std::fabs(cross)-arc)/std::tan(a)+r*std::sin(a);
        return closing<turned ? closing : turned;   // a shallow closing angle: steeper by an S instead
    }
    return turned;
}

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
inline unsigned long long CarpetGapMs(int bombs,float speed,float half=kCarpetHalf) noexcept {
    if(bombs<2 || !(speed>1.0f))return 250;
    const float s=2.0f*half/speed/static_cast<float>(bombs-1);
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
