#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace crew {
// The files the installer wrote to <the game's directory>\Mods\OBJECT and \WEAPON (Mods sits beside EDF6.exe), listed
// once at load (LoadModFiles, EML6_Load) instead of a GetModuleFileNameW and a GetFileAttributesW per question: the
// Preload* checks ask about hundreds of files on the game thread as a mission starts. The install is complete and
// does not change while the game runs. Names compare as the file system does (ordinal, case-insensitive).
namespace mod_file {
struct Dir { const wchar_t* name; std::vector<std::wstring> files; };
inline Dir dirs[]={{L"OBJECT",{}},{L"WEAPON",{}}};
inline bool Less(const std::wstring& a,const std::wstring& b) noexcept {
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;
}
}  // namespace mod_file

// The top-level files of each listed Mods directory, sorted. Called once, before anything asks ModFileIn; returns the
// number of files listed (for the load line).
inline std::size_t LoadModFiles() {
    wchar_t mods[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,mods,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(mods,L'\\') : nullptr;
    if(!slash)return 0;
    *slash=0;
    std::size_t total=0;
    for(auto& dir:mod_file::dirs) {
        dir.files.clear();
        wchar_t pattern[MAX_PATH];
        if(swprintf_s(pattern,L"%ls\\Mods\\%ls\\*",mods,dir.name)<0)continue;
        WIN32_FIND_DATAW found{};
        const HANDLE find=FindFirstFileExW(pattern,FindExInfoBasic,&found,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
        if(find==INVALID_HANDLE_VALUE)continue;
        do {
            if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))dir.files.emplace_back(found.cFileName);
        } while(FindNextFileW(find,&found));
        FindClose(find);
        std::sort(dir.files.begin(),dir.files.end(),&mod_file::Less);
        total+=dir.files.size();
    }
    return total;
}

// Whether Mods\<dir>\<file> was there at load: the installer wrote it. A file of the plugin's own (an EDF6VC_ model)
// that is not there means a broken install; the feature that needs it does not deploy.
inline bool ModFileIn(const wchar_t* dir,const wchar_t* file) noexcept {
    for(const auto& d:mod_file::dirs) {
        if(_wcsicmp(d.name,dir))continue;
        const int size=static_cast<int>(wcslen(file));
        const auto at=std::lower_bound(d.files.begin(),d.files.end(),file,[size](const std::wstring& a,const wchar_t* key) noexcept {
            return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),key,size,TRUE)==CSTR_LESS_THAN;
        });
        return at!=d.files.end() && CompareStringOrdinal(at->c_str(),static_cast<int>(at->size()),file,size,TRUE)==CSTR_EQUAL;
    }
    return false;   // not a listed directory
}
}  // namespace crew
