// src/mod_file.h offline: the Mods listing (LoadModFilesIn) of a made-up game directory under %TEMP%, and ModFileIn
// on it. Letter case (files and directory names), the sort agreeing with the search (names whose case-sensitive and
// case-insensitive orders differ: '_' sits between the upper and the lower letters), a subdirectory's name and the
// files inside it, the directory itself ("", ".", ".."), and directories that are not listed.
#include "../src/mod_file.h"
#include <cstdio>
#include <string>

namespace {
bool failed=false;
void Check(bool ok,const char* what) {
    printf("%s: %s\n",what,ok ? "PASS" : "FAIL");
    failed=failed || !ok;
}

void Touch(const std::wstring& path) {
    const HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
}

std::wstring Upper(std::wstring s) { for(auto& c:s)c=static_cast<wchar_t>(towupper(c)); return s; }
std::wstring Lower(std::wstring s) { for(auto& c:s)c=static_cast<wchar_t>(towlower(c)); return s; }
}  // namespace

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    const std::wstring game=std::wstring(temp)+L"edf6_mod_file_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    const std::wstring mods=game+L"\\Mods",object=mods+L"\\OBJECT",weapon=mods+L"\\Weapon",map=mods+L"\\MAP";
    for(const auto& d:{game,mods,object,weapon,map,object+L"\\Sub"})CreateDirectoryW(d.c_str(),nullptr);

    // Ordinal case-sensitive order: "AB" < "Zeta" < "_x" < "a_b" < "b"; case-insensitive: "_x" < "a_b" < "AB" < "b" < "Zeta".
    // A sort by one and a search by the other would miss some of them.
    const wchar_t* objectFiles[]={L"a_b.sgo",L"AB.sgo",L"_x.sgo",L"b.sgo",L"Zeta.sgo",L"EDF6VC_Mixed.SGO"};
    for(const wchar_t* f:objectFiles)Touch(object+L"\\"+f);
    Touch(weapon+L"\\EDF6VC_Gun.sgo");
    Touch(object+L"\\Sub\\inner.sgo");
    Touch(map+L"\\unlisted.sgo");
    Touch(mods+L"\\top.sgo");

    const std::size_t listed=crew::LoadModFilesIn(game.c_str());
    Check(listed==7,"the listing counts the top-level files of OBJECT and WEAPON only (6 + 1)");
    const auto& files=crew::mod_file::dirs[0].files;
    Check(std::is_sorted(files.begin(),files.end(),&crew::mod_file::Less),"OBJECT's list is sorted by mod_file::Less");

    bool all=true,upper=true,lower=true;
    for(const wchar_t* f:objectFiles) {
        all=all && crew::ModFileIn(L"OBJECT",f);
        upper=upper && crew::ModFileIn(L"OBJECT",Upper(f).c_str());
        lower=lower && crew::ModFileIn(L"OBJECT",Lower(f).c_str());
    }
    Check(all,"every listed file is found by its own name (the search agrees with the sort)");
    Check(upper && lower,"every listed file is found in upper and in lower case");
    Check(crew::ModFileIn(L"object",L"zeta.SGO") && crew::ModFileIn(L"WEAPON",L"edf6vc_gun.sgo") && crew::ModFileIn(L"weapon",L"EDF6VC_GUN.SGO"),
          "directory names compare without case (OBJECT / object; Weapon on disk, WEAPON asked)");
    Check(!crew::ModFileIn(L"OBJECT",L"a_b.sg") && !crew::ModFileIn(L"OBJECT",L"a_b.sgo2") && !crew::ModFileIn(L"OBJECT",L"c.sgo"),
          "a prefix, a longer name and a name between two listed ones are not there");
    Check(!crew::ModFileIn(L"WEAPON",L"Zeta.sgo") && !crew::ModFileIn(L"OBJECT",L"EDF6VC_Gun.sgo"),"a file is only in its own directory");
    Check(!crew::ModFileIn(L"OBJECT",L"Sub") && !crew::ModFileIn(L"OBJECT",L"sub"),"a subdirectory's name is not a file");
    Check(!crew::ModFileIn(L"OBJECT",L"inner.sgo") && !crew::ModFileIn(L"OBJECT",L"Sub\\inner.sgo"),"a file inside a subdirectory is not listed");
    Check(!crew::ModFileIn(L"OBJECT",L"") && !crew::ModFileIn(L"OBJECT",L".") && !crew::ModFileIn(L"OBJECT",L".."),
          "the directory itself (\"\", \".\", \"..\") is not a file");
    Check(!crew::ModFileIn(L"MAP",L"unlisted.sgo") && !crew::ModFileIn(L"SOUND",L"x.sgo") && !crew::ModFileIn(L"",L"top.sgo") &&
              !crew::ModFileIn(L"Mods",L"top.sgo"),
          "a directory that is not listed (MAP on disk, SOUND, \"\", Mods itself) answers no");

    // Listing again (no Mods at all) empties the lists rather than keeping the old ones.
    Check(crew::LoadModFilesIn((game+L"\\missing").c_str())==0 && !crew::ModFileIn(L"OBJECT",L"b.sgo"),"a game without Mods lists nothing");

    DeleteFileW((object+L"\\Sub\\inner.sgo").c_str());
    RemoveDirectoryW((object+L"\\Sub").c_str());
    for(const wchar_t* f:objectFiles)DeleteFileW((object+L"\\"+f).c_str());
    DeleteFileW((weapon+L"\\EDF6VC_Gun.sgo").c_str());
    DeleteFileW((map+L"\\unlisted.sgo").c_str());
    DeleteFileW((mods+L"\\top.sgo").c_str());
    for(const auto& d:{object,weapon,map,mods,game})RemoveDirectoryW(d.c_str());
    puts(failed ? "mod_file_test: FAIL" : "mod_file_test: PASS");
    return failed ? 1 : 0;
}
