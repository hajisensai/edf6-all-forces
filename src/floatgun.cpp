// The float guns (floatgun.h; docs/gravity-re.md §7, static RE 2026-10-20). What happens, and through what:
//  - The mission (FloatGunsMissionStart): the game status' row snapshot (GS+0x15000, written once at the mission's start,
//    0x70F103) and the mode table's field 1 of that row (0xE1560: the mission's directory, "app:/Mission/EDF5_OLD_SCRIPT/M099"
//    for EDF5's last); FloatGuns 1 takes that one alone, 2 every mission.
//  - A death (Watch): the colonists (Humanoid_Basic, vtable 0x17C4D70: EDF5's Primer infantry, and the frogs) among the
//    player's enemies are watched (walked each tick, each one's control block pinned by a weak count: npc_mark.h Assign),
//    and one whose dead flag is set has its guns taken that tick, whoever killed it on whichever machine (the dead flag is
//    the game's own, replicated; the damage function's hook saw only the kills settled here):
//      a colonist's guns are scene objects of their own (HumanoidWeapon_Basic / _Heavy) in its weak list (+0x1120, count
//      +0x1130); each holds its bullet factory (+0x170), the bullet parameters the game built for that enemy (+0x180, 0x1E0
//      bytes: the class's damage times the enemy's level and the difficulty), its rhythm (+0x480: pellets, burst, frames
//      between shots, spread, frames after a burst) and its SGO (+0x100). A wrecked vehicle leaves nothing: the game keeps
//      its wreck whole (no turret comes off), and the gravity gun can pick that up.
//  - The drop: a colonist's gun is the game's own dropped gun, HumanoidWeaponBase::Drop (the factory 0x49CE80 the game calls
//    when a colonist's arm is shot off, 0x4A4FC0, made the same way: the gun's SGO in its parameter, at the gun's matrix):
//    the gun's model falling and lying as the physics has it. Its own update (slot 5, 0x4A1730) deletes it after 600
//    frames; the plugin's hook keeps ours (its counter +0x830 held at 0).
//  - The pick-up: the local player within FloatGunReach of it (horizontally; kPickRise up or down, or the reach if more). Up to FloatGunMost float
//    round a player (another replaces the oldest).
//  - The flight: each its own place on a ring kRingRadius round its owner kRingHigh over them, turning; the hook writes the
//    drop's matrix there and hands it to the model (0x1100B90, the update's own call), the physics steps skipped.
//  - The fire: at the nearest enemy of the owner's side within its rounds' reach that the map does not hide, in the
//    gun's own rhythm, its bullets as the game fired them (0x4A0100's way: a fresh parameter block with the saved numbers,
//    the team the owner's, the owner the player (kills theirs), the target (homing rounds), the custom parameter a
//    persistent copy (edf41classes.h Persist), then the bullet factory 0x1194280 from the gun's muzzle).
//  - The end: FloatGunSec after the pick-up (0: the mission), or a new mission; the drop deleted (0x118A1B0).
//  - Online (gravity_net_protocol.h): the host takes the guns of a colonist that died (its serials) and tells each peer
//    (`drop`: the colonist's native ID and the gun's place in its list); each peer takes the same gun from its own copy of
//    that colonist once it is dead there too (or kDropWaitMs after the message: as it is). A pick-up is the host's: its own
//    player's at once, a peer's player's by `pick`, the first asked; `owner` tells every machine whose it is, `gone` ends one
//    (replaced, or never picked up). Every machine draws each gun round its owner and fires it: a round's owner is that
//    player, and the game deals a round's hit only on the machine whose player the attacker is (EDF6Coop docs/net-re/damage.md), so
//    each gun's damage counts once.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#define _CRT_RAND_S
#include <cstdlib>
#include "floatgun.h"
#include "crew.h"
#include "command_identity.h"
#include "edf41classes.h"
#include "gravity_net_protocol.h"
#include "memory.h"
#include "npc_mark.h"
#include "online_authority.h"
#include "support_net.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kGameStatus=0x20B2890,kRowAt=0x15000,kModeCfg=0x130;
constexpr unsigned kModePath=0xE1560,kRowStore=0x70F100;
constexpr std::size_t kSceneMgr=0x20B2958;
constexpr unsigned kDropMake=0x49CE80,kDropUpdate=0x4A1730,kModelMatrix=0x1100B90,kParamCtor=0x1001D0,kParamDtor=0x100C90,
                   kWeakAssign=0x6CEBB0,kSpawn=0x1194280,kDelete=0x118A1B0;
constexpr unsigned kDropVtable=0x17C4308,kDropParamVtable=0x17C42F8,kColonistVtable=0x17C4D70,
                   kGunBasicVtable=0x17C4920,kGunHeavyVtable=0x17C4B50;
constexpr unsigned kVariantCopy=0x179E1C0,kVariantDtor=0x1765220;   // per-tag tables of the SGO value handle
const unsigned char kModePathSig[]={0x40,0x53,0x48,0x83,0xEC,0x40,0x48,0x81,0xC1,0xF0,0x00,0x00,0x00,0x48,0x8B,0x19};
const unsigned char kRowStoreSig[]={0x8B,0x51,0x48,0x89,0x91,0x00,0x50,0x01,0x00};   // mov edx,[rcx+0x48]; mov [rcx+0x15000],edx
const unsigned char kDropMakeSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57};
// The dropped gun's update whole: its counter, the delete past 600 frames, the ragdoll step, the model's matrix, the ragdoll's end.
const unsigned char kDropUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x91,0x30,0x08,0x00,0x00,0x48,0x8B,0xF9,
    0x8D,0x42,0x01,0x89,0x81,0x30,0x08,0x00,0x00,0x81,0xFA,0x58,0x02,0x00,0x00,0x76,0x05,0xE8,0x57,0x8A,0xCE,0x00,0x48,0x8D,0x8F,
    0x50,0x06,0x00,0x00,0xE8,0x3B,0xC5,0x24,0x00,0x48,0x8D,0x57,0x60,0x48,0x8D,0x8F,0x10,0x02,0x00,0x00,0xE8,0x1B,0xF4,0xC5,0x00};
const unsigned char kModelMatrixSig[]={0xC6,0x81,0xB0,0x00,0x00,0x00,0x01,0x0F,0x28,0x02,0x48,0x8B,0x41,0x10,0x0F,0x29};
const unsigned char kParamCtorSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x48,0x89,0x4C,0x24,0x08,0x57};
const unsigned char kParamDtorSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48};
const unsigned char kWeakAssignSig[]={0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x4C,0x8B,0x42,0x08,0x48,0x8B};
const unsigned char kSpawnSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x49,0x8B,0xF1,0x48,0x8B};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};

// A colonist and its guns.
constexpr std::size_t kGunRows=0x1120,kGunCount=0x1130,kGunOwner=0x5B0,kGunSgo=0x100,kGunFactory=0x170,kGunParam=0x180,
                      kGunPellets=0x480,kGunBurst=0x484,kGunGap=0x488,kGunSpread=0x48C,kGunCool=0x490,kGunMuzzles=0x620,
                      kGunMuzzleCount=0x630,kGunMuzzleStride=0x90,kGunMuzzleMatrix=0x50,kGunEnabled=0x5E2;
// The bullet parameters (InitParam, 0x1E0): the owner weak +0x38, the target weak +0x60, the muzzle matrix it may follow
// +0x70, the team +0x90, the numbers +0x94..+0xE8 (speed m/frame, life frames, damage, ..., colour, gravity), the custom
// parameter's handle +0xE8 (tag +0xF8; 1: a node), the seed +0x1A0; +0x30 a damage scale.
constexpr std::size_t kParamSize=0x1E0,kPOwner=0x38,kPTarget=0x60,kPMuzzle=0x70,kPTeam=0x90,kPNumbers=0x94,kPCustom=0xE8,
                      kPCustomTag=0xF8,kPSeed=0x1A0,kPScale=0x30,kPSpeed=0x94,kPLife=0x98,kPDamage=0x9C;
constexpr std::uint16_t kTagNode=1,kTagNone=0xFFFF;
constexpr std::size_t kLockPoints=0x368,kLockCount=0x378;   // a game object's lock points: {LockPoint*, ctrl} each
constexpr std::size_t kDropCounter=0x830,kDropModel=0x210;

constexpr int kMostGuns=24,kMostWatched=256,kMostDrops=32,kMostOut=64;
constexpr ULONGLONG kWatchDeadMs=15000; // a dead one is kept this long (a peer's drop message names it)
constexpr ULONGLONG kDropWaitMs=3000;   // a peer: a drop of a colonist still alive here is taken this long after its message
constexpr ULONGLONG kDropLostMs=10000;  // ...and one of a colonist not found here at all is given up this long after
constexpr ULONGLONG kAskGapMs=1000;     // a peer asks the host for a gun it stands by no more often than this
constexpr float kPickRise=3.0f;   // m up or down; across: the ini's FloatGunReach (3.5: the stock item box's 3 m, a little more)
constexpr float kRingRadius=3.2f,kRingHigh=3.0f,kRingTurn=0.6f,kBob=0.25f,kFollow=8.0f;
constexpr float kMostReach=400.0f,kLeastReach=30.0f;
constexpr ULONGLONG kGroundMs=120000;                      // a gun no one picked up stays this long
constexpr int kMostBurst=200;
constexpr int kShotLogEvery=100;

enum class State : std::uint8_t { free, ground, floating };
struct Gun {
    State state=State::free;
    ObjRef drop;               // the dropped gun's model (its control block pinned: the factory's weak count, kept)
    float at[3]{};             // on the ground: where it lies
    void* factory=nullptr;
    unsigned char numbers[kPCustom-kPTeam]{};   // +0x90 .. +0xE8 of the parameters as the game built them
    float scale=1.0f;          // +0x30
    const edf41c::SgoNode* custom=nullptr;   // a tag-1 custom parameter (a node pointer): a persistent copy (null: none)
    // Any other kind of handle (the colonists' solid rounds' are tag 2): a copy the game's own handle table made (the copy the
    // game's parameter copy 0x32EF10 makes), copied again into each round's parameters (their destructor ends that one).
    alignas(8) unsigned char customHandle[0x10]{};
    std::uint16_t customTag=0xFFFF;
    int pellets=1,burst=1,gapFrames=4,coolFrames=60;
    float spread=0.0f,reach=100.0f;
    ULONGLONG since=0,until=0;
    ObjRef owner;
    int ring=0;
    float matrix[16]{};
    int burstLeft=0;
    ULONGLONG nextShot=0;
    std::uint64_t rng=0x9E3779B97F4A7C15ull;
    std::uint16_t serial=0;    // the host's for it (online: the gun's name on every machine; 0 offline)
    unsigned char ownerId[32]{};   // floating: its owner's native ID (online)
    ULONGLONG askedAt=0;       // a peer: when it last asked the host for it
    int shots=0;               // rounds fired (the log's first and every kShotLogEvery-th)
    bool drawn=false;          // its model drawn floating once (logged)
    char what[24]{};
};
Gun guns[kMostGuns];
// What a round that follows its muzzle (+0x70) reads, per gun slot: never freed (a round may outlive its gun), 16-aligned.
alignas(16) float muzzles[kMostGuns][16];
// The colonists watched for their deaths.
struct Watched { ObjRef c; unsigned char id[32]; bool hasId=false,dead=false; ULONGLONG deadAt=0; };
Watched watched[kMostWatched];
// A peer: the host's drops not taken yet (the colonist not dead here yet, or not found), and an owner told meanwhile.
struct Drop { bool on=false; std::uint16_t serial=0; std::uint8_t index=0; unsigned char id[32]{}; ULONGLONG heardAt=0;
              bool owned=false; unsigned char ownerId[32]{}; std::uint32_t leftMs=0; };
Drop drops[kMostDrops];
// What goes out (the host's to every peer, a peer's pick to the host).
struct Outgoing { gravity_net::Gun m; bool toHost; };
Outgoing outgoing[kMostOut];
int outCount=0;
bool netReady=false,netHost=false;
std::uint32_t netPeers=0,hostPeer=0,netOrigin=1;
std::uint32_t hostOrigin=0;   // a peer: the host's run its guns are of (0: none heard of this mission)
std::uint16_t nextSerial=0;
bool ok=false,active=false;
int missionLogs=0;
using UpdateFn=void(__fastcall*)(void*,void*);
UpdateFn dropUpdate=nullptr;

using ModePathFn=const wchar_t*(__fastcall*)(void*,unsigned);
using DropMakeFn=void*(__fastcall*)(void*,void*,const float*,void*);
using ModelMatrixFn=void(__fastcall*)(void*,const float*);
using ParamFn=void(__fastcall*)(void*);
using WeakAssignFn=void*(__fastcall*)(void*,const void*);
using SpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);
using DeleteFn=void(__fastcall*)(void*);
using VariantFn=void(__fastcall*)(void*,void*);

float Rand(std::uint64_t& s) noexcept {
    s=s*6364136223846793005ull+1442695040888963407ull;
    return static_cast<float>((s>>40)&0xFFFFFF)/16777216.0f;
}
float Len(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }

// The matrix facing `dir` (rows right, up, forward, position).
void Facing(const float* dir,const float* pos,float* m) noexcept {
    float f[3]={dir[0],dir[1],dir[2]};
    float fl=Len(f);
    if(fl<1e-4f){f[0]=0.0f;f[1]=0.0f;f[2]=1.0f;fl=1.0f;}
    for(float& c:f)c/=fl;
    float r[3]={f[2],0.0f,-f[0]};
    float rl=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(rl<1e-4f){r[0]=1.0f;r[2]=0.0f;rl=1.0f;}
    r[0]/=rl;r[2]/=rl;
    const float u[3]={f[1]*r[2]-f[2]*r[1],f[2]*r[0]-f[0]*r[2],f[0]*r[1]-f[1]*r[0]};
    const float out[16]={r[0],r[1],r[2],0.0f,u[0],u[1],u[2],0.0f,f[0],f[1],f[2],0.0f,pos[0],pos[1],pos[2],1.0f};
    std::memcpy(m,out,sizeof(out));
}

// A pinned reference's object still there: its control block's strong count (the pin keeps the block itself), the same
// object, not deleted (dead or alive).
bool Present(const ObjRef& r) noexcept {
    __try {
        if(!r.obj || !r.ctrl || At<LONG>(r.ctrl,8)<=0)return false;
        const auto* o=static_cast<const unsigned char*>(r.obj);
        return r.Is(o) && !(o[npcmark::kFlags]&npcmark::kDeleted);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Unpin(ObjRef& r) noexcept { npcmark::Assign(r,ObjRef{}); }

Gun* GunOfDrop(const void* drop) noexcept {
    for(auto& g:guns)if(g.state!=State::free && g.drop.obj==drop && g.drop.Is(drop))return &g;
    return nullptr;
}

// The dropped gun's update (slot 5): ours on the ground are kept (their counter held); ours floating are drawn where the
// plugin flies them, the physics steps skipped; any other is the game's.
void __fastcall DropUpdateHook(void* self,void* ctx) {
    Gun* g=nullptr;
    __try { g=active ? GunOfDrop(self) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){g=nullptr;}
    if(g) {
        auto* d=static_cast<unsigned char*>(self);
        Put<std::int32_t>(d,kDropCounter,0);
        if(g->state==State::floating) {
            std::memcpy(d+kMatrix,g->matrix,64);
            reinterpret_cast<ModelMatrixFn>(image+kModelMatrix)(d+kDropModel,reinterpret_cast<const float*>(d+kMatrix));
            if(!g->drawn){g->drawn=true;Log("FLOATGUN %s drawn floating at (%.0f,%.0f,%.0f)",g->what,g->matrix[12],g->matrix[13],g->matrix[14]);}
            return;
        }
    }
    dropUpdate(self,ctx);
}

// (A slot's customHandle copy is never destroyed: it may hold a reference into the mission's SGO, and the slot outlives the
// mission's teardown. A handle a gun, a few a mission.)
void Send(const gravity_net::Gun& m,bool toHost) noexcept {
    if(!netReady || outCount>=kMostOut)return;
    gravity_net::Gun x=m;
    x.origin=netOrigin;
    outgoing[outCount++]=Outgoing{x,toHost};
}
// The host's: every peer told the gun is gone (replaced, never picked up, its time up).
void Gone(const Gun& g) noexcept {
    if(!g.serial || !netReady || !netHost)return;
    gravity_net::Gun m{};
    m.kind=gravity_net::GunKind::gone;m.serial=g.serial;
    Send(m,false);
}
void Free(Gun& g) noexcept {
    Gone(g);
    __try {
        if(Present(g.drop))reinterpret_cast<DeleteFn>(image+kDelete)(const_cast<void*>(g.drop.obj));
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    Unpin(g.drop);
    g=Gun{};
}

Gun* NewGun() noexcept {
    for(auto& g:guns)if(g.state==State::free)return &g;
    // Full: the oldest one lying on the ground gives way (never one floating).
    Gun* oldest=nullptr;
    for(auto& g:guns)if(g.state==State::ground && (!oldest || g.since<oldest->since))oldest=&g;
    if(oldest)Free(*oldest);
    return oldest;
}

// The persistent copy of a parameter block's custom parameter when it is a node pointer (tag 1); null when it holds none or
// another kind (`other`).
const edf41c::SgoNode* CustomOf(const unsigned char* param,bool* other) noexcept {
    *other=false;
    const std::uint16_t tag=At<std::uint16_t>(param,kPCustomTag);
    if(tag==kTagNone)return nullptr;
    if(tag!=kTagNode){*other=true;return nullptr;}
    const auto* n=At<const edf41c::SgoNode*>(param,kPCustom);
    return n && Readable(n,sizeof(edf41c::SgoNode)) ? edf41c::Persist(n) : nullptr;
}
// The SGO value handle at `src` (tag at +0x10) copied into `dst` by the game's per-tag copy (0x179E1C0; 0x4A4FC0 / 0x32EF10's
// way). False: a tag past the table, or none.
bool CopyHandle(void* dst,const void* src,std::uint16_t tag) noexcept {
    if(tag==kTagNone || tag>64)return false;
    const auto copy=At<VariantFn>(image,kVariantCopy+static_cast<std::size_t>(tag)*8);
    if(!copy)return false;
    const void* from=src;
    copy(dst,&from);
    return true;
}

// The numbers of the game's own parameter block for this gun's rounds, its factory and its rhythm.
bool Snapshot(Gun& g,void* factory,const unsigned char* param,int pellets,int burst,int gap,float spread,int cool,const char* what) noexcept {
    if(!factory || !Readable(param,kParamSize))return false;
    bool other=false;
    const edf41c::SgoNode* custom=CustomOf(param,&other);
    if(other) {   // another kind of handle: the game's own copy of it
        const std::uint16_t tag=At<std::uint16_t>(param,kPCustomTag);
        if(!CopyHandle(g.customHandle,param+kPCustom,tag)) {
            Log("FLOATGUN %s: its rounds' custom parameter (tag %u) cannot be copied: not taken",what,tag);
            return false;
        }
        g.customTag=tag;
    }
    g.factory=factory;
    std::memcpy(g.numbers,param+kPTeam,sizeof(g.numbers));
    g.scale=At<float>(param,kPScale);
    g.custom=custom;
    auto fix=[](int v,int lo,int hi){return v<lo ? lo : v>hi ? hi : v;};
    g.pellets=fix(pellets,1,64);g.burst=fix(burst,1,kMostBurst);g.gapFrames=fix(gap,1,600);g.coolFrames=fix(cool,1,1800);
    g.spread=std::isfinite(spread) && spread>0.0f ? (spread<0.3f ? spread : 0.3f) : 0.0f;
    const float speed=At<float>(param,kPSpeed),life=static_cast<float>(At<std::int32_t>(param,kPLife));
    const float reach=speed*life;
    g.reach=std::isfinite(reach) && reach>0.0f ? (reach>kMostReach ? kMostReach : reach<kLeastReach ? kLeastReach : reach) : 100.0f;
    strncpy_s(g.what,what,_TRUNCATE);
    return true;
}

// The dropped gun of colonist gun `w`, the game's own way (0x4A4FC0): its parameter {vtable, zeros, the gun's SGO value},
// made at the gun's matrix. What the factory hands back is a weak reference (0x49CEF9 gives the object to the scene, which
// owns it; 0x49CFB2 takes a weak count and drops its own strong one): kept as the gun's pin on the drop's control block
// (Unpin lets it go), the object the scene's (the first run let a strong count go here and freed the scene's object: heap
// corruption, 2026-10-20).
ObjRef MakeDrop(unsigned char* w) noexcept {
    alignas(16) unsigned char param[0x50]{};
    const void* vt=image+kDropParamVtable;
    std::memcpy(param,&vt,8);
    const std::uint16_t tag=At<std::uint16_t>(w,kGunSgo+0x10);
    Put<std::uint16_t>(param,0x40,tag);
    if(tag!=kTagNone) {
        const auto copy=At<VariantFn>(image,kVariantCopy+static_cast<std::size_t>(tag)*8);
        void* src=w+kGunSgo;
        copy(param+0x30,&src);
    }
    void* out[2]={nullptr,nullptr};
    void* mgr=At<void*>(image,kSceneMgr);
    reinterpret_cast<DropMakeFn>(image+kDropMake)(mgr,out,reinterpret_cast<const float*>(w+kMatrix),param);
    if(tag!=kTagNone) {
        unsigned char scratch[8];
        At<VariantFn>(image,kVariantDtor+static_cast<std::size_t>(tag)*8)(param+0x30,scratch);
    }
    auto* obj=static_cast<unsigned char*>(out[0]);
    auto* ctrl=static_cast<unsigned char*>(out[1]);
    const bool made=obj && ctrl && At<const void*>(obj,0)==image+kDropVtable && At<LONG>(ctrl,8)>0;
    const ObjRef ref=made ? ObjRef::Of(obj) : ObjRef{};
    if(made && ref.ctrl==ctrl)return ref;   // the factory's weak count is the gun's now
    if(ctrl && InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12))==0)   // the weak count: the block's own end
        At<void(__fastcall* const*)(void*)>(ctrl,0)[1](ctrl);
    if(made)return {};
    if(!made)Log("FLOATGUN the drop factory gave %p (vtable %p, ctrl %p)",obj,obj ? At<const void*>(obj,0) : nullptr,ctrl);
    return ref;
}

// Gun `i` (its weak list's order, the same on every machine) of dead colonist `c`, dropped where it was. Null: none there (shot
// off already: the game dropped it, 0x4A4C80 -> 0x4A4FC0; not a gun of the two classes; no drop made).
Gun* TakeGun(unsigned char* c,std::size_t i) noexcept {
    const auto rows=At<const unsigned char*>(c,kGunRows);
    const auto count=At<std::size_t>(c,kGunCount);
    if(!rows || count==0 || count>8 || i>=count || !Readable(rows,count*16))return nullptr;
    auto* w=At<unsigned char*>(rows+i*16,0);
    const auto* ctrl=At<const unsigned char*>(rows+i*16,8);
    if(!w || !ctrl || !Readable(ctrl,24) || At<LONG>(ctrl,8)<=0 || At<const void*>(ctrl,16)!=w)return nullptr;
    if(!Readable(w,kGunMuzzleCount+8) || At<const unsigned char*>(w,kGunOwner)!=c)return nullptr;
    if(!w[kGunEnabled])return nullptr;
    const auto vt=At<const unsigned char*>(w,0);
    if(vt!=image+kGunBasicVtable && vt!=image+kGunHeavyVtable)return nullptr;
    Gun* g=NewGun();
    if(!g)return nullptr;
    *g=Gun{};
    if(!Snapshot(*g,At<void*>(w,kGunFactory),w+kGunParam,At<std::int32_t>(w,kGunPellets),At<std::int32_t>(w,kGunBurst),
                 At<std::int32_t>(w,kGunGap),At<float>(w,kGunSpread),At<std::int32_t>(w,kGunCool),
                 vt==image+kGunHeavyVtable ? "heavy colonist gun" : "colonist gun")) {
        *g=Gun{};
        return nullptr;
    }
    g->drop=MakeDrop(w);
    if(!g->drop){*g=Gun{};Log("FLOATGUN a colonist's gun: no drop made");return nullptr;}
    g->state=State::ground;g->since=GameMs();
    std::memcpy(g->at,w+kPosition,12);
    g->rng^=reinterpret_cast<std::uintptr_t>(w);
    if(missionLogs++<64)Log("FLOATGUN %s dropped at (%.0f,%.0f,%.0f): %d pellet(s), bursts of %d every %d frames, %d frames after; %.2f damage, reach %.0f m",
                            g->what,g->at[0],g->at[1],g->at[2],g->pellets,g->burst,g->gapFrames,g->coolFrames,
                            At<float>(g->numbers,kPDamage-kPTeam),g->reach);
    return g;
}

// The host's (offline: this machine's): a colonist that died, each of its guns dropped, named, and told to the peers.
void TakeColonist(unsigned char* c,const Watched& w) noexcept {
    const auto count=At<std::size_t>(c,kGunCount);
    for(std::size_t i=0;i<count && i<8;++i) {
        Gun* g=TakeGun(c,i);
        if(!g)continue;
        if(!netReady || !w.hasId)continue;   // offline (or no ID to name it by): this machine's alone
        if(++nextSerial==0)nextSerial=1;
        g->serial=nextSerial;
        gravity_net::Gun m{};
        m.kind=gravity_net::GunKind::drop;m.serial=g->serial;m.index=static_cast<std::uint8_t>(i);
        std::memcpy(m.who,w.id,32);std::memcpy(m.at,g->at,12);
        Send(m,false);
    }
}

Gun* GunOf(std::uint16_t serial) noexcept {
    if(!serial)return nullptr;
    for(auto& g:guns)if(g.state!=State::free && g.serial==serial)return &g;
    return nullptr;
}
// The nearest enemy of `team` within `reach` of `from` the map does not hide.
struct Aim { const float* from; float reach; const void* object; float at[3]; float d2; };
void SeeEnemy(void* ctx,const void* object,const float* lock) noexcept {
    auto& a=*static_cast<Aim*>(ctx);
    if(!lock)return;
    const float d[3]={lock[0]-a.from[0],lock[1]-a.from[1],lock[2]-a.from[2]};
    const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(d2>a.reach*a.reach || (a.object && d2>=a.d2))return;
    a.object=object;a.d2=d2;std::memcpy(a.at,lock,12);
}
bool TargetFor(const float* from,float reach,std::int32_t team,const void** object,float* at) noexcept {
    Aim a{from,reach,nullptr,{},0.0f};
    VisitEnemiesOf(team,&SeeEnemy,&a);
    if(!a.object)return false;
    float hit[3];
    const float wall=MapRay(from,a.at,hit);
    if(wall>=0.0f) {
        const float d[3]={a.at[0]-from[0],a.at[1]-from[1],a.at[2]-from[2]};
        if(wall<Len(d)-2.0f)return false;   // the map in between
    }
    *object=a.object;std::memcpy(at,a.at,12);
    return true;
}

void FireOne(Gun& g,const unsigned char* soldier,const void* target,const float* at) noexcept {
    const float* p=g.matrix+12;
    float dir[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
    const float dl=Len(dir);
    if(!(dl>0.5f))return;
    for(float& c:dir)c/=dl;
    void* mgr=At<void*>(image,kSceneMgr);
    if(!mgr)return;
    for(int k=0;k<g.pellets;++k) {
        float f[3]={dir[0],dir[1],dir[2]};
        if(g.spread>0.0f) {   // a cone of half-angle `spread`
            float m[16];Facing(dir,p,m);
            const float tilt=g.spread*std::sqrt(Rand(g.rng)),spin=6.2831853f*Rand(g.rng);
            const float a=std::sin(tilt)*std::cos(spin),b=std::sin(tilt)*std::sin(spin),c=std::cos(tilt);
            for(int i=0;i<3;++i)f[i]=m[8+i]*c+m[i]*a+m[4+i]*b;
        }
        alignas(16) float m[16];
        const float muzzlePos[3]={p[0]+dir[0]*0.8f,p[1]+dir[1]*0.8f,p[2]+dir[2]*0.8f};
        Facing(f,muzzlePos,m);
        float* const follow=muzzles[&g-guns];
        std::memcpy(follow,m,64);
        alignas(16) unsigned char param[kParamSize];
        reinterpret_cast<ParamFn>(image+kParamCtor)(param);
        std::memcpy(param+kPTeam,g.numbers,sizeof(g.numbers));
        const std::int32_t side=At<std::int32_t>(soldier,kTeam);   // the round's team (its parameter block's), not the soldier's own
        Put<std::int32_t>(param,kPTeam,side);
        if(std::isfinite(g.scale) && g.scale>0.0f)Put<float>(param,kPScale,g.scale);
        reinterpret_cast<WeakAssignFn>(image+kWeakAssign)(param+kPOwner,soldier+kSelf);
        // The target is a lock point, not the object (0x4A0100 hands the round 0x548EF0's: the object's lock point 0, a weak
        // reference out of its array +0x368, count +0x378).
        if(target && Readable(static_cast<const unsigned char*>(target)+kLockPoints,kLockCount-kLockPoints+8) &&
           At<std::uint64_t>(target,kLockCount)>0) {
            const auto* points=At<const unsigned char*>(target,kLockPoints);
            if(points && Readable(points,16) && At<const void*>(points,8))
                reinterpret_cast<WeakAssignFn>(image+kWeakAssign)(param+kPTarget,points);
        }
        std::memcpy(param+kPMuzzle,&follow,8);
        if(g.custom) {
            std::memcpy(param+kPCustom,&g.custom,8);
            Put<std::uint16_t>(param,kPCustomTag,kTagNode);
        } else if(g.customTag!=kTagNone && At<std::uint16_t>(param,kPCustomTag)==kTagNone &&
                  CopyHandle(param+kPCustom,g.customHandle,g.customTag)) {
            Put<std::uint16_t>(param,kPCustomTag,g.customTag);
        }
        Put<std::uint32_t>(param,kPSeed,static_cast<std::uint32_t>(Rand(g.rng)*65535.0f));
        reinterpret_cast<SpawnFn>(image+kSpawn)(mgr,m,g.factory,param);
        reinterpret_cast<ParamFn>(image+kParamDtor)(param);
    }
}

// One floating gun's frame: its place on the ring, its aim, its fire.
void Fly(Gun& g,const unsigned char* soldier,int ring,int ringCount,ULONGLONG ms,float dt) noexcept {   // `soldier`: its owner
    const float* pp=reinterpret_cast<const float*>(soldier+kPosition);
    const float t=static_cast<float>(ms%600000)*0.001f;
    const float angle=t*kRingTurn+6.2831853f*static_cast<float>(ring)/static_cast<float>(ringCount>0 ? ringCount : 1);
    const float goal[3]={pp[0]+std::cos(angle)*kRingRadius,pp[1]+kRingHigh+std::sin(t*2.0f+ring)*kBob,pp[2]+std::sin(angle)*kRingRadius};
    float pos[3];
    std::memcpy(pos,g.matrix+12,12);
    if(!(Len(pos)>0.0f) || g.matrix[15]!=1.0f)std::memcpy(pos,goal,12);   // just picked up: there at once
    const float k=dt*kFollow<1.0f ? dt*kFollow : 1.0f;
    for(int i=0;i<3;++i)pos[i]+=(goal[i]-pos[i])*k;
    const void* target=nullptr;float at[3];
    const bool sees=TargetFor(pos,g.reach,At<std::int32_t>(soldier,kTeam),&target,at);
    float dir[3];
    if(sees){for(int i=0;i<3;++i)dir[i]=at[i]-pos[i];}
    else{const float* fwd=reinterpret_cast<const float*>(soldier+kMatrix+32);std::memcpy(dir,fwd,12);dir[1]=0.0f;}
    Facing(dir,pos,g.matrix);
    if(!sees){g.burstLeft=0;return;}
    if(ms<g.nextShot)return;
    if(g.burstLeft<=0)g.burstLeft=g.burst;
    FireOne(g,soldier,target,at);
    if(g.shots++%kShotLogEvery==0) {
        const float* p=g.matrix+12;
        const float d[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
        Log("FLOATGUN %s shot %d at %p, %.0f m off (%d pellet(s))",g.what,g.shots,target,Len(d),g.pellets);
    }
    --g.burstLeft;
    g.nextShot=ms+static_cast<ULONGLONG>((g.burstLeft>0 ? g.gapFrames : g.coolFrames)*1000/60);
}

// It floats round `owner` (`ownerId` its native ID online) from `ms`, for `leftMs` (0: the mission).
void Float(Gun& g,const unsigned char* owner,const unsigned char* ownerId,ULONGLONG ms,ULONGLONG leftMs) noexcept {
    g.state=State::floating;g.owner=ObjRef::Of(owner);g.since=ms;
    if(ownerId)std::memcpy(g.ownerId,ownerId,32);
    g.until=leftMs ? ms+leftMs : 0;
    std::memset(g.matrix,0,sizeof(g.matrix));
    g.nextShot=ms+500;g.burstLeft=0;
}
int FloatingOf(const unsigned char* owner) noexcept {
    int n=0;
    for(auto& g:guns)if(g.state==State::floating && g.owner.Is(owner))++n;
    return n;
}
// The host's (offline: this machine's): gun `g` given to `owner` (the oldest of theirs past FloatGunMost giving way), and
// every peer told.
void Give(Gun& g,const unsigned char* owner,const unsigned char* ownerId,ULONGLONG ms) noexcept {
    int floating=FloatingOf(owner);
    while(floating>=Cfg().floatGunMost) {
        Gun* oldest=nullptr;
        for(auto& o:guns)if(o.state==State::floating && o.owner.Is(owner) && (!oldest || o.since<oldest->since))oldest=&o;
        if(!oldest)break;
        Free(*oldest);--floating;
    }
    const ULONGLONG left=Cfg().floatGunSec>0.0f ? static_cast<ULONGLONG>(Cfg().floatGunSec*1000.0f) : 0;
    Float(g,owner,ownerId,ms,left);
    Log("FLOATGUN %s picked up%s: %d floating round them",g.what,owner==PlayerHuman() ? "" : " by a peer's player",floating+1);
    if(g.serial && netReady && ownerId) {
        gravity_net::Gun m{};
        m.kind=gravity_net::GunKind::owner;m.serial=g.serial;std::memcpy(m.who,ownerId,32);
        m.leftMs=static_cast<std::uint32_t>(left<gravity_net::kMostLeftMs ? left : gravity_net::kMostLeftMs);
        Send(m,false);
    }
}

void Step(ULONGLONG ms,float dt) noexcept {
    unsigned char* me=PlayerHuman();
    const bool host=OnlineHostOnly();
    unsigned char myId[32]{};
    const bool haveId=me && ReadNativeObjectId(me,myId);
    for(auto& g:guns) {
        if(g.state==State::free)continue;
        if(g.state==State::ground) {
            if(g.drop) {
                if(!Present(g.drop)){Unpin(g.drop);g=Gun{};continue;}   // the game deleted it (the mission's clean-up)
                std::memcpy(g.at,static_cast<const unsigned char*>(g.drop.obj)+kPosition,12);
            }
            if(ms-g.since>kGroundMs){Free(g);continue;}
            if(!me)continue;
            const float* pp=reinterpret_cast<const float*>(me+kPosition);
            const float d[3]={g.at[0]-pp[0],g.at[1]-pp[1],g.at[2]-pp[2]};
            const float reach=Cfg().floatGunReach;
            if(d[0]*d[0]+d[2]*d[2]>reach*reach || std::fabs(d[1])>(reach>kPickRise ? reach : kPickRise))continue;
            if(!g.serial)Give(g,me,nullptr,ms);   // this machine's alone (offline, or not told to anyone)
            else if(host){if(haveId)Give(g,me,myId,ms);}   // the peers are told whose it is by that ID
            else if(haveId && (!g.askedAt || ms-g.askedAt>=kAskGapMs)) {   // a peer: the host's to give
                g.askedAt=ms;
                gravity_net::Gun m{};
                m.kind=gravity_net::GunKind::pick;m.serial=g.serial;std::memcpy(m.who,myId,32);
                Send(m,true);
            }
            continue;
        }
        if(!g.owner.Is(g.owner.obj) || (g.until && ms>=g.until) || (g.drop && !Present(g.drop))){Free(g);continue;}
    }
    // Each owner's guns on their own ring.
    for(auto& g:guns) {
        if(g.state!=State::floating || !g.owner.Is(g.owner.obj))continue;
        const auto* owner=static_cast<const unsigned char*>(g.owner.obj);
        int ring=0,count=0;
        for(auto& o:guns) {
            if(o.state!=State::floating || o.owner.obj!=owner)continue;
            if(&o==&g)ring=count;
            ++count;
        }
        Fly(g,owner,ring,count,ms,dt);
    }
}

// The deaths among the colonists: the host (offline: this machine) takes their guns, a peer keeps them for the host's drops.
void Forget(Watched& w) noexcept { Unpin(w.c);w=Watched{}; }
void SeeColonist(void*,const void* object,const float*) noexcept {
    if(!object || At<const unsigned char*>(object,0)!=image+kColonistVtable)return;
    Watched* slot=nullptr;
    Watched* deadest=nullptr;   // full: the longest dead gives way (a live one is never dropped for another)
    for(auto& w:watched) {
        if(w.c.obj==object && w.c.Is(object))return;
        if(!slot && !w.c.obj)slot=&w;
        if(w.c.obj && w.dead && (!deadest || w.deadAt<deadest->deadAt))deadest=&w;
    }
    if(!slot && deadest){Forget(*deadest);slot=deadest;}
    if(!slot)return;
    const ObjRef ref=npcmark::Capture(object);
    if(!ref)return;
    *slot=Watched{};
    npcmark::Assign(slot->c,ref);
    slot->hasId=ReadNativeObjectId(static_cast<const unsigned char*>(object),slot->id);
}
void Watch(const unsigned char* me,ULONGLONG ms) noexcept {
    if(me)VisitEnemiesOf(At<std::int32_t>(me,kTeam),&SeeColonist,nullptr);   // each tick: none spawned and killed unseen
    const bool host=OnlineHostOnly();
    for(auto& w:watched) {
        if(!w.c.obj)continue;
        if(!Present(w.c)){Forget(w);continue;}
        auto* o=static_cast<unsigned char*>(const_cast<void*>(w.c.obj));
        if(!w.dead && o[kDead]) {
            w.dead=true;w.deadAt=ms;
            if(!w.hasId)w.hasId=ReadNativeObjectId(o,w.id);   // not given yet when first seen
            if(host)TakeColonist(o,w);
        }
        if(w.dead && ms-w.deadAt>kWatchDeadMs)Forget(w);
    }
}

// A peer: the host's drops taken from this machine's copy of their colonist (dead here, or kDropWaitMs on).
void TakeDrops(ULONGLONG ms) noexcept {
    for(auto& d:drops) {
        if(!d.on)continue;
        Watched* found=nullptr;
        for(auto& w:watched)if(w.c.obj && w.hasId && !std::memcmp(w.id,d.id,32) && Present(w.c)){found=&w;break;}
        if(!found) {
            if(ms-d.heardAt>kDropLostMs){Log("FLOATGUN the host's gun %u: its colonist is not here",d.serial);d=Drop{};}
            continue;
        }
        if(!found->dead && ms-d.heardAt<kDropWaitMs)continue;
        Gun* g=TakeGun(static_cast<unsigned char*>(const_cast<void*>(found->c.obj)),d.index);
        if(!g){Log("FLOATGUN the host's gun %u: not taken here",d.serial);d=Drop{};continue;}
        g->serial=d.serial;
        if(d.owned) {   // given while it was not here yet
            MarkIdentities ids{};
            if(ResolveMarkIdentities(d.ownerId,nullptr,&ids) && ids.player)
                Float(*g,static_cast<const unsigned char*>(ids.player.obj),d.ownerId,ms,d.leftMs);
        }
        d=Drop{};
    }
}

ULONGLONG lastMs=0;
}  // namespace

bool InstallFloatGuns() noexcept {
    ok=Matches(kModePath,kModePathSig,sizeof(kModePathSig)) && Matches(kRowStore,kRowStoreSig,sizeof(kRowStoreSig)) &&
       Matches(kDropMake,kDropMakeSig,sizeof(kDropMakeSig)) && Matches(kDropUpdate,kDropUpdateSig,sizeof(kDropUpdateSig)) &&
       Matches(kModelMatrix,kModelMatrixSig,sizeof(kModelMatrixSig)) && Matches(kParamCtor,kParamCtorSig,sizeof(kParamCtorSig)) &&
       Matches(kParamDtor,kParamDtorSig,sizeof(kParamDtorSig)) && Matches(kWeakAssign,kWeakAssignSig,sizeof(kWeakAssignSig)) &&
       Matches(kSpawn,kSpawnSig,sizeof(kSpawnSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
    if(ok) {
        void** slot=reinterpret_cast<void**>(image+kDropVtable)+5;
        if(*slot==image+kDropUpdate && PatchVtableSlot(slot,image+kDropUpdate,reinterpret_cast<void*>(&DropUpdateHook))) {
            dropUpdate=reinterpret_cast<UpdateFn>(image+kDropUpdate);
        } else ok=false;
    }
    Log("HOOK float guns: %s",ok ? "on (the dropped gun's update, the bullets' factory)" : "the code is not as expected: off");
    return ok;
}

void FloatGunsMissionStart() noexcept {
    // The last mission's objects are gone: nothing to delete, only the pins on their control blocks let go (the pins kept
    // the blocks themselves).
    for(auto& g:guns){Unpin(g.drop);g=Gun{};}
    for(auto& w:watched)Forget(w);
    for(auto& d:drops)d=Drop{};
    outCount=0;nextSerial=0;missionLogs=0;lastMs=0;hostOrigin=0;
    std::uint32_t origin=0;   // this mission's run: a peer tells the host's messages of the last one from this one's
    if(rand_s(&origin))origin=static_cast<std::uint32_t>(GetTickCount64());
    netOrigin=origin|1u;
    active=false;
    const int mode=Cfg().floatGuns;
    if(!ok || mode<=0)return;
    wchar_t dir[256]=L"";
    __try {
        auto* gs=At<unsigned char*>(image,kGameStatus);
        if(gs && Readable(gs+kRowAt,4)) {
            const unsigned row=At<unsigned>(gs,kRowAt);
            const wchar_t* p=reinterpret_cast<ModePathFn>(image+kModePath)(gs+kModeCfg,row);
            if(p && Readable(p,2))wcsncpy_s(dir,p,_TRUNCATE);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){dir[0]=0;}
    const std::size_t n=std::wcslen(dir);
    const wchar_t* tail=L"EDF5_OLD_SCRIPT/M099";
    const std::size_t tn=std::wcslen(tail);
    bool last=n>=tn && _wcsicmp(dir+n-tn,tail)==0;
    if(last && n>tn && dir[n-tn-1]!=L'/' && dir[n-tn-1]!=L'\\')last=false;
    active=mode==2 || last;
    Log("FLOATGUN mission %ls: %s",dir[0] ? dir : L"(unknown)",active ? (last ? "EDF5's last mission, float guns on" : "float guns on (every mission)") : "off");
}

void FloatGunTick() noexcept {
    if(!active)return;
    const unsigned char* soldier=PlayerHuman();
    const ULONGLONG ms=GameMs();
    const float dt=lastMs && ms>lastMs ? static_cast<float>(ms-lastMs)*0.001f : 1.0f/60.0f;
    lastMs=ms;
    __try { Watch(soldier,ms); } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN watching the colonists faulted");}
    __try { TakeDrops(ms); } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN taking the host's drops faulted");}
    __try { Step(ms,dt<0.1f ? dt : 0.1f); } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN step faulted");}
}

void UpdateFloatGunNetwork(const CommandNetworkContext& context,std::uint64_t) noexcept {
    if(!context.ready || !context.send || !context.peerCount || context.peerCount>gravity_net::kMaxPeers) {
        if(netReady)ResetFloatGunNetwork();
        return;
    }
    if(!netReady || context.peerCount!=netPeers || context.host!=netHost || context.hostPeer!=hostPeer) {
        ResetFloatGunNetwork();
        netReady=true;netPeers=context.peerCount;netHost=context.host;hostPeer=context.hostPeer;
    }
    unsigned char bytes[gravity_net::kGunWire];
    for(int i=0;i<outCount;++i) {
        if(!gravity_net::Encode(outgoing[i].m,bytes,sizeof(bytes)))continue;
        if(outgoing[i].toHost){if(hostPeer)context.send(hostPeer,bytes,sizeof(bytes));}
        else for(std::uint32_t p=1;p<=netPeers;++p)context.send(p,bytes,sizeof(bytes));
    }
    outCount=0;
}

bool ReceiveFloatGunNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(!gravity_net::OwnedGun(bytes,size))return false;
    gravity_net::Gun m;
    if(!netReady || !active || !peer || peer>netPeers || !puid || !std::memchr(puid,0,65) || !gravity_net::Decode(bytes,size,m))return true;
    const ULONGLONG ms=GameMs();
    __try {
        if(netHost) {   // the host takes asks alone: the first for a gun still lying there gets it
            if(m.kind!=gravity_net::GunKind::pick)return true;
            Gun* g=GunOf(m.serial);
            if(!g || g->state!=State::ground)return true;
            MarkIdentities ids{};
            if(!ResolveMarkIdentities(m.who,nullptr,&ids) || !ids.player || !ids.puid || !SupportCommandRequesterMatches(ids.puid,puid)) {
                Log("FLOATGUN peer %u asked for gun %u for a player not its own (or not found): refused",peer,m.serial);
                return true;
            }
            Give(*g,static_cast<const unsigned char*>(ids.player.obj),m.who,ms);
            return true;
        }
        if(peer!=hostPeer)return true;   // a peer takes the host's word alone
        if(m.origin!=hostOrigin) {   // another run of the host's (a new mission, a late message of the last): its guns alone
            if(hostOrigin)for(auto& g:guns)if(g.serial)Free(g);
            for(auto& d:drops)d=Drop{};
            hostOrigin=m.origin;
        }
        switch(m.kind) {
        case gravity_net::GunKind::drop: {
            if(GunOf(m.serial))return true;
            for(auto& d:drops)if(d.on && d.serial==m.serial)return true;
            Drop* d=nullptr;
            for(auto& x:drops)if(!x.on){d=&x;break;}
            if(!d) {   // full: the oldest gives way
                d=&drops[0];
                for(auto& x:drops)if(x.heardAt<d->heardAt)d=&x;
            }
            *d=Drop{};
            d->on=true;d->serial=m.serial;d->index=m.index;std::memcpy(d->id,m.who,32);d->heardAt=ms;
            break;
        }
        case gravity_net::GunKind::owner: {
            Gun* g=GunOf(m.serial);
            if(!g) {
                for(auto& d:drops)if(d.on && d.serial==m.serial){d.owned=true;std::memcpy(d.ownerId,m.who,32);d.leftMs=m.leftMs;}
                return true;
            }
            MarkIdentities ids{};
            if(!ResolveMarkIdentities(m.who,nullptr,&ids) || !ids.player){Free(*g);return true;}   // its owner not here: not shown
            Float(*g,static_cast<const unsigned char*>(ids.player.obj),m.who,ms,m.leftMs);
            if(missionLogs++<64)Log("FLOATGUN %s %u is %s's now",g->what,m.serial,ids.player.obj==PlayerHuman() ? "this player" : "a peer's player");
            break;
        }
        case gravity_net::GunKind::gone: {
            if(Gun* g=GunOf(m.serial))Free(*g);
            for(auto& d:drops)if(d.on && d.serial==m.serial)d=Drop{};
            break;
        }
        case gravity_net::GunKind::pick:break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN a network message faulted");}
    return true;
}

void ResetFloatGunNetwork() noexcept {   // the run's origin stays the mission's (FloatGunsMissionStart)
    outCount=0;
    netReady=false;netHost=false;netPeers=0;hostPeer=0;
}
}  // namespace crew
