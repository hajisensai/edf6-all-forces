// The squads' transports' rules (src/transport_logic.h) checked offline: who rides, where they get off, every phase of a
// ground and a helicopter trip with its timeouts and aborts, the paratroop stick and the canopy's hold on a fall.
#include "../src/transport_logic.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL %s (%g, %g)\n",what,a,b);}
}
using namespace transport;

// A trip run frame by frame against a scripted world: returns the phases it went through (in order) and the acts.
struct World {
    bool squadLive=true,vehicleLive=true,driven=true;
    int alive=4,aboard=0;
    float squadToVehicle=300.0f,vehicleToDrop=900.0f,speed=0.0f;
    bool grounded=false;
};
View ViewOf(const World& w,std::uint64_t ms) {
    View v;
    v.squadLive=w.squadLive;v.vehicleLive=w.vehicleLive;v.driven=w.driven;
    v.aboardAll=w.alive>0 && w.aboard==w.alive;v.aboardAny=w.aboard>0;
    v.squadToVehicle=w.squadToVehicle;v.vehicleToDrop=w.vehicleToDrop;v.speed=w.speed;v.grounded=w.grounded;v.ms=ms;
    return v;
}

void Decisions() {
    const Tuning t{};
    (void)t;
    // 2026-10-10 "站原地冷暴力我": no far-order ride any more -- a paired squad on foot walks, however far.
    Check(!Rides(true,false),"a paired squad on foot walks to any point (no vehicle is sent for it)");
    Check(Rides(true,true),"aboard it rides (gets off) whatever the distance");
    Check(!Rides(false,true),"no pair: no ride");
    // The drop point: standoff short of the point along the way in; an order nearer than that: where the vehicle is.
    const float from[3]={0,0,0},target[3]={0,5,500};float out[3];
    DropPoint(from,target,40.0f,out);
    Check(std::fabs(out[2]-460.0f)<0.01f && out[0]==0.0f && out[1]==5.0f,"the drop point 40 m short of the point, at its height",out[2],out[1]);
    const float close[3]={10,0,20};
    DropPoint(from,close,40.0f,out);
    Check(out[0]==0.0f && out[2]==0.0f,"an order within the standoff: it gets off where the vehicle is",out[0],out[2]);
    // Begin: aboard goes; ready by the squad boards; else it comes.
    World w;View v=ViewOf(w,0);
    Check(Begin(Carrier::ground,v,t)==Phase::pickup,"far from its vehicle: pickup");
    w.squadToVehicle=20.0f;v=ViewOf(w,0);
    Check(Begin(Carrier::ground,v,t)==Phase::boarding,"a stopped vehicle by the squad: boarding");
    Check(Begin(Carrier::heli,v,t)==Phase::pickup,"a helicopter in the air by the squad: it comes down first (pickup)");
    w.grounded=true;v=ViewOf(w,0);
    Check(Begin(Carrier::heli,v,t)==Phase::boarding,"a landed helicopter by the squad: boarding");
    w.aboard=4;v=ViewOf(w,0);
    Check(Begin(Carrier::ground,v,t)==Phase::moving,"all aboard: moving");
}

// A ground trip: pickup (the vehicle comes), boarding, moving, unloading, release; the acts each phase asks.
void GroundTrip() {
    const Tuning t{};
    World w;Trip trip{Phase::pickup,Carrier::ground,{0,0,0},0};
    std::uint64_t ms=0;
    Phase seen[8]{};int n=0;seen[n++]=trip.phase;
    bool askedCome=false,askedBoard=false,askedGo=false,askedUnload=false,released=false;
    for(int frame=0;frame<20000 && !released;++frame,ms+=16) {
        // The world answers the acts: the vehicle closes in, they board, it drives, they get off.
        const Step s=Advance(trip,ViewOf(w,ms),t);
        switch(s.act) {
        case Act::comeTo: askedCome=true;w.speed=8.0f;w.squadToVehicle-=8.0f*0.016f*10.0f;if(w.squadToVehicle<30.0f)w.speed=0.0f;break;
        case Act::board: askedBoard=true;if(frame%30==0 && w.aboard<w.alive)++w.aboard;break;
        case Act::goTo: askedGo=true;w.speed=10.0f;w.vehicleToDrop-=10.0f*0.016f*10.0f;if(w.vehicleToDrop<5.0f)w.speed=0.0f;break;
        case Act::unload: askedUnload=true;if(frame%20==0 && w.aboard>0)--w.aboard;break;
        case Act::release: released=true;break;
        default: break;
        }
        if(s.next!=trip.phase && s.act!=Act::release){trip.phase=s.next;trip.since=ms;if(n<8)seen[n++]=s.next;}
    }
    Check(released,"a ground trip ends (release)");
    Check(n==4 && seen[0]==Phase::pickup && seen[1]==Phase::boarding && seen[2]==Phase::moving && seen[3]==Phase::unloading,
          "pickup -> boarding -> moving -> unloading",n);
    Check(askedCome && askedBoard && askedGo && askedUnload,"every act asked in its phase");
    Check(w.aboard==0,"everybody off at the end",w.aboard);
}

// A helicopter trip: it lands before they board and again at the drop (landing), never unloading in the air.
void HeliTrip() {
    const Tuning t{};
    World w;w.squadToVehicle=200.0f;
    Trip trip{Phase::pickup,Carrier::heli,{0,0,0},0};
    std::uint64_t ms=0;
    bool landedBeforeBoard=false,landedBeforeUnload=false,released=false,unloadAloft=false;
    Phase seen[8]{};int n=0;seen[n++]=trip.phase;
    for(int frame=0;frame<20000 && !released;++frame,ms+=16) {
        const Step s=Advance(trip,ViewOf(w,ms),t);
        switch(s.act) {
        case Act::comeTo: w.squadToVehicle=std::fmax(30.0f,w.squadToVehicle-2.0f);if(w.squadToVehicle<=30.0f)w.grounded=true;break;
        case Act::board: landedBeforeBoard=w.grounded;if(frame%25==0 && w.aboard<w.alive)++w.aboard;break;
        case Act::goTo: w.grounded=false;w.vehicleToDrop=std::fmax(0.0f,w.vehicleToDrop-3.0f);break;
        case Act::land: if(frame%60==0)w.grounded=true;break;
        case Act::unload: unloadAloft=unloadAloft || !w.grounded;landedBeforeUnload=w.grounded;if(frame%20==0 && w.aboard>0)--w.aboard;break;
        case Act::release: released=true;break;
        default: break;
        }
        if(s.next!=trip.phase && s.act!=Act::release){trip.phase=s.next;trip.since=ms;if(n<8)seen[n++]=s.next;}
    }
    Check(released && landedBeforeBoard && landedBeforeUnload && !unloadAloft,"a helicopter boards and unloads on the ground only");
    Check(n==5 && seen[3]==Phase::landing && seen[4]==Phase::unloading,"pickup -> boarding -> moving -> landing -> unloading",n);
}

void Aborts() {
    const Tuning t{};
    World w;Trip trip{Phase::pickup,Carrier::ground,{0,0,0},0};
    w.vehicleLive=false;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::abort,"the vehicle gone: abort (they walk)");
    w=World{};w.squadLive=false;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::release,"the squad gone: the trip just ends");
    w=World{};
    Check(Advance(trip,ViewOf(w,t.pickupMs+1),t).act==Act::abort,"a pickup that never arrives: abort",double(t.pickupMs));
    trip.phase=Phase::boarding;
    Check(Advance(trip,ViewOf(w,t.boardMs+1),t).act==Act::abort,"nobody boarded in time: abort");
    w.aboard=2;
    const Step s=Advance(trip,ViewOf(w,t.boardMs+1),t);
    Check(s.act==Act::goTo && s.next==Phase::moving,"some boarded in time: they go (the rest walk)");
    trip.phase=Phase::moving;w.driven=false;w.speed=5.0f;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::abort,"nobody drives it, still rolling: abort");
    w.speed=0.0f;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::unload,"nobody drives it, at rest with them aboard: they get off");
    w.driven=true;w.aboard=0;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::release,"all off on the way: the trip ends");
    w.aboard=4;w.vehicleToDrop=500.0f;w.speed=0.0f;
    Check(Advance(trip,ViewOf(w,t.moveMs+1),t).act==Act::unload,"a ground route that never arrives: they get off where it stopped");
    trip.carrier=Carrier::heli;w.grounded=false;
    Check(Advance(trip,ViewOf(w,t.moveMs+1),t).next==Phase::landing,"a helicopter that never arrives: lands where it is");
    trip.phase=Phase::landing;
    Check(Advance(trip,ViewOf(w,t.landMs+1),t).act==Act::unload,"a landing that never touches: unload (the ground is that near)");
    trip.phase=Phase::unloading;
    Check(Advance(trip,ViewOf(w,t.unloadMs+1),t).act==Act::release,"an unload that never empties: release");
    trip.phase=Phase::none;
    Check(Advance(trip,ViewOf(w,10),t).act==Act::none,"no trip: nothing asked");
}

void Paradrop() {
    // Where the stick starts (2026-10-10: anywhere within 400 m, and 288 m abeam put the stick 250-400 m off the point):
    // its lead short of the point along the track (the drift its canopies bleed off, half the stick), on the track.
    const float speed=98.0f,lead=StickLead(speed,12,0.6f);
    Check(std::fabs(lead-(speed/0.6f+5.5f*speed*0.35f))<0.5f,"the lead: the drift plus half the stick",lead);
    Check(!StickStarts(lead+20.0f,0.0f,speed,12,0.6f),"not before its lead");
    Check(StickStarts(lead-1.0f,0.0f,speed,12,0.6f),"at its lead, on the track: the stick starts");
    Check(!StickStarts(lead-1.0f,kStickAcross+10.0f,speed,12,0.6f),"not with the point off its track (288 m abeam: none)");
    Check(!StickStarts(lead-speed*kStickWindowS-5.0f,0.0f,speed,12,0.6f),"not once past its window: the next pass");
    Check(!StickStarts(lead-1.0f,0.0f,0.5f,12,0.6f),"not standing still");
    // Landing: the stick's middle jumper (the 6.5th of 12) carried its drift on from where it jumped: over the point.
    const float middle=-(lead-1.0f)+5.5f*speed*0.35f+kJumpInherit*speed/0.6f;
    Check(std::fabs(middle)<5.0f,"the stick lands centred on the point",middle);
    Check(JumpNow(1,12,1000+kJumpEveryMs,1000) && !JumpNow(1,12,1000+kJumpEveryMs-1,1000),"one at a time, the stick goes on");
    Check(!JumpNow(12,12,99999,0),"all out: no more");
    // The canopy: the fall held to the sink, the drift bled; a climb (a Wing Diver's boost) untouched.
    float vel[3]={30.0f,-40.0f,0.0f};
    for(int i=0;i<60;++i)ChuteStep(vel,6.0f,0.6f);
    Check(vel[1]==-6.0f,"the fall held at the sink",vel[1]);
    Check(vel[0]<30.0f*0.6f && vel[0]>30.0f*0.5f,"the drift bled about 0.6 of itself a second",vel[0]);
    float up[3]={10.0f,5.0f,0.0f};ChuteStep(up,6.0f,0.6f);
    Check(up[0]==10.0f && up[1]==5.0f,"rising: nothing held");
    Check(Landed(true,50.0f,1.5f) && Landed(false,1.0f,1.5f) && !Landed(false,10.0f,1.5f) && !Landed(false,-1.0f,1.5f),
          "down: standing, or within 1.5 m of the ground (no ground seen: not down)");
}

// 2026-11-21 「直升机/固定翼 飞翼喷射一下落，剑兵硬着陆，突击索降，空袭降落伞」: how each class comes down, and a helicopter's
// delivery by hovering over the point (no landing).
void Descents() {
    Check(DescentOf(kWingDiver,false)==Descent::jets && DescentOf(kWingDiver,true)==Descent::jets,"a Wing Diver on her jets from either");
    Check(DescentOf(kFencer,false)==Descent::hardLanding && DescentOf(kFencer,true)==Descent::hardLanding,"a Fencer: a hard landing from either");
    Check(DescentOf(kRanger,false)==Descent::canopy && DescentOf(kAirRaider,false)==Descent::canopy,"Ranger / Air Raider: a canopy from a plane");
    Check(DescentOf(kRanger,true)==Descent::rope && DescentOf(kAirRaider,true)==Descent::rope,"...a rope from a helicopter");
    Check(DescentOf(-1,false)==Descent::canopy && DescentOf(-1,true)==Descent::rope,"a class not known: as a Ranger");
    // The jets: a burst out of the door, free fall high up, bursts near the ground with hysteresis.
    Check(JetBurn(false,-50.0f,140.0f,0) && JetBurn(false,0.0f,140.0f,kJetKickMs-1),"the burst out of the door");
    Check(!JetBurn(false,-20.0f,140.0f,kJetKickMs),"then falling free high up");
    Check(JetBurn(false,-45.0f,100.0f,5000) && !JetBurn(false,-45.0f,140.0f,5000),"she burns once the ground is within her stopping distance");
    Check(JetBurn(false,-kJetSinkMost-1.0f,kJetMargin-1.0f,5000) && !JetBurn(false,-kJetSinkMost+1.0f,kJetMargin-1.0f,5000),
          "low down: she burns once falling past the most");
    Check(JetBurn(true,-kJetSinkEase-1.0f,20.0f,5000) && !JetBurn(true,-kJetSinkEase+1.0f,20.0f,5000),"...and lets go once slower than the ease");
    Check(JetBurn(false,-20.0f,-1.0f,5000),"the ground not known: by her fall alone");
    // A simulated fall from 150 m at 9.8 m/s2 with her jets thrusting 20 m/s2 up while on: down at a walking pace.
    {
        float y=150.0f,vy=0.0f;bool burn=false;float most=0.0f;int bursts=0;
        for(int f=0;f<6000 && y>0.0f;++f) {
            const bool b=JetBurn(burn,vy,y,static_cast<std::uint64_t>(f)*16u);
            bursts+=b && !burn;burn=b;
            vy+=(-9.8f+(burn ? 20.0f : 0.0f))*0.016f;y+=vy*0.016f;
            if(y<5.0f && -vy>most)most=-vy;
        }
        Check(y<=0.0f && most<kJetSinkMost+3.0f,"braked: she lands no faster than about the most",most);
        Check(bursts>=3,"in bursts, not one long burn",bursts);
    }
    // The Fencer: no canopy, the fall held at kHardFallMost, the plane's way bled to the floor.
    float vel[3]={98.0f,-50.0f,0.0f};
    for(int i=0;i<600;++i)FreeFallStep(vel,0.6f);
    Check(vel[1]==-kHardFallMost,"a Fencer falls at up to the hard-fall most",vel[1]);
    Check(std::fabs(vel[0]-kDriftFloor)<0.01f,"its way bled down to the floor, not past it",vel[0]);
    float slow[3]={3.0f,-5.0f,0.0f};FreeFallStep(slow,0.6f);
    Check(slow[0]==3.0f && slow[1]==-5.0f,"slow already: untouched");
    // The rope: down at the sink, drawn onto it, no faster than the pull.
    const float pos[3]={2.0f,10.0f,0.0f},anchor[3]={0.0f,18.0f,0.0f},on[3]={0.0f,10.0f,0.0f};
    float rv[3]={40.0f,-20.0f,7.0f};RopeStep(rv,pos,anchor);
    Check(rv[1]==-kRopeSink && rv[0]<0.0f && std::fabs(std::hypot(rv[0],rv[2])-kRopePull)<0.01f && rv[2]==0.0f,
          "down the rope at the sink, drawn onto it at most the pull",rv[0],rv[1]);
    RopeStep(rv,on,anchor);
    Check(rv[0]==0.0f && rv[2]==0.0f && rv[1]==-kRopeSink,"on it: straight down");
    Check(kRopeLength>=kRopeHeight+kHoverBand,"the rope reaches the ground from the highest hover allowed",kRopeLength,kRopeHeight+kHoverBand);
    // The hover: over the point, still, at about the rope height (never all but on the ground).
    Check(Hovering(3.0f,1.0f,kRopeHeight) && !Hovering(kHoverNear+1.0f,1.0f,kRopeHeight) && !Hovering(3.0f,kHoverStill+1.0f,kRopeHeight),
          "held over the point, still");
    Check(!Hovering(3.0f,1.0f,kHoverLow-1.0f) && !Hovering(3.0f,1.0f,kRopeHeight+kHoverBand+1.0f) && !Hovering(3.0f,1.0f,-1.0f),
          "...at the rope height (too low, too high or the ground not known: no)");
}

void HoverTrip() {
    const Tuning t{};
    Trip trip;trip.carrier=Carrier::heli;trip.hover=true;trip.phase=Phase::moving;trip.since=0;
    World w;w.aboard=4;w.vehicleToDrop=20.0f;w.speed=10.0f;
    View v=ViewOf(w,1000);
    Step st=Advance(trip,v,t);
    Check(st.next==Phase::landing && st.act==Act::hover,"near the drop: it hovers (no landing)");
    trip.phase=Phase::landing;trip.since=1000;
    st=Advance(trip,ViewOf(w,2000),t);
    Check(st.next==Phase::landing && st.act==Act::hover,"not held yet: still coming into the hover");
    v=ViewOf(w,3000);v.hovering=true;
    st=Advance(trip,v,t);
    Check(st.next==Phase::unloading && st.act==Act::rope,"held: down the ropes");
    trip.phase=Phase::unloading;trip.since=3000;
    v.ms=4000;st=Advance(trip,v,t);
    Check(st.next==Phase::unloading && st.act==Act::rope,"one after another while any are aboard");
    w.aboard=0;v=ViewOf(w,5000);
    st=Advance(trip,v,t);
    Check(st.next==Phase::none && st.act==Act::release,"all down: over (it never touched the ground)");
    w.aboard=2;v=ViewOf(w,3000+t.ropeMs+1);
    st=Advance(trip,v,t);
    Check(st.next==Phase::landing && st.act==Act::settle,"not down in time: it lands the rest");
    trip.phase=Phase::landing;trip.since=1000;
    st=Advance(trip,ViewOf(w,1000+t.landMs+1),t);
    Check(st.next==Phase::landing && st.act==Act::settle,"no hover held in time: it lands them");
    trip.hover=false;
    w.grounded=true;st=Advance(trip,ViewOf(w,1000),t);
    Check(st.next==Phase::unloading && st.act==Act::unload,"settled: an ordinary landing's unload");
}
}  // namespace

int main() {
    Decisions();GroundTrip();HeliTrip();Aborts();Paradrop();Descents();HoverTrip();
    std::printf("transport_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
