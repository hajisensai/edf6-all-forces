// The performance panel's means (src/perf_hud.h Means): the step's phases a step, the draw parts and the frame a frame,
// the rest never negative, nothing divided by zero.
#include "perf_hud.h"

#include <cmath>
#include <cstdio>

namespace {
int failures=0;
void Near(double got,double want,const char* what) {
    if(std::fabs(got-want)>1e-9){std::printf("FAIL %s: %.6f, want %.6f\n",what,got,want);++failures;}
}
}  // namespace

int main() {
    using namespace crew::perfhud;
    {   // 10 frames of 20 ms (1 tick = 1 ms), 30 steps; each step 5 ms in its phases; a frame 2 ms in its draw parts
        Period p{};
        p.frames=10;p.steps=30;p.frameSum=200;p.frameWorst=35;
        p.scope[kAi]=30;p.scope[kPre]=60;p.scope[kHavok]=15;p.scope[kObject]=30;p.scope[kCamera]=15;
        p.scope[kSafe]=5;p.scope[kRender]=15;
        const Readout r=Means(p,1.0);
        Near(r.frameMs,20,"frame");Near(r.worstMs,35,"worst");Near(r.fps,50,"fps");Near(r.stepsPerFrame,3,"steps a frame");
        Near(r.scopeMs[kAi],1,"ai");Near(r.scopeMs[kPre],2,"pre");Near(r.stepMs,5,"step");
        Near(r.scopeMs[kSafe],0.5,"safe");Near(r.scopeMs[kRender],1.5,"render");
        Near(r.restMs,20-3*5-0.5-1.5,"rest");
    }
    {   // the phases over the frame (steps of a frame before the window): the rest is 0, not negative
        Period p{};
        p.frames=1;p.steps=3;p.frameSum=10;p.frameWorst=10;p.scope[kPre]=30;
        Near(Means(p,1.0).restMs,0,"rest clamped");
    }
    {   // nothing yet
        const Readout r=Means(Period{},1.0);
        Near(r.frameMs,0,"no frames");Near(r.stepMs,0,"no steps");Near(r.fps,0,"no fps");
    }
    {   // QPC ticks: 10 MHz
        Period p{};
        p.frames=1;p.frameSum=166667;p.frameWorst=166667;
        Near(Means(p,1e-4).frameMs,16.6667,"ticks to ms");
    }
    if(failures)return 1;
    std::puts("perf hud means ok");
    return 0;
}
