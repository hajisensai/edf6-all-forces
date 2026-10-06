#pragma once
#include <windows.h>

namespace edf {
// Process-lifetime logger: keep every line immediately visible without reopening
// the file for every message. No user-space queue or crash-time buffer to lose.
class AppendLog {
public:
    AppendLog() noexcept=default;
    AppendLog(const AppendLog&)=delete;
    AppendLog& operator=(const AppendLog&)=delete;
    ~AppendLog() noexcept { if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_); }
    void Write(const wchar_t* path,const char* line,DWORD size) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(file_==INVALID_HANDLE_VALUE)
            file_=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_!=INVALID_HANDLE_VALUE) {
            DWORD written=0;
            WriteFile(file_,line,size,&written,nullptr);
        }
        ReleaseSRWLockExclusive(&lock_);
    }
private:
    SRWLOCK lock_=SRWLOCK_INIT;
    HANDLE file_=INVALID_HANDLE_VALUE;
};
} // namespace edf
