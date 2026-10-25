// EDF4.1's UfoMother301 (the mothership) and its four part classes as the plugin's own scene object classes
// (docs/edf41-ufomother301.md; the 4.1 behaviour read from EDF41.exe, the EDF6 calls from EDF.dll: src/edf41classes.h).
//
//  - The hull is built by UfoCarrier508's constructor under a cloned vtable (slot 1 the deleting destructor, 5 Update, 9 OnMessage,
//    16 the damage filter): the carrier's base keeps doing what 4.1's base did for the hull (route following, durability, the encounter),
//    the rest is 4.1's: the slow spin (0.0003 rad/frame, ObjectAction 0 / 1 / 2), damage only on the `weak` body and only once
//    the main cannon is gone (4.1 slot 10 0x250FC0), a hit's 64 frames of small fire bursts round the weak point, the parts.
//  - The parts (mp_HatchLow 4, mp_HatchMiddle 4 covers, mp_Cannon 10, mp_BigCannon 1, mp_Panel 4 rings of 22 / 40 / 40 / 23)
//    are made on the hull's first Update with the game's own create-from-path, each an EventAnimationObject (model, CAS
//    "main", ragdoll, durability) under a cloned vtable, and hung on the hull: world = L2 x A x L1 x hull (A from the entry's
//    euler degrees and position; L1 / L2 the class's own: the panels' ring turn and push, the main cannon's spin).
//  - 4.1's per-part state machines, one to one (frame counts at 60 FPS; a state switched to inside a frame sees frame 1 next):
//    the main cannon (close / open / standby / 1200-frame charge / a 61-shot LaserBullet02 sweep / destroyed / falling), the
//    cannon (appear, the 600-frame RocketBullet01 charge every 1800..3599 frames; its beam machine: one BeamBullet01 every
//    4 frames from the 4 hatch muzzles in turn at the target), the panel (push out 100 and turn the ring; a LaserBullet02 every
//    max(30, 120 - 5s) frames or a PlasmaBullet01 every 300 at the target), the hatch (4.1's ObjectGenerator: it opens ahead of
//    a wave, spawns at the locator 発生位置 only once open, closes 240 frames after the last). Flags as 4.1: +0x900 enabled
//    (else invulnerable and unregistered), +0x901 attack, +0x902 the main cannon's one-shot trigger, +0x904 the two static
//    panel rings (never fire, never lockable). Hatches and covers are never enabled: invulnerable for good.
//  - Script interface: ObjectAction (message 0x1000000D) 0..17, GetPartsObject (0x10000017: group 0 the main cannon, 1 the
//    cannons by index), the generators (0x20100, +0x48 the hatch index).
// Sounds are 4.1's SEPRESET entries (the hull SGO's edf41_se, tools/make_edf41_objects.py se41_table) on the cues EDF6 ships in
// tikyuu4_en_MotherShip.acb; effects EDF6's EffectGenUtil. 4.1's sound throttle: one sound of each of 6 kinds per 16 frames.
//  - The hull's own 4.1 life (docs/edf41-ufomother301.md 补完规格 6, 补完 2026-10-15): its main state machine (closed / open
//    or show / open: the 16 exhaust vents on, 540 frames after a hit with the main cannon gone it hides the weak point for 30 s),
//    the hit (slot 18 replaced: 64 frames of fire bursts, the damage sound at the hit point, a white flash), 4.1's death in three
//    sequences (10 s of big explosions, the free fall until the ground is within 300, 2 s, then deleted with its parts) in place
//    of the carrier's (which never starts: slot 18 and message 0x1000000F are ours), its fly sound in place of the carrier's.
//  - The glow: 4.1 binds the models' material parameters light0_color and scroll_light0 by name and writes them every frame (hull
//    0x250730 / 0x2507D0, parts 0x25E5E0); EDF6 has the same machinery (the binding 0x6DCBF0 / 0x6DCC80 on model+0x160, set
//    0x6DD2F0, apply 0x6DD420, which UfoSmall507 runs in its draw slot 3): the colour eases to its target, the scroll turns 1/600
//    per frame, both applied in each object's slot 3 before the stock draw.
// Not ported: the exhaust vents are EDF6's ExplosionWind puffs at 4.1's 16 vent points, not 4.1's own particle emitter.
// Online (edf41net, the block "online" at the end): the parts and the hatches' enemies get the game's child identity under the
// hull (durability, damage, kills and targets travel natively); every machine runs the parts' machines from the same script
// actions, a cannon's random charge timer is the host's.
#include "crew.h"
#include "team_native_logic.h"
#include "edf41_hitlayer.h"
#include "edf41classes.h"
#include "edf41mother_pose.h"
#include "edf41_sound_channels.h"
#include "edf41net.h"
#include "enemy_control_legacy_mother.h"
#include "pvp_controlled.h"
#include "pvp_controlled_spawn.h"
#include "enemy_control_pvp_tree.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
using namespace edf41c;
constexpr int kSlots=49,kPartSlots=45;
constexpr int kSlotAccept=16;
constexpr std::size_t kWeakAssign=0x6CEBB0;   // weak_ptr assign (dst, src); the bullet code uses it (edf41classes.cpp)
constexpr float kPi=3.14159265f;

// ---- EDF6 calls of the finish (2026-10-15; each fingerprinted below, H unless noted) ---------------------------------------
// Material parameter binding (0x60 bytes: +8 {material, parameter index} pairs, data / capacity / count; +0x28 the defaults read
// from the first material at bind time, count +0x38; +0x48 the values): ctor 0x6DCBF0, dtor 0x3799B0, bind 0x6DCC80(binding,
// model component + 0x160, material name or null for every material that has it, parameter name), set every value 0x6DD2F0,
// apply 0x6DD420 (0x6DCB30 writes the values into each material's constants and marks it dirty). UfoSmall507: binds in its
// ctor (0x528B79), applies in slot 3 (0x52EEC0) right before the model draw.
constexpr std::size_t kBindCtor=0x6DCBF0,kBindDtor=0x3799B0,kBind=0x6DCC80,kBindSet=0x6DD2F0,kBindApply=0x6DD420;
constexpr std::size_t kModelMaterials=0x160;   // model component + 0x160 (507: +0xD60 for its model at +0xC00; the carrier's own 0x6DC7B0 call: +0x7B0)
// UfoCarrier508's CAS players (model +0x650 + 0x330) and its main player's index (the carrier's state functor 0x4F823D);
// SetState 0x116C2D0 (the same as edf41classes PlayState with these offsets).
constexpr std::size_t kSetState=0x116C2D0,kCarrierCas=0x980,kCarrierMainPlayer=0xB30;
constexpr std::size_t kCarrierFly=0xE10;       // the carrier's fly hum handle (0x4F7DE0 plays se_fly_name into it; its death 0x4F9891 stops it)
constexpr std::size_t kUnregister=0x54DDB0;    // GameObjectBase: off its team's registry, team -1 (the carrier's 0x1000000F does it)
constexpr std::uint32_t kMsgKill=0x1000000Fu;  // the carrier's slot 9: durability to 0 and its death state (0x4F94C0)
constexpr int kSlotDraw=3,kSlotDamaged=18;     // slot 3: draw (EAO 0x5B6D10, carrier 0x4F9530 -> 0x4F74A0); slot 18 OnDamaged(obj, GDI)
struct MPrint { std::size_t rva; unsigned char bytes[8]; };
constexpr MPrint kMotherPrints[]={
    {kBindCtor,{0x33,0xC0,0x48,0x89,0x41,0x08,0x48,0x89}},{kBindDtor,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kBind,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},{kBindSet,{0x45,0x33,0xC0,0x4C,0x39,0x41,0x38,0x74}},
    {kBindApply,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},{kSetState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kUnregister,{0x40,0x53,0x48,0x83,0xEC,0x20,0x83,0xB9}},
    {0x4F9250,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},   // UfoCarrier508 slot 18, replaced
    {0x4F9530,{0xE9,0x6B,0xDF,0xFF,0xFF,0xCC,0xCC,0xCC}},   // its slot 3
    {0x5B6D10,{0x48,0x81,0xC1,0xC0,0x05,0x00,0x00,0xE9}},   // EventAnimationObject slot 3
    // the particle emitter (vents, below)
    {0x300C80,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83}},{0x2E8510,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {0x1120820,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},{0x301290,{0x48,0x89,0x4C,0x24,0x08,0x53,0x48,0x83}},
    {0x302200,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},{0x302EE0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
    {0x302D10,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},{0x12D85B0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {0x301460,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // the emitter's deleting destructor (vtable 0x17A8F70 [0])
};
// ---- EDF6's particle emitter (4.1's mp_HatchEmission emitters; EDF6's UfoMother511 builds the same for its port_effect_list,
// 0x502360 / 0x5086F0 / 0x508370, H). The description block (0x300C80 ctor) has 4.1's layout (4.1 0x1C1920) from +0x00 to +0x11C:
// +0x08 / +0x0C life frames, +0x18 seed, +0x20 drag, +0x50 enabled, +0x60 position, +0x70 direction, +0x80 frames between
// emissions, +0x94..+0xC8 spread / speed / size, +0xE0 start colour, +0x100 end colour; the texture handle at +0x128 (EDF6;
// 4.1 +0x120), filled by EffectGenUtil 0x2E8510(egu, handle, name) and released with 0x1120820. The emitter: new(0x300),
// ctor 0x301290, 0x302200(emitter, &desc, &identity 0x1FFF3D0, 0) copies the description to emitter+0x70; each frame
// 0x302EE0(emitter+0x70, dt) emits and moves the particles, the draw slot 0x302D10(emitter, &identity) submits them; vtable[0]
// (emitter, 1) deletes it.
constexpr std::size_t kDescCtor=0x300C80,kTexture=0x2E8510,kTextureFree=0x1120820,kEmitterCtor=0x301290,kEmitterInit=0x302200,
    kEmitterStep=0x302EE0,kEmitterDraw=0x302D10,kGameNew=0x12D85B0,kIdentity=0x1FFF3D0,kEffectsAt=0x20B2980,
    kEmitterVtable=0x17A8F70,kEmitterDelete=0x301460;
constexpr std::size_t kEmitterDesc=0x70;   // emitter+0x70: its copy of the description
std::atomic<int> motherPrims{0};
bool MotherPrims() noexcept {
    const int s=motherPrims.load();
    if(s)return s==1;
    for(const MPrint& p:kMotherPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41MOTHER the code at EDF.dll+0x%zX is not what this was written for: glow, the hull's states and its 4.1 death are off",p.rva);
            motherPrims=2;
            return false;
        }
    motherPrims=1;
    return true;
}
using DrawFn=void(__fastcall*)(void*,void*);
using DamagedFn=void(__fastcall*)(void*,void*);

// ---- the rest poses (src/edf41mother_pose.h; docs/feedback-2026-10-17-edf41-mother.md) ----------------------------------------
// The game's own setters of a CAS player (cas, player index, value; H): speed +0x100 (0x116DF20, as EDF6_Martian's death 0x479677
// stops its main player) and time +0xE8 (0x116DF40). SetState (0x116BFC0) resets the time, never the speed. Players: an array of
// 0x160 at *(cas + 0x60), the clip length at +0xF0. The main player: EventAnimationObject cas +0x8F0, index [+0xC80];
// UfoCarrier508 cas +0x980, index [+0xB30].
constexpr std::size_t kCasSetSpeed=0x116DF20,kCasSetTime=0x116DF40,kEaoCas=0x8F0,kEaoMainPlayer=0xC80;
constexpr MPrint kPosePrints[]={
    {kCasSetSpeed,{0x8B,0xC2,0x48,0x69,0xD0,0x60,0x01,0x00}},{kCasSetSpeed+0xD,{0xF3,0x0F,0x11,0x94,0x02,0x00,0x01,0x00}},
    {kCasSetTime,{0x8B,0xC2,0x48,0x69,0xD0,0x60,0x01,0x00}},{kCasSetTime+0xD,{0xF3,0x0F,0x11,0x94,0x02,0xE8,0x00,0x00}},
};
std::atomic<int> posePrims{0};
bool PosePrims() noexcept {
    const int s=posePrims.load();
    if(s)return s==1;
    for(const MPrint& p:kPosePrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41MOTHER the code at EDF.dll+0x%zX is not what this was written for: the rest poses are off (the hull opens, the cannons unfold at spawn)",p.rva);
            posePrims=2;
            return false;
        }
    posePrims=1;
    return true;
}
// The main player's clip length (frames) and index; false when the object has no readable player.
bool MainClip(void* obj,std::size_t casAt,std::size_t indexAt,int* index,float* length) noexcept {
    const char* o=static_cast<const char*>(obj);
    if(!Readable(o+casAt+0x60,8) || !Readable(o+indexAt,4))return false;
    const char* players=*reinterpret_cast<const char* const*>(o+casAt+0x60);
    const int i=*reinterpret_cast<const std::int32_t*>(o+indexAt);
    if(!players || i<0 || i>64 || !Readable(players+static_cast<std::size_t>(i)*0x160+0xE8,0x10))return false;
    *index=i;
    std::memcpy(length,players+static_cast<std::size_t>(i)*0x160+0xF0,4);
    return std::isfinite(*length);
}
// 4.1's rest pose on the clip the EDF6 constructor started (edf41pose::RestHold).
bool HoldRest(void* obj,edf41pose::Group g,std::size_t casAt,std::size_t indexAt,const char* what) noexcept {
    const edf41pose::Hold hold=edf41pose::RestHold(g);
    int index=-1;
    float length=0.0f;
    if(hold==edf41pose::Hold::kNone || !PosePrims() || !MainClip(obj,casAt,indexAt,&index,&length))return false;
    char* cas=static_cast<char*>(obj)+casAt;
    using SetFn=void(__fastcall*)(void*,int,float);
    reinterpret_cast<SetFn>(image+kCasSetTime)(cas,index,edf41pose::HoldTime(hold,length));
    if(hold==edf41pose::Hold::kFirstKeyStill)reinterpret_cast<SetFn>(image+kCasSetSpeed)(cas,index,edf41pose::HoldSpeed(hold,1.0f));
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6)Log("EDF41MOTHER %s %p: 4.1's rest pose held (%s of its %.0f-frame clip)",what,obj,
        hold==edf41pose::Hold::kLastKey ? "the end" : "the start, stopped",length);
    return true;
}
// ---- the parts' hit layer (src/edf41_hitlayer.h; H unless noted) ------------------------------------------------------------
// A body's collision filter info: 0x11B1240(body handle) (handle = ragdoll body +0x50; the world's body +0x8C); a ragdoll's bodies
// to a layer, keeping their groups: 0x6E9320(ragdoll, layer) (0x106170 recomposes, 0x11B1840 sets); the area damage's query filter:
// 0x106070 = `xor edx,edx; xor r9d,r9d; xor r8d,r8d; lea ecx,[rdx+13]; jmp 0xD929F0` (the layer is the byte at +0xA). The filter
// itself (hkpGroupFilterBase<Config<5,5,5,16>>, vtable 0x17E0788; its constructor 0xA3E710 fills the 32-word lookup table at +0x58
// with ones, the game's own copy comes from data, 0x67DE40) is found from the physics world ([0x20B2958] + 0x68, the object
// GroundHit and the gravity read use) by its vtable, read only.
constexpr std::size_t kBodyFilterInfo=0x11B1240,kRagdollSetLayer=0x6E9320,kAreaQueryFilter=0x106070,kSceneAt=0x20B2958,
    kScenePhysics=0x68,kGroupFilterVtable=0x17E0788,kGroupFilterTable=0x58,kGroupFilterCtor=0xA3E710;
constexpr MPrint kLayerPrints[]={
    {kBodyFilterInfo,{0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00}},{kRagdollSetLayer,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},
    {kAreaQueryFilter,{0x33,0xD2,0x45,0x33,0xC9,0x45,0x33,0xC0}},
    {kGroupFilterCtor+0x24,{0x48,0x8D,0x4B,0x58,0x48,0x8D,0x05,0x71}},   // lea rcx,[rbx+0x58]: the table
};
std::atomic<int> layerPrims{0};
bool LayerPrims() noexcept {
    const int s=layerPrims.load();
    if(s)return s==1;
    bool ok=true;
    for(const MPrint& p:kLayerPrints)ok=ok && Readable(image+p.rva,8) && std::memcmp(image+p.rva,p.bytes,8)==0;
    // the query filter's tail: lea ecx,[rdx+imm8]; jmp
    ok=ok && Readable(image+kAreaQueryFilter+8,4) && image[kAreaQueryFilter+8]==0x8D && image[kAreaQueryFilter+9]==0x4A && image[kAreaQueryFilter+0xB]==0xE9;
    if(!ok)Log("EDF41MOTHER the collision layer code is not what this was written for: the parts keep EventAnimationObject's layer (rockets do not reach them)");
    layerPrims=ok ? 1 : 2;
    return ok;
}
int AreaQueryLayer() noexcept { return static_cast<int>(image[kAreaQueryFilter+0xA]); }
// The layer of a ragdoll's first body (-1: none).
int RagdollLayer(void* ragdoll,std::size_t* count=nullptr) noexcept {
    const char* r=static_cast<const char*>(ragdoll);
    if(!Readable(r+0x78,0x18))return -1;
    const char* data=*reinterpret_cast<const char* const*>(r+0x78);
    const std::size_t n=*reinterpret_cast<const std::size_t*>(r+0x88);
    if(count)*count=n;
    if(!data || !n || n>4096 || !Readable(data+0x50,8))return -1;
    void* handle=*reinterpret_cast<void* const*>(data+0x50);
    if(!handle || !Readable(static_cast<char*>(handle)+0xF0,0x18))return -1;
    return edf41layer::LayerOf(reinterpret_cast<std::uint32_t(__fastcall*)(void*)>(image+kBodyFilterInfo)(handle));
}
// The game's group filter table, found from the physics world (two pointer levels, or embedded), cached per physics world.
const std::uint32_t* GroupFilterTable() noexcept {
    static const char* seenPhysics=nullptr;
    static const std::uint32_t* table=nullptr;
    const char* scene=Readable(image+kSceneAt,8) ? *reinterpret_cast<const char* const*>(image+kSceneAt) : nullptr;
    const char* physics=scene && Readable(scene+kScenePhysics,8) ? *reinterpret_cast<const char* const*>(scene+kScenePhysics) : nullptr;
    if(!physics)return nullptr;
    if(physics==seenPhysics)return table;
    seenPhysics=physics;
    table=nullptr;
    const void* vt=image+kGroupFilterVtable;
    const auto filterAt=[&](const char* q) -> const std::uint32_t* {
        if(!q || !Readable(q,kGroupFilterTable+edf41layer::kLayers*4))return nullptr;
        return *reinterpret_cast<const void* const*>(q)==vt ? reinterpret_cast<const std::uint32_t*>(q+kGroupFilterTable) : nullptr;
    };
    for(std::size_t i=0;i<0x400 && !table && Readable(physics+i,8);i+=8) {
        const char* q=*reinterpret_cast<const char* const*>(physics+i);
        if((table=filterAt(physics+i))!=nullptr || (table=filterAt(q))!=nullptr)break;
        if(!q || (reinterpret_cast<std::uintptr_t>(q)&7) || !Readable(q,0x800))continue;
        for(std::size_t j=0;j<0x800 && !table;j+=8) {
            const char* r=*reinterpret_cast<const char* const*>(q+j);
            if(!(reinterpret_cast<std::uintptr_t>(r)&7))table=filterAt(r);
        }
    }
    Log("EDF41MOTHER collision filter (vtable EDF+0x%zX) %s%s",kGroupFilterVtable,table ? "found, lookup table at " : "not found from the physics world",
        table ? "" : " (the parts go to the hull's layer)");
    if(table)Log("EDF41MOTHER   table %p: word 13 %08X, word 7 %08X, word 0x11 %08X",static_cast<const void*>(table),table[13],table[7],table[0x11]);
    return table;
}
// A cannon's appear (4.1 0x259D20): from its start at speed 1, whatever the hold left (SetState keeps the clock of the state
// already playing, and the hold stopped it).
void PlayAppear(void* obj) noexcept {
    PlayState(obj,L"appear");
    int index=-1;
    float length=0.0f;
    if(!PosePrims() || !MainClip(obj,kEaoCas,kEaoMainPlayer,&index,&length))return;
    char* cas=static_cast<char*>(obj)+kEaoCas;
    using SetFn=void(__fastcall*)(void*,int,float);
    reinterpret_cast<SetFn>(image+kCasSetTime)(cas,index,edf41pose::kAppearTime);
    reinterpret_cast<SetFn>(image+kCasSetSpeed)(cas,index,edf41pose::kAppearSpeed);
}

Clone hullClass{"UfoCarrier508",kCarrierVtable,kSlots,kCarrierSlots,{},{},{}};
Clone partClass{"EventAnimationObject",kEaoVtable,kPartSlots,kEaoSlots,{},{},{}};
std::atomic<int> created{0};

void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[32];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<32)said[count++]=what;
    Log("EDF41MOTHER check failed: %s",what);
}
bool Rd(const void* p,std::size_t size,const char* what) noexcept {
    if(Readable(p,size))return true;
    Fail(what);
    return false;
}
float F(const void* o,std::size_t at) noexcept { float v=0.0f; if(Readable(static_cast<const char*>(o)+at,4))std::memcpy(&v,static_cast<const char*>(o)+at,4); return v; }
void SetF(void* o,std::size_t at,float v) noexcept { if(Readable(static_cast<char*>(o)+at,4))std::memcpy(static_cast<char*>(o)+at,&v,4); }
std::int32_t I(const void* o,std::size_t at) noexcept { std::int32_t v=0; if(Readable(static_cast<const char*>(o)+at,4))std::memcpy(&v,static_cast<const char*>(o)+at,4); return v; }

void RotY(float a,float* m) noexcept {
    const float c=std::cos(a),s=std::sin(a);
    const float r[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
    std::memcpy(m,r,sizeof(r));
}
void Translate(float x,float y,float z,float* m) noexcept {
    const float r[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1};
    std::memcpy(m,r,sizeof(r));
}
// p (a point in the matrix's space) in world space.
void Point(const float* m,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*m[k]+p[1]*m[4+k]+p[2]*m[8+k]+m[12+k];
}
void Direction(const float* m,const float* d,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=d[0]*m[k]+d[1]*m[4+k]+d[2]*m[8+k];
}
// A direction within `angle` of `axis` (4.1 0x51B7C0: uniform angle in [0, angle], uniform azimuth).
void Cone(const float* axis,float angle,std::uint64_t& rng,float* out) noexcept {
    float a[3]={axis[0],axis[1],axis[2]};
    const float l=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    if(l<1e-6f) { out[0]=0; out[1]=-1; out[2]=0; return; }
    for(float& v:a)v/=l;
    float m[16];
    const float zero[3]={0,0,0};
    AimMatrix(zero,a,m);
    const float t=angle*Random01(rng),s=2.0f*kPi*Random01(rng);
    const float local[3]={std::sin(t)*std::cos(s),std::sin(t)*std::sin(s),std::cos(t)};
    Direction(m,local,out);
}

// ---- the glow (4.1: light0_color eased to a target, scroll_light0 turned 1/600 per frame) ----------------------------------
struct Glow {
    void* light=nullptr;     // our bindings (0x60 bytes each, game-allocated insides): light0_color, scroll_light0
    void* scroll=nullptr;
    int tries=0;
    bool said=false;
    float cur[4]={2.0f,0.0f,0.0f,1.0f},target[4]={2.0f,0.0f,0.0f,1.0f};   // 4.1's start (hull 0x250730, parts 0x25E5E0)
    float rate=0.01f,time=0.0f;
};
std::int64_t BindCount(const void* b) noexcept {
    std::int64_t n=0;
    if(b && Readable(static_cast<const char*>(b)+0x18,8))std::memcpy(&n,static_cast<const char*>(b)+0x18,8);
    return n;
}
void* NewBinding() noexcept {
    void* b=::operator new(0x60,std::nothrow);
    if(!b)return nullptr;
    std::memset(b,0,0x60);
    reinterpret_cast<void*(__fastcall*)(void*)>(image+kBindCtor)(b);
    return b;
}
void FreeBinding(void*& b) noexcept {
    if(!b)return;
    reinterpret_cast<void(__fastcall*)(void*)>(image+kBindDtor)(b);
    ::operator delete(b);
    b=nullptr;
}
void FreeGlow(Glow& g) noexcept { FreeBinding(g.light); FreeBinding(g.scroll); }
// Bound once the model's materials exist (tried for up to 600 frames): every material of the model that has the parameter.
void BindGlow(Glow& g,void* object,std::size_t model,const char* what) noexcept {
    if(!MotherPrims() || (BindCount(g.light)>0 && BindCount(g.scroll)>0) || g.tries>600)return;
    ++g.tries;
    char* materials=static_cast<char*>(object)+model+kModelMaterials;
    if(!Readable(materials,0x40))return;
    using BindFn=void(__fastcall*)(void*,void*,const wchar_t*,const wchar_t*);
    if(!g.light)g.light=NewBinding();
    if(!g.scroll)g.scroll=NewBinding();
    if(g.light && BindCount(g.light)==0)reinterpret_cast<BindFn>(image+kBind)(g.light,materials,nullptr,L"light0_color");
    if(g.scroll && BindCount(g.scroll)==0)reinterpret_cast<BindFn>(image+kBind)(g.scroll,materials,nullptr,L"scroll_light0");
    if(!g.said && (g.tries>600 || (BindCount(g.light)>0 && BindCount(g.scroll)>0))) {
        g.said=true;
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8 || std::strcmp(what,"hull")==0)Log("EDF41MOTHER %s %p glow: light0_color on %lld materials, scroll_light0 on %lld (after %d frames)",what,object,
                                   static_cast<long long>(BindCount(g.light)),static_cast<long long>(BindCount(g.scroll)),g.tries);
    }
}
void TickGlow(Glow& g) noexcept {
    for(int k=0;k<3;++k)g.cur[k]+=(g.target[k]-g.cur[k])*g.rate;   // 4.1 0x25E680 / 0x251AB8: x, y, z ease; w stays
    g.time+=1.0f/600.0f;
}
void SetGlow(Glow& g,float r,float gr,float b,float w) noexcept { g.cur[0]=r; g.cur[1]=gr; g.cur[2]=b; g.cur[3]=w; }
void GlowTo(Glow& g,float r,float gr,float b,float w) noexcept { g.target[0]=r; g.target[1]=gr; g.target[2]=b; g.target[3]=w; }
void Flash(Glow& g,float rate) noexcept { SetGlow(g,100.0f,100.0f,100.0f,1.0f); GlowTo(g,0.0f,0.0f,0.0f,1.0f); g.rate=rate; }
void AtLeast(Glow& g,float v) noexcept { for(int k=0;k<3;++k)g.cur[k]=(std::max)(g.cur[k],v); }
// Slot 3, before the stock draw: light0_color = the colour; scroll_light0 = its default + (0, time) (4.1 adds +0x990 / +0xA00).
void ApplyGlow(Glow& g) noexcept {
    if(!MotherPrims())return;
    using SetFn=void(__fastcall*)(void*,const float*);
    using ApplyFn=void(__fastcall*)(void*);
    if(BindCount(g.light)>0) {
        alignas(16) const float c[4]={g.cur[0],g.cur[1],g.cur[2],g.cur[3]};
        reinterpret_cast<SetFn>(image+kBindSet)(g.light,c);
        reinterpret_cast<ApplyFn>(image+kBindApply)(g.light);
    }
    if(BindCount(g.scroll)>0) {
        const char* s=static_cast<const char*>(g.scroll);
        const float* def=*reinterpret_cast<const float* const*>(s+0x28);
        std::int64_t n=0;
        std::memcpy(&n,s+0x38,8);
        if(n>0 && def && Readable(def,16)) {
            alignas(16) const float v[4]={def[0],def[1]+g.time,def[2],def[3]};
            reinterpret_cast<SetFn>(image+kBindSet)(g.scroll,v);
            reinterpret_cast<ApplyFn>(image+kBindApply)(g.scroll);
        }
    }
}

// ---- the records ------------------------------------------------------------------------------------------------
std::atomic<std::uint64_t> pvpSerial301{0};
std::uint64_t NextPvpIncarnation301() noexcept {return ++pvpSerial301;}
enum Group { kHatch=0, kCover=1, kCannon=2, kBigCannon=3, kPanel=4 };
const char* const kGroupNames[5]={"HatchLow","HatchMiddle","Cannon","BigCannon","Panel"};

struct Wave { wchar_t path[260]{}; int count=0; int interval=1; int delay=0; float level=1.0f; bool active=false; int kind=1; };
struct SoundKey { float time=0.0f; int kind=0,channel=-1; std::wstring name,node; bool trace=false; };
struct SoundClip { int state=-1,clip=0; std::vector<SoundKey> events; };
struct BoneSound { SoundHandle handle; int bone=-1; bool trace=false; };
struct AnimationSounds {
    bool loaded=false;
    std::vector<SoundClip> clips;
    edf41sound::Timeline timeline;
    edf41sound::Channels<BoneSound> channels;
};
struct Part {
    std::uint64_t pvpIncarnation=NextPvpIncarnation301();
    void* hull=nullptr;
    float local[16];                 // A: the entry's euler and position
    int group=0,index=0,ring=0;
    bool enabled=false,attack=false,boost=false,statik=false;
    bool sized=false,dead=false,falling=false,gone=false;
    bool registered=false,teamInitialized=false;
    std::int32_t team=-99;
    float fallVel[3]={},lastPos[3]={};
    bool haveLast=false;
    int state=0,frames=0;            // the main machine
    int beamState=0,beamFrames=0,beamNext=0;
    bool roundStarted=false;
    float spin=0.0f;                 // main cannon: its own axis (4.1 +0xE54); panel: the ring (+0xB34)
    float ringFactor=0.0f,push=0.0f; // panel +0xB3C, +0xB58
    bool started=false,firstStart=false;
    float explodeDelay=0.0f;         // panel: 3..7 s after its death
    int deadFrames=0;
    int type=0;                      // panel mp_type
    int waitFrames=0;                // cannon +0x11C8
    float phi=0.0f;                  // main cannon sweep plane
    int damageCooldown=0;
    float openFrames=120.0f;         // hatch: its open clip
    int closeWait=0;
    bool permit=false,waveDone=false;
    Wave current,permanent;
    int remaining=0,intervalLeft=0,delayLeft=0,made=0;
    LocatorHandle spawnAt{};
    bool spawnTried=false;
    int cannonBone=-2,hatchBones[4]={-2,-2,-2,-2};
    float beamOffsets[4][3]={};
    SoundHandle chargeLoop;
    AnimationSounds animationSounds;
    std::uint64_t rng=0;
    Glow glow;                       // 4.1 PartsBase +0xA10 colour, +0xA20 target, +0xA30 rate, +0xA04 scroll
    bool hullDeathSeen=false;        // 4.1 +0x905: the hull's first frame at durability <= 0 (flash, then dark)
    int devBone=-2;                  // development (Edf41DevShoot): the bone the shots aim at
    void* lock=nullptr;              // its lock point (4.1: the MAB lock target ターゲット, lockable while 0x250520 says so)
    int lockBone=-2;
    bool restHeld=false;             // 4.1's rest pose put on the constructor's clip (edf41pose; once, on the first frame)
    enemy_legacy::Control control;
    std::uint64_t manualLease=0,manualSeed=0;
    bool manualSeedSet=false;
    bool savedEnabled=false,savedAttack=false,savedBoost=false,controlledWave=false,manualBeam=false,panelOnce=false;
    std::vector<mother_control::Command> controlInbox;
    bool configuredOnce=false;
    mother_control::SpawnWave spawnWave;
    Wave configured;                 // only a mission-authored generator message may supply this path
};
// The hull's main state machine (4.1 agent +0xB10, initial 0x252700; ticked only while it lives).
enum { kHullClosed=1,kHullOpening,kHullOpen,kHullHidden,kHullClosing };
// Its death in 4.1's three sequencer functors: A 0x251F30 (explosions, 10 s), B 0x2522C0 (the fall), C 0x2526E0 (2 s), then gone.
enum { kDeathNone=0,kDeathExplode,kDeathFall,kDeathLanded,kDeathGone };
struct Hull {
    std::vector<void*> parts;        // creation order
    bool built=false,dead=false;
    std::uint32_t ticks=0;
    float yawRate=0.0f,yawTarget=0.0003f;   // 4.1 +0xA14 / +0xA24 (the ctor queues action 2)
    int throttle[6]={};
    int hitFrames=0;                 // 4.1 +0xB70
    int weakBone=-2,weakBody=-2;
    std::vector<std::pair<int,Wave>> pending;   // generators that came before the parts were made
    std::uint64_t rng=0;
    Glow glow;                       // 4.1 +0x9A0 colour, +0x9B0 target, +0x9C0 rate, +0x994 scroll
    int mainState=kHullClosed,mainFrames=0;
    bool damaged=false;              // 4.1 +0xB38: hit since the weak point opened
    int hideWait=540;                // 4.1 +0xB3C: frames left (counted while damaged) before it hides
    bool vents=false;                // the 16 exhaust vents (mp_HatchEmission) on
    float ventSize=2.0f;             // 4.1 0x253750: 2 x durability / max
    int death=kDeathNone,deathFrames=0;
    std::uint32_t counter=0;         // 4.1 +0x8BC: frames, reset as each death sequence starts
    float vel[3]={},lastPos[3]={},deadPos[3]={};
    bool haveLast=false;
    SoundHandle fly,fall;            // 母船UFO飛行 (4.1 +0xC10), 母船UFO本体墜落 (+0xCA0)
    bool carrierHumSaid=false;
    void* vent[16]={};               // EDF6 particle emitters for mp_HatchEmission (4.1 +0xB48, 0x300 each)
    bool ventsMade=false;
    // 4.1's movement while alive (0x2512C0): its own position, velocity, the last route point (+0x9D0), yaw (+0xA04)
    bool moveInit=false,routeSeen=false;
    float pos[3]={},point[3]={},yaw=0.0f;
    bool restHeld=false;             // closed as 4.1's, not the carrier's close_state (= the mothership's close clip) from open
    enemy_legacy::Control control;
    float savedYawTarget=0,manualYawLast=0;
    std::vector<mother_control::Command> controlInbox;
};
std::recursive_mutex lock;
int shotCount[5]={},removedCount[5]={};   // bullets fired and parts removed, for the periodic line
std::unordered_map<void*,Hull> hulls;
std::unordered_map<void*,Part> parts;

// Liveness by the objects' own weak references (edf41c::WeakRef), keyed by object. A deleted object (DeleteObject: bit 4 of +0x18)
// stays allocated until the scene frees it, and only then does its destructor (slot 1, HullDtor / PartDtor) drop its record: in
// between, and for the parts of a hull destroyed this frame, the weak reference is what says it is gone. Until 2026-10-17 the
// destructor wrapper sat on slot 0, which is no destructor, so no record was ever dropped by it (M123: the script deleted the
// mothership at the end of its route and its parts read freed memory; the weak references were the fix). The wrapper is on the
// real destructor now; the weak references stay for the time between the deletion and the free, which no destructor sees.
std::unordered_map<void*,WeakRef> refs;

// ---- online (edf41net): declared here, defined in the block "online" at the end of this namespace ----
bool NetDecides(void* part) noexcept;                       // false on a guest copy of a registered part: its fire timer is the host's
void NetCannonFire(void* part,const Part& p) noexcept;      // the deciding copy: a cannon's charge started, with its next wait
void NetPart(void* hull,void* part,int group,std::size_t order) noexcept;   // a part's child identity (its creation order)
void NetSpawn(const Part& p,void* enemy) noexcept;          // a hatch's enemy
bool ApplyControlCommand(void*,const mother_control::Command&) noexcept;
void ReplayControl301(void*) noexcept;
void ControlTick(void* object) noexcept;
void NetControlIn(void* object) noexcept;
void NetPartIn(void* part,Part& p) noexcept;                // a part's frame: the host's decisions
void NetForget(void* part) noexcept;
void Forget(void* o) noexcept { const auto it=refs.find(o); if(it!=refs.end()) { WeakReset(it->second); refs.erase(it); } }
void Track(void* o) noexcept { Forget(o); WeakSet(refs[o],o); }
bool Live(void* o) noexcept { const auto it=refs.find(o); return it!=refs.end() && WeakAlive(it->second); }
Hull* HullOf(void* h) noexcept { const auto it=hulls.find(h); return it==hulls.end() ? nullptr : &it->second; }
Part* PartOf(void* p) noexcept { const auto it=parts.find(p); return it==parts.end() ? nullptr : &it->second; }
void StopAnimationSounds(Part& p,int fade) noexcept {
    p.animationSounds.channels.StopAll(fade,[](BoneSound& sound,int frames) { StopSound(sound.handle,frames); });
    p.animationSounds.timeline.Reset();
}
// A record's own resources go with it (bindings, loops still playing): every erase of a record goes through these.
void DropPart(void* o) noexcept {
    const auto it=parts.find(o);
    if(it==parts.end())return;
    it->second.spawnWave.Finish();
    FreeGlow(it->second.glow);
    StopAnimationSounds(it->second,0);
    StopSound(it->second.chargeLoop);
    parts.erase(it);
}
// The vents' emitters (4.1 0x252FD0): one per vent, 4.1's description (煙_01, 300-frame life, drag 0.99, one emission every 8
// frames, sizes 16 -> 24, white (2,2,2) from alpha 0.35 to 0), straight along the direction the frame gives.
// The emitters are on by default (ini Edf41MotherForce bit 2048 = EffectGenUtil puffs instead). The exit crash first blamed on
// them (EDF.dll+0x7ADCFB / 0x7ADCEE, 2026-10-16) was edf41sound's weapon bank reference outliving the sound system: fixed there
// (ReleaseWeaponBank, docs/edf41-classes.md 12). The emitters only changed whether the exit got far enough to show it.
// Freeing: an emitter is stepped (Vents, hull Update) and drawn (HullDraw) by this file only, under `lock`; a dropped hull's
// emitters wait kVentGraveFrames hull updates in the graveyard (a submitted draw is not reused by a frame in flight), then go
// through their own deleting destructor (vt[0](e, 1) = 0x301460: texture handles 0x1120820, buffers, operator delete 0x300).
// What is left at the game's shutdown is deleted right before the application destructor (edf41c::AtGameShutdown), while
// the texture and effect managers still exist, and every loop still playing (a hull's fly / fall, a cannon's charge) is stopped
// there while the sound system exists; a hull or part destroyed later than that (inside the application destructor) only drops
// its record: its emitters are gone already (FreeVents only parks them), and StopSound does nothing from then on
// (edf41c::ShuttingDown).
constexpr int kVentGraveFrames=8;
struct DeadVents { void* e[16]; int frames; };
std::vector<DeadVents> ventGraveyard;
void DeleteEmitter(void* e) noexcept {
    if(!e)return;
    void** vt=*static_cast<void***>(e);
    if(vt!=reinterpret_cast<void**>(image+kEmitterVtable) || vt[0]!=static_cast<void*>(image+kEmitterDelete))return;   // 0x301290 sets it
    reinterpret_cast<void*(__fastcall*)(void*,int)>(image+kEmitterDelete)(e,1);
}
void FreeVents(Hull& h) noexcept {
    DeadVents d{};
    bool any=false;
    for(int k=0;k<16;++k) { d.e[k]=h.vent[k]; any=any || h.vent[k]; h.vent[k]=nullptr; }
    if(!any)return;
    d.frames=kVentGraveFrames;
    ventGraveyard.push_back(d);
}
void VentGraveyardTick() noexcept {   // once per hull Update, under `lock`
    for(std::size_t i=0;i<ventGraveyard.size();) {
        if(--ventGraveyard[i].frames>0) { ++i; continue; }
        for(void* e:ventGraveyard[i].e)DeleteEmitter(e);
        ventGraveyard[i]=ventGraveyard.back();
        ventGraveyard.pop_back();
    }
}
void ShutdownVents() noexcept;   // below the hull records (edf41c::AtGameShutdown)
void MakeVents(Hull& h) noexcept {
    if(h.ventsMade || !MotherPrims() || (Cfg().edf41MotherForce&2048))return;   // bit 2048: EffectGenUtil puffs instead of the emitters
    h.ventsMade=true;
    void* egu=Readable(image+kEffectsAt,8) ? *reinterpret_cast<void* const*>(image+kEffectsAt) : nullptr;
    if(!egu) { h.ventsMade=false; return; }
    int made=0;
    for(int k=0;k<16;++k) {
        alignas(16) unsigned char d[0x200]={};
        reinterpret_cast<void*(__fastcall*)(void*)>(image+kDescCtor)(d);
        const auto f=[&](std::size_t at,float v) { std::memcpy(d+at,&v,4); };
        const auto n=[&](std::size_t at,std::int32_t v) { std::memcpy(d+at,&v,4); };
        n(0x08,300); n(0x0C,300);
        std::uint64_t seed=h.rng+static_cast<std::uint64_t>(k)*0x9E3779B97F4A7C15ull;
        std::memcpy(d+0x18,&seed,8);
        f(0x20,0.99f);
        d[0x50]=0;                                   // off until the weak point opens
        n(0x80,8);
        n(0x94,0); n(0x98,0);
        f(0xAC,0.0f); f(0xB0,0.1f); f(0xB8,0.5f); f(0xBC,2.5f); n(0xC0,5); f(0xC4,16.0f); f(0xC8,24.0f);
        const float c0[4]={2.0f,2.0f,2.0f,0.35f},c1[4]={2.0f,2.0f,2.0f,0.0f};
        std::memcpy(d+0xE0,c0,16);
        std::memcpy(d+0x100,c1,16);
        reinterpret_cast<void*(__fastcall*)(void*,void*,const wchar_t*)>(image+kTexture)(egu,d+0x128,L"煙_01.dds");
        void* e=reinterpret_cast<void*(__fastcall*)(std::size_t)>(image+kGameNew)(0x300);
        if(e) {
            reinterpret_cast<void*(__fastcall*)(void*)>(image+kEmitterCtor)(e);
            reinterpret_cast<void(__fastcall*)(void*,const void*,const void*,int)>(image+kEmitterInit)(e,d,image+kIdentity,0);
            h.vent[k]=e;
            ++made;
        }
        if(!(Cfg().edf41MotherForce&1024))reinterpret_cast<void(__fastcall*)(void*)>(image+kTextureFree)(d+0x128);   // bit 1024: bisecting
    }
    Log("EDF41MOTHER vents: %d particle emitters made (EDF6 0x301290, 4.1's 煙_01 description)",made);
}
void DrawVents(Hull& h) noexcept {
    if(Cfg().edf41MotherForce&512)return;
    for(void* e:h.vent)if(e)reinterpret_cast<void(__fastcall*)(void*,const void*)>(image+kEmitterDraw)(e,image+kIdentity);
}
void DropHull(void* o) noexcept {
    const auto it=hulls.find(o);
    if(it==hulls.end())return;
    FreeVents(it->second);
    FreeGlow(it->second.glow);
    StopSound(it->second.fly);
    StopSound(it->second.fall);
    hulls.erase(it);
}
void ShutdownVents() noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);
    int loops=0;
    for(auto& kv:hulls) {
        loops+=(kv.second.fly.instance!=nullptr)+(kv.second.fall.instance!=nullptr);
        StopSound(kv.second.fly,0);
        StopSound(kv.second.fall,0);
    }
    for(auto& kv:parts) {
        loops+=!kv.second.animationSounds.channels.Empty();
        StopAnimationSounds(kv.second,0);
        loops+=kv.second.chargeLoop.instance!=nullptr;
        StopSound(kv.second.chargeLoop,0);
    }
    if(loops)Log("EDF41MOTHER the game is shutting down: %d loops stopped first",loops);
    int n=0;
    for(auto& kv:hulls)
        for(void*& e:kv.second.vent)
            if(e) { DeleteEmitter(e); e=nullptr; ++n; }
    for(DeadVents& d:ventGraveyard)
        for(void* e:d.e)
            if(e) { DeleteEmitter(e); ++n; }
    ventGraveyard.clear();
    if(n)Log("EDF41MOTHER the game is shutting down: %d vent emitters deleted first",n);
}
bool Throttle(Hull& h,int slot) noexcept { const int old=h.throttle[slot]; h.throttle[slot]=16; return old==0; }
// The hull's team as its parts take it (4.1 0x250520): -1 or 3 (neutral) -> 1, else the hull's.
std::int32_t PartTeam(void* hull) noexcept { const std::int32_t t=I(hull,kTeamAt); return t==-1 || t==3 ? 1 : t; }
bool HullAlive(void* hull) noexcept { return F(hull,kDurabilityAt)>0.0f; }
// A part's end (4.1 0x56BBC0): deleted by the game's own DeleteObject; the record goes when its destructor runs.
void Remove(void* obj,Part& p) noexcept {
    DeleteObject(obj);
    p.gone=true;
    ++removedCount[p.group];
    if(p.group==kBigCannon)Log("EDF41MOTHER main cannon %p removed: the hull's weak point can be hit",obj);
}

// A custom parameter node of a part's SGO, copied once for the bullets (keyed by the node: parts of one SGO share it).
const SgoNode* Custom(const SgoNode* node) noexcept {
    static std::unordered_map<const SgoNode*,const SgoNode*> copies;
    if(!node)return nullptr;
    const auto it=copies.find(node);
    if(it!=copies.end())return it->second;
    const SgoNode* c=Persist(node);
    copies[node]=c;
    return c;
}
bool FactoryName(void* obj,const wchar_t* key,wchar_t* out,std::size_t room,const wchar_t* fallback) noexcept {
    if(Text(Key(obj,key),out,room))return true;
    wcsncpy_s(out,room,fallback,_TRUNCATE);
    return false;
}
void PartPos(void* obj,float* out) noexcept { std::memcpy(out,static_cast<char*>(obj)+kPosAt,12); }
void Matrix(void* obj,float* out) noexcept { std::memcpy(out,static_cast<char*>(obj)+kMatrixAt,64); }

// ---- firing -------------------------------------------------------------------------------------------------------
const enemy_legacy::Control* ManualAim(const Part& p) noexcept {
    if(p.control.Active())return &p.control;
    const Hull* h=HullOf(p.hull);
    return h && h->control.Active() ? &h->control : nullptr;
}
bool AimAt(void* part,float* aim) noexcept {
    if(const Part* p=PartOf(part))if(const auto* c=ManualAim(*p)) {
        float pos[3];if(!AimPoint(part,pos)){PartPos(part,pos);pos[1]+=2.0f;}c->Aim(pos,1000.0f,aim);return true;
    }
    void* t=Target(part);
    if(t && AimPoint(t,aim))return true;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6) {
        const char* o=static_cast<const char*>(part);
        Log("EDF41MOTHER part %p has no target to fire at: team %d, flags +0x380 %08X, active %u, retarget %d/%d, target ptr %p",part,
            I(part,kTeamAt),static_cast<unsigned>(I(part,kFlags380)),static_cast<unsigned>(static_cast<unsigned char>(o[0x4A0])),I(part,0x510),I(part,0x514),t);
    }
    return false;
}

// The main cannon's shot f of the sweep (4.1 0x2570C0): direction (sin q, -cos q cos phi, -cos q sin phi), q = (f/30 - 1) 1.5,
// from local (0, -790, 0); every 8th a heavy one (blast radius 100, custom [2], the hit sound).
void BigCannonShot(void* obj,Part& p,int f) noexcept {
    float w[16];
    Matrix(obj,w);
    const float q=(static_cast<float>(f)/30.0f-1.0f)*1.5f;
    const float d[3]={std::sin(q),-std::cos(q)*std::cos(p.phi),-std::cos(q)*std::sin(p.phi)};
    const float o[3]={0.0f,-790.0f,0.0f};
    float from[3],dir[3],m[16];
    Point(w,o,from);
    Direction(w,d,dir);
    AimMatrix(from,dir,m);
    const bool heavy=f%8==0;
    const SgoNode* custom=Key(obj,L"mp_LaserCustomParameter");
    wchar_t factory[64];
    FactoryName(obj,L"mp_BulletFactory",factory,64,L"LaserBullet02");
    Bullet b;
    b.factory=factory;
    b.speed=20.0f; b.life=60; b.damage=5000.0f; b.size=50.0f;
    b.explosion=heavy ? 100.0f : 0.0f;
    b.custom=Custom(Child(custom,heavy ? 1 : 0));
    if(heavy) { b.hitSeTable=p.hull; b.hitSe=L"母船UFO砲台命中"; }   // 4.1 P+0xB0: every 8th, else cleared
    if(Fire(obj,m,b))++shotCount[kBigCannon];
}

void CannonRocket(void* obj,Part& p,float s) noexcept {
    if(p.cannonBone==-2) {
        wchar_t bone[32]=L"cannon";
        Text(Child(Key(obj,L"mpc_FireCannonSetting"),0),bone,32);
        p.cannonBone=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,bone);
    }
    float b[16];
    if(!BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.cannonBone,b)) { Fail("the cannon has no cannon bone"); return; }
    const float o[3]={0.0f,-360.0f,0.0f},down[3]={0.0f,-1.0f,0.0f};
    float from[3],dir[3],m[16];
    Point(b,o,from);
    Direction(b,down,dir);
    AimMatrix(from,dir,m);
    wchar_t factory[64];
    FactoryName(obj,L"mpc_CannonBulletFactory",factory,64,L"RocketBullet01");
    Bullet r;
    r.factory=factory;
    r.speed=0.01f; r.life=2700; r.damage=350.0f*s; r.explosion=80.0f; r.size=80.0f;
    const float color[4]={1.0f,0.5f,0.25f,1.0f};
    std::memcpy(r.color,color,16);
    r.custom=Custom(Key(obj,L"mp_CannonCustomParameter"));
    r.hitSeTable=p.hull; r.hitSe=L"母船UFO中型砲台着弾";   // 4.1 +0xE30 (P+0xB0)
    if(Fire(obj,m,r))++shotCount[kCannon];
}

void CannonBeam(void* obj,Part& p,float s) noexcept {
    const int k=p.beamNext;
    p.beamNext=(p.beamNext+1)%4;
    float aim[3];
    if(!AimAt(obj,aim))return;
    const SgoNode* muzzles=Key(obj,L"mpc_FireBeamSetting");
    if(p.hatchBones[k]==-2) {
        wchar_t bone[32]=L"";
        const SgoNode* e=Child(muzzles,k);
        if(!Text(Child(e,0),bone,32) || !Vec(Child(e,1),p.beamOffsets[k],3)) { p.hatchBones[k]=-1; Fail("a cannon's mpc_FireBeamSetting entry is not [bone, offset]"); return; }
        p.hatchBones[k]=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,bone);
        if(p.hatchBones[k]<0)Log("EDF41MOTHER cannon %p has no bone %ls for beam muzzle %d",obj,bone,k);
    }
    float b[16];
    if(!BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.hatchBones[k],b))return;
    float from[3],way[3],dir[3],m[16];
    Point(b,p.beamOffsets[k],from);
    for(int i=0;i<3;++i)way[i]=aim[i]-from[i];
    Cone(way,std::fmax(0.005f,0.2f-0.016f*s),p.rng,dir);
    AimMatrix(from,dir,m);
    wchar_t factory[64];
    FactoryName(obj,L"mpc_BeamBulletFactory",factory,64,L"BeamBullet01");
    Bullet r;
    r.factory=factory;
    r.speed=std::fmin(20.0f,5.0f+0.1f*s); r.life=180; r.damage=0.5f*s; r.size=std::fmax(2.0f,4.0f-0.2f*s);
    const float c=std::fmin(1.0f,1.0f+0.1f*s);
    const float color[4]={0.1f*c,1.2f*c,0.1f*c,1.0f};
    std::memcpy(r.color,color,16);
    r.custom=Custom(Key(obj,L"mpc_BeamBulletCustomParam"));
    r.hitSeTable=p.hull; r.hitSe=L"母船UFO中型砲台ビーム着弾";   // 4.1 beam P+0xB0
    if(Fire(obj,m,r))++shotCount[kCannon];
}

void PanelShot(void* obj,Part& p,float s) noexcept {
    float aim[3],from[3],way[3],dir[3],m[16];
    if(!AimAt(obj,aim))return;
    PartPos(obj,from);
    for(int i=0;i<3;++i)way[i]=aim[i]-from[i];
    wchar_t factory[64];
    Bullet r;
    if(p.type==0) {
        FactoryName(obj,L"mp_BulletFactory",factory,64,L"LaserBullet02");
        Cone(way,std::fmax(0.005f,0.1f-0.008f*s),p.rng,dir);
        r.speed=20.0f; r.life=60; r.damage=2.0f*s; r.size=std::fmax(0.5f,2.0f-0.125f*s);
        const float color[4]={2.0f,0.1f,0.2f,1.0f};
        std::memcpy(r.color,color,16);
    } else {
        FactoryName(obj,L"mp_BulletFactory",factory,64,L"PlasmaBullet01");
        Cone(way,0.01f,p.rng,dir);
        r.speed=std::fmin(2.0f,0.5f+0.075f*(s-0.1f)); r.life=1200; r.damage=25.0f*s;
        r.explosion=std::fmin(15.0f,10.0f+0.05f*(s-10.0f)); r.size=5.0f;
        const float color[4]={3.0f,1.0f,20.0f,1.0f};
        std::memcpy(r.color,color,16);
        r.hitSeTable=p.hull; r.hitSe=L"母船UFOパネルキャノン命中";   // 4.1 +0xC30, the plasma only
    }
    r.factory=factory;
    r.custom=Custom(Key(obj,L"mp_BulletCustomParameter"));
    AimMatrix(from,dir,m);
    if(Fire(obj,m,r))++shotCount[kPanel];
}

// A point near the part (4.1 picks one inside its physics shell: 0x337A60).
void Near(void* obj,Part& p,float radius,float* out) noexcept {
    PartPos(obj,out);
    for(int k=0;k<3;++k)out[k]+=(Random01(p.rng)*2.0f-1.0f)*radius;
}
void RandomDown(Part& p,float maxAngle,float* out) noexcept {
    const float down[3]={0.0f,-1.0f,0.0f};
    Cone(down,maxAngle,p.rng,out);
}

// ---- the state machines (4.1, one to one) ---------------------------------------------------------------------------
void Switch(Part& p,int state) noexcept {
    StopAnimationSounds(p,16); // interrupted clips cannot leave their MAB channels behind
    p.state=state;
    p.frames=0;
}
enum { kBigClosed=1,kBigOpen,kBigStandby,kBigCharge,kBigFire,kBigDestroyed,kBigClose,kBigFall };
enum { kCanDormant=1,kCanAppear,kCanStandby,kCanWait,kCanFire,kCanDestroyed,kCanFall };
enum { kPanDormant=1,kPanStart,kPanLaser,kPanPlasma,kPanDestroyed };
enum { kHatClosed=1,kHatOpening,kHatOpen,kHatClosing };

void SeedControlSkill301(Part& p) noexcept {
    if(p.manualLease && p.manualSeedSet){p.rng=p.manualSeed;p.manualSeedSet=false;}
}
void BigCannonTick(void* obj,Part& p) noexcept {
    const int f=p.frames;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kBigClosed:
            if(p.enabled) { Switch(p,kBigOpen); PlayState(obj,L"open"); }
            break;
        case kBigOpen:
            if(f>1 && AnimDone(obj)) {
                Switch(p,kBigStandby);
                GlowTo(p.glow,1.0f,1.0f,1.0f,1.0f);   // 4.1 0x256A90 exit
                SetGlow(p.glow,10.0f,10.0f,10.0f,1.0f);
            }
            break;
        case kBigStandby: {
            const bool trigger=p.boost;
            p.boost=false;   // 4.1 clears the one-shot trigger every frame of this state
            if(!p.enabled) { Switch(p,kBigClose); PlayState(obj,L"close"); }
            else if((p.attack || trigger) && !p.statik){SeedControlSkill301(p);Switch(p,kBigCharge);}
            break;
        }
        case kBigCharge:
            AtLeast(p.glow,(std::max)(0.0f,static_cast<float>(f-720)/480.0f)*10.0f+2.0f);   // 4.1 0x256CE0: lights up for the last 8 s
            if(f==720)PlaySe(p.hull,L"母船UFO砲台チャージ",pos,&p.chargeLoop);
            if(p.chargeLoop.instance)MoveSound(p.chargeLoop,pos);
            if(f==1045)PlaySe(p.hull,L"母船UFO砲台チャージ完了",pos);
            if(f==1180)StopSound(p.chargeLoop);
            if(!p.enabled) { StopSound(p.chargeLoop); Switch(p,kBigClose); PlayState(obj,L"close"); }
            else if(f>=1200) { StopSound(p.chargeLoop); Switch(p,kBigFire); p.phi=Random01(p.rng)-0.5f; PlaySe(p.hull,L"母船UFO砲台発射",pos); }
            break;
        case kBigFire:
            SetGlow(p.glow,50.0f,50.0f,100.0f,1.0f);   // 4.1 0x256F60, each frame
            if(f>=1)BigCannonShot(obj,p,f);
            if(f>60)Switch(p,kBigStandby);
            break;
        case kBigDestroyed:
            if(f<180 && f%8==0) { float at[3],dir[3]; Near(obj,p,30.0f,at); RandomDown(p,kPi,dir); Blazing(at,dir,0.25f); }
            if(f>300) { Switch(p,kBigFall); PlaySe(p.hull,L"母船UFO砲台墜落",pos); p.falling=true; }
            break;
        case kBigClose:
            if(f>1 && AnimDone(obj)) { Switch(p,kBigClosed); PlayState(obj,L"close_state"); }
            break;
        case kBigFall:
            if(f==360 || f==380 || f==400) {
                const float at[3]={pos[0],pos[1]-(f==360 ? 400.0f : f==380 ? 500.0f : 600.0f),pos[2]};
                float dir[3];
                RandomDown(p,0.7f*kPi,dir);
                BigExplosion(at,dir,3.0f,1.0f);
                if(f==360)PlaySe(p.hull,L"母船UFO砲台爆発",at);
            }
            if(f>420) { Remove(obj,p); }
            break;
        default: Switch(p,kBigClosed); break;
    }
}

void CannonTick(void* obj,Part& p,Hull& h,float s) noexcept {
    const int f=p.frames;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kCanDormant:
            if(p.enabled) { Switch(p,kCanAppear); PlayAppear(obj); }
            break;
        case kCanAppear:
            if(f>1 && AnimDone(obj)) {
                Switch(p,kCanStandby);
                GlowTo(p.glow,5.0f,5.0f,5.0f,1.0f);   // 4.1 0x259D20 exit
                AtLeast(p.glow,10.0f);
            }
            break;
        case kCanStandby:
            if(p.attack && !p.statik) { SeedControlSkill301(p);p.roundStarted=true; Switch(p,kCanWait); }
            break;
        case kCanWait:
            if((p.manualLease && !p.attack) || !NetDecides(obj))break;   // online: a guest copy charges when the host's does (edf41net)
            if(p.waitFrames<f) { Switch(p,kCanFire); p.waitFrames=1800+static_cast<int>(Random01(p.rng)*1800.0f); NetCannonFire(obj,p); }
            break;
        case kCanFire:
            AtLeast(p.glow,10.0f*static_cast<float>(f)/600.0f);   // 4.1 0x25A060: charge light
            if(f==450 && Throttle(h,3))PlaySe(p.hull,L"母船UFO中型砲台チャージ",pos);
            if(f>=600) {
                Switch(p,kCanStandby);
                CannonRocket(obj,p,s);
                if(Throttle(h,4))PlaySe(p.hull,L"母船UFO中型砲台発射",pos);
            }
            break;
        case kCanDestroyed:
            if(f<180 && f%8==0) { float at[3],dir[3]; Near(obj,p,20.0f,at); RandomDown(p,kPi,dir); Blazing(at,dir,0.25f); }
            if(f>300) { Switch(p,kCanFall); PlaySe(p.hull,L"母船UFO中型砲台墜落",pos); p.falling=true; }
            break;
        case kCanFall:
            if(f>=288 && (f-288)%4==0 && (f-288)/4<8) {
                const int k=(f-288)/4;
                float b[16];
                if(p.cannonBone==-2)p.cannonBone=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,L"cannon");
                if(BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.cannonBone,b)) {
                    const float off[3]={0.0f,-(100.0f+30.0f*static_cast<float>(k)),0.0f};
                    float at[3],dir[3];
                    Point(b,off,at);
                    RandomDown(p,0.7f*kPi,dir);
                    Explosion(at,dir,10.0f);
                    if(k%2==0)PlaySe(p.hull,L"母船UFO中型砲台爆発",at);
                }
            }
            if(f>320) { Remove(obj,p); }
            break;
        default: Switch(p,kCanDormant); break;
    }
    // The beam machine: only while the cannon lives.
    if(!p.dead) {
        const bool beam=p.manualLease ? p.manualBeam : p.attack;
        if(p.beamState==0) {
            if(beam && !p.statik && p.roundStarted) { SeedControlSkill301(p);p.beamState=1; p.beamFrames=0; }
        } else {
            if(!beam || p.statik) { p.beamState=0; p.roundStarted=false; }
            else {
                if(p.beamFrames%4==0) {
                    if(Throttle(h,5))PlaySe(p.hull,L"母船UFO中型砲台ビーム",pos);
                    CannonBeam(obj,p,s);
                }
                ++p.beamFrames;
            }
        }
    }
}

void PanelTick(void* obj,Part& p,Hull& h,float s) noexcept {
    const int f=p.frames;
    const float t=static_cast<float>(f)/60.0f;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kPanDormant:
            if(p.enabled) { Switch(p,kPanStart); SetGlow(p.glow,10.0f,10.0f,10.0f,10.0f); }   // 4.1 0x25D400 enter
            break;
        case kPanStart:
            if(t>2.0f && !p.firstStart) {
                p.firstStart=true;
                float hp[3];
                PartPos(p.hull,hp);
                if(Throttle(h,0))PlaySe(p.hull,L"母船UFOパネル起動",hp);
            }
            if(t>2.0f)p.started=true;
            if(t>6.0f && p.attack && !p.statik){SeedControlSkill301(p);Switch(p,p.type==0 ? kPanLaser : kPanPlasma);}
            break;
        case kPanLaser:
        case kPanPlasma: {
            if(!p.attack || p.statik) { Switch(p,kPanStart); break; }
            const int every=p.state==kPanLaser ? (std::max)(30,static_cast<int>(120.0f-5.0f*s)) : 300;
            if(f%every==0) {
                if(Throttle(h,p.state==kPanLaser ? 1 : 2))PlaySe(p.hull,p.state==kPanLaser ? L"母船UFOパネルレーザー" : L"母船UFOパネルキャノン",pos);
                SeedControlSkill301(p);PanelShot(obj,p,s);
                if(p.manualLease && p.panelOnce) {p.attack=false;p.panelOnce=false;}
            }
            break;
        }
        case kPanDestroyed:
            if(t<2.0f && p.deadFrames%8==0) { float at[3],dir[3]; Near(obj,p,8.0f,at); RandomDown(p,kPi,dir); FireBurst(at,dir,5.0f); }
            ++p.deadFrames;
            if(t>p.explodeDelay) {
                float dir[3];
                RandomDown(p,kPi,dir);
                Explosion(pos,dir,10.0f);
                PlaySe(p.hull,L"母船UFOパネル爆発",pos);
                Remove(obj,p);
            }
            break;
        default: Switch(p,kPanDormant); break;
    }
}

bool OriginalSpawnTeam301(const Part& part,int* team) noexcept {
    if(!team)return false;
    const auto weak=refs.find(part.hull);if(weak==refs.end() || !WeakAlive(weak->second))return false;
    const ObjRef hull{part.hull,weak->second.control};
    if(controlled_pvp::HasTemporaryTeam(hull)) {
        int baseline=0,last=0;
        if(!controlled_pvp::ReadBaselineForMember(hull,&baseline,&last))return false;
        *team=baseline==-1 || baseline==3 ? 1 : baseline;
    } else *team=PartTeam(part.hull); // a stable mode-off lease must honour a task's current team, before its next generic Frame
    return *team>=0 && *team<crew::team_native::kTeamCapacity;
}

// ---- the hatch: 4.1's ObjectGenerator (0x330E70 / 0x331270 / 0x3313C0) ------------------------------------------------
void SpawnOne(void* obj,Part& p) noexcept {
    const ObjRef source=ObjRef::Of(obj);int originalTeam=0;
    if(!OriginalSpawnTeam301(p,&originalTeam) || !p.spawnWave.Prepare(source,p.current.path,static_cast<unsigned>((std::max)(0,p.remaining)))) {
        Log("EDF41MOTHER hatch #%d spawn refused: no complete controlled native provider/budget for %ls",p.index,p.current.path);return;
    }
    if(!p.spawnTried) {
        p.spawnTried=true;
        if(!FindLocator(static_cast<char*>(obj)+kEaoModel,L"発生位置",&p.spawnAt))
            Log("EDF41MOTHER hatch #%d has no locator 発生位置: enemies come out at its origin",p.index);
    }
    float at[16],m[16];
    if(!LocatorMatrix(p.spawnAt,at))Matrix(obj,at);
    RotY(2.0f*kPi*Random01(p.rng),m);
    m[12]=at[12]; m[13]=at[13]; m[14]=at[14];
    void* enemy=CreateFromPath(m,p.current.path);
    ++p.made;
    if(!enemy) {
        if(mother_control::OwnedSpawner(source))p.spawnWave.fault=true;
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8)Log("EDF41MOTHER hatch #%d: the game made no %ls",p.index,p.current.path);
        return;
    }
    // What EDF6's own generator does to what it makes (0x6E335F..0x6E3381): level, team (registered), activate.
    const float level=p.current.level*F(p.hull,kRawLevelAt);
    SetLevel(enemy,level);
    SetTeam(enemy,originalTeam,true);
    if(!p.spawnWave.Adopt(source,ObjRef::Of(enemy))) {
        Log("EDF41MOTHER hatch #%d spawn refused after native construction: no verified ownership for %ls",p.index,p.current.path);
        DeleteObject(enemy);return; // this newly constructed object was never activated or published as a native spawn
    }
    Activate(enemy);
    NetSpawn(p,enemy);   // online: the game's child identity, bound to the same enemy on every machine (edf41net)
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12)Log("EDF41MOTHER hatch #%d: enemy #%d %ls at %.0f %.0f %.0f, raw level %.2f, team %d",p.index,p.made,p.current.path,
                                m[12],m[13],m[14],level,I(enemy,kTeamAt));
}

void HatchTick(void* obj,Part& p) noexcept {
    float pos[3];
    PartPos(obj,pos);
    if(p.current.active && (!p.manualLease || p.controlledWave)) {   // the real generator, each frame
        if(p.delayLeft>0)--p.delayLeft;
        else if(p.intervalLeft>0)--p.intervalLeft;
        else if(p.permit) {
            SpawnOne(obj,p);
            p.intervalLeft=p.current.interval;
            if(--p.remaining<=0) {
                p.waveDone=true;
                p.permit=false;
                p.current.active=false;
                p.controlledWave=false;p.spawnWave.Finish();
                if(p.permanent.active && !p.manualLease) { p.current=p.permanent; p.remaining=p.current.count; p.delayLeft=p.current.delay; p.intervalLeft=0; }
            }
        }
    }
    switch(p.state) {
        case kHatClosed:
            if(p.current.active && (!p.manualLease || p.controlledWave) && static_cast<float>(p.delayLeft)<=p.openFrames) {
                if(!p.spawnWave.Prepare(ObjRef::Of(obj),p.current.path,static_cast<unsigned>((std::max)(0,p.remaining)))) {
                    Log("EDF41MOTHER hatch #%d open refused: unsupported controlled spawn %ls",p.index,p.current.path);break;
                }
                Switch(p,kHatOpening);
                PlayState(obj,L"open");
                PlaySe(p.hull,L"母船UFO発進口開く",pos);
            }
            break;
        case kHatOpening: {
            float len=0.0f;
            if(p.frames>1 && AnimDone(obj,&len)) {
                if(len>1.0f)p.openFrames=len;
                Switch(p,kHatOpen);
                p.permit=true;
                p.closeWait=240;
                p.waveDone=false;
                PlaySe(p.hull,L"母船UFO発進口開く完了",pos);
            }
            break;
        }
        case kHatOpen:
            if(p.waveDone && --p.closeWait<=0) {
                Switch(p,kHatClosing);
                p.waveDone=false;
                PlayState(obj,L"close");
                PlaySe(p.hull,L"母船UFO発進口閉じる",pos);
            }
            break;
        case kHatClosing:
            if(p.frames>1 && AnimDone(obj)) { Switch(p,kHatClosed); PlaySe(p.hull,L"母船UFO発進口閉じる完了",pos); }
            break;
        default: Switch(p,kHatClosed); break;
    }
}

// The 0x20100 block (EDF6 = 4.1's offsets + 8, SPEC2 5.2): +0x08 kind (2100: 0, standing; 2101: 1, once), +0x0C the delay
// between waves, +0x10 count, +0x14 interval (frames), +0x18 level, +0x20 std::wstring the SGO, +0x48 the hatch.
void AssignWave(Hull& h,int index,const Wave& w) noexcept;
bool QueueGenerator(void* hullObj,const char* d) noexcept {
    if(!Rd(d,0x50,"generator message block"))return false;
    std::int32_t kind,delay,count,frames,index;
    float level;
    std::memcpy(&kind,d+8,4); std::memcpy(&delay,d+0xC,4); std::memcpy(&count,d+0x10,4); std::memcpy(&frames,d+0x14,4);
    std::memcpy(&level,d+0x18,4); std::memcpy(&index,d+0x48,4);
    std::uint64_t size=0,capacity=0;
    std::memcpy(&size,d+0x30,8); std::memcpy(&capacity,d+0x38,8);
    const wchar_t* text=capacity<8 ? reinterpret_cast<const wchar_t*>(d+0x20) : *reinterpret_cast<const wchar_t* const*>(d+0x20);
    if(size==0 || size>=260 || !Rd(text,(size+1)*sizeof(wchar_t),"generator enemy path")) { Fail("a generator message without a readable enemy path"); return false; }
    if(count<=0 || count>4096 || index<0 || index>=64) { Fail("a generator message with a count or hatch index out of range"); return false; }
    Hull* h=HullOf(hullObj);
    if(!h)return false;
    Wave w;
    std::wmemcpy(w.path,text,static_cast<std::size_t>(size)); w.path[size]=L'\0';
    w.count=count; w.interval=(std::max)(1,frames); w.delay=(std::max)(0,delay); w.level=level>0.0f && std::isfinite(level) ? level : 1.0f;
    w.active=true; w.kind=kind;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<24)Log("EDF41MOTHER generator on hatch %d: %d x %ls every %d frames, kind %d, wave delay %d, level %.2f%s",index,count,w.path,
                                w.interval,kind,delay,level,h->built ? "" : " (before the parts are made: kept until they are)");
    if(!h->built) { h->pending.emplace_back(index,w); return true; }   // the script asks on the creation frame, before the parts
    AssignWave(*h,index,w);
    return true;
}
void AssignWave(Hull& h,int index,const Wave& w) noexcept {
    Part* hatch=nullptr;
    for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==kHatch && p->index==index) { hatch=p; break; } }
    if(!hatch) { Log("EDF41MOTHER generator for hatch %d: the hull has no such hatch, dropped",index); return; }
    hatch->configured=w;hatch->configuredOnce=true;
    const int kind=w.kind;
    if(kind==0)hatch->permanent=w;
    if(kind!=0 || !hatch->current.active) {
        hatch->spawnWave.Finish();
        hatch->current=w;
        hatch->remaining=w.count;
        hatch->delayLeft=kind==0 ? w.delay : 0;
        hatch->intervalLeft=0;
    }
}

// ---- ObjectAction (4.1 0x250870, 18 entries) and GetPartsObject ------------------------------------------------------
const char* const kActionNames[18]={"glow off, spin stops at once","glow white, spin stops","glow slow red, spin 0.0003",
    "BigCannon enable","BigCannon disable","BigCannon attack on","BigCannon attack off","BigCannon fire once",
    "Cannon enable","Cannon disable","Cannon attack on","Cannon attack off","Cannon boost",
    "Panel enable","Panel disable","Panel attack on","Panel attack off","Panel boost"};
void ApplyAction(void* hullObj,Hull& h,int action) noexcept {
    if(action==0) { h.yawTarget=0.0f; h.yawRate=0.0f; }
    else if(action==1)h.yawTarget=0.0f;
    else if(action==2)h.yawTarget=0.0003f;
    int touched=0;
    // 4.1 0x251BC0: the group's flag is set only while the HULL lives (not the part).
    if(action>=3 && action<=17 && HullAlive(hullObj)) {
        const int group=action<8 ? kBigCannon : action<13 ? kCannon : kPanel;
        const int what=(action-3)%5;
        for(void* o:h.parts) {
            Part* p=PartOf(o);
            if(!p || p->group!=group)continue;
            ++touched;
            if(what==0)p->enabled=true; else if(what==1)p->enabled=false; else if(what==2)p->attack=true; else if(what==3)p->attack=false; else p->boost=true;
        }
    }
    Log("EDF41MOTHER ObjectAction %d (%s) on %p: %d parts",action,action>=0 && action<18 ? kActionNames[action] : "unknown to 4.1",hullObj,touched);
}

bool GetParts(Hull& h,char* data) noexcept {
    if(!Rd(data,0x18,"GetPartsObject block"))return false;
    std::int32_t group,index;
    std::memcpy(&group,data,4);
    std::memcpy(&index,data+4,4);
    void* found=nullptr;
    for(void* o:h.parts) {
        Part* p=PartOf(o);
        if(!p || p->gone || !Live(o))continue;
        if(group==0 && p->group==kBigCannon) { found=o; break; }
        if(group==1 && p->group==kCannon && p->index==index) { found=o; break; }
    }
    Log("EDF41MOTHER GetPartsObject group %d index %d -> %p",group,index,found);
    if(found)reinterpret_cast<void*(__fastcall*)(void*,const void*)>(image+kWeakAssign)(data+8,static_cast<char*>(found)+kWeakSelfAt);
    return true;
}

// ---- making the parts -----------------------------------------------------------------------------------------------
bool MakePart(void* hullObj,Hull& h,const SgoNode* entry,int group,int index,int ring) noexcept {
    wchar_t path[260];
    float euler[3],position[3];
    if(!Text(Child(entry,0),path,260) || !Vec(Child(entry,1),euler,3) || !Vec(Child(entry,2),position,3)) {
        Fail("a part entry of the hull's SGO is not [path, euler, position]");
        return false;
    }
    Part rec;
    rec.hull=hullObj; rec.group=group; rec.index=index; rec.ring=ring;
    rec.statik=group==kPanel && ring>=2;   // 4.1 0x24FFF0: the last two panel rings are hung with r9b = 0 (+0x904)
    EulerMatrix(euler,position,rec.local);
    rec.rng=(static_cast<std::uint64_t>(std::fabs(position[0]*7.0f+position[1]*13.0f+position[2]*31.0f)*1000.0f)^0xDEADBEEFBEEFDEADull)+static_cast<std::uint64_t>(index);
    float world[16],hullM[16];
    Matrix(hullObj,hullM);
    Multiply(rec.local,hullM,world);
    void* part=CreateFromPath(world,path);
    if(!part) { Log("EDF41MOTHER part %s #%d (%ls): the game made nothing",kGroupNames[group],index,path); return false; }
    rec.state=group==kBigCannon ? kBigClosed : group==kCannon ? kCanDormant : group==kPanel ? kPanDormant : kHatClosed;
    rec.team=I(part,kTeamAt);   // the cache starts from the real new owner; first Masking still performs the original native SetTeam
    DropPart(part);    // a record left at this address by an object freed without its destructor
    parts[part]=rec;
    Track(part);
    h.parts.push_back(part);
    NetPart(hullObj,part,group,h.parts.size()-1);   // online: the game's child identity (edf41net; given once the hull has its own)
    return true;
}
int MakeGroup(void* hullObj,Hull& h,const wchar_t* key,int group,bool nested) noexcept {
    const SgoNode* node=Key(hullObj,key);
    if(Count(node)<0) { Log("EDF41MOTHER hull SGO has no array %ls: no %s",key,kGroupNames[group]); return 0; }
    int made=0,index=0;
    for(int i=0;i<node->a;++i) {
        const SgoNode* c=Child(node,i);
        if(!c)break;
        if(!nested) { made+=MakePart(hullObj,h,c,group,index++,0) ? 1 : 0; continue; }
        for(int j=0;j<Count(c);++j)made+=MakePart(hullObj,h,Child(c,j),group,index++,i) ? 1 : 0;
    }
    return made;
}
void SpawnParts(void* hullObj,Hull& h) noexcept {
    if(h.built)return;
    h.built=true;
    const int mask=Cfg().edf41MotherParts;
    const ULONGLONG t0=GetTickCount64();
    int n[5]={};
    if(mask&1)n[kHatch]=MakeGroup(hullObj,h,L"mp_HatchLow",kHatch,false);
    if(mask&2)n[kCover]=MakeGroup(hullObj,h,L"mp_HatchMiddle",kCover,false);
    if(mask&4)n[kCannon]=MakeGroup(hullObj,h,L"mp_Cannon",kCannon,false);
    if(mask&8)n[kBigCannon]=MakeGroup(hullObj,h,L"mp_BigCannon",kBigCannon,false);
    if(mask&16)n[kPanel]=MakeGroup(hullObj,h,L"mp_Panel",kPanel,true);
    Log("EDF41MOTHER parts of %p made in %llu ms (ini Edf41MotherParts=%d): %d hatches, %d covers, %d cannons, %d main cannon, %d panels",
        hullObj,static_cast<unsigned long long>(GetTickCount64()-t0),mask,n[0],n[1],n[2],n[3],n[4]);
}

// ---- the part's frame -----------------------------------------------------------------------------------------------
// Where it hangs (4.1 0x25E840): world = L2 x A x L1 x hull; a falling part leaves the hull with the step it last moved by and
// drops 0.002 per frame squared, its orientation frozen.
void Place(void* obj,Part& p) noexcept {
    float w[16];
    if(p.falling) {
        Matrix(obj,w);
        p.fallVel[1]-=0.002f;
        for(int k=0;k<3;++k)w[12+k]+=p.fallVel[k];
    } else {
        float l1[16],l2[16],hullM[16],a[16];
        RotY(p.group==kPanel ? p.spin : 0.0f,l1);
        if(p.group==kBigCannon)RotY(p.spin,l2);
        else Translate(0.0f,0.0f,p.group==kPanel ? p.push : 0.0f,l2);
        Matrix(p.hull,hullM);
        Multiply(l2,p.local,a);
        Multiply(a,l1,a);
        Multiply(a,hullM,w);
        if(p.haveLast)for(int k=0;k<3;++k)p.fallVel[k]=w[12+k]-p.lastPos[k];
        for(int k=0;k<3;++k)p.lastPos[k]=w[12+k];
        p.haveLast=true;
    }
    std::memcpy(static_cast<char*>(obj)+kMatrixAt,w,sizeof(w));
}

// 4.1 0x250520 each frame: the part's team is the hull's (neutral -> 1); a part not enabled (or the neutral ship's main cannon)
// takes no damage and is not registered (not a target, not counted, not lockable); the static panels are never registered.
void Masking(void* obj,Part& p) noexcept {
    const std::int32_t hullTeam=I(p.hull,kTeamAt);
    const bool immune=!p.enabled || (p.group==kBigCannon && hullTeam==3) || p.dead;
    SetFlag(obj,kNoHpLoss,immune);
    const bool reg=!immune && !p.statik;
    std::int32_t team=PartTeam(p.hull);
    // A disabled hull keeps native team -1; its original -1->1 mapping must not overwrite an authenticated PVP part lease.
    int applied=0;if(controlled_pvp::ReadAppliedTeam(ObjRef::Of(obj),&applied))team=applied;
    const std::int32_t now=I(obj,kTeamAt);
    const bool registeredNow=(static_cast<std::uint32_t>(I(obj,kFlags380))&0x40u)!=0;
    if(!p.teamInitialized || reg!=p.registered || team!=p.team || now!=team || registeredNow!=reg) {
        if(p.team==team && now!=team) {   // set before, changed since by someone else: say so once
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<4)Log("EDF41MOTHER part %p: its team was %d, set back to %d",obj,now,team);
        }
        SetTeam(obj,team,reg);
        p.registered=reg;p.teamInitialized=true;
        p.team=team;
    }
    SetFlag(obj,kNotTarget|kNotCounted,!reg);
}

// ---- lock points (H): an object's lock points are the shared_ptrs in its list +0x360 (data +0x368, count +0x378); a point has its
// position at +0x10, +0x29 the owner's registration copied each frame (0x6C7700) and +0x2A, without which the lock search skips
// it (0x22DF43). UfoCarrier508's come from its MAB's lock-target list (list 1; 0x6C74E0 reads [model+0x280] list 1, 0x6C7700 places
// them on their bones): the hull has exactly 4.1's one, ターゲット on `weak` (the two locators mother301_fix adds for the
// carrier's constructor are list 0, locators, never lock points). EventAnimationObject makes none, so the parts get 4.1's from
// their own MAB's list 1 by hand: the main cannon's on `waist` at (0, -343.15, 0), a cannon's on `cannon` at (0, -291.25, 0), a
// panel's at its origin (the hatches and covers are never enabled in 4.1: never lockable, none made).
void LockReport(void* obj,const char* what) noexcept {
    const char* o=static_cast<const char*>(obj);
    if(!Readable(o+0x360,0x20))return;
    const char* data=*reinterpret_cast<char* const*>(o+0x368);
    const std::size_t n=*reinterpret_cast<const std::size_t*>(o+0x378);
    static std::atomic<int> said{0};
    if(said.fetch_add(1)>=24)return;
    Log("EDF41MOTHER %s %p lock points: %zu (team %d, registered %d)",what,obj,n,I(obj,kTeamAt),(I(obj,kFlags380)&0x40)!=0 ? 1 : 0);
    for(std::size_t i=0;i<n && i<8 && data && Readable(data+i*16,16);++i) {
        const char* lp=*reinterpret_cast<char* const*>(data+i*16);
        if(!lp || !Readable(lp,0x30))continue;
        Log("EDF41MOTHER   #%zu at %.0f %.0f %.0f, +0x29 %d, +0x2A %d",i,F(lp,0x10),F(lp,0x14),F(lp,0x18),lp[0x29],lp[0x2A]);
    }
}
// The hull's own lock point(s) (the MAB's ターゲット on `weak`; the carrier made it): lockable only while a shot at the weak point
// would be taken, i.e. the hull lives and the main cannon is gone (HullAccept refuses everything before) and a lock-on weapon
// would otherwise lock what it can only bounce off.
void HullLock(void* self,bool lockable) noexcept {
    const char* o=static_cast<const char*>(self);
    if(!Readable(o+0x360,0x20))return;
    const char* data=*reinterpret_cast<char* const*>(o+0x368);
    const std::size_t n=*reinterpret_cast<const std::size_t*>(o+0x378);
    for(std::size_t i=0;i<n && i<8 && data && Readable(data+i*16,16);++i) {
        char* lp=*reinterpret_cast<char* const*>(data+i*16);
        if(lp && Readable(lp,0x30))lp[0x2A]=lockable ? 1 : 0;
    }
}
void PartLock(void* obj,Part& p,bool lockable) noexcept {
    if(p.group==kHatch || p.group==kCover || (Cfg().edf41MotherForce&256))return;   // bit 256: bisecting, no part lock points
    if(!p.lock) {
        if(p.lockBone==-3)return;
        p.lock=AddLockPoint(obj);
        if(!p.lock) { p.lockBone=-3; Fail("a part's lock point could not be made"); return; }
    }
    float at[3];
    if(p.group==kPanel)PartPos(obj,at);
    else {
        void* skel=static_cast<char*>(obj)+kEaoSkeleton;
        if(p.lockBone==-2)p.lockBone=BoneIndex(skel,p.group==kBigCannon ? L"waist" : L"cannon");
        float b[16];
        if(!BoneMatrix(skel,p.lockBone,b))Matrix(obj,b);
        const float local[3]={0.0f,p.group==kBigCannon ? -343.15f : -291.25f,0.0f};
        Point(b,local,at);
    }
    SetLockPoint(p.lock,at);
    char* lp=static_cast<char*>(p.lock);
    if(Readable(lp+0x28,4)) {
        lp[0x29]=(I(obj,kFlags380)&0x40)!=0 ? 1 : 0;
        lp[0x2A]=lockable ? 1 : 0;
    }
}

// A part's hit bodies onto the layer the area damage's query meets (edf41layer::HitLayer), once, on its first frame.
void PartHitLayer(void* obj,Part& p) noexcept {
    if(!(p.group==kCannon || p.group==kBigCannon || p.group==kPanel) || !LayerPrims())return;
    void* ragdoll=static_cast<char*>(obj)+kEaoRagdoll;
    std::size_t n=0;
    const int part=RagdollLayer(ragdoll,&n),hull=RagdollLayer(static_cast<char*>(p.hull)+kCarrierRagdoll);
    const int query=AreaQueryLayer();
    const std::uint32_t* table=GroupFilterTable();
    const int to=edf41layer::HitLayer(part,hull,query,table);
    if(to!=part && to>=0)reinterpret_cast<void(__fastcall*)(void*,int)>(image+kRagdollSetLayer)(ragdoll,to);
    static std::atomic<int> said[5]{};
    if(said[p.group].fetch_add(1)<1)
        Log("EDF41MOTHER %s %p hit layer: %zu bodies on layer 0x%X, the hull's 0x%X, the area query's 0x%X (met: part %d, hull %d%s) -> 0x%X (now 0x%X)",
            kGroupNames[p.group],obj,n,part,hull,query,table ? (edf41layer::Sees(table,query,part) ? 1 : 0) : -1,
            table ? (edf41layer::Sees(table,query,hull) ? 1 : 0) : -1,table ? "" : ", no table",to,RagdollLayer(ragdoll));
}
void PartFrame(void* obj,Part& p,Hull& h) noexcept {
    if(!p.sized) {
        p.sized=true;
        SetLevel(obj,F(p.hull,kRawLevelAt));
        SizeDurability(obj);
        PartHitLayer(obj,p);   // rockets and missiles reach it (EventAnimationObject's layer 0x11 is not met by the area damage)
        if(p.group==kPanel) {
            std::int32_t t=0;
            p.type=Int(Key(obj,L"mp_type"),&t) ? t : 0;
            p.explodeDelay=3.0f+Random01(p.rng)*4.0f;
        }
    }
    if(p.group==kCannon && edf41pose::HoldNow(edf41pose::Group::kCannon,p.restHeld,p.state==kCanDormant)) {
        p.restHeld=true;   // folded as 4.1's dormant cannon (no clip), not EventAnimationObject's default (= appear) unfolding it
        HoldRest(obj,edf41pose::Group::kCannon,kEaoCas,kEaoMainPlayer,"cannon");
    }
    Masking(obj,p);
    {   // 4.1 0x250520: lockable = enabled, alive, not a static ring, not the neutral ship's main cannon, and the hull not neutral
        const std::int32_t t=I(p.hull,kTeamAt);
        PartLock(obj,p,p.registered && t!=-1 && t!=3);
        if(p.frames==600 && (p.group==kBigCannon || (p.group==kCannon && p.index==0) || (p.group==kPanel && p.index==0)))
            LockReport(obj,kGroupNames[p.group]);
    }
    BindGlow(p.glow,obj,kEaoModel,kGroupNames[p.group]);
    // 4.1 0x25E840 +0x905: the hull's first frame at durability <= 0: every part flashes white and goes dark
    if(!p.hullDeathSeen && !HullAlive(p.hull)) { p.hullDeathSeen=true; SetGlow(p.glow,100.0f,100.0f,100.0f,1.0f); GlowTo(p.glow,0.0f,0.0f,0.0f,1.0f); p.glow.rate=0.02f; }
    const float s=LevelS(obj);
    const bool died=!p.dead && (F(obj,kDurabilityAt)<=0.0f || static_cast<const char*>(obj)[kDestroyedAt]);
    if(died && p.group!=kHatch && p.group!=kCover) {
        p.dead=true;
        p.attack=false;
        float pos[3];
        PartPos(obj,pos);
        StopSound(p.chargeLoop);
        Flash(p.glow,0.1f);   // 4.1 0x2574F0 / 0x25A2D0 / 0x25D740: white, then dark at 0.1
        if(p.group==kBigCannon) { Switch(p,kBigDestroyed); PlaySe(p.hull,L"母船UFO砲台破壊",pos); }
        else if(p.group==kCannon) { Switch(p,kCanDestroyed); PlaySe(p.hull,L"母船UFO中型砲台破壊",pos); }
        else if(p.group==kPanel) {
            Switch(p,kPanDestroyed);
            PlaySe(p.hull,L"母船UFOパネル破壊",pos);
            float dir[3];
            RandomDown(p,kPi,dir);
            Explosion(pos,dir,5.0f);
            p.falling=true;
        }
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<40 || p.group!=kPanel)
            Log("EDF41MOTHER part %s #%d %p destroyed (durability %.0f / %.0f, items dropped %d, destroyed byte %d)",kGroupNames[p.group],p.index,obj,
                F(obj,kDurabilityAt),F(obj,kMaxDurabilityAt),static_cast<int>(static_cast<const char*>(obj)[0x590]),static_cast<int>(static_cast<const char*>(obj)[kDestroyedAt]));
    }
    if(p.group==kBigCannon)p.spin-=0.0003f;
    if(p.group==kPanel && p.started && HullAlive(p.hull) && !p.dead) {
        p.push+=(100.0f-p.push)*0.02f;
        p.ringFactor+=(1.0f-p.ringFactor)*0.001f;
    }
    if(p.group==kPanel && !p.dead)p.spin-=0.001f*p.ringFactor;
    if(p.damageCooldown>0)--p.damageCooldown;
    switch(p.group) {
        case kBigCannon: BigCannonTick(obj,p); break;
        case kCannon: CannonTick(obj,p,h,s); break;
        case kPanel: PanelTick(obj,p,h,s); break;
        case kHatch: HatchTick(obj,p); break;
        default: break;
    }
    TickGlow(p.glow);
    ++p.frames;
}

// BigCannon/Cannon's authored MAB sound tracks, sampled from the real CAS clock. In particular,
// appear stops at 440 / 189 clip units; close has two independent channels at different times.
// Class-code charge/shot sounds remain on their separate paths (their lengths do not define ownership).
void LoadAnimationSounds(void* obj,AnimationSounds& sounds) noexcept {
    if(sounds.loaded)return;
    sounds.loaded=true;
    const SgoNode* table=Key(obj,L"edf41_sound_events");
    const int count=Count(table);
    if(count<0 || count>128) { Fail("a cannon lacks its authored sound-event table"); return; }
    for(int i=0;i<count;++i) {
        const SgoNode* row=Child(table,i);
        wchar_t state[128];
        std::int32_t clip=0;
        if(!Text(Child(row,0),state,128) || !Int(Child(row,1),&clip)) { sounds.clips.clear(); return; }
        SoundClip entry;
        entry.state=StateIndex(obj,state);
        entry.clip=clip;
        const SgoNode* events=Child(row,2);
        const int keys=Count(events);
        if(keys<0 || keys>128) { sounds.clips.clear(); return; }
        for(int k=0;k<keys;++k) {
            const SgoNode* key=Child(events,k);
            SoundKey event;
            std::int32_t kind=0,channel=-1,trace=0;
            wchar_t name[128],node[128];
            if(!Float(Child(key,0),&event.time) || !Int(Child(key,1),&kind) || !Text(Child(key,2),name,128)
               || !Int(Child(key,3),&channel) || !Text(Child(key,4),node,128) || !Int(Child(key,5),&trace)) {
                sounds.clips.clear(); return;
            }
            event.kind=kind;
            event.channel=channel;
            event.name=name;
            event.node=node;
            event.trace=trace!=0;
            entry.events.push_back(std::move(event));
        }
        sounds.clips.push_back(std::move(entry));
    }
}
void SoundPoint(void* obj,int bone,float* out) noexcept {
    float matrix[16];
    if(bone>=0 && BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,bone,matrix))std::memcpy(out,matrix+12,12);
    else PartPos(obj,out);
}
void TickAnimationSounds(void* obj,Part& p) noexcept {
    const bool active=(p.group==kBigCannon && (p.state==kBigOpen || p.state==kBigClose))
        || (p.group==kCannon && p.state==kCanAppear);
    if(!active) { StopAnimationSounds(p,16); return; }
    AnimationSounds& sounds=p.animationSounds;
    LoadAnimationSounds(obj,sounds);
    int state=-1;
    float time=0.0f,length=0.0f;
    if(!ClipNow(obj,&state,&time,&length))return;
    const auto stop=[](BoneSound& sound,int fade) { StopSound(sound.handle,fade); };
    sounds.timeline.Step(state,time,sounds.clips,[&](const SoundKey& event) {
        if(event.kind==1) { sounds.channels.StopOne(event.channel,16,stop); return; }
        if(event.kind!=0 || event.name.empty())return;
        const int bone=event.node.empty() ? -1 : BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,event.node.c_str());
        float at[3];
        SoundPoint(obj,bone,at);
        sounds.channels.Start(event.channel,[&](BoneSound* sound) {
            if(sound) { sound->bone=bone; sound->trace=event.trace; }
            return PlaySe(p.hull,event.name.c_str(),at,sound ? &sound->handle : nullptr);
        },stop);
    },[&] { sounds.channels.StopAll(16,stop); });
    sounds.channels.MoveAll([&](BoneSound& sound) {
        if(!sound.trace)return;
        float at[3];
        SoundPoint(obj,sound.bone,at);
        MoveSound(sound.handle,at);
    });
}

// ---- development switches (ini Edf41MotherForce): what the autopilot cannot make happen (it does not shoot) --------------
// 1: after 5 s every group enabled with attack on (the main cannon's 20 s charge, then its sweep); 2: after 60 s the main
// cannon destroyed (its fall, then the weak point takes damage); 4: after 90 s the cannons destroyed; 8: after 90 s a quarter
// of the panels destroyed; 16: after 5 s every group enabled without attack (with Edf41DevShoot: the damage path to the hull's
// death without the player being killed first); 32: the hull's durability floor the scripts set held at 0.
void Force(void* hullObj,Hull& h) noexcept {
    const int force=Cfg().edf41MotherForce;
    if(!force)return;
    if((force&1) && h.ticks==300) { for(int a:{3,5,8,10,13,15})ApplyAction(hullObj,h,a); Log("EDF41MOTHER dev force: every group enabled, attack on"); }
    // 16: after 5 s every group enabled, attack left off (targets for Edf41DevShoot that do not kill the autopilot's player)
    if((force&16) && !(force&1) && h.ticks==300) { for(int a:{3,8,13})ApplyAction(hullObj,h,a); Log("EDF41MOTHER dev force: every group enabled, no attack"); }
    // 32: the scripts' durability floor (native 3020 writes +0x2F0, e.g. M130 100 %, M993 30 %) held at 0: the hull can be killed
    if((force&32) && F(hullObj,0x2F0)>0.0f) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8)Log("EDF41MOTHER dev force: the durability floor %.0f set to 0",F(hullObj,0x2F0));
        SetF(hullObj,0x2F0,0.0f);
    }
    const auto kill=[&](int group,int every) {
        int n=0,killed=0;
        for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==group && !p->dead && Live(o) && (n++%every)==0) { SetF(o,kDurabilityAt,0.0f); ++killed; } }
        Log("EDF41MOTHER dev force: %d %s destroyed",killed,kGroupNames[group]);
    };
    if((force&2) && h.ticks==3600)kill(kBigCannon,1);
    if((force&4) && h.ticks==5400)kill(kCannon,1);
    if((force&8) && h.ticks==5400)kill(kPanel,4);
}

// ---- the wrappers -------------------------------------------------------------------------------------------------
// Slot 1, the deleting destructors (UfoCarrier508 0x4F8A70, EventAnimationObject 0x5B64F0; rcx this, edx flags): the record goes
// while the object is still whole (the stock destructor runs after). A hull's parts outlive it by a frame: their Update finds the
// hull gone (Live) and deletes them. Called when the script deletes an object mid-mission, when a mission ends or restarts (the
// scene frees everything with the game whole) and, at the game's exit, possibly inside the application destructor: then
// ShutdownVents has stopped the loops and deleted the emitters, and StopSound is inert (edf41c::ShuttingDown).
std::uint64_t __fastcall HullDtor(void* self,unsigned flags) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Hull* h=HullOf(self);
        if(h) {
            Log("EDF41MOTHER hull %p gone: its %zu parts go with it",self,h->parts.size());
            // its parts let go of the address (a hull made there next must not take them): their next Update deletes them
            for(void* o:h->parts)
                if(Part* p=PartOf(o))p->hull=nullptr;
        }
        DropHull(self);
        Forget(self);
    }
    return reinterpret_cast<DtorFn>(hullClass.orig[kSlotDtor])(self,flags);
}

// ---- 4.1's movement (0x2512C0, H; 补完规格 6.1) -------------------------------------------------------------------------
// UfoCarrier508 moves itself (0x4F74D0): its AI slot 7 (0x4F6D70) aims a velocity of the route speed at the route point and
// turns toward it, then +0x5C0 eases to it, +0x90 += +0x5C0, and the rows are rebuilt each frame from the yaw +0x5F4 (target
// +0x604). 4.1's hull never turns toward its way and creeps: velocity v += (t - v) 0.001 with t = (route point - position)
// clipped to max(0, move speed x 0.5), v x 0.99 when there is no route point, and its yaw only by the spin
// (a10 += (a20 - a10) 0.01, a04 += a10). The route itself stays EDF6's: the explorer (+0x4A8) still gives the point (+0x4C0,
// valid while byte +0x4D0; 4.1 +0x270 / +0x280 / +0x290, the same layout one class up) and still counts a point reached when
// the hull comes within its radius (+0x4BC), so Factor_AiMoveEnd 9300 (+0x4A8 == 0) fires as before; SetAiMoveSpeed's value
// is +0x4B8 (4.1 +0x278). Ours is written over the carrier's before and after its update; its yaw fields are kept equal to ours.
constexpr std::size_t kExplorer=0x4A8,kRoutePoint=0x4C0,kRoutePointSet=0x4D0,kMoveSpeed=0x4B8,
    kCarrierVelocity=0x5C0,kCarrierYaw=0x5F4,kCarrierYawTarget=0x604;
// Development (Edf41MotherForce bit 64): the carrier's own movement kept, to compare a mission's timing with 4.1's.
bool CarrierMoves() noexcept { return (Cfg().edf41MotherForce&64)!=0; }
// When the explorer lets go of the route (9300's condition), said with the frame, for the timing comparisons.
void RouteLog(void* self,Hull& h) noexcept {
    const char* o=static_cast<const char*>(self);
    const bool has=Readable(o+kExplorer,8) && *reinterpret_cast<void* const*>(o+kExplorer);
    if(has && !h.routeSeen) { h.routeSeen=true; Log("EDF41MOTHER hull %p frame %u: a route is set (%s movement)",self,h.ticks,CarrierMoves() ? "the carrier's" : "4.1's"); }
    if(!has && h.routeSeen) {
        h.routeSeen=false;
        float pos[3];
        PartPos(self,pos);
        Log("EDF41MOTHER hull %p frame %u: route done (Factor_AiMoveEnd) at %.0f %.0f %.0f (%s movement)",self,h.ticks,pos[0],pos[1],pos[2],
            CarrierMoves() ? "the carrier's" : "4.1's");
    }
}
void HoldPose(void* self,Hull& h) noexcept {
    if(!h.moveInit || Cfg().edf41MotherPlace>0.0f || (CarrierMoves() && !h.control.Active() && h.death==kDeathNone))return;
    char* o=static_cast<char*>(self);
    std::memcpy(o+kPosAt,h.death!=kDeathNone ? h.deadPos : h.pos,12);
    const float zero[3]={0.0f,0.0f,0.0f};
    std::memcpy(o+kCarrierVelocity,zero,12);
    std::memcpy(o+kCarrierYaw,&h.yaw,4);
    std::memcpy(o+kCarrierYawTarget,&h.yaw,4);
}
void Spin(void* self,Hull& h) noexcept {
    char* o=static_cast<char*>(self);
    if(!h.moveInit) {
        h.moveInit=true;
        std::memcpy(h.pos,o+kPosAt,12);
        std::memcpy(h.point,o+kPosAt,12);
        std::memcpy(&h.yaw,o+kCarrierYaw,4);
        if(!std::isfinite(h.yaw))h.yaw=std::atan2(-F(self,kMatrixAt+8),F(self,kMatrixAt));
        Log("EDF41MOTHER hull %p: 4.1's movement from %.0f %.0f %.0f, yaw %.3f",self,h.pos[0],h.pos[1],h.pos[2],h.yaw);
    }
    RouteLog(self,h);
    if(Cfg().edf41MotherPlace>0.0f || (CarrierMoves() && !h.control.Active() && h.death==kDeathNone)) {   // development: placed by hand / compared
        float before[3];
        std::memcpy(before,h.pos,12);
        std::memcpy(h.pos,o+kPosAt,12);
        for(int k=0;k<3;++k)h.vel[k]=h.pos[k]-before[k];   // for its death
        std::memcpy(&h.yaw,o+kCarrierYaw,4);
        return;
    }
    h.yawRate+=(h.yawTarget-h.yawRate)*0.01f;h.yaw+=h.yawRate;
    if(h.control.Active()) {h.yaw+=h.control.yaw-h.manualYawLast;h.manualYawLast=h.control.yaw;}
    if(h.death==kDeathNone) {
        const bool route=!h.control.Active() && Readable(o+kExplorer,8) && *reinterpret_cast<void* const*>(o+kExplorer) && o[kRoutePointSet];
        if(h.control.Active()) {
            const auto& in=h.control.input;
            const float cap=(std::max)(0.0f,F(self,kMoveSpeed)*0.5f),sn=std::sin(h.control.yaw),cs=std::cos(h.control.yaw);
            // Camera-relative: the screen's right of heading yaw is (-cos, 0, sin) (enemy_control_camera.h).
            h.point[0]=h.pos[0]+(in.move[2]*sn-in.move[0]*cs)*cap;
            h.point[1]=h.pos[1]+in.move[1]*cap;
            h.point[2]=h.pos[2]+(in.move[2]*cs+in.move[0]*sn)*cap;
        } else if(route)std::memcpy(h.point,o+kRoutePoint,12);
        else for(float& v:h.vel)v*=0.99f;
        float t[3]={h.point[0]-h.pos[0],h.point[1]-h.pos[1],h.point[2]-h.pos[2]};
        const float l=std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]),cap=(std::max)(0.0f,F(self,kMoveSpeed)*0.5f);
        if(l>cap && l>1e-6f)for(float& c:t)c*=cap/l;
        for(int k=0;k<3;++k) { h.vel[k]+=(t[k]-h.vel[k])*0.001f; h.pos[k]+=h.vel[k]; }
        std::memcpy(o+kPosAt,h.pos,12);
        std::memcpy(o+kCarrierVelocity,h.vel,12);
        if((h.ticks%600)==1) {
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<60)Log("EDF41MOTHER hull %p frame %u move: pos %.0f %.0f %.0f, velocity %.3f %.3f %.3f, route %d point %.0f %.0f %.0f, speed %.2f, explorer %p",
                self,h.ticks,h.pos[0],h.pos[1],h.pos[2],h.vel[0],h.vel[1],h.vel[2],route ? 1 : 0,h.point[0],h.point[1],h.point[2],F(self,kMoveSpeed),
                Readable(o+kExplorer,8) ? *reinterpret_cast<void* const*>(o+kExplorer) : nullptr);
        }
    }
    float r[16];
    RotY(h.yaw,r);
    std::memcpy(o+kMatrixAt,r,48);   // the rows; the position row is ours
    std::memcpy(o+kCarrierYaw,&h.yaw,4);
    std::memcpy(o+kCarrierYawTarget,&h.yaw,4);
}

bool WeakMatrix(void* self,Hull& h,float* out) noexcept {
    if(h.weakBone==-2)h.weakBone=BoneIndex(static_cast<char*>(self)+kCarrierSkeleton,L"weak");
    if(BoneMatrix(static_cast<char*>(self)+kCarrierSkeleton,h.weakBone,out))return true;
    Matrix(self,out);
    return false;
}

// After a hit: 64 frames, every 8th a fire burst near the weak point (4.1 0x2512C0: mp_Weakpoint offset (0, -40, 0) plus a point
// within 80 on the node's xz plane, along the node's -Y, size 20).
void HullHits(void* self,Hull& h) noexcept {
    if(h.hitFrames<=0)return;
    if((h.hitFrames&7)==0) {
        float b[16];
        WeakMatrix(self,h,b);
        const float a=2.0f*kPi*Random01(h.rng),r=80.0f*Random01(h.rng);
        const float local[3]={r*std::sin(a),-40.0f,r*std::cos(a)},down[3]={0.0f,-1.0f,0.0f};
        float at[3],dir[3];
        Point(b,local,at);
        Direction(b,down,dir);
        FireBurst(at,dir,20.0f);
    }
    --h.hitFrames;
}

// The 16 exhaust vents (mp_HatchEmission [16, "weak", 85, 20]; 4.1 0x2537F0 each frame: vent k at (85 sin a, 20, 85 cos a) in the
// weak node's space, a = 2 pi (k + 0.5) / 16, blowing along the node's -Y; on while the weak point is open (0x252A30), its colour
// 2 x durability / max and alpha 1 - 0.65 x durability / max (0x253750)). EDF6's own emitter does the particles (MakeVents).
void Vents(void* self,Hull& h,void* dt) noexcept {
    MakeVents(h);
    if(Cfg().edf41MotherForce&512)return;   // bit 512: bisecting, made but never stepped
    float b[16];
    if((Cfg().edf41MotherForce&2048) || !h.ventsMade) {   // bit 2048, or no emitters: EffectGenUtil's ExplosionWind puffs (煙_01) at the same vent points
        if(!h.vents || !HullAlive(self))return;
        WeakMatrix(self,h,b);
        for(int k=0;k<16;++k) {
            if(((h.ticks+static_cast<std::uint32_t>(k))&7u)!=0)continue;
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f)/16.0f;
            const float local[3]={85.0f*std::sin(a),20.0f,85.0f*std::cos(a)};
            float at[3];
            Point(b,local,at);
            const float mx=F(self,kMaxDurabilityAt);
            Smoke(at,(std::max)(0.1f,mx>0.0f ? 2.0f*F(self,kDurabilityAt)/mx : 2.0f));
        }
        return;
    }
    WeakMatrix(self,h,b);
    const float max=F(self,kMaxDurabilityAt);
    const float r=max>0.0f ? (std::max)(0.0f,F(self,kDurabilityAt))/max : 0.0f;
    for(int k=0;k<16;++k) {
        char* e=static_cast<char*>(h.vent[k]);
        if(!e)continue;
        char* d=e+kEmitterDesc;
        const float a=2.0f*kPi*(static_cast<float>(k)+0.5f)/16.0f;
        const float local[3]={85.0f*std::sin(a),20.0f,85.0f*std::cos(a)};
        float at[3];
        Point(b,local,at);
        const float pos[4]={at[0],at[1],at[2],1.0f},dir[4]={-b[4],-b[5],-b[6],0.0f};
        d[0x50]=h.vents && HullAlive(self) ? 1 : 0;
        std::memcpy(d+0x60,pos,16);
        std::memcpy(d+0x70,dir,16);
        const float s=2.0f*r,al=1.0f-0.65f*r,c0[4]={s,s,s,al},c1[4]={s,s,s,0.0f};
        std::memcpy(d+0xE0,c0,16);
        std::memcpy(d+0x100,c1,16);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kEmitterStep)(d,dt);
    }
}

bool HullClipDone(const void* self) noexcept {
    const char* o=static_cast<const char*>(self);
    if(!Readable(o+kCarrierCas+0x60,8) || !Readable(o+kCarrierMainPlayer,4))return false;
    const char* players=*reinterpret_cast<const char* const*>(o+kCarrierCas+0x60);
    const int index=*reinterpret_cast<const std::int32_t*>(o+kCarrierMainPlayer);
    if(!players || index<0 || index>64)return false;
    const char* p=players+static_cast<std::size_t>(index)*0x160;
    if(!Readable(p+0xE8,0x10))return false;
    float t,len;
    std::memcpy(&t,p+0xE8,4);
    std::memcpy(&len,p+0xF0,4);
    return t+0.5f>=len;
}
bool HullState(void* self,const wchar_t* state) noexcept {
    char* o=static_cast<char*>(self);
    if(!MotherPrims() || !Readable(o+kCarrierCas,0x68) || !Readable(o+kCarrierMainPlayer,4))return false;
    const int index=*reinterpret_cast<const std::int32_t*>(o+kCarrierMainPlayer);
    const bool ok=reinterpret_cast<bool(__fastcall*)(void*,int,const wchar_t*,int)>(image+kSetState)(o+kCarrierCas,index,state,0);
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<24)Log("EDF41MOTHER hull %p plays %ls on its main player %d: %s",self,state,index,ok ? "ok" : "refused");
    return ok;
}

// 4.1's main state machine (0x252700 .. 0x252DD0): the main cannon's enable opens the hull (open; show once the cannon is gone);
// open: the vents blow, 排気; a disable closes it; with the cannon gone, 540 frames after the first hit it hides the weak point
// (hide) for 30 s, then shows it again.
void HullGo(void* self,Hull& h,int state,bool big) noexcept {
    h.mainState=state;
    h.mainFrames=0;
    float pos[3];
    PartPos(self,pos);
    switch(state) {
        case kHullOpening: HullState(self,big ? L"open" : L"show"); break;   // 4.1 0x2527D0 enter: open while the main cannon exists
        case kHullOpen: h.damaged=false; h.hideWait=540; h.vents=true; PlaySe(self,L"母船UFO排気",pos); break;
        case kHullHidden: HullState(self,L"hide"); h.vents=false; break;
        case kHullClosing: HullState(self,L"close"); h.vents=false; break;
        default: break;
    }
    Log("EDF41MOTHER hull %p main state %d at its frame %u",self,state,h.ticks);
}
void HullMain(void* self,Hull& h) noexcept {
    Part* big=nullptr;
    for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==kBigCannon && !p->gone && Live(o)) { big=p; break; } }
    if(h.damaged && h.hideWait>0)--h.hideWait;
    const int f=h.mainFrames;
    switch(h.mainState) {
        case kHullClosed:
            if(big && big->enabled)HullGo(self,h,kHullOpening,true);
            break;
        case kHullOpening:
            if(f>1 && HullClipDone(self))HullGo(self,h,kHullOpen,big!=nullptr);
            break;
        case kHullOpen:
            if(big) { if(!big->enabled)HullGo(self,h,kHullClosing,true); }
            else if(h.hideWait==0)HullGo(self,h,kHullHidden,false);
            break;
        case kHullHidden:
            if(static_cast<float>(f)/60.0f>30.0f)HullGo(self,h,kHullOpening,big!=nullptr);
            break;
        case kHullClosing:
            if(f>1 && HullClipDone(self))HullGo(self,h,kHullClosed,big!=nullptr);
            break;
        default: h.mainState=kHullClosed; break;
    }
    ++h.mainFrames;
}

// 4.1 0x251170 (HP <= 0 the first time) -> sequence A 0x251F30.
void StartDeath(void* self,Hull& h) noexcept {
    if(h.dead)return;
    h.dead=true;
    // Death owns the baseline now. Releasing a player lease later must never restore live attack flags.
    h.control.Restore();h.controlInbox.clear();
    for(void* child:h.parts)if(Part* p=PartOf(child)) {
        p->spawnWave.Finish();p->control.Restore();p->controlInbox.clear();p->manualLease=0;p->manualBeam=false;p->panelOnce=false;p->controlledWave=false;p->manualSeedSet=false;
    }
    h.death=kDeathExplode;
    h.deathFrames=0;
    h.counter=0;
    PartPos(self,h.deadPos);
    SetGlow(h.glow,100.0f,100.0f,100.0f,1.0f);   // white, then dark at the hull's rate (0.01)
    GlowTo(h.glow,0.0f,0.0f,0.0f,1.0f);
    const float at[3]={h.deadPos[0],h.deadPos[1]-350.0f,h.deadPos[2]},down[3]={0.0f,-1.0f,0.0f};
    BigExplosion(at,down,2.0f,1.0f);
    PlaySe(self,L"母船UFO本体墜落前爆発",h.deadPos);
    h.vents=false;
    h.yawTarget=0.0f;
    // the cannons and panels stop attacking (+0x901 = 0); +0x900 = 0 is a no-op in 4.1 (it asks the dead hull); the main
    // cannon is left as it is
    for(void* o:h.parts) { Part* p=PartOf(o); if(p && (p->group==kCannon || p->group==kPanel))p->attack=false; }
    h.pending.clear();
    for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==kHatch) { p->current.active=false; p->permanent.active=false; } }
    Log("EDF41MOTHER hull %p destroyed at its frame %u (pos %.0f %.0f %.0f, velocity %.3f %.3f %.3f): 4.1's death, sequence A",self,h.ticks,
        h.deadPos[0],h.deadPos[1],h.deadPos[2],h.vel[0],h.vel[1],h.vel[2]);
}

// Sequences A / B / C, each frame after the stock update. The hull moves as 4.1's does dead: the velocity it had, times 0.99 a
// frame; falling (B on) only y -= 0.0003 a frame; written over whatever the carrier's base did.
void DeathTick(void* self,Hull& h) noexcept {
    if(h.death==kDeathNone || h.death==kDeathGone)return;
    if(h.death>=kDeathFall)h.vel[1]-=0.0003f;
    else for(float& v:h.vel)v*=0.99f;
    for(int k=0;k<3;++k)h.deadPos[k]+=h.vel[k];
    std::memcpy(static_cast<char*>(self)+kPosAt,h.deadPos,12);
    const float* pos=h.deadPos;
    const float t=static_cast<float>(h.deathFrames)/60.0f;
    switch(h.death) {
        case kDeathExplode:
            if((h.counter&63u)==0) {   // a big explosion 350 out in a random direction
                const float up[3]={0.0f,1.0f,0.0f};
                float dir[3];
                Cone(up,kPi,h.rng,dir);
                const float at[3]={pos[0]+dir[0]*350.0f,pos[1]+dir[1]*350.0f,pos[2]+dir[2]*350.0f};
                BigExplosion(at,dir,2.0f,0.6f);
            }
            if(t>10.0f) {   // B 0x2522C0: falls
                h.death=kDeathFall;
                h.deathFrames=-1;
                h.counter=0;
                StopSound(h.fly);
                PlaySe(self,L"母船UFO本体墜落",pos,&h.fall);
                Log("EDF41MOTHER hull %p: sequence B, it falls from %.0f %.0f %.0f",self,pos[0],pos[1],pos[2]);
            }
            break;
        case kDeathFall: {
            MoveSound(h.fall,pos);
            if((h.counter&63u)==0) {   // smoke and fire under the weak point (its node - 20 Y), within 1 rad of its -Y
                float b[16];
                WeakMatrix(self,h,b);
                const float off[3]={0.0f,-20.0f,0.0f},down[3]={-b[4],-b[5],-b[6]};
                float at[3],dir[3];
                Point(b,off,at);
                Cone(down,1.0f,h.rng,dir);
                Blazing(at,dir,1.5f);
            }
            // 4.1: a Havok sphere of radius 300 round the hull touches something; here the map within 300 straight below
            float hit[3];
            const float to[3]={pos[0],pos[1]-300.0f,pos[2]};
            const bool ground=GroundHit(pos,to,hit,static_cast<char*>(self)+kCarrierRagdoll);
            if(ground || t>60.0f) {
                PlaySe(self,L"母船UFO本体墜落",pos);
                const float at[3]={pos[0],pos[1]-200.0f,pos[2]},up[3]={0.0f,1.0f,0.0f};
                for(int i=0;i<3;++i)BigExplosion(at,up,6.0f,1.0f);
                StopSound(h.fall);
                h.death=kDeathLanded;
                h.deathFrames=-1;
                Log("EDF41MOTHER hull %p: sequence C, %s at %.0f %.0f %.0f after %.1f s",self,ground ? "the ground" : "60 s",pos[0],pos[1],pos[2],t);
            }
            break;
        }
        case kDeathLanded:
            if(t>2.0f) {   // 4.1 0x2526E0 -> 0x56BBC0: deleted, its parts with it
                h.death=kDeathGone;
                Log("EDF41MOTHER hull %p deleted at the end of its death (frame %u)",self,h.ticks);
                DeleteObject(self);
            }
            break;
        default: break;
    }
    ++h.deathFrames;
}

std::uint64_t __fastcall HullThinkControl(void* self,void* dt) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    const Hull* h=HullOf(self);
    if(h && h->control.Active())return 0; // suspend native route decisions, keep slot 5 and the real movement machine
    return reinterpret_cast<UpdateFn>(hullClass.orig[7])(self,dt);
}
std::uint64_t __fastcall HullUpdate(void* self,void* dt) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Hull& h=hulls[self];
    NetControlIn(self);ControlTick(self);
    if(Cfg().edf41MotherPlace>0.0f) {   // development: held in front of the camera (ini Edf41MotherPlace)
        float eye[3],dir[3];
        if(CameraRay(eye,dir)) {
            const float d=Cfg().edf41MotherPlace;
            const float p[3]={eye[0]+dir[0]*d,eye[1]+dir[1]*d+300.0f,eye[2]+dir[2]*d};
            std::memcpy(static_cast<char*>(self)+kPosAt,p,12);
        }
    }
    if(h.death!=kDeathGone)HoldPose(self,h);   // ours, not the carrier's (4.1's movement)
    const std::uint64_t result=reinterpret_cast<UpdateFn>(hullClass.orig[kSlotUpdate])(self,dt);
    edf41net::Pump(self);   // online: the parts (made in the factory, before the script gave the hull its identity) get theirs
    ++h.ticks;
    ++h.counter;
    float pos[3];
    PartPos(self,pos);
    if(h.ticks==1) {
        h.rng=reinterpret_cast<std::uintptr_t>(self)*0x9E3779B97F4A7C15ull;
        Log("EDF41MOTHER hull %p first update: durability %.0f / %.0f, level %.2f (raw %.2f), team %d, pos %.0f %.0f %.0f",self,
            F(self,kDurabilityAt),F(self,kMaxDurabilityAt),F(self,kLevelAt),F(self,kRawLevelAt),I(self,kTeamAt),pos[0],pos[1],pos[2]);
        SpawnParts(self,h);
        for(const auto& job:h.pending)AssignWave(h,job.first,job.second);
        h.pending.clear();
    }
    // Closed as 4.1's hull rests (no clip), not on the carrier constructor's close_state (the mothership's close clip, from the
    // opened pose): before HullMain, which opens it this same frame when a creation-frame ObjectAction 3 enabled the main cannon.
    if(edf41pose::HoldNow(edf41pose::Group::kHull,h.restHeld,h.mainState==kHullClosed)) {
        h.restHeld=true;
        HoldRest(self,edf41pose::Group::kHull,kCarrierCas,kCarrierMainPlayer,"hull");
    }
    // 4.1's fly sound (the ctor plays 母船UFO飛行 into +0xC10) in place of the carrier's se_fly_name hum (0x4F7DE0 -> +0xE10)
    if(h.death<kDeathFall) {
        SoundHandle* hum=reinterpret_cast<SoundHandle*>(static_cast<char*>(self)+kCarrierFly);
        if(Readable(hum,sizeof(SoundHandle)) && hum->instance) {
            StopSound(*hum,0);
            if(!h.carrierHumSaid) { h.carrierHumSaid=true; Log("EDF41MOTHER hull %p: the carrier's fly hum stopped for 4.1's 母船UFO飛行 (frame %u)",self,h.ticks); }
        }
        if(!h.fly.instance && h.ticks<=2)PlaySe(self,L"母船UFO飛行",pos,&h.fly);
        if(h.fly.instance)MoveSound(h.fly,pos);
    }
    for(int& t:h.throttle)if(t>0)--t;
    if(h.death!=kDeathGone)Spin(self,h);
    {
        bool cannon=false;
        for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==kBigCannon && !p->gone && Live(o)) { cannon=true; break; } }
        HullLock(self,HullAlive(self) && !cannon && I(self,kTeamAt)!=3);
    }
    if(h.ticks==1 || h.ticks==600 || h.ticks==2400)LockReport(self,"hull");
    BindGlow(h.glow,self,kCarrierModel,"hull");
    TickGlow(h.glow);
    VentGraveyardTick();
    Vents(self,h,dt);
    if(HullAlive(self)) {
        HullMain(self,h);
        // development (ini Edf41DevShoot): at the weak point (its node x mp_Weakpoint's (0, -40, 0), where 4.1's hit bursts go)
        float wm[16],aim[3];
        const float off[3]={0.0f,-40.0f,0.0f};
        if(Alive(self) && WeakMatrix(self,h,wm)) { Point(wm,off,aim); DevShoot(self,aim); }
    }
    HullHits(self,h);
    if(!h.dead && !HullAlive(self))StartDeath(self,h);
    if(h.death!=kDeathNone)DeathTick(self,h);
    Force(self,h);
    if(h.ticks%1800==0) {
        int alive[5]={},dead[5]={};
        for(void* o:h.parts) { Part* p=PartOf(o); if(p)(p->dead ? dead : alive)[p->group]++; }
        Log("EDF41MOTHER hull %p frame %u: durability %.0f, yaw rate %.5f, main state %d, glow %.2f %.2f %.2f; parts alive / destroyed: cannons %d/%d, main %d/%d, panels %d/%d; fired: main %d, cannons %d, panels %d; removed: main %d, cannons %d, panels %d",self,h.ticks,
            F(self,kDurabilityAt),h.yawRate,h.mainState,h.glow.cur[0],h.glow.cur[1],h.glow.cur[2],alive[kCannon],dead[kCannon],alive[kBigCannon],dead[kBigCannon],alive[kPanel],dead[kPanel],
            shotCount[kBigCannon],shotCount[kCannon],shotCount[kPanel],removedCount[kBigCannon],removedCount[kCannon],removedCount[kPanel]);
    }
    return result;
}

std::uint64_t __fastcall HullMessage(void* self,std::uint32_t msg,void* data) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Hull* h=HullOf(self);
        if(h && msg==kMsgAction && data && Rd(data,4,"action message data")) {
            ApplyAction(self,*h,*static_cast<const std::int32_t*>(data));
            return 1;
        }
        if(h && msg==kMsgParts && data) { if(GetParts(*h,static_cast<char*>(data)))return 1; }
        if(h && msg==kMsgGenerator) {
            if(!data || !QueueGenerator(self,static_cast<const char*>(data)))Log("EDF41MOTHER generator refused: invalid 4.1 message on %p",self);
            return 1; // disabled/missing 4.1 hatches must never activate UfoCarrier508's unrelated stock generator
        }
        // The carrier's kill message (0x4F94C0: durability to 0, off its team, its own death state): the same, with 4.1's death
        if(h && msg==kMsgKill && MotherPrims()) {
            if(!static_cast<const char*>(self)[kDestroyedAt] && HullAlive(self)) {
                SetF(self,kDurabilityAt,(std::min)(0.0f,F(self,kMaxDurabilityAt)));
                reinterpret_cast<void(__fastcall*)(void*)>(image+kUnregister)(self);
                Log("EDF41MOTHER hull %p: kill message 0x%X",self,msg);
                StartDeath(self,*h);
            }
            return 1;
        }
    }
    return reinterpret_cast<MessageFn>(hullClass.orig[kSlotMessage])(self,msg,data);
}

// Slot 18 OnDamaged(obj, GDI), called by ApplyDamage for a hit the hull took (4.1 slot 11 0x251170). The carrier's (0x4F9250) is
// not called: alive it plays its own damage sound and blinks its material animation, dead it starts its death states.
void __fastcall HullDamaged(void* self,void* gdi) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Hull* h=HullOf(self);
    if(!h) { reinterpret_cast<DamagedFn>(hullClass.orig[kSlotDamaged])(self,gdi); return; }
    if(!HullAlive(self)) { StartDeath(self,*h); return; }
    h->damaged=true;
    const float max=F(self,kMaxDurabilityAt);
    if(max>0.0f)h->ventSize=2.0f*F(self,kDurabilityAt)/max;   // 4.1 0x253750
    if(h->hitFrames==0) {   // 4.1 0x251DE0
        h->hitFrames=64;
        float at[3];
        if(gdi && Readable(static_cast<char*>(gdi)+kGdiCentre,12))std::memcpy(at,static_cast<char*>(gdi)+kGdiCentre,12);
        else PartPos(self,at);
        PlaySe(self,L"母船UFOダメージ",at);
        SetGlow(h->glow,10.0f,10.0f,10.0f,1.0f);
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<12) {
            const char* g=static_cast<const char*>(gdi);
            const bool gr=g && Readable(g,0x64);
            Log("EDF41MOTHER hull %p hit at %.0f %.0f %.0f: durability %.0f / %.0f (min %.0f), damage %.1f flags %04X, +0x380 %08X, taken x %.2f",self,at[0],at[1],at[2],
                F(self,kDurabilityAt),max,F(self,0x2F0),gr ? F(g,kGdiDamage) : -1.0f,gr ? static_cast<unsigned>(*reinterpret_cast<const std::uint16_t*>(g+kGdiFlags)) : 0u,
                static_cast<unsigned>(I(self,kFlags380)),F(self,0x394));
        }
    }
}

// Slot 3 (draw): the glow into the materials right before the stock draw (as UfoSmall507's 0x52EEC0).
void __fastcall HullDraw(void* self,void* a2) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Hull* h=HullOf(self);
        if(h) { ApplyGlow(h->glow); DrawVents(*h); }
    }
    reinterpret_cast<DrawFn>(hullClass.orig[kSlotDraw])(self,a2);
}
void __fastcall PartDraw(void* self,void* a2) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* p=PartOf(self);
        if(p && !p->gone)ApplyGlow(p->glow);
    }
    reinterpret_cast<DrawFn>(partClass.orig[kSlotDraw])(self,a2);
}

// 4.1 slot 10 (0x250FC0): the hull takes damage only on its `weak` body, and only once the main cannon object is gone.
bool __fastcall HullAccept(void* self,const void* gdi) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Hull* h=HullOf(self);
    if(!h)return false;
    for(void* o:h->parts) { Part* p=PartOf(o); if(p && p->group==kBigCannon && !p->gone && Live(o))return false; }
    void* ragdoll=static_cast<char*>(self)+kCarrierRagdoll;
    if(h->weakBody==-2) {
        // the shell's bodies are named after the 4.1 SHKT's rigid bodies: RagDollProxys.weak / .waist / .hatch
        h->weakBody=RagdollBody(ragdoll,L"RagDollProxys.weak");
        if(h->weakBody<0)h->weakBody=RagdollBody(ragdoll,L"weak");
        Log("EDF41MOTHER hull %p: weak body %d (RagDollProxys.waist %d, RagDollProxys.hatch %d)",self,h->weakBody,
            RagdollBody(ragdoll,L"RagDollProxys.waist"),RagdollBody(ragdoll,L"RagDollProxys.hatch"));
    }
    if(h->weakBody<0)return true;   // no such body in the shell: any hit counts (logged once above)
    const int body=HitBody(ragdoll,gdi);
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<16)Log("EDF41MOTHER hull %p hit on body %d (weak %d): %s",self,body,h->weakBody,body==h->weakBody ? "taken" : "refused");
    return body==h->weakBody;
}

std::uint64_t __fastcall PartDtor(void* self,unsigned flags) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* p=PartOf(self);
        if(p) {
            Hull* h=HullOf(p->hull);
            if(h)h->parts.erase(std::remove(h->parts.begin(),h->parts.end(),self),h->parts.end());
            DropPart(self);
            NetForget(self);   // online (edf41net)
        }
        Forget(self);
    }
    return reinterpret_cast<DtorFn>(partClass.orig[kSlotDtor])(self,flags);
}

// M123 (2026-10-15) crashed in the stock retarget (0x54C4F9 -> 0x5E0F20) on a part whose team field +0x314 held a float (0.93):
// until the writer is found, a part's team is checked around each step of its frame, logged where it went wrong and put back
// (a team out of range indexes the team table out of bounds).
void TeamGuard(void* self,Part& p,const char* where) noexcept {
    const std::int32_t t=I(self,kTeamAt);
    if(t>=-1 && t<16)return;
    const std::int32_t fix=p.team>=-1 && p.team<16 ? p.team : 1;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12) {
        float asFloat;
        std::memcpy(&asFloat,&t,4);
        Log("EDF41MOTHER part %s #%d %p: team field %08X (as a float %.4f) %s; frame %d state %d; +0x310 %.4f +0x318 %d; put back to %d",
            kGroupNames[p.group],p.index,self,static_cast<unsigned>(t),asFloat,where,p.frames,p.state,F(self,0x310),I(self,0x318),fix);
    }
    std::memcpy(static_cast<char*>(self)+kTeamAt,&fix,4);
}
// Development (Edf41DevShoot): where a player would shoot a part. The cannons' origins are inside the hull (a shot there hits the
// hull's shell first): their barrels instead (the main cannon's waist bone at -450, near its tip; a cannon's `cannon` bone at -200).
bool DevPoint(void* obj,Part& p,float* out) noexcept {
    if(p.group==kBigCannon || p.group==kCannon) {
        void* skel=static_cast<char*>(obj)+kEaoSkeleton;
        if(p.devBone==-2)p.devBone=BoneIndex(skel,p.group==kBigCannon ? L"waist" : L"cannon");
        float b[16];
        if(BoneMatrix(skel,p.devBone,b)) {
            const float local[3]={0.0f,p.group==kBigCannon ? -450.0f : -200.0f,0.0f};
            Point(b,local,out);
            return true;
        }
    }
    return AimPoint(obj,out);
}
std::uint64_t __fastcall PartUpdate(void* self,void* dt) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=PartOf(self);
    if(!p)return reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    Hull* h=Live(p->hull) ? HullOf(p->hull) : nullptr;
    if(!h) {   // the hull is gone (destroyed, or the script deleted it): the part goes with it (4.1 0x56BBC0)
        StopAnimationSounds(*p,0);
        if(!p->gone) { DeleteObject(self); p->gone=true; }
        return reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    }
    TeamGuard(self,*p,"before its update");
    if(!p->gone)Place(self,*p);
    TeamGuard(self,*p,"after Place");
    const std::uint64_t r=reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    p=PartOf(self);
    h=p && Live(p->hull) ? HullOf(p->hull) : nullptr;
    if(p)TeamGuard(self,*p,"after the class's update");
    if(p && h && !p->gone)ControlTick(self);
    if(p && h && !p->gone)NetPartIn(self,*p);   // online: the host's decisions (edf41net)
    if(p && h && !p->gone) {
        TickAnimationSounds(self,*p); // finish crossed keys before PartFrame can leave the old clip
        PartFrame(self,*p,*h);
        TickAnimationSounds(self,*p); // a state entered at time zero starts its keys in this frame
    }
    if(p)TeamGuard(self,*p,"after PartFrame");
    if(p && h && !p->gone){ float aim[3]; if(Alive(self) && DevPoint(self,*p,aim))DevShoot(self,aim); }   // development (ini Edf41DevShoot)
    return r;
}

// The damage message as GameObjectBase's slot 9 (0x54A530) takes it: `add edx, 0xF0000000; cmp edx, 0xF` dispatches 0x10000000..
// 0x1000000F and case 0 (0x10000000) is ApplyDamage (0x54A586 -> 0x547C30); the damage queue 0x543920 sends exactly that, for a
// direct hit and for each object of an area damage alike (docs/damage-stats-re.md, docs/edf41-uforobo.md). edf41classes.h's
// kMsgDamage (0xF0000000) never reaches this case, so until 2026-10-17 no part of the mothership ever played its hit sound here.
constexpr std::uint32_t kMsgHit=0x10000000u;
std::uint64_t __fastcall PartMessage(void* self,std::uint32_t msg,void* data) {
    const float before=F(self,kDurabilityAt);
    const std::uint64_t r=reinterpret_cast<MessageFn>(partClass.orig[kSlotMessage])(self,msg,data);
    if(msg!=kMsgHit || !data)return r;
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=PartOf(self);
    const float after=F(self,kDurabilityAt);
    if(p && after<before) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<24)Log("EDF41MOTHER part %s #%d %p took %.0f: %.0f -> %.0f",kGroupNames[p->group],p->index,self,before-after,before,after);
    }
    if(p && after<before && after>0.0f && p->damageCooldown<=0 && (p->group==kBigCannon || p->group==kCannon)) {
        p->damageCooldown=60;
        float at[3];
        std::memcpy(at,static_cast<const char*>(data)+kGdiCentre,12);
        PlaySe(p->hull,p->group==kBigCannon ? L"母船UFO砲台ダメージ" : L"母船UFO中型砲台ダメージ",at);
    }
    return r;
}

// ---- the creation ---------------------------------------------------------------------------------------------------
// The records of objects the game has freed (no destructor call): dropped when a new hull is made.
void Prune() noexcept {
    for(auto it=hulls.begin();it!=hulls.end();) {
        if(Live(it->first)) { ++it; continue; }
        for(void* o:it->second.parts) { DropPart(o); Forget(o); }
        Forget(it->first);
        void* gone=it->first;
        ++it;
        DropHull(gone);
    }
    for(auto it=parts.begin();it!=parts.end();) {
        if(Live(it->first) || (it->second.hull && hulls.count(it->second.hull))) { ++it; continue; }
        Forget(it->first);
        void* gone=it->first;
        ++it;
        DropPart(gone);
    }
}
void CreatePart(void* info) noexcept {
    void* obj=CloneObject(partClass,kEaoCtor,kEaoSize,info);
    edf41net::Attach(obj);   // online: its NetworkObject takes the host's events (edf41net)
}
void CreateHull(void* info) noexcept {
    void* obj=CloneObject(hullClass,kCarrierCtor,kCarrierSize,info);
    if(!obj)return;
    edf41net::Attach(obj);   // the controlled hull receives its own semantic events, as its child parts do
    Log("EDF41MOTHER create #%d: UfoMother301 %p on UfoCarrier508's constructor",++created,obj);
    // The parts are made here, as 4.1's constructor makes them (0x24FFF0) and EDF6's UfoMother511 makes its ring units in its own
    // (0x4FE740 -> 0x4FEF00): M603 and M993 send ObjectAction / GetPartsObject on the creation frame, before any Update.
    std::lock_guard<std::recursive_mutex> hold(lock);
    Prune();
    DropHull(obj);   // a record left at this address by a hull freed without its destructor
    Track(obj);
    SpawnParts(obj,hulls[obj]);
}

// ---- online (src/edf41net.h; docs/edf41-ufomother301.md 3.5, docs/edf41-classes.md 联机) ---------------------------------------
// Every machine runs the script, so every machine makes the hull (the script gives it the game's network identity, the host's),
// and its factory makes the same parts in the same order. The hull's own pose travels as UfoCarrier508's (its NetworkObject
// slots 7 / 8: the position every 60 frames, the hull's base moves it on every machine along the same route); its spin and the
// parts' flags come from the script's actions, which reach every machine. Each part gets the game's child identity under the
// hull by its creation order (0x781630 as MovingFortress's parts: queued in the factory, given at the hull's first update after
// the script registered it), each hatch's enemy by the hatch and its count: their durability, the damage dealt to them (judged
// by the shooter's machine, the HP change broadcast), their kills and the parts' targets (+0x591) travel natively; PartFrame
// already sees a death from the HP. The one random timer whose rolls can drift between machines (a cannon's 1800..3599-frame
// wait: its rng is shared with the beam's scatter, which rolls only when the cannon has a target) is the host's: kCannonFire on
// the cannon's own NetworkObject, and a guest copy waits in kCanWait for it.
enum : std::uint8_t { kCannonFire=17 };
struct CannonFireMsg { std::int32_t waitFrames,reserved; };
static_assert(sizeof(CannonFireMsg)==8,"fixed wire layout");
struct PartNet { int pendingWait=-1,pendingAge=0; };
std::unordered_map<void*,PartNet> partNets;   // under `lock`
std::uint32_t netSent=0,netApplied=0;

bool NetDecides(void* part) noexcept { return !edf41net::Online() || edf41net::Decides(part); }

void NetCannonFire(void* part,const Part& p) noexcept {
    if(!edf41net::Online() || !edf41net::Decides(part))return;
    const CannonFireMsg m{p.waitFrames,0};
    if(edf41net::Send(part,kCannonFire,&m,sizeof(m)) && ++netSent<=3)
        Log("EDF41MOTHER cannon #%d %p online: its charge sent to the guests (%u so far)",p.index,part,netSent);
}

void NetPart(void* hull,void* part,int group,std::size_t order) noexcept {
    if(!edf41net::Online() || order>=0x7FFF)return;
    edf41net::Child(hull,part,edf41net::kChildPart,static_cast<std::uint16_t>(order),group==kCannon || group==kPanel);
}

void NetSpawn(const Part& p,void* enemy) noexcept {
    if(!edf41net::Online() || !enemy || p.index<0 || p.index>=0x100)return;
    edf41net::Child(p.hull,enemy,static_cast<std::uint16_t>(edf41net::kChildSpawn+p.index),static_cast<std::uint16_t>(p.made&0xFFFF),false);
}

void CannonApply(void* context,std::uint8_t kind,const unsigned char* data,std::size_t size) noexcept {
    void* part=context;
    if(kind==18 && size==sizeof(mother_control::Command)) {
        // Command application is defined with the adapter below; it runs on this object's real machine.
        mother_control::Command command;std::memcpy(&command,data,sizeof(command));
        if(Hull* h=HullOf(part);h && h->control.lease==command.lease && command.lease && h->controlInbox.size()<enemy_legacy::Control::kPending)h->controlInbox.push_back(command);
        else if(Part* p=PartOf(part);p && p->control.lease==command.lease && command.lease && p->controlInbox.size()<enemy_legacy::Control::kPending)p->controlInbox.push_back(command);
        return;
    }
    if(kind!=kCannonFire || size!=sizeof(CannonFireMsg)) { Fail("a 301 part event of a kind or size this version does not send (refused)"); return; }
    CannonFireMsg m;
    std::memcpy(&m,data,sizeof(m));
    if(m.waitFrames<0 || m.waitFrames>100000) { Fail("a 301 part event out of range (refused)"); return; }
    PartNet& n=partNets[part];
    n.pendingWait=m.waitFrames;
    n.pendingAge=0;
}

void NetPartIn(void* part,Part& p) noexcept {
    if(!edf41net::Online() || !edf41net::Guest(part))return;
    edf41net::Drain(part,&CannonApply,part);ReplayControl301(part);
    const auto it=partNets.find(part);
    if(it==partNets.end() || it->second.pendingWait<0)return;
    PartNet& n=it->second;
    if(p.state==kCanWait && !p.dead) {
        Switch(p,kCanFire);
        p.waitFrames=n.pendingWait;
        n.pendingWait=-1;
        if(++netApplied<=3)Log("EDF41MOTHER cannon #%d %p guest copy: the host's charge applied (%u so far)",p.index,part,netApplied);
    } else if(++n.pendingAge>600)n.pendingWait=-1;
}

void NetForget(void* part) noexcept { partNets.erase(part); }
// ---- player control: semantic commands feed the existing hull/part machines -------------------------
const wchar_t* const controlNames301[]={L"立即停止旋转",L"缓慢停止旋转",L"恢复缓慢旋转",
 L"展开主炮",L"收起主炮",L"主炮持续攻击",L"主炮停止攻击",L"主炮单次扫射",
 L"展开中型炮",L"停用中型炮",L"中型炮持续攻击",L"中型炮停止攻击",L"中型炮强化",
 L"展开炮台面板",L"停用炮台面板",L"面板持续攻击",L"面板停止攻击",L"面板强化"};
bool CombatPart301(const Part& p) noexcept {
    const Hull* h=HullOf(p.hull);
    return h && !h->dead && !p.statik && !p.dead && !p.gone && p.group!=kCover && Live(p.hull) && HullAlive(p.hull);
}
bool ControlMatch301(const void* object) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);void* o=const_cast<void*>(object);
    if(const Hull* h=HullOf(o))return Live(o) && !h->dead && HullAlive(o);
    const Part* p=PartOf(o);return p && Live(o) && CombatPart301(*p);
}
unsigned ControlAbilities301(const void* object,enemy_control::Ability* out,unsigned cap) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);void* o=const_cast<void*>(object);unsigned count=0;
    const Hull* h=HullOf(o);const Part* p=PartOf(o);
    if(h) {
        const auto hasGroup=[&](int group) {for(void* child:h->parts)if(const Part* q=PartOf(child);q && q->group==group && !q->dead && !q->gone && !q->statik)return true;return false;};
        for(unsigned a=0;a<18;++a)mother_control::Ability(out,cap,count++,mother_control::Id301(a),controlNames301[a],a<3 || (a!=12 && a!=17 && hasGroup(a<8 ? kBigCannon : a<13 ? kCannon : kPanel)));
        mother_control::Ability(out,cap,count++,mother_control::Id301(19),L"中型炮单次蓄力发射",hasGroup(kCannon));
        mother_control::Ability(out,cap,count++,mother_control::Id301(20),L"切换中型炮束流攻击",hasGroup(kCannon));
        mother_control::Ability(out,cap,count++,mother_control::Id301(21),L"炮台面板单次发射",hasGroup(kPanel));
        for(void* child:h->parts)if(const Part* q=PartOf(child);q && q->group==kHatch && !q->gone) {
            wchar_t name[96];std::swprintf(name,96,L"发进舱 %d：召唤任务配置的敌人",q->index+1);
            mother_control::Ability(out,cap,count++,mother_control::Id301(mother_control::kSummon,static_cast<unsigned>(q->index)),name,q->configuredOnce && q->configured.path[0]!=0 && q->configured.count>0 &&
                mother_control::SpawnWave::CanPrepare(ObjRef::Of(child),q->configured.path,static_cast<unsigned>(q->configured.count)));
        }
    } else if(p && CombatPart301(*p)) {
        if(p->group==kHatch)mother_control::Ability(out,cap,count++,mother_control::Id301(mother_control::kSummon,static_cast<unsigned>(p->index)),L"召唤任务配置的敌人",p->configuredOnce && p->configured.path[0]!=0 && p->configured.count>0 &&
            mother_control::SpawnWave::CanPrepare(ObjRef::Of(o),p->configured.path,static_cast<unsigned>(p->configured.count)));
        else { const unsigned first=p->group==kBigCannon ? 3u : p->group==kCannon ? 8u : 13u;
            for(unsigned a=first;a<first+5;++a)mother_control::Ability(out,cap,count++,mother_control::Id301(a),controlNames301[a],a!=12 && a!=17);
            if(p->group==kCannon) {mother_control::Ability(out,cap,count++,mother_control::Id301(19),L"单次蓄力发射");mother_control::Ability(out,cap,count++,mother_control::Id301(20),L"切换束流攻击");}
            if(p->group==kPanel)mother_control::Ability(out,cap,count++,mother_control::Id301(21),L"单次发射"); }
    }
    return count;
}
void CapturePart301(Part& p,std::uint64_t lease) noexcept {
    p.manualLease=lease;p.savedEnabled=p.enabled;p.savedAttack=p.attack;p.savedBoost=p.boost;
    // A charge or sweep already in progress completes; no new autonomous attack starts.
    p.attack=false;p.manualBeam=false;p.panelOnce=false;p.manualSeedSet=false;
    p.boost=false;p.controlledWave=p.group==kHatch && p.current.active && !p.waveDone &&
        (p.state==kHatOpening || p.state==kHatOpen);
}
bool ControlBegin301(const void* object,std::uint64_t lease) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);void* o=const_cast<void*>(object);
    if(!lease || !ControlMatch301(o))return false;
    Hull* hull=HullOf(o);Part* selected=PartOf(o);Hull* family=hull ? hull : selected ? HullOf(selected->hull) : nullptr;
    if(!family)return false;
    if(hull && hull->control.Active())return hull->control.lease==lease;
    if(selected && selected->control.Active())return selected->control.lease==lease;
    if(family->control.Active())return false;
    // PVP owns the complete 301 family. A second private controller cannot clear another part's pending spawn gate.
    for(void* child:family->parts)if(const Part* part=PartOf(child);part && part->manualLease)return false;
    for(void* child:family->parts)if(Part* part=PartOf(child);part && part->group==kHatch) {
        part->spawnWave.fault=false;
        if(part->current.active && !part->waveDone && (part->state==kHatOpening || part->state==kHatOpen) &&
           !part->spawnWave.Prepare(ObjRef::Of(child),part->current.path,static_cast<unsigned>((std::max)(0,part->remaining))))return false;
    }
    if(hull) {
        if(!hull->control.Begin(lease,hull->moveInit ? hull->yaw : std::atan2(-F(o,kMatrixAt+8),F(o,kMatrixAt))))return false;
        hull->savedYawTarget=hull->yawTarget;hull->manualYawLast=hull->control.yaw;
        for(void* child:hull->parts)if(Part* part=PartOf(child);part && CombatPart301(*part))CapturePart301(*part,lease);
        return true;
    }
    if(!selected || !selected->control.Begin(lease,std::atan2(F(o,kMatrixAt+32),F(o,kMatrixAt+40))))return false;
    CapturePart301(*selected,lease);return true;
}
bool ControlApply301(const void* object,const enemy_control::Input& in,bool first) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);void* o=const_cast<void*>(object);
    if(!ControlMatch301(o))return false;
    const Hull* family=HullOf(o);if(!family)if(const Part* selected=PartOf(o))family=HullOf(selected->hull);
    if(family)for(void* child:family->parts)if(const Part* part=PartOf(child);part && part->spawnWave.fault)return false;
    if(Hull* h=HullOf(o))return mother_control::Sample(h->control,in,first);
    Part* p=PartOf(o);return p && mother_control::Sample(p->control,in,first);
}
void RestorePart301(Part& p) noexcept {
    if(CombatPart301(p)) {p.enabled=p.savedEnabled;p.attack=p.savedAttack;p.boost=p.savedBoost;
        if(p.group==kHatch && !p.current.active && p.permanent.active) {p.current=p.permanent;p.remaining=p.current.count;p.delayLeft=p.current.delay;p.intervalLeft=0;} }
    p.manualLease=0;p.controlledWave=false;p.manualBeam=false;p.panelOnce=false;p.manualSeedSet=false;p.controlInbox.clear();p.control.Restore();
}
bool ControlRestore301(const void* object) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);void* o=const_cast<void*>(object);
    if(Hull* h=HullOf(o)) {
        if(!h->control.Active())for(void* child:h->parts)if(const Part* part=PartOf(child);part && part->manualLease)return true;
        for(void* child:h->parts)if(Part* part=PartOf(child))part->spawnWave.Reset();
        if(!h->control.Active())return true;
        const auto lease=h->control.lease;
        for(void* child:h->parts)if(Part* p=PartOf(child);p && p->manualLease==lease)RestorePart301(*p);
        h->yawTarget=h->savedYawTarget;std::memcpy(h->point,h->pos,12);h->controlInbox.clear();h->control.Restore();return true;
    }
    if(Part* p=PartOf(o)) {
        Hull* family=HullOf(p->hull);
        if(!p->control.Active() && family) {
            if(family->control.Active())return true;
            for(void* child:family->parts)if(const Part* other=PartOf(child);other && other->manualLease)return true;
        }
        // A failed sibling acquisition must not cancel the already controlled family's reserved wave or fault state.
        if(family)for(void* child:family->parts)if(Part* part=PartOf(child))part->spawnWave.Reset();
        if(p->control.Active())RestorePart301(*p);
    }
    return true;
}
bool ApplyControlCommand(void* object,const mother_control::Command& command) noexcept {
    if(!ControlMatch301(object))return false;
    if(command.action>mother_control::kLast301 || command.index>mother_control::kAll)return false;
    Hull* h=HullOf(object);Part* single=PartOf(object);
    if((h && !h->control.Active()) || (single && !single->control.Active()) || (!h && !single))return false;
    const auto lease=h ? h->control.lease : single ? single->control.lease : 0;
    if(!lease || command.lease!=lease)return false;
    if(command.action>=19) {
        const auto eligible=[&](const Part& p) {return CombatPart301(p) && p.enabled &&
            ((command.action==19 || command.action==20) ? p.group==kCannon : p.group==kPanel);};
        const auto ready=[&](const Part& p) {return command.action==20 || (command.action==19 ?
            p.state==kCanStandby || p.state==kCanWait : p.state==kPanStart || p.state==kPanLaser || p.state==kPanPlasma);};
        bool any=false;
        if(h) {for(void* child:h->parts)if(const Part* p=PartOf(child);p && eligible(*p)){any=true;if(!ready(*p))return false;}}
        else {any=single && eligible(*single);if(any && !ready(*single))return false;}
        if(!any)return false;
        const auto fire=[&](void* o,Part& p) {
            if(!eligible(p))return;
            if(command.action==19)p.rng=command.seed^static_cast<unsigned>(p.index);
            if(command.action==19 && p.group==kCannon && (p.state==kCanStandby || p.state==kCanWait))Switch(p,kCanFire);
            if(command.action==20 && p.group==kCannon) {p.manualBeam=!p.manualBeam;p.roundStarted=p.manualBeam;if(p.manualBeam)p.rng=command.seed^static_cast<unsigned>(p.index);}
            if(command.action==21 && p.group==kPanel) {p.panelOnce=true;p.attack=true;p.manualSeed=command.seed^static_cast<unsigned>(p.index);p.manualSeedSet=true;}
            (void)o;
        };
        if(h)for(void* child:h->parts){if(Part* p=PartOf(child))fire(child,*p);}else if(single)fire(object,*single);
        return true;
    }
    if(command.action<18) {
        if(command.action==12 || command.action==17)return false;
        const auto prime=[&](Part& p) {
            if((p.group==kBigCannon && (command.action==7 || (command.action==5 && !p.attack))) ||
               (p.group==kCannon && command.action==10 && !p.attack) || (p.group==kPanel && command.action==15 && !p.attack)) {
                p.manualSeed=command.seed^static_cast<unsigned>(p.index);p.manualSeedSet=true;
            }
        };
        if(h) {for(void* child:h->parts)if(Part* p=PartOf(child);p && p->manualLease)prime(*p);ApplyAction(object,*h,static_cast<int>(command.action));
            if(command.action==10 || command.action==11)for(void* child:h->parts)if(Part* p=PartOf(child);p && p->group==kCannon){p->manualBeam=command.action==10;p->roundStarted=p->manualBeam;} }
        else if(single && CombatPart301(*single)) {
            const unsigned start=single->group==kBigCannon ? 3u : single->group==kCannon ? 8u : 13u;
            if(single->group==kHatch || command.action<start || command.action>=start+5)return false;
            prime(*single);const unsigned what=command.action-start;
            if(what==0)single->enabled=true;else if(what==1)single->enabled=false;else if(what==2)single->attack=true;else if(what==3)single->attack=false;else single->boost=true;
            if(single->group==kCannon && (what==2 || what==3)){single->manualBeam=what==2;single->roundStarted=single->manualBeam;}
        }
        return true;
    }
    Part* hatch=single;void* hatchObject=single ? object : nullptr;
    if(h)for(void* child:h->parts)if(Part* p=PartOf(child);p && p->group==kHatch && p->index==static_cast<int>(command.index)){hatch=p;hatchObject=child;break;}
    if(!hatch || hatch->group!=kHatch || !hatch->configuredOnce || !hatch->configured.path[0] || hatch->state!=kHatClosed)return false;
    if(!hatch->spawnWave.Prepare(ObjRef::Of(hatchObject),
        hatch->configured.path,static_cast<unsigned>(hatch->configured.count))) {
        Log("EDF41MOTHER controlled summon refused: %ls has no complete native ownership provider/budget",hatch->configured.path);return false;
    }
    hatch->current=hatch->configured;hatch->current.kind=1;hatch->current.active=true;hatch->remaining=hatch->current.count;
    hatch->delayLeft=0;hatch->intervalLeft=0;hatch->controlledWave=true;hatch->rng=command.seed;return true;
}
void ControlTick(void* object) noexcept {
    Hull* h=HullOf(object);Part* p=PartOf(object);auto* c=h ? &h->control : p ? &p->control : nullptr;
    if(!c || !c->Active())return;
    if(NetDecides(object))for(unsigned i=0;i<c->pending;++i) {
        unsigned action=0,index=0;if(!mother_control::Decode301(c->pressed[i].id,action,index))continue;
        const mother_control::Command command{action,index,c->pressed[i].seed,c->lease};
        if(ApplyControlCommand(object,command) && edf41net::Online())edf41net::Send(object,18,&command,sizeof(command));
    }
    c->ClearEdges();
}
bool RetryControl301(void* object,const mother_control::Command& command) noexcept {
    if(!ControlMatch301(object))return false;
    const Hull* h=HullOf(object);const Part* single=PartOf(object);
    const auto pending=[&](const Part& p) {
        if(!CombatPart301(p))return false;
        if(command.action==mother_control::kSummon)return p.group==kHatch && p.index==static_cast<int>(command.index) && p.configuredOnce && p.configured.path[0];
        if(command.action>=19)return p.enabled && p.group==(command.action==21 ? kPanel : kCannon);
        return false;
    };
    if(h)for(void* child:h->parts)if(const Part* p=PartOf(child);p && pending(*p))return true;
    return single && pending(*single);
}
void ReplayControl301(void* object) noexcept {
    Hull* h=HullOf(object);Part* p=PartOf(object);auto* inbox=h ? &h->controlInbox : p ? &p->controlInbox : nullptr;
    const auto lease=h ? h->control.lease : p ? p->control.lease : 0;
    if(!inbox)return;
    while(!inbox->empty()) {
        if(inbox->front().lease!=lease){inbox->erase(inbox->begin());continue;}
        if(!ApplyControlCommand(object,inbox->front()) && RetryControl301(object,inbox->front()))break;
        // A weapon removed while its accepted command was in flight cannot block the surviving hull's other skills.
        inbox->erase(inbox->begin());
    }
}
void NetControlIn(void* object) noexcept {
    if(edf41net::Online() && edf41net::Guest(object)){edf41net::Drain(object,&CannonApply,object);ReplayControl301(object);}
}
std::uint64_t ControlHash301(const void* object) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);const Part* p=PartOf(const_cast<void*>(object));
    return ControlMatch301(object) ? mother_control::Hash(301,p ? static_cast<unsigned>(p->group)+1u : 0u) : 0;
}
bool ControlNetwork301(const void* object) noexcept {return edf41net::Enabled() && edf41net::Registered(object);}
// PVP collects the real complete 301 family: Masking otherwise reattaches every part to its hull's team.
controlled_pvp::Life PvpIdentity301(const ObjRef& ref) noexcept {
    using controlled_pvp::Life;
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16))return Life::invalid;
    if(At<LONG>(ref.ctrl,8)<=0)return Life::gone;
    if(!Readable(ref.obj,kTeamAt+8))return Life::invalid;
    if(At<const void*>(ref.obj,kSelfCtrl)!=ref.ctrl || (At<unsigned char>(ref.obj,kFlagsAt)&4))return Life::gone;
    return Life::live;
}
bool PvpCollect301(void*,const ObjRef& root,ObjRef* members,unsigned capacity,unsigned* count,
                   controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);
    if(count)*count=0;if(emitted)*emitted=0;
    if(!members || !count || !emitters || !emitted || !capacity || PvpIdentity301(root)!=controlled_pvp::Life::live)return false;
    unsigned used=0,emission=0;
    void* selected=const_cast<void*>(root.obj);const Hull* h=HullOf(selected);void* hull=selected;
    if(!h) {const Part* p=PartOf(selected);if(!p)return false;hull=p->hull;h=HullOf(hull);}
    const auto hullWeak=refs.find(hull);
    if(!h || !h->built || hullWeak==refs.end())return false;
    const ObjRef hullRef{hull,hullWeak->second.control};
    if(PvpIdentity301(hullRef)!=controlled_pvp::Life::live || At<const void*>(hull,0)!=&hullClass.block[1])return false;
    const auto add=[&](const ObjRef& ref) {
        for(unsigned i=0;i<used;++i)if(members[i].obj==ref.obj)return members[i].ctrl==ref.ctrl;
        if(used>=capacity || PvpIdentity301(ref)!=controlled_pvp::Life::live)return false;
        members[used++]=ref;return true;
    };
    if(!add(root) || !add(hullRef))return false;
    for(void* object:h->parts) {
        const Part* p=PartOf(object);
        // The only removal paths erase the typed record on destruction/replacement. Never follow the old raw address.
        if(!p || p->hull!=hull || p->gone)continue;
        const auto weak=refs.find(object);if(weak==refs.end())return false;
        const ObjRef ref{object,weak->second.control};
        const auto life=PvpIdentity301(ref);if(life==controlled_pvp::Life::gone)continue;
        if(life!=controlled_pvp::Life::live || At<const void*>(object,0)!=&partClass.block[1])return false;
        if(p->team<-1 || p->team>=crew::team_native::kTeamCapacity || !add(ref) || emission>=emitterCapacity)return false;
        emitters[emission++]={controlled_pvp::EmitterKind::ownerTeamMirror,ref,&p->team,p->pvpIncarnation};
    }
    // Native +550 children are a separate proven container. Any live descendant outside our typed family is unsupported.
    ObjRef native[controlled_pvp::kMaxMembers]{};
    for(unsigned i=0;i<used;++i) {
        unsigned total=0;if(!enemy_pvp_native::CollectTree(members[i],native,controlled_pvp::kMaxMembers,&total))return false;
        for(unsigned n=0;n<total;++n) {bool known=false;for(unsigned j=0;j<used;++j)known=known ||
            (members[j].obj==native[n].obj && members[j].ctrl==native[n].ctrl);if(!known)return false;}
    }
    *count=used;*emitted=emission;return true;
}
controlled_pvp::Life PvpRead301(void*,const controlled_pvp::Emitter& emitter,int* team) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);using controlled_pvp::Life;
    if(!team || emitter.kind!=controlled_pvp::EmitterKind::ownerTeamMirror || !emitter.object || !emitter.incarnation)return Life::invalid;
    const auto identity=PvpIdentity301(emitter.member);if(identity!=Life::live)return identity;
    const Part* p=PartOf(const_cast<void*>(emitter.member.obj));
    if(!p)return Life::invalid;
    if(p->pvpIncarnation!=emitter.incarnation)return Life::gone;
    if(emitter.object!=&p->team || At<const void*>(emitter.member.obj,0)!=&partClass.block[1])return Life::invalid;
    const auto it=refs.find(const_cast<void*>(emitter.member.obj));
    if(it==refs.end())return Life::invalid;
    if(it->second.object!=emitter.member.obj || it->second.control!=emitter.member.ctrl)return Life::invalid;
    if(p->team<-1 || p->team>=crew::team_native::kTeamCapacity)return Life::invalid;
    *team=p->team;return Life::live;
}
bool PvpWrite301(void*,const controlled_pvp::Emitter& emitter,int team) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);int previous=0;
    if(team<-1 || team>=crew::team_native::kTeamCapacity || PvpRead301(nullptr,emitter,&previous)!=controlled_pvp::Life::live)return false;
    Part* p=PartOf(const_cast<void*>(emitter.member.obj));p->team=team;return p->team==team;
}
bool PvpMemberBaseline301(void*,const ObjRef& root,const ObjRef& member,int,int,int* team,int* last) noexcept {
    // SpawnParts is constructor-only (built is permanent). An unexpected new owner has no proven baseline; refuse it.
    return controlled_pvp::ReadBaseline(root,member,team,last);
}
bool PvpEmitterBaseline301(void*,const controlled_pvp::Emitter&,int,int,int*) noexcept {
    // There is no in-place emitter rebuild in a 301 Part. A new incarnation is a new native owner, never an inherited guess.
    return false;
}
bool ControlPvpHooks301(const void* object,controlled_pvp::Hooks* out) noexcept {
    if(!out || !ControlMatch301(object))return false;
    *out={nullptr,&PvpCollect301,&PvpRead301,&PvpWrite301,&PvpMemberBaseline301,&PvpEmitterBaseline301};return true;
}
bool ResolveMother301Spawn(const wchar_t* resource,controlled_pvp::SpawnProof* out) noexcept {
    if(!resource || !out || _wcsicmp(resource,L"app:/Object/UfoMother301.Sgo")!=0)return false;
    // The shipped port's real SGO class is UfoMother301: 4 hatches, 4 covers, 10 cannons, 1 main, 125 panels.
    // Cfg only removes these constructor groups. The actual newborn class/full tree is checked again by PvpCollect301.
    const int mask=Cfg().edf41MotherParts;unsigned partCount=0;
    if(mask&1)partCount+=4;if(mask&2)partCount+=4;if(mask&4)partCount+=10;if(mask&8)partCount+=1;if(mask&16)partCount+=125;
    *out={};out->hooks={nullptr,&PvpCollect301,&PvpRead301,&PvpWrite301,&PvpMemberBaseline301,&PvpEmitterBaseline301};
    out->maxMembers=partCount+1;out->maxEmitters=partCount;return true;
}
}  // namespace
const EnemyControlAdapter& Edf41Mother301ControlAdapter() noexcept {
    static const EnemyControlAdapter adapter{"EDF4.1 母舰",&ControlMatch301,&ControlAbilities301,&ControlHash301,&ControlBegin301,&ControlApply301,&ControlRestore301,&ControlNetwork301,&ControlPvpHooks301};return adapter;
}

bool InstallEdf41Mother() noexcept {
    if(!Cfg().edf41Mother) {
        Log("EDF41MOTHER off (ini Edf41Mother=0)");
        return false;
    }
    if(!BuildClone(hullClass,&HullDtor,&HullUpdate,&HullMessage) || !BuildClone(partClass,&PartDtor,&PartUpdate,&PartMessage)) {
        Log("EDF41MOTHER the vtable clones could not be built: off");
        return false;
    }
    SetSlot(hullClass,kSlotAccept,reinterpret_cast<void*>(&HullAccept));
    SetSlot(hullClass,7,reinterpret_cast<void*>(&HullThinkControl));
    if(MotherPrims()) {   // 4.1's hit, death and glow (else the carrier's own hit and death stay)
        SetSlot(hullClass,kSlotDamaged,reinterpret_cast<void*>(&HullDamaged));
        SetSlot(hullClass,kSlotDraw,reinterpret_cast<void*>(&HullDraw));
        SetSlot(partClass,kSlotDraw,reinterpret_cast<void*>(&PartDraw));
    }
    AllowAnyTeam(partClass);   // the parts take the hull's team (EventAnimationObject's slot 22 takes team 6 only)
    AtGameShutdown(&ShutdownVents);   // the vents' emitters deleted and the loops stopped while the managers / sound system still exist
    const bool ok=AddFactory(L"UfoMother301",&CreateHull) && AddFactory(L"UfoMother301_Hatch",&CreatePart)
        && AddFactory(L"UfoMother301_Panel",&CreatePart) && AddFactory(L"UfoMother301_Cannon",&CreatePart)
        && AddFactory(L"UfoMother301_BigCannon",&CreatePart);
    const bool spawnProof=ok && RegisterControlledPvpSpawnResolver(&ResolveMother301Spawn);
    Log("EDF41MOTHER controlled spawn resource proof %s",spawnProof ? "registered" : "REFUSED");
    Log("EDF41MOTHER on: UfoMother301 and its four part classes %s (dev: Edf41MotherParts=%d, Edf41MotherForce=%d)",
        ok ? "queued for the game's registration" : "NOT registered",Cfg().edf41MotherParts,Cfg().edf41MotherForce);
    return ok;
}
}  // namespace crew
