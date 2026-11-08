// Whether 原版启动 (tools/vanilla_launch.py) left the saves out of the mod's set: SAVE_DATA.vanilla-on beside SAVE_DATA,
// or a SAVE_DATA\<account>\saveslot*\EDF6AF.mode that is not "mod" (vanilla_launch._mode: a slot without one, or one
// that cannot be read, is the mod's while there is no SAVE_DATA.vanilla-on). Read only; the plugin runs the generator's
// restore-saves (and waits for it) only when this says so (loadout_store.h RestoreModSaves).
#pragma once
#include <windows.h>
#include <cstring>
#include <cwchar>

namespace crew {

// `root`: %LOCALAPPDATA%\EarthDefenceForce6 (vanilla_launch.save_root).
inline bool SavesNeedModRestore(const wchar_t* root) noexcept {
    wchar_t path[MAX_PATH];
    if(_snwprintf_s(path,_TRUNCATE,L"%ls\\SAVE_DATA.vanilla-on",root)<0)return true;   // too long: let the generator see
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)return true;
    wchar_t pattern[MAX_PATH];
    if(_snwprintf_s(pattern,_TRUNCATE,L"%ls\\SAVE_DATA\\*",root)<0)return true;
    WIN32_FIND_DATAW user{};
    HANDLE users=FindFirstFileW(pattern,&user);
    if(users==INVALID_HANDLE_VALUE)return false;   // no saves at all
    bool need=false;
    do {
        if(!(user.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(user.cFileName,L".") || !wcscmp(user.cFileName,L".."))
            continue;
        if(_snwprintf_s(pattern,_TRUNCATE,L"%ls\\SAVE_DATA\\%ls\\saveslot*",root,user.cFileName)<0){need=true;break;}
        WIN32_FIND_DATAW slot{};
        HANDLE slots=FindFirstFileW(pattern,&slot);
        if(slots==INVALID_HANDLE_VALUE)continue;
        do {
            if(!(slot.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
            if(_snwprintf_s(path,_TRUNCATE,L"%ls\\SAVE_DATA\\%ls\\%ls\\EDF6AF.mode",root,user.cFileName,slot.cFileName)<0)
                {need=true;break;}
            HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f==INVALID_HANDLE_VALUE)continue;   // no MODE: the mod's
            char text[32]{};
            DWORD got=0;
            const BOOL read=ReadFile(f,text,sizeof(text)-1,&got,nullptr);
            CloseHandle(f);
            if(!read)continue;   // unreadable: the mod's (as _mode)
            const char* b=text;
            const char* e=text+got;
            while(b<e && (*b==' ' || *b=='\t' || *b=='\r' || *b=='\n'))++b;
            while(e>b && (e[-1]==' ' || e[-1]=='\t' || e[-1]=='\r' || e[-1]=='\n'))--e;
            if(e-b!=3 || memcmp(b,"mod",3)!=0){need=true;break;}
        } while(FindNextFileW(slots,&slot));
        FindClose(slots);
    } while(!need && FindNextFileW(users,&user));
    FindClose(users);
    return need;
}

}  // namespace crew
