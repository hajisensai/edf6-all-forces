// The Proteus rework (README 普罗透斯, docs/proteus-re.md; ini Proteus*; the user, 2026-10-06: "重构普罗透斯：行走时是快速
// 转移的指挥平台，架设后变成能保护周围友军的火力据点。座位：两。"; 2026-10-09: "护盾用原版的护盾样式" "完全重做"). The Proteus is the
// stock VehicleBigBegaruta (vtable 0x17DEC40: V614_PROTEUS_MK2*, VEHICLE407_BIGBEGARUTA*), its stock model and weapons.
// One table per concern, the game doing the work wherever it can:
//  - Seats (proteus_logic.h kSeats): the driver (0) and the gunner (1). Seats 2 / 3 (the right cannon's, the
//    launcher's) are closed to boarding while ProteusTwoSeats (their class mask seat+0x30 = 0: CanRideSeat 0x6346FC
//    refuses everyone; the stock RideAi's dummy gunners there are sent off), given back after.
//  - Weapons (proteus_weapons.inc): the three stock mounts, each borrowed by the first soldier of its chain while its own
//    seat is empty (kMounts): the gunner works both cannons, the driver works them with nobody at the guns and the
//    launcher deployed (its second trigger). The stock weapon activates, aims, locks on, fires and hurts.
//  - Shield (proteus_shield.inc): the stock Air Raider's electromagnetic barrier (BarrierBullet01) raised round the hull
//    from EDF6VC_PROTEUS_SHIELD.SGO (tools/make_proteus.py), kept on the hull every frame, facing the nose walking and
//    the driver's view deployed; the game stops hostile rounds on it and spends its HP.
//  - Stances (proteus_logic.h Step): legs (vehicle +0x1978 walk speed, +0x197C its ease, +0x1980 turn rate, set once
//    from the SGO by 0x647EA0 and read each frame by 0x645A20; the jump's speed at controller +0x290; the walkable
//    ground normal at controller +0xD4 for ProteusStepHeight) and the cannons' rate / spread (weapon +0xE10 / +0xE14).
//  - Deployed, the field (proteus_field.inc), the piles down (proteus_visual.inc), the camera raised (ProteusViewLift).
//  - Walking with the shield up, the allies' priority zone (SearchHook, EDF6AutoTurret's PriorityZoneV1).
//  - Online (proteus_net.inc): the driver's machine publishes stance / shield / field; the registered owner the shield's HP.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "vecmath.h"
#include "body506.h"
#include "proteus_pose.h"
#include "proteus_net.h"
#include "online_authority.h"
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
constexpr std::size_t kSeatClassMask=0x30,kSeatPad=0x2B0,kSeatButtons=0x2E8,kSeatFire=0x2E4,kSeatFire2=0x2E0;
constexpr float kTriggerOn=0.8f;
// A weapon (docs/proteus-re.md §5): the trigger latch and its held copy, the shot countdown, its rate, the cone's scale.
constexpr std::size_t kPull=0x139,kHeld=0x13A,kRate=0xE10,kSpread=0xE14;
constexpr unsigned kPullFn=0x62C000;
constexpr unsigned kAxisApply=0x5FC280;
// The weapon user (heli.cpp DoorGunUser's): the vehicle's interface at +0x120, its slot 11 (0x62D950).
constexpr unsigned kUserSlotRva=0x17DEE68,kUserFn=0x62D950;
constexpr std::size_t kUserIface=0x120;
// The soldiers (sidecar.cpp kSoldierVts): AssultSoldier, PaleWing (the Wing Diver), HeavyArmor, Engineer (the Air Raider).
constexpr unsigned kVtRanger=0x17CDF28,kVtWingDiver=0x17D0FF8,kVtFencer=0x17CF5B8,kVtAirRaider=0x17CF100;
constexpr std::size_t kHumanVehicle=0x1548,kHumanWeapons=0x1950,kHumanWeaponCount=0x1960;
constexpr std::size_t kTakenMul=0x384,kDealtMul=0x388,kEnergyMax=0x304,kEnergy=0x308,kCountdown=0xE0C;
constexpr std::size_t kReloadType=0x208,kReloadLeft=0xE68;
constexpr std::int32_t kReloadByPoints=2;
// The team manager's walk of a side's friends (sidecar.cpp): functor slot 1 per object.
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978;
// SearchAttackTarget (vtable 0x17D24C0), slot 1 (0x598C50): functor +8 the soldier searching, +0x10 the least distance
// so far, +0x18 the enemy that has it; an enemy's target (weak object) at +0x518, compared only.
constexpr unsigned kSearchSlotRva=0x17D24C8,kSearchFn=0x598C50;
constexpr std::size_t kSearcher=0x8,kSearchBest=0x10,kSearchFound=0x18,kEnemyTarget=0x518;
constexpr ULONGLONG kStaleMs=1500,kLogMs=2000,kRingMs=500,kReadoutMs=250;
constexpr ULONGLONG kDefenseSilentMs=3000;   // the owner sends its count every 250 ms: this long without one is no v2 owner
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
    {kAxisApply,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18},15},
};
const Sig kUserSig={kUserFn,{0x41,0x57,0x48,0x83,0xEC,0x30,0x4C,0x69,0x99,0xF8,0x04,0x00,0x00,0x40,0x03,0x00},16};
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

bool ok=false,fieldOk=false,searchOk=false,userOk=false;
using UserFn=const void*(__fastcall*)(void*,const void*);
using SearchFn=void(__fastcall*)(void*,void*);
using PullFn=void(__fastcall*)(void*);
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);
using SeatFn=void(__fastcall*)(void*,void*);
using AxisApplyFn=void(__fastcall*)(void*,bool);
UserFn nextUser=nullptr;
SearchFn nextSearch=nullptr;

struct NetState {
    proteus_net::Gate gate;
    proteus_net::State control;
    bool networked=false,remote=false,haveControl=false,defenseDirty=false;
    std::int32_t epoch=-1;
    ULONGLONG receivedAt=0,sentAt=0,defenseSentAt=0;
    ULONGLONG defenseAt=0;             // the owner's last shield count received here
    ULONGLONG activeAt=0;              // this machine's control of the rework began
    // A peer runs another build of the rework (a packet of another version, or as the local driver no shield count
    // from the owner within kDefenseSilentMs): this Proteus stays stock on this machine until the session or mission
    // ends, so the two machines never disagree on a wall or on borrowed weapons.
    bool incompatible=false;
};
// One stock weapon mount as the rework holds it (proteus_logic.h kMounts' order).
struct Arm {
    unsigned char* weapon=nullptr;
    unsigned char* holder=nullptr;
    float rate=1.0f,spread=1.0f;     // its countdown rate and cone scale as taken (given back)
};
// The native shield standing for a unit (proteus_shield.inc).
struct Barrier {
    unsigned char* obj=nullptr;      // the BarrierBullet01 (nullptr: none standing)
    ULONGLONG raisedFrame=0;         // the frame a raise was fired (its first step makes the barrier); 0 none pending
    ULONGLONG seenFrame=0;           // the last frame its own update ran
    float full=0.0f;                 // the HP share 1 stands for (the hull's max HP x ProteusBarrier)
    float raised=0.0f;               // the HP the barrier was given
    bool failed=false;               // a raise never appeared: no more raises this ride (logged once)
};
// One Proteus and its ObjRef-bound network state (a slot not seen for kStaleMs is free).
struct Unit {
    ObjRef ref;
    ULONGLONG seen,frame,lastMs,logAt,ringAt;
    bool active;                       // reworked this frame (its stock numbers kept below)
    bool posed;                        // its piles were posed (proteus_visual.inc: put back once when given back)
    proteus::State st;
    // The stock numbers, taken when it was first reworked (given back when it is not).
    float walk,walkEase,turn,jump,stepNormal;
    std::int32_t seatMask[proteus::kSeats];
    bool closed;                       // seats 2 and 3 closed
    Arm arm[proteus::kMountCount];
    bool held[2];                      // the driver's stance and shield buttons last frame
    float lift;                        // the camera's raise now (eases to ProteusViewLift deployed)
    float credits;                     // the Air Raiders' points owed (whole ones handed over)
    int allies;
    int ringCount;
    float ring[kProteusRing][3];
    unsigned playerSeat;               // the first seat a local player holds
    float shieldNose[3];               // where the shield faces (horizontal)
    Barrier barrier;
    NetState net;
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
    k.shieldRegenSec=c.proteusBarrierRegenSec;k.shieldDelaySec=c.proteusBarrierDelaySec;
    return k;
}

bool RegisteredProteus(const void* v) noexcept { return drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)); }
// The shield's HP belongs to the registered owner (vehicle +0x128 remote bit 0), where the native damage path settles
// hits (BigBegaruta slot 34 0x6347C0); remote copies take its count.
bool DefenseOwner(const void* v) noexcept { return !RegisteredProteus(v) || (At<std::uint16_t>(v,0x128)&1)==0; }
bool ControlFresh(const Unit& u,const void* v,ULONGLONG ms) noexcept {
    return Cfg().enabled && Cfg().proteus && !At<unsigned char>(v,kDead) && u.net.haveControl && ms>=u.net.receivedAt && ms-u.net.receivedAt<=kStaleMs && RegisteredProteus(v) &&
        u.net.control.controller==ProteusNetController(const_cast<unsigned char*>(static_cast<const unsigned char*>(v)));
}
proteus::Tunables RulesOf(const Unit& u) noexcept {
    auto k=TunablesOf(Cfg());
    if(u.net.remote && u.net.haveControl) {
        const auto& s=u.net.control;k.deploySec=s.deploySec;k.stowSec=s.stowSec;
        k.shieldRegenSec=s.regenSec;k.shieldDelaySec=s.delaySec;
    }
    return k;
}
float BarrierShare(const Unit& u) noexcept { return u.net.remote && u.net.haveControl ? u.net.control.barrierShare : Cfg().proteusBarrier; }

Unit* UnitOf(const void* v,bool make) noexcept {
    const ULONGLONG ms=GameMs();
    Unit* free=nullptr;
    for(auto& u:units) {
        if(u.ref.Is(v))return &u;
        if(!free && (!u.ref || u.ref.obj==v || ms-u.seen>kStaleMs))free=&u;
    }
    if(!make || !free)return nullptr;
    *free=Unit{};
    free->ref=ObjRef::Of(v);
    return free;
}

// The active unit of the vehicle (game thread: the hooks the game calls on it).
Unit* ActiveOf(const void* v) noexcept {
    for(auto& u:units)if(u.ref.Is(v)) {
        if(u.net.incompatible)return nullptr;
        if(RegisteredProteus(v) && !IsOnlineAuthority(v))
            return u.net.remote && ControlFresh(u,v,GameMs()) && (u.net.control.flags&proteus_net::kActive) ? &u : nullptr;
        if(u.net.networked && u.net.epoch!=ProteusNetController(const_cast<unsigned char*>(static_cast<const unsigned char*>(v))))return nullptr;
        return u.active && !u.net.remote ? &u : nullptr;
    }
    return nullptr;
}

#include "proteus_visual.inc"
#include "proteus_weapons.inc"
#include "proteus_shield.inc"

// --- giving back ---
void RefreshFieldWrites() noexcept;
void OpenSeats(Unit& u,unsigned char* v) noexcept;
void Incompatible(Unit& u,unsigned char* v,std::uint32_t version) noexcept;
void GiveBack(Unit& u,unsigned char* v,const char* why) noexcept {
    DropBarrier(u,why);
    if(!u.active)return;
    u.active=false;
    Put<float>(v,kWalk,u.walk);Put<float>(v,kWalkEase,u.walkEase);Put<float>(v,kTurn,u.turn);
    Put<float>(v,kJump,u.jump);Put<float>(v,kStepNormal,u.stepNormal);
    OpenSeats(u,v);
    GunsBack(u);
    const float shield=u.st.shield,quiet=u.st.quiet;
    u.st=proteus::State{};
    u.st.shield=shield;u.st.quiet=quiet;   // the shield's HP outlives a ride (no free refill by getting out)
    u.lift=0.0f;
    RefreshFieldWrites();
    Log("PROTEUS v=%p: stock again (%s)",v,why);
}

// --- the stance's numbers on the legs ---
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

// --- the seats ---
// A real soldier of this machine at the gunner's seat (a copied remote player / NPC fires on their own machine).
bool LocalGunner(unsigned char* v) noexcept {
    if(SeatCount(v)<=proteus::kGunner)return false;
    const auto seat=SeatAt(v,proteus::kGunner);
    if(!edf::LivingSoldierInSeat(image,seat))return false;
    return !InSession() || IsOnlineAuthority(At<const void*>(seat,kSeatRider));
}
void OpenSeats(Unit& u,unsigned char* v) noexcept {
    if(!u.closed)return;
    for(unsigned s=proteus::kRightGunner;s<proteus::kSeats && s<SeatCount(v);++s)Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,u.seatMask[s]);
    u.closed=false;
}
// Seats 2 and 3 closed (two seats wanted) or open, their dummy gunners sent off while closed. `kick`: this machine may
// send them off (not a replica's observer with no say over the seat).
void Seats(Unit& u,unsigned char* v,bool two,bool kick) noexcept {
    if(!two || SeatCount(v)<proteus::kSeats){OpenSeats(u,v);return;}
    if(!u.closed) {
        for(unsigned s=proteus::kRightGunner;s<proteus::kSeats;++s) {
            u.seatMask[s]=At<std::int32_t>(SeatAt(v,s),kSeatClassMask);
            Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,0);
        }
        u.closed=true;
        Log("PROTEUS v=%p: two seats (seats 2, 3 closed; masks were %#x %#x)",v,u.seatMask[proteus::kRightGunner],u.seatMask[proteus::kLauncherSeat]);
    }
    for(unsigned s=proteus::kRightGunner;kick && s<proteus::kSeats;++s)
        if(SeatRider(SeatAt(v,s))==Rider::dummy) {
            reinterpret_cast<SeatFn>(image+kSeatKick)(v,SeatAt(v,s));
            Log("PROTEUS v=%p: the NPC gunner of seat %u sent off (two seats)",v,s);
        }
}

// --- the driver's keys ---
// The button `b` (0 stance, 1 shield) pressed this frame.
bool Pressed(Unit& u,const unsigned char* seat,int b,const Config& c) noexcept {
    bool down=false;
    if(At<unsigned char>(seat,kSeatPad)) {
        const int button=b==0 ? c.proteusModeButton : c.proteusShieldButton;
        down=button>0 && (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(button))!=0;
    } else down=KeyHeld(b==0 ? c.proteusModeKey : c.proteusShieldKey);
    const bool press=down && !u.held[b];
    u.held[b]=down;
    return press;
}

// --- the field ---
bool SoldierVt(const void* vt,unsigned* which) noexcept {
    const unsigned kVts[]={kVtRanger,kVtWingDiver,kVtFencer,kVtAirRaider};
    for(unsigned x:kVts)if(vt==image+x){*which=x;return true;}
    return false;
}

// A weapon's shot countdown run `extra` more frames of its rate.
void Hurry(unsigned char* w,float extra) noexcept {
    if(!Readable(w,kRate+4,true))return;
    float* const cd=reinterpret_cast<float*>(w+kCountdown);
    const float rate=At<float>(w,kRate);
    if(std::isfinite(*cd) && *cd>0.0f && std::isfinite(rate) && rate>0.0f) {
        const float less=*cd-extra*rate;
        *cd=less>0.0f ? less : 0.0f;
    }
}

struct Field {
    void** vtable;
    const unsigned char* self;
    Unit* source;
    float centre[3],r2;
    float taken,dealt,extra,energy;   // extra: countdown frames this frame on top; energy: share of the max this frame
    std::int32_t points;              // the Air Raiders' points this frame
    int allies;
    float dt;
};
#include "proteus_field.inc"
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
        if(!FieldOwnedHere(o))return;
        ++w.allies;RecordField(w,o,human,soldier);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall FieldDtor(void*,unsigned) noexcept {}
void* kFieldVtable[]={reinterpret_cast<void*>(&FieldDtor),reinterpret_cast<void*>(&FieldVisit)};

void FieldFrame(Unit& u,unsigned char* v,float dt) noexcept {
    u.allies=0;
    RefreshFieldWrites();
    const auto p=FieldParameters(u);
    if(!fieldOk || u.st.mode!=proteus::Mode::deployed || p.radius<=0.0f || dt<0.0f)return;
    const auto manager=At<void*>(image,kTeamManager);
    if(!manager)return;
    u.credits+=p.power*dt;
    const std::int32_t points=static_cast<std::int32_t>(u.credits);
    u.credits-=static_cast<float>(points);
    Field f{kFieldVtable,v,&u,{},p.radius*p.radius,1.0f-p.defense,1.0f+p.attack,
            (p.fireRate-1.0f)*dt*60.0f,p.energy*dt,points,0,dt};
    std::memcpy(f.centre,Pos(v),12);
    // The player's side (a ridden vehicle's own team may still be the boardable 5): its friends, the side itself among them.
    reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,p.team,&f);
    u.allies=f.allies;
}

void Ring(Unit& u,const unsigned char* v,ULONGLONG ms) noexcept {
    const float radius=FieldParameters(u).radius;
    if(u.st.mode!=proteus::Mode::deployed || radius<=0.0f){u.ringCount=0;return;}
    if(u.ringCount && ms-u.ringAt<kRingMs)return;
    u.ringAt=ms;
    const float* p=Pos(v);
    for(int i=0;i<kProteusRing;++i) {
        const float a=2.0f*kPi*static_cast<float>(i)/static_cast<float>(kProteusRing);
        const float x=p[0]+radius*std::cos(a),z=p[2]+radius*std::sin(a);
        const float top[3]={x,p[1]+60.0f,z},bottom[3]={x,p[1]-60.0f,z};
        float hit[3];
        u.ring[i][0]=x;u.ring[i][2]=z;
        u.ring[i][1]=(MapRay(top,bottom,hit)>=0.0f ? hit[1] : p[1])+0.5f;
    }
    u.ringCount=kProteusRing;
}

bool PriorityOn(const Unit& u,const Config& c) noexcept {
    return proteus::ShieldUp(u.st) && u.st.mode!=proteus::Mode::deployed && c.proteusPriority<1.0f && c.proteusPriorityRadius>0.0f;
}
void PublishZone(const Unit& u,const unsigned char* v,const Config& c) noexcept {
    AcquireSRWLockExclusive(&zoneLock);
    if(PriorityOn(u,c)) {
        zone.on=true;
        std::memcpy(zone.z.centre,Pos(v),12);
        zone.z.radius=c.proteusPriorityRadius;zone.z.weight=c.proteusPriority;zone.z.vehicle=v;
        zone.team=player.team;zone.human=PlayerHuman();zone.at=GameMs();
    } else if(zone.z.vehicle==v)zone.on=false;
    ReleaseSRWLockExclusive(&zoneLock);
}
void DropZone(const unsigned char* v) noexcept {
    AcquireSRWLockExclusive(&zoneLock);
    if(zone.z.vehicle==v)zone.on=false;
    ReleaseSRWLockExclusive(&zoneLock);
}

int ObserverSeat(unsigned char* v) noexcept {
    const unsigned char* human=PlayerHuman();
    if(!human)return -1;
    for(unsigned i=0;i<SeatCount(v);++i) {
        const auto seat=SeatAt(v,i);
        if(SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==human)return static_cast<int>(i);
    }
    return -1;
}
void Publish(const Unit& u,unsigned char* v,const Config& c) noexcept {
    const int observer=ObserverSeat(v);
    if(observer<0)return;
    const auto observerSeat=SeatAt(v,static_cast<unsigned>(observer));
    ProteusReadout r{};
    r.vehicle=v;
    std::memcpy(r.pos,Pos(v),12);
    std::memcpy(r.hull,u.shieldNose,12);
    r.seat=static_cast<unsigned>(observer);r.driver=observer==0;
    r.mode=u.st.mode;r.stagger=proteus::StaggerShare(u.st,RulesOf(u));
    r.shieldOn=u.st.shieldOn;r.shieldUp=proteus::ShieldUp(u.st);r.dirShield=proteus::ShieldFollowsView(u.st);
    r.overheated=u.st.overheated;r.broken=u.st.broken;r.heat=u.st.heat;
    r.shieldReady=BarrierReady() && !u.barrier.failed;r.shieldHalfArc=kBarrierHalfArc;
    r.shield=u.st.shield;r.shieldHp=BarrierShare(u)*At<float>(v,kHpMax);
    r.launcher=false;
    for(unsigned m=0;m<proteus::kMountCount;++m)
        if(proteus::kMounts[m].seat==proteus::kLauncherSeat && OperatorOf(u,v,m)==static_cast<int>(proteus::kDriver))r.launcher=true;
    r.priority=PriorityOn(u,c);
    r.fieldRadius=u.st.mode==proteus::Mode::deployed ? FieldParameters(u).radius : 0.0f;
    r.allies=u.allies;
    r.ringCount=u.ringCount;
    std::memcpy(r.ring,u.ring,sizeof(r.ring));
    r.keys=At<unsigned char>(observerSeat,kSeatPad)==0;
    r.modeKey=c.proteusModeKey;r.modeButton=c.proteusModeButton;r.shieldKey=c.proteusShieldKey;r.shieldButton=c.proteusShieldButton;
    r.launcherKey=c.proteusSalvoKey;
    AcquireSRWLockExclusive(&readoutLock);
    out=Out{r,GameMs()};
    ReleaseSRWLockExclusive(&readoutLock);
}

void DebugLog(Unit& u,unsigned char* v) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-u.logAt<kLogMs)return;
    u.logAt=now;
    const char* const modes[]={"walk","deploying","deployed","stowing"};
    int ops[proteus::kMountCount];
    for(unsigned m=0;m<proteus::kMountCount;++m)ops[m]=OperatorOf(u,v,m);
    Log("PROTEUS v=%p %s shield %s%s%s hp %.0f%% heat %.0f%% barrier %p allies %d lift %.1f; mounts L%d R%d M%d (operator seats)",
        v,modes[static_cast<int>(u.st.mode)],u.st.shieldOn ? "on" : "off",proteus::ShieldUp(u.st) ? " UP" : "",u.st.broken ? " BROKEN" : u.st.overheated ? " OVERHEAT" : "",
        u.st.shield*100.0f,u.st.heat*100.0f,u.barrier.obj,u.allies,u.lift,ops[0],ops[1],ops[2]);
    Log("PROTEUS v=%p legs walk %.2f (stock %.2f) turn %.3f (stock %.3f) step normal %.2f (stock %.2f: %.1f m) jump %.1f",v,At<float>(v,kWalk),u.walk,
        At<float>(v,kTurn),u.turn,At<float>(v,kStepNormal),u.stepNormal,proteus::StepOf(At<float>(v,kStepNormal),kFootRadius),At<float>(v,kJump));
}

#include "proteus_net.inc"

// Taken: its stock numbers kept, its weapons found.
void Take(Unit& u,unsigned char* v,ULONGLONG ms,int playerSeat) noexcept {
    u.walk=At<float>(v,kWalk);u.walkEase=At<float>(v,kWalkEase);u.turn=At<float>(v,kTurn);
    u.jump=At<float>(v,kJump);u.stepNormal=At<float>(v,kStepNormal);
    for(auto& a:u.arm)a=Arm{};
    RefreshWeapons(u,v);
    u.active=true;u.lastMs=ms;u.barrier.failed=false;
    const float shield=u.st.shield,quiet=u.st.quiet;
    u.st=proteus::State{};u.st.shield=shield;u.st.quiet=quiet;
    Log("PROTEUS v=%p: reworked (player in seat %d; seats %u; walk %.2f turn %.3f jump %.1f step normal %.2f = %.1f m; mounts %p %p %p)",v,
        playerSeat,SeatCount(v),u.walk,u.turn,u.jump,u.stepNormal,proteus::StepOf(u.stepNormal,kFootRadius),u.arm[0].weapon,u.arm[1].weapon,u.arm[2].weapon);
}

void Frame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    const bool live=ok && c.enabled && c.proteus && !v[kDead] && SeatCount(v)>=1;
    int playerSeat=-1;bool anyPlayer=false;
    for(unsigned s=0;live && s<SeatCount(v);++s) {
        const auto seat=SeatAt(v,s);anyPlayer=anyPlayer || AnyPlayerIn(seat);
        if(playerSeat<0 && SeatRider(seat)==Rider::player)playerSeat=static_cast<int>(s);
    }
    Unit* const u=UnitOf(v,live && (playerSeat>=0 || RegisteredProteus(v)));
    if(!u)return;
    const ULONGLONG ms=GameMs();
    if(u->net.incompatible && !RegisteredProteus(v))u->net.incompatible=false;   // out of the session: ours again
    if(u->net.incompatible) {
        GiveBack(*u,v,"a peer runs another build of the Proteus rework");
        DropZone(v);u->seen=ms;
        return;
    }
    if(NetworkFrame(*u,v,live,playerSeat,ms))return;
    if(!live || !anyPlayer) {
        const bool wasActive=u->active;
        GiveBack(*u,v,!c.enabled || !c.proteus ? "the plugin or ProteusRework off" : v[kDead] ? "wrecked" : "no player aboard");
        DropZone(v);
        u->seen=ms;
        SendControl(*u,v,ms,wasActive); // an inactive host/empty-seat snapshot retires the old driver's shield
        return;
    }
    if(u->frame==GameFrame())return;
    u->frame=GameFrame();
    if(!u->active){Take(*u,v,ms,playerSeat);u->net.activeAt=ms;}
    u->seen=ms;u->playerSeat=static_cast<unsigned>(playerSeat);
    RefreshWeapons(*u,v);
    const float dt=vec::Clamp(static_cast<float>(ms-u->lastMs)*0.001f,0.0f,0.1f);
    u->lastMs=ms;
    const bool driver=playerSeat==0;
    proteus::Input in{};
    in.dt=dt;
    if(driver) {
        const auto driverSeat=SeatAt(v,proteus::kDriver);
        in.toggle=Pressed(*u,driverSeat,0,c);
        in.shield=Pressed(*u,driverSeat,1,c);
    }
    const proteus::State before=u->st;
    const proteus::Output o=proteus::Step(u->st,in,TunablesOf(c));
    if(u->net.networked && !DefenseOwner(v)){u->st.shield=before.shield;u->st.quiet=before.quiet;u->st.broken=before.broken;}   // the owner's count
    if(o.modeChanged) {
        const char* const modes[]={"walking","deploying","deployed","stowing"};
        Log("PROTEUS v=%p: %s",v,modes[static_cast<int>(u->st.mode)]);
    }
    Legs(*u,v,c);
    Seats(*u,v,c.proteusTwoSeats,true);
    Guns(*u,c);
    FieldFrame(*u,v,dt);
    Ring(*u,v,ms);
    FaceShield(*u,v,driver);
    BarrierFrame(*u,v);
    const float liftWant=u->st.mode==proteus::Mode::deployed ? c.proteusViewLift : 0.0f;
    u->lift+=(liftWant-u->lift)*vec::Clamp(dt*2.0f,0.0f,1.0f);
    PublishZone(*u,v,c);
    if(playerSeat>=0)Publish(*u,v,c);
    DebugLog(*u,v);
    SendDefense(*u,v,ms);
    SendControl(*u,v,ms,o.modeChanged || in.shield);
    // The local driver of a Proteus another machine owns hears that owner's shield count every 250 ms when the owner
    // runs this build; silence means it does not (an older rework, or none): back to stock here.
    if(u->net.networked && !DefenseOwner(v)) {
        const ULONGLONG since=u->net.defenseAt>u->net.activeAt ? u->net.defenseAt : u->net.activeAt;
        if(ms>since && ms-since>kDefenseSilentMs)Incompatible(*u,v,0);
    }
}

// A peer runs another build of the rework (`version` its packets', 0: the owner's count never came): stock here.
void Incompatible(Unit& u,unsigned char* v,std::uint32_t version) noexcept {
    if(!u.net.incompatible) {
        if(version)Log("PROTEUS v=%p: a peer sends Proteus protocol version %u (this build %u): stock Proteus here this session",v,version,proteus_net::kVersion);
        else Log("PROTEUS v=%p: no shield count from the Proteus's owner in %llu ms: it runs another build (this one %u); stock Proteus here",
                 v,kDefenseSilentMs,proteus_net::kVersion);
    }
    u.net.incompatible=true;
    GiveBack(u,v,"a peer runs another build of the Proteus rework");
    DropZone(v);
}

// --- the allies' priority (the soldiers' target search) ---
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

bool IsProteus(const void* vehicle) noexcept {
    __try { return vehicle && Big(vehicle); } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

int ProteusBorrowedWeapons(const unsigned char* vehicle,unsigned seat,const unsigned char** outW,int max) noexcept {
    int n=0;
    __try {
        if(!ok || !userOk || !outW || !Cfg().enabled || !Cfg().proteus || !IsProteus(vehicle) || vehicle[kDead])return 0;
        auto v=const_cast<unsigned char*>(vehicle);
        Unit* const u=ActiveOf(v);
        if(!u || SeatCount(v)<proteus::kSeats)return 0;
        RefreshWeapons(*u,v);
        for(unsigned m=0;m<proteus::kMountCount && n<max;++m) {
            const int op=OperatorOf(*u,v,m);
            if(op==static_cast<int>(seat) && proteus::Borrowed(proteus::kMounts[m],op) && u->arm[m].weapon &&
               SeatRider(SeatAt(v,proteus::kMounts[m].seat))==Rider::none)outW[n++]=u->arm[m].weapon;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return n;}
    return n;
}

unsigned ProteusVisibleSeats(const unsigned char* vehicle,unsigned count) noexcept {
    const Unit* u=ActiveOf(vehicle);
    if(!u || !u->closed || count!=proteus::kSeats)return count;
    // Another player or a real soldier who boarded before the rework keeps their place, and must remain visible until
    // they leave it.
    for(unsigned i=proteus::kRightGunner;i<proteus::kSeats;++i)
        if(At<const void*>(SeatAt(const_cast<unsigned char*>(vehicle),i),kSeatRider))return count;
    return 2;
}

bool InstallProteus() noexcept {
    __try {
        ok=AllMatch(kSigs,sizeof(kSigs)/sizeof(kSigs[0]),"the rework");
        if(!ok){Log("PROTEUS off: the Proteus stays stock");return false;}
        if(!InstallProteusWeapons()){ok=false;Log("PROTEUS off: the weapon mounts' slot 5 is not as expected");return false;}
        InstallProteusUser();
        InstallProteusPose();
        InstallProteusBarrier();
        InstallProteusNet();
        fieldOk=AllMatch(kFieldSigs,sizeof(kFieldSigs)/sizeof(kFieldSigs[0]),"the field");
        if(AllMatch(kSearchSigs,sizeof(kSearchSigs)/sizeof(kSearchSigs[0]),"the allies' priority")) {
            void** const slot=reinterpret_cast<void**>(image+kSearchSlotRva);
            void* next=nullptr;
            searchOk=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&SearchHook),&next);
            if(searchOk)nextSearch=reinterpret_cast<SearchFn>(next);
        }
        Log("HOOK proteus: rework=1 weaponUser=%d shield=%d field=%d alliesPriority=%d",userOk,barrierOk,fieldOk,searchOk);
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
    for(auto& u:units)u=Unit{};   // the barriers went with the mission's objects
    for(auto& r:raised)r=Raised{};
    RefreshFieldWrites();for(auto& field:fieldTargets)field=FieldTarget{};
    AcquireSRWLockExclusive(&zoneLock);
    zone=Zone{};
    ReleaseSRWLockExclusive(&zoneLock);
    AcquireSRWLockExclusive(&readoutLock);
    out=Out{};
    ReleaseSRWLockExclusive(&readoutLock);
}
void ProteusNetIncompatible(unsigned char* v,std::uint32_t version) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().proteus || !IsProteus(v) || v[kDead] || !RegisteredProteus(v))return;
    Unit* u=UnitOf(v,true);if(!u)return;
    Incompatible(*u,v,version);
}
void ProteusNetReceived(unsigned char* v,const proteus_net::State& s) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().proteus || !IsProteus(v) || v[kDead] || !RegisteredProteus(v))return;
    Unit* u=UnitOf(v,true);if(!u)return;
    if(!u->net.gate.Admit(s,true,IsOnlineAuthority(v),DefenseOwner(v),ProteusNetController(v)))return;
    ReceiveNetwork(*u,v,s,GameMs());
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
