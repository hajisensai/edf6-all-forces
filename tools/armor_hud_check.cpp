// The stock armor gauge's hold while the player rides (src/stock_armor_hud.h over map_stock_hud.h's record) checked
// without the game (the user, 2026-10-09: "上了载具以后，可以把原版的左上角的血条hud隐藏吧"): a gauge's root shown flag
// stepped as stockgauge.cpp ArmorHook steps it, one update a frame, `hide` being the cover (our HUD lists the vehicle
// its player rides):
//  - boarding hides it; a seat switch (still covered) keeps it hidden; getting out, the vehicle wrecked (the player off
//    it), HideStockGauges / the plugin off, our HUD unable to draw text: shown again, the value it had put back;
//  - a gauge whose flag was 0 before the hold (hidden by something else) is left at 0 when the hold ends;
//  - a mission ending while it is held: the next mission's gauge at another address starts shown and untouched; one at
//    the same address (a new object there) is shown, not left hidden by the old hold;
//  - two local players' gauges are held apart; old missions' holds give their records up to new gauges.
// Exit code 1 when one fails. cmake --build build --target armor_hud_check && build\armor_hud_check.exe
#include "../src/stock_armor_hud.h"
#include <cstdio>

namespace {
using namespace crew;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

struct Gauge { unsigned char root[0x200]; };   // the root node; +0x1F8 its shown flag
constexpr std::size_t kShown=0x1F8;

void Ride() {
    maphud::Record holds[armorhud::kGauges]{};
    Gauge g{};g.root[kShown]=1;
    std::uint64_t mission=1;
    const auto frame=[&](bool hide){ return armorhud::Step(holds,&g,mission,hide,g.root+kShown); };
    frame(false);
    Expect(g.root[kShown]==1,"on foot: shown, untouched");
    Expect(frame(true) && g.root[kShown]==0,"boarding: hidden");
    Expect(frame(true) && g.root[kShown]==0,"a seat switch (still covered): still hidden");
    Expect(!frame(false) && g.root[kShown]==1,"getting out / wrecked / HideStockGauges off: shown again");
    frame(false);
    Expect(g.root[kShown]==1,"on foot after: left alone");
    // Hidden by something else before: the hold ends with it as it was.
    g.root[kShown]=0;
    frame(true);frame(true);
    Expect(!frame(false) && g.root[kShown]==0,"a flag that was 0 before the hold is left 0");
    g.root[kShown]=1;
    // Shown by something else during the hold: kept hidden, shown at its end (the game's latest).
    frame(true);g.root[kShown]=1;frame(true);
    Expect(g.root[kShown]==0,"written 1 during the hold: hidden again");
    Expect(!frame(false) && g.root[kShown]==1,"...and shown at its end");
}

void Missions() {
    maphud::Record holds[armorhud::kGauges]{};
    Gauge a{},b{};a.root[kShown]=1;b.root[kShown]=1;
    armorhud::Step(holds,&a,1,true,a.root+kShown);
    Expect(a.root[kShown]==0,"mission 1: held");
    // The mission ends while held. Mission 2's gauge elsewhere: shown, untouched.
    armorhud::Step(holds,&b,2,false,b.root+kShown);
    Expect(b.root[kShown]==1,"mission 2's gauge at another address: untouched");
    // A new gauge at the old address (its constructor showed it): not left hidden by the old hold.
    a.root[kShown]=1;
    Expect(!armorhud::Step(holds,&a,2,false,a.root+kShown) && a.root[kShown]==1,"a new gauge at the old address: shown");
    a.root[kShown]=1;
    Expect(armorhud::Step(holds,&a,2,true,a.root+kShown) && a.root[kShown]==0,"...and held in its own mission");
    armorhud::Step(holds,&a,2,false,a.root+kShown);
    Expect(a.root[kShown]==1,"...and let go");
}

void TwoPlayersAndOldHolds() {
    maphud::Record holds[armorhud::kGauges]{};
    Gauge old[armorhud::kGauges]{};
    for(auto& g:old){g.root[kShown]=1;armorhud::Step(holds,&g,1,true,g.root+kShown);}   // every record held (mission 1)
    Gauge p1{},p2{};p1.root[kShown]=1;p2.root[kShown]=1;
    Expect(armorhud::Step(holds,&p1,2,true,p1.root+kShown) && p1.root[kShown]==0,"an old mission's hold gives its record up");
    Expect(armorhud::Step(holds,&p2,2,false,p2.root+kShown)==false && p2.root[kShown]==1,"the second player on foot: shown");
    Expect(armorhud::Step(holds,&p2,2,true,p2.root+kShown) && p2.root[kShown]==0,"the second player rides: held apart");
    Expect(!armorhud::Step(holds,&p1,2,false,p1.root+kShown) && p1.root[kShown]==1 && p2.root[kShown]==0,
           "the first gets out: only theirs shown");
    Expect(!armorhud::Step(holds,&p2,2,false,p2.root+kShown) && p2.root[kShown]==1,"the second gets out: shown");
}
}  // namespace

int main() {
    Ride();
    Missions();
    TwoPlayersAndOldHolds();
    std::printf(failures ? "%d of %d FAILED\n" : "all %d passed\n",failures ? failures : checks,checks);
    return failures ? 1 : 0;
}
