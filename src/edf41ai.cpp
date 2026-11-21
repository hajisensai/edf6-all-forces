// EDF4.1 Hector's attack behaviour on EDF6_Berserker_Large (src/hector_ai.h has the rules and where they come from in
// EDF41.exe). Only an object whose SGO carries the key `edf41_hector` is touched (tools/make_edf41_objects.py writes
// it into every Hector variant); EDF6's own Berserkers go through unchanged.
//
//   edf41_hector: { ranges: [arms_l range, arms_r range], same_weapon: 0 | 1, ai: [720, 1440, 540, 720, 150, 20, 80] }
//
// The key is read through the game's own SGO accessors (the class reads its keys the same way, EDF.dll 0x34C99A):
// the object's tree is the member at +0xA8 and a handle is {tree, node index}. 0xD7480 (handle*, out*, wchar_t* key)
// is the child by name (a missing key gives tree == null), 0x53060 a node's child count, 0x52DE0 (handle*, i) the
// node index of child i, 0x52F30 a number node as float. The class never looks the key up, so it is read-only data.
//
// Hooks (EDF.dll, TimeDateStamp 0x678CCB46; each is a call site, RedirectCall verifies the callee it replaces):
//  - 0x34F2A0, the call of the base update 0x343DE0 in the Large's update (vtable 0x17ADA78 slot 5 = 0x34F280): the
//    per-frame tick of the back-off cycle, before the game's own update so this frame's walk sees it.
//  - 0x34D8D8, 0x34D955 and 0x34DA65, the calls of the walk 0x33E3B0 (MoveTo(this, &goal, a, b, speed, c): writes the
//    walk vector at +0x1290 as normalize(goal - position) * speed * clamp((dist - a) / (b / a), .., 1), nothing when
//    dist <= a, no division for a zero vector, 0x33E563 / 0x33E5DB) in the three walking states 0x34D890 (follow, the
//    fight: goal 0x3408D0 = a slot beside the target plus its lead, a = b = 0, speed 0.9..1.2 x linear_speed),
//    0x34D910 (to the point +0x4C0) and 0x34D9C0 (chase: a 1, b 20, speed 0.125), and 0x34D8FD, 0x34D96A and
//    0x34DA7A, their calls of FaceTarget 0x33E840 (the follow state faces the target, the others their goal).
//    A Hector with a target walks as 4.1's does at any distance (hector::Steer; EDF41.exe 0x2234B6..0x2238BF): it faces
//    the target (FaceHook) and its walk is along its own facing (the goal is its position +-100 along its matrix's z
//    row): forward at 1, backward at 1 in a back-off, 0 inside the stop radius (the class multiplies the walk vector
//    by MoveSpeed[0] = 6 units/s, 0x53D7CC), a = b = 0 (4.1 walks at full speed to the end). The class's own follow
//    state instead faced the target and walked to the slot beside it: the legs walked forward while the body slid
//    sideways (2026-10-18 M113: 43 % of the walking frames over 11 degrees off its facing) and one Hector walked on the
//    spot against a wall for 140 s. No target: the class's own walk.
//  - After the base update (the skeleton is built from the bones' locals at its start, 0x317B88 -> 0x1100B90, which
//    writes only the world matrices, bone record +0xB0), the arms aim as 4.1's weapons did (hector::ArmAim): the arm
//    bone's local (+0x70) becomes Rx(pitch) Ry(yaw) x the animation's, and its world and every bone below it are moved to
//    match now (the muzzle locators hang on the arm bone), so this frame's muzzles, weapons and draw see it whichever comes
//    first, the CAS's next write or the next build. An arm aims while its gun battery attacks: the condition the class's
//    own per-gun aim (0x34F900) runs under.
//    Then the weapons (SGO edf41_hector_weapons; Weapons below): each arm's weapon is an object of its own, as in 4.1 (the
//    plugin's class Hector41_Weapon / Hector401_Shield, an EventAnimationObject with the weapon's own model, ragdoll and
//    material animation), made once and put on its arm bone's aimed world matrix every frame; its own animation (its SGO's
//    edf41_weapon_anim, hector::StepOpenClose / StepRoll) is written into its own bones' locals and its skeleton rebuilt
//    from that matrix (0x1100B90, edf41c::ModelMatrix). And the head player (`head` of Hector.CAS) opens and closes as
//    4.1's head does (hector::StepHead; 0x1166FB0 finds the player, 0x116C2D0 sets its state). The functions only this
//    calls are checked by their first bytes (kAnimPrints): on another build it is all left off.
//  - 0x34FEFE (the gun loop of StartAttack(1) = 0x34FE90) and 0x34E123 (0x34E0D0): the call of the per-gun attack start
//    0x34E180(gun). An attack picks its arms once (PickArms) and the arms not picked are not started.
//  - The legs' animation (hector::PickGait / GaitRate, 4.1's 0x2220E0 / 0x2224E0): WalkingEnemyBase's walk state
//    (0x540710) and stand state (0x5405B0) set the main CAS player's state with 0x116BFC0(cas, player, state, flags)
//    at 0x540697 / 0x5406C1 / 0x5406DF / 0x540821 and its rate with 0x116DF20(cas, player, rate) at 0x5406F5 /
//    0x54083E; cas = object + 0xBD0. For a Hector the state is 4.1's for the yaw step (+0x1320), the eased velocity
//    (+0x1430, units/s, read in the body's frame: matrix rows +0x60 x, +0x80 z) and the walk vector (+0x1290 x MoveSpeed
//    +0xF94), out of the class's own state table (+0x1000 stand, +0x1004 turn_l, +0x1008 turn_r, +0x101C.. walk f b l r,
//    which the battle swaps for walk_battle_*, 0x53F230), and the rate 4.1's (v_walk_animation_speed: +0xFD4 x 60,
//    v_turn_animation_speed: +0xFD8, 0x53CFB1 / 0x53CFD1). Without it a Hector turning on the spot plays `stand`
//    (the stand state turns only past the clamp the walking states give FaceTarget) and slides round on its feet.
// The gun object is {GunBattery*, ...} with its owner at +0x10; the owner's guns are the vector of 16-byte
// {gun*, control} at +0x1AA8 (count +0x1AB8), in gun_control's order. The target is 0x30C0F0(owner) (null: none), its
// position the vec4 at +0x90 (the vec4 at *(*(+0x368)) + 0x10 when +0x378 is set), the owner's at +0x90.
#include "crew.h"
#include "edf41classes.h"
#include "enemy_control.h"
#include "hector_ai.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
constexpr std::size_t kBaseUpdate=0x343DE0;
constexpr std::size_t kBaseUpdateSite=0x34F2A0;
constexpr std::size_t kMoveTo=0x33E3B0;
constexpr std::size_t kMoveToSites[]={0x34D8D8,0x34D955,0x34DA65};
constexpr std::size_t kFaceTarget=0x33E840;   // FaceTarget(this, &point, limit): +0x12A0 / +0x12A4 (0x33E99B)
constexpr std::size_t kFaceSites[]={0x34D8FD,0x34D96A,0x34DA7A};
constexpr std::size_t kGunStart=0x34E180;
constexpr std::size_t kGunStartSites[]={0x34FEFE,0x34E123};
constexpr std::size_t kGetTarget=0x30C0F0;
constexpr std::size_t kChildByName=0xD7480;
constexpr std::size_t kChildCount=0x53060;
constexpr std::size_t kChildIndex=0x52DE0;
constexpr std::size_t kNodeFloat=0x52F30;
constexpr std::size_t kNodeText=0x551E0;       // const wchar_t* (handle*): a string node (0x53B832 reads InterpolateNode so)
constexpr std::size_t kCasPlayerByName=0x1166FB0;  // int (CasPlayers*, const wchar_t*): -1 none (0x33D392 "additive_action")
constexpr std::size_t kCasSetState=0x116C2D0;  // bool (CasPlayers*, int player, const wchar_t* state, int)
constexpr std::size_t kCasPlayState=0x116BFC0;  // bool (CasPlayers*, int player, int state, flags): by state index
constexpr std::size_t kCasSetRate=0x116DF20;    // void (CasPlayers*, int player, float rate): player +0x100
constexpr std::size_t kGaitStateSites[]={0x540697,0x5406C1,0x5406DF,0x540821};
constexpr std::size_t kGaitRateSites[]={0x5406F5,0x54083E};
// WalkingEnemyBase's fields the gait reads (0x53D560 and the constructor 0x53CD00..0x53CFD1)
constexpr std::size_t kAxisXAt=0x60,kAxisZAt=0x80,kYawStepAt=0x1320,kEasedVelocityAt=0x1430,kWalkVectorAt=0x1290,
                      kMoveSpeedAt=0xF94,kWalkAnimAt=0xFD4,kTurnAnimAt=0xFD8;
constexpr std::size_t kGaitStateAt[]={0x1000,0x101C,0x1020,0x1024,0x1028,0x1004,0x1008};  // hector::Gait order
// first bytes of the functions only the head animation and the weapons' SGO key read call: on any other build those stay off
struct Print { std::size_t rva; unsigned char bytes[8]; };
constexpr Print kAnimPrints[]={
    {kNodeText,{0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8}},
    {kCasPlayerByName,{0x48,0x89,0x5C,0x24,0x08,0x48,0x83,0x79}},
    {kCasSetState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kChildCount,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C}},
    {kChildIndex,{0x4C,0x8B,0x09,0x44,0x8B,0x51,0x08,0x44}},
};

constexpr std::size_t kTreeAt=0xA8;          // the object's SGO tree
constexpr std::size_t kPosAt=0x90;           // vec4
constexpr std::size_t kHasTargetAt=0x4A0;    // byte
constexpr std::size_t kGunsAt=0x1AA8;        // {gun*, control*}[]
constexpr std::size_t kGunCountAt=0x1AB8;
constexpr std::size_t kGunOwnerAt=0x10;
constexpr std::size_t kSkeletonAt=0x940;     // model component +0x8A0, skeleton = model + 0xA0 (0x53B86C, 0x317B7A)
constexpr std::size_t kDestroyedAt=0x2E8;    // byte
constexpr std::size_t kBoneSize=0x110;       // bone records at [skeleton+0x10], count u64 [skeleton+0x20]
constexpr std::size_t kBoneParentAt=0x10,kBoneLocalAt=0x70,kBoneWorldAt=0xB0;
// the gun battery's attack state, as 0x34F913..0x34F94E reads it: GunControl {GunBattery*, ...}
constexpr std::size_t kBatteryFlagsAt=0x2C0,kBatteryLeftAt=0x2E0,kBatteryCountAt=0x30;
constexpr std::size_t kBatteryChargeAt=0x2E8;  // frames of charge left (0x6D547A sets it from charge.time, 0x6D556A counts down)
// the CAS players (0x53BB17: +0xBD0, main player [+0xFD0]); each player 0x160 bytes at [+0x60], count [+0x70] (0x1166FB0
// reads them so); a player's clip clock +0xE8, length +0xF0 (edf41classes ClipNow, real-run with the dragon)
constexpr std::size_t kCasAt=0xBD0,kCasPlayers=0x60,kCasPlayerCount=0x70,kCasPlayerSize=0x160,kClipTime=0xE8,kClipLength=0xF0;
// An entry not ticked during this many Hector ticks (all Hectors' together) is of an object gone. Counted in ticks, not by the
// wall clock: a paused game ticks nothing, so a pause never drops a live entry (its shields deleted and made again). Identity
// is the weak-this control block (hector::CacheMatches), never age.
constexpr std::uint64_t kStaleTicks=0x4000;

struct Handle {
    void* tree;
    std::uint32_t index;
    std::uint32_t pad;
};
using ChildByNameFn=Handle*(__fastcall*)(const Handle* parent,Handle* out,const wchar_t* key);
using ChildCountFn=int(__fastcall*)(const Handle*);
using ChildIndexFn=std::uint32_t(__fastcall*)(const Handle*,std::uint32_t);
using NodeFloatFn=float(__fastcall*)(const Handle*);
using GetTargetFn=void*(__fastcall*)(void* self);
using BaseUpdateFn=std::uint64_t(__fastcall*)(void* self,void* dt);
using MoveToFn=bool(__fastcall*)(void* self,const float* goal,float a,float b,float speed,float c);
using FaceFn=void(__fastcall*)(void* self,const float* point,float limit);
using GunStartFn=void(__fastcall*)(void* gun);
using CasPlayStateFn=bool(__fastcall*)(void* cas,int player,int state,int flags);
using CasSetRateFn=void(__fastcall*)(void* cas,int player,float rate);

struct Entry {
    ObjRef ref;
    hector::Config cfg;
    hector::Cycle cycle;
    hector::Rng rng;
    std::uint64_t seenAt=0;          // `ticks` at its last Tick
    std::uint64_t tick=0;
    std::uint64_t pickTick=~0ull;
    unsigned pickMask=0;
    bool haveTarget=false;
    float dist=0.0f;                 // horizontal distance to the target at the last tick
    float target[3]{};
    // the arms' aim (arms_l, arms_r): bone, the bones below it, the eased angles, the animation's local of the arm
    // bone and what this file last wrote there (the CAS may or may not write it again before the next build)
    bool bonesTried=false;
    int arm[hector::kArms]{-1,-1};
    std::vector<int> below[hector::kArms];
    hector::ArmAim aim[hector::kArms];
    float base[hector::kArms][16]{};
    float written[hector::kArms][16]{};
    bool haveWritten[hector::kArms]{};
    // the head
    int headPlayer=-2;                   // -2: not looked up, -1: none
    hector::Head head;
    // the weapons' charge and fire look (edf41_hector fx; tools/make_edf41_objects.py HECTOR_FX)
    struct ArmFx {
        int kind=0;                      // 0 none, 1 cannon, 2 launcher, 3 gatling
        float glow=0.0f;                 // the cannon's charge state in frames: the ball shows over its last `glow` charge frames
        float z=0.0f;                    // the muzzle on the arm bone (0, 0, z)
        float rgb[3]{};                  // 4.1's flash colour, its largest channel 1
        int flashLife=0;                 // frames
        std::uint32_t left=0;            // the battery's shots left at the last frame (a drop is a shot)
        std::uint32_t shots=0;
    } fx[hector::kArms];
    bool fxRead=false;
    // the weapon objects (SGO edf41_hector_weapons; Weapons below): each arm's weapon SGO, the arms that hold a shield, the objects
    bool weaponsRead=false;
    unsigned shieldMask=0;               // bit i: gun_control entry i is a shield (it never fires)
    wchar_t weaponPath[hector::kArms][128]{};
    bool weaponMade[hector::kArms]{};
    edf41c::WeakRef weapon[hector::kArms];
};

std::atomic<int> animState{0};          // 0 unchecked, 1 the functions are this build's, 2 they are not: no animation

std::mutex tableLock;
std::unordered_map<const void*,Entry> table;
std::uint64_t ticks=0;

// The child `key` of `parent`; false when there is none.
bool Child(const Handle& parent,const wchar_t* key,Handle& out) noexcept {
    out={nullptr,0xFFFFFFFFu,0};
    reinterpret_cast<ChildByNameFn>(image+kChildByName)(&parent,&out,key);
    return out.tree!=nullptr;
}

bool Floats(const Handle& node,float* values,int want) noexcept {
    if(reinterpret_cast<ChildCountFn>(image+kChildCount)(&node)!=want)return false;
    for(int i=0;i<want;++i) {
        Handle e{node.tree,reinterpret_cast<ChildIndexFn>(image+kChildIndex)(&node,static_cast<std::uint32_t>(i)),0};
        values[i]=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&e);
    }
    return true;
}

bool AnimReady() noexcept {
    const int s=animState.load();
    if(s)return s==1;
    for(const Print& p:kAnimPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41AI the code at EDF.dll+0x%zX is not what this was written for: the Hector's head animation and weapon objects are off",p.rva);
            animState=2;
            return false;
        }
    animState=1;
    return true;
}

Handle At(const Handle& node,int i) noexcept {
    return Handle{node.tree,reinterpret_cast<ChildIndexFn>(image+kChildIndex)(&node,static_cast<std::uint32_t>(i)),0};
}
int CountOf(const Handle& node) noexcept { return reinterpret_cast<ChildCountFn>(image+kChildCount)(&node); }

hector::Clip ClipOf(const std::vector<float>& v) noexcept {
    hector::Clip c;
    if(v.size()<2)return c;
    c.frames=static_cast<int>(v[0]); c.step=v[1]; c.keys=v.data()+2;
    return c;
}

// The head: 4.1's wait / open / wait / close on the CAS player `head` (Hector.CAS has it; the class never plays it).
void StepHead(void* self,Entry& e) noexcept {
    char* cas=static_cast<char*>(self)+kCasAt;
    if(!Readable(cas,kCasPlayerCount+8))return;
    const std::uint64_t players=*reinterpret_cast<const std::uint64_t*>(cas+kCasPlayerCount);
    char* array=*reinterpret_cast<char**>(cas+kCasPlayers);
    if(e.headPlayer==-2) {
        e.headPlayer=reinterpret_cast<int(__fastcall*)(void*,const wchar_t*)>(image+kCasPlayerByName)(cas,L"head");
        if(e.headPlayer>=0 && static_cast<std::uint64_t>(e.headPlayer)>=players)e.headPlayer=-1;
        Log("EDF41AI hector %p head player %d",self,e.headPlayer);
    }
    if(e.headPlayer<0 || !array)return;
    const char* p=array+static_cast<std::size_t>(e.headPlayer)*kCasPlayerSize;
    if(!Readable(p,kCasPlayerSize))return;
    const float time=*reinterpret_cast<const float*>(p+kClipTime),length=*reinterpret_cast<const float*>(p+kClipLength);
    const bool done=std::isfinite(time) && std::isfinite(length) && time+1.0f>=length;
    if(const wchar_t* state=hector::StepHead(e.head,e.rng,done))
        reinterpret_cast<bool(__fastcall*)(void*,int,const wchar_t*,int)>(image+kCasSetState)(cas,e.headPlayer,state,0);
}

// The object's edf41_hector key. false: the object is not a 4.1 Hector (or its key is malformed).
bool ReadConfig(void* self,hector::Config& cfg) noexcept {
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{},ranges{},same{},ai{};
    if(!Child(root,L"edf41_hector",key))return false;
    float r[2]={},s[1]={},a[7]={};
    if(!Child(key,L"ranges",ranges) || !Floats(ranges,r,2))return false;
    if(!Child(key,L"same_weapon",same))return false;
    s[0]=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&same);
    if(!Child(key,L"ai",ai) || !Floats(ai,a,7))return false;
    cfg.range[0]=r[0]; cfg.range[1]=r[1];
    cfg.sameWeapon=s[0]>0.5f;
    cfg.dwellBase=a[0]; cfg.dwellRand=a[1]; cfg.retreatBase=a[2]; cfg.retreatRand=a[3];
    cfg.closeIn=a[4]; cfg.stopMin=a[5]; cfg.stopMax=a[6];
    return true;
}

// Only the edf41_hector marker. An entry already holds the rest of the key, parsed from this instance's own tree,
// which does not change while the object lives; a missing marker still drops the entry.
bool Marked(void* self) noexcept {
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{};
    return Child(root,L"edf41_hector",key);
}

// The target's position, or false when the object has none.
bool TargetOf(void* self,float* out) noexcept {
    char* me=static_cast<char*>(self);
    if(!*reinterpret_cast<std::uint8_t*>(me+kHasTargetAt))return false;
    char* t=static_cast<char*>(reinterpret_cast<GetTargetFn>(image+kGetTarget)(self));
    if(!t)return false;
    const float* p;
    if(*reinterpret_cast<void**>(t+0x378))p=reinterpret_cast<const float*>(**reinterpret_cast<char***>(t+0x368)+0x10);
    else p=reinterpret_cast<const float*>(t+kPosAt);
    out[0]=p[0]; out[1]=p[1]; out[2]=p[2];
    return true;
}

float FlatDistance(void* self,const float* target) noexcept {
    const float* me=reinterpret_cast<const float*>(static_cast<char*>(self)+kPosAt);
    const float dx=target[0]-me[0],dz=target[2]-me[2];
    return std::sqrt(dx*dx+dz*dz);
}

void DropWeapons(Entry& e) noexcept;
void EraseEntry(const void* self) noexcept {
    const auto it=table.find(self);
    if(it==table.end())return;
    DropWeapons(it->second);
    table.erase(it);
}

// Forget the objects that are gone (kStaleTicks), their shields with them. Call with tableLock held.
void SweepGone() noexcept {
    for(auto it=table.begin();it!=table.end();) {
        if(ticks-it->second.seenAt<=kStaleTicks) { ++it; continue; }
        DropWeapons(it->second);
        it=table.erase(it);
    }
}

// The entry of a 4.1 Hector, or null. Call with tableLock held.
Entry* Find(const void* self) noexcept {
    const auto it=table.find(self);
    return it==table.end() || !hector::CacheMatches(it->second.ref.ctrl,ObjRef::Of(self).ctrl,true)
        ? nullptr : &it->second;
}

void Tick(void* self) noexcept {
    // The whole key is parsed only for a new entry (cfg is used nowhere else); a live entry of this instance needs the marker only.
    bool known;
    {
        std::lock_guard<std::mutex> hold(tableLock);
        known=Find(self)!=nullptr;
    }
    hector::Config cfg;
    const bool marked=known ? Marked(self) : ReadConfig(self,cfg);
    const ObjRef ref=ObjRef::Of(self);
    if(!marked || !ref.ctrl) {
        std::lock_guard<std::mutex> hold(tableLock);
        EraseEntry(self);
        return;
    }
    float target[3];
    const bool controlled=EnemyControlTargetActive(self);
    const bool have=controlled ? EnemyControlWeaponAim(self,target) : TargetOf(self,target);
    const ULONGLONG now=GetTickCount64();
    std::lock_guard<std::mutex> hold(tableLock);
    auto cached=table.find(self);
    if(cached!=table.end() && !hector::CacheMatches(cached->second.ref.ctrl,ref.ctrl,marked))EraseEntry(self);   // another object here
    Entry* e=Find(self);
    if(!e) {
        if(known && !ReadConfig(self,cfg)) {     // the entry went stale (or away) after the marker-only check: parse it now
            EraseEntry(self);
            return;
        }
        e=&table[self];
        e->ref=ref;
        e->cfg=cfg;
        e->rng.Seed(reinterpret_cast<std::uintptr_t>(self)^(now*0x9E3779B97F4A7C15ull));
        e->cycle=hector::Begin(cfg,e->rng);
        Log("EDF41AI hector %p: ranges %.0f/%.0f same=%d ai=%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f first counter %.0f stop radius %.1f",self,
            cfg.range[0],cfg.range[1],cfg.sameWeapon ? 1 : 0,cfg.dwellBase,cfg.dwellRand,cfg.retreatBase,cfg.retreatRand,cfg.closeIn,
            cfg.stopMin,cfg.stopMax,e->cycle.counter,e->cycle.stopRadius);
    }
    e->seenAt=++ticks;
    ++e->tick;
    e->haveTarget=have;
    if(have) {
        e->target[0]=target[0]; e->target[1]=target[1]; e->target[2]=target[2];
        e->dist=FlatDistance(self,target);
        const hector::Event ev=controlled ? hector::Event::None : hector::Step(e->cycle,e->cfg,e->rng,e->dist);
        if(ev==hector::Event::BackoffStart)
            Log("EDF41AI backoff start %p dist %.1f stop radius %.1f walks backward for %d frames",self,e->dist,e->cycle.stopRadius,e->cycle.retreatLeft);
        else if(ev==hector::Event::BackoffEnd)
            Log("EDF41AI backoff end %p dist %.1f next counter %.0f stop radius %.1f",self,e->dist,e->cycle.counter,e->cycle.stopRadius);
    }
    if((ticks&(kStaleTicks-1))==0)SweepGone();
}

// Whether gun `i` of the owner attacks now: the class's own gate for its per-gun aim (0x34F913..0x34F94E).
bool Attacking(const void* owner,int i) noexcept {
    const char* base=static_cast<const char*>(owner);
    void* const* guns=*reinterpret_cast<void* const* const*>(base+kGunsAt);
    const std::uint64_t count=*reinterpret_cast<const std::uint64_t*>(base+kGunCountAt);
    if(!guns || count!=hector::kArms || !Readable(guns,sizeof(void*)*2*hector::kArms))return false;
    const char* control=static_cast<const char*>(guns[2*i]);
    if(!control || !Readable(control,sizeof(void*)))return false;
    const char* battery=*reinterpret_cast<const char* const*>(control);
    if(!battery || !Readable(battery,kBatteryLeftAt+4))return false;
    const std::uint32_t flags=*reinterpret_cast<const std::uint32_t*>(battery+kBatteryFlagsAt);
    const std::uint32_t left=*reinterpret_cast<const std::uint32_t*>(battery+kBatteryLeftAt);
    const std::uint32_t v=(flags&1u) && left==0 ? *reinterpret_cast<const std::uint32_t*>(battery+kBatteryCountAt) : left;
    return v!=0;
}

bool Finite16(const float* m) noexcept {
    for(int i=0;i<16;++i)
        if(!std::isfinite(m[i]))return false;
    return true;
}

// The arms' aim, after the base update (see the header). Call with tableLock held.
void AimArms(void* self,Entry& e) noexcept {
    char* skel=static_cast<char*>(self)+kSkeletonAt;
    if(!Readable(skel,0x28))return;
    char* recs=*reinterpret_cast<char**>(skel+0x10);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(skel+0x20);
    if(!recs || count<=0 || count>1024 || !Readable(recs,static_cast<std::size_t>(count)*kBoneSize))return;
    const auto parentOf=[&](std::int64_t b) { return *reinterpret_cast<const std::int32_t*>(recs+b*kBoneSize+kBoneParentAt); };
    if(!e.bonesTried) {
        e.bonesTried=true;
        const wchar_t* names[hector::kArms]={L"arms_l",L"arms_r"};
        for(int i=0;i<hector::kArms;++i) {
            e.arm[i]=edf41c::BoneIndex(skel,names[i]);
            e.below[i].clear();
            if(e.arm[i]<0)continue;
            for(std::int64_t b=0;b<count;++b) {                // every bone whose parent chain passes the arm bone
                std::int64_t p=parentOf(b);
                for(int guard=0;p>=0 && p<count && guard<256;++guard,p=parentOf(p))
                    if(p==e.arm[i]) { e.below[i].push_back(static_cast<int>(b)); break; }
            }
        }
        Log("EDF41AI hector %p arm bones %d (%zu below) / %d (%zu below)",self,e.arm[0],e.below[0].size(),e.arm[1],e.below[1].size());
    }
    const bool dead=*reinterpret_cast<const std::uint8_t*>(static_cast<char*>(self)+kDestroyedAt)!=0;
    for(int i=0;i<hector::kArms;++i) {
        const int bone=e.arm[i];
        if(bone<0 || bone>=count)continue;
        char* rec=recs+static_cast<std::size_t>(bone)*kBoneSize;
        const std::int32_t parent=parentOf(bone);
        float cur[16];
        std::memcpy(cur,rec+kBoneLocalAt,sizeof(cur));
        if(!Finite16(cur) || parent<0 || parent>=count)continue;
        if(!(e.haveWritten[i] && std::memcmp(cur,e.written[i],sizeof(cur))==0))std::memcpy(e.base[i],cur,sizeof(cur));
        if(dead) {                                          // the ragdoll has it: leave the animation's pose
            if(e.haveWritten[i])std::memcpy(rec+kBoneLocalAt,e.base[i],sizeof(cur));
            e.haveWritten[i]=false;
            e.aim[i]=hector::ArmAim{};
            continue;
        }
        float parentWorld[16],animWorld[16];
        std::memcpy(parentWorld,recs+static_cast<std::size_t>(parent)*kBoneSize+kBoneWorldAt,sizeof(parentWorld));
        hector::Mul4(e.base[i],parentWorld,animWorld);
        float pitch=0.0f,yaw=0.0f;
        bool attacking=e.haveTarget && Attacking(self,i);
        if(attacking) {
            float l[3];
            hector::InFrame(animWorld,e.target,l);
            attacking=hector::AimAngles(l,i,pitch,yaw);
            // 4.1 aims back to 0 at the same ease when the target is out of the arm's reach
            if(!attacking) { pitch=0.0f; yaw=0.0f; attacking=true; }
        }
        hector::StepAim(e.aim[i],attacking,pitch,yaw);
        float rot[16],local[16],world[16],built[16],inv[16],move[16];
        hector::AimRotation(e.aim[i].pitch,e.aim[i].yaw,rot);
        hector::Mul4(rot,e.base[i],local);
        hector::Mul4(local,parentWorld,world);
        std::memcpy(built,rec+kBoneWorldAt,sizeof(built));
        if(!Finite16(world) || !Finite16(built) || !hector::InverseAffine(built,inv))continue;
        hector::Mul4(inv,world,move);                       // the arm as built -> the arm as aimed, for the bones below
        for(const int b:e.below[i]) {
            float* w=reinterpret_cast<float*>(recs+static_cast<std::size_t>(b)*kBoneSize+kBoneWorldAt);
            float moved[16];
            hector::Mul4(w,move,moved);
            if(Finite16(moved))std::memcpy(w,moved,sizeof(moved));
        }
        std::memcpy(rec+kBoneWorldAt,world,sizeof(world));
        std::memcpy(rec+kBoneLocalAt,local,sizeof(local));
        std::memcpy(e.written[i],local,sizeof(local));
        e.haveWritten[i]=true;
    }
}

// Gun `i`'s battery (gun_control's order), or null.
const char* BatteryOf(const void* owner,int i) noexcept {
    const char* base=static_cast<const char*>(owner);
    void* const* guns=*reinterpret_cast<void* const* const*>(base+kGunsAt);
    if(!guns || *reinterpret_cast<const std::uint64_t*>(base+kGunCountAt)!=hector::kArms || !Readable(guns,sizeof(void*)*2*hector::kArms))
        return nullptr;
    const char* control=static_cast<const char*>(guns[2*i]);
    const char* battery=control && Readable(control,sizeof(void*)) ? *reinterpret_cast<const char* const*>(control) : nullptr;
    return battery && Readable(battery,kBatteryChargeAt+4) ? battery : nullptr;
}

// The object's edf41_hector fx: per arm [kind, glow frames, muzzle z, r, g, b, flash frames]. An arm without it: no look.
void ReadFx(void* self,Entry& e) noexcept {
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{},fx{};
    if(!Child(root,L"edf41_hector",key) || !Child(key,L"fx",fx) || CountOf(fx)!=hector::kArms)return;
    for(int i=0;i<hector::kArms;++i) {
        float v[7];
        if(!Floats(At(fx,i),v,7))continue;
        Entry::ArmFx& f=e.fx[i];
        f.kind=v[0]>=1.0f && v[0]<=3.0f ? static_cast<int>(v[0]) : 0;
        f.glow=v[1]; f.z=v[2]; f.rgb[0]=v[3]; f.rgb[1]=v[4]; f.rgb[2]=v[5];
        f.flashLife=static_cast<int>(v[6]);
        if(const char* b=BatteryOf(self,i))f.left=*reinterpret_cast<const std::uint32_t*>(b+kBatteryLeftAt);
    }
    Log("EDF41AI hector %p fx: arm 0 kind %d glow %.0f muzzle %.1f flash %d, arm 1 kind %d glow %.0f muzzle %.1f flash %d",self,
        e.fx[0].kind,e.fx[0].glow,e.fx[0].z,e.fx[0].flashLife,e.fx[1].kind,e.fx[1].glow,e.fx[1].z,e.fx[1].flashLife);
}

// 4.1's weapon look at each muzzle (tools/make_edf41_objects.py HECTOR_FX), after the arms are aimed (their world matrices are
// this frame's). The battery's attack state: +0x2E8 the charge frames left (0x6D547A sets it from charge.time, 0x6D556A counts
// down to the first shot), +0x2E0 the shots left of the attack (set to fire.count at the start, one less per shot): a drop
// since the last frame is a shot. The muzzle is the locator (0, 0, z) on the arm bone (tools: alias_dsgo's locators).
constexpr float kBallSize=14.0f,kBallStart=0.15f,kCannonFlash=12.0f,kGatlingFlash=6.0f,kLightRadius=30.0f,kBright=3.0f;
void Effects(void* self,Entry& e) noexcept {
    if(!e.fxRead) { e.fxRead=true; ReadFx(self,e); }
    char* skel=static_cast<char*>(self)+kSkeletonAt;
    char* recs=Readable(skel,0x28) ? *reinterpret_cast<char**>(skel+0x10) : nullptr;
    const std::int64_t count=recs ? *reinterpret_cast<const std::int64_t*>(skel+0x20) : 0;
    for(int i=0;i<hector::kArms;++i) {
        Entry::ArmFx& f=e.fx[i];
        const char* battery=f.kind ? BatteryOf(self,i) : nullptr;
        if(!battery || e.arm[i]<0 || e.arm[i]>=count)continue;
        const std::int32_t charge=*reinterpret_cast<const std::int32_t*>(battery+kBatteryChargeAt);
        const std::uint32_t left=*reinterpret_cast<const std::uint32_t*>(battery+kBatteryLeftAt);
        const bool shot=left<f.left;
        f.left=left;
        const bool ball=f.kind==1 && charge>0 && static_cast<float>(charge)<=f.glow;
        if(!shot && !ball)continue;
        const float* m=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(e.arm[i])*kBoneSize+kBoneWorldAt);
        if(!Finite16(m))continue;
        const float at[3]={m[12]+m[8]*f.z,m[13]+m[9]*f.z,m[14]+m[10]*f.z};
        const float colour[4]={f.rgb[0]*kBright,f.rgb[1]*kBright,f.rgb[2]*kBright,1.0f};
        if(ball) {   // grows through the charge state, as Deiroi's main gun gathers its light (ChargeEffect_DeiroiBody, Particle3_k)
            const float t=1.0f-static_cast<float>(charge)/f.glow;
            edf41c::Particle(at,3,kBallSize*(kBallStart+(1.0f-kBallStart)*t),colour,L"Particle3_k.dds");
        }
        if(!shot)continue;
        ++f.shots;
        if(f.kind==1 || f.kind==3)
            edf41c::Particle(at,(std::max)(1,f.flashLife),f.kind==1 ? kCannonFlash : kGatlingFlash,colour,L"マズルフラッシュ01.dds");
        if(f.kind==1 || f.kind==2)edf41c::StencilLight(at,colour,kLightRadius,(std::max)(1,f.flashLife));
        if(f.shots<=2)Log("EDF41AI hector %p arm %d shot #%u (%u left) kind %d at %.0f %.0f %.0f",self,i,f.shots,left,f.kind,at[0],at[1],at[2]);
    }
}

// ---- the weapons (tools/make_edf41_objects.py hector_weapons; docs/edf41-hector-split-models.md) ---------------------------------
// 4.1 (EDF41.exe, H): every arm weapon is an object of its own (Hector301_Cannon ... Hector401_Shield) on its arm bone, with its own
// model, ragdoll (owner = the Hector), CAS and material animation; the shield (vtable 0xA98C10) tags its ragdoll bodies with property
// 2 (0x3342B0): bullets meet them only when hostile to the shield's team (0x169550) and the Hector refuses a hit landing on them (slot
// 10 0x2210E0): it stops the player's bullets and takes nothing.
// Here: an EventAnimationObject of the plugin's class Hector41_Weapon (the shield: Hector401_Shield; one cloned vtable for both) made
// by its Hector's update from the SGO edf41_hector_weapons names for that arm (the weapon's own model archive, the still CAS, 4.1's
// weapon ragdoll, its own material_animation and edf41_weapon_anim). Every frame, after the arms are aimed: its matrix (+0x60) is the
// arm bone's world, its own animation (4.1's clips, hector::StepOpenClose / StepRoll, attacking as the arm's battery does) is written
// into its bones' locals and its skeleton rebuilt from that matrix (0x1100B90, edf41c::ModelMatrix: whichever of the two updates runs
// first this frame, the draw sees the arm's pose; the object's own Update rebuilds from the same matrix and locals). Its material
// animation: [0] the glow, looped from the start; [1] the death block, played to its end and held when the Hector dies (4.1: each
// weapon object plays its own, 0x2255D4 .. 0x22A289). Its ragdoll: a live shield's bodies on the Hector's own ragdoll layer
// (WalkingEnemyBase +0xD80, 0x53BCD9; EventAnimationObject's constructor put them on the map layer 0x11, which walkers and climbers
// take for ground), every other weapon's and a dead Hector's shield's out of the world (Ragdoll::Disable 0x6E97C0: the port's guns
// took no hits before either). Every hit refused (slot 16), the Hector's team, not registered (no target, not counted). It stays on
// its Hector's corpse (the arm bones the ragdoll moves) and goes when the Hector object goes (its own update checks), or when the
// Hector's entry here is dropped. Every machine makes its own for its own copy of the Hector (a mission object on each machine): the
// objects take no damage and carry no state of their own (pose, animation and death come from that machine's Hector), so nothing of
// them travels and every machine shows the same weapons on the same arms.
constexpr std::size_t kHectorRagdollAt=0xD80;   // WalkingEnemyBase's ragdoll component (0x53BCD9; the kind EAO has at +0xAA0)
constexpr std::size_t kHectorTeamAt=0x314;
constexpr int kWeaponAccept=16;                  // slot 16 Accept(object, GDI*) -> bool (ApplyDamage 0x547C30 asks it)
constexpr int kWeaponAnimKinds[2]={0,2};         // material_animation load kinds of [glow, death] (the generator writes them too)
struct WeaponRec {
    edf41c::WeakRef owner;
    bool shield=false;                           // Hector401_Shield: its ragdoll stops hostile bullets while its Hector lives
    bool layered=false;
    bool gone=false;                             // DeleteObject asked
    bool dead=false;                             // its Hector died: the death block played
    bool disabledSaid=false;
    edf41c::MaterialAnims mat;
    // its own animation (edf41_weapon_anim): the bones it moves, the clips (open, close, roll: frames, step, keys)
    hector::WeaponKind kind=hector::WeaponKind::None;
    std::vector<int> bone;
    std::vector<float> clip[3];
    hector::OpenClose oc;
    hector::Barrels roll;
};
std::mutex weaponLock;
std::unordered_map<const void*,WeaponRec> weapons;
edf41c::Clone weaponClone{"EventAnimationObject (Hector41_Weapon / Hector401_Shield)",edf41c::kEaoVtable,45,edf41c::kEaoSlots,{},{},{}};
std::atomic<int> weaponsMade{0};

hector::Clip ClipOf(const std::vector<float>& v) noexcept;

// The weapon's edf41_weapon_anim ([kind, [bone], open, close, roll]; tools/make_edf41_objects.py weapon_anim) on its own skeleton;
// none (301's weapons, the shield): no animation. Data that does not fit the model: logged, left still.
void ReadWeaponAnim(void* obj,WeaponRec& r) noexcept {
    const edf41c::SgoNode* key=edf41c::Key(obj,L"edf41_weapon_anim");
    if(!key)return;
    std::int32_t kind=0;
    const edf41c::SgoNode* names=edf41c::Child(key,1);
    const int nb=edf41c::Count(names);
    bool ok=edf41c::Count(key)==5 && edf41c::Int(edf41c::Child(key,0),&kind) && kind>=1 && kind<=2 && nb>0 && nb<=16;
    if(ok)r.bone.assign(static_cast<std::size_t>(nb),-1);
    for(int b=0;b<nb && ok;++b) {
        wchar_t text[64]{};
        ok=edf41c::Text(edf41c::Child(names,b),text,64) &&
           (r.bone[static_cast<std::size_t>(b)]=edf41c::BoneIndex(static_cast<char*>(obj)+edf41c::kEaoSkeleton,text))>=0;
    }
    for(int c=0;c<3 && ok;++c) {
        const edf41c::SgoNode* clip=edf41c::Child(key,2+c);
        const int n=edf41c::Count(clip);
        if(n<0) { ok=false; break; }
        r.clip[c].resize(static_cast<std::size_t>(n));
        for(int j=0;j<n && ok;++j)ok=edf41c::Float(edf41c::Child(clip,j),&r.clip[c][static_cast<std::size_t>(j)]);
        if(ok && n>0) {
            const float frames=r.clip[c][0];
            ok=frames>=1.0f && static_cast<std::size_t>(n)==2+static_cast<std::size_t>(frames)*static_cast<std::size_t>(nb)*hector::kKeyFloats;
        }
    }
    ok=ok && (kind==1 ? !r.clip[0].empty() && !r.clip[1].empty() : !r.clip[2].empty());
    if(!ok) {
        Log("EDF41AI weapon %p: its edf41_weapon_anim does not fit its model (%d bones), left still",obj,nb);
        r.bone.clear();
        for(std::vector<float>& c:r.clip)c.clear();
        return;
    }
    r.kind=static_cast<hector::WeaponKind>(kind);
    Log("EDF41AI weapon %p animation %s, %d bones",obj,kind==1 ? "open/close" : "roll",nb);
}

// The weapon's own animation this frame (4.1's state machine: open / close on the attack, the barrels' roll up and down) into its
// bones' locals; the caller rebuilds its skeleton after. Call with weaponLock held.
void WeaponPose(WeaponRec& r,char* obj,bool attacking) noexcept {
    if(r.kind==hector::WeaponKind::None)return;
    char* skel=obj+edf41c::kEaoSkeleton;
    if(!Readable(skel,0x28))return;
    char* recs=*reinterpret_cast<char**>(skel+0x10);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(skel+0x20);
    if(!recs || count<=0 || count>1024 || !Readable(recs,static_cast<std::size_t>(count)*kBoneSize))return;
    int which=2;
    float time=0.0f;
    if(r.kind==hector::WeaponKind::OpenClose)
        which=hector::StepOpenClose(r.oc,attacking,ClipOf(r.clip[0]).Length(),ClipOf(r.clip[1]).Length(),time);
    else
        time=hector::StepRoll(r.roll,attacking,ClipOf(r.clip[2]).Length());
    const hector::Clip clip=ClipOf(r.clip[which]);
    const int nb=static_cast<int>(r.bone.size());
    for(int k=0;k<nb;++k) {
        const int b=r.bone[static_cast<std::size_t>(k)];
        if(b<0 || b>=count)continue;
        float pose[hector::kKeyFloats],local[16];
        hector::SamplePose(clip,nb,k,time,which==2,pose);
        hector::PoseMatrix(pose,local);
        if(Finite16(local))std::memcpy(recs+static_cast<std::size_t>(b)*kBoneSize+kBoneLocalAt,local,sizeof(local));
    }
}

std::uint64_t __fastcall WeaponDtor(void* self,unsigned flags) {
    {
        std::lock_guard<std::mutex> hold(weaponLock);
        const auto it=weapons.find(self);
        if(it!=weapons.end()) {
            edf41c::MaterialAnimsFree(it->second.mat);   // before the model it is bound to goes
            edf41c::WeakReset(it->second.owner);
            weapons.erase(it);
        }
    }
    return reinterpret_cast<edf41c::DtorFn>(weaponClone.orig[edf41c::kSlotDtor])(self,flags);
}
std::uint64_t __fastcall WeaponUpdate(void* self,void* dt) {
    bool drop=false;
    {
        std::lock_guard<std::mutex> hold(weaponLock);
        const auto it=weapons.find(self);
        if(it!=weapons.end() && !it->second.gone) {
            drop=!edf41c::WeakAlive(it->second.owner);
            if(drop)it->second.gone=true;
        }
    }
    if(drop)edf41c::DeleteObject(self);   // its Hector went (deleted, its corpse removed): so does the weapon
    const std::uint64_t result=reinterpret_cast<edf41c::UpdateFn>(weaponClone.orig[edf41c::kSlotUpdate])(self,dt);
    std::lock_guard<std::mutex> hold(weaponLock);
    const auto it=weapons.find(self);
    if(it!=weapons.end())edf41c::MaterialAnimsUpdate(it->second.mat,dt);   // with the Update's frame info, as the 401's parts
    return result;
}
std::uint64_t __fastcall WeaponMessage(void* self,std::uint32_t msg,void* data) {
    return reinterpret_cast<edf41c::MessageFn>(weaponClone.orig[edf41c::kSlotMessage])(self,msg,data);
}
bool __fastcall WeaponAccept(void*,void*) { return false; }   // 4.1: no hit on the shield counts (0x2210E0); the guns take none

void MakeWeapon(void* info,bool shield) noexcept {
    void* obj=edf41c::CloneObject(weaponClone,edf41c::kEaoCtor,edf41c::kEaoSize,info);
    if(!obj)return;
    // EventAnimationObject's constructor set +0x380 bit 0 (no HP loss); nothing aims at it and no count waits for it
    edf41c::SetFlag(obj,edf41c::kNoHpLoss|edf41c::kNotTarget|edf41c::kNotCounted,true);
    std::lock_guard<std::mutex> hold(weaponLock);
    WeaponRec& r=weapons[obj];   // in place: the material animation component is bound where it lives
    r.shield=shield;
    ReadWeaponAnim(obj,r);
    const int anims=edf41c::MaterialAnimsLoad(r.mat,obj,edf41c::Key(obj,L"material_animation"),kWeaponAnimKinds,2);
    if(anims>0)edf41c::MaterialAnimsPlay(r.mat,0,2);   // the glow, looped from the start (4.1's weapons: 0x2255D4 ...)
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<16)Log("EDF41AI weapon %p (%s): %d material animations",obj,shield ? "shield" : "gun",anims);
}
void CreateWeapon(void* info) noexcept { MakeWeapon(info,false); }
void CreateShield(void* info) noexcept { MakeWeapon(info,true); }

// The game is shutting down: every weapon's material animation given back while the material data exists (edf41c::AtGameShutdown;
// a weapon destroyed later only drops its record).
void ShutdownWeapons() noexcept {
    std::lock_guard<std::mutex> hold(weaponLock);
    for(auto& kv:weapons)edf41c::MaterialAnimsFree(kv.second.mat);
}

// Each arm's weapon SGO and the shield arms of edf41_hector_weapons ({sgo: [arms_l's, arms_r's], shield: [entry...]}). A Hector
// without it (the key is in every generated variant): its install is broken, logged, no weapon made.
void ReadWeapons(void* self,Entry& e) noexcept {
    e.weaponsRead=true;
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{},sgos{},shield{};
    if(!Child(root,L"edf41_hector_weapons",key)) {
        Log("EDF41AI hector %p: no edf41_hector_weapons in its SGO (an old or broken install): its weapons are not made",self);
        return;
    }
    if(!AnimReady() || !Child(key,L"sgo",sgos) || CountOf(sgos)!=hector::kArms || !Child(key,L"shield",shield)) {
        Log("EDF41AI hector %p: edf41_hector_weapons is not {sgo: [2], shield} (or the SGO readers are not this build's): no weapons",self);
        return;
    }
    for(int i=0;i<hector::kArms;++i) {
        const Handle n=At(sgos,i);
        const wchar_t* text=reinterpret_cast<const wchar_t*(__fastcall*)(const Handle*)>(image+kNodeText)(&n);
        if(text)wcsncpy_s(e.weaponPath[i],text,_TRUNCATE);
    }
    const int n=CountOf(shield);
    for(int i=0;i<n && i<hector::kArms;++i) {
        const Handle a=At(shield,i);
        const float v=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&a);
        if(v>=0.0f && v<static_cast<float>(hector::kArms))e.shieldMask|=1u<<static_cast<int>(v);
    }
    Log("EDF41AI hector %p weapons: %ls / %ls, shield arms %s%s",self,e.weaponPath[0],e.weaponPath[1],
        (e.shieldMask&1u) ? "L" : "",(e.shieldMask&2u) ? "R" : "");
}

void DropWeapons(Entry& e) noexcept {
    for(int i=0;i<hector::kArms;++i) {
        if(edf41c::WeakAlive(e.weapon[i]))edf41c::DeleteObject(e.weapon[i].object);
        edf41c::WeakReset(e.weapon[i]);
    }
}

// After the arms are aimed (their world matrices are this frame's): each weapon made once, then kept on its arm bone, posed and,
// when the Hector dies, given its death look. Call with tableLock held.
void Weapons(void* self,Entry& e) noexcept {
    if(!e.weaponsRead)ReadWeapons(self,e);
    const bool dead=*reinterpret_cast<const std::uint8_t*>(static_cast<char*>(self)+kDestroyedAt)!=0;
    char* skel=static_cast<char*>(self)+kSkeletonAt;
    char* recs=Readable(skel,0x28) ? *reinterpret_cast<char**>(skel+0x10) : nullptr;
    const std::int64_t count=recs ? *reinterpret_cast<const std::int64_t*>(skel+0x20) : 0;
    const std::int32_t team=*reinterpret_cast<const std::int32_t*>(static_cast<char*>(self)+kHectorTeamAt);
    for(int i=0;i<hector::kArms;++i) {
        if(!e.weaponPath[i][0] || e.arm[i]<0 || e.arm[i]>=count)continue;
        float m[16];
        std::memcpy(m,recs+static_cast<std::size_t>(e.arm[i])*kBoneSize+kBoneWorldAt,sizeof(m));
        if(!Finite16(m))continue;
        if(!e.weaponMade[i]) {           // once: a weapon the game would not make is not asked for again
            e.weaponMade[i]=true;
            void* obj=edf41c::CreateFromPath(m,e.weaponPath[i]);
            bool ours=false;
            if(obj) {
                std::lock_guard<std::mutex> hold(weaponLock);
                const auto it=weapons.find(obj);
                ours=it!=weapons.end();
                if(ours)edf41c::WeakSet(it->second.owner,self);
            }
            if(!ours) {
                Log("EDF41AI hector %p arm %d: %ls made %p, not the plugin's weapon class: this arm shows no weapon",self,i,e.weaponPath[i],obj);
                if(obj)edf41c::DeleteObject(obj);
                continue;
            }
            edf41c::SetTeam(obj,team,false);
            edf41c::WeakSet(e.weapon[i],obj);
            Log("EDF41AI hector %p arm %d: weapon #%d %p made (%ls), team %d",self,i,++weaponsMade,obj,e.weaponPath[i],team);
        }
        if(!edf41c::WeakAlive(e.weapon[i]))continue;
        char* obj=static_cast<char*>(e.weapon[i].object);
        std::memcpy(obj+edf41c::kMatrixAt,m,sizeof(m));
        if(*reinterpret_cast<const std::int32_t*>(obj+kHectorTeamAt)!=team)edf41c::SetTeam(obj,team,false);
        const bool attacking=!dead && e.haveTarget && Attacking(self,i);
        {
            std::lock_guard<std::mutex> hold(weaponLock);
            const auto it=weapons.find(obj);
            if(it!=weapons.end()) {
                WeaponRec& r=it->second;
                if(dead && !r.dead) {    // 4.1: each weapon plays its own death block when the Hector dies
                    r.dead=true;
                    edf41c::MaterialAnimsPlay(r.mat,1,1);
                    Log("EDF41AI hector %p arm %d: died, weapon %p plays its death block",self,i,obj);
                }
                if(r.shield && !dead) {
                    if(!r.layered) {     // once, after the constructor built its ragdoll on the map layer
                        r.layered=true;
                        const int from=edf41c::RagdollLayer(obj+edf41c::kEaoRagdoll);
                        const int to=edf41c::RagdollLayer(static_cast<char*>(self)+kHectorRagdollAt);
                        const bool moved=to>=0 && from>=0 && (from==to || edf41c::SetRagdollLayer(obj+edf41c::kEaoRagdoll,to));
                        Log("EDF41AI hector %p arm %d: shield %p bodies on layer %d, the Hector's %d: %s",self,i,obj,from,to,
                            moved ? "now the Hector's" : "LEFT where they are (walkers may take it for ground)");
                    }
                } else if(edf41c::DisableRagdoll(obj+edf41c::kEaoRagdoll) && !r.disabledSaid) {
                    // every frame (cheap: a flag per body): whatever enables the bodies later, they stay out of the world
                    r.disabledSaid=true;
                    Log("EDF41AI hector %p arm %d: weapon %p ragdoll out of the world (%s)",self,i,obj,r.shield ? "its Hector died" : "a gun");
                }
                WeaponPose(r,obj,attacking);
            }
        }
        edf41c::ModelMatrix(obj,m);
    }
}

std::uint64_t __fastcall BaseUpdateHook(void* self,void* dt) {
    Tick(self);
    const std::uint64_t result=reinterpret_cast<BaseUpdateFn>(image+kBaseUpdate)(self,dt);
    std::lock_guard<std::mutex> hold(tableLock);
    if(Entry* e=Find(self)) {
        AimArms(self,*e);
        Weapons(self,*e);
        if(!*reinterpret_cast<const std::uint8_t*>(static_cast<char*>(self)+kDestroyedAt)) {
            Effects(self,*e);
            if(AnimReady())StepHead(self,*e);
        }
    }
    return result;
}

// These three call sites belong only to slot-7 AI follow/point/chase states;
// native pad movement and real attack/root motion use other paths. Do not let a
// stale AI call overwrite the player's native +1290 movement or +12A4 facing.
bool __fastcall MoveToHook(void* self,const float* goal,float a,float b,float speed,float c) {
    float aim[4];
    {
        std::lock_guard<std::mutex> hold(tableLock);
        const Entry* e=Find(self);
        if(e && EnemyControlTargetActive(self))return false;
        if(e) {
            const hector::Walk walk=hector::Steer(e->cycle,e->cfg,e->dist,e->haveTarget);
            speed=walk.speed;
            if(walk.takeover) {
                // along the body's own facing (its matrix's z row, flattened), forward or backward: 4.1's walk command
                // is the local z alone (0x2238A6..0x2238BF); FaceHook turns the body to the target meanwhile
                const char* me=static_cast<char*>(self);
                const float* p=reinterpret_cast<const float*>(me+kPosAt);
                const float* z=reinterpret_cast<const float*>(me+kAxisZAt);
                const float n=std::sqrt(z[0]*z[0]+z[2]*z[2]);
                const float k=(walk.retreat ? -100.0f : 100.0f)/(n>1e-6f ? n : 1.0f);
                if(n>1e-6f) {
                    aim[0]=p[0]+z[0]*k; aim[1]=p[1]; aim[2]=p[2]+z[2]*k; aim[3]=goal[3];
                    goal=aim;
                    a=0.0f; b=0.0f;             // 4.1 walks at full speed to the end (no slow-down ramp)
                }
            }
        }
    }
    return reinterpret_cast<MoveToFn>(image+kMoveTo)(self,goal,a,b,speed,c);
}

// The walking states' FaceTarget: a Hector with a target turns to the target (4.1's heading, 0x2234B6), whichever point
// the state gives (the follow state's slot beside it, the chase state's route point).
void __fastcall FaceHook(void* self,const float* point,float limit) {
    float aim[4];
    {
        std::lock_guard<std::mutex> hold(tableLock);
        const Entry* e=Find(self);
        if(e && EnemyControlTargetActive(self))return;
        if(e && e->haveTarget) {
            aim[0]=e->target[0]; aim[1]=e->target[1]; aim[2]=e->target[2]; aim[3]=point[3];
            point=aim;
        }
    }
    reinterpret_cast<FaceFn>(image+kFaceTarget)(self,point,limit);
}

// 4.1's gait for the Hector at `self` now: its state index in the class's table (-1: the CAS has none) and its rate.
int GaitState(const char* self,float& rate) noexcept {
    const auto f=[self](std::size_t at) noexcept { return reinterpret_cast<const float*>(self+at); };
    const float* v=f(kEasedVelocityAt);
    const auto along=[v](const float* axis) noexcept {
        const float n=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
        return n>1e-6f ? (v[0]*axis[0]+v[1]*axis[1]+v[2]*axis[2])/n/60.0f : 0.0f;   // per frame, as 4.1 keeps it
    };
    const float vx=along(f(kAxisXAt)),vz=along(f(kAxisZAt));
    const float* w=f(kWalkVectorAt);
    const float command=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2])**f(kMoveSpeedAt)/60.0f;
    const float yawStep=*f(kYawStepAt);
    if(!std::isfinite(vx) || !std::isfinite(vz) || !std::isfinite(command) || !std::isfinite(yawStep))return -1;
    const hector::Gait g=hector::PickGait(yawStep,vx,vz,command*command);
    rate=hector::GaitRate(g,yawStep,vx,vz,*f(kWalkAnimAt)*60.0f,*f(kTurnAnimAt));
    return *reinterpret_cast<const int*>(self+kGaitStateAt[static_cast<int>(g)]);
}

bool __fastcall GaitStateHook(void* cas,int which,int state,int flags) {
    const char* self=static_cast<char*>(cas)-kCasAt;
    {
        std::lock_guard<std::mutex> hold(tableLock);
        float rate=0.0f;
        int mine=-1;
        if(Find(self) && (mine=GaitState(self,rate))>=0)state=mine;
    }
    return reinterpret_cast<CasPlayStateFn>(image+kCasPlayState)(cas,which,state,flags);
}

void __fastcall GaitRateHook(void* cas,int which,float rate) {
    const char* self=static_cast<char*>(cas)-kCasAt;
    {
        std::lock_guard<std::mutex> hold(tableLock);
        float mine=0.0f;
        if(Find(self) && GaitState(self,mine)>=0)rate=mine;
    }
    reinterpret_cast<CasSetRateFn>(image+kCasSetRate)(cas,which,rate);
}

void __fastcall GunStartHook(void* gun) {
    void* owner=*reinterpret_cast<void**>(static_cast<char*>(gun)+kGunOwnerAt);
    unsigned mask=hector::kArmLeft|hector::kArmRight;
    if(owner) {
        std::lock_guard<std::mutex> hold(tableLock);
        Entry* e=Find(owner);
        const char* base=static_cast<char*>(owner);
        void** guns=*reinterpret_cast<void***>(const_cast<char*>(base)+kGunsAt);
        const std::uint64_t count=*reinterpret_cast<const std::uint64_t*>(base+kGunCountAt);
        int index=-1;
        if(e && count==hector::kArms && guns)
            for(int i=0;i<hector::kArms;++i)
                if(guns[2*i]==gun)index=i;
        // a shield arm's gun_control entry is there because the class wants one per arm; 4.1's shield has no weapon
        // (Hector401_Shield's vtable: no fire), so it never starts, whoever drives the Hector
        if(e && !e->weaponsRead)ReadWeapons(owner,*e);
        if(index>=0 && (e->shieldMask&(1u<<index)))return;
        if(e && !EnemyControlTargetActive(owner) && e->haveTarget && count==hector::kArms && guns) {
            if(e->pickTick!=e->tick) {          // one pick per attack: the loop starts the arms in the same tick
                e->pickTick=e->tick;
                e->pickMask=hector::PickArms(e->cfg,e->rng,e->dist);
                Log("EDF41AI gate %p dist %.1f ranges %.0f/%.0f -> arms %s%s%s",owner,e->dist,e->cfg.range[0],e->cfg.range[1],
                    (e->pickMask&hector::kArmLeft) ? "L" : "",(e->pickMask&hector::kArmRight) ? "R" : "",e->pickMask ? "" : "none");
            }
            if(index>=0)mask=e->pickMask;
            if(index>=0 && !(mask&(1u<<index)))return;
        }
    }
    reinterpret_cast<GunStartFn>(image+kGunStart)(gun);
}
}  // namespace

bool InstallEdf41Ai() noexcept {
    if(!Cfg().edf41Ai) {
        Log("EDF41AI off (ini Edf41Ai=0)");
        return false;
    }
    bool changed=false;
    bool ok=RedirectCall(image+kBaseUpdateSite,image+kBaseUpdate,reinterpret_cast<void*>(&BaseUpdateHook),changed);
    int walks=0,guns=0;
    for(const std::size_t site:kMoveToSites)
        walks+=RedirectCall(image+site,image+kMoveTo,reinterpret_cast<void*>(&MoveToHook),changed) ? 1 : 0;
    int faces=0;
    for(const std::size_t site:kFaceSites)
        faces+=RedirectCall(image+site,image+kFaceTarget,reinterpret_cast<void*>(&FaceHook),changed) ? 1 : 0;
    for(const std::size_t site:kGunStartSites)
        guns+=RedirectCall(image+site,image+kGunStart,reinterpret_cast<void*>(&GunStartHook),changed) ? 1 : 0;
    int gaits=0,rates=0;
    for(const std::size_t site:kGaitStateSites)
        gaits+=RedirectCall(image+site,image+kCasPlayState,reinterpret_cast<void*>(&GaitStateHook),changed) ? 1 : 0;
    for(const std::size_t site:kGaitRateSites)
        rates+=RedirectCall(image+site,image+kCasSetRate,reinterpret_cast<void*>(&GaitRateHook),changed) ? 1 : 0;
    // the weapons (4.1's weapon objects as objects again: Weapons above)
    bool weaponClasses=edf41c::BuildClone(weaponClone,&WeaponDtor,&WeaponUpdate,&WeaponMessage);
    if(weaponClasses) {
        edf41c::SetSlot(weaponClone,kWeaponAccept,reinterpret_cast<void*>(&WeaponAccept));
        edf41c::AllowAnyTeam(weaponClone);   // EventAnimationObject's slot 22 takes team 6 only: the Hector's team would not take
        weaponClasses=edf41c::AddFactory(L"Hector41_Weapon",&CreateWeapon) && edf41c::AddFactory(L"Hector401_Shield",&CreateShield);
        edf41c::AtGameShutdown(&ShutdownWeapons);
    }
    Log("EDF41AI hooked: update=%d walk=%d/3 face=%d/3 gun start=%d/2 gait state=%d/4 rate=%d/2 weapon classes=%d",ok ? 1 : 0,walks,faces,
        guns,gaits,rates,weaponClasses ? 1 : 0);
    return ok && walks==3 && faces==3 && guns==2 && gaits==4 && rates==2;
}
}  // namespace crew
