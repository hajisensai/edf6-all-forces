// common/log.cpp's crash path offline: CrashFlush and Crash run on the crash dump's thread while the crashed thread may
// own the process heap's or the CRT's lock. They write the bytes already formatted to the log's one handle: no rotation
// (MoveFileExW), no formatting (_snprintf_s), and at most one CreateFileW in a whole crash, which opens a log not
// opened yet so that the lines before the crash are kept. The three calls are counted everywhere the crash path can
// reach them, AppendCrashLine in log.h included: the counting macros come before the first include of log.h. The same
// counters see all three on the ordinary Flush(true) (process exit) of the same state, so a zero is the crash path
// skipping them.
#include <Windows.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <share.h>
#include <string>

namespace counted {
int create=0,move=0,format=0;
void Reset() { create=move=format=0; }
HANDLE WINAPI CreateFileW(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templ) {
    ++create;
    return ::CreateFileW(name,access,share,security,disposition,flags,templ);
}
BOOL WINAPI MoveFileExW(LPCWSTR from,LPCWSTR to,DWORD flags) {
    ++move;
    return ::MoveFileExW(from,to,flags);
}
int Format(char* out,std::size_t size,std::size_t count,const char* spec,...) {
    ++format;
    va_list args;va_start(args,spec);
    const int n=_vsnprintf_s(out,size,count,spec,args);
    va_end(args);
    return n;
}
}  // namespace counted

#define CreateFileW counted::CreateFileW
#define MoveFileExW counted::MoveFileExW
#define _snprintf_s counted::Format
#include "log.h"   // first here: AppendCrashLine's CreateFileW is counted too
#include "../common/log.cpp"
#undef CreateFileW
#undef MoveFileExW
#undef _snprintf_s

namespace {
bool failed=false;
void Check(bool ok,const char* what) {
    printf("%s: %s\n",what,ok ? "PASS" : "FAIL");
    failed=failed || !ok;
}

std::wstring path,old;

std::string Read(const std::wstring& file) {
    std::string out;
    FILE* const f=_wfsopen(file.c_str(),L"rb",_SH_DENYNO);   // shared: the log handle is still open (_wfopen_s would not share)
    if(!f)return out;
    char chunk[4096];
    for(std::size_t n;(n=fread(chunk,1,sizeof(chunk),f))>0;)out.append(chunk,n);
    fclose(f);
    return out;
}

bool Exists(const std::wstring& file) { return GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES; }

void Append(const char* text) { edf::logfile::buffer.Append(text,std::strlen(text)); }   // a line Line() would have formatted

// A log never opened: the handle closed, the files gone, the buffer empty.
void Fresh() {
    if(edf::logfile::file)CloseHandle(edf::logfile::file);
    edf::logfile::file=nullptr;
    edf::logfile::fileSize=0;
    edf::logfile::buffer.used=0;
    edf::logfile::buffer.dropped=0;
    DeleteFileW(path.c_str());
    DeleteFileW(old.c_str());
}
}  // namespace

int main() {
    // Lines go into the buffer directly: Line() would start the writer thread, whose 50 ms passes would open and
    // rotate on their own and blur the counts.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    const std::wstring dir=std::wstring(temp)+L"edf6_log_crash_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    CreateDirectoryW(dir.c_str(),nullptr);
    path=dir+L"\\crash.log";
    old=path+L".1";
    edf::logfile::Start(path.c_str(),64);
    const char crash[]="CRASH test: exception C0000005\r\n";

    // 1. Never opened, Crash(): the lines before the crash end up in the file, then the crash line, through one handle:
    //    exactly one CreateFileW in the whole crash path (AppendCrashLine's counted), no rotation, no formatting.
    Fresh();
    Append("[00:00:00.001] before the first write\r\n");
    edf::logfile::buffer.dropped=3;
    counted::Reset();
    edf::logfile::Crash(crash,sizeof(crash)-1);
    Check(Read(path)==std::string("[00:00:00.001] before the first write\r\n(log lines dropped before the crash)\r\n")+crash,
          "a crash before the log was ever opened keeps the buffered lines: they are in the file, before the crash line");
    Check(counted::create==1,"the whole crash path opens the log exactly once (CreateFileW, AppendCrashLine included)");
    Check(counted::move==0 && counted::format==0,"no MoveFileExW, no _snprintf_s");

    // 2. Never opened, CrashFlush() (EDF6AutoTurret's export): one open, the lines kept, nothing else.
    Fresh();
    Append("[00:00:00.002] A\r\n");
    counted::Reset();
    edf::logfile::CrashFlush();
    Check(Read(path)=="[00:00:00.002] A\r\n" && edf::logfile::buffer.used==0,"a crash flush of a log not opened yet writes its lines");
    Check(counted::create==1 && counted::move==0 && counted::format==0,"a crash flush opens once, no MoveFileExW, no _snprintf_s");

    // 3. Opened by the writer's flush, then past the limit with lines dropped: the crash flush appends to the open handle
    //    and nothing else.
    Fresh();
    Append("[00:00:00.003] B\r\n");
    counted::Reset();
    edf::logfile::Flush(false);
    Check(counted::create==1 && Read(path)=="[00:00:00.003] B\r\n","the ordinary flush opens the log through the counted CreateFileW");
    edf::logfile::fileSize=1000;   // past logMax (64): the writer would rotate now
    Append("[00:00:00.004] C\r\n");
    edf::logfile::buffer.dropped=2;
    counted::Reset();
    edf::logfile::CrashFlush();
    Check(counted::create==0 && counted::move==0 && counted::format==0,
          "a crash flush of the open log past the limit with dropped lines: no CreateFileW, MoveFileExW or _snprintf_s");
    Check(!Exists(old),"a crash flush does not rotate");
    Check(Read(path)=="[00:00:00.003] B\r\n[00:00:00.004] C\r\n(log lines dropped before the crash)\r\n",
          "a crash flush appends the buffered lines and the fixed dropped line to the open log");

    // 4. Open, Crash(): the buffered line and the crash line on the open handle, no CreateFileW at all.
    Append("[00:00:00.005] D\r\n");
    counted::Reset();
    edf::logfile::Crash(crash,sizeof(crash)-1);
    Check(counted::create==0 && counted::move==0 && counted::format==0,"a crash with the log open: no CreateFileW, MoveFileExW or _snprintf_s");
    Check(Read(path)==std::string("[00:00:00.003] B\r\n[00:00:00.004] C\r\n(log lines dropped before the crash)\r\n[00:00:00.005] D\r\n")+crash,
          "a crash with the log open writes the buffered line, then the crash line");

    // 5. Open, nothing buffered, Crash(): the crash line alone, still on the open handle.
    counted::Reset();
    const std::size_t before=Read(path).size();
    edf::logfile::Crash(crash,sizeof(crash)-1);
    Check(counted::create==0 && Read(path).size()==before+sizeof(crash)-1,"a crash with nothing buffered still writes its line on the open handle");

    // 6. A lock held (the crashed thread owns it): neither waits nor takes the buffer; Crash appends its line alone
    //    through AppendCrashLine, the one CreateFileW of the crash.
    Append("[00:00:00.006] E\r\n");
    AcquireSRWLockExclusive(&edf::logfile::buffer.fileLock);
    edf::logfile::CrashFlush();
    Check(edf::logfile::buffer.used!=0,"a crash flush with the file lock held returns without taking the buffer");
    counted::Reset();
    const std::size_t held=Read(path).size();
    edf::logfile::Crash(crash,sizeof(crash)-1);
    ReleaseSRWLockExclusive(&edf::logfile::buffer.fileLock);
    Check(counted::create==1 && counted::move==0 && counted::format==0,"a crash with a lock held opens exactly once: AppendCrashLine's own handle");
    Check(Read(path).size()==held+sizeof(crash)-1 && edf::logfile::buffer.used!=0,"a crash with a lock held writes the crash line and leaves the buffer");

    // 7. The same state through the ordinary best-effort flush (process exit) still rotates and formats: the counters
    //    see MoveFileExW, CreateFileW and _snprintf_s, so the zeros above are the crash path skipping them.
    edf::logfile::buffer.dropped=1;
    counted::Reset();
    edf::logfile::Flush(true);
    Check(counted::move==1 && counted::create==1 && counted::format>=2,
          "the ordinary flush past the limit rotates (MoveFileExW), reopens (CreateFileW) and formats (_snprintf_s)");
    Check(Exists(old),"the ordinary flush rotated to .log.1");

    Fresh();
    RemoveDirectoryW(dir.c_str());
    puts(failed ? "log_crash_test: FAIL" : "log_crash_test: PASS");
    return failed ? 1 : 0;
}
