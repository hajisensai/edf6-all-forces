// EDF4.1 classes EDF6 no longer has, as the plugin's own scene object classes (src/edf41classes.cpp): what UfoMother301
// (edf41mother.cpp), the 401 mothership and UfoRobo share.
//
//  - Factories. AddFactory(name, create) puts a class in the game's own factory table (RegisterSceneObjectFactory, right after
//    the game registers its own classes at start-up): an SGO whose xgs_scene_object_class is `name` is then made by `create`,
//    which builds the object (on a stock class's constructor, CloneObject) and leaves it to the game as a stock creation does.
//    Call it from an Install* function (before the game's start-up registration runs).
//  - Cloned vtables. A stock class's vtable copied (its RTTI pointer too), slots 0 (deleting destructor), 5 (Update(dt)) and 9
//    (OnMessage(msg, data)) replaced, every other slot a counting stub to the stock function as the file has it (not the live
//    table, which other hooks may have patched).
//  - SGO nodes of the object's own SGO (object+0xA0; EDF.dll 0x528A0), child objects made from an SGO path (0x11945E0), CAS
//    states, matrices, and bullets (0x1194280 with an InitParam built by the game's own constructor 0x1001D0).
// Every EDF.dll function used is checked against its first bytes before anything is hooked or called (Ready()).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace crew::edf41c {

// ---- factories --------------------------------------------------------------------------------------------------
using CreateFn=void(*)(void* createInfo) noexcept;
// false: the table is full, the fingerprints failed, or the game already registered its classes (too late).
bool AddFactory(const wchar_t* className,CreateFn create) noexcept;
bool Ready() noexcept;     // the fingerprints matched and the registration hook is in

// ---- cloned vtables ---------------------------------------------------------------------------------------------
constexpr int kSlotDtor=0,kSlotUpdate=5,kSlotMessage=9;
struct Clone {
    const char* tag;
    std::size_t vtable;            // the stock class's primary vtable (EDF.dll RVA)
    int slots;
    const std::uint32_t* rvas;     // its slots as the file has them
    void* orig[64];
    void* block[65];               // [0] RTTI, then the slots: the objects' vptr is &block[1]
    std::atomic<std::uint32_t> calls[64];
};
using DtorFn=std::uint64_t(__fastcall*)(void*,unsigned);
using UpdateFn=std::uint64_t(__fastcall*)(void*,void*);
using MessageFn=std::uint64_t(__fastcall*)(void*,std::uint32_t,void*);
bool BuildClone(Clone& c,DtorFn dtor,UpdateFn update,MessageFn message) noexcept;
// new(size), ctor(obj, createInfo), then the vptr pointed at the clone; null (and logged) when the class made something else.
void* CloneObject(Clone& c,std::size_t ctor,std::size_t size,void* createInfo) noexcept;

// The two stock classes the ports build on.
constexpr std::size_t kEaoCtor=0x5B5FB0,kEaoVtable=0x17D4D20,kEaoSize=0xC90;           // EventAnimationObject: model, CAS "main", ragdoll
constexpr std::size_t kCarrierCtor=0x4F8690,kCarrierVtable=0x17C9DC0,kCarrierSize=0x1630;   // UfoCarrier508
extern const std::uint32_t kEaoSlots[45];
extern const std::uint32_t kCarrierSlots[49];

// ---- object fields (GameObjectBase) ------------------------------------------------------------------------------
constexpr std::size_t kMatrixAt=0x60,kPosAt=0x90,kSgoAt=0xA0,kFlagsAt=0x18,kDestroyedAt=0x2E8,kDurabilityAt=0x2F8,
                      kMaxDurabilityAt=0x2F4,kBaseDurabilityAt=0x2EC,kTeamAt=0x314,kWeakSelfAt=0x28;
constexpr std::uint32_t kMsgDamage=0xF0000000u,kMsgAction=0x1000000Du,kMsgParts=0x10000017u,kMsgGenerator=0x20100u;

// ---- SGO --------------------------------------------------------------------------------------------------------
// A node is 12 bytes {type, a, b}: 0 array (a children at node + b), 1 int and 2 float (value at node + 8), 3 UTF-16 string
// (a characters at node + b, not NUL-ended).
struct SgoNode { std::int32_t type; std::int32_t a; std::uint32_t b; };
const SgoNode* Key(const void* object,const wchar_t* key) noexcept;      // the object's own SGO; null when absent
const SgoNode* Child(const SgoNode* array,int i) noexcept;
int Count(const SgoNode* array) noexcept;                                // -1: not an array
bool Float(const SgoNode* n,float* out) noexcept;                         // an int node converts
bool Int(const SgoNode* n,std::int32_t* out) noexcept;                    // a float node truncates
bool Vec(const SgoNode* n,float* out,int count) noexcept;
bool Text(const SgoNode* n,wchar_t* out,std::size_t capacity) noexcept;
// A copy of the subtree (numbers, strings and arrays) that lives as long as the process: a bullet keeps a pointer to its
// custom parameter, so the one handed to Fire must outlive it.
const SgoNode* Persist(const SgoNode* n) noexcept;

// ---- objects ----------------------------------------------------------------------------------------------------
void* CreateFromPath(const float* matrix,const wchar_t* sgoPath) noexcept;   // null: nothing made
void DeleteObject(void* object) noexcept;                                    // what the script's DeleteObject does
bool Alive(const void* object) noexcept;                                     // not deleted (flag 4 of +0x18), durability > 0
// The CAS state `state` on the object's main player; offsets of EventAnimationObject / UfoCarrier508 (+0x8F0, [+0xC80]).
bool PlayState(void* object,const wchar_t* state) noexcept;

// ---- matrices (row-major, row vectors: rows x, y, z, position) ----------------------------------------------------
void Multiply(const float* a,const float* b,float* out) noexcept;                 // out = a * b
void EulerMatrix(const float* degrees,const float* position,float* out) noexcept; // Rx * Ry * Rz (4.1's order), position last
void AimMatrix(const float* from,const float* dir,float* out) noexcept;           // +Z along dir, +Y up as far as it can
float Random01(std::uint64_t& state) noexcept;

// ---- bullets ----------------------------------------------------------------------------------------------------
struct Bullet {
    const wchar_t* factory=L"";   // EDF6 bullet class (LaserBullet02, RocketBullet01, PlasmaBullet01, BeamBullet01 ...)
    float speed=1.0f;             // InitParam +0x94, per frame
    std::int32_t life=60;         // +0x98, frames
    float damage=1.0f;            // +0x9C
    float size=1.0f;              // +0xC0
    float hitStop=0.0f;           // +0xCC
    float color[4]={1.0f,1.0f,1.0f,1.0f};   // +0xD0
    float explosion=0.0f;         // +0xB0 explosion radius (0: none; 4.1 +0x70, the bullet core init 0x231CC0 reads it)
    float gravity=0.0f;           // +0xE0 (4.1 +0xA0)
    const SgoNode* custom=nullptr;// +0xE8 (tag +0xF8 = 1); must outlive the bullet (Persist)
};
// One bullet along the matrix's +Z from its position, owned by `owner` (its team and weak pointer); false when the
// factory is unknown.
bool Fire(void* owner,const float* matrix,const Bullet& b) noexcept;

// ---- the player -------------------------------------------------------------------------------------------------
// The position of the nearest live player human to `from` (offline: the local one); false when none is seen.
bool NearestPlayer(const float* from,float* out) noexcept;

// ---- EDF6 primitives (docs/edf41-classes.md section 2, read from EDF.dll; H unless noted) -----------------------------------
// Object bases: EventAnimationObject model +0x5C0, skeleton +0x660, ragdoll +0xAA0; UfoCarrier508 model +0x650, skeleton +0x6F0,
// ragdoll +0xB60.
constexpr std::size_t kEaoModel=0x5C0,kEaoSkeleton=0x660,kEaoRagdoll=0xAA0;
constexpr std::size_t kCarrierModel=0x650,kCarrierSkeleton=0x6F0,kCarrierRagdoll=0xB60;
constexpr std::size_t kFlags380=0x380,kRawLevelAt=0x498,kLevelAt=0x49C;
// +0x380 flags: bit 0 no HP loss (EventAnimationObject's ctor sets it), 0x80 never a target, 0x800 invulnerable (slots 16 / 17
// skipped, only forced damage), 0x2000 not counted as an enemy.
constexpr std::uint32_t kNoHpLoss=1u,kNotTarget=0x80u,kInvulnerable=0x800u,kNotCounted=0x2000u;
void SetFlag(void* object,std::uint32_t bits,bool on) noexcept;

// MAB locator: found once by name on a model component (0x6BADD0), evaluated each frame (0x6BB5A0: locator x bone world).
struct LocatorHandle { const void* entry; void* bone; };
bool FindLocator(void* model,const wchar_t* name,LocatorHandle* out) noexcept;
bool LocatorMatrix(const LocatorHandle& h,float* out) noexcept;
// A bone's world matrix (record +0xB0) by name on a skeleton (model+0xA0); fresh after the stock Update. -1 / false: none.
int BoneIndex(void* skeleton,const wchar_t* name) noexcept;
bool BoneMatrix(void* skeleton,int index,float* out) noexcept;

// Team (0x54EE70 SetTeam(obj, team, register)); level (0x54E740 SetLevel(obj, level): raw level x level_adjust -> +0x498,
// difficulty-scaled -> +0x49C, s = +0x49C / 10) and durability the way UfoCarrier508 sizes itself (0x4F6D00: max =
// 0xD7720(game status, base x s), written to max and current).
bool SetTeam(void* object,std::int32_t team,bool registered) noexcept;
void SetLevel(void* object,float rawLevel) noexcept;
float LevelS(const void* object) noexcept;                     // +0x49C / 10 (4.1's s)
void SizeDurability(void* object) noexcept;                    // the carrier's formula on the object's own base durability
float Difficulty(float x) noexcept;                            // 0xD7720: x times the difficulty / online durability factor

// The stock target (GameObjectBase::Update retargets every 60 frames: the nearest live hostile registered object), read with
// 0x30C0F0; its aim point = its first lock point, else its +0x90.
void* Target(void* object) noexcept;
bool AimPoint(const void* target,float* out) noexcept;

// The ragdoll body (index) a damage message hit: the sub-hit nearest the hit centre (GDI +0x78 records of 0x30, body id
// +0x20), mapped by 0x6EA430; and a body's index by name (0x6EA670). -1: none.
int HitBody(void* ragdoll,const void* damageInfo) noexcept;
int RagdollBody(void* ragdoll,const wchar_t* name) noexcept;
// GDI (the data of 0xF0000000): +0x30 hit centre, +0x50 damage, +0x60 u16 flags (0x40 forced).
constexpr std::size_t kGdiCentre=0x30,kGdiDamage=0x50,kGdiFlags=0x60;

// Sounds: a raw ACB cue by name (the lookup 0x7AEAE0 over every loaded bank, a SePreset filled as FireSe 0x7B3EA0 does) played
// once at a point, or kept in `handle` to follow (0x7A8C20) and stop (0x7A8CD0).
struct SoundHandle { void* control=nullptr; void* instance=nullptr; };
// The preset's numbers (SePreset +0x74 volume, +0x70 pitch, +0x10 / +0x14 / +0x18 the three distance values; defaults 0x7B3200's).
struct SeParams { float volume=1.0f,pitch=1.0f,inner=1.0f,reference=1000.0f,cull=4.0f; };
bool PlayCue(const wchar_t* cue,const float* pos,SoundHandle* handle=nullptr,const SeParams& params=SeParams{}) noexcept;
// 4.1's sound by its SEPRESET name (母船UFOパネルキャノン ...): EDF6's SEPRESET lacks 4.1's names, so tools/make_edf41_objects.py
// copies 4.1's entries into the object's SGO key edf41_se ([[name, cue, volume, pitch, a, b, c], ...]); `table` is the object
// whose SGO holds it (a part plays from its hull's table).
bool PlaySe(const void* table,const wchar_t* name,const float* pos,SoundHandle* handle=nullptr) noexcept;
void MoveSound(SoundHandle& handle,const float* pos) noexcept;
void StopSound(SoundHandle& handle,int fadeFrames=4) noexcept;

// Visual effects (EffectGenUtil *(EDF+0x20B2980); no damage, no sound): 4.1 0x1b16b0 fire burst -> Flame 0x2E7FF0, 0x1b1ba0
// smoke and fire -> Blazing 0x2E0B70, 0x1b2070 directed explosion -> Explosion 0x2E5ED0, 0x1b2ab0 big -> Explosion_Big 0x2E7A30
// (the look of each: M).
void FireBurst(const float* pos,const float* dir,float size) noexcept;
void Blazing(const float* pos,const float* dir,float size) noexcept;
void Explosion(const float* pos,const float* dir,float size) noexcept;
void BigExplosion(const float* pos,const float* dir,float size,float k) noexcept;

// Drop the object's game_object_drop_item once (0x548AE0); for an object that dies outside ApplyDamage.
void DropItems(void* object) noexcept;
// Activate an object (0x548D50, what CreateEnemy's `active` argument and EDF6's own generator 0x6E3381 call; sets +0x4A0).
void Activate(void* object) noexcept;
// The main CAS player of an EventAnimationObject (players *(+0x8F0+0x60), 0x160 each, index [+0xC80]): its clip has played to
// the end (time +0xE8 >= length +0xF0); `length` receives the clip length in frames when not null.
bool AnimDone(const void* object,float* length=nullptr) noexcept;

// Replace one more slot of a built clone (slot 16 Accept(obj, GDI*) -> bool: the per-body damage filter; slot 6 OnLevel).
void SetSlot(Clone& c,int slot,void* fn) noexcept;

}  // namespace crew::edf41c
