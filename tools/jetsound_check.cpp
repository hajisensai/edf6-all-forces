// Production sound dispatch with stand-in vehicle memory and recording audio voices.
// No game process, audio device or installed files are used.
#include "../src/jetsound.cpp"
#include <cstdio>
#include <vector>

namespace {
int failures=0,opens=0,closes=0,sets=0;
ULONGLONG nowMs=1000,nowFrame=60;
crew::Config config{};
crew::jetsound::State flight{};
bool playerState=false,npcState=false,occupied=false;
crew::jet::Jet npc{};
crew::audio::Mix lastMix{};
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
Jet* FindJet(const unsigned char*) noexcept {return npcState ? &npc : nullptr;}
}
namespace audio {
bool Start() noexcept {return true;}
int Open() noexcept {return opens++;}
void Close(int slot) noexcept {if(slot>=0)++closes;}
void Set(int,const Mix& mix) noexcept {++sets;lastMix=mix;}
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
    occupied=true;playerState=true;flight={false,true,true,false,0.0f,0.0f};
    JetSound(v);const float idle=lastMix.roarL+lastMix.roarR;
    Check(opens==1 && idle>0.0f,"occupied parked aircraft has quiet idle");
    flight={false,true,false,true,0.0f,0.0f};JetSound(v);
    Check(opens==1 && lastMix.roarL+lastMix.roarR>idle*4.0f,"zero-speed powered hover remains audible");
    occupied=false;flight={false,true,true,true,0.0f,0.0f};JetSound(v);
    Check(closes==1,"getting out of a parked aircraft closes its voices");
    playerState=false;npcState=true;npc.role=jet::Role::carrier;npc.lastStep=nowMs;
    JetSound(v);
    Check(opens==2 && lastMix.roarL+lastMix.roarR>idle*4.0f,"NPC carrier hover without a local player remains audible");
    nowMs+=201;++nowFrame;JetSound(v);
    Check(closes==2,"retained stale NPC record cannot keep an engine running");
    npc.lastStep=nowMs;JetSound(v);v[kDead]=1;JetSound(v);
    Check(closes==3,"destroyed aircraft closes immediately");
    v[kDead]=0;JetSound(v);nowMs+=201;++nowFrame;Tick();
    Check(closes==4,"deleted or no-longer-updated aircraft is reaped");
    nowMs+=16;npc.lastStep=nowMs;JetSound(v);ResetJetSound();
    Check(closes==5,"mission reset releases the remaining aircraft voices");
    const auto fast=jetsound::For({true,true,false,false,1.0f,150.0f});
    const auto taxi=jetsound::For({true,true,false,false,0.1f,3.0f});
    Check(fast.power>taxi.power && fast.power==1.0f,"engine load still follows throttle and flight speed");
    std::printf("%d failures; %d voices opened, %d closed, %d mixes\n",failures,opens,closes,sets);
    return failures ? 1 : 0;
}
