// The plugin's own crash dump: src/crash_dump.h says what it catches and what it cannot.
#include "crash_dump.h"
#include <Psapi.h>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew {
void Log(const char* format,...) noexcept;
// A direct append of already formatted text (plugin.cpp): no normal logging/CRT formatting locks. It is called only
// after the dump attempt, so diagnostic output can never prevent the primary crash evidence from being produced.
void CrashLog(const char* text,std::size_t size) noexcept;

namespace {
INIT_ONCE writerOnce=INIT_ONCE_STATIC_INIT;
MiniDumpWriteDumpFn writer=nullptr;
INIT_ONCE serviceOnce=INIT_ONCE_STATIC_INIT;

BOOL CALLBACK ResolveWriter(PINIT_ONCE,void*,void**) noexcept {
    if(const HMODULE dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32))writer=reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));
    return TRUE;
}

// The crash being written: the first thread to get here owns it; any other crashing thread waits for the dump (the
// process is not torn down under it) and goes on to the next filter, so one process writes one dump.
struct Request {
    const char* kind=nullptr;
    EXCEPTION_POINTERS* ep=nullptr;
    DWORD thread=0;
};
Request request;
std::atomic<DWORD> owner{0};
HANDLE go=nullptr,done=nullptr;   // the dumping thread's start, and the dump's end (manual reset: every waiter)
DWORD dumperId=0;
HANDLE hangGo=nullptr,hangDone=nullptr;
// Only ordinary dump submitters take this lock. The crash path and worker never acquire it, even if a watchdog
// itself faults while submitting a hang dump. The submitter waits until completion, keeping these pointers alive.
SRWLOCK hangSubmitLock=SRWLOCK_INIT;
struct HangRequest {
    HANDLE process=nullptr,file=nullptr;
    DWORD pid=0;
    MINIDUMP_TYPE type=MiniDumpNormal;
    PMINIDUMP_EXCEPTION_INFORMATION exception=nullptr;
    PMINIDUMP_USER_STREAM_INFORMATION streams=nullptr;
    PMINIDUMP_CALLBACK_INFORMATION callback=nullptr;
    BOOL result=FALSE;
    DWORD error=0;
} hangRequest;
BOOL WINAPI SerializedWrite(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                             PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION) noexcept;
// The dump of a crash in a big game takes a few seconds; a dumping thread that never ends (the crashed thread held a
// lock it needs) lets the crash go on to Windows after this.
constexpr DWORD kDumpWaitMs=60000;
wchar_t dumpPath[MAX_PATH]{};
std::atomic<bool> installed{false};

// The filter set before ours (the game exe's CRT filter) or after it (through the redirect): called after the dump.
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> behind{nullptr};
std::atomic<DWORD> chaining{0};   // the thread calling the filter behind: a filter there that calls ours again ends
bool redirected=false;

constexpr DWORD kStatusFatalAppExit=0x40000015;              // what abort() reports (STATUS_FATAL_APP_EXIT)
constexpr DWORD kStatusInvalidCrtParameter=0xC0000417;       // STATUS_INVALID_CRUNTIME_PARAMETER

// The module holding `address` and the offset in it, without the loader lock (a crash inside the loader holds it):
// the image's allocation base and its mapped file's name.
void Where(const void* address,wchar_t* name,std::size_t capacity,unsigned long long& offset) noexcept {
    wcscpy_s(name,capacity,L"?");
    offset=reinterpret_cast<std::uintptr_t>(address);
    MEMORY_BASIC_INFORMATION mbi{};
    if(!address || !VirtualQuery(address,&mbi,sizeof(mbi)) || mbi.Type!=MEM_IMAGE)return;
    wchar_t path[MAX_PATH]{};
    const DWORD length=K32GetMappedFileNameW(GetCurrentProcess(),mbi.AllocationBase,path,MAX_PATH);
    if(!length || length>=MAX_PATH)return;
    const wchar_t* slash=wcsrchr(path,L'\\');
    wcscpy_s(name,capacity,slash ? slash+1 : path);
    offset=reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
}

// Fixed storage and integer/UTF-16 formatting, without printf/locale/heap locks: any of those may be owned by the
// crashed thread. Truncation affects only the diagnostic line; the dump has already been attempted at this point.
struct CrashLine {
    char text[4096]{};
    std::size_t size=0;
    void Put(char c) noexcept { if(size<sizeof(text))text[size++]=c; }
    CrashLine& Text(const char* s) noexcept { while(*s)Put(*s++);return *this; }
    CrashLine& Wide(const wchar_t* s) noexcept {
        while(*s) {
            unsigned c=*s++;
            if(c>=0xD800 && c<=0xDBFF && *s>=0xDC00 && *s<=0xDFFF)c=0x10000+((c-0xD800)<<10)+(*s++-0xDC00);
            if(c<0x80)Put(static_cast<char>(c));
            else if(c<0x800){Put(static_cast<char>(0xC0|(c>>6)));Put(static_cast<char>(0x80|(c&63)));}
            else {
                if(c>=0x10000){Put(static_cast<char>(0xF0|(c>>18)));Put(static_cast<char>(0x80|((c>>12)&63)));}
                else Put(static_cast<char>(0xE0|(c>>12)));
                Put(static_cast<char>(0x80|((c>>6)&63)));Put(static_cast<char>(0x80|(c&63)));
            }
        }
        return *this;
    }
    CrashLine& Number(unsigned long long value,unsigned base=10,unsigned width=0) noexcept {
        char digits[32];unsigned n=0;
        do { digits[n++]="0123456789ABCDEF"[value%base];value/=base; } while(value);
        while(width>n){Put('0');--width;}
        while(n)Put(digits[--n]);
        return *this;
    }
    CrashLine& Hex(unsigned long long value) noexcept { return Text("0x").Number(value,16); }
};

// The dump itself, on the dumping thread (or the crashed one when that thread could not be started). Dump first:
// even the ordinary logger's first Log call can deadlock when the faulting thread owns its buffer or CRT lock.
void Write() noexcept {
    const MiniDumpWriteDumpFn write=writer;   // resolved during installation; no InitOnce/loader lock here
    bool opened=false,ok=false;
    DWORD error=0;
    LARGE_INTEGER size{};
    if(write && dumpPath[0]) {
        const HANDLE file=CreateFileW(dumpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            opened=true;
            MINIDUMP_EXCEPTION_INFORMATION info{request.thread,request.ep,FALSE};
            ok=SerializedWrite(GetCurrentProcess(),GetCurrentProcessId(),file,kDumpType,request.ep ? &info : nullptr,nullptr,nullptr)!=FALSE;
            error=ok ? 0 : GetLastError();
            GetFileSizeEx(file,&size);
            CloseHandle(file);
        } else error=GetLastError();
    }
    const EXCEPTION_RECORD* record=request.ep ? request.ep->ExceptionRecord : nullptr;
    const DWORD code=record ? record->ExceptionCode : 0;
    wchar_t module[MAX_PATH];
    unsigned long long offset=0;
    Where(record ? record->ExceptionAddress : nullptr,module,std::size(module),offset);
    CrashLine line;
    line.Text("CRASH ").Text(request.kind).Text(": exception ").Number(code,16,8).Text(" at ").Wide(module).Text("+").Hex(offset);
    if(record && code==EXCEPTION_ACCESS_VIOLATION && record->NumberParameters>=2) {
        line.Text(record->ExceptionInformation[0]==0 ? ", reading " : record->ExceptionInformation[0]==8 ? ", executing " : ", writing ")
            .Hex(record->ExceptionInformation[1]);
    }
    line.Text(", thread ").Number(request.thread).Text("; dump ").Wide(dumpPath[0] ? dumpPath : L"(no path)").Text("\r\n");
    if(!write || !dumpPath[0])line.Text("CRASH dump not written: ").Text(write ? "no dump path" : "no dbghelp MiniDumpWriteDump");
    else if(!opened)line.Text("CRASH dump not written: ").Wide(dumpPath).Text(" (error ").Number(error).Text(")");
    else if(ok)line.Text("CRASH dump written: ").Wide(dumpPath).Text(", ").Number(static_cast<unsigned long long>(size.QuadPart)>>10)
        .Text(" KB (exception ").Number(code,16,8).Text(" at ").Wide(module).Text("+").Hex(offset).Text(", thread ").Number(request.thread).Text(")");
    else line.Text("CRASH dump failed: ").Wide(dumpPath).Text(", MiniDumpWriteDump error ").Number(error).Text(" (")
        .Number(static_cast<unsigned long long>(size.QuadPart)>>10).Text(" KB written)");
    line.Text("\r\n");
    CrashLog(line.text,line.size);
}

DWORD WINAPI Dumper(void*) noexcept {
    const HANDLE pending[]={go,hangGo};   // a pending crash is serviced before the next hang
    for(;;) {
        const DWORD event=WaitForMultipleObjects(2,pending,FALSE,INFINITE);
        if(event==WAIT_OBJECT_0) {
            Write();
            SetEvent(done);
        } else if(event==WAIT_OBJECT_0+1) {
            hangRequest.result=SerializedWrite(hangRequest.process,hangRequest.pid,hangRequest.file,hangRequest.type,
                                                hangRequest.exception,hangRequest.streams,hangRequest.callback);
            hangRequest.error=hangRequest.result ? 0 : GetLastError();
            SetEvent(hangDone);
        } else return 0;
    }
}

BOOL CALLBACK StartService(PINIT_ONCE,void*,void**) noexcept {
    if(!writer)return TRUE;
    done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    go=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    hangDone=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    hangGo=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    const HANDLE thread=go && done && hangGo && hangDone ? CreateThread(nullptr,0,&Dumper,nullptr,0,&dumperId) : nullptr;
    if(thread)CloseHandle(thread);
    else {
        for(const HANDLE event:{go,done,hangGo,hangDone})if(event)CloseHandle(event);
        go=done=hangGo=hangDone=nullptr;
        dumperId=0;
    }
    return TRUE;
}

// The only call into dbghelp lives on the worker. Ordinary callers submit a fixed-slot request and retain their
// file/context until it completes. A crash signals its separate event, so it never waits on an ordinary submitter's
// lock. If dbghelp hangs, the existing crash timeout releases Windows; starting another concurrent call is unsafe.
BOOL WINAPI SerializedWrite(HANDLE process,DWORD pid,HANDLE file,MINIDUMP_TYPE type,PMINIDUMP_EXCEPTION_INFORMATION exception,
                             PMINIDUMP_USER_STREAM_INFORMATION streams,PMINIDUMP_CALLBACK_INFORMATION callback) noexcept {
    if(!writer || !dumperId){SetLastError(ERROR_NOT_READY);return FALSE;}
    if(GetCurrentThreadId()==dumperId)return writer(process,pid,file,type,exception,streams,callback);
    AcquireSRWLockExclusive(&hangSubmitLock);
    hangRequest={process,file,pid,type,exception,streams,callback,FALSE,0};
    const BOOL submitted=SetEvent(hangGo);
    const DWORD wait=submitted ? WaitForSingleObject(hangDone,INFINITE) : WAIT_FAILED;
    const BOOL result=wait==WAIT_OBJECT_0 ? hangRequest.result : FALSE;
    const DWORD error=wait==WAIT_OBJECT_0 ? hangRequest.error : GetLastError();
    ReleaseSRWLockExclusive(&hangSubmitLock);
    SetLastError(error);
    return result;
}

// Small on the crashed thread's stack (a stack overflow leaves it a page): the dump is written by the dumping thread.
void Capture(const char* kind,EXCEPTION_POINTERS* ep) noexcept {
    const DWORD me=GetCurrentThreadId();
    if(me==dumperId)return;                  // the dumping thread itself crashed
    DWORD expected=0;
    if(!owner.compare_exchange_strong(expected,me)) {
        if(expected!=me && done)WaitForSingleObject(done,kDumpWaitMs);
        return;
    }
    request.kind=kind;request.ep=ep;request.thread=me;
    if(go && SetEvent(go))WaitForSingleObject(done,kDumpWaitMs);
    else {
        Write();
        if(done)SetEvent(done);
    }
}

LONG WINAPI Filter(EXCEPTION_POINTERS* ep) noexcept {
    const DWORD me=GetCurrentThreadId();
    if(chaining.load()==me)return EXCEPTION_CONTINUE_SEARCH;   // the filter behind called ours again
    Capture("unhandled exception",ep);
    const LPTOP_LEVEL_EXCEPTION_FILTER next=behind.load();
    if(!next || next==&Filter)return EXCEPTION_CONTINUE_SEARCH;
    chaining.store(me);
    const LONG result=next(ep);
    chaining.store(0);
    return result;
}

// kernelbase!SetUnhandledExceptionFilter redirected here: a filter set after ours goes behind it, and its caller gets
// back what it would have replaced (the filter behind; never ours, so nothing chains back into it).
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetFilterRedirect(LPTOP_LEVEL_EXCEPTION_FILTER filter) noexcept {
    if(filter==&Filter)return behind.load();
    const LPTOP_LEVEL_EXCEPTION_FILTER before=behind.exchange(filter);
    wchar_t caller[MAX_PATH],target[MAX_PATH];
    unsigned long long at=0,to=0;
    Where(_ReturnAddress(),caller,std::size(caller),at);
    Where(reinterpret_cast<const void*>(filter),target,std::size(target),to);
    Log("CRASHDUMP SetUnhandledExceptionFilter(%ls+%#llx) from %ls+%#llx: it runs after the plugin's dump",target,to,caller,at);
    return before;
}

// The first bytes of kernelbase!SetUnhandledExceptionFilter replaced by a jump to SetFilterRedirect (mov rax,imm64;
// jmp rax). The original is never called again, so nothing of it is kept. Not over a jump someone else put there.
bool Redirect() noexcept {
    const HMODULE kernelbase=GetModuleHandleW(L"kernelbase.dll");
    auto* at=kernelbase ? reinterpret_cast<unsigned char*>(GetProcAddress(kernelbase,"SetUnhandledExceptionFilter")) : nullptr;
    if(!at){Log("CRASHDUMP kernelbase!SetUnhandledExceptionFilter not found: the filter is put back at each mission start");return false;}
    if(at[0]==0xE9 || at[0]==0xEB || at[0]==0xCC || (at[0]==0xFF && at[1]==0x25) || (at[0]==0x48 && at[1]==0xB8)) {
        Log("CRASHDUMP kernelbase!SetUnhandledExceptionFilter already starts with a jump (%02X %02X): left alone, the filter "
            "is put back at each mission start",at[0],at[1]);
        return false;
    }
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&SetFilterRedirect);
    std::memcpy(jump+2,&target,sizeof(target));
    DWORD old=0;
    if(!VirtualProtect(at,sizeof(jump),PAGE_EXECUTE_READWRITE,&old)){Log("CRASHDUMP redirect: VirtualProtect error %lu",GetLastError());return false;}
    std::memcpy(at,jump,sizeof(jump));
    VirtualProtect(at,sizeof(jump),old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,sizeof(jump));
    return true;
}

// abort() / std::terminate(): the CRT raises SIGABRT first (then fast-fails or exits). The context is the handler's
// own, so the dump's stack is raise <- abort <- terminate <- whoever called it.
using SignalHandler=void(__cdecl*)(int);
using SignalFn=SignalHandler(__cdecl*)(int,SignalHandler);
SignalFn ucrtSignal=nullptr;
std::atomic<SignalHandler> ucrtAbortBefore{nullptr},ownAbortBefore{nullptr};

bool Chains(SignalHandler h) noexcept { return h && h!=SIG_DFL && h!=SIG_IGN && h!=SIG_ERR; }

__declspec(noinline) void CaptureHere(const char* kind,DWORD code) noexcept {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode=code;
    record.ExceptionFlags=EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress=reinterpret_cast<void*>(context.Rip);
    EXCEPTION_POINTERS ep{&record,&context};
    Capture(kind,&ep);
}

void __cdecl OnUcrtAbort(int signal) {
    CaptureHere("abort() in the game's CRT (ucrtbase)",kStatusFatalAppExit);
    if(const SignalHandler h=ucrtAbortBefore.load();Chains(h))h(signal);
}
void __cdecl OnOwnAbort(int signal) {
    CaptureHere("abort() in the plugin",kStatusFatalAppExit);
    if(const SignalHandler h=ownAbortBefore.load();Chains(h))h(signal);
}

// An invalid CRT parameter: with no handler the CRT fast-fails (_invoke_watson). A handler someone else set decides
// for itself (it may return, and the call then fails with EINVAL: no crash, no dump).
using InvalidParameterFn=void(__cdecl*)(const wchar_t*,const wchar_t*,const wchar_t*,unsigned,std::uintptr_t);
using SetInvalidParameterFn=InvalidParameterFn(__cdecl*)(InvalidParameterFn);
SetInvalidParameterFn ucrtSetInvalid=nullptr;
InvalidParameterFn ucrtWatson=nullptr;
std::atomic<InvalidParameterFn> ucrtInvalidBefore{nullptr};

void __cdecl OnUcrtInvalidParameter(const wchar_t* expression,const wchar_t* function,const wchar_t* file,unsigned line,
                                    std::uintptr_t reserved) {
    if(const InvalidParameterFn h=ucrtInvalidBefore.load()){h(expression,function,file,line,reserved);return;}
    CaptureHere("invalid parameter to the game's CRT (ucrtbase)",kStatusInvalidCrtParameter);
    if(ucrtWatson)ucrtWatson(expression,function,file,line,reserved);
    __fastfail(FAST_FAIL_INVALID_ARG);
}
void __cdecl OnOwnInvalidParameter(const wchar_t*,const wchar_t*,const wchar_t*,unsigned,std::uintptr_t) {
    CaptureHere("invalid parameter to the plugin's CRT",kStatusInvalidCrtParameter);
    __fastfail(FAST_FAIL_INVALID_ARG);
}

// ucrtbase's SIGABRT handler and invalid parameter handler ours again, the ones found there chained (EOSSDK and
// others call signal() themselves). True when they are ours.
bool ClaimUcrt(bool log) noexcept {
    bool ok=true;
    if(ucrtSignal) {
        const SignalHandler before=ucrtSignal(SIGABRT,&OnUcrtAbort);
        if(before!=&OnUcrtAbort) {
            if(before!=SIG_ERR)ucrtAbortBefore.store(before);
            if(log && Chains(before))Log("CRASHDUMP ucrtbase's SIGABRT handler had been replaced (%p): chained after ours",
                                         reinterpret_cast<void*>(before));
            ok=before!=SIG_ERR;
        }
    } else ok=false;
    if(ucrtSetInvalid) {
        const InvalidParameterFn before=ucrtSetInvalid(&OnUcrtInvalidParameter);
        if(before!=&OnUcrtInvalidParameter) {
            ucrtInvalidBefore.store(before);
            if(log && before)Log("CRASHDUMP ucrtbase's invalid parameter handler had been replaced (%p): it decides first",
                                 reinterpret_cast<void*>(before));
        }
    } else ok=false;
    return ok;
}

// <folder>\<exe name>.<pid>.plugin.dmp
bool MakePath(const wchar_t* folder) noexcept {
    wchar_t dir[MAX_PATH];
    if(folder)wcscpy_s(dir,folder);
    else {
        const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",dir,MAX_PATH);
        if(!n || n>=MAX_PATH-16)return false;
        wcscat_s(dir,L"\\CrashDumps");
    }
    CreateDirectoryW(dir,nullptr);   // there already, or made now; a failure shows when the dump is created
    wchar_t exe[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    const wchar_t* name=wcsrchr(exe,L'\\');
    name=name ? name+1 : exe;
    return swprintf_s(dumpPath,L"%ls\\%ls.%lu.plugin.dmp",dir,name,GetCurrentProcessId())>0;
}
}  // namespace

MiniDumpWriteDumpFn MiniDumpWriter() noexcept {
    InitOnceExecuteOnce(&writerOnce,&ResolveWriter,nullptr,nullptr);
    InitOnceExecuteOnce(&serviceOnce,&StartService,nullptr,nullptr);
    return writer && dumperId ? &SerializedWrite : nullptr;
}

bool InstallCrashDump(const wchar_t* folder) noexcept {
    if(installed.exchange(true))return true;
    const bool path=MakePath(folder);
    const bool dbghelp=MiniDumpWriter()!=nullptr;   // now, nothing crashed yet
    behind.store(SetUnhandledExceptionFilter(&Filter));
    redirected=Redirect();
    // The game's CRT (EDF.dll, EDF6.exe and most modules use ucrtbase.dll) and the plugin's own static one.
    if(const HMODULE ucrt=GetModuleHandleW(L"ucrtbase.dll")) {
        ucrtSignal=reinterpret_cast<SignalFn>(GetProcAddress(ucrt,"signal"));
        ucrtSetInvalid=reinterpret_cast<SetInvalidParameterFn>(GetProcAddress(ucrt,"_set_invalid_parameter_handler"));
        ucrtWatson=reinterpret_cast<InvalidParameterFn>(GetProcAddress(ucrt,"_invoke_watson"));
    }
    const bool ucrt=ClaimUcrt(true);
    ownAbortBefore.store(std::signal(SIGABRT,&OnOwnAbort));
    _set_invalid_parameter_handler(&OnOwnInvalidParameter);
    Log("CRASHDUMP installed: %ls (dbghelp %d, dumping thread %d, filter kept first %s, ucrtbase abort/invalid parameter %d)",
        path ? dumpPath : L"(no path)",dbghelp,go!=nullptr,redirected ? "by redirect" : "at each mission start",ucrt);
    return path && dbghelp;
}

void CheckCrashFilter() noexcept {
    if(!installed.load())return;
    if(!redirected) {
        const LPTOP_LEVEL_EXCEPTION_FILTER before=SetUnhandledExceptionFilter(&Filter);
        if(before!=&Filter) {
            behind.store(before);
            wchar_t module[MAX_PATH];
            unsigned long long offset=0;
            Where(reinterpret_cast<const void*>(before),module,std::size(module),offset);
            Log("CRASHDUMP the top-level filter had been replaced by %ls+%#llx: ours first again, that one after it",module,offset);
        }
    }
    ClaimUcrt(true);
}
}  // namespace crew
