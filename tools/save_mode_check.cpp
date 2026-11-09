// src/save_mode.h against a temporary save folder: the plugin asks the generator's restore-saves exactly when
// tools/vanilla_launch.py would find something to switch back (its _mode), so a mod start never runs on the vanilla saves
// (tests/vanilla_launch_test.py covers restore-saves itself).
#include "save_mode.h"
#include <cstdio>
#include <string>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}

void Write(const std::wstring& path,const char* text) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE){std::printf("FAIL cannot write %ls\n",path.c_str());++failures;return;}
    DWORD put=0;
    WriteFile(f,text,static_cast<DWORD>(strlen(text)),&put,nullptr);
    CloseHandle(f);
}

void Remove(const std::wstring& dir) {   // the temporary tree, depth first
    WIN32_FIND_DATAW e{};
    HANDLE h=FindFirstFileW((dir+L"\\*").c_str(),&e);
    if(h!=INVALID_HANDLE_VALUE) {
        do {
            if(!wcscmp(e.cFileName,L".") || !wcscmp(e.cFileName,L".."))continue;
            const std::wstring p=dir+L"\\"+e.cFileName;
            if(e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)Remove(p);
            else DeleteFileW(p.c_str());
        } while(FindNextFileW(h,&e));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
}  // namespace

int main() {
    wchar_t tmp[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,tmp))return 1;
    wchar_t name[64];
    swprintf_s(name,L"edf6-save-mode-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
    const std::wstring root=std::wstring(tmp)+name;
    CreateDirectoryW(root.c_str(),nullptr);

    Expect(!crew::SavesNeedModRestore(root.c_str()),"no SAVE_DATA: nothing to do");
    const std::wstring user=root+L"\\SAVE_DATA\\76561198000000000";
    const std::wstring slot0=user+L"\\saveslot00",slot1=user+L"\\SaveSlot01";
    CreateDirectoryW((root+L"\\SAVE_DATA").c_str(),nullptr);
    CreateDirectoryW(user.c_str(),nullptr);
    CreateDirectoryW(slot0.c_str(),nullptr);
    CreateDirectoryW(slot1.c_str(),nullptr);
    CreateDirectoryW((user+L"\\SYSTEM").c_str(),nullptr);
    Write(slot0+L"\\MAIN.GST","save");
    Expect(!crew::SavesNeedModRestore(root.c_str()),"slots without a mode file: the mod's");
    Write(slot0+L"\\EDF6AF.mode","mod");
    Write(slot1+L"\\EDF6AF.mode","mod\r\n");
    Expect(!crew::SavesNeedModRestore(root.c_str()),"every slot 'mod' (blanks around ignored)");
    Write(user+L"\\SYSTEM\\EDF6AF.mode","vanilla");
    Expect(!crew::SavesNeedModRestore(root.c_str()),"a folder that is no saveslot is not looked at");
    for(const char* mode:{"vanilla","mod-saved","vanilla-saved","modx",""}) {
        Write(slot1+L"\\EDF6AF.mode",mode);
        char what[64];
        sprintf_s(what,"a slot in mode '%s': restore",mode);
        Expect(crew::SavesNeedModRestore(root.c_str()),what);
    }
    Write(slot1+L"\\EDF6AF.mode","mod");
    Write(root+L"\\SAVE_DATA.vanilla-on","");
    Expect(crew::SavesNeedModRestore(root.c_str()),"SAVE_DATA.vanilla-on: a vanilla run on, restore");
    DeleteFileW((root+L"\\SAVE_DATA.vanilla-on").c_str());
    Expect(!crew::SavesNeedModRestore(root.c_str()),"and settled again");

    Remove(root);
    if(failures)return 1;
    std::printf("save_mode_check: OK\n");
    return 0;
}
