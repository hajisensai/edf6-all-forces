// Frames above the game's 59.94 Hz, stage 2 (docs/unlimited-fps-re.md §4): the frame gate, the pacing guards, the input
// on tick frames and the swap chain's sync interval. ini FrameRateCap: 0 (the game's 59.94 Hz) installs nothing; 1 (the
// display's refresh rate, shipped) or a cap
// installs everything below, the camera and the player's root interpolation (frame_rate_view.cpp) and the Present's
// statistics (swap_flip.cpp; its flip swap chain only under the developer switch FrameRateFlip). Other objects still move at the tick rate (docs/unlimited-fps-re.md §4.3, stage 4).
//
// The gate (fps-1). The main loop counts its ticks at 0x11A2F4F / 0x11A2F6B (`call 0x11DAE60; mov ebx,eax; test eax,eax`)
// and waits (0x11A2F65, frame_pacing.cpp TickWait) while the count is 0. Both calls go to a near stub that returns
// eax = whether this frame runs (its presentation slot came, frame_rate_logic.h Ready) and edx = the ticks it steps
// (0x11DAE60, asked only for a frame that runs, so no tick is counted and dropped); both `mov ebx,eax` become
// `mov ebx,edx`. A frame may so run with 0 ticks: it renders and steps nothing. TickWait then waits for the slot.
//
// The guards (fps-2). Application::Update (0x705530) runs once a frame and does some work per call, not per tick:
// SteamAPI_RunCallbacks (0x705633), 0x788460 (0x705693), EOS_Platform_Tick through 0x12ADB30 (0x7056AE: EDF6Coop's
// whole per-frame work hangs on it), the map's wind time += 0.0125 (0x1230A0 at 0x7056BA) and the input hold-off count
// Application+0x6C (0x70595C). On a 0-tick frame these are skipped, so they keep the rate of the ticks (the room's
// machines step and talk at the same rate whatever each one draws at: no room protocol change).
//
// The input (fps-3, latency-3). Each frame the game samples its devices into the input sources (0x11A2FF3), then
// System::Update (0x705715) renders and steps, and only after it 0x77320 (0x7058C7) copies the sources into the action
// wrappers the step reads (and sets their press / release / repeat edges): the step reads the frame before's input.
// Here the wrapper update runs before System::Update, on frames that step, with the frame's ticks: the step reads this
// frame's input (a tick sooner). A 0-tick frame does not update the wrappers: it only clears their edges (the UI saw
// them on the frame that set them) and copies the sources' values (sticks as they are now; the mouse and the wheel
// read 0, see below). The next tick frame's update compares with the held state of the last tick frame, so a key
// pressed during 0-tick frames gives its edge to the step once and to the UI once. The mouse's and the wheel's
// accumulators (filled by the window thread) are not taken on 0-tick frames: the next tick frame takes all of it.
//
// The sync interval (fps-4). The game passes 0x111D890 `[main+0xC]==0`: 1 (vsync) but for the 8 frames after a frame
// that caught up (n > 1). With the gate on that count is in frames, not ticks; the plugin passes its own: 0 when the
// cap is above the display's refresh rate or there is none, else 1 but for 8 ticks after a catch-up.
#include "frame_rate.h"
#include "frame_rate_view.h"
#include "frame_rate_objects.h"
#include "swap_flip.h"
#include "frame_pacing_logic.h"
#include "frame_rate_logic.h"
#include "frame_pacing.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "memory.h"
#include "edf/patch.h"
#include <algorithm>
#include <cstring>
#include <initializer_list>

namespace crew {
namespace {
// The main loop's tick counts and the `mov ebx,eax` after each.
constexpr unsigned kCountFirst=0x11A2F4F,kCountAgain=0x11A2F6B,kTickCount=0x11DAE60;
const unsigned char kCountFirstSig[]={0xE8,0x0C,0x7F,0x03,0x00,0x8B,0xD8,0x85,0xC0,0x75,0x1C};
const unsigned char kCountAgainSig[]={0xE8,0xF0,0x7E,0x03,0x00,0x8B,0xD8,0x85,0xC0,0x74,0xEA};
const unsigned char kMovEbxEdx[]={0x8B,0xDA};
// The frame's final tick count: after the counts the loop clamps n to 3 (0x11A2F80) and, when [rdi+9] is set (0x11A2FAC),
// zeroes it and dt; then it stores n for Application::Update ([rsp+0xBC], there [r14+4]). That store (7 bytes) becomes a
// call to a leaf stub that stores it and keeps a copy: every guard below reads the n the step really runs with.
constexpr unsigned kFinalStore=0x11A2FD8;
const unsigned char kFinalStoreSig[]={0x89,0x9C,0x24,0xBC,0x00,0x00,0x00};   // mov [rsp+0BCh],ebx
// Application::Update's per-call work.
constexpr unsigned kSteamCall=0x705633,kNetCall=0x705693,kNet=0x788460,kEosCall=0x7056AE,kEos=0x12ADB30;
constexpr unsigned kWindCall=0x7056BA,kWind=0x1230A0,kHoldOff=0x70595C;
// The sound's volume smoothing 0x7B3010(sound, dt) (0x705946): its +0x3C eases 0.1 of the way to its target and its
// +0xD8 grows 0.01 a call (both written to the volumes through 0x7AED10), so more calls than ticks fade faster. Called
// on frames with ticks only, with the dt of every frame since the last call (its +0x30 / +0x34 count down by dt).
constexpr unsigned kVolumeCall=0x705946,kVolume=0x7B3010;
// The load meter: 0x11A3033 stores the frame's Update time in percent of a 60 Hz frame to 0x2139B84, read by the
// level-of-detail cuts (0x134210, 0x136621: thresholds 200-350 %). A 0-tick frame's light Update would pull it down
// and keep the cuts from coming; the leaf stub stores it on frames with ticks only.
constexpr unsigned kLoadStore=0x11A3033,kLoadMeter=0x2139B84;
const unsigned char kLoadStoreSig[]={0xF3,0x0F,0x11,0x05,0x49,0x6B,0xF9,0x00};   // movss [rip+0F96B49h] (0x2139B84),xmm0
constexpr int kGuards=8;
const unsigned char kSteamSig[]={0xFF,0x15,0x4F,0x11,0x05,0x01};   // call [SteamAPI_RunCallbacks]
const unsigned char kHoldOffSig[]={0x8B,0x46,0x6C,0x85,0xC0,0x7E,0x11,0xFF,0xC8,0x89,0x46,0x6C};   // mov eax,[rsi+6Ch]; test; jle; dec; mov
// The shared random number (1191 readers) Application::Update steps once a call: seed = seed*mult+0x269EC3.
constexpr unsigned kRandomStep=0x705570,kRandomSeed=0x1F54128;
const unsigned char kRandomStepSig[]={0x48,0xB9,0x65,0x89,0x07,0x6C,0x65,0x8B,0x58,0x5D,0x48,0x8B,0x05,0xA7,0xEB,0x84,0x01,
                                      0x48,0x0F,0xAF,0xC1,0x48,0x05,0xC3,0x9E,0x26,0x00,0x48,0x89,0x05,0x96,0xEB,0x84,0x01};
// The input: System::Update's call, the wrapper update's call, the input manager whose +0x5AE8 is the wrapper table.
constexpr unsigned kSystemCall=0x705715,kSystemUpdate=0x1198DA0,kWrapCall=0x7058C7,kWrapUpdate=0x77320;
constexpr unsigned kInputManager=0x20B28A0,kWrapTable=0x5AE8;
// The wrapper table (0x7CDC0): 4 players of 0xDD0 bytes; in each, 146 wrappers of 0x18 from +8 (source pointer +0,
// repeat count +8, value +0xC, held +0x10, pressed +0x11, released +0x12, repeat pulse +0x13), then the masks of the
// player's menu keys: held +0xDB8, pressed +0xDBC, repeat +0xDC0, released +0xDC4. A source: value +0x48, held +0x4C,
// off +0x4D (0x1182A00 writes them, 0x1182BC0 reads them).
constexpr unsigned kPlayers=4,kPlayerSize=0xDD0,kWrapFirst=8,kWrapSize=0x18,kWrapEnd=0xDB8;
constexpr unsigned kMaskPressed=0xDBC,kMaskRepeat=0xDC0,kMaskReleased=0xDC4;
// The mouse's and the wheel's takes in the input callback 0x1188270, and the accumulator's cleared value.
constexpr unsigned kMouseCall=0x1188479,kMouseTake=0x11831F0,kWheelCall=0x1188480,kWheelTake=0x11832D0,kMouseCleared=0x1765B30;
// The swap chain's sync interval.
constexpr unsigned kSyncCall=0x11A309B,kSyncSubmit=0x111D890;
const unsigned char kSyncSig[]={0x8B,0xD6,0x48,0x8D,0x0D,0x5B,0x3F,0xF9,0x00,0x39,0x57,0x0C,0x0F,0x94,0xC2};
constexpr int kCatchUpTicks=8;
constexpr std::uint64_t kStatsSeconds=5;

int cap=framerate::kOff;       // FixCap's
int gateCap=framerate::kOff;   // the slots': kRefresh has none (kUnlimited), the sync interval holds it to the refresh
std::uint64_t qpcFreq=0,slotPeriod=0;
framerate::Gate gate;
bool gateOn=false;
// The ticks the frame being run steps, as Application::Update gets them (the leaf stub at kFinalStore; game thread).
volatile std::uint32_t finalTicks=1;
// The phase this frame draws at (frame_rate_logic.h RenderAlpha), set by the gate.
float frameAlpha=1.0f;
bool viewOn=false;
// The shared random seed (0x1F54128) across a 0-tick frame (#394 review e): its own step (InstallRandomStep) runs only
// on frames with ticks, so a seed moved from one frame's start to the next over a 0-tick frame is something else drawing
// from it on that frame (the draw, an effect): the sequence would then drift from 60 fps's, online too.
std::uint64_t seedAtFrame=0;
bool lastFrameZero=false;
unsigned seedMoved=0,seedFrames=0;

std::uint64_t tickPeriod=0;
constexpr unsigned kTickBase=0x2139D10,kTickLast=0x2139D18;
// The game's load count (docs/unlimited-fps-re.md §4.1.1): 0x1133FC0(obj, dl) adds +1 (dl 1) / -1 (dl 0) to obj+0x34 by
// `lock xadd`; with obj 0x2139AB0 the mission scripts' BeginLoading / EndLoading (0x1ACC03 / 0x1B4A69) and the script
// natives 0x0A / 0x0B (0x21070F / 0x210889) call it, and the game's job thread holds its queue while it is above 0
// (0x1133993: `lock xadd [rbp+34h],0`, then Sleep). Read once a frame by the gate: while loading every frame steps.
constexpr unsigned kLoadAdd=0x1133FC0,kLoadJobRead=0x1133993,kLoad=0x2139AB0,kLoadCount=0x34;
const unsigned char kLoadAddSig[]={0x0F,0xB6,0xC2,0x8D,0x04,0x45,0xFF,0xFF,0xFF,0xFF,0xF0,0x0F,0xC1,0x41,0x34,0xC3};
const unsigned char kLoadJobReadSig[]={0xF0,0x0F,0xC1,0x45,0x34};   // lock xadd [rbp+34h],eax
// The input move: whether this frame's wrapper update already ran before System::Update.
bool wrapped=false;
int catchUp=0;
bool vsyncWanted=true;
// FRAMERATE statistics, game thread only.
struct Stats { std::uint64_t since=0,frames=0,ticks=0,zero=0,lastRun=0,longestZero=0,loading=0; bool lastZero=false; } stats;
// The load the gate last saw (game thread): its start and the frames it ran, for the FRAMERATE lines at its ends.
bool wasLoading=false;
std::uint64_t loadSince=0,loadFrames=0;
// The FRAMERATE loading lines at most once per kLoadLogSeconds (a count raised for a moment off the mission load, by
// 0x110B460 or the HQ model view's pairs, would otherwise log a line pair each time): the loads not logged are counted
// and told with the next one logged; a load's end is logged only when its start was.
constexpr std::uint64_t kLoadLogSeconds=5;
std::uint64_t loadLoggedAt=0;
unsigned loadsUnlogged=0;
bool loadLogged=false;

using CountFn=std::uint32_t(*)();
using VoidFn=void(*)(void*);
using SteamFn=void(*)();
using SystemUpdateFn=void(*)(void*,float,int);
using WrapUpdateFn=void(*)(void*,int);
using MouseTakeFn=void*(*)(void*,char);
using WheelTakeFn=int(*)(char);
using SyncSubmitFn=void(*)(void*,unsigned);
void* steamRun=nullptr;

std::uint64_t Now() noexcept {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<std::uint64_t>(t.QuadPart);
}

// The frame intervals of the stats window (for its 1 % low): game thread, at most kIntervals kept.
constexpr std::size_t kIntervals=16384;
std::uint64_t intervals[kIntervals];
std::size_t intervalCount=0;

void CountStats(std::uint64_t now,std::uint32_t n) noexcept {
    if(!stats.since)stats.since=now;
    if(stats.lastRun && intervalCount<kIntervals)intervals[intervalCount++]=now-stats.lastRun;
    if(stats.lastRun && stats.lastZero && now-stats.lastRun>stats.longestZero)stats.longestZero=now-stats.lastRun;
    stats.lastRun=now;
    stats.lastZero=!n;
    ++stats.frames;
    if(wasLoading)++stats.loading;
    stats.ticks+=n;
    if(!n)++stats.zero;
    const std::uint64_t span=now-stats.since;
    if(span<kStatsSeconds*qpcFreq)return;
    const double s=static_cast<double>(span)/static_cast<double>(qpcFreq);
    unsigned blended=0,unblended=0,roots=0,rootsLeft=0,rootsRefused=0;
    if(viewOn)framerateview::TakeCounts(blended,unblended,roots,rootsLeft,rootsRefused);
    // 1 % low: the frame rate of the 99th percentile interval (the slowest 1 % of the window's frames).
    double low=0;
    if(intervalCount) {
        const std::size_t k=intervalCount-1-intervalCount/100;
        std::nth_element(intervals,intervals+k,intervals+intervalCount);
        if(intervals[k])low=static_cast<double>(qpcFreq)/static_cast<double>(intervals[k]);
    }
    Log("FRAMERATE %.1f s: ticks %.2f/s, frames %.1f/s (1%% low %.1f), 0-tick frames %.1f%%, longest 0-tick frame %.2f ms; "
        "0-tick projections blended %u, as stepped %u; player root blended %u, as stepped %u (changed since its tick), "
        "refused %u (a jump or not rigid); random seed moved over %u of %u 0-tick frames after a 0-tick frame; "
        "%.1f%% of the frames loading (no 0-tick frames)",s,
        static_cast<double>(stats.ticks)/s,static_cast<double>(stats.frames)/s,low,
        stats.frames ? 100.0*static_cast<double>(stats.zero)/static_cast<double>(stats.frames) : 0.0,
        1000.0*static_cast<double>(stats.longestZero)/static_cast<double>(qpcFreq),blended,unblended,roots,rootsLeft,
        rootsRefused,seedMoved,seedFrames,
        stats.frames ? 100.0*static_cast<double>(stats.loading)/static_cast<double>(stats.frames) : 0.0);
    seedMoved=seedFrames=0;
    if(viewOn) {
        // Stage 4 (frame_rate_objects.cpp): the other objects, and why roots were refused (alpha, not rigid, mirrored,
        // a move or a turn over the cut), the player's and the objects'.
        static_assert(static_cast<int>(framerate::Refusal::kCount)==6,"the five reasons logged");
        frameobjects::Counts o;
        frameobjects::TakeCounts(o);
        unsigned refusedAny=0;
        for(int i=1;i<static_cast<int>(framerate::Refusal::kCount);++i)refusedAny+=o.rootRefused[i]+o.refused[i];
        static_assert(frameobjects::kLeftReasons==8,"the eight reasons logged");
        const unsigned stepped=o.AsStepped();
        if(o.posed || o.still || stepped || refusedAny || o.idleTicks)
            Log("FRAMERATE objects %u: blended %u (riders %u), still %u, as stepped %u (gone/vtable/pose/samples/"
                "unsampled/refused/full/fault %u/%u/%u/%u/%u/%u/%u/%u), most poses a frame %u, ticks not sampled (idle) %u; refused "
                "(alpha/not rigid/mirrored/move/turn) player root %u/%u/%u/%u/%u, objects %u/%u/%u/%u/%u",o.objects,
                o.posed,o.riders,o.still,stepped,o.left[0],o.left[1],o.left[2],o.left[3],o.left[4],o.left[5],o.left[6],
                o.left[7],o.mostPoses,o.idleTicks,o.rootRefused[1],o.rootRefused[2],o.rootRefused[3],
                o.rootRefused[4],o.rootRefused[5],o.refused[1],o.refused[2],o.refused[3],o.refused[4],o.refused[5]);
    }
    const PresentStats p=SwapFlipPresentStats();
    if(p.valid)Log("PRESENT calls %.1f/s, shown %.1f/s, display refreshes %.1f/s; failed %u, occluded %u",
                   p.calls,p.shown,p.refreshes,p.failed,p.occluded);
    else Log("PRESENT calls %.1f/s; failed %u, occluded %u; no frame statistics (0x%08lX: a windowed blt chain has none)",
             p.calls,p.failed,p.occluded,static_cast<unsigned long>(p.statsHr));
    stats.since=now;stats.frames=stats.ticks=stats.zero=stats.longestZero=stats.loading=0;
    intervalCount=0;
}

// Low 32 bits: whether the frame runs; high: its ticks. Called by the near stub in place of 0x11DAE60.
bool LoadingNow() noexcept {
    return framerate::Loading(*reinterpret_cast<const volatile std::int32_t*>(image+kLoad+kLoadCount));
}

// The FRAMERATE lines at a load's ends: how long the gate kept the game's cadence and how many frames it ran.
void SeeLoad(bool loading,std::uint64_t now) noexcept {
    if(loading==wasLoading)return;
    wasLoading=loading;
    if(loading) {
        loadSince=now;loadFrames=0;
        loadLogged=!loadLoggedAt || now-loadLoggedAt>=kLoadLogSeconds*qpcFreq;
        if(!loadLogged){++loadsUnlogged;return;}
        loadLoggedAt=now;
        Log("FRAMERATE loading (count %d): no 0-tick frames until it ends (%u loads since the last line not logged)",
            *reinterpret_cast<const volatile std::int32_t*>(image+kLoad+kLoadCount),loadsUnlogged);
        loadsUnlogged=0;
        return;
    }
    if(!loadLogged)return;
    Log("FRAMERATE loaded after %.2f s, %llu frames: the frame gate is back",
        static_cast<double>(now-loadSince)/static_cast<double>(qpcFreq),static_cast<unsigned long long>(loadFrames));
}

std::uint64_t GateCount() noexcept {
    const bool loading=LoadingNow();
    SeeLoad(loading,Now());
    if(loading) {
        // The game's cadence (frame_rate_logic.h LoadingRuns): the ticks first, a frame only with ticks.
        const std::uint32_t n=reinterpret_cast<CountFn>(image+kTickCount)();
        if(!framerate::LoadingRuns(gate,n))return 0;
        SwapFlipWait();   // once a frame that presents, as below
        ++loadFrames;
        frameAlpha=1.0f;
        return 1u|static_cast<std::uint64_t>(n)<<32;
    }
    if(!framerate::Ready(gate,Now(),gateCap,slotPeriod))return 0;
    SwapFlipWait();   // latency-5: the frame starts when the GPU can take it; its ticks and alpha are counted after
    const std::uint64_t now=Now();
    const std::uint32_t n=reinterpret_cast<CountFn>(image+kTickCount)();
    frameAlpha=framerate::RenderAlpha(n,now,*reinterpret_cast<const std::uint64_t*>(image+kTickBase),
                                      *reinterpret_cast<const std::uint64_t*>(image+kTickLast),tickPeriod);
    return 1u|static_cast<std::uint64_t>(n)<<32;
}

// TickWait (frame_pacing.cpp) asks this while the gate is on: QPC counts to the next slot.
// While loading: to the next tick boundary, as the game's own wait.
std::uint64_t GateWaitCounts(std::uint64_t now) noexcept {
    if(LoadingNow())return pacing::CountsToBoundary(now,*reinterpret_cast<const std::uint64_t*>(image+kTickBase),
                                                    *reinterpret_cast<const std::uint64_t*>(image+kTickLast),tickPeriod);
    return framerate::WaitCounts(gate,now,gateCap);
}

bool Steps() noexcept { return !gateOn || finalTicks; }

void SteamGuard() noexcept { if(Steps())reinterpret_cast<SteamFn>(steamRun)(); }
void NetGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kNet)(self); }
void EosGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kEos)(self); }
void WindGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kWind)(self); }
float volumeDt=0.0f;   // the dt of the frames since the volume smoothing last ran (game thread)
void VolumeGuard(void* sound,float dt) {
    volumeDt+=dt;
    if(!Steps())return;
    reinterpret_cast<void(*)(void*,float)>(image+kVolume)(sound,volumeDt);
    volumeDt=0.0f;
}

// A 0-tick frame's wrappers: the edges the UI saw last frame cleared, the values as the sources hold them now.
void ClearForUi(unsigned char* table) noexcept {
    for(unsigned p=0;p<kPlayers;++p) {
        unsigned char* const block=table+p*kPlayerSize;
        for(unsigned at=kWrapFirst;at<kWrapEnd;at+=kWrapSize) {
            unsigned char* const w=block+at;
            const unsigned char* const src=*reinterpret_cast<unsigned char* const*>(w);
            if(src && !src[0x4D])std::memcpy(w+0xC,src+0x48,4);
            w[0x11]=w[0x12]=w[0x13]=0;
        }
        for(unsigned mask:{kMaskPressed,kMaskRepeat,kMaskReleased})std::memset(block+mask,0,4);
    }
}

void SystemUpdateHook(void* system,float dt,int n) {
    void* const input=*reinterpret_cast<void**>(image+kInputManager);
    if(gateOn) {
        const std::uint64_t seed=*reinterpret_cast<const volatile std::uint64_t*>(image+kRandomSeed);
        // Two 0-tick frames in a row only: a frame with ticks runs the game's own step (0x705570, n > 0) in
        // Application::Update before this read, so a 0-tick frame followed by one with ticks always reads moved
        // (M001 2026-11-24: 300 of ~1500 every 5 s, the ticks' count exactly). Between two 0-tick reads nothing of the
        // game's steps it.
        if(lastFrameZero && n<=0){++seedFrames;if(seed!=seedAtFrame)++seedMoved;}
        seedAtFrame=seed;
        lastFrameZero=n<=0;
        // The catch-up window in ticks (the game's own counts frames): 8 ticks after a frame that stepped more than one.
        if(n>1)catchUp=kCatchUpTicks;
        else catchUp=catchUp>n ? catchUp-n : 0;
        CountStats(Now(),n>0 ? static_cast<std::uint32_t>(n) : 0u);
    }
    if(input) {
        // Without the gate a frame with n = 0 (the loop's reset, [rdi+9]) updates the wrappers as the game does.
        if(n>0 || !gateOn)reinterpret_cast<WrapUpdateFn>(image+kWrapUpdate)(input,n);
        else ClearForUi(static_cast<unsigned char*>(input)+kWrapTable);
        wrapped=true;
    }
    framerateview::BeginFrame(n>0 ? static_cast<std::uint32_t>(n) : 0u,gateOn && n==0 ? frameAlpha : 1.0f);
    reinterpret_cast<SystemUpdateFn>(image+kSystemUpdate)(system,dt,n);
}

void WrapUpdateHook(void* input,int n) {
    if(wrapped){wrapped=false;return;}   // done before System::Update this frame
    reinterpret_cast<WrapUpdateFn>(image+kWrapUpdate)(input,n);
}

void* MouseTakeHook(void* out,char take) noexcept {
    if(Steps())return reinterpret_cast<MouseTakeFn>(image+kMouseTake)(out,take);
    std::memcpy(out,image+kMouseCleared,16);   // nothing moved this frame; the window thread's sum waits for a step
    return out;
}
int WheelTakeHook(char take) noexcept {
    if(Steps())return reinterpret_cast<WheelTakeFn>(image+kWheelTake)(take);
    return 0;
}

void SyncHook(void* renderer,unsigned vsync) {
    if(gateOn)vsync=vsyncWanted && !catchUp ? 1u : 0u;
    reinterpret_cast<SyncSubmitFn>(image+kSyncSubmit)(renderer,vsync);
}

// The near stub the two tick counts call: `sub rsp,28h; mov rax,GateCount; call rax; mov rdx,rax; shr rdx,32;
// add rsp,28h; ret`, its unwind info (UWOP_ALLOC_SMALL 0x28 after the 4-byte prolog) and its function table entry,
// in one page, the table registered so a stack walk through the stub (a crash dump) goes on to the main loop.
void* MakeGateStub() noexcept {
    unsigned char page[64]={};
    const unsigned char code[]={0x48,0x83,0xEC,0x28,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x8B,0xD0,0x48,0xC1,0xEA,0x20,
                                0x48,0x83,0xC4,0x28,0xC3};
    static_assert(sizeof(code)<=32,"the stub's code fits before its unwind info");
    std::memcpy(page,code,sizeof(code));
    const auto target=reinterpret_cast<std::uintptr_t>(&GateCount);
    std::memcpy(page+6,&target,sizeof(target));
    const unsigned char unwind[]={0x01,0x04,0x01,0x00,0x04,0x42,0x00,0x00};   // v1, prolog 4, 1 code; @4 ALLOC_SMALL (0x28-8)/8
    std::memcpy(page+32,unwind,sizeof(unwind));
    const RUNTIME_FUNCTION entry{0,static_cast<DWORD>(sizeof(code)),32};
    std::memcpy(page+40,&entry,sizeof(entry));
    unsigned char* const at=static_cast<unsigned char*>(edf::AllocateNearCode(image+kCountFirst,page,sizeof(page)));
    if(!at)return nullptr;
    if(!RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(at+40),1,reinterpret_cast<DWORD64>(at)))
        Log("FRAMERATE the gate stub's unwind table was not registered: a stack walk stops at it");
    return at;
}

bool PatchCall(unsigned rva,const void* to) noexcept {
    unsigned char* const site=image+rva;
    const auto delta=static_cast<const unsigned char*>(to)-(site+5);
    if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0)return false;
    unsigned char call[5]={0xE8};
    const auto rel=static_cast<std::int32_t>(delta);
    std::memcpy(call+1,&rel,4);
    unsigned char expected[5];
    std::memcpy(expected,site,5);
    return edf::PatchCode(site,expected,call,5);
}

// The gate's sites as read and the frame step's wait ours (not EDF6VR's or an FPS unlimiter's: frame_pacing.cpp,
// frame_pacing_logic.h ForeignTickWait). Asked before anything is installed: all or nothing.
bool GateSitesAsRead() noexcept {
    if(!FramePacingTickWaitInstalled()) {
        Log("FRAMERATE the frame step's wait is not ours (the PACING line says whose): nothing installed, the game keeps its rate");
        return false;
    }
    if(!edf::Matches(image,kCountFirst,kCountFirstSig,sizeof(kCountFirstSig)) ||
       !edf::Matches(image,kCountAgain,kCountAgainSig,sizeof(kCountAgainSig))) {
        Log("FRAMERATE the main loop's tick counts are not as read (another MOD?): nothing installed, the game keeps its rate");
        return false;
    }
    if(!edf::Matches(image,kFinalStore,kFinalStoreSig,sizeof(kFinalStoreSig))) {
        Log("FRAMERATE the main loop's store of n (0x%X) is not as read: nothing installed, the game keeps its rate",kFinalStore);
        return false;
    }
    if(!edf::Matches(image,kLoadAdd,kLoadAddSig,sizeof(kLoadAddSig)) ||
       !edf::Matches(image,kLoadJobRead,kLoadJobReadSig,sizeof(kLoadJobReadSig))) {
        Log("FRAMERATE the load count (0x%X+0x%X) is not as read: nothing installed, the game keeps its rate",kLoad,kLoadCount);
        return false;
    }
    return true;
}

bool InstallGate() noexcept {
    if(!GateSitesAsRead())return false;
    void* const stub=MakeGateStub();
    // `mov [rsp+0C4h],ebx` (the caller's rsp+0xBC); `mov r11,&finalTicks; mov [r11],ebx; ret` (r11 is dead: a call follows).
    unsigned char leaf[]={0x89,0x9C,0x24,0xC4,0x00,0x00,0x00,0x49,0xBB,0,0,0,0,0,0,0,0,0x41,0x89,0x1B,0xC3};
    const auto copy=reinterpret_cast<std::uintptr_t>(&finalTicks);
    std::memcpy(leaf+9,&copy,sizeof(copy));
    void* const final=edf::AllocateNearCode(image+kFinalStore,leaf,sizeof(leaf));
    if(!stub || !final){Log("FRAMERATE no page near the main loop for the gate stubs");return false;}
    // Three sites, each one write (a count's call with its `mov ebx,edx`, the store's call), all or none: a count
    // without its mov would step one tick a frame (the game sped up), a mov without its call steps garbage. Written at
    // load, before the main loop runs; a write that fails puts back the ones before it.
    struct Site { unsigned rva; const unsigned char* sig; std::size_t size; const void* to; unsigned char code[7]; };
    Site sites[]={{kCountFirst,kCountFirstSig,7,stub,{}},{kCountAgain,kCountAgainSig,7,stub,{}},{kFinalStore,kFinalStoreSig,7,final,{}}};
    for(Site& t:sites) {
        const auto delta=static_cast<const unsigned char*>(t.to)-(image+t.rva+5);
        if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0){Log("FRAMERATE a gate stub is out of call range: no frame gate");return false;}
        const auto rel=static_cast<std::int32_t>(delta);
        t.code[0]=0xE8;
        std::memcpy(t.code+1,&rel,4);
        if(t.rva==kFinalStore)t.code[5]=t.code[6]=0x90;
        else std::memcpy(t.code+5,kMovEbxEdx,2);
    }
    int written=0;
    while(written<3 && edf::PatchCode(image+sites[written].rva,sites[written].sig,sites[written].code,sites[written].size))++written;
    if(written<3) {
        bool back=true;
        for(int i=written-1;i>=0;--i)back=edf::PatchCode(image+sites[i].rva,sites[i].code,sites[i].sig,sites[i].size) && back;
        Log("FRAMERATE the frame gate's write %d of 3 failed: %s",written+1,back ? "the ones before it put back, no frame gate"
                                                                                 : "and putting back failed: the main loop is in an unknown state");
        return false;
    }
    SetFramePacingGate(&GateWaitCounts);
    return true;
}

// The random step (34 bytes, rax and rcx dead after it) becomes a call to a leaf stub and NOPs; the stub steps the
// seed only on a frame with ticks, so the sequence follows the ticks as at 60 fps: `cmp dword [r14+4],0; je 1f;
// mov rcx,mult; mov rax,[seed]; imul rax,rcx; add rax,269EC3h; mov [seed],rax; 1: ret`.
bool InstallRandomStep() noexcept {
    if(!edf::Matches(image,kRandomStep,kRandomStepSig,sizeof(kRandomStepSig)))return false;
    unsigned char leaf[]={0x41,0x83,0x7E,0x04,0x00,0x74,0x28,
                          0x48,0xB9,0x65,0x89,0x07,0x6C,0x65,0x8B,0x58,0x5D,
                          0x48,0xA1,0,0,0,0,0,0,0,0,
                          0x48,0x0F,0xAF,0xC1,0x48,0x05,0xC3,0x9E,0x26,0x00,
                          0x48,0xA3,0,0,0,0,0,0,0,0,
                          0xC3};
    const auto seed=reinterpret_cast<std::uintptr_t>(image+kRandomSeed);
    std::memcpy(leaf+19,&seed,sizeof(seed));
    std::memcpy(leaf+39,&seed,sizeof(seed));
    unsigned char* const at=static_cast<unsigned char*>(edf::AllocateNearCode(image+kRandomStep,leaf,sizeof(leaf)));
    if(!at)return false;
    unsigned char* const site=image+kRandomStep;
    const auto delta=at-(site+5);
    if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0)return false;
    unsigned char call[sizeof(kRandomStepSig)];
    std::memset(call,0x90,sizeof(call));
    call[0]=0xE8;
    const auto rel=static_cast<std::int32_t>(delta);
    std::memcpy(call+1,&rel,4);
    return edf::PatchCode(site,kRandomStepSig,call,sizeof(call));
}

int InstallGuards() noexcept {
    int done=0;
    bool changed=false;
    if(edf::RedirectImportCall(image+kSteamCall,kSteamSig,reinterpret_cast<void*>(&SteamGuard),&steamRun))++done;
    done+=edf::RedirectCall(image+kNetCall,image+kNet,reinterpret_cast<void*>(&NetGuard),changed) ? 1 : 0;
    done+=edf::RedirectCall(image+kEosCall,image+kEos,reinterpret_cast<void*>(&EosGuard),changed) ? 1 : 0;
    done+=edf::RedirectCall(image+kWindCall,image+kWind,reinterpret_cast<void*>(&WindGuard),changed) ? 1 : 0;
    // The hold-off count: `mov eax,[rsi+6Ch]; test eax,eax` (5 bytes) becomes a call to a leaf stub that reads it as
    // 0 on a 0-tick frame (Application::Update's own n, [r14+4]), so `jle` skips the dec: `mov eax,[rsi+6Ch];
    // cmp dword [r14+4],0; jne 1f; xor eax,eax; 1: test eax,eax; ret`. A leaf: no stack of its own, no unwind info.
    const unsigned char leaf[]={0x8B,0x46,0x6C,0x41,0x83,0x7E,0x04,0x00,0x75,0x02,0x33,0xC0,0x85,0xC0,0xC3};
    if(edf::Matches(image,kHoldOff,kHoldOffSig,sizeof(kHoldOffSig))) {
        void* const at=edf::AllocateNearCode(image+kHoldOff,leaf,sizeof(leaf));
        if(at && PatchCall(kHoldOff,at))++done;
    }
    if(InstallRandomStep())++done;
    done+=edf::RedirectCall(image+kVolumeCall,image+kVolume,reinterpret_cast<void*>(&VolumeGuard),changed) ? 1 : 0;
    // The load meter: the 8-byte store becomes a call to a meterLeaf stub and 3 NOPs (r11 and the flags are dead: a call
    // follows): `mov r11,&finalTicks; cmp dword [r11],0; je 1f; mov r11,meter; movss [r11],xmm0; 1: ret`.
    if(edf::Matches(image,kLoadStore,kLoadStoreSig,sizeof(kLoadStoreSig))) {
        unsigned char meterLeaf[]={0x49,0xBB,0,0,0,0,0,0,0,0,0x41,0x83,0x3B,0x00,0x74,0x0F,0x49,0xBB,0,0,0,0,0,0,0,0,
                              0xF3,0x41,0x0F,0x11,0x03,0xC3};
        const auto ticks=reinterpret_cast<std::uintptr_t>(&finalTicks);
        const auto meter=reinterpret_cast<std::uintptr_t>(image+kLoadMeter);
        std::memcpy(meterLeaf+2,&ticks,sizeof(ticks));
        std::memcpy(meterLeaf+18,&meter,sizeof(meter));
        unsigned char* const at=static_cast<unsigned char*>(edf::AllocateNearCode(image+kLoadStore,meterLeaf,sizeof(meterLeaf)));
        const auto delta=at ? at-(image+kLoadStore+5) : 0;
        if(at && delta<=0x7FFFFFF0 && delta>=-0x7FFFFFF0) {
            unsigned char call[8]={0xE8,0,0,0,0,0x90,0x90,0x90};
            const auto rel=static_cast<std::int32_t>(delta);
            std::memcpy(call+1,&rel,4);
            if(edf::PatchCode(image+kLoadStore,kLoadStoreSig,call,sizeof(call)))++done;
        }
    }
    return done;
}

int InstallInputTakes() noexcept {
    bool changed=false;
    int done=edf::RedirectCall(image+kMouseCall,image+kMouseTake,reinterpret_cast<void*>(&MouseTakeHook),changed) ? 1 : 0;
    done+=edf::RedirectCall(image+kWheelCall,image+kWheelTake,reinterpret_cast<void*>(&WheelTakeHook),changed) ? 1 : 0;
    return done;
}

bool InstallInputMove() noexcept {
    bool changed=false;
    // The wrapper update's call first: until System::Update's is redirected it just calls through.
    if(!edf::RedirectCall(image+kWrapCall,image+kWrapUpdate,reinterpret_cast<void*>(&WrapUpdateHook),changed))return false;
    return edf::RedirectCall(image+kSystemCall,image+kSystemUpdate,reinterpret_cast<void*>(&SystemUpdateHook),changed);
}

bool InstallSync() noexcept {
    if(!edf::Matches(image,kSyncCall-sizeof(kSyncSig),kSyncSig,sizeof(kSyncSig)))return false;
    DEVMODEW mode{};
    mode.dmSize=sizeof(mode);
    const int refresh=EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&mode) ? static_cast<int>(mode.dmDisplayFrequency) : 0;
    vsyncWanted=cap==framerate::kRefresh || (cap!=framerate::kUnlimited && refresh>1 && cap<=refresh);
    Log("FRAMERATE display %d Hz, cap %d: sync interval %d (0 for %d ticks after a catch-up)",refresh,cap,vsyncWanted ? 1 : 0,kCatchUpTicks);
    bool changed=false;
    return edf::RedirectCall(image+kSyncCall,image+kSyncSubmit,reinterpret_cast<void*>(&SyncHook),changed);
}
}  // namespace

void InstallFrameRate() noexcept {
    const Config& c=Cfg();
    if(!image)return;
    if(c.frameRateCap==framerate::kOff)return;   // the game's own rate: nothing installed
    if(!GateSitesAsRead())return;                // another frame limiter (EDF6VR) or another MOD: nothing installed
    // The input move first: the gate needs it.
    const bool input=InstallInputMove();
    const bool present=InstallSwapFlip(c.frameRateFlip);
    const bool flip=present && c.frameRateFlip;
    Log("FRAMERATE FrameRateCap=%d: input wrappers updated before the step=%d, Present statistics=%d, flip swap chain=%d "
        "(developer: FrameRateFlip)",c.frameRateCap,input,present,flip);
    if(!input)return;
    cap=c.frameRateCap;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcFreq=static_cast<std::uint64_t>(freq.QuadPart);
    gateCap=cap==framerate::kRefresh ? framerate::kUnlimited : cap;
    slotPeriod=framerate::PresentPeriod(qpcFreq,gateCap);
    tickPeriod=pacing::TickPeriod(qpcFreq);
    // Everything a 0-tick frame would do wrong goes in before the gate that makes them: the guards, the takes, the sync.
    const int guards=InstallGuards();
    const int takes=InstallInputTakes();
    const bool sync=InstallSync();
    if(guards!=kGuards || takes!=2 || !sync) {
        Log("FRAMERATE guards %d/%d, input takes %d/2, sync=%d: not all in, no frame gate (the game keeps its rate)",guards,kGuards,takes,sync);
        return;
    }
    // The interpolation before the gate: without it the frames between the ticks would only draw the same scene again.
    viewOn=framerateview::Install();
    gateOn=viewOn && InstallGate();
    if(gateOn && flip && c.maxFrameLatency>0)SwapFlipUseWaitable(static_cast<unsigned>(c.maxFrameLatency));
    Log("FRAMERATE FrameRateCap=%d: frame gate=%d, guards %d/%d, input takes %d/2, sync=%d, camera interpolation=%d",
        cap,gateOn,guards,kGuards,takes,sync,viewOn);
}
}  // namespace crew
