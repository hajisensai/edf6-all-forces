// The game clock through the pause menu (src/game_clock.h, as crew.cpp GameMs runs it), without the game
// (docs/hud-re.md §10). The frame order is the game's: every frame the vehicle's input reads the clock (the turret
// camera's seat seen, turretcam.cpp shared.seenMs / aimMs) and then the riding camera's step reads it again
// (turretcam.cpp Camera: live = now - seenMs <= 200 ms, aimed = now - aimMs <= 150 ms; aimed lost hands the camera back
// to the stock seat view, live lost drops it at once). The pause menu stops the vehicles' input, not the camera step
// (the System's update steps the viewport cameras on its paused path too, 0x119953B).
//  - old: the rule before (a gap longer than 250 ms is one 16 ms frame, no pause flag): the camera step's reads 16 ms
//    apart keep the clock running, the turret camera lets go a few frames into the pause (the view swings away under
//    the menu and back on resume: "暂停以后hud会飞走");
//  - new: the clock stops while the game says it is paused: the camera keeps its view, the first frame after the pause
//    steps one frame. A load (no reads at all for 2 s) is one 16 ms frame in both.
// Exit code 1 when the new rule does not hold.
// Built on request only: cmake --build build --target pause_clock_check && build\pause_clock_check.exe
#include "../src/game_clock.h"
#include <cstdint>
#include <cstdio>

namespace {
constexpr std::uint64_t kFresh=200,kAimFresh=150,kFrame=16;
constexpr int kBefore=30,kPaused=180,kAfter=5;   // frames: 0.5 s of play, a 3 s pause, then play again

struct Result { int aimLostAt,liveLostAt; std::uint64_t pausedFor,resumeStep,loadStep; };

// One run; `flag`: the clock is told about the pause (the new rule), else it never is (the old).
Result Run(bool flag) noexcept {
    gameclock::Clock c;
    std::uint64_t wall=100000,seen=0,aim=0,last=0,pausedAt=0;
    Result r{-1,-1,0,0,0};
    for(int f=0;f<kBefore;++f,wall+=kFrame) {
        last=seen;
        seen=aim=gameclock::Read(c,wall,false);     // the vehicle's input
        pausedAt=gameclock::Read(c,wall,false);     // the camera's step
    }
    for(int f=0;f<kPaused;++f,wall+=kFrame) {   // only the camera steps
        const std::uint64_t now=gameclock::Read(c,wall,flag);
        if(r.aimLostAt<0 && now-aim>kAimFresh)r.aimLostAt=f;
        if(r.liveLostAt<0 && now-seen>kFresh)r.liveLostAt=f;
        r.pausedFor=now-pausedAt;
    }
    for(int f=0;f<kAfter;++f,wall+=kFrame) {
        last=seen;
        seen=aim=gameclock::Read(c,wall,false);
        gameclock::Read(c,wall,false);
        if(f==0)r.resumeStep=seen-last;
    }
    wall+=2000;   // a load: nobody reads the clock
    r.loadStep=gameclock::Read(c,wall,false)-seen;
    return r;
}

void Print(const char* what,const Result& r) noexcept {
    std::printf("%s: game clock moved %llu ms over a %d ms pause; turret camera aim lost at pause frame index %d (%d ms in), "
                "seat lost at %d; first frame after it steps %llu ms; a 2 s load steps %llu ms\n",what,
                static_cast<unsigned long long>(r.pausedFor),kPaused*static_cast<int>(kFrame),r.aimLostAt,
                r.aimLostAt<0 ? -1 : (r.aimLostAt+1)*static_cast<int>(kFrame),r.liveLostAt,
                static_cast<unsigned long long>(r.resumeStep),static_cast<unsigned long long>(r.loadStep));
}
}  // namespace

int main() {
    const Result old=Run(false),now=Run(true);
    Print("old",old);
    Print("new",now);
    bool ok=true;
    if(now.pausedFor!=0){std::printf("FAIL the clock moved while paused\n");ok=false;}
    if(now.aimLostAt>=0 || now.liveLostAt>=0){std::printf("FAIL the turret camera let go during the pause\n");ok=false;}
    if(now.resumeStep!=kFrame){std::printf("FAIL the first frame after the pause is not one frame\n");ok=false;}
    if(now.loadStep!=gameclock::kStepMs){std::printf("FAIL a load is not one frame\n");ok=false;}
    if(old.aimLostAt<0){std::printf("FAIL the stand-in of the old rule does not lose the camera (the check proves nothing)\n");ok=false;}
    std::printf(ok ? "pause_clock_check: ok\n" : "pause_clock_check: FAILED\n");
    return ok ? 0 : 1;
}
