// The stock red aim lines' decision per seat (src/aim_line_want.h), checked without the game (the user, 2026-10-10:
// "npc载具红线会显示出来"): the support's crews are real soldiers seated on the spot, so the seat says Rider::other, not
// the Dummy the old test knew; read as an NPC (npcai.cpp NpcInSeat) their lines are hidden like a Dummy's were.
//  - an NPC in the seat, Dummy or real soldier alike: hidden;
//  - an empty seat (a door gun nobody sits at) of a vehicle an NPC drives: hidden; of one nobody drives: left as it is;
//  - the player: hidden only where our sight replaces it, given back otherwise;
//  - another machine's player: left as it is.
// The old reading (the seat's rider class alone) is checked to leave a real crew's line drawn, so this fails if it
// stops reproducing the report. Exit code 1 when one fails.
// cmake --build build --target aim_line_want_check && build\aim_line_want_check.exe
#include "../src/aim_line_want.h"
#include <cstdio>

namespace {
using namespace crew::aimline;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// The seat as the game reads it, and what it really holds.
enum class Read { none, dummy, player, other };
struct Seat { Read read; bool npc; };   // npc: NpcInSeat (a Dummy, or a live soldier no player drives)

Holder Now(Seat s) {   // crew.cpp LineHolder
    switch(s.read) {
        case Read::none: return Holder::empty;
        case Read::player: return Holder::player;
        default: return s.npc ? Holder::npc : Holder::other;
    }
}
Holder Old(Seat s) {   // before: the rider's class alone, a real soldier Rider::other
    switch(s.read) {
        case Read::none: return Holder::empty;
        case Read::dummy: return Holder::npc;
        case Read::player: return Holder::player;
        default: return Holder::other;
    }
}
}  // namespace

int main() {
    const Seat dummy{Read::dummy,true},realCrew{Read::other,true},remotePlayer{Read::other,false},empty{Read::none,false},
               you{Read::player,false};
    // A support tank: a real soldier drives, a real soldier at the gun.
    Expect(Want(Now(realCrew),true,false)==LineWant::hide,"a real soldier's gun on an NPC-driven vehicle: hidden");
    Expect(Want(Now(realCrew),false,false)==LineWant::hide,"a real soldier's gun on a vehicle a player drives: hidden");
    Expect(Want(Old(realCrew),true,false)==LineWant::keep,"the old reading left a real crew's line drawn (the report)");
    Expect(Want(Now(dummy),false,false)==LineWant::hide,"a mission script's Dummy: hidden as before");
    // The 410's door guns with nobody in them, aimed by the plugin.
    Expect(Want(Now(empty),true,false)==LineWant::hide,"an empty door gun of an NPC-driven heli: hidden");
    Expect(Want(Now(empty),false,false)==LineWant::keep,"an empty seat of a vehicle nobody drives: left as it is");
    // The player.
    Expect(Want(Now(you),true,true)==LineWant::hide,"the player with our sight: hidden");
    Expect(Want(Now(you),true,false)==LineWant::show,"the player without our sight: given back");
    // Another machine's player (online): theirs to see.
    Expect(Want(Now(remotePlayer),true,false)==LineWant::keep,"another machine's player: left as it is");
    std::printf("%d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
