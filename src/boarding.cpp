// The boarding gun (tools/calls.py EDF6VC_CALL_BOARDING_GUN): a KFF 50 LS (laser sight, 5.5x scope) whose rounds
// carry a tag (tools/call_weapons.py gun_sgo). A round of it at a friendly vehicle does it no harm and puts the
// player into it:
//  1. the stock collision pipeline finds a real hit, after map clipping and object shape casts. Its damage call
//     (0x230EA6 in 0x230CA0) is intercepted: BoardingHit suppresses only that friendly hit's damage and asks for
//     the vehicle (the nearest actual contact to the sweep start). Stock hit events and bullet lifetime still run;
//  2. the next frame, on the game thread and outside any team walk (FrameTick), BoardingTick presses the stock board
//     button for the player (0x56D700, heli.cpp PressBoardButtonBumping) with
//       - the player's position, for the seat check's reach alone (CanRideSeat 0x6346D0 measures human+0x90 to the
//         seat's riding point), on the riding point of the seat tried;
//       - FindSeat (crew.cpp FindSeatHook) answering for this vehicle only (BoardingOnly), so a vehicle the button's
//         visitor comes to first is never the one boarded;
//       - a vehicle of the friends' team (2) put on the nobody's team (5) first, as the sea rescue does (heli.cpp):
//         the button's visitor walks team 5 and the player's own team only, and the stock seat check lets a human of
//         another team into no seat of these. The stock ride then gives it the player's team; a press that seated
//         nobody gives it its own back.
//     The seats are tried in order (the driver's first); an NPC's is taken as the player's own press takes it (crew.cpp
//     Bump: the NPC moves to a free gunner seat or gets off). Seated, the player stays where the stock ride put them;
//     a press that seated nobody puts their position back.
// Only the local player's rounds, only vehicles of the board-able classes (crew.cpp kClasses) and not the plugin's
// aircraft without player flight controls or submarine carriers, only the player's side, the friends' and
// nobody's (an enemy's vehicle takes the round as the stock game has it).
// Static RE and what is still to be seen in game: docs/boarding-re.md.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include <atomic>
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kHitDamageCall=0x230EA6,kHitDamage=0x541FF0;
constexpr std::size_t kDamageBlock=0x6E0,kHitRecords=0x7A8,kBulletOwner=0x9A8,kSweepFrom=0xB80;
constexpr std::size_t kHumanVehicle=0x1548;                      // the vehicle a human rides (weak_ptr object)
constexpr ULONGLONG kAskMs=250;                                   // an ask older than this (wall clock) is dropped
constexpr ULONGLONG kLogMs=2000;
using HitDamageFn=void(__fastcall*)(void*,const void*,void*);
HitDamageFn nextHitDamage=nullptr;

bool ready=false;
// The local player's human as the game thread last saw it (BoardingTick), for the hit's test of a round's owner.
// The bullet batch is not proven to run on the game thread (docs/bullet-pass-re.md §4.1 "M"), PlayerHuman() is.
std::atomic<const void*> shooter{nullptr};

// The vehicle the last round asked for. Written by the hit hook, taken by the tick.
struct Ask { const void* vehicle; const void* ctrl; float dist2; ULONGLONG at; };
Ask ask{};
SRWLOCK askLock=SRWLOCK_INIT;
const void* only=nullptr;   // BoardingOnly: game thread, for the length of one press

thread_local ULONGLONG saidAt=0;

// The tag: the gun's AmmoColor alpha (call_weapons.gun_sgo), 1 + its mark (tools/calls.py, 7301) ulps over 1.0. The
// weapon init reads AmmoColor as 4 floats into weapon+0x8D0 (0x68D978), the round's parameters are the weapon's from
// +0x830 (fire: 0x69712F), and the parameter copy (0x2307F0) puts them at core+0x9A0 on, the colour at core+0xA40..
// +0xA4C dword by dword (0x2309D6); nothing in the bullet code reads or writes it but the drawing. Unlike AmmoSpeed,
// AmmoDamage, AmmoAlive or AmmoSize no star curve and no fire modifier scales it. The owner is the soldier the weapon
// was made for (its create info's human, weak at weapon+0x838 = core+0x9A8: 0x68D5AD). A stock colour with this
// alpha: none (2026-10-05: the 1564 stock weapons use six alphas, 0.05 to 1.0).
constexpr std::uint32_t kTagBits=0x3F800000u+7301u;
constexpr std::size_t kColorAlpha=0xA4C;
struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kTagSignatures[]={
    {0x2307F0,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20},16},   // param copy
    {0x2309D6,{0x8B,0x87,0xA0,0x00,0x00,0x00,0x89,0x83,0xA0,0x00,0x00,0x00,0x8B,0x87,0xA4,0x00},16},   // ...the colour
    {0x68D978,{0x0F,0x28,0x45,0x50,0x0F,0x11,0x86,0xD0,0x08,0x00,0x00,0x4C,0x89,0xB5,0xE0,0x05},16},   // AmmoColor
    {0x68D5AD,{0x48,0x8D,0x8E,0x38,0x08,0x00,0x00,0x48,0x8D,0x55,0xE0,0xE8,0x83,0x15,0xF0,0xFF},16},   // the owner
    {0x69712F,{0x49,0x8D,0x97,0x30,0x08,0x00,0x00,0x49,0x8D,0x8F,0x10,0x0A,0x00,0x00,0xE8,0xAE},16},   // fire's copy
    {0x230E34,{0x41,0x8B,0x07,0x48,0x8B,0x8B,0xA8,0x07,0x00,0x00,0x89,0x41,0x20},13}, // body's id in the hit record
    {0x230E41,{0x48,0x8B,0x83,0xA8,0x07,0x00,0x00,0x48,0x8B,0x4D,0x6F,0x0F,0x10,0x01,0x0F,0x11},16}, // actual contact
    {0x230E70,{0x48,0x8D,0x8B,0xE0,0x06,0x00,0x00,0x0F,0x57,0xC0,0xF3,0x0F,0x7F,0x44,0x24,0x20},16}, // damage block
    {0x230E80,{0x49,0x8B,0x56,0x30,0x48,0x85,0xD2,0x74,0x11,0x49,0x8B,0x46,0x28,0x48,0x89,0x44},16}, // target weak reference
    {0x230E9A,{0x4C,0x8D,0x83,0x30,0x07,0x00,0x00,0x48,0x8D,0x54,0x24,0x20,0xE8,0x45,0x11,0x31},16}, // three arguments
};

bool TaggedOk() noexcept {
    for(const auto& sig:kTagSignatures)if(!Matches(sig.rva,sig.bytes,sig.size))return false;
    return true;
}

// Whether the round of this core is a boarding gun's.
bool Tagged(const unsigned char* core) noexcept {
    const auto bits=At<std::uint32_t>(core,kColorAlpha);
    return bits>=kTagBits && bits<=kTagBits+3u; // Ranger, Wing Diver, Air Raider, Fencer
}

bool Friendly(std::int32_t team,std::int32_t playerTeam) noexcept {
    return team==playerTeam || team==kTeamVehicle || team==kTeamFriend;
}

bool Seated(const unsigned char* human,const unsigned char* v) noexcept {
    if(HumanOnFoot(human) || At<const void*>(human,kHumanVehicle)!=v)return false;
    for(unsigned i=0;i<SeatCount(v);++i)if(SeatRider(SeatAt(const_cast<unsigned char*>(v),i))==Rider::player)return true;
    return false;
}

// One press with the player's position on seat `i`'s riding point; true when they are in `v` after it.
bool PressOn(unsigned char* human,unsigned char* v,unsigned i) noexcept {
    float at[3],reach=0.0f;
    if(!SeatPoint(v,i,at,&reach))return false;
    float* const pos=reinterpret_cast<float*>(human+kPosition);
    float was[3];
    std::memcpy(was,pos,12);
    std::memcpy(pos,at,12);
    only=v;
    PressBoardButtonBumping(human);
    only=nullptr;
    if(Seated(human,v))return true;
    std::memcpy(pos,was,12);   // nobody seated: back where they stood
    return false;
}

// The vehicle being boarded: the board button is pressed once a frame for up to kTryMs (game clock) until the player
// is in it. The stock button does nothing while the soldier is busy (0x56D700's gates human+0x128 bit 0, +0x5D0 bit
// 2, +0x39C != 0: the sniper's bolt after the shot may be one), as the catch jet's presses wait it out (playerjet.cpp).
constexpr ULONGLONG kTryMs=1500;
struct Boarding { ObjRef ref; std::int32_t team; bool moved; ULONGLONG since; int presses; };
Boarding boarding{};

void EndBoarding(unsigned char* v,bool seated) noexcept {
    if(!seated && boarding.moved && boarding.ref.Is(v) && !v[kDead])SetObjectTeam(v,boarding.team);   // its own team back
    boarding=Boarding{};
}

// A new vehicle to board (the round's ask): refused (logged) or taken on, the friends' team moved to nobody's.
void StartBoarding(unsigned char* human,unsigned char* v,ULONGLONG ms) noexcept {
    if(boarding.ref.obj)EndBoarding(const_cast<unsigned char*>(static_cast<const unsigned char*>(boarding.ref.obj)),false);
    const std::int32_t team=At<std::int32_t>(v,kTeam),playerTeam=At<std::int32_t>(human,kTeam);
    const char* refused=nullptr;
    if(VehicleClassOf(v)<0 || v[kDead])refused="not a board-able vehicle any more";
    else if(IsSub(v) || (IsJet(v) && !PlayerJetBoardingSupported(v)))refused="this aircraft has no player flight controls";
    else if(!Friendly(team,playerTeam))refused="not on the player's side";
    else if(!HumanOnFoot(human))refused="the player is in a vehicle";
    else if(SeatCount(v)==0)refused="no seats";
    if(refused){Log("BOARDING v=%p team=%d: %s",v,team,refused);return;}
    const bool moved=team!=playerTeam && team!=kTeamVehicle;
    if(moved)SetObjectTeam(v,kTeamVehicle);
    boarding=Boarding{ObjRef::Of(v),team,moved,ms,0};
}

// A frame of it: a press per seat, the driver's first, until one seats the player.
void BoardStep(unsigned char* human,ULONGLONG ms) noexcept {
    auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(boarding.ref.obj));
    if(!boarding.ref.Is(v) || v[kDead]){Log("BOARDING v=%p: gone before the player was in it",v);EndBoarding(v,false);return;}
    if(!HumanOnFoot(human)){Log("BOARDING v=%p: the player got into %p meanwhile",v,At<const void*>(human,kHumanVehicle));EndBoarding(v,Seated(human,v));return;}
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    const float* vp=reinterpret_cast<const float*>(v+kPosition);
    const float d[3]={vp[0]-p[0],vp[1]-p[1],vp[2]-p[2]};
    const float away=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    ++boarding.presses;
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i) {
        if(!PressOn(human,v,i))continue;
        Log("BOARDING v=%p class %d seat %u: the player boarded it from %.0f m (press %d)",v,VehicleClassOf(v),i,away,boarding.presses);
        EndBoarding(v,true);
        return;
    }
    if(ms-boarding.since<kTryMs)return;
    Log("BOARDING v=%p team=%d: no seat of %u taken in %d presses (%.0f m out); gates +0x128=%02x +0x5D0=%08x +0x39C=%d",v,
        boarding.team,count,boarding.presses,away,At<unsigned char>(human,0x128),At<std::uint32_t>(human,0x5D0),At<std::int32_t>(human,0x39C));
    EndBoarding(v,false);
}
bool BoardingHit(const unsigned char* core,const void* object) noexcept {
    if(!ready || !Cfg().enabled || !Cfg().boardingGun)return false;
    const void* const shot=shooter.load(std::memory_order_relaxed);
    if(!shot)return false;
    if(!core || At<const void*>(core,kBulletOwner)!=shot || !Tagged(core))return false;
    if(!object || VehicleClassOf(object)<0)return false;
    // Only a vehicle of the player's side is spared: an enemy's takes the round (the tick checks the rest).
    const std::int32_t team=At<std::int32_t>(object,kTeam);
    if(!Friendly(team,At<std::int32_t>(shot,kTeam)))return false;
    const float* from=reinterpret_cast<const float*>(core+kSweepFrom);
    const float* at=At<const float*>(core,kHitRecords);   // 0x230E41..0x230E4F copied the actual contact here
    if(!at)return false;
    const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
    const float dist2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(!std::isfinite(dist2))return false;
    const ULONGLONG now=GetTickCount64();
    const void* const ctrl=At<const void*>(object,kSelfCtrl);   // no game-memory read while holding the lock
    AcquireSRWLockExclusive(&askLock);
    if(!ask.vehicle || now-ask.at>kAskMs || dist2<ask.dist2)ask=Ask{object,ctrl,dist2,now};
    ReleaseSRWLockExclusive(&askLock);
    if(Cfg().debug && now-saidAt>kLogMs){saidAt=now;Log("BOARDING hit v=%p (team %d): damage suppressed",object,team);}
    return true;
}

// Only the direct-hit damage call in 0x230CA0 is redirected. Its target argument is the weak reference the stock
// code just took from the hit object. Keep the surrounding hit bookkeeping, effects and bullet stop/penetration.
void __fastcall HitDamageHook(void* damage,const void* target,void* info) {
    bool board=false;
    __try {
        board=BoardingHit(static_cast<const unsigned char*>(damage)-kDamageBlock,At<const void*>(target,0));
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("BOARDING hit: fault (stock damage retained)");board=false; }
    if(!board)nextHitDamage(damage,target,info);
}
}  // namespace

void BoardingTick() noexcept {
    if(!ready)return;
    unsigned char* const human=Cfg().boardingGun ? PlayerHuman() : nullptr;
    shooter.store(human,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&askLock);
    const Ask a=ask;
    ask=Ask{};
    ReleaseSRWLockExclusive(&askLock);
    const ULONGLONG ms=GameMs();
    if(!human) {
        if(boarding.ref.obj)EndBoarding(const_cast<unsigned char*>(static_cast<const unsigned char*>(boarding.ref.obj)),false);
        return;
    }
    if(a.vehicle && GetTickCount64()-a.at<=kAskMs && a.vehicle!=boarding.ref.obj) {
        const ObjRef ref{a.vehicle,a.ctrl};
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(a.vehicle));
        if(ref.Is(v))StartBoarding(human,v,ms);
        else Log("BOARDING v=%p: gone before the player could board it",v);
    }
    if(boarding.ref.obj)BoardStep(human,ms);
}

const void* BoardingOnly() noexcept { return only; }

// A board request of the plugin's own (sazabi.cpp's SazabiTestBoard): the player into `v` as a boarding gun round at it
// would put them (StartBoarding; BoardingTick presses on until they are in or kTryMs is out).
void BoardingRequest(unsigned char* v) noexcept {
    unsigned char* const human=ready ? PlayerHuman() : nullptr;
    if(human && v)StartBoarding(human,v,GameMs());
}

void ResetBoarding() noexcept {
    boarding=Boarding{};   // the mission's objects are gone: nothing to give a team back to
    shooter.store(nullptr,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&askLock);
    ask=Ask{};
    ReleaseSRWLockExclusive(&askLock);
    only=nullptr;
}

bool InstallBoarding() noexcept {
    const bool profile=TaggedOk();
    if(profile && BoardButtonReady()) {
        nextHitDamage=reinterpret_cast<HitDamageFn>(image+kHitDamage);
        bool changed=false;
        ready=RedirectCall(image+kHitDamageCall,image+kHitDamage,reinterpret_cast<void*>(&HitDamageHook),changed);
        if(!ready && changed)Log("BOARDING hit damage call half patched: the hook forwards every hit");
    }
    Log("HOOK boarding gun=%d (profile=%d board button=%d)",ready,profile,BoardButtonReady());
    return ready;
}
}  // namespace crew
