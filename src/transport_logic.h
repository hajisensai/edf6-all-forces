// The squads' transports, their pure part (src/transport.cpp drives it; tools/transport_check.cpp checks it offline).
// The user, 2026-10-09: "飞机和直升机应该也有运输机。卡车之类的运输载具改成断剑那种操作方式", "飞机就空降，飞翼的话可以直接
// 飞下来不用降落伞" -- the way Broken Arrow handles infantry and its transport:
//  - a squad is paired with one vehicle (its transport: a truck, an APC, a transport helicopter), by boarding it (the map's
//    BOARD), by a crewed transport's delivery or a helicopter assault (support_dispatch.cpp);
//  - a point order (move / attack-move / guard) for a paired squad aboard its vehicle goes by it: the vehicle drives /
//    flies to a point short of the order's (the standoff), stops (lands), the squad gets off and carries out the order on
//    foot. A squad on foot always walks (2026-10-10, the user: the far-order ride, TransportAutoRange, is gone -- a far
//    move left the squad standing while it waited for a vehicle to come: "站原地冷暴力我");
//  - a support delivery (a crewed truck / APC, a transport helicopter) goes back once its passengers are off (the same
//    day: "载具送过来以后就会直接回去"): it is not left as their transport;
//  - its transport is withdrawn on order (a support vehicle leaves the field and is removed);
//  - a paratroop drop: a transport plane flies over the point and its passengers jump one after another (a stick);
//  - a helicopter's support delivery (2026-11-21, the user: 「直升机/固定翼 飞翼喷射一下落，剑兵硬着陆，突击索降，空袭降落伞
//    进场动画」) does not land: it hovers kRopeHeight over its drop point and they go down from it one after another;
//  - how each comes down, by class (DescentOf): a Wing Diver on her own jets (out of the door with a burst, falling, and
//    braking with bursts near the ground: JetBurn), a Fencer falling straight down onto his feet (a hard landing:
//    FreeFallStep), the Ranger and the Air Raider under a parachute from a plane and down a rope from a helicopter.
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
    float groundReach=40.0f;       // m: a ground vehicle this near the squad's top: board it
    float heliReach=45.0f;         // m: a landed helicopter this near: board it
    float groundStandoff=40.0f;    // m short of the order's point the vehicle stops (no closer than the point itself)
    float heliStandoff=60.0f;
    float groundArrive=8.0f;       // m: the vehicle this near the drop point is there
    float heliArrive=30.0f;        // m (level): a helicopter this near starts down
    float stopped=0.8f;            // m/s: a ground vehicle slower than this has stopped
    // ropeMs: a hovering helicopter's whole load down its ropes within this (else it lands them: Act::settle).
    std::uint64_t pickupMs=90000,boardMs=25000,moveMs=240000,landMs=30000,unloadMs=10000,ropeMs=20000;
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
    bool hovering=false;           // a helicopter held over the drop point at its rope height (Hovering)
    bool ropeOccupied=false;       // someone is still descending from this helicopter
    std::uint64_t ms=0;
};

// What the trip asks of the vehicle and the squad this frame.
//  comeTo: the vehicle to the squad (a driverless one: the squad walks to it); board: the members to their seats, the
//  vehicle holds (a helicopter stays down); goTo: the vehicle to the drop point; land: the helicopter down at the drop
//  point; unload: the members off, the vehicle holds; release: the trip is over, the squad takes its order on foot, the
//  vehicle waits where it is; abort: the trip cannot go on (the vehicle lost, nobody to drive it, too long): the same.
//  A hovering trip (Trip::hover, a helicopter's support delivery): hover: the helicopter held kRopeHeight over the drop
//  point; rope: they go down from it one after another (transport.cpp, by class: DescentOf); settle: the hover did not
//  come about or they did not get down in time: the trip lands them as any other (its hover off, the landing phase anew).
enum class Act : std::uint8_t { none, comeTo, board, goTo, land, unload, release, abort, hover, rope, settle };
const char* const kActNames[]={"none","come to","board","go to","land","unload","release","abort","hover","rope","settle"};

struct Trip { Phase phase=Phase::none; Carrier carrier=Carrier::ground; float drop[3]{}; std::uint64_t since=0; bool hover=false; };
struct Step { Phase next; Act act; };

inline float Level(const float* a,const float* b) noexcept { return std::hypot(a[0]-b[0],a[2]-b[2]); }

// Whether a point order goes by the squad's transport: only when it is paired and aboard it now (a squad on foot walks,
// however far: no vehicle is sent for it).
inline bool Rides(bool paired,bool aboardAny) noexcept {
    return paired && aboardAny;
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
            if(v.vehicleToDrop<=t.heliArrive || age>t.moveMs)return {Phase::landing,trip.hover ? Act::hover : Act::land};
            return {Phase::moving,Act::goTo};
        }
        if((v.vehicleToDrop<=t.groundArrive || age>t.moveMs) && v.speed<=t.stopped)return {Phase::unloading,Act::unload};
        return {Phase::moving,Act::goTo};
    case Phase::landing:
        if(trip.hover) {
            if(v.hovering)return {Phase::unloading,Act::rope};
            if(age>t.landMs)return {Phase::landing,Act::settle};
            return {Phase::landing,Act::hover};
        }
        if(v.grounded || age>t.landMs)return {Phase::unloading,Act::unload};
        return {Phase::landing,Act::land};
    case Phase::unloading:
        if(trip.hover && !v.aboardAny && v.ropeOccupied && age<=t.ropeMs)return {Phase::unloading,Act::rope};
        if(!v.aboardAny)return {Phase::none,Act::release};
        if(trip.hover)return age>t.ropeMs ? Step{Phase::landing,Act::settle} : Step{Phase::unloading,Act::rope};
        if(age>t.unloadMs)return {Phase::none,Act::release};
        return {Phase::unloading,Act::unload};
    default: return {Phase::none,Act::none};
    }
}

// --- The paratroop drop ---
// The stick: the plane's passengers jump one after another, kJumpEveryMs apart, from its start (StickStarts) until all
// are out, whatever the distance by then.
inline constexpr std::uint64_t kJumpEveryMs=350;
inline bool JumpNow(int jumped,int total,std::uint64_t ms,std::uint64_t lastJump) noexcept {
    return jumped<total && ms>=lastJump+kJumpEveryMs;
}
// Where the stick starts (2026-10-10: it began anywhere within 400 m of the point, so a plane going by 288 m abeam put its
// twelve down 250-400 m off it). The plane flies its line over the point (ferry_line.h); the stick starts `StickLead` m
// short of it along its track: the jumpers' drift (each leaves with kJumpInherit of the plane's way and its canopy bleeds
// it off at `bleed` a second: ChuteStep, speed / bleed of way in all) and half the stick's length (total - 1 jumps
// kJumpEveryMs apart at `speed`), so the stick lands centred on the point. Not with the point more than kStickAcross off its
// track, nor once it is more than kStickWindowS of flight past the start: that pass missed, the next one comes over
// (ferry_line.h turns it round onto the same line). `along`: m the point lies ahead along its track (negative: behind);
// `across`: m off the track.
inline constexpr float kJumpInherit=1.0f,kStickAcross=80.0f,kStickWindowS=1.5f;
inline float StickLead(float speed,int total,float bleed) noexcept {
    const float drift=bleed>0.01f ? kJumpInherit*speed/bleed : 0.0f;
    const float stick=static_cast<float>(total>1 ? total-1 : 0)*speed*static_cast<float>(kJumpEveryMs)*0.001f;
    return drift+0.5f*stick;
}
inline bool StickStarts(float along,float across,float speed,int total,float bleed) noexcept {
    if(!(speed>1.0f) || std::fabs(across)>kStickAcross)return false;
    const float lead=StickLead(speed,total,bleed);
    return along<=lead && along>=lead-speed*kStickWindowS;
}
// The canopy's hold on a jumper's velocity (as the player's parachute: playerjet.cpp EjectTick): the fall held to `sink`
// m/s, the drift bled `bleed` of itself a second (a 60 Hz frame). A Wing Diver has none (she flies down herself).
inline void ChuteStep(float* vel,float sink,float bleed) noexcept {
    if(vel[1]<-sink)vel[1]=-sink;
    if(vel[1]<0.0f){const float keep=1.0f-bleed/60.0f;vel[0]*=keep;vel[2]*=keep;}
}
// A jumper is down: standing (the walk's support), or this near the ground.
inline bool Landed(bool support,float clearance,float close) noexcept { return support || (clearance>=0.0f && clearance<close); }

// --- How each comes down (2026-11-21: 「飞翼喷射一下落，剑兵硬着陆，突击索降，空袭降落伞」) ---
// The soldier's class as npcai.cpp kSoldiers orders them (NpcSoldierClass): 0 Ranger, 1 Wing Diver, 2 Fencer, 3 Air Raider.
enum class Descent : std::uint8_t { canopy, jets, hardLanding, rope };
const char* const kDescentNames[]={"parachute","jets","hard landing","rope"};
inline constexpr int kRanger=0,kWingDiver=1,kFencer=2,kAirRaider=3;
// A Wing Diver on her jets and a Fencer onto his feet from either; the others (and a class not known) under a canopy
// from a plane, down a rope from a hovering helicopter.
inline Descent DescentOf(int soldierClass,bool fromHeli) noexcept {
    if(soldierClass==kWingDiver)return Descent::jets;
    if(soldierClass==kFencer)return Descent::hardLanding;
    return fromHeli ? Descent::rope : Descent::canopy;
}
// Every jumper out of a plane carries its way (kJumpInherit); it is bled off as a canopy bleeds it (ChuteStep's
// `bleed`) down to kDriftFloor m/s level whatever its descent, so the stick's lead (StickLead) holds for all of them. A
// Wing Diver's own flight below that is hers.
inline constexpr float kDriftFloor=6.0f;
inline void DriftBleed(float* vel,float bleed) noexcept {
    const float level=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    if(!(level>kDriftFloor))return;
    float keep=1.0f-bleed/60.0f;
    if(level*keep<kDriftFloor)keep=kDriftFloor/level;
    vel[0]*=keep;vel[2]*=keep;
}
// A Fencer's fall (no canopy, no jets: he lands on his feet at speed, as a player falling off a tower does), held to
// kHardFallMost m/s: the walk's controller takes a frame's step of that onto the ground (0.5 m at 60 Hz).
inline constexpr float kHardFallMost=30.0f;
inline void FreeFallStep(float* vel,float bleed) noexcept {
    if(vel[1]<-kHardFallMost)vel[1]=-kHardFallMost;
    DriftBleed(vel,bleed);
}
// A Wing Diver's jets (her own flight, the jump held: docs/insertion-re.md §2): a burst out of the door
// (kJetKickMs), then falling free until the ground comes within what her jets need to take her fall down to kJetSinkMost
// (its stopping distance at an assumed net kJetDecel m/s2, plus kJetMargin); then she burns while falling faster than
// kJetSinkEase and lets go below it, so she comes down in bursts (she never hovers: a burst ends once slow). Low down
// (within kJetMargin) she burns at anything past kJetSinkMost. The ground not known under her (clearance < 0): by her
// fall alone.
inline constexpr std::uint64_t kJetKickMs=450;
inline constexpr float kJetDecel=8.0f,kJetMargin=12.0f,kJetSinkMost=9.0f,kJetSinkEase=3.0f;
inline bool JetBurn(bool burning,float vy,float clearance,std::uint64_t sinceOut) noexcept {
    if(sinceOut<kJetKickMs)return true;
    if(burning)return vy<-kJetSinkEase;
    if(!(vy<-kJetSinkMost))return false;
    if(clearance<0.0f)return true;
    const float over=-vy-kJetSinkMost;   // what the jets must take off her fall
    const float stop=(vy*vy-kJetSinkMost*kJetSinkMost)/(2.0f*kJetDecel);
    return over>0.0f && clearance<=stop+kJetMargin;
}
// The rope (a helicopter's): down it at kRopeSink m/s, drawn onto it level (`anchor`: the rope's x / z) at kRopeGain a
// second of the gap, no faster than kRopePull m/s. The helicopter holds kRopeHeight over its drop point; the rope hangs
// kRopeLength from its door (src/playerjet.cpp RopeMake, pylib/chute_model.py ROPE_LENGTH).
inline constexpr float kRopeSink=5.0f,kRopeGain=4.0f,kRopePull=3.0f,kRopeHeight=18.0f,kRopeLength=28.0f;
inline void RopeStep(float* vel,const float* pos,const float* anchor) noexcept {
    float dx=(anchor[0]-pos[0])*kRopeGain,dz=(anchor[2]-pos[2])*kRopeGain;
    const float l=std::sqrt(dx*dx+dz*dz);
    if(l>kRopePull){dx*=kRopePull/l;dz*=kRopePull/l;}
    vel[0]=dx;vel[1]=-kRopeSink;vel[2]=dz;
}
// The helicopter is held for the ropes: within kHoverNear m (level) of the drop point, slower than kHoverStill m/s, and
// its height over the ground under it (clearance; < 0 not known) within kHoverBand of kRopeHeight, never below
// kHoverLow (nobody goes down a rope from a helicopter all but on the ground: it lands them, Act::settle).
inline constexpr float kHoverNear=8.0f,kHoverStill=3.0f,kHoverBand=8.0f,kHoverLow=6.0f;
inline bool Hovering(float level,float speed,float clearance) noexcept {
    return level<=kHoverNear && speed<=kHoverStill && clearance>=kHoverLow && clearance<=kRopeHeight+kHoverBand;
}
// The ropes' one after another: kRopeEveryMs apart.
inline constexpr std::uint64_t kRopeEveryMs=600;
}  // namespace transport
