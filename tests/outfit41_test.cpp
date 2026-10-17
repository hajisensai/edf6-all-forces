// The EDF4.1 outfit's pure logic (src/outfit41_logic.h) offline: every class's three files named alike (the battle and
// menu SGOs the game is handed, the Mods files checked), the stored choice round-tripping and tolerant of a BOM, CRLF,
// junk and other sections, whose model it is (the creation's slot, the session member's remote bit), and the popup's
// numbers (our item after the five stock ones, the game's own growth rule, the cursor).
// Exit code 1 when one fails.  cmake --build build --target outfit41_test && build\outfit41_test.exe
#include "../src/outfit41_logic.h"
#include <cstdio>
#include <cwchar>

namespace {
using namespace crew::outfit41;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool EndsWith(const wchar_t* s,const wchar_t* tail) {
    const std::size_t a=std::wcslen(s),b=std::wcslen(tail);
    return a>=b && std::wcscmp(s+a-b,tail)==0;
}
}  // namespace

int main() {
    const wchar_t* stems[kClasses]={L"EDF41_P_RANGER",L"EDF41_P_WINGDIVER",L"EDF41_P_AIRRAIDER",L"EDF41_P_FENCER"};
    for(int c=0;c<kClasses;++c) {
        wchar_t want[96];
        std::swprintf(want,96,L"/%ls.SGO",stems[c]);
        Expect(EndsWith(kFiles[c].battle,want) && std::wcsncmp(kFiles[c].battle,L"app:/object/",12)==0,"battle SGO under app:/object/");
        Expect(EndsWith(kFiles[c].menu,want) && std::wcsncmp(kFiles[c].menu,L"app:/MENUOBJECT/",16)==0,"menu SGO under app:/MENUOBJECT/");
        std::swprintf(want,96,L"OBJECT\\%ls.SGO",stems[c]);
        Expect(std::wcscmp(kFiles[c].onDisk[0],want)==0,"the battle SGO in Mods");
        std::swprintf(want,96,L"OBJECT\\%ls.MRAB",stems[c]);
        Expect(std::wcscmp(kFiles[c].onDisk[1],want)==0,"the model archive in Mods");
        std::swprintf(want,96,L"MENUOBJECT\\%ls.SGO",stems[c]);
        Expect(std::wcscmp(kFiles[c].onDisk[2],want)==0,"the menu SGO in Mods");
    }
    Expect(!ValidClass(-1) && ValidClass(0) && ValidClass(3) && !ValidClass(4),"classes 0..3");

    State s;
    for(int c=0;c<kClasses;++c)Expect(!s.on[c],"off by default");
    s.on[0]=s.on[2]=true;
    const std::string text=Format(s);
    Expect(text=="[Outfit41]\r\nRanger=1\r\nWingDiver=0\r\nAirRaider=1\r\nFencer=0\r\n","the stored form");
    const State back=Parse(text);
    Expect(back.on[0] && !back.on[1] && back.on[2] && !back.on[3],"round trip");
    const State odd=Parse("\xEF\xBB\xBF[Other]\nFencer=1  \r\nRanger=2\nWingDiverX=1\nAirRaider=\n=1\nnonsense\n");
    Expect(!odd.on[0] && !odd.on[1] && !odd.on[2] && odd.on[3],"BOM, other section, junk, only =1 is on");
    const State none=Parse("");
    Expect(!none.on[0] && !none.on[3],"an empty file: all off");

    Expect(OwnPlayer(0) && !OwnPlayer(1) && !OwnPlayer(-1),"only this machine's first player");
    Expect(!RemoteMember(0) && RemoteMember(2) && !RemoteMember(1) && RemoteMember(0xFFFFFFFFu),"member +0x10 bit 1");

    Expect(kItemEdf41==static_cast<std::int32_t>(kStockItems),"our item follows the five stock ones");
    Expect(Grown(0)==2 && Grown(5)==12,"reserve(capacity * 2 + 2)");
    Expect(Cursor(true,3)==kItemEdf41 && Cursor(false,3)==3,"the cursor");
    Expect(kItemSize==0x90 && kItemImpl==0x58 && kReqVector==0x150 && kReqCursor==0x148 && kReqType==0x128,"the request's layout");
    Expect(std::wcscmp(kItemLabel,L"EDF4.1")==0,"the label");
    std::printf("outfit41_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
