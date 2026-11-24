// The game's own two waits, made to wait (docs/frame-pacing-re.md).
//
// The window thread (0x1184460): `while(!quit){ 0x1183880(); Sleep(0); }`. 0x1183880 pumps the window's messages,
// takes GetKeyboardState into 0x21360E0 and, with the mouse locked, the cursor's offset from the window's centre
// into 0x1FEEE20 (then centres it again); the game thread reads both once per frame. Sleep(0) gives the processor
// up only to a ready thread of the same priority, so the loop spins: one core busy the whole time the game runs
// (rip sampler, M001: the thread's CPU 82 s of 90 s, 72 % of its samples in Sleep(0) / PeekMessage).
// Now it waits for input or 1 ms (a high-resolution timer): input still wakes it at once and the mouse offset is a
// sum, so nothing the game reads is later or lost.
//
// The game thread's frame step (0x11A2F43..0x11A2F74): `while(!(n = 0x11DAE60()))Sleep(1);`, where 0x11DAE60 is the
// number of whole 1/59.94 s ticks since the last tick boundary (QueryPerformanceCounter). EDF.dll never raises the
// timer resolution, so Sleep(1) sleeps until the next system tick (15.6 ms by default): the frame's wait rounds up to
// that grid and a frame now and then takes two ticks (33 ms) though the work fits in one. Now it reads the game's own
// tick clock (frame_pacing_logic.h: its start-up QPC 0x2139D10 and last boundary 0x2139D18) and waits once on a
// high-resolution timer until the next boundary, without changing the process's timer resolution; the loop's own
// call to 0x11DAE60 after the wait then finds the tick.
//
// The game thread's wait for the render thread (0x111CC20, every frame from 0x11A305A and some 40 load paths): it
// records a signal command for a fresh auto-reset event, then `while(!0x50E00(&event)){ if(over 1 s) pump; Sleep(0); }`
// where 0x50E00 is WaitForSingleObject(event, 0) == 0. A zero-timeout poll and Sleep(0): the game thread spins one
// core the whole time it waits (docs/frame-pacing-re.md). The loop's call to 0x50E00 at 0x111CD49 now waits on the
// same event for up to kFenceWaitMs: it wakes as soon as the render thread signals, and the loop's own 1 s message
// pump still runs (every kFenceWaitMs once past the second instead of every spin).
//
// The DXGI queue: EDF.dll never calls IDXGIDevice1::SetMaximumFrameLatency (its only use of that interface is
// GetAdapter at 0x111B65E), so the CPU may run up to DXGI's default 3 frames ahead of the display. After the game's
// device init 0x111B420 (its only caller 0x11A33DF) the plugin sets ini MaxFrameLatency (default 0: not set) on the device
// (renderer 0x2136FF0 +0x80, the ID3D11Device the init QIs at 0x111B63C). The device is made once; the swap chain it
// rebuilds (0x111B220) keeps the device's setting.
#include "frame_pacing.h"
#include "frame_pacing_logic.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <dxgi.h>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kPumpSleep=0x11844EA,kTickSleep=0x11A2F65;
// The tick clock TickWait reads: the function the frame step calls after the wait, and its two words in .data.
constexpr unsigned kTickCount=0x11DAE60,kTickBase=0x2139D10,kTickLast=0x2139D18;
// The sites with what loads Sleep's argument before them: `xor ecx,ecx` (0) and `mov ecx,1`.
const unsigned char kPumpArg[]={0x33,0xC9},kTickArg[]={0xB9,0x01,0x00,0x00,0x00};
const unsigned char kPumpSig[]={0xFF,0x15,0x38,0x12,0x5D,0x00};   // call [Sleep]
const unsigned char kTickSig[]={0xFF,0x15,0xBD,0x27,0x5B,0x00};   // call [Sleep]
// The frame step's loop back to the tick count after the wait: `je 0x11A2F60` (frame_pacing_logic.h ForeignTickWait).
constexpr unsigned kTickBranch=0x11A2F74;
const unsigned char kTickBranchSig[]={0x74,0xEA};
// 0x11DAE60 whole (docs/frame-pacing-re.md): the arithmetic frame_pacing_logic.h repeats, over kTickBase / kTickLast.
const unsigned char kTickCountSig[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D,0x0D,0xA3,0xEE,0xF5,0x00,0xE8,0x5E,0xBE,0xE8,0xFE,0x48,0x8B,0xD8,0xE8,
    0xE6,0xBE,0xE8,0xFE,0x4C,0x8B,0x05,0x97,0xEE,0xF5,0x00,0x48,0x6B,0xC8,0x64,0x49,0x2B,0xD8,0x48,0xB8,0xC3,0x55,
    0x44,0x60,0xD5,0x06,0xF0,0xAE,0x48,0xF7,0xE1,0x48,0x8B,0xC3,0x4C,0x8B,0xCA,0x33,0xD2,0x49,0xC1,0xE9,0x0C,0x49,
    0xF7,0xF1,0x48,0x83,0xF8,0x01,0x72,0x11,0x48,0x8B,0xC8,0x49,0x0F,0xAF,0xC9,0x4C,0x03,0xC1,0x4C,0x89,0x05,0x5D,
    0xEE,0xF5,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3};
// The render fence's loop: `xor ecx,ecx; call [Sleep]; lea rcx,[rsp+80h]; call 0x50E00; test al,al; je loop`, and
// 0x50E00 whole: `WaitForSingleObject([rcx],0); sete al`.
constexpr unsigned kFenceCall=0x111CD49,kFencePoll=0x50E00,kFenceLoop=0x111CD39;
const unsigned char kFenceLoopSig[]={0x33,0xC9,0xFF,0x15,0xE7,0x89,0x63,0x00,0x48,0x8D,0x8C,0x24,0x80,0x00,0x00,0x00,
    0xE8,0xB2,0x40,0xF3,0xFE,0x84,0xC0,0x74,0x8E};
const unsigned char kFencePollSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x09,0x33,0xD2,0xFF,0x15,0xE9,0x48,0x70,0x01,0x85,
    0xC0,0x0F,0x94,0xC0,0x48,0x83,0xC4,0x28,0xC3};
constexpr DWORD kFenceWaitMs=10;
// The device init's call and the renderer whose +0x80 holds the device.
constexpr unsigned kDeviceInitCall=0x11A33DF,kDeviceInit=0x111B420,kRenderer=0x2136FF0,kRendererDevice=0x80;
constexpr LONGLONG kWait=-10000;   // 1 ms, relative, in 100 ns: the window thread's

using SleepFn=void(WINAPI*)(DWORD);
void* gameSleep=nullptr;
bool tickWaitInstalled=false;

// EDF6VR loaded already, or beside this plugin to be loaded after it (frame_pacing_logic.h ForeignTickWait).
bool EdfVrInstalled() noexcept {
    if(GetModuleHandleW(L"EDF6VR.dll"))return true;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&EdfVrInstalled),&self))return false;
    wchar_t path[MAX_PATH]{};
    const DWORD n=GetModuleFileNameW(self,path,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    wchar_t* const slash=wcsrchr(path,L'\\');
    if(!slash || wcscpy_s(slash+1,static_cast<std::size_t>(path+MAX_PATH-(slash+1)),L"EDF6VR.dll"))return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
}
GateWaitFn gateWait=nullptr;   // set once at load, before the main loop runs
std::uint64_t qpcFreq=0,tickPeriod=0;

// One timer per thread (set and waited on by its own thread only), closed when the thread ends.
struct ThreadTimer {
    HANDLE h=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    ThreadTimer()=default;
    ThreadTimer(const ThreadTimer&)=delete;
    ThreadTimer& operator=(const ThreadTimer&)=delete;
    ~ThreadTimer(){if(h)CloseHandle(h);}
};
HANDLE Timer() noexcept {
    thread_local ThreadTimer timer;
    return timer.h;
}

bool Arm(HANDLE timer,LONGLONG due100ns) noexcept {
    LARGE_INTEGER due;due.QuadPart=due100ns;
    return SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)!=FALSE;
}

void WINAPI PumpWait(DWORD ms) noexcept {
    const HANDLE timer=Timer();
    if(!timer || !Arm(timer,kWait)){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
    MsgWaitForMultipleObjectsEx(1,&timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
}

// Called only while 0x11DAE60 has just returned 0, on the game thread, the only one that writes the two words.
void WINAPI TickWait(DWORD ms) noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if(gateWait) {   // the frame gate's slot, not the tick (frame_rate.cpp)
        const std::uint64_t counts=gateWait(static_cast<std::uint64_t>(now.QuadPart));
        if(!counts)return;
        const HANDLE timer=Timer();
        if(!timer || !Arm(timer,-static_cast<LONGLONG>(pacing::CountsTo100ns(counts,qpcFreq)))){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
        WaitForSingleObject(timer,INFINITE);
        return;
    }
    const std::uint64_t base=*reinterpret_cast<const std::uint64_t*>(image+kTickBase);
    const std::uint64_t last=*reinterpret_cast<const std::uint64_t*>(image+kTickLast);
    const std::uint64_t counts=pacing::CountsToBoundary(static_cast<std::uint64_t>(now.QuadPart),base,last,tickPeriod);
    if(!counts)return;   // the boundary came between the game's count and ours: its next count finds it
    const std::uint64_t due=pacing::CountsTo100ns(counts,qpcFreq);   // under one tick: fits a LONGLONG
    const HANDLE timer=Timer();
    if(!timer || !Arm(timer,-static_cast<LONGLONG>(due))){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
    WaitForSingleObject(timer,INFINITE);
}
// 0x50E00's place in the fence loop: whether the render thread has signalled, now waiting for it.
bool FenceWait(const HANDLE* event) noexcept {
    return WaitForSingleObject(*event,kFenceWaitMs)==WAIT_OBJECT_0;
}

void SetFrameLatency(void* renderer) noexcept {
    IUnknown* const device=*reinterpret_cast<IUnknown* const*>(static_cast<unsigned char*>(renderer)+kRendererDevice);
    if(!device){Log("LATENCY the device init returned no device: the DXGI queue left as it is");return;}
    IDXGIDevice1* dxgi=nullptr;
    HRESULT hr=device->QueryInterface(__uuidof(IDXGIDevice1),reinterpret_cast<void**>(&dxgi));
    if(FAILED(hr) || !dxgi){Log("LATENCY no IDXGIDevice1 (0x%08lX): the DXGI queue left as it is",static_cast<unsigned long>(hr));return;}
    UINT before=0;
    if(FAILED(dxgi->GetMaximumFrameLatency(&before)))before=0;
    const UINT want=static_cast<UINT>(Cfg().maxFrameLatency);
    hr=dxgi->SetMaximumFrameLatency(want);
    dxgi->Release();
    if(FAILED(hr))Log("LATENCY SetMaximumFrameLatency(%u) failed (0x%08lX): %u frames queued at most",want,static_cast<unsigned long>(hr),before);
    else Log("LATENCY max frame latency %u (was %u)",want,before);
}

using DeviceInitFn=bool(*)(void*,void*);
// 0x111B420(renderer, params): makes the device and the swap chain, true when it did.
bool DeviceInitHook(void* renderer,void* params) {
    const bool made=reinterpret_cast<DeviceInitFn>(image+kDeviceInit)(renderer,params);
    if(made)SetFrameLatency(renderer);
    return made;
}
}  // namespace

bool FramePacingTickWaitInstalled() noexcept { return tickWaitInstalled; }
void SetFramePacingGate(GateWaitFn wait) noexcept { gateWait=wait; }

bool InstallRenderFence() noexcept {
    if(!image)return false;
    bool changed=false,ok=false;
    if(!edf::Matches(image,kFenceLoop,kFenceLoopSig,sizeof(kFenceLoopSig)) ||
       !edf::Matches(image,kFencePoll,kFencePollSig,sizeof(kFencePollSig)))
        Log("PACING the render fence loop 0x111CC20 is not as read (another hook?): left as it is");
    else ok=edf::RedirectCall(image+kFenceCall,image+kFencePoll,reinterpret_cast<void*>(&FenceWait),changed);
    Log("PACING render fence wait=%d",ok);
    return ok;
}

bool InstallFrameLatency() noexcept {
    if(!image)return false;
    if(!Cfg().maxFrameLatency){Log("LATENCY MaxFrameLatency=0: the DXGI queue left at the game's (3)");return false;}
    bool changed=false;
    const bool ok=edf::RedirectCall(image+kDeviceInitCall,image+kDeviceInit,reinterpret_cast<void*>(&DeviceInitHook),changed);
    if(!ok)Log("LATENCY the device init call 0x11A33DF is not as read (another hook?): the DXGI queue left as it is");
    return ok;
}

bool InstallFramePacing() noexcept {
    if(!image)return false;
    const HANDLE probe=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    if(!probe){Log("PACING no high-resolution timer (error %lu): the game's waits left as they are",GetLastError());return false;}
    CloseHandle(probe);
    // Each wait on its own: PumpWait reads no tick clock, so the clock's signature and period gate TickWait only.
    bool pump=false,tick=false;
    if(edf::Matches(image,kPumpSleep-sizeof(kPumpArg),kPumpArg,sizeof(kPumpArg)))
        pump=edf::RedirectImportCall(image+kPumpSleep,kPumpSig,reinterpret_cast<void*>(&PumpWait),&gameSleep);
    else Log("PACING the window thread's wait is not as read (another hook?): left as it is");
    const pacing::Foreign foreign=pacing::ForeignTickWait(image+kTickSleep,kTickSig,image+kTickBranch,kTickBranchSig,EdfVrInstalled());
    if(foreign==pacing::Foreign::kVr)
        Log("PACING EDF6VR is installed (it removes the frame step's wait itself in a mission): the wait left to it, no frame gate");
    else if(foreign!=pacing::Foreign::kNone)
        Log("PACING the frame step's %s (0x%X) is not the game's (an FPS unlimiter patch?): the wait left to it, no frame gate",
            foreign==pacing::Foreign::kWait ? "wait" : "loop branch",foreign==pacing::Foreign::kWait ? kTickSleep : kTickBranch);
    else if(!edf::Matches(image,kTickSleep-sizeof(kTickArg),kTickArg,sizeof(kTickArg)))
        Log("PACING the frame step's wait is not as read (another hook?): left as it is");
    else if(!edf::Matches(image,kTickCount,kTickCountSig,sizeof(kTickCountSig)))
        Log("PACING the tick clock 0x11DAE60 is not as read (another MOD or hook?): the frame step's wait left as it is");
    else {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);   // fixed at boot; 0x11DAE60 asks it each call (0x66D60) for the same value
        qpcFreq=static_cast<std::uint64_t>(freq.QuadPart);
        tickPeriod=pacing::TickPeriod(qpcFreq);
        if(!tickPeriod)Log("PACING QPC frequency %llu: no tick period, the frame step's wait left as it is",qpcFreq);
        else tick=edf::RedirectImportCall(image+kTickSleep,kTickSig,reinterpret_cast<void*>(&TickWait),&gameSleep);
    }
    tickWaitInstalled=tick;
    Log("PACING window thread wait=%d, frame step wait=%d",pump,tick);
    return pump && tick;
}
}  // namespace crew
