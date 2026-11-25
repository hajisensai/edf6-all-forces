// The game's own dialog pages' pure side (src/room_dialog_logic.h) offline: a Choice packed into a button's id and back,
// and the unit / soldier lines 整备's placeholder lists show.
// cmake --build build --target room_dialog_test && build\room_dialog_test.exe
#include "../src/room_dialog_logic.h"
#include <cstdio>
#include <initializer_list>
#include <cwchar>

namespace {
using namespace crew;
using namespace crew::roomdialog;
namespace rm=crew::roommenu;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
std::size_t NoIni(void*,const wchar_t*,wchar_t* out,std::size_t) noexcept { out[0]=0; return 0; }
rm::Menu Fresh() {
    rm::Menu m;
    m.open=true;
    rm::Load(m,rm::kDefaultUnits,rm::kDefaultUnitCount,&NoIni,nullptr);
    return m;
}
void PackRoundTrip() {
    for(int w=0;w<=static_cast<int>(Do::set);++w)
        for(std::int32_t a:{0,1,47,-2,-1,0x7FFFFF,0xFFFFFF}) {
            const Choice c{static_cast<Do>(w),a};
            const Choice back=Unpack(Pack(c));
            Expect(back.what==c.what && back.arg==c.arg,"Pack / Unpack");
            Expect((Pack(c)>>40)==0,"an id fits the low 40 bits (room_dialog.cpp puts the dialog's serial above)");
        }
}

// Every default unit's line names it; every soldier's line is not empty and fits.
void Lines() {
    rm::Menu m=Fresh();
    for(int i=0;i<m.unitCount;++i) {
        wchar_t line[kTextMost];
        UnitLine(m.unit[i],line,kTextMost);
        Expect(line[0] && std::wcsstr(line,UnitName(m.unit[i]))!=nullptr,"a unit's line names it");
        if(!rm::IsSquad(m.unit[i]))continue;
        for(int s=0;s<m.unit[i].squad.count;++s) {
            SoldierLine(m.unit[i],s,line,kTextMost);
            Expect(line[0] && std::wcslen(line)<kTextMost,"a soldier's line");
        }
    }
    wchar_t tiny[4];
    UnitLine(m.unit[0],tiny,4);
    Expect(std::wcslen(tiny)<4,"a short buffer: cut, terminated");
}
}  // namespace

int main() {
    PackRoundTrip();
    Lines();
    std::printf(failures ? "room_dialog_test: %d of %d FAILED\n" : "room_dialog_test: %d checks passed\n",failures ? failures : checks,checks);
    return failures ? 1 : 0;
}
