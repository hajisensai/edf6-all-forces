// src/step_fresh.h: handoff freshness by the scene step (docs/step-freshness.md). A publisher stamps Now(); a reader takes
// the data while the stamp is within `lag` steps. Checked here: a slow frame (a step longer than any old wall window) does
// not age it, a publisher that stopped does within kEveryStep, the draw-thread lag (kFromDraw), a stamp read ahead of
// the reader's Now(), "never published", and a new mission making every old stamp stale.
#include "step_fresh.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
int fails=0,checks=0;
void Check(bool ok,const char* what) {++checks;if(!ok){++fails;std::printf("FAIL: %s\n",what);}}
}  // namespace

int main() {
    using namespace stepfresh;
    Check(Now()==1,"before the first SeeFrame: the plugin's first step, never the 0 of never published");
    Check(Fresh(Now()),"a stamp taken before the first SeeFrame is fresh");
    Check(!Fresh(0),"0 is never published");
    Check(!Fresh(Step{0},Step{0},kEveryStep),"0 is never published, whatever the step");

    // Every-step publisher, the reader before or after it in the same step.
    See(10);
    const Step at=Now();
    Check(at==10,"Now() is the step SeeFrame saw");
    Check(Fresh(at),"read in the step it was published");
    // A slow frame: 600 ms of wall time between two steps (the old 200..500 ms windows dropped it).
    Sleep(600);
    See(11);
    Check(Fresh(at),"one step later, after 600 ms of wall time: still fresh");
    See(12);
    Check(Fresh(at),"kEveryStep steps later: still fresh (a publisher skipped once)");
    See(13);
    Check(!Fresh(at),"kEveryStep+1 steps without a publish: stale (the publisher stopped)");

    // Up to 3 steps a frame: data the draw published once a frame, read by the game thread in that frame's steps.
    See(20);
    const Step drawn=Now();
    See(23);
    Check(Fresh(drawn,kFromDraw),"draw -> game thread: 3 steps on, fresh");
    See(24);
    Check(Fresh(drawn,kFromDraw),"draw -> game thread: kFromDraw steps on, fresh");
    See(25);
    Check(!Fresh(drawn,kFromDraw),"draw -> game thread: a frame not drawn at all and more, stale");

    // A frame-pacing n = 0 frame (no step): the draw keeps the last step's data however many frames pass.
    See(30);
    const Step last=Now();
    for(int frame=0;frame<100;++frame)Check(Fresh(last),"n = 0 frames: the last step's data stays");

    // A stamp ahead of the reader's own Now() (the reader loaded it before the publisher's step reached it).
    Check(Fresh(Step{31},Step{30},kEveryStep),"a stamp ahead of now is the newest data");

    // Another thread (the draw) sees the game thread's step.
    std::atomic<Step> seen{0};
    See(40);
    std::thread([&]{seen.store(Now());}).join();
    Check(seen.load()==Now(),"another thread reads the same step");

    // The pause menu: System::update increments +CE0 (GameFrame) before its pause test skips the scene, so the first
    // traversal after a 10 s pause sees GameFrame 600 on. One step: a readout published before the pause is fresh after.
    {
        Traversals t;
        See(t.Observe(100,false));
        const Step beforePause=Now();
        See(t.Observe(700,false));
        Check(Now()==beforePause+1,"the steps the pause skipped are not steps");
        Check(Fresh(beforePause),"a readout published just before the pause is fresh after it");
        See(t.Observe(701,true));
        Check(Now()==beforePause+1,"a move seen while paused is not a step");
        See(t.Observe(701,false));See(t.Observe(701,false));
        Check(Now()==beforePause+1,"many SeeFrame callers in one traversal: one step");
        See(t.Observe(702,false));See(t.Observe(703,false));
        Check(!Fresh(beforePause),"kEveryStep+1 traversals that ran without a publish: stale");
    }

    // A new mission: the step clock restarts (ResetCrew resets frameClock) yet every old stamp is stale.
    See(5000);
    const Step old=Now();
    NewMission(0);
    Check(!Fresh(old),"after NewMission an old stamp is stale");
    See(1);
    Check(!Fresh(old),"the new mission's first steps: still stale (logical restarted low)");
    const Step fresh=Now();
    Check(Fresh(fresh),"the new mission's own stamps are fresh");
    Check(fresh>old,"new-mission steps come after every old one");

    std::printf("step_fresh: %d checks, %d failed\n",checks,fails);
    return fails ? 1 : 0;
}
