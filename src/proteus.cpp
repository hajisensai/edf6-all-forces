// The Proteus rework (README 普罗透斯, docs/proteus-re.md; ini Proteus*; the user, 2026-10-06: "重构普罗透斯：行走时是快速
// 转移的指挥平台，架设后变成能保护周围友军的火力据点。座位：两。"). The Proteus is the stock VehicleBigBegaruta (vtable 0x17DEC40:
// V614_PROTEUS_MK2*, VEHICLE407_BIGBEGARUTA*); while a local player rides one it is reworked, and gets its stock numbers
// back the moment nobody of ours is aboard (the plugin off too). The rules are proteus_logic.h's (checked offline by
// tools/proteus_check.cpp); this file feeds them the driver's keys and puts their decisions into the game:
//  - Two seats (ProteusTwoSeats): the driver (seat 0) and the gunner (seat 1, its left cannon). The right cannon's seat
//    (2) and the missile launcher's (3) are closed (their class mask seat+0x30 = 0: CanRideSeat 0x6346FC refuses
//    everyone; the stock RideAi's dummy gunners in them are sent off) and the right cannon serves the gunner: its aim
//    copies the left one's angles (clamped to its own stops) and it is pulled whenever the left one is (weapon +0x139 /
//    +0x13A, the holder pull 0x62C000), its operator the left one's (the weapon user hook: the fire step 0x690C0E
//    refuses a weapon nobody operates). The launcher's rounds are the driver's salvo instead (below).
//  - The legs (vehicle +0x1978 walk speed, +0x197C its ease, +0x1980 turn rate, all set once from the SGO's setup [1]
//    by 0x647EA0 and read each frame by 0x645A20): walking faster (ProteusWalkSpeed / ProteusWalkTurn), slowed by the
//    front shield (ProteusShieldSlow); in a stagger none, and the move (+0x1950) and turn (+0x1974) zeroed so it stops
//    at once; deployed it only turns on the spot (ProteusDeployTurn). The jump (setup [2]'s speed, controller +0x290)
//    is 0 out of the walk: planted. The step height: the controller's walkable test (+0xD4, the least ground normal
//    height a contact may have: sinf of the SGO's begaruta_rigid_body[3], 0x11B9D67, read by the contact callback
//    0x11B95EB) set for ProteusStepHeight m on the 5 m foot capsule (proteus::StepNormal), walking only.
//  - The guns (weapon +0xE10 the countdown's rate, +0xE14 the accuracy cone's scale: both 1.0 from the constructor,
//    read every frame / every shot, written by nothing else for this class): walking loose and slow, deployed tight and
//    quick. The stock launcher is held (+0xE10 0, its countdown parked at kProteusHoldCountdown) in either stance while the
//    salvo takes its place (the shells preloaded, its seat closed), and given back the frame either is not so (Guns).
//  - Deployed, the driver's remote gun (ProteusDriverGun): only with both gunner seats empty, the right cannon
//    follows the driver's native aim and fires along its physical muzzle; a marked target near the bore is led. The salvo (the second
//    trigger, a target marked, the cooldown over): ProteusSalvoCount rounds of the gunship's shells on their arcs at the
//    marked target, led.
//  - The shields and the barrier (proteus_logic.h Absorb) on every hit through the damage call (0x54A586 -> 0x547C30:
//    vehicles and soldiers alike, GameDamageInfo +0x50 the damage, +0x30 where it hit). The front shield / directional
//    shield faces the hull's nose while walking, and the driver's horizontal view while deployed. Its model and damage test share that direction.
//  - Deployed, the field (ProteusFieldRadius): every soldier and vehicle of its side within the radius, each frame,
//    through the team manager's walk of the side's friends (0x5E11D0): damage taken x (1 - ProteusFieldDefense) (object
//    +0x384, the per-update multiplier 0x54BE40 folds into +0x394 and resets), a soldier's damage dealt x (1 +
//    ProteusFieldAttack) (+0x388 -> +0x398 -> its weapons' +0x788), weapons' shot countdown (+0xE0C) run ProteusFieldFireRate
//    times as fast, a Wing Diver's energy (+0x308 of +0x304) refilled, an Air Raider's call weapons (ReloadType 2) charged
//    ProteusFieldPower credits a second on top of their side's points.
//  - The front shield up, the allies look to it first: a friendly soldier's target search (SearchAttackTarget slot 1,
//    nearest by distance) weighs enemies near the Proteus, or after it, ProteusPriority of their distance; EDF6AutoTurret
//    asks the same zone (common/edf/aimlink.h PriorityZoneV1).
//  - Deployed, the camera's targets raised (ProteusViewLift; turretcam.cpp's look-at fetch hands them over).
// What the engine does not let it do is said in docs/proteus-re.md §8 (the body's height, the field on another machine).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "vecmath.h"
#include "body506.h"
#include "proteus_pose.h"
#include "seat_aim.h"
#include "edf/aimlink.h"
#include "edf/weapon.h"
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kVtBig=0x17DEC40;
// The legs (vehicle; docs/proteus-re.md §2).
constexpr std::size_t kMove=0x1950,kTurnNow=0x1974,kWalk=0x1978,kWalkEase=0x197C,kTurn=0x1980;
constexpr std::size_t kCtrl=0x1720,kJump=kCtrl+0x290,kStepNormal=kCtrl+0xD4;
constexpr float kFootRadius=5.0f;   // m: begaruta_rigid_body[1] of every VehicleBigBegaruta SGO (the capsule's, 0x63FE86)
// The seats (docs/proteus-re.md §4): 0 the driver (no weapon), 1 / 2 the left / right cannon, 3 the missile launcher.
constexpr unsigned kGunnerSeat=1,kRightSeat=2,kLauncherSeat=3,kProteusSeats=4;
constexpr std::size_t kSeatClassMask=0x30,kSeatPad=0x2B0,kSeatButtons=0x2E8,kSeatFire=0x2E4,kSeatFire2=0x2E0;
constexpr float kTriggerOn=0.8f;
// A weapon (docs/proteus-re.md §5): the trigger latch and its held copy, the shot countdown, its rate, the cone's scale.
constexpr std::size_t kPull=0x139,kHeld=0x13A,kCountdown=0xE0C,kRate=0xE10,kSpread=0xE14;
constexpr unsigned kPullFn=0x62C000;
constexpr unsigned kAimVt=0x17D8A68,kAimSeVt=0x17D8A90,kAxisApply=0x5FC280;
constexpr float kHoldCountdown=kProteusHoldCountdown;   // frames: the stock launcher held (proteus.h: vehsound.cpp reads it)
// The weapon user (heli.cpp DoorGunUser's): the vehicle's interface at +0x120, its slot 11 (0x62D950).
constexpr unsigned kUserSlotRva=0x17DEE68,kUserFn=0x62D950;
constexpr std::size_t kUserIface=0x120;
// The damage call (docs/subcarrier-re.md §8.1): 0x54A586 call 0x547C30(object, GameDamageInfo).
constexpr unsigned kDamageCall=0x54A586,kDamageFn=0x547C30;
constexpr std::size_t kDmgAmount=0x50,kDmgAt=0x30;
// The soldiers (sidecar.cpp kSoldierVts): AssultSoldier, PaleWing (the Wing Diver), HeavyArmor, Engineer (the Air Raider).
constexpr unsigned kVtRanger=0x17CDF28,kVtWingDiver=0x17D0FF8,kVtFencer=0x17CF5B8,kVtAirRaider=0x17CF100;
constexpr std::size_t kHumanVehicle=0x1548,kHumanWeapons=0x1950,kHumanWeaponCount=0x1960;
constexpr std::size_t kTakenMul=0x384,kDealtMul=0x388,kEnergyMax=0x304,kEnergy=0x308;
constexpr std::size_t kReloadType=0x208,kReloadLeft=0xE68;
constexpr std::int32_t kReloadByPoints=2;
// The team manager's walk of a side's friends (sidecar.cpp): functor slot 1 per object.
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978;
// SearchAttackTarget (vtable 0x17D24C0), slot 1 (0x598C50): functor +8 the soldier searching, +0x10 the least distance
// so far, +0x18 the enemy that has it; an enemy's target (weak object) at +0x518, compared only.
constexpr unsigned kSearchSlotRva=0x17D24C8,kSearchFn=0x598C50;
constexpr std::size_t kSearcher=0x8,kSearchBest=0x10,kSearchFound=0x18,kEnemyTarget=0x518;
constexpr float kDurability=7500.0f;    // every VehicleBigBegaruta SGO's game_object_durability (the tier's base)
constexpr ULONGLONG kStaleMs=1500,kLogMs=2000,kRingMs=500,kSalvoGapMs=90,kReadoutMs=250;
constexpr float kMarkCone=0.14f;        // rad (8 deg) off the screen's centre a mark is taken within
constexpr float kLeadCone=0.10f;        // rad: the marked target this near the centre is led by the driver's gun
constexpr float kGunReach=2000.0f,kRoundSpeed=960.0f,kShellSpeed=480.0f;   // m; m/s (make_jets.py, the gunship shell)
constexpr float kSalvoSpread=6.0f;      // m round the led point the salvo's rounds fall
constexpr float kPi=3.14159265f;
constexpr int kMaxUnits=4;

struct Sig { unsigned rva; unsigned char bytes[20]; std::size_t size; };
const Sig kSigs[]={
    {0x645A2E,{0xF3,0x0F,0x10,0xA3,0x78,0x19,0x00,0x00},8},          // the move: walk speed +0x1978
    {0x645A94,{0x0F,0x11,0xA3,0x50,0x19,0x00,0x00},7},               // ...the move vector +0x1950
    {0x645AA3,{0xF3,0x0F,0x10,0x8B,0x80,0x19,0x00,0x00},8},          // ...the turn rate +0x1980
    {0x645AC3,{0xF3,0x0F,0x11,0x8B,0x74,0x19,0x00,0x00},8},          // ...the turn +0x1974
    {0x63937A,{0x48,0x8D,0xBB,0x20,0x17,0x00,0x00},7},               // the jump: the controller at +0x1720
    {0x6393C8,{0xF3,0x0F,0x10,0x87,0x90,0x02,0x00,0x00},8},          // ...its speed +0x290
    {0x11B9D67,{0xF3,0x0F,0x11,0x86,0xD4,0x00,0x00,0x00},8},         // the walkable test +0xD4 written
    {0x11B95EB,{0xF3,0x0F,0x10,0x87,0xD4,0x00,0x00,0x00},8},         // ...read per contact
    {0x6346FC,{0x8B,0x82,0x1C,0x03,0x00,0x00,0x41,0x23,0x40,0x34,0x41,0x85,0x40,0x30},14},   // the seat's class mask
    {0x693A58,{0x4C,0x8D,0xBE,0x0C,0x0E,0x00,0x00,0xF3,0x41,0x0F,0x10,0x07,0xF3,0x0F,0x5C,0x86,0x10,0x0E,0x00,0x00},20},  // countdown -= rate
    {0x691AFA,{0xF3,0x0F,0x10,0x9F,0x14,0x0E,0x00,0x00,0xF3,0x0F,0x59,0x9F,0x78,0x03,0x00,0x00},16},   // cone = scale x accuracy
    {kPullFn,{0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x11,0x83,0x78,0x08,0x00,0x74,0x0B,0x48},16},
};
const Sig kUserSig={kUserFn,{0x41,0x57,0x48,0x83,0xEC,0x30,0x4C,0x69,0x99,0xF8,0x04,0x00,0x00,0x40,0x03,0x00},16};
const Sig kAimSigs[]={
    {0x6459D0,{0x48,0x8B,0x8B,0x08,0x06,0x00,0x00,0x48,0x81,0xC1,0xE0,0x00,0x00,0x00},14},
    {0x6459E9,{0x48,0x03,0xCE,0x48,0x8B,0x01,0xFF,0x50,0x10},9},
    {kAxisApply,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18},15},
};
const Sig kDamageSigs[]={
    {0x54A579,{0x0F,0xB6,0x9F,0xE8,0x02,0x00,0x00,0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0xA5,0xD6,0xFF,0xFF},18},
    {0x547C70,{0x4C,0x8B,0xEA},3},                                    // the GameDamageInfo kept
    {0x548109,{0xF3,0x41,0x0F,0x10,0x75,0x50},6},                     // ...its damage read
};
const Sig kFieldSigs[]={
    {0x5E11D0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},   // the team walk
    {0x54BED5,{0xF3,0x0F,0x59,0x8F,0x84,0x03,0x00,0x00,0xF3,0x0F,0x11,0x8F,0x94,0x03,0x00,0x00},16},   // taken x +0x384
    {0x54BEE5,{0xF3,0x0F,0x59,0x97,0x88,0x03,0x00,0x00,0xF3,0x0F,0x11,0x97,0x98,0x03,0x00,0x00},16},   // dealt x +0x388
    {0x59B53E,{0x48,0x8B,0x93,0x50,0x19,0x00,0x00,0x48,0x8B,0x83,0x60,0x19,0x00,0x00},14},             // a soldier's weapons
    {0x580D6B,{0x83,0xBF,0xFC,0x02,0x00,0x00,0x01,0xF3,0x0F,0x10,0x87,0x08,0x03,0x00,0x00},15},        // the Wing Diver's energy
    {0x693E8D,{0x8B,0x86,0x08,0x02,0x00,0x00,0x85,0xC0},8},                                            // ReloadType
    {0x693F18,{0x29,0x86,0x68,0x0E,0x00,0x00,0xF3,0x0F,0x11,0xB6,0x80,0x0E,0x00,0x00},14},             // points off the reload
};
const Sig kSearchSigs[]={
    {kSearchFn,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0xFF,0x41,0x20,0x48,0x8B,0xFA},16},
    {0x598C60,{0x80,0xBA,0xA0,0x04,0x00,0x00,0x00,0x48,0x8B,0xD9,0x0F,0x84,0x8C,0x00,0x00,0x00},16},
    {0x598CE9,{0xF3,0x0F,0x10,0x4B,0x10,0x0F,0x2F,0xC8,0x76,0x09,0xF3,0x0F,0x11,0x43,0x10,0x48,0x89,0x7B,0x18},19},
};

bool ok=false,damageOk=false,fieldOk=false,searchOk=false,userOk=false;
using DamageFn=void(__fastcall*)(void*,void*);
using UserFn=const void*(__fastcall*)(void*,const void*);
using SearchFn=void(__fastcall*)(void*,void*);
using PullFn=void(__fastcall*)(void*);
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);
using SeatFn=void(__fastcall*)(void*,void*);
using AimFn=void(__fastcall*)(void*,const float*);
using AxisApplyFn=void(__fastcall*)(void*,bool);
void* nextAim[2]{};   // ChainVtableSlot fills the continuation before publishing our hook
UserFn nextUser=nullptr;
SearchFn nextSearch=nullptr;
const unsigned char* damageThunk=nullptr;   // where the redirected damage call goes now (the near thunk to DamageHook)

// One Proteus a local player rides (a slot not seen for kStaleMs is free).
struct Unit {
    ObjRef ref;
    ULONGLONG seen,frame,lastMs,logAt,ringAt,gunAt,salvoAt;
    bool active;                       // reworked this frame (its stock numbers kept below)
    proteus::State st;
    // The stock numbers, taken when it was first reworked (given back when it is not).
    float walk,walkEase,turn,jump,stepNormal;
    std::int32_t seatMask[kProteusSeats];
    bool closed;                       // seats 2 and 3 closed
    unsigned char* weapon[kProteusSeats];   // the seats' first weapons (seat 0: none)
    unsigned char* holder[kProteusSeats];
    float rate[kProteusSeats],spread[kProteusSeats];   // their countdown rate and cone scale as taken (given back)
    bool launcherHeld;                 // the stock launcher parked: the salvo is its (Guns)
    bool held[4];                      // the driver's mode, shield, mark, salvo buttons last frame
    // The mark: the enemy, its lock point and velocity (m/s, smoothed).
    const void* mark;
    float markAt[3],markVel[3];
    ULONGLONG markSeen;
    int salvoLeft;
    float lift;                        // the camera's raise now (eases to ProteusViewLift deployed)
    float credits;                     // the Air Raiders' points owed (whole ones handed over)
    int allies;
    int ringCount;
    float ring[kProteusRing][3];
    unsigned playerSeat;
    // The damage it took this second (Debug): blocked by a shield, taken by the barrier, through to the hull.
    float blocked,barred,through;
    float shieldNose[3];             // the same direction for the visible shield and the damage test
};
Unit units[kMaxUnits]{};

SRWLOCK zoneLock=SRWLOCK_INIT;
struct Zone { bool on; ProteusZone z; std::int32_t team; const void* human; ULONGLONG at; } zone{};
SRWLOCK readoutLock=SRWLOCK_INIT;
struct Out { ProteusReadout r; ULONGLONG at; } out{};

bool Big(const void* v) noexcept { return At<const unsigned char*>(v,0)==image+kVtBig; }
float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
const float* Pos(const void* o) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kPosition); }

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

proteus::Tunables TunablesOf(const Config& c) noexcept {
    proteus::Tunables k{};
    k.deploySec=c.proteusDeploySec;k.stowSec=c.proteusStowSec;
    k.heatSec=c.proteusHeatSec;k.coolSec=c.proteusCoolSec;k.resume=c.proteusResumeHeat;
    k.shieldBlock=c.proteusShieldBlock;k.shieldHalfArc=c.proteusShieldArc*0.5f*kPi/180.0f;
    k.barrierRegenSec=c.proteusBarrierRegenSec;k.barrierDelaySec=c.proteusBarrierDelaySec;
    k.salvoCooldownSec=c.proteusSalvoCooldownSec;
    return k;
}

Unit* UnitOf(const void* v,bool make) noexcept {
    const ULONGLONG ms=GameMs();
    Unit* free=nullptr;
    for(auto& u:units) {
        if(u.ref.Is(v))return &u;
        if(!free && (!u.ref || ms-u.seen>kStaleMs))free=&u;
    }
    if(!make || !free)return nullptr;
    *free=Unit{};
    free->ref=ObjRef::Of(v);
    return free;
}

// The active unit of the vehicle (game thread: the hooks the game calls on it).
Unit* ActiveOf(const void* v) noexcept {
    for(auto& u:units)if(u.active && u.ref.Is(v))return &u;   // the same object, not a new one at its address
    return nullptr;
}

#include "proteus_visual.inc"

// Seat `s`'s first weapon and its holder (nullptr: none).
unsigned char* SeatWeapon(unsigned char* v,unsigned s,unsigned char** holder) noexcept {
    *holder=nullptr;
    if(s>=SeatCount(v))return nullptr;
    unsigned char* const seat=SeatAt(v,s);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!n || n>8 || !Readable(holders,8) || !Readable(holders[0],kHolderWeapon+8))return nullptr;
    unsigned char* const w=At<unsigned char*>(holders[0],kHolderWeapon);
    if(!Readable(w,kSpread+4,true))return nullptr;
    *holder=holders[0];
    return w;
}

bool HolderAlive(const unsigned char* holder) noexcept {
    const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
    return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0;
}

// --- giving back ---
void GiveBack(Unit& u,unsigned char* v,const char* why) noexcept {
    if(!u.active)return;
    u.active=false;
    Put<float>(v,kWalk,u.walk);Put<float>(v,kWalkEase,u.walkEase);Put<float>(v,kTurn,u.turn);
    Put<float>(v,kJump,u.jump);Put<float>(v,kStepNormal,u.stepNormal);
    if(u.closed) {
        for(unsigned s=kRightSeat;s<kProteusSeats && s<SeatCount(v);++s)Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,u.seatMask[s]);
        u.closed=false;
    }
    for(unsigned s=1;s<kProteusSeats;++s) {
        unsigned char* const w=u.weapon[s];
        if(!w || !Readable(w,kSpread+4,true))continue;
        Put<float>(w,kRate,u.rate[s]);Put<float>(w,kSpread,u.spread[s]);   // the numbers it had when taken
        if(s==kLauncherSeat && At<float>(w,kCountdown)>=kHoldCountdown*0.5f)Put<float>(w,kCountdown,0.0f);
    }
    u.launcherHeld=false;
    u.st=proteus::State{};u.lift=0.0f;u.salvoLeft=0;u.mark=nullptr;
    Log("PROTEUS v=%p: stock again (%s)",v,why);
}

// --- the stance's numbers on the legs and the guns ---
void Legs(Unit& u,unsigned char* v,const Config& c) noexcept {
    const proteus::State& s=u.st;
    const float leg=proteus::LegShare(s,c.proteusShieldSlow),turn=proteus::TurnShare(s,c.proteusShieldSlow,c.proteusDeployTurn);
    const bool walking=s.mode==proteus::Mode::walk;
    Put<float>(v,kWalk,u.walk*leg*(walking ? c.proteusWalkSpeed : 1.0f));
    Put<float>(v,kWalkEase,u.walkEase*(walking ? c.proteusWalkSpeed : 1.0f));
    Put<float>(v,kTurn,u.turn*turn*(walking ? c.proteusWalkTurn : 1.0f));
    if(leg<=0.0f){float* m=reinterpret_cast<float*>(v+kMove);m[0]=m[1]=m[2]=0.0f;}   // stopped at once, not eased down
    if(turn<=0.0f)Put<float>(v,kTurnNow,0.0f);
    Put<float>(v,kJump,walking ? u.jump : 0.0f);
    // The walkable test: never stricter than the stock one.
    const float want=proteus::StepNormal(c.proteusStepHeight,kFootRadius);
    Put<float>(v,kStepNormal,walking && want<u.stepNormal ? want : u.stepNormal);
}

// The cannons get the stance's numbers. The stock launcher is the salvo's only while the salvo can take its place: the
// gunship's shells preloaded this mission (ProteusRoundsReady) and its seat closed (TwoSeats: nobody else fires it);
// otherwise it is the launcher it always was. Decided every frame (`salvo`: the shells are there).
void Guns(Unit& u,bool salvo,const Config& c) noexcept {
    const bool deployed=u.st.mode==proteus::Mode::deployed;
    const float rate=deployed ? c.proteusDeployGunRate : c.proteusWalkGunRate,spread=deployed ? c.proteusDeployGunSpread : c.proteusWalkGunSpread;
    for(unsigned s=kGunnerSeat;s<=kRightSeat;++s) {
        unsigned char* const w=u.weapon[s];
        if(!w)continue;
        Put<float>(w,kRate,rate);Put<float>(w,kSpread,spread);
    }
    unsigned char* const m=u.weapon[kLauncherSeat];
    if(!m)return;
    const bool hold=salvo && u.closed;
    if(hold) {
        Put<float>(m,kRate,0.0f);
        if(At<float>(m,kCountdown)<kHoldCountdown*0.5f)Put<float>(m,kCountdown,kHoldCountdown);
    } else if(u.launcherHeld) {   // given back: its own rate, its countdown run out (ready, as before it was held)
        Put<float>(m,kRate,u.rate[kLauncherSeat]);
        if(At<float>(m,kCountdown)>=kHoldCountdown*0.5f)Put<float>(m,kCountdown,0.0f);
    }
    u.launcherHeld=hold;
}

// Seats 2 and 3 closed, their dummy gunners sent off; the right cannon follows the left one.
void TwoSeats(Unit& u,unsigned char* v) noexcept {
    if(SeatCount(v)<kProteusSeats)return;
    if(!u.closed) {
        for(unsigned s=kRightSeat;s<kProteusSeats;++s) {
            u.seatMask[s]=At<std::int32_t>(SeatAt(v,s),kSeatClassMask);
            Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,0);
        }
        u.closed=true;
        Log("PROTEUS v=%p: two seats (seats 2, 3 closed; masks were %#x %#x)",v,u.seatMask[kRightSeat],u.seatMask[kLauncherSeat]);
    }
    for(unsigned s=kRightSeat;s<kProteusSeats;++s)
        if(SeatRider(SeatAt(v,s))==Rider::dummy) {
            reinterpret_cast<SeatFn>(image+kSeatKick)(v,SeatAt(v,s));
            Log("PROTEUS v=%p: the NPC gunner of seat %u sent off (two seats)",v,s);
        }
    // Aim follows in AimHook after the left seat's step, before this frame's fire.
    // ...and its trigger: pulled whenever the left one is.
    unsigned char* const lw=u.weapon[kGunnerSeat];
    if(lw && u.holder[kRightSeat] && (lw[kPull] || lw[kHeld]) && HolderAlive(u.holder[kRightSeat]))
        reinterpret_cast<PullFn>(image+kPullFn)(u.holder[kRightSeat]);
}

// --- the driver's keys ---
// The button `b` (0..3: mode, shield, mark, salvo) pressed this frame.
bool Pressed(Unit& u,const unsigned char* seat,int b,const Config& c) noexcept {
    const bool pad=At<unsigned char>(seat,kSeatPad)!=0;
    bool down=false;
    if(pad) {
        const std::uint16_t bits=At<std::uint16_t>(seat,kSeatButtons);
        const int button=b==0 ? c.proteusModeButton : b==1 ? c.proteusShieldButton : b==2 ? c.proteusMarkButton : 0;
        down=b==3 ? At<float>(seat,kSeatFire2)>=kTriggerOn : button>0 && (bits&static_cast<std::uint16_t>(button))!=0;
    } else {
        const int key=b==0 ? c.proteusModeKey : b==1 ? c.proteusShieldKey : b==2 ? c.proteusMarkKey : c.proteusSalvoKey;
        down=KeyHeld(key);
    }
    const bool press=down && !u.held[b];
    u.held[b]=down;
    return press;
}

// --- the mark ---
struct MarkPick { const unsigned char* v; float eye[3],dir[3],reach; float best; const void* found; float at[3]; };
void PickVisit(void* ctx,const void* object,const float* aim) noexcept {
    auto& p=*static_cast<MarkPick*>(ctx);
    const float d[3]={aim[0]-p.eye[0],aim[1]-p.eye[1],aim[2]-p.eye[2]};
    const float l=std::sqrt(Dot3(d,d));
    if(!(l>1.0f) || vec::Dist(aim,Pos(p.v))>p.reach)return;
    const float off=std::acos(vec::Clamp(Dot3(d,p.dir)/l,-1.0f,1.0f));
    if(off>kMarkCone || off>=p.best)return;
    p.best=off;p.found=object;std::memcpy(p.at,aim,12);
}
struct MarkTrack { const void* mark; bool seen; float at[3]; };
void TrackVisit(void* ctx,const void* object,const float* aim) noexcept {
    auto& t=*static_cast<MarkTrack*>(ctx);
    if(t.seen || object!=t.mark)return;
    t.seen=true;std::memcpy(t.at,aim,12);
}

void Mark(Unit& u,unsigned char* v,bool press,float dt,const Config& c) noexcept {
    if(press) {
        MarkPick p{v,{},{},c.proteusSalvoRange>kGunReach ? c.proteusSalvoRange : kGunReach,kMarkCone,nullptr,{}};
        if(CameraRay(p.eye,p.dir))VisitEnemies(v,&PickVisit,&p);
        const void* was=u.mark;
        u.mark=p.found && p.found!=was ? p.found : nullptr;   // the same one again: the mark let go
        if(u.mark){std::memcpy(u.markAt,p.at,12);std::memset(u.markVel,0,12);u.markSeen=GameMs();}
        Log("PROTEUS v=%p: %s",v,u.mark ? "target marked" : was ? "mark let go" : "nothing near the centre to mark");
        return;
    }
    if(!u.mark)return;
    MarkTrack t{u.mark,false,{}};
    VisitEnemies(v,&TrackVisit,&t);
    if(!t.seen){u.mark=nullptr;Log("PROTEUS v=%p: the marked target is gone",v);return;}
    if(dt>0.0f) {
        for(int i=0;i<3;++i) {
            const float vel=(t.at[i]-u.markAt[i])/dt;
            u.markVel[i]+=(vel-u.markVel[i])*vec::Clamp(dt/0.3f,0.0f,1.0f);
        }
    }
    if(Dot3(u.markVel,u.markVel)>200.0f*200.0f)std::memset(u.markVel,0,12);   // a teleport or a respawn, not a move
    std::memcpy(u.markAt,t.at,12);u.markSeen=GameMs();
}

// Where a round at `speed` m/s from `from` meets the marked target (two passes of its flight).
void Led(const Unit& u,const float* from,float speed,float* at) noexcept {
    std::memcpy(at,u.markAt,12);
    for(int pass=0;pass<2;++pass) {
        const float t=vec::Dist(from,at)/speed;
        for(int i=0;i<3;++i)at[i]=u.markAt[i]+u.markVel[i]*t;
    }
}

// Use one physical barrel, never the mean of a launcher's tubes (that point can be in its hull).
// Missing weapon/bone data means there is no muzzle to fire from.
bool RoundFrom(const Unit& u,unsigned seat,unsigned shot,float* from,float* dir) noexcept {
    const unsigned char* const w=u.weapon[seat];
    if(!Readable(w,edf::kMuzzleCount+8))return false;
    const auto count=At<std::uint64_t>(w,edf::kMuzzleCount);
    const auto muzzles=At<const unsigned char*>(w,edf::kMuzzles);
    if(count==0 || count>64 || !Readable(muzzles,count*edf::kMuzzleStride))return false;
    return edf::MuzzleFrame(w,muzzles+(shot%count)*edf::kMuzzleStride,from,dir) && vec::Normalize(dir);
}

// The driver's remote use of the right cannon gives way to either real gunner.
bool DriverCannonFree(const unsigned char* v) noexcept {
    return SeatCount(v)>=kProteusSeats && SeatRider(SeatAt(const_cast<unsigned char*>(v),kGunnerSeat))==Rider::none &&
           SeatRider(SeatAt(const_cast<unsigned char*>(v),kRightSeat))==Rider::none;
}

float Tier(const unsigned char* v) noexcept {
    const float hpMax=At<float>(v,kHpMax);
    return std::isfinite(hpMax) && hpMax>0.0f ? hpMax/kDurability : 1.0f;
}

// The driver remotely operates the unoccupied right cannon, along its real barrel.
// A marked target near that bore may be led; the camera cannot fire backwards through the hull.
void DriverGun(Unit& u,unsigned char* v,const unsigned char* seat,ULONGLONG ms,const Config& c) noexcept {
    if(!c.proteusDriverGun || !u.closed || !DriverCannonFree(v) || c.proteusGunRate<=0.0f || u.st.mode!=proteus::Mode::deployed)return;
    if(At<float>(seat,kSeatFire)<kTriggerOn)return;
    const ULONGLONG gap=static_cast<ULONGLONG>(1000.0f/c.proteusGunRate);
    if(ms-u.gunAt<gap)return;
    float dir[3],from[3],at[3];
    if(!RoundFrom(u,kRightSeat,0,from,dir))return;
    bool led=false;
    if(u.mark) {
        const float d[3]={u.markAt[0]-from[0],u.markAt[1]-from[1],u.markAt[2]-from[2]};
        const float l=std::sqrt(Dot3(d,d));
        led=l>1.0f && std::acos(vec::Clamp(Dot3(d,dir)/l,-1.0f,1.0f))<=kLeadCone;
    }
    if(led) {
        Led(u,from,kRoundSpeed,at);
        const float delta[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
        const float distance=std::sqrt(Dot3(delta,delta));
        led=distance>1.0f && Dot3(delta,dir)/distance>=std::cos(kLeadCone);
    }
    if(!led) {
        const float end[3]={from[0]+dir[0]*kGunReach,from[1]+dir[1]*kGunReach,from[2]+dir[2]*kGunReach};
        if(MapRay(from,end,at)<0.0f)std::memcpy(at,end,12);
    }
    if(ProteusGunRound(v,from,at,c.proteusGunDamage*Tier(v)))u.gunAt=ms;
}

// The salvo: its rounds one every kSalvoGapMs at the mark, led, spread round it.
void Salvo(Unit& u,unsigned char* v,ULONGLONG ms,const Config& c) noexcept {
    if(u.salvoLeft<=0 || ms-u.salvoAt<kSalvoGapMs)return;
    if(!u.mark || SeatCount(v)<=kLauncherSeat || SeatRider(SeatAt(v,kLauncherSeat))!=Rider::none){u.salvoLeft=0;return;}
    float from[3],at[3],dir[3];
    if(!RoundFrom(u,kLauncherSeat,static_cast<unsigned>(u.salvoLeft),from,dir)){u.salvoLeft=0;return;}
    Led(u,from,kShellSpeed,at);
    const float a=static_cast<float>(u.salvoLeft)*2.39996f;   // the golden angle: the rounds fall round the point
    const float r=kSalvoSpread*std::sqrt(static_cast<float>(u.salvoLeft%c.proteusSalvoCount+1)/static_cast<float>(c.proteusSalvoCount));
    at[0]+=r*std::cos(a);at[2]+=r*std::sin(a);
    const float delta[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
    if(Dot3(delta,dir)<=0.0f){u.salvoLeft=0;return;}   // never fire back through the launcher
    if(ProteusSalvoRound(v,from,at,c.proteusSalvoDamage*Tier(v))){u.salvoAt=ms;--u.salvoLeft;}
    else u.salvoLeft=0;
}

// --- the field ---
bool SoldierVt(const void* vt,unsigned* which) noexcept {
    const unsigned kVts[]={kVtRanger,kVtWingDiver,kVtFencer,kVtAirRaider};
    for(unsigned x:kVts)if(vt==image+x){*which=x;return true;}
    return false;
}

// A weapon's shot countdown run `frames` more frames at `extra` of its rate.
void Hurry(unsigned char* w,float extra) noexcept {
    if(!Readable(w,kRate+4,true))return;
    float* const cd=reinterpret_cast<float*>(w+kCountdown);
    const float rate=At<float>(w,kRate);
    if(std::isfinite(*cd) && *cd>0.0f && *cd<kHoldCountdown*0.5f && std::isfinite(rate) && rate>0.0f) {
        const float less=*cd-extra*rate;
        *cd=less>0.0f ? less : 0.0f;
    }
}

struct Field {
    void** vtable;
    const unsigned char* self;
    float centre[3],r2;
    float taken,dealt,extra,energy;   // extra: countdown frames this frame on top; energy: share of the max this frame
    std::int32_t points;              // the Air Raiders' points this frame
    int allies;
};
void __fastcall FieldVisit(void* f,void* object) noexcept {
    __try {
        auto& w=*static_cast<Field*>(f);
        auto o=static_cast<unsigned char*>(object);
        if(!o || o==w.self || o[kDead])return;
        const float* p=Pos(o);
        const float d[3]={p[0]-w.centre[0],p[1]-w.centre[1],p[2]-w.centre[2]};
        if(Dot3(d,d)>w.r2)return;
        unsigned soldier=0;
        const bool human=SoldierVt(At<const void*>(o,0),&soldier);
        if(human && At<const void*>(o,kHumanVehicle))return;   // riding: its vehicle is covered instead
        if(!human && !KnownVehicle(o))return;
        ++w.allies;
        Put<float>(o,kTakenMul,At<float>(o,kTakenMul)*w.taken);
        if(human) {
            Put<float>(o,kDealtMul,At<float>(o,kDealtMul)*w.dealt);
            const auto list=At<unsigned char* const*>(o,kHumanWeapons);
            const auto n=At<std::uint64_t>(o,kHumanWeaponCount);
            if(n && n<=16 && Readable(list,n*8))
                for(std::uint64_t i=0;i<n;++i) {
                    unsigned char* const wp=list[i];
                    if(!Readable(wp,kReloadLeft+4,true))continue;
                    if(w.extra>0.0f)Hurry(wp,w.extra);
                    if(soldier==kVtAirRaider && w.points>0 && At<std::int32_t>(wp,kReloadType)==kReloadByPoints &&
                       At<std::int32_t>(wp,kReloadLeft)>0)
                        Put<std::int32_t>(wp,kReloadLeft,At<std::int32_t>(wp,kReloadLeft)-w.points);
                }
            if(soldier==kVtWingDiver && w.energy>0.0f) {
                const float most=At<float>(o,kEnergyMax),now=At<float>(o,kEnergy);
                if(std::isfinite(most) && most>0.0f && std::isfinite(now) && now<most)Put<float>(o,kEnergy,std::fmin(most,now+most*w.energy));
            }
        } else if(w.extra>0.0f) {
            const auto n=At<std::uint64_t>(o,kHolderCount);
            const auto base=At<unsigned char*>(o,kHolders);
            if(n && n<=16 && Readable(base,n*kHolderStride))
                for(std::uint64_t i=0;i<n;++i) {
                    unsigned char* const h=base+i*kHolderStride;
                    if(HolderAlive(h))Hurry(At<unsigned char*>(h,kHolderWeapon),w.extra);
                }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall FieldDtor(void*,unsigned) noexcept {}
void* kFieldVtable[]={reinterpret_cast<void*>(&FieldDtor),reinterpret_cast<void*>(&FieldVisit)};

void FieldFrame(Unit& u,unsigned char* v,float dt,const Config& c) noexcept {
    u.allies=0;
    if(!fieldOk || u.st.mode!=proteus::Mode::deployed || c.proteusFieldRadius<=0.0f || dt<=0.0f)return;
    const auto manager=At<void*>(image,kTeamManager);
    if(!manager)return;
    u.credits+=c.proteusFieldPower*dt;
    const std::int32_t points=static_cast<std::int32_t>(u.credits);
    u.credits-=static_cast<float>(points);
    Field f{kFieldVtable,v,{},c.proteusFieldRadius*c.proteusFieldRadius,1.0f-c.proteusFieldDefense,1.0f+c.proteusFieldAttack,
            (c.proteusFieldFireRate-1.0f)*dt*60.0f,c.proteusFieldEnergy*dt,points,0};
    std::memcpy(f.centre,Pos(v),12);
    // The player's side (a ridden vehicle's own team may still be the boardable 5): its friends, the side itself among them.
    reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,player.team,&f);
    u.allies=f.allies;
}

void Ring(Unit& u,const unsigned char* v,ULONGLONG ms,const Config& c) noexcept {
    if(u.st.mode!=proteus::Mode::deployed || c.proteusFieldRadius<=0.0f){u.ringCount=0;return;}
    if(u.ringCount && ms-u.ringAt<kRingMs)return;
    u.ringAt=ms;
    const float* p=Pos(v);
    for(int i=0;i<kProteusRing;++i) {
        const float a=2.0f*kPi*static_cast<float>(i)/static_cast<float>(kProteusRing);
        const float x=p[0]+c.proteusFieldRadius*std::cos(a),z=p[2]+c.proteusFieldRadius*std::sin(a);
        const float top[3]={x,p[1]+60.0f,z},bottom[3]={x,p[1]-60.0f,z};
        float hit[3];
        u.ring[i][0]=x;u.ring[i][2]=z;
        u.ring[i][1]=(MapRay(top,bottom,hit)>=0.0f ? hit[1] : p[1])+0.5f;
    }
    u.ringCount=kProteusRing;
}

void PublishZone(const Unit& u,const unsigned char* v,const Config& c) noexcept {
    const bool on=proteus::ShieldUp(u.st) && u.st.mode!=proteus::Mode::deployed && c.proteusPriority<1.0f && c.proteusPriorityRadius>0.0f;
    AcquireSRWLockExclusive(&zoneLock);
    if(on) {
        zone.on=true;
        std::memcpy(zone.z.centre,Pos(v),12);
        zone.z.radius=c.proteusPriorityRadius;zone.z.weight=c.proteusPriority;zone.z.vehicle=v;
        zone.team=player.team;zone.human=PlayerHuman();zone.at=GameMs();
    } else if(zone.z.vehicle==v)zone.on=false;
    ReleaseSRWLockExclusive(&zoneLock);
}

void Publish(const Unit& u,const unsigned char* v,bool driver,const unsigned char* driverSeat,const Config& c) noexcept {
    ProteusReadout r{};
    r.vehicle=v;
    std::memcpy(r.pos,Pos(v),12);
    std::memcpy(r.hull,u.shieldNose,12);
    r.seat=u.playerSeat;r.driver=driver;
    r.mode=u.st.mode;r.stagger=proteus::StaggerShare(u.st,TunablesOf(c));
    r.shieldOn=u.st.shieldOn;r.shieldUp=proteus::ShieldUp(u.st);r.dirShield=proteus::ShieldFollowsView(u.st);
    r.overheated=u.st.overheated;r.heat=u.st.heat;r.shieldHalfArc=c.proteusShieldArc*0.5f*kPi/180.0f;
    r.barrier=u.st.barrier;r.barrierHp=c.proteusBarrier*At<float>(v,kHpMax);
    r.marked=u.mark!=nullptr;
    if(r.marked){std::memcpy(r.markAt,u.markAt,12);r.markRange=vec::Dist(u.markAt,Pos(v));}
    r.salvoWait=u.st.salvoWait;r.salvoCooldown=c.proteusSalvoCooldownSec;r.salvoLeft=u.salvoLeft;
    bool gun=false,salvo=false;
    ProteusRoundsReady(&gun,&salvo);
    r.salvoArmed=salvo;
    r.gun=c.proteusDriverGun && gun && u.closed && DriverCannonFree(v) && u.st.mode==proteus::Mode::deployed;
    r.priority=proteus::ShieldUp(u.st) && u.st.mode!=proteus::Mode::deployed && c.proteusPriority<1.0f;
    r.fieldRadius=u.st.mode==proteus::Mode::deployed ? c.proteusFieldRadius : 0.0f;
    r.allies=u.allies;
    r.ringCount=u.ringCount;
    std::memcpy(r.ring,u.ring,sizeof(r.ring));
    r.keys=driverSeat && At<unsigned char>(driverSeat,kSeatPad)==0;
    r.modeKey=c.proteusModeKey;r.modeButton=c.proteusModeButton;r.shieldKey=c.proteusShieldKey;r.shieldButton=c.proteusShieldButton;
    r.markKey=c.proteusMarkKey;r.markButton=c.proteusMarkButton;r.salvoKey=c.proteusSalvoKey;
    AcquireSRWLockExclusive(&readoutLock);
    out=Out{r,GameMs()};
    ReleaseSRWLockExclusive(&readoutLock);
}

void DebugLog(Unit& u,const unsigned char* v) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-u.logAt<kLogMs)return;
    u.logAt=now;
    const char* const modes[]={"walk","deploying","deployed","stowing"};
    Log("PROTEUS v=%p %s shield %s heat %.0f%%%s barrier %.0f%% salvo %.1fs left %d mark %s allies %d lift %.1f; hits: %.0f blocked %.0f barrier %.0f hull",
        v,modes[static_cast<int>(u.st.mode)],u.st.shieldOn ? "on" : "off",u.st.heat*100.0f,u.st.overheated ? " OVERHEAT" : "",u.st.barrier*100.0f,
        u.st.salvoWait,u.salvoLeft,u.mark ? "yes" : "no",u.allies,u.lift,u.blocked,u.barred,u.through);
    Log("PROTEUS v=%p legs walk %.2f (stock %.2f) turn %.3f (stock %.3f) step normal %.2f (stock %.2f: %.1f m) jump %.1f",v,At<float>(v,kWalk),u.walk,
        At<float>(v,kTurn),u.turn,At<float>(v,kStepNormal),u.stepNormal,proteus::StepOf(At<float>(v,kStepNormal),kFootRadius),At<float>(v,kJump));
    u.blocked=u.barred=u.through=0.0f;
}

void Frame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    const bool live=ok && c.enabled && c.proteus && !v[kDead] && SeatCount(v)>=1;
    int playerSeat=-1;
    for(unsigned s=0;live && s<SeatCount(v);++s)if(SeatRider(SeatAt(v,s))==Rider::player){playerSeat=static_cast<int>(s);break;}
    Unit* const u=UnitOf(v,live && playerSeat>=0);
    if(!u)return;
    const ULONGLONG ms=GameMs();
    if(!live || playerSeat<0) {
        GiveBack(*u,v,!c.enabled || !c.proteus ? "the plugin or ProteusRework off" : v[kDead] ? "wrecked" : "no player aboard");
        if(zone.z.vehicle==v){AcquireSRWLockExclusive(&zoneLock);zone.on=false;ReleaseSRWLockExclusive(&zoneLock);}
        u->seen=ms;
        return;
    }
    if(u->frame==GameFrame())return;
    u->frame=GameFrame();
    if(!u->active) {   // taken: its stock numbers kept, its weapons found
        u->walk=At<float>(v,kWalk);u->walkEase=At<float>(v,kWalkEase);u->turn=At<float>(v,kTurn);
        u->jump=At<float>(v,kJump);u->stepNormal=At<float>(v,kStepNormal);
        for(unsigned s=1;s<kProteusSeats;++s) {
            u->weapon[s]=SeatWeapon(v,s,&u->holder[s]);
            u->rate[s]=u->weapon[s] ? At<float>(u->weapon[s],kRate) : 1.0f;
            u->spread[s]=u->weapon[s] ? At<float>(u->weapon[s],kSpread) : 1.0f;
        }
        u->active=true;u->lastMs=ms;
        u->st=proteus::State{};
        Log("PROTEUS v=%p: reworked (player in seat %d; seats %u; walk %.2f turn %.3f jump %.1f step normal %.2f = %.1f m; weapons %p %p %p)",v,
            playerSeat,SeatCount(v),u->walk,u->turn,u->jump,u->stepNormal,proteus::StepOf(u->stepNormal,kFootRadius),u->weapon[1],u->weapon[2],u->weapon[3]);
    }
    u->seen=ms;u->playerSeat=static_cast<unsigned>(playerSeat);
    const float dt=vec::Clamp(static_cast<float>(ms-u->lastMs)*0.001f,0.0f,0.1f);
    u->lastMs=ms;
    const bool driver=playerSeat==0;
    unsigned char* const driverSeat=SeatAt(v,0);
    proteus::Input in{};
    in.dt=dt;
    if(driver) {
        in.toggle=Pressed(*u,driverSeat,0,c);
        in.shield=Pressed(*u,driverSeat,1,c);
        const bool mark=Pressed(*u,driverSeat,2,c);
        in.salvo=Pressed(*u,driverSeat,3,c);
        Mark(*u,v,mark,dt,c);
    } else Mark(*u,v,false,dt,c);
    bool salvoReady=false;
    ProteusRoundsReady(nullptr,&salvoReady);
    in.marked=u->mark!=nullptr && salvoReady && vec::Dist(u->markAt,Pos(v))<=c.proteusSalvoRange;
    const proteus::Output o=proteus::Step(u->st,in,TunablesOf(c));
    if(o.modeChanged) {
        const char* const modes[]={"walking","deploying","deployed","stowing"};
        Log("PROTEUS v=%p: %s",v,modes[static_cast<int>(u->st.mode)]);
    }
    if(o.salvoFired){u->salvoLeft=c.proteusSalvoCount;u->salvoAt=0;Log("PROTEUS v=%p: salvo of %d at %.0f m",v,u->salvoLeft,vec::Dist(u->markAt,Pos(v)));}
    Legs(*u,v,c);
    if(c.proteusTwoSeats)TwoSeats(*u,v);
    else if(u->closed) {
        for(unsigned s=kRightSeat;s<kProteusSeats;++s)Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,u->seatMask[s]);
        u->closed=false;
    }
    Guns(*u,salvoReady,c);   // after the seats: whether seat 3 is closed decides the launcher's
    if(driver)DriverGun(*u,v,driverSeat,ms,c);
    Salvo(*u,v,ms,c);
    FieldFrame(*u,v,dt,c);
    Ring(*u,v,ms,c);
    std::memcpy(u->shieldNose,v+kMatrix+32,12);
    if(driver && proteus::ShieldFollowsView(u->st)) {
        float eye[3],nose[3];
        if(CameraRay(eye,nose)) {
            nose[1]=0.0f;
            if(vec::Normalize(nose))std::memcpy(u->shieldNose,nose,12);
        }
    }
    const float liftWant=u->st.mode==proteus::Mode::deployed ? c.proteusViewLift : 0.0f;
    u->lift+=(liftWant-u->lift)*vec::Clamp(dt*2.0f,0.0f,1.0f);
    PublishZone(*u,v,c);
    Publish(*u,v,driver,driverSeat,c);
    DebugLog(*u,v);
}

// --- the hooks the game calls ---
void FollowCannon(void* aim) noexcept {
    if(!Cfg().enabled || !Cfg().proteus || !Cfg().proteusTwoSeats)return;
    for(const auto& u:units) {
        if(!u.active || !u.closed)continue;
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(u.ref.obj));
        if(!u.ref.Is(v) || v[kDead] || SeatCount(v)<kProteusSeats)continue;
        auto right=seataim::Object(SeatAt(v,kRightSeat));
        const auto launcher=seataim::Object(SeatAt(v,kLauncherSeat));
        if(aim==launcher && u.playerSeat==0 && u.st.mode==proteus::Mode::deployed) {
            seataim::Follow(seataim::Object(SeatAt(v,0)),launcher,[](unsigned char* axis) noexcept {
                reinterpret_cast<AxisApplyFn>(image+kAxisApply)(axis,true);
            });
            return;
        }
        if(aim!=right)continue;
        const unsigned source=Cfg().proteusDriverGun && u.playerSeat==0 && u.st.mode==proteus::Mode::deployed && DriverCannonFree(v) ? 0 : kGunnerSeat;
        const auto left=seataim::Object(SeatAt(v,source));
        seataim::Follow(left,right,[](unsigned char* axis) noexcept {
            reinterpret_cast<AxisApplyFn>(image+kAxisApply)(axis,true);
        });
        return;
    }
}

template<int I> void __fastcall AimHook(void* aim,const float* input) {
    reinterpret_cast<AimFn>(nextAim[I])(aim,input);
    __try { FollowCannon(aim); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

float* Shield(void* object,void* gdi,float* was) noexcept {
    Unit* const u=ActiveOf(object);
    if(!u || !damageOk)return nullptr;
    auto o=static_cast<unsigned char*>(object);
    auto g=static_cast<unsigned char*>(gdi);
    if(o[kDead] || !Readable(g,kDmgAmount+4,true))return nullptr;
    float* const dmg=reinterpret_cast<float*>(g+kDmgAmount);
    if(!(*dmg>0.0f) || !std::isfinite(*dmg))return nullptr;
    const Config& c=Cfg();
    const float* hit=reinterpret_cast<const float*>(g+kDmgAt);
    const float* p=Pos(o);
    const float* nose=u->shieldNose;
    const proteus::Tunables k=TunablesOf(c);
    const bool inArc=std::isfinite(hit[0]+hit[2]) && proteus::InArc(nose[0],nose[2],hit[0]-p[0],hit[2]-p[2],k.shieldHalfArc);
    const float before=u->st.barrier;
    const float hp=c.proteusBarrier*At<float>(o,kHpMax);
    const float through=proteus::Absorb(u->st,k,*dmg,inArc,hp);
    const float barred=(before-u->st.barrier)*hp;
    u->barred+=barred;u->blocked+=*dmg-through-barred;u->through+=through;
    *was=*dmg;*dmg=through;
    return dmg;
}

void __fastcall DamageHook(void* object,void* gdi) {
    float was=0.0f;
    float* dmg=nullptr;
    __try { dmg=Shield(object,gdi,&was); } __except(EXCEPTION_EXECUTE_HANDLER){dmg=nullptr;}
    reinterpret_cast<DamageFn>(image+kDamageFn)(object,gdi);
    if(dmg)*dmg=was;   // the queue's own copy: nothing reads it after, put back only to leave no trace
}

const void* __fastcall UserHook(void* iface,const void* weapon) noexcept {
    const void* const user=nextUser(iface,weapon);
    if(user)return user;
    __try {
        const Unit* const u=ActiveOf(static_cast<unsigned char*>(iface)-kUserIface);
        if(u && u->closed && weapon==u->weapon[kRightSeat] && u->weapon[kGunnerSeat])return nextUser(iface,u->weapon[kGunnerSeat]);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return user;
}

// Whether the soldier searching (`f`) is on the zone's side and `cand` is an enemy the zone puts first.
bool Priority(const unsigned char* f,const unsigned char* cand,float* weight) noexcept {
    AcquireSRWLockShared(&zoneLock);
    const Zone z=zone;
    ReleaseSRWLockShared(&zoneLock);
    if(!z.on || GameMs()-z.at>kStaleMs)return false;
    const auto searcher=At<const unsigned char*>(f,kSearcher);
    if(!Readable(searcher,kTeam+4) || At<std::int32_t>(searcher,kTeam)!=z.team || !Readable(cand,kEnemyTarget+8))return false;
    const void* const target=At<const void*>(cand,kEnemyTarget);
    const float* p=Pos(cand);
    const float d[3]={p[0]-z.z.centre[0],p[1]-z.z.centre[1],p[2]-z.z.centre[2]};
    if(target!=z.z.vehicle && (!target || target!=z.human) && Dot3(d,d)>z.z.radius*z.z.radius)return false;
    *weight=z.z.weight;
    return *weight>0.0f && *weight<1.0f;
}

// The stock keeps the least distance (+0x10) and its enemy (+0x18). A priority enemy is weighed by `w`: compared
// against the best / w, and kept (as d x w) when it wins; the best put back as it was when it does not.
void __fastcall SearchHook(void* functor,void* cand) {
    auto f=static_cast<unsigned char*>(functor);
    float w=1.0f;
    bool weigh=false;
    __try { weigh=cand && Priority(f,static_cast<const unsigned char*>(cand),&w); } __except(EXCEPTION_EXECUTE_HANDLER){weigh=false;}
    if(!weigh){nextSearch(functor,cand);return;}
    const float best=At<float>(f,kSearchBest);
    const void* const found=At<const void*>(f,kSearchFound);
    Put<float>(f,kSearchBest,best/w);
    nextSearch(functor,cand);
    if(At<const void*>(f,kSearchFound)==cand && found!=cand)Put<float>(f,kSearchBest,At<float>(f,kSearchBest)*w);
    else Put<float>(f,kSearchBest,best);
}

bool AllMatch(const Sig* s,std::size_t n,const char* what) noexcept {
    for(std::size_t i=0;i<n;++i)if(!Matches(s[i].rva,s[i].bytes,s[i].size)){Log("PROTEUS %s: code at %#x not as expected",what,s[i].rva);return false;}
    return true;
}
}  // namespace

bool ProteusReady() noexcept { return ok; }

bool ProteusDamageThunk(const void* target) noexcept { return damageThunk && target==damageThunk; }

bool IsProteus(const void* vehicle) noexcept {
    __try { return vehicle && Big(vehicle); } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

unsigned ProteusVisibleSeats(const unsigned char* vehicle,unsigned count) noexcept {
    const Unit* u=ActiveOf(vehicle);
    if(!u || !u->closed || count!=kProteusSeats)return count;
    // Another player or a real soldier who boarded before the rework keeps their
    // place, and must remain visible until they leave it.
    for(unsigned i=kRightSeat;i<kProteusSeats;++i)
        if(At<const void*>(SeatAt(const_cast<unsigned char*>(vehicle),i),kSeatRider))return count;
    return 2;
}

bool InstallProteus() noexcept {
    __try {
        ok=AllMatch(kSigs,sizeof(kSigs)/sizeof(kSigs[0]),"the rework");
        if(!ok){Log("PROTEUS off: the Proteus stays stock");return false;}
        InstallProteusPose();
        // Installed after the turret camera and stabilizer: retain their hooks.
        if(!AllMatch(kAimSigs,sizeof(kAimSigs)/sizeof(kAimSigs[0]),"the paired cannons")){ok=false;return false;}
        const unsigned tables[2]={kAimVt,kAimSeVt};
        void* hooks[2]={reinterpret_cast<void*>(&AimHook<0>),reinterpret_cast<void*>(&AimHook<1>)};
        for(int i=0;i<2;++i) {
            if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+tables[i])+2,hooks[i],&nextAim[i])) {
                ok=false;
                for(int j=0;j<i;++j) {
                    // Restore only our own slot. A failed restore leaves a safe forwarding hook:
                    // keep nextAim alive and no unit is active while installation failed.
                    if(!edf::PatchVtableSlot(reinterpret_cast<void**>(image+tables[j])+2,hooks[j],nextAim[j]))
                        Log("PROTEUS aim hook %d rollback failed; retaining its original continuation",j);
                }
                return false;
            }
        }
        bool changed=false;
        damageOk=AllMatch(kDamageSigs,sizeof(kDamageSigs)/sizeof(kDamageSigs[0]),"shields") &&
                 RedirectCall(image+kDamageCall,image+kDamageFn,reinterpret_cast<void*>(&DamageHook),changed);
        if(damageOk) {   // the call's new target, for subcarrier.cpp's check of the same call (ProteusDamageThunk)
            std::int32_t rel=0;
            std::memcpy(&rel,image+kDamageCall+1,4);
            damageThunk=image+kDamageCall+5+rel;
        }
        if(Matches(kUserSig.rva,kUserSig.bytes,kUserSig.size)) {
            void** const slot=reinterpret_cast<void**>(image+kUserSlotRva);
            void* next=nullptr;
            userOk=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&UserHook),&next);
            if(userOk)nextUser=reinterpret_cast<UserFn>(next);
        }
        fieldOk=AllMatch(kFieldSigs,sizeof(kFieldSigs)/sizeof(kFieldSigs[0]),"the field");
        if(AllMatch(kSearchSigs,sizeof(kSearchSigs)/sizeof(kSearchSigs[0]),"the allies' priority")) {
            void** const slot=reinterpret_cast<void**>(image+kSearchSlotRva);
            void* next=nullptr;
            searchOk=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&SearchHook),&next);
            if(searchOk)nextSearch=reinterpret_cast<SearchFn>(next);
        }
        Log("HOOK proteus: rework=1 shields=%d weaponUser=%d field=%d alliesPriority=%d",damageOk,userOk,fieldOk,searchOk);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;return false;}
}

void ProteusFrame(unsigned char* v) noexcept {
    if(!IsProteus(v))return;
    Frame(v);
}

bool ProteusViewLift(const unsigned char* seat,float* look,float* eye) noexcept {
    for(const auto& u:units) {
        if(!u.active || u.lift<0.05f)continue;
        const auto v=static_cast<const unsigned char*>(u.ref.obj);
        if(!Readable(v,kSelfCtrl+8) || !u.ref.Is(v))continue;   // gone, or another object at its address: not this unit
        const auto first=At<const unsigned char*>(v,kSeats);
        if(seat<first || seat>=first+SeatCount(v)*kSeatStride)continue;
        float back[3]={eye[0]-look[0],0.0f,eye[2]-look[2]};
        if(!vec::Normalize(back))back[0]=back[2]=0.0f;
        for(int i=0;i<3;i+=2)eye[i]+=back[i]*u.lift*0.5f;
        eye[1]+=u.lift;look[1]+=u.lift*0.6f;
        return true;
    }
    return false;
}

bool ProteusPriorityZone(ProteusZone* z) noexcept {
    AcquireSRWLockShared(&zoneLock);
    const Zone c=zone;
    ReleaseSRWLockShared(&zoneLock);
    if(!c.on || !Cfg().enabled || GameMs()-c.at>kStaleMs)return false;
    *z=c.z;
    return true;
}

bool PlayerProteus(ProteusReadout* r) noexcept {
    AcquireSRWLockShared(&readoutLock);
    const Out o=out;
    ReleaseSRWLockShared(&readoutLock);
    if(!o.at || GameMs()-o.at>kReadoutMs)return false;
    *r=o.r;
    return true;
}

void ResetProteus() noexcept {
    for(auto& u:units)u=Unit{};
    AcquireSRWLockExclusive(&zoneLock);
    zone=Zone{};
    ReleaseSRWLockExclusive(&zoneLock);
    AcquireSRWLockExclusive(&readoutLock);
    out=Out{};
    ReleaseSRWLockExclusive(&readoutLock);
}
}  // namespace crew

// EDF6AutoTurret asks where its turrets look first (common/edf/aimlink.h PriorityZoneV1).
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_PriorityZoneV1(edf::aimlink::PriorityZoneV1* out) {
    if(!out)return false;
    __try {
        crew::ProteusZone z{};
        if(!crew::ProteusPriorityZone(&z))return false;
        std::memcpy(out->centre,z.centre,sizeof(out->centre));
        out->radius=z.radius;out->weight=z.weight;out->vehicle=z.vehicle;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
