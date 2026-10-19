// The ultimate strike's show without the game (src/ultimate.cpp drives it, tools/ultimate_check.cpp checks it; no EDF.dll in
// here): its timeline in frames, the seeded random stream every machine replays it from, and what each frame of it fires
// (docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「特效拉满 昏天暗地」「不怕性能问题，误伤也没关系」).
//   charge  the sky darkens; aim beams fall from a ring high above onto the edge of the area and close in on its centre
//   storm   lightning bolts all over the area, a burst where each lands
//   cannon  the main beam straight down on the centre; rings of blasts run out from it to the edge
//   after   fires and smoke over the area; the sky comes back
#pragma once
#include <cmath>
#include <cstdint>

namespace crew {
namespace ultimate {
constexpr float kPi=3.14159265358979f;
constexpr int kFps=60;
// Frames from the call (the stock countdown already over: the hook is the satellite's fire state).
constexpr int kChargeEnd=5*kFps;          // 0 .. 300: charge
constexpr int kStormEnd=kChargeEnd+6*kFps;   // .. 660: storm
constexpr int kCannonEnd=kStormEnd+4*kFps;   // .. 900: cannon
constexpr int kAfterEnd=kCannonEnd+10*kFps;  // .. 1500: after; the show is over
constexpr int kSkyBackAt=kCannonEnd+1*kFps;  // the sky starts coming back a second into the after
constexpr float kSkyBackSec=7.0f;
constexpr float kSkyDarkSec=4.0f;         // the charge's fade into the dark sky

constexpr float kRadius=220.0f;           // m: the area the strike clears
// The aim beams: kAims from a ring kAimRing out and kAimHeight up, their aims from the area's edge to its centre.
constexpr int kAims=8;
constexpr float kAimRing=520.0f,kAimHeight=900.0f;
constexpr float kAimSizeFrom=0.8f,kAimSizeTo=7.0f;
// The bolts: from kBoltHeight over where they land (a little off plumb), kBoltsPerSec on average, more as the storm goes on.
constexpr float kBoltHeight=700.0f,kBoltLean=40.0f;
constexpr float kBoltsPerSec=90.0f;
constexpr int kBoltFrames=8;              // a bolt's rounds (the SGO's: pylib/vcobjects.py ULT_BOLT_ROUNDS)
// The main beam: from kBeamHeight straight down, growing to full size in kBeamGrow frames.
constexpr float kBeamHeight=1300.0f;
constexpr int kBeamGrow=40;
constexpr float kBeamSize=48.0f,kCoreSize=18.0f;
// The rings: kRings, one every kRingGap frames from the cannon's start, the first on the centre; each blast kRingBlast m.
constexpr int kRings=12;
constexpr int kRingGap=14;
constexpr float kRingBlast=55.0f;
static_assert(kChargeEnd<kStormEnd && kStormEnd<kCannonEnd && kCannonEnd<kAfterEnd,"phases in order");
static_assert(kStormEnd+kRings*kRingGap<=kCannonEnd,"every ring goes off in the cannon phase");

enum class Phase : unsigned char { charge, storm, cannon, after, over };
inline Phase PhaseAt(int frame) noexcept {
    return frame<kChargeEnd ? Phase::charge : frame<kStormEnd ? Phase::storm : frame<kCannonEnd ? Phase::cannon :
           frame<kAfterEnd ? Phase::after : Phase::over;
}

// The random stream (splitmix64): the same seed gives every machine the same show.
struct Rng {
    std::uint64_t s;
    std::uint64_t Next() noexcept {
        std::uint64_t z=(s+=0x9E3779B97F4A7C15ull);
        z=(z^(z>>30))*0xBF58476D1CE4E5B9ull;
        z=(z^(z>>27))*0x94D049BB133111EBull;
        return z^(z>>31);
    }
    float Unit() noexcept { return static_cast<float>(Next()>>40)*(1.0f/16777216.0f); }   // [0, 1)
};
// Frame `frame`'s own stream: a frame's events do not depend on how many frames before it were replayed (a late joiner).
inline Rng FrameRng(std::uint64_t seed,int frame) noexcept {
    Rng r{seed^(0xD1B54A32D192ED03ull*static_cast<std::uint64_t>(frame+1))};
    r.Next();
    return r;
}
// A point evenly over the disc of radius `r`: x, z offsets.
inline void InDisc(Rng& g,float r,float* out) noexcept {
    const float a=2.0f*kPi*g.Unit(),d=r*std::sqrt(g.Unit());
    out[0]=d*std::cos(a);out[1]=d*std::sin(a);
}

// Aim beam `i` at charge frame `frame`: where it starts (x, z offset from the centre; its height kAimHeight) and where it
// aims (x, z offset), and its size. The aims go in from the edge to the centre on an ease-in curve, turning as they go.
struct Aim { float from[2],at[2],size; };
inline Aim AimAt(int i,int frame) noexcept {
    const float t=frame<=0 ? 0.0f : frame>=kChargeEnd ? 1.0f : static_cast<float>(frame)/kChargeEnd;
    const float base=2.0f*kPi*static_cast<float>(i)/kAims;
    const float in=1.0f-(1.0f-t)*(1.0f-t);             // ease out: quick at first, settling on the centre
    const float turn=base+1.2f*t;
    Aim a{};
    a.from[0]=kAimRing*std::cos(base);a.from[1]=kAimRing*std::sin(base);
    const float r=kRadius*(1.0f-in);
    a.at[0]=r*std::cos(turn);a.at[1]=r*std::sin(turn);
    a.size=kAimSizeFrom+(kAimSizeTo-kAimSizeFrom)*t*t;
    return a;
}

// Bolts landing on storm frame `frame` (0 = the storm's first): how many (at most kMaxBoltsAFrame), each one's landing
// offset and its top's lean. The rate climbs from half to one and a half kBoltsPerSec over the storm.
constexpr int kMaxBoltsAFrame=4;
struct Bolt { float at[2],lean[2]; };
inline int BoltsAt(std::uint64_t seed,int frame,Bolt* out) noexcept {
    if(frame<0 || frame>=kStormEnd-kChargeEnd)return 0;
    Rng g=FrameRng(seed^0xB017ull,frame);
    const float t=static_cast<float>(frame)/(kStormEnd-kChargeEnd);
    const float mean=kBoltsPerSec*(0.5f+t)/kFps;       // bolts this frame on average
    int n=static_cast<int>(mean);
    if(g.Unit()<mean-static_cast<float>(n))++n;
    if(n>kMaxBoltsAFrame)n=kMaxBoltsAFrame;
    for(int k=0;k<n;++k) {
        InDisc(g,kRadius,out[k].at);
        InDisc(g,kBoltLean,out[k].lean);
    }
    return n;
}

// Ring `k` (0 .. kRings-1): its radius, how many blasts round it and the angle of the first. The blasts spaced about a
// blast's size apart, so each ring is a closed wall of fire; the first ring is the one blast on the centre.
struct Ring { float radius; int count; float phase; };
inline Ring RingOf(int k,std::uint64_t seed) noexcept {
    Ring r{};
    r.radius=kRadius*static_cast<float>(k)/(kRings-1);
    const int n=static_cast<int>(std::ceil(2.0f*kPi*r.radius/(kRingBlast*1.1f)));
    r.count=k==0 ? 1 : n<6 ? 6 : n;
    Rng g=FrameRng(seed^0x2196ull,k);
    r.phase=2.0f*kPi*g.Unit();
    return r;
}
// The ring that goes off on cannon frame `frame` (0 = the cannon's first), or -1.
inline int RingAt(int frame) noexcept {
    if(frame<0 || frame%kRingGap)return -1;
    const int k=frame/kRingGap;
    return k<kRings ? k : -1;
}

// The main beam's size on cannon frame `frame`: grows over kBeamGrow frames, thins away over the last kBeamGrow.
inline float BeamScale(int frame) noexcept {
    const int len=kCannonEnd-kStormEnd;
    if(frame<0 || frame>=len)return 0.0f;
    const float up=frame<kBeamGrow ? static_cast<float>(frame+1)/kBeamGrow : 1.0f;
    const int left=len-frame;
    const float down=left<kBeamGrow ? static_cast<float>(left)/kBeamGrow : 1.0f;
    return up<down ? up : down;
}

// The magic circles (the user: 「魔法阵是必须有」): instances of four models (pylib/magic_circle.py LAYERS; src/ultimate.cpp
// kCircleFiles in this order), each spawned on its frame, opened out from nothing, turned, and closed away in the after.
// The ground's three lie on the area (a little over its highest ground: `lift` m), the sky's three rise to `lift` m over it.
// Their sizes are the models' own (the pattern's radius in metres) times `size`.
enum class CircleModel : unsigned char { outer, star, core, sky };
struct CircleSpec { CircleModel model; int appear; float lift,size,spin; bool sky; };   // spin: rad/s at the charge's pace
constexpr CircleSpec kCircles[]={
    {CircleModel::outer,0,0.6f,1.0f,0.22f,false},
    {CircleModel::star,24,1.0f,1.0f,-0.38f,false},
    {CircleModel::core,48,1.4f,1.0f,0.9f,false},
    {CircleModel::sky,70,260.0f,1.0f,-0.28f,true},
    {CircleModel::sky,100,430.0f,0.72f,0.34f,true},
    {CircleModel::sky,130,600.0f,0.5f,-0.45f,true},
};
constexpr int kCircleCount=static_cast<int>(sizeof(kCircles)/sizeof(kCircles[0]));
constexpr int kCircleOpen=50;                     // frames to open out
constexpr int kCircleRise=90;                     // frames a sky circle takes to rise
constexpr int kCircleCloseAt=kCannonEnd+2*kFps;   // the first starts closing; each next kCircleStagger later
constexpr int kCircleStagger=10;
constexpr int kCircleClose=80;                    // frames to close away
static_assert(kCircleCloseAt+(kCircleCount-1)*kCircleStagger+kCircleClose<=kAfterEnd,"every circle is gone before the end");

// The turn's pace: 1 in the charge, quicker as the show builds.
inline float SpinPace(Phase p) noexcept {
    return p==Phase::charge ? 1.0f : p==Phase::storm ? 1.8f : p==Phase::cannon ? 3.2f : 2.2f;
}
// Seconds of turning (at pace 1) from frame 0 to `frame`: the pace integrated, so the angle never jumps between phases.
inline float SpinTime(int frame) noexcept {
    const int ends[4]={kChargeEnd,kStormEnd,kCannonEnd,kAfterEnd};
    float t=0.0f;
    int from=0;
    for(int k=0;k<4 && from<frame;++k) {
        const int to=frame<ends[k] ? frame : ends[k];
        t+=static_cast<float>(to-from)/kFps*SpinPace(static_cast<Phase>(k));
        from=to;
    }
    return t;
}
struct CirclePose { bool shown; float scale,angle,lift; };
inline float EaseOutBack(float t) noexcept {
    const float c=1.70158f,u=t-1.0f;
    return 1.0f+(c+1.0f)*u*u*u+c*u*u;
}
// Circle `i` on frame `frame`: whether it is there (not yet spawned, or closed and gone: not), its scale (of its model),
// its angle about the vertical and its lift.
inline CirclePose CircleAt(int i,int frame) noexcept {
    const CircleSpec& c=kCircles[i];
    CirclePose p{};
    const int closeAt=kCircleCloseAt+i*kCircleStagger;
    if(frame<c.appear || frame>=closeAt+kCircleClose)return p;
    p.shown=true;
    const float t=static_cast<float>(frame-c.appear);
    const float open=t>=kCircleOpen ? 1.0f : EaseOutBack(t/kCircleOpen);
    float close=1.0f;
    if(frame>=closeAt) {
        const float u=static_cast<float>(frame-closeAt)/kCircleClose;
        close=1.0f-u*u*(3.0f-2.0f*u);
    }
    const float s=c.size*open*close;
    p.scale=s>0.001f ? s : 0.001f;
    p.angle=c.spin*(SpinTime(frame)-SpinTime(c.appear));
    if(frame>=closeAt)p.angle+=c.spin*4.0f*static_cast<float>(frame-closeAt)/kFps;   // it whirls as it closes
    const float rise=c.sky ? (t>=kCircleRise ? 1.0f : 1.0f-(1.0f-t/kCircleRise)*(1.0f-t/kCircleRise)) : 1.0f;
    p.lift=c.lift*rise;
    return p;
}

// The after's fires: on after frame `frame`, whether one starts and where (an offset in the area).
inline bool FireAt(std::uint64_t seed,int frame,float* at) noexcept {
    if(frame<0 || frame>=4*kFps || frame%4)return false;
    Rng g=FrameRng(seed^0xF14Eull,frame);
    InDisc(g,kRadius*0.9f,at);
    return true;
}
}  // namespace ultimate
}  // namespace crew
