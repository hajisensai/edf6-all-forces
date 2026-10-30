#pragma once
#include <windows.h>
#include <cwchar>

namespace crew {
// Whether <the game's directory>\Mods\<dir>\<file> exists (Mods sits beside EDF6.exe): the installer wrote it. A file of
// the plugin's own (an EDF6VC_ model) that is not there means a broken install; the feature that needs it does not deploy.
inline bool ModFileIn(const wchar_t* dir,const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\")!=0 || wcscat_s(path,dir)!=0 || wcscat_s(path,L"\\")!=0 || wcscat_s(path,file)!=0)
        return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
}
}  // namespace crew
