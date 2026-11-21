// commander_logic.h offline (docs/feature-2026-11-21-commander-mode.md): the NPC kind a player's class and weapons make,
// the request's retries and give-up, the defeat's arming and debounce. No game.
#include "commander_logic.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace crew;
using namespace crew::commander;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
// A weapon id of `kind`'s series (the tier table's first row of it).
int IdOf(SupportWeapon kind) {
    for(const auto& r:npctier::kTierRows)if(r.kind==static_cast<int>(kind))return r.id;
    return -1;
}

void KindChoice() {
    const int none[6]={-1,-1,-1,-1,-1,-1};
    Check(KindFor(kRanger,none,6)==static_cast<int>(SupportWeapon::rifle),"a Ranger with nothing: the rifle");
    Check(KindFor(kWingDiver,none,6)==static_cast<int>(SupportWeapon::wingLance),"a Wing Diver with nothing: the lance");
    Check(KindFor(kFencer,none,6)==static_cast<int>(SupportWeapon::fencerCannon),"a Fencer with nothing: the cannon");
    Check(KindFor(kAirRaider,none,6)==static_cast<int>(SupportWeapon::rifle),"an Air Raider: a Ranger with a rifle");
    Check(KindFor(-1,none,6)==-1 && KindFor(4,none,6)==-1,"no class: none");
    // Every class's kinds: its first equipped weapon of the kind's series picks the kind.
    const struct { int cls; SupportWeapon kind; } cases[]={
        {kRanger,SupportWeapon::sniper},{kRanger,SupportWeapon::shotgun},{kRanger,SupportWeapon::rocket},{kRanger,SupportWeapon::flame},
        {kWingDiver,SupportWeapon::wingLaser},{kWingDiver,SupportWeapon::wingThunderBow},{kWingDiver,SupportWeapon::wingIzuna},
        {kFencer,SupportWeapon::fencerShotgun},{kFencer,SupportWeapon::fencerPileBanker},{kFencer,SupportWeapon::fencerMiddleCannon}};
    for(const auto& c:cases) {
        const int id=IdOf(c.kind);
        Check(id>=0,"the kind has a series");
        const int equipped[6]={-1,id,-1,-1,-1,-1};
        Check(KindFor(c.cls,equipped,6)==static_cast<int>(c.kind),"the equipped weapon's kind");
        Check(CommanderKind(static_cast<std::uint32_t>(c.kind)),"a player class's kind");
    }
    // Slot order: the first weapon a kind carries wins; one no kind of the class carries is passed over.
    const int sniper=IdOf(SupportWeapon::sniper),rocket=IdOf(SupportWeapon::rocket),lance=IdOf(SupportWeapon::wingLance);
    const int order[6]={lance,rocket,sniper,-1,-1,-1};
    Check(KindFor(kRanger,order,6)==static_cast<int>(SupportWeapon::rocket),"a Wing Diver's lance passed over by a Ranger");
    // An Air Raider's own weapons are no kind's: the rifle whatever it carries.
    Check(KindFor(kAirRaider,order,6)==static_cast<int>(SupportWeapon::rifle),"an Air Raider carrying a rocket row: still the rifle");
    Check(!CommanderKind(static_cast<std::uint32_t>(SupportWeapon::xmInfantry)) && !CommanderKind(static_cast<std::uint32_t>(SupportWeapon::count)),
          "the X-Morph infantry is no player's class");
}

void Requests() {
    Request r;
    Check(NextAsk(r,false,0)==Ask::wait,"not ready: wait");
    Check(NextAsk(r,true,100)==Ask::ask,"ready: ask");
    r.tries=1;r.at=100;
    Check(NextAsk(r,true,100+kRetryMs-1)==Ask::wait && NextAsk(r,true,100+kRetryMs)==Ask::ask,"nothing came of it: again after kRetryMs");
    r.tries=kTries;r.at=0;
    Check(NextAsk(r,true,kRetryMs)==Ask::giveUp,"every try spent: given up");
    r.given=true;
    Check(NextAsk(r,true,10*kRetryMs)==Ask::wait,"its NPC here: never again");
    Request g;g.gaveUp=true;
    Check(NextAsk(g,true,0)==Ask::wait,"given up: never again");
}

void Defeat() {
    Watch w;
    Check(!Sample(w,false,0,1000) && !Sample(w,false,0,1000+10*kLostMs),"before its NPC: nothing to lose");
    Check(!Sample(w,true,1,2000) && w.armed,"its NPC seen: armed");
    Check(!Sample(w,false,0,3000),"the first empty sample: not yet");
    Check(!Sample(w,false,2,3000+kLostMs/2),"some back (a seat change): the wait starts over");
    Check(!Sample(w,false,0,4000) && !Sample(w,false,0,4000+kLostMs-1),"empty under kLostMs: not yet");
    Check(Sample(w,false,0,4000+kLostMs) && w.lost,"empty for kLostMs: lost");
    Check(!Sample(w,false,0,4000+10*kLostMs),"lost once");
    Watch other;
    Check(!Sample(other,true,3,10) && !Sample(other,false,3,20),"its own NPC down, others standing: fighting on");
}
}  // namespace

int main() {
    KindChoice();
    Requests();
    Defeat();
    std::printf("commander_logic_test: %d checks passed\n",checks);
    return 0;
}
