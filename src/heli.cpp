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
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kHeliVtables[]={0x17DB238,0x17DEF98,0x17DF338,0x17DF790};   // 506, 409, 410, base
constexpr unsigned kHeli506=0x17DB238,kHeli409=0x17DEF98,kHeli410=0x17DF338;
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
// The 409's gun sits in a turret that turns onto any enemy within ~kTurretReach by itself (pitch
// 0..-90: below only), so there it fires whatever the nose does.
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponSpeed=0x894,kWeaponGravity=0x8E0,kWeaponAmmo=0xBE8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::int32_t kHoming=1;
constexpr float kGravity=14.7f;      // m/s^2 (measured, autoturret re-notes)
constexpr float kRocketStart=0.5f,kRocketAccel=0.03f,kRocketCone=3.0f,kRocketRange=250.0f;
constexpr float kTurretReach=70.0f;
// No weapon of the helis reloads (ReloadTime -1), so like the AI's ground vehicles (whose _ai weapons do)
// an emptied one is refilled to what it held when first seen, kReloadGunMs / kReloadAltMs after it ran
// dry. Short of that the guns fire kBurstMs bursts with kBurstRest between.
constexpr ULONGLONG kReloadGunMs=8000,kReloadAltMs=15000,kBurstMs=2000,kBurstRest=1000;
constexpr float kMissileMin=50.0f;   // m: no missile closer than this
constexpr float kKeepTarget=30.0f;   // m: the current target counts this much nearer (less switching)
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

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kHeliSignatures[]={
    {0x6543A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48},16},   // base input
    {0x61B8F0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x20,0x48},16},   // 506 input
    {0x65451A,{0x4C,0x89,0xB7,0x40,0x15,0x00,0x00,0x44,0x89,0xB7,0x48,0x15,0x00,0x00,0x48,0xC7},16},   // input block reset
    {0x61B71E,{0x80,0xBB,0x20,0x20,0x00,0x00,0x00},7},                                                 // 506 gatlings read
    {0x61B743,{0x80,0xBB,0x21,0x20,0x00,0x00,0x00},7},                                                 // 506 missile read
};
bool profileOk=false;

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
float CastRay(const float* a,const float* b,float* hit=nullptr,bool any=false,std::uint32_t* flags=nullptr) noexcept {
    if(!rayOk)return -1.0f;
    const auto g=At<unsigned char*>(image,kHavokGlobal);
    if(!Readable(g,0x70) || !At<const void*>(g,0x68))return -1.0f;
    const RayInput in{{a[0],a[1],a[2],1.0f},{b[0],b[1],b[2],1.0f},0x16,0,0};
    RayHits col{};
    *reinterpret_cast<const void**>(col.raw)=image+(any ? kHitVtbl : kGroundVtbl);
    reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
    reinterpret_cast<void(*)(void*,void*,const RayInput*)>(image+kCastRay)(g+0x10,&col,&in);
    if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)return -1.0f;
    const float f=*reinterpret_cast<const float*>(col.raw+0x50);
    if(!std::isfinite(f) || f<0.0f || f>1.0f)return -1.0f;
    if(hit)std::memcpy(hit,col.raw+0x30,12);
    if(flags)std::memcpy(flags,col.raw+0x9C,4);
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return f*std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

// One 410 door gun as DoorGun drives it (see there).
struct Door {
    const unsigned char* weapon;
    const void* target;
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
    const void* vehicle;
    const void* kind;     // its vtable: the type its flight is made of
    ULONGLONG crewedAt,seen,loggedAt,missileAt,targetAt,groundAt;
    LARGE_INTEGER last;
    float prev[3],vel[3];
    float hover;          // learned rotor speed that holds height
    float prevHeading,lastYaw,yawRate;
    int yawSign,votes;    // +1: a positive yaw input increases atan2(fwd.x, fwd.z)
    bool yawLocked,started;
    const void* target;
    float hold[3];        // where it holds when it has nobody to follow
    float pos[3];         // last position, for the other helis' formation and separation
    bool extend;          // engaged: broke off a run, flying out to turn back in (see Engage)
    ULONGLONG extendAt;   // when it broke off
    float extendTo[3];    // where it extends to
    struct Arm { unsigned char* weapon; std::int32_t full; ULONGLONG emptyAt; } arms[4];
    ULONGLONG burstAt,restUntil;   // the gun's current burst began / it rests until
    const void* passed;   // the target it last broke off from (see kPassedMs)
    ULONGLONG passedUntil;
    bool firing;
    float losLift;        // metres its engaged hover is raised to see over the map (see kAimWall)
    const void* tracked;  // the target tgtPrev/tgtVel belong to
    float tgtPrev[3],tgtVel[3];
    ULONGLONG playerAt;   // the player fix pVel was last updated from
    ULONGLONG enemyAt;    // game ms: an enemy was last within heliRange of whom it follows
    float pPrev[3],pVel[3];
    float top,stopDecel;  // m/s at full stick, and the braking it plans with (see Tune)
    bool tuned;           // Fly writes params (k, b, max yaw, yaw smoothing) every frame
    float params[4];
    ULONGLONG circleUntil;// 409: circling for its turret until then (see kTurretCircleMs)
    Door doors[2];        // 410: left, right
};
Heli helis[16]{};
// The player's last move: they count as standing still once within 3 m of `still` since `stillAt`
// (game clock: a pause does not count as standing still).
// moveDir: see kDirMs.
float still[3]{},moveDir[3]{0,0,1};ULONGLONG stillAt=0;

// Where the player has been (see kTrackMs), and the ellipse's area round it: centre (x, z), unit long
// axis u and w (u turned a right angle), the half spans along both, and the net move over the track.
struct TrackPoint { float x,z; ULONGLONG at; };
TrackPoint track[kTrackSize]{};int trackHead=0,trackCount=0;
struct Area { float centre[2],u[2],w[2],halfU,halfW,net; };
Area area{{0,0},{0,1},{1,0},0,0,0};
bool roaming=false;   // the player travels: the helis escort (else they fly the ellipse)

const TrackPoint& TrackAt(int back) noexcept { return track[(trackHead-1-back+2*kTrackSize)%kTrackSize]; }

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
    for(auto& h:helis)if(h.vehicle==vehicle)return &h;
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
float MinAimHoriz() noexcept { return cfg.heliFireHeight/std::tan(kMaxDip); }

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

// The enemy lock point to engage, among the enemies within `range` of `around`: the one nearest to
// `from` (the heli: the shortest turn and flight), the current one counting kKeepTarget nearer and one
// too close below to aim at kTooClose farther. Returns false with none.
bool PickTarget(Heli& h,const unsigned char* v,const float* around,const float* from,float range,float* aim) noexcept {
    const float minHoriz=MinAimHoriz();
    const ULONGLONG now=GetTickCount64();
    const float speed=std::sqrt(Dot2(h.vel,h.vel));
    float best=0.0f,bestAim[3]{};const void* bestObject=nullptr;
    ForEachEnemy(v,[&](const void* object,const float* a) noexcept {
        const float d[3]={a[0]-around[0],a[1]-around[1],a[2]-around[2]};
        if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>range*range)return;
        const float f[3]={a[0]-from[0],a[1]-from[1],a[2]-from[2]};
        float score=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
        if(object==h.target)score-=kKeepTarget;
        if(object==h.passed && now<h.passedUntil)score+=kPassed;
        if(speed>3.0f && Dot2(f,f)>1.0f)
            score+=kTurnCost*std::fabs(Wrap(std::atan2(f[0],f[2])-std::atan2(h.vel[0],h.vel[2])));
        for(const auto& o:helis)if(&o!=&h && o.vehicle && o.target==object && now-o.seen<2000)score+=kShareTarget;
        if(std::sqrt(Dot2(f,f))<minHoriz)score+=kTooClose;
        if(!bestObject || score<best){best=score;bestObject=object;std::memcpy(bestAim,a,12);}
    });
    if(!bestObject)return false;
    if(bestObject!=h.target)h.targetAt=now;
    h.target=bestObject;std::memcpy(aim,bestAim,12);
    return true;
}

float GunRange(const unsigned char* v) noexcept {
    return At<const unsigned char*>(v,0)==image+kHeli506 && kGun506Range<cfg.heliRange ? kGun506Range : cfg.heliRange;
}

// Would a burst from `from` towards `to` pass within 8 m of the player before reaching the target?
bool PlayerInLine(const float* from,const float* to) noexcept {
    if(!player.at || GetTickCount64()-player.at>2000)return false;
    const float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float p[3]={player.pos[0]-from[0],player.pos[1]-from[1],player.pos[2]-from[2]};
    const float dd=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(dd<1.0f)return false;
    const float t=(p[0]*d[0]+p[1]*d[1]+p[2]*d[2])/dd;
    if(t<0.0f || t>1.0f)return false;
    const float c[3]={p[0]-d[0]*t,p[1]-d[1]*t,p[2]-d[2]*t};
    return c[0]*c[0]+c[1]*c[1]+c[2]*c[2]<64.0f;
}

// This heli's flight: the helis flown right now of its type, in `helis` order (the first one leads);
// `group` numbers the flights in the order their first heli comes, `first` is the first heli of all.
struct Flight { int group,groups,wing,count; const Heli* leader; const Heli* first; };

// See kReach410.
float Span(const Heli& h) noexcept {
    return h.kind==image+kHeli410 ? kReach410 : kReachOther;
}
// A flight's helis (one type) WingGap apart: two reaches plus kClearAir.
float WingGap(const Heli& h) noexcept {
    return 2.0f*Span(h)+kClearAir;
}
Flight FlightOf(const Heli& h,ULONGLONG ms) noexcept {
    Flight f{0,0,0,0,nullptr,nullptr};
    const void* kinds[16];
    for(const auto& o:helis) {
        if(!o.vehicle || ms-o.seen>=2000)continue;
        if(!f.first)f.first=&o;
        int g=0;
        while(g<f.groups && kinds[g]!=o.kind)++g;
        if(g==f.groups)kinds[f.groups++]=o.kind;
        if(o.kind!=h.kind)continue;
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
        if(&o==&h || !o.vehicle || ms-o.seen>2000)continue;
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
// and heliMoveGain/heliBrakeGain per metre near it (the settle of the old position PD).
void Arrive(const Heli& h,const float* pos,const float* goal,const float* goalVel,float* out) noexcept {
    const float e[3]={goal[0]-pos[0],0,goal[2]-pos[2]};
    const float len=std::sqrt(Dot2(e,e));
    out[0]=goalVel[0];out[1]=0;out[2]=goalVel[2];
    if(len<0.1f)return;
    const float rel[3]={h.vel[0]-goalVel[0],0,h.vel[2]-goalVel[2]};
    const float closing=Dot2(rel,e)/len>0.0f ? Dot2(rel,e)/len : 0.0f;
    const float left=len-closing*kStopLag>0.0f ? len-closing*kStopLag : 0.0f;
    const float settle=len*cfg.heliMoveGain/(cfg.heliBrakeGain>0.01f ? cfg.heliBrakeGain : 0.01f);
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
    *height=player.pos[1]+cfg.heliHeight+Stack(fl);
    if(roaming) {
        // Each flight a V: the first flight on the side the first heli is on, heliFollow out and
        // kEscortAhead forward, the second on the other side, the next ones kGroupGap farther out,
        // alternating. Wing n takes place (n+1)/2 on alternating sides, WingGap back and out per place.
        const float perp[2]={moveDir[2],-moveDir[0]};
        const float toFirst[2]={first[0]-player.pos[0],first[2]-player.pos[2]};
        const float flank=(toFirst[0]*perp[0]+toFirst[1]*perp[1]<0 ? -1.0f : 1.0f)*(fl.group%2 ? -1.0f : 1.0f);
        const float place=static_cast<float>((fl.wing+1)/2),side=fl.wing%2 ? 1.0f : -1.0f;
        const float out=cfg.heliFollow+kGroupGap*static_cast<float>(fl.group/2)+side*place*WingGap(h);
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
    const float r=cfg.heliFollow>10.0f ? cfg.heliFollow : 10.0f;
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
// passed 30 deg of dip 85 m out and hardly aimed at all. Returns how far it is from where it is going.
float Engage(Heli& h,const float* pos,const float* aim,float dipWant,bool wall,const Flight& fl,bool follow,bool circleOnBreak,
             ULONGLONG ms,float* vel,float* height) noexcept {
    const float ground=follow && player.pos[1]>aim[1] ? player.pos[1] : aim[1];
    *height=ground+cfg.heliFireHeight+kRunStack*static_cast<float>(fl.group)+kRunWing*static_cast<float>(fl.wing);
    float to[3]={aim[0]-pos[0],0,aim[2]-pos[2]};
    const float horiz=std::sqrt(Dot2(to,to));
    if(horiz<0.1f){to[0]=0;to[2]=1;}
    else{to[0]/=horiz;to[2]/=horiz;}
    if(!h.extend && (dipWant>kMaxDip || wall)) {
        if(circleOnBreak){h.circleUntil=ms+kTurretCircleMs;h.passed=h.target;h.passedUntil=ms+kPassedMs;}
        else StartExtend(h,pos,aim,fl,follow,ms);
    }
    if(h.extend) {
        float e[3]={h.extendTo[0]-pos[0],0,h.extendTo[2]-pos[2]};
        const float off=std::sqrt(Dot2(e,e));
        const float speed=std::sqrt(Dot2(h.vel,h.vel));
        const bool ahead=h.target!=h.passed && speed>3.0f && horiz>MinAimHoriz()+20.0f &&
            std::fabs(Wrap(std::atan2(to[0],to[2])-std::atan2(h.vel[0],h.vel[2])))<kAhead;
        if(ahead || off<20.0f || horiz>kExtend || ms-h.extendAt>kExtendMs)h.extend=false;
        else {
            vel[0]=e[0]/off*h.top;vel[1]=0;vel[2]=e[2]/off*h.top;
            return off;
        }
    }
    vel[0]=h.tgtVel[0]+to[0]*h.top;vel[1]=0;vel[2]=h.tgtVel[2]+to[2]*h.top;
    if(follow && PlayerInLine(pos,aim)) {
        const float tangent[3]={to[2],0,-to[0]};
        const float toPlayer[3]={player.pos[0]-aim[0],0,player.pos[2]-aim[2]};
        const float away=Dot2(tangent,toPlayer)>0.0f ? -kSidestep : kSidestep;
        vel[0]+=tangent[0]*away;vel[2]+=tangent[2]*away;
    }
    return horiz;
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

// The weapons in the pilot's seat (seat 0): the gun (fastest straight round), the homing missile
// (LockonType 1) and the straight rockets (slow, accelerating rounds). Refills an emptied one (see
// kReloadGunMs). Reads the gun's round speed and drop for the lead.
struct Loadout { float gunSpeed,gunGravity,rocketGravity; bool gun,missile,rockets; std::int32_t ammo[4],rocketAmmo; };
Loadout Arms(Heli& h,unsigned char* v,ULONGLONG ms) noexcept {
    Loadout l{kBulletSpeed,0.0f,1.0f,false,false,false,{-1,-1,-1,-1},0};
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
        if(ammo<=0 && arm.full>0) {
            if(!arm.emptyAt)arm.emptyAt=ms;
            else if(ms-arm.emptyAt>((homing || rocket) ? kReloadAltMs : kReloadGunMs)) {
                Put<std::int32_t>(weapon,kWeaponAmmo,arm.full);arm.emptyAt=0;
                if(cfg.debug)Log("HELI v=%p reloaded weapon %llu (%p) to %d",v,static_cast<unsigned long long>(i),weapon,arm.full);
            }
        } else arm.emptyAt=0;
        l.ammo[i]=ammo;
        if(homing)l.missile=true;
        else if(rocket){l.rockets=true;l.rocketAmmo+=ammo>0 ? ammo : 0;if(std::isfinite(gravity))l.rocketGravity=gravity;}
        else if(std::isfinite(speed) && speed>best) {
            best=speed;l.gun=true;l.gunSpeed=speed;l.gunGravity=std::isfinite(gravity) && gravity>0.0f ? gravity : 0.0f;
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
    if(!cfg.heliAvoid || !rayOk)return r;
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
    if(cfg.debug)r.anyRoof=RoofBelow(ahead,top,true,&r.anyFlags);
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
// Gun i (0 left, 1 right) belongs to seat i+1 and trigger i (veh+0x638, stride 0x48: +8 the weapon's
// weak_ptr control block, +0x10 the weapon). The stock input (slot 55, 0x64E080) fills block i at
// veh+0x2030+i*0x20 from that seat's rider, zeros for an empty seat: +0 yaw input, +4 pitch input, +0x10
// the trigger byte. Slot 57 (0x64D870) hands each block to its seat's aim (seat+0xE0, the tanks'
// VehicleWeaponAim: axes at +0x10, stride 0x40, {min, max, angle}; an axis turns input x k rad per frame)
// and pulls the trigger (0x62C000) while the byte is set. Writing the block after the stock input aims and
// fires an unmanned door gun, and DoorGunUser lets its rounds out. The aim is closed on the real barrel
// (the muzzle frame fire builds, see Barrel) with one sign per axis (axis angle vs geometric angle)
// learned from how the barrel moves, as EDF6AutoTurret's tank gunners do; an axis held at a stop with the
// error not closing for kStuckMs flips its sign too.
constexpr std::size_t kTriggers=0x638,kTriggerCount=0x648,kTriggerStride=0x48,kTriggerCtrl=0x8,kTriggerWeapon=0x10;
constexpr std::size_t kDoorBlock=0x2030,kDoorStride=0x20,kDoorPull=0x10;
constexpr std::size_t kSeatAim=0xE0,kAimAxes=0x10,kAxisStride=0x40,kAxisMin=0x0,kAxisMax=0x4,kAxisAngle=0x8;
constexpr std::size_t kMuzzles=0x1D0,kMuzzleCount=0x1E0,kMuzzleStride=0xF0,kMuzzleLocal=0x10,kBoneRows=0xB0;
constexpr std::size_t kMuzzleMode=0xE0,kWeaponRows=0x150,kWeaponAlive=0x898;
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
void DoorGun(Heli& h,unsigned char* v,int i,bool hold,float dt,ULONGLONG ms) noexcept {
    Door& g=h.doors[i];
    unsigned char* blk=v+kDoorBlock+i*kDoorStride;
    unsigned char* seat=SeatAt(v,static_cast<unsigned>(i+1));
    if(SeatRider(seat)==Rider::player){g.prevValid=false;return;}   // theirs: their stick, their trigger
    const auto triggers=At<unsigned char*>(v,kTriggers);
    if(At<std::uint64_t>(v,kTriggerCount)<=static_cast<std::uint64_t>(i) || !Readable(triggers+i*kTriggerStride,kTriggerStride))return;
    const auto trigger=triggers+i*kTriggerStride;
    const auto ctrl=At<const unsigned char*>(trigger,kTriggerCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)return;
    const auto weapon=At<unsigned char*>(trigger,kTriggerWeapon);
    if(!Readable(weapon,kWeaponAmmo+4,true))return;
    float gp[3],gd[3];
    if(!Barrel(v,weapon,gp,gd))return;
    // No weapon of the helis reloads: refill an emptied gun as Arms does.
    const std::int32_t ammo=At<std::int32_t>(weapon,kWeaponAmmo);
    if(g.weapon!=weapon){g.weapon=weapon;g.full=ammo;g.emptyAt=0;}
    if(ammo>g.full)g.full=ammo;
    if(ammo<=0 && g.full>0) {
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
    // The target: the cheapest enemy in reach, the current one and the pilot's counting nearer.
    const float speed=At<float>(weapon,kWeaponSpeed)*60.0f;
    const float reach=At<float>(weapon,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(weapon,kWeaponAlive));
    const float range=std::isfinite(reach) && reach>0.0f && reach<kDoorRange ? reach : kDoorRange;
    float gravity=At<float>(weapon,kWeaponGravity);
    if(!std::isfinite(gravity) || gravity<0.0f)gravity=0.0f;
    const void* best=nullptr;float bestScore=0.0f,bestAt[3]{};
    if(std::isfinite(speed) && speed>1.0f)
        ForEachEnemy(v,[&](const void* object,const float* p) noexcept {
            const float d[3]={p[0]-gp[0],p[1]-gp[1],p[2]-gp[2]};
            const float dist=std::sqrt(Dot3(d,d));
            float err[2],axis[2];
            if(dist>range || dist<kDoorMin || !Reach(v,a,gp,p,err,axis))return;
            float score=dist+(std::fabs(err[0])+std::fabs(err[1]))*kDoorSlew;
            if(object==g.target)score-=kDoorKeep;
            if(object==h.target)score-=kDoorShare;
            if(!best || score<bestScore){best=object;bestScore=score;std::memcpy(bestAt,p,12);}
        });
    float in[2]={0,0},err[2]={0,0},axis[2]={a.angle[0],a.angle[1]},lead[3]{},dist=0.0f;
    bool fire=false;
    if(best) {
        TrackVelocity(g.tgtPrev,g.tgtVel,bestAt,dt,40.0f,g.target!=best);
        g.target=best;
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
        fire=!hold && cfg.heliFire && std::fabs(err[0])<cone && std::fabs(err[1])<cone && dist>kDoorMin && !PlayerInLine(gp,lead);
    } else g.target=nullptr;
    Put<float>(blk,0,in[0]);Put<float>(blk,4,in[1]);blk[kDoorPull]=fire ? 1 : 0;
    g.firing=fire;
    for(int k=0;k<2;++k){g.in[k]=in[k];g.axisPrev[k]=a.angle[k];g.barrelPrev[k]=a.barrel[k];}
    g.prevValid=true;
    if(cfg.debug && ms-g.loggedAt>1000) {
        g.loggedAt=ms;
        Log("GUNNER410 v=%p gun=%d t=%p dist=%.0f barrel=(%.2f,%.2f) err=(%.3f,%.3f) axis=(%.2f,%.2f)->(%.2f,%.2f) lim=(%.2f..%.2f, %.2f..%.2f) sign=(%+.0f,%+.0f) k=(%.4f,%.4f) in=(%.2f,%.2f) fire=%d ammo=%d",
            v,i,best,dist,a.barrel[0],a.barrel[1],err[0],err[1],a.angle[0],a.angle[1],axis[0],axis[1],a.lo[0],a.hi[0],a.lo[1],a.hi[1],
            a.sign[0],a.sign[1],g.k[0],g.k[1],in[0],in[1],fire,ammo);
    }
}

bool doorOk=false;   // the 410 layout DoorGun writes matched (CheckHeliProfile)

void Fly(Heli& h,unsigned char* v,bool playerAboard) noexcept {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    float right[3],fwd[3];
    if(!Row(v,kHeadRight,right) || !Row(v,kHeadForward,fwd))return;
    const float heading=std::atan2(fwd[0],fwd[2]);
    std::memcpy(h.pos,pos,12);
    if(!h.started) {
        h.started=true;h.last=now;std::memcpy(h.prev,pos,12);std::memcpy(h.hold,pos,12);
        const float rotor=At<float>(v,kRotor);
        h.hover=std::isfinite(rotor) && rotor>0.2f && rotor<1.0f ? rotor : 0.5f;
        h.prevHeading=heading;h.yawSign=1;
        return;
    }
    const float dt=Clamp(static_cast<float>(now.QuadPart-h.last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    h.last=now;
    if(h.tuned) {   // see Tune
        Put<float>(v,kSpeedGain,h.params[0]);Put<float>(v,kBlend,h.params[1]);
        Put<float>(v,kMaxYaw,h.params[2]);Put<float>(v,kYawSmooth,h.params[3]);
    }
    for(int i=0;i<3;++i){const float raw=(pos[i]-h.prev[i])/dt;h.vel[i]+= (raw-h.vel[i])*0.3f;h.prev[i]=pos[i];}
    // In contact (see kGroundContact): on the ground, or perched on another body.
    const bool contact=(v[kContact]&kContactGround)!=0;
    bool grounded=contact;
    if(contact && cfg.heliAvoid && rayOk) {
        const float down[3]={pos[0],pos[1]-kRoofProbe*2.0f,pos[2]};
        const float below=CastRay(pos,down);
        grounded=below>=0.0f && below<kGroundContact;
    }
    const bool perched=contact && !grounded;

    // Learn the yaw sign from the turn the last input produced.
    const float turned=Wrap(heading-h.prevHeading);h.prevHeading=heading;
    h.yawRate+=(turned/dt-h.yawRate)*0.3f;
    if(!grounded && !h.yawLocked && std::fabs(h.lastYaw)>0.3f && std::fabs(turned)>0.0005f) {
        h.votes+=(turned>0)==(h.lastYaw*static_cast<float>(h.yawSign)>0) ? 1 : -1;   // did it turn the way we meant?
        if(h.votes<=-15){h.yawSign=-h.yawSign;h.votes=0;Log("HELI v=%p yaw sign flipped to %d",v,h.yawSign);}
        else if(h.votes>=30){h.yawLocked=true;Log("HELI v=%p yaw sign locked at %d",v,h.yawSign);}
    }
    // The nose (the guns are fixed along it), pitch included.
    const float* noseRow=reinterpret_cast<const float*>(v+kMatrix+0x20);
    const float noseLen=std::sqrt(noseRow[0]*noseRow[0]+noseRow[1]*noseRow[1]+noseRow[2]*noseRow[2]);
    const bool noseOk=std::isfinite(noseLen) && noseLen>0.5f;
    const float nose[3]={noseOk ? noseRow[0]/noseLen : fwd[0],noseOk ? noseRow[1]/noseLen : 0.0f,noseOk ? noseRow[2]/noseLen : fwd[2]};
    const float dip=-std::asin(Clamp(nose[1],-1.0f,1.0f));

    // Who to follow, what to shoot, and how they move.
    const ULONGLONG ms=GetTickCount64();
    const Flight flight=FlightOf(h,ms);
    const int wing=flight.wing;
    const bool follow=!playerAboard && player.at && ms-player.at<10000;
    if(follow)TrackPlayerStill();
    if(follow && player.at!=h.playerAt) {
        const float pdt=h.playerAt ? static_cast<float>(player.at-h.playerAt)*0.001f : 0.0f;
        TrackVelocity(h.pPrev,h.pVel,player.pos,pdt>0.005f ? pdt : 0.005f,40.0f,!h.playerAt || pdt>0.5f);
        h.playerAt=player.at;
    }
    if(!follow){h.pVel[0]=h.pVel[1]=h.pVel[2]=0.0f;h.playerAt=0;}
    const float* anchor=follow ? player.pos : pos;
    float aim[3]{};
    // Following the player it only takes on enemies its gun reaches from within heliCombatRange of them.
    const float pick=follow && cfg.heliCombatRange+GunRange(v)<cfg.heliRange ? cfg.heliCombatRange+GunRange(v) : cfg.heliRange;
    const bool engage=PickTarget(h,v,anchor,pos,pick,aim);
    if(engage) {
        TrackVelocity(h.tgtPrev,h.tgtVel,aim,dt,40.0f,h.tracked!=h.target);
        h.tracked=h.target;
    } else {
        h.tracked=nullptr;h.extend=false;
    }
    const Loadout arms=Arms(h,v,ms);
    const bool is409=At<const unsigned char*>(v,0)==image+kHeli409;
    const bool is410=At<const unsigned char*>(v,0)==image+kHeli410;
    // Lead the target by the rounds' flight time and drop: the gun's, and the rockets' for the 409, whose
    // nose aims them (its gun is in a turret). The nose, the dip and the fire test use the lead point.
    float lead[3]{},gunLead[3]{},dist=0.0f,dipWant=0.0f,losRate=0.0f;
    if(engage) {
        const float gunSpeed=arms.gunSpeed>1.0f ? arms.gunSpeed : kBulletSpeed;
        LeadPoint(pos,aim,h.tgtVel,arms.gunGravity,[gunSpeed](float d) noexcept { return d/gunSpeed; },gunLead);
        if(is409 && arms.rockets)LeadPoint(pos,aim,h.tgtVel,arms.rocketGravity,RocketTime,lead);
        else std::memcpy(lead,gunLead,12);
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        dipWant=std::atan2(-d[1],std::sqrt(Dot2(d,d)));
        // Bearing rate of the target: d/dt atan2(dx, dz) with the relative velocity.
        const float rel[3]={h.tgtVel[0]-h.vel[0],0,h.tgtVel[2]-h.vel[2]};
        if(Dot2(d,d)>1.0f)losRate=(d[2]*rel[0]-d[0]*rel[2])/Dot2(d,d);
    }
    const float toPlayer[3]={player.pos[0]-pos[0],0,player.pos[2]-pos[2]};
    const bool byPlayer=follow && Dot2(toPlayer,toPlayer)<kBoardRange*kBoardRange;
    // Land by a player who stands still with no enemy about (so they can walk up and take it over),
    // and stay down while they are next to it. "No enemy about" is none within heliRange of the player
    // for heliLandMs, not "no target picked this frame": the pick range is narrower while following,
    // and between two kills there is none, which used to set it down mid-fight.
    const ULONGLONG game=GameMs();
    if(!follow)h.enemyAt=game;
    else ForEachEnemy(v,[&](const void*,const float* p) noexcept {
        const float d[3]={p[0]-player.pos[0],0,p[2]-player.pos[2]};
        if(Dot2(d,d)<cfg.heliRange*cfg.heliRange)h.enemyAt=game;
    });
    const bool quiet=game-h.enemyAt>cfg.heliLandMs;
    const bool land=follow && !engage && quiet && cfg.heliLandMs && (game-stillAt>cfg.heliLandMs || (grounded && byPlayer));

    // The map between it and the target, and a wall along the nose (see kAimWall).
    bool hidden=false,wallAhead=false;
    if(engage && cfg.heliAvoid && rayOk) {
        const float nosePt[3]={pos[0]+fwd[0]*kAimWall,pos[1],pos[2]+fwd[2]*kAimWall};
        wallAhead=!h.extend && CastRay(pos,nosePt)>=0.0f;
        const float seen=CastRay(pos,lead);
        hidden=seen>=0.0f && seen<dist-kLosSlack;
    }

    // The wanted horizontal velocity and height.
    const float range=GunRange(v);
    const float aimRange=range<kRunAim ? range : kRunAim;
    float want[3]={0,0,0},height=pos[1],off=0.0f;
    const float rest[3]={0,0,0};
    // Engaged, the 410 always circles (its guns are in its doors); the 409 circles for its turret after a
    // rocket run broke off, and while its rockets are spent (see kTurretCircleMs).
    const bool rocketsLeft=arms.rockets && arms.rocketAmmo>0;
    bool circling=false;
    if(land) {
        float dir[3]={pos[0]-player.pos[0],0,pos[2]-player.pos[2]};
        float len=std::sqrt(Dot2(dir,dir));
        if(len<1.0f){dir[0]=-fwd[0];dir[2]=-fwd[2];len=1.0f;}
        const float r=cfg.heliFollow<kLandDistance ? cfg.heliFollow : kLandDistance;
        const float spot[3]={player.pos[0]+dir[0]/len*(r+WingGap(h)*static_cast<float>(wing)),0,
                             player.pos[2]+dir[2]/len*(r+WingGap(h)*static_cast<float>(wing))};
        Arrive(h,pos,spot,rest,want);
        off=Dist2(pos,spot);
        height=player.pos[1]-10.0f;   // below the ground: it descends until it touches down
    } else if(engage) {
        if(is409 && h.circleUntil && ms>=h.circleUntil) {   // circled long enough: out, and in for another run
            h.circleUntil=0;
            if(rocketsLeft)StartExtend(h,pos,aim,flight,follow,ms);
        }
        circling=is410 || (is409 && (h.circleUntil>ms || !rocketsLeft));
        if(!circling) {
            off=Engage(h,pos,aim,dist<aimRange ? dipWant : 0.0f,wallAhead,flight,follow,is409,ms,want,&height);
            circling=is409 && h.circleUntil>ms;
        }
        if(circling) {
            h.extend=false;
            const float ground=follow && player.pos[1]>aim[1] ? player.pos[1] : aim[1];
            height=ground+(is410 ? kGunshipHeight : kTurretHeight)+Stack(flight);
            off=Circle(h,pos,fwd,aim,h.tgtVel,is410 ? kGunshipRadius : kTurretRadius,is410 ? kGunshipSpeed : kTurretSpeed,want);
        }
    } else if(follow) {
        off=Formation(h,pos,fwd,flight,want,&height);
    } else {
        Arrive(h,pos,h.hold,rest,want);
        off=Dist2(pos,h.hold);height=h.hold[1];
    }
    if(!land)Separate(h,pos,want,ms);
    h.losLift=Clamp(h.losLift+(hidden || wallAhead ? kLosClimb : -kLosSink)*dt,0.0f,kLosMax);
    if(engage)height+=h.losLift;
    if(perched) {
        // The higher of it and the nearest other heli climbs off, the lower one sinks away.
        const Heli* by=nullptr;float nearest=0.0f;
        for(const auto& o:helis) {
            if(&o==&h || !o.vehicle || ms-o.seen>2000)continue;
            const float d=Dist2(pos,o.pos);
            if(!by || d<nearest){by=&o;nearest=d;}
        }
        const bool above=!by || nearest>30.0f || pos[1]>by->pos[1] || (pos[1]==by->pos[1] && &h<by);
        height=pos[1]+(above ? kUnstick : -kUnstick);
    }
    const Avoidance avoid=Avoid(pos,h.vel,want,&height,land,h.stopDecel);
    if(follow || engage)std::memcpy(h.hold,pos,12);
    // Aim only once the nose has come round: with the target behind, the dip stick flew it away.
    const float bearingOff=engage ? Wrap(std::atan2(lead[0]-pos[0],lead[2]-pos[2])-heading) : kPi;
    const bool aiming=engage && !h.extend && !circling && dist<aimRange && std::fabs(bearingOff)<kAimOff;

    // Horizontal: the stick for the wanted velocity (full stick flies h.top) plus heliBrakeGain per
    // m/s it is off, on the heading rows.
    float c[3]={want[0]/h.top+(want[0]-h.vel[0])*cfg.heliBrakeGain,0,
                want[2]/h.top+(want[2]-h.vel[2])*cfg.heliBrakeGain};
    Limit2(c,1.0f);
    float forward=Clamp(Dot2(c,fwd),-1.0f,1.0f);
    const float lateral=Clamp(Dot2(c,right),-1.0f,1.0f);
    // Aiming, the forward stick is the nose dip (kMaxTilt at full stick), and it creeps along the nose.
    if(aiming)forward=Clamp((dipWant+(dipWant-dip)*kPitchGain)/kMaxTilt,-1.0f,1.0f);

    // Vertical: the rotor (kRotor) is the lift, and it trails the throttle by seconds (spooling down
    // slower than up). So altitude -> climb rate -> wanted rotor -> a throttle that drives the rotor
    // there. h.hover is the rotor that holds height; it is learned only near the goal, where the
    // climb rate is not saturated (learning on the climb winds it up to 1 and it overshoots by 20 m).
    const float dy=height-pos[1];
    const float climb=Clamp(dy*0.25f,-3.0f,3.0f);
    const float err=climb-h.vel[1];
    if(std::fabs(dy)<6.0f)h.hover=Clamp(h.hover+err*cfg.heliHoverLearn*dt,0.1f,1.0f);
    const float wantRotor=Clamp(h.hover+err*cfg.heliClimbGain,0.0f,1.0f);
    const float rotor=At<float>(v,kRotor);
    float throttle=std::isfinite(rotor) ? Clamp(wantRotor+(wantRotor-rotor)*kRotorGain,0.0f,1.0f) : wantRotor;
    float stickF=forward,stickL=lateral;
    if(grounded)h.groundAt=ms;
    if(land && grounded){throttle=0.0f;stickF=stickL=0.0f;}
    else if(ms-h.groundAt<kLiftOffMs){stickF=stickL=0.0f;}

    // Yaw: running in, onto the target (with its bearing rate fed forward); else (circling too) the way it
    // flies (so it banks round), or the player when slow.
    float face[3]={0,0,0},faceRate=0.0f;
    if(engage && !h.extend && !circling){face[0]=lead[0]-pos[0];face[2]=lead[2]-pos[2];faceRate=losRate;}
    else if(std::sqrt(Dot2(want,want))>kFaceSpeed){face[0]=want[0];face[2]=want[2];}
    else if(follow){face[0]=player.pos[0]-pos[0];face[2]=player.pos[2]-pos[2];}
    float yaw=0.0f,offYaw=kPi;
    if(Dot2(face,face)>1.0f) {
        offYaw=Wrap(std::atan2(face[0],face[2])-heading);
        yaw=Clamp(offYaw*1.5f-(h.yawRate-faceRate)*kYawDamp+faceRate*kYawFeed,-1.0f,1.0f)*static_cast<float>(h.yawSign);
    }
    h.lastYaw=yaw;

    Put<float>(v,kInLateral,stickL);Put<float>(v,kInForward,stickF);Put<float>(v,kInThrottle,throttle);
    Put<float>(v,kInW,1.0f);Put<float>(v,kInYaw,yaw);

    // Fire: the nose within the cone of the gun's lead point (heliFireCone, wider up close so kHitRadius
    // at the target still counts), that within the guns' reach, and not through the player; the 409's
    // turret gun also at anything below within kTurretReach. The missile homes, so it goes with a rough
    // aim and farther; the 409's rockets fly straight, so they want a tight one. Guns fire in bursts.
    const auto missOf=[&](const float* p,float* along) noexcept {
        const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
        *along=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(*along<1.0f)return 0.0f;
        return std::acos(Clamp((d[0]*nose[0]+d[1]*nose[1]+d[2]*nose[2])/ *along,-1.0f,1.0f))*180.0f/kPi;
    };
    const auto coneAt=[](float base,float d) noexcept {
        const float wide=d>1.0f ? std::atan(kHitRadius/d)*180.0f/kPi : 90.0f;
        return wide>base ? wide : base;
    };
    bool gun=false,missile=false;float miss=180.0f,gunDist=0.0f,cone=cfg.heliFireCone;
    if(engage) {
        miss=missOf(gunLead,&gunDist);
        cone=coneAt(cfg.heliFireCone,gunDist);
    }
    if(engage && cfg.heliFire && !grounded && !land && !hidden && !PlayerInLine(pos,lead)) {
        const bool turret=is409 && gunDist<kTurretReach && aim[1]<pos[1];
        gun=(miss<cone && gunDist<range) || turret;
        if(gun && !h.firing){h.firing=true;h.burstAt=ms;}
        if(h.firing && ms-h.burstAt>kBurstMs){h.firing=false;h.restUntil=ms+kBurstRest;}
        if(!gun)h.firing=false;
        if(ms<h.restUntil)gun=false;
        if(is409 && arms.rockets) {
            float rocketDist=0.0f;
            const float rocketMiss=missOf(lead,&rocketDist);
            missile=rocketMiss<coneAt(kRocketCone,rocketDist) && rocketDist<kRocketRange;
        } else {
            missile=cfg.heliMissile && miss<kMissileCone && dist>kMissileMin && dist<cfg.heliRange && ms-h.missileAt>cfg.heliMissileMs;
            if(missile)h.missileAt=ms;
        }
    } else h.firing=false;
    if(!is410){v[kFireGun]=gun;v[kFireMissile]=missile;}   // the 410 fires through its door gun blocks
    else if(doorOk && cfg.heliDoorGuns && SeatCount(v)>=3)
        for(int i=0;i<2;++i)DoorGun(h,v,i,grounded || land,dt,ms);

    if(cfg.debug && ms-h.loggedAt>1000) {
        h.loggedAt=ms;
        const float speed=std::sqrt(Dot2(h.vel,h.vel)),aimedLead=Dist2(aim,lead);
        Log("HELI v=%p %s%s flight=%d.%d ammo=%d/%d/%d y=%.1f goal=%.1f vy=%.2f thr=%.3f hover=%.3f rotor=%.3f fwd=%.2f lat=%.2f yaw=%.2f rate=%.0fdeg/s sign=%d%s votes=%d dGoal=%.0f ground=%d target=%p dist=%.0f off=%.0fdeg miss=%.1fdeg pitch=%.0fdeg gun=%d msl=%d spd=%.1f want=%.1f dipWant=%.0fdeg cone=%.1fdeg lead=%.1f tv=%.1f los=%.0fdeg/s ahead=%.0f clear=%.0f roof=%.0f any=%.0f/%X lift=%.0f%s%s",
            v,perched ? "perched " : "",land ? "land" : engage ? (circling ? "circle" : h.extend ? "extend" : aiming ? "aim" : "run") : follow ? (roaming ? "escort" : "orbit") : "hold",
            flight.group,wing,arms.ammo[0],arms.ammo[1],arms.ammo[2],pos[1],height,h.vel[1],throttle,h.hover,rotor,
            stickF,stickL,yaw,h.yawRate*180.0f/kPi,h.yawSign,h.yawLocked ? "(locked)" : "",h.votes,off,grounded,
            engage ? h.target : nullptr,dist,offYaw*180.0f/kPi,miss,-dip*180.0f/kPi,gun,missile,
            speed,std::sqrt(Dot2(want,want)),dipWant*180.0f/kPi,cone,aimedLead,std::sqrt(Dot2(h.tgtVel,h.tgtVel)),losRate*180.0f/kPi,avoid.ahead,avoid.clear,avoid.roof,avoid.anyRoof,avoid.anyFlags,h.losLift,hidden ? " hidden" : "",wallAhead ? " wall" : "");
    }
}
}  // namespace

bool GunBarrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept {
    __try { return Barrel(v,weapon,pos,dir); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsHelicopter(const void* vehicle) noexcept {
    if(!Readable(vehicle,8))return false;
    const auto vtable=At<const unsigned char*>(vehicle,0);
    for(auto rva:kHeliVtables)if(vtable==image+rva)return true;
    return false;
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
    const float frames=cfg.heliAgility*60.0f,stockTop=b*k/denom;
    if(cfg.heliSpeed>stockTop && frames>=30.0f) {
        const float blend=1.0f-(1.0f-1.0f/frames)/d;   // 1-d*(1-blend) = 1/frames
        if(blend>0.0f && blend<1.0f) {
            h.params[1]=blend;h.params[0]=cfg.heliSpeed/(frames*blend);
            h.top=cfg.heliSpeed;h.stopDecel=kStopShare*h.top/cfg.heliAgility;h.tuned=true;
        }
    }
    const float yawWant=cfg.heliYawRate*kPi/180.0f;
    if(yawWant>yaw){h.params[2]=yawWant;h.params[3]=smooth<kTunedYawSmooth ? kTunedYawSmooth : smooth;h.tuned=true;}
    Log("HELI v=%p tune: stock k=%.2f b=%.5f d=%.4f top=%.1fm/s tau=%.1fs yaw=%.0fdeg/s smooth=%.4f -> k=%.2f b=%.5f top=%.1fm/s brake=%.2fm/s2 yaw=%.0fdeg/s smooth=%.4f%s",
        v,k,b,d,stockTop,1.0f/denom/60.0f,yaw*180.0f/kPi,smooth,h.params[0],h.params[1],h.top,h.stopDecel,h.params[2]*180.0f/kPi,h.params[3],h.tuned ? "" : " (stock)");
}
}  // namespace

void HeliCrewed(const void* vehicle) noexcept {
    if(IsJet(vehicle))return;   // flown by jet.cpp
    Heli* slot=Find(vehicle);
    if(!slot){slot=&helis[0];for(auto& h:helis)if(h.seen<slot->seen)slot=&h;}
    *slot=Heli{};slot->vehicle=vehicle;slot->kind=At<const void*>(vehicle,0);slot->crewedAt=slot->seen=GetTickCount64();
    const auto c=static_cast<const unsigned char*>(vehicle);
    Log("HELI v=%p crewed: the plugin flies it; maxTilt=%.3f",vehicle,At<float>(c,0x1640));
    Tune(*slot,c);
}

void HeliFrame(unsigned char* vehicle) noexcept {
    if(!profileOk || vehicle[kDead])return;
    if(SeatCount(vehicle)==0 || SeatRider(SeatAt(vehicle,0))!=Rider::dummy)return;   // only NPC pilots
    if(IsJet(vehicle)){if(cfg.jetPilot)JetFrame(vehicle);return;}
    if(!cfg.heliPilot)return;
    Heli* h=Find(vehicle);
    if(!h){HeliCrewed(vehicle);h=Find(vehicle);}   // a mission-spawned NPC heli (CreateFriend): fly it too
    h->seen=GetTickCount64();
    bool playerAboard=false;
    for(unsigned i=1;i<SeatCount(vehicle);++i)playerAboard=playerAboard || SeatRider(SeatAt(vehicle,i))==Rider::player;
    Fly(*h,vehicle,playerAboard);
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
    if(user || !doorOk || !cfg.heliPilot || !cfg.heliDoorGuns)return user;
    const auto v=static_cast<unsigned char*>(iface)-kUserIface;
    if(At<const unsigned char*>(v,0)!=image+kHeli410 || SeatCount(v)==0)return user;
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

// Checks the 410 layout and chains the weapon-user hook (onto another plugin's, if one is there).
void InstallDoorGuns() noexcept {
    bool ok=true;
    for(const auto& s:kDoorSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI door guns: mismatch at %#zx",s.rva);ok=false;}
    if(!ok)return;
    const auto slot=reinterpret_cast<void**>(image+kUserIface410)+kUserSlot;
    void* const current=*slot;
    if(!current)return;
    if(current!=image+kWeaponUserFn)Log("HELI door guns: weapon user chained onto %p (another plugin)",current);
    nextUser=reinterpret_cast<UserFn>(current);
    doorOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&DoorGunUser));
}
}  // namespace

float MapRay(const float* a,const float* b,float* hit) noexcept { return CastRay(a,b,hit); }

bool VisitEnemies(const unsigned char* vehicle,EnemyVisitor visit,void* ctx) noexcept {
    return ForEachEnemy(vehicle,[&](const void* object,const float* aim) noexcept { visit(ctx,object,aim); });
}

bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept {
    return ForEachEnemyOf(team,nullptr,[&](const void* object,const float* aim) noexcept { visit(ctx,object,aim); });
}

bool BurstHitsPlayer(const float* from,const float* to) noexcept { return PlayerInLine(from,to); }

bool CheckHeliProfile() noexcept {
    __try {
        for(const auto& s:kHeliSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("HELI profile mismatch at %#zx",s.rva);return false;}
        profileOk=true;
        InstallDoorGuns();
        Log("HELI door guns=%d (410 door guns aimed by the plugin)",doorOk);
        // Avoidance has its own check: without it the helis still fly, just blind.
        rayOk=Readable(image+kHitVtbl,0x28) && At<const unsigned char*>(image,kHitVtbl)==image+kHitSlot0 &&
              At<const unsigned char*>(image,kHitVtbl+0x20)==image+kHitAdd &&
              Readable(image+kGroundVtbl,0x28) && At<const unsigned char*>(image,kGroundVtbl)==image+kHitSlot0 &&
              At<const unsigned char*>(image,kGroundVtbl+0x10)==image+kHitReset &&
              At<const unsigned char*>(image,kGroundVtbl+0x20)==image+kGroundAdd;
        for(const auto& s:kRaySignatures)rayOk=rayOk && Matches(s.rva,s.bytes,s.size);
        Log("HELI ray=%d (obstacle avoidance %s)",rayOk,rayOk ? "on" : "off: unexpected EDF.dll code");
        InstallJets();
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
