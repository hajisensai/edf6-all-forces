// The squads riding along with the player and the seats handed out by distance (src/ride_along.h), and DISMOUNT ALL's
// choices (src/mapcmd_logic.h DismountLeaves) checked offline. Test site #45 (docs/feedback-2026-11-10-wheel-follow.md).
#include "../src/ride_along.h"
#include "../src/mapcmd_logic.h"
#include "../src/map_buttons.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace ridealong;

// A vehicle of `n` seats (`roles`), every seat open, every member fitting each, the walk |member - seat| along a line.
Offer Line(int members,const float* memberAt,int seats,const int* roles,const float* seatAt) {
    Offer o;
    o.members=members;o.seats=seats;
    for(int k=0;k<seats;++k)o.seat[k]=Seat{roles[k],true};
    for(int m=0;m<members;++m)for(int k=0;k<seats;++k){o.fits[m][k]=true;o.distance[m][k]=std::fabs(memberAt[m]-seatAt[k]);}
    return o;
}

void Seats() {
    // An APC: the driver's seat, a gunner's, four passengers' along its side.
    const int roles[6]={kDriver,kGunner,kPassenger,kPassenger,kPassenger,kPassenger};
    const float seatAt[6]={0.0f,1.0f,10.0f,11.0f,12.0f,13.0f};
    {   // Member order no longer decides: the member standing by seat 5 gets seat 5, not seat 2.
        const float at[2]={13.0f,10.2f};
        Offer o=Line(2,at,6,roles,seatAt);
        int out[kMembersMost];
        Check(Assign(o,kAlongRoles,2,out)==2,"two members, two passenger seats");
        Check(out[0]==5 && out[1]==2,"each member the passenger seat nearest it (not the first free in member order)");
    }
    {   // Riding along: never the driver's seat; the gunner's once the passengers' are full.
        const float at[6]={0.0f,0.5f,10.0f,11.0f,12.0f,13.0f};
        Offer o=Line(6,at,6,roles,seatAt);
        int out[kMembersMost];
        const int given=Assign(o,kAlongRoles,2,out);
        Check(given==5,"riding along: four passengers and the gunner, the driver's seat left");
        bool driver=false;for(int m=0;m<6;++m)driver=driver || out[m]==0;
        Check(!driver,"riding along never takes the driver's seat");
        int gunner=-1;for(int m=0;m<6;++m)if(out[m]==1)gunner=m;
        Check(gunner==1 || gunner==0,"the gunner's seat to a member nearest it");
        int walking=0;for(int m=0;m<6;++m)walking+=out[m]<0;
        Check(walking==1,"the one with no seat walks on after the player");
    }
    {   // The map's BOARD: the driver first (the nearest to it), then the gunner, then the passengers.
        const float at[3]={12.0f,0.3f,9.0f};
        Offer o=Line(3,at,6,roles,seatAt);
        int out[kMembersMost];
        Check(Assign(o,kBoardRoles,3,out)==3,"BOARD seats all three");
        Check(out[1]==0,"the driver's seat to the member nearest it");
        Check(out[2]==1 || out[0]==1,"then the gunner's");
    }
    {   // A seat another squad's walker has (closed), a seat the class does not fit: never given.
        const float at[2]={10.0f,11.0f};
        Offer o=Line(2,at,6,roles,seatAt);
        o.seat[2].open=false;o.fits[1][3]=false;
        int out[kMembersMost];
        Assign(o,kAlongRoles,2,out);
        Check(out[0]!=2 && out[1]!=2,"a reserved seat is not offered (NPC bu qiang zaiju)");
        Check(out[1]!=3,"a seat the member's class does not fit is not given");
        Check(out[0]>=2 && out[1]>=2 && out[0]!=out[1],"both still seated, on different seats");
    }
    {   // No way there (a NaN or negative distance): no seat.
        const float at[1]={10.0f};
        Offer o=Line(1,at,6,roles,seatAt);
        for(int k=0;k<6;++k)o.distance[0][k]=-1.0f;
        int out[kMembersMost];
        Check(Assign(o,kBoardRoles,3,out)==0 && out[0]==-1,"no seat without a way to it");
    }
}

void Riding() {
    int a=0,b=0;const void* va=&a;const void* vb=&b;
    Follow f;
    // On foot, following: nothing.
    Check(Advance(f,true,nullptr,0).act==Act::none,"on foot: nothing");
    // The player gets in: the squad is offered the seats.
    Step s=Advance(f,true,va,100);
    Check(s.act==Act::board && f.along==va,"the player gets in: the squad boards with them");
    Check(Advance(f,true,va,200).act==Act::none,"no offer again before kRetryMs");
    Check(Advance(f,true,va,100+kRetryMs).act==Act::board,"offered again every kRetryMs while the player stays in");
    // The player gets out: those in that vehicle get out.
    s=Advance(f,true,nullptr,5000);
    Check(s.act==Act::leave && s.left==va && f.along==nullptr,"the player gets out: the riders of that vehicle get out too");
    Check(Advance(f,true,nullptr,6000).act==Act::none,"on foot again: nothing more");
    // Straight from one vehicle into another: out of the first, into the second.
    Advance(f,true,va,7000);
    s=Advance(f,true,vb,8000);
    Check(s.act==Act::leaveThenBoard && s.left==va && f.along==vb,"from one vehicle to another: out of the first, into the second");
    // A map order (BOARD elsewhere, DISMOUNT...): no more offers, no getting out with the player.
    Ordered(f);
    Check(Advance(f,true,vb,8000+kRetryMs*2).act==Act::none,"after a map order no offer into the player's vehicle");
    s=Advance(f,true,nullptr,20000);
    Check(s.act==Act::none && s.left==nullptr,"after a map order the squad does not get out with the player");
    // ...until the player's vehicle changes again.
    Check(Advance(f,true,va,21000).act==Act::board,"the next vehicle the player takes: offered again");
    // An order of its own (guard, move...) or a dismissal: not eligible, nothing of the ride kept.
    s=Advance(f,false,va,22000);
    Check(s.act==Act::none && f.along==nullptr,"not following: no ride");
    s=Advance(f,false,nullptr,23000);
    Check(s.act==Act::none && s.left==nullptr,"not following: no getting out with the player either");
    // Following again with the player already in a vehicle: no offer until the vehicle changes (FOLLOW resets the state
    // in npcai.cpp, which then offers it at once).
    Check(Advance(f,true,nullptr,24000).act==Act::none,"following again on foot");
    Follow fresh;fresh.seen=nullptr;
    Check(Advance(fresh,true,va,25000).act==Act::board,"FOLLOW with the player aboard: offered at once (a fresh state)");
}

void Hints() {
    int a=0;const void* who=&a;
    Hint h;bool finished=false;
    Check(!HintStep(h,nullptr,0,&finished) && !finished,"nobody near: no hint");
    Check(HintStep(h,who,1000,&finished) && h.who==who,"a commandable squad near: the hint over it");
    Check(HintStep(h,nullptr,5000,&finished) && !finished,"shown for its whole time even when the player walks off");
    Check(!HintStep(h,who,1000+kHintMs,&finished) && finished && h.done,"after kHintMs it ends, for good");
    Check(!HintStep(h,who,1000+kHintMs*3,&finished) && !finished,"never shown again");
}

void Dismounts() {
    using namespace mapcmd;
    // DISMOUNT: the passengers; DISMOUNT ALL with no choice: everyone (as before).
    Check(DismountLeaves(kSeatPassenger,false,0,0) && !DismountLeaves(kSeatGunner,false,0,0) && !DismountLeaves(kSeatDriver,false,0,0),
          "DISMOUNT: the passengers alone");
    Check(DismountLeaves(kSeatDriver,true,0,1) && DismountLeaves(kSeatGunner,true,0,2),"ALL OUT: every rider");
    // All but the driver (#44 「全体下车只留司机」).
    Check(!DismountLeaves(kSeatDriver,true,kDismountKeepDriver,0) && DismountLeaves(kSeatGunner,true,kDismountKeepDriver,0) &&
          DismountLeaves(kSeatPassenger,true,kDismountKeepDriver,3),"all but the driver");
    // One class (#45 「如何让某个角色单独下车」): its riders off, the driver kept even of that class.
    const std::uint32_t wing=kDismountKeepDriver|kDismountWingDiver;
    Check(DismountLeaves(kSeatPassenger,true,wing,1) && DismountLeaves(kSeatGunner,true,wing,1),"Wing Divers out");
    Check(!DismountLeaves(kSeatPassenger,true,wing,0) && !DismountLeaves(kSeatPassenger,true,wing,2) &&
          !DismountLeaves(kSeatPassenger,true,wing,3),"the other classes stay");
    Check(!DismountLeaves(kSeatDriver,true,wing,1),"the driver stays even of that class");
    Check(!DismountLeaves(kSeatPassenger,true,wing,-1),"an unknown class stays");
    // The wire's choices (command protocol v10).
    Check(DismountParamValid(Order::dismountAll,0) && DismountParamValid(Order::dismountAll,kDismountKeepDriver) &&
          DismountParamValid(Order::dismountAll,wing),"the valid choices");
    Check(!DismountParamValid(Order::dismountAll,kDismountWingDiver) && !DismountParamValid(Order::dismountAll,kDismountKeepDriver|0x10u) &&
          !DismountParamValid(Order::dismount,kDismountKeepDriver) && !DismountParamValid(Order::guard,1u),"the invalid ones");
    // The menu: every row a valid DISMOUNT ALL, its entry code apart from the formation menu's.
    for(int i=0;i<mapbtn::kDismountMenuRows;++i) {
        const int e=mapbtn::DismountEntry(mapbtn::kDismountMenu[i]);
        Check(mapbtn::MenuDismount(e) && mapbtn::MenuDismountParam(e)==mapbtn::kDismountMenu[i] &&
              DismountParamValid(Order::dismountAll,mapbtn::kDismountMenu[i]),"a menu row: a valid choice");
    }
    Check(!mapbtn::MenuDismount(mapbtn::MenuEntry(true,7)) && !mapbtn::MenuDismount(mapbtn::MenuEntry(false,3)),
          "the formation menu's entries are not the dismount menu's");
    Check(mapbtn::kDismountMenu[0]==0 && mapbtn::kDismountMenu[1]==kDismountKeepDriver,"everyone first, then all but the driver");
}
}  // namespace

int main() {
    Seats();
    Riding();
    Hints();
    Dismounts();
    std::printf("ride_along_check: %d/%d passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
