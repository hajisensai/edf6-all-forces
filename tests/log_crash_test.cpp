// common/log.cpp's crash flush offline: CrashFlush (and Crash through it) writes only to the handle already open and
// only bytes already formatted. It runs on the crash dump's thread while the crashed thread may own the process heap's
// or the CRT's lock, so it must call none of CreateFileW (a log not opened yet), MoveFileExW (the rotation past the
// limit) or _snprintf_s (the dropped count): log.cpp is compiled here with those three counted. The same counters see
// all three on the ordinary Flush(true) (process exit) of the same state, so a zero means the crash path skipped them.
#include "log.h"
#include <cstdio>
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

std::string Read(const std::wstring& path) {
    std::string out;
    FILE* const f=_wfsopen(path.c_str(),L"rb",_SH_DENYNO);   // shared: the log handle is still open (_wfopen_s would not share)
    if(!f)return out;
    char chunk[4096];
    for(std::size_t n;(n=fread(chunk,1,sizeof(chunk),f))>0;)out.append(chunk,n);
    fclose(f);
    return out;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES; }

void Append(const char* text) { edf::logfile::buffer.Append(text,std::strlen(text)); }   // a line Line() would have formatted
}  // namespace

int main() {
    // Lines go into the buffer directly: Line() would start the writer thread, whose 50 ms passes would open and
    // rotate on their own and blur the counts.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    const std::wstring dir=std::wstring(temp)+L"edf6_log_crash_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    CreateDirectoryW(dir.c_str(),nullptr);
    const std::wstring path=dir+L"\\crash.log",old=path+L".1";
    edf::logfile::Start(path.c_str(),64);

    // 1. Never opened: the crash flush leaves the log unopened, formats no dropped count, and the buffer is taken.
    Append("[00:00:00.001] before the first write\r\n");
    edf::logfile::buffer.dropped=3;
    counted::Reset();
    edf::logfile::CrashFlush();
    Check(counted::create==0,"a crash flush opens no log not opened yet (CreateFileW)");
    Check(counted::format==0,"a crash flush formats no dropped count (_snprintf_s)");
    Check(!Exists(path),"a crash flush creates no log file");
    Check(edf::logfile::buffer.used==0 && edf::logfile::buffer.dropped==0,"a crash flush still takes the buffer");

    // 2. The writer's flush opens the log (the counter sees CreateFileW).
    Append("[00:00:00.002] B\r\n");
    counted::Reset();
    edf::logfile::Flush(false);
    Check(counted::create==1,"the ordinary flush opens the log through the counted CreateFileW");
    Check(Read(path)=="[00:00:00.002] B\r\n","the ordinary flush wrote the line");

    // 3. Opened, past the limit, lines dropped: the crash flush appends to the open handle and nothing else.
    edf::logfile::fileSize=1000;   // past logMax (64): the writer would rotate now
    Append("[00:00:00.003] C\r\n");
    edf::logfile::buffer.dropped=2;
    counted::Reset();
    edf::logfile::CrashFlush();
    Check(counted::create==0 && counted::move==0 && counted::format==0,
          "a crash flush past the limit with dropped lines: no CreateFileW, MoveFileExW or _snprintf_s");
    Check(!Exists(old),"a crash flush does not rotate");
    Check(Read(path)=="[00:00:00.002] B\r\n[00:00:00.003] C\r\n(log lines dropped before the crash)\r\n",
          "a crash flush appends the buffered lines and the fixed dropped line to the open log");

    // 4. A lock held: the crash flush neither waits nor writes (try-only); the lines stay buffered.
    Append("[00:00:00.004] D\r\n");
    AcquireSRWLockExclusive(&edf::logfile::buffer.fileLock);
    edf::logfile::CrashFlush();
    ReleaseSRWLockExclusive(&edf::logfile::buffer.fileLock);
    Check(edf::logfile::buffer.used!=0,"a crash flush with the file lock held returns without taking the buffer");

    // 5. Crash(): the older lines (through the crash flush) and then the crash line, with no counted call.
    counted::Reset();
    const char crash[]="CRASH test: exception C0000005\r\n";
    edf::logfile::Crash(crash,sizeof(crash)-1);
    Check(counted::create==0 && counted::move==0 && counted::format==0,"Crash flushes without CreateFileW, MoveFileExW or _snprintf_s");
    const std::string after=Read(path);
    Check(after.size()>=sizeof(crash)-1 && after.compare(after.size()-(sizeof(crash)-1),std::string::npos,crash)==0 &&
              after.find("[00:00:00.004] D\r\nCRASH test")!=std::string::npos,
          "Crash writes the buffered line, then the crash line");

    // 6. The same state through the ordinary best-effort flush (process exit) still rotates and formats: the counters
    //    see MoveFileExW, CreateFileW and _snprintf_s, so the zeros above are the crash path skipping them.
    Append("[00:00:00.006] E\r\n");
    edf::logfile::buffer.dropped=1;
    counted::Reset();
    edf::logfile::Flush(true);
    Check(counted::move==1 && counted::create==1 && counted::format>=2,
          "the ordinary flush past the limit rotates (MoveFileExW), reopens (CreateFileW) and formats (_snprintf_s)");
    Check(Exists(old),"the ordinary flush rotated to .log.1");

    CloseHandle(edf::logfile::file);
    edf::logfile::file=nullptr;
    DeleteFileW(path.c_str());
    DeleteFileW(old.c_str());
    RemoveDirectoryW(dir.c_str());
    puts(failed ? "log_crash_test: FAIL" : "log_crash_test: PASS");
    return failed ? 1 : 0;
}
