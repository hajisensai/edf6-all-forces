// The squads' transports, their pure part (src/transport.cpp drives it; tools/transport_check.cpp checks it offline).
// The user, 2026-10-09: "飞机和直升机应该也有运输机。卡车之类的运输载具改成断剑那种操作方式", "飞机就空降，飞翼的话可以直接
// 飞下来不用降落伞" -- the way Broken Arrow handles infantry and its transport:
//  - a squad is paired with one vehicle (its transport: a truck, an APC, a transport helicopter), by boarding it (the map's
//    BOARD), by a crewed transport's delivery or a helicopter assault (support_dispatch.cpp);
//  - a point order (move / attack-move / guard) for a paired squad farther than the auto range goes by its transport: the
//    vehicle comes for the squad (or the squad walks to an empty one), the squad boards, the vehicle drives / flies to a
//    point short of the order's (the standoff), stops (lands), the squad gets off and carries out the order on foot, the
//    vehicle waits there. Nearer, the squad walks (and one aboard gets off first);
//  - its transport is withdrawn on order (a support vehicle leaves the field and is removed);
//  - a paratroop drop: a transport plane flies over the point and its passengers jump one after another (a stick); every
//    class but the Wing Diver comes down under a parachute, a Wing Diver flies down on her own.
// One trip a pair at a time; its phases below (Advance). No game, no Windows.
#pragma once
#include <cmath>
#include <cstdint>

namespace transport {
enum class Carrier : std::uint8_t { ground, heli };
// none: no trip (the pair waits). pickup: the vehicle comes to the squad (or the squad walks to an empty one). boarding:
// the members walk to their seats. moving: aboard, the vehicle on its way to the drop point. landing: a helicopter over
// the drop point coming down. unloading: the members getting off.
enum class Phase : std::uint8_t { none, pickup, boarding, moving, landing, unloading };
const char* const kPhaseNames[]={"none","pickup","boarding","moving","landing","unloading"};

struct Tuning {
    float autoRange=200.0f;        // m: a point order farther than this from the squad goes by its transport (ini)
    float groundReach=40.0f;       // m: a ground vehicle this near the squad's top: board it
    float heliReach=45.0f;         // m: a landed helicopter this near: board it
    float groundStandoff=40.0f;    // m short of the order's point the vehicle stops (no closer than the point itself)
    float heliStandoff=60.0f;
    float groundArrive=8.0f;       // m: the vehicle this near the drop point is there
    float heliArrive=30.0f;        // m (level): a helicopter this near starts down
    float stopped=0.8f;            // m/s: a ground vehicle slower than this has stopped
    std::uint64_t pickupMs=90000,boardMs=25000,moveMs=240000,landMs=30000,unloadMs=10000;
};

// What a trip sees this frame (transport.cpp reads it from the game).
struct View {
    bool squadLive=true,vehicleLive=true;
    bool driven=true;              // an NPC flies / drives the vehicle now (pickup: the vehicle can come; moving: it can go)
    bool aboardAll=false,aboardAny=false;   // the squad's live members seated in it: all / any
    float squadToVehicle=0.0f;     // m (level) from the squad's top to the vehicle
    float vehicleToDrop=0.0f;      // m (level) from the vehicle to the drop point
    float speed=0.0f;              // m/s, the vehicle's
    bool grounded=true;            // a helicopter on the ground (a ground vehicle always)
    std::uint64_t ms=0;
};

// What the trip asks of the vehicle and the squad this frame.
//  comeTo: the vehicle to the squad (a driverless one: the squad walks to it); board: the members to their seats, the
//  vehicle holds (a helicopter stays down); goTo: the vehicle to the drop point; land: the helicopter down at the drop
//  point; unload: the members off, the vehicle holds; release: the trip is over, the squad takes its order on foot, the
//  vehicle waits where it is; abort: the trip cannot go on (the vehicle lost, nobody to drive it, too long): the same.
enum class Act : std::uint8_t { none, comeTo, board, goTo, land, unload, release, abort };
const char* const kActNames[]={"none","come to","board","go to","land","unload","release","abort"};

struct Trip { Phase phase=Phase::none; Carrier carrier=Carrier::ground; float drop[3]{}; std::uint64_t since=0; };
struct Step { Phase next; Act act; };

inline float Level(const float* a,const float* b) noexcept { return std::hypot(a[0]-b[0],a[2]-b[2]); }

// Whether a point order `distance` m from the squad's top goes by its transport: the squad is paired and aboard it now,
// or the point is past the auto range.
inline bool Rides(bool paired,bool aboardAny,float distance,float autoRange) noexcept {
    return paired && (aboardAny || distance>autoRange);
}

// The drop point for an order to `target` with the vehicle at `from`: `standoff` m short of it along the way in, never
// past the vehicle (an order nearer than the standoff: where the vehicle is). Its height is the target's.
inline void DropPoint(const float* from,const float* target,float standoff,float* out) noexcept {
    const float dx=target[0]-from[0],dz=target[2]-from[2],d=std::hypot(dx,dz);
    if(!(d>standoff)){out[0]=from[0];out[1]=target[1];out[2]=from[2];return;}
    out[0]=target[0]-dx/d*standoff;out[1]=target[1];out[2]=target[2]-dz/d*standoff;
}

inline float ReachOf(Carrier c,const Tuning& t) noexcept { return c==Carrier::heli ? t.heliReach : t.groundReach; }
// The vehicle is ready for boarding: near the squad, stopped (a helicopter: down).
inline bool Ready(Carrier c,const View& v,const Tuning& t) noexcept {
    return v.squadToVehicle<=ReachOf(c,t) && (c==Carrier::heli ? v.grounded : v.speed<=t.stopped);
}

// The phase a new trip starts in: aboard (all of them) it goes; ready by the squad, they board; else it comes for them.
inline Phase Begin(Carrier c,const View& v,const Tuning& t) noexcept {
    if(v.aboardAll)return Phase::moving;
    if(Ready(c,v,t) || (v.aboardAny && v.squadToVehicle<=ReachOf(c,t)))return Phase::boarding;
    return Phase::pickup;
}

// A frame of trip `trip` (its phase begun at trip.since): the next phase and the act of this frame.
inline Step Advance(const Trip& trip,const View& v,const Tuning& t) noexcept {
    if(trip.phase==Phase::none)return {Phase::none,Act::none};
    if(!v.squadLive)return {Phase::none,Act::release};
    if(!v.vehicleLive)return {Phase::none,Act::abort};
    const std::uint64_t age=v.ms>=trip.since ? v.ms-trip.since : 0;
    const bool heli=trip.carrier==Carrier::heli;
    switch(trip.phase) {
    case Phase::pickup:
        if(Ready(trip.carrier,v,t))return {Phase::boarding,Act::board};
        if(age>t.pickupMs)return {Phase::none,Act::abort};
        return {Phase::pickup,Act::comeTo};
    case Phase::boarding:
        if(v.aboardAll)return {Phase::moving,Act::goTo};
        if(age>t.boardMs)return v.aboardAny ? Step{Phase::moving,Act::goTo} : Step{Phase::none,Act::abort};
        return {Phase::boarding,Act::board};
    case Phase::moving:
        if(!v.driven)return v.aboardAny && (heli ? v.grounded : v.speed<=t.stopped) ? Step{Phase::unloading,Act::unload}
                                                                                     : Step{Phase::none,Act::abort};
        if(!v.aboardAny)return {Phase::none,Act::release};   // everyone got off on the way (killed, thrown out)
        if(heli) {
            if(v.vehicleToDrop<=t.heliArrive || age>t.moveMs)return {Phase::landing,Act::land};
            return {Phase::moving,Act::goTo};
        }
        if((v.vehicleToDrop<=t.groundArrive || age>t.moveMs) && v.speed<=t.stopped)return {Phase::unloading,Act::unload};
        return {Phase::moving,Act::goTo};
    case Phase::landing:
        if(v.grounded || age>t.landMs)return {Phase::unloading,Act::unload};
        return {Phase::landing,Act::land};
    case Phase::unloading:
        if(!v.aboardAny || age>t.unloadMs)return {Phase::none,Act::release};
        return {Phase::unloading,Act::unload};
    default: return {Phase::none,Act::none};
    }
}

// --- The paratroop drop ---
// The stick: the plane's passengers jump one after another, kJumpEveryMs apart, from when it comes within `radius` m
// (level) of the drop point until all are out; it never starts once it is past the point by more than `radius` (a plane
// that missed it goes round: transport.cpp keeps it on its way back over).
inline constexpr std::uint64_t kJumpEveryMs=350;
inline bool JumpNow(float distance,float radius,int jumped,int total,std::uint64_t ms,std::uint64_t lastJump) noexcept {
    if(jumped>=total)return false;
    if(jumped==0)return distance<=radius;
    return ms>=lastJump+kJumpEveryMs;   // the stick, once begun, goes on whatever the distance
}
// The canopy's hold on a jumper's velocity (as the player's parachute: playerjet.cpp EjectTick): the fall held to `sink`
// m/s, the drift bled `bleed` of itself a second (a 60 Hz frame). A Wing Diver has none (she flies down herself).
inline void ChuteStep(float* vel,float sink,float bleed) noexcept {
    if(vel[1]<-sink)vel[1]=-sink;
    if(vel[1]<0.0f){const float keep=1.0f-bleed/60.0f;vel[0]*=keep;vel[2]*=keep;}
}
// A jumper is down: standing (the walk's support), or this near the ground.
inline bool Landed(bool support,float clearance,float near) noexcept { return support || (clearance>=0.0f && clearance<near); }
}  // namespace transport
