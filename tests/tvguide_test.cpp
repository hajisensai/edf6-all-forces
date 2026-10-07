// Production TV lifecycle with real seat/player layout checks and protected missile memory. No EDF.dll or game.
// Link edf6common; run even with no vehicle frames. The clock follows the same pause-aware rule as crew.cpp.
#include "../src/tvguide.cpp"
#include "../src/game_clock.h"
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
bool online=false,paused=false;
std::uint64_t wall=1000;
gameclock::Clock clock{};
unsigned char owner[0x400]{},other[0x400]{},ownerCtrl[16]{},otherCtrl[16]{},roundCtrl[16]{},entry[0x30]{};
unsigned char* missile=nullptr;
unsigned char* currentPlayer=owner;
int cases=0,failed=0;
void Check(bool ok,const char* message) noexcept {
    ++cases;
    if(!ok){++failed;std::printf("FAIL %s\n",message);}
}
void Human(unsigned char* h,void* ctrl) noexcept {
    std::memset(h,0,0x400);Put<void*>(h,kSelfCtrl,ctrl);
    Put<void*>(h,edf::kHumanPad,h);h[edf::kHumanPlayer]=1;
}
void Round(unsigned char* b,void* ctrl,int age=20) noexcept {
    std::memset(b,0,0x1500);
    Put<const void*>(b,0,image+kVtable);Put<void*>(b,kSelfCtrl,ctrl);
    Put<std::uint32_t>(b,kDelay,kTempestDelay);Put<std::int32_t>(b,kFlown,age);
    Put<const void*>(b,kLock,entry);Put<const void*>(entry,kEntryOwner,owner);
    Put<float>(b,kTurn,0.03f);Put<float>(b,kTop,1.0f);Put<float>(b,kAccel,0.03f);Put<float>(b,kOwn+8,1.0f);
}
void Prepare(int age=20) noexcept {
    ResetTv();config=Config{};online=paused=false;wall=1000;clock=gameclock::Clock{};currentPlayer=owner;
    Human(owner,ownerCtrl);Human(other,otherCtrl);Round(missile,roundCtrl,age);
}
void Start(int age=20) noexcept {
    Prepare(age);Check(TvSteer(missile),"offline live player's Tempest acquired");
}
bool ViewShown() noexcept {
    const void* h=nullptr;float eye[3],look[3];
    return TvView(&h,eye,look) && h==owner;
}
// The missile's real homing condition, used by both stock and PN: a nonzero extra delay is strictly exceeded.
bool GateOpen(const unsigned char* b,unsigned age) noexcept {
    const unsigned delay=At<unsigned>(b,kDelay),extra=At<unsigned>(b,kExtra);
    return age>=delay && (!extra || age-delay>extra);
}
bool GuardedFrame(unsigned char* h,const TvInput& in,bool* held) noexcept {
    __try {*held=TvFrame(h,false,in);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Lifetime() noexcept {
    Start();TvInput in{};in.front=true;
    // The soldier keeps updating while no vehicles and no missile callback update the old GameFrame clock.
    for(int i=0;i<20;++i){wall+=16;TvFrame(owner,false,in);}
    Check(!TvHoldsKeys() && !tv.active && !ViewShown(),"missile disappearance releases an on-foot player without vehicle ticks");
    Check(At<unsigned>(missile,kDelay)==kTvHomingDelay,"soldier timeout did not write through a missile pointer");
    Check(!TvSteer(missile) && GateOpen(missile,20),"late live missile callback safely consumes timeout release");

    Start();DWORD previous=0;
    const bool protectedPage=VirtualProtect(missile,8192,PAGE_NOACCESS,&previous)!=FALSE;
    Check(protectedPage,"protect missile memory after last update");
    if(protectedPage) {
        bool held=false;
        in.rx=1.0f;wall+=16;
        Check(GuardedFrame(owner,in,&held) && held,"fresh input uses cached turn even when missile address is inaccessible");
        in.leave=true;
        Check(GuardedFrame(owner,in,&held) && held && !tv.active,"Esc ends view without touching inaccessible missile memory");
        in.leave=false;
        Check(GuardedFrame(owner,in,&held) && !held && !TvHoldsKeys(),"Esc hold drains on release without missile access");
        DWORD ignored=0;VirtualProtect(missile,8192,previous,&ignored);
        Check(At<unsigned>(missile,kDelay)==kTvHomingDelay,"Esc did not restore the gate from the input callback");
        Check(!TvSteer(missile) && GateOpen(missile,20),"release is performed only in the live missile callback");
    }
    Start();in=TvInput{};in.fire=true;
    TvFrame(owner,false,in);wall+=200;
    Check(TvFrame(owner,false,in) && !tv.active,"a held boost button drains after missile timeout");
    Check(!TvFrame(other,false,in),"second local player does not inherit timeout hold");
    in.fire=false;Check(!TvFrame(owner,false,in) && !TvHoldsKeys(),"timeout hold released when fire is released");
}
void EarlyReturn() noexcept {
    for(const int age:{0,20,59}) {
        Start(age);Put<unsigned>(missile,kExtra,45);
        TvInput in{};in.leave=true;
        Check(TvFrame(owner,false,in) && !ViewShown(),"Esc immediately ends view and keeps only its key hold");
        Check(At<unsigned>(missile,kDelay)==kTvHomingDelay,"early return queues no raw-pointer write");
        Check(!TvSteer(missile) && GateOpen(missile,static_cast<unsigned>(age)),"early return opens delay and extra gates at current age");
        Check(!TvSteer(missile),"a handed-back round is not acquired again");
    }
    Start();TvInput in{};
    Check(!TvFrame(owner,true,in) && !TvHoldsKeys() && !ViewShown(),"map takes view without a TV input hold");
    Check(!TvSteer(missile) && GateOpen(missile,20),"map cancellation restores laser at missile callback");
}
void PauseAndReset() noexcept {
    Start();TvInput in{};paused=true;
    for(int i=0;i<120;++i){wall+=1000;Check(ViewShown(),"pause retains TV camera past wall-clock timeout");}
    Check(TvFrame(owner,false,in) && TvHoldsKeys(),"paused game clock does not expire session");
    paused=false;wall+=16;
    Check(ViewShown() && TvFrame(owner,false,in),"resume keeps current camera and controls");
    ResetTv();
    Check(!TvHoldsKeys() && !ViewShown() && !TvFrame(owner,false,in),"mission reset immediately drops view and input state");
    Check(At<unsigned>(missile,kDelay)==kTvHomingDelay,"mission reset never touches old missile memory");
}
void Availability() noexcept {
    TvInput in{};
    Start();owner[kDead]=1;
    Check(!TvFrame(owner,false,in) && !TvHoldsKeys() && !ViewShown(),"player death releases view and controls");
    Check(!TvSteer(missile) && GateOpen(missile,20),"dead owner's live missile returns to laser");
    Prepare();owner[kDead]=1;
    Check(!TvSteer(missile) && At<unsigned>(missile,kDelay)==720,"dead player cannot start TV");
    Start();Put<void*>(owner,kSelfCtrl,otherCtrl);
    Check(!TvFrame(owner,false,in) && !TvHoldsKeys(),"reused soldier address ends the old identity's session");
    Check(!TvSteer(missile) && GateOpen(missile,20),"identity change restores live missile at callback");
    Start();owner[edf::kHumanPlayer]=0;
    Check(!TvFrame(owner,false,in) && !ViewShown(),"loss of local player control ends TV");
    for(int option=0;option<2;++option) {
        Start();if(option==0)config.enabled=false;else config.tempestTv=false;
        Check(!TvFrame(owner,false,in) && !TvHoldsKeys(),"disabled setting releases soldier immediately");
        Check(!TvSteer(missile) && GateOpen(missile,20),"disabled setting still allows release in missile callback");
    }
    Prepare();online=true;
    Check(!TvSteer(missile) && !TvHoldsKeys() && !ViewShown() && At<unsigned>(missile,kDelay)==720 && At<float>(missile,kTop)==1.0f,
          "online Tempest is untouched for stock or PN laser guidance");
    Start();online=true;
    Check(!TvSteer(missile) && !TvHoldsKeys() && GateOpen(missile,20),"entering online during TV returns the owned round");
    Prepare();Put<unsigned>(missile,kDelay,1000000);Put<unsigned>(missile,kExtra,4242);
    Check(!TvSteer(missile) && At<unsigned>(missile,kDelay)==1000000 && At<unsigned>(missile,kExtra)==4242,
          "ordinary plugin missile sentinel is never treated as a TV release");
}
void OverlapAndBoost() noexcept {
    Start();TvInput in{};in.leave=true;
    TvFrame(owner,false,in);in.leave=false;TvFrame(owner,false,in);
    unsigned char next[0x1500]{},nextCtrl[16]{};Round(next,nextCtrl);
    Check(TvSteer(next),"new TV can start before an old released missile returns");
    Check(!TvSteer(missile) && GateOpen(missile,20) && tv.active && tv.round.obj==next,
          "late callback restores old missile without ending the new session");
    ResetTv();Start();in=TvInput{};in.fire=true;
    TvFrame(owner,false,in);TvSteer(missile);
    Check(At<float>(missile,kTop)==1.0f,"launch button must first be released before boosting");
    in.fire=false;TvFrame(owner,false,in);in.fire=true;TvFrame(owner,false,in);TvSteer(missile);
    Check(At<float>(missile,kTop)==3.0f,"fire applies one boost after release");
    TvSteer(missile);Check(At<float>(missile,kTop)==3.0f,"boost does not multiply repeatedly");
    in.leave=true;TvFrame(owner,false,in);TvSteer(missile);
    Check(At<float>(missile,kTop)==3.0f && GateOpen(missile,20),"laser handback preserves the irreversible boost");
}
}  // namespace
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return gameclock::Read(clock,wall,paused); }
ULONGLONG GameFrame() noexcept { return 100; } // deliberately never advanced: on-foot missions are supported
unsigned char* PlayerHuman() noexcept { return currentPlayer; }
bool InSession() noexcept { return online; }
void Log(const char*,...) noexcept {}
}  // namespace crew
int main() {
    using namespace crew;
    // Matches reads real readable bytes, but the look-to signature is absent, so no game function is ever called.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    missile=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image || !missile)return 2;
    Lifetime();EarlyReturn();PauseAndReset();Availability();OverlapAndBoost();
    std::printf("tvguide_test: %d checks, %d failed\n",cases,failed);
    VirtualFree(missile,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    return failed ? 1 : 0;
}
