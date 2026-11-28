// The performance panel's numbers (perf_hud.h).
#include "perf_hud.h"

#include <psapi.h>

#include "config.h"

namespace crew::perfhud {

namespace {
double tickMs=0;
bool chained=false;
Period period{};
LARGE_INTEGER lastFrame{};
ULONGLONG periodAt=0,cpuAt=0;
SRWLOCK lock=SRWLOCK_INIT;
Readout shown{};

ULONGLONG ProcessCpu100ns() noexcept {
    FILETIME c,e,k,u;
    if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;
    return (static_cast<ULONGLONG>(k.dwHighDateTime)<<32 | k.dwLowDateTime)+
           (static_cast<ULONGLONG>(u.dwHighDateTime)<<32 | u.dwLowDateTime);
}

void Publish(ULONGLONG now) noexcept {
    Readout r=Means(period,tickMs);
    const ULONGLONG cpu=ProcessCpu100ns();
    if(cpuAt && periodAt && now>periodAt && cpu>=cpuAt)
        r.cpuCores=static_cast<double>(cpu-cpuAt)/10000.0/static_cast<double>(now-periodAt);
    cpuAt=cpu;
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb=sizeof(m);
    if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))
        r.commitMb=static_cast<double>(m.PrivateUsage)/(1024.0*1024.0);
    // the game thread after a scene step: the team manager is the scripts' (GetTeamObjectCount takes its lock)
    r.enemies=TeamObjectCount(1);r.team=TeamObjectCount(0);r.friends=TeamObjectCount(2);
    r.at=now;
    AcquireSRWLockExclusive(&lock);
    shown=r;
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

void ScopeEnded(unsigned index,long long ticks) noexcept {
    if(index>=kScopes)return;
    if(!tickMs) {
        LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);
        tickMs=1000.0/static_cast<double>(hz.QuadPart);
    }
    if(ticks>0)period.scope[index]+=ticks;
    if(index==kRender) {   // once a frame (the render recording, before the frame's steps)
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        if(lastFrame.QuadPart) {
            const long long d=now.QuadPart-lastFrame.QuadPart;
            if(d>0 && static_cast<double>(d)*tickMs<1000.0) {   // a load or a pause between: not a frame
                ++period.frames;period.frameSum+=d;
                if(d>period.frameWorst)period.frameWorst=d;
            }
        }
        lastFrame=now;
        return;
    }
    if(index!=kCamera)return;
    ++period.steps;   // the step's last phase
    const ULONGLONG now=GetTickCount64();
    if(!periodAt){periodAt=now;cpuAt=ProcessCpu100ns();period=Period{};return;}
    if(now-periodAt<kPublishMs)return;
    if(Cfg().perfHud)Publish(now);
    else cpuAt=ProcessCpu100ns();
    periodAt=now;
    period=Period{};
}

void ScopesChained(bool all) noexcept { chained=all; }

bool Latest(Readout* out) noexcept {
    if(!chained)return false;
    static Readout last{};   // the draw thread's: a publish under way leaves the one before (never waited on)
    if(TryAcquireSRWLockShared(&lock)) {
        last=shown;
        ReleaseSRWLockShared(&lock);
    }
    const Readout r=last;
    if(!r.at || GetTickCount64()-r.at>kFreshMs)return false;
    *out=r;
    return true;
}
}  // namespace crew::perfhud
