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
std::atomic<bool> dumped{false};
ULONGLONG lastAt=0;   // the game thread's own: the previous phase's start, for the logged step time
INIT_ONCE watchOnce=INIT_ONCE_STATIC_INIT;
ULONGLONG stuckMs=kMissionPhaseStuckMs;   // tests/mission_watch_test.cpp shortens it

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
    if(dumped.exchange(true))return;
    wchar_t path[MAX_PATH];
    if(!DumpPath(path,std::size(path))){Log("MISSION stuck dump: no path");return;}
    const HMODULE dbghelp=LoadLibraryW(L"dbghelp.dll");
    using WriteFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                                PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    const auto write=dbghelp ? reinterpret_cast<WriteFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump")) : nullptr;
    if(!write){Log("MISSION stuck dump: dbghelp.dll has no MiniDumpWriteDump");return;}
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){Log("MISSION stuck dump: %ls not written (error %lu)",path,GetLastError());return;}
    const auto type=static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|
                                               MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithProcessThreadData);
    const BOOL ok=write(GetCurrentProcess(),GetCurrentProcessId(),file,type,nullptr,nullptr,nullptr);
    const DWORD error=ok ? 0 : GetLastError();
    CloseHandle(file);
    if(ok)Log("MISSION stuck dump: %ls (phase %s; every thread's stack)",path,stuck);
    else Log("MISSION stuck dump: MiniDumpWriteDump failed (error %lu)",error);
}

DWORD WINAPI Watch(void*) noexcept {
    const char* reported=nullptr;
    ULONGLONG reportedAt=0;
    for(;;) {
        Sleep(1000);
        const char* now=phase.load();
        const ULONGLONG at=phaseAt.load();
        if(!now) {
            if(reported)Log("MISSION phase %s ended after %llu s",reported,(GetTickCount64()-reportedAt)/1000);
            reported=nullptr;
            continue;
        }
        if(now==reported && at==reportedAt)continue;
        if(reported)Log("MISSION phase %s ended after %llu s",reported,(at-reportedAt)/1000);
        reported=nullptr;
        const ULONGLONG age=GetTickCount64()-at;
        if(age<stuckMs)continue;
        reported=now;reportedAt=at;
        Log("MISSION phase %s still running after %llu s (game thread %lu)",now,age/1000,phaseThread.load());
        LogMemory("stuck mission phase");
        WriteHangDump(now);
    }
}

BOOL CALLBACK StartWatch(PINIT_ONCE,void*,void**) noexcept {
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
