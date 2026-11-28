// The performance panel (ini PerfHud, hot-reloaded; hud.cpp PerfPanel draws it): frame rate, frame time, the scene
// step's phases, the frame's draw parts, the population, the process CPU and memory, on screen in a mission.
//
// The numbers are the game's own profile scopes: each phase of System::update 0x1198DA0 is bracketed by
// 0x11AA830 (begin: [scope] the QPC base, +8 its name, +0x10 QPC - base) and 0x11AA880 (end: +0x18 QPC - base), so
// +0x18 - +0x10 is the phase's time in QPC ticks. perf_probe.cpp chains the end calls (kScopeEndCalls) whatever Debug
// says and hands each here (ScopeEnded, the game thread); every kPublishMs the period's means are published as one
// Readout under a lock the draw thread only tries (Latest). Docs: docs/perf-hud.md.
#pragma once
#include <Windows.h>
#include <cstdint>

namespace crew::perfhud {
// The scopes, in kScopeEndCalls' order (perf_probe.cpp): the step's five phases, then the frame's two draw parts.
enum Scope : unsigned { kAi, kPre, kHavok, kObject, kCamera, kSafe, kRender, kScopes };
constexpr unsigned kStepScopes=5;
constexpr ULONGLONG kPublishMs=500;
constexpr ULONGLONG kFreshMs=2000;   // older: not shown (no steps: a menu, a load)

struct Readout {
    double fps=0,frameMs=0,worstMs=0,stepsPerFrame=0;
    double stepMs=0;                 // the five phases' sum, a step
    double scopeMs[kScopes]{};       // the phases a step, the draw parts a frame
    double restMs=0;                 // a frame less its steps and draw parts (the tick wait, the present, the rest)
    int enemies=-1,friends=-1,team=-1;
    double cpuCores=0;               // the process's CPU over the period, in cores (1.0 = one core busy)
    double commitMb=0;
    ULONGLONG at=0;                  // GetTickCount64 when published
};

// The game thread, after the stock end of scope `index` that took `ticks` QPC ticks.
void ScopeEnded(unsigned index,long long ticks) noexcept;
// perf_probe.cpp at load: whether every scope end took its hook (else the panel shows nothing: its sums would not add up).
void ScopesChained(bool all) noexcept;
// The draw thread: the latest readout when one was published in the last kFreshMs.
bool Latest(Readout* out) noexcept;

// What a period's sums come to (pure; tests/perf_hud_test.cpp).
struct Period {
    long long scope[kScopes]{};
    unsigned steps=0,frames=0;
    long long frameSum=0,frameWorst=0;
};
inline Readout Means(const Period& p,double tickMs) noexcept {
    Readout r{};
    if(p.frames) {
        r.frameMs=static_cast<double>(p.frameSum)*tickMs/p.frames;
        r.worstMs=static_cast<double>(p.frameWorst)*tickMs;
        r.fps=r.frameMs>0 ? 1000.0/r.frameMs : 0;
        r.stepsPerFrame=static_cast<double>(p.steps)/p.frames;
    }
    for(unsigned i=0;i<kScopes;++i) {
        const unsigned n=i<kStepScopes ? p.steps : p.frames;
        r.scopeMs[i]=n ? static_cast<double>(p.scope[i])*tickMs/n : 0;
        if(i<kStepScopes)r.stepMs+=r.scopeMs[i];
    }
    if(p.frames) {
        const double rest=r.frameMs-r.stepsPerFrame*r.stepMs-r.scopeMs[kSafe]-r.scopeMs[kRender];
        r.restMs=rest>0 ? rest : 0;
    }
    return r;
}
}  // namespace crew::perfhud

namespace crew {
// The live objects of a team as the mission scripts count them (crew.cpp TeamObjects); -1 off a mission.
int TeamObjectCount(std::int32_t team) noexcept;
}
