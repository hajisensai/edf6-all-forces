// Production sound dispatch with stand-in vehicle memory and recording audio voices.
// No game process, audio device or installed files are used.
#include "../src/jetsound.cpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures=0,opens=0,closes=0,sets=0,shots=0,onces=0;
// A stand-in fault: raised inside a stub, `faultPending` while the stub's frame is still on the stack (the filter
// runs then; the stub's __finally clears it when the stack unwinds, before the handler).
bool faultPending=false,setFaults=false,jetFaults=false;
int closesBeforeUnwind=0;
void RaiseFault() noexcept {
    __try { faultPending=true;RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr); }
    __finally { faultPending=false; }
}
ULONGLONG nowMs=1000,nowFrame=60;
crew::Config config{};
crew::jetsound::State flight{};
bool playerState=false,npcState=false,occupied=false,leverHeard=false;
crew::jetsound::Lever heardLever{};
crew::jet::Jet npc{};
// The voices open (by loop index) and what each was last set to: the engine's level is their sum.
bool used[256]{};
int groupOf[256]{};   // the WT group each one plays
crew::audio::Heard lastSet[256]{};
int pool=256;   // the stand-in voice pool's size (the plugin's: audio::kLoopPool)
float Loud() noexcept { float g=0.0f;for(int i=0;i<256;++i)if(used[i])g+=lastSet[i].gain;return g; }
int Open() noexcept { int n=0;for(bool u:used)n+=u ? 1 : 0;return n; }
int OpenOf(int group) noexcept { int n=0;for(int i=0;i<256;++i)n+=used[i] && groupOf[i]==group ? 1 : 0;return n; }
int oncesOf[crew::wt::kCount]{};    // the one-time voices opened, by WT group
void Check(bool pass,const char* name) {
    std::printf("%s %s\n",pass ? "PASS" : "FAIL",name);
    if(!pass)++failures;
}
}
namespace edf {
bool Readable(const void* p,std::size_t,bool) noexcept {return p!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
unsigned SeatCount(const unsigned char*) noexcept {return 1;}
Rider SeatRider(const unsigned char*,const unsigned char*) noexcept {return occupied ? Rider::player : Rider::none;}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept {return config;}
ULONGLONG GameMs() noexcept {return nowMs;}
ULONGLONG GameFrame() noexcept {return nowFrame;}
void Log(const char*,...) noexcept {}
PluginBody BodyOf(const void*) noexcept {return PluginBody::jet;}
bool IsStoreWeapon(const unsigned char*) noexcept {return false;}
namespace jetsound {
bool PlayerState(const unsigned char*,State* out) noexcept {*out=flight;return playerState;}
bool HeardLever(const unsigned char*,Lever* out) noexcept {*out=heardLever;return leverHeard;}
}
namespace jet {
Jet* FindJet(const unsigned char*) noexcept {if(jetFaults)RaiseFault();return npcState ? &npc : nullptr;}
}
namespace audio {
bool Start() noexcept {return true;}
int OpenWtLoop(int group) noexcept {
    if(::Open()>=pool)return -1;
    for(int i=0;i<256;++i)if(!used[i]){used[i]=true;groupOf[i]=group;lastSet[i]=Heard{};++opens;return i;}
    return -1;
}
int OpenWtOnce(int group,int) noexcept {
    const int i=OpenWtLoop(group);
    if(i>=0){++onces;if(group>=0 && group<crew::wt::kCount)++oncesOf[group];}
    return i;
}
float WtSeconds(int,int) noexcept {return 2.0f;}
void CloseLoop(int i) noexcept {if(i<0 || !used[i])return;used[i]=false;++closes;if(faultPending)++closesBeforeUnwind;}
void SetLoop(int i,const Heard& h) noexcept {if(setFaults)RaiseFault();++sets;if(i>=0)lastSet[i]=h;}
void PlayWt(int,int,const Heard&) noexcept {++shots;}
bool WtThere(int) noexcept {return true;}
int LoopsOpen() noexcept {return ::Open();}
int LoopsFree() noexcept {return pool-::Open();}
void Beat(float) noexcept {}
}
}
// The voices one aircraft has open (its entry's), -1 with no entry.
int VoicesOf(unsigned char* p) noexcept {
    for(int i=0;i<crew::rowCount;++i)if(const auto& s=crew::rows[i]->s;s.ref.Is(p)) {
        int n=0;
        for(int l:s.voice)n+=l ? 1 : 0;
        return n;
    }
    return -1;
}
int main() {
    using namespace crew;
    std::vector<unsigned char> fakeImage(0x20B3000),vehicle(0x3000),seat(kSeatStride),soundSystem(0x100);
    image=fakeImage.data();
    // The camera (Tick): the sound system's listener 0 at the origin, looking down its rows.
    float cam[kListenerFloats]{};
    cam[4]=cam[9]=cam[14]=cam[19]=1.0f;
    Put<const float*>(soundSystem.data(),kListeners,cam);Put<std::uint64_t>(soundSystem.data(),kListenerCount,1);
    Put<unsigned char*>(image,kSoundSystem,soundSystem.data());
    auto* v=vehicle.data();
    // The air's layers (jet_air.h) off for the engine's own checks; theirs below.
    config.jetSoundPassby=config.jetSoundWind=config.jetSoundOverload=false;
    auto body=[&](unsigned char* p,float x) {
        Put<void*>(p,kSelfCtrl,seat.data());Put<void*>(p,kSeats,seat.data());
        Put<float>(p,kMatrix,1.0f);Put<float>(p,kMatrix+20,1.0f);Put<float>(p,kMatrix+40,1.0f);
        Put<float>(p,kPosition,x);
    };
    body(v,0.0f);
    sigOk=true;hasListener=true;
    listener[4]=listener[9]=listener[14]=listener[19]=1.0f;
    JetSound(v);
    Check(opens==0,"empty parked display does not allocate or play a voice");
    // Merely sliding or falling while empty is not an engine command.
    Put<float>(v,kPosition,100.0f);++nowFrame;nowMs+=16;JetSound(v);
    Check(opens==0,"empty displaced aircraft remains silent");
    // A frame: the jet's step, then the frame's mix (JetSoundTick: the camera, every jet heard together).
    auto frameOn=[&]{++nowFrame;nowMs+=16;JetSound(v);JetSoundTick();};
    auto rpm=[]{ return rows[0]->s.heard.v[engine::kRpm]; };
    // Frames of `step` until no voice is open: how many (-1: not within `most`), the first one's voices still open
    // (none cut at once), and the worst frame's fall as a share of the level before (no step down).
    struct Out { int frames=-1;bool firstOpen=false;float worst=0.0f; };
    auto fadeOut=[&](auto step,int most) {
        Out o{};
        const float from=Loud();
        float prev=from;
        for(int i=1;i<=most;++i) {
            step();
            if(i==1)o.firstOpen=Open()>0;
            const float l=Loud();
            if(from>0.0f && (prev-l)/from>o.worst)o.worst=(prev-l)/from;
            prev=l;
            if(!Open()){o.frames=i;break;}
        }
        return o;
    };
    auto smoothOut=[](const Out& o,int least,int most) {
        return o.firstOpen && o.frames>=least && o.frames<=most && o.worst<0.05f;
    };
    occupied=true;playerState=true;flight={false,true,true,false,0.0f,0.0f};
    for(int i=0;i<120;++i)frameOn();
    const float idle=Loud(),idleRpm=rpm();
    Check(Open()>0 && idle>0.0f && std::fabs(idleRpm-engine::kJetIdle)<1.0f,"occupied parked aircraft idles at the idle rpm");
    flight={false,true,false,true,0.0f,0.0f};
    for(int i=0;i<120;++i)frameOn();
    Check(Open()>0 && rpm()>idleRpm+400.0f,"zero-speed powered hover stays spooled up (its rotor floor)");
    // A rotor craft's throttle is its engine (Hover's power, HoverDone): more of it, a higher engine at one speed.
    const float hoverRpm=rpm();
    flight={false,true,false,true,0.9f,0.0f};
    for(int i=0;i<300;++i)frameOn();
    Check(rpm()>hoverRpm+300.0f,"a rotor craft's engine power raises its rpm");
    flight={false,true,false,false,1.0f,200.0f};
    for(int i=0;i<300;++i)frameOn();
    Check(std::fabs(rpm()-engine::kJetWep)<1.0f && rows[0]->s.spool.wep>0.99f && shots>0,
          "full throttle lights the afterburner: the top rpm, its layer, the ignition's one-shot");
    std::printf("  idle %.0f rpm level %.3f; afterburner %.0f rpm level %.3f, %d voices\n",idleRpm,idle,rpm(),Loud(),Open());
    flight={false,true,true,true,0.0f,0.0f};frameOn();
    occupied=false;
    // Its sound 100 m off: 0.29 s on its way, then the fade (1.5 s): about 110 frames, none cut at once.
    Out o=fadeOut(frameOn,200);
    std::printf("  getting out: closed after %d frames, worst frame's fall %.1f%%\n",o.frames,100.0f*o.worst);
    Check(smoothOut(o,90,130),"getting out of a parked aircraft fades its voices out (not cut), then closes them");
    playerState=false;npcState=true;npc.role=jet::Role::carrier;
    for(int i=0;i<120;++i){npc.lastStep=nowMs+16;frameOn();}
    Check(Open()>0 && rpm()>engine::AiRpm(engine::kJetIdle)+300.0f,"NPC carrier hover without a local player remains audible");
    const float npcRpm=rpm();
    npc.m.power=0.9f;
    for(int i=0;i<300;++i){npc.lastStep=nowMs+16;frameOn();}
    Check(rpm()>npcRpm,"an NPC rotor craft's engine follows its Hover power");
    npc.m.power=0.0f;npc.lastStep=nowMs;
    o=fadeOut(frameOn,200);
    Check(smoothOut(o,90,140),"retained stale NPC record cannot keep an engine running (faded out, then closed)");
    for(int i=0;i<120;++i){npc.lastStep=nowMs+16;frameOn();}
    const int beforeDead=Open();
    v[kDead]=1;
    o=fadeOut(frameOn,200);
    std::printf("  wrecked: closed after %d frames\n",o.frames);
    Check(beforeDead>0 && o.firstOpen && o.frames>=15 && o.frames<=45,
          "destroyed aircraft: its last sound heard, then faded out in kDeadFade (0.25 s)");
    v[kDead]=0;
    for(int i=0;i<120;++i){npc.lastStep=nowMs+16;frameOn();}
    o=fadeOut([&]{++nowFrame;nowMs+=16;JetSoundTick();},300);
    Check(smoothOut(o,100,150),"deleted or no-longer-updated aircraft is reaped (stale, heard out, faded)");
    npc.lastStep=nowMs+16;
    for(int i=0;i<120;++i){npc.lastStep=nowMs+16;frameOn();}
    ResetJetSound();
    Check(Open()==0,"mission reset releases the remaining aircraft voices");
    // A registered jet another machine runs (online): no flight record here. With no lever heard of it, someone in it
    // idles, however fast it moves (no guess from its speed: 250 m/s lit the afterburner before).
    npcState=false;playerState=false;occupied=true;
    for(int i=0;i<300;++i){Put<float>(v,kPosition+8,static_cast<float>(i)*250.0f/60.0f);frameOn();}
    Check(Open()>0 && std::fabs(rpm()-engine::AiRpm(engine::kJetIdle))<1.0f && rows[0]->s.spool.wep<0.01f,
          "another machine's fast jet, no lever heard: idling, no afterburner from its speed");
    // Its lever heard (jet_lever_net.cpp): the engine and the afterburner follow it, as ours.
    // (Back by the camera and still: what is heard is the lever, not the motion.)
    Put<float>(v,kPosition+8,0.0f);
    leverHeard=true;heardLever={1.0f,false,false};
    for(int i=0;i<300;++i)frameOn();
    Check(rpm()>engine::AiRpm(engine::kJetIdle)+300.0f && rows[0]->s.spool.wep>0.99f,"its heard lever full: spooled up, the afterburner lit");
    heardLever={0.55f,false,false};
    for(int i=0;i<300;++i)frameOn();
    Check(rows[0]->s.spool.wep<0.01f,"its heard lever at cruise: the afterburner out");
    // An unmanned one (a drone) is heard by its lever alone.
    occupied=false;frameOn();
    Check(Open()>0,"an empty jet with its lever heard still sounds");
    leverHeard=false;
    o=fadeOut(frameOn,200);
    Check(smoothOut(o,90,130),"...and fades out once nobody is in it and no lever is heard");

    // The mix (jet_mix.h) over several jets: NPC rotor craft at full power, each its own body.
    ResetJetSound();
    leverHeard=false;playerState=false;occupied=false;npcState=true;npc.role=jet::Role::carrier;npc.m.power=0.9f;
    std::vector<std::vector<unsigned char>> bodies(48,std::vector<unsigned char>(0x3000));
    auto jetAt=[&](int i){return bodies[static_cast<std::size_t>(i)].data();};
    auto frames=[&](int n,int from,int to) {
        for(int f=0;f<n;++f) {
            ++nowFrame;nowMs+=16;npc.lastStep=nowMs;
            for(int i=from;i<to;++i)JetSound(jetAt(i));
            JetSoundTick();
        }
    };
    // A jet newly heard (spawning 100 m off): from nothing to its level over the fade, no frame's step.
    {
        body(jetAt(0),100.0f);
        float prev=0.0f,worst=0.0f,first=0.0f;int at=-1;
        std::vector<float> l;
        for(int f=0;f<150;++f){frames(1,0,1);l.push_back(Loud());}
        const float top=l.back();
        for(int f=0;f<150;++f) {
            if(f==0)first=l[0];
            if(top>0.0f)worst=std::fmax(worst,std::fabs(l[static_cast<std::size_t>(f)]-prev)/top);
            prev=l[static_cast<std::size_t>(f)];
            if(at<0 && l[static_cast<std::size_t>(f)]>=0.99f*top)at=f+1;
        }
        std::printf("  spawning 100 m off: first frame %.4f of %.4f, at its level after %d frames, worst step %.1f%%\n",
                    first,top,at,100.0f*worst);
        Check(top>0.0f && first<0.05f*top && worst<0.04f && at>=80 && at<=100,
              "a jet newly heard fades in over JetSoundFade (1.5 s), not there at once");
    }
    // Every jet heard by default (JetSoundMostHeard 0): 40 of them, out to 4.9 km (32 entries before: the rest none).
    ResetJetSound();
    Check(config.jetSoundMostHeard==0,"JetSoundMostHeard's default: 0, every jet");
    for(int i=0;i<40;++i)body(jetAt(i),1000.0f+100.0f*static_cast<float>(i));
    frames(300,0,40);
    int heard=0;
    for(int i=0;i<40;++i)heard+=VoicesOf(jetAt(i))>0 ? 1 : 0;
    std::printf("  40 jets 1.0 to 4.9 km off: %d heard, %d voices, %d entries\n",heard,Open(),rowCount);
    Check(heard==40 && rowCount==40,"40 jets: every one heard, an entry each");
    // A cap asked for (JetSoundMostHeard 2): the two nearest of four heard, the two far ones not.
    ResetJetSound();
    config.jetSoundMostHeard=2;
    body(jetAt(0),200.0f);body(jetAt(1),400.0f);body(jetAt(2),800.0f);body(jetAt(3),1600.0f);
    frames(600,0,4);
    Check(VoicesOf(jetAt(0))>0 && VoicesOf(jetAt(1))>0 && VoicesOf(jetAt(2))==0 && VoicesOf(jetAt(3))==0,
          "JetSoundMostHeard 2: the two nearest of four heard, the two far ones not");
    // One newly near takes a far one's place: the newcomer fades in, the one it outranks fades out (not cut).
    body(jetAt(4),100.0f);
    frames(30,0,5);
    const bool fading=VoicesOf(jetAt(1))>0 && VoicesOf(jetAt(4))>0 && VoicesOf(jetAt(0))>0;
    frames(300,0,5);
    Check(fading && VoicesOf(jetAt(4))>0 && VoicesOf(jetAt(0))>0 && VoicesOf(jetAt(1))==0,
          "a jet newly near outranks the farther one heard: it fades in as that one fades out");
    config.jetSoundMostHeard=0;
    // The voice pool full (16 voices for 20 jets): the nearest heard, as many as fit; one newly near is heard in a far
    // one's place, never past the pool.
    ResetJetSound();
    pool=16;
    for(int i=0;i<20;++i)body(jetAt(i),1000.0f+100.0f*static_cast<float>(i));
    int most=0;
    for(int f=0;f<300;++f){frames(1,0,20);most=Open()>most ? Open() : most;}
    heard=0;
    for(int i=0;i<20;++i)heard+=VoicesOf(jetAt(i))>0 ? 1 : 0;
    std::printf("  20 jets, 16 voices: %d heard, at most %d voices open\n",heard,most);
    Check(heard>0 && heard<20 && VoicesOf(jetAt(0))>0 && VoicesOf(jetAt(19))==0 && most<=16,
          "the voice pool full: the nearest jets heard, the farthest not, the pool never passed");
    body(jetAt(20),150.0f);
    for(int f=0;f<300;++f){frames(1,0,21);most=Open()>most ? Open() : most;}
    Check(VoicesOf(jetAt(20))>0 && most<=16,"...a jet newly near heard in a far one's place");
    pool=256;
    // A jet gone 2 km off: its sound still on its way (5.9 s) heard on, then faded; not cut when it went.
    ResetJetSound();
    body(jetAt(0),2000.0f);
    frames(700,0,1);
    const float before=Loud();
    int closedAt=-1;float at4s=0.0f;
    for(int f=1;f<=700 && closedAt<0;++f) {
        frames(1,0,0);
        if(f==250)at4s=Loud();
        if(!Open())closedAt=f;
    }
    std::printf("  gone 2 km off: %.4f before, %.4f 4 s after, closed after %d frames\n",before,at4s,closedAt);
    Check(before>0.0f && at4s>0.9f*before && closedAt>=368 && closedAt<=480,
          "a jet gone 2 km off is heard until its last sound reaches the camera (5.9 s), then fades out");
    // Past the AI event's max distance (3 km): 1/d on and away to nothing by JetSoundFar; 0 holds it (as before).
    ResetJetSound();
    body(jetAt(0),8500.0f);
    frames(700,0,1);
    const int farOpen=Open();
    ResetJetSound();
    config.jetSoundFar=0.0f;
    frames(700,0,1);
    const int heldOpen=Open();
    config.jetSoundFar=8000.0f;
    Check(farOpen==0 && heldOpen>0,"a jet 8.5 km off: unheard past JetSoundFar 8000; heard at its 3 km level with it 0");

    // The passby (jet_air.h): a jet flying past the camera 50 m off at 250 m/s whooshes once a pass, ducking the
    // engines a moment; none with JetSoundPassby off; at most 3 at once.
    auto pass=[&](int count,float lateral,int n) {
        for(int i=0;i<count;++i){body(jetAt(i),-900.0f);Put<float>(jetAt(i),kPosition+8,lateral+10.0f*static_cast<float>(i));}
        float x=-900.0f,leastDuck=1.0f;int most=0;
        for(int f=0;f<n;++f) {
            x+=250.0f/60.0f;
            for(int i=0;i<count;++i)Put<float>(jetAt(i),kPosition,x);
            frames(1,0,count);
            int on=0;for(const Passby& b:passbys)on+=b.on ? 1 : 0;
            most=on>most ? on : most;
            const float d=PassbyDuck();
            leastDuck=d<leastDuck ? d : leastDuck;
        }
        return std::pair<int,float>{most,leastDuck};
    };
    const int medClip=wt::k_jet_passby_med;
    ResetJetSound();
    config.jetSoundPassby=true;
    oncesOf[medClip]=0;
    auto [most1,duck1]=pass(1,50.0f,900);
    std::printf("  a jet 50 m by at 250 m/s: %d passby(s), the engines ducked to %.3f\n",oncesOf[medClip],duck1);
    Check(oncesOf[medClip]==1 && most1==1 && duck1<0.1f,"a jet flying by 50 m off: one passby, the other engines ducked");
    Check(OpenOf(wt::k_jet_passby_close)==0 && OpenOf(medClip)==0,"...its voices closed once played out");
    ResetJetSound();
    oncesOf[medClip]=0;
    pass(1,600.0f,900);
    Check(oncesOf[medClip]==0,"a jet going by 600 m off: no passby (WT's 300 m)");
    ResetJetSound();
    oncesOf[medClip]=0;
    auto [most4,duck4]=pass(4,50.0f,900);
    Check(oncesOf[medClip]>=3 && most4==jetair::kMostPassbys,"four jets by at once: at most 3 passbys (WT's maxVoices)");
    ResetJetSound();
    config.jetSoundPassbyDuck=false;
    oncesOf[medClip]=0;
    auto [most5,duck5]=pass(1,50.0f,900);
    Check(oncesOf[medClip]==1 && duck5==1.0f,"JetSoundPassbyDuck 0: the passby, no duck");
    config.jetSoundPassbyDuck=true;
    ResetJetSound();
    config.jetSoundPassby=false;
    oncesOf[medClip]=0;
    pass(1,50.0f,900);
    Check(oncesOf[medClip]==0,"JetSoundPassby 0: no passby");
    (void)most5;(void)duck4;
    npcState=false;

    // The wind and the airframe (the own jet): the wind's loops by its speed, switched within a run.
    ResetJetSound();
    config.jetSoundWind=config.jetSoundOverload=true;
    playerState=true;occupied=true;flight={false,true,false,false,1.0f,200.0f};
    body(v,0.0f);
    {
        float z=0.0f;
        for(int i=0;i<300;++i){z+=200.0f/60.0f;Put<float>(v,kPosition+8,z);cam[2]=z;frameOn();}
        const int wind=OpenOf(wt::k_jet_wind_low)+OpenOf(wt::k_jet_wind_high);
        Check(wind>0 && OpenOf(wt::k_jet_overload)==0,"the own jet at 200 m/s: its wind heard, no airframe in level flight");
        config.jetSoundWind=false;
        z+=200.0f/60.0f;Put<float>(v,kPosition+8,z);cam[2]=z;frameOn();
        Check(OpenOf(wt::k_jet_wind_low)+OpenOf(wt::k_jet_wind_high)==0,"JetSoundWind 0: the wind closed at once");
        config.jetSoundWind=true;
        cam[2]=0.0f;Put<float>(v,kPosition+8,0.0f);
    }
    Check(jetair::OverloadAt(1.0f)==0.0f && jetair::OverloadAt(6.0f)>0.0f && jetair::OverloadAt(9.0f)>jetair::OverloadAt(6.0f),
          "the airframe: none at 1 g, louder the more g");
    const jetair::Wind slow=jetair::WindAt(15.0f,600.0f),quick=jetair::WindAt(95.0f,900.0f);
    Check(slow.low>0.0f && slow.high==0.0f && quick.high>quick.low && quick.high>slow.low,
          "the wind: wind_low slow, wind_hi_4 fast and louder");
    ResetJetSound();
    Check(Open()==0,"mission reset closes the air's voices too");
    config.jetSoundPassby=config.jetSoundWind=config.jetSoundOverload=false;
    playerState=false;occupied=false;

    const auto fast=jetsound::For({true,true,false,false,1.0f,150.0f});
    const auto taxi=jetsound::For({true,true,false,false,0.1f,3.0f});
    Check(fast.power>taxi.power && fast.power==1.0f,"engine load still follows throttle and flight speed");
    // A fault: the filter only decides, the voices are dealt with after the stack unwinds (never inside the call that
    // faulted). One in the game's memory: the sound voices are closed.
    ResetJetSound();broken=false;npcState=false;playerState=true;occupied=true;flight={false,true,false,false,0.5f,100.0f};
    for(int i=0;i<120;++i)frameOn();
    const int closedBefore=closes,openBefore=Open();
    playerState=false;jetFaults=true;nowMs+=16;++nowFrame;JetSound(v);jetFaults=false;
    Check(broken && openBefore>0 && closes==closedBefore+openBefore && closesBeforeUnwind==0 && !rows[0]->s.ref,
          "a game-memory fault closes the voices after unwinding, not in the filter");
    // One inside a voice call: no voice is called into again, neither in the filter nor after.
    broken=false;playerState=true;for(int i=0;i<120;++i)frameOn();
    const int closedThen=closes,openedThen=opens;
    setFaults=true;frameOn();setFaults=false;
    Check(broken && closes==closedThen && closesBeforeUnwind==0 && !rows[0]->s.ref && !inAudio,
          "a fault inside a voice call lets the voices go untouched");
    frameOn();
    Check(closes==closedThen && opens==openedThen,"jet sound stays off after a fault");
    std::printf("%d failures; %d voices opened, %d closed, %d sets, %d one-shots\n",failures,opens,closes,sets,shots);
    return failures ? 1 : 0;
}
