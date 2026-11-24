// Frames above the game's 59.94 Hz, stage 2 (docs/unlimited-fps-re.md §4): the frame gate, the pacing guards, the input
// on tick frames and the swap chain's sync interval. Developer only: ini FrameRateDev=1 (not in the shipped ini); with
// FrameRateCap=0 it installs only the input move (latency-3), with a cap everything below. Frames without a step draw
// the same state as the frame before (no interpolation yet: stage 3), so this stage is for checking on the real game that
// the simulation, the input and the online play keep the game's rate (§7), not for players.
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
#include "frame_rate_logic.h"
#include "frame_pacing.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstring>
#include <initializer_list>

namespace crew {
namespace {
// The main loop's tick counts and the `mov ebx,eax` after each.
constexpr unsigned kCountFirst=0x11A2F4F,kCountAgain=0x11A2F6B,kTickCount=0x11DAE60;
const unsigned char kCountFirstSig[]={0xE8,0x0C,0x7F,0x03,0x00,0x8B,0xD8,0x85,0xC0,0x75,0x1C};
const unsigned char kCountAgainSig[]={0xE8,0xF0,0x7E,0x03,0x00,0x8B,0xD8,0x85,0xC0,0x74,0xEA};
const unsigned char kMovEbxEax[]={0x8B,0xD8},kMovEbxEdx[]={0x8B,0xDA};
// Application::Update's per-call work.
constexpr unsigned kSteamCall=0x705633,kNetCall=0x705693,kNet=0x788460,kEosCall=0x7056AE,kEos=0x12ADB30;
constexpr unsigned kWindCall=0x7056BA,kWind=0x1230A0,kHoldOff=0x70595C;
const unsigned char kSteamSig[]={0xFF,0x15,0x4F,0x11,0x05,0x01};   // call [SteamAPI_RunCallbacks]
const unsigned char kHoldOffSig[]={0x8B,0x46,0x6C,0x85,0xC0,0x7E,0x11,0xFF,0xC8,0x89,0x46,0x6C};   // mov eax,[rsi+6Ch]; test; jle; dec; mov
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

int cap=framerate::kOff;
std::uint64_t qpcFreq=0,slotPeriod=0;
framerate::Gate gate;
bool gateOn=false;
// The ticks the frame being run steps (the gate's count; the game thread only).
std::uint32_t frameTicks=1;
// The input move: whether this frame's wrapper update already ran before System::Update.
bool wrapped=false;
int catchUp=0;
bool vsyncWanted=true;
// FRAMERATE statistics, game thread only.
struct Stats { std::uint64_t since=0,frames=0,ticks=0,zero=0,lastRun=0,longestZero=0; bool lastZero=false; } stats;

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

void CountStats(std::uint64_t now,std::uint32_t n) noexcept {
    if(!stats.since)stats.since=now;
    if(stats.lastRun && stats.lastZero && now-stats.lastRun>stats.longestZero)stats.longestZero=now-stats.lastRun;
    stats.lastRun=now;
    stats.lastZero=!n;
    ++stats.frames;
    stats.ticks+=n;
    if(!n)++stats.zero;
    const std::uint64_t span=now-stats.since;
    if(span<kStatsSeconds*qpcFreq)return;
    const double s=static_cast<double>(span)/static_cast<double>(qpcFreq);
    Log("FRAMERATE %.1f s: ticks %.2f/s, frames %.1f/s, 0-tick frames %.1f%%, longest 0-tick frame %.2f ms",s,
        static_cast<double>(stats.ticks)/s,static_cast<double>(stats.frames)/s,
        stats.frames ? 100.0*static_cast<double>(stats.zero)/static_cast<double>(stats.frames) : 0.0,
        1000.0*static_cast<double>(stats.longestZero)/static_cast<double>(qpcFreq));
    stats.since=now;stats.frames=stats.ticks=stats.zero=stats.longestZero=0;
}

// Low 32 bits: whether the frame runs; high: its ticks. Called by the near stub in place of 0x11DAE60.
std::uint64_t GateCount() noexcept {
    const std::uint64_t now=Now();
    if(!framerate::Ready(gate,now,cap,slotPeriod))return 0;
    const std::uint32_t n=reinterpret_cast<CountFn>(image+kTickCount)();
    frameTicks=n;
    // The catch-up window in ticks (the game's own counts frames): 8 ticks after a frame that stepped more than one.
    if(n>1)catchUp=kCatchUpTicks;
    else catchUp=catchUp>static_cast<int>(n) ? catchUp-static_cast<int>(n) : 0;
    CountStats(now,n);
    return 1u|static_cast<std::uint64_t>(n)<<32;
}

// TickWait (frame_pacing.cpp) asks this while the gate is on: QPC counts to the next slot.
std::uint64_t GateWaitCounts(std::uint64_t now) noexcept {
    return framerate::WaitCounts(gate,now,cap);
}

bool Steps() noexcept { return !gateOn || frameTicks; }

void SteamGuard() noexcept { if(Steps())reinterpret_cast<SteamFn>(steamRun)(); }
void NetGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kNet)(self); }
void EosGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kEos)(self); }
void WindGuard(void* self) { if(Steps())reinterpret_cast<VoidFn>(image+kWind)(self); }

// A 0-tick frame's wrappers: the edges the UI saw last frame cleared, the values as the sources hold them now.
void ClearForUi(unsigned char* table) noexcept {
    for(unsigned p=0;p<kPlayers;++p) {
        unsigned char* const player=table+p*kPlayerSize;
        for(unsigned at=kWrapFirst;at<kWrapEnd;at+=kWrapSize) {
            unsigned char* const w=player+at;
            const unsigned char* const src=*reinterpret_cast<unsigned char* const*>(w);
            if(src && !src[0x4D])std::memcpy(w+0xC,src+0x48,4);
            w[0x11]=w[0x12]=w[0x13]=0;
        }
        for(unsigned mask:{kMaskPressed,kMaskRepeat,kMaskReleased})std::memset(player+mask,0,4);
    }
}

void SystemUpdateHook(void* system,float dt,int n) {
    void* const input=*reinterpret_cast<void**>(image+kInputManager);
    if(input) {
        if(n>0)reinterpret_cast<WrapUpdateFn>(image+kWrapUpdate)(input,n);
        else ClearForUi(static_cast<unsigned char*>(input)+kWrapTable);
        wrapped=true;
    }
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

bool InstallGate() noexcept {
    if(!edf::Matches(image,kCountFirst,kCountFirstSig,sizeof(kCountFirstSig)) ||
       !edf::Matches(image,kCountAgain,kCountAgainSig,sizeof(kCountAgainSig))) {
        Log("FRAMERATE the main loop's tick counts are not as read (another MOD?): no frame gate");
        return false;
    }
    if(!FramePacingTickWaitInstalled()) {
        Log("FRAMERATE the frame step's wait is not ours (frame_pacing.cpp): no frame gate");
        return false;
    }
    void* const stub=MakeGateStub();
    if(!stub){Log("FRAMERATE no page near the main loop for the gate stub");return false;}
    SetFramePacingGate(&GateWaitCounts);
    // At load, before the application's init: the main loop has not run yet, so the four writes need no order.
    const bool ok=edf::PatchCode(image+kCountFirst+5,kMovEbxEax,kMovEbxEdx,2) &&
                  edf::PatchCode(image+kCountAgain+5,kMovEbxEax,kMovEbxEdx,2) &&
                  PatchCall(kCountFirst,stub) && PatchCall(kCountAgain,stub);
    if(!ok)Log("FRAMERATE the frame gate was not written whole: the main loop is in an unknown state");
    return ok;
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
    vsyncWanted=cap!=framerate::kUnlimited && refresh>1 && cap<=refresh;
    Log("FRAMERATE display %d Hz, cap %d: sync interval %d (0 for %d ticks after a catch-up)",refresh,cap,vsyncWanted ? 1 : 0,kCatchUpTicks);
    bool changed=false;
    return edf::RedirectCall(image+kSyncCall,image+kSyncSubmit,reinterpret_cast<void*>(&SyncHook),changed);
}
}  // namespace

void InstallFrameRate() noexcept {
    const Config& c=Cfg();
    if(!image)return;
    if(!c.frameRateDev) {
        if(c.frameRateCap!=framerate::kOff)
            Log("FRAMERATE FrameRateCap=%d read; rendering above the game's 59.94 Hz is a developer stage (FrameRateDev=1, "
                "docs/unlimited-fps-re.md): the game runs at its own rate",c.frameRateCap);
        return;
    }
    // The input move first: it is all FrameRateDev does at FrameRateCap=0 (latency-3), and the gate needs it.
    const bool input=InstallInputMove();
    Log("FRAMERATE FrameRateDev=1: input wrappers updated before the step=%d",input);
    if(c.frameRateCap==framerate::kOff || !input)return;
    cap=c.frameRateCap;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcFreq=static_cast<std::uint64_t>(freq.QuadPart);
    slotPeriod=framerate::PresentPeriod(qpcFreq,cap);
    // Everything a 0-tick frame would do wrong goes in before the gate that makes them: the guards, the takes, the sync.
    const int guards=InstallGuards();
    const int takes=InstallInputTakes();
    const bool sync=InstallSync();
    if(guards!=5 || takes!=2 || !sync) {
        Log("FRAMERATE guards %d/5, input takes %d/2, sync=%d: not all in, no frame gate (the game keeps its rate)",guards,takes,sync);
        return;
    }
    gateOn=InstallGate();
    Log("FRAMERATE FrameRateCap=%d: frame gate=%d, guards %d/5, input takes %d/2, sync=%d",cap,gateOn,guards,takes,sync);
}
}  // namespace crew
