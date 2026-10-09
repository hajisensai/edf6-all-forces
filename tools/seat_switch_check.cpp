// The seat switch's decisions (src/seat_switch_logic.h) checked without the game (the user, 2026-10-09: "切换载具席位的
// 时候，旧席位会显示有人，切换不回去"):
//  - one press of a key held across a move is one press, though the seat's input mode byte reads "pad" for a frame
//    (the new seat's own stale byte: the log's "(pad)" on the move's frame, then the bounce 0 -> 1 -> 0);
//  - the old reading (the key read through the seat's input mode) is checked to make that second press, so the check
//    fails if it stops reproducing the log;
//  - the move carries the rider's input mode and buttons to the new seat: it reads as the rider's from the move's frame;
//  - the next-seat key goes round every seat the player may take, an NPC's (the driver the player left, SeatPilot) too:
//    driver -> gunner -> other gunner -> driver again; a number key never picks the seat sat in.
// Exit code 1 when one fails. cmake --build build --target seat_switch_check && build\seat_switch_check.exe
#include "../src/seat_switch_logic.h"
#include <cstdio>

namespace {
using namespace crew;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// Frames of a key held down (`down`) while the seat's input mode reads `pad` (true: the pad's, the key not read). The
// presses the new tracking makes, and the old one's (the key's held state read through the mode).
struct Count { int now,old; };
Count Presses(const bool* down,const bool* pad,int frames) {
    bool held=false,oldHeld=false;
    Count c{0,0};
    for(int f=0;f<frames;++f) {
        if(seatsw::Press(held,down[f]) && !pad[f])++c.now;
        const bool seen=!pad[f] && down[f];
        if(seen && !oldHeld)++c.old;
        oldHeld=seen;
    }
    return c;
}

void HeldAcrossTheMove() {
    // F held 6 frames; the move on frame 1; the new seat reads "pad" on frame 2 (its stale byte), "keys" after.
    const bool down[]={true,true,true,true,true,true,false,false};
    const bool pad[]={false,false,true,false,false,false,false,false};
    const Count c=Presses(down,pad,8);
    Expect(c.now==1,"a key held across the move is one press");
    Expect(c.old==2,"the old tracking makes it two (the log's bounce 0 -> 1 -> 0)");
    // Let go and pressed again: a second press.
    const bool again[]={true,true,false,false,true,true,false,false};
    const bool keys[8]={};
    Expect(Presses(again,keys,8).now==2,"a key let go and pressed again is a second press");
}

void CarriedInput() {
    unsigned char from[0x300]={},to[0x300]={};
    from[seatsw::kSeatPad]=0;from[seatsw::kSeatButtons]=0x02;from[seatsw::kSeatButtons+1]=0x01;   // keys; B and bit 8 down
    to[seatsw::kSeatPad]=1;to[seatsw::kSeatButtons]=0x40;                                          // the seat's stale state
    const auto in=seatsw::ReadRiderInput(from);
    from[seatsw::kSeatButtons]=0;from[seatsw::kSeatButtons+1]=0;   // the leave zeroes the old seat's buttons after the read
    seatsw::CarryRiderInput(in,to);
    Expect(to[seatsw::kSeatPad]==0,"the new seat reads the rider's input mode (keys) from the move's frame");
    Expect(to[seatsw::kSeatButtons]==0x02 && to[seatsw::kSeatButtons+1]==0x01,"the new seat holds the rider's buttons");
    Expect(to[seatsw::kSeatPad-1]==0 && to[seatsw::kSeatButtons+2]==0,"nothing else of the seat written");
}

enum class Holder { empty, you, npc, other };
void Rotation() {
    // The Titan of the log: 3 seats. The player moved 0 -> 1; an NPC driver was put in 0 (SeatPilot).
    Holder seats[3]={Holder::npc,Holder::you,Holder::empty};
    const auto takeable=[&](unsigned i){ return seats[i]==Holder::empty || seats[i]==Holder::npc; };
    Expect(seatsw::Wanted(3,1,-1,takeable)==2,"next from the gunner: the empty seat");
    seats[1]=Holder::empty;seats[2]=Holder::you;
    Expect(seatsw::Wanted(3,2,-1,takeable)==0,"next from the last seat: the driver's, its NPC changing places");
    seats[0]=Holder::you;seats[2]=Holder::npc;   // the NPC driver took the seat the player left
    Expect(seatsw::Wanted(3,0,-1,takeable)==1,"next from the driver's: round again");
    // Another player's seat and a seat not the player's class are passed by.
    Holder full[4]={Holder::you,Holder::other,Holder::other,Holder::empty};
    const auto open=[&](unsigned i){ return full[i]==Holder::empty || full[i]==Holder::npc; };
    Expect(seatsw::Wanted(4,0,-1,open)==3,"another player's seats passed by");
    Holder none[2]={Holder::you,Holder::other};
    Expect(seatsw::Wanted(2,0,-1,[&](unsigned i){ return none[i]==Holder::empty; })==-1,"no seat to take: none");
    // Number keys: that seat, never the one sat in or one past the vehicle's.
    Expect(seatsw::Wanted(3,1,0,takeable)==0 && seatsw::Wanted(3,1,1,takeable)==-1 && seatsw::Wanted(3,1,3,takeable)==-1,
           "a number key: that seat, not the one sat in, not past the seats");
}
}  // namespace

int main() {
    HeldAcrossTheMove();
    CarriedInput();
    Rotation();
    std::printf(failures ? "%d of %d FAILED\n" : "all %d passed\n",failures ? failures : checks,checks);
    return failures ? 1 : 0;
}
