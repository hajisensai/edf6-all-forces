// src/seat_events.h offline: a crew created aboard (support_dispatch.cpp BoardAirborne -> npcai.cpp NpcSeatCrewNow) plays
// no boarding sound, a soldier who walks aboard still does, and leaving sounds as before. The vehicle side is the stock
// one modelled bit for bit from EDF.dll (seat_events.h): the seat scan (0x62F160..0x62F1BD) marks a seat whose rider
// differs from its bit, the sound site (0x632A60) plays a marked seat's boarding / leaving preset, the base update
// (0x630596) clears the marks. The user, 2026-10-09: "来支援的时候会有上车声音", "上车声是原版的上载具声音".
#include "../src/seat_events.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,int a=0,int b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL %s (%d, %d)\n",what,a,b);}
}
// One stock vehicle frame: the seat scan over `riders`, then the sound site (counts what it would play), then the clear.
struct Vehicle {
    std::uint16_t mask=0;unsigned seats=0;bool rider[16]{};
    int boarded=0,left=0;
    void Frame() noexcept {
        for(unsigned i=0;i<seats;++i)if(seatevt::ScanChanges(mask,i,rider[i])) {
            const std::uint16_t bit=seatevt::SeatBit(i);
            mask=static_cast<std::uint16_t>(rider[i] ? mask|bit : mask&~bit);
            mask=static_cast<std::uint16_t>(mask|seatevt::ChangeBit(seats,i));
        }
        for(unsigned i=0;i<seats;++i)if(seatevt::Sounds(mask,seats,i))(rider[i] ? boarded : left)+=1;
        mask=static_cast<std::uint16_t>(mask&((1u<<seats)-1u));   // 0x630596: and [+0x628], [+0x624] (the taken bits)
    }
};
}  // namespace

int main() {
    // The old way (the user's report): soldiers made aboard, nothing told the vehicle: its next scan sees new riders.
    {
        Vehicle v;v.seats=3;v.Frame();
        v.rider[0]=v.rider[1]=v.rider[2]=true;v.Frame();
        Check(v.boarded==3,"without the fix the vehicle plays a boarding sound for each crew made aboard",v.boarded);
    }
    // Made aboard with Quiet: the vehicle holds the seats as taken from the start; no sound, then or later.
    {
        Vehicle v;v.seats=3;v.Frame();
        for(unsigned i=0;i<3;++i){v.rider[i]=true;v.mask=seatevt::Quiet(v.mask,v.seats,i);}
        v.Frame();v.Frame();
        Check(v.boarded==0 && v.left==0,"made aboard: no boarding sound",v.boarded,v.left);
        v.rider[1]=false;v.Frame();
        Check(v.left==1,"one getting off later still sounds",v.left);
        v.rider[1]=true;v.Frame();
        Check(v.boarded==1,"one walking back aboard still sounds",v.boarded);
    }
    // RideVehicle marking the change itself the same frame: Quiet clears that mark too.
    {
        Vehicle v;v.seats=4;v.Frame();
        v.rider[2]=true;v.mask=static_cast<std::uint16_t>(v.mask|seatevt::SeatBit(2)|seatevt::ChangeBit(v.seats,2));
        v.mask=seatevt::Quiet(v.mask,v.seats,2);v.Frame();
        Check(v.boarded==0,"a change already marked is cleared",v.boarded);
    }
    // The native arithmetic at its edges: seats from 8 up have no bits (shl al), a mark past bit 15 none (shl ax).
    Check(seatevt::SeatBit(7)==0x80 && seatevt::SeatBit(8)==0 && seatevt::ChangeBit(13,7)==0 && seatevt::ChangeBit(5,2)==0x80,
          "8-bit seat bits, 16-bit marks");
    Check(seatevt::Quiet(0,13,10)==0 && seatevt::Quiet(0xFFFF,5,1)==static_cast<std::uint16_t>(0xFFFF&~0x40),
          "an untracked seat leaves the mask alone; a tracked one loses only its own mark");
    {
        // A 13-seat transport helicopter: seats 8..12 never sound either way (the vehicle does not track them).
        Vehicle v;v.seats=13;v.Frame();
        for(unsigned i=0;i<13;++i){v.rider[i]=true;v.mask=seatevt::Quiet(v.mask,v.seats,i);}
        v.Frame();
        Check(v.boarded==0,"the transport's thirteen seats: silent",v.boarded);
    }
    std::printf(failures ? "seat_events_check: %d of %d FAILED\n" : "seat_events_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
