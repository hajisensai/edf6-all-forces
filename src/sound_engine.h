#pragma once
// The engines as War Thunder layers them (docs/feature-wt-sound.md 「引擎」; the user, 2026-11-26: 「现有的喷气机、车辆声
// 该优化就优化，做的并不是很好」「分层和距离混音，环绕声道之类的记得做好」): an engine is an event of groups, a group a
// set of looping layers (wt_clips.h, one recording each) cross-faded over a parameter (the rpm, the load, the
// afterburner, the speed) with their pitch following it (FMOD's autopitch) and the group's gain over the distance, the
// cone (where the listener is around the source: in front of it 0 degrees, behind it 180) and the parameters; plus the
// one-shots a parameter sets off crossing a point (a load switch, the afterburner lighting, the tracks stopping).
//
// The numbers are the events' in GaijinEntertainment/fmod_studio_warthunder_for_modders (Metadata: Master/aircraft/
// engines/engine05_ej200_WEP, _WEP_ai, engine_jet_far_ai; Master/ground/engines/abrams, leo2a5, m8, tracks_heavy,
// tracks_wheels, tracks_ai_heavy, tracks_ai_wheels and ai/abrams, ai/leo2a5, ai/m8: their sheets' regions and fades, the
// instruments' volume, pitch and autopitch, the groups' GainEffects and pitch automation over Distance, EventConeAngle
// and the parameters, the spatialiser's min / max distance and Doppler scale, the masters); the recordings are War
// Thunder's own banks' (tools/wt_sound.py ENGINES). What is not in them is marked "ours" here and in the doc: how the
// game's state becomes the parameters (the rpm with a gearbox, the spool), which event plays for whom, the levels
// between the families, the speed-of-sound delay.
//
// Who hears what (WT's split): the listener's own vehicle (the one the local player sits in) plays the player's event
// (no rolloff of its own: its Distance curves are the mix), every other one (NPCs, the other players online: the same
// on every machine, from what this machine sees of it) the AI's (FMOD's inverse rolloff between its min and max distance
// times its Distance curves). Pure: tools/sound_engine_check.cpp checks it offline; jetsound.cpp and vehsound.cpp play it.
#include "wt_clips.h"
#include <cmath>
#include <cstdint>

namespace crew::engine {
constexpr int kMostPoints=13,kMostLayers=6,kMostGroups=6,kMostShots=3,kMostEventLayers=kMostLayers*kMostGroups;
constexpr float kQuietDb=-60.0f;   // a layer under this is not given a voice (FMOD's curves end at -80: silence)
constexpr float kOpenGain=0.001f,kCloseGain=0.0005f;   // ...its voice opened over -60 dB, closed under -66 (no chatter)

// Ours: the engines' level against the plugin's other sounds (WT's buses are its own mix). Measured on the recordings
// (their RMS through Hear, the riding camera 14 m off, behind): a tank's own engine at full load 14 dB under a 75 mm
// shot's first 0.3 s at the same distance (sound_event.h kMedium), its idle 22 dB under; a jet's own at full power
// 16 dB under from 15 m behind. Each family's master and bus below are WT's.
constexpr float kTrimDb=-2.0f;

struct Point { float at,value; };
// A value over a parameter: straight between its points (FMOD's linear automation: dB for a volume, semitones for a
// pitch), held past its ends; none: 0.
struct Curve { int n; Point p[kMostPoints]; };
inline float At(const Curve& c,float x) noexcept {
    if(c.n<=0)return 0.0f;
    if(!(x>c.p[0].at))return c.p[0].value;
    for(int i=1;i<c.n;++i)
        if(x<=c.p[i].at) {
            const float span=c.p[i].at-c.p[i-1].at;
            const float t=span>0.0f ? (x-c.p[i-1].at)/span : 1.0f;
            return c.p[i-1].value+t*(c.p[i].value-c.p[i-1].value);
        }
    return c.p[c.n-1].value;
}
inline float Amp(float db) noexcept { return db<=-80.0f ? 0.0f : std::pow(10.0f,db/20.0f); }

// An instrument's place on its sheet: heard from `lo` to `hi`, fading in up to `in` and out from `out` (amplitude
// straight: FMOD's region fades), none past its ends.
struct Region { float lo,in,out,hi; };
inline float Fade(const Region& r,float x) noexcept {
    if(!(x>=r.lo) || !(x<=r.hi))return 0.0f;
    if(x<r.in)return r.in>r.lo ? (x-r.lo)/(r.in-r.lo) : 1.0f;
    if(x>r.out)return r.hi>r.out ? (r.hi-x)/(r.hi-r.out) : 1.0f;
    return 1.0f;
}
// FMOD's autopitch: the playback rate 1 at `root`, `atMin` at the sheet's minimum, straight between and on past root
// (root 0: none).
struct AutoPitch { float root,atMin; };
inline float Pitch(const AutoPitch& a,float x,float min) noexcept {
    if(!(a.root>min))return 1.0f;
    const float r=a.atMin+(1.0f-a.atMin)*(x-min)/(a.root-min);
    return r>0.05f ? r : 0.05f;
}
inline float Semitones(float st) noexcept { return std::pow(2.0f,st/12.0f); }

// The parameters an engine's sheets and automation read.
enum Param : int { kRpm, kLoad, kWep, kSpeed, kTurn, kSpeedShare, kParamCount };
struct Inputs { float v[kParamCount]; };
// The sheets' ranges: the jets' rpm 800..2750 (the AI's to 2400), the ground's rpm_result 0..100, load / wep 0..1,
// speed km/h 0..100, speed_relative 0..100 (% of its top speed), turn rad/s. A layer's sheetMin is its sheet's minimum
// (its autopitch's).

struct Layer { int clip; Param on; Region r; float db,st; AutoPitch ap; float sheetMin; };
struct Group {
    int n; Layer layer[kMostLayers];
    float db;                         // its volume (the instruments' track, a 'broken' parameter at 0) and the nested event's
    Curve distance;                   // dB over the distance (m)
    Curve cone,cone2;                 // dB over the cone angle (degrees): a group's GainEffects stack
    Curve gain[2]; Param gainOn[2];   // dB over its parameters (n 0: none)
    Curve conePitch;                  // semitones over the cone angle
    Curve pitch; Param pitchOn;       // semitones over a parameter
};
// A one-shot set off when its parameter crosses `at`: up (+1), down (-1) or either way (0).
struct Shot { int clip; Param on; float at; int way; float db; Curve gain; Param gainOn; };
struct Event {
    int groups; Group g[kMostGroups];
    int shots; Shot s[kMostShots];
    float db;                 // the event's master and its bus (WT's), with kTrimDb
    float minD,maxD;          // the AI's inverse rolloff (m); 0: none (the player's: its Distance curves alone)
    float doppler;            // its Doppler scale (1: the whole shift)
    Curve dopplerCone;        // ...or over the cone angle (% : the jets' AI; n 0: `doppler`)
    Curve conePitch;          // the master's pitch over the cone (semitones)
};

// What a layer is heard as: its gain (linear: the event's, the group's and the region's fade) and its playback rate
// (its autopitch, its own pitch and the groups' and the master's pitch automation; the Doppler ratio is the caller's).
struct Heard { int clip; float gain,ratio; };
// Every layer of `e` in order (the flat index the caller keeps a voice for: Index), at the distance `d` (m) and the cone
// angle `cone` (degrees).
inline int LayerCount(const Event& e) noexcept {
    int n=0;
    for(int i=0;i<e.groups;++i)n+=e.g[i].n;
    return n;
}
inline float Rolloff(const Event& e,float d) noexcept {
    if(!(e.minD>0.0f))return 1.0f;
    const float x=d<e.minD ? e.minD : d>e.maxD ? e.maxD : d;
    return e.minD/x;
}
inline int Hear(const Event& e,const Inputs& in,float d,float cone,Heard* out) noexcept {
    const float roll=Rolloff(e,d),master=Semitones(At(e.conePitch,cone));
    int k=0;
    for(int gi=0;gi<e.groups;++gi) {
        const Group& g=e.g[gi];
        float db=e.db+g.db+At(g.distance,d)+At(g.cone,cone)+At(g.cone2,cone);
        for(int j=0;j<2;++j)if(g.gain[j].n)db+=At(g.gain[j],in.v[g.gainOn[j]]);
        const float st=At(g.conePitch,cone)+(g.pitch.n ? At(g.pitch,in.v[g.pitchOn]) : 0.0f);
        for(int li=0;li<g.n;++li,++k) {
            const Layer& l=g.layer[li];
            const float x=in.v[l.on];
            const float gain=roll*Amp(db+l.db)*Fade(l.r,x);
            out[k]=Heard{l.clip,std::isfinite(gain) ? gain : 0.0f,master*Semitones(st+l.st)*Pitch(l.ap,x,l.sheetMin)};
        }
    }
    return k;
}
// The Doppler ratio at the event's scale (`cone`: the jets' AI scale theirs by it).
inline float Doppler(const Event& e,float ratio,float cone) noexcept {
    const float scale=e.dopplerCone.n ? At(e.dopplerCone,cone)/100.0f : e.doppler;
    return std::isfinite(ratio) && ratio>0.0f ? 1.0f+scale*(ratio-1.0f) : 1.0f;
}
// The one-shots `from` -> `to` sets off (bit i: e.s[i]), and each one's gain.
inline unsigned Crossed(const Event& e,const Inputs& from,const Inputs& to) noexcept {
    unsigned bits=0;
    for(int i=0;i<e.shots;++i) {
        const Shot& s=e.s[i];
        const float a=from.v[s.on],b=to.v[s.on];
        const bool up=a<s.at && b>=s.at,down=a>=s.at && b<s.at;
        if((s.way>=0 && up) || (s.way<=0 && down))bits|=1u<<i;
    }
    return bits;
}
inline float ShotGain(const Event& e,int i,const Inputs& in,float d) noexcept {
    const Shot& s=e.s[i];
    return Rolloff(e,d)*Amp(e.db+s.db+(s.gain.n ? At(s.gain,in.v[s.gainOn]) : 0.0f));
}

// --- The engines' parameters from the game (ours) -------------------------------------------------------------------
// The cone angle: where the listener is around the source, 0 in front of its nose, 180 behind it. `nose` its forward
// (unit), `toListener` the unit line from it to the listener.
inline float ConeAngle(const float* nose,const float* toListener) noexcept {
    const float c=nose[0]*toListener[0]+nose[1]*toListener[1]+nose[2]*toListener[2];
    const float k=c<-1.0f ? -1.0f : c>1.0f ? 1.0f : c;
    return std::isfinite(k) ? std::acos(k)*57.29578f : 90.0f;
}
inline float Toward(float x,float to,float up,float down,float dt) noexcept {
    if(!std::isfinite(x))x=to;
    const float d=to-x,most=(d>0.0f ? up : down)*dt;
    return d>most ? x+most : d<-most ? x-most : to;
}

// A ground vehicle's drive: its engine (the event family) and running gear, its gearbox and its top speed.
enum class Kind : std::uint8_t { turbine, diesel, petrol };
enum class Gear : std::uint8_t { tracks, wheels };
struct Drive { Kind kind; Gear gear; int gears; float top,pitch; };
// The plugin's engined classes (vehsound.cpp: CarBase's engine sound in the vtable) and theirs (ours: by what the
// vehicles are: the E651 Titan, the Gigantus and the Balam-class tanks a gas turbine as an M1's, the Proteus-era E551 and
// the Maser and the Naegling a diesel, the Grape and the trucks a petrol six, the bikes a petrol twin pitched up).
struct ClassDrive { const char* name; Drive d; };
constexpr ClassDrive kDrives[]={
    {"505_Tank",{Kind::turbine,Gear::tracks,0,70.0f,1.0f}},{"601_Tank",{Kind::turbine,Gear::tracks,0,60.0f,1.0f}},
    {"603_Flak",{Kind::turbine,Gear::tracks,0,65.0f,1.0f}},{"403_Tank",{Kind::diesel,Gear::tracks,5,65.0f,1.0f}},
    {"404_Tank",{Kind::diesel,Gear::tracks,5,60.0f,1.0f}},{"510_Maser",{Kind::diesel,Gear::tracks,4,45.0f,0.94f}},
    {"402_Rocket",{Kind::diesel,Gear::wheels,5,80.0f,1.0f}},{"Car",{Kind::petrol,Gear::wheels,5,110.0f,1.0f}},
    {"503_Bike",{Kind::petrol,Gear::wheels,5,140.0f,1.25f}},{"511_Bike",{Kind::petrol,Gear::wheels,5,120.0f,1.25f}},
};

// The rpm (0..100) of a geared engine, and its load (0..1): in a gear the rpm is the speed's (kShiftUp at the gear's top
// speed, the gears' tops kStep apart: up at kShiftUp, down under kShiftDown); pulling away the clutch slips: no lower
// than kSlip + kSlipThrottle * throttle in the first; standing, the idle; a shift a moment's lift of the load. A turbine
// has no gears: its rpm follows the throttle (and the speed it holds) with its spool's lag.
constexpr float kIdle=5.0f,kShiftUp=88.0f,kShiftDown=40.0f,kStep=0.7f,kSlip=20.0f,kSlipThrottle=60.0f;
constexpr float kRevUp=140.0f,kRevDown=90.0f;       // rpm / s
constexpr float kSpoolUp=35.0f,kSpoolDown=25.0f;    // a turbine's rpm / s
constexpr float kShiftSec=0.25f;
struct Gearbox { float rpm=0.0f,load=0.0f,shift=0.0f; int gear=0; };
inline float GearTop(const Drive& d,int g) noexcept { return d.top*std::pow(kStep,static_cast<float>(d.gears-1-g)); }
// `speed` km/h, `throttle` 0..1 (the stock engine's drive +0x1A80), `on` 0..1 (the engine winding up / down).
inline void Step(Gearbox& b,const Drive& d,float speed,float throttle,float on,float dt) noexcept {
    const float v=std::isfinite(speed) && speed>0.0f ? speed : 0.0f;
    const float t=std::isfinite(throttle) ? (throttle<0.0f ? 0.0f : throttle>1.0f ? 1.0f : throttle) : 0.0f;
    float want;
    if(d.kind==Kind::turbine || d.gears<=0) {
        const float held=v/d.top;
        want=kIdle+(100.0f-kIdle)*(t>held*0.6f ? t : held*0.6f);
        b.rpm=Toward(b.rpm,want*on,kSpoolUp,kSpoolDown,dt);
        b.load=Toward(b.load,t,2.0f,2.0f,dt);
        b.gear=0;
        return;
    }
    if(b.gear<0 || b.gear>=d.gears)b.gear=0;
    auto inGear=[&](int g){ return kShiftUp*v/GearTop(d,g); };
    if(b.gear+1<d.gears && inGear(b.gear)>kShiftUp){++b.gear;b.shift=kShiftSec;}
    else if(b.gear>0 && inGear(b.gear)<kShiftDown){--b.gear;b.shift=kShiftSec;}
    want=inGear(b.gear);
    if(b.gear==0 && want<kSlip+kSlipThrottle*t)want=kSlip+kSlipThrottle*t;
    if(t<0.02f && v<1.0f)want=kIdle;
    if(want<kIdle)want=kIdle;
    if(want>100.0f)want=100.0f;
    b.rpm=Toward(b.rpm,want*on,kRevUp,kRevDown,dt);
    b.shift=b.shift>dt ? b.shift-dt : 0.0f;
    b.load=Toward(b.load,b.shift>0.0f ? 0.0f : t,6.0f,8.0f,dt);
}

// A jet's engine (ours): its rpm (800 idle .. 2600 full military power .. 2750 with the afterburner) spooling up and
// down, the afterburner's share (wep) lighting and going out, the load its throttle.
constexpr float kJetIdle=800.0f,kJetMil=2600.0f,kJetWep=2750.0f,kJetAiTop=2400.0f;
constexpr float kJetSpoolUp=500.0f,kJetSpoolDown=650.0f;   // rpm / s: idle to full in about 4 s
struct Spool { float rpm=kJetIdle,wep=0.0f,load=0.0f; };
inline void Step(Spool& s,float power,bool burner,float dt) noexcept {
    const float p=std::isfinite(power) ? (power<0.0f ? 0.0f : power>1.0f ? 1.0f : power) : 0.0f;
    s.rpm=Toward(s.rpm,burner ? kJetWep : kJetIdle+(kJetMil-kJetIdle)*p,kJetSpoolUp,kJetSpoolDown,dt);
    s.wep=Toward(s.wep,burner ? 1.0f : 0.0f,2.0f,1.5f,dt);
    s.load=Toward(s.load,p,1.5f,1.5f,dt);
}
// The AI's sheets end at 2400: the player's range mapped onto theirs.
inline float AiRpm(float rpm) noexcept { return kJetIdle+(rpm-kJetIdle)*(kJetAiTop-kJetIdle)/(kJetWep-kJetIdle); }

// --- Heard late: the speed of sound (ours) --------------------------------------------------------------------------
// A source's last kTrail frames (its place, velocity, nose and parameters): what the listener hears now left it when
// the sound's travel time from there equals its age (the retarded time: |pos(t') - L| = c (t - t')). Past the oldest:
// the oldest (a source just seen far off).
constexpr int kTrail=640;   // 10 s at 60 fps: a jet 3 km off
constexpr float kSoundSpeed=340.0f;
struct Sample { float pos[3],vel[3],nose[3]; Inputs in; std::uint64_t ms; };
struct Trail {
    Sample s[kTrail];
    int head=0,count=0;
};
inline void Push(Trail& t,const Sample& x) noexcept {
    t.head=(t.head+1)%kTrail;
    t.s[t.head]=x;
    if(t.count<kTrail)++t.count;
}
inline const Sample* Retarded(const Trail& t,const float* listener,std::uint64_t now) noexcept {
    if(!t.count)return nullptr;
    const Sample* last=nullptr;
    for(int i=0;i<t.count;++i) {
        const Sample& x=t.s[(t.head-i+kTrail)%kTrail];
        const float dx=x.pos[0]-listener[0],dy=x.pos[1]-listener[1],dz=x.pos[2]-listener[2];
        const float travel=std::sqrt(dx*dx+dy*dy+dz*dz)/kSoundSpeed;
        const float age=static_cast<float>(now-x.ms)/1000.0f;
        if(age>=travel)return &x;
        last=&x;
    }
    return last;
}

// --- The events -----------------------------------------------------------------------------------------------------
using namespace wt;
constexpr Curve kNone{0,{}};
// The Typhoon's EJ200 the player flies (engine05_ej200_WEP; its bus -20): rear, front, afterburner and turbine groups.
constexpr Curve kJetDist{5,{{0.0f,0.0f},{62.8f,-2.15f},{125.6f,-5.0f},{562.8f,-11.0f},{1000.0f,-80.0f}}};
constexpr Curve kJetLoadPitch{3,{{0.0f,-0.354f},{0.5f,0.207f},{1.0f,0.768f}}};
constexpr Region kJetIdleR{800.0f,800.0f,1100.0f,1500.0f},kJetMaxR{1100.0f,1500.0f,2750.0f,2750.0f};
constexpr float kJ=kJetIdle;
constexpr Event kJetPlayer{4,{
    {2,{{k_jet_idle_rear,kRpm,kJetIdleR,6.0f,0.0f,{1000.0f,0.905f},kJ},
        {k_jet_max_rear,kRpm,kJetMaxR,6.0f,6.0f,{2600.0f,0.705f},kJ}},
     0.0f,kJetDist,{2,{{0.0f,-6.0f},{180.0f,6.0f}}},kNone,{{2,{{0.0f,0.0f},{1.0f,-6.0f}}},{2,{{808.0f,3.0f},{1100.0f,6.0f}}}},
     {kWep,kRpm},kNone,kNone,kRpm},
    // The front group's Distance is not automated in the event: the rear's (ours).
    {2,{{k_jet_max_front,kRpm,kJetMaxR,10.0f,0.0f,{2600.0f,0.705f},kJ},
        {k_jet_idle_front,kRpm,kJetIdleR,10.0f,0.0f,{1000.0f,0.905f},kJ}},
     0.0f,kJetDist,{2,{{0.0f,3.0f},{180.0f,-6.0f}}},kNone,{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    {1,{{k_jet_ab,kWep,{0.05f,0.55f,1.0f,1.0f},8.0f,0.0f,{0.0f,1.0f},0.0f}},
     0.0f,{4,{{0.0f,0.0f},{125.6f,-5.0f},{562.8f,-16.0f},{1000.0f,-80.0f}}},{2,{{0.0f,-8.0f},{180.0f,0.0f}}},
     {3,{{100.0f,0.0f},{177.0f,7.0f},{180.0f,7.0f}}},{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    {5,{{k_jet_turb_02,kRpm,{800.0f,800.0f,927.5f,1287.5f},6.0f,0.0f,{839.0f,0.905f},kJ},
        {k_jet_turb_04,kRpm,{848.75f,1208.75f,1353.1f,1653.1f},3.0f,0.0f,{1580.0f,0.505f},kJ},
        {k_jet_turb_06,kRpm,{1287.5f,1587.5f,1653.1f,2018.75f},0.0f,0.0f,{1970.0f,0.505f},kJ},
        {k_jet_turb_08,kRpm,{1653.1f,2018.75f,2148.1f,2628.1f},0.0f,0.0f,{2018.75f,0.5f},kJ},
        {k_jet_turb_085,kRpm,{2018.75f,2518.75f,2750.0f,2750.0f},0.0f,0.0f,{2433.1f,0.5f},kJ}},
     0.0f,kJetDist,{2,{{0.0f,-6.0f},{180.0f,0.0f}}},kNone,{{2,{{0.0f,-1.92f},{1.0f,6.0f}}},kNone},{kLoad,kRpm},
     {3,{{0.0f,0.0f},{90.0f,0.25f},{180.0f,0.5f}}},kJetLoadPitch,kLoad},
},1,{{k_jet_ab_start,kWep,0.01f,+1,4.0f,kNone,kRpm}},
 -20.0f+kTrimDb,0.0f,0.0f,0.54f,kNone,{2,{{0.0f,0.0f},{180.0f,-1.0f}}}};

// Every other jet (engine05_ej200_WEP_ai, min 100 m, max 3000 m; its far layers the nested engine_jet_far_ai's, +3 and
// its master +4). The afterburner by the speed against its top (speed_relative): the AI has no afterburner of its own.
constexpr Region kAiMaxR{900.0f,1380.0f,2400.0f,2400.0f},kAiFarR{833.7f,1250.9f,2400.0f,2400.0f};
constexpr Event kJetAi{5,{
    {2,{{k_jet_max_rear,kRpm,kAiMaxR,8.0f,0.0f,{2276.9f,0.705f},kJ},
        {k_jet_idle_front,kRpm,{800.0f,800.0f,900.0f,1200.0f},0.0f,0.0f,{964.1f,0.905f},kJ}},
     0.0f,{3,{{0.0f,10.0f},{487.0f,-3.87f},{1611.0f,-80.0f}}},kNone,kNone,{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    // Its front group's region is not in the sheet's metadata: the rear's (ours).
    {1,{{k_jet_max_front,kRpm,kAiMaxR,0.0f,0.0f,{2276.9f,0.705f},kJ}},
     0.0f,{2,{{0.0f,0.0f},{1300.0f,-80.0f}}},kNone,kNone,{kNone,kNone},{kRpm,kRpm},{2,{{0.0f,6.0f},{180.0f,-6.0f}}},kNone,kRpm},
    {1,{{k_jet_ab,kRpm,{900.0f,1700.0f,2400.0f,2400.0f},6.0f,0.0f,{0.0f,1.0f},kJ}},
     0.0f,{3,{{0.0f,0.0f},{61.5f,0.0f},{1200.0f,-80.0f}}},{2,{{0.0f,-80.0f},{90.0f,0.0f}}},kNone,
     {{2,{{0.0f,-10.0f},{97.0f,0.0f}}},kNone},{kSpeedShare,kRpm},kNone,kNone,kRpm},
    {2,{{k_jet_far_low,kRpm,{800.0f,800.0f,833.7f,1250.9f},-6.0f,0.0f,{880.0f,0.9f},kJ},
        {k_jet_far,kRpm,kAiFarR,0.0f,0.0f,{2160.0f,0.505f},kJ}},
     7.0f,kNone,{3,{{0.0f,2.0f},{90.0f,-4.0f},{180.0f,2.0f}}},{2,{{0.0f,-6.0f},{180.0f,0.0f}}},{kNone,kNone},{kRpm,kRpm},
     kNone,kJetLoadPitch,kLoad},
    {1,{{k_jet_approach,kRpm,kAiFarR,0.0f,0.0f,{2160.0f,0.505f},kJ}},
     7.0f,kNone,{3,{{0.0f,2.0f},{90.0f,-4.0f},{180.0f,2.0f}}},{3,{{0.0f,0.0f},{100.0f,-14.0f},{180.0f,-80.0f}}},
     {kNone,kNone},{kRpm,kRpm},kNone,kJetLoadPitch,kLoad},
},0,{},-20.0f+kTrimDb,100.0f,3000.0f,0.54f,{2,{{0.0f,30.0f},{180.0f,80.0f}}},{2,{{0.0f,0.0f},{180.0f,-2.0f}}}};

// The ground's own (the player's events, bus -8; the diesel's and the petrol's Distance not automated: the abrams'
// (ours)). rpm_result 0..100 and load 0..1.
constexpr Curve kTankDist{13,{{0.0f,0.0f},{7.1f,-1.41f},{14.2f,-3.09f},{58.0f,-6.72f},{101.9f,-13.1f},{148.0f,-14.7f},
                              {194.0f,-16.4f},{283.0f,-18.6f},{372.0f,-22.6f},{532.0f,-25.5f},{693.0f,-31.2f},
                              {846.0f,-36.0f},{1000.0f,-49.9f}}};
constexpr float kGroundBusDb=-8.0f;
constexpr Event kTurbinePlayer{2,{
    {4,{{k_gt_00,kRpm,{0.0f,0.0f,1.0f,10.0f},4.0f,0.0f,{1.0f,0.99f},0.0f},
        {k_gt_33,kRpm,{1.0f,10.0f,30.0f,45.0f},2.0f,0.0f,{33.0f,0.705f},0.0f},
        {k_gt_66,kRpm,{30.0f,45.0f,65.0f,80.0f},0.0f,0.0f,{55.0f,0.705f},0.0f},
        {k_gt_99,kRpm,{65.0f,80.0f,100.0f,100.0f},0.0f,0.0f,{100.0f,0.43f},0.0f}},
     0.0f,kTankDist,kNone,kNone,{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    {4,{{k_gt_00_load,kLoad,{0.0f,0.0f,0.01f,0.1f},0.0f,0.0f,{0.01f,0.99f},0.0f},
        {k_gt_33_load,kLoad,{0.01f,0.1f,0.3f,0.45f},0.0f,0.0f,{0.275f,0.675f},0.0f},
        {k_gt_66_load,kLoad,{0.3f,0.45f,0.65f,0.8f},0.0f,0.0f,{0.55f,0.705f},0.0f},
        {k_gt_99_load,kLoad,{0.65f,0.8f,1.0f,1.0f},0.0f,0.0f,{1.0f,0.77f},0.0f}},
     0.0f,{2,{{0.0f,6.0f},{1000.0f,-80.0f}}},{3,{{0.0f,-2.83f},{90.0f,-1.3f},{180.0f,0.0f}}},kNone,
     {{2,{{0.0f,-10.0f},{1.0f,-4.0f}}},kNone},{kLoad,kRpm},kNone,kNone,kRpm},
},0,{},kGroundBusDb+kTrimDb,0.0f,0.0f,0.0f,kNone,kNone};

constexpr Region kDi00{0.0f,0.0f,3.0f,15.0f},kDi33{2.0f,10.0f,33.1f,55.5f},kDi66{33.1f,33.1f,67.8f,83.7f},
                 kDi99{67.8f,83.7f,100.0f,100.0f};
constexpr Event kDieselPlayer{3,{
    {4,{{k_di_00,kRpm,kDi00,0.0f,0.0f,{5.0f,0.985f},0.0f},{k_di_33,kRpm,kDi33,6.0f,0.0f,{40.0f,0.5f},0.0f},
        {k_di_66,kRpm,kDi66,6.0f,-3.4f,{75.0f,0.5f},0.0f},{k_di_99_load,kRpm,kDi99,4.0f,0.0f,{75.0f,0.4f},0.0f}},
     0.0f,kTankDist,kNone,kNone,{{2,{{0.0f,-4.0f},{1.0f,-80.0f}}},kNone},{kLoad,kRpm},kNone,kNone,kRpm},
    // The loaded layers' regions are not in the player event's metadata: the AI event's (ours).
    {3,{{k_di_33_load,kRpm,kDi33,3.0f,0.0f,{40.0f,0.5f},0.0f},{k_di_66_load,kRpm,kDi66,0.0f,0.0f,{70.0f,0.66f},0.0f},
        {k_di_99_load,kRpm,kDi99,0.0f,0.0f,{100.0f,0.6f},0.0f}},
     0.0f,kTankDist,kNone,kNone,{{2,{{0.0f,-80.0f},{1.0f,0.0f}}},kNone},{kLoad,kRpm},kNone,kNone,kRpm},
    {3,{{k_di_hiss_idle,kRpm,{0.0f,0.0f,5.0f,20.0f},-8.0f,4.0f,{5.0f,0.9f},0.0f},
        {k_di_hiss_low,kRpm,{4.95f,20.0f,40.0f,74.95f},0.0f,0.0f,{30.0f,0.8f},0.0f},
        {k_di_hiss_high,kRpm,{40.0f,74.95f,100.0f,100.0f},3.0f,0.0f,{75.0f,0.8f},0.0f}},
     0.0f,kTankDist,{3,{{0.0f,-6.0f},{90.0f,-1.81f},{180.0f,0.0f}}},kNone,{{2,{{0.0f,-6.0f},{1.0f,0.0f}}},kNone},{kLoad,kRpm},
     kNone,kNone,kRpm},
},2,{{k_di_switch,kLoad,0.15f,0,8.0f,{2,{{0.0f,-80.0f},{19.9f,-4.0f}}},kSpeed},
     // Ours: the turbo's blow-off on lifting off (the event's hiss_load has no trigger in the metadata).
     {k_di_hiss_load,kLoad,0.15f,-1,0.0f,{2,{{0.0f,-80.0f},{19.9f,-4.0f}}},kSpeed}},
 kGroundBusDb+kTrimDb,0.0f,0.0f,0.0f,kNone,kNone};

constexpr Region kPe33{12.0f,21.0f,35.0f,50.0f},kPe66{35.0f,50.0f,65.0f,80.0f},kPe99{65.0f,80.0f,100.0f,100.0f};
constexpr Event kPetrolPlayer{2,{
    {4,{{k_pe_00,kRpm,{0.0f,0.0f,12.0f,21.0f},3.5f,0.0f,{1.0f,1.0f},0.0f},{k_pe_33,kRpm,kPe33,4.0f,0.0f,{20.0f,0.7f},0.0f},
        {k_pe_66,kRpm,kPe66,3.0f,0.0f,{50.0f,0.705f},0.0f},{k_pe_99,kRpm,kPe99,0.0f,0.0f,{100.0f,0.765f},0.0f}},
     0.0f,kTankDist,kNone,kNone,{{2,{{0.0f,0.0f},{1.0f,-80.0f}}},kNone},{kLoad,kRpm},kNone,kNone,kRpm},
    {3,{{k_pe_33_load,kRpm,kPe33,0.0f,0.0f,{24.0f,0.705f},0.0f},
        {k_pe_66_load,kRpm,{35.0f,50.0f,70.0f,85.0f},3.0f,0.0f,{55.0f,0.705f},0.0f},
        {k_pe_99_load,kRpm,kPe99,0.0f,0.0f,{100.0f,0.4f},0.0f}},
     0.0f,kTankDist,kNone,kNone,{{2,{{0.0f,-80.0f},{1.0f,0.0f}}},kNone},{kLoad,kRpm},kNone,kNone,kRpm},
},2,{{k_pe_revv,kLoad,0.1f,+1,0.0f,kNone,kRpm},
     // Ours: the release on lifting off (the event's sets it off at the engine's start, not played here).
     {k_pe_release,kLoad,0.1f,-1,-3.0f,kNone,kRpm}},
 kGroundBusDb+kTrimDb,0.0f,0.0f,0.0f,kNone,kNone};

// The others' (ai/abrams, ai/leo2a5, ai/m8: min 3 m, max 1000 m, Doppler 205 %).
constexpr float kGroundAiMin=3.0f,kGroundAiMax=1000.0f,kGroundAiDoppler=2.05f;
constexpr Event kTurbineAi{3,{
    {2,{{k_gt_99_load,kLoad,{0.1f,0.2f,1.0f,1.0f},0.0f,0.0f,{1.0f,0.8f},0.0f},
        {k_gt_00_load,kLoad,{0.0f,0.0f,0.1f,0.2f},0.0f,0.0f,{0.05f,0.935f},0.0f}},
     4.0f,{2,{{0.0f,0.0f},{100.0f,-80.0f}}},kNone,kNone,{{2,{{0.0f,-3.0f},{99.3f,0.0f}}},kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    {2,{{k_gt_99,kRpm,{10.0f,20.0f,100.0f,100.0f},6.0f,0.0f,{100.0f,0.7f},0.0f},
        {k_gt_33,kRpm,{0.0f,0.0f,10.0f,20.0f},6.0f,0.0f,{10.0f,0.8f},0.0f}},
     7.0f,{2,{{0.0f,-6.0f},{100.0f,-16.0f}}},kNone,kNone,{{2,{{0.0f,-4.0f},{99.3f,0.0f}}},kNone},{kRpm,kRpm},kNone,kNone,kRpm},
    {1,{{k_gt_dist,kRpm,{0.0f,0.0f,100.0f,100.0f},10.0f,0.0f,{100.0f,0.7f},0.0f}},
     0.0f,{2,{{0.0f,-80.0f},{100.0f,10.0f}}},kNone,kNone,{{2,{{0.14f,-4.0f},{19.6f,0.0f}}},kNone},{kRpm,kRpm},kNone,kNone,kRpm},
},0,{},kGroundBusDb+kTrimDb,kGroundAiMin,kGroundAiMax,kGroundAiDoppler,kNone,kNone};

constexpr Event kDieselAi{1,{
    {4,{{k_di_00,kRpm,{0.0f,0.0f,3.0f,15.0f},0.0f,0.0f,{5.0f,0.985f},0.0f},
        {k_di_33_load,kRpm,{2.0f,10.0f,33.09f,55.52f},3.0f,0.0f,{40.0f,0.5f},0.0f},
        {k_di_66_load,kRpm,{33.09f,33.09f,83.68f,83.68f},4.0f,0.0f,{70.0f,0.66f},0.0f},
        {k_di_99_load,kRpm,{67.8f,83.68f,100.0f,100.0f},4.0f,0.0f,{100.0f,0.6f},0.0f}},
     0.0f,kNone,kNone,kNone,{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
},1,{{k_di_switch,kLoad,0.15f,0,0.0f,kNone,kRpm}},kGroundBusDb+kTrimDb,kGroundAiMin,kGroundAiMax,kGroundAiDoppler,kNone,kNone};

constexpr Event kPetrolAi{1,{
    {4,{{k_pe_99_load,kRpm,kPe99,1.5f,0.0f,{100.0f,0.4f},0.0f},
        {k_pe_00,kRpm,{0.0f,0.0f,2.0f,14.0f},3.0f,0.0f,{1.0f,0.99f},0.0f},
        {k_pe_33_load,kRpm,{2.0f,11.0f,35.0f,50.0f},0.0f,0.0f,{24.0f,0.705f},0.0f},
        {k_pe_66_load,kRpm,{35.0f,50.0f,65.0f,85.0f},3.0f,0.0f,{55.0f,0.705f},0.0f}},
     -8.0f,kNone,kNone,kNone,{kNone,kNone},{kRpm,kRpm},kNone,kNone,kRpm},
},1,{{k_pe_revv,kLoad,0.1f,+1,0.0f,kNone,kRpm}},6.0f+kGroundBusDb+kTrimDb,kGroundAiMin,kGroundAiMax,kGroundAiDoppler,kNone,kNone};

// The running gear: speed km/h. The player's tracks (tracks_heavy, master +3, bus -12) and wheels (tracks_wheels, master
// +4: its Distance not automated: the tracks' (ours)); the others' (tracks_ai_heavy / _wheels: master +6, min 10 m,
// max 1000 m, Doppler on: 100 %).
constexpr Curve kTracksDist{13,{{1.96f,-6.0f},{3.0f,0.0f},{58.0f,-6.72f},{101.9f,-13.1f},{148.0f,-14.7f},{194.0f,-16.4f},
                                {283.0f,-18.6f},{372.0f,-22.6f},{532.0f,-25.5f},{693.0f,-31.2f},{846.0f,-36.0f},
                                {1000.0f,-80.0f}}};
constexpr float kGearBusDb=-12.0f;
constexpr Event kTracksPlayer{1,{
    {3,{{k_tr_33,kSpeed,{0.0f,0.0f,25.0f,42.3f},6.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_tr_66,kSpeed,{25.0f,42.3f,65.0f,81.2f},0.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_tr_99,kSpeed,{65.0f,81.2f,100.0f,100.0f},0.0f,0.0f,{0.0f,1.0f},0.0f}},
     0.0f,kTracksDist,{2,{{121.0f,0.0f},{180.0f,-4.0f}}},kNone,{{2,{{0.0f,-80.0f},{1.0f,0.0f}}},kNone},{kSpeed,kRpm},
     kNone,kNone,kRpm},
},1,{{k_tr_stop,kSpeed,7.0f,-1,0.0f,kNone,kRpm}},3.0f+kGearBusDb+kTrimDb,0.0f,0.0f,0.0f,kNone,kNone};
constexpr Event kWheelsPlayer{1,{
    {3,{{k_su_33,kSpeed,{0.0f,0.0f,25.0f,40.0f},0.0f,0.0f,{15.0f,0.9f},0.0f},
        {k_su_66,kSpeed,{25.0f,40.0f,60.0f,75.0f},0.0f,0.0f,{50.0f,0.695f},0.0f},
        {k_su_99,kSpeed,{60.0f,75.0f,100.0f,100.0f},0.0f,0.0f,{85.0f,0.845f},0.0f}},
     0.0f,kTracksDist,{3,{{1.5f,0.0f},{91.0f,4.0f},{180.0f,0.0f}}},kNone,
     {{4,{{0.0f,-80.0f},{1.0f,-18.0f},{4.66f,0.0f},{100.0f,8.0f}}},kNone},{kSpeed,kRpm},kNone,kNone,kRpm},
},1,{{k_su_load,kSpeed,1.15f,+1,0.0f,kNone,kRpm}},4.0f+kGearBusDb+kTrimDb,0.0f,0.0f,0.0f,kNone,kNone};
constexpr Event kTracksAi{1,{
    {3,{{k_tr_33,kSpeed,{0.0f,0.0f,25.0f,42.34f},0.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_tr_66,kSpeed,{25.0f,42.34f,67.28f,81.22f},0.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_tr_99,kSpeed,{67.28f,81.22f,100.0f,100.0f},0.0f,0.0f,{0.0f,1.0f},0.0f}},
     0.0f,kNone,kNone,kNone,{{4,{{0.0f,-80.0f},{1.0f,-6.0f},{2.0f,0.0f},{100.0f,0.0f}}},kNone},{kSpeed,kRpm},
     kNone,{2,{{0.0f,0.0f},{0.527f,-2.82f}}},kTurn},
},1,{{k_tr_stop,kSpeed,7.0f,-1,0.0f,kNone,kRpm}},6.0f+kGearBusDb+kTrimDb,10.0f,1000.0f,1.0f,kNone,kNone};
constexpr Event kWheelsAi{1,{
    {3,{{k_su_33,kSpeed,{0.0f,1.67f,25.0f,40.0f},0.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_su_66,kSpeed,{25.0f,40.0f,60.0f,75.0f},0.0f,0.0f,{0.0f,1.0f},0.0f},
        {k_su_99,kSpeed,{60.0f,75.0f,100.0f,100.0f},0.0f,0.0f,{0.0f,1.0f},0.0f}},
     0.0f,kNone,kNone,kNone,{{3,{{0.0f,-80.0f},{9.48f,0.0f},{100.0f,0.0f}}},kNone},{kSpeed,kRpm},kNone,kNone,kRpm},
},1,{{k_su_load,kSpeed,1.15f,+1,0.0f,kNone,kRpm}},6.0f+kGearBusDb+kTrimDb,10.0f,1000.0f,1.0f,kNone,kNone};

inline const Event& EngineEvent(Kind k,bool own) noexcept {
    switch(k) {
    case Kind::turbine: return own ? kTurbinePlayer : kTurbineAi;
    case Kind::diesel: return own ? kDieselPlayer : kDieselAi;
    default: return own ? kPetrolPlayer : kPetrolAi;
    }
}
inline const Event& GearEvent(Gear g,bool own) noexcept {
    return g==Gear::tracks ? (own ? kTracksPlayer : kTracksAi) : (own ? kWheelsPlayer : kWheelsAi);
}
}  // namespace crew::engine
