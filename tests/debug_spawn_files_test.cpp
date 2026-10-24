#include "../src/debug_spawn_files.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace ds=crew::debugspawn;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
int main() {
    static_assert(ds::ParseModFile(L"EDF6VC_VULCAN.MRAB").directory==ds::ModDirectory::object);
    static_assert(ds::ParseModFile(L"OBJECT/EDF6VC_VULCAN.MRAB").directory==ds::ModDirectory::object);
    static_assert(ds::ParseModFile(L"WEAPON/EDF6VC_BMPT_ATGM_4.SGO").directory==ds::ModDirectory::weapon);
    static_assert(!ds::ParseModFile(L"../escape.SGO"));
    static_assert(ds::ParseModFile(L"EDF41MP01/ANTHILL301UG.SGO").directory==ds::ModDirectory::object);
    static_assert(ds::ParseModFile(L"OBJECT/EDF41MP01/ANTHILL301UG.SGO").directory==ds::ModDirectory::object);
    static_assert(ds::ParseModFile(L"EDF41MP02\\RAGDOLL_ANTHILL301LARGE.SHKT").directory==ds::ModDirectory::object);
    const auto weapon=ds::ParseModFile(L"WEAPON/EDF6VC_BMPT_ATGM_4.SGO");
    Check(weapon && std::wcscmp(weapon.name,L"EDF6VC_BMPT_ATGM_4.SGO")==0,"weapon file name excludes the directory");
    const wchar_t* bad[]={nullptr,L"",L"OBJECT/",L"WEAPON/",L"OBJECT/../x.SGO",L"WEAPON/a//b.SGO",L"WEAPON/a\\..\\b.SGO",
        L"C:/game/x.SGO",L"/OBJECT/x.SGO",L"OBJECT//x.SGO",L"../x.SGO",L"a..SGO",L"a.",L".a",L"WEAPON/x:ads",
        L"WEAPON/x\n.SGO",L"WEAPON/x\t.SGO",L"WEAPON/x\".SGO",L"OBJECT/\u4e2d.SGO",L"OBJECT/x*.SGO"};
    for(const auto text:bad)Check(!ds::ParseModFile(text),"reject an unsafe or noncanonical Mods path");
    Check(ds::ParseModFile(L"weapon/x.SGO").directory==ds::ModDirectory::object,"only canonical WEAPON prefix changes the root");
    const std::wstring longest(128,L'a'),tooLong(129,L'a');
    Check(static_cast<bool>(ds::ParseModFile(longest.c_str())),"allow a bounded flat file name");
    Check(!ds::ParseModFile(tooLong.c_str()),"reject an oversized file name");
    Check(ds::ParseModFile(L"vehicle-301_01.sgo.lod").directory==ds::ModDirectory::object,"allow normal asset punctuation");
    std::printf("%d debug-spawn file reference checks passed\n",checks);
}
