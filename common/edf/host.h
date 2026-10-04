// What both plugins do at load and while running, outside the game's objects (common/host.cpp): find the
// one EDF.dll build every address is for, and notice a saved ini.
#pragma once
#include <Windows.h>

namespace edf {
constexpr DWORD kImageTimeDateStamp=0x678CCB46,kImageSize=0x22CE000;
// The EDF.dll base when `handle` is that build (x64, its TimeDateStamp and SizeOfImage), else nullptr.
unsigned char* IdentifyImage(HMODULE handle) noexcept;

// An ini file watched for edits: Changed() looks at its write time at most once a second (game thread or
// any one thread) and is true once per save.
struct IniWatch {
    const wchar_t* path=nullptr;
    FILETIME stamp{};
    ULONGLONG checkedAt=0;
    void Start(const wchar_t* iniPath) noexcept;   // remembers the current write time
    bool Changed() noexcept;
};
}  // namespace edf
