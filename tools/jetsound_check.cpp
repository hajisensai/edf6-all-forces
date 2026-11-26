// Production sound dispatch with stand-in vehicle memory and recording audio voices.
// No game process, audio device or installed files are used.
#include "../src/jetsound.cpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures=0,opens=0,closes=0,sets=0,shots=0;
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
bool playerState=false,npcState=false,occupied=false;
crew::jet::Jet npc{};
// The voices open (by loop index) and what each was last set to: the engine's level is their sum.
bool used[256]{};
crew::audio::Heard lastSet[256]{};
float Loud() noexcept { float g=0.0f;for(int i=0;i<256;++i)if(used[i])g+=lastSet[i].gain;return g; }
int Open() noexcept { int n=0;for(bool u:used)n+=u ? 1 : 0;return n; }
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
}
namespace jet {
Jet* FindJet(const unsigned char*) noexcept {if(jetFaults)RaiseFault();return npcState ? &npc : nullptr;}
}
namespace audio {
bool Start() noexcept {return true;}
int OpenWtLoop(int) noexcept {
    for(int i=0;i<256;++i)if(!used[i]){used[i]=true;lastSet[i]=Heard{};++opens;return i;}
    return -1;
}
void CloseLoop(int i) noexcept {if(i<0 || !used[i])return;used[i]=false;++closes;if(faultPending)++closesBeforeUnwind;}
void SetLoop(int i,const Heard& h) noexcept {if(setFaults)RaiseFault();++sets;if(i>=0)lastSet[i]=h;}
void PlayWt(int,int,const Heard&) noexcept {++shots;}
bool WtThere(int) noexcept {return true;}
int LoopsOpen() noexcept {return ::Open();}
void Beat(float) noexcept {}
}
}
int main() {
    using namespace crew;
    std::vector<unsigned char> fakeImage(0x20B3000),vehicle(0x3000),seat(kSeatStride);
    image=fakeImage.data();
    auto* v=vehicle.data();
    Put<void*>(v,kSelfCtrl,seat.data());Put<void*>(v,kSeats,seat.data());
    Put<float>(v,kMatrix,1.0f);Put<float>(v,kMatrix+20,1.0f);Put<float>(v,kMatrix+40,1.0f);
    sigOk=true;hasListener=true;
    listener[4]=listener[9]=listener[14]=listener[19]=1.0f;
    JetSound(v);
    Check(opens==0,"empty parked display does not allocate or play a voice");
    // Merely sliding or falling while empty is not an engine command.
    Put<float>(v,kPosition,100.0f);++nowFrame;nowMs+=16;JetSound(v);
    Check(opens==0,"empty displaced aircraft remains silent");
    auto frameOn=[&]{++nowFrame;nowMs+=16;JetSound(v);};
    auto rpm=[]{ return sounds[0].heard.v[engine::kRpm]; };
    occupied=true;playerState=true;flight={false,true,true,false,0.0f,0.0f};
    for(int i=0;i<30;++i)frameOn();
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
    Check(std::fabs(rpm()-engine::kJetWep)<1.0f && sounds[0].spool.wep>0.99f && shots>0,
          "full throttle lights the afterburner: the top rpm, its layer, the ignition's one-shot");
    std::printf("  idle %.0f rpm level %.3f; afterburner %.0f rpm level %.3f, %d voices\n",idleRpm,idle,rpm(),Loud(),Open());
    flight={false,true,true,true,0.0f,0.0f};frameOn();
    occupied=false;frameOn();
    Check(Open()==0,"getting out of a parked aircraft closes its voices");
    playerState=false;npcState=true;npc.role=jet::Role::carrier;
    for(int i=0;i<60;++i){npc.lastStep=nowMs+16;frameOn();}
    Check(Open()>0 && rpm()>engine::AiRpm(engine::kJetIdle)+300.0f,"NPC carrier hover without a local player remains audible");
    const float npcRpm=rpm();
    npc.m.power=0.9f;
    for(int i=0;i<300;++i){npc.lastStep=nowMs+16;frameOn();}
    Check(rpm()>npcRpm,"an NPC rotor craft's engine follows its Hover power");
    npc.m.power=0.0f;npc.lastStep=nowMs;JetSound(v);
    nowMs+=201;++nowFrame;JetSound(v);
    Check(Open()==0,"retained stale NPC record cannot keep an engine running");
    npc.lastStep=nowMs+16;frameOn();
    const int beforeDead=Open();
    v[kDead]=1;frameOn();
    Check(beforeDead>0 && Open()==0,"destroyed aircraft closes immediately");
    v[kDead]=0;npc.lastStep=nowMs+16;frameOn();
    nowMs+=201;++nowFrame;Tick();
    Check(Open()==0,"deleted or no-longer-updated aircraft is reaped");
    npc.lastStep=nowMs+16;frameOn();ResetJetSound();
    Check(Open()==0,"mission reset releases the remaining aircraft voices");
    // Another player's jet online: no flight record here, someone in it: heard from its motion alone.
    hasListener=true;   // (the reset let the camera go: the next frame's Tick places it again)
    npcState=false;playerState=false;occupied=true;
    for(int i=0;i<60;++i){Put<float>(v,kPosition+8,static_cast<float>(i)*3.0f);frameOn();}
    Check(Open()>0,"another player's moving jet is heard with no flight record of ours");
    occupied=false;frameOn();
    Check(Open()==0,"...and silent once nobody is in it");
    ResetJetSound();
    const auto fast=jetsound::For({true,true,false,false,1.0f,150.0f});
    const auto taxi=jetsound::For({true,true,false,false,0.1f,3.0f});
    Check(fast.power>taxi.power && fast.power==1.0f,"engine load still follows throttle and flight speed");
    // A fault: the filter only decides, the voices are dealt with after the stack unwinds (never inside the call that
    // faulted). One in the game's memory: the sound voices are closed.
    ResetJetSound();hasListener=true;broken=false;npcState=false;playerState=true;occupied=true;flight={false,true,false,false,0.5f,100.0f};
    for(int i=0;i<10;++i){nowMs+=16;++nowFrame;JetSound(v);}
    const int closedBefore=closes,openBefore=Open();
    playerState=false;jetFaults=true;nowMs+=16;++nowFrame;JetSound(v);jetFaults=false;
    Check(broken && openBefore>0 && closes==closedBefore+openBefore && closesBeforeUnwind==0 && !sounds[0].ref,
          "a game-memory fault closes the voices after unwinding, not in the filter");
    // One inside a voice call: no voice is called into again, neither in the filter nor after.
    broken=false;playerState=true;for(int i=0;i<10;++i){nowMs+=16;++nowFrame;JetSound(v);}
    const int closedThen=closes,openedThen=opens;
    setFaults=true;nowMs+=16;++nowFrame;JetSound(v);setFaults=false;
    Check(broken && closes==closedThen && closesBeforeUnwind==0 && !sounds[0].ref && !inAudio,
          "a fault inside a voice call lets the voices go untouched");
    nowMs+=16;++nowFrame;JetSound(v);Tick();
    Check(closes==closedThen && opens==openedThen,"jet sound stays off after a fault");
    std::printf("%d failures; %d voices opened, %d closed, %d sets, %d one-shots\n",failures,opens,closes,sets,shots);
    return failures ? 1 : 0;
}
