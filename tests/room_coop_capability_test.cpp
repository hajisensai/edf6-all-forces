#include "../src/room_coop_capability.h"
#include <cstdio>

extern "C" __declspec(dllexport) void EDF6CoopGetMemberAttributeApi() {}

int main() {
    wchar_t self[MAX_PATH],kernel[MAX_PATH];
    GetModuleFileNameW(nullptr,self,MAX_PATH);
    GetModuleFileNameW(GetModuleHandleW(L"kernel32.dll"),kernel,MAX_PATH);
    int checks=0;
    const auto check=[&](bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);return false;}return true;};
    if(!check(crew::roomui::HasDllExport(self,"EDF6CoopGetMemberAttributeApi"),"a disk PE with the new ABI is recognized") ||
       !check(!crew::roomui::HasDllExport(self,"EDF6CoopGetMemberAttributeApiX"),"export names matched exactly") ||
       !check(!crew::roomui::HasDllExport(kernel,"EDF6CoopGetMemberAttributeApi"),"an older image without the ABI is refused") ||
       !check(!crew::roomui::HasDllExport(L"C:\\no\\such\\EDF6Coop.dll","EDF6CoopGetMemberAttributeApi"),"unreadable image refused"))return 1;
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH,temp);
    wchar_t copy[MAX_PATH];
    _snwprintf_s(copy,_TRUNCATE,L"%lsedf6vc_coop_probe_%lu.dll",temp,GetCurrentProcessId());
    if(!check(CopyFileW(self,copy,FALSE)!=0,"a stand-in image copied beside no loaded module"))return 1;
    const bool unloadedBefore=GetModuleHandleW(copy)==nullptr;
    const bool recognized=crew::roomui::HasDllExport(copy,"EDF6CoopGetMemberAttributeApi");
    const bool unloadedAfter=GetModuleHandleW(copy)==nullptr;
    const bool removable=DeleteFileW(copy)!=0;
    if(!check(unloadedBefore && recognized && unloadedAfter,"probing does not register the image as loaded") ||
       !check(removable,"the read-only mapping releases every file handle"))return 1;
    std::printf("room_coop_capability: %d checks passed\n",checks);
    return 0;
}
