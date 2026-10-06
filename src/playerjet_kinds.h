// The plugin's NPC aircraft the player can board (2026-10-05, the user: "every one of our planes, and each really
// good to fly"): which they are, how each flies, what it fights with besides its guns and stores, and its flight
// performance for the player's flight model (playerjet.cpp Kind), derived here from the very row the NPC flies by
// (jet_internal.h kKinds), so the player gets the performance the NPC has. playerjet.cpp turns each Perf into its
// Kind; tools/pjet_kinds.cpp prints and checks the table offline.
//
// The derivation (a wing), from the NPC row n (m/s, m/s^2, g, rad/s):
//   top     = n.attack + kTopOver        the NPC's attack speed is what it flies flat out (jet_flight.cpp JetSteer
//                                        holds it under 1.3 x attack); the player's throttle's full is a little over it
//   maxG    = n.maxG (+1 for a dogfighter, Prefer::air: the player's fighter pulls 6 to the NPC fighter's 5)
//   corner  = kCornerShare x n.cruise    the speed from which the wing gives its maxG
//   minAir  = the larger of kIdleShare x n.minSpeed and kIdleOver x the speed the wing holds 1 g at
//             (corner / sqrt(maxG)): at idle in level flight it is never already sinking
//   rotate  = minAir + kRotateOver, thrust = n.thrust + 1 (at least kLeastThrust), brake = kBrakeShare x n.brake,
//   roll    = n.roll + 0.2 (at least kLeastRoll: the body keeps up with the attitude), landMax = the larger of
//             top / 2 and rotate + kLandOver
// Applied to the NPC fighter and strike jet these come within 10% of the hand-tuned player fighter and strike jet
// (playerjet.cpp checks that at compile time: kKinds[0] / kKinds[1]).
// A rotor craft (the carriers, the blast and doll drones) flies its NPC row as it is (jet_flight.cpp Hover, its
// lean): top = n.cruise, thrust = n.thrust, brake = n.brake, roll = n.roll; no wing speeds (minAir = rotate = 0);
// landMax is the fastest it may touch down (any way) without damage.
// The bombers the airstrikes take over (BOMBER401, BOMBER501_2) are the strike role in a bomber's model: their row is
// the stock bomber's own (it flies 3 m a frame, 180 m/s: kStockBomber), heavy and slow to turn.
// Not boardable: the enemy's jets (BodyRow::hostile: the enemy fighter, the Primer fighter: the stock seat check
// lets nobody into the other side's vehicle, and a jet the enemy flies is no ride for the player) and the helis
// (stock helicopters, flown by the stock heli code already).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include "jet_internal.h"

namespace crew {
namespace pjet {
using jet::Body;

// How it flies under the player: a wing (playerjet.cpp Air / Ground) or a rotor craft (Hover).
enum class Airframe { wing, rotor };
// Its own weapon besides its guns and stores, offered as one more store in the cockpit's cycle (playerjet_board.inc
// SpecialStore): none, the gunship's shells, the carrier's drones, the blast / doll drone's charge. (A bomber's bay
// is its jet's, not its kind's: any jet that still has one offers it.)
enum class Arm { none, shells, drones, charge };

// The player's flight model's numbers (playerjet.cpp Kind, the same fields and units).
struct Perf {
    const char* name;
    int mark;
    float minAir,rotate,top,thrust,brake,maxG,corner,roll,landMax,ram;
};

struct Boardable {
    Body body;
    Airframe frame;
    Arm arm;
    Perf perf;
};

constexpr float kTopOver=25.0f,kCornerShare=0.72f,kIdleShare=0.6f,kIdleOver=1.05f,kRotateOver=10.0f,kLeastThrust=6.0f,
                kBrakeShare=1.65f,kLeastRoll=1.0f,kLandOver=25.0f,kRotorTouch=6.0f;

constexpr float Root(float x) noexcept {
    float r=x>1.0f ? x : 1.0f;
    for(int i=0;i<24;++i)r=0.5f*(r+x/r);
    return r;
}
constexpr float Most(float a,float b) noexcept { return a>b ? a : b; }

// A wing's Perf from its NPC flight row (see the file comment). `ram`: m, its ram's blast radius (half its size).
constexpr Perf Wing(const char* name,float mark,const jet::Kind& n,float ram) noexcept {
    const float top=n.attack+kTopOver;
    const float maxG=n.maxG+(n.prefer==jet::Prefer::air ? 1.0f : 0.0f);
    const float corner=kCornerShare*n.cruise;
    const float minAir=Most(kIdleShare*n.minSpeed,kIdleOver*corner/Root(maxG));
    const float rotate=minAir+kRotateOver;
    return Perf{name,static_cast<int>(mark),minAir,rotate,top,Most(n.thrust+1.0f,kLeastThrust),kBrakeShare*n.brake,maxG,corner,
                Most(n.roll+0.2f,kLeastRoll),Most(top*0.5f,rotate+kLandOver),ram};
}
constexpr Perf Rotor(const char* name,float mark,const jet::Kind& n,float ram) noexcept {
    return Perf{name,static_cast<int>(mark),0.0f,0.0f,n.cruise,n.thrust,n.brake,n.maxG,0.0f,n.roll,kRotorTouch,ram};
}

// The stock bomber as a flight row (see the file comment): 180 m/s cruise and attack, the strike jet's least speed,
// a heavy airframe's thrust, brake, g and roll (the NPC flies its bombing run at kBombG 1.5 g: jet_flight.cpp).
inline constexpr jet::Kind kStockBomber{jet::Role::strike,"bomber",jet::Prefer::ground,jet::FlightModel::wing,jet::Weapon::guns,
                                        jet::Pose::elevons,nullptr,180.0f,180.0f,85.0f,8.0f,12.0f,3.0f,1.0f,450.0f,0.0f,0.0f,0.0f,
                                        0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,1.0f,0.0f,false,Body::strike};

constexpr const jet::Kind& Npc(jet::Role r) noexcept { return jet::KindOf(r); }
constexpr float MarkOf(Body b) noexcept { return jet::Row(b).mark; }

// `ram`: the ram's blast radius, half each model's size (pylib/vcobjects.py JETS): the bomber501 jets 25 m across (12),
// the interceptor 16 m (8), the multirole 26 m (13), the carrier 59 x 77 m (35), the drones 5.7 m long (3), the
// bomber401 bodies (gunship, BOMBER401) ~40 m (20). jet_bay.cpp ImpactDamage takes the impact charge nearest it.
inline constexpr Boardable kBoardable[]={
    {Body::strike,Airframe::wing,Arm::none,Wing("strike",MarkOf(Body::strike),Npc(jet::Role::strike),12.0f)},
    {Body::fighter,Airframe::wing,Arm::none,Wing("fighter",MarkOf(Body::fighter),Npc(jet::Role::fighter),12.0f)},
    {Body::bomber401,Airframe::wing,Arm::none,Wing("bomber401",MarkOf(Body::bomber401),kStockBomber,20.0f)},
    {Body::bomber501_2,Airframe::wing,Arm::none,Wing("bomber501_2",MarkOf(Body::bomber501_2),kStockBomber,12.0f)},
    {Body::interceptor,Airframe::wing,Arm::none,Wing("interceptor",MarkOf(Body::interceptor),Npc(jet::Role::interceptor),8.0f)},
    {Body::multirole,Airframe::wing,Arm::none,Wing("multirole",MarkOf(Body::multirole),Npc(jet::Role::multirole),13.0f)},
    {Body::carrier,Airframe::rotor,Arm::drones,Rotor("carrier",MarkOf(Body::carrier),Npc(jet::Role::carrier),35.0f)},
    {Body::blastCarrier,Airframe::rotor,Arm::drones,Rotor("blastCarrier",MarkOf(Body::blastCarrier),Npc(jet::Role::carrier),35.0f)},
    {Body::dollCarrier,Airframe::rotor,Arm::drones,Rotor("dollCarrier",MarkOf(Body::dollCarrier),Npc(jet::Role::carrier),35.0f)},
    {Body::drone,Airframe::wing,Arm::none,Wing("drone",MarkOf(Body::drone),Npc(jet::Role::drone),3.0f)},
    {Body::blast,Airframe::rotor,Arm::charge,Rotor("blast",MarkOf(Body::blast),Npc(jet::Role::blast),3.0f)},
    {Body::doll,Airframe::rotor,Arm::charge,Rotor("doll",MarkOf(Body::doll),Npc(jet::Role::doll),3.0f)},
    {Body::gunship,Airframe::wing,Arm::shells,Wing("gunship",MarkOf(Body::gunship),Npc(jet::Role::gunship),20.0f)},
};
constexpr int kBoardableCount=static_cast<int>(sizeof(kBoardable)/sizeof(kBoardable[0]));

// Each row's body flies its row's way (a rotor role is a rotor craft), is a jet body (a mark), not the enemy's,
// and no body is listed twice; a wing can idle above its 1 g speed, lift off past it and land at it.
constexpr bool BoardableConsistent() noexcept {
    for(int i=0;i<kBoardableCount;++i) {
        const Boardable& b=kBoardable[i];
        const jet::BodyRow& row=jet::Row(b.body);
        const bool rotor=jet::KindOf(row.role).flight==jet::FlightModel::rotor;
        if(row.mark<=0.0f || row.hostile || rotor!=(b.frame==Airframe::rotor) || b.perf.mark!=static_cast<int>(row.mark))return false;
        for(int k=i+1;k<kBoardableCount;++k)if(kBoardable[k].body==b.body)return false;
        const Perf& p=b.perf;
        if(b.frame==Airframe::wing &&
           !(p.minAir*p.minAir*p.maxG>p.corner*p.corner && p.rotate>p.minAir && p.top>p.rotate+20.0f && p.landMax>p.rotate))return false;
        if(p.top<=0.0f || p.thrust<=0.0f || p.roll<=0.0f || p.ram<=0.0f)return false;
    }
    return true;
}
static_assert(BoardableConsistent(),"kBoardable: rotor rows rotor craft, jet bodies of our side once each, flyable wings");

// A rotor craft's height over the floor (docs/player-jet-re.md §14). The ground probe (body506.cpp GroundClearance)
// measures from the vehicle's position, which on a 506 is its collision box's centre (heli_rigid_body[0]), while the
// drawn model's origin, its lowest point (pylib/jet_models.py grounded), is its mesh bone, `rest` m under it. The rotor
// flight (HoverStep, HoverDone, RotorHail) reads "on the ground" as under kTouch (3 m): the carrier's position is
// 8.516 m over its bottom, so standing on the ground it read 9 m up (2026-10-06 14:56:55, "air ... 9 m over the
// ground"): never set down, never parked, left on the ground it was handed back to its NPC pilot as if in the air, a
// called-down one never reached its spot. Its clearance is its bottom's: the position's less `rest`. (The wings keep
// the position's: their rests, 1.38 / 2.12 m, are under kTouch, which their landing was tuned on.)
constexpr float kRestMost=100.0f;   // m: a rest no airframe of ours has (the bone not where it should be: none)
// The rest from the position's height and the mesh bone's: 0 when it is not a plausible one (or NaN).
constexpr float RestHeight(float posY,float meshY) noexcept {
    const float r=posY-meshY;
    return r>0.0f && r<kRestMost ? r : 0.0f;
}
// The bottom's clearance from the position's (`none`: no ground seen, kept).
constexpr float BottomClear(float clear,float rest,float none) noexcept { return clear==none ? clear : clear-rest; }
static_assert(BottomClear(8.516f,RestHeight(108.516f,100.0f),-1e9f)<0.01f,"a carrier standing on the ground is on it");
static_assert(BottomClear(20.0f,RestHeight(108.516f,100.0f),-1e9f)>11.0f,"a carrier 11.5 m up is in the air");
static_assert(RestHeight(100.0f,108.0f)==0.0f && RestHeight(500.0f,100.0f)==0.0f,"no rest from a bone out of place");
static_assert(BottomClear(-1e9f,8.0f,-1e9f)==-1e9f,"no ground stays no ground");

// The row of body `b`, or nullptr (not boardable).
constexpr const Boardable* Row(Body b) noexcept {
    for(const auto& r:kBoardable)if(r.body==b)return &r;
    return nullptr;
}
}  // namespace pjet
}  // namespace crew
