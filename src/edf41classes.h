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
                      kMaxDurabilityAt=0x2FC,kTeamAt=0x314,kWeakSelfAt=0x28;
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

}  // namespace crew::edf41c
