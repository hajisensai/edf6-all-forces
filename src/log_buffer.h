#pragma once
// The normal logger's two buffers and lock order, shared with its crash-path regression test. A crash never waits
// for either lock: the faulting thread may own it. Taking the snapshot keeps the buffer lock until the swap is done,
// including in the try-only path; probing a lock and then acquiring it again leaves a deadlock window.
#include <Windows.h>
#include <cstddef>
#include <cstring>

namespace crew::logging {
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
}  // namespace crew::logging
