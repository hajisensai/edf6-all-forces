// Image identification and the ini watch shared by EDF6VehicleCrew and EDF6AutoTurret.
#include "edf/host.h"
#include "edf/memory.h"

namespace edf {
unsigned char* IdentifyImage(HMODULE handle) noexcept {
    __try {
        auto base=reinterpret_cast<unsigned char*>(handle);
        if(!Readable(base,0x1000))return nullptr;
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x800)return nullptr;
        auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           || pe->FileHeader.TimeDateStamp!=kImageTimeDateStamp || pe->OptionalHeader.SizeOfImage!=kImageSize)return nullptr;
        return base;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

static FILETIME Stamp(const wchar_t* path) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return path && GetFileAttributesExW(path,GetFileExInfoStandard,&data) ? data.ftLastWriteTime : FILETIME{};
}

void IniWatch::Start(const wchar_t* iniPath) noexcept {
    path=iniPath;stamp=Stamp(path);checkedAt=GetTickCount64();
}

bool IniWatch::Changed() noexcept {
    const auto now=GetTickCount64();
    if(now-checkedAt<1000)return false;
    checkedAt=now;
    const auto next=Stamp(path);
    if(CompareFileTime(&next,&stamp)==0)return false;
    stamp=next;
    return true;
}
}  // namespace edf
