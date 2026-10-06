// vsynth.h: the ground vehicles' sounds made in the plugin (vehsound.cpp plays them through jetaudio.cpp; docs/sound-re.md
// §9), kept apart from the game and from XAudio2 so that tools/vsound_check.cpp renders and measures the very same
// samples offline. Mono float samples at kRate, their peak at 1 (the player's caller scales them).
//  - Loops are made periodic by construction: their excitation (pulse trains placed modulo the loop, noise tables of the
//    loop's length) is periodic, and every filter is run round the loop twice, the first time only to reach its steady
//    state (Steady): the last sample runs into the first as any other two do, no cross-fade, no swell.
//  - The engine loops are made at their idle (playback rate 1); the mixer raises the rate with the revs. Each engine has
//    two layers at the same pitch, faded across by the load: idle (the firing pulses through the exhaust's resonances, a
//    little knock) and load (the same pulses harder, the exhaust's roar, the turbo's whistle on the heavy one).
//  - One-shots are made whole (Normalized) and start on their first sample.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace crew::vsynth {
constexpr int kRate=48000;
constexpr float kTau=6.2831853f;
using Wave=std::vector<float>;

struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) noexcept : s(seed ? seed : 1u) {}
    float Next() noexcept { s^=s<<13;s^=s>>17;s^=s<<5;return static_cast<float>(s)/2147483648.0f-1.0f; }   // -1..1
    float Uni() noexcept { return 0.5f*(Next()+1.0f); }                                                     // 0..1
};

// A biquad (RBJ cookbook): low-pass, high-pass, band-pass (constant peak gain) at `hz`, quality `q`.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    enum Kind { low, high, band };
    Biquad(Kind k,float hz,float q) noexcept {
        const float w=kTau*hz/static_cast<float>(kRate),c=std::cos(w),al=std::sin(w)/(2.0f*q),a0=1.0f+al;
        if(k==low){b0=(1.0f-c)*0.5f/a0;b1=(1.0f-c)/a0;b2=b0;}
        else if(k==high){b0=(1.0f+c)*0.5f/a0;b1=-(1.0f+c)/a0;b2=b0;}
        else{b0=al/a0;b1=0.0f;b2=-al/a0;}
        a1=-2.0f*c/a0;a2=(1.0f-al)/a0;
    }
    float Run(float x) noexcept { const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=y;return y; }
};
inline Biquad Lp(float hz,float q=0.707f) noexcept { return Biquad(Biquad::low,hz,q); }
inline Biquad Hp(float hz,float q=0.707f) noexcept { return Biquad(Biquad::high,hz,q); }
inline Biquad Bp(float hz,float q) noexcept { return Biquad(Biquad::band,hz,q); }

inline float Peak(const Wave& x) noexcept {
    float p=0.0f;
    for(float v:x)p=std::fabs(v)>p ? std::fabs(v) : p;
    return p;
}
// `x` scaled to the peak `peak`.
inline Wave Normalized(Wave x,float peak) {
    const float p=Peak(x);
    if(p>1e-9f)for(auto& v:x)v*=peak/p;
    return x;
}
inline int Samples(float sec) noexcept { return static_cast<int>(sec*static_cast<float>(kRate)); }

// `in` (periodic: a loop's excitation) through `chain` (a callable float -> float holding its filters' state) round the
// loop twice, the second pass kept: the filters start it in the state the loop's end leaves them in.
template<class Chain> Wave Steady(const Wave& in,Chain chain) {
    for(float x:in)(void)chain(x);
    Wave out(in.size());
    for(std::size_t i=0;i<in.size();++i)out[i]=chain(in[i]);
    return out;
}
// A noise table of `n` samples (a loop's noise: it repeats with the loop).
inline Wave NoiseTable(int n,std::uint32_t seed) {
    Rng r(seed);
    Wave x(static_cast<std::size_t>(n));
    for(auto& v:x)v=r.Next();
    return x;
}
// Adds `shape(t)` (t seconds after the event) for `len` s at sample `at`, wrapped round a loop of x.size().
template<class Shape> void AddWrapped(Wave& x,int at,float len,Shape shape) {
    const int n=static_cast<int>(x.size()),m=Samples(len);
    for(int i=0;i<m;++i) {
        const int k=((at+i)%n+n)%n;
        x[static_cast<std::size_t>(k)]+=shape(static_cast<float>(i)/static_cast<float>(kRate));
    }
}

// --- The engines ---
// What one engine is: the firing rate its idle makes (Hz: rpm / 60 x cylinders / 2), its cylinders (each fires a little
// differently: the engine's lope), its exhaust's two resonances, how much low rumble, knock and turbo whistle.
struct EngineSpec { float firing; int cylinders; float res1,res2,rumble,knock,whistle; std::uint32_t seed; };
// A V12 tank diesel (the E551s, the Titan, the Kepler): idle near 600 rpm, deep and lumpy, a turbo.
constexpr EngineSpec kHeavyEngine{30.0f,12,85.0f,190.0f,1.0f,0.35f,1.0f,0x51u};
// An inline-6 truck diesel (the Grape, the trucks): idle near 840 rpm, lighter and higher, no whistle to speak of.
constexpr EngineSpec kLightEngine{42.0f,6,140.0f,330.0f,0.45f,0.5f,0.15f,0x77u};
// A V-twin motorcycle's (the bikes, the sidecar): idle near 1500 rpm, two uneven beats, barking, no whistle.
constexpr EngineSpec kBikeEngine{25.0f,2,160.0f,420.0f,0.3f,0.6f,0.0f,0xB1u};
constexpr float kEngineLoopSec=2.0f;   // whole firings at all three rates (60, 84 and 50)

// The firing pulses: each cylinder's own strength (fixed per cylinder) and a small jitter of each firing, every pulse a
// short burst of pressure (decaying noise and a thump) wrapped round the loop.
inline Wave Firings(const EngineSpec& e,int n,float hard) {
    Wave x(static_cast<std::size_t>(n),0.0f);
    Rng r(e.seed);
    float cyl[16];
    for(int c=0;c<e.cylinders && c<16;++c)cyl[c]=0.8f+0.4f*r.Uni();
    const float period=static_cast<float>(kRate)/e.firing;
    const int firings=static_cast<int>(std::lround(static_cast<float>(n)/period));
    Rng burst(e.seed*7u+3u);
    for(int k=0;k<firings;++k) {
        const float a=cyl[k%e.cylinders]*(0.9f+0.2f*r.Uni());
        const int at=static_cast<int>(static_cast<float>(k)*period+0.06f*period*r.Next());
        const float decay=0.004f+0.004f*hard;
        AddWrapped(x,at,0.03f,[&](float t){ return a*(burst.Next()*std::exp(-t/decay)+1.6f*std::exp(-t/0.003f)); });
    }
    return x;
}

// One engine layer (see the top): `load` false the idle one, true the load one.
inline Wave EngineLayer(const EngineSpec& e,bool load) {
    const int n=Samples(kEngineLoopSec);
    const Wave pulses=Firings(e,n,load ? 1.0f : 0.0f);
    const Wave hiss=NoiseTable(n,e.seed+11u),hum=NoiseTable(n,e.seed+23u);
    // The firing envelope (the pulses' level, smoothed): the roar and the rumble pulse with it.
    Wave env=Steady(pulses,[lp=Lp(e.firing*0.7f)](float v) mutable { return lp.Run(std::fabs(v)); });
    float envPeak=Peak(env);
    for(auto& v:env)v/=envPeak>1e-9f ? envPeak : 1.0f;
    const Wave body=Steady(pulses,[r1=Bp(e.res1,1.8f),r2=Bp(e.res2,2.5f),lp=Lp(1400.0f)](float v) mutable {
        return 1.6f*r1.Run(v)+0.9f*r2.Run(v)+0.35f*lp.Run(v);
    });
    const Wave knock=Steady(pulses,[bp=Bp(2600.0f,1.4f),hp=Hp(1500.0f)](float v) mutable { return hp.Run(bp.Run(v)); });
    const Wave rumble=Steady(hum,[a=Lp(55.0f),b=Lp(55.0f)](float v) mutable { return b.Run(a.Run(v)); });
    const Wave roar=Steady(hiss,[a=Lp(700.0f),b=Hp(70.0f)](float v) mutable { return b.Run(a.Run(v)); });
    // The turbo's whistle: a narrow band of noise round 2.3 kHz over a faint tone, the tone in whole cycles of the loop.
    const Wave whistleBand=Steady(hiss,[bp=Bp(2300.0f,30.0f)](float v) mutable { return bp.Run(v); });
    Wave x(static_cast<std::size_t>(n));
    for(int i=0;i<n;++i) {
        const std::size_t k=static_cast<std::size_t>(i);
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        const float e1=env[k];
        float v=body[k]+(load ? 0.7f : 0.3f)*e.knock*knock[k]*4.0f+e.rumble*rumble[k]*14.0f*(0.6f+0.4f*e1);
        if(load)v+=0.9f*roar[k]*(0.5f+0.5f*e1)*3.0f+
                   e.whistle*(0.35f*whistleBand[k]*6.0f+0.05f*std::sin(kTau*2300.0f*t));
        x[k]=v;
    }
    // The pressure pulses push one way: the offset they leave taken out (a high-pass round the loop, under the firing).
    x=Steady(x,[hp=Hp(20.0f)](float v) mutable { return hp.Run(v); });
    return Normalized(std::move(x),1.0f);
}

// --- The tracks ---
// A tank's tracks at kTrackSpeed: the links' clack (kLinkRate a second: a metal tick through two bands), every sixth a
// road wheel's thud, the drive sprocket's grind under it all. The mixer plays it at speed / kTrackSpeed.
constexpr float kTrackSpeed=8.0f,kLinkRate=30.0f,kTrackLoopSec=2.0f;
inline Wave Tracks() {
    const int n=Samples(kTrackLoopSec);
    Wave clicks(static_cast<std::size_t>(n),0.0f),thuds(static_cast<std::size_t>(n),0.0f);
    Rng r(0x7AC4u),burst(0x99u);
    const int links=static_cast<int>(kTrackLoopSec*kLinkRate);
    for(int k=0;k<links;++k) {
        const int at=static_cast<int>((static_cast<float>(k)+0.25f*r.Next())*static_cast<float>(n)/static_cast<float>(links));
        const float a=0.6f+0.4f*r.Uni();
        AddWrapped(clicks,at,0.012f,[&](float t){ return a*burst.Next()*std::exp(-t/0.0025f); });
        if(k%6==0)AddWrapped(thuds,at,0.08f,[&](float t){ return (0.7f+0.3f*a)*std::sin(kTau*70.0f*t)*std::exp(-t/0.022f); });
    }
    const Wave grindIn=NoiseTable(n,0x5EEDu);
    const Wave clack=Steady(clicks,[a=Bp(1700.0f,3.0f),b=Bp(3500.0f,4.0f)](float v) mutable { return 2.0f*a.Run(v)+1.2f*b.Run(v); });
    const Wave grind=Steady(grindIn,[a=Lp(380.0f),b=Hp(60.0f)](float v) mutable { return b.Run(a.Run(v)); });
    Wave x(static_cast<std::size_t>(n));
    for(std::size_t i=0;i<x.size();++i)x[i]=clack[i]+thuds[i]+0.9f*grind[i];
    return Normalized(std::move(x),1.0f);
}

// --- The turret ---
// The traverse drive at full slew: an electric motor's hum (kTurretHz and its harmonics, whole cycles of the 1 s loop)
// under the gear train's mesh whine, a hiss of hydraulics wavering. The mixer raises its rate and level with the slew.
constexpr float kTurretHz=175.0f,kTurretLoopSec=1.0f;
inline Wave Turret() {
    const int n=Samples(kTurretLoopSec);
    const Wave hissIn=NoiseTable(n,0x7077u),waverIn=NoiseTable(n,0x3141u);
    const Wave hiss=Steady(hissIn,[a=Bp(2200.0f,2.0f)](float v) mutable { return a.Run(v); });
    const Wave waver=Steady(waverIn,[a=Lp(3.0f),b=Lp(3.0f)](float v) mutable { return b.Run(a.Run(v)); });
    const float wp=Peak(waver);
    Wave x(static_cast<std::size_t>(n));
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        const float motor=std::sin(kTau*kTurretHz*t)+0.5f*std::sin(kTau*2.0f*kTurretHz*t)+0.3f*std::sin(kTau*3.0f*kTurretHz*t)+
                          0.15f*std::sin(kTau*5.0f*kTurretHz*t);
        const float mesh=0.45f*std::sin(kTau*6.0f*kTurretHz*t)*(0.7f+0.3f*std::sin(kTau*kTurretHz*t));
        const std::size_t k=static_cast<std::size_t>(i);
        x[k]=0.55f*motor+mesh+1.4f*hiss[k]*(1.0f+0.35f*waver[k]/(wp>1e-9f ? wp : 1.0f));
    }
    return Normalized(std::move(x),1.0f);
}
// The traverse stopping: the drive's brake catching (a click, a short thud, the hull's ring).
inline Wave TurretStop() {
    Wave x(static_cast<std::size_t>(Samples(0.3f)),0.0f);
    Rng r(0x5701u);
    Biquad click=Bp(1800.0f,1.5f);
    for(std::size_t i=0;i<x.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        x[i]=0.8f*click.Run(r.Next()*std::exp(-t/0.003f))*3.0f+std::sin(kTau*95.0f*t)*std::exp(-t/0.04f)+
             0.3f*std::sin(kTau*620.0f*t)*std::exp(-t/0.06f);
    }
    return Normalized(std::move(x),1.0f);
}

// --- The main gun ---
// Heard near (GunNear): the muzzle's crack (bright noise falling in 3 ms), the blast (noise falling in 45 ms), the boom (a
// sine falling from 93 to 38 Hz in 60 ms, lasting 0.3 s: the punch in the chest), the body (low noise, 0.25 s) and the
// tail (the air and the ground rolling it back: low noise over a second and more, three echoes of the blast), the
// whole driven into a soft limit (tanh) for its density. Heard far (GunFar): no crack, the highs gone, a slower front, a
// longer and lower rumble with more echoes. The mixer fades between the two over the distance and delays them by it.
inline float SoftLimit(float x,float drive) noexcept { return std::tanh(drive*x)/std::tanh(drive); }
inline Wave GunNear() {
    const int n=Samples(3.2f);
    Wave dry(static_cast<std::size_t>(n),0.0f),x(static_cast<std::size_t>(n),0.0f);
    Rng r(0x60Bu);
    Biquad crackHp=Hp(1800.0f),blastLp=Lp(5000.0f),bodyLp=Lp(500.0f),bodyLp2=Lp(500.0f),tailLp=Lp(220.0f),tailLp2=Lp(220.0f),
           modLp=Lp(4.0f);
    float phase=0.0f;
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate),w=r.Next();
        const float crack=crackHp.Run(w)*std::exp(-t/0.003f)*3.0f;
        const float blast=blastLp.Run(w)*std::exp(-t/0.045f)*2.4f;
        phase+=kTau*(38.0f+55.0f*std::exp(-t/0.06f))/static_cast<float>(kRate);
        const float boom=std::sin(phase)*std::exp(-t/0.28f)*std::fmin(1.0f,t/0.002f)*1.6f;
        const float body=bodyLp2.Run(bodyLp.Run(w))*std::exp(-t/0.25f)*3.0f;
        const float mod=0.8f+0.2f*modLp.Run(r.Next())*20.0f;
        const float tail=tailLp2.Run(tailLp.Run(w))*std::exp(-t/1.1f)*std::fmin(1.0f,t/0.03f)*mod*4.0f;
        dry[static_cast<std::size_t>(i)]=blast+boom;
        x[static_cast<std::size_t>(i)]=crack+blast+boom+body+tail;
    }
    // The echoes: the blast and boom back off the ground and what stands on it, duller each time.
    const float delays[3]={0.32f,0.74f,1.3f},gains[3]={0.28f,0.18f,0.1f};
    for(int e=0;e<3;++e) {
        Biquad lp=Lp(900.0f-200.0f*static_cast<float>(e));
        const int d=Samples(delays[e]);
        for(int i=d;i<n;++i)x[static_cast<std::size_t>(i)]+=gains[e]*lp.Run(dry[static_cast<std::size_t>(i-d)]);
    }
    x=Normalized(std::move(x),1.0f);
    for(auto& v:x)v=SoftLimit(v,1.2f);
    return Normalized(std::move(x),1.0f);
}
inline Wave GunFar() {
    const int n=Samples(4.5f);
    Wave dry(static_cast<std::size_t>(n),0.0f),x(static_cast<std::size_t>(n),0.0f);
    Rng r(0xFA4u);
    Biquad blastLp=Lp(500.0f),blastLp2=Lp(500.0f),tailLp=Lp(160.0f),tailLp2=Lp(160.0f),modLp=Lp(2.5f);
    float phase=0.0f;
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate),w=r.Next();
        const float front=std::fmin(1.0f,t/0.02f);
        const float blast=blastLp2.Run(blastLp.Run(w))*std::exp(-t/0.12f)*front*3.0f;
        phase+=kTau*(30.0f+30.0f*std::exp(-t/0.1f))/static_cast<float>(kRate);
        const float boom=std::sin(phase)*std::exp(-t/0.45f)*front*1.2f;
        const float mod=0.75f+0.25f*modLp.Run(r.Next())*25.0f;
        const float tail=tailLp2.Run(tailLp.Run(w))*std::exp(-t/1.8f)*std::fmin(1.0f,t/0.08f)*mod*5.0f;
        dry[static_cast<std::size_t>(i)]=blast+boom;
        x[static_cast<std::size_t>(i)]=blast+boom+tail;
    }
    const float delays[4]={0.4f,0.9f,1.6f,2.4f},gains[4]={0.45f,0.35f,0.25f,0.15f};
    for(int e=0;e<4;++e) {
        Biquad lp=Lp(300.0f);
        const int d=Samples(delays[e]);
        for(int i=d;i<n;++i)x[static_cast<std::size_t>(i)]+=gains[e]*lp.Run(dry[static_cast<std::size_t>(i-d)]);
    }
    return Normalized(std::move(x),1.0f);
}

// --- The reload ---
// A struck metal part: inharmonic partials (Hz) each ringing down at its own rate, struck at `at` s with `gain`.
inline void Strike(Wave& x,float at,float gain,const float* hz,const float* decay,int partials) {
    const int from=Samples(at);
    for(int i=from;i<static_cast<int>(x.size());++i) {
        const float t=static_cast<float>(i-from)/static_cast<float>(kRate);
        float v=0.0f;
        for(int p=0;p<partials;++p)v+=std::sin(kTau*hz[p]*t)*std::exp(-t/decay[p])/static_cast<float>(p+1);
        x[static_cast<std::size_t>(i)]+=gain*v*std::fmin(1.0f,t/0.0005f);
    }
}
// A hard knock at `at` s: a click of noise through a band at `hz`, and a low thud at `thud` Hz.
inline void Knock(Wave& x,float at,float gain,float hz,float thud,std::uint32_t seed) {
    Rng r(seed);
    Biquad bp=Bp(hz,1.5f);
    const int from=Samples(at);
    for(int i=from;i<static_cast<int>(x.size());++i) {
        const float t=static_cast<float>(i-from)/static_cast<float>(kRate);
        x[static_cast<std::size_t>(i)]+=gain*(3.0f*bp.Run(r.Next()*std::exp(-t/0.002f))+std::sin(kTau*thud*t)*std::exp(-t/0.03f));
    }
}
// The spent case thrown out: the breech opening (a knock), the brass case's ring as it slides out, its fall on the
// floor and a bounce, a short roll.
inline Wave ReloadEject() {
    Wave x(static_cast<std::size_t>(Samples(1.1f)),0.0f);
    const float brass[4]={740.0f,1980.0f,3560.0f,5200.0f},ring[4]={0.25f,0.18f,0.12f,0.08f},bounce[4]={760.0f,2030.0f,3610.0f,5300.0f};
    Knock(x,0.0f,0.8f,2500.0f,140.0f,0xE1u);
    Strike(x,0.08f,0.25f,brass,ring,4);
    Strike(x,0.24f,0.9f,brass,ring,4);
    Strike(x,0.46f,0.45f,bounce,ring,4);
    Rng r(0x2011u);
    for(int k=0;k<6;++k)Strike(x,0.58f+0.045f*static_cast<float>(k)+0.01f*r.Next(),0.12f*(1.0f-0.12f*static_cast<float>(k)),bounce,ring,2);
    return Normalized(std::move(x),1.0f);
}
// The round rammed home: the shell sliding in (a rising scrape), the ram's clank (a thud, a ring of steel).
inline Wave ReloadLoad() {
    Wave x(static_cast<std::size_t>(Samples(0.75f)),0.0f);
    Rng r(0x10ADu);
    Biquad lp=Lp(1200.0f),hp=Hp(250.0f);
    for(int i=0;i<Samples(0.28f);++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        x[static_cast<std::size_t>(i)]+=0.35f*hp.Run(lp.Run(r.Next()))*(t/0.28f)*2.0f;
    }
    const float steel[3]={430.0f,1150.0f,2380.0f},ring[3]={0.12f,0.08f,0.05f};
    Knock(x,0.28f,0.9f,1900.0f,110.0f,0x7A5u);
    Strike(x,0.28f,0.5f,steel,ring,3);
    return Normalized(std::move(x),1.0f);
}
// The breech block closing: a sharp clack, a heavy thunk, the gun's ring, the latch.
inline Wave ReloadClose() {
    Wave x(static_cast<std::size_t>(Samples(0.55f)),0.0f);
    const float steel[3]={300.0f,880.0f,1720.0f},ring[3]={0.15f,0.09f,0.05f};
    Knock(x,0.0f,1.0f,3000.0f,80.0f,0xC105u);
    Strike(x,0.0f,0.45f,steel,ring,3);
    Knock(x,0.09f,0.3f,4200.0f,200.0f,0x1A7u);
    return Normalized(std::move(x),1.0f);
}

// --- The small guns ---
// One shot of a machine gun or an autocannon at sample `at` (wrapped round a loop of x.size(): a burst's loop; or
// not, for a one-shot made long enough): the muzzle's crack (bright noise in `crack` s), the blast (noise under 4 kHz
// falling in `blast` s), the thump (a sine falling from `hz` x 2 to `hz` in 20 ms, lasting `thump` s) and the action's
// clack (the bolt or the feed, a band of noise round 3 kHz `action` s later). `seed` makes each shot its own.
struct ShotSpec { float crack,blast,thump,hz,action,gain; };
inline void AddShot(Wave& x,int at,const ShotSpec& sp,std::uint32_t seed) {
    Rng r(seed);
    Biquad hp=Hp(1500.0f),lp=Lp(4000.0f),click=Bp(3000.0f,2.0f);
    float phase=0.0f;
    const float len=sp.thump*5.0f>0.25f ? sp.thump*5.0f : 0.25f;
    AddWrapped(x,at,len,[&](float t) {
        const float w=r.Next();
        phase+=kTau*sp.hz*(1.0f+std::exp(-t/0.02f))/static_cast<float>(kRate);
        const float action=t>=sp.action ? click.Run(w*std::exp(-(t-sp.action)/0.004f))*2.5f : click.Run(0.0f);
        return sp.gain*(hp.Run(w)*std::exp(-t/sp.crack)*2.0f+lp.Run(w)*std::exp(-t/sp.blast)*1.6f+
                        std::sin(phase)*std::exp(-t/sp.thump)*std::fmin(1.0f,t/0.001f)*1.4f+0.35f*action);
    });
}
constexpr ShotSpec kMgShot{0.0015f,0.014f,0.03f,130.0f,0.02f,1.0f};
constexpr ShotSpec kGatlingShot{0.001f,0.009f,0.018f,150.0f,0.015f,0.9f};
// The bursts as loops (each at its rate: the mixer plays it at made / actual interval): a machine gun's kMgRate rounds
// a second, a gatling's kGatlingRate over its barrels' whir; each round a little different, the gun's own rattle under
// them. The loops' length holds whole rounds.
constexpr float kMgRate=12.0f,kGatlingRate=20.0f,kBurstLoopSec=1.0f;
inline Wave Burst(float rate,const ShotSpec& sp,float whir,std::uint32_t seed) {
    const int n=Samples(kBurstLoopSec);
    Wave x(static_cast<std::size_t>(n),0.0f);
    Rng r(seed);
    const int rounds=static_cast<int>(std::lround(kBurstLoopSec*rate));
    for(int k=0;k<rounds;++k) {
        ShotSpec one=sp;one.gain*=0.85f+0.3f*r.Uni();
        AddShot(x,static_cast<int>((static_cast<float>(k)+0.04f*r.Next())*static_cast<float>(n)/static_cast<float>(rounds)),one,seed*31u+static_cast<std::uint32_t>(k));
    }
    const Wave rattleIn=NoiseTable(n,seed+5u);
    const Wave rattle=Steady(rattleIn,[a=Bp(900.0f,1.2f),b=Bp(2400.0f,3.0f)](float v) mutable { return a.Run(v)+0.6f*b.Run(v); });
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        x[static_cast<std::size_t>(i)]+=0.25f*rattle[static_cast<std::size_t>(i)]+
            whir*(0.4f*std::sin(kTau*3.0f*rate*t)+0.25f*std::sin(kTau*6.0f*rate*t));   // the barrels' whir: whole cycles
    }
    return Normalized(std::move(x),1.0f);
}
inline Wave MachineGun() { return Burst(kMgRate,kMgShot,0.0f,0x3A6u); }
inline Wave Gatling() { return Burst(kGatlingRate,kGatlingShot,0.25f,0x6A7u); }
// A burst ending: the last rounds' echo rolling off the ground (low noise falling over a second, three echoes).
inline Wave BurstTail() {
    Wave x(static_cast<std::size_t>(Samples(1.4f)),0.0f);
    Rng r(0x7A11u);
    Biquad lp=Lp(600.0f),lp2=Lp(600.0f);
    for(std::size_t i=0;i<x.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        float e=std::exp(-t/0.12f);
        for(const float d:{0.25f,0.55f,0.9f})e+=t>d ? 0.35f*std::exp(-(t-d)/0.15f)*(1.0f-d) : 0.0f;
        x[i]=lp2.Run(lp.Run(r.Next()))*e*std::fmin(1.0f,t/0.005f);
    }
    return Normalized(std::move(x),1.0f);
}
// An autocannon's one round (the Striker's, the Titan's side guns, the robot truck's rifle): a heavier shot, a short
// tail and an echo.
inline Wave Autocannon() {
    Wave x(static_cast<std::size_t>(Samples(1.3f)),0.0f);
    AddShot(x,0,ShotSpec{0.002f,0.03f,0.07f,70.0f,0.05f,1.0f},0xAC1u);
    Rng r(0xAC2u);
    Biquad lp=Lp(450.0f),lp2=Lp(450.0f),echo=Lp(700.0f);
    const Wave dry=x;
    const int d=Samples(0.38f);
    for(std::size_t i=0;i<x.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        x[i]+=lp2.Run(lp.Run(r.Next()))*std::exp(-t/0.35f)*std::fmin(1.0f,t/0.01f)*2.0f;
        if(static_cast<int>(i)>=d)x[i]+=0.25f*echo.Run(dry[i-static_cast<std::size_t>(d)]);
    }
    return Normalized(std::move(x),1.0f);
}
// Spent cases and links raining from a firing gun: small brass rings at random about kBrassRate a second (wrapped round
// the 1 s loop).
constexpr float kBrassRate=11.0f;
inline Wave Brass() {
    const int n=Samples(1.0f);
    Wave x(static_cast<std::size_t>(n),0.0f);
    Rng r(0xB4A5u);
    const int falls=static_cast<int>(kBrassRate);
    for(int k=0;k<falls;++k) {
        const float base=2100.0f*(0.85f+0.3f*r.Uni()),g=0.5f+0.5f*r.Uni();
        AddWrapped(x,static_cast<int>(r.Uni()*static_cast<float>(n)),0.15f,[=](float t) {
            return g*(std::sin(kTau*base*t)*std::exp(-t/0.06f)+0.6f*std::sin(kTau*base*2.27f*t)*std::exp(-t/0.035f)+
                      0.4f*std::sin(kTau*base*3.51f*t)*std::exp(-t/0.02f))*std::fmin(1.0f,t/0.0003f);
        });
    }
    return Normalized(std::move(x),1.0f);
}
// One autocannon case landing: a clink and its bounce.
inline Wave CaseSmall() {
    Wave x(static_cast<std::size_t>(Samples(0.5f)),0.0f);
    const float brass[3]={1300.0f,3400.0f,6100.0f},ring[3]={0.12f,0.07f,0.04f},bounce[3]={1330.0f,3460.0f,6200.0f};
    Strike(x,0.0f,1.0f,brass,ring,3);
    Strike(x,0.17f,0.45f,bounce,ring,3);
    Strike(x,0.29f,0.2f,bounce,ring,2);
    return Normalized(std::move(x),1.0f);
}

// --- Missiles and rockets ---
// A launch: the motor's ignition (a pop), its roar coming up in 30 ms and falling away (a band of noise sliding from
// 1.6 kHz down to 450 Hz as it leaves), the launcher's back-blast under it.
inline Wave MissileLaunch() {
    Wave x(static_cast<std::size_t>(Samples(1.8f)),0.0f);
    Rng r(0x3155u);
    Biquad band=Bp(1600.0f,1.6f),blast=Lp(300.0f);
    for(std::size_t i=0;i<x.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate),w=r.Next();
        if(i%64==0) {
            const float hz=450.0f+1150.0f*std::exp(-t/0.45f);   // the band's centre now (new coefficients every 64 samples)
            Biquad b=Bp(hz,1.6f);
            band.b0=b.b0;band.b1=b.b1;band.b2=b.b2;band.a1=b.a1;band.a2=b.a2;   // its state kept: no click
        }
        const float roar=band.Run(w)*std::fmin(1.0f,t/0.03f)*std::exp(-t/0.55f)*3.0f;
        const float ignite=w*std::exp(-t/0.004f)*1.5f;
        const float back=blast.Run(w)*std::exp(-t/0.18f)*4.0f;
        x[i]=roar+ignite+back;
    }
    return Normalized(std::move(x),1.0f);
}
}  // namespace crew::vsynth
