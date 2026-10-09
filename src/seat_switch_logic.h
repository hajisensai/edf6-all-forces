// The seat switch's decisions (seatswitch.cpp), kept apart from the game so tools/seat_switch_check.cpp checks them off it.
//
// The user, 2026-10-09: "切换载具席位的时候，旧席位会显示有人，切换不回去". The log (Debug=1) showed one press of the
// next-seat key moving the player twice (0 -> 1, then 1 -> 0 42 ms later) and a number key refused one frame after it had
// moved them ("seat 1 refused" right after "moved seat 0 -> 1": the line then reads "SEAT 2 TAKEN", the seat just taken).
// The seat a player has never sat in holds no input of theirs yet: its input mode byte (+0x2B0, 1 pad / 0 keys) is what
// was there (the GUNNER line logged "(pad)" for a keyboard player on the move's frame), its button word what was there.
// The rider writes both only in its own step (HumanBase slot 4, docs/heli-input-re.md §4), after the vehicles' input this
// frame. The press tracking read the keys through that byte: the keyboard read as up for a frame ("not keys"), so the key
// still held came back as a new press. Two fixes, one each side:
//  - the rider's input mode and buttons go with them to the new seat at the move (CarryRiderInput: what the rider would
//    write there itself next step): the seat reads as the rider's from the move's own frame;
//  - a key's held state follows the key itself (Press on the physical key every frame), not the seat's input mode, so a
//    flip of that byte can never make a held key a new press.
// The next-seat key goes round every seat the player may take (an empty one, or one whose NPC changes places with them),
// as the number keys do: the driver's seat an NPC was put in when the player left it (SeatPilot) is on the way round
// again, not a seat the next-seat key can never reach.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew {
namespace seatsw {
// The seat's input block (docs/heli-input-re.md §4): 1 = a pad (0 keyboard and mouse), the buttons' word.
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;

// A key's press this frame: down now, up last frame. `held` is the key's own state, kept every frame whatever reads it.
inline bool Press(bool& held,bool down) noexcept {
    const bool press=down && !held;
    held=down;
    return press;
}

// The rider's own input state in their seat (read before the seat is left: the leave zeroes the buttons).
struct RiderInput { unsigned char pad; std::uint16_t buttons; };
inline RiderInput ReadRiderInput(const unsigned char* seat) noexcept {
    RiderInput in{seat[kSeatPad],0};
    std::memcpy(&in.buttons,seat+kSeatButtons,sizeof(in.buttons));
    return in;
}
// The new seat given the rider's input mode and buttons: what the rider's own step writes there from the next frame.
inline void CarryRiderInput(const RiderInput& in,unsigned char* seat) noexcept {
    seat[kSeatPad]=in.pad;
    std::memcpy(seat+kSeatButtons,&in.buttons,sizeof(in.buttons));
}

// The seat a press asks for, -1 none: `number` 0..8 a number key (that seat, not the one sat in), -1 the next-seat key
// (the next seat round the vehicle from `at` that `takeable(seat)` says the player may take now).
template<class Takeable>
int Wanted(unsigned count,unsigned at,int number,Takeable takeable) noexcept {
    if(number>=0)return static_cast<unsigned>(number)<count && static_cast<unsigned>(number)!=at ? number : -1;
    for(unsigned k=1;k<count;++k) {
        const unsigned to=(at+k)%count;
        if(takeable(to))return static_cast<int>(to);
    }
    return -1;
}
}  // namespace seatsw
}  // namespace crew
