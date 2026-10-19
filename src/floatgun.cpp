// The float guns (floatgun.h; docs/gravity-re.md §7, static RE 2026-10-20). What happens, and through what:
//  - The mission (FloatGunsMissionStart): the game status' row snapshot (GS+0x15000, written once at the mission's start,
//    0x70F103) and the mode table's field 1 of that row (0xE1560: the mission's directory, "app:/Mission/EDF5_OLD_SCRIPT/M099"
//    for EDF5's last); FloatGuns 1 takes that one alone, 2 every mission. Offline only (InSession: off).
//  - A kill (FloatGunNoteKill, from the damage function's hook, damagestats.cpp): a colonist (Humanoid_Basic, vtable
//    0x17C4D70: EDF5's Primer infantry, and the frogs) or a vehicle, queued; the next tick takes its gun while it is still
//    there (making objects inside the damage function is not safe):
//      a colonist's guns are scene objects of their own (HumanoidWeapon_Basic / _Heavy) in its weak list (+0x1120, count
//      +0x1130); each holds its bullet factory (+0x170), the bullet parameters the game built for that enemy (+0x180, 0x1E0
//      bytes: the class's damage times the enemy's level and the difficulty), its rhythm (+0x480: pellets, burst, frames
//      between shots, spread, frames after a burst) and its SGO (+0x100). A vehicle's main gun is its first mount's weapon
//      (+0x638, 0x48 a mount, the weapon at +0x10): a WeaponBase, factory +0x7F8, parameters +0x800, rhythm +0x364..0x374.
//  - The drop: a colonist's gun is the game's own dropped gun, HumanoidWeaponBase::Drop (the factory 0x49CE80 the game calls
//    when a colonist's arm is shot off, 0x4A4FC0, made the same way: the gun's SGO in its parameter, at the gun's matrix):
//    the gun's model falling and lying as the physics has it. Its own update (slot 5, 0x4A1730) deletes it after 600
//    frames; the plugin's hook keeps ours (its counter +0x830 held at 0). A vehicle's gun has no such model: its wreck is
//    where it is picked up, and it floats unseen (its rounds show it).
//  - The pick-up: the local player within kPickReach (horizontally; kPickRise up or down) of it. Up to FloatGunMost float
//    round them (another replaces the oldest).
//  - The flight: each its own place on a ring kRingRadius round the player kRingHigh over them, turning; the hook writes the
//    drop's matrix there and hands it to the model (0x1100B90, the update's own call), the physics steps skipped.
//  - The fire: at the nearest enemy of the player's side within its rounds' reach that the map does not hide, in the
//    gun's own rhythm, its bullets as the game fired them (0x4A0100's way: a fresh parameter block with the saved numbers,
//    the team the player's, the owner the player (kills theirs), the target (homing rounds), the custom parameter a
//    persistent copy (edf41classes.h Persist), then the bullet factory 0x1194280 from the gun's muzzle).
//  - The end: FloatGunSec after the pick-up (0: the mission), or a new mission; the drop deleted (0x118A1B0).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "floatgun.h"
#include "crew.h"
#include "edf41classes.h"
#include "gravity.h"
#include "memory.h"
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
constexpr std::uint32_t kTdVehicle=0x2006480,kTdWeapon=0x2045768;   // VehicleBase, WeaponBase
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
// A vehicle and its main gun (a WeaponBase).
// Its rhythm: FireSpreadWidth +0x364, FireCount (rounds a shot) +0x368, FireInterval (frames between shots) +0x36C, FireBurstCount
// +0x370 and FireBurstInterval +0x374 (a burst's shots, the frames between them), AmmoCount +0x248, ReloadTime +0x20C (WeaponInfo
// +0x1A0: +0xA8, +0x6C).
constexpr std::size_t kMounts=0x638,kMountCount=0x648,kMountStride=0x48,kMountWeapon=0x10,kVehFactory=0x7F8,kVehParam=0x800,
                      kVehSpread=0x364,kVehCount=0x368,kVehGap=0x36C,kVehBurst=0x370,kVehBurstGap=0x374,kVehAmmo=0x248,
                      kVehReload=0x20C;
// The bullet parameters (InitParam, 0x1E0): the owner weak +0x38, the target weak +0x60, the muzzle matrix it may follow
// +0x70, the team +0x90, the numbers +0x94..+0xE8 (speed m/frame, life frames, damage, ..., colour, gravity), the custom
// parameter's handle +0xE8 (tag +0xF8; 1: a node), the seed +0x1A0; +0x30 a damage scale.
constexpr std::size_t kParamSize=0x1E0,kPOwner=0x38,kPTarget=0x60,kPMuzzle=0x70,kPTeam=0x90,kPNumbers=0x94,kPCustom=0xE8,
                      kPCustomTag=0xF8,kPSeed=0x1A0,kPScale=0x30,kPSpeed=0x94,kPLife=0x98,kPDamage=0x9C;
constexpr std::uint16_t kTagNode=1,kTagNone=0xFFFF;
constexpr std::size_t kLockPoints=0x368,kLockCount=0x378;   // a game object's lock points: {LockPoint*, ctrl} each
constexpr std::size_t kDropCounter=0x830,kDropModel=0x210;

constexpr int kMostGuns=24,kMostPending=32;
constexpr float kPickReach=3.5f,kPickRise=3.0f;           // the stock item box's 3 m, a little more
constexpr float kRingRadius=3.2f,kRingHigh=3.0f,kRingTurn=0.6f,kBob=0.25f,kFollow=8.0f;
constexpr float kMostReach=400.0f,kLeastReach=30.0f;
constexpr ULONGLONG kGroundMs=120000;                      // a gun no one picked up stays this long
constexpr int kMostBurst=200;

enum class Source : std::uint8_t { colonist, vehicle };
enum class State : std::uint8_t { free, ground, floating };
struct Gun {
    State state=State::free;
    Source source=Source::colonist;
    ObjRef drop;               // the dropped gun's model (none: a vehicle's)
    float at[3]{};             // on the ground: where it lies (a vehicle's: its wreck)
    void* factory=nullptr;
    unsigned char numbers[kPCustom-kPTeam]{};   // +0x90 .. +0xE8 of the parameters as the game built them
    float scale=1.0f;          // +0x30
    const edf41c::SgoNode* custom=nullptr;   // a persistent copy (null: none)
    int pellets=1,burst=1,gapFrames=4,coolFrames=60;
    float spread=0.0f,reach=100.0f;
    ULONGLONG since=0,until=0;
    ObjRef owner;
    int ring=0;
    float matrix[16]{};
    int burstLeft=0;
    ULONGLONG nextShot=0;
    std::uint64_t rng=0x9E3779B97F4A7C15ull;
    char what[24]{};
};
Gun guns[kMostGuns];
// What a round that follows its muzzle (+0x70) reads, per gun slot: never freed (a round may outlive its gun), 16-aligned.
alignas(16) float muzzles[kMostGuns][16];
ObjRef pending[kMostPending];
int pendingCount=0;
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
            return;
        }
    }
    dropUpdate(self,ctx);
}

void Free(Gun& g) noexcept {
    __try {
        if(g.drop && g.drop.Is(g.drop.obj))reinterpret_cast<DeleteFn>(image+kDelete)(const_cast<void*>(g.drop.obj));
    } __except(EXCEPTION_EXECUTE_HANDLER){}
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

// The persistent copy of a parameter block's custom parameter (a node handle: tag 1), null when it holds none / another kind.
const edf41c::SgoNode* CustomOf(const unsigned char* param,bool* other) noexcept {
    *other=false;
    const std::uint16_t tag=At<std::uint16_t>(param,kPCustomTag);
    if(tag==kTagNone)return nullptr;
    if(tag!=kTagNode){*other=true;return nullptr;}
    const auto* n=At<const edf41c::SgoNode*>(param,kPCustom);
    return n && Readable(n,sizeof(edf41c::SgoNode)) ? edf41c::Persist(n) : nullptr;
}

// The numbers of the game's own parameter block for this gun's rounds, its factory and its rhythm.
bool Snapshot(Gun& g,void* factory,const unsigned char* param,int pellets,int burst,int gap,float spread,int cool,const char* what) noexcept {
    if(!factory || !Readable(param,kParamSize))return false;
    bool other=false;
    const edf41c::SgoNode* custom=CustomOf(param,&other);
    if(other) {
        Log("FLOATGUN %s: its rounds' custom parameter is not a node (tag %u): not taken",what,At<std::uint16_t>(param,kPCustomTag));
        return false;
    }
    g.factory=factory;
    std::memcpy(g.numbers,param+kPTeam,sizeof(g.numbers));
    g.scale=At<float>(param,kPScale);
    g.custom=custom;
    auto fix=[](int v,int lo,int hi){return v<lo ? lo : v>hi ? hi : v;};
    g.pellets=fix(pellets,1,32);g.burst=fix(burst,1,kMostBurst);g.gapFrames=fix(gap,1,600);g.coolFrames=fix(cool,1,1800);
    g.spread=std::isfinite(spread) && spread>0.0f ? (spread<0.3f ? spread : 0.3f) : 0.0f;
    const float speed=At<float>(param,kPSpeed),life=static_cast<float>(At<std::int32_t>(param,kPLife));
    const float reach=speed*life;
    g.reach=std::isfinite(reach) && reach>0.0f ? (reach>kMostReach ? kMostReach : reach<kLeastReach ? kLeastReach : reach) : 100.0f;
    strncpy_s(g.what,what,_TRUNCATE);
    return true;
}

// The dropped gun of colonist gun `w`, the game's own way (0x4A4FC0): its parameter {vtable, zeros, the gun's SGO value},
// made at the gun's matrix; the shared_ptr it returns let go (the scene keeps its own).
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
    if(ctrl && InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+8))==0) {   // never the last: the scene holds it
        At<void(__fastcall* const*)(void*)>(ctrl,0)[0](ctrl);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12))==0)At<void(__fastcall* const*)(void*)>(ctrl,0)[1](ctrl);
        return {};
    }
    if(!obj || At<const void*>(obj,0)!=image+kDropVtable)return {};
    return ObjRef::Of(obj);
}

// A killed colonist's guns, each dropped where it was.
void TakeColonist(unsigned char* c) noexcept {
    const auto rows=At<const unsigned char*>(c,kGunRows);
    const auto count=At<std::size_t>(c,kGunCount);
    if(!rows || count==0 || count>8 || !Readable(rows,count*16))return;
    for(std::size_t i=0;i<count;++i) {
        auto* w=At<unsigned char*>(rows+i*16,0);
        const auto* ctrl=At<const unsigned char*>(rows+i*16,8);
        if(!w || !ctrl || !Readable(ctrl,24) || At<LONG>(ctrl,8)<=0 || At<const void*>(ctrl,16)!=w)continue;
        if(!Readable(w,kGunMuzzleCount+8) || At<const unsigned char*>(w,kGunOwner)!=c)continue;
        if(!w[kGunEnabled])continue;   // its arm was shot off: the game dropped this gun already (0x4A4C80 -> 0x4A4FC0)
        const auto vt=At<const unsigned char*>(w,0);
        if(vt!=image+kGunBasicVtable && vt!=image+kGunHeavyVtable)continue;
        Gun* g=NewGun();
        if(!g)return;
        *g=Gun{};
        if(!Snapshot(*g,At<void*>(w,kGunFactory),w+kGunParam,At<std::int32_t>(w,kGunPellets),At<std::int32_t>(w,kGunBurst),
                     At<std::int32_t>(w,kGunGap),At<float>(w,kGunSpread),At<std::int32_t>(w,kGunCool),
                     vt==image+kGunHeavyVtable ? "heavy colonist gun" : "colonist gun")) {
            *g=Gun{};
            continue;
        }
        g->drop=MakeDrop(w);
        if(!g->drop){*g=Gun{};Log("FLOATGUN a colonist's gun: no drop made");continue;}
        g->source=Source::colonist;g->state=State::ground;g->since=GameMs();
        std::memcpy(g->at,w+kPosition,12);
        g->rng^=reinterpret_cast<std::uintptr_t>(w);
        if(missionLogs++<64)Log("FLOATGUN %s dropped at (%.0f,%.0f,%.0f): %d pellet(s), bursts of %d every %d frames, %d frames after; %.2f damage, reach %.0f m",
                                g->what,g->at[0],g->at[1],g->at[2],g->pellets,g->burst,g->gapFrames,g->coolFrames,
                                At<float>(g->numbers,kPDamage-kPTeam),g->reach);
    }
}

// A wrecked vehicle's main gun (its first mount's), picked up at the wreck.
void TakeVehicle(unsigned char* v) noexcept {
    const auto mounts=At<const unsigned char*>(v,kMounts);
    const auto count=At<std::uint64_t>(v,kMountCount);
    if(!mounts || count==0 || count>16 || !Readable(mounts,static_cast<std::size_t>(count)*kMountStride))return;
    for(std::uint64_t i=0;i<count;++i) {
        auto* w=At<unsigned char*>(mounts+static_cast<std::size_t>(i)*kMountStride,kMountWeapon);
        if(!w || !Readable(w,kVehParam+kParamSize) || !gravity::IsA(w,kTdWeapon))continue;   // a WeaponBase
        void* factory=At<void*>(w,kVehFactory);
        if(!factory)continue;   // a gun with no round of its own (a marker, a support call)
        Gun* g=NewGun();
        if(!g)return;
        *g=Gun{};
        // A burst of FireBurstCount shots FireBurstInterval apart, FireInterval after it; a gun of one round a magazine (a
        // tank's cannon) waits its reload too.
        const int burst=At<std::int32_t>(w,kVehBurst)>1 ? At<std::int32_t>(w,kVehBurst) : 1;
        const int gap=burst>1 ? At<std::int32_t>(w,kVehBurstGap) : At<std::int32_t>(w,kVehGap);
        int cool=At<std::int32_t>(w,kVehGap);
        if(At<std::int32_t>(w,kVehAmmo)<=2 && At<std::int32_t>(w,kVehReload)>cool)cool=At<std::int32_t>(w,kVehReload);
        if(!Snapshot(*g,factory,w+kVehParam,At<std::int32_t>(w,kVehCount),burst,gap,At<float>(w,kVehSpread),cool,"vehicle gun")) {
            *g=Gun{};continue;
        }
        g->source=Source::vehicle;g->state=State::ground;g->since=GameMs();
        std::memcpy(g->at,v+kPosition,12);
        g->rng^=reinterpret_cast<std::uintptr_t>(v);
        if(missionLogs++<64)Log("FLOATGUN vehicle %p's gun left at its wreck (%.0f,%.0f,%.0f): %.2f damage, reach %.0f m",v,g->at[0],g->at[1],
                                g->at[2],At<float>(g->numbers,kPDamage-kPTeam),g->reach);
        return;   // its main gun only
    }
}

bool IsVehicle(const void* o) noexcept { return gravity::IsA(o,kTdVehicle); }

void TakePending() noexcept {
    const int n=pendingCount;
    pendingCount=0;
    for(int i=0;i<n;++i) {
        auto* o=static_cast<unsigned char*>(const_cast<void*>(pending[i].obj));
        __try {
            if(!pending[i].Is(o))continue;
            if(At<const unsigned char*>(o,0)==image+kColonistVtable)TakeColonist(o);
            else if(IsVehicle(o))TakeVehicle(o);
        } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN taking a killed %p's gun faulted",o);}
    }
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
        Put<std::int32_t>(param,kPTeam,At<std::int32_t>(soldier,kTeam));
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
        }
        Put<std::uint32_t>(param,kPSeed,static_cast<std::uint32_t>(Rand(g.rng)*65535.0f));
        reinterpret_cast<SpawnFn>(image+kSpawn)(mgr,m,g.factory,param);
        reinterpret_cast<ParamFn>(image+kParamDtor)(param);
    }
}

// One floating gun's frame: its place on the ring, its aim, its fire.
void Fly(Gun& g,const unsigned char* soldier,int ring,int ringCount,ULONGLONG ms,float dt) noexcept {
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
    --g.burstLeft;
    g.nextShot=ms+static_cast<ULONGLONG>((g.burstLeft>0 ? g.gapFrames : g.coolFrames)*1000/60);
}

void Step(unsigned char* soldier,ULONGLONG ms,float dt) noexcept {
    const float* pp=reinterpret_cast<const float*>(soldier+kPosition);
    int floating=0;
    for(auto& g:guns)if(g.state==State::floating && g.owner.Is(soldier))++floating;
    for(auto& g:guns) {
        if(g.state==State::free)continue;
        if(g.state==State::ground) {
            if(g.drop) {
                if(!g.drop.Is(g.drop.obj)){g=Gun{};continue;}   // the game deleted it (the mission's clean-up)
                std::memcpy(g.at,static_cast<const unsigned char*>(g.drop.obj)+kPosition,12);
            }
            if(ms-g.since>kGroundMs){Free(g);continue;}
            const float d[3]={g.at[0]-pp[0],g.at[1]-pp[1],g.at[2]-pp[2]};
            if(d[0]*d[0]+d[2]*d[2]>kPickReach*kPickReach || std::fabs(d[1])>kPickRise)continue;
            if(floating>=Cfg().floatGunMost) {   // the oldest of theirs gives way
                Gun* oldest=nullptr;
                for(auto& o:guns)if(o.state==State::floating && o.owner.Is(soldier) && (!oldest || o.since<oldest->since))oldest=&o;
                if(oldest){Free(*oldest);--floating;}
            }
            g.state=State::floating;g.owner=ObjRef::Of(soldier);g.since=ms;
            g.until=Cfg().floatGunSec>0.0f ? ms+static_cast<ULONGLONG>(Cfg().floatGunSec*1000.0f) : 0;
            std::memset(g.matrix,0,sizeof(g.matrix));
            g.nextShot=ms+500;g.burstLeft=0;
            ++floating;
            Log("FLOATGUN %s picked up: %d floating",g.what,floating);
            continue;
        }
        if(!g.owner.Is(g.owner.obj) || (g.until && ms>=g.until) || (g.drop && !g.drop.Is(g.drop.obj))){Free(g);continue;}
    }
    int ring=0;
    for(auto& g:guns) {
        if(g.state!=State::floating || !g.owner.Is(soldier))continue;
        Fly(g,soldier,ring++,floating,ms,dt);
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
    for(auto& g:guns)g=Gun{};   // the last mission's objects are gone: nothing to delete
    pendingCount=0;missionLogs=0;lastMs=0;
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

void FloatGunNoteKill(const void* object) noexcept {
    if(!active || !object || pendingCount>=kMostPending)return;
    __try {
        const auto vt=At<const unsigned char*>(object,0);
        if(vt!=image+kColonistVtable && !IsVehicle(object))return;
        pending[pendingCount++]=ObjRef::Of(object);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

void FloatGunTick() noexcept {
    if(!active)return;
    if(InSession())return;   // offline only: nothing here is replicated
    unsigned char* soldier=PlayerHuman();
    const ULONGLONG ms=GameMs();
    const float dt=lastMs && ms>lastMs ? static_cast<float>(ms-lastMs)*0.001f : 1.0f/60.0f;
    lastMs=ms;
    TakePending();
    if(!soldier)return;
    __try { Step(soldier,ms,dt<0.1f ? dt : 0.1f); } __except(EXCEPTION_EXECUTE_HANDLER){Log("FLOATGUN step faulted");}
}
}  // namespace crew
