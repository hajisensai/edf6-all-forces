// The engines' War Thunder layers offline (src/sound_engine.h, played by jetsound.cpp and vehsound.cpp): the curves
// straight and held past their ends; the regions' fades; the autopitch 1 at its root; every layer a recording of the set;
// over each sheet the layers cross-fade with no hole (some layer heard everywhere on it) and the pitch rising with the
// rpm; the jet heard louder behind than in front, its afterburner only lit; the AI's inverse rolloff; the one-shots on
// their crossings only; the gearbox shifting up and down, the turbine spooling with lag; the jet's spool; the retarded
// time (the far source heard as it was); the class table.
//
//   cmake --build build --target sound_engine_check && build\sound_engine_check.exe
#include "../src/sound_engine.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace crew::engine;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps*(1.0f+std::fabs(b)); }

Inputs In(float rpm,float load=0.0f,float wep=0.0f,float speed=0.0f,float share=0.0f) {
    Inputs in{};
    in.v[kRpm]=rpm;in.v[kLoad]=load;in.v[kWep]=wep;in.v[kSpeed]=speed;in.v[kSpeedShare]=share;
    return in;
}
float Power(const Event& e,const Inputs& in,float d,float cone) {
    Heard h[kMostEventLayers];
    const int n=Hear(e,in,d,cone,h);
    float p=0.0f;
    for(int i=0;i<n;++i)p+=h[i].gain*h[i].gain;
    return p;
}
float GainOf(const Event& e,const Inputs& in,float d,float cone,int clip) {
    Heard h[kMostEventLayers];
    const int n=Hear(e,in,d,cone,h);
    float g=0.0f;
    for(int i=0;i<n;++i)if(h[i].clip==clip)g+=h[i].gain;
    return g;
}
float RatioOf(const Event& e,const Inputs& in,int clip) {
    Heard h[kMostEventLayers];
    const int n=Hear(e,in,10.0f,90.0f,h);
    for(int i=0;i<n;++i)if(h[i].clip==clip)return h[i].ratio;
    return 0.0f;
}

const Event* const kAll[]={&kJetPlayer,&kJetAi,&kTurbinePlayer,&kDieselPlayer,&kPetrolPlayer,&kTurbineAi,&kDieselAi,
                           &kPetrolAi,&kTracksPlayer,&kWheelsPlayer,&kTracksAi,&kWheelsAi};

void Basics() {
    constexpr Curve c{3,{{0.0f,0.0f},{10.0f,-10.0f},{20.0f,-30.0f}}};
    Check(Near(At(c,5.0f),-5.0f) && Near(At(c,15.0f),-20.0f) && Near(At(c,-1.0f),0.0f) && Near(At(c,99.0f),-30.0f) &&
              Near(At(kNone,3.0f),0.0f),"a curve: straight between its points, held past its ends, none 0");
    constexpr Region r{10.0f,20.0f,30.0f,50.0f};
    Check(Fade(r,5.0f)==0.0f && Near(Fade(r,15.0f),0.5f) && Fade(r,25.0f)==1.0f && Near(Fade(r,40.0f),0.5f) &&
              Fade(r,51.0f)==0.0f && Fade({0.0f,0.0f,1.0f,1.0f},0.0f)==1.0f,"a region: faded in, held, faded out");
    Check(Near(Pitch({100.0f,0.5f},100.0f,0.0f),1.0f) && Near(Pitch({100.0f,0.5f},0.0f,0.0f),0.5f) &&
              Near(Pitch({100.0f,0.5f},50.0f,0.0f),0.75f) && Pitch({0.0f,1.0f},5.0f,0.0f)==1.0f,
          "autopitch: 1 at its root, its minimum's at the sheet's minimum, none without a root");
    Check(Near(Amp(0.0f),1.0f) && Near(Amp(-6.0206f),0.5f) && Amp(-80.0f)==0.0f,"dB to amplitude, -80 silent");
    const float nose[3]={0.0f,0.0f,1.0f},ahead[3]={0.0f,0.0f,1.0f},behind[3]={0.0f,0.0f,-1.0f},side[3]={1.0f,0.0f,0.0f};
    Check(Near(ConeAngle(nose,ahead),0.0f,1e-2f) && Near(ConeAngle(nose,behind),180.0f) && Near(ConeAngle(nose,side),90.0f),
          "the cone angle: 0 in front of the nose, 180 behind");
}

void Tables() {
    bool clips=true,counts=true,regions=true,mins=true;
    for(const Event* e : kAll) {
        counts=counts && e->groups>0 && e->groups<=kMostGroups && e->shots>=0 && e->shots<=kMostShots &&
               LayerCount(*e)<=kMostEventLayers;
        for(int g=0;g<e->groups;++g) {
            counts=counts && e->g[g].n>0 && e->g[g].n<=kMostLayers;
            for(int i=0;i<e->g[g].n;++i) {
                const Layer& l=e->g[g].layer[i];
                clips=clips && l.clip>=0 && l.clip<crew::wt::kCount && crew::wt::kVariants[l.clip]==1;
                regions=regions && l.r.lo<=l.r.in && l.r.in<=l.r.out && l.r.out<=l.r.hi;
                mins=mins && (l.ap.root==0.0f || l.ap.root>l.sheetMin);
            }
        }
        for(int i=0;i<e->shots;++i)clips=clips && e->s[i].clip>=0 && e->s[i].clip<crew::wt::kCount;
    }
    Check(clips,"every layer one recording of the set (a loop: one variant), every shot a group of it");
    Check(counts,"every event within its sizes");
    Check(regions,"every region in order");
    Check(mins,"every autopitch root above its sheet's minimum");
}

// Over a sheet, some layer is heard at every point (no hole in the cross-fade) and the summed level moves smoothly.
bool NoHole(const Event& e,float from,float to,Inputs (*at)(float)) {
    float last=-1.0f;
    for(int i=0;i<=200;++i) {
        const float x=from+(to-from)*static_cast<float>(i)/200.0f;
        const float p=Power(e,at(x),20.0f,90.0f);
        if(!(p>1e-8f))return false;
        if(last>0.0f && (p>last*4.0f || p<last/4.0f))return false;
        last=p;
    }
    return true;
}
void Sheets() {
    Check(NoHole(kJetPlayer,800.0f,2750.0f,[](float x){ return In(x,0.5f); }),"the player's jet: heard all over its rpm");
    Check(NoHole(kJetAi,800.0f,2400.0f,[](float x){ return In(x,0.5f); }),"the AI jet: heard all over its rpm");
    Check(NoHole(kTurbinePlayer,0.0f,100.0f,[](float x){ return In(x,x/100.0f); }),"the turbine: heard all over its rpm");
    Check(NoHole(kDieselPlayer,0.0f,100.0f,[](float x){ return In(x,0.0f); }) &&
              NoHole(kDieselPlayer,0.0f,100.0f,[](float x){ return In(x,1.0f); }),
          "the diesel: heard all over its rpm, off load and on it");
    Check(NoHole(kPetrolPlayer,0.0f,100.0f,[](float x){ return In(x,0.0f); }) &&
              NoHole(kPetrolPlayer,13.0f,100.0f,[](float x){ return In(x,1.0f); }),
          "the petrol: heard all over its rpm off load, on it over its loaded layers (from 12)");
    Check(NoHole(kTurbineAi,0.0f,100.0f,[](float x){ return In(x,x/100.0f); }) &&
              NoHole(kDieselAi,0.0f,100.0f,[](float x){ return In(x,0.5f); }) &&
              NoHole(kPetrolAi,0.0f,100.0f,[](float x){ return In(x,0.5f); }),"the AI's engines: heard all over");
    Check(NoHole(kTracksPlayer,3.0f,100.0f,[](float x){ return In(0.0f,0.0f,0.0f,x); }) &&
              NoHole(kWheelsPlayer,5.0f,100.0f,[](float x){ return In(0.0f,0.0f,0.0f,x); }) &&
              NoHole(kTracksAi,3.0f,100.0f,[](float x){ return In(0.0f,0.0f,0.0f,x); }) &&
              NoHole(kWheelsAi,10.0f,100.0f,[](float x){ return In(0.0f,0.0f,0.0f,x); }),"the running gear: heard over its speed");
    Check(Power(kTracksPlayer,In(0.0f),10.0f,90.0f)==0.0f && Power(kWheelsAi,In(0.0f),10.0f,90.0f)==0.0f,
          "the running gear: silent standing");
    // The pitch rises with the rpm in each layer.
    Check(RatioOf(kJetPlayer,In(2000.0f),crew::wt::k_jet_max_rear)<RatioOf(kJetPlayer,In(2600.0f),crew::wt::k_jet_max_rear) &&
              RatioOf(kDieselPlayer,In(20.0f),crew::wt::k_di_33)<RatioOf(kDieselPlayer,In(40.0f),crew::wt::k_di_33) &&
              Near(RatioOf(kTurbinePlayer,In(100.0f),crew::wt::k_gt_99),1.0f),
          "the pitch follows the rpm (1 at the layer's root)");
    // Load: the diesel's loaded layers come in on load, its unloaded ones go.
    Check(GainOf(kDieselPlayer,In(50.0f,1.0f),10.0f,90.0f,crew::wt::k_di_33_load)>
                  GainOf(kDieselPlayer,In(50.0f,0.0f),10.0f,90.0f,crew::wt::k_di_33_load)*10.0f &&
              GainOf(kDieselPlayer,In(50.0f,1.0f),10.0f,90.0f,crew::wt::k_di_33)<
                  GainOf(kDieselPlayer,In(50.0f,0.0f),10.0f,90.0f,crew::wt::k_di_33)*0.1f,
          "the diesel's load: its loaded layers in, its unloaded ones out");
}

void Space() {
    const Inputs mil=In(2600.0f,1.0f);
    Check(GainOf(kJetPlayer,mil,50.0f,180.0f,crew::wt::k_jet_max_rear)>GainOf(kJetPlayer,mil,50.0f,0.0f,crew::wt::k_jet_max_rear)*3.0f &&
              GainOf(kJetPlayer,mil,50.0f,0.0f,crew::wt::k_jet_max_front)>GainOf(kJetPlayer,mil,50.0f,180.0f,crew::wt::k_jet_max_front)*2.0f,
          "the jet: its rear recording behind it, its front one in front");
    Check(GainOf(kJetPlayer,mil,50.0f,180.0f,crew::wt::k_jet_ab)==0.0f &&
              GainOf(kJetPlayer,In(2750.0f,1.0f,1.0f),50.0f,180.0f,crew::wt::k_jet_ab)>0.0f,"the afterburner: heard only lit");
    Check(Power(kJetPlayer,mil,10.0f,180.0f)>Power(kJetPlayer,mil,500.0f,180.0f) && Power(kJetPlayer,mil,1001.0f,180.0f)==0.0f,
          "the player's jet: quieter the farther, silent past 1 km (its Distance curves)");
    Check(Near(Rolloff(kJetAi,50.0f),1.0f) && Near(Rolloff(kJetAi,200.0f),0.5f) && Near(Rolloff(kJetAi,9000.0f),100.0f/3000.0f),
          "the AI's rolloff: min / d between its min and max distance");
    const Inputs far=In(2400.0f,1.0f,0.0f,0.0f,100.0f);
    Check(GainOf(kJetAi,far,2500.0f,180.0f,crew::wt::k_jet_far)>0.0f && GainOf(kJetAi,far,2500.0f,180.0f,crew::wt::k_jet_max_rear)==0.0f,
          "the AI jet far off: its far recording, not the near ones");
    Check(GainOf(kJetAi,far,300.0f,10.0f,crew::wt::k_jet_approach)>GainOf(kJetAi,far,300.0f,170.0f,crew::wt::k_jet_approach)*10.0f,
          "the AI jet: its approach coming, not going");
    Check(Near(Doppler(kJetAi,2.0f,0.0f),1.3f) && Near(Doppler(kJetAi,2.0f,180.0f),1.8f) &&
              Near(Doppler(kJetPlayer,2.0f,0.0f),1.54f) && Near(Doppler(kTracksPlayer,2.0f,0.0f),1.0f) &&
              Doppler(kJetAi,NAN,0.0f)==1.0f,"the Doppler at each event's scale");
    Check(Power(kTurbineAi,In(50.0f,0.5f),5.0f,90.0f)>Power(kTurbineAi,In(50.0f,0.5f),500.0f,90.0f)*50.0f,
          "the AI's tank: far quieter far off");
}

void Shots() {
    Check(Crossed(kJetPlayer,In(2600.0f,1.0f,0.0f),In(2600.0f,1.0f,0.2f))==1u &&
              Crossed(kJetPlayer,In(2600.0f,1.0f,0.2f),In(2600.0f,1.0f,0.0f))==0u &&
              Crossed(kJetPlayer,In(2600.0f,1.0f,0.2f),In(2600.0f,1.0f,0.5f))==0u,"the afterburner lights once, going up");
    Check(Crossed(kDieselPlayer,In(30.0f,0.0f),In(30.0f,0.5f))==1u && Crossed(kDieselPlayer,In(30.0f,0.5f),In(30.0f,0.0f))==3u,
          "the diesel: its switch either way, its blow-off lifting off");
    Check(Crossed(kTracksPlayer,In(0.0f,0.0f,0.0f,20.0f),In(0.0f,0.0f,0.0f,5.0f))==1u &&
              Crossed(kTracksPlayer,In(0.0f,0.0f,0.0f,5.0f),In(0.0f,0.0f,0.0f,20.0f))==0u,"the tracks' stop, slowing only");
    Check(ShotGain(kDieselPlayer,0,In(30.0f,0.5f,0.0f,0.0f),5.0f)==0.0f && ShotGain(kDieselPlayer,0,In(30.0f,0.5f,0.0f,30.0f),5.0f)>0.0f,
          "the diesel's switch: not heard standing");
}

void Models() {
    const Drive tank{Kind::diesel,Gear::tracks,5,65.0f,1.0f};
    Gearbox b;
    const float dt=1.0f/60.0f;
    for(int i=0;i<120;++i)Step(b,tank,0.0f,0.0f,1.0f,dt);
    Check(Near(b.rpm,kIdle,1e-2f) && b.gear==0,"standing: the idle in the first");
    int most=0;bool up=true;float lastGear=0;
    for(int i=0;i<1200;++i) {
        const float v=65.0f*static_cast<float>(i)/1200.0f;
        Step(b,tank,v,1.0f,1.0f,dt);
        if(b.gear<lastGear)up=false;
        lastGear=static_cast<float>(b.gear);
        if(b.gear>most)most=b.gear;
    }
    Check(most==4 && up && b.rpm>60.0f && b.rpm<=100.0f,"accelerating: up through the five gears, never down");
    for(int i=0;i<1200;++i)Step(b,tank,65.0f*(1.0f-static_cast<float>(i)/1200.0f),0.0f,1.0f,dt);
    Check(b.gear==0,"slowing: down to the first");
    Gearbox s;
    for(int i=0;i<30;++i)Step(s,tank,0.0f,1.0f,1.0f,dt);
    Check(s.rpm>=kSlip && s.rpm<kSlip+kSlipThrottle+1.0f,"pulling away: the clutch slips, the rpm over the speed's");
    Gearbox off;
    for(int i=0;i<300;++i)Step(off,tank,0.0f,0.0f,0.0f,dt);
    Check(off.rpm<0.5f,"the engine off: no rpm");
    // Backing: the one reverse gear, high (War Thunder's), in and out of it a shift; braking: the forward gears down.
    {
        Gearbox r;
        for(int i=0;i<600;++i)Step(r,tank,30.0f,0.8f,1.0f,dt);
        Check(!r.reverse && r.gear>0,"forward at 30 km/h: a forward gear over the first");
        const float fwd=r.rpm;
        for(int i=0;i<60;++i)Step(r,tank,30.0f*(1.0f-static_cast<float>(i)/60.0f),0.3f,1.0f,dt);
        Check(!r.reverse,"braking to a stand: still forward");
        Step(r,tank,-0.5f,1.0f,1.0f,dt);
        Check(!r.reverse,"under kReverseKmh back: not yet the reverse gear");
        Step(r,tank,-2.0f,1.0f,1.0f,dt);
        Check(r.reverse && r.gear==0 && r.shift>0.0f,"past kReverseKmh back: into the reverse gear, a shift");
        int backMost=0;float low=1000.0f;
        for(int i=0;i<1200;++i) {
            const float v=-50.0f*static_cast<float>(i)/1200.0f;
            Step(r,tank,v,1.0f,1.0f,dt);
            if(r.gear>backMost)backMost=r.gear;
            if(i>120 && r.rpm<low)low=r.rpm;
            if(!r.reverse)backMost=99;
        }
        Check(backMost==0 && r.reverse,"backing up to 50 km/h: the one reverse gear, never a forward one");
        Check(low>=kSlip+kSlipThrottle*0.9f,"backing at full throttle: the rpm high all the way");
        Check(r.rpm>fwd,"backing at 50 km/h: higher than forward in its gear at 30");
        Check(r.rpm>kShiftUp && r.rpm<=100.0f,"backing near the top: past kShiftUp (the gear as short as kReverseShare)");
        Step(r,tank,0.5f,0.0f,1.0f,dt);
        Check(r.reverse,"under kReverseKmh forward: still in reverse");
        Step(r,tank,2.0f,0.5f,1.0f,dt);
        Check(!r.reverse && r.gear==0 && r.shift>0.0f,"forward again: the first, a shift");
        const Drive turbine{Kind::turbine,Gear::tracks,0,60.0f,1.0f};
        Gearbox g;
        for(int i=0;i<600;++i)Step(g,turbine,-20.0f,0.6f,1.0f,dt);
        Gearbox h;
        for(int i=0;i<600;++i)Step(h,turbine,20.0f,0.6f,1.0f,dt);
        Check(!g.reverse && Near(g.rpm,h.rpm,1e-3f),"the turbine: no gears either way, backing as forward");
    }
    // The driver's pedal (forward negative on the stick).
    Check(PedalOf(30.0f,-1.0f)==Pedal::drive && PedalOf(-30.0f,1.0f)==Pedal::drive && PedalOf(0.0f,1.0f)==Pedal::drive,
          "with the way it rolls, or standing: a drive");
    Check(PedalOf(30.0f,1.0f)==Pedal::brake && PedalOf(-30.0f,-0.5f)==Pedal::brake,"against it: a brake");
    Check(PedalOf(30.0f,0.1f)==Pedal::coast && PedalOf(30.0f,-0.1f)==Pedal::coast,"the stick in the middle: a coast");
    Check(PedalOf(0.5f,1.0f)==Pedal::drive,"under kReverseKmh: no brake, backing away");
    Check(PedalOf(std::numeric_limits<float>::quiet_NaN(),1.0f)==Pedal::coast &&
          PedalOf(30.0f,std::numeric_limits<float>::infinity())==Pedal::coast,"not finite: a coast");
    const Drive gt{Kind::turbine,Gear::tracks,0,60.0f,1.0f};
    Gearbox t;
    for(int i=0;i<60;++i)Step(t,gt,0.0f,0.0f,1.0f,dt);
    const float idle=t.rpm;
    Step(t,gt,0.0f,1.0f,1.0f,dt);
    Check(t.rpm<idle+1.0f,"the turbine: its spool lags the throttle");
    for(int i=0;i<300;++i)Step(t,gt,0.0f,1.0f,1.0f,dt);
    Check(Near(t.rpm,100.0f,1e-2f) && t.gear==0,"the turbine: full rpm on full throttle, no gears");
    Spool j;
    for(int i=0;i<60;++i)Step(j,0.0f,false,dt);
    Check(Near(j.rpm,kJetIdle) && j.wep==0.0f,"the jet: idle at no power");
    for(int i=0;i<60;++i)Step(j,1.0f,false,dt);
    Check(j.rpm>kJetIdle+300.0f && j.rpm<kJetMil,"the jet: spooling up over seconds");
    for(int i=0;i<600;++i)Step(j,1.0f,true,dt);
    Check(Near(j.rpm,kJetWep) && Near(j.wep,1.0f),"the jet: the afterburner's rpm and lit");
    // Slammed from idle: unlit through the spool, lit only from military power on.
    Spool slam;
    bool early=false;
    for(int i=0;i<600;++i) {
        Step(slam,1.0f,true,dt);
        if(slam.rpm<kJetLight && slam.wep>0.0f)early=true;
    }
    Check(!early && Near(slam.rpm,kJetWep) && Near(slam.wep,1.0f),"the jet: the afterburner lights at military power, not before");
    for(int i=0;i<600;++i)Step(slam,0.6f,false,dt);
    Check(slam.wep==0.0f && slam.rpm<kJetLight,"the jet: the afterburner out off the last notch");
    Check(Near(AiRpm(kJetIdle),kJetIdle) && Near(AiRpm(kJetWep),kJetAiTop),"the AI's rpm: the player's range onto its sheet");
}

void Late() {
    static Trail t;
    const float listener[3]={0.0f,0.0f,0.0f};
    for(int i=0;i<600;++i) {
        Sample s{};
        s.pos[2]=100.0f+100.0f/60.0f*static_cast<float>(i);   // going away at 100 m/s, 1.1 km off now
        s.ms=static_cast<std::uint64_t>(i)*1000/60;
        Push(t,s);
    }
    const std::uint64_t now=599ull*1000/60;
    const Sample* x=Retarded(t,listener,now);
    // Heard now: from where it was when its sound has since come to the listener: age * 340 = distance.
    const float age=static_cast<float>(now-x->ms)/1000.0f,dist=std::fabs(x->pos[2]);
    Check(x && std::fabs(age*kSoundSpeed-dist)<10.0f && age>2.0f,"the speed of sound: heard as it was seconds ago");
    static Trail one;
    Sample s{};
    s.pos[0]=3000.0f;s.ms=1000;
    Push(one,s);
    Check(Retarded(one,listener,1000)==&one.s[one.head] && Retarded(Trail{},listener,0)==nullptr,
          "just seen far off: its oldest; none seen: none");
}

void Classes() {
    bool ok=true;
    for(const ClassDrive& c : kDrives)
        ok=ok && c.name && c.d.top>0.0f && c.d.pitch>0.5f && c.d.pitch<2.0f &&
           (c.d.kind==Kind::turbine ? c.d.gears==0 : c.d.gears>0);
    Check(ok && sizeof(kDrives)/sizeof(kDrives[0])==10,"the class table: every engined class, a turbine has no gears");
}
}  // namespace

int main() {
    Basics();
    Tables();
    Sheets();
    Space();
    Shots();
    Models();
    Late();
    Classes();
    std::printf("%s (%d failures)\n",failures ? "FAILED" : "all ok",failures);
    return failures ? 1 : 0;
}
