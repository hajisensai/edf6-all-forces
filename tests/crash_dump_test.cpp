// The plugin's crash dump (src/crash_dump.cpp; docs/feedback-2026-10-18-crash-dump.md: testers' crash reports came
// with no dump). Each case runs in a child process of this test that installs the dump and then crashes the way the
// case says; the parent checks the exit code, the dump (its exception stream: the code and the crashed thread) and
// the log (diagnostics appended only after the dump, even while the real logger's locks are held). A case that must not dump (exceptions a
// __try or a catch handles) checks there is none. The child's own filter, set before the plugin's, ends the process
// once the chain reaches it, and ucrtbase's abort is told not to fast-fail, so no case goes on to Windows' error
// reporting. __fastfail is not a case: nothing in the process runs for it (crash_dump.h).
// The peer cases load a stand-in EDF6AutoTurret.dll (tests/crash_flush_peer.cpp, no writer thread of its own) whose
// buffered lines must reach its log through the crash path (common/log.h kCrashFlush), before or after the install.
#include "../src/crash_dump.cpp"
#include "../common/log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
wchar_t logPath[MAX_PATH]{};
bool endWhenWritten=false;   // the invalid parameter case: its handler fast-fails after the dump, so the test ends here
bool failCrashLog=false;
edf::logfile::Buffer<4096> logBuffer;
constexpr UINT kChained=0xC0DE0001;    // the child's own filter, behind the plugin's, ended the process
constexpr UINT kLate=0xC0DE0002;       // a filter set after the plugin's (through the redirect) ended it
constexpr UINT kWritten=0xC0DE0003;    // the invalid parameter case ended after its dump
constexpr UINT kAbortExit=3;           // abort() without fast fail: _exit(3)

void Append(const char* text) noexcept {
    const HANDLE h=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    DWORD wrote=0;
    WriteFile(h,text,static_cast<DWORD>(std::strlen(text)),&wrote,nullptr);
    WriteFile(h,"\n",1,&wrote,nullptr);
    CloseHandle(h);
}
}  // namespace

namespace crew {
void Log(const char* format,...) noexcept {
    char text[1000];va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    logBuffer.Append(text,std::strlen(text));
    logBuffer.Append("\n",1);
    logBuffer.Drain([](const char* bytes,std::size_t size,unsigned) noexcept { edf::logfile::AppendCrashLine(logPath,bytes,size); });
}
void FlushLog() noexcept {
    logBuffer.Drain([](const char* bytes,std::size_t size,unsigned) noexcept { edf::logfile::AppendCrashLine(logPath,bytes,size); },true);
}
void CrashLog(const char* text,std::size_t size) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA dump{};
    if(GetFileAttributesExW(dumpPath,GetFileExInfoStandard,&dump) && (dump.nFileSizeHigh || dump.nFileSizeLow))Append("DUMP_READY");
    // The same raw append used by plugin.cpp, without the logger's buffer/file locks or printf formatting.
    edf::logfile::AppendCrashLine(failCrashLog ? L"?:\\invalid\\log.txt" : logPath,text,size);
    if(endWhenWritten)TerminateProcess(GetCurrentProcess(),kWritten);
}
}  // namespace crew

namespace {
// ---- the child ----
// The exe's own filter needs a real stack, as the game's filters behind the plugin's do (TerminateProcess alone first
// runs RtlReportSilentProcessExit, 2.7 KB): 64 KB here, more than a stack overflow ever leaves its thread. It notes
// the thread that called it: the crashed one, but the dumping thread after a stack overflow. HungryHandler's measure
// is written here too: the overflowed thread has no stack for it (the dumping thread runs after it is set).
std::size_t handlerFree=0,handlerAte=0;
__declspec(noinline) void Chained() noexcept {
    volatile char frame[64*1024];
    frame[0]=1;frame[sizeof(frame)-1]=frame[0];
    char t[64];_snprintf_s(t,sizeof(t),_TRUNCATE,"TEST chained on thread %lu",GetCurrentThreadId());Append(t);
    if(handlerFree){_snprintf_s(t,sizeof(t),_TRUNCATE,"TEST handler free %zu ate %zu",handlerFree,handlerAte);Append(t);}
}
LONG WINAPI Last(EXCEPTION_POINTERS*) { Chained(); TerminateProcess(GetCurrentProcess(),kChained); return EXCEPTION_CONTINUE_SEARCH; }
LONG WINAPI Late(EXCEPTION_POINTERS*) { TerminateProcess(GetCurrentProcess(),kLate); return EXCEPTION_CONTINUE_SEARCH; }

void Mark() noexcept { char t[64];_snprintf_s(t,sizeof(t),_TRUNCATE,"TEST crash thread %lu",GetCurrentThreadId());Append(t); }

DWORD WINAPI AccessViolation(void*) {
    Mark();
    *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10))=1;
    return 0;
}
DWORD WINAPI BufferLockedCrash(void*) {
    Mark();
    AcquireSRWLockExclusive(&logBuffer.bufferLock);
    *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10))=1;
    return 0;
}
DWORD WINAPI FileLockedCrash(void*) {
    Mark();
    AcquireSRWLockExclusive(&logBuffer.fileLock);
    *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10))=1;
    return 0;
}
DWORD WINAPI Throw(void*) {
    Mark();
    throw std::runtime_error("nobody catches this");
}
__declspec(noinline) int Deep(int n) {
    volatile char pad[4096];
    pad[0]=static_cast<char>(n);
    return n ? Deep(n+1)+pad[0] : 0;
}
DWORD WINAPI Overflow(void*) {
    Mark();
    ULONG guarantee=0;   // 0: only reads this thread's guarantee back
    SetThreadStackGuarantee(&guarantee);
    char t[64];_snprintf_s(t,sizeof(t),_TRUNCATE,"TEST stack guarantee %lu",guarantee);Append(t);
    return static_cast<DWORD>(Deep(1));
}
// What else runs on an overflowed thread before the plugin's filter: other modules' first-chance vectored handlers
// (overlays and the like register them) and an exception frame whose size is the CPU's XSAVE state. HungryHandler
// stands for them: it takes kHungry, but always leaves kHandlerKeep below it for the dispatch, our filter and its
// wait (they fail with 4 KB left, the reserve's last page counted, and go through with 5 KB: Windows 11 26200). Its
// free stack (from the reserve's bottom) measured on Windows 11 26200 with AVX-512: 36.8-40.9 KB with the guarantee,
// 12.2-16.3 KB with Windows' default, so 16 KB is taken there, 8.3 KB at most without it. A CPU with a larger
// exception frame (more XSAVE state) leaves less at the handler: it then takes less rather than ending the process,
// and the parent's check (at least kHungryLeast taken, more than the default could ever give) says so with the numbers.
// Without the guarantee, the default leaves no room for 12 KB: the CI's occasional "stack" failure (0xC0000005, no
// dump at all) was that default running out with no handler of this kind.
constexpr std::size_t kHungry=16*1024,kHandlerKeep=8*1024,kHungryLeast=12*1024;
__declspec(noinline) void UseStack(std::size_t n) noexcept {
    volatile char* bytes=static_cast<volatile char*>(_alloca(n));
    bytes[0]=1;bytes[n-1]=bytes[0];
}
LONG WINAPI HungryHandler(EXCEPTION_POINTERS* ep) noexcept {
    if(ep->ExceptionRecord->ExceptionCode!=EXCEPTION_STACK_OVERFLOW)return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR low=0,high=0;
    GetCurrentThreadStackLimits(&low,&high);
    handlerFree=reinterpret_cast<ULONG_PTR>(_AddressOfReturnAddress())-low;
    handlerAte=handlerFree>kHandlerKeep ? (std::min)(kHungry,handlerFree-kHandlerKeep) : 0;
    if(handlerAte)UseStack(handlerAte);
    return EXCEPTION_CONTINUE_SEARCH;
}
DWORD WINAPI Handled(void*) {
    __try { *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10))=1; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
// The stand-in EDF6AutoTurret.dll (CRASH_PEER_DLL, CMakeLists.txt): lines buffered there just before the crash.
using PeerLineFn=void(__cdecl*)(const char*);
using PeerStartFn=void(__cdecl*)(const wchar_t*);
using PeerLockFn=void(__cdecl*)();
PeerLineFn peerLine=nullptr;
PeerLockFn peerLock=nullptr;
bool peerLocked=false;
bool LoadPeer(const wchar_t* dir) noexcept {
    const HMODULE peer=LoadLibraryW(CRASH_PEER_DLL);
    if(!peer)return false;
    peerLine=reinterpret_cast<PeerLineFn>(GetProcAddress(peer,"PeerLine"));
    peerLock=reinterpret_cast<PeerLockFn>(GetProcAddress(peer,"PeerLock"));
    const auto start=reinterpret_cast<PeerStartFn>(GetProcAddress(peer,"PeerStart"));
    if(!peerLine || !peerLock || !start)return false;
    wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\peer.log",dir);
    start(log);
    return true;
}
DWORD WINAPI PeerCrash(void*) {
    Mark();
    for(int i=1;i<=20;++i){char t[32];_snprintf_s(t,sizeof(t),_TRUNCATE,"PEER line %d",i);peerLine(t);}
    peerLine("PEER last line before the crash");
    if(peerLocked)peerLock();
    *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10))=1;
    return 0;
}

HANDLE gate=nullptr;
DWORD WINAPI Racer(void*) {
    WaitForSingleObject(gate,INFINITE);
    Mark();
    *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x20))=1;
    return 0;
}

void Join(LPTHREAD_START_ROUTINE f) { const HANDLE t=CreateThread(nullptr,0,f,nullptr,0,nullptr);WaitForSingleObject(t,INFINITE);CloseHandle(t); }

int Child(const char* mode,const wchar_t* dir) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    swprintf_s(logPath,L"%ls\\log.txt",dir);
    SetUnhandledExceptionFilter(&Last);   // the exe's own filter, there before the plugin loads (EDF6.exe's CRT has one)
    const HMODULE ucrt=LoadLibraryW(L"ucrtbase.dll");
    using AbortBehavior=unsigned(__cdecl*)(unsigned,unsigned);
    if(auto set=reinterpret_cast<AbortBehavior>(GetProcAddress(ucrt,"_set_abort_behavior")))set(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    if((!std::strcmp(mode,"peer") || !std::strcmp(mode,"peerlock")) && !LoadPeer(dir)){Append("TEST no peer");return 2;}
    if(!crew::InstallCrashDump(dir)){Append("TEST install failed");return 2;}
    using VoidFn=void(__cdecl*)();
    if(!std::strcmp(mode,"av"))Join(&AccessViolation);
    else if(!std::strcmp(mode,"bufferlock"))Join(&BufferLockedCrash);
    else if(!std::strcmp(mode,"filelock"))Join(&FileLockedCrash);
    else if(!std::strcmp(mode,"nolog")){failCrashLog=true;Join(&BufferLockedCrash);}
    else if(!std::strcmp(mode,"peer"))Join(&PeerCrash);
    else if(!std::strcmp(mode,"peerlock")){peerLocked=true;Join(&PeerCrash);}
    else if(!std::strcmp(mode,"latepeer")) {   // loaded after the crash dump: found at the mission start
        if(!LoadPeer(dir)){Append("TEST no peer");return 2;}
        crew::CheckCrashFilter();
        Join(&PeerCrash);
    }
    else if(!std::strcmp(mode,"cpp"))Join(&Throw);
    else if(!std::strcmp(mode,"stack"))Join(&Overflow);
    else if(!std::strcmp(mode,"stackhandler")){AddVectoredExceptionHandler(1,&HungryHandler);Join(&Overflow);}
    else if(!std::strcmp(mode,"abort")){Mark();reinterpret_cast<VoidFn>(GetProcAddress(ucrt,"abort"))();}
    else if(!std::strcmp(mode,"terminate")){Mark();reinterpret_cast<VoidFn>(GetProcAddress(ucrt,"terminate"))();}
    else if(!std::strcmp(mode,"ownabort")){Mark();std::abort();}
    else if(!std::strcmp(mode,"invalid")) {
        endWhenWritten=true;
        Mark();
        using CloseFn=int(__cdecl*)(FILE*);
        reinterpret_cast<CloseFn>(GetProcAddress(ucrt,"fclose"))(nullptr);
    } else if(!std::strcmp(mode,"handled")) {
        Join(&Handled);
        try { throw std::runtime_error("caught"); } catch(const std::exception&) {}
        crew::CheckCrashFilter();   // a mission start with nothing replaced: no change, no dump
        Append("TEST no crash");
        return 0;
    } else if(!std::strcmp(mode,"late")) {
        const LPTOP_LEVEL_EXCEPTION_FILTER before=SetUnhandledExceptionFilter(&Late);
        Append(before==&Last ? "TEST late got the filter behind" : "TEST late got something else");
        Join(&AccessViolation);
    } else if(!std::strcmp(mode,"race")) {
        gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        HANDLE t[4];
        for(auto& h:t)h=CreateThread(nullptr,0,&Racer,nullptr,0,nullptr);
        Sleep(100);
        SetEvent(gate);
        WaitForMultipleObjects(4,t,TRUE,INFINITE);
    }
    Append("TEST no crash");
    return 0;
}

// ---- the parent ----
int failures=0,checks=0;
void Check(bool ok,const std::string& what){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",what.c_str());}}

std::string Read(const wchar_t* path) {
    std::string out;
    FILE* f=nullptr;
    if(_wfopen_s(&f,path,L"rb") || !f)return out;
    char buf[4096];
    for(std::size_t n;(n=std::fread(buf,1,sizeof(buf),f))>0;)out.append(buf,n);
    std::fclose(f);
    return out;
}

struct Dumped { bool found=false; DWORD code=0,thread=0; ULONG64 address=0; long long size=0; std::wstring name; };

Dumped ReadDump(const wchar_t* dir) {
    Dumped d;
    wchar_t pattern[MAX_PATH];swprintf_s(pattern,L"%ls\\*.plugin.dmp",dir);
    WIN32_FIND_DATAW fd{};
    const HANDLE find=FindFirstFileW(pattern,&fd);
    if(find==INVALID_HANDLE_VALUE)return d;
    FindClose(find);
    d.name=fd.cFileName;
    wchar_t path[MAX_PATH];swprintf_s(path,L"%ls\\%ls",dir,fd.cFileName);
    const HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return d;
    LARGE_INTEGER size{};GetFileSizeEx(file,&size);d.size=size.QuadPart;
    const HANDLE map=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
    void* view=map ? MapViewOfFile(map,FILE_MAP_READ,0,0,0) : nullptr;
    MINIDUMP_DIRECTORY* dirent=nullptr;void* stream=nullptr;ULONG streamSize=0;
    if(view && MiniDumpReadDumpStream(view,ExceptionStream,&dirent,&stream,&streamSize) && stream) {
        const auto* e=static_cast<const MINIDUMP_EXCEPTION_STREAM*>(stream);
        d.found=true;d.code=e->ExceptionRecord.ExceptionCode;d.thread=e->ThreadId;d.address=e->ExceptionRecord.ExceptionAddress;
    }
    if(view)UnmapViewOfFile(view);
    if(map)CloseHandle(map);
    CloseHandle(file);
    return d;
}

void Clean(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH];swprintf_s(pattern,L"%ls\\*",dir);
    WIN32_FIND_DATAW fd{};
    const HANDLE find=FindFirstFileW(pattern,&fd);
    if(find!=INVALID_HANDLE_VALUE) {
        do {
            if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
            wchar_t path[MAX_PATH];swprintf_s(path,L"%ls\\%ls",dir,fd.cFileName);
            DeleteFileW(path);
        } while(FindNextFileW(find,&fd));
        FindClose(find);
    }
    CreateDirectoryW(dir,nullptr);
}

DWORD Run(const wchar_t* self,const char* mode,const wchar_t* dir) {
    wchar_t cmd[1024];swprintf_s(cmd,L"\"%ls\" child %hs \"%ls\"",self,mode,dir);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,cmd,nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi))return 0xFFFFFFFF;
    if(WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);std::fprintf(stderr,"%s: child hung\n",mode);}
    DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return code;
}

bool Keep() { return GetEnvironmentVariableW(L"CRASH_DUMP_TEST_KEEP",nullptr,0)>0; }

// The threads the child said it crashed on ("race": four of them, any one may own the dump).
std::vector<DWORD> MarkedThreads(const std::string& log) {
    std::vector<DWORD> out;
    for(auto at=log.find("TEST crash thread ");at!=std::string::npos;at=log.find("TEST crash thread ",at+1))
        out.push_back(std::strtoul(log.c_str()+at+18,nullptr,10));
    return out;
}

int Count(const std::string& text,const char* part) {
    int n=0;
    for(auto at=text.find(part);at!=std::string::npos;at=text.find(part,at+1))++n;
    return n;
}

// A blocked hang dump and a crash queued behind it must use exactly one dbghelp caller, while retaining their
// own file and exception metadata. The fake runs through the actual serialized service used by mission_watch.
HANDLE fakeEntered=nullptr,fakeRelease=nullptr;
std::atomic<int> fakeActive{0},fakeMaximum{0},fakeCalls{0};
struct FakeCall { DWORD worker=0,thread=0,code=0;wchar_t path[MAX_PATH]{}; } fakeCall[2];
BOOL WINAPI FakeWriter(HANDLE,DWORD,HANDLE file,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION info,
                       PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION) {
    const int active=++fakeActive;
    int old=fakeMaximum.load();
    while(active>old && !fakeMaximum.compare_exchange_weak(old,active)) {}
    const int call=fakeCalls++;
    if(call<2) {
        fakeCall[call].worker=GetCurrentThreadId();
        fakeCall[call].thread=info ? info->ThreadId : 0;
        fakeCall[call].code=info && info->ExceptionPointers ? info->ExceptionPointers->ExceptionRecord->ExceptionCode : 0;
        GetFinalPathNameByHandleW(file,fakeCall[call].path,MAX_PATH,0);
    }
    if(call==0){SetEvent(fakeEntered);WaitForSingleObject(fakeRelease,5000);}
    --fakeActive;
    return TRUE;
}
DWORD WINAPI SubmitHang(void* file) {
    const auto write=crew::MiniDumpWriter();
    return write && write(GetCurrentProcess(),GetCurrentProcessId(),file,crew::kDumpType,nullptr,nullptr,nullptr) ? 0 : 1;
}
DWORD WINAPI SubmitCrash(void*) {
    EXCEPTION_RECORD record{};record.ExceptionCode=0xE0123456;
    CONTEXT context{};
    EXCEPTION_POINTERS ep{&record,&context};
    crew::Capture("serialized crash",&ep);
    return 0;
}
void CheckSerializedDumps(const wchar_t* dir) {
    Check(crew::MiniDumpWriter()!=nullptr,"serialized dumps: service ready before requests");
    const auto original=crew::writer;
    crew::writer=&FakeWriter;
    wchar_t path[MAX_PATH];swprintf_s(path,L"%ls\\serial-hang.dmp",dir);
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    swprintf_s(crew::dumpPath,L"%ls\\serial-crash.dmp",dir);
    fakeEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);fakeRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const HANDLE hang=CreateThread(nullptr,0,&SubmitHang,file,0,nullptr);
    Check(WaitForSingleObject(fakeEntered,5000)==WAIT_OBJECT_0,"serialized dumps: hang is inside the writer");
    const HANDLE crash=CreateThread(nullptr,0,&SubmitCrash,nullptr,0,nullptr);
    // The worker is blocked inside FakeWriter, so observing/restoring its crash event cannot race its consumption.
    Check(WaitForSingleObject(crew::go,5000)==WAIT_OBJECT_0,"serialized dumps: crash queued while hang is in flight");
    SetEvent(crew::go);
    Check(fakeCalls==1 && fakeMaximum==1,"serialized dumps: a queued crash never reenters dbghelp");
    SetEvent(fakeRelease);
    Check(WaitForSingleObject(hang,5000)==WAIT_OBJECT_0 && WaitForSingleObject(crash,5000)==WAIT_OBJECT_0,
          "serialized dumps: both requesters finish");
    Check(fakeCalls==2 && fakeMaximum==1,"serialized dumps: both dumps written with max in-flight one");
    Check(fakeCall[0].worker==crew::dumperId && fakeCall[1].worker==crew::dumperId,"serialized dumps: one dedicated worker");
    Check(fakeCall[0].thread==0 && fakeCall[0].code==0 && std::wcsstr(fakeCall[0].path,L"serial-hang.dmp"),
          "serialized dumps: hang retains its file and no exception");
    Check(fakeCall[1].thread==GetThreadId(crash) && fakeCall[1].code==0xE0123456 && std::wcsstr(fakeCall[1].path,L"serial-crash.dmp"),
          "serialized dumps: crash retains its file, exception code, and faulting thread");
    CloseHandle(hang);CloseHandle(crash);CloseHandle(file);CloseHandle(fakeEntered);CloseHandle(fakeRelease);
    crew::writer=original;
}

// A crash case: the exit code, a dump with `code` of the marked thread, and the log's line before the dump.
void Crash(const wchar_t* self,const wchar_t* dir,const char* mode,DWORD exit,DWORD code,const char* kind) {
    Clean(dir);
    const DWORD got=Run(self,mode,dir);
    wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\log.txt",dir);
    const std::string text=Read(log);
    const Dumped d=ReadDump(dir);
    char what[200];
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: exit %#lx (want %#lx)",mode,got,exit);Check(got==exit,what);
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: a dump with an exception stream",mode);Check(d.found,what);
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: exception code %08lX (want %08lX)",mode,d.code,code);Check(d.code==code,what);
    const std::vector<DWORD> marked=MarkedThreads(text);
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: the dump's thread %lu is a crashed one (%zu marked)",mode,d.thread,marked.size());
    Check(d.thread && std::find(marked.begin(),marked.end(),d.thread)!=marked.end(),what);
    if(exit==kChained) {
        const auto at=text.find("TEST chained on thread ");
        const DWORD chained=at==std::string::npos ? 0 : std::strtoul(text.c_str()+at+23,nullptr,10);
        const bool crashed=std::find(marked.begin(),marked.end(),chained)!=marked.end();
        const bool overflow=code==EXCEPTION_STACK_OVERFLOW;
        _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: the filter behind ran on thread %lu, %s",mode,chained,
                    overflow ? "the dumping thread (the overflowed one has no stack for it)" : "the crashed one");
        Check(chained && crashed!=overflow,what);
    }
    const auto line=text.find(std::string("CRASH ")+kind);
    const auto ready=text.find("DUMP_READY");
    const auto written=text.find("CRASH dump written");
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: dump complete before any crash diagnostic",mode);
    Check(ready!=std::string::npos && line!=std::string::npos && written!=std::string::npos && ready<line && line<written,what);
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: one crash line",mode);Check(Count(text,"CRASH dump written")==1,what);
    _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: named <exe>.<pid>.plugin.dmp",mode);
    Check(d.name.rfind(L"crash_dump_test.exe.",0)==0,what);
    std::printf("%-10s exit %#010lx, dump %ls %lld KB, code %08lX thread %lu\n",mode,got,d.name.c_str(),d.size>>10,d.code,d.thread);
    if(Keep()) {   // for cdb: the dump kept as <dir>\..\<mode>.dmp
        wchar_t from[MAX_PATH],to[MAX_PATH];
        swprintf_s(from,L"%ls\\%ls",dir,d.name.c_str());swprintf_s(to,L"%ls\\..\\%hs.dmp",dir,mode);
        CopyFileW(from,to,FALSE);
        swprintf_s(to,L"%ls\\..\\%hs.log.txt",dir,mode);CopyFileW(log,to,FALSE);
    }
}
}  // namespace

int main(int argc,char** argv) {
    if(argc==4 && !std::strcmp(argv[1],"child")) {
        wchar_t dir[MAX_PATH];
        MultiByteToWideChar(CP_UTF8,0,argv[3],-1,dir,MAX_PATH);
        return Child(argv[2],dir);
    }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    wchar_t self[MAX_PATH];GetModuleFileNameW(nullptr,self,MAX_PATH);
    wchar_t dir[MAX_PATH];
    if(argc>1)MultiByteToWideChar(CP_UTF8,0,argv[1],-1,dir,MAX_PATH);
    else {
        GetTempPathW(MAX_PATH,dir);
        wchar_t sub[64];swprintf_s(sub,L"crash_dump_test_%lu",GetCurrentProcessId());
        wcscat_s(dir,sub);
    }
    CreateDirectoryW(dir,nullptr);
    wchar_t work[MAX_PATH];swprintf_s(work,L"%ls\\run",dir);

    Crash(self,work,"av",kChained,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
    Crash(self,work,"cpp",kChained,0xE06D7363,"unhandled exception");
    // A thread started after the install has the plugin's stack overflow guarantee (crash_dump.cpp kOverflowStack),
    // and with it the dump and the chain go through even with 12-16 KB of other handlers ahead of ours (HungryHandler).
    for(const char* mode:{"stack","stackhandler"}) {
        Crash(self,work,mode,kChained,EXCEPTION_STACK_OVERFLOW,"unhandled exception");
        wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\log.txt",work);
        const std::string text=Read(log);
        const auto at=text.find("TEST stack guarantee ");
        const unsigned long guarantee=at==std::string::npos ? 0 : std::strtoul(text.c_str()+at+21,nullptr,10);
        char what[200];
        _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: the overflowed thread's stack guarantee %lu (want %lu)",
                    mode,guarantee,crew::kOverflowStack);
        Check(guarantee>=crew::kOverflowStack,what);
        if(std::strcmp(mode,"stackhandler"))continue;
        const auto hungry=text.find("TEST handler free ");
        std::size_t free=0,ate=0;
        if(hungry!=std::string::npos)sscanf_s(text.c_str()+hungry,"TEST handler free %zu ate %zu",&free,&ate);
        _snprintf_s(what,sizeof(what),_TRUNCATE,"stackhandler: the handler took %zu of the %zu bytes free (want %zu at least)",
                    ate,free,kHungryLeast);
        Check(ate>=kHungryLeast,what);
    }
    Crash(self,work,"abort",kAbortExit,0x40000015,"abort() in the game's CRT");
    Crash(self,work,"terminate",kAbortExit,0x40000015,"abort() in the game's CRT");
    Crash(self,work,"ownabort",kAbortExit,0x40000015,"abort() in the plugin");
    Crash(self,work,"invalid",kWritten,0xC0000417,"invalid parameter to the game's CRT");
    Crash(self,work,"late",kLate,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
    {
        wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\log.txt",work);
        const std::string text=Read(log);
        Check(text.find("TEST late got the filter behind")!=std::string::npos,"late: SetUnhandledExceptionFilter returned the filter behind ours");
        Check(text.find("CRASHDUMP SetUnhandledExceptionFilter(")!=std::string::npos,"late: the filter set after ours is logged");
        Check(text.find("filter kept first by redirect")!=std::string::npos,"the redirect went in");
    }
    Crash(self,work,"race",kChained,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
    {   // Five threads started after the install (four racers, the dumping thread): the TLS callback's line once.
        wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\log.txt",work);
        const std::string text=Read(log);
        const char line[]="CRASHDUMP stack overflow guarantee 32 KB set by the TLS callback on a thread started after "
                          "the install (thread ";
        std::size_t count=0;
        for(auto at=text.find(line);at!=std::string::npos;at=text.find(line,at+1))++count;
        Check(count==1,"race: the first thread given the stack overflow guarantee by the TLS callback is logged, once");
    }
    Crash(self,work,"bufferlock",kChained,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
    Crash(self,work,"filelock",kChained,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
    // EDF6AutoTurret's buffered lines reach its log on a crash (#372's review: its async log lost the last 50 ms).
    for(const char* mode:{"peer","latepeer","peerlock"}) {
        Crash(self,work,mode,kChained,EXCEPTION_ACCESS_VIOLATION,"unhandled exception");
        wchar_t path[MAX_PATH];
        swprintf_s(path,L"%ls\\peer.log",work);const std::string peer=Read(path);
        swprintf_s(path,L"%ls\\log.txt",work);const std::string own=Read(path);
        char what[200];
        _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: the peer's flush was found",mode);
        Check(own.find("CRASHDUMP EDF6AutoTurret.dll's buffered log lines are written on a crash too")!=std::string::npos,what);
        if(!std::strcmp(mode,"peerlock")) {   // its lock owned by the crashed thread: given up, the chain went on (Crash)
            _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: a locked peer buffer is left, not waited for",mode);
            Check(peer.empty(),what);
            continue;
        }
        _snprintf_s(what,sizeof(what),_TRUNCATE,"%s: the peer's last lines reached its log, in order",mode);
        const auto first=peer.find("PEER line 1\r\n"),last=peer.find("PEER last line before the crash\r\n");
        Check(Count(peer,"PEER line ")==20 && first!=std::string::npos && last!=std::string::npos && first<last,what);
    }
    {
        Clean(work);
        Check(Run(self,"nolog",work)==kChained,"failed crash log: the crash chain still exits promptly");
        Check(ReadDump(work).found,"failed crash log: the primary dump is still written");
    }
    {
        edf::logfile::Buffer<16> buffer;
        std::string written;
        unsigned dropped=0;
        auto write=[&](const char* bytes,std::size_t size,unsigned lost) noexcept { written.append(bytes,size);dropped+=lost; };
        buffer.Append("first",5);
        AcquireSRWLockExclusive(&buffer.bufferLock);
        Check(!buffer.Drain(write,true),"flush: never waits for a buffer lock owned by the caller");
        ReleaseSRWLockExclusive(&buffer.bufferLock);
        AcquireSRWLockExclusive(&buffer.fileLock);
        Check(!buffer.Drain(write,true),"flush: never waits for a file lock owned by the caller");
        ReleaseSRWLockExclusive(&buffer.fileLock);
        Check(buffer.Drain([&](const char* bytes,std::size_t size,unsigned lost) noexcept {
            write(bytes,size,lost);buffer.Append("next",4);
        },true),"flush: releases the buffer lock before writing its immutable snapshot");
        buffer.Append("overflowoverflows",17);
        Check(buffer.Drain(write,true),"flush: drains the next buffer without reacquiring the snapshot lock");
        Check(written=="firstnext" && dropped==1,"flush: retains ordering, pending lines, and overflow count");
    }

    // Handled exceptions write nothing.
    Clean(work);
    const DWORD handled=Run(self,"handled",work);
    wchar_t log[MAX_PATH];swprintf_s(log,L"%ls\\log.txt",work);
    const std::string text=Read(log);
    Check(handled==0,"handled: the child ends normally");
    Check(!ReadDump(work).found && Count(text,"CRASH ")==0,"handled: an exception a __try or a catch handles writes no dump");
    Check(text.find("TEST no crash")!=std::string::npos,"handled: the child ran through");
    Check(Count(text,"had been replaced")==0,"handled: a mission start with nothing replaced logs nothing");
    CheckSerializedDumps(work);
    if(!Keep())Clean(work);
    std::printf("%d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
