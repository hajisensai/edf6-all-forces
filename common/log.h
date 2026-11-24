#pragma once
// A plugin's log (EDF6VehicleCrew.log, EDF6AutoTurret.log), one per DLL: each links its own copy of edf6common.
//
// Lines are copied into a memory buffer on the caller's thread (the game thread, the call picker, the draw thread:
// one memcpy under a lock) and a writer thread of its own writes the buffer to one handle kept open every kFlushMs.
// A WriteFile per line on the game thread was the hitch under a burst of lines (an air strike's jets, each logging
// what it saw: 15000 lines a second, 2026-10-04); opening and closing the file per line before that, with the virus
// scanner looking at each close, was the hitch when a mission started. A crash writes what is buffered without waiting
// for a lock, rotating or formatting, opening the log at most once (CrashFlush): its own log in Crash, EDF6AutoTurret's through
// kCrashFlush below. Two buffers of kBuffer: the writer takes the full one and the lines go on into the other; a line that does
// not fit is dropped and counted (said with the next write), never waited for. Past kLogMax the file is renamed to
// .log.1 (the one before replaced) and a new one begun, by the writer. No FILE_SHARE_DELETE: a log deleted while the
// game runs would take the rest unseen. Process exit (DllMain's DLL_PROCESS_DETACH) writes what is left, best-effort.
#include <Windows.h>
#include <cstdarg>
#include <cstddef>
#include <cstring>

namespace edf::logfile {

// The two buffers and their lock order, shared with the crash-path regression test (tests/crash_dump_test.cpp). A
// crash never waits for either lock: the faulting thread may own it. Taking the snapshot keeps the buffer lock until
// the swap is done, including in the try-only path; probing a lock and then acquiring it again leaves a deadlock window.
template<std::size_t Capacity> struct Buffer {
    SRWLOCK bufferLock=SRWLOCK_INIT;
    SRWLOCK fileLock=SRWLOCK_INIT;
    char bytes[2][Capacity]{};
    std::size_t used=0;
    unsigned dropped=0;
    int active=0;

    void Append(const char* text,std::size_t size) noexcept {
        AcquireSRWLockExclusive(&bufferLock);
        if(size<=Capacity-used) {
            std::memcpy(bytes[active]+used,text,size);
            used+=size;
        } else ++dropped;
        ReleaseSRWLockExclusive(&bufferLock);
    }

    // `always`: call `write` (under the file lock) even with nothing buffered, for a crash line after the buffered ones.
    template<class Write> bool Drain(Write write,bool tryOnly=false,bool always=false) noexcept {
        if(tryOnly) {
            if(!TryAcquireSRWLockExclusive(&fileLock))return false;
            if(!TryAcquireSRWLockExclusive(&bufferLock)) {
                ReleaseSRWLockExclusive(&fileLock);
                return false;
            }
        } else {
            AcquireSRWLockExclusive(&fileLock);
            AcquireSRWLockExclusive(&bufferLock);
        }
        const int full=active;
        const std::size_t size=used;
        const unsigned lost=dropped;
        active^=1;used=0;dropped=0;
        ReleaseSRWLockExclusive(&bufferLock);
        if(size || lost || always)write(bytes[full],size,lost);
        ReleaseSRWLockExclusive(&fileLock);
        return true;
    }
};

// Preformatted crash diagnostics bypass the normal logger and its CRT formatting/initialization locks. The dump
// is already complete before this is called. FILE_APPEND_DATA also works alongside the normal writer's open handle.
inline void AppendCrashLine(const wchar_t* path,const char* text,std::size_t size) noexcept {
    if(!path[0])return;
    const HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;
    WriteFile(file,text,static_cast<DWORD>(size),&written,nullptr);
    CloseHandle(file);
}

constexpr long long kLogMax=32ll<<20;
constexpr std::size_t kBuffer=1u<<20;
constexpr DWORD kFlushMs=50;

// The log at `path` (its rotation at `path`.1, past `maxBytes`), set once at load before the first line.
void Start(const wchar_t* path,long long maxBytes=kLogMax) noexcept;
// The log's path ("" before Start).
const wchar_t* Path() noexcept;
// One line, "[hh:mm:ss.mmm] text\r\n"; nothing before Start. The writer thread starts with the first line.
void Line(const char* format,va_list args) noexcept;
// What is buffered written now, opening the log or rotating it as the writer would. Best-effort (tryOnly): both locks
// once, without waiting (process exit, a refused load ending the game); otherwise waiting for the writer (tests).
void Flush(bool tryOnly=true) noexcept;
// A crash's flush: both locks once without waiting, then the buffered bytes to the log's one handle. A log not opened
// yet is opened then (one CreateFileW: the lines before a crash are the ones worth keeping, and the crash line needs a
// handle anyway); no rotation past the limit (no MoveFileExW), a fixed line for dropped lines (no _snprintf_s).
// A lock held (the crashed thread owns it): nothing written, nothing taken.
void CrashFlush() noexcept;
// A crash: under the same locks and the same one handle, what is buffered and then the preformatted crash diagnostics.
// With a lock held the crash line alone is appended through its own handle (AppendCrashLine). Either way at most one
// CreateFileW in all (tests/log_crash_test.cpp counts it, AppendCrashLine's included).
void Crash(const char* text,std::size_t size) noexcept;

// The one crash handler of the process is EDF6VehicleCrew's (src/crash_dump.cpp), and EDF6AutoTurret's buffer is its
// own copy of this file's: after the dump the handler calls this export of EDF6AutoTurret's once, which is CrashFlush
// (the crashed thread may own either lock: no wait). Without it a crash lost that log's last kFlushMs (#372's review).
using CrashFlushFn=void(__cdecl*)();
constexpr wchar_t kCrashFlushDll[]=L"EDF6AutoTurret.dll";
constexpr char kCrashFlush[]="EDF6AutoTurret_CrashFlushLogV1";

}  // namespace edf::logfile
