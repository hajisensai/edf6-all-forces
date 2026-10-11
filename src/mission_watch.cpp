#include "mission_watch.h"
#include "crew.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
std::atomic<const char*> phase{nullptr};
std::atomic<ULONGLONG> phaseAt{0};
std::atomic<DWORD> phaseThread{0};
ULONGLONG lastAt=0;   // the game thread's own: the previous phase's start, for the logged step time
INIT_ONCE watchOnce=INIT_ONCE_STATIC_INIT;
// tests/mission_watch_test.cpp shortens these.
ULONGLONG stuckMs=kMissionPhaseStuckMs,dumpMs=kMissionPhaseDumpMs;
DWORD pollMs=1000;

using WriteFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                            PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
// Resolved when the watchdog starts, nothing stuck yet: a game thread stuck holding the loader lock would otherwise
// stop the watchdog in LoadLibrary before it wrote anything.
WriteFn writeDump=nullptr;
wchar_t dumpPath[MAX_PATH]{};

// <this DLL's folder>\EDF6VehicleCrew.hang.dmp: Mods\Plugins, where the installer's "send the logs" looks.
bool DumpPath(wchar_t* out,std::size_t capacity) noexcept {
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DumpPath),&self))return false;
    const DWORD n=GetModuleFileNameW(self,out,static_cast<DWORD>(capacity));
    wchar_t* slash=n && n<capacity ? wcsrchr(out,L'\\') : nullptr;
    return slash && (*++slash=0,wcscat_s(out,capacity,L"EDF6VehicleCrew.hang.dmp")==0);
}

// The process's threads and stacks, and the memory they point at: enough for where each one waits, not a full dump.
void WriteHangDump(const char* stuck) noexcept {
    if(!writeDump || !dumpPath[0]){Log("MISSION stuck dump: not written (no dbghelp MiniDumpWriteDump or no path)");return;}
    const HANDLE file=CreateFileW(dumpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){Log("MISSION stuck dump: %ls not written (error %lu)",dumpPath,GetLastError());return;}
    const auto type=static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|
                                               MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithProcessThreadData);
    const BOOL ok=writeDump(GetCurrentProcess(),GetCurrentProcessId(),file,type,nullptr,nullptr,nullptr);
    const DWORD error=ok ? 0 : GetLastError();
    LARGE_INTEGER size{};
    GetFileSizeEx(file,&size);
    CloseHandle(file);
    if(ok)Log("MISSION stuck dump: %ls, %lld KB (phase %s; every thread's stack)",dumpPath,size.QuadPart>>10,stuck);
    else Log("MISSION stuck dump: MiniDumpWriteDump failed (error %lu)",error);
}

DWORD WINAPI Watch(void*) noexcept {
    const char* seen=nullptr;   // the phase (and its start) this loop last looked at
    ULONGLONG seenAt=0;
    bool logged=false,dumped=false;
    for(;;) {
        Sleep(pollMs);
        const char* now=phase.load();
        const ULONGLONG at=phaseAt.load();
        if(now!=seen || at!=seenAt) {
            // A new phase (or none): the one reported stuck, if it was, ended.
            if(logged)Log("MISSION phase %s ended after %llu s",seen,((now ? at : GetTickCount64())-seenAt)/1000);
            seen=now;seenAt=at;logged=false;
        }
        if(!now)continue;
        const ULONGLONG age=GetTickCount64()-at;
        if(!logged && age>=stuckMs) {
            logged=true;
            Log("MISSION phase %s still running after %llu s (game thread %lu)",now,age/1000,phaseThread.load());
            LogMemory("stuck mission phase");
        }
        if(logged && !dumped && age>=dumpMs) {
            dumped=true;
            Log("MISSION phase %s: writing a dump after %llu s",now,age/1000);
            WriteHangDump(now);
        }
    }
}

BOOL CALLBACK StartWatch(PINIT_ONCE,void*,void**) noexcept {
    if(const HMODULE dbghelp=LoadLibraryW(L"dbghelp.dll"))writeDump=reinterpret_cast<WriteFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));
    if(!DumpPath(dumpPath,std::size(dumpPath)))dumpPath[0]=0;
    const HANDLE t=CreateThread(nullptr,0,&Watch,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    return TRUE;
}
}  // namespace

void WatchMissionPhase(const char* next) noexcept {
    InitOnceExecuteOnce(&watchOnce,&StartWatch,nullptr,nullptr);
    const ULONGLONG now=GetTickCount64();
    const char* previous=phase.load();
    if(next)Log("MISSION phase %s (%s %llu ms)",next,previous ? previous : "since none",previous ? now-lastAt : 0ull);
    else if(previous)Log("MISSION phases done (%s %llu ms)",previous,now-lastAt);
    lastAt=now;
    phaseThread.store(GetCurrentThreadId());
    phaseAt.store(now);
    phase.store(next);
}
}  // namespace crew
