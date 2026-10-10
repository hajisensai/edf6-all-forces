// Helicopter pilot. EDF.dll has no helicopter flight AI (docs/heli-input-re.md): a heli with the
// stock NPC rider in seat 0 just sits there. So for every heli with an NPC pilot (crewed by this plugin
// or spawned with one by the mission), after the
// stock input (slot 55) has filled the input block, we overwrite it:
//   veh+0x1540 lateral  (+ = along heading row 0)     veh+0x1548 forward (+ = along heading row 2)
//   veh+0x1544 throttle (target rotor speed, 0..1)    veh+0x154C 1.0     veh+0x1550 yaw rate
//   byte veh+0x2020 both gatlings, byte veh+0x2021 missile (506 and 409; 410 fires per gunner seat)
// Slot 57 (physics + weapons) consumes them the same frame.
//
// Flight: every mode yields a wanted horizontal velocity, and the stick tracks it (Stick): the orbit
// round a standing player and the escort of a moving one are pure velocity tracking (they never stop,
// so they never brake); landing, holding and the attack station arrive at a point as fast as the heli
// can still stop there (Arrive). The stick works on the heading rows, so its signs are right by
// construction. Altitude = climb-rate loop on the rotor speed (the lift, which lags the throttle by
// seconds) with a throttle loop under it that drives the rotor there. The world sign of yaw was
// not provable statically, so it is learned online from how the heading actually turns.
// When the player stands still for heliLandMs it lands next to them and stays down while they are
// close, so they can walk up and bump the NPC pilot.
// With the player aboard (in a gunner seat) it does not follow: it attacks the enemies around itself.
// Attack: strafing runs (see Engage). It fires only when the 3D nose line is within the fire cone of
// the target's lead point and that is within the guns' reach (the 506 gatling's rounds die at 160 m);
// the 409's turret gun also fires on its own up close (see Loadout). Per type: the 506 (Eros, Heron)
// strafes; the 409 (Nereid) makes rocket runs and circles the target for its turret in between; the 410
// (Brute), whose guns are in its doors, circles the target and its door guns aim and fire (DoorGuns).
// Speed: see Tune.
// Called helis (HeliCalled: the Air Raider's call weapons, airstrike.cpp): a guard heli circles its call's
// marker (its post) Cfg().heliGuardRadius out, Cfg().heliHeight over it (GuardOrbit), and fights what comes
// within heliRange of it, never following nor landing; a follow heli flies like any NPC heli. Its weapons are not refilled: with every round fired,
// its fuel (HeliCalled's fuelSec) gone or below kLeaveHp of its HP it flies off away from the player
// (StartLeave), fighting no more, and is deleted (HeliReap) kGoneFar from them or kLeaveMaxMs after.
// What differs per type (reach, gun range, attack, circle, turret, how it fires) is one row of kHeliTypes.
// Per heli the module keeps a Heli, keyed by its ObjRef (a new object at an old address is a new heli),
// dropped at a new mission (ResetHelis, with the player's track and the rescue) and reused only once its heli
// has not been flown for kStaleMs: a full table takes on no new heli rather than drop a live one.
#include "crew.h"
#include "map_floor.h"
#include "body506.h"
#include "heliaim.h"
#include "airbound.h"
#include "layout.h"
#include "memory.h"
#include "online_authority.h"
#include "npcai.h"
#include "npc_gunner_aim.h"
#include "roundaim.h"
#include "edf/weapon.h"
#include "warn.h"
#include "retired_loadout.h"
#include "support_aircraft.h"
#include "support_call.h"
#include "support_net.h"
#include "hudtext.h"
#include "rescue_logic.h"
#include <cmath>
#include <cstdarg>

namespace crew {
namespace {
// Input block, heading basis, contact byte, rotor speed
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHeadRight=0x15C0,kHeadForward=0x15E0,kContact=0x1580,kRotor=0x1BF8;
constexpr unsigned char kContactGround=2;
// Lock-target registry and team relations (same as EDF6AutoTurret's turret.h)
constexpr std::size_t kRegistry=0x20B2AB0,kRegList=0x8,kNodeTarget=0x10;
constexpr std::size_t kTargetObject=0x8,kTargetAim=0x10,kTargetValid=0x29,kTargetLockable=0x2A;
constexpr std::size_t kTeams=0x20B2978,kTeamArray=0x38,kTeamStride=0x38,kTeamRelation=0x18;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;
constexpr int kMaxNodes=8192;
constexpr float kPi=3.14159265f;
constexpr float kBoardRange=25.0f;   // a landed heli stays down while the player is this close
constexpr float kLandDistance=20.0f; // it lands this far from a player standing still
constexpr float kRotorGain=4.0f;     // throttle per unit the rotor is off the wanted rotor
// The flight controller's gains (once ini HeliMoveGain / HeliBrakeGain / HeliClimbGain / HeliHoverLearn: tuned
// against the logs, not settings to play with): stick per metre off a goal it arrives at (Arrive's settle),
// stick per m/s off the wanted velocity, rotor speed per m/s of climb-rate error, and how fast the rotor speed
// that holds height is learned.
constexpr float kMoveGain=0.04f,kBrakeGain=0.12f,kClimbGain=0.08f,kHoverLearn=0.03f;
constexpr aim::RotorGains kRotorGains{kClimbGain,kHoverLearn,kRotorGain};   // the throttle's (heliaim.h StockThrottle)
// Take-off: no horizontal stick on the ground and for kLiftOffMs after leaving it, so it climbs straight
// up. (With full forward stick before the rotor spun up, a heli slid along the ground into a tank and both
// blew up.)
constexpr ULONGLONG kLiftOffMs=1500;
// The 506 gatling's rounds live 40 frames at 4 m/frame (V_506HELI_GATLING01_*.SGO): 160 m. The 409 and
// 410 guns reach 480 m and 720 m, so for those heliRange is the limit.
constexpr float kGun506Range=150.0f;
constexpr float kBulletSpeed=240.0f; // m/s: a gun's round speed when its weapon cannot be read
// Speed. Slot 57 drives the horizontal velocity towards k * stick along the heading rows, and the nose
// pitches with the forward stick: the logs (14:33-14:45, VEH positions 5 s apart at fwd=1.00) show
// pitch = -35 deg * stick (lagging ~3 s) and the speed creeping up to 16-17 m/s over ~30 s, so
// kTopSpeed is the speed of full stick. Back stick from 15 m/s stops it at 1.5-2 m/s^2; kStopDecel
// leaves margin for the ~1.5 s (kStopLag) it travels before the nose comes up and the brake bites.
constexpr float kTopSpeed=17.0f;
constexpr float kStopDecel=1.2f,kStopLag=1.5f;
// Tune: slot 57 reads the flight params every frame (docs/aircraft-re.md): per frame the horizontal
// velocity goes v = d*(1-b)*v + b*k*stick, so full stick tops out at b*k/(1-d*(1-b)) and gets there with a
// time constant of 1/(1-d*(1-b)) frames (the 506: 18.5 m/s, 12.8 s, so ~1.3 m/s^2 at most: too slow to
// circle anything). With heliSpeed above that, b and k are rewritten for heliSpeed m/s with a
// heliAgility-second time constant, and the brake it plans with scales the same way (kStopShare of
// top/agility: 1.06 m/s^2 for the stock 506, close to the measured kStopDecel). The yaw rate limit is
// raised to heliYawRate where lower, and its smoothing to kTunedYawSmooth.
constexpr std::size_t kDamp=0x1614,kSpeedGain=0x162C,kBlend=0x1630,kMaxYaw=0x1634,kYawSmooth=0x1638;
// The vertical damping (heli_movement [0][2]), the tilt smoothing (vehicle_setup[1][6]) and the rotor's up / down rates
// (heli_roter [1] / [2], R+4 / R+8 of the rotor at +0x1BC8: 0x656744 / 0x656770), its idle (R+0xC, 0x651EE1):
// docs/aircraft-re.md §1.
constexpr std::size_t kVertDamp=0x1618,kTiltSmooth=0x1644,kRotorUp=0x1BCC,kRotorDown=0x1BD0,kRotorIdle=0x1BD4;
constexpr float kTunedYawSmooth=0.005f,kStopShare=0.8f;
// Attack: strafing runs. The guns are fixed along the nose, the nose dips 35 deg * forward stick, and
// steady forward stick s flies the heli at kTopSpeed * s, so the dip and the speed are one control. Hovering
// above the target and dipping just as far as it lies below (the old overwatch) crept along at a few m/s and
// swung the whole airframe after every ant: in the 15:28 round the nose missed by 27-85 deg (median) and
// six bursts were fired. A run keeps the heli heliFireHeight above the target and flies at it at full
// speed; from kRunAim out it aims (the stick holds the dip onto the lead point) and coasts in on its
// momentum, the dip and so the stick growing as it closes. Once the dip passes kMaxDip it breaks off past
// the target, extends kExtend metres out on a slant (kExtendTurn), turns back, and runs in again.
constexpr float kMaxTilt=0.61f;      // rad: nose dip at full forward stick (35 deg in the logs)
constexpr float kPitchGain=1.0f;     // extra dip asked per rad the nose lags the wanted dip (halves the ~3 s lag)
constexpr float kMaxDip=0.52f;       // rad (30 deg): the deepest dip it aims with; deeper, it breaks off
constexpr float kRunAim=160.0f;      // m: it aims from this close in (or the gun range, if less)
constexpr float kExtend=140.0f;      // m: after the break it flies on until this far from the target
constexpr float kExtendTurn=0.7f;    // rad: the extension slants this far off the way it broke
constexpr ULONGLONG kExtendMs=12000; // it turns back in after this long at most
constexpr float kRunStack=5.0f,kRunWing=3.0f;   // m: run height per flight and per wing
constexpr float kAimOff=0.6f;        // rad (35 deg): it aims only with the nose this near the target's bearing
// Engaged, the map must not be in the way (rays, see Avoid). Aiming, the forward stick holds the nose dip,
// so it cannot brake for a wall: a wall within kAimWall along the nose makes it break off instead, and like
// a target hidden behind terrain or a building (the line to it hits the map more than kLosSlack short),
// lifts its run kLosClimb m/s until it sees over (at most kLosMax), easing back down at kLosSink once
// clear. It does not fire at the wall.
constexpr float kAimWall=25.0f,kLosSlack=3.0f,kLosClimb=4.0f,kLosSink=1.5f,kLosMax=40.0f;
// Contact: veh+0x1580 bit 1 is set by any contact whose normal points up, not only the ground: a heli
// sitting on another one has it too, and slot 57 then ignores its tilt, yaw and horizontal input (see
// docs/heli-input-re.md 2a). In the 15:44 round a 506 and a 409 running in on one target at heights 3 m
// apart met at 30 m and hung there together for 45 s. So the runs stack like the formation (kGroupStep
// per flight, kWingLift per wing), targets are shared out (kShareTarget), and a heli in contact more than
// kGroundContact above the ground (a ray) is perched on another body: the higher of the two climbs
// kUnstick metres, the lower one sinks.
constexpr float kWingLift=4.0f,kGroundContact=4.0f,kUnstick=12.0f,kShareTarget=40.0f;
constexpr float kSidestep=6.0f;      // m/s around the target while its burst would pass the player
constexpr float kHitRadius=3.0f;     // m: the cone widens up close so a miss of this much at the target still fires
constexpr float kMissileCone=10.0f;  // deg: the missile homes (LockonType 1), so a rough aim is enough
// The weapons (seat 0's holders, see Loadout). A gun's lead uses its round speed and drop, read from the
// weapon (+0x894 m/frame, gravity factor +0x8E0 of kGravity). The 409's rockets fly straight (no
// LockonType) and accelerate: kRocketStart m/frame plus kRocketAccel per frame (V409 rocket SGO), so
// their lead takes the flight frames of that; they fire only kRocketCone off and within kRocketRange.
// The 409's gun sits in a turret that turns onto enemies within ~kTurretReach by itself (pitch 0..-90:
// below only), so there it fires when the barrel, wherever the nose is, points at the lead point. (It
// fired whenever a target was below in reach, and the turret cannot turn everywhere: 2026-10-03 it
// fired with the target 70-140 degrees off the nose, into the air.)
// (The weapon's offsets, the seat's holders and the vehicle's HP: layout.h.)
constexpr float kGravity=14.7f;      // m/s^2 (measured, autoturret re-notes)
constexpr float kRocketStart=0.5f,kRocketAccel=0.03f,kRocketCone=3.0f,kRocketRange=250.0f;
constexpr float kTurretReach=70.0f;
// No weapon of the helis reloads (ReloadTime -1), so like the AI's ground vehicles (whose _ai weapons do)
// an emptied one is refilled to what it held when first seen, kReloadGunMs / kReloadAltMs after it ran
// dry. Short of that the guns fire kBurstMs bursts with kBurstRest between.
constexpr ULONGLONG kReloadGunMs=8000,kReloadAltMs=15000,kBurstMs=2000,kBurstRest=1000;
constexpr float kMissileMin=50.0f;   // m: no missile closer than this
// The store: an unguided weapon on the missile byte (holder 2, docs/heli-input-re.md 2b) of a nose-gun heli that strafes:
// the N9 Eros Blaze's, Vulture ZA's and ZAM's twin napalm gun (V_506HELI_NAPALM01: a NapalmBullet01 at 3 m/frame,
// AmmoGravityFactor 1, FireAccuracy 0.2 rad, bursts of 6) and the Eros No. 6's napalm drop pod (V_506HELI_UNDER_NAPALM01:
// 0.1 m/frame along FireVector (0,-1,0), AmmoGravityFactor 2, AmmoOwnerMove 1). Until 2026-10-06 it was fired as if it
// were the homing missile: the nose within kMissileCone (10 deg) of the gun's lead (the gatling's 240 m/s straight line,
// no drop) anywhere from kMissileMin to HeliRange. The napalm falls 28 m over 350 m and the cone is 61 m wide there; the
// pod fell under the heli with the target 50-350 m ahead (the user, 2026-10-06: "npc直升机的烧夷弹好像射的非常不准").
// Now its own round is flown (roundaim.h, the bullet core's per-frame step): the nose is aimed so its muzzle's arc meets
// the target where it will be (a pod has no aim: it falls), and the store fires only while the arc from the muzzle as it
// points now passes within what the weapon's own scatter misses by anyway (roundaim::Worth, at least kHitRadius or its
// blast), its scatter at most kStoreSpread there (the napalm gun: 120 m), and not within kStoreClear of the player.
// The weapon's fields: FireVector (weapon+0x350, a float4 the SGO reader writes at 0x68CCFA only when the list is not
// empty: H; fire reads it at 0x691943, its frame there not traced: M, taken in the vehicle's frame, which a level heli
// does not tell apart from the world's) and AmmoExplosion (weapon+0x8B0, written at 0x68D82F: M).
constexpr std::uint64_t kStoreHolder=2;
constexpr std::size_t kWeaponFireVector=0x350,kWeaponExplosion=0x8B0;
constexpr float kStoreSpread=12.0f,kStoreClear=15.0f;
constexpr float kKeepTarget=30.0f;   // m: the current target counts this much nearer (less switching)
// The 410 circles its target (kGunshipRadius) and its door guns pick their own: the target is only the
// circle's centre, so it keeps it unless another is kCircleKeep nearer, and the turn (kTurnCost) does not
// count, as its heading goes round with the circle. With both, its velocity swinging round the circle
// re-ranked the enemies every second or two (2026-10-03: 8 targets in 20 s), each new centre 100 m and
// more off, and it swung its yaw and stick full left and right.
constexpr float kCircleKeep=150.0f;
// Target after target: the one it just broke off from counts kPassed farther for kPassedMs, and every
// target kTurnCost farther per rad its bearing is off the way the heli flies, so after a pass it takes the
// next one ahead. During the extension, a new target within kAhead of the way it flies and far enough out
// to aim at ends the extension at once (the 15:49 round: it flew out and round to come back to one ant).
constexpr float kPassed=200.0f,kTurnCost=50.0f,kAhead=0.87f;
constexpr ULONGLONG kPassedMs=6000;
// (kShareTarget: a target counts this much farther per other heli already on it)
constexpr float kTooClose=1000.0f;   // m: a target too close below to aim at counts this much farther
// Formation: the helis fly in flights of one type each (see FlightOf), so a flight shares one top speed
// and turn. A flight is a V, WingGap metres per place; flights escort kGroupGap apart and hover
// kGroupStep apart in height. All flown helis keep their reaches (Span) plus kClearAir apart, pushed by
// kSeparationGain m/s per metre of overlap (more than the pull onto a slot, so it wins).
// Reach: how far rotor, nose and tail go round the rotor shaft (the MDB bones' extents): the 410's rotor
// is 20 m across and its tail 13.6 m back; the 506's and 409's about 9 m. At 25 m apart (the old gap)
// the 410s' rotors overlapped and the others had 7 m between them (the 17:25 runs: pairs 9-16 m apart).
constexpr float kReach410=14.0f,kReachOther=9.0f,kClearAir=20.0f,kSeparationGain=0.8f;
constexpr float kGroupGap=110.0f,kGroupStep=10.0f;
// With no enemy: while the player travels (see kRoamSpan) the helis escort in a V on their flank,
// kEscortAhead metres forward: the player's velocity plus kSlotGain m/s per metre off the slot (at most
// kSlotCatch). Otherwise (standing, or moving about a small area) they fly an ellipse round where the
// player has been the last kTrackMs, heliFollow metres outside it, at kOrbitSpeed (more on a larger
// one): tangent speed plus kRadialGain m/s per metre off the ellipse; wingmen hold their share of it
// from the leader by speeding up or slowing down kPhaseGain of kOrbitSpeed per rad behind or ahead.
// A standing player's ellipse is a circle heliFollow out.
constexpr ULONGLONG kMovingMs=2000;
// The player's track: a sample every kTrackStepMs over the last kTrackMs (game clock). They travel once
// it spans more than kRoamSpan along its long axis and the net move is kRoamStraight of that span (one
// way, not back and forth); they stop travelling under kRoamKeep of either, or kMovingMs after their
// last step. moveDir is their net move over the last kDirMs.
constexpr ULONGLONG kTrackMs=10000,kTrackStepMs=250,kDirMs=3000;
constexpr int kTrackSize=static_cast<int>(kTrackMs/kTrackStepMs)+1;
constexpr float kRoamSpan=70.0f,kRoamStraight=0.7f,kRoamKeep=0.7f,kEllipseSpeedMax=2.5f;
constexpr float kEscortAhead=15.0f,kSlotGain=0.33f;   // the slot is caught at most at the heli's top speed
constexpr float kOrbitSpeed=10.0f,kRadialGain=0.3f,kPhaseGain=1.0f,kPhaseMax=0.5f;
constexpr float kFaceSpeed=3.0f;     // m/s: slower than this it faces the player instead of the way it flies
// Yaw input per rad/s of turn rate. Full yaw turns about 45-70 deg/s and the turn lags the input by about
// 0.8 s (the logs: 0.6 overshot by 38 deg), so it has to start easing off about 36 deg early.
// On a moving target the damping alone holds the nose rate/1.5 rad behind it (8 deg for an ant crossing
// 100 m out at 10 m/s, the 14:42 runs' |off| of 5-15 deg), so the target's bearing rate is fed forward:
// damping acts on the rate relative to it, plus kYawFeed input per rad/s of it.
constexpr float kYawDamp=1.2f,kYawFeed=1.1f;
// The 409 (Nereid): rocket runs, and after each break (or while its rockets are spent) kTurretCircleMs
// circling the target kTurretRadius out and kTurretHeight above at kTurretSpeed, where its turret gun
// (below only, kTurretReach) has it; then it extends and runs in again.
constexpr float kTurretRadius=40.0f,kTurretHeight=20.0f,kTurretSpeed=8.0f;
constexpr ULONGLONG kTurretCircleMs=10000;
// The 410 (Brute) has no forward gun, only the two door guns: it circles the target kGunshipRadius out
// and kGunshipHeight above at kGunshipSpeed, nose along its path, so the gun on the inside of the turn
// bears (a pylon turn). Its guns reach 720 m (V_410HELI_GATLING01: 6 m/frame for 120 frames).
constexpr float kGunshipRadius=90.0f,kGunshipHeight=40.0f,kGunshipSpeed=10.0f;

// The helicopter types, one row per vtable (IsHelicopter: a vtable in this table; crew.cpp kClasses crews the
// same layout.h vtables): its reach (see kReach410), its forward guns' reach (0: heliRange), how it attacks (the
// 506 strafes, the 409 makes rocket runs and circles for its turret, the 410 circles for its door guns), the
// circle it flies round a target, how far below it its turret gun fires on its own (0: no turret), and how it
// fires (the nose bytes veh+0x2020/0x2021, or the 410's door gun blocks).
enum class Attack { strafe, rocketRun, gunship };
enum class Guns { nose, doors };
struct HeliType {
    unsigned vtable; const char* name;
    float reach,gunRange;
    Attack attack;
    float circleRadius,circleHeight,circleSpeed,turretReach;
    Guns guns;
};
const HeliType kHeliTypes[]={
    {kVt506,"506",kReachOther,kGun506Range,Attack::strafe,0.0f,0.0f,0.0f,0.0f,Guns::nose},
    {kVt409,"409",kReachOther,0.0f,Attack::rocketRun,kTurretRadius,kTurretHeight,kTurretSpeed,kTurretReach,Guns::nose},
    {kVt410,"410",kReach410,0.0f,Attack::gunship,kGunshipRadius,kGunshipHeight,kGunshipSpeed,0.0f,Guns::doors},
    {kVtHeliBase,"base",kReachOther,0.0f,Attack::strafe,0.0f,0.0f,0.0f,0.0f,Guns::nose},
};
const HeliType* TypeOf(const void* vehicle) noexcept {
    if(!Readable(vehicle,8))return nullptr;
    const auto vtable=At<const unsigned char*>(vehicle,0);
    for(const auto& t:kHeliTypes)if(vtable==image+t.vtable)return &t;
    return nullptr;
}

// Two references to one object (both taken from it while it lived).
bool Same(const ObjRef& a,const ObjRef& b) noexcept { return a.obj==b.obj && a.ctrl==b.ctrl; }

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kHeliSignatures[]={
    {0x6543A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48},16},   // base input
    {0x61B8F0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x20,0x48},16},   // 506 input
    {0x65451A,{0x4C,0x89,0xB7,0x40,0x15,0x00,0x00,0x44,0x89,0xB7,0x48,0x15,0x00,0x00,0x48,0xC7},16},   // input block reset
    {0x61B71E,{0x80,0xBB,0x20,0x20,0x00,0x00,0x00},7},                                                 // 506 gatlings read
    {0x61B743,{0x80,0xBB,0x21,0x20,0x00,0x00,0x00},7},                                                 // 506 missile read
};
bool profileOk=false;
bool deleteOk=false;   // HeliReap may delete with the game's Delete (CheckHeliProfile)

// Obstacle avoidance casts rays at the map: terrain and buildings, never units (docs/raycast-re.md).
// EDF.dll's wrapper of the Havok ray cast takes the world wrapper *(image+kHavokGlobal)+0x10, the ray
// (filter 0x16: layer 22, the game's map layer) and a stack collector: count at +0x0C, point at +0x30,
// fraction of the segment at +0x50, flags at +0x9C. Layer 22 also meets other bodies (the 15:06 round:
// each heli's roof probe hit its wingman, and the pair climbed each other to 90 m), so like the game's
// own internal_GetGroundPosition it uses that function's collector, whose addHit (kGroundAdd) keeps only
// hits with flags & 2 and lets the ray go on past the rest. kHitVtbl, the plain nearest-hit collector,
// only feeds the log (what the filter skipped).
constexpr std::size_t kHavokGlobal=0x20B2958,kCastRay=0x11A7EE0,kHitVtbl=0x1768B78,kHitReset=0xFDF00;
constexpr std::size_t kHitSlot0=0x978880,kHitAdd=0xD93980,kGroundVtbl=0x179CBE8,kGroundAdd=0x208850;
const Signature kRaySignatures[]={
    {kCastRay,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0x05,0x66,0x71},16},
    {kHitReset,{0x33,0xD2,0xB8,0xFF,0xFF,0x00,0x00,0x89,0x51,0x0C,0x0F,0x28,0x05,0x1F,0x4B,0xE8},16},
    {kHitAdd,{0xF3,0x0F,0x10,0x4A,0x20,0x0F,0x10,0x41,0x10,0x0F,0xC6,0xC9,0x00,0x0F,0x2E,0xC1},16},
    // test byte [rdx+0x6C],2; jne kHitAdd; ret
    {kGroundAdd,{0xF6,0x42,0x6C,0x02,0x0F,0x85,0x26,0xB1,0xB8,0x00,0xC3},11},
};
bool rayOk=false;
struct alignas(16) RayInput { float from[4],to[4]; std::uint32_t filter,unk24; std::uint64_t pad; };
static_assert(sizeof(RayInput)==0x30,"EdfRayInput");
struct alignas(16) RayHits { unsigned char raw[0xA0]; };

// Metres along a->b to the nearest terrain/building, or -1 with none (or no physics world). `hit`
// receives the point. `any`: the nearest hit of any kind instead (log only); `flags` gets its flags.
// `filter`: the ray's collision filter (its layer; kMapLayer the game's map ray).
constexpr std::uint32_t kMapLayer=0x16;
// `normal`: the hit's normal (collector +0x40; whose side it faces: map_floor.h Learn).
float CastRay(const float* a,const float* b,float* hit=nullptr,bool any=false,std::uint32_t* flags=nullptr,
              std::uint32_t filter=kMapLayer,float* normal=nullptr) noexcept {
    if(!rayOk)return -1.0f;
    const auto g=At<unsigned char*>(image,kHavokGlobal);
    if(!Readable(g,0x70) || !At<const void*>(g,0x68))return -1.0f;
    const RayInput in{{a[0],a[1],a[2],1.0f},{b[0],b[1],b[2],1.0f},filter,0,0};
    RayHits col{};
    *reinterpret_cast<const void**>(col.raw)=image+(any ? kHitVtbl : kGroundVtbl);
    reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
    reinterpret_cast<void(*)(void*,void*,const RayInput*)>(image+kCastRay)(g+0x10,&col,&in);
    if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)return -1.0f;
    const float f=*reinterpret_cast<const float*>(col.raw+0x50);
    if(!std::isfinite(f) || f<0.0f || f>1.0f)return -1.0f;
    if(hit)std::memcpy(hit,col.raw+0x30,12);
    if(flags)std::memcpy(flags,col.raw+0x9C,4);
    if(normal)std::memcpy(normal,col.raw+0x40,12);
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return f*std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

// Water (docs/water-re.md): not map geometry (a layer-22 ray over the sea finds the seabed) but trigger
// bodies on layer 25 that the map load makes, one per water area, kept in MoveAreaManager's std::list at
// mgr+0x30 (mgr = *(image+kMoveAreas)-8; a node: +0 next, +0x10 the area; the area's body id at
// *(area+0x58)+0xF0). Each frame the game finds a soldier's water surface with a vertical ray cast against
// that one body (kCastRayBodies: wrapper, collector, ray with filter 0, body ids, count); the plugin asks
// the same way. kWaterSignatures: that function's entry and the manager getter 0x11BD90 (mov rax,[rip ->
// kMoveAreas]; test; jz; add rax,-8).
constexpr std::size_t kMoveAreas=0x20B2998,kCastRayBodies=0x11A7480;
const Signature kWaterSignatures[]={
    {kCastRayBodies,{0x4C,0x89,0x4C,0x24,0x20,0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48},16},
    {0x11BD90,{0x48,0x8B,0x05,0x01,0x6C,0xF9,0x01,0x48,0x85,0xC0,0x74,0x05,0x48,0x83,0xC0,0xF8},16},
};
bool waterOk=false;

// One play's rays settle the unverified parts (the list's layout at run time, the surface heights): each
// different answer is logged once.
void LogSea(Sea sea,std::size_t areas,float y) noexcept {
    static Sea lastSea=Sea::unknown;static std::size_t lastAreas=~std::size_t{0};static float lastY=0.0f;
    if(sea==lastSea && areas==lastAreas && std::fabs(y-lastY)<0.5f)return;
    lastSea=sea;lastAreas=areas;lastY=y;
    Log("WATER %s: %zu water area(s) on the map%s%.1f",sea==Sea::water ? "sea" : sea==Sea::land ? "no water here" : "unknown",
        areas,sea==Sea::water ? ", surface y=" : "",sea==Sea::water ? y : 0.0f);
}

Sea SeaProbe(float x,float z,float* surface) noexcept {
    if(!rayOk || !waterOk)return Sea::unknown;
    __try {
        const auto g=At<unsigned char*>(image,kHavokGlobal);
        const auto top=At<unsigned char*>(image,kMoveAreas);
        if(!Readable(g,0x70) || !At<const void*>(g,0x68) || !top)return Sea::unknown;
        const auto mgr=top-8;
        if(!Readable(mgr+0x30,0x10))return Sea::unknown;
        const auto head=At<unsigned char*>(mgr,0x30);
        const auto count=At<std::uint64_t>(mgr,0x38);
        if(!Readable(head,8) || count>256)return Sea::unknown;
        bool found=false;float best=0.0f;
        std::uint64_t n=0;
        for(auto node=At<unsigned char*>(head,0);node!=head && n<count;node=At<unsigned char*>(node,0),++n) {
            if(!Readable(node,0x18))return Sea::unknown;
            const auto area=At<unsigned char*>(node,0x10);
            if(!Readable(area,0x60))continue;
            const auto holder=At<unsigned char*>(area,0x58);
            if(!Readable(holder,0xF4))continue;
            const std::uint32_t id=At<std::uint32_t>(holder,0xF0);
            const RayInput in{{x,4000.0f,z,1.0f},{x,-4000.0f,z,1.0f},0,0,0};
            RayHits col{};
            *reinterpret_cast<const void**>(col.raw)=image+kHitVtbl;
            reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
            reinterpret_cast<void(*)(void*,void*,const RayInput*,const std::uint32_t*,int)>(image+kCastRayBodies)(g+0x10,&col,&in,&id,1);
            if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)continue;
            const float y=*reinterpret_cast<const float*>(col.raw+0x34);
            if(!std::isfinite(y))continue;
            if(!found || y>best)best=y;
            found=true;
        }
        if(n!=count)return Sea::unknown;
        const Sea sea=found ? Sea::water : Sea::land;
        LogSea(sea,static_cast<std::size_t>(count),best);
        if(found && surface)*surface=best;
        return sea;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Sea::unknown;}
}

// One 410 door gun as DoorGun drives it (see there). Its weapon is known by its address and its holder's control
// block (identity only: never read through again); `ammo` is what DoorGun read through the trigger in frame
// `ammoFrame`.
struct Door {
    const unsigned char* weapon;
    const void* weaponCtrl;
    std::int32_t ammo;
    ULONGLONG ammoFrame;
    ObjRef target;
    float tgtPrev[3],tgtVel[3];
    float sign[2],axisPrev[2],barrelPrev[2];   // learned axis-vs-geometric sign per axis, and last frame's
    bool prevValid;
    float k[2],in[2];     // learned rad per frame per unit input, and the input given last frame
    ULONGLONG stuckAt[2];
    bool firing;
    std::int32_t full;
    ULONGLONG emptyAt,loggedAt;
};

struct Heli {
    ObjRef ref;
    const HeliType* type; // what its flight is made of
    ULONGLONG crewedAt,seen,loggedAt,missileAt,targetAt,groundAt;
    ULONGLONG seenFrame;  // the game frame it was last flown in (HeliReap deletes only a heli flown just now)
    LARGE_INTEGER last;
    float prev[3],vel[3];
    float hover;          // learned rotor speed that holds height
    float prevHeading,lastYaw,yawRate;
    int yawSign,votes;    // +1: a positive yaw input increases atan2(fwd.x, fwd.z)
    bool yawLocked,started;
    ObjRef target;
    float hold[3];        // where it holds when it has nobody to follow
    float pos[3];         // last position, for the other helis' formation and separation
    bool extend;          // engaged: broke off a run, flying out to turn back in (see Engage)
    ULONGLONG extendAt;   // when it broke off
    float extendTo[3];    // where it extends to
    struct Arm { unsigned char* weapon; std::int32_t full; ULONGLONG emptyAt; } arms[4];   // weapon: identity only
    ULONGLONG burstAt,restUntil;   // the gun's current burst began / it rests until
    std::int32_t storeAmmo;        // the store's rounds last frame, and when a burst of it last left (see kStoreHolder)
    ULONGLONG storeShotAt;
    ObjRef passed;        // the target it last broke off from (see kPassedMs)
    ULONGLONG passedUntil;
    bool firing;
    float losLift;        // metres its engaged hover is raised to see over the map (see kAimWall)
    ObjRef tracked;       // the target tgtPrev/tgtVel belong to
    float tgtPrev[3],tgtVel[3];
    ULONGLONG playerAt;   // the player fix pVel was last updated from
    ULONGLONG enemyAt;    // game ms: an enemy was last within heliRange of whom it follows
    float pPrev[3],pVel[3];
    float top,stopDecel;  // m/s at full stick, and the braking it plans with (see Tune)
    bool edgeOut;         // past its soft edge (SoftEdge): logged once each way
    bool tuned;           // Fly writes params (k, b, max yaw, yaw smoothing) every frame
    float params[4];
    float stock[4];       // ...and the heli's own (Tune read them): put back when its NPC stops flying it (Restore)
    bool applied;         // params are on the heli now
    ULONGLONG circleUntil;// 409: circling for its turret until then (see kTurretCircleMs)
    Door doors[2];        // 410: left, right
    bool medic;           // its door guns heal (Medic): it aims at hurt friends, never at enemies
    // A called heli (HeliCalled): its post (guard), when its sortie ends (game ms), leaving since leftAt, and
    // deleted from another object's update once reap is set (HeliReap).
    bool called,guard,leaving,reap;
    float post[3];
    ULONGLONG leaveAt,leftAt;
    // A guard heli's orbit (GuardOrbit): the centre it flies round (eased toward where it should be), whether
    // it orbits now (for the log of each change), and its last orbit log.
    float orbitCentre[3];
    bool orbitSet,orbiting;
    ULONGLONG orbitLogAt;
    // A map command (HeliCommand, mapcmd.cpp) is the same post as a call's: guard / post / hold written as HeliCalled
    // writes them; what they were before the first command kept here to put back on its release.
    Command cmd;
    bool cmdMoving;
    bool ownGuard;
    float ownPost[3],ownHold[3];
    // A map focus order's target (HeliCommand: the enemy the player marked): engaged before any other, wherever it is,
    // its post and order kept; let go once it is no longer among its targets (PickTarget) or another order comes.
    ObjRef focus;
    // A ferry (HeliFerry, transport.cpp: carrying a squad): to ferryAt, HeliHeight over it (hold), and with ferryLand down
    // on it (Mode::land there, staying down); it follows nobody and engages nothing meanwhile. `grounded`: last frame's.
    bool ferry,ferryLand,grounded;
    float ferryAt[3];
    // A squad's transport (HeliKeep, transport.cpp): it stays theirs until WITHDRAW; out of fuel or ammo (its door guns
    // are the squad's to fire) it does not leave on its own, only badly damaged.
    bool keep;
};
Heli helis[16]{};
constexpr float kFerryLandNear=80.0f;   // m (level) from its landing point a ferry starts down
constexpr ULONGLONG kStaleMs=2000;   // a heli flown every frame; one not flown this long is gone (or not NPC-flown)
constexpr ULONGLONG kAliveFrames=8;  // frames since a heli was last flown that still count as now (HeliReap, the rescue)
ULONGLONG fullLoggedAt=0;
// The player's last move: they count as standing still once within 3 m of `still` since `stillAt`
// (game clock: a pause does not count as standing still).
// moveDir: see kDirMs.
float still[3]{},moveDir[3]{0,0,1};ULONGLONG stillAt=0;
// The player's position at the last track step: a jump past kTeleport from it (a teleport, a respawn, the
// next mission's start) starts the track afresh instead of reading a flight across the map into it.
constexpr float kTeleport=60.0f;
float trackLast[3]{};bool trackLastSet=false;

// Where the player has been (see kTrackMs), and the ellipse's area round it: centre (x, z), unit long
// axis u and w (u turned a right angle), the half spans along both, and the net move over the track.
struct TrackPoint { float x,z; ULONGLONG at; };
TrackPoint track[kTrackSize]{};int trackHead=0,trackCount=0;
struct Area { float centre[2],u[2],w[2],halfU,halfW,net; };
Area area{{0,0},{0,1},{1,0},0,0,0};
bool roaming=false;   // the player travels: the helis escort (else they fly the ellipse)

const TrackPoint& TrackAt(int back) noexcept { return track[(trackHead-1-back+2*kTrackSize)%kTrackSize]; }

// The player's track and everything made from it, as before the first step.
void ResetTrack() noexcept {
    std::memset(still,0,sizeof(still));stillAt=0;
    moveDir[0]=0.0f;moveDir[1]=0.0f;moveDir[2]=1.0f;
    trackHead=trackCount=0;
    area=Area{{0,0},{0,1},{1,0},0,0,0};
    roaming=false;trackLastSet=false;
}

// The area of the track: the long axis from the samples' covariance, centred on their extent.
Area AreaOf() noexcept {
    Area a{{player.pos[0],player.pos[2]},{moveDir[0],moveDir[2]},{moveDir[2],-moveDir[0]},0,0,0};
    if(trackCount<2)return a;
    float mx=0,mz=0;
    for(int i=0;i<trackCount;++i){mx+=TrackAt(i).x;mz+=TrackAt(i).z;}
    mx/=static_cast<float>(trackCount);mz/=static_cast<float>(trackCount);
    float cxx=0,cxz=0,czz=0;
    for(int i=0;i<trackCount;++i){const float dx=TrackAt(i).x-mx,dz=TrackAt(i).z-mz;cxx+=dx*dx;cxz+=dx*dz;czz+=dz*dz;}
    const float axis=0.5f*std::atan2(2.0f*cxz,cxx-czz);   // the major axis, from x toward z
    a.u[0]=std::cos(axis);a.u[1]=std::sin(axis);a.w[0]=a.u[1];a.w[1]=-a.u[0];
    float lo[2]={1e9f,1e9f},hi[2]={-1e9f,-1e9f};
    for(int i=0;i<trackCount;++i) {
        const float dx=TrackAt(i).x-mx,dz=TrackAt(i).z-mz;
        const float p[2]={dx*a.u[0]+dz*a.u[1],dx*a.w[0]+dz*a.w[1]};
        for(int k=0;k<2;++k){lo[k]=p[k]<lo[k] ? p[k] : lo[k];hi[k]=p[k]>hi[k] ? p[k] : hi[k];}
    }
    const float cu=0.5f*(lo[0]+hi[0]),cw=0.5f*(lo[1]+hi[1]);
    a.centre[0]=mx+a.u[0]*cu+a.w[0]*cw;a.centre[1]=mz+a.u[1]*cu+a.w[1]*cw;
    a.halfU=0.5f*(hi[0]-lo[0]);a.halfW=0.5f*(hi[1]-lo[1]);
    const TrackPoint& oldest=TrackAt(trackCount-1);
    a.net=std::sqrt((player.pos[0]-oldest.x)*(player.pos[0]-oldest.x)+(player.pos[2]-oldest.z)*(player.pos[2]-oldest.z));
    return a;
}

// The player's last step (stillAt), their track every kTrackStepMs, and from it moveDir, the area and
// whether they travel (see kRoamSpan).
void TrackPlayerStill() noexcept {
    const ULONGLONG ms=GameMs();
    const float j[3]={player.pos[0]-trackLast[0],player.pos[1]-trackLast[1],player.pos[2]-trackLast[2]};
    if(trackLastSet && j[0]*j[0]+j[1]*j[1]+j[2]*j[2]>kTeleport*kTeleport) {
        Log("HELI player track restarted: the player moved %.0f m at once",std::sqrt(j[0]*j[0]+j[1]*j[1]+j[2]*j[2]));
        ResetTrack();
    }
    std::memcpy(trackLast,player.pos,12);trackLastSet=true;
    const float d[3]={player.pos[0]-still[0],player.pos[1]-still[1],player.pos[2]-still[2]};
    if(!stillAt || d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>9.0f){std::memcpy(still,player.pos,12);stillAt=ms;}
    if(trackCount && ms-TrackAt(0).at<kTrackStepMs)return;
    while(trackCount && ms-TrackAt(trackCount-1).at>kTrackMs)--trackCount;
    track[trackHead]={player.pos[0],player.pos[2],ms};
    trackHead=(trackHead+1)%kTrackSize;
    if(trackCount<kTrackSize)++trackCount;
    const int back=static_cast<int>(kDirMs/kTrackStepMs)<trackCount-1 ? static_cast<int>(kDirMs/kTrackStepMs) : trackCount-1;
    const float mx=player.pos[0]-TrackAt(back).x,mz=player.pos[2]-TrackAt(back).z,len=std::sqrt(mx*mx+mz*mz);
    if(len>3.0f){moveDir[0]=mx/len;moveDir[1]=0;moveDir[2]=mz/len;}
    area=AreaOf();
    const float span=2.0f*area.halfU,keep=roaming ? kRoamKeep : 1.0f;
    roaming=ms-stillAt<kMovingMs && span>kRoamSpan*keep && area.net>span*kRoamStraight*keep;
}

Heli* Find(const void* vehicle) noexcept {
    for(auto& h:helis)if(h.ref.Is(vehicle))return &h;
    return nullptr;
}

float Dot2(const float* a,const float* b) noexcept { return a[0]*b[0]+a[2]*b[2]; }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
float Wrap(float a) noexcept { while(a>kPi)a-=2*kPi; while(a<-kPi)a+=2*kPi; return a; }

// Scales the horizontal part of `v` down to at most `limit` long.
void Limit2(float* v,float limit) noexcept {
    const float len=std::sqrt(Dot2(v,v));
    if(len>limit){v[0]*=limit/len;v[2]*=limit/len;}
}

// A horizontal unit vector from the heading basis row at `offset`, or false.
bool Row(const unsigned char* v,std::size_t offset,float* out) noexcept {
    const float* r=reinterpret_cast<const float*>(v+offset);
    const float len=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(!std::isfinite(len) || len<0.1f)return false;
    out[0]=r[0]/len;out[1]=0;out[2]=r[2]/len;
    return true;
}

const std::int32_t* Relations(std::int32_t team) noexcept {
    if(team<0 || team>=kMaxTeam)return nullptr;
    const auto manager=At<const unsigned char*>(image,kTeams);
    if(!Readable(manager,kTeamArray+8))return nullptr;
    const auto rows=At<const unsigned char*>(manager,kTeamArray);
    if(!Readable(rows+team*kTeamStride,kTeamStride))return nullptr;
    const auto relation=At<const std::int32_t*>(rows+team*kTeamStride,kTeamRelation);
    return Readable(relation,kMaxTeam*4) ? relation : nullptr;
}

// The horizontal distance below which a target sits too steeply under a heli heliFireHeight above it.
float MinAimHoriz() noexcept { return Cfg().heliFireHeight/std::tan(kMaxDip); }

// Calls f(object, lock point) for every valid, lockable lock point of an enemy of `v`'s side (a vehicle
// nobody owns fights the player's enemies); an object can own several. False when the lock registry or
// the team relations cannot be read.
template<class F> bool ForEachEnemyOf(std::int32_t team,const void* v,F&& f) noexcept {
    if(team==kTeamVehicle)team=player.team;
    const auto relation=Relations(team);
    const auto registry=At<const unsigned char*>(image,kRegistry);
    if(!relation || !Readable(registry,kRegList+0x10))return false;
    const auto head=At<const unsigned char*>(registry,kRegList);
    if(!Readable(head,0x10))return false;
    int n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<kMaxNodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid] || !target[kTargetLockable])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object==v || object[kDead])continue;
        const auto other=At<std::int32_t>(object,kTeam);
        if(other<0 || other>=kMaxTeam || relation[other]!=kEnemyRelation)continue;
        const float* a=reinterpret_cast<const float*>(target+kTargetAim);
        if(!std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2]))continue;
        f(static_cast<const void*>(object),a);
    }
    return true;
}
template<class F> bool ForEachEnemy(const unsigned char* v,F&& f) noexcept {
    return ForEachEnemyOf(At<std::int32_t>(v,kTeam),v,static_cast<F&&>(f));
}

// ---- The medic heli (tools/make_jets.py MEDIC_HELI_FILE; the user, 2026-10-06: 「增加救护直升机，射的子弹射到队友会回血，
// 自瞄也是锁队友」) ----
// A heli whose door gun heals (its AmmoDamage, weapon +0x89C, negative: the stock Reverser's way) aims at hurt friends.
// Its PlasmaBullet blast also needs the native friendly-damage permission, restored before shooting below. It aims at feet
// and never at enemies: its pilot circles the most hurt one in its range (PickTarget: as a 410 circles an enemy), its
// gunners shoot the hurt friends they reach (DoorGun) and hold their fire while an enemy is near the line (the round would
// hit it first: a heal for the enemy, or nothing; not checked which).
// Friends: the soldiers (the four classes, the player's and the NPCs') of every team friendly to the heli's, through the
// game's own walk 0x5E11D0(team manager, team, functor): under the manager's lock (EnterCriticalSection: reentrant), slot 1
// (functor, object) for each object of each team whose relation to `team` is 1 (the board button's walk, docs/rescue-re.md).
// On foot only (a soldier in a vehicle takes no rounds, the vehicle does), alive, below kHurt of its HP (GameObjectBase
// +0x2F4 / +0x2F8, as a vehicle's). Walked once a frame for every medic (FriendsNow).
constexpr std::size_t kWeaponDamage=0x89C;   // AmmoDamage (the SGO reader 0x68A920 at 0x68D6E3, docs/boarding-re.md)
constexpr unsigned kVisitFriends=0x5E11D0;
const unsigned char kVisitFriendsSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr float kHurt=0.97f;        // below this share of its HP a friend is hurt
constexpr float kAimOver=0.3f;      // m over a soldier's origin (its feet) the gunners aim: the healing round bursts there
constexpr float kHurtWeight=200.0f; // m a friend counts farther per share of its HP it still has (the most hurt first)
constexpr float kEnemyClear=5.0f;   // m: no healing round passes this near an enemy's lock point (plus the round's blast)
constexpr std::size_t kWeaponBlast=0x8B0;   // AmmoExplosion, the blast radius (0x68D82F, docs/heli-input-re.md)
constexpr int kMaxFriends=64;
bool visitOk=false;                  // 0x5E11D0 is the walk read above (CheckHeliProfile)

struct Friend { const void* object; float aim[3]; float share; };
struct FriendList { Friend f[kMaxFriends]; int n; };
struct FriendVisitor { const void* const* vtable; FriendList* list; };
using VisitFriendsFn=void(__fastcall*)(void*,std::int32_t,FriendVisitor*);

bool IsSoldier(const unsigned char* o) noexcept {
    if(!Readable(o,8))return false;
    const auto vt=At<const unsigned char*>(o,0);
    for(const auto r:kSoldierVts)if(vt==image+r)return true;
    return false;
}

void AddFriend(FriendList& l,const unsigned char* o) noexcept {
    if(l.n>=kMaxFriends || !IsSoldier(o) || !Readable(o,kHumanVehicleCtrl+8) || o[kDead])return;
    const auto ride=At<const unsigned char*>(o,kHumanVehicleCtrl);
    if(ride && Readable(ride,0x10) && At<std::int32_t>(ride,8)!=0)return;   // in a vehicle (0x56D700's own test)
    const float hp=At<float>(o,kHp),max=At<float>(o,kHpMax);
    if(!std::isfinite(hp) || !std::isfinite(max) || !(max>0.0f) || !(hp>0.0f) || hp>=max*kHurt)return;
    const float* p=reinterpret_cast<const float*>(o+kPosition);
    if(!std::isfinite(p[0]+p[1]+p[2]))return;
    Friend& f=l.f[l.n++];
    f.object=o;f.aim[0]=p[0];f.aim[1]=p[1]+kAimOver;f.aim[2]=p[2];f.share=hp/max;
}

void __fastcall VisitorNone(FriendVisitor*) noexcept {}
void __fastcall VisitFriend(FriendVisitor* self,const unsigned char* object) noexcept {
    __try { AddFriend(*self->list,object); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
const void* const kFriendVisitorVtable[]={reinterpret_cast<const void*>(&VisitorNone),reinterpret_cast<const void*>(&VisitFriend)};

struct FriendsCache { ULONGLONG frame; std::int32_t team; bool ok; FriendList list; };
FriendsCache friendsCache{};

// This frame's hurt friends of `team` (walked once a frame per team), or nullptr when the walk is not there.
const FriendList* FriendsNow(std::int32_t team) noexcept {
    if(!visitOk || team<0 || team>=kMaxTeam)return nullptr;
    const ULONGLONG frame=GameFrame();
    if(friendsCache.ok && friendsCache.frame==frame && friendsCache.team==team)return &friendsCache.list;
    friendsCache.ok=false;friendsCache.frame=frame;friendsCache.team=team;friendsCache.list.n=0;
    const auto teams=At<void*>(image,kTeams);
    if(!teams)return nullptr;
    FriendVisitor visitor{kFriendVisitorVtable,&friendsCache.list};
    reinterpret_cast<VisitFriendsFn>(image+kVisitFriends)(teams,team,&visitor);
    friendsCache.ok=true;
    return &friendsCache.list;
}

// f(object, aim point, share of its HP) for every hurt friend of `v`'s side (a vehicle nobody owns: the player's).
template<class F> bool ForEachHurtFriend(const unsigned char* v,F&& f) noexcept {
    std::int32_t team=At<std::int32_t>(v,kTeam);
    if(team==kTeamVehicle)team=player.team;
    const FriendList* l=FriendsNow(team);
    if(!l)return false;
    for(int i=0;i<l->n;++i)f(l->f[i].object,l->f[i].aim,l->f[i].share);
    return true;
}

// Whether `weapon`'s rounds heal (a negative AmmoDamage).
bool HealingGun(const unsigned char* weapon) noexcept {
    if(!Readable(weapon,kWeaponDamage+4))return false;
    const float d=At<float>(weapon,kWeaponDamage);
    return std::isfinite(d) && d<0.0f;
}

constexpr std::size_t kWeaponFriendlyDamage=0x8B6;
constexpr unsigned kMedicShot=0x696FD0;
// Complete 15-byte prologue: mov rax,rsp; eight pushes, no relative instructions.
constexpr unsigned char kMedicShotSig[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
using MedicShotFn=void(__fastcall*)(unsigned char*,unsigned,void*,int*,bool);
MedicShotFn medicShotNext=nullptr;

void RestoreMedicPermission(unsigned char* weapon) noexcept {
    if(!HealingGun(weapon) || !Readable(weapon+kWeaponFriendlyDamage,1,true))return;
    const auto owner=At<const unsigned char*>(weapon,0x120);
    if(Readable(owner,8) && At<const unsigned char*>(owner,0)==image+kVt410)
        weapon[kWeaponFriendlyDamage]=1;
}

void __fastcall MedicShotHook(unsigned char* weapon,unsigned muzzle,void* overrideParam,int* counter,bool replay) {
    // RideAi 0x6330C9 and later script setup 0x632DA0 clear +8B6 even on healing guns. Both the blast filter
    // and HP handler need its GDI bit 0x20. The common local/replay shot entry repairs it before parameter copying.
    // Weapon semantics are independent of AI/aim settings and network ownership; positive guns stay untouched.
    __try {
        // Old requests can survive a DLL-only upgrade. Reject the retired plugin
        // weapon before either local or replay fire sets native recoil +BD4.
        if(RetiredLoadout(weapon)){ReportRetiredLoadout();return;}
        RestoreMedicPermission(weapon);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    medicShotNext(weapon,muzzle,overrideParam,counter,replay);
}

bool InstallMedicPermission() noexcept {
    if(medicShotNext)return true;
    if(!Matches(kMedicShot,kMedicShotSig,sizeof(kMedicShotSig)))return false;
    unsigned char trampoline[sizeof(kMedicShotSig)+14];
    std::memcpy(trampoline,kMedicShotSig,sizeof(kMedicShotSig));
    const unsigned char jump[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(trampoline+sizeof(kMedicShotSig),jump,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+kMedicShot+sizeof(kMedicShotSig));
    std::memcpy(trampoline+sizeof(kMedicShotSig)+6,&back,8);
    void* const code=edf::AllocateNearCode(image+kMedicShot,trampoline,sizeof(trampoline));
    if(!code)return false;
    unsigned char patch[sizeof(kMedicShotSig)];std::memset(patch,0x90,sizeof(patch));
    std::memcpy(patch,jump,6);
    const auto hook=reinterpret_cast<std::uintptr_t>(&MedicShotHook);
    std::memcpy(patch+6,&hook,8);
    medicShotNext=reinterpret_cast<MedicShotFn>(code);
    if(edf::PatchCode(image+kMedicShot,kMedicShotSig,patch,sizeof(patch)))return true;
    medicShotNext=nullptr;VirtualFree(code,0,MEM_RELEASE);
    return false;
}

// Whether any of `v`'s weapons heals: a medic.
bool Medic(const unsigned char* v) noexcept {
    const auto holders=At<const unsigned char*>(v,kHolders);
    const std::uint64_t count=At<std::uint64_t>(v,kHolderCount);
    if(count==0 || count>8 || !Readable(holders,count*kHolderStride))return false;
    for(std::uint64_t i=0;i<count;++i)
        if(HealingGun(At<const unsigned char*>(holders+i*kHolderStride,kHolderWeapon)))return true;
    return false;
}

// What `h` aims at: f(object, lock point, extra score in m). A medic: the hurt friends, the most hurt counting nearest;
// any other: the enemies.
template<class F> bool ForEachTarget(bool medic,const unsigned char* v,F&& f) noexcept {
    if(medic)return ForEachHurtFriend(v,[&](const void* o,const float* a,float share) noexcept { f(o,a,share*kHurtWeight); });
    return ForEachEnemy(v,[&](const void* o,const float* a) noexcept { f(o,a,0.0f); });
}

// Would a healing round of `weapon` from `from` to `to` pass within kEnemyClear of an enemy, or burst with one in its blast?
bool EnemyInLine(const unsigned char* v,const unsigned char* weapon,const float* from,const float* to) noexcept {
    const float blast=At<float>(weapon,kWeaponBlast);
    const float clear=kEnemyClear+(std::isfinite(blast) && blast>0.0f && blast<50.0f ? blast : 0.0f);
    bool close=false;
    ForEachEnemy(v,[&](const void*,const float* p) noexcept { close=close || NearLine(from,to,p,clear); });
    return close;
}

// The enemy lock point to engage, among the enemies within `range` of `around` (a guard's post, the
// player it follows). Without a map command, the current target is chased beyond that range.
// The one nearest to `from` (the heli: the shortest turn and flight) wins, the current
// one counting kKeepTarget nearer and one too close below to aim at kTooClose farther. Returns false
// with none.
bool PickTarget(Heli& h,const unsigned char* v,const float* around,const float* from,float range,float* aim) noexcept {
    const bool circler=h.type->attack==Attack::gunship;
    const float minHoriz=MinAimHoriz();
    const ULONGLONG now=GameMs();
    const float speed=std::sqrt(Dot2(h.vel,h.vel));
    float best=0.0f,bestAim[3]{};const void* bestObject=nullptr;
    bool focusSeen=false;
    ForEachTarget(h.medic,v,[&](const void* object,const float* a,float extra) noexcept {
        if(focusSeen)return;
        // The map's focus order (Heli::focus): this one, past its post's range and before any other.
        if(h.focus.Is(object)){focusSeen=true;bestObject=object;std::memcpy(bestAim,a,12);return;}
        const float d[3]={a[0]-around[0],a[1]-around[1],a[2]-around[2]};
        const bool current=h.target.Is(object);
        if((!current || h.cmd.order!=Order::none) && d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>range*range)return;
        const float f[3]={a[0]-from[0],a[1]-from[1],a[2]-from[2]};
        float score=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2])+extra;
        if(current)score-=circler ? kCircleKeep : kKeepTarget;
        if(now<h.passedUntil && h.passed.Is(object))score+=kPassed;
        if(!circler && speed>3.0f && Dot2(f,f)>1.0f)
            score+=kTurnCost*std::fabs(Wrap(std::atan2(f[0],f[2])-std::atan2(h.vel[0],h.vel[2])));
        for(const auto& o:helis)if(&o!=&h && o.ref && now-o.seen<2000 && o.target.Is(object))score+=kShareTarget;
        if(std::sqrt(Dot2(f,f))<minHoriz)score+=kTooClose;
        if(!bestObject || score<best){best=score;bestObject=object;std::memcpy(bestAim,a,12);}
    });
    if(h.focus && !focusSeen) {
        Log("HELI v=%p focus target %p no longer among its targets: back to its order",v,h.focus.obj);
        h.focus={};
    }
    if(!bestObject)return false;
    if(!h.target.Is(bestObject))h.targetAt=now;
    h.target=ObjRef::Of(bestObject);std::memcpy(aim,bestAim,12);
    return true;
}

// Its forward guns' reach: the type's (the 506's rounds die at 160 m), at most heliRange.
float GunRange(const HeliType& t) noexcept {
    return t.gunRange>0.0f && t.gunRange<Cfg().heliRange ? t.gunRange : Cfg().heliRange;
}

// Would a burst from `from` towards `to` pass within 8 m of the player before reaching the target?
bool PlayerInLine(const float* from,const float* to) noexcept {
    if(!player.at || GameMs()-player.at>2000)return false;
    return NearLine(from,to,player.pos,8.0f);
}

// This heli's flight: the helis flown right now of its type, in `helis` order (the first one leads);
// `group` numbers the flights in the order their first heli comes, `first` is the first heli of all.
struct Flight { int group,groups,wing,count; const Heli* leader; const Heli* first; };

// See kReach410.
float Span(const Heli& h) noexcept { return h.type->reach; }
// A flight's helis (one type) WingGap apart: two reaches plus kClearAir.
float WingGap(const Heli& h) noexcept {
    return 2.0f*Span(h)+kClearAir;
}
Flight FlightOf(const Heli& h,ULONGLONG ms) noexcept {
    Flight f{0,0,0,0,nullptr,nullptr};
    const HeliType* kinds[16];
    for(const auto& o:helis) {
        if(!o.ref || ms-o.seen>=2000)continue;
        if(!f.first)f.first=&o;
        int g=0;
        while(g<f.groups && kinds[g]!=o.type)++g;
        if(g==f.groups)kinds[f.groups++]=o.type;
        if(o.type!=h.type)continue;
        f.group=g;
        if(!f.leader)f.leader=&o;
        if(&o==&h)f.wing=f.count;
        ++f.count;
    }
    return f;
}

// Metres a heli flies above the formation or run height: its flight's step plus its place in the flight.
float Stack(const Flight& fl) noexcept {
    return kGroupStep*static_cast<float>(fl.group)+kWingLift*static_cast<float>(fl.wing);
}

float Dist2(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],0,a[2]-b[2]};
    return std::sqrt(Dot2(d,d));
}

// Adds to the wanted velocity `vel` a push away from the other helis flown right now, so they keep their
// reaches plus kClearAir apart.
void Separate(const Heli& h,const float* pos,float* vel,ULONGLONG ms) noexcept {
    for(const auto& o:helis) {
        if(&o==&h || !o.ref || ms-o.seen>2000)continue;
        const float d[3]={pos[0]-o.pos[0],0,pos[2]-o.pos[2]};
        const float len=std::sqrt(Dot2(d,d)),keep=Span(h)+Span(o)+kClearAir;
        if(len<0.1f || len>keep)continue;
        vel[0]+=d[0]/len*(keep-len)*kSeparationGain;vel[2]+=d[2]/len*(keep-len)*kSeparationGain;
    }
}

// Filtered velocity of `point` (the player or the target), from its moves between fixes `dt` seconds
// apart; a jump faster than `limit` (a new target, a teleport) restarts it at rest.
void TrackVelocity(float* prev,float* vel,const float* point,float dt,float limit,bool restart) noexcept {
    const float raw[3]={(point[0]-prev[0])/dt,(point[1]-prev[1])/dt,(point[2]-prev[2])/dt};
    if(restart || raw[0]*raw[0]+raw[1]*raw[1]+raw[2]*raw[2]>limit*limit)vel[0]=vel[1]=vel[2]=0.0f;
    else for(int i=0;i<3;++i)vel[i]+=(raw[i]-vel[i])*0.2f;
    std::memcpy(prev,point,12);
}

// Arrive at `goal`, moving at `goalVel`, as fast as it can still stop there: the speed towards it is the
// least of its top speed, what its brake stops in the distance left after kStopLag at the closing speed,
// and kMoveGain/kBrakeGain per metre near it (the settle of the old position PD).
void Arrive(const Heli& h,const float* pos,const float* goal,const float* goalVel,float* out) noexcept {
    const float e[3]={goal[0]-pos[0],0,goal[2]-pos[2]};
    const float len=std::sqrt(Dot2(e,e));
    out[0]=goalVel[0];out[1]=0;out[2]=goalVel[2];
    if(len<0.1f)return;
    const float rel[3]={h.vel[0]-goalVel[0],0,h.vel[2]-goalVel[2]};
    const float closing=Dot2(rel,e)/len>0.0f ? Dot2(rel,e)/len : 0.0f;
    const float left=len-closing*kStopLag>0.0f ? len-closing*kStopLag : 0.0f;
    const float settle=len*kMoveGain/kBrakeGain;
    float want=std::sqrt(2.0f*h.stopDecel*left);
    if(want>settle)want=settle;
    if(want>h.top)want=h.top;
    out[0]+=e[0]/len*want;out[2]+=e[2]/len*want;
}

// `p` in the area's frame (along u, w from its centre), scaled by 1/a and 1/b: on the unit circle when on
// the ellipse with those semi-axes.
void Unit(const float* p,float a,float b,float* q) noexcept {
    const float dx=p[0]-area.centre[0],dz=p[2]-area.centre[1];
    q[0]=(dx*area.u[0]+dz*area.u[1])/a;q[1]=(dx*area.w[0]+dz*area.w[1])/b;
}

// No target: escort the travelling player, or fly the ellipse round the area they are about (see
// kRoamSpan). Sets the wanted velocity and height; returns how far it is off its slot or the ellipse.
float Formation(const Heli& h,const float* pos,const float* fwd,const Flight& fl,float* vel,float* height) noexcept {
    const float* first=fl.first ? fl.first->pos : pos;
    const float group=static_cast<float>(fl.group),wing=static_cast<float>(fl.wing);
    *height=player.pos[1]+Cfg().heliHeight+Stack(fl);
    if(roaming) {
        // Each flight a V: the first flight on the side the first heli is on, heliFollow out and
        // kEscortAhead forward, the second on the other side, the next ones kGroupGap farther out,
        // alternating. Wing n takes place (n+1)/2 on alternating sides, WingGap back and out per place.
        const float perp[2]={moveDir[2],-moveDir[0]};
        const float toFirst[2]={first[0]-player.pos[0],first[2]-player.pos[2]};
        const float flank=(toFirst[0]*perp[0]+toFirst[1]*perp[1]<0 ? -1.0f : 1.0f)*(fl.group%2 ? -1.0f : 1.0f);
        const float place=static_cast<float>((fl.wing+1)/2),side=fl.wing%2 ? 1.0f : -1.0f;
        const float out=Cfg().heliFollow+kGroupGap*static_cast<float>(fl.group/2)+side*place*WingGap(h);
        const float ahead=kEscortAhead-place*WingGap(h);
        float fix[3]={player.pos[0]+perp[0]*flank*out+moveDir[0]*ahead-pos[0],0,
                      player.pos[2]+perp[1]*flank*out+moveDir[2]*ahead-pos[2]};
        const float off=std::sqrt(Dot2(fix,fix));
        fix[0]*=kSlotGain;fix[2]*=kSlotGain;Limit2(fix,h.top);
        vel[0]=h.pVel[0]+fix[0];vel[1]=0;vel[2]=h.pVel[2]+fix[2];
        return off;
    }
    // The ellipse (see kTrackMs): semi-axes A, B heliFollow outside the area's half spans, flown one way
    // round (its parameter t grows, from u toward w), so a player turning about inside the area never
    // turns the helis about. The area's centre drifts slowly and is not added in, so their steps do not
    // shake it. Tangent at kOrbitSpeed scaled with its size (at most kEllipseSpeedMax), pulled onto it by
    // kRadialGain. The flights share it out evenly in t from the first heli; in a flight each wingman
    // trails the one before it WingGap; a heli runs faster or slower until it sits at its place.
    const float r=Cfg().heliFollow>10.0f ? Cfg().heliFollow : 10.0f;
    const float A=area.halfU+r,B=area.halfW+r,mean=0.5f*(A+B);
    float q[2],qf[2];
    Unit(pos,A,B,q);Unit(first,A,B,qf);
    if(q[0]*q[0]+q[1]*q[1]<1e-4f){q[0]=-(fwd[0]*area.u[0]+fwd[2]*area.u[1]);q[1]=-(fwd[0]*area.w[0]+fwd[2]*area.w[1]);}
    const float t=std::atan2(q[1],q[0]);
    const float want=std::atan2(qf[1],qf[0])+2.0f*kPi*group/static_cast<float>(fl.groups>0 ? fl.groups : 1)-wing*WingGap(h)/mean;
    const float scale=Clamp(mean/r,1.0f,kEllipseSpeedMax);
    const float speed=kOrbitSpeed*scale*(1.0f+Clamp(Wrap(want-t)*kPhaseGain,-kPhaseMax,kPhaseMax));
    // The point at t, and the unit tangent there (d/dt of A cos t u + B sin t w).
    const float ct=std::cos(t),st=std::sin(t);
    const float on[3]={area.centre[0]+area.u[0]*A*ct+area.w[0]*B*st,0,area.centre[1]+area.u[1]*A*ct+area.w[1]*B*st};
    float tangent[3]={-area.u[0]*A*st+area.w[0]*B*ct,0,-area.u[1]*A*st+area.w[1]*B*ct};
    const float tl=std::sqrt(Dot2(tangent,tangent));
    if(tl>1e-3f){tangent[0]/=tl;tangent[2]/=tl;}
    float fix[3]={on[0]-pos[0],0,on[2]-pos[2]};
    const float off=std::sqrt(Dot2(fix,fix));
    fix[0]*=kRadialGain;fix[2]*=kRadialGain;Limit2(fix,kOrbitSpeed*scale);
    vel[0]=tangent[0]*speed+fix[0];vel[1]=0;vel[2]=tangent[2]*speed+fix[2];
    return off;
}

// Breaks off a run (see Engage): it flies on at full speed to a point kExtend past the target, slanted
// kExtendTurn off the way it flies (to the side of the player when following, else of its wing).
void StartExtend(Heli& h,const float* pos,const float* aim,const Flight& fl,bool follow,ULONGLONG ms) noexcept {
    h.extend=true;h.extendAt=ms;h.passed=h.target;h.passedUntil=ms+kPassedMs;
    float dir[3]={h.vel[0],0,h.vel[2]};
    const float speed=std::sqrt(Dot2(dir,dir));
    if(speed>3.0f){dir[0]/=speed;dir[2]/=speed;}
    else {
        dir[0]=aim[0]-pos[0];dir[2]=aim[2]-pos[2];
        const float len=std::sqrt(Dot2(dir,dir));
        if(len>0.1f){dir[0]/=len;dir[2]/=len;}else{dir[0]=0;dir[2]=1;}
    }
    float a=fl.wing%2 ? -kExtendTurn : kExtendTurn;
    if(follow) {   // the side nearer the player: (dir rotated by +a) vs (by -a), dotted with the way to them
        const float side[3]={dir[2],0,-dir[0]},toPlayer[3]={player.pos[0]-aim[0],0,player.pos[2]-aim[2]};
        a=Dot2(side,toPlayer)>0.0f ? kExtendTurn : -kExtendTurn;
    }
    const float c=std::cos(a),s=std::sin(a);
    h.extendTo[0]=aim[0]+(dir[0]*c+dir[2]*s)*kExtend;h.extendTo[1]=0;h.extendTo[2]=aim[2]+(dir[2]*c-dir[0]*s)*kExtend;
}

// Engaged (see kMaxTilt): a strafing run. The height is heliFireHeight (plus its Stack) above the target,
// or the player if higher. Running in, it flies at the target at full speed (its velocity on top) and,
// while its burst would pass the player, slides around the target away from them. It breaks off once the
// dip onto the lead point passes kMaxDip or a wall is ahead: StartExtend, or with `circleOnBreak` (the
// 409) kTurretCircleMs of circling (h.circleUntil, see Fly). From the extension point, or after
// kExtendMs, it turns back in. The extension may leave heliCombatRange: clamped to it (as at first), the
// point often lay by the target, it never got far enough out to turn, and it came back in with the
// target behind it (the 15:49 round).
// Runs stack lower than the formation (kRunStack/kRunWing): stacked 10 m per flight, the top flight
// passed 30 deg of dip 85 m out and hardly aimed at all.
// RunAdvance moves the run's state on (break off, end the extension) before Fly picks its mode; RunIn and
// Extend are the two halves of the run's flight.
void RunAdvance(Heli& h,const float* pos,const float* aim,float dipWant,bool wall,const Flight& fl,bool follow,bool circleOnBreak,
                ULONGLONG ms) noexcept {
    if(!h.extend && (dipWant>kMaxDip || wall)) {
        if(circleOnBreak){h.circleUntil=ms+kTurretCircleMs;h.passed=h.target;h.passedUntil=ms+kPassedMs;}
        else StartExtend(h,pos,aim,fl,follow,ms);
    }
    if(!h.extend)return;
    float to[3]={aim[0]-pos[0],0,aim[2]-pos[2]};
    const float horiz=std::sqrt(Dot2(to,to));
    if(horiz<0.1f){to[0]=0;to[2]=1;}
    else{to[0]/=horiz;to[2]/=horiz;}
    const float e[3]={h.extendTo[0]-pos[0],0,h.extendTo[2]-pos[2]};
    const float off=std::sqrt(Dot2(e,e));
    const float speed=std::sqrt(Dot2(h.vel,h.vel));
    const bool ahead=!Same(h.target,h.passed) && speed>3.0f && horiz>MinAimHoriz()+20.0f &&
        std::fabs(Wrap(std::atan2(to[0],to[2])-std::atan2(h.vel[0],h.vel[2])))<kAhead;
    if(ahead || off<20.0f || horiz>kExtend || ms-h.extendAt>kExtendMs)h.extend=false;
}

float RunHeight(const float* aim,const Flight& fl,bool follow) noexcept {
    const float ground=follow && player.pos[1]>aim[1] ? player.pos[1] : aim[1];
    return ground+Cfg().heliFireHeight+kRunStack*static_cast<float>(fl.group)+kRunWing*static_cast<float>(fl.wing);
}

// Running in: at the target at full speed, its velocity on top, sliding round it away from the player while
// the burst would pass them. Returns the horizontal distance to it.
float RunIn(const Heli& h,const float* pos,const float* aim,bool follow,float* vel) noexcept {
    float to[3]={aim[0]-pos[0],0,aim[2]-pos[2]};
    const float horiz=std::sqrt(Dot2(to,to));
    if(horiz<0.1f){to[0]=0;to[2]=1;}
    else{to[0]/=horiz;to[2]/=horiz;}
    vel[0]=h.tgtVel[0]+to[0]*h.top;vel[1]=0;vel[2]=h.tgtVel[2]+to[2]*h.top;
    if(follow && PlayerInLine(pos,aim)) {
        const float tangent[3]={to[2],0,-to[0]};
        const float toPlayer[3]={player.pos[0]-aim[0],0,player.pos[2]-aim[2]};
        const float away=Dot2(tangent,toPlayer)>0.0f ? -kSidestep : kSidestep;
        vel[0]+=tangent[0]*away;vel[2]+=tangent[2]*away;
    }
    return horiz;
}

// Extending: to its extension point at full speed. Returns how far it is.
float Extend(const Heli& h,const float* pos,float* vel) noexcept {
    const float e[3]={h.extendTo[0]-pos[0],0,h.extendTo[2]-pos[2]};
    const float off=std::sqrt(Dot2(e,e));
    vel[0]=vel[1]=vel[2]=0.0f;
    if(off>0.1f){vel[0]=e[0]/off*h.top;vel[2]=e[2]/off*h.top;}
    return off;
}

// Circle `centre` (moving at `cv`) `r` metres out, counterclockwise in atan2(x, z) at `speed`: the
// tangent plus kRadialGain m/s per metre off the radius, at most its top speed in or out, so from farther
// than 2r it just closes at full speed. Returns how far it is off the radius.
float Circle(const Heli& h,const float* pos,const float* fwd,const float* centre,const float* cv,float r,float speed,float* vel) noexcept {
    float out[3]={pos[0]-centre[0],0,pos[2]-centre[2]};
    float dist=std::sqrt(Dot2(out,out));
    if(dist<1.0f){out[0]=-fwd[0];out[2]=-fwd[2];dist=1.0f;}
    else{out[0]/=dist;out[2]/=dist;}
    const float along=dist<2.0f*r ? speed : 0.0f;
    const float radial=Clamp((r-dist)*kRadialGain,-h.top,h.top);
    vel[0]=cv[0]+out[2]*along+out[0]*radial;vel[1]=0;vel[2]=cv[2]-out[0]*along+out[2]*radial;
    return std::fabs(dist-r);
}

// Guard orbit: a guard heli (HeliCalled) circles its post Cfg().heliGuardRadius out at Cfg().heliGuardSpeed (at
// most kGuardTopShare of its top speed) instead of hovering over it, counterclockwise like Circle, nose along
// the circle (Fly faces the way it flies), so the 410's door gun on the inside of the turn bears on the post
// all the way round (a pylon turn). It enters the circle from wherever it is: from beyond 2r it closes
// straight in, between 2r and r the tangent speed fades in, so it curves onto the circle instead of meeting
// it head on. Guard helis on one post (within kSamePost) share the circle out evenly: each trails the first
// by its share, speeding up or slowing down kPhaseGain per rad (as the player orbit does). Engaged, the 410
// keeps orbiting, but the centre moves from the post toward the target until the target lies kGuardBear of
// the radius from it (60-180 m from the heli at the default radius, well inside its door guns' reach); the
// centre moves at most kCentreSpeed, so a new target does not jerk the circle. The 506 and 409, whose guns
// are along the nose, fly their gun runs as before and come back to the circle (see Fly).
constexpr float kGuardTopShare=0.8f,kSamePost=30.0f,kGuardBear=0.5f,kCentreSpeed=10.0f;
struct Orbit { float dist,angle,want,speed; int place,count; };

// Where the guard orbit's centre should be: the post, moved toward `aim` (engaged 410, or nullptr) so `aim`
// lies kGuardBear * radius from it. Returns how far it moved off the post.
float OrbitCentre(const Heli& h,const float* aim,float* centre) noexcept {
    std::memcpy(centre,h.post,12);
    if(!aim)return 0.0f;
    const float d[3]={aim[0]-h.post[0],0,aim[2]-h.post[2]};
    const float len=std::sqrt(Dot2(d,d)),shift=len-Cfg().heliGuardRadius*kGuardBear;
    if(shift<=0.0f || len<0.1f)return 0.0f;
    centre[0]+=d[0]/len*shift;centre[2]+=d[2]/len*shift;
    return shift;
}

Orbit GuardOrbit(Heli& h,const float* pos,const float* fwd,const float* centre,float dt,ULONGLONG ms,float* vel) noexcept {
    float cv[3]={0,0,0};
    if(!h.orbitSet){std::memcpy(h.orbitCentre,centre,12);h.orbitSet=true;}
    else {
        float m[3]={centre[0]-h.orbitCentre[0],0,centre[2]-h.orbitCentre[2]};
        Limit2(m,kCentreSpeed*dt);
        h.orbitCentre[0]+=m[0];h.orbitCentre[1]=centre[1];h.orbitCentre[2]+=m[2];
        cv[0]=m[0]/dt;cv[2]=m[2]/dt;
    }
    const float r=Cfg().heliGuardRadius;
    float out[3]={pos[0]-h.orbitCentre[0],0,pos[2]-h.orbitCentre[2]};
    float dist=std::sqrt(Dot2(out,out));
    if(dist<1.0f){out[0]=-fwd[0];out[2]=-fwd[2];dist=1.0f;}
    else{out[0]/=dist;out[2]/=dist;}
    Orbit o{dist,std::atan2(out[0],out[2]),0.0f,Cfg().heliGuardSpeed,0,0};
    if(o.speed>h.top*kGuardTopShare)o.speed=h.top*kGuardTopShare;
    if(o.speed<1.0f)o.speed=1.0f;
    o.want=o.angle;
    const Heli* first=nullptr;
    for(const auto& g:helis) {
        if(!g.ref || ms-g.seen>2000 || !g.guard || g.leaving || Dist2(g.post,h.post)>kSamePost)continue;
        if(!first)first=&g;
        if(&g==&h)o.place=o.count;
        ++o.count;
    }
    if(first && first!=&h && o.count>1) {
        const float fo[3]={first->pos[0]-h.orbitCentre[0],0,first->pos[2]-h.orbitCentre[2]};
        o.want=Wrap(std::atan2(fo[0],fo[2])-2.0f*kPi*static_cast<float>(o.place)/static_cast<float>(o.count));
        o.speed*=1.0f+Clamp(Wrap(o.want-o.angle)*kPhaseGain,-kPhaseMax,kPhaseMax);
    }
    const float along=o.speed*Clamp((2.0f*r-dist)/r,0.0f,1.0f);
    const float radial=Clamp((r-dist)*kRadialGain,-h.top,h.top);
    vel[0]=cv[0]+out[2]*along+out[0]*radial;vel[1]=0;vel[2]=cv[2]-out[0]*along+out[2]*radial;
    Limit2(vel,h.top);
    return o;
}

// The weapons in the pilot's seat (seat 0): the gun (fastest straight round), the homing missile
// (LockonType 1) and the straight rockets (slow, accelerating rounds). Refills an emptied one (see
// kReloadGunMs). Reads the gun's round speed and drop for the lead.
struct Loadout { float gunSpeed,gunGravity,rocketGravity; bool gun,missile,rockets; std::int32_t ammo[4],rocketAmmo;
                 const unsigned char* gunWeapon;    // the fastest gun (its barrel: the 409's turret)
                 const unsigned char* store; };     // the unguided store (see kStoreHolder), or null
// Whether holder `i` of a heli of type `t` is the store: holder 2 of a strafing nose-gun type, not homing.
bool IsStore(const HeliType* t,std::uint64_t i,bool homing) noexcept {
    return t && i==kStoreHolder && !homing && t->guns==Guns::nose && t->attack==Attack::strafe;
}
Loadout Arms(Heli& h,unsigned char* v,ULONGLONG ms) noexcept {
    Loadout l{kBulletSpeed,0.0f,1.0f,false,false,false,{-1,-1,-1,-1},0,nullptr,nullptr};
    if(SeatCount(v)==0)return l;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return l;
    float best=0.0f;
    for(std::uint64_t i=0;i<count && i<4;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto weapon=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(weapon,kWeaponAmmo+4,true))continue;
        auto& arm=h.arms[i];
        const std::int32_t ammo=At<std::int32_t>(weapon,kWeaponAmmo);
        if(arm.weapon!=weapon){arm.weapon=weapon;arm.full=ammo;arm.emptyAt=0;}
        if(ammo>arm.full)arm.full=ammo;
        const float speed=At<float>(weapon,kWeaponSpeed)*60.0f,gravity=At<float>(weapon,kWeaponGravity);
        const bool homing=At<std::int32_t>(weapon,kWeaponLockon)==kHoming,rocket=!homing && speed<120.0f;
        const bool store=IsStore(h.type,i,homing);
        if(ammo<=0 && arm.full>0 && !h.called) {
            if(!arm.emptyAt)arm.emptyAt=ms;
            else if(ms-arm.emptyAt>((homing || rocket || store) ? kReloadAltMs : kReloadGunMs)) {
                Put<std::int32_t>(weapon,kWeaponAmmo,arm.full);arm.emptyAt=0;
                if(Cfg().debug)Log("HELI v=%p reloaded weapon %llu (%p) to %d",v,static_cast<unsigned long long>(i),weapon,arm.full);
            }
        } else arm.emptyAt=0;
        l.ammo[i]=ammo;
        if(store)l.store=weapon;
        else if(homing)l.missile=true;
        else if(rocket){l.rockets=true;l.rocketAmmo+=ammo>0 ? ammo : 0;if(std::isfinite(gravity))l.rocketGravity=gravity;}
        else if(std::isfinite(speed) && speed>best) {
            best=speed;l.gun=true;l.gunSpeed=speed;l.gunGravity=std::isfinite(gravity) && gravity>0.0f ? gravity : 0.0f;
            l.gunWeapon=weapon;
        }
    }
    return l;
}

// Seconds the 409's rocket takes to fly `d` metres (see kRocketStart).
float RocketTime(float d) noexcept {
    return (-kRocketStart+std::sqrt(kRocketStart*kRocketStart+2.0f*kRocketAccel*d))/kRocketAccel/60.0f;
}

// Where to point to hit `aim` moving at `tv`: `time(d)` seconds of flight to d metres, falling
// gravity * kGravity on the way (twice over: the lead point moves the flight time).
template<class F> void LeadPoint(const float* pos,const float* aim,const float* tv,float gravity,F time,float* out) noexcept {
    std::memcpy(out,aim,12);
    for(int pass=0;pass<2;++pass) {
        const float d[3]={out[0]-pos[0],out[1]-pos[1],out[2]-pos[2]};
        const float t=time(std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]));
        for(int i=0;i<3;++i)out[i]=aim[i]+tv[i]*t;
        out[1]+=0.5f*gravity*kGravity*t*t;
    }
}

// Obstacle avoidance, applied to the wanted velocity and height of every mode but landing:
// - the height stays kGroundClear over the ground below and over whatever lies kLookAhead seconds ahead
//   (a ray down from high above that point finds hill tops and roofs alike);
// - a ray straight along the way it flies (and two kAvoidSide to the sides) finds walls: the speed into
//   one is cut to what stops it kAvoidStop short, the height goes kRoofClear over the wall's top, and it
//   veers to the clearer side.
constexpr float kLookAhead=3.0f,kLookMin=25.0f,kAvoidSide=0.52f;   // s; m; rad (30 deg)
constexpr float kGroundClear=6.0f,kRoofClear=10.0f,kRoofProbe=150.0f,kAvoidStop=12.0f,kAvoidSteer=8.0f;
// metres to the wall ahead and over the ground (-1: none seen); for the log, the roof ahead and the
// nearest hit of any kind there with its flags (-1e9: none)
struct Avoidance { float ahead,clear,roof,anyRoof; std::uint32_t anyFlags; };

float RoofBelow(const float* at,float top,bool any=false,std::uint32_t* flags=nullptr) noexcept {
    const float a[3]={at[0],top,at[2]},b[3]={at[0],top-kRoofProbe*2.0f,at[2]};
    float hit[3];
    return CastRay(a,b,hit,any,flags)>=0.0f ? hit[1] : -1e9f;
}

Avoidance Avoid(const float* pos,const float* vel,float* want,float* height,bool land,float stopDecel) noexcept {
    Avoidance r{-1.0f,-1.0f,-1e9f,-1e9f,0};
    if(!Cfg().heliAvoid || !rayOk)return r;
    const float down[3]={pos[0],pos[1]-kRoofProbe*2.0f,pos[2]};
    float hit[3];
    if(CastRay(pos,down,hit)>=0.0f) {
        r.clear=pos[1]-hit[1];
        if(!land && *height<hit[1]+kGroundClear)*height=hit[1]+kGroundClear;
    }
    if(land)return r;
    // The way it flies: where it wants to go, or where it drifts.
    float dir[3]={want[0],0,want[2]};
    if(Dot2(dir,dir)<4.0f){dir[0]=vel[0];dir[2]=vel[2];}
    const float dl=std::sqrt(Dot2(dir,dir));
    if(dl<2.0f)return r;
    dir[0]/=dl;dir[2]/=dl;
    const float speed=std::sqrt(Dot2(vel,vel));
    const float look=speed*kLookAhead>kLookMin ? speed*kLookAhead : kLookMin;
    const float top=(pos[1]>*height ? pos[1] : *height)+kRoofProbe;
    const float ahead[3]={pos[0]+dir[0]*look,0,pos[2]+dir[2]*look};
    const float roof=RoofBelow(ahead,top);
    r.roof=roof;
    if(Cfg().debug)r.anyRoof=RoofBelow(ahead,top,true,&r.anyFlags);
    if(*height<roof+kGroundClear)*height=roof+kGroundClear;
    // Walls: straight ahead and to both sides.
    float dist[3];
    for(int i=0;i<3;++i) {
        const float a=static_cast<float>(i-1)*kAvoidSide,c=std::cos(a),s=std::sin(a);
        const float d[3]={dir[0]*c+dir[2]*s,0,dir[2]*c-dir[0]*s};
        const float end[3]={pos[0]+d[0]*look,pos[1],pos[2]+d[2]*look};
        dist[i]=CastRay(pos,end,i==1 ? hit : nullptr);
    }
    const float centre=dist[1];
    if(centre>=0.0f) {
        r.ahead=centre;
        const float beyond[3]={hit[0]+dir[0]*3.0f,0,hit[2]+dir[2]*3.0f};
        const float wall=RoofBelow(beyond,top);
        if(*height<wall+kRoofClear)*height=wall+kRoofClear;
        const float room=centre-kAvoidStop;
        const float allowed=room>0.0f ? std::sqrt(2.0f*stopDecel*room) : 0.0f;
        const float into=Dot2(want,dir);
        if(into>allowed){want[0]-=dir[0]*(into-allowed);want[2]-=dir[2]*(into-allowed);}
    }
    // Veer to the clearer side (a side with no hit is clear for the whole look).
    const float left=dist[0]<0.0f ? look : dist[0],rightSide=dist[2]<0.0f ? look : dist[2];
    if(centre>=0.0f || left<look || rightSide<look) {
        const float side=left>rightSide ? -1.0f : 1.0f;   // -1: toward the first ray (negative angle)
        const float s=std::sin(side*kAvoidSide*3.0f),c=std::cos(side*kAvoidSide*3.0f);   // 90 deg off
        const float perp[3]={dir[0]*c+dir[2]*s,0,dir[2]*c-dir[0]*s};
        const float nearest=centre>=0.0f ? centre : (left<rightSide ? left : rightSide);
        const float push=kAvoidSteer*(1.0f-nearest/look);
        want[0]+=perp[0]*push;want[2]+=perp[2]*push;
    }
    return r;
}

// ---- 410 door guns ----
// Gun i (0 left, 1 right) belongs to seat i+1 and trigger i (layout.h kHolders: veh+0x638, stride 0x48: +8 the
// weapon's weak_ptr control block, +0x10 the weapon). The stock input (slot 55, 0x64E080) fills block i at
// veh+0x2030+i*0x20 from that seat's rider, zeros for an empty seat: +0 yaw input, +4 pitch input, +0x10
// the trigger byte. Slot 57 (0x64D870) hands each block to its seat's aim (seat+0xE0, the tanks'
// VehicleWeaponAim: axes at +0x10, stride 0x40, {min, max, angle}; an axis turns input x k rad per frame)
// and pulls the trigger (0x62C000) while the byte is set. Writing the block after the stock input aims and
// fires an unmanned door gun, and DoorGunUser lets its rounds out. The aim is closed on the real barrel
// (the muzzle frame fire builds, see Barrel) with one sign per axis (axis angle vs geometric angle)
// learned from how the barrel moves, as EDF6AutoTurret's tank gunners do; an axis held at a stop with the
// error not closing for kStuckMs flips its sign too.
constexpr std::size_t kDoorBlock=0x2030,kDoorStride=0x20,kDoorPull=0x10;
// The seat aim's axes (kSeatAim, kAimAxes, ...): common/edf/layout.h.
constexpr std::size_t kMuzzles=0x1D0,kMuzzleCount=0x1E0,kMuzzleStride=0xF0,kMuzzleLocal=0x10,kBoneRows=0xB0;
constexpr std::size_t kMuzzleMode=0xE0,kWeaponRows=0x150;
constexpr std::int32_t kModeWeaponRows=0;
constexpr float kMuzzleReach=30.0f;   // m: a muzzle farther than this from the vehicle is garbage
constexpr float kAxisMargin=0.03f;    // rad past a stop still counted as reachable
constexpr float kTurnPerInput=1.1f/60.0f,kTurnMin=0.2f/60.0f,kTurnMax=6.0f/60.0f,kSettleFrames=6.0f;
constexpr float kDoorRange=450.0f;    // m: farther than this (or the round's reach) it does not shoot
constexpr float kDoorMin=15.0f;       // m: nothing closer to the muzzle
constexpr float kDoorCone=0.026f;     // rad (1.5 deg), widened up close so kHitRadius at the target still counts
constexpr float kDoorHold=2.0f;       // a firing gun keeps firing until it is this many cones off
constexpr float kDoorSlew=60.0f,kDoorKeep=50.0f,kDoorShare=30.0f;   // target score: m per rad to turn, m off for the current and the pilot's
constexpr ULONGLONG kStuckMs=1000;

float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The vehicle frame: rows right, up, forward, position (veh+0x60).
void ToFrame(const unsigned char* v,const float* w,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    out[0]=Dot3(w,m);out[1]=Dot3(w,m+4);out[2]=Dot3(w,m+8);
}

// Geometric yaw (round the vehicle's up, toward its right) and elevation of a vehicle-frame vector.
void Angles(const float* local,float* geo) noexcept {
    geo[0]=std::atan2(local[0],local[2]);
    geo[1]=std::atan2(local[1],std::sqrt(local[0]*local[0]+local[2]*local[2]));
}

// One muzzle's world position and direction as fire (0x6969A0) builds them: the position is row 3 of
// local x bone, the direction row 2 of the matrix its mode picks.
bool MuzzleFrame(const unsigned char* weapon,const unsigned char* muzzle,float* pos,float* dir) noexcept {
    const auto bone=At<const unsigned char*>(muzzle,0);
    if(!Readable(bone,kBoneRows+0x40))return false;
    float b[4][4],l[4][4];
    std::memcpy(b,bone+kBoneRows,sizeof(b));std::memcpy(l,muzzle+kMuzzleLocal,sizeof(l));
    for(int c=0;c<3;++c) {
        dir[c]=l[2][0]*b[0][c]+l[2][1]*b[1][c]+l[2][2]*b[2][c];
        pos[c]=l[3][0]*b[0][c]+l[3][1]*b[1][c]+l[3][2]*b[2][c]+l[3][3]*b[3][c];
    }
    if(At<std::int32_t>(muzzle,kMuzzleMode)==kModeWeaponRows)std::memcpy(dir,weapon+kWeaponRows+0x20,12);
    const float length=std::sqrt(Dot3(dir,dir));
    return std::isfinite(pos[0]+pos[1]+pos[2]) && std::isfinite(length) && length>0.5f && length<2.0f;
}

// The barrel: the mean of the gun's muzzles.
bool Barrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept {
    const auto muzzles=At<const unsigned char*>(weapon,kMuzzles);
    const auto count=At<std::uint64_t>(weapon,kMuzzleCount);
    if(count==0 || count>8 || !Readable(muzzles,count*kMuzzleStride))return false;
    std::memset(pos,0,12);std::memset(dir,0,12);
    for(std::uint64_t i=0;i<count;++i) {
        float p[3],f[3];
        if(!MuzzleFrame(weapon,muzzles+i*kMuzzleStride,p,f))return false;
        for(int c=0;c<3;++c){pos[c]+=p[c];dir[c]+=f[c];}
    }
    const float length=std::sqrt(Dot3(dir,dir));
    if(!(length>0.1f))return false;
    for(int c=0;c<3;++c){pos[c]/=static_cast<float>(count);dir[c]/=length;}
    const float* at=reinterpret_cast<const float*>(v+kPosition);
    const float d[3]={pos[0]-at[0],pos[1]-at[1],pos[2]-at[2]};
    return Dot3(d,d)<kMuzzleReach*kMuzzleReach;
}

// Whether the gun's barrel (the mean of its muzzles) points at `lead` within the fire cone at `dist`.
bool TurretOn(const unsigned char* v,const unsigned char* gun,const float* lead,float dist) noexcept {
    float at[3],dir[3];
    if(!gun || !Barrel(v,gun,at,dir))return false;
    float to[3]={lead[0]-at[0],lead[1]-at[1],lead[2]-at[2]};
    const float l=std::sqrt(Dot3(to,to));
    if(l<1.0f)return true;
    const float off=std::acos(Clamp(Dot3(to,dir)/l,-1.0f,1.0f))*180.0f/kPi;
    const float wide=dist>1.0f ? std::atan(kHitRadius/dist)*180.0f/kPi : 90.0f;
    return off<(wide>Cfg().heliFireCone ? wide : Cfg().heliFireCone);
}

// A door gun's axes this frame and where its barrel points (geometric, vehicle frame).
struct DoorAim { float angle[2],lo[2],hi[2],barrel[2],sign[2]; };

// The geometric error from the barrel to `p` (from the muzzle `from`) and the axis angles that close it;
// false when an axis cannot get there.
bool Reach(const unsigned char* v,const DoorAim& a,const float* from,const float* p,float* err,float* axis) noexcept {
    const float d[3]={p[0]-from[0],p[1]-from[1],p[2]-from[2]};
    float local[3],want[2];
    ToFrame(v,d,local);Angles(local,want);
    err[0]=Wrap(want[0]-a.barrel[0]);err[1]=want[1]-a.barrel[1];
    bool ok=true;
    for(int i=0;i<2;++i) {
        axis[i]=a.angle[i]+a.sign[i]*err[i];
        ok=ok && axis[i]>=a.lo[i]-kAxisMargin && axis[i]<=a.hi[i]+kAxisMargin;
    }
    return ok;
}

// Writes door gun i's block: aims it at the best enemy it can reach (the pilot's target preferred) with
// lead for its round, and pulls while on it. `hold`: aim but never fire.
// `g` the gun's state, `share` the pilot's target (preferred; none under a player pilot), `refill` an emptied gun
// refilled after kReloadGunMs (the NPC heli's own, as Arms does; not a called heli's, not a player's heli).
void DoorGun(Door& g,const ObjRef& share,bool refill,unsigned char* v,int i,bool hold,float dt,ULONGLONG ms) noexcept {
    unsigned char* blk=v+kDoorBlock+i*kDoorStride;
    unsigned char* seat=SeatAt(v,static_cast<unsigned>(i+1));
    // The player's gun: their stick, their trigger; a medic's (MedicGunnerAim): aimed for them at the hurt friend it
    // reaches while there is one, the trigger still theirs (else their stick as it is).
    const bool theirs=SeatRider(seat)==Rider::player;
    if(AnyPlayerIn(seat) && !theirs){g.prevValid=false;return;}   // remote players own their gun input
    if(theirs && !Cfg().medicGunnerAim){g.prevValid=false;return;}
    const auto triggers=At<unsigned char*>(v,kHolders);
    if(At<std::uint64_t>(v,kHolderCount)<=static_cast<std::uint64_t>(i) || !Readable(triggers+i*kHolderStride,kHolderStride))return;
    const auto trigger=triggers+i*kHolderStride;
    const auto ctrl=At<const unsigned char*>(trigger,kHolderCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)return;
    const auto weapon=At<unsigned char*>(trigger,kHolderWeapon);
    if(!Readable(weapon,kWeaponAmmo+4,true))return;
    const bool heals=HealingGun(weapon);
    if(theirs && !heals){g.prevValid=false;return;}
    float gp[3],gd[3];
    if(!Barrel(v,weapon,gp,gd))return;
    // No weapon of the helis reloads: refill an emptied gun as Arms does.
    const std::int32_t ammo=At<std::int32_t>(weapon,kWeaponAmmo);
    if(g.weapon!=weapon || g.weaponCtrl!=ctrl){g.weapon=weapon;g.weaponCtrl=ctrl;g.full=ammo;g.emptyAt=0;}
    if(ammo>g.full)g.full=ammo;
    g.ammo=ammo;g.ammoFrame=GameFrame();   // what LeaveReason counts (it never reads the weapon itself)
    if(ammo<=0 && g.full>0 && refill) {
        if(!g.emptyAt)g.emptyAt=ms;
        else if(ms-g.emptyAt>kReloadGunMs){Put<std::int32_t>(weapon,kWeaponAmmo,g.full);g.emptyAt=0;}
    } else g.emptyAt=0;
    // The axes, and the sign of each learned from the barrel's own motion.
    DoorAim a{};
    const auto axes=seat+kSeatAim+kAimAxes;
    float local[3];ToFrame(v,gd,local);Angles(local,a.barrel);
    for(int k=0;k<2;++k) {
        a.angle[k]=At<float>(axes+k*kAxisStride,kAxisAngle);
        a.lo[k]=At<float>(axes+k*kAxisStride,kAxisMin);a.hi[k]=At<float>(axes+k*kAxisStride,kAxisMax);
        if(!std::isfinite(a.angle[k]+a.lo[k]+a.hi[k]+a.barrel[k]) || a.lo[k]>a.hi[k])return;
        if(g.sign[k]==0.0f)g.sign[k]=k==0 ? 1.0f : -1.0f;   // the tank gunners' signs, until learned
        if(g.prevValid) {
            const float moved=a.angle[k]-g.axisPrev[k];
            const float turned=k==0 ? Wrap(a.barrel[k]-g.barrelPrev[k]) : a.barrel[k]-g.barrelPrev[k];
            const float ratio=std::fabs(moved)>0.002f ? turned/moved : 0.0f;
            if(std::fabs(ratio)>0.3f && std::fabs(ratio)<3.0f)g.sign[k]+=0.2f*((ratio>0.0f ? 1.0f : -1.0f)-g.sign[k]);
            if(std::fabs(g.in[k])>0.15f) {
                const float rate=moved/g.in[k];
                if(rate>kTurnMin && rate<kTurnMax)g.k[k]+=0.05f*(rate-g.k[k]);
            }
        }
        if(g.k[k]<=0.0f)g.k[k]=kTurnPerInput;
        a.sign[k]=g.sign[k]>=0.0f ? 1.0f : -1.0f;
    }
    // The target: the cheapest enemy in reach, the current one and the pilot's counting nearer (a healing gun: the cheapest
    // hurt friend, the most hurt counting nearer; see Medic).
    const float speed=At<float>(weapon,kWeaponSpeed)*60.0f;
    const float reach=At<float>(weapon,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(weapon,kWeaponAlive));
    const float range=std::isfinite(reach) && reach>0.0f && reach<kDoorRange ? reach : kDoorRange;
    float gravity=At<float>(weapon,kWeaponGravity);
    if(!std::isfinite(gravity) || gravity<0.0f)gravity=0.0f;
    const void* best=nullptr;float bestScore=0.0f,bestAt[3]{};
    if(std::isfinite(speed) && speed>1.0f)
        ForEachTarget(heals,v,[&](const void* object,const float* p,float extra) noexcept {
            const float d[3]={p[0]-gp[0],p[1]-gp[1],p[2]-gp[2]};
            const float dist=std::sqrt(Dot3(d,d));
            float err[2],axis[2];
            if(dist>range || dist<kDoorMin || !Reach(v,a,gp,p,err,axis))return;
            float score=dist+extra+(std::fabs(err[0])+std::fabs(err[1]))*kDoorSlew;
            if(g.target.Is(object))score-=kDoorKeep;
            if(share.Is(object))score-=kDoorShare;
            if(!best || score<bestScore){best=object;bestScore=score;std::memcpy(bestAt,p,12);}
        });
    float in[2]={0,0},err[2]={0,0},axis[2]={a.angle[0],a.angle[1]},lead[3]{},dist=0.0f;
    bool fire=false;
    if(best) {
        TrackVelocity(g.tgtPrev,g.tgtVel,bestAt,dt,40.0f,!g.target.Is(best));
        g.target=ObjRef::Of(best);
        LeadPoint(gp,bestAt,g.tgtVel,gravity,[speed](float d) noexcept { return d/speed; },lead);
        Reach(v,a,gp,lead,err,axis);
        const float d[3]={lead[0]-gp[0],lead[1]-gp[1],lead[2]-gp[2]};
        dist=std::sqrt(Dot3(d,d));
        for(int k=0;k<2;++k) {
            axis[k]=Clamp(axis[k],a.lo[k],a.hi[k]);
            in[k]=Clamp((axis[k]-a.angle[k])/(g.k[k]*kSettleFrames),-1.0f,1.0f);
            // Held at a stop, the error not closing: the sign is wrong (see the header).
            const bool atStop=a.angle[k]<=a.lo[k]+0.02f || a.angle[k]>=a.hi[k]-0.02f;
            if(atStop && std::fabs(err[k])>0.2f) {
                if(!g.stuckAt[k])g.stuckAt[k]=ms;
                else if(ms-g.stuckAt[k]>kStuckMs){g.sign[k]=-g.sign[k];g.stuckAt[k]=0;Log("GUNNER410 v=%p gun=%d axis %d stuck at a stop: sign flipped",v,i,k);}
            } else g.stuckAt[k]=0;
        }
        const float wide=dist>1.0f ? std::atan(kHitRadius/dist) : 1.0f;
        const float cone=(wide>kDoorCone ? wide : kDoorCone)*(g.firing ? kDoorHold : 1.0f);
        // A healing round onto the player is the point; one past an enemy is not (see Medic).
        fire=!hold && !theirs && Cfg().heliFire && std::fabs(err[0])<cone && std::fabs(err[1])<cone && dist>kDoorMin &&
             (heals ? !EnemyInLine(v,weapon,gp,lead) : !PlayerInLine(gp,lead));
    } else g.target=ObjRef{};
    if(theirs && !best){g.prevValid=false;return;}   // nobody to heal: their own aim
    Put<float>(blk,0,in[0]);Put<float>(blk,4,in[1]);
    if(!theirs)blk[kDoorPull]=fire ? 1 : 0;
    g.firing=fire;
    for(int k=0;k<2;++k){g.in[k]=in[k];g.axisPrev[k]=a.angle[k];g.barrelPrev[k]=a.barrel[k];}
    g.prevValid=true;
    if(Cfg().debug && ms-g.loggedAt>1000) {
        g.loggedAt=ms;
        Log("GUNNER410 v=%p gun=%d%s t=%p dist=%.0f barrel=(%.2f,%.2f) err=(%.3f,%.3f) axis=(%.2f,%.2f)->(%.2f,%.2f) lim=(%.2f..%.2f, %.2f..%.2f) sign=(%+.0f,%+.0f) k=(%.4f,%.4f) in=(%.2f,%.2f) fire=%d ammo=%d",
            v,i,heals ? (theirs ? " (heals, player's)" : " (heals)") : "",best,dist,a.barrel[0],a.barrel[1],err[0],err[1],a.angle[0],a.angle[1],axis[0],axis[1],a.lo[0],a.hi[0],a.lo[1],a.hi[1],
            a.sign[0],a.sign[1],g.k[0],g.k[1],in[0],in[1],fire,ammo);
        // A medic's: the friend's HP now, a line a second (whether the rounds heal shows as it rising).
        const auto friendObj=static_cast<const unsigned char*>(best);
        if(heals && friendObj && Readable(friendObj,kHp+4))
            Log("MEDIC v=%p gun=%d friend=%p hp %.0f/%.0f",v,i,best,At<float>(friendObj,kHp),At<float>(friendObj,kHpMax));
    }
}

bool doorOk=false;   // the 410 layout DoorGun writes matched and its weapon-user hook is in (InstallDoorGuns)

// A called heli leaving: kLeaveOut away from the player (or on along its heading), kLeaveClimb higher.
constexpr float kLeaveOut=3000.0f,kLeaveClimb=60.0f,kGoneFar=1000.0f;
constexpr ULONGLONG kLeaveMaxMs=90000;
constexpr float kLeaveHp=0.25f;
void StartLeave(Heli& h,const float* pos,const float* fwd,const char* why) noexcept {
    float d[3]={pos[0]-player.pos[0],0.0f,pos[2]-player.pos[2]};
    float l=std::sqrt(Dot2(d,d));
    if(!player.at || l<1.0f){d[0]=fwd[0];d[2]=fwd[2];l=std::sqrt(Dot2(d,d));}
    if(l<1e-3f){d[0]=0.0f;d[2]=1.0f;l=1.0f;}
    h.hold[0]=pos[0]+d[0]/l*kLeaveOut;h.hold[1]=pos[1]+kLeaveClimb;h.hold[2]=pos[2]+d[2]/l*kLeaveOut;
    h.leaving=true;h.leftAt=GameMs();h.target=ObjRef{};h.extend=false;
    Log("HELI v=%p %s: leaving towards (%.0f,%.0f)",h.ref.obj,why,h.hold[0],h.hold[2]);
}

// Why a called heli leaves now (see the file comment), or nullptr: the weapons Arms read this frame (`l`) and
// the door guns DoorGun read just now all empty (once any was seen loaded), fuel, damage. No remembered
// weapon is read again: one that was not read through the vehicle just now does not count.
const char* LeaveReason(const Heli& h,const unsigned char* v,const Loadout& l) noexcept {
    if(!h.keep && GameMs()>=h.leaveAt)return "out of fuel";
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    if(hpMax>0.0f && hp<hpMax*kLeaveHp)return "damaged";
    bool armed=false;
    std::int32_t left=0;
    for(int i=0;i<4;++i)
        if(h.arms[i].weapon && h.arms[i].full>0 && l.ammo[i]>=0){armed=true;left+=l.ammo[i];}
    for(const auto& d:h.doors)
        if(d.weapon && d.full>0 && GameFrame()-d.ammoFrame<=kAliveFrames){armed=true;left+=d.ammo>0 ? d.ammo : 0;}
    return !h.keep && armed && left==0 ? "out of ammo" : nullptr;
}

// ---- Sea rescue (docs/rescue-re.md) ----
// A local player on foot below Cfg().rescueBelow for kWetMs, with a submarine carrier out (SubDeck): this machine
// asks for the support catalog's rescue entry at the swimmer (support_call.h SupportRescueAt, 2026-10-09, the user:
// 「给救援加一个支援目录项，走正规的呼叫支援流程」). The support deployment plans it as any air support (an entry at
// the map's edge, a clear corridor, open sky), makes one Brute (410) in the air there with its real pilot made inside
// it and seated at once, and registers both on every peer; the plugin never makes the heli itself, and nobody
// boards in mid-air. The dispatcher hands the heli over (RescueHeliDeployed) on every machine that made it:
//  - where it is flown (offline, the host): the flight (Rescue): to the player who asked (the requester, by identity:
//    support_net.h SupportTransactionRequester; 2026-10-10), down so the free door seat's riding point is over them,
//    hovering there; aboard, to the deck (ferry), kDeckHover over it until they jump off; then it leaves (StartLeave).
//    The requester gone, dead, ashore, in another vehicle or picked up by another rescue calls it off at once
//    (rescue_logic.h PickupCancel): the reason logged, the heli leaves as support aircraft leave;
//  - on the swimmer's machine: the call (RescueCall): the board button pressed for them (Cfg().rescueAutoBoard, the
//    stock board path 0x56D700, only within the stock reach; else they press it themselves), and when it ends.
// A request that brings no heli (refused, timed out, nothing made) is logged and shown (RescueBanner) and asked
// again kRetryMs later. The player is never moved by the plugin: only the stock boarding seats them, and they get
// off themselves.
// The 410, not the 506: the 506 has one seat (V506_HELI vehicle_riding_position: the driver only), so a
// player boarding it would bump the NPC pilot out and fly it themselves; the 410 has two door gunner seats.
// While it waits it is on team kTeamVehicle (5, nobody's): the stock seat check lets a human of another team
// into a seat only with bit 7 of its masks, which none of the 410's seats have (masks 9 / 15), and the call
// helis are team 2 (friend). Its own team is given back when it leaves (on a peer's copy, by the call).
constexpr float kSeaY=0.0f;            // the sea surface when the water probe is off (SeaAt unknown): about y = 0, a guess
constexpr float kUnderSurface=1.0f;     // in the sea: this far under the water's surface at least
constexpr ULONGLONG kWetMs=1500;       // in the sea this long before a heli is sent
constexpr ULONGLONG kRetryMs=10000;    // after a request brought no heli, or the heli was lost
constexpr ULONGLONG kRequestWaitMs=150000;   // a request whose heli has not come by then failed (support planning: 120 s)
constexpr float kOverHull=15.0f;        // m: over the carrier's hull footprint or this near it (a 410's rotor), not below its deck
constexpr std::size_t kHumanVehicle=0x1548;   // the vehicle a human rides (0x56A3D0 writes it; +0x1550 its control block)
constexpr ULONGLONG kAgainMs=30000;    // after a rescue ended: not another heli each few seconds for one who will not board
constexpr float kRescueApproach=40.0f; // m: farther out it flies kRescueCruise over the sea, nearer it comes down
constexpr float kRescueCruise=20.0f;
constexpr float kSkid=1.5f;            // m: its origin no lower than this over the sea (the 410 box bottom is 0.1 m over it)
constexpr float kSeatAbove=0.5f;       // m: the door seat's riding point this far over the swimmer (or the sea)
constexpr ULONGLONG kBoardTryMs=500,kPickupMs=120000,kDryMs=3000,kOverDeckLogMs=10000;
constexpr DWORD kRescueFuelSec=900;     // support_dispatch.cpp gives the rescue entry the same
constexpr float kDeckHover=8.0f;       // m over the deck it hovers at for the player to jump off
constexpr float kFerrySpeed=15.0f;     // m/s at most while it carries them
constexpr float kHullMargin=80.0f;     // m: lower than the deck and this near the hull it climbs before it closes in
constexpr float kOverDeck=15.0f;
// The stock seat check (CanRideSeat 0x6346D0): a seat's riding point is the MAB point *(seat+0x1E0) (local
// position at record + *(int*)(record+0xC)) on the bone *(seat+0x1E8) (rows +0xB0..+0xE0, as 0x6BB420 builds
// it), and a human whose position is within the point's radius (record+0x10) plus kReachSlack (0x1C36990) of
// it is in reach. The class mask: (human+0x31C & seat+0x34) & seat+0x30, as dwords (0x6346FC).
constexpr std::size_t kSeatPoint=0x1E0,kSeatPointBone=0x1E8,kPointLocal=0xC,kPointRadius=0x10;
constexpr std::size_t kSeatMask=0x30,kSeatClassMask=0x34,kHumanRideMask=0x31C;
constexpr unsigned kReachSlackRva=0x1C36990;
constexpr float kReachSlack=0.5f;
// The board button: 0x59B417 tests human+0xD78 and calls 0x56D700(human): nothing if the human is in a
// vehicle (+0x1550) or in some states (+0x128 bit 0, +0x5D0 bit 2, +0x39C != 0), else a visitor over the
// vehicles (team 5, then the human's side) calls each one's slot 49 (FindSeat) until one gives a seat it may
// take, reserves it and rides (0x5763E0). FindSeat only offers a seat in the reach above.
constexpr unsigned kBoardButton=0x56D700;
const Signature kRescueSignatures[]={
    {kBoardButton,{0x40,0x53,0x48,0x83,0xEC,0x40,0x48,0x8B,0xD9,0x48,0x8B,0x81,0x50,0x15,0x00,0x00},16},
    {0x59B417,{0x44,0x38,0xBB,0x78,0x0D,0x00,0x00,0x74,0x08,0x48,0x8B,0xCB,0xE8,0xD8,0x22,0xFD},16},   // button -> 0x56D700(human)
    {0x572610,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},   // its visitor
    {0x6346FC,{0x8B,0x82,0x1C,0x03,0x00,0x00,0x41,0x23,0x40,0x34,0x41,0x85,0x40,0x30},14},             // class mask
    {0x634726,{0x48,0x89,0x5C,0x24,0x40,0x48,0x8D,0x54,0x24,0x20,0x49,0x8D,0x98,0xE0,0x01,0x00},16},   // seat+0x1E0
    {0x634758,{0xF3,0x0F,0x10,0x58,0x10,0xF3,0x0F,0x58,0x1D,0x2B,0x22,0x60,0x01},13},                  // radius + slack
    {0x6BB420,{0x4C,0x8B,0x01,0x49,0x63,0x40,0x0C,0x42,0x0F,0x10,0x14,0x00,0x48,0x8B,0x41,0x08},16},   // the point
};
bool reachOk=false,boardOk=false;   // the seat reach can be read / the board button can be pressed

// The flight of a rescue heli this machine flies (offline, the host): one a rescue deployment (a player of each
// machine may be in the sea at once).
enum class RescuePhase { none, pickup, ferry };
struct Rescue {
    RescuePhase phase;
    unsigned char* vehicle;
    ObjRef ref;                         // the heli as the deployment handed it over
    ObjRef swimmer;                     // the player (of any machine) it picks up
    ULONGLONG launchFrame,seenFrame;    // the game frame it was handed over in / its input last ran in (HeliFrame; 0: not yet)
    std::int32_t team;                  // the heli's own team, given back when it leaves
    ULONGLONG startedAt,nearAt,dryAt,overDeckAt,loggedAt;
    int seat;
    float seatDist,seatReach;           // the free door seat's riding point to the swimmer, and its stock reach
    // What Fly flies to (RescueGoal): a point and height; `climb`: no horizontal move yet; `slow`: kFerrySpeed;
    // `low`: down to the swimmer, under the kGroundClear that Avoid keeps over the ground (or the sea) below.
    float goal[3],height;
    bool climb,slow,low;
};
constexpr int kRescues=4;
Rescue rescues[kRescues]{};
// This machine's player's rescue: its request, then the heli the deployment made for it here (flown here, or a
// peer's copy of the host's) until the player is out of it or it is over.
enum class CallPhase { idle, requested, assigned };
struct RescueCall {
    CallPhase phase;
    ULONGLONG wetSince,retryAt,requestedAt,nearAt,boardTryAt,dryAt,loggedAt;
    float at[3];                        // the request's point: the deployment's plan target on every machine
    unsigned char* vehicle;
    ObjRef ref;
    ULONGLONG madeFrame,seenFrame;      // as Rescue's launchFrame / seenFrame
    bool flown;                         // this machine flies it (the flight above keeps its team); else a peer's copy
    std::int32_t team;                  // a peer's copy's own team, given back when the call ends
    int tries;
    float seatDist,seatReach;
    bool aboard,warned;
};
RescueCall call{};
ULONGLONG rescueFrame=0;   // RescueTick: at most once a frame

// The rescue banner (draw thread reads, game thread writes): what became of this machine's rescue, a moment.
SRWLOCK rescueCueLock=SRWLOCK_INIT;
wchar_t rescueCueText[128]{};
bool rescueCueBad=false;
ULONGLONG rescueCueAt=0;
constexpr ULONGLONG kRescueCueMs=6000;
void RescueBanner(bool bad,const wchar_t* format,...) noexcept {
    wchar_t text[128];
    va_list a;va_start(a,format);_vsnwprintf_s(text,_countof(text),_TRUNCATE,format,a);va_end(a);
    AcquireSRWLockExclusive(&rescueCueLock);
    std::memcpy(rescueCueText,text,sizeof(text));rescueCueBad=bad;rescueCueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&rescueCueLock);
}

// A seat's riding point and its stock reach (see kSeatPoint); false when they cannot be read.
bool RidingPoint(const unsigned char* seat,float* at,float* reach) noexcept {
    const auto record=At<const unsigned char*>(seat,kSeatPoint),bone=At<const unsigned char*>(seat,kSeatPointBone);
    if(!Readable(record,kPointRadius+4) || !Readable(bone,kBoneRows+0x40))return false;
    const auto local=record+At<std::int32_t>(record,kPointLocal);
    if(!Readable(local,16))return false;
    float l[4],b[4][4];
    std::memcpy(l,local,16);std::memcpy(b,bone+kBoneRows,sizeof(b));
    for(int c=0;c<3;++c)at[c]=l[0]*b[0][c]+l[1]*b[1][c]+l[2]*b[2][c]+l[3]*b[3][c];
    *reach=At<float>(record,kPointRadius)+kReachSlack;
    return std::isfinite(at[0]+at[1]+at[2]+*reach) && *reach>=0.0f && *reach<200.0f;
}

// The free seat other than the pilot's that `human` may take (class mask) whose riding point is nearest to
// them: its index, point and reach; -1 with none.
int DoorSeat(unsigned char* v,const unsigned char* human,float* at,float* reach) noexcept {
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    const std::uint32_t mask=At<std::uint32_t>(human,kHumanRideMask);
    int best=-1;float bestDist=0.0f;
    for(unsigned i=1;i<SeatCount(v);++i) {
        const auto seat=SeatAt(v,i);
        float a[3],r=0.0f;
        if(SeatRider(seat)!=Rider::none || !(mask&At<std::uint32_t>(seat,kSeatClassMask)&At<std::uint32_t>(seat,kSeatMask)))continue;
        if(!RidingPoint(seat,a,&r))continue;
        const float d[3]={a[0]-p[0],a[1]-p[1],a[2]-p[2]};
        const float dist=std::sqrt(Dot3(d,d));
        if(best<0 || dist<bestDist){best=static_cast<int>(i);bestDist=dist;std::memcpy(at,a,12);*reach=r;}
    }
    return best;
}

// Whether `v` is a rescue heli flying a rescue, and where to (see Rescue::goal).
bool RescueGoal(const unsigned char* v,float* goal,float* height,bool* climb,bool* slow,bool* low) noexcept {
    for(const auto& r:rescues) {
        if(r.phase==RescuePhase::none || !r.ref.Is(v))continue;
        std::memcpy(goal,r.goal,12);*height=r.height;*climb=r.climb;*slow=r.slow;*low=r.low;
        return true;
    }
    return false;
}

// ---- Flight (Fly) ----
// Each frame: Sense (where it is, what it learns, whom it follows, what it shoots and where the lead is),
// RunAdvance (the run's own state), SelectMode (one Mode, in this priority), the mode's function (the
// velocity and height it wants, and how far off it is), then one controller for every mode (Steer:
// separation, the climb over what hides the target, avoidance, stick, rotor, yaw) and the guns (Fire).
// The log names the mode by kModeNames.
enum class Mode { rescue, land, guardFight, circle, aim, run, extend, escort, orbit, guard, hold };
const char* const kModeNames[]={"rescue","land","guard-fight","circle","aim","run","extend","escort","orbit","guard","hold"};

// What Fly sensed this frame.
struct Sense {
    unsigned char* v;
    const HeliType* type;
    const float* pos;
    float right[3],fwd[3],nose[3];
    float heading,dip,dt;
    ULONGLONG ms;
    Flight flight;
    bool grounded,perched;
    bool rescuing,rescueClimb,rescueSlow,rescueLow;
    float rescueGoal[3],rescueHeight;
    bool follow,engage,land,guardOrbit,rocketsLeft,hidden,wallAhead;
    float aim[3],lead[3],gunLead[3];
    float dist,dipWant,losRate,range,aimRange,bearingOff;
    Loadout arms;
    // The store (see kStoreHolder), engaged: read (store), its muzzle (storeFrom), the pass of a round fired now
    // (storePass, storeRange m from the muzzle, storeTol the miss allowed, storeWorth: the gate), and the aim point
    // that puts its muzzle on the solved arc (storeLead; storeAim: it has one: not a dropped round, solved in reach).
    bool store,storeAim,storeWorth;
    roundaim::Pass storePass;
    float storeRange,storeTol,storeFrom[3],storeLead[3];
};
// What a mode wants: the horizontal velocity and the height, how far it is off where it is going, and for
// the guard orbit its state (the log's).
struct Want { float vel[3],height,off; Orbit orbit; float orbitShift; };
// What the controller gave the inputs, for the fire and the log.
struct Control { float stickF,stickL,throttle,rotor,yaw,offYaw; Avoidance avoid; };
// What the guns did, for the log.
struct Shot { bool gun,missile; float miss,cone; };

// In contact (veh+0x1580 bit 1, see kGroundContact) and on the ground: the map (terrain, a building's roof) within
// kGroundContact under it (`below`, m: the ray down, < 0 none), else perched on another body (a heli, a vehicle, an
// enemy). Without the ray (`probed` false) every contact counts as the ground. The NPC's Sense and the player's
// PlayerHeli both.
bool OnGround(bool contact,bool probed,float below) noexcept {
    return contact && (!probed || (below>=0.0f && below<kGroundContact));
}

// The store's round as its weapon fires it (see kStoreHolder): its speed, fall (the world's gravity, or kGravity down
// when that cannot be read), owner move and life; the cone (FireAccuracy x weapon+0xE14 as launcher.cpp Cone reads it),
// the blast and the FireVector (in the vehicle's frame; zero: none). False when it cannot be read.
struct StoreRound { roundaim::Round r; float cone,blast,fireVector[3]; };
bool ReadStore(const unsigned char* w,StoreRound* out) noexcept {
    __try {
        if(!Readable(w,edf::kWeaponAccuracyScale+4))return false;
        const float speed=At<float>(w,kWeaponSpeed),factor=At<float>(w,kWeaponGravity),move=At<float>(w,edf::kWeaponAmmoOwnerMove);
        const std::int32_t alive=At<std::int32_t>(w,kWeaponAlive);
        if(!std::isfinite(speed) || speed<0.0f || !std::isfinite(factor) || !std::isfinite(move) || alive<=0 || alive>100000)return false;
        float g[3]={0.0f,-kGravity,0.0f};
        float world[3];
        if(edf::WorldGravity(image,world) && vec::Len(world)>1.0f)std::memcpy(g,world,12);
        out->r.speed=speed;out->r.ownerMove=move;out->r.alive=alive;
        for(int i=0;i<3;++i)out->r.drop[i]=g[i]*factor/3600.0f;
        const float accuracy=At<float>(w,edf::kWeaponAccuracy),scale=At<float>(w,edf::kWeaponAccuracyScale);
        const float k=std::isfinite(scale) && scale>0.0f && scale<=4.0f ? scale : 1.0f;
        out->cone=std::isfinite(accuracy) && accuracy>0.0f && accuracy<1.0f ? accuracy*k : 0.0f;
        const float blast=At<float>(w,kWeaponExplosion);
        out->blast=std::isfinite(blast) && blast>0.0f && blast<100.0f ? blast : 0.0f;
        std::memcpy(out->fireVector,w+kWeaponFireVector,12);
        if(!std::isfinite(out->fireVector[0]+out->fireVector[1]+out->fireVector[2]) || vec::Len(out->fireVector)<0.5f)
            std::memset(out->fireVector,0,12);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The store's muzzle (its muzzles' mean, as fire builds a shot) and the way its round leaves now: along the muzzle,
// or a FireVector turned by the vehicle's rows. False when it cannot be read.
bool StoreMuzzle(const unsigned char* v,const unsigned char* w,const StoreRound& sr,float* pos,float* dir) noexcept {
    __try {
        if(!edf::MeanMuzzle(w,8,pos,dir))return false;
        const float* at=reinterpret_cast<const float*>(v+kPosition);
        if(vec::Dist(pos,at)>kMuzzleReach)return false;
        if(vec::Len(sr.fireVector)>0.5f) {
            const float* m=reinterpret_cast<const float*>(v+kMatrix);
            for(int i=0;i<3;++i)dir[i]=m[i]*sr.fireVector[0]+m[4+i]*sr.fireVector[1]+m[8+i]*sr.fireVector[2];
            if(!vec::Normalize(dir))return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The store, engaged (see kStoreHolder): the pass of a round fired now and the gate, and (a round it aims) the point the
// nose is to be put on so the muzzle lies along the solved arc: the nose turned by what the muzzle is off the solution
// (the muzzle sits fixed on the airframe, so turning the nose turns it the same).
void StoreSense(const Heli& h,Sense& s) noexcept {
    s.store=s.storeAim=s.storeWorth=false;
    const unsigned char* w=s.arms.store;
    StoreRound sr{};
    float dir[3];
    if(!w || !ReadStore(w,&sr) || !StoreMuzzle(s.v,w,sr,s.storeFrom,dir))return;
    s.store=true;
    s.storePass=roundaim::Fire(sr.r,s.storeFrom,dir,h.vel,s.aim,h.tgtVel);
    s.storeRange=vec::Dist(s.storeFrom,s.storePass.round);
    const float hit=sr.blast>kHitRadius ? sr.blast : kHitRadius;
    s.storeWorth=roundaim::Worth(s.storePass,s.storeRange,sr.cone,hit,kStoreSpread,&s.storeTol);
    if(vec::Len(sr.fireVector)>0.5f)return;   // a dropped round: nothing to aim, the run takes it over the target
    float want[3];roundaim::Pass solved{};
    if(!roundaim::Solve(sr.r,s.storeFrom,h.vel,s.aim,h.tgtVel,hit,want,&solved))return;
    float nose[3];
    for(int i=0;i<3;++i)nose[i]=s.nose[i]+want[i]-dir[i];
    if(!vec::Normalize(nose))return;
    const float reach=vec::Dist(s.pos,s.aim);
    for(int i=0;i<3;++i)s.storeLead[i]=s.pos[i]+nose[i]*reach;
    s.storeAim=true;
}

// Whether the store may fire now (its rounds left, HeliMissileMs since its last burst left): then the run aims it.
bool StoreReady(const Heli& h,const Sense& s) noexcept {
    return s.store && Cfg().heliMissile && h.storeAmmo>0 && s.ms-h.storeShotAt>static_cast<ULONGLONG>(Cfg().heliMissileMs);
}

bool CommandMoving(Heli& h,const float* pos,const float* anchor) noexcept {
    const float arrive=std::fmax(40.0f,h.cmd.order==Order::guard ? Cfg().heliGuardRadius+20.0f : Cfg().heliCombatRange);
    const float leash=std::fmax(Cfg().heliRange,arrive+100.0f);
    return AirCommandTransit(h.cmd,h.cmdMoving,pos,anchor,arrive,leash);
}

// The first frame (false): it only starts its state. Else (true) the frame's sensing in `s`.
bool SenseFrame(Heli& h,unsigned char* v,bool playerAboard,Sense& s) noexcept {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    s.v=v;s.type=h.type;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    s.pos=pos;
    if(!Row(v,kHeadRight,s.right) || !Row(v,kHeadForward,s.fwd))return false;
    const float* fwd=s.fwd;
    s.heading=std::atan2(fwd[0],fwd[2]);
    std::memcpy(h.pos,pos,12);
    if(!h.started) {
        h.started=true;h.last=now;std::memcpy(h.prev,pos,12);
        if(!h.guard)std::memcpy(h.hold,pos,12);   // a guard heli's is its post (HeliCalled)
        const float rotor=At<float>(v,kRotor);
        h.hover=std::isfinite(rotor) && rotor>0.2f && rotor<1.0f ? rotor : 0.5f;
        h.prevHeading=s.heading;h.yawSign=1;
        return false;
    }
    const float dt=Clamp(static_cast<float>(now.QuadPart-h.last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    s.dt=dt;h.last=now;
    if(h.tuned) {   // see Tune
        Put<float>(v,kSpeedGain,h.params[0]);Put<float>(v,kBlend,h.params[1]);
        Put<float>(v,kMaxYaw,h.params[2]);Put<float>(v,kYawSmooth,h.params[3]);
        h.applied=true;
    }
    for(int i=0;i<3;++i){const float raw=(pos[i]-h.prev[i])/dt;h.vel[i]+= (raw-h.vel[i])*0.3f;h.prev[i]=pos[i];}
    // In contact (see kGroundContact): on the ground, or perched on another body.
    const bool contact=(v[kContact]&kContactGround)!=0,probed=contact && Cfg().heliAvoid && rayOk;
    const float down[3]={pos[0],pos[1]-kRoofProbe*2.0f,pos[2]};
    s.grounded=OnGround(contact,probed,probed ? CastRay(pos,down) : -1.0f);
    s.perched=contact && !s.grounded;

    // Learn the yaw sign from the turn the last input produced.
    const float turned=Wrap(s.heading-h.prevHeading);h.prevHeading=s.heading;
    h.yawRate+=(turned/dt-h.yawRate)*0.3f;
    if(!s.grounded && !h.yawLocked && std::fabs(h.lastYaw)>0.3f && std::fabs(turned)>0.0005f) {
        h.votes+=(turned>0)==(h.lastYaw*static_cast<float>(h.yawSign)>0) ? 1 : -1;   // did it turn the way we meant?
        if(h.votes<=-15){h.yawSign=-h.yawSign;h.votes=0;Log("HELI v=%p yaw sign flipped to %d",v,h.yawSign);}
        else if(h.votes>=30){h.yawLocked=true;Log("HELI v=%p yaw sign locked at %d",v,h.yawSign);}
    }
    // The nose (the guns are fixed along it), pitch included.
    const float* noseRow=reinterpret_cast<const float*>(v+kMatrix+0x20);
    const float noseLen=std::sqrt(noseRow[0]*noseRow[0]+noseRow[1]*noseRow[1]+noseRow[2]*noseRow[2]);
    const bool noseOk=std::isfinite(noseLen) && noseLen>0.5f;
    s.nose[0]=noseOk ? noseRow[0]/noseLen : fwd[0];s.nose[1]=noseOk ? noseRow[1]/noseLen : 0.0f;s.nose[2]=noseOk ? noseRow[2]/noseLen : fwd[2];
    s.dip=-std::asin(Clamp(s.nose[1],-1.0f,1.0f));

    // Who to follow, what to shoot, and how they move.
    const ULONGLONG ms=GameMs();
    s.ms=ms;
    s.flight=FlightOf(h,ms);
    if(h.leaving && !playerAboard && (Dist2(pos,player.pos)>kGoneFar || GameMs()-h.leftAt>kLeaveMaxMs))h.reap=true;
    // A sea rescue (RescueTick) flies to its goal, fighting nothing, following nobody.
    s.rescuing=!h.leaving && RescueGoal(v,s.rescueGoal,&s.rescueHeight,&s.rescueClimb,&s.rescueSlow,&s.rescueLow);
    s.follow=!s.rescuing && !playerAboard && player.at && ms-player.at<10000 && !h.guard && !h.leaving && !h.ferry;
    if(s.follow)TrackPlayerStill();
    if(s.follow && player.at!=h.playerAt) {
        const float pdt=h.playerAt ? static_cast<float>(player.at-h.playerAt)*0.001f : 0.0f;
        TrackVelocity(h.pPrev,h.pVel,player.pos,pdt>0.005f ? pdt : 0.005f,40.0f,!h.playerAt || pdt>0.5f);
        h.playerAt=player.at;
    }
    if(!s.follow){h.pVel[0]=h.pVel[1]=h.pVel[2]=0.0f;h.playerAt=0;}
    const float* anchor=s.follow ? player.pos : h.ferry ? h.ferryAt : h.guard ? h.post : pos;
    // Following the player it only takes on enemies its gun reaches from within heliCombatRange of them.
    const float gunRange=GunRange(*s.type);
    const float pick=s.follow && Cfg().heliCombatRange+gunRange<Cfg().heliRange ? Cfg().heliCombatRange+gunRange : Cfg().heliRange;
    const bool medic=Medic(v);
    if(medic!=h.medic)Log("HELI v=%p %s",v,medic ? "medic: its door guns heal, it aims at hurt friends" : "no longer a medic");
    h.medic=medic;
    const bool moving=!h.focus && CommandMoving(h,pos,anchor);   // a focus order: at it first
    s.engage=!moving && !h.ferry && !s.rescuing && !h.leaving && PickTarget(h,v,anchor,pos,pick,s.aim);
    if(s.engage) {
        TrackVelocity(h.tgtPrev,h.tgtVel,s.aim,dt,40.0f,!Same(h.tracked,h.target));
        h.tracked=h.target;
    } else {
        h.tracked=ObjRef{};h.extend=false;
    }
    s.arms=Arms(h,v,ms);
    if(h.called && !h.leaving && !playerAboard && !s.rescuing)
        if(const char* why=LeaveReason(h,v,s.arms))StartLeave(h,pos,fwd,why);
    // Lead the target by the rounds' flight time and drop: the gun's, and the rockets' for a rocket-run type
    // (the 409), whose nose aims them (its gun is in a turret). The nose, the dip and the fire test use the lead.
    s.dist=s.dipWant=s.losRate=0.0f;
    // The store's rounds: a burst left when its count fell (the weapon's own interval and burst decide when).
    if(s.arms.store) {
        const std::int32_t ammo=s.arms.ammo[kStoreHolder];
        if(ammo<h.storeAmmo)h.storeShotAt=ms;
        h.storeAmmo=ammo;
    }
    if(s.engage) {
        const float gunSpeed=s.arms.gunSpeed>1.0f ? s.arms.gunSpeed : kBulletSpeed;
        LeadPoint(pos,s.aim,h.tgtVel,s.arms.gunGravity,[gunSpeed](float d) noexcept { return d/gunSpeed; },s.gunLead);
        StoreSense(h,s);
        if(s.type->attack==Attack::rocketRun && s.arms.rockets)LeadPoint(pos,s.aim,h.tgtVel,s.arms.rocketGravity,RocketTime,s.lead);
        else if(s.storeAim && StoreReady(h,s))std::memcpy(s.lead,s.storeLead,12);   // the nose onto the store's arc
        else std::memcpy(s.lead,s.gunLead,12);
        const float d[3]={s.lead[0]-pos[0],s.lead[1]-pos[1],s.lead[2]-pos[2]};
        s.dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        s.dipWant=std::atan2(-d[1],std::sqrt(Dot2(d,d)));
        // Bearing rate of the target: d/dt atan2(dx, dz) with the relative velocity.
        const float rel[3]={h.tgtVel[0]-h.vel[0],0,h.tgtVel[2]-h.vel[2]};
        if(Dot2(d,d)>1.0f)s.losRate=(d[2]*rel[0]-d[0]*rel[2])/Dot2(d,d);
    }
    const float toPlayer[3]={player.pos[0]-pos[0],0,player.pos[2]-pos[2]};
    const bool byPlayer=s.follow && Dot2(toPlayer,toPlayer)<kBoardRange*kBoardRange;
    // Land by a player who stands still with no enemy about (so they can walk up and take it over),
    // and stay down while they are next to it. "No enemy about" is none within heliRange of the player
    // for heliLandMs, not "no target picked this frame": the pick range is narrower while following,
    // and between two kills there is none, which used to set it down mid-fight.
    const ULONGLONG game=GameMs();
    if(!s.follow)h.enemyAt=game;
    else ForEachEnemy(v,[&](const void*,const float* p) noexcept {
        const float d[3]={p[0]-player.pos[0],0,p[2]-player.pos[2]};
        if(Dot2(d,d)<Cfg().heliRange*Cfg().heliRange)h.enemyAt=game;
    });
    const bool quiet=game-h.enemyAt>Cfg().heliLandMs;
    s.land=s.follow && !s.engage && quiet && Cfg().heliLandMs && (game-stillAt>Cfg().heliLandMs || (s.grounded && byPlayer));
    if(h.ferry)s.land=h.ferryLand && Dist2(pos,h.ferryAt)<kFerryLandNear;   // a ferry lands on its point, nowhere else
    h.grounded=s.grounded;

    // The map between it and the target, and a wall along the nose (see kAimWall).
    s.hidden=s.wallAhead=false;
    if(s.engage && Cfg().heliAvoid && rayOk) {
        const float nosePt[3]={pos[0]+fwd[0]*kAimWall,pos[1],pos[2]+fwd[2]*kAimWall};
        s.wallAhead=!h.extend && CastRay(pos,nosePt)>=0.0f;
        const float seen=CastRay(pos,s.lead);
        s.hidden=seen>=0.0f && seen<s.dist-kLosSlack;
    }
    s.range=gunRange;
    s.aimRange=s.range<kRunAim ? s.range : kRunAim;
    s.rocketsLeft=s.arms.rockets && s.arms.rocketAmmo>0;
    // A guard heli circles its post (GuardOrbit); the engaged 410 too, round a centre moved toward the target.
    s.guardOrbit=h.guard && !h.focus && !h.ferry && !h.leaving && !s.rescuing && Cfg().heliGuardRadius>0.0f;   // focused: round its target
    // Aim only once the nose has come round: with the target behind, the dip stick flew it away.
    s.bearingOff=s.engage ? Wrap(std::atan2(s.lead[0]-pos[0],s.lead[2]-pos[2])-s.heading) : kPi;
    return true;
}

// Engaged, whether it circles the target instead of running at it: the 410 always (its guns are in its doors);
// the 409 for its turret after a rocket run broke off, and while its rockets are spent (see kTurretCircleMs).
bool Circling(const Heli& h,const Sense& s) noexcept {
    return s.type->attack==Attack::gunship ||
           (s.type->attack==Attack::rocketRun && (h.circleUntil>s.ms || !s.rocketsLeft));
}

// The run's state before the mode is picked: the 409 out of its circle into another run once it circled long
// enough, then (running) the break-off and the extension's end (RunAdvance).
void Advance(Heli& h,const Sense& s) noexcept {
    if(!s.engage)return;
    if(s.type->attack==Attack::rocketRun && h.circleUntil && s.ms>=h.circleUntil) {
        h.circleUntil=0;
        if(s.rocketsLeft)StartExtend(h,s.pos,s.aim,s.flight,s.follow,s.ms);
    }
    if(Circling(h,s))return;
    RunAdvance(h,s.pos,s.aim,s.dist<s.aimRange ? s.dipWant : 0.0f,s.wallAhead,s.flight,s.follow,
               s.type->attack==Attack::rocketRun,s.ms);
}

// The one mode it flies this frame, highest priority first.
Mode SelectMode(const Heli& h,const Sense& s) noexcept {
    if(s.rescuing)return Mode::rescue;
    if(s.land)return Mode::land;
    if(s.engage) {
        if(Circling(h,s))return s.guardOrbit && s.type->attack==Attack::gunship ? Mode::guardFight : Mode::circle;
        if(h.extend)return Mode::extend;
        return s.dist<s.aimRange && std::fabs(s.bearingOff)<kAimOff ? Mode::aim : Mode::run;
    }
    if(s.follow)return roaming ? Mode::escort : Mode::orbit;
    return s.guardOrbit ? Mode::guard : Mode::hold;
}

const float kRest[3]={0,0,0};

// Rescue: to the rescue's goal (only up while `climb`), at most kFerrySpeed while it carries the player.
Want FlyRescue(const Heli& h,const Sense& s) noexcept {
    Want w{};
    Arrive(h,s.pos,s.rescueClimb ? s.pos : s.rescueGoal,kRest,w.vel);
    Limit2(w.vel,s.rescueSlow && kFerrySpeed<h.top ? kFerrySpeed : h.top);
    w.off=Dist2(s.pos,s.rescueGoal);w.height=s.rescueHeight;
    return w;
}

// Land: beside the player (its wing's place farther out), aiming below the ground so it comes down; a ferry on its point.
Want FlyLand(const Heli& h,const Sense& s) noexcept {
    Want w{};
    if(h.ferry) {
        Arrive(h,s.pos,h.ferryAt,kRest,w.vel);
        w.off=Dist2(s.pos,h.ferryAt);w.height=h.ferryAt[1]-10.0f;
        return w;
    }
    float dir[3]={s.pos[0]-player.pos[0],0,s.pos[2]-player.pos[2]};
    float len=std::sqrt(Dot2(dir,dir));
    if(len<1.0f){dir[0]=-s.fwd[0];dir[2]=-s.fwd[2];len=1.0f;}
    const float r=Cfg().heliFollow<kLandDistance ? Cfg().heliFollow : kLandDistance;
    const float out=r+WingGap(h)*static_cast<float>(s.flight.wing);
    const float spot[3]={player.pos[0]+dir[0]/len*out,0,player.pos[2]+dir[2]/len*out};
    Arrive(h,s.pos,spot,kRest,w.vel);
    w.off=Dist2(s.pos,spot);
    w.height=player.pos[1]-10.0f;   // below the ground: it descends until it touches down
    return w;
}

// Guard fight (an engaged guard 410): its guard orbit, the centre moved toward the target.
Want FlyGuardFight(Heli& h,const Sense& s) noexcept {
    Want w{};
    h.extend=false;
    float centre[3];
    w.orbitShift=OrbitCentre(h,s.aim,centre);
    w.orbit=GuardOrbit(h,s.pos,s.fwd,centre,s.dt,s.ms,w.vel);
    w.off=std::fabs(w.orbit.dist-Cfg().heliGuardRadius);
    const float over=s.aim[1]+s.type->circleHeight;
    w.height=(h.hold[1]>over ? h.hold[1] : over)+Stack(s.flight);
    return w;
}

// Circle: round the target on its type's circle (the 409's turret circle, the 410's gunship circle).
Want FlyCircle(Heli& h,const Sense& s) noexcept {
    Want w{};
    h.extend=false;
    const float ground=s.follow && player.pos[1]>s.aim[1] ? player.pos[1] : s.aim[1];
    w.height=ground+s.type->circleHeight+Stack(s.flight);
    w.off=Circle(h,s.pos,s.fwd,s.aim,h.tgtVel,s.type->circleRadius,s.type->circleSpeed,w.vel);
    return w;
}

// Run / aim: running in at the target (aim: the forward stick then holds the nose dip, see Steer).
Want FlyRun(const Heli& h,const Sense& s) noexcept {
    Want w{};
    w.height=RunHeight(s.aim,s.flight,s.follow);
    w.off=RunIn(h,s.pos,s.aim,s.follow,w.vel);
    return w;
}

// Extend: out to the extension point after a break-off, at the run's height.
Want FlyExtend(const Heli& h,const Sense& s) noexcept {
    Want w{};
    w.height=RunHeight(s.aim,s.flight,s.follow);
    w.off=Extend(h,s.pos,w.vel);
    return w;
}

// Escort / orbit: the formation round the player (see kRoamSpan).
Want FlyFormation(const Heli& h,const Sense& s) noexcept {
    Want w{};
    w.off=Formation(h,s.pos,s.fwd,s.flight,w.vel,&w.height);
    return w;
}

// Guard: its guard orbit round the post.
Want FlyGuard(Heli& h,const Sense& s) noexcept {
    Want w{};
    float centre[3];
    OrbitCentre(h,nullptr,centre);
    w.orbit=GuardOrbit(h,s.pos,s.fwd,centre,s.dt,s.ms,w.vel);
    w.off=std::fabs(w.orbit.dist-Cfg().heliGuardRadius);w.height=h.hold[1]+Stack(s.flight);
    return w;
}

// Hold: where it was left (nobody to follow, no post).
Want FlyHold(const Heli& h,const Sense& s) noexcept {
    Want w{};
    Arrive(h,s.pos,h.hold,kRest,w.vel);
    w.off=Dist2(s.pos,h.hold);w.height=h.hold[1];
    return w;
}

Want FlyMode(Heli& h,const Sense& s,Mode mode) noexcept {
    switch(mode) {
    case Mode::rescue: return FlyRescue(h,s);
    case Mode::land: return FlyLand(h,s);
    case Mode::guardFight: return FlyGuardFight(h,s);
    case Mode::circle: return FlyCircle(h,s);
    case Mode::aim:
    case Mode::run: return FlyRun(h,s);
    case Mode::extend: return FlyExtend(h,s);
    case Mode::escort:
    case Mode::orbit: return FlyFormation(h,s);
    case Mode::guard: return FlyGuard(h,s);
    case Mode::hold: break;
    }
    return FlyHold(h,s);
}

// The controller every mode shares, on what the mode wants: the push from the other helis (not landing), the
// climb over what hides the target, unsticking a perched heli, avoidance; then the stick for the wanted
// velocity (aiming: the nose dip), the rotor for the height, the yaw; written into the input block.
Control Steer(Heli& h,const Sense& s,Mode mode,Want& w) noexcept {
    const float* pos=s.pos;
    const bool land=mode==Mode::land;
    if(!land)Separate(h,pos,w.vel,s.ms);
    h.losLift=Clamp(h.losLift+(s.hidden || s.wallAhead ? kLosClimb : -kLosSink)*s.dt,0.0f,kLosMax);
    if(s.engage)w.height+=h.losLift;
    if(s.perched) {
        // The higher of it and the nearest other heli climbs off, the lower one sinks away.
        const Heli* by=nullptr;float nearest=0.0f;
        for(const auto& o:helis) {
            if(&o==&h || !o.ref || s.ms-o.seen>2000)continue;
            const float d=Dist2(pos,o.pos);
            if(!by || d<nearest){by=&o;nearest=d;}
        }
        const bool above=!by || nearest>30.0f || pos[1]>by->pos[1] || (pos[1]==by->pos[1] && &h<by);
        w.height=pos[1]+(above ? kUnstick : -kUnstick);
    }
    Control c{};
    c.avoid=Avoid(pos,h.vel,w.vel,&w.height,land || (mode==Mode::rescue && s.rescueLow),h.stopDecel);
    if((s.follow || s.engage) && !h.guard && !h.leaving)std::memcpy(h.hold,pos,12);

    // Horizontal: the stick for the wanted velocity (full stick flies h.top) plus kBrakeGain per m/s it is
    // off, on the heading rows.
    float forward=0.0f,lateral=0.0f;
    aim::StockStick(w.vel,h.vel,h.top,kBrakeGain,s.fwd,s.right,&forward,&lateral);
    // Aiming, the forward stick is the nose dip (kMaxTilt at full stick), and it creeps along the nose.
    if(mode==Mode::aim)forward=Clamp((s.dipWant+(s.dipWant-s.dip)*kPitchGain)/kMaxTilt,-1.0f,1.0f);

    // Vertical: the rotor (kRotor) is the lift, and it trails the throttle by seconds (spooling down
    // slower than up). So altitude -> climb rate -> wanted rotor -> a throttle that drives the rotor
    // there. h.hover is the rotor that holds height; it is learned only near the goal, where the
    // climb rate is not saturated (learning on the climb winds it up to 1 and it overshoots by 20 m).
    const float dy=w.height-pos[1];
    const float climb=Clamp(dy*0.25f,-3.0f,3.0f);
    c.rotor=At<float>(s.v,kRotor);
    c.throttle=aim::StockThrottle(climb,h.vel[1],c.rotor,&h.hover,std::fabs(dy)<6.0f,s.dt,kRotorGains);
    c.stickF=forward;c.stickL=lateral;
    if(s.grounded)h.groundAt=s.ms;
    if(land && s.grounded){c.throttle=0.0f;c.stickF=c.stickL=0.0f;}
    else if(s.ms-h.groundAt<kLiftOffMs){c.stickF=c.stickL=0.0f;}

    // Yaw: running in, onto the target (with its bearing rate fed forward); else (circling too) the way it
    // flies (so it banks round), or the player when slow.
    float face[3]={0,0,0},faceRate=0.0f;
    if(mode==Mode::run || mode==Mode::aim){face[0]=s.lead[0]-pos[0];face[2]=s.lead[2]-pos[2];faceRate=s.losRate;}
    else if(std::sqrt(Dot2(w.vel,w.vel))>kFaceSpeed){face[0]=w.vel[0];face[2]=w.vel[2];}
    else if(s.follow){face[0]=player.pos[0]-pos[0];face[2]=player.pos[2]-pos[2];}
    c.yaw=0.0f;c.offYaw=kPi;
    if(Dot2(face,face)>1.0f) {
        c.offYaw=Wrap(std::atan2(face[0],face[2])-s.heading);
        c.yaw=aim::StockYaw(c.offYaw,h.yawRate,faceRate,kYawDamp,kYawFeed)*static_cast<float>(h.yawSign);
    }
    h.lastYaw=c.yaw;

    Put<float>(s.v,kInLateral,c.stickL);Put<float>(s.v,kInForward,c.stickF);Put<float>(s.v,kInThrottle,c.throttle);
    Put<float>(s.v,kInW,1.0f);Put<float>(s.v,kInYaw,c.yaw);
    return c;
}

// Fire: the nose within the cone of the gun's lead point (heliFireCone, wider up close so kHitRadius at the
// target still counts), that within the guns' reach, and not through the player; a turret gun (the 409's)
// also at anything below within its type's turretReach. The missile homes, so it goes with a rough aim and
// farther; a rocket-run type's rockets fly straight, so they want a tight one. Guns fire in bursts. A type
// with door guns fires through their blocks instead (DoorGun).
Shot Fire(Heli& h,const Sense& s,Mode mode) noexcept {
    const float* pos=s.pos;
    const auto missOf=[&](const float* p,float* along) noexcept {
        const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
        *along=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(*along<1.0f)return 0.0f;
        return std::acos(Clamp((d[0]*s.nose[0]+d[1]*s.nose[1]+d[2]*s.nose[2])/ *along,-1.0f,1.0f))*180.0f/kPi;
    };
    const auto coneAt=[](float base,float d) noexcept {
        const float wide=d>1.0f ? std::atan(kHitRadius/d)*180.0f/kPi : 90.0f;
        return wide>base ? wide : base;
    };
    const bool land=mode==Mode::land,rescuing=mode==Mode::rescue;
    Shot shot{false,false,180.0f,Cfg().heliFireCone};
    float gunDist=0.0f;
    if(s.engage) {
        shot.miss=missOf(s.gunLead,&gunDist);
        shot.cone=coneAt(Cfg().heliFireCone,gunDist);
    }
    if(s.engage && Cfg().heliFire && !s.grounded && !land && !s.hidden && !PlayerInLine(pos,s.lead)) {
        const bool turret=s.type->turretReach>0.0f && gunDist<s.type->turretReach && s.aim[1]<pos[1] &&
                          TurretOn(s.v,s.arms.gunWeapon,s.gunLead,gunDist);
        shot.gun=(shot.miss<shot.cone && gunDist<s.range) || turret;
        if(shot.gun && !h.firing){h.firing=true;h.burstAt=s.ms;}
        if(h.firing && s.ms-h.burstAt>kBurstMs){h.firing=false;h.restUntil=s.ms+kBurstRest;}
        if(!shot.gun)h.firing=false;
        if(s.ms<h.restUntil)shot.gun=false;
        if(s.type->attack==Attack::rocketRun && s.arms.rockets) {
            float rocketDist=0.0f;
            const float rocketMiss=missOf(s.lead,&rocketDist);
            shot.missile=rocketMiss<coneAt(kRocketCone,rocketDist) && rocketDist<kRocketRange;
        } else if(s.arms.store) {
            // The store's own arc from its muzzle as it points now (StoreSense): held while the heli's attitude has it
            // off the solution, and never onto the player. Unread this frame (s.store false), it holds.
            const bool clear=!player.at || s.ms-player.at>2000 ||
                             (vec::Dist(s.storePass.round,player.pos)>kStoreClear && !NearLine(s.storeFrom,s.storePass.round,player.pos,8.0f));
            shot.missile=s.store && StoreReady(h,s) && s.storeWorth && clear;
            if(shot.missile && Cfg().debug && s.ms-h.missileAt>500) {
                h.missileAt=s.ms;
                Log("HELI v=%p store fire: target=%p miss=%.1f m (allowed %.1f) %.0f m from the muzzle, %.0f frames, ammo=%d",
                    s.v,h.target.obj,s.storePass.miss,s.storeTol,s.storeRange,s.storePass.frames,h.storeAmmo);
            }
        } else {
            shot.missile=Cfg().heliMissile && shot.miss<kMissileCone && s.dist>kMissileMin && s.dist<Cfg().heliRange &&
                         s.ms-h.missileAt>Cfg().heliMissileMs;
            if(shot.missile)h.missileAt=s.ms;
        }
    } else h.firing=false;
    if(s.type->guns==Guns::nose){s.v[kFireGun]=shot.gun;s.v[kFireMissile]=shot.missile;}
    else if(doorOk && Cfg().heliDoorGuns && SeatCount(s.v)>=3)
        for(int i=0;i<2;++i)DoorGun(h.doors[i],h.target,!h.called,s.v,i,s.grounded || land || rescuing,s.dt,s.ms);
    return shot;
}

// The guard orbit's changes (always) and state (Debug, each second), and the flight data (Debug, each second).
void FlyLog(Heli& h,const Sense& s,Mode mode,const Want& w,const Control& c,const Shot& shot) noexcept {
    const unsigned char* v=s.v;
    const float* pos=s.pos;
    const ULONGLONG ms=s.ms;
    const bool orbiting=mode==Mode::guardFight || mode==Mode::guard;
    if(h.guard && orbiting!=h.orbiting) {
        h.orbiting=orbiting;
        if(orbiting)Log("HELI v=%p guard orbit on: post (%.0f,%.0f,%.0f), radius %.0f m, speed %.1f m/s, %.0f m from the post",
                        v,h.post[0],h.post[1],h.post[2],Cfg().heliGuardRadius,w.orbit.speed,Dist2(pos,h.post));
        else Log("HELI v=%p guard orbit off: %s",v,h.leaving ? "leaving" : s.rescuing ? "rescue" : s.engage ? "gun run (fixed guns)" :
                 "hold");
    }
    if(orbiting && Cfg().debug && ms-h.orbitLogAt>1000) {
        h.orbitLogAt=ms;
        Log("HELI v=%p guard orbit%s: centre=(%.0f,%.0f) shift=%.0f r=%.0f dist=%.0f angle=%.0f want=%.0f place=%d/%d spd=%.1f/%.1f y=%.1f goal=%.1f target=%p tdist=%.0f",
            v,s.engage ? " (fighting)" : "",h.orbitCentre[0],h.orbitCentre[2],w.orbitShift,Cfg().heliGuardRadius,w.orbit.dist,
            w.orbit.angle*180.0f/kPi,w.orbit.want*180.0f/kPi,w.orbit.place,w.orbit.count,std::sqrt(Dot2(h.vel,h.vel)),w.orbit.speed,pos[1],
            w.height,s.engage ? h.target.obj : nullptr,s.engage ? Dist2(pos,s.aim) : 0.0f);
    }
    if(!Cfg().debug || ms-h.loggedAt<=1000)return;
    h.loggedAt=ms;
    const float speed=std::sqrt(Dot2(h.vel,h.vel)),aimedLead=Dist2(s.aim,s.lead);
    const Avoidance& a=c.avoid;
    Log("HELI v=%p %s%s flight=%d.%d ammo=%d/%d/%d y=%.1f goal=%.1f vy=%.2f thr=%.3f hover=%.3f rotor=%.3f fwd=%.2f lat=%.2f yaw=%.2f rate=%.0fdeg/s sign=%d%s votes=%d dGoal=%.0f ground=%d target=%p dist=%.0f off=%.0fdeg miss=%.1fdeg pitch=%.0fdeg gun=%d msl=%d spd=%.1f want=%.1f dipWant=%.0fdeg cone=%.1fdeg lead=%.1f tv=%.1f los=%.0fdeg/s ahead=%.0f clear=%.0f roof=%.0f any=%.0f/%X lift=%.0f%s%s",
        v,s.perched ? "perched " : "",kModeNames[static_cast<int>(mode)],
        s.flight.group,s.flight.wing,s.arms.ammo[0],s.arms.ammo[1],s.arms.ammo[2],pos[1],w.height,h.vel[1],c.throttle,h.hover,c.rotor,
        c.stickF,c.stickL,c.yaw,h.yawRate*180.0f/kPi,h.yawSign,h.yawLocked ? "(locked)" : "",h.votes,w.off,s.grounded,
        s.engage ? h.target.obj : nullptr,s.dist,c.offYaw*180.0f/kPi,shot.miss,-s.dip*180.0f/kPi,shot.gun,shot.missile,
        speed,std::sqrt(Dot2(w.vel,w.vel)),s.dipWant*180.0f/kPi,shot.cone,aimedLead,std::sqrt(Dot2(h.tgtVel,h.tgtVel)),s.losRate*180.0f/kPi,
        a.ahead,a.clear,a.roof,a.anyRoof,a.anyFlags,h.losLift,s.hidden ? " hidden" : "",s.wallAhead ? " wall" : "");
    if(s.store)Log("HELI v=%p store: miss=%.1f allowed=%.1f range=%.0f frames=%.0f worth=%d ready=%d aimed=%d ammo=%d",
                   v,s.storePass.miss,s.storeTol,s.storeRange,s.storePass.frames,s.storeWorth,StoreReady(h,s),s.storeAim,h.storeAmmo);
}

// The soft edge (airbound.h; the user 2026-10-06: a small edge before the edge, past it they come back). A heli is
// held inside the move area (shrunk by its inset, veh+kAreaInset) by the stock input, which teleports it back: one
// chasing out there sat on that edge (or, on the big map, 650 m out past the ground's edge, BigWorld's widened area).
// Its edge: that box and the play edge (crew.h PlayEdge) overlapped; its soft edge that less the band (ini HeliSoftEdge,
// at least what it takes to stop from full speed, kHeliSoftReact s of it at full speed included). Its post and its hold
// are put inside it (a guard post or a map command's point out there: its guard circle stays inside), and its wanted
// velocity out across it is cut to what still stops on it (LimitOut); past it, in at kHeliBackShare of its top speed.
// Rescuing or landing by the player it goes where the player is.
constexpr std::size_t kAreaInset=0xE00;
constexpr float kHeliSoftReact=0.5f,kHeliBackShare=0.3f;
void SoftEdge(Heli& h,const Sense& s,Mode mode,Want& w) noexcept {
    const airbound::Box edge=HeldBox(At<float>(s.v,kAreaInset));   // mapbounds.h
    const float brake=h.stopDecel>0.5f ? h.stopDecel : 0.5f;
    const float stop=h.top*h.top/(2.0f*brake)+h.top*kHeliSoftReact,least=Cfg().heliSoftEdge;
    float band=stop>least ? stop : least;
    const float most=airbound::HalfOf(edge)*0.5f;
    if(band>most)band=most;
    const airbound::Box soft=airbound::Inset(edge,band);
    airbound::ClampIn(soft,h.post,Cfg().heliGuardRadius);
    airbound::ClampIn(soft,h.hold,0.0f);
    const bool out=!airbound::Inside(soft,s.pos);
    if(out!=h.edgeOut) {
        h.edgeOut=out;
        Log("HELI v=%p %s its soft edge at (%.0f,%.0f): band %.0f, edge (%.0f,%.0f)-(%.0f,%.0f)",s.v,out ? "past" : "back inside",s.pos[0],
            s.pos[2],band,edge.lo[0],edge.lo[1],edge.hi[0],edge.hi[1]);
    }
    if(mode==Mode::rescue || mode==Mode::land)return;
    airbound::LimitOut(soft,s.pos,brake,kHeliSoftReact,h.top*kHeliBackShare,w.vel);
    const float top=CeilingY()-Cfg().airSoftCeil;
    if(w.height>top)w.height=top;
}

void MirrorStick(unsigned char* v,const Control& c) noexcept;   // online: the input block onto seat 0's stick (below)

void Fly(Heli& h,unsigned char* v,bool playerAboard) noexcept {
    Sense s{};
    if(!SenseFrame(h,v,playerAboard,s))return;
    Advance(h,s);
    const Mode mode=SelectMode(h,s);
    Want w=FlyMode(h,s,mode);
    SoftEdge(h,s,mode,w);
    const Control c=Steer(h,s,mode,w);
    MirrorStick(v,c);
    const Shot shot=Fire(h,s,mode);
    FlyLog(h,s,mode,w,c,shot);
}
}  // namespace

bool GunBarrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept {
    __try { return Barrel(v,weapon,pos,dir); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsHelicopter(const void* vehicle) noexcept { return TypeOf(vehicle)!=nullptr; }

// The takeoff cue (HeliCue): the player at the controls of a stock helicopter (not a plugin jet on a 506 body) with
// it on the ground (contact bit 1). The lift per rotor speed +0x1610 and the mass factor +0x161C give the rotor that
// lifts it (aim::HoverRotor: 0.424 for the 506's 34 / 60; the cue was missing for the 506 and stuck at 1% for the 602
// while it took +0x1610 for 70).
namespace {
constexpr std::size_t kLiftPerRotor=0x1610,kLiftMass=0x161C;
constexpr ULONGLONG kCueFreshMs=300;
SRWLOCK cueLock=SRWLOCK_INIT;
HeliCue cue{};
ULONGLONG cueAt=0;
}  // namespace

void HeliCueStep(unsigned char* v) noexcept {
    if(!IsHelicopter(v) || BodyOf(v)!=PluginBody::none || SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::player)return;
    if(!(At<unsigned char>(v,kContact)&2))return;
    const float lift=At<float>(v,kLiftPerRotor),mass=At<float>(v,kLiftMass);
    if(!(lift>1e-3f) || !(mass>0.0f) || !std::isfinite(lift+mass))return;
    AcquireSRWLockExclusive(&cueLock);
    cue=HeliCue{At<float>(v,kRotor),aim::HoverRotor(lift,mass)};
    cueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&cueLock);
}

bool PlayerHeliCue(HeliCue* out) noexcept {
    AcquireSRWLockShared(&cueLock);
    const bool fresh=cueAt && GetTickCount64()-cueAt<=kCueFreshMs;
    if(fresh)*out=cue;
    ReleaseSRWLockShared(&cueLock);
    return fresh;
}

namespace {
// See kSpeedGain: the heli's own top speed and brake, and with heliSpeed above its stock top speed (or
// heliYawRate above its yaw limit) the params Fly writes back every frame.
void Tune(Heli& h,const unsigned char* v) noexcept {
    h.top=kTopSpeed;h.stopDecel=kStopDecel;h.tuned=false;
    const float k=At<float>(v,kSpeedGain),b=At<float>(v,kBlend),d=At<float>(v,kDamp);
    const float yaw=At<float>(v,kMaxYaw),smooth=At<float>(v,kYawSmooth);
    const float denom=1.0f-d*(1.0f-b);
    if(!std::isfinite(k+b+d+yaw+smooth) || k<=0.0f || b<=0.0f || b>=1.0f || d<=0.5f || d>=1.0f || denom<1e-6f)return;
    h.params[0]=k;h.params[1]=b;h.params[2]=yaw;h.params[3]=smooth;
    std::memcpy(h.stock,h.params,sizeof(h.stock));
    const float frames=Cfg().heliAgility*60.0f,stockTop=b*k/denom;
    h.top=stockTop;h.stopDecel=kStopShare*stockTop*denom*60.0f;
    if(Cfg().heliSpeed>stockTop && frames>=30.0f) {
        // A strongly damped heli (Heron: d=.99) cannot coast for HeliAgility=4 s with a positive
        // blend. Keep its quicker stock response instead of abandoning the requested speed.
        const float useFrames=std::fmin(frames,1.0f/denom);
        const float blend=1.0f-(1.0f-1.0f/useFrames)/d;   // 1-d*(1-blend) = 1/useFrames
        if(blend>0.0f && blend<1.0f) {
            h.params[1]=blend;h.params[0]=Cfg().heliSpeed/(useFrames*blend);
            h.top=Cfg().heliSpeed;h.stopDecel=kStopShare*h.top*60.0f/useFrames;h.tuned=true;
        }
    }
    const float yawWant=Cfg().heliYawRate*kPi/180.0f;
    if(yawWant>std::fabs(yaw)){h.params[2]=yawWant*aim::YawSign(yaw);h.params[3]=smooth<kTunedYawSmooth ? kTunedYawSmooth : smooth;h.tuned=true;}
    Log("HELI v=%p tune: stock k=%.2f b=%.5f d=%.4f top=%.1fm/s tau=%.1fs yaw=%.0fdeg/s smooth=%.4f -> k=%.2f b=%.5f top=%.1fm/s brake=%.2fm/s2 yaw=%.0fdeg/s smooth=%.4f%s",
        v,k,b,d,stockTop,1.0f/denom/60.0f,yaw*180.0f/kPi,smooth,h.params[0],h.params[1],h.top,h.stopDecel,h.params[2]*180.0f/kPi,h.params[3],h.tuned ? "" : " (stock)");
}
}  // namespace

bool HeliCrewed(const void* vehicle) noexcept {
    if(IsJet(vehicle) || IsSub(vehicle) || IsSazabi(vehicle))return false;   // flown by jet.cpp / subcarrier.cpp / sazabi.cpp
    const HeliType* const type=TypeOf(vehicle);
    if(!type)return false;
    const ULONGLONG ms=GameMs();
    // Its own entry (started afresh), a free one, a gone object's at the same address or a stale one's: never
    // a live heli's.
    Heli* slot=Find(vehicle);
    for(auto& h:helis)if(!slot && (!h.ref || h.ref.obj==vehicle || ms-h.seen>kStaleMs))slot=&h;
    if(!slot) {
        if(ms-fullLoggedAt>10000){fullLoggedAt=ms;Log("HELI table full (16 flown): v=%p not flown by the plugin",vehicle);}
        return false;
    }
    *slot=Heli{};slot->ref=ObjRef::Of(vehicle);slot->type=type;slot->crewedAt=slot->seen=ms;slot->seenFrame=GameFrame();
    const auto c=static_cast<const unsigned char*>(vehicle);
    Log("HELI v=%p crewed (%s): the plugin flies it; maxTilt=%.3f",vehicle,type->name,At<float>(c,0x1640));
    Tune(*slot,c);
    return true;
}

namespace {
// The heli's own speed and yaw params back (Tune's stock), once its NPC no longer flies it: the player bumped it out
// (the user, 2026-10-05: a stock heli the player flies "drifts, does not stop": they flew on the NPC's tuning, its
// velocity settling over HeliAgility's 4 s instead of the stock few tenths), or the seat emptied. Left on, a later
// crew's Tune also took them for the stock ones. Its NPC back, Fly applies them again.
void Restore(Heli& h,unsigned char* v) noexcept {
    if(!h.applied)return;
    h.applied=false;
    Put<float>(v,kSpeedGain,h.stock[0]);Put<float>(v,kBlend,h.stock[1]);
    Put<float>(v,kMaxYaw,h.stock[2]);Put<float>(v,kYawSmooth,h.stock[3]);
    Log("HELI v=%p no NPC pilot: its own params back (k=%.2f b=%.5f yaw=%.0fdeg/s smooth=%.4f)",v,h.stock[0],h.stock[1],
        h.stock[2]*180.0f/kPi,h.stock[3]);
}
}  // namespace

namespace {
// The player's hover assist (ini PlayerHeliStopSec; the user, 2026-10-05: a stock heli "drifts, does not stop"): the
// stock helis' horizontal velocity settles over ~13 s (d 0.999, blend 0.0003: the log's "tau=12.8s"), so let go it
// slides on. The heli's own params made to settle over PlayerHeliStopSec instead, as Tune does for the NPC (the game's
// own velocity law, nothing written to the velocity): blend from 1-d*(1-blend) = 1/frames, k keeping its stock top
// speed (b k / (1-d (1-b))). Written every frame the player flies it (as Fly does), the stock ones back as they get off.
// The mouse-aim flight's own params (PlayerMouseTune): the heli's max yaw rate and smoothing, its rotor's up / down
// rates and its tilt smoothing are kept with the speed params and put back as the player gets off (or takes a pad).
struct Assist { ObjRef ref; float k,b,yaw,smooth,rotorUp,rotorDown,tilt; ULONGLONG seen; bool said,yawSaid; };
Assist assists[8];
constexpr ULONGLONG kAssistStaleMs=2000;

void AssistOff(unsigned char* v) noexcept {
    for(auto& a:assists) {
        if(!a.ref.Is(v))continue;
        Put<float>(v,kSpeedGain,a.k);Put<float>(v,kBlend,a.b);Put<float>(v,kMaxYaw,a.yaw);Put<float>(v,kYawSmooth,a.smooth);
        Put<float>(v,kRotorUp,a.rotorUp);Put<float>(v,kRotorDown,a.rotorDown);Put<float>(v,kTiltSmooth,a.tilt);
        a=Assist{};
        Log("HELI v=%p the player is off: its own speed and yaw params back",v);
    }
}

// The player's record of the heli: its first frame under the player takes its params as its own (Restore: no NPC
// tuning left on it).
Assist* AssistOf(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs();
    Assist* a=nullptr;
    for(auto& x:assists)if(x.ref.Is(v))a=&x;
    if(!a) {
        for(auto& x:assists)if(!a && (!x.ref || ms-x.seen>kAssistStaleMs))a=&x;
        if(!a)return nullptr;
        *a=Assist{ObjRef::Of(v),At<float>(v,kSpeedGain),At<float>(v,kBlend),At<float>(v,kMaxYaw),At<float>(v,kYawSmooth),
                  At<float>(v,kRotorUp),At<float>(v,kRotorDown),At<float>(v,kTiltSmooth),ms,false,false};
    }
    a->seen=ms;
    return a;
}

// The instructor's actuators (AimFly), each at least as quick as it needs and put back with the pad (or off):
//  - yaw: enough native angle authority/smoothing to track its rate command (AimFly inverts that angle-state lag).
//    +1634 is NOT rad/s: the native spring converts the angle to angular velocity. HeliYawRate remains the player's
//    desired rate limit;
//  - the rotor's up / down rates (0x656744 / 0x656770: rotor += rate (throttle - rotor) a frame) to aim::kPlayerRotorRate:
//    the stock 0.001 / 0.0007 lag the collective by 17 / 24 s, and no throttle can make up for it (heliaim.h
//    CollectiveThrottle; heli_aim_check's "before" rows porpoise +-10 m for 40 s after one climb);
//  - the tilt smoothing (+0x1644, pitch = lerp(pitch, maxTilt x input, it) 0x654E69) to kPlayerTiltSmooth: the stock
//    0.005 a frame lagged the nose 3 s behind the mouse. The tilt is only the attitude: the motion is the velocity law's.
constexpr float kPlayerTiltSmooth=0.05f;
float AtLeast(float stock,float want) noexcept { return std::isfinite(stock) && stock>want ? stock : want; }
void PlayerMouseTune(unsigned char* v,bool mouse) noexcept {
    Assist* const a=AssistOf(v);
    if(!a)return;
    const float yaw=a->yaw,smooth=a->smooth,want=Cfg().heliYawRate*kPi/180.0f;
    if(!std::isfinite(yaw+smooth+a->rotorUp+a->rotorDown+a->tilt))return;
    if(!mouse) {
        Put<float>(v,kMaxYaw,yaw);Put<float>(v,kYawSmooth,smooth);
        Put<float>(v,kRotorUp,a->rotorUp);Put<float>(v,kRotorDown,a->rotorDown);Put<float>(v,kTiltSmooth,a->tilt);
        return;
    }
    Put<float>(v,kRotorUp,AtLeast(a->rotorUp,aim::kPlayerRotorRate));Put<float>(v,kRotorDown,AtLeast(a->rotorDown,aim::kPlayerRotorRate));
    Put<float>(v,kTiltSmooth,AtLeast(a->tilt,kPlayerTiltSmooth));
    if(!(want>std::fabs(yaw))){Put<float>(v,kMaxYaw,yaw);Put<float>(v,kYawSmooth,smooth);return;}
    Put<float>(v,kMaxYaw,want*aim::YawSign(yaw));Put<float>(v,kYawSmooth,smooth<kTunedYawSmooth ? kTunedYawSmooth : smooth);
    if(!a->yawSaid && (a->yawSaid=true))
        Log("HELI v=%p player turn limit %.0f deg/s: native angle authority %.0f deg, smoothing %.4f (its own %.0f deg, %.4f)",v,
            Cfg().heliYawRate,want*180.0f/kPi,
            smooth<kTunedYawSmooth ? kTunedYawSmooth : smooth,yaw*180.0f/kPi,smooth);
}

void PlayerAssist(unsigned char* v) noexcept {
    const float sec=Cfg().playerHeliStopSec;
    Assist* const a=AssistOf(v);
    if(!a)return;
    if(sec<=0.0f){Put<float>(v,kSpeedGain,a->k);Put<float>(v,kBlend,a->b);return;}
    const float k=a->k,b=a->b,d=At<float>(v,kDamp);
    const float denom=1.0f-d*(1.0f-b),frames=std::fmax(sec*60.0f,15.0f);
    if(!std::isfinite(k+b+d) || k<=0.0f || b<=0.0f || b>=1.0f || d<=0.5f || d>=1.0f || denom<1e-6f)return;
    const float blend=1.0f-(1.0f-1.0f/frames)/d;
    if(!(blend>b && blend<1.0f))return;   // it settles faster than that already
    const float top=b*k/denom;
    if(!a->said && (a->said=true))
        Log("HELI v=%p player assist: settles over %.1fs (stock %.1fs), top speed %.1f m/s kept",v,sec,1.0f/denom/60.0f,top);
    Put<float>(v,kSpeedGain,top/(frames*blend));Put<float>(v,kBlend,blend);
}

// ---- The player at the stick of a stock helicopter: the mouse-aim flight and the helicopter HUD's readout ----
// The mouse-aim flight (ini HeliMouseAim, heliaim.h's instructor; keyboard and mouse only: a pad keeps the stock control):
// the stock input (slot 55) copies LX, the trigger, LY and RX to the heli and never RY, so the mouse's Y only ever moved
// the camera (heli-input-re.md §2, §4). After it (HeliFrame runs in its post-hook, crew.cpp InputHook, so slot 57 reads
// this frame's values) the input block is written in three layers:
//  1. the attitude wanted (aim::Instructor): the aim's heading and pitch, the cyclic's forward speed for that pitch, the
//     A / D slide, the collective's climb (W / S, Space, the brake key; let go: the height held);
//  2. the attitude controller: the yaw onto the aim's heading through the native yaw lag's inverse (aim::PlayerYawInput;
//     + grows the heading angle with a positive max yaw rate, aim::YawSign), the pitch the aim's (PlayerAttitudeHook
//     hands it to the attitude function alone), the roll the slide's plus a coordinated turn's bank (aim::BankInput);
//  3. the native input block: forward / lateral the velocity law's stick (aim::StockStick: the stock heli flies its
//     velocity directly, docs/aircraft-re.md §2), the throttle the collective through the rotor lag's inverse
//     (aim::CollectiveThrottle), on the actuators PlayerMouseTune quickens.
// PlayerAssist's settle runs first: let go of the pitch the heli stops within PlayerHeliStopSec. On the ground (OnGround:
// contact bit 1 with the map under it, not perched on a body) nothing horizontal and the stock throttle, W or Space
// spinning it up to lift off; for kLiftOffMs after it no horizontal stick (the NPC's lift-off).
// The readout (PlayerHeliHud) is gathered while the mouse flies it, the HUD (ini HeliFlightHud) is on or the warnings are
// heard (ini WarnAudio, warn.cpp): the mouse's aim is drawn whenever it flies, the HUD around it only with HeliFlightHud.
constexpr std::size_t kSeatPad=0x2B0,kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0;   // §4
constexpr float kPlayerClimb=6.0f;     // m/s: Space / the brake key (the stock rotor's most is ~8: aircraft-re.md)
constexpr float kPlayerMark=800.0f;    // m: the aim's mark ahead (playerjet.cpp kAimMark), kept within kAimOnScreen
constexpr float kAimOnScreen=0.85f;
constexpr float kMovingSpeed=5.0f;     // m/s: slower, it has no flight path to mark (the HUD shows its drift)
constexpr float kThreatRadius=20.0f;   // m: a missile's lock point this near it homes on it (playerjet.cpp's)
constexpr float kGpwsSlack=1.0f;       // m/s over the descent key's sink before the ground-proximity warning counts it
struct Pilot {
    ObjRef ref;
    ULONGLONG seen,lastMs,groundAt,inputLogAt;
    float prev[3],vel[3];
    bool havePrev;
    float aim[3];                      // the mouse's aim (heliaim.h), a world direction
    aim::Hold hold;
    float hover;                       // the rotor that holds its height (learned, StockThrottle)
    float prevHeading,yawRate;
    float prevFwd[3],turn;             // the level nose last frame; its turn toward row 0 (rad/s, BankInput)
    float pitchIn,rollIn;              // the attitude the controller asks this frame (PlayerAttitudeHook's pitch / roll input)
    bool flying;                       // the mouse-aim flight wrote its input last frame (logged as it changes)
};
Pilot pilots[4];

// In the stock slot-57 physics, 0x654A80 uses input+8 for pitch (0x654E69), then the caller uses the original input+8
// for forward velocity (0x651E2F). Keep those uses separate only for this frame's local mouse pilot.
constexpr std::size_t kPlayerAttitude=0x654A80,kPlayerAttitudeCopied=14,kPlayerMaxTilt=0x1640;
const unsigned char kPlayerAttitudeSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57};
constexpr std::size_t kPlayerYawUpdate=0x654E3F,kPlayerYawConvert=0x6CE9C1,kPlayerYawState=0x1604,kPlayerYawSpring=0x1620;
const unsigned char kPlayerYawUpdateSig[]={0xF3,0x0F,0x10,0x4B,0x74,0x44,0x0F,0x28,0xDB,0xF3,0x0F,0x59,0x4F,0x10,
    0xF3,0x0F,0x10,0xA3,0x84,0,0,0,0xF3,0x0F,0x5C,0x4B,0x44,0xF3,0x0F,0x59,0x4B,0x78,0xF3,0x0F,0x58,0x4B,0x44,
    0xF3,0x0F,0x11,0x4B,0x44};
const unsigned char kPlayerYawConvertSig[]={0xF3,0x41,0x0F,0x59,0xD1,0xF3,0x0F,0x5E,0x15,0x8A,0x09,0x0D,0x01};
using PlayerAttitudeFn=void(__fastcall*)(unsigned char*,void*,const float*,const unsigned char*);
PlayerAttitudeFn playerAttitudeNext=nullptr;

// This frame's attitude input of the local mouse pilot of the heli whose attitude block is `attitude` (AimFly's).
bool PlayerAttitudeInput(const unsigned char* attitude,float* pitch,float* roll) noexcept {
    if(!Cfg().enabled || !Cfg().heliMouseAim)return false;
    for(const auto& p:pilots) {
        const auto v=static_cast<const unsigned char*>(p.ref.obj);
        if(!v || v+kHeadRight!=attitude || !p.flying || p.lastMs!=GameMs() || !p.ref.Is(v) || v[kDead])continue;
        if(!SeatCount(v))return false;
        const auto seat=SeatAt(const_cast<unsigned char*>(v),0);
        if(SeatRider(seat)!=Rider::player || At<unsigned char>(seat,kSeatPad)!=0)return false;
        if(edf::RemoteRider(At<const unsigned char*>(seat,kSeatRider)))return false;
        *pitch=p.pitchIn;*roll=p.rollIn;
        return true;
    }
    return false;
}

void __fastcall PlayerAttitudeHook(unsigned char* attitude,void* body,const float* input,const unsigned char* contact) {
    alignas(16) float own[5];
    const float* use=input;
    __try {
        float pitch=0.0f,roll=0.0f;
        if(input && PlayerAttitudeInput(attitude,&pitch,&roll)) {
            std::memcpy(own,input,sizeof(own));own[0]=roll;own[2]=pitch;use=own;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    playerAttitudeNext(attitude,body,use,contact);
}

bool InstallPlayerAttitude() noexcept {
    if(playerAttitudeNext)return true;
    if(!Matches(kPlayerAttitude,kPlayerAttitudeSig,sizeof(kPlayerAttitudeSig)))return false;
    if(!Matches(kPlayerYawUpdate,kPlayerYawUpdateSig,sizeof(kPlayerYawUpdateSig)) ||
       !Matches(kPlayerYawConvert,kPlayerYawConvertSig,sizeof(kPlayerYawConvertSig)))return false;
    unsigned char trampoline[kPlayerAttitudeCopied+14];
    std::memcpy(trampoline,kPlayerAttitudeSig,kPlayerAttitudeCopied);
    const unsigned char jump[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(trampoline+kPlayerAttitudeCopied,jump,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+kPlayerAttitude+kPlayerAttitudeCopied);
    std::memcpy(trampoline+kPlayerAttitudeCopied+6,&back,8);
    void* const code=edf::AllocateNearCode(image+kPlayerAttitude,trampoline,sizeof(trampoline));
    if(!code)return false;
    unsigned char patch[kPlayerAttitudeCopied];std::memcpy(patch,jump,6);
    const auto hook=reinterpret_cast<std::uintptr_t>(&PlayerAttitudeHook);
    std::memcpy(patch+6,&hook,8);
    playerAttitudeNext=reinterpret_cast<PlayerAttitudeFn>(code);
    if(edf::PatchCode(image+kPlayerAttitude,kPlayerAttitudeSig,patch,sizeof(patch)))return true;
    playerAttitudeNext=nullptr;VirtualFree(code,0,MEM_RELEASE);
    return false;
}

// Its record: a new one (a free slot or a stale one) starts level, at a hover, the aim on its nose.
Pilot* PilotOf(unsigned char* v,const float* fwd,ULONGLONG ms) noexcept {
    for(auto& p:pilots)if(p.ref.Is(v) && ms-p.seen<=kAssistStaleMs)return &p;
    Pilot* p=nullptr;
    for(auto& x:pilots)if(!p && (!x.ref || x.ref.Is(v) || ms-x.seen>kAssistStaleMs))p=&x;
    if(!p)return nullptr;
    *p=Pilot{};p->ref=ObjRef::Of(v);p->lastMs=p->seen=ms;
    std::memcpy(p->aim,fwd,12);std::memcpy(p->prevFwd,fwd,12);
    const float lift=At<float>(v,kLiftPerRotor),mass=At<float>(v,kLiftMass);   // the takeoff cue's hover speed
    p->hover=aim::HoverRotor(lift,mass);
    p->prevHeading=std::atan2(fwd[0],fwd[2]);
    return p;
}

// The heli's own top speed now (its params as PlayerAssist leaves them: b k / (1 - d (1 - b))), else kTopSpeed.
float PlayerTop(const unsigned char* v) noexcept {
    const float k=At<float>(v,kSpeedGain),b=At<float>(v,kBlend),d=At<float>(v,kDamp),denom=1.0f-d*(1.0f-b);
    const float top=denom>1e-6f ? b*k/denom : 0.0f;
    return std::isfinite(top) && top>1.0f ? top : kTopSpeed;
}

bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}
float SeatAxis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    return std::isfinite(x) ? Clamp(x,-1.0f,1.0f) : 0.0f;
}

// The mouse-aim flight's frame (see the top): the aim moved, the attitude wanted, the attitude controller, the input block.
void AimFly(Pilot& p,unsigned char* v,const unsigned char* seat,const float* pos,const float* fwd,const float* right,bool grounded,
            float clear,float dt,ULONGLONG ms) noexcept {
    const float ly=SeatAxis(seat,kSeatLY),ry=SeatAxis(seat,kSeatRY),side=SeatAxis(seat,kSeatLX);
    // The collective: W / S (the stock's -LY: W gives LY < 0), Space (the stock ascend) and the brake key.
    const float vert=Clamp((ly<-0.3f ? 1.0f : ly>0.3f ? -1.0f : 0.0f)+(At<float>(seat,kSeatAscend)>0.5f ? 1.0f : 0.0f)-
                           (KeyDown(Cfg().playerJetBrakeKey) ? 1.0f : 0.0f),-1.0f,1.0f);
    const float mx=SeatAxis(seat,kSeatRX),my=Cfg().playerJetInvertPitch ? ry : -ry,k=aim::kPerUnit*Cfg().playerJetMouseSpeed;
    float vp[16];
    if(LastViewProj(vp)) {
        // A level nose can itself be outside a downward-looking camera. Recover toward the actual view centre,
        // not that same invisible nose, when the user moves the mouse; no mouse must not introduce a new pitch.
        float centre[3]={fwd[0],fwd[1],fwd[2]},eye[3],view[3];
        if(CameraRay(eye,view))aim::ViewCentreAim(pos,eye,view,kPlayerMark,centre);
        const bool visible=aim::OnScreen(vp,pos,p.aim,kPlayerMark,kAimOnScreen);
        if(visible || mx!=0.0f || my!=0.0f)aim::MoveOnScreen(vp,pos,p.aim,mx,my,k,centre,kPlayerMark,kAimOnScreen);
    } else aim::Move(p.aim,mx,my,k);
    const float top=PlayerTop(v),maxTilt=At<float>(v,kPlayerMaxTilt);
    if(grounded)p.groundAt=ms;
    const bool lifting=ms-p.groundAt<kLiftOffMs;   // the NPC's lift-off: straight up, nothing horizontal
    // 1. The attitude wanted.
    float pitch=0.0f;
    const aim::Want w=aim::Instructor(p.hold,p.aim,fwd,pos,p.vel,vert,side,top,kPlayerClimb,maxTilt,grounded,lifting,clear,&pitch);
    // 3. (the velocity first: the roll is built on its lateral) The cyclic's stick for the velocity.
    float forward=0.0f,lateral=0.0f;
    aim::StockStick(w.vel,p.vel,top,kBrakeGain,fwd,right,&forward,&lateral);
    if(lifting)forward=lateral=0.0f;
    // 2. The attitude controller. The native yaw channel is a lagged heading offset, not a rate: compensate its current
    // state and the native spring conversion (aim::PlayerYawInput). The pitch the aim's; the roll the slide's plus the
    // turn's bank (the nose's turn toward row 0, measured, times the forward speed).
    const float heading=std::atan2(fwd[0],fwd[2]);
    const float maxYaw=At<float>(v,kMaxYaw);
    const float spring=At<float>(v,kPlayerYawSpring),blend=At<float>(v,kYawSmooth),state=At<float>(v,kPlayerYawState);
    const float rateLimit=Cfg().heliYawRate>0.0f ? Cfg().heliYawRate*kPi/180.0f : std::fabs(maxYaw)*spring*60.0f;
    const float yaw=aim::PlayerYawInput(Wrap(std::atan2(w.face[0],w.face[2])-heading),p.yawRate,rateLimit,maxYaw,state,blend,spring);
    const float turnNow=((fwd[0]-p.prevFwd[0])*right[0]+(fwd[2]-p.prevFwd[2])*right[2])/dt;
    std::memcpy(p.prevFwd,fwd,12);
    if(std::isfinite(turnNow))p.turn+=(turnNow-p.turn)*0.3f;
    const float along=p.vel[0]*fwd[0]+p.vel[2]*fwd[2];
    p.pitchIn=maxTilt>1e-3f ? Clamp(-pitch/maxTilt,-1.0f,1.0f) : 0.0f;
    p.rollIn=grounded ? lateral : Clamp(lateral+aim::BankInput(along,p.turn,maxTilt),-1.0f,1.0f);
    Put<float>(v,kInLateral,lateral);Put<float>(v,kInForward,forward);Put<float>(v,kInW,1.0f);Put<float>(v,kInYaw,yaw);
    // 3. The collective. On the ground the stock throttle (Space) stands, W spins the rotor up too.
    if(!grounded) {
        const bool learn=p.hold.holding && std::fabs(p.hold.y-pos[1])<6.0f;
        const aim::Rotor rotor{At<float>(v,kLiftPerRotor),At<float>(v,kVertDamp),At<float>(v,kRotorIdle),At<float>(v,kRotorUp),
                               At<float>(v,kRotorDown)};
        Put<float>(v,kInThrottle,aim::CollectiveThrottle(w.climb,p.vel[1],At<float>(v,kRotor),&p.hover,learn,dt,rotor));
    } else if(vert>0.0f)Put<float>(v,kInThrottle,1.0f);
    if(!p.flying)Log("HELI v=%p the mouse-aim flight (instructor): top %.1f m/s, hover rotor %.3f, max tilt %.0f deg",v,top,p.hover,
                     maxTilt*180.0f/kPi);
    if(Cfg().debug && ms-p.inputLogAt>=1000) {
        p.inputLogAt=ms;
        Log("HELI INPUT v=%p mouse=(%.3f,%.3f) vert=%.0f side=%.2f set=%.2f climb=%.2f aim=(%.3f,%.3f,%.3f) pitch=%.3f roll=%.3f "
            "move=%.3f yaw=%.3f rate=%.3f turn=%.3f state=%.4f ground=%d lift=%d",v,mx,my,vert,side,p.hold.speed,w.climb,p.aim[0],
            p.aim[1],p.aim[2],p.pitchIn,p.rollIn,forward,yaw,p.yawRate,p.turn,state,grounded,lifting);
    }
    p.flying=true;
}

// The HUD's readout (the flight's state; the threats as the jets' Threats gathers them), published for HudPublish.
SRWLOCK heliHudLock=SRWLOCK_INIT;
PlayerHeliReadout heliHud{};
ULONGLONG heliHudAt=0;
void PublishHud(const Pilot& p,unsigned char* v,const float* pos,bool grounded,float clear,bool keys) noexcept {
    PlayerHeliReadout r{};
    HeliFlight& f=r.f;
    std::memcpy(f.vel,p.vel,12);
    f.speed=std::sqrt(p.vel[0]*p.vel[0]+p.vel[2]*p.vel[2]);
    f.ground=clear!=kNoGround;f.clear=f.ground ? clear : pos[1];f.climb=p.vel[1];
    f.hp=At<float>(v,kHp);f.hpMax=At<float>(v,kHpMax);
    f.keys=keys;f.aiming=p.flying;f.collective=p.flying;f.holding=p.flying && p.hold.holding;f.landed=grounded;
    f.setSpeed=p.hold.speed;f.top=PlayerTop(v);
    const float engine=At<float>(v,kRotor);   // its rotor is its engine: the collective's throttle drives it (CollectiveThrottle)
    f.power=std::isfinite(engine) ? Clamp(engine,0.0f,1.0f) : -1.0f;
    // The ground-proximity warning (warn.cpp), off the ground: sinking faster than the descent key's kPlayerClimb (plus
    // kGpwsSlack) onto the ground, or the path into something standing higher than it.
    f.gpws=Gpws::none;f.impactIn=-1.0f;
    if(!grounded) {
        bool rising=false;
        f.impactIn=ClosureIn(pos,p.vel,p.vel[1],clear,kPlayerClimb+kGpwsSlack,kTerrainSeconds,&rising);
        f.gpws=GpwsOf(f.impactIn,rising);
        if(f.gpws==Gpws::none)f.impactIn=-1.0f;
    }
    for(int i=0;i<3;++i)f.aim[i]=pos[i]+p.aim[i]*kPlayerMark;
    if(grounded) {
        const float lift=At<float>(v,kLiftPerRotor),mass=At<float>(v,kLiftMass),rotor=At<float>(v,kRotor);
        if(lift>1e-3f && mass>0.0f && std::isfinite(lift+mass+rotor)){f.rotor=rotor;f.hover=aim::HoverRotor(lift,mass);}
    }
    PlayerJetSymbols& y=r.sym;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    std::memcpy(y.pos,pos,12);
    y.nose[0]=m[8];y.nose[1]=m[9];y.nose[2]=m[10];
    const float nl=std::sqrt(y.nose[0]*y.nose[0]+y.nose[1]*y.nose[1]+y.nose[2]*y.nose[2]);
    if(std::isfinite(nl) && nl>0.5f)for(auto& c:y.nose)c/=nl;
    else{y.nose[0]=0.0f;y.nose[1]=0.0f;y.nose[2]=1.0f;}
    const float sp=std::sqrt(p.vel[0]*p.vel[0]+p.vel[1]*p.vel[1]+p.vel[2]*p.vel[2]);
    y.moving=sp>kMovingSpeed;
    if(y.moving)for(int i=0;i<3;++i)y.dir[i]=p.vel[i]/sp;
    int n=MissilesHomingAt(pos,kThreatRadius,y.threatAt,kMostThreats);
    if(n>kMostThreats)n=kMostThreats;
    for(int i=0;i<n;++i)y.threatKind[i]=2;
    const int locks=jet::LockersOf(v,y.threatAt+n,kMostThreats-n);
    for(int i=n;i<n+locks;++i)y.threatKind[i]=1;
    y.threats=n+locks;
    FuelGauge(v,&r.fuel);   // the tank the stock FUEL gauge showed (stockgauge.cpp): HeliStrip's line, LOW FUEL
    AcquireSRWLockExclusive(&heliHudLock);
    heliHud=r;heliHudAt=GetTickCount64();
    ReleaseSRWLockExclusive(&heliHudLock);
}

// The 410 without a local NPC pilot (which already drives its guns in Fly): an AI rider in a door seat (AiGunner: an NPC soldier, or the NPC a seat swap moved
// there; the user 2026-10-07: "上车的npc应该可以用对应的炮塔武器") works its gun as the NPC heli's DoorGun does, on the
// gun's real rounds (no refill: the player's heli). An empty door seat stays silent (DoorGunUser lends the pilot's
// user only under an NPC pilot); the player's own seat is theirs.
struct CrewDoors { ObjRef ref; ULONGLONG lastMs; Door doors[2]; };
CrewDoors crewDoors[4]{};
constexpr ULONGLONG kCrewDoorsStaleMs=2000;

void CrewDoorGuns(unsigned char* v) noexcept {
    if(!doorOk || !Cfg().heliDoorGuns || At<const unsigned char*>(v,0)!=image+kVt410 || SeatCount(v)<3)return;
    if(InSession() && !NpcGunnerAimReady())return;
    const ULONGLONG ms=GameMs();
    CrewDoors* c=nullptr;
    for(auto& e:crewDoors)if(e.ref.Is(v)){c=&e;break;}
    if(!c)for(auto& e:crewDoors)if(!e.ref || ms-e.lastMs>kCrewDoorsStaleMs){e=CrewDoors{};e.ref=ObjRef::Of(v);e.lastMs=ms;c=&e;break;}
    if(!c)return;
    const float dt=GameStep(ms-c->lastMs);
    c->lastMs=ms;
    for(int i=0;i<2;++i) {
        if(AiGunner(v,SeatAt(v,static_cast<unsigned>(i+1))))DoorGun(c->doors[i],ObjRef{},false,v,i,false,dt,ms);
        else c->doors[i].prevValid=false;
    }
}

// The player in seat 0 of a stock heli, each frame after PlayerAssist.
void PlayerHeli(unsigned char* v) noexcept {
    if(!Cfg().heliMouseAim && !Cfg().heliFlightHud && !Cfg().warnAudio)return;
    float fwd[3],right[3];
    if(!Row(v,kHeadForward,fwd) || !Row(v,kHeadRight,right))return;
    const ULONGLONG ms=GameMs();
    Pilot* const p=PilotOf(v,fwd,ms);
    if(!p)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=GameStep(ms-p->lastMs);   // game time: a slow frame moved it no more than 1/60 s (body506.h)
    p->lastMs=ms;p->seen=ms;
    if(p->havePrev)for(int i=0;i<3;++i)p->vel[i]+=((pos[i]-p->prev[i])/dt-p->vel[i])*0.3f;
    std::memcpy(p->prev,pos,12);p->havePrev=true;
    const float heading=std::atan2(fwd[0],fwd[2]);
    p->yawRate+=(Wrap(heading-p->prevHeading)/dt-p->yawRate)*0.3f;
    p->prevHeading=heading;
    // On the ground as the NPC tells it (OnGround): contact on top of an enemy, a vehicle or another heli is not the
    // ground. Taken as the ground, a low scrape over one zeroed the speed set, handed the throttle back to the stock and
    // locked the sidestep for kLiftOffMs.
    const float clear=GroundClearance(pos);
    const bool grounded=OnGround((v[kContact]&kContactGround)!=0,rayOk,clear==kNoGround ? -1.0f : clear);
    const unsigned char* seat=SeatAt(v,0);
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    const bool mouse=Cfg().heliMouseAim && keys && playerAttitudeNext;
    PlayerMouseTune(v,mouse);
    if(mouse)AimFly(*p,v,seat,pos,fwd,right,grounded,clear==kNoGround ? -1.0f : clear,dt,ms);
    else {
        if(p->flying)Log("HELI v=%p the mouse-aim flight off: the stock input flies it",v);
        p->flying=false;std::memcpy(p->aim,fwd,12);p->hold=aim::Hold{};
    }
    if(Cfg().heliFlightHud || p->flying || Cfg().warnAudio)PublishHud(*p,v,pos,grounded,clear,keys);   // the aim's square drawn either way
}
// ---- Online: the NPC pilot's flight where the heli's authority is, its stick everywhere (docs/online-re.md §4-5) ----
// The heli's replication sends seat 0's stick block (mask 4, slot 7 0x6559D0) from the machine that runs it, and every
// other machine's copy flies on that stick through the stock flight law (slot 55 copies it to the input block while seat
// 0 has a rider, slot 51 0x651F90 pulls the body to the pose it was sent). The plugin's pilot writes the input block
// (+0x1540..), which is not replicated, so on its authority (OnlineRunsHere) the block it wrote is also put on seat 0's
// stick the way the stock copy reads it back (MirrorStick: lateral = -LX, forward = -LY, yaw = -RX, throttle = the ascend
// trigger; heli-input-re.md §2a). Elsewhere (Replay) the pilot does not fly it: its NPC exists on the authority only (a
// RideAi rider has no network identity, so seat 0 is empty here, and slot 55 then zeroes the block), so the replicated
// stick is copied to the input block as slot 55 would, under the same params Tune gives the pilot (ini HeliSpeed /
// HeliAgility / HeliYawRate, the same on every machine with the same ini). A stick block all zero is no pilot's: the
// stock zeroes it after 30 frames without a packet (slot 51 0x652259: the frame count +0x1D7C reaching 30 clears the
// block by 0x62C120 and starts the count again at 0, so the count never stays up and tells nothing by itself), a heli
// never flown has it zero from its constructor, and the pilot's hover throttle is never 0 in the air. Then the heli's own
// params go back and its record goes (ReplicaOff), and nothing is written: the stock input stands.
Heli replicas[8]{};

void MirrorStick(unsigned char* v,const Control& c) noexcept {
    if(!InSession() || SeatCount(v)==0)return;
    unsigned char* const seat=SeatAt(v,0);
    Put<float>(seat,kSeatLX,-c.stickL);Put<float>(seat,kSeatLY,-c.stickF);Put<float>(seat,kSeatRX,-c.yaw);
    Put<float>(seat,kSeatAscend,Clamp(c.throttle,0.0f,1.0f));
}

// Seat 0's stick holds a value (see above: all zero is no pilot's).
bool StickLive(const unsigned char* seat) noexcept {
    return At<float>(seat,kSeatLX)!=0.0f || At<float>(seat,kSeatLY)!=0.0f || At<float>(seat,kSeatRX)!=0.0f || At<float>(seat,kSeatAscend)!=0.0f;
}

Heli* ReplicaOf(unsigned char* v,ULONGLONG ms) noexcept {
    for(auto& h:replicas)if(h.ref.Is(v))return &h;
    Heli* slot=nullptr;
    for(auto& h:replicas)if(!slot && (!h.ref || ms-h.seen>kStaleMs))slot=&h;
    if(!slot)return nullptr;
    *slot=Heli{};slot->ref=ObjRef::Of(v);slot->type=TypeOf(v);slot->seen=ms;
    Tune(*slot,v);
    return slot;
}

// A stock heli (with seats) another machine runs whose seat 0 is empty here, or holds an NPC seated here (not its
// authority's): no player of any machine at its stick.
bool Replica(unsigned char* v) noexcept {
    const Rider r=SeatRider(SeatAt(v,0));
    return (r==Rider::none || NpcDriver(v)) && !OnlineRunsHere(v);
}

// Its replica record's params back, the record dropped: it is run here again (or by a player).
void ReplicaOff(unsigned char* v) noexcept {
    for(auto& h:replicas)if(h.ref.Is(v)){Restore(h,v);h=Heli{};}
}

// A Replica: its replicated stick into the input block, after the stock slot 55 (crew.cpp InputHook), under the
// pilot's params.
void Replay(unsigned char* v) noexcept {
    // No stick coming in: no pilot's (a parked heli takes no record, so the table keeps room for the flown ones).
    if(!StickLive(SeatAt(v,0))){ReplicaOff(v);return;}
    const ULONGLONG ms=GameMs();
    Heli* const h=ReplicaOf(v,ms);
    if(!h)return;
    h->seen=ms;
    if(h->tuned) {
        Put<float>(v,kSpeedGain,h->params[0]);Put<float>(v,kBlend,h->params[1]);
        Put<float>(v,kMaxYaw,h->params[2]);Put<float>(v,kYawSmooth,h->params[3]);
        h->applied=true;
    }
    const unsigned char* const seat=SeatAt(v,0);
    Put<float>(v,kInLateral,-SeatAxis(seat,kSeatLX));Put<float>(v,kInForward,-SeatAxis(seat,kSeatLY));
    Put<float>(v,kInThrottle,Clamp(At<float>(seat,kSeatAscend),0.0f,1.0f));Put<float>(v,kInW,1.0f);
    Put<float>(v,kInYaw,-SeatAxis(seat,kSeatRX));
}
}  // namespace

void HeliFrame(unsigned char* vehicle) noexcept {
    for(auto& r:rescues)if(r.phase!=RescuePhase::none && r.ref.Is(vehicle))r.seenFrame=GameFrame();   // RescueHeliAlive
    if(call.phase==CallPhase::assigned && call.ref.Is(vehicle))call.seenFrame=GameFrame();   // a peer's copy too (Replay)
    if(!profileOk || vehicle[kDead])return;
    const bool stockHeli=!IsJet(vehicle) && !IsSub(vehicle) && !IsPlayerJet(vehicle) && !IsSazabi(vehicle) && TypeOf(vehicle);
    // NPC gunners have their own firing authority. A remote player pilot must not suppress host/local NPC door
    // gunners; the native weapon messages replicate their shots. NPC pilots already call DoorGun through Fly.
    if(stockHeli && SeatCount(vehicle)>0 && !NpcDriver(vehicle))CrewDoorGuns(vehicle);
    // Online, a stock heli another machine runs is flown there: here it flies on the stick it sends (Replay).
    if(stockHeli && SeatCount(vehicle)>0 && Replica(vehicle)) {
        if(Heli* h=Find(vehicle))Restore(*h,vehicle);
        AssistOff(vehicle);
        Replay(vehicle);
        return;
    }
    ReplicaOff(vehicle);
    if(!NpcDriver(vehicle)) {   // only NPC pilots
        if(Heli* h=Find(vehicle))Restore(*h,vehicle);
        if(stockHeli && SeatCount(vehicle)>0 && SeatRider(SeatAt(vehicle,0))==Rider::player) {
            PlayerAssist(vehicle);PlayerHeli(vehicle);
        }
        else AssistOff(vehicle);
        return;
    }
    AssistOff(vehicle);   // an NPC in its seat again: Tune's stock is the heli's own
    // The plugin's jets: its own copies, flown on every machine (OnlineRunsHere); a registered one only where it is run.
    if(IsJet(vehicle)){if(Cfg().jetPilot && OnlineRunsHere(vehicle))JetFrame(vehicle);return;}
    if(IsSub(vehicle))return;   // the submarine carrier: driven from the input hook (crew.cpp SubStep)
    if(IsPlayerJet(vehicle))return;   // a player jet an NPC sat in (a stock squadmate): not flown as a heli
    if(IsSazabi(vehicle))return;      // the Sazabi (sazabi.cpp): never flown as a heli
    if(!Cfg().heliPilot)return;
    Heli* h=Find(vehicle);
    if(!h){if(!HeliCrewed(vehicle))return;h=Find(vehicle);}   // a mission-spawned NPC heli (CreateFriend): fly it too
    if(!h)return;
    h->seen=GameMs();h->seenFrame=GameFrame();
    bool playerAboard=false;
    for(unsigned i=1;i<SeatCount(vehicle);++i)playerAboard=playerAboard || AnyPlayerIn(SeatAt(vehicle,i));   // of any machine
    Fly(*h,vehicle,playerAboard);
}

bool PlayerHeliHud(PlayerHeliReadout* out) noexcept {
    AcquireSRWLockShared(&heliHudLock);
    const bool fresh=heliHudAt && GetTickCount64()-heliHudAt<=kCueFreshMs;
    if(fresh)*out=heliHud;
    ReleaseSRWLockShared(&heliHudLock);
    return fresh;
}

void HeliCalled(unsigned char* vehicle,bool guard,const float* post,DWORD fuelSec) noexcept {
    __try {
        Heli* const h=HeliCrewed(vehicle) ? Find(vehicle) : nullptr;
        if(!h){Log("HELI v=%p called, but not flown by the plugin (see above): it stays where it is",vehicle);return;}
        h->called=true;h->guard=guard;
        std::memcpy(h->post,post,12);
        h->hold[0]=post[0];h->hold[1]=post[1]+Cfg().heliHeight;h->hold[2]=post[2];
        h->leaveAt=GameMs()+static_cast<ULONGLONG>(fuelSec)*1000;
        // Spawned in the air with the rotor still: it starts at the rotor Fly assumes for hover, so it
        // does not drop while the rotor spins up.
        const float rotor=At<float>(vehicle,kRotor);
        if(!(std::isfinite(rotor) && rotor>0.2f))Put<float>(vehicle,kRotor,0.5f);
        Log("HELI v=%p called: %s at (%.0f,%.0f,%.0f), fuel %lus",vehicle,guard ? "guard" : "follow",post[0],post[1],post[2],fuelSec);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

namespace {
// A heli a map command reaches: flown by its NPC now (as HeliReap tells a live one), not on its way out.
// A heli a rescue flies (Rescue: to its swimmer, to the deck): the rescue's, not the map's to order (2026-10-10 review: a
// focus, a post or a withdrawal would take it off its swimmer).
bool RescueFlies(const ObjRef& ref) noexcept {
    for(const auto& r:rescues)if(r.phase!=RescuePhase::none && r.ref.obj==ref.obj && r.ref.ctrl==ref.ctrl)return true;
    return false;
}
bool Commandable(const Heli& h) noexcept {
    return h.ref && h.seenFrame && GameFrame()-h.seenFrame<=kAliveFrames && !h.leaving && !h.reap && CommandVehicleLive(h.ref) &&
           !RescueFlies(h.ref);
}
}  // namespace

bool HeliSharesPost() noexcept { return Cfg().heliGuardRadius>0.0f; }   // GuardOrbit spaces helis on one post round it

int HeliCommandUnits(CommandUnit* out,int most) noexcept {
    int n=0;
    for(const auto& h:helis)
        if(n<most && Commandable(h) &&
           ReadCommandUnit(h.ref,h.type ? h.type->name : "heli",h.focus ? Command{Order::focus,{0.0f,0.0f,0.0f}} : h.cmd,true,&out[n]))++n;
    return n;
}

// guard: the post moved to the point (its guard orbit round it, HeliGuardRadius out; HeliHeight over it with the orbit
// off), as a guard call's; follow: no post (it follows the player, Fly's escort / orbit); none: the call's own back.
bool HeliCommand(const void* vehicle,const Command& c,const ObjRef& focus) noexcept {
    __try {
        Heli* const h=Find(vehicle);
        if(!h || !Commandable(*h))return false;
        // focus: the marked enemy engaged first (Heli::focus), its post and order kept; a medic's guns heal (none), and
        // the enemy must be one of its targets now (as npcai.cpp checks a squad's focus).
        if(c.order==Order::focus) {
            if(!focus || h->medic)return false;
            bool seen=false;
            ForEachEnemy(static_cast<const unsigned char*>(vehicle),[&](const void* object,const float*) noexcept { seen=seen || focus.Is(object); });
            if(!seen)return false;
            h->focus=focus;h->target=ObjRef{};h->tracked=ObjRef{};h->circleUntil=0;h->extend=false;
            Log("HELI v=%p map command: focus %p",vehicle,focus.obj);
            return true;
        }
        h->focus={};
        if(h->cmd.order==Order::none) {
            h->ownGuard=h->guard;
            std::memcpy(h->ownPost,h->post,12);std::memcpy(h->ownHold,h->hold,12);
        }
        if(c.order==Order::guard) {
            h->guard=true;
            std::memcpy(h->post,c.at,12);
            h->hold[0]=c.at[0];h->hold[1]=c.at[1]+Cfg().heliHeight;h->hold[2]=c.at[2];
        } else if(c.order==Order::follow) {
            h->guard=false;
        } else if(h->cmd.order!=Order::none) {
            h->guard=h->ownGuard;
            std::memcpy(h->post,h->ownPost,12);std::memcpy(h->hold,h->ownHold,12);
        }
        h->cmd=c;
        h->cmdMoving=c.order!=Order::none;
        h->target=ObjRef{};h->tracked=ObjRef{};h->circleUntil=0;
        // A new centre at once: GuardOrbit eases its centre at 10 m/s, which would drag the orbit across the map.
        h->orbitSet=false;h->extend=false;
        Log("HELI v=%p map command: %s (%.0f,%.0f,%.0f)",vehicle,c.order==Order::guard ? "guard" : c.order==Order::follow ? "follow" : "release",
            c.at[0],c.at[1],c.at[2]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool HeliFerry(const void* vehicle,const float* at,bool land) noexcept {
    __try {
        Heli* const h=Find(vehicle);
        if(!h || h->leaving || h->reap)return false;
        if(!at) {
            if(h->ferry)Log("HELI v=%p ferry over",vehicle);
            h->ferry=h->ferryLand=false;return true;
        }
        if(!std::isfinite(at[0]+at[1]+at[2]))return false;
        const bool same=h->ferry && h->ferryLand==land && Dist2(h->ferryAt,at)<1.0f;
        h->ferry=true;h->ferryLand=land;std::memcpy(h->ferryAt,at,12);
        h->hold[0]=at[0];h->hold[1]=at[1]+Cfg().heliHeight;h->hold[2]=at[2];
        h->target=ObjRef{};h->tracked=ObjRef{};h->focus={};h->extend=false;h->circleUntil=0;
        if(!same)Log("HELI v=%p ferry to (%.0f,%.0f,%.0f)%s",vehicle,at[0],at[1],at[2],land ? ", landing there" : "");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool HeliKeep(const void* vehicle) noexcept {
    Heli* const h=Find(vehicle);
    if(h && !h->keep){h->keep=true;Log("HELI v=%p a squad's transport now: it stays until withdrawn",vehicle);}
    return h!=nullptr;
}
bool HeliGrounded(const void* vehicle) noexcept {
    const Heli* const h=Find(vehicle);
    return h && h->grounded;
}
bool HeliStartLeaving(const void* vehicle) noexcept {
    __try {
        Heli* const h=Find(vehicle);
        if(!h || h->reap)return false;
        if(h->leaving)return true;
        const auto* v=static_cast<const unsigned char*>(vehicle);
        float fwd[3];
        if(!Row(v,kHeadForward,fwd)){fwd[0]=0.0f;fwd[1]=0.0f;fwd[2]=1.0f;}
        h->ferry=h->ferryLand=false;
        StartLeave(*h,reinterpret_cast<const float*>(v+kPosition),fwd,"withdrawn by a map order");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool HeliLeft(const void* vehicle) noexcept {
    for(const auto& h:helis)if(h.ref.Is(vehicle))return h.reap;
    return false;
}

bool HeliFuel(const void* vehicle,float* sec) noexcept {
    for(const auto& h:helis) {
        if(!h.called || !h.ref.Is(vehicle))continue;
        const ULONGLONG ms=GameMs();
        *sec=h.leaving || ms>=h.leaveAt ? 0.0f : static_cast<float>(h.leaveAt-ms)*0.001f;
        return true;
    }
    return false;
}

void HeliReap(const void* self) noexcept {
    using DeleteFn=void(*)(void*);
    using KickFn=void(*)(void*,void*);
    constexpr std::size_t kObjFlags=0x18;
    constexpr unsigned char kObjDeleted=4;
    if(!deleteOk)return;   // EDF.dll's Delete differs: a called heli that left just flies on away (CheckHeliProfile)
    __try {
        for(auto& h:helis) {
            if(!h.ref || !h.reap || h.ref.obj==self)continue;
            auto v=static_cast<unsigned char*>(const_cast<void*>(h.ref.obj));
            const ObjRef ref=h.ref;
            const bool flown=GameFrame()-h.seenFrame<=kAliveFrames;
            // Only the same object, alive: flown just now (its input ran: it was there), and still the object
            // it was then. One shot down, taken over or gone meanwhile is the game's to clean up.
            if(!flown || !Readable(v,kSeats+8) || v[kDead] || (v[kObjFlags]&kObjDeleted) || !ref.Is(v) || !IsHelicopter(v)){h=Heli{};continue;}
            bool playerAboard=false,realCrew=false;
            for(unsigned i=0;i<SeatCount(v);++i) {
                const auto* seat=SeatAt(v,i);
                playerAboard=playerAboard || AnyPlayerIn(seat);
                realCrew=realCrew || SeatRider(seat)==Rider::other;
            }
            if(playerAboard){h=Heli{};continue;}
            // Never under real soldiers: a support deployment's heli (its crew and hull) is retired by the dispatcher
            // that made them (support_dispatch.cpp Retire), from HeliLeft; until then the flight state stays whole.
            if(realCrew || SupportAircraftOwned(v))continue;
            h=Heli{};
            if(SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy)reinterpret_cast<KickFn>(image+kSeatKick)(v,SeatAt(v,0));
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            Log("HELI v=%p gone (deleted)",v);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

namespace {
// A rescue heli as it was handed over: its input ran just now (HeliFrame stamps `seen`; before its first input,
// within kFirstFrames of the hand-over), and it is still that object, alive.
constexpr ULONGLONG kFirstFrames=120;
bool HeliAlive(unsigned char* v,const ObjRef& ref,ULONGLONG made,ULONGLONG seen) noexcept {
    const ULONGLONG frame=GameFrame();
    const bool recent=seen ? frame-seen<=kAliveFrames : frame-made<=kFirstFrames;
    return recent && Readable(v,kSeats+8) && !v[kDead] && ref.Is(v) && IsHelicopter(v);
}
bool RescueHeliAlive(const Rescue& r) noexcept { return HeliAlive(r.vehicle,r.ref,r.launchFrame,r.seenFrame); }

// The seat `human` (a player of any machine) holds in `v`, or -1.
int HumanSeat(unsigned char* v,const unsigned char* human) noexcept {
    for(unsigned i=0;i<SeatCount(v);++i) {
        const auto* seat=SeatAt(v,i);
        if(At<const void*>(seat,kSeatRider)==human && AnyPlayerIn(seat))return static_cast<int>(i);
    }
    return -1;
}

bool OnFoot(const unsigned char* human) noexcept {
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    return !ctrl || (Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)==0);
}

// The requester's object if it is still the same one (alive or not), else nullptr: gone (left the room, the mission over).
unsigned char* RequesterObject(const ObjRef& ref) noexcept {
    auto* h=static_cast<unsigned char*>(const_cast<void*>(ref.obj));
    return ref && Readable(h,kHumanVehicleCtrl+8) && ref.Is(h) && IsAnyPlayer(h) ? h : nullptr;
}

// Whether `human` rides another rescue's heli than `own` (one flown here, or this machine's call's).
bool InOtherRescue(const unsigned char* human,const unsigned char* own) noexcept {
    const auto* rides=At<const unsigned char*>(human,kHumanVehicle);
    if(!rides || rides==own)return false;
    for(const auto& r:rescues)if(r.phase!=RescuePhase::none && r.vehicle==rides)return true;
    return call.phase==CallPhase::assigned && call.vehicle==rides;
}
bool InSea(const float* p,float* surface) noexcept;

// What the requester is now (rescue_logic.h), for the rescue heli `v`: its object (`*human`), its seat in `v` (`*seat`),
// and `*dryAt` kept (when they came out of the sea; 0: in it).
rescue::Requester Assess(unsigned char* v,const ObjRef& who,ULONGLONG* dryAt,ULONGLONG ms,unsigned char** human,int* seat) noexcept {
    rescue::Requester q{};
    *human=RequesterObject(who);*seat=-1;
    if(!*human)return q;
    unsigned char* h=*human;
    q.present=true;q.dead=h[kDead]!=0;
    *seat=HumanSeat(v,h);q.aboard=*seat>=0;
    q.onFoot=OnFoot(h);
    q.inOtherRescue=!q.onFoot && InOtherRescue(h,v);
    float sea=kSeaY;
    if(InSea(reinterpret_cast<const float*>(h+kPosition),&sea))*dryAt=0;
    else if(!*dryAt)*dryAt=ms;
    q.dry=*dryAt && ms-*dryAt>kDryMs;
    return q;
}

// Sends a rescue heli away at once (StartLeave), as a support heli leaves.
void LeaveNow(unsigned char* v,const char* why) noexcept {
    float fwd[3];
    if(Heli* h=Find(v); h && Row(v,kHeadForward,fwd))StartLeave(*h,reinterpret_cast<const float*>(v+kPosition),fwd,why);
    else Log("RESCUE heli %p: %s, but it is not flown here (it goes as its support flight goes)",v,why);
}

// Ends a flight; `leave`: the heli (alive) gets its own team back and flies off (StartLeave), else it is left as it
// is (a player took its pilot seat: theirs now).
void EndRescue(Rescue& r,const char* why,bool leave) noexcept {
    Log("RESCUE over: %s (heli %p)",why,r.vehicle);
    if(leave && RescueHeliAlive(r)) {
        auto v=r.vehicle;
        SetObjectTeam(v,r.team);
        if(!Find(v))HeliCalled(v,false,reinterpret_cast<const float*>(v+kPosition),kRescueFuelSec);   // re-crewed meanwhile
        float fwd[3];
        if(Heli* h=Find(v); h && Row(v,kHeadForward,fwd))StartLeave(*h,reinterpret_cast<const float*>(v+kPosition),fwd,why);
    }
    r=Rescue{};
}

// Presses the board button for `human` (see kBoardButton). `bump` off: it takes a free seat in the stock reach or
// nothing, never the NPC pilot's; on (the boarding gun): an NPC's seat too, as the player's own press does.
void PressBoard(unsigned char* human,bool bump=false) noexcept {
    if(!bump)SuppressBump(true);
    __try { reinterpret_cast<void(__fastcall*)(void*)>(image+kBoardButton)(human); }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("BOARD button: fault (the button is off from now)");boardOk=false;}
    if(!bump)SuppressBump(false);
}

// Whether `p` is in the sea, and the surface there: under a water area's surface (SeaAt) by kUnderSurface;
// with the probe off, below Cfg().rescueBelow (the surface taken as kSeaY). On M082 the sea is at about y = 14
// (2026-10-04): a swimmer at y -1 was taken for out of it by the fixed height, and the rescue gave up.
bool InSea(const float* p,float* surface) noexcept {
    float y=kSeaY;
    const Sea sea=SeaProbe(p[0],p[2],&y);
    *surface=sea==Sea::water ? y : kSeaY;
    if(sea==Sea::unknown)return p[1]<Cfg().rescueBelow;
    return sea==Sea::water && p[1]<y-kUnderSurface;
}

// Pickup: the heli flies to the swimmer and brings a free door seat's riding point over them.
void Pickup(Rescue& r,unsigned char* human,ULONGLONG ms) noexcept {
    auto v=r.vehicle;
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    SetObjectTeam(v,kTeamVehicle);   // see the Rescue comment
    float sea=kSeaY;
    InSea(p,&sea);
    const float surface=p[1]>sea ? p[1] : sea;
    const float away=Dist2(pos,p);
    float at[3]{},reach=0.0f;
    const int seat=reachOk ? DoorSeat(v,human,at,&reach) : -1;
    r.climb=false;r.slow=false;r.low=false;
    if(away>kRescueApproach || seat<0) {
        // Out there, or no door seat to bring over them: over the sea next to them.
        std::memcpy(r.goal,p,12);
        r.height=surface+(away>kRescueApproach ? kRescueCruise : kSkid+2.0f);
    } else {
        // The seat's point (it moves with the heli) over the swimmer: the heli kSeatAbove higher than that.
        r.goal[0]=p[0]-(at[0]-pos[0]);r.goal[1]=p[1];r.goal[2]=p[2]-(at[2]-pos[2]);
        r.height=surface+kSeatAbove-(at[1]-pos[1]);
        // Under kGroundClear only once over the spot (the walls are not looked for then: the steep coast).
        r.low=Dist2(pos,r.goal)<10.0f;
    }
    if(r.height<sea+kSkid)r.height=sea+kSkid;
    // Taken off from a carrier's deck (support_dispatch.cpp RescueTakeoffSpots): over its hull (map rays do not see it)
    // it keeps kDeckHover over the deck, and comes down only once clear of it.
    float deck[3];
    if(SubDeck(pos,deck)) {
        const float gap=SubHullGap(pos);
        if(gap>=0.0f && gap<kOverHull && r.height<deck[1]+kDeckHover){r.height=deck[1]+kDeckHover;r.low=false;}
    }
    if(away<=kRescueApproach && !r.nearAt){r.nearAt=ms;Log("RESCUE heli %p beside the player (%.0f m), coming down",v,away);}
    if(r.nearAt && ms-r.nearAt>kPickupMs){EndRescue(r,"not boarded in 120 s",true);return;}
    r.seat=seat;
    if(seat>=0) {
        const float d[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
        r.seatDist=std::sqrt(Dot3(d,d));r.seatReach=reach;
    }
    if(ms-r.loggedAt>2000) {
        r.loggedAt=ms;
        Log("RESCUE pickup: heli %p %.0f m from the player, y=%.1f (player %.1f, goal %.1f), seat %d point %.2f m (stock reach %.2f)%s",
            v,away,pos[1],p[1],r.height,seat,seat>=0 ? r.seatDist : -1.0f,seat>=0 ? reach : -1.0f,
            seat>=0 && r.seatDist<=reach ? " in reach" : "");
    }
}

// Ferry: the swimmer in a door seat; to the nearest carrier's deck, kDeckHover over it, until they get off.
void Ferry(Rescue& r,ULONGLONG ms) noexcept {
    auto v=r.vehicle;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    SetObjectTeam(v,kTeamVehicle);   // a door seat the player takes back after a stray exit
    float deck[3];
    r.slow=true;r.climb=false;r.low=false;
    if(!SubDeck(pos,deck)) {   // the carrier is gone: it holds where it is
        if(ms-r.loggedAt>kOverDeckLogMs){r.loggedAt=ms;Log("RESCUE ferry: no carrier out, holding at (%.0f,%.0f,%.0f)",pos[0],pos[1],pos[2]);}
        std::memcpy(r.goal,pos,12);r.height=pos[1];
        return;
    }
    std::memcpy(r.goal,deck,12);r.height=deck[1]+kDeckHover;
    const float gap=SubHullGap(pos);
    r.climb=pos[1]<deck[1]+5.0f && gap>=0.0f && gap<kHullMargin;   // not into the hull's side
    const bool over=Dist2(pos,deck)<kOverDeck && std::fabs(pos[1]-r.height)<4.0f;
    if(over && !r.overDeckAt) {
        r.overDeckAt=r.loggedAt=ms;
        Log("RESCUE over the carrier deck (%.0f,%.0f,%.0f), %.1f m above it: the player can jump off",deck[0],deck[1],deck[2],pos[1]-deck[1]);
    } else if(ms-r.loggedAt>(r.overDeckAt ? kOverDeckLogMs : 2000)) {
        r.loggedAt=ms;
        Log("RESCUE ferry: %.0f m to the deck, y=%.1f (deck %.1f)%s%s",Dist2(pos,deck),pos[1],deck[1],r.climb ? " climbing clear of the hull" : "",
            r.overDeckAt ? ", over it: waiting for the player to jump off" : "");
    }
}

// One flight's step, once a frame: the requester assessed (rescue_logic.h), then pickup or ferry.
void FlightStep(Rescue& r,ULONGLONG ms) noexcept {
    if(!RescueHeliAlive(r)){Log("RESCUE heli %p lost (shot down or gone)",r.vehicle);r=Rescue{};return;}
    unsigned char* human=nullptr;int seat=-1;
    const rescue::Requester q=Assess(r.vehicle,r.swimmer,&r.dryAt,ms,&human,&seat);
    if(seat==0){EndRescue(r,"the player took the pilot seat: the heli is theirs",false);return;}
    if(r.phase==RescuePhase::pickup) {
        if(const auto cancel=rescue::PickupCancel(q);cancel!=rescue::Cancel::none) {
            Log("RESCUE cancelled: %s",rescue::CancelText(cancel));
            EndRescue(r,rescue::CancelText(cancel),true);return;
        }
        if(seat<0){Pickup(r,human,ms);return;}
        Log("RESCUE boarded: seat %d, its riding point was %.2f m from the player (stock reach %.2f); ferrying to the carrier",
            seat,r.seatDist,r.seatReach);
        r.phase=RescuePhase::ferry;r.loggedAt=0;
    }
    if(!q.present || q.dead) {
        const auto cancel=!q.present ? rescue::Cancel::gone : rescue::Cancel::dead;
        Log("RESCUE cancelled: %s",rescue::CancelText(cancel));
        EndRescue(r,rescue::CancelText(cancel),true);return;
    }
    if(seat<0) {
        const float* p=reinterpret_cast<const float*>(human+kPosition);
        const float* pos=reinterpret_cast<const float*>(r.vehicle+kPosition);
        float deck[3];
        const bool sub=SubDeck(pos,deck);
        Log("RESCUE player got off at (%.0f,%.1f,%.0f)%s%.1f m over the deck",p[0],p[1],p[2],sub ? ", " : " (no carrier) ",sub ? p[1]-deck[1] : 0.0f);
        EndRescue(r,"the player got off",true);
        return;
    }
    Ferry(r,ms);
}

// A fault in a flight's step: it ends as any rescue ends (the heli's own team back, sent off), and only if that
// faults too is it just dropped (the heli then keeps team 5 until the mission ends).
void FlightFault(Rescue& r) noexcept {
    __try { EndRescue(r,"fault in the rescue step",true); }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("RESCUE fault ending it: heli %p left as it is",r.vehicle);r=Rescue{};}
}

// This machine's call is over: a peer's copy gets its own team back; asked again `again` ms later.
void EndCall(const char* why,ULONGLONG ms,ULONGLONG again) noexcept {
    Log("RESCUE call over: %s (heli %p)",why,call.vehicle);
    if(call.phase==CallPhase::assigned && !call.flown && HeliAlive(call.vehicle,call.ref,call.madeFrame,call.seenFrame))
        SetObjectTeam(call.vehicle,call.team);
    call=RescueCall{};call.retryAt=ms+again;
}

// This machine's request brought no heli: said (log and banner), asked again kRetryMs later.
void CallFailed(const wchar_t* why,ULONGLONG ms) noexcept {
    Log("RESCUE request failed: %ls; asking again in %llus",why && why[0] ? why : L"(no reason given)",kRetryMs/1000);
    RescueBanner(true,hudtext::Tr(hudtext::Tx::rescueFailed),why && why[0] ? why : L"-",static_cast<int>(kRetryMs/1000));
    call=RescueCall{};call.retryAt=ms+kRetryMs;
}

// No call yet: the player on foot in the sea for kWetMs with a carrier out asks for the rescue entry at them. The call
// waits from before the request: offline the heli is made within it (RescueHeliDeployed).
void StartRescue(unsigned char* human,ULONGLONG ms) noexcept {
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    float sea=kSeaY;
    if(!Cfg().seaRescue || !OnFoot(human) || !InSea(p,&sea)){call.wetSince=0;call.warned=false;return;}
    if(!call.wetSince)call.wetSince=ms;
    if(ms-call.wetSince<kWetMs || ms<call.retryAt)return;
    float deck[3];
    if(!SubDeck(p,deck)) {
        if(!call.warned)Log("RESCUE player in the sea at (%.0f,%.1f,%.0f), but no submarine carrier out: no rescue",p[0],p[1],p[2]);
        call.warned=true;call.retryAt=ms+kRetryMs;
        return;
    }
    const float at[3]={p[0],p[1],p[2]};
    const ULONGLONG wet=ms-call.wetSince;
    call=RescueCall{};
    call.phase=CallPhase::requested;call.requestedAt=ms;std::memcpy(call.at,at,12);
    Log("RESCUE requested: the player at (%.0f,%.1f,%.0f) in the sea %.1fs, carrier deck (%.0f,%.0f,%.0f); reach=%d autoBoard=%d",
        p[0],p[1],p[2],static_cast<float>(wet)*0.001f,deck[0],deck[1],deck[2],reachOk,Cfg().rescueAutoBoard && boardOk);
    RescueBanner(false,L"%ls",hudtext::Tr(hudtext::Tx::rescueRequested));
    wchar_t note[160]{};
    if(!SupportRescueAt(at,note,_countof(note)) && call.phase==CallPhase::requested)CallFailed(note,ms);
}

// The call's heli is here (`call.vehicle`): the player boards it (pressed for them in the stock reach), rides it, and
// gets off; or it is called off as the flight is (rescue_logic.h PickupCancel), or not boarded in time.
void CallStep(ULONGLONG ms) noexcept {
    auto v=call.vehicle;
    if(!HeliAlive(v,call.ref,call.madeFrame,call.seenFrame)) {
        Log("RESCUE heli %p lost (shot down or gone)",v);
        RescueBanner(true,hudtext::Tr(hudtext::Tx::rescueLost),static_cast<int>(kRetryMs/1000));
        call=RescueCall{};call.retryAt=ms+kRetryMs;return;
    }
    unsigned char* human=nullptr;int seat=-1;
    const ObjRef me=ObjRef::Of(PlayerHuman());
    const rescue::Requester q=Assess(v,me,&call.dryAt,ms,&human,&seat);
    if(seat==0){EndCall("the player took the pilot seat: the heli is theirs",ms,kAgainMs);return;}
    if(!call.flown)SetObjectTeam(v,kTeamVehicle);   // a peer's copy: the stock seat check runs here (see the Rescue comment)
    if(seat>0) {
        if(!call.aboard)Log("RESCUE aboard: seat %d after %d presses",seat,call.tries);
        call.aboard=true;return;
    }
    if(call.aboard && q.present && !q.dead){EndCall("the player got off",ms,kAgainMs);return;}
    if(const auto cancel=rescue::PickupCancel(q);cancel!=rescue::Cancel::none) {
        Log("RESCUE call cancelled: %s",rescue::CancelText(cancel));
        EndCall(rescue::CancelText(cancel),ms,kAgainMs);return;
    }
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    const float away=Dist2(reinterpret_cast<const float*>(v+kPosition),p);
    if(away<=kRescueApproach && !call.nearAt)call.nearAt=ms;
    if(call.nearAt && ms-call.nearAt>kPickupMs){EndCall("not boarded in 120 s",ms,kAgainMs);return;}
    float at[3]{},reach=0.0f;
    const int door=reachOk ? DoorSeat(v,human,at,&reach) : -1;
    if(door<0)return;
    const float d[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
    call.seatDist=std::sqrt(Dot3(d,d));call.seatReach=reach;
    if(call.seatDist>reach || !Cfg().rescueAutoBoard || !boardOk || ms-call.boardTryAt<kBoardTryMs)return;
    call.boardTryAt=ms;++call.tries;
    Log("RESCUE board try %d: seat %d riding point %.2f m from the player (stock reach %.2f), heli %.1f m over them",
        call.tries,door,call.seatDist,reach,reinterpret_cast<const float*>(v+kPosition)[1]-p[1]);
    PressBoard(human);
}

// The rescue's step, once a frame (RescueTick): every flight flown here, then this machine's call.
void RescueStep() noexcept {
    const ULONGLONG ms=GameMs();
    for(auto& r:rescues) {
        if(r.phase==RescuePhase::none)continue;
        __try { FlightStep(r,ms); }
        __except(EXCEPTION_EXECUTE_HANDLER){Log("RESCUE fault: ending the flight");FlightFault(r);}
    }
    unsigned char* const human=PlayerHuman();
    if(call.phase==CallPhase::idle) {
        if(human)StartRescue(human,ms);
        return;
    }
    if(call.phase==CallPhase::requested) {
        if(ms-call.requestedAt>kRequestWaitMs)CallFailed(hudtext::Tr(hudtext::Tx::rescueNoHeli),ms);
        return;
    }
    CallStep(ms);
}
}  // namespace

void RescueHeliDeployed(unsigned char* vehicle,const float* target,bool flown,const ObjRef& requester) noexcept {
    if(!vehicle || !target)return;
    const ULONGLONG frame=GameFrame();
    if(flown) {
        Rescue* r=nullptr;
        for(auto& row:rescues)if(row.phase==RescuePhase::none){r=&row;break;}
        // The heli is the requester's: with none known (left the room meanwhile) or no slot, it leaves at once, as a
        // support heli leaves; it never stays to guard the point on its fuel (2026-10-10, the user: 「为什么会找不到」).
        if(!r || !RequesterObject(requester)) {
            const char* why=r ? rescue::CancelText(rescue::Cancel::gone) : "every rescue slot taken";
            Log("RESCUE cancelled: heli %p made for (%.0f,%.1f,%.0f): %s",vehicle,target[0],target[1],target[2],why);
            LeaveNow(vehicle,why);
        } else {
            *r=Rescue{};
            r->phase=RescuePhase::pickup;r->vehicle=vehicle;r->ref=ObjRef::Of(vehicle);r->swimmer=requester;r->launchFrame=frame;
            r->team=At<std::int32_t>(vehicle,kTeam);r->startedAt=GameMs();r->seat=-1;
            std::memcpy(r->goal,target,12);r->height=reinterpret_cast<const float*>(vehicle+kPosition)[1];
            Log("RESCUE flight: heli %p (410) for the requester %p, asked at (%.0f,%.1f,%.0f), seats=%u",vehicle,requester.obj,target[0],
                target[1],target[2],SeatCount(vehicle));
        }
    }
    if(call.phase!=CallPhase::requested || std::memcmp(call.at,target,12)!=0)return;   // another machine's rescue
    call.phase=CallPhase::assigned;call.vehicle=vehicle;call.ref=ObjRef::Of(vehicle);call.madeFrame=frame;call.flown=flown;
    call.team=At<std::int32_t>(vehicle,kTeam);
    Log("RESCUE heli %p made for this machine's player (%s), %.0f s after the request",vehicle,flown ? "flown here" : "a peer's copy",
        static_cast<float>(GameMs()-call.requestedAt)*0.001f);
    RescueBanner(false,L"%ls",hudtext::Tr(hudtext::Tx::rescueComing));
}

void RescueRequestFailed(const wchar_t* why) noexcept {
    if(call.phase!=CallPhase::requested)return;   // not this machine's waiting request (or already over)
    CallFailed(why,GameMs());
}

bool PlayerRescueCue(RescueCue* out) noexcept {
    AcquireSRWLockShared(&rescueCueLock);
    const bool fresh=rescueCueAt && GetTickCount64()-rescueCueAt<=kRescueCueMs;
    if(fresh){std::memcpy(out->text,rescueCueText,sizeof(out->text));out->bad=rescueCueBad;}
    ReleaseSRWLockShared(&rescueCueLock);
    return fresh;
}

void PressBoardButton(unsigned char* human) noexcept { PressBoard(human); }
void PressBoardButtonBumping(unsigned char* human) noexcept { PressBoard(human,true); }
bool BoardButtonReady() noexcept { return profileOk && boardOk; }
bool HumanOnFoot(const unsigned char* human) noexcept { return OnFoot(human); }

bool SeatPoint(const unsigned char* v,unsigned seat,float* at,float* reach) noexcept {
    if(!reachOk || seat>=SeatCount(v))return false;
    __try { return RidingPoint(SeatAt(const_cast<unsigned char*>(v),seat),at,reach); } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void RescueTick() noexcept {
    if(!profileOk || rescueFrame==GameFrame())return;   // it flies the heli through Fly; at most once a frame
    rescueFrame=GameFrame();
    __try { RescueStep(); }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("RESCUE fault: this machine's call dropped");call=RescueCall{};call.retryAt=GameMs()+kRetryMs;}
}

namespace {
// Weapon user (see EDF6AutoTurret's gunner.cpp): the vehicle's interface at +0x120 answers who operates
// one of its weapons (0x62D950, slot 11 of that interface's vtable): the rider of the seat holding it, or
// null. The fire step refuses a null answer (0x690C0E), so an empty door gun seat could never fire. For a
// 410 the plugin flies (an NPC pilot in seat 0), such a gun is operated by whoever operates the pilot's
// weapon, as autoturret does for the tanks' empty gunner seats.
constexpr unsigned kUserIface410=0x17DF530,kWeaponUserFn=0x62D950;
constexpr std::size_t kUserIface=0x120,kUserSlot=0x58/8;
using UserFn=const void*(__fastcall*)(void*,const void*);
UserFn nextUser=nullptr;

const void* __fastcall DoorGunUser(void* iface,const void* weapon) noexcept {
    const auto user=nextUser(iface,weapon);
    if(user || !doorOk || !Cfg().heliPilot || !Cfg().heliDoorGuns)return user;
    const auto v=static_cast<unsigned char*>(iface)-kUserIface;
    if(At<const unsigned char*>(v,0)!=image+kVt410 || SeatCount(v)==0)return user;
    const auto seat=SeatAt(v,0);
    if(SeatRider(seat)!=Rider::dummy || At<std::uint64_t>(seat,kSeatWeaponCount)==0)return user;
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    if(!Readable(holders,8) || !Readable(holders[0],kHolderWeapon+8))return user;
    const auto pilotWeapon=At<const void*>(holders[0],kHolderWeapon);
    return pilotWeapon && pilotWeapon!=weapon ? nextUser(iface,pilotWeapon) : user;
}

const Signature kDoorSignatures[]={
    {0x64E0B3,{0x49,0x8D,0xBF,0x34,0x20,0x00,0x00,0x45,0x33,0xE4,0x41,0xBE,0x02,0x00,0x00,0x00},16},   // input: the gunner blocks
    {0x64D893,{0x48,0x8D,0xAF,0x40,0x20,0x00,0x00,0xBE,0x40,0x03,0x00,0x00,0x90,0x48,0x8B,0x8F},16},   // apply: block, seat stride
    {0x64D8C8,{0x80,0x7D,0x00,0x00,0x74,0x14,0x48,0x8B,0x87,0x38,0x06,0x00,0x00,0x48,0x8D,0x0C},16},   // apply: trigger byte -> pull
    {kWeaponUserFn,{0x41,0x57,0x48,0x83,0xEC,0x30,0x4C,0x69,0x99,0xF8,0x04,0x00,0x00,0x40,0x03,0x00},16},
    {0x690C0E,{0xFF,0x50,0x58,0x48,0x85,0xC0,0x0F,0x84,0x92,0x00,0x00,0x00,0xF6,0x40,0x08,0x01},16},  // fire: null user -> no shot
};

}  // namespace

// Checks the 410 layout, then chains the weapon-user hook (onto another plugin's, if one is there).
bool InstallDoorGuns() noexcept {
    __try {
        bool ok=profileOk;
        for(const auto& s:kDoorSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI door guns: mismatch at %#zx",s.rva);ok=false;}
        const auto slot=reinterpret_cast<void**>(image+kUserIface410)+kUserSlot;
        void* const current=ok ? *slot : nullptr;
        if(current) {
            if(current!=image+kWeaponUserFn)Log("HELI door guns: weapon user chained onto %p (another plugin)",current);
            nextUser=reinterpret_cast<UserFn>(current);
            doorOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&DoorGunUser));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){doorOk=false;}
    if(doorOk)InstallNpcGunnerAim();
    Log("HELI door guns=%d (410 door guns aimed by the plugin)",doorOk);
    return doorOk;
}

float MapRay(const float* a,const float* b,float* hit) noexcept { return CastRay(a,b,hit); }
// The floor along a map ray (map_floor.h): the hits on a triangle's back skipped once the normals are known to be the
// triangles' own. Learned from the player's floor (LearnMapNormals): `own` is kept for the run (it is the engine's, not
// the map's); `facing` is asked again every kRelearnMs (a thin slab or a second body under the floor reads as facing).
// Game thread only (the map's frame, the commands, the marks).
namespace {
mapfloor::Normals mapNormals=mapfloor::Normals::unknown;
ULONGLONG normalsAskedAt=0;
constexpr ULONGLONG kRelearnMs=1000;
constexpr float kStandClear=2.5f;   // m of open air over a floor a unit can stand on
float FloorHit(const float* from,const float* to,float* hit,float* normal) noexcept {
    return CastRay(from,to,hit,false,nullptr,kMapLayer,normal);
}
}  // namespace
void LearnMapNormals(const float* standing) noexcept {
    if(mapNormals==mapfloor::Normals::own || !rayOk || !std::isfinite(standing[0]+standing[1]+standing[2]))return;
    const ULONGLONG now=GetTickCount64();
    if(mapNormals==mapfloor::Normals::facing && now-normalsAskedAt<kRelearnMs)return;
    normalsAskedAt=now;
    const float top[3]={standing[0],standing[1]+1.5f,standing[2]},bottom[3]={standing[0],standing[1]-20.0f,standing[2]};
    float h1[3],n1[3];
    if(CastRay(top,bottom,h1,false,nullptr,kMapLayer,n1)<0.0f)return;
    // The same floor from just under it: its own normal still up, a facing one now down.
    const float below[3]={h1[0],h1[1]-0.08f,h1[2]},above[3]={h1[0],h1[1]+0.08f,h1[2]};
    float h2[3],n2[3];
    if(CastRay(below,above,h2,false,nullptr,kMapLayer,n2)<0.0f || std::fabs(h2[1]-h1[1])>0.05f)return;
    const mapfloor::Normals was=mapNormals;
    const mapfloor::Normals learned=mapfloor::Learn(n1,n2);
    if(learned==mapfloor::Normals::unknown)return;
    mapNormals=learned;
    if(mapNormals!=was)
        Log("MAP ray normals: %s (a floor at y=%.1f: from above (%.2f,%.2f,%.2f), from below (%.2f,%.2f,%.2f))%s",
            mapNormals==mapfloor::Normals::own ? "the triangles' own" : "facing the ray",h1[1],n1[0],n1[1],n1[2],n2[0],n2[1],n2[2],
            mapNormals==mapfloor::Normals::own ? ": cave roofs seen from above are skipped" : ": cave roofs cannot be told apart");
}
float MapFloorRay(const float* a,const float* b,float* hit) noexcept {
    return mapfloor::Floor(a,b,mapNormals,&FloorHit,hit);
}
bool MapGroundNear(float x,float z,float y,float* h,bool standable) noexcept {
    const float top[3]={x,y+4000.0f,z},bottom[3]={x,y-4000.0f,z};
    // Standable: open air over it (not the ground inside a building's or a rock's collision under its roof).
    auto room=[standable](const float* p){
        if(!standable)return true;
        const float a[3]={p[0],p[1]+0.3f,p[2]},b[3]={p[0],p[1]+kStandClear,p[2]};
        return CastRay(a,b)<0.0f;
    };
    return mapfloor::Near(top,bottom,y,mapNormals,&FloorHit,room,h) && std::isfinite(*h);
}
// Layer 27 (filter 0x1B) collides with layers 15, 16, 17, 18 and 20 alone (the CollisionFilter ctor 0x105510's pair
// table, docs/emc-re.md §3): the layers the map objects' creation code puts buildings on (docs/raycast-re.md §3), not
// 19 / 26 (the terrain's and the units' / vehicles'). With the plain nearest-hit collector: every hit on those layers.
float BuildingRay(const float* a,const float* b,float* hit) noexcept { return CastRay(a,b,hit,true,nullptr,0x1B); }
Sea SeaAt(float x,float z,float* surface) noexcept { return SeaProbe(x,z,surface); }

bool VisitEnemies(const unsigned char* vehicle,EnemyVisitor visit,void* ctx) noexcept {
    return ForEachEnemy(vehicle,[&](const void* object,const float* aim) noexcept { visit(ctx,object,aim); });
}

bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept {
    const auto registry=At<const unsigned char*>(image,kRegistry);
    if(!Readable(registry,kRegList+0x10))return false;
    const auto head=At<const unsigned char*>(registry,kRegList);
    if(!Readable(head,0x10))return false;
    int n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<kMaxNodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object[kDead])continue;
        const float* a=reinterpret_cast<const float*>(target+kTargetAim);
        if(std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]))visit(ctx,object,a);
    }
    return true;
}

bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept {
    return ForEachEnemyOf(team,nullptr,[&](const void* object,const float* aim) noexcept { visit(ctx,object,aim); });
}

bool NearLine(const float* from,const float* to,const float* point,float radius) noexcept {
    const float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float p[3]={point[0]-from[0],point[1]-from[1],point[2]-from[2]};
    const float dd=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(dd<1.0f)return false;
    const float t=(p[0]*d[0]+p[1]*d[1]+p[2]*d[2])/dd;
    if(t<0.0f || t>1.0f)return false;
    const float c[3]={p[0]-d[0]*t,p[1]-d[1]*t,p[2]-d[2]*t};
    return c[0]*c[0]+c[1]*c[1]+c[2]*c[2]<radius*radius;
}

bool FriendInLine(const float* from,const float* to,const void* self) noexcept {
    return PlayerInLine(from,to) || JetInLine(from,to,self);
}

bool CheckHeliProfile() noexcept {
    __try {
        for(const auto& s:kHeliSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI profile mismatch at %#zx",s.rva);return false;}
        profileOk=true;
        Log("HELI medic healing permission=%d (410 local/replayed shots heal friends)",InstallMedicPermission());
        Log("HELI player attitude=%d (mouse pitch separated from forward speed)",InstallPlayerAttitude());
        // Called helis that left are deleted (HeliReap) with the game's Delete, as jet.cpp deletes its jets.
        deleteOk=Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
        if(!deleteOk)Log("HELI delete: profile mismatch (called helis that leave fly on away instead of being deleted)");
        // Avoidance has its own check: without it the helis still fly, just blind.
        rayOk=Readable(image+kHitVtbl,0x28) && At<const unsigned char*>(image,kHitVtbl)==image+kHitSlot0 &&
              At<const unsigned char*>(image,kHitVtbl+0x20)==image+kHitAdd &&
              Readable(image+kGroundVtbl,0x28) && At<const unsigned char*>(image,kGroundVtbl)==image+kHitSlot0 &&
              At<const unsigned char*>(image,kGroundVtbl+0x10)==image+kHitReset &&
              At<const unsigned char*>(image,kGroundVtbl+0x20)==image+kGroundAdd;
        for(const auto& s:kRaySignatures)rayOk=rayOk && Matches(s.rva,s.bytes,s.size);
        Log("HELI ray=%d (obstacle avoidance %s)",rayOk,rayOk ? "on" : "off: unexpected EDF.dll code");
        visitOk=Matches(kVisitFriends,kVisitFriendsSig,sizeof(kVisitFriendsSig));
        Log("HELI friends walk=%d%s",visitOk,visitOk ? "" : " (unexpected EDF.dll code: medic helis aim at nobody)");
        waterOk=true;
        for(const auto& s:kWaterSignatures)waterOk=waterOk && Matches(s.rva,s.bytes,s.size);
        Log("WATER probe=%d%s",waterOk,waterOk ? "" : " (unexpected EDF.dll code: the carrier surfaces anywhere)");
        // The sea rescue: the seat reach is read for the pickup, the board button pressed with RescueAutoBoard.
        reachOk=Readable(image+kReachSlackRva,4) && At<float>(image,kReachSlackRva)==kReachSlack;
        for(std::size_t i=3;i<sizeof(kRescueSignatures)/sizeof(kRescueSignatures[0]);++i)
            reachOk=reachOk && Matches(kRescueSignatures[i].rva,kRescueSignatures[i].bytes,kRescueSignatures[i].size);
        boardOk=reachOk;
        for(std::size_t i=0;i<3;++i)
            boardOk=boardOk && Matches(kRescueSignatures[i].rva,kRescueSignatures[i].bytes,kRescueSignatures[i].size);
        Log("RESCUE seat reach=%d board button=%d (sea rescue %s, auto board %s)",reachOk,boardOk,Cfg().seaRescue ? "on" : "off",
            Cfg().rescueAutoBoard ? (boardOk ? "on" : "off: unexpected EDF.dll code") : "off: the player boards with their own button");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){profileOk=false;return false;}
}

// A new mission (mission.cpp MissionStart): the last mission's helis are gone, the player's track with them (a
// new map), and the rescue (its heli was the last mission's).
void ResetHelis() noexcept {
    for(auto& h:helis)h=Heli{};
    for(auto& a:assists)a=Assist{};
    for(auto& p:pilots)p=Pilot{};
    fullLoggedAt=0;
    ResetTrack();
    for(auto& r:rescues)r=Rescue{};
    call=RescueCall{};rescueFrame=0;
    AcquireSRWLockExclusive(&rescueCueLock);rescueCueAt=0;ReleaseSRWLockExclusive(&rescueCueLock);
}
}  // namespace crew
