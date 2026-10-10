// NPC jets: what the jet files share (docs/jet-plan.md, docs/jet-model-re.md).
//   jet.cpp         the table of jets (identity, lifecycle: crewing, reaping, the mission's reset) and the
//                   frame that runs each jet's flight and weapon off its kind's table row
//   jet_flight.cpp  flight: the wing's guidance and attitude, rotor craft (Hover), ground, ceiling, walls
//   jet_combat.cpp  targets, the guns' and missiles' attack runs and their fire gate
//   jet_carrier.cpp the carrier and its drones (launch, recovery, the blast / doll charges, the dolls)
//   jet_bay.cpp     IndirectFireControl users: a bomber's bomb bay, the gunship's shells, impact charges
//   jet_spawn.cpp   the bodies (SGOs), their preload, spawning, the body's fix-ups and far rendering
//   jet_hooks.cpp   the 506 physics step (body506.cpp hands it here), the bullets' pass-through, install
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include "crew.h"
#include "body506.h"
#include "memory.h"
#include "airbound.h"
#include "ferry_line.h"
#include "air_chase.h"
#include <cmath>

namespace crew {
// A fault caught in the plugin's jets or airstrikes (__except filter): logged once per `where` every few
// seconds with its code and address, then handled.
int FaultLog(const char* where,const EXCEPTION_POINTERS* e) noexcept;
// booster.cpp: drops the boosters of carriers gone or dead; from the per-frame reap (JetReap), so a carrier
// that went (the last one, the mission's end) leaves no flame behind.
void BoosterSweep(ULONGLONG ms) noexcept;

namespace jet {
// --- Layout ---
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr unsigned kDelete=0x118A1B0,kCreateObject=0x11945E0,kInitParamVtable=0x1762068;   // SetTeam: crew.h kSetTeam
constexpr std::size_t kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamFriend=2;
constexpr std::int32_t kTeamEnemy=1;   // the game's team relations: 0 (player) and 2 (friends) are both hostile to 1
// InitParamBase as DemoAirStrike's ctor builds it on its stack (0x5B433A): the vtable, the rest zero.
struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using DeleteFn=void(*)(void*);

constexpr float kPi=3.14159265f,kG=9.8f,kGravity=14.7f;
// m/s no jet is commanded past (see jet_flight.cpp JetSteer and jetprops.cpp).
constexpr float kBodyTop=250.0f;
constexpr float kMinAlt=25.0f;         // never lower over the ground than this
constexpr float kCeilingGap=12.0f;
// Game ms a jet not flown this long is no longer flown (its position and command stale): left out of the
// spatial checks (other jets near, in a burst's way, a drone docking). Not its identity: that is its ObjRef.
constexpr ULONGLONG kStaleMs=1500;
constexpr unsigned kPlacedFlight=1;    // the jets a mission places (see jet_hooks.cpp, flights)

// --- Roles and their kinds ---
// Flight, per role (Kind). Speeds m/s, heights m above the target (or the anchor: the player, or where
// it first flew). The stock bombers fly 3 m a frame (180 m/s): the strike jet attacks at that, the fighter
// is faster and pulls harder. Every distance of an attack scales with the turn radius v^2/(n g).
enum class Role { strike, fighter, interceptor, multirole, carrier, drone, blast, doll, gunship, primer, centipede,
                  dragonfly };
constexpr int kRoleCount=12;
// What a role goes for first: ground or flying targets (the other only with none of its own), or either.
enum class Prefer { ground, air, any };
// How it flies: a wing (JetSteer: lift along its up, it banks to turn) or a rotor craft (Hover: it goes
// where it wants at any speed and hovers still).
enum class FlightModel { wing, rotor };
// What it fights with: guns and missiles (it flies at its targets: Strike, Chase, Missile), shells from where
// it flies (the gunship: GunshipFire), drones it launches (the carrier: LaunchDrones), or a charge it carries
// into the enemy (the blast and doll drones: Detonate), or the Primer creatures' own (primer.cpp: enemies).
enum class Weapon { guns, shells, drones, charge, primer };
// The bones it moves: elevons (Elevons), the carrier's nacelles (Thrusters), the Primer fighter's wings (Flap), or none.
enum class Pose { none, elevons, thrusters, flap };
// How a rotor craft shows its thrust: `pitchShare` of the fore-and-aft part leans its body (1: all, as a
// drone's rotors; the carrier's nacelles take the rest, kCarrierPitchShare), and `drag` m/s^2 per m/s of its
// speed is the thrust that would hold that speed (kThrustDrag; 0: none shown). `respond`: s its velocity takes
// to close on the one wanted (0: at once, at its thrust), and `jerk` m/s^3 its acceleration changes at most
// (0: no limit), so a heavy one swings into a move and out of it instead of sliding; `maxLean` rad it tilts;
// `bank` times the sideways thrust it rolls by (1: as the thrust; more: it heels into its turns).
struct Lean { float pitchShare,drag,respond,jerk,maxLean,bank; };
constexpr float kHoverLean=0.35f;      // rad: the most a rotor craft tilts
// Its flight has no drag (the plugin sets the velocity), so the thrust a real one needs to hold its speed,
// kThrustDrag per m/s, is added for the look (the carrier's nacelles).
constexpr float kThrustDrag=0.12f;
// The carrier's fuselage leans only this share of the fore-and-aft thrust (the nacelles take it); sideways,
// which the nacelles cannot vector, it rolls as before. (0.25 until 2026-10-04: it looked to float, the body
// level whatever it did; then half, its acceleration eased: see kCarrierLean; still floating, so 0.8, and it
// banks kCarrierBank times its turn: a heavy craft heels into its orbit instead of sliding round it level.)
constexpr float kCarrierPitchShare=0.8f,kCarrierBank=2.5f;
inline constexpr Lean kRotorLean{1.0f,0.0f,0.0f,0.0f,kHoverLean,1.0f};
inline constexpr Lean kCarrierLean{kCarrierPitchShare,kThrustDrag,2.5f,1.2f,0.3f,kCarrierBank};

// The bodies a jet flies in (and the helis the Air Raider calls): the SGO, its file in Mods/OBJECT, the mark its
// mission_setup writes into the speed gain k (veh+0x162C; body506.cpp's range 7001-7099 for jets), and what that
// mark makes it. The mark is the one source of what a jet is: an entry made again for a jet (JetFrame) reads it.
enum class Body { strike, fighter, bomber401, bomber501_2, interceptor, multirole, carrier, drone, blast, doll, heli410, heli506,
                  gunship, blastCarrier, dollCarrier, enemyFighter, primerFighter, centipede, dragonfly, heliMedic,
                  heliTransport, transportPlane };
constexpr int kBodyCount=22;
struct BodyRow {
    Body body;
    const wchar_t* sgo;
    const wchar_t* file;
    float mark;                     // 0: a heli (no jet mark)
    Role role;
    Role drones;                    // a carrier's: what it launches (blast, doll; else the gun drone)
    const char* name;
    bool hostile=false;             // the enemy's: on first sight it joins the enemy team (CrewPlaced) and hunts the
                                    // player's side round the player, as a friendly one guards them
};
// The strike jets that take over a BOMBER401 or BOMBER501_2 fly that bomber's own model with the strike jet's
// mark; tools/make_jets.py writes them all (testrange/gen.py JETS has the marks of the mission-placed ones).
inline constexpr BodyRow kBodies[kBodyCount]={
    {Body::strike,L"app:/object/edf6vc_jet_strike.sgo",L"EDF6VC_JET_STRIKE.SGO",7001.0f,Role::strike,Role::drone,"strike"},
    {Body::fighter,L"app:/object/edf6vc_jet_fighter.sgo",L"EDF6VC_JET_FIGHTER.SGO",7002.0f,Role::fighter,Role::drone,"fighter"},
    {Body::bomber401,L"app:/object/edf6vc_bomber401.sgo",L"EDF6VC_BOMBER401.SGO",7001.0f,Role::strike,Role::drone,"bomber401"},
    {Body::bomber501_2,L"app:/object/edf6vc_bomber501_2.sgo",L"EDF6VC_BOMBER501_2.SGO",7001.0f,Role::strike,Role::drone,"bomber501_2"},
    {Body::interceptor,L"app:/object/edf6vc_jet_interceptor.sgo",L"EDF6VC_JET_INTERCEPTOR.SGO",7003.0f,Role::interceptor,Role::drone,
     "interceptor"},
    {Body::multirole,L"app:/object/edf6vc_jet_multirole.sgo",L"EDF6VC_JET_MULTIROLE.SGO",7004.0f,Role::multirole,Role::drone,"multirole"},
    {Body::carrier,L"app:/object/edf6vc_jet_carrier.sgo",L"EDF6VC_JET_CARRIER.SGO",7005.0f,Role::carrier,Role::drone,"carrier"},
    {Body::drone,L"app:/object/edf6vc_jet_drone.sgo",L"EDF6VC_JET_DRONE.SGO",7006.0f,Role::drone,Role::drone,"drone"},
    {Body::blast,L"app:/object/edf6vc_jet_blast.sgo",L"EDF6VC_JET_BLAST.SGO",7007.0f,Role::blast,Role::drone,"blast"},
    {Body::doll,L"app:/object/edf6vc_jet_doll.sgo",L"EDF6VC_JET_DOLL.SGO",7008.0f,Role::doll,Role::drone,"doll"},
    {Body::heli410,L"app:/object/edf6vc_heli_410.sgo",L"EDF6VC_HELI_410.SGO",0.0f,Role::fighter,Role::drone,"heli410"},
    {Body::heli506,L"app:/object/edf6vc_heli_506.sgo",L"EDF6VC_HELI_506.SGO",0.0f,Role::fighter,Role::drone,"heli506"},
    {Body::gunship,L"app:/object/edf6vc_jet_gunship.sgo",L"EDF6VC_JET_GUNSHIP.SGO",7011.0f,Role::gunship,Role::drone,"gunship"},
    {Body::blastCarrier,L"app:/object/edf6vc_jet_blast_carrier.sgo",L"EDF6VC_JET_BLAST_CARRIER.SGO",7009.0f,Role::carrier,Role::blast,
     "blastCarrier"},
    {Body::dollCarrier,L"app:/object/edf6vc_jet_doll_carrier.sgo",L"EDF6VC_JET_DOLL_CARRIER.SGO",7010.0f,Role::carrier,Role::doll,
     "dollCarrier"},
    // The enemy fighter (testrange/gen.py: the interceptor's dark bomber501_2 model, a dogfighter's role).
    {Body::enemyFighter,L"app:/object/edf6vc_jet_enemy_fighter.sgo",L"EDF6VC_JET_ENEMY_FIGHTER.SGO",7020.0f,Role::fighter,Role::drone,
     "enemyFighter",true},
    // The Primers' fighter (pylib/primer_fighter_model.py: a pod of their new ship with two of its hatch petals for wings),
    // the enemy's, flapping (Role::primer, Pose::flap).
    {Body::primerFighter,L"app:/object/edf6vc_jet_primer_fighter.sgo",L"EDF6VC_JET_PRIMER_FIGHTER.SGO",7030.0f,Role::primer,
     Role::drone,"primerFighter",true},
    // The Primer creatures (primer.cpp, docs/primer-plan.md), enemies a mission places.
    {Body::centipede,L"app:/object/edf6vc_centipede.sgo",L"EDF6VC_CENTIPEDE.SGO",7012.0f,Role::centipede,Role::drone,"centipede",true},
    {Body::dragonfly,L"app:/object/edf6vc_dragonfly.sgo",L"EDF6VC_DRAGONFLY.SGO",7013.0f,Role::dragonfly,Role::drone,"dragonfly",true},
    // The medic heli (tools/make_jets.py MEDIC_HELI_FILE): the 410 with healing door guns (heli.cpp Medic).
    {Body::heliMedic,L"app:/object/edf6vc_heli_medic.sgo",L"EDF6VC_HELI_MEDIC.SGO",0.0f,Role::fighter,Role::drone,"heliMedic"},
    // The transports (tools/make_jets.py TRANSPORT_*; transport.cpp): the 410 with ten passenger seats more, and the bomber401
    // strike jet with twelve behind its pilot (flown as a strike jet that attacks nothing: jet.cpp JetFerry).
    {Body::heliTransport,L"app:/object/edf6vc_heli_transport.sgo",L"EDF6VC_HELI_TRANSPORT.SGO",0.0f,Role::fighter,Role::drone,"heliTransport"},
    {Body::transportPlane,L"app:/object/edf6vc_jet_transport.sgo",L"EDF6VC_JET_TRANSPORT.SGO",7001.0f,Role::strike,Role::drone,"transportPlane"},
};
constexpr bool BodiesInOrder() noexcept {
    for(int i=0;i<kBodyCount;++i)if(static_cast<int>(kBodies[i].body)!=i)return false;
    return true;
}
static_assert(BodiesInOrder(),"kBodies is indexed by Body");
// No two jet bodies of different roles share a mark (the bodies of one role may: the bomber takeovers).
constexpr bool MarksUnique() noexcept {
    for(int i=0;i<kBodyCount;++i)
        for(int k=i+1;k<kBodyCount;++k)
            if(kBodies[i].mark>0.0f && kBodies[i].mark==kBodies[k].mark &&
               (kBodies[i].role!=kBodies[k].role || kBodies[i].drones!=kBodies[k].drones))return false;
    return true;
}
static_assert(MarksUnique(),"a mark names one role");
constexpr const BodyRow& Row(Body b) noexcept { return kBodies[static_cast<int>(b)]; }

struct Kind {
    Role role;
    const char* name;
    Prefer prefer;
    FlightModel flight;
    Weapon weapon;
    Pose pose;
    const Lean* lean;               // a rotor craft's (FlightModel::rotor), else nullptr
    float cruise,attack,minSpeed;   // m/s
    float thrust,brake;             // m/s^2 toward the wanted speed (gravity along the path comes on top)
    float maxG;                     // lift: at most this many g
    float roll;                     // rad/s: how fast it rolls (and pitches) its body round
    float alt;                      // m over the target or the anchor it cruises at
    float diveStart,pullAlt,extendOut;   // Strike
    float gunOpen,gunClose;         // m: the guns fire from gunOpen (or their reach) in to gunClose
    float patrol,patrolStep;        // m: patrol circle, plus this per jet of its flight
    float overrun,chaseOver;        // Chase
    float range;                    // m from the anchor it takes targets in
    float missileRange;             // m: it fires its missiles from here in, standing off (0: never), and never farther
                                    // than its missile locks (Arms::missileRange: the weapon's own LockonRange)
    float fuel;                     // its time in the air, times Cfg().jetFuelSec
    float trigger;                  // a charge's: m from its target it goes off (see kBlastTrigger), else 0
    bool doll;                      // it carries a hololive doll (DollMake)
    Body body;                      // the body it flies in when launched as itself
    float blast=0.0f;               // a charge's: m its charge's blast reaches (pylib/vcobjects.py jet_guns' AmmoExplosion of
                                    // EDF6VC_BLAST_CHARGE / EDF6VC_DOLL_CHARGE), else 0: held off its target it goes off only
                                    // within this (JetFrame, airchase::Step)
};
// Blast and doll drones (Weapon::charge: a blast or doll carrier's): rotor drones that fly at their target
// (Hover; no guns) and, within their trigger of it (or held off it by its body within kTriggerHeld times that),
// fire their one charge (weapon 2, testrange/gen.py JET_BLAST_FILES: a grenade that bursts two frames on, where
// the drone is) and are deleted kBlastMs later (Blast). The blast's damage is filtered by team (GameDamageInfo,
// docs/decoy-blast-re.md 1.1): no friend is hurt.
constexpr float kBlastTrigger=8.0f,kDollTrigger=6.0f;
// A doll hangs kDollBelow under its drone (jet_carrier.cpp DollPose), never under the ground beneath it; a doll drone
// charging comes in kDollRide over the ground under its target at least, so its doll is not pressed into its body.
constexpr float kDollBelow=4.0f,kDollRide=kDollBelow+1.0f;
inline constexpr Kind kKinds[kRoleCount]={
    // 2026-10-04: faster (750-900 km/h at the attack; own motion properties lift the 200 m/s cap), higher, about 5 g at most.
    // 2026-10-05: strafing runs open fire from 1000 m (the guns' reach caps it) and pull out lower (80-100 m over the
    // target): about 4 s of fire a pass, as a real gun run, not 2.
    {Role::strike,"strike",Prefer::ground,FlightModel::wing,Weapon::guns,Pose::elevons,nullptr, 190.0f,215.0f,85.0f, 10.0f,15.0f, 5.0f,1.4f,
     450.0f, 1500.0f,80.0f,2200.0f, 1000.0f,120.0f, 1000.0f,120.0f, 120.0f,30.0f, 1200.0f, 800.0f,1.0f, 0.0f,false,Body::strike},
    {Role::fighter,"fighter",Prefer::air,FlightModel::wing,Weapon::guns,Pose::elevons,nullptr, 210.0f,235.0f,110.0f, 15.0f,20.0f, 5.0f,2.4f,
     550.0f, 1700.0f,90.0f,2500.0f, 1000.0f,120.0f, 1400.0f,150.0f, 160.0f,40.0f, 1800.0f, 1100.0f,1.0f, 0.0f,false,Body::fighter},
    {Role::interceptor,"interceptor",Prefer::air,FlightModel::wing,Weapon::guns,Pose::elevons,nullptr, 220.0f,245.0f,120.0f, 25.0f,20.0f,
     5.0f,2.0f, 600.0f, 1900.0f,100.0f,2800.0f, 1000.0f,130.0f, 1500.0f,150.0f, 220.0f,50.0f, 2600.0f, 1600.0f,1.0f, 0.0f,false,
     Body::interceptor},
    {Role::multirole,"multirole",Prefer::any,FlightModel::wing,Weapon::guns,Pose::elevons,nullptr, 200.0f,225.0f,100.0f, 12.0f,18.0f,
     5.0f,2.0f, 500.0f, 1600.0f,90.0f,2300.0f, 1000.0f,120.0f, 1200.0f,130.0f, 150.0f,35.0f, 1500.0f, 1000.0f,1.0f, 0.0f,false,
     Body::multirole},
    // Over its anchor, its drones do the reaching (2026-10-03: 1300 m out).
    {Role::carrier,"carrier",Prefer::any,FlightModel::rotor,Weapon::drones,Pose::thrusters,&kCarrierLean, 60.0f,60.0f,40.0f, 4.0f,4.0f,
     1.3f,0.35f, 150.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 450.0f,80.0f, 0.0f,0.0f, 1800.0f, 0.0f,4.0f, 0.0f,false,Body::carrier},
    {Role::drone,"drone",Prefer::any,FlightModel::wing,Weapon::guns,Pose::elevons,nullptr, 140.0f,160.0f,60.0f, 25.0f,25.0f, 8.0f,3.5f,
     120.0f, 500.0f,35.0f,700.0f, 350.0f,30.0f, 300.0f,40.0f, 50.0f,20.0f, 1800.0f, 0.0f,1.0f, 0.0f,false,Body::drone},
    // Rotor drones (Hover): cruise is the most they fly at, thrust what they turn with; no gun ever fires.
    {Role::blast,"blast",Prefer::any,FlightModel::rotor,Weapon::charge,Pose::none,&kRotorLean, 70.0f,70.0f,0.0f, 30.0f,30.0f, 8.0f,3.5f,
     20.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 1800.0f, 0.0f,1.0f, kBlastTrigger,false,Body::blast,15.0f},
    {Role::doll,"doll",Prefer::any,FlightModel::rotor,Weapon::charge,Pose::none,&kRotorLean, 25.0f,25.0f,0.0f, 12.0f,12.0f, 4.0f,2.0f,
     10.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 1800.0f, 0.0f,1.0f, kDollTrigger,true,Body::doll,25.0f},
    // The gunship (JetRole::gunship): the bomber401 body (its own SGO, mark 7011) circling its anchor wide and
    // slow, never diving; it shells ground targets in reach from where it flies and fires its side cannon at them
    // from further out (GunshipFire; TargetRange: its range grows with the cannon).
    {Role::gunship,"gunship",Prefer::ground,FlightModel::wing,Weapon::shells,Pose::elevons,nullptr, 120.0f,120.0f,70.0f, 3.0f,3.0f, 2.0f,0.3f,
     350.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 600.0f,80.0f, 0.0f,0.0f, 1500.0f, 0.0f,3.0f, 0.0f,false,Body::gunship},
    // The Primer fighter: a flapping dogfighter, slower than ours and nimbler (its wings beat it round: Flap), guns only.
    {Role::primer,"primer",Prefer::air,FlightModel::wing,Weapon::guns,Pose::flap,nullptr, 170.0f,195.0f,80.0f, 18.0f,22.0f, 7.0f,3.0f,
     450.0f, 1400.0f,90.0f,2000.0f, 900.0f,100.0f, 1200.0f,120.0f, 120.0f,35.0f, 1800.0f, 0.0f,1.0f, 0.0f,false,
     Body::primerFighter},
    // The Primer creatures (primer.cpp flies them; only cruise, thrust, brake, roll and the lean are read): rotor
    // craft, the centipede steady, the dragonfly quick to dart.
    {Role::centipede,"centipede",Prefer::any,FlightModel::rotor,Weapon::primer,Pose::none,&kRotorLean, 35.0f,35.0f,0.0f, 18.0f,18.0f,
     2.0f,1.6f, 45.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 1500.0f, 0.0f,1.0f, 0.0f,false,Body::centipede},
    {Role::dragonfly,"dragonfly",Prefer::air,FlightModel::rotor,Weapon::primer,Pose::none,&kRotorLean, 85.0f,95.0f,0.0f, 45.0f,45.0f,
     6.0f,4.0f, 60.0f, 0.0f,0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 0.0f,0.0f, 1500.0f, 0.0f,1.0f, 0.0f,false,Body::dragonfly},
};
constexpr bool KindsInOrder() noexcept {
    for(int i=0;i<kRoleCount;++i) {
        const Kind& k=kKinds[i];
        if(static_cast<int>(k.role)!=i || Row(k.body).role!=k.role)return false;
        if((k.flight==FlightModel::rotor)!=(k.lean!=nullptr) || (k.weapon==Weapon::charge)!=(k.trigger>0.0f) ||
           (k.weapon==Weapon::charge)!=(k.blast>k.trigger))return false;
    }
    return true;
}
static_assert(KindsInOrder(),"kKinds is indexed by Role, each row's body is of its role, rotor craft lean, charges trigger and blast");
constexpr const Kind& KindOf(Role r) noexcept { return kKinds[static_cast<int>(r)]; }

// The roles a launch asks for (crew.h JetRole) and the body each flies in. The blast and doll carriers are
// carriers whose own bodies' marks name their drones.
struct LaunchRow { JetRole as; Body body; };
inline constexpr LaunchRow kLaunchRows[]={
    {JetRole::strike,Body::strike},{JetRole::fighter,Body::fighter},{JetRole::interceptor,Body::interceptor},
    {JetRole::multirole,Body::multirole},{JetRole::carrier,Body::carrier},{JetRole::blastCarrier,Body::blastCarrier},
    {JetRole::dollCarrier,Body::dollCarrier},{JetRole::gunship,Body::gunship},
};
constexpr int kLaunchCount=static_cast<int>(sizeof(kLaunchRows)/sizeof(kLaunchRows[0]));
constexpr bool LaunchRowsInOrder() noexcept {
    for(int i=0;i<kLaunchCount;++i)if(static_cast<int>(kLaunchRows[i].as)!=i || Row(kLaunchRows[i].body).mark<=0.0f)return false;
    return true;
}
static_assert(kLaunchCount==static_cast<int>(JetRole::gunship)+1,"a launch row per JetRole");
static_assert(LaunchRowsInOrder(),"kLaunchRows is indexed by JetRole, each a jet body");

// --- A jet's state ---
enum class Mode { takeoff, patrol, approach, dive, pull, extend, chase, runOut, withdraw, bomb, missile, crank, recover };
inline const char* const kModeNames[]={"takeoff","patrol","approach","dive","pull","extend","chase","runOut","withdraw","bomb","missile",
                                       "crank","recover"};

// Bones the plugin hinges along their local X (elevons, thrusters): their records, bind locals, angles.
constexpr int kMaxSurfaces=4;
struct Surfaces {
    const unsigned char* model;   // the bone array they were looked up in (null: not looked yet)
    unsigned char* rec[kMaxSurfaces];   // null: not in this model
    float bind[kMaxSurfaces][16],at[kMaxSurfaces],set[kMaxSurfaces][16];
    int count;                    // all `count` found, else 0
    bool fresh,written,logged;    // fresh: the first pose snaps to the angle wanted
};

// The carrier and its drones (LaunchDrones, Recover): see jet_carrier.cpp.
constexpr int kCarrierDrones=6;
constexpr ULONGLONG kDroneOut=~0ull;
// A carrier's ammo: this many drone launches. With none left and every drone back (docked or lost) it
// withdraws "out of drones", as a jet out of ammo does.
constexpr int kCarrierSorties=18;

// The flight a wing flies (JetSteer, Guard, Sense): what the physics stage writes, and what it learned.
struct Motion {
    float vel[3],omega[3];   // what the physics stage writes
    bool ready;              // vel/omega hold this frame's command
    float flap;              // the Primer fighter's wing-beat phase (rad, Flap)
    float aoa;               // a wing's angle of attack (rad, nose above the path; see kAoaPerG)
    float top;               // m/s it never goes past: its kind's, or a faster bomber's speed
    float prevPos[3];        // where the body was at prevAt (Sense)
    ULONGLONG prevAt,blockedFor;
    float real;              // m/s the body really flies (game time, smoothed): against Len(vel), what it is told
    float groundY;           // the surface under it when a ray last found one (groundSeen): off the map's
    bool groundSeen;         // terrain no ray finds any, and that is where it is held over (Guard, HoldOffGround)
    ULONGLONG floorLogAt;    // when HoldOffGround last logged it (once a second)
    float thrust[3];         // a rotor craft's (Hover): the thrust its flight asks for, world, m/s^2
    float acc[3];            // a rotor craft's eased acceleration (see Lean::respond)
    float power;             // a rotor craft's engine, 0..1: its share of its full thrust `thrust` takes (Hover)
    ULONGLONG thrustLogAt;   // Thrusters' last log
    std::uint8_t sweep;      // Ahead's next stretch of its track
    float obstTop,obstAt[3]; // the highest thing Ahead found on its track: its top, where its face was hit
    ULONGLONG obstUntil;     // ...kept till then (0: none), or till the jet is past it or off its track
    std::int8_t obstSide;    // ...too steep to climb: the side it turns off to (+1 / -1, picked once; 0 none)
    bool edgeBack;           // past its soft edge: flying back in first (airbound.h KeepIn), its targets out there let be
    signed char edgeTurn;    // ...turning back from it: the side it turns on (KeepIn: +1 left, -1 right, 0 none)
};
// What it goes for (Pick, Lead) and its guns' and missiles' state (Fire, Missile).
struct Aim {
    const void* target;
    bool flyer;
    float aim[3],tgtPrev[3],tgtVel[3];
    ULONGLONG trackFrame;    // GameFrame of tgtPrev (PickTarget: the velocity from one frame's move only)
    ULONGLONG seenTarget;    // game ms it last had a target
    float out[3];            // extend / run-out / crank direction
    float outRun;            // m from the target its extend flies out to (Strike: PlanExtend, the room it has)
    float outAt[3];          // a teardrop extend's turn point (outTear): beside the line out, outRun along it
    bool outTear;            // its extend is a teardrop to outAt (a box too small to fly straight out and round)
    ULONGLONG missileAt;     // its last missile salvo
    ULONGLONG lockAt;        // game ms the nose came onto the target within kMissileCone (0: not on it)
    ULONGLONG lockSeen;      // standing off with missiles: game ms the lock list last held a target (0: not)
    ULONGLONG gunsUntil;     // no lock came (kNoLockMs): guns only until then
    ULONGLONG gateAt;        // the last gun gate log (Fire)
    ULONGLONG bombAt;        // its last bomb (Fire)
    ULONGLONG rocketAt;      // its last rocket ripple (Fire)
    airchase::Closing closing;   // a charge drone's run at its target (JetFrame: airchase::Step)
    airchase::ShunList shun;     // targets let be for now: a charge drone's it gave up, its carrier's (VisitTarget)
};
// A carrier's work (CarrierGoal, LaunchDrones): hit (hpSeen fell) it sidesteps to evadeTo until evadeUntil, and
// not again before evadeAgain; it holds still while a drone docks (docking); its station follows its target.
struct CarrierState {
    Role drones;             // what it launches (blast, doll; else the drone): its body's mark's
    float hpSeen,evadeTo[3];
    ULONGLONG evadeUntil,evadeAgain;
    bool docking;
    const void* stationFor;
    ULONGLONG dock[kCarrierDrones];   // its drones: game ms each is ready (kDroneOut: out)
    ULONGLONG launchAt;      // its last launch
    int sorties;             // its launches left (kCarrierSorties)
    float order[3];          // the player flying it sent its drones here (PlayerLaunchDrone): they work round it,
    bool ordered;            // ...within kOrderRange, instead of round the carrier
};
// A carrier's drone: its carrier (the carrier entry's control block; nullptr once that entry is gone) and its
// place on it. `carried` stays: a drone whose carrier is gone withdraws ("carrier lost").
struct DroneState {
    const void* mother;
    bool carried;
    int slot;
    ULONGLONG blastAt;       // a charge: game ms it went (0: not yet)
};
// The Primer creatures' (primer.cpp). A centipede: the ones ahead of it and behind it in a long one (their
// entries' control blocks, nullptr: none), the tail it makes for to join, the trail it leaves (kPrimerTrail samples
// kTrailStep apart, newest at trailHead-1), its orbit and dives. A dragonfly: its hunt (0 hunting, 1 striking,
// 2 darting off) and where it darts to. Both: their moving parts' bone records and bind locals (Pose), the
// legs' phase, the abdomen's curl.
constexpr int kPrimerTrail=24,kPrimerParts=16;
struct PrimerState {
    bool init;
    const void* ahead;
    const void* behind;
    const void* joining;
    ULONGLONG lookAt;        // its last look for a tail to join
    float trail[kPrimerTrail][3],last[3];
    int trailCount,trailHead;
    bool flying;
    float orbit,seed;
    bool dive;
    float diveTo[3];
    ULONGLONG diveAt;
    int hunt;
    ULONGLONG huntAt;
    float dartTo[3];
    const unsigned char* poseModel;
    unsigned char* poseRec[kPrimerParts];
    float poseBind[kPrimerParts][16];
    bool posed;              // every part found (its moving parts, then its aimed mounts: Pose)
    float phase,curl;
    ULONGLONG logAt;
    ULONGLONG regrowAt;      // a split's new front: game ms its head began to grow back (0: not growing)
    ULONGLONG tailAt;        // ...a split's new end: game ms its tail began to grow back (0: not growing)
    float writheFace[3];     // writhing: the way it pointed when it began (Writhe swings it about that)
    float bloodAt[3],bloodDamage;   // hits since its last frame (PrimerMessage): the last one's point, their damage
    float headShown,tailShown;   // how much of its head / tail showed last frame (a corpse keeps them)
    const char* what;        // what it did this frame, and what it fired (Debug, Trace: 1 spit, 2 stinger, 4 barbs)
    int fired;
    ULONGLONG traceAt;
};
// The bomb bay of a jet that takes over a bomber (jet_bay.cpp).
struct BayState {
    unsigned char* ifc;      // the bomb bay (see kIfcCtor), or nullptr
    float bombAt[3],bombDir[3],bombAlt,bombSpeed,fireDist,reach;
    bool bombing;            // the bay is open
    float bayFrom;           // m along the line (from the target, + past it) where the bay opened
    std::int32_t baySteps;   // steps of the open bay: its drop point is bayFrom+reach+baySteps x speed a frame
    ULONGLONG bayOpenAt;     // game ms the bay opened
    const void* hold;        // the stock bomber kept (hidden) while the bay is there: its call's marker stays up
    const void* bombOwner;   // whose bombs the bay drops (the caller: the bomb rounds' owner)
    ULONGLONG bombClear;     // game ms until which the owner's rounds still pass its flight (0: bay open)
};
// The gunship's side guns besides its shells (jet_bay.cpp kSideGuns, in this order): the 40 mm long-range cannon and the
// 25 mm gatling (an AC-130's three guns with the shells).
enum class SideGun : int { cannon, gatling, count };
// A side gun's own clock (jet_bay.cpp GunShot / GunAtTarget).
struct GunClock {
    ULONGLONG at;            // its last round
    int shots;               // ...and how many it has fired (the gatling's spread pattern counts on it)
    ULONGLONG lookAt;        // the NPCs' last look along its line (GunAtTarget: a ray a gap at the most)
    int held;                // ...and how many found the map in the way (logged every tenth)
};
// The gunship's shells and its side guns (GunshipFire): three guns, each with its own gap.
struct ShellState {
    ULONGLONG gunAt;         // its last shell
    int gunShots;            // ...and how many it has fired
    GunClock guns[static_cast<int>(SideGun::count)];
};

// The targets a jet goes for by what its stores can strike (the user, 2026-10-09: loads made before the battle, "飞机可以
// 全带炸弹，或者全带导弹"): carrying only air-to-air stores, flying targets first; only ground ones (air-to-ground
// missiles, bombs, rockets), ground targets first; mixed, none, or all spent: its kind's own (Kind::prefer). The stores
// carried, not their rounds left (jet.cpp): a mixed load with one side spent stays mixed. Every
// stock load keeps its kind's (the fighter's and the interceptor's are air-to-air, the strike jet's and the multirole's
// mixed), so this changes only a load the player chose (a fighter loaded with bombs now goes for the ground).
constexpr Prefer LoadoutPrefer(Prefer kind,int airRounds,int groundRounds) noexcept {
    return airRounds>0 && groundRounds<=0 ? Prefer::air : groundRounds>0 && airRounds<=0 ? Prefer::ground : kind;
}
struct Jet {
    ObjRef ref;              // the vehicle: address and weak-this control block, on which a weak reference is
                             // held while the entry is (HoldRef): the block outlives the object, so a destroyed
                             // jet is told by its use count and no other object can take its block's address
    Role role;
    Mode mode;
    ULONGLONG bornAt,seen,modeAt,loggedAt;
    ULONGLONG lastStep;   // GameMs of the last frame step (GameStep)
    float anchor[3];         // where it patrols when there is no player
    bool reap;               // withdrawn: delete from another object's update (JetReap)
    bool loadSet;            // loadPrefer read this frame from its stores (jet.cpp; else its kind's preference)
    Prefer loadPrefer;       // LoadoutPrefer of its stores
    const char* why;         // why it withdrew
    ULONGLONG emptyFrame;    // the game frame its rider was put off for the reap (JetReap: the delete waits for it), 0 none
    bool launched;           // made by JetLaunch: anchor is its strike point
    bool entered;            // ...its first attack run begun: its arrival over (Entering)
    bool escort;             // ...or the player, while seen (a call's follow variant; anchor: where they were last)
    unsigned flight;         // its rounds pass through the other jets of this flight (kPlacedFlight)
    int wing;                // its place in its flight: its patrol ring, a carrier's way round its orbit
    ULONGLONG fuelMs;
    bool farOff;             // its render node is not the one FarRender knows: far rendering left alone
    Motion m;
    Aim t;
    Surfaces surf;           // its elevons, or a carrier's thrusters (Elevons, Thrusters)
    CarrierState carrier;
    DroneState drone;
    BayState bay;
    ShellState shells;
    Burden burden{1.0f,0.0f};   // what its stores weigh (BurdenOf; JetSteer)
    // A thrown drone (JetLaunchThrown): no carrier, it works round where its bomb landed, taking targets within
    // `reach` of it (0: its kind's range; the carrier's drones reach as far as their carrier sends them), and a
    // charge out of fuel goes off there instead of flying away (jet.cpp Leave).
    bool thrown=false;
    float reach=0.0f;
    int flares=4;               // flare pairs left (jet.cpp NpcFlares)
    ULONGLONG flareAt=0,flareLook=0;   // its last pair; its last look for a missile coming
    PrimerState primer;
    bool cmdMoving=false;       // reach a new map order before taking another target
    Command cmd{};              // a map command (JetCommand, mapcmd.cpp): what it works round instead (jet.cpp JetFrame)
    // A map focus order's target (JetCommand: the enemy the player marked, as a squad's focus takes it): attacked before
    // any other, wherever in the play area, past its order's range and its way to its order's point; let go once it is no
    // longer among the enemies (dead, gone: PickTarget) or another order comes. Its point and its order stay as they were.
    ObjRef focus{};
    // A ferry (JetFerry, transport.cpp: the paratroop plane): kFerryAlt m over its drop point, taking no target at all on
    // the way or after (it carries soldiers, not bombs). Its passes (Ferry, ferry_line.h): along `ferryLine`, the straight
    // line from off the map through the point to off the map past it, either way in turn (`ferryPass`) until its stick is
    // out (JetFerryDone); past the end of that pass `ferryGone`: deleted there (JetFrame reaps it), off the map.
    bool ferry=false,ferryGone=false;
    support::PassLine ferryLine{};
    ferry::Pass ferryPass{};
    unsigned char* Vehicle() const noexcept { return static_cast<unsigned char*>(const_cast<void*>(ref.obj)); }
};
constexpr int kMaxJets=64,kPatrolRings=6;
extern Jet jets[kMaxJets];
inline const Kind& KindOf(const Jet& j) noexcept { return KindOf(j.role); }
// The plugin's drones (drone, blast, doll): no one sits in them. One the plugin launched (Launch: a carrier's, the
// submarine carrier's, a thrown one) is flown by JetFrame with its seats empty (heli.cpp HeliFrame, JetFliesItself):
// since the real crews (2026-10-08) no soldier can walk aboard a drone made in the air, and none is made in it
// (docs/real-npc-crew.md), so a drone that waited for a pilot fell from where it was launched (2026-10-09 log: six
// doll drones driver=0, seats=[-], falling through the ground). Every crewed body comes with its real crew instead
// (support_dispatch.cpp: made inside it, seated at spawn); Launch makes no crewed one.
constexpr bool Unmanned(Role r) noexcept { return r==Role::drone || r==Role::blast || r==Role::doll; }
inline bool FliesItself(const Jet& j) noexcept { return j.launched && Unmanned(j.role); }
inline int IndexOf(const Jet& j) noexcept { return static_cast<int>(&j-jets); }

// Retargeting abandons an old attack but preserves ammunition cooldowns and lifecycle flight modes.
inline void ApplyMapCommand(Jet& j,const Command& cmd,ULONGLONG ms) noexcept {
    j.cmd=cmd;j.cmdMoving=cmd.order!=Order::none;j.focus={};
    j.t.target=nullptr;j.t.trackFrame=0;j.t.lockAt=0;j.t.lockSeen=0;
    j.carrier.stationFor=nullptr;j.carrier.evadeUntil=0;
    if(j.mode!=Mode::takeoff && j.mode!=Mode::withdraw && j.mode!=Mode::recover && j.mode!=Mode::bomb)
        {j.mode=Mode::patrol;j.modeAt=ms;}
}

// A map focus order: `target` attacked first (Jet::focus); the order and the point it works round are kept. The attack it
// flew is abandoned as a new order's is (ApplyMapCommand).
inline void ApplyMapFocus(Jet& j,const ObjRef& target,ULONGLONG ms) noexcept {
    j.focus=target;
    j.t.target=nullptr;j.t.trackFrame=0;j.t.lockAt=0;j.t.lockSeen=0;
    j.carrier.stationFor=nullptr;j.carrier.evadeUntil=0;
    if(j.mode!=Mode::takeoff && j.mode!=Mode::withdraw && j.mode!=Mode::recover && j.mode!=Mode::bomb)
        {j.mode=Mode::patrol;j.modeAt=ms;}
}

// Patrol flies a ring rather than the centre. Arrival must include that ring, including outer wing slots;
// the wider leash also includes its attack's extension so an ordinary pass can finish.
inline bool MapCommandMoving(Jet& j,const float* pos,const float* anchor,float range) noexcept {
    const Kind& k=KindOf(j);
    const float ring=k.patrol+k.patrolStep*static_cast<float>(j.wing%kPatrolRings);
    const float arrive=std::fmax(40.0f,ring+40.0f);
    const float leash=std::fmax(range,std::fmax(arrive+100.0f,k.extendOut));
    return AirCommandTransit(j.cmd,j.cmdMoving,pos,anchor,arrive,leash);
}

// --- Math ---
inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
// Scales `a` to unit length; false (and `a` untouched) when it is too short.
inline bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
inline float HorizDist(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

// --- jet.cpp: the table ---
// Identity (see Jet::ref): the object `r` names is still there (its use count, not deleted). Read under the
// caller's __try.
bool Alive(const ObjRef& r) noexcept;
// The entry flying `v` (the same object), or nullptr.
Jet* FindJet(const unsigned char* v) noexcept;
// A new entry for jet `v` (what it is comes from its mark), or nullptr: not a jet, or kMaxJets entries of live
// jets (logged at most every few seconds; the jet is not taken over).
Jet* NewEntry(unsigned char* v,ULONGLONG ms) noexcept;
bool SlotFree() noexcept;                 // NewEntry would find an entry (logged when not, at most every few seconds)
void JoinFlight(Jet& j,unsigned flight) noexcept;   // its flight and its place in it
bool IsJetVehicle(const unsigned char* v,Role* role,Role* drones) noexcept;
bool HostileJet(const unsigned char* v) noexcept;   // a jet body of the enemy's (BodyRow::hostile)
void SetJetTeam(unsigned char* v,std::int32_t team) noexcept;   // jet_spawn.cpp: SetTeam, registered with the team manager
// Whether jet `o` is flown: in the table and flown within kStaleMs (its position and command are current).
inline bool Flown(const Jet& o,ULONGLONG ms) noexcept { return o.ref && ms-o.seen<=kStaleMs; }

// A called jet's arrival (the user, 2026-10-05: "有些入场情况，可以一开始就进入攻击状态"): launched by a call, kEntryMs
// at most, until its first attack run begins (entered). Strike flies it in at its attack speed and the height it came
// at, turning onto its target, where it used to climb to its attack height and, the target off its nose, fly out up
// to 2.2 km and 12 s first to come round.
constexpr ULONGLONG kEntryMs=15000;
inline bool Entering(const Jet& j,ULONGLONG ms) noexcept { return j.launched && !j.entered && ms-j.bornAt<kEntryMs; }

// --- jet_flight.cpp ---
void SetMode(Jet& j,Mode m,ULONGLONG ms) noexcept;
void Withdraw(Jet& j,const char* why,ULONGLONG ms) noexcept;
// Clearance / Ceiling / the body part / the attitude command are the 506 body's shared tools (body506.h:
// GroundClearance, CeilingY, FixBodyPart506, BodyAttitude), the same for the carrier and the player jets.
void Toward(const float* pos,const float* goal,float* out) noexcept;
void Level(const float* pos,const float* dir,float height,float* out) noexcept;
bool Sense(Jet& j,const float* pos,ULONGLONG ms) noexcept;
bool NearWall(const Jet& j,const float* pos,float range,ULONGLONG ms) noexcept;
// `clear`: GroundClearance(pos) this frame (the frame step has it: no second ray).
void Guard(Jet& j,const float* pos,float clear,float* want,ULONGLONG ms) noexcept;
// The soft edge (airbound.h) a jet of its kind keeps inside: the play edge (crew.h PlayEdge) less its band (ini
// AirSoftEdge / AirSoftTurns: at least that many of its full-speed turn diameters); `band` gets the band's width.
airbound::Box JetSoftBox(const Jet& j,float* band=nullptr) noexcept;
// m/s: the most a wing of kind `k` flies inside a play area of half size `half` (turns that fit it; jet_flight.cpp).
float TightSpeed(const Kind& k,float half) noexcept;
// m: its turn's radius at `speed` with its stores' `mass` (the g its thrust holds, kTurnWide).
float TurnRadiusOf(const Kind& k,float mass,float speed) noexcept;
// The anchor it works round, put inside its soft box less its patrol circle (`room` holds the copy when it moved).
const float* SoftAnchor(const Jet& j,const float* anchor,float* room) noexcept;
void HoldOffGround(Jet& j,const float* pos,float clear,float dt,ULONGLONG ms,float rest=0.0f) noexcept;
float Patrol(const Jet& j,const float* pos,const float* anchor,float height,float* want) noexcept;
// A ferry's passes along its line (Jet::ferry, the paratroop plane; ferry_line.h), `height` over it. Returns the speed to fly
// it at (FerrySpeed: its slowest loiter, the stick jumps over the point); sets ferryGone past its last pass's end.
float Ferry(Jet& j,const float* pos,float height,float* want) noexcept;
// m/s a ferry of kind `k` flies at, and m: its turn's radius there (the room its line's ends keep: support_entry.h PassEnd).
float FerrySpeed(const Kind& k) noexcept;
float FerryTurn(const Kind& k) noexcept;
// `lift`: m/s^2 of vertical acceleration apart from the kind's thrust (hover_lift.h: the player's); 0, one budget (the NPCs').
// `npcGoal`: clamp autonomous goals to the NPC soft band; player control and its hail pass false.
void Hover(Jet& j,const Kind& k,const unsigned char* v,const float* pos,const float* goal,const float* face,float speed,float climb,
           float dt,float lift=0.0f,bool npcGoal=true) noexcept;
// A wing's step toward `want` at `speed`: the path, the body's attitude onto it, its pose (elevons).
void Wing(Jet& j,const Kind& k,unsigned char* v,const float* pos,float clear,const float* nose,float* want,float speed,float dt,
          ULONGLONG ms) noexcept;
// `clear`: the bottom's clearance over the ground (playerjet FloorClear); near it the nacelles keep over it
// (nacelle_reach.h). The NPCs' carriers keep kMinAlt * 2 over the ground: none.
void Thrusters(Jet& j,const Kind& k,unsigned char* v,float dt,ULONGLONG ms,float clear=1e9f) noexcept;
void ResetWalls() noexcept;
constexpr float kHoverClimb=12.0f;     // m/s up or down at the most
constexpr float kHoverLeave=500.0f;    // m: leaving, it heads this far along its way out

// --- jet_combat.cpp ---
// The pilot's seat weapons: guns (straight, fastest round speed for the lead), the homing missile.
// What its weapons are, as their SGOs set them: the guns' speed, drop and reach (AmmoSpeed x AmmoAlive), the
// homing weapons' lock range (LockonRange); rounds left, targets locked. Its stores (stores.h) besides: until
// PickStore the missile counts are every homing weapon's; after it, the one store picked for the target's (pick).
struct Arms {
    float gunSpeed,gunGravity,gunRange,missileRange;
    std::int32_t guns,missiles,locked,bombs;
    bool hasGun,hasMissile;
    Store stores[kMostStores];
    int storeCount,pick;     // pick: the missile store PickStore chose (-1: none, or no stores: the stock fire byte)
    int rocket;              // the rocket store with rounds left (StoreRole::rocket), or -1
};
Arms ReadArms(unsigned char* v) noexcept;
// The missile for the target: of the stores of its kind (air-to-air at a flyer, else air-to-ground) with rounds,
// the one whose lock reaches `dist` with the least to spare (a long-range one far out, a short-range one close in),
// else the longest. The arms' missile counts become that store's.
void PickStore(Arms& a,bool flyer,float dist) noexcept;
// The distance a jet fires its missiles from: its role's standoff, within what its missile locks (0: never).
inline float MissileReach(const Kind& k,const Arms& a) noexcept {
    return k.missileRange<a.missileRange ? k.missileRange : a.missileRange;
}
void Lead(const float* from,const float* aim,const float* tv,const Arms& a,float* out) noexcept;
// The target as the role prefers among the enemies within `range` of `anchor` (the current one counting nearer):
// j.t gets it and its motion, or none.
void PickTarget(Jet& j,unsigned char* v,const float* pos,const float* anchor,float range,float dt,ULONGLONG ms) noexcept;
// The guns' and missiles' attack (Weapon::guns, with a target): `want`, `speed`; whether the guns and the
// missile may fire this frame.
void Attack(Jet& j,const Arms& arms,const float* pos,const float* nose,const float* lead,float height,ULONGLONG ms,float* want,float* speed,
            bool* gunsOk,bool* missileOk) noexcept;
// Whether a jet may use its weapons now (every weapon of every kind: guns, missiles, shells): flown by the
// plugin (JetPilot), a target, and not taking off, going back or leaving.
bool WeaponsFree(const Jet& j) noexcept;
void Fire(Jet& j,unsigned char* v,const float* pos,const float* nose,const float* lead,bool gunsOk,bool missileOk,const Arms& a,
          ULONGLONG ms) noexcept;
void JetLog(const Jet& j,const unsigned char* v,const float* pos,const Arms& a,float speed,float clear,ULONGLONG ms) noexcept;
void ResetTargets() noexcept;

// --- jet_carrier.cpp ---
constexpr float kDockBelow=15.0f;
constexpr float kTriggerHeld=4.0f;
constexpr ULONGLONG kDroneSortieMs=30000,kIdleMs=4000,kKamikazeSortieMs=90000;
Jet* MotherOf(const Jet& d) noexcept;
bool Recover(const Jet& d,const Jet& mother,const float* pos,float* want,float* speed) noexcept;
void Dock(Jet& d,Jet& mother,ULONGLONG ms) noexcept;
void CarrierGoal(Jet& c,const Kind& k,const float* pos,const float* anchor,float height,float hp,float hpMax,ULONGLONG ms,
                 float* goal,float* face,float* speed) noexcept;
void LaunchDrones(Jet& c,const float* pos,const float* nose,ULONGLONG ms) noexcept;
bool OutOfDrones(const Jet& c) noexcept;
void Detonate(Jet& j,Jet* mother,float dist,ULONGLONG ms) noexcept;
void Blast(Jet& j,unsigned char* v,ULONGLONG ms) noexcept;
void DollMake(int i,const unsigned char* v,DWORD lifeSec) noexcept;
void DollFree(int i) noexcept;
void DollsFollow() noexcept;              // once a frame (Sweep): every doll kDollBelow under its drone, whoever moves it
void ResetDolls() noexcept;                // the mission's end: forgotten, not deleted (they went with it)
bool PreloadDolls(void* mgr,bool dollBody) noexcept;   // the dolls' SGOs with the doll drone's body: whether
bool InstallDolls() noexcept;
// The player's carrier (playerjet_board.inc): a drone launched now at `at` (its drones then work round that point, within
// kOrderRange; false: none ready, the gap since the last not passed, the table full), its drones called back (how
// many), its launches left.
constexpr float kOrderRange=400.0f;
bool PlayerLaunchDrone(unsigned char* carrier,const float* at,ULONGLONG ms) noexcept;
int RecallDrones(unsigned char* carrier,ULONGLONG ms) noexcept;
int DronesLeft(const unsigned char* carrier) noexcept;

// --- jet_bay.cpp ---
void BombRun(Jet& j,const float* pos,ULONGLONG ms,float* want,float* speed) noexcept;
void BayFrame(Jet& j,const float* pos) noexcept;
void BayFree(unsigned char*& ifc) noexcept;
void GunshipFire(Jet& j,const unsigned char* v,const float* pos,ULONGLONG ms) noexcept;
bool InstallBay(bool spawnOk) noexcept;
void PreloadShells(void* mgr,bool gunship) noexcept;   // the gunship's shells (with its body), the impact charges
void ResetShells() noexcept;
// The player's aircraft (playerjet_board.inc): a bay's bombs left (0: no bay, or it is open already); the bay opened
// with its first bomb on `at`, the carpet laid along `vel` at its speed (false: none); a frame of the open bay; the
// gunship's shell fired at `at` (false: not ready, out of reach, not preloaded); whether its shells are there at all.
int BayLeft(const unsigned char* v) noexcept;
bool PlayerOpenBay(unsigned char* v,const float* at,const float* vel) noexcept;
void PlayerBayFrame(unsigned char* v,const float* pos) noexcept;
bool PlayerShell(unsigned char* v,const float* at,ULONGLONG ms) noexcept;
bool ShellsReady() noexcept;
// The gunship's crew (playerjet_crew.inc): its NPC gunner's shell under a player pilot (its own target round the
// gunship; false: none, not ready); the gun's wait before the next shell (s, 0: ready); a shell's reach (m).
bool CrewShell(unsigned char* v,float dt,ULONGLONG ms) noexcept;
float ShellWait(const unsigned char* v,ULONGLONG ms) noexcept;
float ShellReach() noexcept;
// Its side guns (jet_bay.cpp kSideGuns: the long-range cannon, the gatling): a round of gun `g` from the player at `at`
// (false: not ready, out of reach, no such gun this mission); whether the gun is there at all (its SGO installed and
// preloaded: an install from before has none); its wait before the next round (s, 0: ready); its reach (m).
bool PlayerSideGun(unsigned char* v,SideGun g,const float* at,ULONGLONG ms) noexcept;
bool SideGunReady(SideGun g) noexcept;
float SideGunWait(const unsigned char* v,SideGun g,ULONGLONG ms) noexcept;
float SideGunReach(SideGun g) noexcept;
// How far from its anchor kind `k` takes targets (its range; a gunship's further with its cannon: jet.cpp PickTarget).
float TargetRange(const Kind& k) noexcept;

// --- jet_spawn.cpp ---
// Rows right, up, forward, position, as BombingPlane_Init builds its matrix (right = up x forward).
void Facing(const float* heading,const float* at,float* m) noexcept;
// Whether body `b`'s SGO was preloaded this mission (PreloadJets): only those are spawned.
bool Preloaded(Body b) noexcept;
// CreateFriend's steps (CreateObject, SetTeam(team), RideAi(true)): the vehicle, or nullptr.
unsigned char* SpawnJet(Body b,const float* m,std::int32_t team=kTeamFriend) noexcept;
unsigned NewFlight() noexcept;   // a flight number no jet has (the Primer creatures')
bool ModFileThere(const wchar_t* file) noexcept;   // Mods/OBJECT (next to the game's exe) holds `file`
// A jet launched now from `source`, flying `b` along `heading` at `speed` to work round `target`: its entry
// (role from its mark), or nullptr (not preloaded, JetPilot off, the game failed to build it, kMaxJets). It starts
// at least `clear` over the ground under `from` (kLaunchClear: a jet's; a thrown drone starts off its bomb's spot).
constexpr float kLaunchClear=100.0f;
Jet* Launch(Body b,const float* from,const float* heading,const float* target,DWORD fuelSec,float speed,const void* source,
            float clear=kLaunchClear) noexcept;
void FarRender(Jet& j,unsigned char* v) noexcept;
// jet.cpp: a jet the player flew or called down handed back to its NPC pilot (playerjet_board.inc), flying at `vel`.
void ResumeNpc(unsigned char* v,const float* vel) noexcept;
// jet.cpp: the entry of one of ours the player boarded, made now if a mission placed it empty (nullptr: none).
Jet* Adopt(unsigned char* v) noexcept;
bool SpawnReady() noexcept;
bool InstallSpawn() noexcept;
bool InstallFarRender() noexcept;
void ResetFlights() noexcept;

// --- primer.cpp ---
inline bool IsPrimer(const Jet& j) noexcept { return j.role==Role::centipede || j.role==Role::dragonfly; }
// Its dummy pilot and then itself on the enemy's team (a vehicle's team is its riders': primer.cpp's head).
void PrimerTeam(unsigned char* v) noexcept;
// A centipede lost the one ahead of it (`front`: it is a front again) or the one behind it, shot down: its head (or
// tail) grows back from now on (primer.cpp). From its own frame, or from Release when the other's entry goes first.
void PrimerUnlinked(Jet& j,bool front,ULONGLONG ms) noexcept;
// A centipede's entry let go of because it was shot down: its body is posed on as it falls (true: the corpse
// took a reference on its control block); each physics step of a dead jet poses its corpse; a new mission
// forgets them.
bool PrimerDied(const Jet& j) noexcept;
void PrimerCorpseStep(unsigned char* v) noexcept;
void ResetCorpses() noexcept;
void HoldRef(const ObjRef& r) noexcept;   // jet.cpp: a weak reference on an object's control block
void DropRef(const ObjRef& r) noexcept;
// A Primer creature's frame (from JetFrame, which has set the stock input aside and the clock).
void PrimerFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept;

// --- jet_hooks.cpp ---
// The read-only copy of who is in which flight the bullets' pass-through reads (any thread): published by
// the game thread once a frame and whenever an entry comes or goes.
void Publish(bool force) noexcept;
bool PassThrough() noexcept;   // a jet's rounds pass through its wingmen (the addBody hook is in)
bool HooksOk() noexcept;       // InstallJets succeeded: jets are flown
}  // namespace jet
}  // namespace crew
