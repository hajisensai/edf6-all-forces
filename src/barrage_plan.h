// The long-range barrages without the game (src/barrage.cpp fires and flies them, tools/barrage_check.cpp checks them; no
// EDF.dll in here; docs/plan-2026-10-25-strikes.md §1.1). A barrage is a list of rounds planned once at the call from its
// marked point, its heading and a seed, the same on every machine: when each leaves (frames from the call), from where, the
// curve it flies and how long it takes, how big it is and how wide it bursts. The plugin flies each round along its curve
// (PathAt: where it is `frame` frames after it left), whatever the game's own motion would do.
//   arc      a shell's flight: the chord from its launcher to its point, raised by `rise` in the middle (a parabola in
//            time: the horizontal speed even, the vertical a fall)
//   lobDive  a cubic from its launcher straight up, over its point and straight down onto it (`rise` over both ends); `warp`
//            > 1 spends its time going up and comes down fast (the ballistic missile)
//   cruise   low over the ground at `cruiseY` (over everything on the way), a pop-up short of its point and a dive onto it
// A cluster's carrier ends at `splitAt` frames into its flight (the plugin puts it out); its bomblets are rounds of their own
// leaving that point at that frame, and `burst` marks the flash there.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace crew {
namespace barrage {
constexpr float kPi=3.14159265358979f;
constexpr int kFps=60;

// The calls (tools/calls.py brings 'barrage', `barrage` its kind's name; kKindNames below, in this order).
enum class Kind : std::uint8_t { katyusha, guided, thermobaric, cluster, creeping, mrsi, heavy, cruise, ballistic, count };
constexpr int kKinds=static_cast<int>(Kind::count);
constexpr const char* kKindNames[kKinds]={"katyusha","guided","thermobaric","cluster","creeping","mrsi","heavy","cruise",
                                          "ballistic"};
enum class Path : std::uint8_t { arc, lobDive, cruise };

constexpr int kMaxRounds=160;          // a barrage's rounds at most (the cluster's 12 carriers and 144 bomblets)
constexpr float kEdgeRoom=200.0f;      // m: a launcher this far inside the physics world's edge at least
constexpr float kLaunchLift=4.0f;      // m: a launcher's round leaves this far over its ground

struct Round {
    int launch=0;            // frames from the call it leaves
    int flight=1;            // frames it takes to its point
    Path path=Path::arc;
    float from[3]{},to[3]{};
    float rise=0.0f;         // arc: over the chord's middle; lobDive: the control points over both ends
    float warp=1.0f;         // lobDive: the curve's parameter is (time share)^warp
    float cruiseY=0.0f;      // cruise: its level
    float popY=0.0f;         // cruise: the top of its pop-up
    float size=1.0f;         // AmmoSize: its thickness (and, with the mark, its hit radius)
    float blast=1.0f;        // AmmoExplosion, m
    float share=1.0f;        // of the call's damage
    int splitAt=-1;          // frames into its flight it splits (a cluster's carrier), -1 never
    bool burst=false;        // a bomblet's flash goes off where it leaves (one per carrier)
};

// The setup every machine has alike: the marked point (on the ground), the call's heading (the rounds come from behind it,
// the creeping barrage walks along it), the seed (Seed), the physics world's half side, and for the guided rockets the
// points they may strike, nearest the mark first (an enemy each; fewer than its rounds: the rest spread round the mark).
struct Setup {
    float target[3]{};
    float heading[3]{0.0f,0.0f,1.0f};
    std::uint64_t seed=0;
    float worldHalf=3000.0f;
    int aims=0;
    const float (*aim)[3]=nullptr;
};

// The seed every machine works out alike from what the call itself carries (its point and heading, as sent).
inline std::uint64_t Seed(const float* target,const float* heading,int kind) noexcept {
    std::uint64_t h=1469598103934665603ull^static_cast<std::uint64_t>(kind);
    for(int i=0;i<3;++i)for(const float* v:{target,heading}) {
        std::uint32_t bits;
        static_assert(sizeof(bits)==sizeof(float),"float bits");
        std::memcpy(&bits,v+i,4);
        h=(h^bits)*1099511628211ull;
    }
    return h;
}

// A seeded stream (splitmix64).
struct Rng {
    std::uint64_t s;
    std::uint64_t Next() noexcept {
        std::uint64_t z=(s+=0x9E3779B97F4A7C15ull);
        z=(z^(z>>30))*0xBF58476D1CE4E5B9ull;z=(z^(z>>27))*0x94D049BB133111EBull;
        return z^(z>>31);
    }
    float Unit() noexcept { return static_cast<float>(Next()>>40)*(1.0f/16777216.0f); }   // [0, 1)
    float Range(float a,float b) noexcept { return a+(b-a)*Unit(); }
    // A point in the disc of `radius` round (0, 0), evenly over its area.
    void Disc(float radius,float* x,float* z) noexcept {
        const float r=radius*std::sqrt(Unit()),a=2.0f*kPi*Unit();
        *x=r*std::cos(a);*z=r*std::sin(a);
    }
};

// ---------------------------------------------------------------------------------------------------------- the curves
inline float Lerp(float a,float b,float t) noexcept { return a+(b-a)*t; }

// The cruise curve's control points: its launcher, its climb, the start of its pop-up, the top of it, its point.
inline void CruisePoints(const Round& r,float (*p)[3]) noexcept {
    float d[2]={r.to[0]-r.from[0],r.to[2]-r.from[2]};
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]);
    if(len>1e-3f){d[0]/=len;d[1]/=len;}
    const float climb=len<600.0f ? len*0.25f : 150.0f;
    const float pop=len<1500.0f ? len*0.3f : 480.0f,top=pop*0.4f;
    const float at[5][2]={{0.0f,r.from[1]},{climb,r.cruiseY},{len-pop,r.cruiseY},{len-top,r.popY},{len,r.to[1]}};
    for(int i=0;i<5;++i){p[i][0]=r.from[0]+d[0]*at[i][0];p[i][1]=at[i][1];p[i][2]=r.from[2]+d[1]*at[i][0];}
    p[0][1]=r.from[1];p[4][0]=r.to[0];p[4][2]=r.to[2];
}
// The clamped uniform quadratic B-spline over the cruise points at s in [0, 1]: it leaves the launcher toward the climb and
// comes onto the point from the pop-up's top.
inline void CruiseSpline(const float (*p)[3],float s,float* out) noexcept {
    constexpr int kSpans=3;   // 5 points, degree 2: knots 0 0 0 1 2 3 3 3
    float u=s*kSpans;
    int k=static_cast<int>(u);
    if(k>kSpans-1)k=kSpans-1;
    if(k<0)k=0;
    const float t=u-static_cast<float>(k);
    // The spans' basis (clamped ends): the first and the last differ from the middle one.
    float b0,b1,b2;
    if(k==0){b0=(1-t)*(1-t);b1=2*t-1.5f*t*t;b2=0.5f*t*t;}
    else if(k==kSpans-1){b0=0.5f*(1-t)*(1-t);b1=-1.5f*t*t+t+0.5f;b2=t*t;}
    else {b0=0.5f*(1-t)*(1-t);b1=-t*t+t+0.5f;b2=0.5f*t*t;}
    for(int i=0;i<3;++i)out[i]=b0*p[k][i]+b1*p[k+1][i]+b2*p[k+2][i];
}
constexpr int kCruiseSamples=64;
// The cruise curve's length and where along it (its spline parameter) a share `u` of that length is.
inline float CruiseParam(const float (*p)[3],float u,float* length=nullptr) noexcept {
    float acc[kCruiseSamples+1];
    float last[3];CruiseSpline(p,0.0f,last);
    acc[0]=0.0f;
    for(int i=1;i<=kCruiseSamples;++i) {
        float q[3];CruiseSpline(p,static_cast<float>(i)/kCruiseSamples,q);
        const float d[3]={q[0]-last[0],q[1]-last[1],q[2]-last[2]};
        acc[i]=acc[i-1]+std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        last[0]=q[0];last[1]=q[1];last[2]=q[2];
    }
    if(length)*length=acc[kCruiseSamples];
    const float want=u*acc[kCruiseSamples];
    for(int i=1;i<=kCruiseSamples;++i)if(acc[i]>=want) {
        const float span=acc[i]-acc[i-1];
        const float f=span>1e-4f ? (want-acc[i-1])/span : 0.0f;
        return (static_cast<float>(i-1)+f)/kCruiseSamples;
    }
    return 1.0f;
}

// Round `r` on its curve at time share `u` (0 left, 1 at its point).
inline void CurveAt(const Round& r,float u,float* out) noexcept {
    if(u<0.0f)u=0.0f;
    if(u>1.0f)u=1.0f;
    switch(r.path) {
        case Path::arc:
            for(int i=0;i<3;++i)out[i]=Lerp(r.from[i],r.to[i],u);
            out[1]+=4.0f*r.rise*u*(1.0f-u);
            return;
        case Path::lobDive: {
            const float s=r.warp==1.0f ? u : std::pow(u,r.warp),t=1.0f-s;
            const float a=t*t*t,b=3.0f*t*t*s,c=3.0f*t*s*s,d=s*s*s;
            for(int i=0;i<3;++i) {
                const float lift=i==1 ? r.rise : 0.0f;
                out[i]=a*r.from[i]+b*(r.from[i]+lift)+c*(r.to[i]+lift)+d*r.to[i];
            }
            return;
        }
        case Path::cruise: {
            float p[5][3];CruisePoints(r,p);
            CruiseSpline(p,CruiseParam(p,u),out);
            return;
        }
    }
}

// Where round `r` is `frame` frames after it left: on its curve, and past its point on along the way it came in (it bursts
// on the ground or what it meets there).
inline void PathAt(const Round& r,float frame,float* out) noexcept {
    const float flight=static_cast<float>(r.flight<1 ? 1 : r.flight);
    if(frame<=flight){CurveAt(r,frame/flight,out);return;}
    float end[3],before[3];
    CurveAt(r,1.0f,end);CurveAt(r,(flight-1.0f)/flight,before);
    const float over=frame-flight;
    for(int i=0;i<3;++i)out[i]=end[i]+(end[i]-before[i])*over;
}

// ---------------------------------------------------------------------------------------------------------- the plan
// What each kind brings (the numbers the plan below is made of; docs/plan-2026-10-25-strikes.md §1.1's table).
struct KindSpec {
    float range;      // m from the mark to its launchers (less near the world's edge)
    float speed;      // m/s: its shells' even horizontal speed (arc), its path's mean (lobDive, cruise)
    float size,blast;
};
constexpr KindSpec kSpecs[kKinds]={
    {2400.0f,300.0f,6.0f,12.0f},    // katyusha: 40 rockets over 2.5 s, 80 m wide
    {3000.0f,420.0f,7.0f,18.0f},    // guided: 6 rockets up and straight down, one an aim
    {1400.0f,250.0f,8.0f,30.0f},    // thermobaric: 16 heavy rockets inside 30 m
    {3500.0f,420.0f,9.0f,8.0f},     // cluster: 12 carriers splitting 260 m up into 12 bomblets each (their blast)
    {4000.0f,450.0f,5.0f,15.0f},    // creeping: 8 lines of 8 shells walking along the heading
    {4000.0f,450.0f,5.0f,25.0f},    // mrsi: 6 shells on one point in one frame
    {1600.0f,200.0f,14.0f,45.0f},   // heavy: one 380 mm round
    {4500.0f,280.0f,10.0f,40.0f},   // cruise: one missile low over the ground
    {5000.0f,0.0f,16.0f,70.0f},     // ballistic: one warhead nearly straight down (its time below)
};
constexpr int kKatyushaRounds=40,kKatyushaGap=4,kGuidedRounds=6,kGuidedGap=20,kThermoRounds=16,kThermoGap=12;
constexpr int kClusterCarriers=12,kClusterGap=30,kBomblets=12,kCreepLines=8,kCreepPer=8,kCreepGap=90,kMrsiRounds=6;
constexpr float kKatyushaSpread=80.0f,kThermoSpread=30.0f,kClusterSpread=60.0f,kBombletSpread=140.0f;
constexpr float kClusterSplitHeight=260.0f,kBombletSpeed=110.0f,kBombletSize=3.0f,kBombletShare=0.3f;
constexpr float kCreepWidth=15.0f,kCreepStep=30.0f;
constexpr float kGuidedRise=1200.0f,kGuidedReach=200.0f,kCruiseOver=60.0f,kCruisePop=180.0f,kBallisticRise=4500.0f;
constexpr float kBallisticWarp=1.7f;
constexpr int kBallisticFlight=22*kFps;
static_assert(kClusterCarriers*(1+kBomblets)<=kMaxRounds && kCreepLines*kCreepPer<=kMaxRounds,"rounds fit");

namespace detail {
inline void Flat(const float* v,float* out) noexcept {
    out[0]=v[0];out[1]=0.0f;out[2]=v[2];
    const float l=std::sqrt(out[0]*out[0]+out[2]*out[2]);
    if(l<1e-4f){out[0]=0.0f;out[2]=1.0f;return;}
    out[0]/=l;out[2]/=l;
}
// The launchers' site: `range` behind the mark against the heading, brought in to `kEdgeRoom` inside the world.
inline void Site(const Setup& s,float range,float* out) noexcept {
    float f[3];Flat(s.heading,f);
    const float lim=s.worldHalf-kEdgeRoom;
    float t=range;
    for(int i:{0,2}) {
        const float d=-f[i];
        if(d>1e-4f && s.target[i]+d*t>lim)t=(lim-s.target[i])/d;
        if(d<-1e-4f && s.target[i]+d*t<-lim)t=(-lim-s.target[i])/d;
    }
    if(t<0.0f)t=0.0f;
    out[0]=s.target[0]-f[0]*t;out[1]=s.target[1];out[2]=s.target[2]-f[2]*t;
}
inline float FlatDist(const float* a,const float* b) noexcept {
    const float x=a[0]-b[0],z=a[2]-b[2];
    return std::sqrt(x*x+z*z);
}
inline int Frames(float metres,float speed) noexcept {
    const int f=static_cast<int>(metres/speed*static_cast<float>(kFps)+0.5f);
    return f<30 ? 30 : f;
}
}  // namespace detail

// Barrage `kind`'s rounds (its launch frames from 0) into `out` (`cap` of them at most); how many. `ground(x, z, y)`: the
// height of what stands at (x, z) (the map's top: terrain or a roof), `y` when nothing is found there.
template<class Ground>
int Plan(Kind kind,const Setup& s,Ground ground,Round* out,int cap) noexcept {
    using detail::Flat;
    const int k=static_cast<int>(kind);
    if(k<0 || k>=kKinds || cap<=0)return 0;
    const KindSpec& spec=kSpecs[k];
    Rng rng{s.seed^(0xB4A77A9Eull*static_cast<std::uint64_t>(k+1))};
    float fwd[3];Flat(s.heading,fwd);
    const float side[3]={fwd[2],0.0f,-fwd[0]};
    float site[3];detail::Site(s,spec.range,site);
    site[1]=ground(site[0],site[2],s.target[1]);
    int n=0;
    // One round from the site (moved `lateral` across the heading, `jitter` round it) onto `to`.
    auto add=[&](int launch,const float* to,float lateral,float jitter,Path path)->Round* {
        if(n>=cap)return nullptr;
        Round& r=out[n++];
        r=Round{};
        float jx,jz;rng.Disc(jitter,&jx,&jz);
        r.from[0]=site[0]+side[0]*lateral+jx;r.from[2]=site[2]+side[2]*lateral+jz;
        r.from[1]=ground(r.from[0],r.from[2],site[1])+kLaunchLift;
        r.to[0]=to[0];r.to[1]=to[1];r.to[2]=to[2];
        r.launch=launch;r.path=path;r.size=spec.size;r.blast=spec.blast;
        return &r;
    };
    auto onGround=[&](float x,float z,float* p){p[0]=x;p[2]=z;p[1]=ground(x,z,s.target[1]);};
    auto arc=[&](Round* r,float riseShare,float speed){
        if(!r)return;
        const float d=detail::FlatDist(r->from,r->to);
        r->rise=riseShare*d;
        r->flight=detail::Frames(d,speed);
    };
    switch(kind) {
        case Kind::katyusha:
            for(int i=0;i<kKatyushaRounds;++i) {
                float x,z,p[3];rng.Disc(kKatyushaSpread,&x,&z);onGround(s.target[0]+x,s.target[2]+z,p);
                Round* r=add(i*kKatyushaGap+static_cast<int>(rng.Range(0.0f,3.0f)),p,(static_cast<float>(i%4)-1.5f)*25.0f,4.0f,Path::arc);
                arc(r,0.22f,spec.speed*rng.Range(0.95f,1.05f));
            }
            break;
        case Kind::thermobaric:
            for(int i=0;i<kThermoRounds;++i) {
                float x,z,p[3];rng.Disc(kThermoSpread,&x,&z);onGround(s.target[0]+x,s.target[2]+z,p);
                Round* r=add(i*kThermoGap,p,(static_cast<float>(i%2)-0.5f)*6.0f,2.0f,Path::arc);
                arc(r,0.12f,spec.speed*rng.Range(0.97f,1.03f));
            }
            break;
        case Kind::guided:
            for(int i=0;i<kGuidedRounds;++i) {
                float p[3];
                if(i<s.aims && s.aim){p[0]=s.aim[i][0];p[1]=s.aim[i][1];p[2]=s.aim[i][2];}
                else {
                    const float a=2.0f*kPi*static_cast<float>(i)/kGuidedRounds;
                    onGround(s.target[0]+40.0f*std::cos(a),s.target[2]+40.0f*std::sin(a),p);
                }
                Round* r=add(i*kGuidedGap,p,(static_cast<float>(i%2)-0.5f)*8.0f,2.0f,Path::lobDive);
                if(!r)break;
                r->rise=kGuidedRise;
                r->flight=detail::Frames(detail::FlatDist(r->from,r->to)+2.0f*kGuidedRise,spec.speed);
            }
            break;
        case Kind::cluster:
            for(int i=0;i<kClusterCarriers;++i) {
                float x,z,p[3];rng.Disc(kClusterSpread,&x,&z);onGround(s.target[0]+x,s.target[2]+z,p);
                Round* r=add(i*kClusterGap,p,(static_cast<float>(i%3)-1.0f)*20.0f,4.0f,Path::arc);
                if(!r)break;
                arc(r,0.25f,spec.speed);
                // Its split: on the way down, kClusterSplitHeight over its point.
                int split=r->flight;
                for(int f=r->flight/2;f<r->flight;++f) {
                    float q[3];PathAt(*r,static_cast<float>(f),q);
                    if(q[1]<=r->to[1]+kClusterSplitHeight){split=f;break;}
                }
                r->splitAt=split;
                const Round carrier=*r;
                float at[3];PathAt(carrier,static_cast<float>(split),at);
                for(int b=0;b<kBomblets && n<cap;++b) {
                    Round& m=out[n++];
                    m=Round{};
                    float bx,bz;rng.Disc(kBombletSpread,&bx,&bz);
                    onGround(carrier.to[0]+bx,carrier.to[2]+bz,m.to);
                    m.from[0]=at[0];m.from[1]=at[1];m.from[2]=at[2];
                    m.launch=carrier.launch+split;m.path=Path::arc;m.rise=0.0f;
                    const float dx=m.to[0]-at[0],dy=m.to[1]-at[1],dz=m.to[2]-at[2];
                    m.flight=detail::Frames(std::sqrt(dx*dx+dy*dy+dz*dz),kBombletSpeed*rng.Range(0.85f,1.15f));
                    m.size=kBombletSize;m.blast=spec.blast;m.share=kBombletShare;m.burst=b==0;
                }
            }
            break;
        case Kind::creeping: {
            const int flight=detail::Frames(spec.range,spec.speed);
            const int first=flight+30;
            for(int line=0;line<kCreepLines;++line)for(int j=0;j<kCreepPer;++j) {
                const float across=(static_cast<float>(j)-(kCreepPer-1)*0.5f)*kCreepWidth+rng.Range(-4.0f,4.0f);
                const float along=static_cast<float>(line)*kCreepStep+rng.Range(-6.0f,6.0f);
                float p[3];
                onGround(s.target[0]+fwd[0]*along+side[0]*across,s.target[2]+fwd[2]*along+side[2]*across,p);
                Round* r=add(0,p,(static_cast<float>(j%4)-1.5f)*20.0f,15.0f,Path::arc);
                if(!r)break;
                arc(r,0.3f,spec.speed);
                r->launch=first+line*kCreepGap+static_cast<int>(rng.Range(0.0f,8.0f))-r->flight;
                if(r->launch<0)r->launch=0;
            }
            break;
        }
        case Kind::mrsi: {
            // Each a slower, higher arc than the last: the slowest leaves first, all land in one frame.
            int longest=0,flights[kMrsiRounds];
            const float d=detail::FlatDist(site,s.target);
            for(int i=0;i<kMrsiRounds;++i){flights[i]=detail::Frames(d,spec.speed/(1.0f+0.22f*i));if(flights[i]>longest)longest=flights[i];}
            const int impact=longest+30;
            float p[3];onGround(s.target[0],s.target[2],p);
            for(int i=0;i<kMrsiRounds;++i) {
                Round* r=add(0,p,0.0f,8.0f,Path::arc);
                if(!r)break;
                r->rise=(0.12f+0.08f*static_cast<float>(i))*detail::FlatDist(r->from,r->to);
                r->flight=flights[i];
                r->launch=impact-r->flight;
            }
            break;
        }
        case Kind::heavy: {
            float p[3];onGround(s.target[0],s.target[2],p);
            arc(add(0,p,0.0f,0.0f,Path::arc),0.22f,spec.speed);
            break;
        }
        case Kind::cruise: {
            float p[3];onGround(s.target[0],s.target[2],p);
            Round* r=add(0,p,0.0f,0.0f,Path::cruise);
            if(!r)break;
            // Its level: over the highest thing on the way (every 50 m), so it clears the hills and the roofs.
            float top=r->from[1];
            const float d=detail::FlatDist(r->from,r->to);
            for(float t=0.0f;t<=d;t+=50.0f) {
                const float u=d>0.0f ? t/d : 0.0f;
                const float g=ground(Lerp(r->from[0],r->to[0],u),Lerp(r->from[2],r->to[2],u),r->to[1]);
                if(g>top)top=g;
            }
            r->cruiseY=top+kCruiseOver;
            r->popY=(r->cruiseY>r->to[1] ? r->cruiseY : r->to[1])+kCruisePop;
            float pts[5][3];CruisePoints(*r,pts);
            float length=0.0f;CruiseParam(pts,0.0f,&length);
            r->flight=detail::Frames(length,spec.speed);
            break;
        }
        case Kind::ballistic: {
            float p[3];onGround(s.target[0],s.target[2],p);
            Round* r=add(0,p,0.0f,0.0f,Path::lobDive);
            if(!r)break;
            r->rise=kBallisticRise;r->warp=kBallisticWarp;r->flight=kBallisticFlight;
            break;
        }
        case Kind::count: break;
    }
    return n;
}

// The frame the barrage is over: its last round's flight with time to burst (frames from the call).
inline int EndFrame(const Round* r,int n) noexcept {
    int end=0;
    for(int i=0;i<n;++i) {
        const int e=r[i].launch+(r[i].splitAt>=0 ? r[i].splitAt : r[i].flight)+4*kFps;
        if(e>end)end=e;
    }
    return end;
}
}  // namespace barrage
}  // namespace crew
