// The EDF4.1 outfit's pure logic (src/outfit41_logic.h) offline: every class's three files named alike (the battle and
// menu SGOs the game is handed, the Mods files checked), the stored per-player choice round-tripping and tolerant of a
// BOM, CRLF, junk and other sections, who wears it (the choice anywhere; in an EDF4.1 pack also the mission's outfit or a
// locked outfit change), what a member tells the room and what a reader accepts of it, whose model it is (the session
// member's remote bit), and the popup's numbers (our item after the five stock ones, the game's own growth rule, the
// cursor).
// Exit code 1 when one fails.  cmake --build build --target outfit41_test && build\outfit41_test.exe
#include "../src/outfit41_logic.h"
#include <cstdio>
#include <cstring>
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
    Expect(!ValidLocal(-1) && ValidLocal(0) && ValidLocal(1) && !ValidLocal(2),"local players 0 and 1 (split screen)");

    // The stored choice, one a local player.
    State s;
    Expect(!s.on[0] && !s.on[1],"off by default");
    s.on[1]=true;
    const std::string text=Format(s);
    Expect(text=="[Outfit41]\r\nPlayer1=0\r\nPlayer2=1\r\n","the stored form");
    const State back=Parse(text);
    Expect(!back.on[0] && back.on[1],"round trip");
    const State odd=Parse("\xEF\xBB\xBF[Other]\nPlayer1=1  \r\nPlayer2=2\nPlayer1X=1\nRanger=1\n=1\nnonsense\n");
    Expect(odd.on[0] && !odd.on[1],"BOM, other section, junk, the old per-class keys ignored, only =1 is on");
    Expect(!Parse("").on[0],"an empty file: all off");

    // Who wears it.
    Expect(MissionDefault(-1,true) && !MissionDefault(0,true) && !MissionDefault(3,true),"the mission's outfit (-1)");
    Expect(MissionDefault(2,false) && MissionDefault(-1,false),"a locked outfit change: the mission's whatever the save says");
    Expect(Wears(true,false,false) && Wears(true,true,true),"chosen: worn in every pack");
    Expect(Wears(false,true,true) && !Wears(false,true,false),"the mission default: only in an EDF4.1 pack");
    Expect(!Wears(false,false,true),"another outfit picked in an EDF4.1 pack: that outfit");
    Expect(!Pack41(10,0) && !Pack41(6,7) && Pack41(7,7) && Pack41(8,7) && Pack41(9,7) && !Pack41(10,7) && !Pack41(-1,7),
           "the EDF4.1 packs: EDF41Content .. +2, none when not installed");

    // What a member tells the room.
    char v[kWireSize];
    FormatWire(Wire{true,false},v);
    Expect(std::strcmp(v,"1;10")==0,"the value: version 1, chosen, mission default");
    FormatWire(Wire{false,true},v);
    Expect(std::strcmp(v,"1;01")==0,"the value, the other flag");
    Expect(std::strlen(kAttrKey)<=32 && std::strncmp(kAttrKey,"EDF6X_",6)==0,"an EDF6Coop extension key");
    Wire w{};
    Expect(ParseWire("1;11",w) && w.chosen && w.missionDefault,"read back");
    Expect(ParseWire("1;00",w) && !w.chosen && !w.missionDefault,"read back, both off");
    w=Wire{true,true};
    Expect(!ParseWire("2;11",w) && !ParseWire("1;1",w) && !ParseWire("1;111",w) && !ParseWire("1;12",w) && !ParseWire("",w) &&
           !ParseWire("1,11",w) && w.chosen && w.missionDefault,"another version or a malformed value refused, the output untouched");

    Expect(!RemoteMember(0) && RemoteMember(2) && !RemoteMember(1) && RemoteMember(0xFFFFFFFFu),"member +0x10 bit 1");
    Expect(kMemberIndex==0x48 && kMemberFlags==0x10 && ValidIndex(0) && ValidIndex(3) && !ValidIndex(-1) && !ValidIndex(kMissionPlayers),
           "the member's mission index");

    Expect(kItemEdf41==static_cast<std::int32_t>(kStockItems),"our item follows the five stock ones");
    Expect(Grown(0)==2 && Grown(5)==12,"reserve(capacity * 2 + 2)");
    Expect(Cursor(true,3)==kItemEdf41 && Cursor(false,3)==3,"the cursor");
    Expect(kItemSize==0x90 && kItemImpl==0x58 && kReqVector==0x150 && kReqCursor==0x148 && kReqType==0x128,"the request's layout");
    Expect(kViewPlayer==0x2AC && kViewSlotMessage==3,"the 3D view's player and OnMessage slot");
    std::printf("outfit41_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
