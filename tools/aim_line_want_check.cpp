// The stock red aim lines' decision per seat (src/aim_line_want.h), checked without the game (the user, 2026-10-10:
// "npc载具红线会显示出来"): the support's crews are real soldiers seated on the spot, so the seat says Rider::other, not
// the Dummy the old test knew; read as an NPC (npcai.cpp NpcInSeat) their lines are hidden like a Dummy's were.
//  - an NPC in the seat, Dummy or real soldier alike: hidden;
//  - an empty seat: hidden, whoever drives (the user, 2026-10-14: 「飞机在外面会有红色预瞄线」: a requested aircraft
//    delivered empty, a drone the plugin flies with no one seated, an aircraft whose crew is still walking aboard);
//  - the player: hidden only where our sight replaces it, given back otherwise;
//  - another machine's player: given back (it may have been hidden while the seat was empty).
// The old readings (the seat's rider class alone; an empty seat kept unless an NPC drove) are checked to reproduce the
// reports, so this fails if they stop doing so. Exit code 1 when one fails.
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
// The 2026-10-10 rule for an empty seat: hidden only when an NPC drove the vehicle, else left as the game drew it.
LineWant OldEmpty(bool npcDriven) { return npcDriven ? LineWant::hide : LineWant::keep; }
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
    Expect(Want(Now(realCrew),false)==LineWant::hide,"a real soldier's gun: hidden");
    Expect(Want(Old(realCrew),false)!=LineWant::hide,"the old reading left a real crew's line drawn (the 2026-10-10 report)");
    Expect(Want(Now(dummy),false)==LineWant::hide,"a mission script's Dummy: hidden as before");
    // Empty seats: the 410's door guns aimed by the plugin, and the aircraft nobody sits in (2026-10-14).
    Expect(Want(Now(empty),false)==LineWant::hide,"an empty door gun of an NPC-driven heli: hidden");
    Expect(Want(Now(empty),false)==LineWant::hide,"an empty seat of an aircraft nobody drives (requested, a drone): hidden");
    Expect(OldEmpty(false)==LineWant::keep,"the old rule left an unpiloted aircraft's line drawn (the 2026-10-14 report)");
    // The player: getting in after the seat was hidden empty gives it back without our sight, keeps it hidden with it.
    Expect(Want(Now(you),true)==LineWant::hide,"the player with our sight: hidden");
    Expect(Want(Now(you),false)==LineWant::show,"the player without our sight: given back");
    // Another machine's player (online): theirs to see, given back if hidden while the seat was empty.
    Expect(Want(Now(remotePlayer),false)==LineWant::show,"another machine's player: given back");
    std::printf("%d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
