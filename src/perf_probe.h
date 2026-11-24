// The measurement probes (debug only; docs/plan-2026-11-24-optimization.md section 3 A, tools/perf_report.py reads them).
//
// The plugin's per-step hook (crew.cpp SharedFrameTick, from the player soldier's pre-update) runs once a simulation
// STEP, not once a frame: System::update (0x1198DA0) draws once, then steps the scene n times, n = the whole ticks due,
// clamped to 3 (0x11A2F76 cmova). So the frame itself is anchored on the main loop's one input sample a frame
// (0x11A2FF3 call 0x1182A10, after the tick wait, before Application::Update): its QPC starts the frame.
//
//  - Debug=1: the frame anchor. Frame times, the steps each frame took (System +0xCE0's delta), and a
//    SLOW FRAME <qpc begin> <qpc end> line for a frame over kSlowMs (tools/rip_sampler.cpp stamps every sample with the
//    same QPC, so perf_report can take the stacks inside the slow frames).
//  - PerfProbe=1 (perfrun sets it): the frame split by the main loop's calls (Application::Update's System::update,
//    its draw 0x11978D0, the Umbra resolve 0x12D6110 in it, the render fence wait 0x111CC20, the Present command
//    0x111D890), the render thread's Present (the swap chain's vtable), input to Present, and the draw submits by pass
//    (0x11E1630) and mesh draws (0x10FF4E0). Each call site is chained over whatever is there (another hook's near
//    thunk or the stock call), so it is installed after every other load-time hook.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew::perfprobe {
// At the plugin's load, after every other hook (plugin.cpp, just before LOAD complete).
void Install() noexcept;
// Points the E8 call at image+`rva` to `hook` over whatever it calls now: `stock`, or another hook's near thunk that ends
// in it (common/memory.cpp AllocateNearThunk). `*next` gets that target before the site changes. False: not a call to
// either (nothing written), or the write failed.
bool ChainCall(unsigned rva,unsigned stock,void* hook,void** next) noexcept;
// The main loop's thread (the Debug=1 frame anchor records it each frame); 0 before the first frame or without the anchor.
DWORD GameThread() noexcept;
// The frames since the last take (game thread, from PerfTick every 5 s): count, sum and worst ms, over 20 / 33 ms.
struct Frames { unsigned frames,steps,over20,over33; double sum,worst; };
Frames TakeFrames() noexcept;
// The PERF FRAME / RENDER / NET CHANNELS (net_counters.h) lines for the same period (debug), then zeroed. `frames` as TakeFrames gave.
void Report(unsigned frames) noexcept;
}  // namespace crew::perfprobe
