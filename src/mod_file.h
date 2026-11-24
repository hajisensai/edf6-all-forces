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
// Only the installer's files: a variant the plugin writes into Mods while the game runs (a derived SGO or weapon file
// generated at run time) is not in the list and must not be asked about here; check that file on disk where it is made.
namespace mod_file {
struct Dir { const wchar_t* name; std::vector<std::wstring> files; };
inline Dir dirs[]={{L"OBJECT",{}},{L"WEAPON",{}}};
// The one comparison: the sort (Less) and ModFileIn's search and match all call it, so the order searched is the
// order sorted. CSTR_LESS_THAN / CSTR_EQUAL / CSTR_GREATER_THAN.
inline int Compare(const wchar_t* a,int aSize,const wchar_t* b,int bSize) noexcept {
    return CompareStringOrdinal(a,aSize,b,bSize,TRUE);
}
inline bool Less(const std::wstring& a,const std::wstring& b) noexcept {
    return Compare(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()))==CSTR_LESS_THAN;
}
}  // namespace mod_file

// The top-level files of each listed directory under <game>\Mods, sorted with mod_file::Less (the comparison ModFileIn
// searches with). Subdirectories, "." and ".." are not files. Returns the number of files listed.
inline std::size_t LoadModFilesIn(const wchar_t* game) {
    std::size_t total=0;
    for(auto& dir:mod_file::dirs) {
        dir.files.clear();
        wchar_t pattern[MAX_PATH];
        if(swprintf_s(pattern,L"%ls\\Mods\\%ls\\*",game,dir.name)<0)continue;
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

// The game's Mods (beside EDF6.exe), once at load before anything asks ModFileIn; the count is for the load line.
// tests/mod_file_test.cpp lists a made-up game directory through LoadModFilesIn.
inline std::size_t LoadModFiles() {
    wchar_t game[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,game,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(game,L'\\') : nullptr;
    if(!slash)return 0;
    *slash=0;
    return LoadModFilesIn(game);
}

// Whether Mods\<dir>\<file> was there at load: the installer wrote it. A file of the plugin's own (an EDF6VC_ model)
// that is not there means a broken install; the feature that needs it does not deploy.
inline bool ModFileIn(const wchar_t* dir,const wchar_t* file) noexcept {
    for(const auto& d:mod_file::dirs) {
        if(CompareStringOrdinal(d.name,-1,dir,-1,TRUE)!=CSTR_EQUAL)continue;
        const int size=static_cast<int>(wcslen(file));
        const auto at=std::lower_bound(d.files.begin(),d.files.end(),file,[size](const std::wstring& a,const wchar_t* key) noexcept {
            return mod_file::Compare(a.c_str(),static_cast<int>(a.size()),key,size)==CSTR_LESS_THAN;
        });
        return at!=d.files.end() && mod_file::Compare(at->c_str(),static_cast<int>(at->size()),file,size)==CSTR_EQUAL;
    }
    return false;   // not a listed directory
}
}  // namespace crew
