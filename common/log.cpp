// The plugin's log (log.h): the buffer, the writer thread and the rotation, one set per DLL.
#include "log.h"
#include <cstdio>
#include <cwchar>

namespace edf::logfile {
namespace {
Buffer<kBuffer> buffer;
INIT_ONCE writerOnce=INIT_ONCE_STATIC_INIT;
wchar_t logPath[MAX_PATH]{},oldPath[MAX_PATH+2]{};
long long logMax=kLogMax;
HANDLE file=nullptr;   // under buffer.fileLock
long long fileSize=0;

HANDLE Open() noexcept {
    const HANDLE h=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER size{};
    fileSize=GetFileSizeEx(h,&size) ? size.QuadPart : 0;
    return h;
}

void Put(const char* text,std::size_t n) noexcept {
    DWORD wrote=0;
    if(file && n && WriteFile(file,text,static_cast<DWORD>(n),&wrote,nullptr))fileSize+=wrote;
}

// The log renamed to .log.1 and a new one opened. A rename that fails (another program holds .log.1, or the log
// without sharing delete) is said in the log, which then grows another logMax first.
void Rotate() noexcept {
    CloseHandle(file);
    const BOOL moved=MoveFileExW(logPath,oldPath,MOVEFILE_REPLACE_EXISTING);
    const DWORD error=moved ? 0 : GetLastError();
    file=Open();
    if(!file)return;
    char line[160];
    const int n=moved ? _snprintf_s(line,sizeof(line),_TRUNCATE,"(log continued: the previous %lld MB are in .log.1)\r\n",logMax>>20)
                      : _snprintf_s(line,sizeof(line),_TRUNCATE,"(log rotation failed, error %lu: this file grows on)\r\n",error);
    if(!moved)fileSize=0;   // the next try another logMax on, not on every line
    if(n>0)Put(line,static_cast<std::size_t>(n));
}

// Called with the file lock and an immutable buffer snapshot, by the writer or an explicit flush.
void WritePending(const char* text,std::size_t n,unsigned dropped) noexcept {
    if(!n && !dropped)return;
    if(!file)file=Open();
    if(!file)return;
    if(fileSize+static_cast<long long>(n)>logMax)Rotate();
    Put(text,n);
    if(dropped) {
        char line[120];
        const int k=_snprintf_s(line,sizeof(line),_TRUNCATE,"(%u log lines dropped: more than %zu KB in %lu ms)\r\n",dropped,
                                kBuffer>>10,kFlushMs);
        if(k>0)Put(line,static_cast<std::size_t>(k));
    }
}

// A crash's write (CrashFlush, Crash), on the crash dump's thread while the crashed one may own the process heap's or
// the CRT's lock: the bytes already formatted, to the log's one handle. A log not opened yet is opened here, once: the
// crash line needs a handle anyway (AppendCrashLine opened one per crash), and leaving it unopened dropped the lines
// before the crash for no lock saved. One past logMax grows on (no MoveFileExW), the dropped count is a fixed line
// (no _snprintf_s). Crash's own line (crashText) goes after the buffered ones under the same file lock.
constexpr char kCrashDropped[]="(log lines dropped before the crash)\r\n";
const char* crashText=nullptr;
std::size_t crashSize=0;
bool crashHandled=false;   // WriteCrash ran for Crash (both locks taken): the crash line is its
void WriteCrash(const char* text,std::size_t n,unsigned dropped) noexcept {
    if(!file)file=Open();
    Put(text,n);
    if(dropped)Put(kCrashDropped,sizeof(kCrashDropped)-1);
    if(crashText) {
        Put(crashText,crashSize);   // nothing when the open failed: a second CreateFileW would fail as well
        crashHandled=true;
    }
}

DWORD WINAPI Writer(void*) {
    for(;;) {
        Sleep(kFlushMs);
        buffer.Drain(&WritePending);
    }
}

// The writer runs this module's code for the rest of the process, so the module is pinned: a loader that frees a
// plugin refusing to load (after its REFUSED line started the writer) would otherwise leave the thread in unmapped code.
BOOL CALLBACK StartWriter(PINIT_ONCE,void*,void**) {
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&Writer),&self);
    const HANDLE t=CreateThread(nullptr,0,&Writer,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    return TRUE;
}
}  // namespace

void Start(const wchar_t* path,long long maxBytes) noexcept {
    wcscpy_s(logPath,path);
    swprintf_s(oldPath,L"%ls.1",logPath);
    logMax=maxBytes;
}

const wchar_t* Path() noexcept { return logPath; }

void Line(const char* format,va_list args) noexcept {
    if(!logPath[0])return;
    char text[1000]{};
    vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);
    SYSTEMTIME t{};GetLocalTime(&t);
    char line[1100];
    const int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"[%02u:%02u:%02u.%03u] %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text);
    if(n<=0)return;
    InitOnceExecuteOnce(&writerOnce,&StartWriter,nullptr,nullptr);
    buffer.Append(line,static_cast<std::size_t>(n));
}

void Flush(bool tryOnly) noexcept {
    if(logPath[0])buffer.Drain(&WritePending,tryOnly);
}

void CrashFlush() noexcept {
    if(logPath[0])buffer.Drain(&WriteCrash,true);
}

void Crash(const char* text,std::size_t size) noexcept {
    if(!logPath[0])return;
    crashText=text;crashSize=size;crashHandled=false;
    buffer.Drain(&WriteCrash,true,true);   // the lines before the crash, then the crash line: one handle
    crashText=nullptr;
    if(!crashHandled)AppendCrashLine(logPath,text,size);   // a lock held: the crash line by itself, its own handle
}

}  // namespace edf::logfile
