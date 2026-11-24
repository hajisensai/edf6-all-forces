#pragma once
// A plugin's log (EDF6VehicleCrew.log, EDF6AutoTurret.log), one per DLL: each links its own copy of edf6common.
//
// Lines are copied into a memory buffer on the caller's thread (the game thread, the call picker, the draw thread:
// one memcpy under a lock) and a writer thread of its own writes the buffer to one handle kept open every kFlushMs.
// A WriteFile per line on the game thread was the hitch under a burst of lines (an air strike's jets, each logging
// what it saw: 15000 lines a second, 2026-10-04); opening and closing the file per line before that, with the virus
// scanner looking at each close, was the hitch when a mission started. A crash takes at most the last kFlushMs of
// lines. Two buffers of kBuffer: the writer takes the full one and the lines go on into the other; a line that does
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

    template<class Write> bool Drain(Write write,bool tryOnly=false) noexcept {
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
        if(size || lost)write(bytes[full],size,lost);
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
// What is buffered written now. Best-effort (tryOnly): both locks once, without waiting (a crash, process exit, a
// refused load ending the game); otherwise waiting for the writer (tests).
void Flush(bool tryOnly=true) noexcept;
// Preformatted crash diagnostics appended directly (AppendCrashLine on the log's path).
void Crash(const char* text,std::size_t size) noexcept;

}  // namespace edf::logfile
