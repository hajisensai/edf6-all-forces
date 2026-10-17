// The test range's 「摆放」 panel on the M map (src/range_place.h) without the game: it opens on its first key and stays as
// it was, a row's click picks and arms it (again: disarmed), the right button disarms, shutting it disarms, place asks for
// the picked row only while it is open, the moves go round a category and keep each category's pick, the arrows page by
// a window and stop at the ends, the window keeps the pick in view; and the catalog's tabs are all there. Exit code 1
// when one fails.
// cmake --build build --target range_place_test && build\range_place_test.exe
#include "../src/range_place.h"
#include <cstdio>
#include <string_view>

namespace {
using namespace crew::rangeplace;
using crew::debugspawn::CountIn;
using crew::debugspawn::RowOf;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
Press Key(void (*set)(Press&)) { Press p{};set(p);return p; }

void Opening() {
    Panel p{};
    Expect(!p.open && p.armed<0,"it starts shut, nothing armed");
    Expect(Step(p,Key([](Press& k){k.place=true;})).act==Act::none && !p.open,"shut: place does nothing (Enter / A are the map's)");
    Expect(Step(p,Key([](Press& k){k.close=true;})).act==Act::none && !p.open,"shut: close does nothing (B closes the map)");
    Expect(Step(p,Key([](Press& k){k.next=true;})).act==Act::opened && p.open && p.pick[0]==0,"the first move opens it, moving nothing");
    Expect(Step(p,Key([](Press& k){k.next=true;})).act==Act::moved && p.pick[0]==1,"then it moves");
    Expect(Step(p,Key([](Press& k){k.toggle=true;})).act==Act::closed && !p.open,"the title's click shuts it");
    Expect(Step(p,Key([](Press& k){k.toggle=true;})).act==Act::opened && p.open && p.pick[0]==1,"and opens it as it was");
}

void Arming() {
    Panel p{};
    const int row=RowOf(Category::enemy41,3);
    Expect(row>=0,"an EDF4.1 enemy's fourth row");
    Result r=Step(p,Key([](Press&){}));
    Press k{};k.row=row;
    r=Step(p,k);
    Expect(r.act==Act::armed && r.row==row && p.armed==row && p.open,"a row's click arms it (and opens the panel)");
    Expect(p.category==static_cast<int>(Category::enemy41) && p.pick[p.category]==3 && Picked(p)==row,"and picks it in its category");
    r=Step(p,k);
    Expect(r.act==Act::disarmed && p.armed<0,"clicked again: disarmed");
    Step(p,k);
    Press cancel{};cancel.cancel=true;
    Expect(Step(p,cancel).act==Act::disarmed && p.armed<0,"the right button disarms it");
    Expect(Step(p,cancel).act==Act::none,"nothing armed: the right button is the map's");
    Step(p,k);
    Press shut{};shut.close=true;
    Expect(Step(p,shut).act==Act::closed && !p.open && p.armed<0,"shutting the panel disarms it");
    Step(p,k);
    Press tab{};tab.tab=static_cast<int>(Category::vehicle);
    Expect(Step(p,tab).act==Act::moved && p.category==0 && p.armed==row,"a tab shows its category; the armed row stays armed");
    Press place{};place.place=true;
    r=Step(p,place);
    Expect(r.act==Act::place && r.row==RowOf(Category::vehicle,p.pick[0]),"place: the row picked in the category shown");
}

void Moving() {
    Panel p{};p.open=true;
    const int n=CountIn(Category::vehicle);
    Press prev{};prev.prev=true;
    Expect(Step(p,prev).act==Act::moved && p.pick[0]==n-1,"prev from the first wraps to the last");
    Press next{};next.next=true;
    Step(p,next);
    Expect(p.pick[0]==0,"next from the last wraps to the first");
    Press cat{};cat.catNext=true;
    Step(p,next);Step(p,next);
    Step(p,cat);
    Expect(p.category==1 && p.pick[1]==0,"the next category, from its first row");
    Press back{};back.catPrev=true;
    Step(p,back);
    Expect(p.category==0 && p.pick[0]==2,"and back: the pick kept");
    for(int i=0;i<kCategoryCount;++i)Step(p,back);
    Expect(p.category==0,"round the categories both ways");
    Press down{};down.page=1;
    Step(p,down);
    Expect(p.pick[0]==(2+kWindow<n ? 2+kWindow : n-1),"an arrow: a window further");
    for(int i=0;i<20;++i)Step(p,down);
    Expect(p.pick[0]==n-1,"the arrows stop at the last row");
    Press up{};up.page=-1;
    for(int i=0;i<20;++i)Step(p,up);
    Expect(p.pick[0]==0,"and at the first");
    p.category=99;
    Step(p,next);
    Expect(p.category==0,"a broken category resets to the first");
}

void Window() {
    Expect(WindowFirst(0,40,12)==0 && WindowFirst(5,40,12)==0,"near the top: from the first row");
    Expect(WindowFirst(20,40,12)==14,"in the middle: the pick in the middle");
    Expect(WindowFirst(39,40,12)==28,"at the end: the last window");
    Expect(WindowFirst(3,8,12)==0,"fewer rows than the window: all of them");
    for(int pick=0;pick<60;++pick) {
        const int first=WindowFirst(pick,60,12);
        Expect(first>=0 && first<=pick && pick<first+12 && first+12<=60,"the pick always in view, the window inside the rows");
    }
}

void Catalog() {
    for(int c=0;c<kCategoryCount;++c)Expect(CountIn(static_cast<Category>(c))>0,"every tab has rows");
    // The rows the user asked for (2026-10-17): EDF4.1's motherships and UFO robot, the civilian sedan, the rescue APC 507.
    bool mother301=false,mother401=false,robo=false,sedan=false,rescue=false;
    for(const auto& e:crew::debugspawn::kEntries) {
        if(!e.sgo)continue;
        const std::wstring_view s(e.sgo);
        mother301=mother301 || s==L"app:/object/UFOMOTHER301.sgo";
        mother401=mother401 || s==L"app:/object/UFOMOTHER401.sgo";
        robo=robo || s==L"app:/object/UFOROBO401.sgo";
        sedan=sedan || s==L"app:/object/EDF6VC_SEDAN.sgo";
        rescue=rescue || s==L"app:/object/V507_RESCUETANK_AI.sgo";
    }
    Expect(mother301 && mother401 && robo,"EDF4.1's motherships and UFO robot are rows");
    Expect(sedan && rescue,"the civilian sedan and the rescue APC 507 are rows");
}
}  // namespace

int main() {
    Opening();
    Arming();
    Moving();
    Window();
    Catalog();
    std::printf("range_place_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
