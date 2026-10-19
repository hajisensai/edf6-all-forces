// The test range's 「摆放」 panel on the M map (src/range_place.h) without the game: it opens on its first key and stays as
// it was, a row's click picks and arms it (again: disarmed), the right button disarms, shutting it disarms, place asks for
// the picked row only while it is open, the moves go round a category and keep each category's pick, the arrows page by
// the rows shown and stop at the ends, the wheel scrolls the list, the view keeps the pick in it; and the catalog's tabs are all there. Exit code 1
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
    p.window=8;
    Step(p,down);
    Expect(p.pick[0]==2+8 && p.top[0]==8,"an arrow: the list and the pick a page (the rows shown) further");
    for(int i=0;i<20;++i)Step(p,down);
    Expect(p.pick[0]==n-1 && p.top[0]==n-8,"the arrows stop at the last row, the last page shown");
    Press up{};up.page=-1;
    for(int i=0;i<20;++i)Step(p,up);
    Expect(p.pick[0]==0 && p.top[0]==0,"and at the first");
    p.category=99;
    Step(p,next);
    Expect(p.category==0,"a broken category resets to the first");
}

void Window() {
    Expect(ViewFirst(0,0,40,12)==0 && ViewFirst(0,5,40,12)==0,"near the top: from the first row");
    Expect(ViewFirst(10,20,40,12)==10,"the list's own scroll kept while the pick is in view");
    Expect(ViewFirst(0,20,40,12)==9,"a pick below the view brings the list down to it");
    Expect(ViewFirst(30,20,40,12)==20,"a pick above the view brings the list up to it");
    Expect(ViewFirst(99,39,40,12)==28,"never past the last page");
    Expect(ViewFirst(5,3,8,12)==0,"fewer rows than the window: all of them");
    for(int top=-3;top<70;top+=7)for(int pick=0;pick<60;++pick) {
        const int first=ViewFirst(top,pick,60,12);
        Expect(first>=0 && first<=pick && pick<first+12 && first+12<=60,"the pick always in view, the window inside the rows");
    }
    Expect(Pages(40,12)==4 && PageOf(0,40,12)==1 && PageOf(12,40,12)==2 && PageOf(28,40,12)==4,"the page line");
    // The wheel scrolls the list; the pick follows only to stay in view, and nothing scrolls past the ends.
    Panel p{};p.open=true;p.window=8;
    const int n=CountIn(Category::enemy41);
    Press wheel{};wheel.scroll=3;
    Expect(Step(p,wheel).act==Act::moved && p.top[0]==3 && p.pick[0]==3,"the wheel down: three rows, the pick dragged into view");
    p.pick[0]=6;Step(p,wheel);
    Expect(p.top[0]==6 && p.pick[0]==6,"a pick still in view stays");
    p.category=static_cast<int>(Category::enemy41);
    for(int i=0;i<40;++i)Step(p,wheel);
    Expect(p.top[p.category]==n-8,"the wheel stops at the last page");
    Press back{};back.scroll=-3;
    for(int i=0;i<40;++i)Step(p,back);
    Expect(p.top[p.category]==0 && p.pick[p.category]<8,"and at the first, the pick in view");
    Panel shut{};
    Expect(Step(shut,wheel).act==Act::none && !shut.open,"shut: the wheel is the map's (zoom)");
}

void Holding() {
    Repeat r{};
    Expect(Repeats(r,true,false,1000),"the press itself");
    Expect(!Repeats(r,true,true,1000+kRepeatFirstMs-1),"held: nothing until the first delay");
    Expect(Repeats(r,true,true,1000+kRepeatFirstMs),"then a repeat");
    Expect(!Repeats(r,true,true,1000+kRepeatFirstMs+kRepeatMs-1) && Repeats(r,true,true,1000+kRepeatFirstMs+kRepeatMs),"and one every interval");
    Expect(!Repeats(r,false,true,5000) && Repeats(r,true,false,5001),"let go and pressed again: a press at once");
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
    Holding();
    Catalog();
    std::printf("range_place_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
