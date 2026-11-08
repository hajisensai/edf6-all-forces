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
#include "frame_pacing.h"
#include "frame_pacing_logic.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"

namespace crew {
namespace {
constexpr unsigned kPumpSleep=0x11844EA,kTickSleep=0x11A2F65;
// The tick clock TickWait reads: the function the frame step calls after the wait, and its two words in .data.
constexpr unsigned kTickCount=0x11DAE60,kTickBase=0x2139D10,kTickLast=0x2139D18;
// The sites with what loads Sleep's argument before them: `xor ecx,ecx` (0) and `mov ecx,1`.
const unsigned char kPumpArg[]={0x33,0xC9},kTickArg[]={0xB9,0x01,0x00,0x00,0x00};
const unsigned char kPumpSig[]={0xFF,0x15,0x38,0x12,0x5D,0x00};   // call [Sleep]
const unsigned char kTickSig[]={0xFF,0x15,0xBD,0x27,0x5B,0x00};   // call [Sleep]
// 0x11DAE60 whole (docs/frame-pacing-re.md): the arithmetic frame_pacing_logic.h repeats, over kTickBase / kTickLast.
const unsigned char kTickCountSig[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D,0x0D,0xA3,0xEE,0xF5,0x00,0xE8,0x5E,0xBE,0xE8,0xFE,0x48,0x8B,0xD8,0xE8,
    0xE6,0xBE,0xE8,0xFE,0x4C,0x8B,0x05,0x97,0xEE,0xF5,0x00,0x48,0x6B,0xC8,0x64,0x49,0x2B,0xD8,0x48,0xB8,0xC3,0x55,
    0x44,0x60,0xD5,0x06,0xF0,0xAE,0x48,0xF7,0xE1,0x48,0x8B,0xC3,0x4C,0x8B,0xCA,0x33,0xD2,0x49,0xC1,0xE9,0x0C,0x49,
    0xF7,0xF1,0x48,0x83,0xF8,0x01,0x72,0x11,0x48,0x8B,0xC8,0x49,0x0F,0xAF,0xC9,0x4C,0x03,0xC1,0x4C,0x89,0x05,0x5D,
    0xEE,0xF5,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3};
constexpr LONGLONG kWait=-10000;   // 1 ms, relative, in 100 ns: the window thread's

using SleepFn=void(WINAPI*)(DWORD);
void* gameSleep=nullptr;
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
    const std::uint64_t base=*reinterpret_cast<const std::uint64_t*>(image+kTickBase);
    const std::uint64_t last=*reinterpret_cast<const std::uint64_t*>(image+kTickLast);
    const std::uint64_t counts=pacing::CountsToBoundary(static_cast<std::uint64_t>(now.QuadPart),base,last,tickPeriod);
    if(!counts)return;   // the boundary came between the game's count and ours: its next count finds it
    const std::uint64_t due=pacing::CountsTo100ns(counts,qpcFreq);   // under one tick: fits a LONGLONG
    const HANDLE timer=Timer();
    if(!timer || !Arm(timer,-static_cast<LONGLONG>(due))){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
    WaitForSingleObject(timer,INFINITE);
}
}  // namespace

bool InstallFramePacing() noexcept {
    if(!image)return false;
    const HANDLE probe=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    if(!probe){Log("PACING no high-resolution timer (error %lu): the game's waits left as they are",GetLastError());return false;}
    CloseHandle(probe);
    if(!edf::Matches(image,kPumpSleep-sizeof(kPumpArg),kPumpArg,sizeof(kPumpArg)) ||
       !edf::Matches(image,kTickSleep-sizeof(kTickArg),kTickArg,sizeof(kTickArg)) ||
       !edf::Matches(image,kTickCount,kTickCountSig,sizeof(kTickCountSig))) {
        Log("PACING the game's waits or its tick clock are not as read (another hook?): left as they are");
        return false;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);   // fixed at boot; 0x11DAE60 asks it each call (0x66D60) for the same value
    qpcFreq=static_cast<std::uint64_t>(freq.QuadPart);
    tickPeriod=pacing::TickPeriod(qpcFreq);
    if(!tickPeriod){Log("PACING QPC frequency %llu: no tick period, the game's waits left as they are",qpcFreq);return false;}
    const bool pump=edf::RedirectImportCall(image+kPumpSleep,kPumpSig,reinterpret_cast<void*>(&PumpWait),&gameSleep);
    const bool tick=edf::RedirectImportCall(image+kTickSleep,kTickSig,reinterpret_cast<void*>(&TickWait),&gameSleep);
    Log("PACING window thread wait=%d, frame step wait=%d",pump,tick);
    return pump && tick;
}
}  // namespace crew
