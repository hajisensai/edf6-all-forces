// The ultimate strike's show without the game (src/ultimate.cpp drives it, tools/ultimate_check.cpp checks it; no EDF.dll in
// here): its timeline in frames, the seeded random stream every machine replays it from, and what each frame of it fires
// (docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「特效拉满 昏天暗地」「不怕性能问题，误伤也没关系」).
//   charge  the sky darkens; the magic circles open one by one; aim beams from the sky circles fall onto the edge of the
//           area and close in on its centre
//   storm   lightning bolts from the lowest sky circle all over the area, a burst where each lands; at its end the gather:
//           the bolts stop, every circle draws in and sends its light to the top circle's centre
//   cannon  the main beam from the top circle straight down on the centre, a shock wave; rings of blasts run out from it to
//           the edge, the last with the great shock wave
//   after   fires and smoke over the area; the sky comes back
// Every ray starts on a circle (the user, 2026-10-20: 「射线出发点应该在法阵上吧」); the gather is their 「最后一发时法阵增加
// 一个向中间蓄力的细节」, the shock waves 「最后的爆炸加个冲击波」.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew {
namespace ultimate {
constexpr float kPi=3.14159265358979f;
constexpr int kFps=60;
// Frames from the call (the stock countdown already over: the hook is the satellite's fire state).
// Slow on purpose (the user, 2026-10-20, after a 25 s first cut: 「来的太快了，应该慢一点，不然没那种宏状感」): 36 s in all.
// The after is short (a player, 2026-10-31: 「爆炸光效持续过久 使用后几乎需要一段时间……才会恢复」; before, 10 s with the
// sky coming back over the last 8 of them): the circles close, the sky is back 4 s after the cannon, and it is over.
constexpr int kChargeEnd=12*kFps;            // 0 .. 720: charge
constexpr int kStormEnd=kChargeEnd+10*kFps;  // .. 1320: storm (its last kGather frames the gather)
constexpr int kCannonEnd=kStormEnd+8*kFps;   // .. 1800: cannon
constexpr int kAfterEnd=kCannonEnd+6*kFps;   // .. 2160: after; the show is over (was 10 s)
constexpr int kSkyBackAt=kCannonEnd+1*kFps;  // the sky starts coming back a second into the after (was 2 s)
constexpr float kSkyBackSec=3.0f;            // and is back 4 s into it, ~5 s after the last ring (was 8 s: 10 s into it)
constexpr float kSkyDarkSec=8.0f;            // the charge's fade into the dark sky
constexpr int kGather=150;                   // the storm's last 2.5 s: the circles gather their light for the shot
constexpr int kGatherAt=kStormEnd-kGather;
constexpr int kBoltCalm=40;                  // frames the bolts take to die out once the gather begins

constexpr float kRadius=220.0f;           // m: the area the strike clears
constexpr int kAims=9;                    // the aim beams: three from each sky circle
constexpr float kAimSizeFrom=0.8f,kAimSizeTo=7.0f;
constexpr float kAimRim=0.83f;            // of a sky circle's radius: its rune band, where an aim beam leaves it
constexpr int kAimStagger=25;             // frames between a circle's three aim beams
// The bolts: from the lowest sky circle (their landing scaled in by kBoltFrom, a little off plumb), kBoltsPerSec on average,
// more as the storm goes on. They break out from the centre the aim beams closed in on: the disc they land in grows from
// kBoltCore of the area to all of it over the storm's first kBoltSpread frames (the user, 2026-10-20, of bolts all over the
// area the moment the aims had met on the centre: 「射线会往中间蓄力，但是又突然开始散射，感觉稍微有点怪」).
constexpr float kBoltFrom=0.5f,kBoltLean=15.0f;
constexpr int kBoltSpread=180;
static_assert(kChargeEnd+kBoltSpread<kGatherAt,"the bolts cover the area before the gather");
constexpr float kBoltCore=0.08f;
constexpr float kBoltsPerSec=150.0f;
constexpr int kBoltFrames=8;              // a bolt's rounds (the SGO's: pylib/vcobjects.py ULT_BOLT_ROUNDS)
// The main beam: from the top sky circle's centre straight down, growing to full size in kBeamGrow frames (and thinning
// away as long).
constexpr int kBeamGrow=120;
constexpr float kBeamSize=48.0f,kCoreSize=18.0f;
// The rings: kRings, one every kRingGap frames once the beam is full (kBeamGrow into the cannon), the first on the centre;
// each blast kRingBlast m. A slow wave out to the edge.
constexpr int kRings=12;
constexpr int kRingGap=26;
constexpr float kRingBlast=55.0f;
constexpr int kLastRing=kStormEnd+kBeamGrow+(kRings-1)*kRingGap;   // the show frame the last ring goes off
static_assert(kChargeEnd<kGatherAt && kGatherAt<kStormEnd && kStormEnd<kCannonEnd && kCannonEnd<kAfterEnd,"phases in order");
static_assert(kStormEnd+kBeamGrow+kRings*kRingGap<=kCannonEnd,"every ring goes off in the cannon phase");

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
inline float Smooth(float t) noexcept { t=t<0.0f ? 0.0f : t>1.0f ? 1.0f : t; return t*t*(3.0f-2.0f*t); }

// ---- the magic circles (the user: 「魔法阵是必须有」) ----
// Instances of the models (pylib/magic_circle.py LAYERS; src/ultimate.cpp kCircleFiles in this order), each spawned on its
// frame, opened out from nothing, turned, drawn in for the gather and closed away in the after. The ground's three lie on the
// area, each `lift` m over its own plane (src/ultimate.cpp CirclePlane: kPlaneOver over the highest ground under its disc as
// it opens out past its size, never more than kPlaneMost over the centre: the user, 2026-10-20, 「法阵会进入地里一部分」), the
// sky's three rise to `lift` m over the centre. Their sizes are the models'
// own (kModelRadius: the pattern's radius in metres) times `size`. The shock waves' model is the last.
enum class CircleModel : unsigned char { outer, star, core, sky, shock };
constexpr int kModelCount=5;
constexpr float kModelRadius[kModelCount]={220.0f,176.0f,72.0f,160.0f,100.0f};   // pylib/magic_circle.py LAYERS' radius
struct CircleSpec { CircleModel model; int appear; float lift,size,spin; bool sky; };   // spin: rad/s at the charge's pace
constexpr CircleSpec kCircles[]={
    {CircleModel::outer,0,0.6f,1.0f,0.22f,false},
    {CircleModel::star,70,1.0f,1.0f,-0.38f,false},
    {CircleModel::core,140,1.4f,1.0f,0.9f,false},
    {CircleModel::sky,220,260.0f,1.0f,-0.28f,true},
    {CircleModel::sky,310,430.0f,0.72f,0.34f,true},
    {CircleModel::sky,400,600.0f,0.5f,-0.45f,true},
};
constexpr int kCircleCount=static_cast<int>(sizeof(kCircles)/sizeof(kCircles[0]));
constexpr int kGroundCircles=3;                   // the first three
constexpr float kPlaneOver=1.5f,kPlaneMost=60.0f;
constexpr float kPlaneReach=1.12f;                // of a circle's size: the most it opens out to (EaseOutBack, the release)
constexpr int kSkyLow=3,kSkyTop=5;                // the bolts' circle, the main beam's
constexpr int kCircleOpen=100;                    // frames to open out
constexpr int kCircleRise=180;                    // frames a sky circle takes to rise
constexpr int kCircleCloseAt=kCannonEnd+2*kFps;   // the first starts closing; each next kCircleStagger later
constexpr int kCircleStagger=15;
constexpr int kCircleClose=120;                   // frames to close away
constexpr float kSqueeze=0.18f;                   // the gather draws every circle in by this much of its size
constexpr int kRelease=36;                        // frames the circles take to spring back out when the beam fires
static_assert(kCircleCloseAt+(kCircleCount-1)*kCircleStagger+kCircleClose<=kAfterEnd,"every circle is gone before the end");
static_assert(kCircles[kSkyLow].appear+kCircleRise<kChargeEnd,"the bolts' circle is up before the storm");
static_assert(!kCircles[kGroundCircles-1].sky && kCircles[kGroundCircles].sky && kSkyLow==kGroundCircles,"the ground's circles first");

// The turn's pace by stretch of the show: 1 in the charge, quicker as it builds, quickest in the gather. Joined, so the
// angle never jumps.
constexpr int kPaceEnds[]={kChargeEnd,kGatherAt,kStormEnd,kCannonEnd,kAfterEnd};
constexpr float kPaces[]={1.0f,1.8f,5.0f,3.2f,2.2f};
// Seconds of turning (at pace 1) from frame 0 to `frame`: the pace integrated.
inline float SpinTime(int frame) noexcept {
    float t=0.0f;
    int from=0;
    for(int k=0;k<5 && from<frame;++k) {
        const int to=frame<kPaceEnds[k] ? frame : kPaceEnds[k];
        t+=static_cast<float>(to-from)/kFps*kPaces[k];
        from=to;
    }
    return t;
}
struct CirclePose { bool shown; float scale,angle,lift; };
inline float EaseOutBack(float t) noexcept {
    const float c=1.70158f,u=t-1.0f;
    return 1.0f+(c+1.0f)*u*u*u+c*u*u;
}
// The gather's squeeze of every circle on show frame `frame`: drawn in through the gather, sprung back out (a little past
// its size) as the beam fires, 1 otherwise.
inline float Squeeze(int frame) noexcept {
    if(frame>=kGatherAt && frame<kStormEnd)return 1.0f-kSqueeze*Smooth(static_cast<float>(frame-kGatherAt)/kGather);
    if(frame>=kStormEnd && frame<kStormEnd+kRelease) {
        const float t=static_cast<float>(frame-kStormEnd)/kRelease;
        return 1.0f-kSqueeze*(1.0f-t)+0.12f*std::sin(kPi*t);
    }
    return 1.0f;
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
    const float s=c.size*open*close*Squeeze(frame);
    p.scale=s>0.001f ? s : 0.001f;
    p.angle=c.spin*(SpinTime(frame)-SpinTime(c.appear));
    if(frame>=closeAt)p.angle+=c.spin*4.0f*static_cast<float>(frame-closeAt)/kFps;   // it whirls as it closes
    const float rise=c.sky ? (t>=kCircleRise ? 1.0f : 1.0f-(1.0f-t/kCircleRise)*(1.0f-t/kCircleRise)) : 1.0f;
    p.lift=c.lift*rise;
    return p;
}
// The point at `rim` of circle `i`'s radius and `angle` on its own turn, on frame `frame`: x, z offsets from the centre and
// its lift (over the ground plane for a ground circle, over the centre for a sky one): out = {x, lift, z}.
inline void CirclePoint(int i,int frame,float rim,float angle,float* out) noexcept {
    const CirclePose p=CircleAt(i,frame);
    const float r=kModelRadius[static_cast<int>(kCircles[i].model)]*p.scale*rim,a=angle+p.angle;
    out[0]=r*std::cos(a);out[1]=p.lift;out[2]=r*std::sin(a);
}

// Aim beam `i` on charge frame `frame`: whether it is out yet, where it leaves its sky circle (CirclePoint's {x, lift, z})
// and where it aims (x, z offset), and its size. Each leaves its circle once that has opened; the aims go in from the edge to
// the centre on an ease-out curve, turning as they go.
struct Aim { bool shown; float from[3],at[2],size; };
inline int AimStart(int i) noexcept { return kCircles[kSkyLow+i%3].appear+kCircleOpen+(i/3)*kAimStagger; }
inline Aim AimAt(int i,int frame) noexcept {
    Aim a{};
    const int start=AimStart(i);
    if(frame<start || frame>=kChargeEnd)return a;
    a.shown=true;
    const float t=static_cast<float>(frame-start)/(kChargeEnd-start);
    const float base=2.0f*kPi*static_cast<float>(i)/kAims;
    CirclePoint(kSkyLow+i%3,frame,kAimRim,base,a.from);
    const float in=1.0f-(1.0f-t)*(1.0f-t);             // ease out: quick at first, settling on the centre
    const float turn=base+1.2f*t;
    const float r=kRadius*(1.0f-in);
    a.at[0]=r*std::cos(turn);a.at[1]=r*std::sin(turn);
    a.size=kAimSizeFrom+(kAimSizeTo-kAimSizeFrom)*t*t;
    return a;
}
static_assert(kCircles[kSkyTop].appear+kCircleOpen+2*kAimStagger<kChargeEnd,"every aim beam is out in the charge");

// Bolts landing on storm frame `frame` (0 = the storm's first): how many (at most kMaxBoltsAFrame), each one's landing
// offset and where it leaves the lowest sky circle (x, z offset: its landing scaled in by kBoltFrom, off by its lean).
// BoltReach: the radius they land in on that frame. The rate climbs from a third to five thirds kBoltsPerSec over the storm and dies out as the gather begins.
constexpr int kMaxBoltsAFrame=6;
struct Bolt { float at[2],from[2]; };
inline float BoltReach(int frame) noexcept {
    const float t=frame>=kBoltSpread ? 1.0f : static_cast<float>(frame>0 ? frame : 0)/kBoltSpread;
    return kRadius*(kBoltCore+(1.0f-kBoltCore)*(1.0f-(1.0f-t)*(1.0f-t)));   // ease out: bursting out, slowing to the edge
}
inline int BoltsAt(std::uint64_t seed,int frame,Bolt* out) noexcept {
    if(frame<0 || frame>=kStormEnd-kChargeEnd)return 0;
    Rng g=FrameRng(seed^0xB017ull,frame);
    const float t=static_cast<float>(frame)/(kStormEnd-kChargeEnd);
    const float calm=1.0f-static_cast<float>(frame-(kGatherAt-kChargeEnd))/kBoltCalm;
    float mean=kBoltsPerSec*(1.0f/3.0f+4.0f/3.0f*t)/kFps;   // bolts this frame on average
    if(calm<1.0f)mean*=calm>0.0f ? calm : 0.0f;
    int n=static_cast<int>(mean);
    if(g.Unit()<mean-static_cast<float>(n))++n;
    if(n>kMaxBoltsAFrame)n=kMaxBoltsAFrame;
    for(int k=0;k<n;++k) {
        InDisc(g,BoltReach(frame),out[k].at);
        float lean[2];
        InDisc(g,kBoltLean,lean);
        out[k].from[0]=out[k].at[0]*kBoltFrom+lean[0];out[k].from[1]=out[k].at[1]*kBoltFrom+lean[1];
    }
    return n;
}

// The gather's rays (show frames kGatherAt..kStormEnd): kGatherEach from the rim of each circle but the top one, pulses of
// light running from the rim to the top circle's centre (where the main beam starts). A pulse is a short laser (kGatherReach
// m: pylib/vcobjects.py ULT_GATHER's speed x life) fired each frame from a point sliding along the ray, from the rim to
// kGatherReach short of the centre (it ends on the centre, never past it), over kGatherPulse frames, again and again.
constexpr int kGatherEach=4;
constexpr int kGatherRays=(kCircleCount-1)*kGatherEach;   // kSkyTop is the last circle: none from it
constexpr float kGatherRim=0.95f,kGatherReach=120.0f;
constexpr int kGatherPulse=40;
static_assert(kSkyTop==kCircleCount-1,"the gather's rays come from every circle before the top one");
struct GatherRay { bool shown; int circle; float angle,slide,size; };   // slide: 0 at the rim .. 1 at the farthest start
inline GatherRay GatherAt(int k,int frame) noexcept {
    GatherRay g{};
    if(k<0 || k>=kGatherRays || frame<kGatherAt || frame>=kStormEnd)return g;
    g.shown=true;
    g.circle=k/kGatherEach;
    g.angle=2.0f*kPi*static_cast<float>(k%kGatherEach)/kGatherEach+0.6f*static_cast<float>(g.circle);
    const float t=static_cast<float>(frame-kGatherAt)/kGather;
    const float u=static_cast<float>(frame-kGatherAt)/kGatherPulse+0.37f*static_cast<float>(k);
    g.slide=u-std::floor(u);
    g.size=0.6f+3.4f*t*t;
    return g;
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
    frame-=kBeamGrow;
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

// The shock waves (the user, 2026-10-20: 「最后的爆炸加个冲击波之类的感觉」): flat rings of light (the SHOCK model) running out
// from the centre at `speed` m/s from show frame `at` until `reach` m, on the ground or at a sky circle's height (`lift` over
// the plane / the centre). The great one goes with the last ring of blasts, the others with the beam.
struct WaveSpec { int at; float speed,reach,lift; bool sky; float spin; };
constexpr WaveSpec kWaves[]={
    {kStormEnd+6,300.0f,1.6f*kRadius,3.0f,false,0.5f},         // the beam lands
    {kStormEnd,420.0f,2.2f*kRadius,600.0f,true,-0.8f},         // the top circle lets go (kCircles[kSkyTop].lift)
    {kLastRing,380.0f,3.0f*kRadius,3.0f,false,0.35f},          // the last ring: the final blast's
    {kLastRing+18,300.0f,2.2f*kRadius,3.0f,false,-0.5f},       // and its echo
};
constexpr int kWaveCount=static_cast<int>(sizeof(kWaves)/sizeof(kWaves[0]));
constexpr int kGreatWave=2;
constexpr float kWaveFrom=12.0f;   // m: a wave's radius on its first frame
struct WavePose { bool shown; float radius,scale,angle; };
inline int WaveFrames(int w) noexcept {
    return static_cast<int>(std::ceil((kWaves[w].reach-kWaveFrom)/kWaves[w].speed*static_cast<float>(kFps)));
}
inline WavePose WaveAt(int w,int frame) noexcept {
    WavePose p{};
    const WaveSpec& s=kWaves[w];
    if(frame<s.at || frame>=s.at+WaveFrames(w))return p;
    p.shown=true;
    const float t=static_cast<float>(frame-s.at)/kFps;
    p.radius=kWaveFrom+s.speed*t;
    p.scale=p.radius/kModelRadius[static_cast<int>(CircleModel::shock)];
    p.angle=s.spin*t;
    return p;
}

// ---- the bursts: every blast the show puts on the ground, as seen and as dealt ----
// The user, 2026-10-25: 「天罚的伤害和实际落点是不是不一样啊，感觉外面的闪电爆炸没伤害」. Before, each phase fired its damage
// round and drew its explosion separately (the storm's spark, a ring's blast), and the ground shock waves threw up bursts
// along their fronts out to 1.6 R with no damage at all: blasts seen outside the area the rounds covered. One list a frame now
// (BurstsAt) is both: src/ultimate.cpp fires each one's damage round (the charge whose blast radius is the burst's: kSparkRadius
// the spark SGO's, kRingBlast the blast SGO's, pylib/vcobjects.py ULT_*_RADIUS) at the burst's point and draws its look from
// that same point and radius, and only when the round is there (a show whose caller is gone plays its effects out alone).
// The bolts' landings are the storm's bursts (BoltsAt, the same numbers), the rings' blasts the cannon's, and every
// kWaveEvery frames each ground shock wave's front kWaveBursts round its circle while it is within kWaveBurstReach.
enum class BurstKind : unsigned char { spark, blast, wave };   // a bolt's landing, a ring's blast, a shock wave's front
struct Burst { float at[2]; BurstKind kind; float radius,share; bool great; };   // great: the great shock wave's
constexpr float kSparkRadius=40.0f;            // m: the spark SGO's blast (ULT_SPARK_RADIUS); a wave's bursts are sparks too
constexpr float kBoltShare=0.25f,kSparkShare=0.25f,kBlastShare=1.0f,kBeamShare=0.03f,kCoreShare=0.03f;
constexpr float kWaveShare=0.05f;              // a wave front's burst (未调: a front crosses a point in a few of them)
constexpr int kWaveEvery=5,kWaveBursts=16;
constexpr float kWaveBurstReach=1.6f*kRadius;  // m: how far out a ground wave's front bursts (its disc runs on, light only)
constexpr int kMaxBurstsAFrame=64;
inline float BurstRadius(BurstKind k) noexcept { return k==BurstKind::blast ? kRingBlast : kSparkRadius; }
// Ground wave `w`'s front on show frame `frame`: whether it bursts then, and its radius.
inline bool WaveBursts(int w,int frame,float* radius) noexcept {
    const WaveSpec& s=kWaves[w];
    const WavePose p=WaveAt(w,frame);
    if(!p.shown || s.sky || (frame-s.at)%kWaveEvery || p.radius>kWaveBurstReach)return false;
    *radius=p.radius;
    return true;
}
// The bursts of show frame `frame` (at most `most`): how many. The same seed and frame give every machine the same.
inline int BurstsAt(std::uint64_t seed,int frame,Burst* out,int most) noexcept {
    int n=0;
    const auto add=[&](float x,float z,BurstKind kind,float share,bool great){
        if(n<most)out[n++]=Burst{{x,z},kind,BurstRadius(kind),share,great};
    };
    if(PhaseAt(frame)==Phase::storm) {
        Bolt bolts[kMaxBoltsAFrame];
        const int b=BoltsAt(seed,frame-kChargeEnd,bolts);
        for(int k=0;k<b;++k)add(bolts[k].at[0],bolts[k].at[1],BurstKind::spark,kSparkShare,false);
    }
    if(PhaseAt(frame)==Phase::cannon) {
        const int k=RingAt(frame-kStormEnd);
        if(k>=0) {
            const Ring r=RingOf(k,seed);
            for(int i=0;i<r.count;++i) {
                const float a=r.phase+2.0f*kPi*static_cast<float>(i)/r.count;
                add(r.radius*std::cos(a),r.radius*std::sin(a),BurstKind::blast,kBlastShare,false);
            }
        }
    }
    for(int w=0;w<kWaveCount;++w) {
        float radius;
        if(!WaveBursts(w,frame,&radius))continue;
        const int step=(frame-kWaves[w].at)/kWaveEvery;
        for(int k=0;k<kWaveBursts;++k) {
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f*static_cast<float>(step%2))/kWaveBursts;
            add(radius*std::cos(a),radius*std::sin(a),BurstKind::wave,kWaveShare,w==kGreatWave);
        }
    }
    return n;
}

// ---- the looks, on a budget ----
// A player, 2026-10-31 (docs/feedback-2026-10-31-ultimate-fx.md): 「天罚导弹的爆炸光效持续过久 使用后几乎需要一段时间……才会
// 恢复 并且低配置玩家使用后会很卡 几乎没有战斗能力」. Every burst's round (BurstsAt) is a SolidBullet01 charge with its blast
// radius (pylib/vcobjects.py ult_rounds: the gunship's charge), whose own explosion is the burst seen; before, each burst drew
// a stock effect on top of it as well: an Explosion(20) for each of ~1000 bolt landings and 624 wave fronts' bursts, a Smoke
// (ExplosionWind) of 25 / 40 for each of those 624 (stock: 0.1 .. 2), an Explosion_Big of 27.5 for each of the 148 ring
// blasts (stock 1, the 4.1 ports' at most 6; it makes an ExplosionWind, a Blazing and a quake of its own: docs/edf41-classes.md
// Explosion_Big), the after 60 Blazing(6) and 20 Smoke(25), a 110 m StencilLight 8 frames long for every bolt (~27 at once).
// Now the bursts' rounds are their explosions, and on top of them only what LooksAt lists, every frame, within these:
// sizes no larger than the stock code's (kStock*), the counts and lights at once capped (tools/ultimate_check.cpp counts a
// whole show). The damage, the rounds and their SGOs' radii are as they were.
constexpr float kStockExplosionMost=6.0f;   // Explosion_Big's size, the 4.1 ports' largest (src/edf41mother.cpp: 6)
constexpr float kStockSmokeMost=2.0f;       // ExplosionWind's size, the stock's largest (src/edf41mother.cpp Smoke: 0.1 .. 2)
constexpr float kStockBlazeMost=1.5f;       // Blazing's size, the 4.1 ports' largest (src/edf41mother.cpp: 0.25 .. 1.5)
constexpr float kBlastLook=6.0f;            // a ring blast's Explosion (the plain one: no smoke, no fire of its own)
constexpr float kDustSize=2.0f;             // the great shock wave's dust where its front crosses kDustAt
constexpr float kDustAt=0.9f*kRadius;
constexpr int kDustEvery=2;                 // of its front's bursts there: every other one
constexpr int kFireEvery=10,kFireFor=2*kFps;   // the after's fires: one every kFireEvery frames over its first kFireFor
constexpr float kFireSize=1.5f;
constexpr int kFireSmokeEvery=3;            // of those fires, every third smokes
constexpr float kFireSmoke=2.0f;
constexpr int kMostSmokes=12,kMostFires=12;    // a show's whole (tools/ultimate_check.cpp)
static_assert(kBlastLook<=kStockExplosionMost && kDustSize<=kStockSmokeMost && kFireSmoke<=kStockSmokeMost &&
              kFireSize<=kStockBlazeMost,"no look larger than the stock code's");
// The lights (StencilLight, alive `frames` frames from the frame it is made). A light of kBigLight m or more is big (it
// covers most of the screen): at most kMostBigLights of a show's at once; the bolts' at most kMostBoltLights (one made every
// kBoltLightEvery frames, on the frame's first bolt), every light of a show at most kMostLights.
constexpr int kBoltLightEvery=2,kBoltLightFrames=8;
constexpr float kBoltLightRadius=80.0f;     // was 110 m, one for every bolt
constexpr int kChargeLightEvery=30,kChargeLightFrames=30;   // the charge's crimson (was 40 frames: two at once)
constexpr float kChargeLightRadius=0.8f*kRadius;
constexpr int kGatherLightEvery=10,kGatherLightFrames=10;   // the gather's gold, growing (was one every 8 frames, 10 long)
constexpr float kGatherLightFrom=40.0f,kGatherLightGrow=160.0f;
constexpr int kBeamLightEvery=12,kBeamLightFrames=12;       // the main beam's violet (was one every 10 frames, 14 long)
constexpr float kBeamLightRadius=kRadius;
constexpr float kBigLight=150.0f;
constexpr int kMostBoltLights=4,kMostBigLights=1,kMostLights=kMostBoltLights+kMostBigLights;
static_assert((kBoltLightFrames+kBoltLightEvery-1)/kBoltLightEvery<=kMostBoltLights && kChargeLightFrames<=kChargeLightEvery &&
              kGatherLightFrames<=kGatherLightEvery && kBeamLightFrames<=kBeamLightEvery,"the lights within their caps");
constexpr int kMaxLooksAFrame=40;           // a ring's blasts (at most 23), the great wave's dust (8), a light

enum class LookKind : unsigned char { explosion, smoke, blaze, light };
// Where: the ground under the offset (`at`), the centre (on the ground), the top sky circle's centre (the main beam's start).
enum class LookPlace : unsigned char { ground, centre, beamTop };
enum class LightTint : unsigned char { none, crimson, bolt, violet, gold };   // a light's colour (src/ultimate.cpp)
// `size`: an effect's size or a light's radius (m); `frames`: a light's life; `burst`: the index in BurstsAt's list of the
// same frame whose look it is (drawn only where that burst's round is: nothing seen without its damage), -1 none.
struct Look { LookKind kind; LookPlace place; float at[2]; float size; int frames; LightTint tint; int burst; };

// The after's fires: on after frame `frame`, whether one starts and where (an offset in the area).
inline bool FireAt(std::uint64_t seed,int frame,float* at) noexcept {
    if(frame<0 || frame>=kFireFor || frame%kFireEvery)return false;
    Rng g=FrameRng(seed^0xF14Eull,frame);
    InDisc(g,kRadius*0.9f,at);
    return true;
}
// Whether ground wave `w`'s front bursts on show frame `frame` for the first time at kDustAt or past it.
inline bool DustAt(int w,int frame) noexcept {
    float r,before;
    if(w!=kGreatWave || !WaveBursts(w,frame,&r) || r<kDustAt)return false;
    return !WaveBursts(w,frame-kWaveEvery,&before) || before<kDustAt;
}
// The looks of show frame `frame` (at most `most`): how many. `bursts`/`count`: BurstsAt(seed, frame)'s list, which their
// `burst` index into. The same seed and frame give every machine the same.
inline int LooksAt(std::uint64_t seed,int frame,const Burst* bursts,int count,Look* out,int most) noexcept {
    int n=0;
    const auto add=[&](LookKind kind,LookPlace place,float x,float z,float size,int frames,LightTint tint,int burst){
        if(n<most)out[n++]=Look{kind,place,{x,z},size,frames,tint,burst};
    };
    const auto light=[&](LookPlace place,float x,float z,float radius,int frames,LightTint tint){
        add(LookKind::light,place,x,z,radius,frames,tint,-1);
    };
    for(int k=0;k<count;++k) {
        const Burst& b=bursts[k];
        if(b.kind==BurstKind::blast)add(LookKind::explosion,LookPlace::ground,b.at[0],b.at[1],kBlastLook,0,LightTint::none,k);
    }
    for(int w=0;w<kWaveCount;++w) {
        if(!DustAt(w,frame))continue;
        int i=0;
        for(int k=0;k<count;++k) {
            const Burst& b=bursts[k];
            if(b.kind!=BurstKind::wave || b.great!=(w==kGreatWave))continue;
            if(i++%kDustEvery==0)add(LookKind::smoke,LookPlace::ground,b.at[0],b.at[1],kDustSize,0,LightTint::none,k);
        }
    }
    switch(PhaseAt(frame)) {
    case Phase::charge:
        if(frame%kChargeLightEvery==0)light(LookPlace::centre,0,0,kChargeLightRadius,kChargeLightFrames,LightTint::crimson);
        break;
    case Phase::storm: {
        const int f=frame-kChargeEnd;
        Bolt bolt[kMaxBoltsAFrame];
        if(f%kBoltLightEvery==0 && BoltsAt(seed,f,bolt)>0)
            light(LookPlace::ground,bolt[0].at[0],bolt[0].at[1],kBoltLightRadius,kBoltLightFrames,LightTint::bolt);
        const int g=frame-kGatherAt;
        if(g>=0 && g%kGatherLightEvery==0)
            light(LookPlace::beamTop,0,0,kGatherLightFrom+kGatherLightGrow*static_cast<float>(g)/kGather,kGatherLightFrames,LightTint::gold);
        break;
    }
    case Phase::cannon:
        if((frame-kStormEnd)%kBeamLightEvery==0)light(LookPlace::centre,0,0,kBeamLightRadius,kBeamLightFrames,LightTint::violet);
        break;
    case Phase::after: {
        const int f=frame-kCannonEnd;
        float at[2];
        if(FireAt(seed,f,at)) {
            add(LookKind::blaze,LookPlace::ground,at[0],at[1],kFireSize,0,LightTint::none,-1);
            if((f/kFireEvery)%kFireSmokeEvery==0)add(LookKind::smoke,LookPlace::ground,at[0],at[1],kFireSmoke,0,LightTint::none,-1);
        }
        break;
    }
    case Phase::over: break;
    }
    return n;
}
}  // namespace ultimate
}  // namespace crew
