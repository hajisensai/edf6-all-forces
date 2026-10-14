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
constexpr int kSlotDtor=0,kSlotUpdate=5,kSlotMessage=9,kSlotAcceptDamage=16,kSlotDamaged=18;
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
// Another slot of a built clone pointed at `fn` (EventAnimationObject: 16 bool Accept(obj, GameDamageInfo*), the filter ApplyDamage
// 0x547C30 asks before the HP changes; 18 void OnDamaged(obj, GameDamageInfo*), after). False when the slot is out of range.
bool SetSlot(Clone& c,int slot,void* fn) noexcept;
// new(size), ctor(obj, createInfo), then the vptr pointed at the clone; null (and logged) when the class made something else.
void* CloneObject(Clone& c,std::size_t ctor,std::size_t size,void* createInfo) noexcept;

// The two stock classes the ports build on.
constexpr std::size_t kEaoCtor=0x5B5FB0,kEaoVtable=0x17D4D20,kEaoSize=0xC90;           // EventAnimationObject: model, CAS "main", ragdoll
constexpr std::size_t kCarrierCtor=0x4F8690,kCarrierVtable=0x17C9DC0,kCarrierSize=0x1630;   // UfoCarrier508
extern const std::uint32_t kEaoSlots[45];
extern const std::uint32_t kCarrierSlots[49];

// ---- object fields (GameObjectBase) ------------------------------------------------------------------------------
// (jobs/14618f05/tmp/re6/PRIMITIVES.md: +0x2EC the SGO durability, +0x2F0 the floor (the script's SetObjectDurability 3020,
// 0x2288C0, writes it as a percentage of the max), +0x2F4 the max (ApplyDamage clamps to [+0x2F0, +0x2F4]; +0x2FC is an int 0),
// +0x380 flags (bit 0 no HP loss: EventAnimationObject's constructor sets it; 0x80 not a target; 0x2000 not counted as an
// enemy; 0x800 only forced damage), +0x498 raw level, +0x49C level (10 = level 1).)
constexpr std::size_t kMatrixAt=0x60,kPosAt=0x90,kSgoAt=0xA0,kFlagsAt=0x18,kDestroyedAt=0x2E8,kDurabilityAt=0x2F8,
                      kMaxDurabilityAt=0x2F4,kMinDurabilityAt=0x2F0,kBaseDurabilityAt=0x2EC,kTeamAt=0x314,kWeakSelfAt=0x28,
                      kObjFlagsAt=0x380,kRawLevelAt=0x498,kLevelAt=0x49C;
constexpr std::uint32_t kObjNoHpLoss=1u,kObjNotTarget=0x80u,kObjNotCounted=0x2000u;
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
    float radiusFactor=-1.0f;     // +0xC4 collision radius factor (4.1 +0x80); < 0: the default
    float speedFactor=-1.0f;      // +0xC8 (4.1 +0x84); < 0: the default
    const SgoNode* custom=nullptr;// +0xE8 (tag +0xF8 = 1); must outlive the bullet (Persist)
};
// One bullet along the matrix's +Z from its position, owned by `owner` (its team and weak pointer); false when the
// factory is unknown.
bool Fire(void* owner,const float* matrix,const Bullet& b) noexcept;

// ---- game objects (PRIMITIVES.md 3, 6, 7) ---------------------------------------------------------------------------
bool SetTeam(void* object,int team,bool registered) noexcept;   // 0x54EE70: +0x314, the team's set (hostility, enemy count)
bool SetLevel(void* object,float level) noexcept;               // 0x54E740: raw level x level_adjust -> +0x498, +0x49C
bool Activate(void* object) noexcept;                           // 0x548D50: what the script's "active" flag does
float DurabilityFactor(float x) noexcept;                       // 0xD7720: x x difficulty durability x online players (x when unknown)
// The object's current target (GameObjectBase keeps the nearest live hostile, refreshed every 60 frames; 0x30C0F0) and its aim point
// (the target's first lock point, else its origin). Null when none.
void* Target(void* object,float* aim) noexcept;
// The ragdoll body (index) the damage info's sub-hit nearest its centre struck (0x6EA430 on the body id); -1 when none. `ragdoll`
// is the object's ragdoll component (EventAnimationObject +0xAA0).
int HitBody(void* ragdoll,const void* damageInfo) noexcept;
int BodyByName(void* ragdoll,const wchar_t* name) noexcept;     // 0x6EA670, -1 when absent
// The skeleton rebuilt from `matrix` (0x1100B90 on EventAnimationObject's +0x660): after the stock Update, it draws the model
// there instead of at +0x60 (a scaled or hidden model; the ragdoll stays where the stock Update put it).
bool ModelMatrix(void* object,const float* matrix) noexcept;
// Far rendering on for an EventAnimationObject's model node (+0x5C0; 0x11B3020, the SGO FarRender switch): past 1000 m from the
// camera the near pass no longer draws it. True once set (or set already); false: try again next frame.
bool FarRender(void* object) noexcept;

// ---- weak references (the game's shared_ptr control block: use +8, weak +0xC, vtable {destroy, delete}) --------------
struct WeakRef { void* object=nullptr; void* control=nullptr; };
void WeakSet(WeakRef& w,const void* object) noexcept;    // from the object's own weak pointer (+0x28 / +0x30); empty on failure
void WeakReset(WeakRef& w) noexcept;
bool WeakAlive(const WeakRef& w) noexcept;               // use count > 0 and not deleted
// Writes the object's weak pointer to `out` ({object, control}, as GetPartsObject's message wants it), one weak count taken.
bool WeakOut(const void* object,void* out) noexcept;

// ---- sounds (PRIMITIVES.md 4) ------------------------------------------------------------------------------------
// A cue of a loaded ACB by its name (the bank an object's game_sound loads; 4.1's own banks through edf41sound.cpp), at `pos`,
// 4.1's preset numbers (volume, pitch, the inner and reference distances). With `loop`, the handle to move and stop it.
struct SeHandle { void* control=nullptr; void* instance=nullptr; };

bool PlayCue(const wchar_t* cue,const float* pos,SeHandle* handle,float volume=1.0f,float pitch=1.0f,float inner=1.0f,float reach=1000.0f) noexcept;
void MoveSe(SeHandle& h,const float* pos) noexcept;
void StopSe(SeHandle& h,int fadeFrames) noexcept;        // stops and releases

// ---- effects (EffectGenUtil, PRIMITIVES.md 5; visual only) ----------------------------------------------------------
void EffectFlame(const float* pos,const float* dir,float size) noexcept;          // 0x2E7FF0 (4.1 0x1b16b0)
void EffectBlazing(const float* pos,const float* dir,float size) noexcept;        // 0x2E0B70 (4.1 0x1b1ba0, smoke + fire)
void EffectExplosion(const float* pos,const float* dir,float size) noexcept;      // 0x2E5ED0 (4.1 0x1b2070)
void EffectExplosionBig(const float* pos,const float* dir,float size,float a) noexcept;   // 0x2E7A30 (4.1 0x1b2ab0)

// ---- the player -------------------------------------------------------------------------------------------------
// The position of the nearest live player human to `from` (offline: the local one); false when none is seen.
bool NearestPlayer(const float* from,float* out) noexcept;

}  // namespace crew::edf41c
