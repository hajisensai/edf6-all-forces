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
// number of whole 1/60 s ticks since the last frame (QueryPerformanceCounter). EDF.dll never raises the timer
// resolution, so Sleep(1) sleeps until the next system tick (15.6 ms by default): the frame's wait rounds up to
// that grid and a frame now and then takes two ticks (33 ms) though the work fits in one. Now it waits 1 ms on a
// high-resolution timer, without changing the process's timer resolution.
#include "frame_pacing.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"

namespace crew {
namespace {
constexpr unsigned kPumpSleep=0x11844EA,kTickSleep=0x11A2F65;
// The sites with what loads Sleep's argument before them: `xor ecx,ecx` (0) and `mov ecx,1`.
const unsigned char kPumpArg[]={0x33,0xC9},kTickArg[]={0xB9,0x01,0x00,0x00,0x00};
const unsigned char kPumpSig[]={0xFF,0x15,0x38,0x12,0x5D,0x00};   // call [Sleep]
const unsigned char kTickSig[]={0xFF,0x15,0xBD,0x27,0x5B,0x00};   // call [Sleep]
constexpr LONGLONG kWait=-10000;   // 1 ms, relative, in 100 ns

using SleepFn=void(WINAPI*)(DWORD);
void* gameSleep=nullptr;

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

bool Arm(HANDLE timer) noexcept {
    LARGE_INTEGER due;due.QuadPart=kWait;
    return SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)!=FALSE;
}

void WINAPI PumpWait(DWORD ms) noexcept {
    const HANDLE timer=Timer();
    if(!timer || !Arm(timer)){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
    MsgWaitForMultipleObjectsEx(1,&timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
}

void WINAPI TickWait(DWORD ms) noexcept {
    const HANDLE timer=Timer();
    if(!timer || !Arm(timer)){reinterpret_cast<SleepFn>(gameSleep)(ms);return;}
    WaitForSingleObject(timer,INFINITE);
}
}  // namespace

bool InstallFramePacing() noexcept {
    if(!image)return false;
    const HANDLE probe=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    if(!probe){Log("PACING no high-resolution timer (error %lu): the game's waits left as they are",GetLastError());return false;}
    CloseHandle(probe);
    if(!edf::Matches(image,kPumpSleep-sizeof(kPumpArg),kPumpArg,sizeof(kPumpArg)) ||
       !edf::Matches(image,kTickSleep-sizeof(kTickArg),kTickArg,sizeof(kTickArg))) {
        Log("PACING the game's waits are not as read (another hook?): left as they are");
        return false;
    }
    const bool pump=edf::RedirectImportCall(image+kPumpSleep,kPumpSig,reinterpret_cast<void*>(&PumpWait),&gameSleep);
    const bool tick=edf::RedirectImportCall(image+kTickSleep,kTickSig,reinterpret_cast<void*>(&TickWait),&gameSleep);
    Log("PACING window thread wait=%d, frame step wait=%d",pump,tick);
    return pump && tick;
}
}  // namespace crew
