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
    float radiusFactor=-1.0f;     // +0xC4 collision radius factor (4.1 +0x80); < 0: the default
    float speedFactor=-1.0f;      // +0xC8 (4.1 +0x84); < 0: the default
    const SgoNode* custom=nullptr;// +0xE8 (tag +0xF8 = 1); must outlive the bullet (Persist)
    // (added for UfoMother301) the hit sound: InitParam +0x100 is a SePreset (the constructor 0x1001D0 inits it with 0x7B3200;
    // a weapon's key 14 fills it by SEPRESET name, 0x6CEF81), played where the bullet hits. 4.1 fills its +0xB0 the same way.
    // `hitSe` is a 4.1 preset name looked up in `hitSeTable`'s edf41_se (as PlaySe); null: none.
    const void* hitSeTable=nullptr;
    const wchar_t* hitSe=nullptr;
};
// One bullet along the matrix's +Z from its position, owned by `owner` (its team and weak pointer); false when the
// factory is unknown.
bool Fire(void* owner,const float* matrix,const Bullet& b) noexcept;

// ---- the player -------------------------------------------------------------------------------------------------
// The position of the nearest live player human to `from` (offline: the local one); false when none is seen.
bool NearestPlayer(const float* from,float* out) noexcept;
// Development only (ini Edf41DevShoot = n frames, 0 off; Edf41DevShootDamage per bullet): the autopilot cannot fire, so each
// class calls this every frame for each object a player would shoot at (`point`: where); every n-th call per object one
// LaserBullet02 owned by the local player flies at it from 20 m short of it, so the hit goes the game's own way (the damage
// message, the class's filter, its hit sound, the kill and drop credit to the player). `salt` staggers several points on
// one object. No effect with the switch off.
void DevShoot(const void* object,const float* point,unsigned salt=0) noexcept;

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
// (FillSePreset: below, shared by UfoMother301's Bullet::hitSe and UfoRobo's FireSe.)
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
// SetTeam (0x54EE70) first asks slot 22 (+0xB0) whether the object may take the team; EventAnimationObject's (0x5B6BA0) says yes
// to team 6 only, so a clone of it never becomes an enemy. This points the clone's slot 22 at GameObjectBase's (0x54ABD0, true).
void AllowAnyTeam(Clone& c) noexcept;

// ---- CAS player, the world, lock points, more effects, area damage (added for UfoRobo, src/edf41robo.cpp) ------------------
// The main player (EventAnimationObject / UfoCarrier508 layout: players *(object+0x8F0+0x60), 0x160 each, index [+0xC80]).
int StateIndex(void* object,const wchar_t* state) noexcept;                 // 0x11664F0(cas, player, name, 1); -1: none
bool PlayStateIndex(void* object,int state) noexcept;                       // 0x116BFC0(cas, player, state, 0)
bool ClipNow(const void* object,int* state,float* time,float* length) noexcept;   // +0x104 state, +0xE8 time, +0xF0 length
// The main player's clip back to its start (+0xE8 = 0): 0x116BFC0 asked for the state already playing may keep its clock.
bool ClipRewind(void* object) noexcept;
// The nearest map hit (terrain / buildings: 0x11A7EE0 with the ground collector 0x179CBE8, filter 0x16, as heli.cpp) on from -> to.
// `skipRagdoll` (a ragdoll component, EAO +0xAA0): hits on its own bodies are passed.
bool GroundHit(const float* from,const float* to,float* hit,void* skipRagdoll=nullptr) noexcept;
// The game's own camera (scene system +0x4D8 slots, wrapper +0x70: edf41mother.cpp GameCamera): eye and unit forward.
bool CameraNow(float* eye,float* forward) noexcept;
// A lock point of the object's own (AddLockTarget 0x547A50(object): the object keeps it in its list +0x360; the lock search
// takes it while the object is hostile). EventAnimationObject registers none; the caller keeps its position (+0x10) current.
void* AddLockPoint(void* object) noexcept;
void SetLockPoint(void* lockPoint,const float* pos) noexcept;
// More EffectGenUtil effects: ExplosionWind 0x2E4D90 (煙_01: dust / smoke), the camera quake 0x2E9B30(egu, pos, strength, radius, 1).
void Smoke(const float* pos,float size) noexcept;
void Quake(const float* pos,float strength,float radius) noexcept;
// Area damage of `owner` (its team and weak pointer) at `pos`: a GrenadeBullet01 with custom parameter #0 = 1 (it explodes when
// its life ends, docs/decoy-blast-re.md 1.4), still, 1 frame of life, blast radius `radius` (InitParam +0xB0), `damage`. Its
// explosion is drawn too.
bool Blast(void* owner,const float* pos,float radius,float damage) noexcept;
// The skeleton rebuilt from `matrix` (0x1100B90 on EventAnimationObject's +0x660): called after the stock Update it draws the
// model there instead of at +0x60 (scaled, or hidden at a tiny scale); the ragdoll stays where the stock Update put it.
bool ModelMatrix(void* object,const float* matrix) noexcept;
// Far rendering on for an EventAnimationObject's model node (+0x5C0; 0x11B3020, the switch of the SGO's FarRender): past 1000 m
// from the camera the near pass draws nothing. True once set (or set already); false: ask again next frame.
bool FarRender(void* object) noexcept;

// Weak references through the game's shared_ptr control block (use +8, weak +0xC, vtable {destroy, delete}).
struct WeakRef { void* object=nullptr; void* control=nullptr; };
void WeakSet(WeakRef& w,const void* object) noexcept;          // from the object's own weak pointer (+0x28 / +0x30)
void WeakReset(WeakRef& w) noexcept;
bool WeakAlive(const WeakRef& w) noexcept;                     // use count > 0 and not deleted
// The object's weak pointer written to `out` ({object, control}: what GetPartsObject's message 0x10000017 returns at +8), one
// weak count taken for the receiver.
bool WeakOut(const void* object,void* out) noexcept;

// ---- UfoRobo, phase 3 (src/edf41robo.cpp) ------------------------------------------------------------------------------------
// Fire, the bullet's hit sound set: EDF6's bullet InitParam keeps one SePreset at +0x100 (its constructor 0x1001D0 initialises it
// with 0x7B3200; 4.1's InitParam has it at +0xB0, and UfoRobo's 0x275730 fills it per gun with ロボビーム着弾小 / 中 / 大). `hitSe` is a
// 4.1 SEPRESET name looked up in `seTable`'s edf41_se and filled by cue as PlayCue does; nothing found: Fire's silent preset.
bool FireSe(void* owner,const float* matrix,const Bullet& b,const void* seTable,const wchar_t* hitSe) noexcept;
// (UfoMother301 and UfoRobo) `preset` (0x80 bytes, anywhere; a bullet's +0x100 too: initialising it again is harmless) initialised as EDF6's SePreset constructor does (0x7B3200) and filled from 4.1's preset `name` in
// `table`'s edf41_se (cue, volume, pitch, distances), as FireSe does for a bullet; false (the preset still initialised): no such name.
bool FillSePreset(unsigned char* preset,const void* table,const wchar_t* name) noexcept;

}  // namespace crew::edf41c
