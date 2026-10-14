// EDF4.1's UfoMother301 (the mothership) as a class of its own, prototype (docs/edf41-ufomother301.md, phase 1).
// Development only: nothing here runs unless the ini says Edf41MotherDev=1 (tools/edf41_objects.py sets it when the
// installer was run with EDF41_MOTHER_DEV=1 and edf41port_dev/ exists; a release build never has either).
//
// What this settles (all of it logged as EDF41MOTHER):
//  1. Factory registration. The game creates a scene object by the SGO's xgs_scene_object_class: EDF.dll 0x1194410
//     (create from an SGO tree) reads the name (0x119ADE0), looks the factory up (GetSceneObjectFactory 0x1195C50: an
//     FNV-1a hash of the name's bytes into an unordered_map at manager+0x2C8; a miss logs "xgs::game::System::
//     GetSceneObjectFactory" and gives null) and calls the factory's vtable slot 2 (Create(factory, createInfo)). The
//     game's own classes are registered by 0x79D570, called from 0x701859 at start-up: per class a factory object
//     (8 bytes, only a vtable {dtor, name, create}), a shared_ptr control block, and 0x119AF00(manager, &{factory,
//     control}) = RegisterSceneObjectFactory (it asks the factory for its name and stores it in the map). We do the same
//     for "UfoMother301" right after 0x79D570 returns: the table is the game's own, no byte of it is patched.
//  2. A cloned vtable. The hull is built by UfoCarrier508's own constructor (0x4F8690, size 0x1630; the same class the
//     4.1 carriers already run on), so every field is initialised the way the class does it, and then its vtable
//     pointer (+0, the primary; the secondary at +0x120 is left alone) is pointed at a copy of the class's table
//     (0x17C9DC0, 49 slots, RTTI pointer copied so dynamic_cast still sees UfoCarrier508). In the copy, slots 0
//     (deleting destructor), 5 (Update(dt), the class's own, which calls GameObjectBase::Update 0x54BE40) and 9
//     (OnMessage(msg, data); the three-step SendMessage 0x1DA480 calls slots 10 / 9 / 11) are wrappers that log and
//     then call the class's own function; every other slot is a counting stub (lock inc, then jmp to the original),
//     so the log also says which slots the game really calls. Durability is the float at +0x2F8 (GameObjectBase,
//     0x545670 writes it), destroyed the byte at +0x2E8, the position the vec4 at +0x90. Damage arrives as message
//     0xF0000000 in slot 9 (the base handler 0x54A530 -> ApplyDamage 0x547C30); the script's ObjectAction (native 3200,
//     0x212A36) as message 0x1000000D with an int, GetPartsObject (native 3300, 0x222470) as 0x10000017 {grp, idx, out}.
//  3. Parts as real objects (phase 2, step 1). The hull's SGO lists its 144 parts (mp_HatchLow, mp_HatchMiddle, mp_Cannon, mp_BigCannon,
//     mp_Panel: [sgo path, euler degrees, position]). On its first Update the hull makes each one with the game's own "create a scene
//     object from an SGO path" (0x11945E0(manager, matrix, path, context): loads the SGO, asks the class factory, inserts the object
//     in the manager's list; the script natives CreateEnemy / CreateNeutral and UfoMother511's ring units go the same way). The part
//     classes UfoMother301_Hatch / _Panel / _Cannon / _BigCannon are registered like the hull: an EventAnimationObject (0x5B5FB0, size
//     0xC90: a model, a CAS player "main", a ragdoll, durability) with its vtable (0x17D4D20, 45 slots) cloned the same way. Their Update
//     puts the object matrix (+0x60) at local * hull matrix, then runs the class's own Update.
//  4. Nothing here dereferences what it has not checked: every address of EDF.dll we call is compared with the bytes
//     it had when this was written, every game pointer goes through Readable, and a failure is logged and the stock
//     path is left alone.
#include "crew.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crew {
namespace {
constexpr std::size_t kRegisterAllSite=0x701859;      // call 0x79D570 in the start-up routine
constexpr std::size_t kRegisterAll=0x79D570;          // registers the game's scene object factories
constexpr std::size_t kRegisterFactory=0x119AF00;     // RegisterSceneObjectFactory(manager, shared_ptr<Factory>*)
constexpr std::size_t kGetFactory=0x1195C50;          // GetSceneObjectFactory(manager, const wchar_t* name)
constexpr std::size_t kManagerGlobal=0x20B2958;       // the manager (xgs::game::System's scene object registry)
constexpr std::size_t kNew=0x12D85B0;                 // the game's operator new(size)
constexpr std::size_t kCarrierCtor=0x4F8690;          // UfoCarrier508::UfoCarrier508(this, createInfo)
constexpr std::size_t kCarrierVtable=0x17C9DC0;       // its primary vtable
constexpr std::size_t kCarrierSize=0x1630;
constexpr std::size_t kCreateFromPath=0x11945E0;      // Object* (manager, const float matrix[16], const wchar_t* sgoPath, CreateContext*)
constexpr std::size_t kSgoKey=0x528A0;                // node* (SgoRoot* = object+0xA0, const wchar_t* key); null when the key is absent
constexpr std::size_t kDeleteObject=0x118A1B0;        // marks a scene object deleted (bit 4 of +0x18); what the script's DeleteObject does
constexpr std::size_t kCreateContextVtable=0x1762068; // the 0x30-byte context the script natives and UfoMother511 hand to the creation
constexpr std::size_t kSetCasState=0x116C2D0;         // bool (CasPlayers* = object+0x8F0, int player = [object+0xC80], const wchar_t* state, int)
constexpr std::size_t kCasAt=0x8F0,kCasPlayerAt=0xC80;
constexpr std::size_t kSgoAt=0xA0,kMatrixAt=0x60,kFlagsAt=0x18;
constexpr std::size_t kPartCtor=0x5B5FB0;             // EventAnimationObject::EventAnimationObject(this, createInfo)
constexpr std::size_t kPartVtable=0x17D4D20;
constexpr std::size_t kPartSize=0xC90;
constexpr int kSlots=49,kPartSlots=45;
constexpr int kSlotDtor=0,kSlotUpdate=5,kSlotMessage=9;
// EventAnimationObject's vtable 0x17D4D20 as the file has it: the same slot meaning as UfoCarrier508's (0 dtor, 5 Update, 9 OnMessage).
constexpr std::uint32_t kPartStockSlots[kPartSlots]={
    0x5B6BB0, 0x5B64F0, 0x54BE10, 0x5B6D10, 0x5B6A10, 0x5B6D20, 0x125270, 0x54A000, 0x54A290, 0x5B6540, 0x54AA50, 0x54A970,
    0x266BE0, 0x54ABE0, 0x54C4B0, 0x54A330, 0x54AA40, 0x54AA30, 0x54A280, 0x54A510, 0x54A4B0, 0x54AAC0, 0x5B6BA0, 0x266BB0,
    0x266BC0, 0x266970, 0x54A320, 0x266BF0, 0x54A240, 0x54A260, 0x54A250, 0x54A2E0, 0x266BD0, 0x266960, 0x54F8D0, 0x54F8E0,
    0x54C800, 0x54CAA0, 0x54C900, 0x267180, 0x54CBE0, 0x54CBC0, 0x54CBD0, 0x54CB60, 0x54C6E0};
// UfoCarrier508's vtable 0x17C9DC0 as the file has it (slot -> EDF.dll RVA); 7 is the ship AI, 5 Update, 9 OnMessage.
constexpr std::uint32_t kStockSlots[kSlots]={
    0x54B010, 0x4F8A70, 0x54BE10, 0x4F9530, 0x4F72F0, 0x4F9540, 0x4F91C0, 0x4F6D70, 0x54A290, 0x4F93E0, 0x54AA50, 0x54A970,
    0x266BE0, 0x54ABE0, 0x54C4B0, 0x54A330, 0x4F7190, 0x54AA30, 0x4F9250, 0x54A510, 0x54A4B0, 0x54AAC0, 0x54ABD0, 0x266BB0,
    0x266BC0, 0x266970, 0x54A320, 0x266BF0, 0x54A240, 0x54A260, 0x54A250, 0x54A2E0, 0x266BD0, 0x266960, 0x54F8D0, 0x54F8E0,
    0x54C800, 0x54CAA0, 0x54C900, 0x267180, 0x54CBE0, 0x54CBC0, 0x54CBD0, 0x54CB60, 0x54C6E0, 0x4F37F0, 0x4F90A0, 0x4F3FD0,
    0x4F7D30};

constexpr std::size_t kDurabilityAt=0x2F8,kDestroyedAt=0x2E8,kPosAt=0x90;
constexpr std::uint32_t kMsgDamage=0xF0000000u,kMsgAction=0x1000000Du,kMsgParts=0x10000017u,kMsgGenerator=0x20100u;
constexpr std::uint32_t kUpdateLogEvery=600;          // frames between the state lines of an object
constexpr int kMaxMessageKinds=64;

struct Print { std::size_t rva; unsigned char bytes[8]; };
// The bytes of every EDF.dll function we call or replace, as they are in the build this plugin is for.
constexpr Print kPrints[]={
    {kRegisterAllSite,{0xE8,0x12,0xBD,0x09,0x00,0xE8,0xED,0xCF}},
    {kRegisterAll,{0x40,0x55,0x53,0x56,0x57,0x48,0x8D,0xAC}},
    {kRegisterFactory,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},
    {kNew,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kCarrierCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x4F8A70,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 0
    {0x4F9540,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x4F93E0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},   // slot 9
    {kCreateFromPath,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},
    {kSgoKey,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDeleteObject,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kSetCasState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kPartCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x5B6BB0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},   // part slot 0
    {0x5B6D20,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // part slot 5
    {0x5B6540,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57}},   // part slot 9
};

using NewFn=void*(__fastcall*)(std::size_t);
using CtorFn=void*(__fastcall*)(void*,void*);
using RegisterFn=void(__fastcall*)(void*,void*);
using GetFactoryFn=void*(__fastcall*)(void*,const wchar_t*);
using RegisterAllFn=std::uint64_t(__fastcall*)(void*,void*,void*,void*);
using DtorFn=std::uint64_t(__fastcall*)(void*,unsigned);
using UpdateFn=std::uint64_t(__fastcall*)(void*,void*);
using MessageFn=std::uint64_t(__fastcall*)(void*,std::uint32_t,void*);
using CreateFromPathFn=void*(__fastcall*)(void*,const float*,const wchar_t*,void*);
using SgoKeyFn=const void*(__fastcall*)(void*,const wchar_t*);
using DeleteObjectFn=void(__fastcall*)(void*);
using BulletSpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);

// A stock class whose vtable is cloned: the table as the file has it, the class's own functions, the clone the objects' vptr points
// at ([0] the RTTI pointer, then the slots), and a call counter per slot (the counting stubs).
struct StockClass {
    const char* tag;
    std::size_t vtable;
    int slots;
    const std::uint32_t* rvas;
    void* orig[64];
    void* block[65];
    std::atomic<std::uint32_t> calls[64];
    std::uint32_t seen[64];
};
StockClass hull{"UfoCarrier508",kCarrierVtable,kSlots,kStockSlots,{},{},{},{}};
StockClass partClass{"EventAnimationObject",kPartVtable,kPartSlots,kPartStockSlots,{},{},{},{}};
std::atomic<bool> installed{false},registered{false};
std::atomic<int> created{0};

// Said once per kind of failure per run (a per-frame check must not fill the log).
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

// ---- the factories ---------------------------------------------------------------------------------------------
// Five classes, one factory object each (8 bytes, only a vtable): the hull and the four part classes. A factory knows its index by
// its address in the array.
constexpr int kFactoryCount=5;
constexpr int kHullFactory=0,kHatchFactory=1,kPanelFactory=2,kCannonFactory=3,kBigCannonFactory=4;
const wchar_t* const kClassNames[kFactoryCount]={L"UfoMother301",L"UfoMother301_Hatch",L"UfoMother301_Panel",L"UfoMother301_Cannon",L"UfoMother301_BigCannon"};
struct Factory { const void* const* vtable; };
// The shared_ptr control block the game's own registrations make (0x79D5C0): vtable, use count at +8, weak count at +0xC, object at +0x10.
struct Control { const void* const* vtable; std::atomic<std::int32_t> use; std::atomic<std::int32_t> weak; void* object; };
void __fastcall ControlDestroy(void*) noexcept {}   // never reached: the counts start far from 1
void __fastcall ControlDelete(void*) noexcept {}
const void* const controlTable[2]={reinterpret_cast<const void*>(&ControlDestroy),reinterpret_cast<const void*>(&ControlDelete)};
Factory factories[kFactoryCount];
Control controls[kFactoryCount];

void* __fastcall FactoryDtor(Factory* self,unsigned) noexcept { return self; }
const wchar_t* __fastcall FactoryName(Factory* self) noexcept {
    const std::ptrdiff_t at=self-factories;
    return at>=0 && at<kFactoryCount ? kClassNames[at] : L"UfoMother301";
}
void __fastcall FactoryCreate(Factory* self,void* info) noexcept;
const void* const factoryTable[3]={reinterpret_cast<const void*>(&FactoryDtor),reinterpret_cast<const void*>(&FactoryName),
                                   reinterpret_cast<const void*>(&FactoryCreate)};
void InitFactories() noexcept {
    for(int i=0;i<kFactoryCount;++i) {
        factories[i].vtable=factoryTable;
        controls[i].vtable=controlTable;
        controls[i].use=0x40000000;
        controls[i].weak=0x40000000;
        controls[i].object=&factories[i];
    }
}

float Len3(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }

// ---- an objective visibility signal -----------------------------------------------------------------------------------
// The camera comes from the game itself (docs/view-distance-re.md 2.2, nothing of the plugin's HUD needed): the scene system
// (EDF+0x20B2958) holds four camera slots of 0x188 bytes from +0x4D8, each a weak_ptr {camera, control block}; the camera's
// near-pass Umbra wrapper is at camera+0x70: +0x00..+0x3F the cameraToWorld matrix (rows: right, up, forward, position), +0x58..
// the frustum (left, right, top, bottom, near, far, type; camera+0x28 / +0x2C are near / far). Slot 0 is the first live one.
// The object's own bounding sphere (the Umbra wrapper's centre +0x50 and radius +0x64, set for the mothership as for a carrier)
// is tested against the view cone (a conservative test that needs no sign convention of the frustum's sides), and counted every
// frame: a screenshot only shows where the autopilot looked.
constexpr std::size_t kSceneGlobal=0x20B2958,kCameraSlots=0x4D8,kCameraSlotSize=0x188,kCameraWrapper=0x70;
struct GameCam { float eye[3],fwd[3],tanHalf,nearClip,farClip; int slot; };
bool GameCamera(GameCam& cam) noexcept {
    if(!Rd(image+kSceneGlobal,8,"scene system global"))return false;
    const char* scene=*reinterpret_cast<const char* const*>(image+kSceneGlobal);
    if(!scene)return false;
    for(int i=0;i<4;++i) {
        const char* slot=scene+kCameraSlots+static_cast<std::size_t>(i)*kCameraSlotSize;
        if(!Rd(slot,16,"camera slot"))return false;
        const char* obj=*reinterpret_cast<const char* const*>(slot);
        const char* ctrl=*reinterpret_cast<const char* const*>(slot+8);
        if(!obj || !ctrl || !Rd(ctrl,16,"camera control block") || *reinterpret_cast<const std::int32_t*>(ctrl+8)<=0)continue;   // empty or expired
        if(!Rd(obj+kCameraWrapper,0x80,"camera wrapper"))continue;
        float m[16],fr[7];
        std::memcpy(m,obj+kCameraWrapper,sizeof(m));
        std::memcpy(fr,obj+kCameraWrapper+0x58,sizeof(fr));
        bool sane=true;
        for(float v:m)sane=sane && std::isfinite(v);
        for(float v:fr)sane=sane && std::isfinite(v);
        const float lr=Len3(m),lu=Len3(m+4),lf=Len3(m+8);
        sane=sane && lr>0.9f && lr<1.1f && lu>0.9f && lu<1.1f && lf>0.9f && lf<1.1f && fr[4]>0.0f && fr[5]>fr[4];
        if(!sane)continue;
        // Before the first frame the game's camera is the identity (eye 0 0 0, looking down +Z): not a view, skip it.
        if(m[12]==0.0f && m[13]==0.0f && m[14]==0.0f && m[0]==1.0f && m[5]==1.0f && m[10]==1.0f)continue;
        const float hx=std::fmax(std::fabs(fr[0]),std::fabs(fr[1]))/fr[4],hy=std::fmax(std::fabs(fr[2]),std::fabs(fr[3]))/fr[4];
        for(int k=0;k<3;++k) { cam.eye[k]=m[12+k]; cam.fwd[k]=m[8+k]/lf; }
        cam.tanHalf=std::sqrt(hx*hx+hy*hy);
        cam.nearClip=fr[4]; cam.farClip=fr[5]; cam.slot=i;
        static std::atomic<bool> said{false};
        if(!said.exchange(true))
            Log("EDF41MOTHER camera source: the game's own camera slot %d (scene system +0x%zX, wrapper +0x70 cameraToWorld, frustum near %.2f far %.0f, half angle of the view diagonal %.1f deg); eye %.0f %.0f %.0f forward %.2f %.2f %.2f",
                i,kCameraSlots+static_cast<std::size_t>(i)*kCameraSlotSize,fr[4],fr[5],std::atan(cam.tanHalf)*57.29578f,cam.eye[0],cam.eye[1],cam.eye[2],cam.fwd[0],cam.fwd[1],cam.fwd[2]);
        return true;
    }
    return false;
}

enum class View { none, inside, intersects, outside, beyondFar };
View FrustumTest(const float* centre,float radius,float* distance,const char** detail) noexcept {
    GameCam cam{};
    if(!GameCamera(cam))return View::none;
    const float d[3]={centre[0]-cam.eye[0],centre[1]-cam.eye[1],centre[2]-cam.eye[2]};
    const float dist=Len3(d);
    *distance=dist;
    if(dist<=radius)return View::intersects;                       // the camera is inside the sphere
    if(dist-radius>cam.farClip) { if(detail)*detail="beyond the camera far plane"; return View::beyondFar; }
    const float cosA=(d[0]*cam.fwd[0]+d[1]*cam.fwd[1]+d[2]*cam.fwd[2])/dist;
    const float angle=std::acos(std::fmax(-1.0f,std::fmin(1.0f,cosA)));
    const float angular=std::asin(std::fmin(1.0f,radius/dist));
    const float half=std::atan(cam.tanHalf);
    static char text[64];
    std::snprintf(text,sizeof(text),"%.0f deg off the view axis, sphere spans %.0f deg, view half %.0f deg",angle*57.29578f,angular*57.29578f,half*57.29578f);
    if(detail)*detail=text;
    if(angle-angular>half)return View::outside;
    return angle+angular<=half ? View::inside : View::intersects;
}
const char* ViewName(View v) noexcept {
    switch(v) { case View::inside: return "inside"; case View::intersects: return "intersects"; case View::outside: return "outside"; case View::beyondFar: return "beyond far"; default: return "unknown (no camera)"; }
}

struct ViewCount { std::uint32_t frames=0,inside=0,intersects=0,outside=0,near1000=0,noCamera=0; };
void CountView(ViewCount& c,const float* centre,float radius) noexcept {
    float dist=0.0f;
    const View v=FrustumTest(centre,radius,&dist,nullptr);
    ++c.frames;
    if(v==View::inside)++c.inside; else if(v==View::intersects)++c.intersects; else if(v==View::outside || v==View::beyondFar)++c.outside; else ++c.noCamera;
    if(v!=View::none && dist<1000.0f)++c.near1000;
}

// The object's sphere (wrapper +0x50 centre, +0x64 radius), for the probe lines.
bool SphereOf(const void* obj,float* centre,float* radius) noexcept {
    const char* w=static_cast<const char*>(obj)+0x650;
    if(!Rd(w,0x70,"umbra wrapper sphere"))return false;
    std::memcpy(centre,w+0x50,12);
    std::memcpy(radius,w+0x64,4);
    return std::isfinite(centre[0]+centre[1]+centre[2]+*radius);
}

// ---- the parts: registry --------------------------------------------------------------------------------------------
// A part's place is not stored in the object (its class has no field for it): this table is keyed by the part object. `local` is the
// part's matrix relative to the hull (rotation from the euler degrees, the position in the last row, row-vector convention like the
// game's own matrices); the part's world matrix is local * the hull's matrix at +0x60.
struct PartRec { const void* hull; float local[16]; int group; int index; bool enabled, attack, boost; };
std::mutex partLock;
std::unordered_map<const void*,PartRec> partOf;                        // part object -> where it hangs
std::unordered_map<const void*,std::vector<void*>> partsOfHull;        // hull object -> its parts, in creation order
std::unordered_set<const void*> liveHulls;
enum PartGroup { kGroupHatch=0, kGroupCover=1, kGroupCannon=2, kGroupBigCannon=3, kGroupPanel=4 };
const char* const kGroupNames[5]={"HatchLow","HatchMiddle","Cannon","BigCannon","Panel"};

void MultiplyRows(const float* a,const float* b,float* out) noexcept {   // out = a * b, 4x4 row-major, row vectors
    float r[16];
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            r[i*4+j]=a[i*4+0]*b[0*4+j]+a[i*4+1]*b[1*4+j]+a[i*4+2]*b[2*4+j]+a[i*4+3]*b[3*4+j];
    std::memcpy(out,r,sizeof(r));
}

// The part's local matrix: Rx(a) * Ry(b) * Rz(c) (euler degrees, 4.1's order X, Y, Z), the position in the last row.
void LocalMatrix(const float* euler,const float* position,float* out) noexcept {
    const float d2r=0.017453292f;
    const float cx=std::cos(euler[0]*d2r),sx=std::sin(euler[0]*d2r),cy=std::cos(euler[1]*d2r),sy=std::sin(euler[1]*d2r),
                cz=std::cos(euler[2]*d2r),sz=std::sin(euler[2]*d2r);
    const float rx[16]={1,0,0,0, 0,cx,sx,0, 0,-sx,cx,0, 0,0,0,1};
    const float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
    const float rz[16]={cz,sz,0,0, -sz,cz,0,0, 0,0,1,0, 0,0,0,1};
    float m[16];
    MultiplyRows(rx,ry,m);
    MultiplyRows(m,rz,m);
    m[12]=position[0]; m[13]=position[1]; m[14]=position[2]; m[15]=1.0f;
    std::memcpy(out,m,sizeof(m));
}

// ---- the generators ----------------------------------------------------------------------------------------------
// The script's SetEnemyGenerator / SetInstantEnemyGenerator (natives 2100 / 2101) send the mothership message 0x20100 with a block:
// +0x08 int kind (2101: 1), +0x10 int count, +0x14 int interval in frames (seconds * 60), +0x18 float, +0x1C byte flag,
// +0x20 std::wstring (the enemy's SGO path), +0x40 int 1, +0x48 int the hatch index (EDF.dll 0x211E90 / 0x212017; UfoMother511 reads
// [data+0x48] and forwards the message to that ring unit; UfoCarrier508 hands it to its one generator component, 0x6E1630, which
// reads +0x40). The clone of the carrier has that one component; the mothership has four hatches, so the job is taken here: for the
// hatch it names, the hatch opens, one enemy is made at the hatch every `interval` frames, the hatch closes after the last.
struct GenJob { int hatch; wchar_t path[260]; int remaining; int interval; int countdown; bool opened; int closeIn; bool done; int made; };
std::unordered_map<const void*,std::vector<GenJob>> genJobs;
std::unordered_set<const void*> partsBuilt;      // hulls whose SpawnParts has run
std::atomic<int> generatorLogged{0};

bool QueueGenerator(void* hullObj,const char* d) noexcept {
    if(!(Cfg().edf41MotherParts&1))return false;                     // no hatch objects: the carrier's own generator keeps the message
    if(!Rd(d,0x50,"generator message block"))return false;
    GenJob j{};
    std::int32_t kind,count,frames,index;
    float level;
    std::memcpy(&kind,d+8,4); std::memcpy(&count,d+0x10,4); std::memcpy(&frames,d+0x14,4); std::memcpy(&level,d+0x18,4); std::memcpy(&index,d+0x48,4);
    const std::uint8_t flag=static_cast<std::uint8_t>(d[0x1C]);
    std::uint64_t size=0,capacity=0;
    std::memcpy(&size,d+0x30,8); std::memcpy(&capacity,d+0x38,8);
    const wchar_t* text=capacity<8 ? reinterpret_cast<const wchar_t*>(d+0x20) : *reinterpret_cast<const wchar_t* const*>(d+0x20);
    if(size==0 || size>=260 || !Rd(text,(size+1)*sizeof(wchar_t),"generator enemy path")) { Fail("a generator message without a readable enemy path"); return false; }
    if(count<=0 || count>256 || index<0 || index>=64) { Fail("a generator message with a count or hatch index out of range"); return false; }
    std::wmemcpy(j.path,text,static_cast<std::size_t>(size)); j.path[size]=L'\0';
    j.hatch=index; j.remaining=count; j.interval=frames>0 ? frames : 1; j.countdown=0; j.opened=false; j.closeIn=0; j.done=false; j.made=0;
    if(generatorLogged.fetch_add(1)<24)
        Log("EDF41MOTHER generator job on %p: hatch %d, %d x %ls every %d frames (block: kind %d, level %.2f, flag %u, +0x40 = %d)",hullObj,index,count,j.path,j.interval,kind,level,flag,*reinterpret_cast<const std::int32_t*>(d+0x40));
    std::lock_guard<std::mutex> hold(partLock);
    genJobs[hullObj].push_back(j);
    return true;
}

// The hatch part `index` of the hull (0 when there is none).
void* HatchPartOf(const void* hullObj,int index) noexcept {          // partLock held
    const auto it=partsOfHull.find(hullObj);
    if(it==partsOfHull.end())return nullptr;
    for(void* part:it->second) {
        const auto r=partOf.find(part);
        if(r!=partOf.end() && r->second.group==kGroupHatch && r->second.index==index)return part;
    }
    return nullptr;
}

bool PlayState(void* part,const wchar_t* state) noexcept {
    char* o=static_cast<char*>(part);
    if(!Rd(o+kCasPlayerAt,4,"part CAS player index") || !Rd(o+kCasAt,8,"part CAS players"))return false;
    const int casPlayer=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    return reinterpret_cast<bool(__fastcall*)(void*,int,const wchar_t*,int)>(image+kSetCasState)(o+kCasAt,casPlayer,state,0);
}

// One enemy at the hatch: the hull's orientation, the hatch's position, a step below it, spread on a ring so they do not overlap.
bool SpawnFromHatch(void* hullObj,void* hatchPart,const wchar_t* path,int n) noexcept {
    const char* h=static_cast<const char*>(hullObj);
    const char* p=static_cast<const char*>(hatchPart);
    if(!Rd(h+kMatrixAt,64,"hull matrix (generator)") || !Rd(p+kMatrixAt,64,"hatch matrix (generator)"))return false;
    float m[16];
    std::memcpy(m,h+kMatrixAt,sizeof(m));
    const float* hatchPos=reinterpret_cast<const float*>(p+kMatrixAt+0x30);
    const float angle=static_cast<float>(n)*2.39996f,radius=4.0f+static_cast<float>(n%5)*2.0f;
    m[12]=hatchPos[0]+std::cos(angle)*radius; m[13]=hatchPos[1]-12.0f; m[14]=hatchPos[2]+std::sin(angle)*radius; m[15]=1.0f;
    if(!Readable(image+kManagerGlobal,8) || !*reinterpret_cast<void**>(image+kManagerGlobal))return false;
    void* manager=*reinterpret_cast<void**>(image+kManagerGlobal);
    alignas(16) unsigned char context[0x30]={};
    const std::uint64_t vt=reinterpret_cast<std::uint64_t>(image+kCreateContextVtable);
    std::memcpy(context,&vt,8);
    return reinterpret_cast<CreateFromPathFn>(image+kCreateFromPath)(manager,m,path,context)!=nullptr;
}

void RunGenerators(void* hullObj) noexcept {
    struct Act { void* hatch; bool open,close,spawn; wchar_t path[260]; int n; int index; };
    std::vector<Act> acts;
    {
        std::lock_guard<std::mutex> hold(partLock);
        const auto it=genJobs.find(hullObj);
        if(it==genJobs.end())return;
        const bool built=partsBuilt.count(hullObj)!=0;
        for(GenJob& j:it->second) {
            if(j.done)continue;
            void* hatch=HatchPartOf(hullObj,j.hatch);
            if(!hatch) {
                if(built) { Log("EDF41MOTHER generator job on %p: the hull has no hatch %d, dropped",hullObj,j.hatch); j.done=true; }
                continue;
            }
            Act a{hatch,false,false,false,{},j.made,j.hatch};
            if(!j.opened) { a.open=true; j.opened=true; }
            if(j.remaining>0) {
                if(j.countdown<=0) {
                    a.spawn=true; std::wmemcpy(a.path,j.path,260);
                    --j.remaining; ++j.made; j.countdown=j.interval;
                    if(j.remaining==0)j.closeIn=90;
                } else --j.countdown;
            } else if(j.closeIn>0 && --j.closeIn==0) { a.close=true; j.done=true; }
            if(a.open || a.close || a.spawn)acts.push_back(a);
        }
        auto& v=it->second;
        v.erase(std::remove_if(v.begin(),v.end(),[](const GenJob& j){ return j.done; }),v.end());
    }
    for(const Act& a:acts) {
        if(a.open) { const bool ok=PlayState(a.hatch,L"open"); Log("EDF41MOTHER hatch %d %p: open (CAS state %s)",a.index,a.hatch,ok ? "set" : "REFUSED"); }
        if(a.spawn) {
            const bool ok=SpawnFromHatch(hullObj,a.hatch,a.path,a.n);
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<40)Log("EDF41MOTHER hatch %d: enemy #%d %ls %s",a.index,a.n+1,a.path,ok ? "made" : "NOT made");
        }
        if(a.close) { const bool ok=PlayState(a.hatch,L"close"); Log("EDF41MOTHER hatch %d %p: close (CAS state %s)",a.index,a.hatch,ok ? "set" : "REFUSED"); }
    }
}

// ---- ObjectAction 3..17 ------------------------------------------------------------------------------------------
// 4.1 (docs/edf41-ufomother301.md 1.4): groups of five, BigCannon 3..7, Cannon 8..12, Panel 13..17: +0 enable (flag 0x900 = 1), +1
// disable (0x900 = 0), +2 attack on (0x901 = 1), +3 attack off, +4 boost (0x902 = 1); only parts that are still alive (durability > 0)
// take it. The flags live in the part's record. The visible part: a main cannon opens when enabled and closes when disabled, a
// cannon shows (CAS state "appear") when enabled. The firing the flags allow comes with the bullets.
void ApplyAction(void* hullObj,int action) noexcept {
    if(action<3 || action>17)return;
    const int group=(action-3)/5==0 ? kGroupBigCannon : (action-3)/5==1 ? kGroupCannon : kGroupPanel;
    const int what=(action-3)%5;
    struct Play { void* part; const wchar_t* state; };
    std::vector<Play> play;
    int touched=0;
    {
        std::lock_guard<std::mutex> hold(partLock);
        const auto it=partsOfHull.find(hullObj);
        if(it==partsOfHull.end())return;
        for(void* part:it->second) {
            const auto r=partOf.find(part);
            if(r==partOf.end() || r->second.group!=group)continue;
            const char* o=static_cast<const char*>(part);
            float durability=0.0f;
            if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
            if(!(durability>0.0f))continue;
            PartRec& rec=r->second;
            ++touched;
            switch(what) {
                case 0: rec.enabled=true;  if(group==kGroupBigCannon)play.push_back({part,L"open"}); else if(group==kGroupCannon)play.push_back({part,L"appear"}); break;
                case 1: rec.enabled=false; if(group==kGroupBigCannon)play.push_back({part,L"close"}); break;
                case 2: rec.attack=true; break;
                case 3: rec.attack=false; break;
                default: rec.boost=true; break;
            }
        }
    }
    for(const Play& p:play)PlayState(p.part,p.state);
    Log("EDF41MOTHER action %d on %p: %s %s -> %d parts changed (%zu played a state)",action,hullObj,kGroupNames[group],
        what==0 ? "enable" : what==1 ? "disable" : what==2 ? "attack on" : what==3 ? "attack off" : "boost",touched,play.size());
}

// ---- the hull's death takes its parts with it -----------------------------------------------------------------------
// The first frame the hull reports destroyed, its parts are deleted one after another (every 3 frames, panels first). 4.1 lets them
// fall and explode (docs 1.6, not read to the end); this is the stand-in so no part is left hanging in the air.
std::unordered_map<const void*,std::uint32_t> dyingSince;     // hull -> its tick when the death was noticed
void RunDeath(void* hullObj,std::uint32_t tick,bool destroyed) noexcept {
    std::vector<void*> doomed;
    {
        std::lock_guard<std::mutex> hold(partLock);
        if(destroyed && !dyingSince.count(hullObj)) {
            dyingSince[hullObj]=tick;
            genJobs.erase(hullObj);
            Log("EDF41MOTHER hull %p destroyed at its frame %u: its parts go, one every 3 frames",hullObj,tick);
        }
        const auto d=dyingSince.find(hullObj);
        if(d==dyingSince.end())return;
        const auto it=partsOfHull.find(hullObj);
        if(it==partsOfHull.end())return;
        const std::uint32_t n=(tick-d->second)/3+1;
        auto& v=it->second;
        for(std::size_t k=0;k<n && !v.empty();++k) { doomed.push_back(v.back()); v.pop_back(); }
    }
    for(void* part:doomed) {
        const char* o=static_cast<const char*>(part);
        if(Readable(o+kFlagsAt,1) && !(o[kFlagsAt]&4))reinterpret_cast<DeleteObjectFn>(image+kDeleteObject)(part);
        std::lock_guard<std::mutex> hold(partLock);
        partOf.erase(part);
    }
}

// ---- the parts: reading the hull's SGO and making them ---------------------------------------------------------
// An SGO node is 12 bytes {int type, int a, uint b}: 0 array (a children at node + b), 1 int and 2 float (value at node + 8),
// 3 UTF-16 string (a characters at node + b). Every node is checked with Readable before it is read.
struct SgoNode { std::int32_t type; std::int32_t a; std::uint32_t b; };
const SgoNode* SgoChild(const SgoNode* array,int i) noexcept {
    if(!array || array->type!=0 || i<0 || i>=array->a)return nullptr;
    const SgoNode* c=reinterpret_cast<const SgoNode*>(reinterpret_cast<const char*>(array)+array->b+static_cast<std::size_t>(i)*sizeof(SgoNode));
    return Rd(c,sizeof(SgoNode),"sgo child node") ? c : nullptr;
}
bool SgoFloat(const SgoNode* n,float* out) noexcept {
    if(!n || (n->type!=2 && n->type!=1))return false;
    if(n->type==2) { std::memcpy(out,reinterpret_cast<const char*>(n)+8,4); return std::isfinite(*out); }
    *out=static_cast<float>(*reinterpret_cast<const std::int32_t*>(reinterpret_cast<const char*>(n)+8));
    return true;
}
bool SgoVec3(const SgoNode* n,float* out) noexcept {
    for(int i=0;i<3;++i)
        if(!SgoFloat(SgoChild(n,i),out+i))return false;
    return true;
}
bool SgoText(const SgoNode* n,wchar_t* out,std::size_t capacity) noexcept {
    if(!n || n->type!=3 || n->a<=0 || static_cast<std::size_t>(n->a)+1>capacity)return false;
    const wchar_t* t=reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(n)+n->b);
    if(!Rd(t,static_cast<std::size_t>(n->a)*sizeof(wchar_t),"sgo string"))return false;
    std::wmemcpy(out,t,static_cast<std::size_t>(n->a));
    out[n->a]=L'\0';
    return true;
}

// One part: [path, euler, position]. Creates it where it belongs (the matrix the creation gets becomes the object's +0x60) and
// records where it hangs. Returns false when the entry or the creation failed.
bool MakePart(void* hullObj,const SgoNode* entry,int group,int index) noexcept {
    wchar_t path[260];
    float euler[3],position[3];
    if(!SgoText(SgoChild(entry,0),path,260) || !SgoVec3(SgoChild(entry,1),euler) || !SgoVec3(SgoChild(entry,2),position)) {
        Fail("a part entry of the hull's SGO is not [path, euler, position]");
        return false;
    }
    PartRec rec{hullObj,{},group,index,false,false,false};
    LocalMatrix(euler,position,rec.local);
    float world[16];
    const char* h=static_cast<const char*>(hullObj);
    if(!Rd(h+kMatrixAt,sizeof(world),"hull matrix"))return false;
    float hullM[16];
    std::memcpy(hullM,h+kMatrixAt,sizeof(hullM));
    MultiplyRows(rec.local,hullM,world);
    void* manager=*reinterpret_cast<void**>(image+kManagerGlobal);
    alignas(16) unsigned char context[0x30]={};
    const std::uint64_t vt=reinterpret_cast<std::uint64_t>(image+kCreateContextVtable);
    std::memcpy(context,&vt,8);
    void* part=reinterpret_cast<CreateFromPathFn>(image+kCreateFromPath)(manager,world,path,context);
    if(!part) {
        Log("EDF41MOTHER part %s #%d (%ls): the game made nothing",kGroupNames[group],index,path);
        return false;
    }
    std::lock_guard<std::mutex> hold(partLock);
    partOf[part]=rec;
    partsOfHull[hullObj].push_back(part);
    return true;
}

// Group `key` of the hull's SGO: an array of entries (panels: an array of such arrays).
int MakeGroup(void* hullObj,const wchar_t* key,int group,bool nested) noexcept {
    void* sgoRoot=static_cast<char*>(hullObj)+kSgoAt;
    if(!Rd(sgoRoot,8,"hull SGO root"))return 0;
    const SgoNode* node=static_cast<const SgoNode*>(reinterpret_cast<SgoKeyFn>(image+kSgoKey)(sgoRoot,key));
    if(!node || !Rd(node,sizeof(SgoNode),"hull SGO key node") || node->type!=0) {
        Log("EDF41MOTHER hull SGO has no array %ls: no %s",key,kGroupNames[group]);
        return 0;
    }
    int made=0,index=0;
    for(int i=0;i<node->a;++i) {
        const SgoNode* c=SgoChild(node,i);
        if(!c)break;
        if(!nested) {
            if(MakePart(hullObj,c,group,index++))++made;
            continue;
        }
        for(int j=0;j<c->a;++j) {
            const SgoNode* e=SgoChild(c,j);
            if(!e)break;
            if(MakePart(hullObj,e,group,index++))++made;
        }
    }
    return made;
}

void SpawnParts(void* hullObj) noexcept {
    const int mask=Cfg().edf41MotherParts;
    {
        std::lock_guard<std::mutex> hold(partLock);
        if(!liveHulls.count(hullObj)) { return; }
        if(partsOfHull.count(hullObj))return;     // once
        partsOfHull.try_emplace(hullObj);
    }
    if(!mask) { Log("EDF41MOTHER parts: ini Edf41MotherParts=0, none made"); return; }
    if(!Readable(image+kManagerGlobal,8) || !*reinterpret_cast<void**>(image+kManagerGlobal)) { Fail("the scene manager is gone when the parts are made"); return; }
    const ULONGLONG t0=GetTickCount64();
    int hatches=0,covers=0,cannons=0,big=0,panels=0;
    if(mask&1)hatches=MakeGroup(hullObj,L"mp_HatchLow",kGroupHatch,false);
    if(mask&2)covers=MakeGroup(hullObj,L"mp_HatchMiddle",kGroupCover,false);
    if(mask&4)cannons=MakeGroup(hullObj,L"mp_Cannon",kGroupCannon,false);
    if(mask&8)big=MakeGroup(hullObj,L"mp_BigCannon",kGroupBigCannon,false);
    if(mask&16)panels=MakeGroup(hullObj,L"mp_Panel",kGroupPanel,true);
    Log("EDF41MOTHER parts of %p made in %llu ms (ini Edf41MotherParts=%d): %d hatches, %d covers, %d cannons, %d main cannon, %d panels",
        hullObj,static_cast<unsigned long long>(GetTickCount64()-t0),mask,hatches,covers,cannons,big,panels);
    std::lock_guard<std::mutex> hold(partLock);
    partsBuilt.insert(hullObj);
}

// ---- the wrappers ----------------------------------------------------------------------------------------------
struct Entry {
    std::uint32_t ticks=0;
    float lastDurability=0.0f;
    bool haveDurability=false;
};
std::mutex tableLock;
std::unordered_map<const void*,Entry> table;

bool Fields(const void* self,float& durability,std::uint8_t& destroyed,float* pos) noexcept {
    const char* p=static_cast<const char*>(self);
    if(!Rd(p+kDurabilityAt,4,"object durability field") || !Rd(p+kDestroyedAt,1,"object destroyed field") || !Rd(p+kPosAt,16,"object position field"))
        return false;
    std::memcpy(&durability,p+kDurabilityAt,4);
    destroyed=*reinterpret_cast<const std::uint8_t*>(p+kDestroyedAt);
    std::memcpy(pos,p+kPosAt,12);
    return true;
}

void DumpSlots() noexcept {
    char line[kSlots*12+64];   // slot call counts of the hull
    int n=std::snprintf(line,sizeof(line),"EDF41MOTHER slot calls since the last line:");
    bool any=false;
    for(int i=0;i<kSlots && n>0 && n<static_cast<int>(sizeof(line))-16;++i) {
        const std::uint32_t now=hull.calls[i].load(std::memory_order_relaxed);
        if(now!=hull.seen[i]) {
            n+=std::snprintf(line+n,sizeof(line)-static_cast<std::size_t>(n)," %d:+%u",i,now-hull.seen[i]);
            hull.seen[i]=now;
            any=true;
        }
    }
    if(any)Log("%s",line);
}

std::uint64_t __fastcall DtorWrap(void* self,unsigned flags) {
    Log("EDF41MOTHER destructor %p flags %u",self,flags);
    {
        std::lock_guard<std::mutex> hold(tableLock);
        table.erase(self);
    }
    {
        std::lock_guard<std::mutex> hold(partLock);
        liveHulls.erase(self);
        genJobs.erase(self);
        partsBuilt.erase(self);
        dyingSince.erase(self);
        const auto it=partsOfHull.find(self);
        if(it!=partsOfHull.end()) {
            Log("EDF41MOTHER hull %p gone: its %zu parts delete themselves on their next update",self,it->second.size());
            partsOfHull.erase(it);
        }
    }
    return reinterpret_cast<DtorFn>(hull.orig[kSlotDtor])(self,flags);
}

std::uint64_t __fastcall UpdateWrap(void* self,void* dt) {
    if(Cfg().edf41MotherPlace>0.0f) {
        GameCam cam{};
        char* o=static_cast<char*>(self);
        if(GameCamera(cam) && Rd(o+kPosAt,12,"object position (place)")) {
            const float d=Cfg().edf41MotherPlace;
            const float p[3]={cam.eye[0]+cam.fwd[0]*d,cam.eye[1]+cam.fwd[1]*d,cam.eye[2]+cam.fwd[2]*d};
            std::memcpy(o+kPosAt,p,12);
            static std::atomic<bool> said{false};
            if(!said.exchange(true))Log("EDF41MOTHER place: the mothership is held %.0f m in front of the camera (ini Edf41MotherPlace), first at %.0f %.0f %.0f",d,p[0],p[1],p[2]);
        }
    }
    const std::uint64_t result=reinterpret_cast<UpdateFn>(hull.orig[kSlotUpdate])(self,dt);
    float durability=0.0f,pos[3]={};
    std::uint8_t destroyed=0;
    if(!Fields(self,durability,destroyed,pos))return result;
    std::uint32_t tick=0;
    bool changed=false;
    float before=0.0f;
    {
        std::lock_guard<std::mutex> hold(tableLock);
        Entry& e=table[self];
        tick=++e.ticks;
        if(e.haveDurability && (durability<e.lastDurability-0.5f || durability>e.lastDurability+0.5f)) { changed=true; before=e.lastDurability; }
        e.lastDurability=durability;
        e.haveDurability=true;
    }
    {
        static ViewCount counts;
        float centre[3],radius=0.0f;
        if(SphereOf(self,centre,&radius))CountView(counts,centre,radius);
        if(tick%600==0)Log("EDF41MOTHER VIEW mothership %p frames %u: in the view %u, partly %u, outside %u, no camera %u, within 1000 m %u",
                           self,counts.frames,counts.inside,counts.intersects,counts.outside,counts.noCamera,counts.near1000);
    }
    if(tick==1)Log("EDF41MOTHER first update %p durability %.1f destroyed %u pos %.1f %.1f %.1f",self,durability,destroyed,pos[0],pos[1],pos[2]);
    if(tick==1)SpawnParts(self);
    RunDeath(self,tick,destroyed!=0);
    RunGenerators(self);
    if(changed)Log("EDF41MOTHER durability %p %.1f -> %.1f (destroyed %u)",self,before,durability,destroyed);
    if(tick%kUpdateLogEvery==0) {
        Log("EDF41MOTHER update %p frame %u durability %.1f destroyed %u pos %.1f %.1f %.1f",self,tick,durability,destroyed,pos[0],pos[1],pos[2]);
        DumpSlots();
    }
    return result;
}

// What 4.1's UfoMother301 does for each ObjectAction number (docs/edf41-ufomother301.md 1.4), for the log only.
const char* ActionMeaning(std::int32_t action) noexcept {
    static const char* const text[18]={
        "glow off","glow white","glow slow red",
        "BigCannon enable","BigCannon disable","BigCannon attack on","BigCannon attack off","BigCannon boost",
        "Cannon enable","Cannon disable","Cannon attack on","Cannon attack off","Cannon boost",
        "Panel enable","Panel disable","Panel attack on","Panel attack off","Panel boost"};
    return action>=0 && action<18 ? text[action] : "unknown to 4.1";
}

std::uint64_t __fastcall MessageWrap(void* self,std::uint32_t msg,void* data) {
    // A kind of message is logged the first times it comes; the ones that matter to the port every time.
    static std::mutex lock;
    static std::uint32_t kinds[kMaxMessageKinds];
    static std::uint32_t counts[kMaxMessageKinds];
    static int kindCount=0;
    int seen=0;
    {
        std::lock_guard<std::mutex> hold(lock);
        int at=-1;
        for(int i=0;i<kindCount;++i)
            if(kinds[i]==msg)at=i;
        if(at<0 && kindCount<kMaxMessageKinds) { at=kindCount++; kinds[at]=msg; counts[at]=0; }
        if(at>=0)seen=static_cast<int>(++counts[at]);
    }
    const bool important=msg==kMsgDamage || msg==kMsgAction || msg==kMsgParts || msg==kMsgGenerator;
    float before=0.0f,pos[3]={};
    std::uint8_t destroyed=0;
    const bool have=Fields(self,before,destroyed,pos);
    if(msg==kMsgAction && data && Rd(data,4,"action message data")) {
        const std::int32_t action=*static_cast<const std::int32_t*>(data);
        Log("EDF41MOTHER ObjectAction %p action %d = %s (message 0x%X)",self,action,ActionMeaning(action),msg);
        ApplyAction(self,action);
    }
    else if(msg==kMsgGenerator && data) {
        if(QueueGenerator(self,static_cast<const char*>(data)))return 1;   // handled: the hatches do it, not the carrier's one generator
    }
    else if(msg==kMsgParts && data && Rd(data,8,"parts message data"))
        Log("EDF41MOTHER GetPartsObject %p group %d index %d",self,static_cast<const std::int32_t*>(data)[0],static_cast<const std::int32_t*>(data)[1]);
    else if(seen<=3 || (important && msg==kMsgDamage && seen%20==1))
        Log("EDF41MOTHER message 0x%X #%d to %p data %p",msg,seen,self,data);
    const std::uint64_t result=reinterpret_cast<MessageFn>(hull.orig[kSlotMessage])(self,msg,data);
    if(msg==kMsgDamage && have) {
        float after=before;
        std::uint8_t destroyedAfter=destroyed;
        float p2[3]={};
        if(Fields(self,after,destroyedAfter,p2) && (after!=before || destroyedAfter!=destroyed))
            Log("EDF41MOTHER damage %p durability %.1f -> %.1f destroyed %u -> %u",self,before,after,destroyed,destroyedAfter);
    }
    return result;
}

// The first time an object is made (every plugin hook is in by then): which slots of UfoCarrier508's live table differ from the
// stock ones the clone was built from. Information only: the clone does not follow them (BuildClone).
void AuditLiveTable() noexcept {
    static std::atomic<bool> done{false};
    if(done.exchange(true))return;
    const void* const* vt=reinterpret_cast<const void* const*>(image+kCarrierVtable);
    if(!Rd(vt,sizeof(void*)*kSlots,"UfoCarrier508 vtable at creation"))return;
    int differ=0;
    for(int i=0;i<kSlots;++i)
        if(vt[i]!=image+kStockSlots[i]) {
            ++differ;
            Log("EDF41MOTHER at creation, UfoCarrier508 slot %d is %p (stock %p): not followed by the mothership",i,vt[i],static_cast<void*>(image+kStockSlots[i]));
        }
    Log("EDF41MOTHER at creation, %d of %d UfoCarrier508 slots are patched by someone; the clone uses the stock functions",differ,kSlots);
}

// ---- the creation ----------------------------------------------------------------------------------------------
std::uint64_t __fastcall PartDtorWrap(void* self,unsigned flags) {
    {
        std::lock_guard<std::mutex> hold(partLock);
        const auto it=partOf.find(self);
        if(it!=partOf.end()) {   // off its hull's list too: a part shot off first must not be deleted again by RunDeath
            const auto h=partsOfHull.find(it->second.hull);
            if(h!=partsOfHull.end())h->second.erase(std::remove(h->second.begin(),h->second.end(),self),h->second.end());
            partOf.erase(it);
        }
    }
    return reinterpret_cast<DtorFn>(partClass.orig[kSlotDtor])(self,flags);
}

// A part: where the hull says it hangs, then the class's own update.
std::uint64_t __fastcall PartUpdateWrap(void* self,void* dt) {
    PartRec rec{};
    bool known=false,parentAlive=false;
    {
        std::lock_guard<std::mutex> hold(partLock);
        const auto it=partOf.find(self);
        if(it!=partOf.end()) { rec=it->second; known=true; parentAlive=liveHulls.count(rec.hull)!=0; }
    }
    char* o=static_cast<char*>(self);
    if(known && !parentAlive) {
        // The hull is gone: the part goes with it (the script's DeleteObject does the same to a mothership it created).
        if(Rd(o+kFlagsAt,1,"part flags") && !(o[kFlagsAt]&4)) {
            Log("EDF41MOTHER part %s #%d %p: its hull is gone, deleting it",kGroupNames[rec.group],rec.index,self);
            reinterpret_cast<DeleteObjectFn>(image+kDeleteObject)(self);
        }
        std::lock_guard<std::mutex> hold(partLock);
        partOf.erase(self);
    } else if(known) {
        const char* h=static_cast<const char*>(rec.hull);
        if(Rd(h+kMatrixAt,64,"hull matrix (part)") && Rd(o+kMatrixAt,64,"part matrix")) {
            float hullM[16],world[16];
            std::memcpy(hullM,h+kMatrixAt,sizeof(hullM));
            MultiplyRows(rec.local,hullM,world);
            std::memcpy(o+kMatrixAt,world,sizeof(world));
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<5)Log("EDF41MOTHER part %s #%d %p placed at %.1f %.1f %.1f (hull %.1f %.1f %.1f)",kGroupNames[rec.group],rec.index,self,
                                       world[12],world[13],world[14],hullM[12],hullM[13],hullM[14]);
        }
    }
    return reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
}

std::uint64_t __fastcall PartMessageWrap(void* self,std::uint32_t msg,void* data) {
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12)Log("EDF41MOTHER part message 0x%X to %p data %p",msg,self,data);
    return reinterpret_cast<MessageFn>(partClass.orig[kSlotMessage])(self,msg,data);
}

void CreatePart(Factory* self,void* info) noexcept {
    const int cls=static_cast<int>(self-factories);
    void* obj=reinterpret_cast<NewFn>(image+kNew)(kPartSize);
    if(!obj) { Log("EDF41MOTHER create %ls: out of memory",kClassNames[cls]); return; }
    reinterpret_cast<CtorFn>(image+kPartCtor)(obj,info);
    void** vptr=static_cast<void**>(obj);
    if(!Rd(vptr,sizeof(void*),"new part") || *vptr!=image+kPartVtable) {
        Log("EDF41MOTHER create %ls: object %p has vtable %p, not EventAnimationObject's %p: left as the class made it",kClassNames[cls],obj,Rd(vptr,sizeof(void*),"new part")?*vptr:nullptr,static_cast<void*>(image+kPartVtable));
        return;
    }
    *vptr=&partClass.block[1];
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6)Log("EDF41MOTHER create %ls: part %p built by EventAnimationObject's constructor, vtable -> clone %p",kClassNames[cls],obj,static_cast<void*>(&partClass.block[1]));
}

void __fastcall FactoryCreate(Factory* self,void* info) noexcept {
    if(!info || !Rd(info,0x28,"createInfo")) { Log("EDF41MOTHER create: no create info (%p), nothing made",info); return; }
    if(!installed.load()) { Log("EDF41MOTHER create: the vtable was not built, nothing made"); return; }
    if(self!=&factories[kHullFactory]) { CreatePart(self,info); return; }
    AuditLiveTable();
    void* obj=reinterpret_cast<NewFn>(image+kNew)(kCarrierSize);
    if(!obj) { Log("EDF41MOTHER create: out of memory for %zu bytes",kCarrierSize); return; }
    reinterpret_cast<CtorFn>(image+kCarrierCtor)(obj,info);
    void** vptr=static_cast<void**>(obj);
    if(!Rd(vptr,sizeof(void*),"new object") || *vptr!=image+kCarrierVtable) {
        Log("EDF41MOTHER create: object %p has vtable %p, not UfoCarrier508's %p: left as the class made it",obj,Rd(vptr,sizeof(void*),"new object")?*vptr:nullptr,static_cast<void*>(image+kCarrierVtable));
        return;
    }
    *vptr=&hull.block[1];
    {
        std::lock_guard<std::mutex> hold(partLock);
        liveHulls.insert(obj);
    }
    const int n=++created;
    Log("EDF41MOTHER create #%d: UfoMother301 object %p built by UfoCarrier508's constructor, vtable %p -> clone %p; info %p",n,obj,static_cast<void*>(image+kCarrierVtable),static_cast<void*>(&hull.block[1]),info);
}


// ---- bullet probe (dev, ini Edf41MotherBulletProbe) ----------------------------------------------------------------
// Every bullet the game makes goes through 0x1194280(manager, matrix, bulletFactory, info) (the 507 family at 0x52C9B1, the E510
// fortress, the E511 mothership ...). `info` is a creation context like the one a script spawn hands 0x11945E0 ({vtable, matrix*,
// 0, 0, out*, ...}) with the bullet's settings after it; the bullet base constructor (0x22E9C0) reads +0x30 (a matrix copy), +0x38 /
// +0x40 (the owner's weak pointer) and +0x1A0. This logs the bullet class and the first bytes of `info` the first times each class is
// made, so the layout can be read off a real fortress / UFO fight instead of guessed.
constexpr std::size_t kBulletSpawn=0x1194280;
constexpr std::size_t kBulletSites[]={0x84c5a,0x97785,0x97c3c,0x98252,0x9ab41,0x9ae4e,0x9b2cf,0x9b78a,0x9f29f,0x1018d1,0x247bf7,0x2666a3,
    0x2a4291,0x2a42f2,0x2a4932,0x2a4998,0x2b9f68,0x3525a3,0x3e742a,0x40e073,0x4362af,0x43d4da,0x491342,0x4a0ecd,0x4a11ce,0x4ace88,
    0x4b2c2c,0x4e3805,0x510eb9,0x510f5d,0x511165,0x52c9b1,0x690b3f,0x69799f,0x697fd0,0x6b125d,0x6d025a,0x6d331d};
void* __fastcall BulletSpawnProbe(void* manager,const float* matrix,void* factory,void* info) {
    static std::mutex lock;
    static const void* seenFactory[48];
    static int seenCount[48];
    static int kinds=0;
    int at=-1;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(int i=0;i<kinds;++i)
            if(seenFactory[i]==factory)at=i;
        if(at<0 && kinds<48) { at=kinds++; seenFactory[at]=factory; seenCount[at]=0; }
        if(at>=0)++seenCount[at];
    }
    const int n=at>=0 ? seenCount[at] : 0;
    if(at>=0 && (n<=2 || n%200==0) && factory && Readable(factory,8)) {
        const void* const* vt=*reinterpret_cast<const void* const* const*>(factory);
        const wchar_t* name=L"?";
        if(vt && Readable(vt,24) && Readable(vt[1],8))name=reinterpret_cast<const wchar_t*(__fastcall*)(void*)>(vt[1])(factory);
        const bool sane=Readable(name,8);
        Log("EDF41MOTHER bullet #%d of %ls (factory %p): matrix pos %.1f %.1f %.1f, info %p",n,sane ? name : L"?",factory,
            matrix && Readable(matrix,64) ? matrix[12] : 0.0f,matrix && Readable(matrix,64) ? matrix[13] : 0.0f,matrix && Readable(matrix,64) ? matrix[14] : 0.0f,info);
        if(n<=2 && info && Readable(info,0x1C0)) {
            const unsigned char* b=static_cast<const unsigned char*>(info);
            for(std::size_t o=0;o<0x1C0;o+=32) {
                std::uint64_t q[4];
                std::memcpy(q,b+o,32);
                Log("EDF41MOTHER bullet info +0x%03zX: %016llX %016llX %016llX %016llX",o,static_cast<unsigned long long>(q[0]),static_cast<unsigned long long>(q[1]),
                    static_cast<unsigned long long>(q[2]),static_cast<unsigned long long>(q[3]));
            }
        }
    }
    return reinterpret_cast<BulletSpawnFn>(image+kBulletSpawn)(manager,matrix,factory,info);
}

// ---- the registration -----------------------------------------------------------------------------------------
bool RegisterFactoryNow() noexcept {
    if(registered.exchange(true))return true;
    void* manager=nullptr;
    if(!Rd(image+kManagerGlobal,sizeof(void*),"manager global") || (manager=*reinterpret_cast<void**>(image+kManagerGlobal))==nullptr) {
        Log("EDF41MOTHER register: the scene object manager does not exist yet; UfoMother301 is not registered");
        registered=false;
        return false;
    }
    if(!Rd(manager,0x300,"manager object")) { registered=false; return false; }
    bool all=true;
    for(int i=0;i<kFactoryCount;++i) {
        struct Shared { void* object; void* control; } shared{&factories[i],&controls[i]};
        reinterpret_cast<RegisterFn>(image+kRegisterFactory)(manager,&shared);
        void* found=reinterpret_cast<GetFactoryFn>(image+kGetFactory)(manager,kClassNames[i]);
        Log("EDF41MOTHER registered %ls with manager %p: lookup gives %p (ours %p) -> %s",kClassNames[i],manager,found,static_cast<void*>(&factories[i]),found==&factories[i] ? "OK" : "FAILED");
        all=all && found==&factories[i];
    }
    return all;
}

std::uint64_t __fastcall RegisterAllHook(void* a,void* b,void* c,void* d) {
    const std::uint64_t result=reinterpret_cast<RegisterAllFn>(image+kRegisterAll)(a,b,c,d);
    RegisterFactoryNow();
    return result;
}

// Counting stubs for the slots we do not wrap: mov rax,&count / lock inc [rax] / mov rax,orig / jmp rax.
// The clone is the STOCK class, built from the table of its slots as the file has them (c.rvas), not from the live vtable: this
// plugin patches slot 7 of UfoCarrier508's own table (carrierlaser.cpp ShipAiHook, the ship AI for the teleportation laser: it
// recognises ships by the old vptr and steps the laser's state machine once a frame, so on a mothership it would only add a second
// step per frame), and a clone taken before or after that patch would differ. The live table is compared slot by slot; what differs
// is logged and left out of the clone.
bool BuildTable(StockClass& c,void* dtor,void* update,void* message) noexcept {
    const void* const* vt=reinterpret_cast<const void* const*>(image+c.vtable);
    if(!Readable(vt-1,sizeof(void*)*static_cast<std::size_t>(c.slots+1))) { Log("EDF41MOTHER the %s vtable is not readable: prototype off",c.tag); return false; }
    c.block[0]=const_cast<void*>(vt[-1]);
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&BuildTable),&self);
    for(int i=0;i<c.slots;++i) {
        void* const stock=image+c.rvas[i];
        void* const live=const_cast<void*>(vt[i]);
        if(!Readable(stock,8)) { Log("EDF41MOTHER stock slot %d of %s (EDF.dll+0x%X) is not readable: prototype off",i,c.tag,c.rvas[i]); return false; }
        if(live!=stock) {
            HMODULE owner=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(live),&owner);
            Log("EDF41MOTHER vtable slot %d of %s is patched (%p, %s): the clone keeps the stock function EDF.dll+0x%X",
                i,c.tag,live,owner && owner==self ? "this plugin's hook" : "another module",c.rvas[i]);
        }
        c.orig[i]=stock;
    }
    constexpr std::size_t kStub=32;
    unsigned char code[64*kStub];
    std::memset(code,0xCC,sizeof(code));
    for(int i=0;i<c.slots;++i) {
        unsigned char* s=code+static_cast<std::size_t>(i)*kStub;
        const std::uint64_t counter=reinterpret_cast<std::uint64_t>(&c.calls[i]),target=reinterpret_cast<std::uint64_t>(c.orig[i]);
        s[0]=0x48; s[1]=0xB8; std::memcpy(s+2,&counter,8);     // mov rax, counter
        s[10]=0xF0; s[11]=0xFF; s[12]=0x00;                     // lock inc dword ptr [rax]
        s[13]=0x48; s[14]=0xB8; std::memcpy(s+15,&target,8);   // mov rax, original
        s[23]=0xFF; s[24]=0xE0;                                 // jmp rax
    }
    unsigned char* stubs=static_cast<unsigned char*>(AllocateNearCode(image,code,static_cast<std::size_t>(c.slots)*kStub));
    if(!stubs) { Log("EDF41MOTHER no executable page for the %s counting stubs: prototype off",c.tag); return false; }
    for(int i=0;i<c.slots;++i)
        c.block[1+i]=stubs+static_cast<std::size_t>(i)*kStub;
    c.block[1+kSlotDtor]=dtor;
    c.block[1+kSlotUpdate]=update;
    c.block[1+kSlotMessage]=message;
    return true;
}

bool BuildClone() noexcept {
    for(const Print& p:kPrints) {
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41MOTHER the code at EDF.dll+0x%zX is not what this was written for: prototype off",p.rva);
            return false;
        }
    }
    return BuildTable(hull,reinterpret_cast<void*>(&DtorWrap),reinterpret_cast<void*>(&UpdateWrap),reinterpret_cast<void*>(&MessageWrap))
        && BuildTable(partClass,reinterpret_cast<void*>(&PartDtorWrap),reinterpret_cast<void*>(&PartUpdateWrap),reinterpret_cast<void*>(&PartMessageWrap));
}
}  // namespace

bool InstallEdf41Mother() noexcept {
    if(!Cfg().edf41MotherDev) {
        Log("EDF41MOTHER off (ini Edf41MotherDev=0)");
        return false;
    }
    InitFactories();
    if(!BuildClone())return false;
    installed=true;
    bool changed=false;
    const bool ok=RedirectCall(image+kRegisterAllSite,image+kRegisterAll,reinterpret_cast<void*>(&RegisterAllHook),changed);
    if(ok) {
    }
    if(ok && Cfg().edf41MotherBulletProbe) {
        int hooked=0;
        for(const std::size_t site:kBulletSites) {
            bool c=false;
            if(Readable(image+site,5) && static_cast<unsigned char>(image[site])==0xE8 && RedirectCall(image+site,image+kBulletSpawn,reinterpret_cast<void*>(&BulletSpawnProbe),c))++hooked;
        }
        Log("EDF41MOTHER bullet probe: %d of %zu bullet spawn call sites hooked",hooked,sizeof(kBulletSites)/sizeof(kBulletSites[0]));
    }
    // When this runs the game's start-up routine has not reached 0x701859 yet (the manager is null); if it has, the hook
    // can no longer fire and the log says so instead of the registration line.
    const bool late=Readable(image+kManagerGlobal,sizeof(void*)) && *reinterpret_cast<void**>(image+kManagerGlobal)!=nullptr;
    Log("EDF41MOTHER development switch ON: vtable clone built (%d slots, wrapped %d/%d/%d), register hook %s%s",kSlots,kSlotDtor,kSlotUpdate,kSlotMessage,ok ? "installed" : "FAILED",
        late ? " (WARNING: the scene object manager already exists, the game's registration ran before this plugin: UfoMother301 will not be registered)" : "");
    return ok;
}
}  // namespace crew
